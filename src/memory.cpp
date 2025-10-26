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

#include <cstdlib>

#if defined(__has_include)
    #if __has_include(<features.h>)
        #include <features.h>
    #endif
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

// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc().
// Memory allocated with alloc_aligned_std() must be freed with free_aligned_std().

void* alloc_aligned_std(std::size_t allocSize, std::size_t alignment) noexcept {

    // POSIX requires power-of-two and >= alignof(void*).
    // Windows tolerates more, but normalizing helps keep behavior consistent.
    alignment = std::max(alignment, alignof(void*));

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
    using OpenProcessToken_      = BOOL(WINAPI*)(HANDLE, DWORD, PHANDLE);
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lookupprivilegevaluew
    using LookupPrivilegeValueW_ = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, PLUID);
    // https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges
    using AdjustTokenPrivileges_ = BOOL(WINAPI*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
    // clang-format on

    static constexpr LPCWSTR ModuleName = TEXT("advapi32.dll");

    constexpr Advapi() noexcept = default;
    ~Advapi() noexcept { free(); }

    // The needed Windows API for processor groups could be missed from old Windows versions,
    // so instead of calling them directly (forcing the linker to resolve the calls at compile time),
    // try to load them at runtime.
    bool load() noexcept {

        hModule = GetModuleHandle(ModuleName);
        if (hModule == nullptr)
        {
            hModule = LoadLibraryEx(ModuleName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (hModule == nullptr)
                hModule = LoadLibrary(ModuleName);  // optional last resort
            if (hModule == nullptr)
                return false;
            loaded = true;
        }

        openProcessToken =
          OpenProcessToken_((void (*)()) GetProcAddress(hModule, "OpenProcessToken"));
        if (openProcessToken == nullptr)
            return false;
        lookupPrivilegeValueW =
          LookupPrivilegeValueW_((void (*)()) GetProcAddress(hModule, "LookupPrivilegeValueW"));
        if (lookupPrivilegeValueW == nullptr)
            return false;
        adjustTokenPrivileges =
          AdjustTokenPrivileges_((void (*)()) GetProcAddress(hModule, "AdjustTokenPrivileges"));
        if (adjustTokenPrivileges == nullptr)
            return false;

        return true;
    }

    void free() noexcept {
        if (loaded && hModule != nullptr)
            FreeLibrary(hModule);
        hModule = nullptr;
        loaded  = false;
    }

    HMODULE hModule = nullptr;
    bool    loaded  = false;

    OpenProcessToken_      openProcessToken      = nullptr;
    LookupPrivilegeValueW_ lookupPrivilegeValueW = nullptr;
    AdjustTokenPrivileges_ adjustTokenPrivileges = nullptr;
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

    // Round up size to full pages
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
            && GetLastError() == ERROR_SUCCESS)
        {
            // Allocate large page memory
            mem = VirtualAlloc(nullptr, allocSize, MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);

            if (mem == nullptr)
                err = GetLastError();

            // Privilege no longer needed, restore the privileges
            if (  //oldTp.PrivilegeCount > 0 &&
              !advapi.adjustTokenPrivileges(tokenHandle, FALSE, &oldTp, 0, nullptr, nullptr))
                if (err == ERROR_SUCCESS)
                    err = GetLastError();
        }
        else
            err = GetLastError();
    }

    if (tokenHandle != nullptr && !CloseHandle(tokenHandle))
        err = GetLastError();

    advapi.free();

    if (mem == nullptr)
    {
        std::cerr << "Failed to allocate " << allocSize << "B for large page memory.\n"
                  << "Error code: 0x" << std::hex << err << std::dec << std::endl;
    }

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
    {
        constexpr std::size_t Alignment = 4 * 1024;

        allocSize = round_up_pow2(allocSize, Alignment);

        mem = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    return mem;
#else
    constexpr std::size_t Alignment =
    #if defined(__linux__)
      2 * 1024 * 1024  // Assume 2MB page-size
    #else
      4 * 1024  // Assume small page-size
    #endif
      ;
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
bool free_aligned_lp(void* mem) noexcept {

#if defined(_WIN32)
    if (mem != nullptr && !VirtualFree(mem, 0, MEM_RELEASE))
    {
        DWORD err = GetLastError();
        std::cerr << "Failed to free memory.\n"
                  << "Error code: 0x " << std::hex << err << std::dec << std::endl;
        return false;
    }
#else
    free_aligned_std(mem);
#endif
    return true;
}

// Check Large Pages support
bool has_lp() noexcept {

#if defined(_WIN32)
    void* mem = alloc_aligned_lp_windows(2 * 1024 * 1024);  // 2MB page-size assumed
    if (mem == nullptr)
        return false;
    [[maybe_unused]] bool success = free_aligned_lp(mem);
    assert(success);
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
