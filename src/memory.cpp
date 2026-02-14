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

#include <cstdlib>  // Header for malloc(), free(), aligned_alloc()

#if defined(_WIN32)
    #include <malloc.h>  // MSVC Header for _mm_malloc(), _mm_free()
#endif

#include "misc.h"

namespace DON {

// Wrapper for systems where the c++17 implementation
// does not guarantee the availability of aligned_alloc().
// Memory allocated with alloc_aligned_std() must be freed with free_aligned_std().

void* alloc_aligned_std(std::size_t allocSize, std::size_t alignment) noexcept {

    // Treat zero-size requests as null for simplicity and to avoid UB in some allocators.
    if (allocSize == 0)
        return nullptr;

    // POSIX requires power-of-two and >= alignof(void*).
    // Windows tolerates more, but normalizing helps keep behavior consistent.
    alignment = std::max(alignment, alignof(void*));

#if defined(_WIN32)
    #if !defined(_M_ARM) && !defined(_M_ARM64)
    return _mm_malloc(allocSize, alignment);
    #else
    return _aligned_malloc(allocSize, alignment);
    #endif
#elif defined(_ISOC11_SOURCE) || defined(__cpp_aligned_new)
    // std::aligned_alloc requires size to be multiple of alignment
    allocSize = round_up_to_pow2_multiple(allocSize, alignment);
    return std::aligned_alloc(alignment, allocSize);
#else
    void* mem = nullptr;
    return posix_memalign(&mem, alignment, allocSize) != 0 ? nullptr : mem;
#endif
}

void free_aligned_std(void* mem) noexcept {
    if (mem == nullptr)
        return;
#if defined(_WIN32)
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
      [&](std::size_t LargePageSize) noexcept {
          // Round up size to full large page
          std::size_t roundedAllocSize = round_up_to_pow2_multiple(allocSize, LargePageSize);
          // Allocate large page memory
          void* mem = VirtualAlloc(nullptr, roundedAllocSize,
                                   MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

          if (mem == nullptr)
              DEBUG_LOG("Failed to allocate large page memory for "
                        << roundedAllocSize / ONE_MB
                        << "MB, error = " << error_to_string(GetLastError()));
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
        constexpr std::size_t Alignment =
    #if defined(_WIN64)
          4 * ONE_KB
    #else
          ONE_KB
    #endif
          ;

        std::size_t roundedAllocSize = round_up_to_pow2_multiple(allocSize, Alignment);

        mem = VirtualAlloc(nullptr, roundedAllocSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
#else
    // Choose a heuristic alignment for huge pages / fallback
    constexpr std::size_t Alignment =
    #if defined(__linux__)
      2 * ONE_MB  // Assume 2MB page-size
    #else
      4 * ONE_KB  // Assume small page-size
    #endif
      ;

    std::size_t roundedAllocSize = round_up_to_pow2_multiple(allocSize, Alignment);

    mem = alloc_aligned_std(roundedAllocSize, Alignment);
    #if defined(MADV_HUGEPAGE)
    //DEBUG_LOG("Using madvise() to advise kernel to use huge pages for " << roundedAllocSize / ONE_MB << "MB allocation");
    if (mem != nullptr && madvise(mem, roundedAllocSize, MADV_HUGEPAGE) != 0)
    {
        //DEBUG_LOG("madvise() failed, error = " << strerror(errno));
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
    void* mem = alloc_windows_aligned_large_page(2 * ONE_MB);  // 2MB page-size assumed
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
