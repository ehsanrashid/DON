/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "memory.h"

#include <cassert>
#include <cstdlib>

#if __has_include("features.h")
    #include <features.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    #include <sys/mman.h>
#endif

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__OpenBSD__) \
  || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32)) \
  || defined(__e2k__)
    #define POSIX_ALIGNED
#endif

#if defined(_WIN32)
    #include <iostream>

    #if !defined(NOMINMAX)
        #define NOMINMAX  // Disable min()/max() macros
    #endif
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <sdkddkver.h>
    #if defined(_WIN32_WINNT) && _WIN32_WINNT < _WIN32_WINNT_WIN7
        #undef _WIN32_WINNT
    #endif
    #if !defined(_WIN32_WINNT)
        // Force to include needed API prototypes
        #define _WIN32_WINNT _WIN32_WINNT_WIN7  // or _WIN32_WINNT_WIN10
    #endif
    #define UNICODE
    #include <windows.h>
    #if defined(small)
        #undef small
    #endif
#endif

namespace DON {

namespace {

// Round up to multiples of alignment
[[nodiscard]] constexpr std::size_t round_up_pow2(std::size_t size,
                                                  std::size_t alignment) noexcept {
    assert(alignment && ((alignment & (alignment - 1)) == 0));
    std::size_t mask = alignment - 1;
    return (size + mask) & ~mask;
}
}  // namespace

// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc().
// Memory allocated with alloc_aligned_std() must be freed with free_aligned_std().

void* alloc_aligned_std(std::size_t allocSize, std::size_t alignment) noexcept {

    // POSIX requires power-of-two and >= sizeof(void*). Windows tolerates more,
    // but normalizing helps keep behavior consistent.
    alignment = std::max(alignment, sizeof(void*));

#if defined(_ISOC11_SOURCE)
    return std::aligned_alloc(alignment, allocSize);
#elif defined(POSIX_ALIGNED)
    void* mem = nullptr;
    ::posix_memalign(&mem, alignment, allocSize);
    return mem;
#elif defined(_WIN32)
    #if !defined(_M_ARM) && !defined(_M_ARM64)
    return _mm_malloc(allocSize, alignment);
    #else
    return _aligned_malloc(allocSize, alignment);
    #endif
#else
    return std::aligned_alloc(alignment, allocSize);
#endif
}

void free_aligned_std(void* mem) noexcept {

#if defined(POSIX_ALIGNED)
    std::free(mem);
#elif defined(_WIN32)
    #if !defined(_M_ARM) && !defined(_M_ARM64)
    _mm_free(mem);
    #else
    _aligned_free(mem);
    #endif
#else
    std::free(mem);
#endif
}

// Return suitably aligned memory, if possible using large pages.
#if defined(_WIN32)

namespace {

struct Advapi final {
    // clang-format off
    // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocesstoken
    using OpenProcessToken      = BOOL(WINAPI*)(HANDLE, DWORD, PHANDLE);
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lookupprivilegevaluew
    using LookupPrivilegeValueW = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, PLUID);
    // https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges
    using AdjustTokenPrivileges = BOOL(WINAPI*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
    // clang-format on

    static constexpr LPCWSTR Name = TEXT("advapi32.dll");

    // The needed Windows API for processor groups could be missed from old Windows versions,
    // so instead of calling them directly (forcing the linker to resolve the calls at compile time),
    // try to load them at runtime.
    bool load() noexcept {

        module = GetModuleHandle(Name);
        if (module == nullptr)
            module = LoadLibraryEx(Name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr)
            module = LoadLibrary(Name);
        if (module == nullptr)
            return false;

        openProcessToken =
          OpenProcessToken((void (*)()) GetProcAddress(module, "OpenProcessToken"));
        if (openProcessToken == nullptr)
            return false;
        lookupPrivilegeValueW =
          LookupPrivilegeValueW((void (*)()) GetProcAddress(module, "LookupPrivilegeValueW"));
        if (lookupPrivilegeValueW == nullptr)
            return false;
        adjustTokenPrivileges =
          AdjustTokenPrivileges((void (*)()) GetProcAddress(module, "AdjustTokenPrivileges"));
        if (adjustTokenPrivileges == nullptr)
            return false;

        return true;
    }

    void unload() noexcept {
        if (module == nullptr)
            return;
        if (GetModuleHandle(Name) == nullptr)
            FreeLibrary(module);
        module = nullptr;
    }

    HMODULE module = nullptr;

    OpenProcessToken      openProcessToken      = nullptr;
    LookupPrivilegeValueW lookupPrivilegeValueW = nullptr;
    AdjustTokenPrivileges adjustTokenPrivileges = nullptr;
};

void* alloc_aligned_lp_windows([[maybe_unused]] std::size_t allocSize) noexcept {

    #if !defined(_WIN64)
    return nullptr;
    #else

    void* mem = nullptr;

    const SIZE_T largePageSize = GetLargePageMinimum();
    if (largePageSize == 0)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return mem;
    }

    Advapi advapi;
    if (!advapi.load())
        return mem;

    HANDLE tokenHandle = nullptr;
    // Need SeLockMemoryPrivilege, so try to enable it for the process
    if (!advapi.openProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                 &tokenHandle))
        return mem;

    // Round up size to full pages and allocate
    allocSize = round_up_pow2(allocSize, largePageSize);

    DWORD err = ERROR_SUCCESS;

    if (LUID luid{}; advapi.lookupPrivilegeValueW(nullptr, SE_LOCK_MEMORY_NAME, &luid))
    {
        TOKEN_PRIVILEGES oldTp{};
        DWORD            oldTpLen = 0;

        TOKEN_PRIVILEGES newTp{};
        newTp.PrivilegeCount           = 1;
        newTp.Privileges[0].Luid       = luid;
        newTp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
        // Still need to query GetLastError() to ensure that the privileges were actually obtained.
        SetLastError(ERROR_SUCCESS);
        if (advapi.adjustTokenPrivileges(tokenHandle, FALSE, &newTp, sizeof(oldTp), &oldTp,
                                         &oldTpLen)
            && (err = GetLastError()) == ERROR_SUCCESS)
        {
            mem = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                               PAGE_READWRITE);

            if (mem == nullptr)
                err = GetLastError();

            // Privilege no longer needed, restore the privileges
            if (!advapi.adjustTokenPrivileges(tokenHandle, FALSE, &oldTp, 0, nullptr, nullptr))
                if (err == ERROR_SUCCESS)
                    err = GetLastError();
        }
        else
            err = GetLastError();
    }

    CloseHandle(tokenHandle);

    advapi.unload();

    if (mem == nullptr)
        std::cerr << "Failed to allocate " << allocSize << "B for large page memory.\n"
                  << "Error code: 0x" << std::hex << err << std::dec << std::endl;

    return mem;
    #endif
}

}  // namespace

#endif

// Alloc Aligned Large Pages
void* alloc_aligned_lp(std::size_t allocSize) noexcept {

#if defined(_WIN32)
    // Try to allocate large pages
    void* mem = alloc_aligned_lp_windows(allocSize);
    // Fall back to regular, page-aligned, allocation if necessary
    if (mem == nullptr)
        mem = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return mem;
#else
    constexpr std::size_t Alignment =
    #if defined(__linux__)
      2 * 1024 * 1024;  // Assume 2MB page-size
    #else
      4 * 1024;  // Assume small page-size
    #endif

    allocSize = round_up_pow2(allocSize, Alignment);

    void* mem = alloc_aligned_std(allocSize, Alignment);
    #if defined(MADV_HUGEPAGE)
    if (mem != nullptr)
        madvise(mem, allocSize, MADV_HUGEPAGE);
    #endif
    return mem;
#endif
}

// Free Aligned Large Pages.
// The effect is a nop if mem == nullptr
void free_aligned_lp(void* mem) noexcept {

#if defined(_WIN32)
    if (mem != nullptr)
    {
        if (!VirtualFree(mem, 0, MEM_RELEASE))
            std::cerr << "Failed to free large page memory.\n"
                      << "Error code: 0x" << std::hex << GetLastError() << std::dec << std::endl;
        else
            mem = nullptr;
    }
#else
    free_aligned_std(mem);
#endif
}

// Check Large Pages support
bool has_lp() noexcept {

#if defined(_WIN32)
    void* mem = alloc_aligned_lp_windows(2 * 1024 * 1024);  // 2MB page-size assumed
    if (mem == nullptr)
        return false;
    free_aligned_lp(mem);
    return true;
#elif defined(__linux__)
    #if defined(MADV_HUGEPAGE)
    return true;
    #else
    return false;
    #endif
#else
    return false;
#endif
}

}  // namespace DON
