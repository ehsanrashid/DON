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
    #include <stdlib.h>
#endif

#ifdef _WIN32
    #if _WIN32_WINNT < 0x0601
        #undef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601  // Force to include needed API prototypes
    #endif

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <ios>       // std::hex, std::dec
    #include <iostream>  // std::cerr
    #include <ostream>   // std::endl
    #include <windows.h>
// The needed Windows API for processor groups could be missed from old Windows
// versions, so instead of calling them directly (forcing the linker to resolve
// the calls at compile time), try to load them at runtime. To do this we need
// first to define the corresponding function pointers.
extern "C" {
// clang-format off
using OpenProcessToken_      = bool (*)(HANDLE, DWORD, PHANDLE);
using LookupPrivilegeValueA_ = bool (*)(LPCSTR, LPCSTR, PLUID);
using AdjustTokenPrivileges_ = bool (*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
// clang-format on
}
#endif

namespace DON {

// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc().
// Memory allocated with alloc_aligned_std() must be freed with free_aligned_std().
void* alloc_aligned_std(std::size_t alignment, std::size_t allocSize) noexcept {
    // Apple requires 10.15, which is enforced in the makefile
#if defined(_ISOC11_SOURCE) || defined(__APPLE__)
    return aligned_alloc(alignment, allocSize);
#elif defined(POSIX_ALIGNED)
    void* mem;
    return posix_memalign(&mem, alignment, allocSize) ? nullptr : mem;
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
    return _mm_malloc(allocSize, alignment);
#elif defined(_WIN32)
    return _aligned_malloc(allocSize, alignment);
#else
    return std::aligned_alloc(alignment, allocSize);
#endif
}

void free_aligned_std(void* mem) noexcept {

#if defined(POSIX_ALIGNED)
    free(mem);
#elif defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64)
    _mm_free(mem);
#elif defined(_WIN32)
    _aligned_free(mem);
#else
    free(mem);
#endif
}

// Return suitably aligned memory, if possible using large pages.
#if defined(_WIN32)

namespace {

void* alloc_aligned_lp_windows([[maybe_unused]] std::size_t allocSize) noexcept {

    #if !defined(_WIN64)
    return nullptr;
    #else

    const std::size_t lpSize = GetLargePageMinimum();
    if (!lpSize)
        return nullptr;

    // Dynamically link OpenProcessToken, LookupPrivilegeValue and AdjustTokenPrivileges

    HMODULE hAdvapi32 = GetModuleHandle(TEXT("advapi32.dll"));

    if (!hAdvapi32)
        hAdvapi32 = LoadLibrary(TEXT("advapi32.dll"));

    auto openProcessToken =
      OpenProcessToken_((void (*)()) GetProcAddress(hAdvapi32, "OpenProcessToken"));
    if (!openProcessToken)
        return nullptr;
    auto lookupPrivilegeValueA =
      LookupPrivilegeValueA_((void (*)()) GetProcAddress(hAdvapi32, "LookupPrivilegeValueA"));
    if (!lookupPrivilegeValueA)
        return nullptr;
    auto adjustTokenPrivileges =
      AdjustTokenPrivileges_((void (*)()) GetProcAddress(hAdvapi32, "AdjustTokenPrivileges"));
    if (!adjustTokenPrivileges)
        return nullptr;

    HANDLE hProcessToken{};
    // Need SeLockMemoryPrivilege, so try to enable it for the process
    if (!openProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hProcessToken))
        return nullptr;

    void* mem = nullptr;

    LUID luid{};
    if (lookupPrivilegeValueA(nullptr, "SeLockMemoryPrivilege", &luid))
    {
        TOKEN_PRIVILEGES tp{};
        TOKEN_PRIVILEGES prevTp{};
        DWORD            prevTpLen = 0;

        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
        // we still need to query GetLastError() to ensure that the privileges were actually obtained.
        if (adjustTokenPrivileges(hProcessToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &prevTp,
                                  &prevTpLen)
            && GetLastError() == ERROR_SUCCESS)
        {
            // Round up size to full pages and allocate
            allocSize = (allocSize + lpSize - 1) & ~std::size_t(lpSize - 1);
            mem       = VirtualAlloc(nullptr, allocSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                                     PAGE_READWRITE);

            // Privilege no longer needed, restore previous state
            adjustTokenPrivileges(hProcessToken, FALSE, &prevTp, 0, nullptr, nullptr);
        }
    }

    CloseHandle(hProcessToken);

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
    #if defined(__linux__)
    constexpr std::size_t Alignment = 2 * 1024 * 1024;  // assumed 2MB page size
    #else
    constexpr std::size_t Alignment = 4 * 1024;  // assumed small page size
    #endif

    // Round up to multiples of Alignment
    std::size_t roundAllocSize = ((allocSize + Alignment - 1) / Alignment) * Alignment;

    void* mem = alloc_aligned_std(Alignment, roundAllocSize);
    #if defined(MADV_HUGEPAGE)
    if (mem != nullptr)
        madvise(mem, roundAllocSize, MADV_HUGEPAGE);
    #endif
    return mem;
#endif
}

// Free Aligned Large Pages
// nop if mem == nullptr
void free_aligned_lp(void* mem) noexcept {
#if defined(_WIN32)
    if (mem != nullptr && !VirtualFree(mem, 0, MEM_RELEASE))
    {
        std::cerr << "Failed to free large page memory."
                  << "Error code: 0x" << std::hex << GetLastError() << std::dec << '\n';
        exit(EXIT_FAILURE);
    }
#else
    free_aligned_std(mem);
#endif
}

}  // namespace DON
