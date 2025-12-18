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

#if defined(__ANDROID__)

#elif defined(_WIN32)
    #include <iostream>
#elif defined(__linux__)
    #include <sys/mman.h>
#endif

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__OpenBSD__) \
  || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32)) \
  || defined(__e2k__)
    #define POSIX_ALIGNED
#endif

#include "misc.h"  // IWYU pragma: keep

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

// Return suitably aligned memory, if possible using large page
#if defined(_WIN32)
namespace {

void* alloc_windows_aligned_large_page(std::size_t allocSize) noexcept {

    return try_with_windows_lock_memory_privilege(
      [&](std::size_t largePageSize) {
          // Round up size to full large page
          std::size_t roundedAllocSize = round_up_pow2(allocSize, largePageSize);
          // Allocate large page memory
          void* mem = VirtualAlloc(nullptr, roundedAllocSize,
                                   MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
          if (mem == nullptr)
          {
              std::cerr << "Failed to allocate " << (roundedAllocSize >> 20)
                        << "MB for large page memory." << std::endl;
              std::cerr << "Error code: " << u32_to_string(GetLastError()) << std::endl;
          }
          return mem;
      },
      []() { return (void*) nullptr; });
}

}  // namespace
#endif

// Allocate aligned large page
void* alloc_aligned_large_page(std::size_t allocSize) noexcept {

    void* mem;
#if defined(_WIN32)
    // Try to allocate large page
    mem = alloc_windows_aligned_large_page(allocSize);
    // Fall back to regular, page-aligned, allocation if necessary
    if (mem == nullptr)
    {
        constexpr std::size_t Alignment = 4 * 1024;

        std::size_t roundedAllocSize = round_up_pow2(allocSize, Alignment);

        mem = VirtualAlloc(nullptr, roundedAllocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
#else
    constexpr std::size_t Alignment =
    #if defined(__linux__)
      2 * 1024 * 1024  // Assume 2MB page-size
    #else
      4 * 1024  // Assume small page-size
    #endif
      ;

    std::size_t roundedAllocSize = round_up_pow2(allocSize, Alignment);

    mem = alloc_aligned_std(roundedAllocSize, Alignment);
    #if defined(MADV_HUGEPAGE)
    if (mem != nullptr)
        madvise(mem, roundedAllocSize, MADV_HUGEPAGE);
    #endif
#endif
    return mem;
}

// Free aligned large page
// The effect is a nop if mem == nullptr
bool free_aligned_large_page(void* mem) noexcept {

#if defined(_WIN32)
    if (mem != nullptr && !VirtualFree(mem, 0, MEM_RELEASE))
    {
        std::cerr << "Failed to free large page memory." << std::endl;
        std::cerr << "Error code: " << u32_to_string(GetLastError()) << std::endl;
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
    void* mem = alloc_windows_aligned_large_page(2 * 1024 * 1024);  // 2MB page-size assumed
    if (mem == nullptr)
        return false;
    [[maybe_unused]] bool success = free_aligned_large_page(mem);
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
