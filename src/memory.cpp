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

#if defined(_WIN32)
    #include <malloc.h>  // _mm_malloc(), _mm_free()
#else
    #if defined(__linux__) && !defined(__ANDROID__)
        #include <sys/mman.h>
    #endif
    #if defined(__has_include) && __has_include(<features.h>)
        #include <features.h>  // glibc feature-test macros
    #endif
#endif

#if defined(__APPLE__)      /* macOS / iOS */ \
  || defined(__ANDROID__)   /* Android */ \
  || defined(__FreeBSD__)   /* FreeBSD */ \
  || defined(__OpenBSD__)   /* OpenBSD */ \
  || defined(__NetBSD__)    /* NetBSD */ \
  || defined(__DragonFly__) /* DragonFly BSD */ \
  || defined(__e2k__)       /* Elbrus 2000 */ \
  || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) \
      && !defined(_WIN32)) /* libstdc++ without aligned_alloc */
    #include <stdlib.h>
    #define USE_POSIX_ALIGNED_ALLOC
#endif

#include "misc.h"

namespace DON {

// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc().
// Memory allocated with alloc_aligned_std() must be freed with free_aligned_std().

void* alloc_aligned_std(usize allocSize, usize alignment) noexcept {

    // Treat zero-size requests as null for simplicity and to avoid UB in some allocators.
    if (allocSize == 0)
        return nullptr;

#if defined(_ISOC11_SOURCE) || !(defined(USE_POSIX_ALIGNED_ALLOC) || defined(_WIN32))
    // std::aligned_alloc requires size to be a multiple of alignment
    allocSize = round_up_to_pow2_multiple(allocSize, alignment);
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

void free_aligned_std(void* mem) noexcept {

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

// Return suitably aligned memory, if possible using large page
#if defined(_WIN32)
namespace {

void* alloc_windows_aligned_large_page(usize allocSize) noexcept {

    return try_with_windows_lock_memory_privilege(
      [&](usize LargePageSize) noexcept {
          // Round up size to full large page
          usize roundedAllocSize = round_up_to_pow2_multiple(allocSize, LargePageSize);
          // Allocate large page memory
          void* mem = VirtualAlloc(nullptr, roundedAllocSize,
                                   MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
          if (mem == nullptr)
              DEBUG_LOG("Failed to allocate large page memory for "
                        << roundedAllocSize / MB
                        << "MB, error = " << error_to_string(GetLastError()));
          return mem;
      },
      []() { return (void*) nullptr; });
}

}  // namespace
#endif

// Allocate aligned large page
void* alloc_aligned_large_page(usize allocSize) noexcept {

    void* mem;
#if defined(_WIN32)
    // Try to allocate large page
    mem = alloc_windows_aligned_large_page(allocSize);
    // Fall back to regular, page-aligned, allocation if necessary
    if (mem == nullptr)
    {
        constexpr usize Alignment =
    #if defined(_WIN64)
          4 * KB
    #else
          1 * KB
    #endif
          ;

        usize roundedAllocSize = round_up_to_pow2_multiple(allocSize, Alignment);

        mem = VirtualAlloc(nullptr, roundedAllocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (mem == nullptr)
            DEBUG_LOG("Failed to allocate memory for " << roundedAllocSize / MB << "MB, error = "
                                                       << error_to_string(GetLastError()));
    }
#else
    // Choose a heuristic alignment for huge pages / fallback
    constexpr usize Alignment =
    #if defined(__linux__)
      2 * MB  // Assume 2MB page-size
    #else
      4 * KB  // Assume small page-size
    #endif
      ;

    usize roundedAllocSize = round_up_to_pow2_multiple(allocSize, Alignment);

    mem = alloc_aligned_std(roundedAllocSize, Alignment);
    #if defined(MADV_HUGEPAGE)
    if (mem != nullptr && ::madvise(mem, roundedAllocSize, MADV_HUGEPAGE) != 0)
    {
        //DEBUG_LOG("::madvise() failed, error = " << strerror(errno));
    }
    #endif
#endif
    return mem;
}

// Free aligned large page
// The effect is a nop if mem == nullptr
bool free_aligned_large_page(void* mem) noexcept {
    if (mem == nullptr)
        return true;
#if defined(_WIN32)
    if (!VirtualFree(mem, 0, MEM_RELEASE))
    {
        DEBUG_LOG("Failed to free memory, error = " << error_to_string(GetLastError()));
        return false;
    }
#else
    free_aligned_std(mem);
#endif
    return true;
}

// Check large page support
bool has_large_page() noexcept {

#if defined(_WIN32)
    void* mem = alloc_windows_aligned_large_page(2 * MB);  // 2MB page-size assumed
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
