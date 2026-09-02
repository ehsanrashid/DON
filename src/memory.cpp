/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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

#include <cstdlib>  // malloc(), free(), std::aligned_alloc()
#include <iostream>

#if defined(_WIN32)
    #include <malloc.h>  // <mm_malloc.h>: _mm_malloc(), _mm_free()

    #include "platform_win.h"
#else
    // IWYU pragma: no_include <bits/mman-map-flags-generic.h>
    #if defined(__has_include) && __has_include(<features.h>)
        #include <features.h>  // glibc feature-test macros
    #endif
    #if defined(__linux__) && !defined(__ANDROID__)
        #include <sys/mman.h>
        #if defined(X86_64) && defined(MAP_HUGE_SHIFT)
            #define USE_POSIX_X86_64_HUGE_PAGES
        #endif
    #endif
    #include <cerrno>
    #include <cstring>
#endif

#if defined(__APPLE__)      /* macOS / iOS */ \
  || defined(__ANDROID__)   /* Android */ \
  || defined(__FreeBSD__)   /* FreeBSD */ \
  || defined(__OpenBSD__)   /* OpenBSD */ \
  || defined(__NetBSD__)    /* NetBSD */ \
  || defined(__DragonFly__) /* DragonFly BSD */ \
  || defined(__e2k__)       /* Elbrus 2000 */ \
  || defined(_AIX)          /* IBM AIX */ \
  || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) \
      && !defined(_WIN32)) /* libstdc++ without aligned_alloc() */
    #include <stdlib.h>
    #define USE_POSIX_ALIGNED_ALLOC
#endif

#include "misc.h"

namespace DON {

// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc().
// Memory allocated with alloc_aligned_std() must be freed with free_aligned_std().

void* alloc_aligned_std(usize allocSize, const usize alignment) noexcept {

    // Treat zero-size requests as null for simplicity and to avoid UB in some allocators.
    if (allocSize == 0)
        return nullptr;

#if defined(_ISOC11_SOURCE) || !(defined(USE_POSIX_ALIGNED_ALLOC) || defined(_WIN32))
    // std::aligned_alloc requires size to be a multiple of alignment
    allocSize = round_up_to_multiple(allocSize, alignment);
#endif

#if defined(_ISOC11_SOURCE)
    return std::aligned_alloc(alignment, allocSize);
#elif defined(USE_POSIX_ALIGNED_ALLOC)
    void* mem = nullptr;
    return ::posix_memalign(&mem, alignment, allocSize) != 0 ? nullptr : mem;
#elif defined(_WIN32)
    #if defined(_M_ARM) || defined(_M_ARM64)
    return _aligned_malloc(allocSize, alignment);
    #else
    return _mm_malloc(allocSize, alignment);
    #endif
#else
    return std::aligned_alloc(alignment, allocSize);
#endif
}

void free_aligned_std(void* const mem) noexcept {

#if defined(USE_POSIX_ALIGNED_ALLOC)
    ::free(mem);
#elif defined(_WIN32)
    #if defined(_M_ARM) || defined(_M_ARM64)
    _aligned_free(mem);
    #else
    _mm_free(mem);
    #endif
#else
    std::free(mem);
#endif
}

namespace {

#if defined(_WIN32)
// Allocate suitably aligned memory using Windows large pages, if possible
void* alloc_windows_aligned_large_page(const usize allocSize) noexcept {

    return try_with_windows_lock_memory_privilege(
      [&](const usize largePageSize) noexcept {
          // Round allocation size up to a multiple of the large-page size
          const usize roundedAllocSize = round_up_to_multiple(allocSize, largePageSize);
          // Allocate memory using Windows large pages
          void* mem = VirtualAlloc(nullptr, roundedAllocSize,
                                   MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
          if (mem == nullptr)
          {
              std::cerr << "Failed to allocate large page memory for " << roundedAllocSize / MB
                        << "MB, error = " << error_to_string(GetLastError()) << std::endl;
          }
          return mem;
      },
      []() { return (void*) nullptr; });
}

#else

    #if defined(USE_POSIX_X86_64_HUGE_PAGES)

void* alloc_aligned_huge_page(const usize allocSize) noexcept {
    void* mem = ::mmap(
      nullptr, allocSize, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (u64{HUGE_PAGE_SHIFT} << MAP_HUGE_SHIFT), -1, 0);
    if (mem == MAP_FAILED)
        return nullptr;

    return mem;
}

bool free_aligned_huge_page(void* const mem, const usize allocSize) noexcept {
    if (::munmap(mem, allocSize) != 0)
    {
        std::cerr << "::munmap() failed: error = " << std::strerror(errno) << std::endl;
        return false;
    }

    return true;
}

AllocationSizes HugePageSizes(alloc_aligned_huge_page, free_aligned_huge_page);

    #endif

#endif

}  // namespace

// Allocate aligned memory with large-page support with hint
void* alloc_aligned_large_page_with_hint(const usize                 allocSize,
                                         [[maybe_unused]] const bool hugePageHint) noexcept {
    void* mem;
#if defined(_WIN32)
    // Try allocating with Windows large pages
    mem = alloc_windows_aligned_large_page(allocSize);
    if (mem == nullptr)
    {
        constexpr usize Alignment =
    #if defined(_WIN64)
          4 * KB
    #else
          1 * KB
    #endif
          ;

        const usize roundedAllocSize = round_up_to_multiple(allocSize, Alignment);
        // Fall back to regular Windows page-aligned allocation
        mem = VirtualAlloc(nullptr, roundedAllocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (mem == nullptr)
        {
            std::cerr << "Failed to allocate memory for " << roundedAllocSize / MB
                      << "MB, error = " << error_to_string(GetLastError()) << std::endl;
            return mem;
        }
    }
#else
    #if defined(USE_POSIX_X86_64_HUGE_PAGES)
    if (hugePageHint && allocSize >= HUGE_PAGE_SIZE)
    {
        const usize roundedAllocSize = round_up_to_multiple(allocSize, HUGE_PAGE_SIZE);
        // Allocate memory
        mem = HugePageSizes.alloc(roundedAllocSize);
        if (mem != nullptr)
            return mem;

        std::cerr << "Failed to allocate memory for " << roundedAllocSize / MB
                  << "MB, error = " << std::strerror(errno) << std::endl;
    }
    #endif
    // Choose a heuristic alignment for huge pages / fallback
    constexpr usize Alignment =
    #if defined(__linux__)
      2 * MB  // Assume 2MB page-size
    #else
      4 * KB  // Assume small page-size
    #endif
      ;

    const usize roundedAllocSize = round_up_to_multiple(allocSize, Alignment);

    mem = alloc_aligned_std(roundedAllocSize, Alignment);
    if (mem == nullptr)
    {
        std::cerr << "Failed to allocate memory for " << roundedAllocSize / MB
                  << "MB, error = " << std::strerror(errno) << std::endl;
        return mem;
    }
    // Prefer huge pages where supported
    #if defined(MADV_HUGEPAGE)
    if (::madvise(mem, roundedAllocSize, MADV_HUGEPAGE) != 0)
    {
        std::cerr << "::madvise() failed: error = " << std::strerror(errno) << std::endl;
    }
    #endif
#endif
    return mem;
}

// Allocate aligned memory with large-page support
void* alloc_aligned_large_page(const usize allocSize) noexcept {
    return alloc_aligned_large_page_with_hint(allocSize, false);
}

// Free aligned large page
// The effect is a nop if mem == nullptr
bool free_aligned_large_page(void* const mem) noexcept {
    if (mem == nullptr)
        return true;
#if defined(_WIN32)
    if (!VirtualFree(mem, 0, MEM_RELEASE))
    {
        std::cerr << "Failed to free memory, error = " << error_to_string(GetLastError())
                  << std::endl;
        return false;
    }
#else
    #if defined(USE_POSIX_X86_64_HUGE_PAGES)
    if (HugePageSizes.free(mem))
        return true;
    #endif
    free_aligned_std(mem);
#endif
    return true;
}

// Check large page support
bool has_large_page() noexcept {

#if defined(_WIN32)
    constexpr usize PageSize = 2 * MB;  // Assume 2MB page-size

    void* mem = alloc_windows_aligned_large_page(PageSize);
    if (mem == nullptr)
        return false;
    [[maybe_unused]] bool success = free_aligned_large_page(mem);
    assert(success);
    return true;
#elif defined(__linux__)
    return
    #if defined(MADV_HUGEPAGE)
      true
    #else
      false
    #endif
      ;
#else
    return false;
#endif
}

}  // namespace DON
