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

    #if defined(_WIN32_WINNT) && _WIN32_WINNT < _WIN32_WINNT_WIN7
        #undef _WIN32_WINNT
    #endif
    #if !defined(_WIN32_WINNT)
        // Force to include needed API prototypes
        #define _WIN32_WINNT _WIN32_WINNT_WIN7  // or _WIN32_WINNT_WIN10
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX  // Disable macros min() and max()
    #endif
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #if defined(small)
        #undef small
    #endif

// The needed Windows API for processor groups could be missed from old Windows
// versions, so instead of calling them directly (forcing the linker to resolve
// the calls at compile time), try to load them at runtime. To do this we need
// first to define the corresponding function pointers.
extern "C" {
using OpenProcessToken_      = bool (*)(HANDLE, DWORD, PHANDLE);
using LookupPrivilegeValueA_ = bool (*)(LPCSTR, LPCSTR, PLUID);
using AdjustTokenPrivileges_ =
  bool (*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
}
#endif

namespace DON {

// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc().
// Memory allocated with alloc_aligned_std() must be freed with free_aligned_std().

void* alloc_aligned_std(std::size_t allocSize, std::size_t alignment) noexcept {

    // POSIX requires power-of-two and >= sizeof(void*). Windows tolerates more,
    // but normalizing helps keep behavior consistent.
    if (alignment < sizeof(void*))
        alignment = sizeof(void*);

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

void* alloc_aligned_lp_windows([[maybe_unused]] std::size_t allocSize) noexcept {

    #if !defined(_WIN64)
    return nullptr;
    #else
    std::size_t lpSize = GetLargePageMinimum();
    if (lpSize == 0)
        return nullptr;

    // Dynamically link OpenProcessToken, LookupPrivilegeValue and AdjustTokenPrivileges

    HMODULE advapi32Module = GetModuleHandle(TEXT("advapi32.dll"));

    bool freeNeeded = false;

    if (!advapi32Module)
    {
        advapi32Module = LoadLibrary(TEXT("advapi32.dll"));
        freeNeeded     = true;
    }

    auto openProcessToken =
      OpenProcessToken_((void (*)()) GetProcAddress(advapi32Module, "OpenProcessToken"));
    if (openProcessToken == nullptr)
        return nullptr;
    auto lookupPrivilegeValueA =
      LookupPrivilegeValueA_((void (*)()) GetProcAddress(advapi32Module, "LookupPrivilegeValueA"));
    if (lookupPrivilegeValueA == nullptr)
        return nullptr;
    auto adjustTokenPrivileges =
      AdjustTokenPrivileges_((void (*)()) GetProcAddress(advapi32Module, "AdjustTokenPrivileges"));
    if (adjustTokenPrivileges == nullptr)
        return nullptr;

    HANDLE processHandle{};
    // Need SeLockMemoryPrivilege, so try to enable it for the process
    if (!openProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &processHandle))
        return nullptr;

    void* mem = nullptr;

    LUID luid{};
    if (lookupPrivilegeValueA(nullptr, "SeLockMemoryPrivilege", &luid))
    {
        TOKEN_PRIVILEGES curTp{};
        curTp.PrivilegeCount           = 1;
        curTp.Privileges[0].Luid       = luid;
        curTp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        TOKEN_PRIVILEGES preTp{};
        DWORD            preTpLen{};
        // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
        // Still need to query GetLastError() to ensure that the privileges were actually obtained.
        if (adjustTokenPrivileges(processHandle, FALSE, &curTp, sizeof(TOKEN_PRIVILEGES), &preTp,
                                  &preTpLen)
            && GetLastError() == ERROR_SUCCESS)
        {
            // Round up size to full pages and allocate
            allocSize = (allocSize + lpSize - 1) & ~std::size_t(lpSize - 1);

            mem = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                               PAGE_READWRITE);

            // Privilege no longer needed, restore previous state
            adjustTokenPrivileges(processHandle, FALSE, &preTp, 0, nullptr, nullptr);
        }
    }

    CloseHandle(processHandle);

    if (freeNeeded)
        FreeLibrary(advapi32Module);

    //if (mem == nullptr)
    //    std::cerr << "Failed to allocate large page memory.\n"
    //              << "Error code: 0x" << std::hex << GetLastError() << std::dec << std::endl;

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
    // Round up to multiples of Alignment
    std::size_t roundAllocSize = ((allocSize + Alignment - 1) / Alignment) * Alignment;

    void* mem = alloc_aligned_std(roundAllocSize, Alignment);
    #if defined(MADV_HUGEPAGE)
    if (mem != nullptr)
        madvise(mem, roundAllocSize, MADV_HUGEPAGE);
    #endif
    return mem;
#endif
}

// Free Aligned Large Pages.
// The effect is a nop if mem == nullptr
void free_aligned_lp(void* mem) noexcept {

#if defined(_WIN32)
    if (mem != nullptr && !VirtualFree(mem, 0, MEM_RELEASE))
    {
        std::cerr << "Failed to free large page memory.\n"
                  << "Error code: 0x" << std::hex << GetLastError() << std::dec << std::endl;
        std::exit(EXIT_FAILURE);
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
