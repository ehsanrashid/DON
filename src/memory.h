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

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "misc.h"
#include "types.h"

namespace DON {

inline constexpr u8    HUGE_PAGE_SHIFT = 30;
inline constexpr usize HUGE_PAGE_SIZE  = usize{1} << HUGE_PAGE_SHIFT;

void* alloc_aligned_std(usize allocSize, usize alignment) noexcept;

void free_aligned_std(void* mem) noexcept;

// memory aligned by page size, min alignment: 4096 bytes
void* alloc_aligned_large_page_with_hint(usize allocSize, bool hugePageHint = false) noexcept;
void* alloc_aligned_large_page(usize allocSize) noexcept;

bool free_aligned_large_page(void* mem) noexcept;

bool has_large_page() noexcept;

// Frees memory which was placed there with placement new.
// Works for both single objects and arrays of unknown bound.
template<typename T, typename FreeFunc>
void memory_deleter(T* mem, FreeFunc&& freeFunc) noexcept {
    if (mem == nullptr)
        return;

    // Explicitly needed to call the destructor
    if constexpr (!std::is_trivially_destructible_v<T>)
        std::destroy_at(mem);

    freeFunc(mem);
}

// Frees memory which was placed there with placement new.
// Works for both single objects and arrays of unknown bound.
template<typename T, typename FreeFunc>
void memory_array_deleter(T* mem, FreeFunc&& freeFunc) noexcept {
    if (mem == nullptr)
        return;

    constexpr usize ArrayOffset = std::max(sizeof(usize), alignof(T));
    // Move back on the pointer to where the size is allocated.
    auto* rawMem = reinterpret_cast<char*>(mem) - ArrayOffset;

    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        usize size = *reinterpret_cast<usize*>(rawMem);
        // Reverse order
        std::destroy(std::make_reverse_iterator(mem + size), std::make_reverse_iterator(mem));
    }

    freeFunc(rawMem);
}

// Allocates memory for a single object and places it there with placement new.
template<typename T, typename AllocFunc, typename... Args>
inline std::enable_if_t<!std::is_array_v<T>, T*> memory_allocator(AllocFunc&& allocFunc,
                                                                  Args&&... args) noexcept {
    void* rawMem = allocFunc(sizeof(T));
    ASSERT_ALIGNED(rawMem, alignof(T));
    return new (rawMem) T(std::forward<Args>(args)...);
}

// Allocates memory for an array of unknown bound and places it there with placement new.
template<typename T, typename AllocFunc>
inline std::enable_if_t<std::is_array_v<T>, std::remove_extent_t<T>*>
memory_allocator(AllocFunc&& allocFunc, usize size) noexcept {
    using ElementType = std::remove_extent_t<T>;

    constexpr usize ArrayOffset = std::max(alignof(ElementType), sizeof(usize));

    // Save the array size in the memory location
    auto* rawMem = reinterpret_cast<char*>(allocFunc(ArrayOffset + size * sizeof(ElementType)));
    ASSERT_ALIGNED(rawMem, alignof(T));

    new (rawMem) usize(size);

    for (usize i = 0; i < size; ++i)
        new (rawMem + ArrayOffset + i * sizeof(ElementType)) ElementType();

    // Need to return the pointer at the start of the array so that the indexing in unique_ptr<T[]> works
    return reinterpret_cast<ElementType*>(rawMem + ArrayOffset);
}

//
// Aligned std unique ptr
//

template<typename T>
struct AlignedStdDeleter {
    void operator()(T* mem) const noexcept { return memory_deleter<T>(mem, free_aligned_std); }
};

template<typename T>
struct AlignedStdArrayDeleter {
    void operator()(T* mem) const noexcept {
        return memory_array_deleter<T>(mem, free_aligned_std);
    }
};

template<typename T>
using AlignedStdPtr =
  std::conditional_t<std::is_array_v<T>,
                     std::unique_ptr<T, AlignedStdArrayDeleter<std::remove_extent_t<T>>>,
                     std::unique_ptr<T, AlignedStdDeleter<T>>>;

// make_unique_aligned_std() for single objects
template<typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, AlignedStdPtr<T>>
make_unique_aligned_std(Args&&... args) noexcept {
    auto allocFunc = [](usize allocSize) { return alloc_aligned_std(allocSize, alignof(T)); };

    auto* obj = memory_allocator<T>(allocFunc, std::forward<Args>(args)...);

    return AlignedStdPtr<T>(obj);
}

// make_unique_aligned_std() for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, AlignedStdPtr<T>>
make_unique_aligned_std(usize size) noexcept {
    using ElementType = std::remove_extent_t<T>;

    auto allocFunc = [](usize allocSize) {
        return alloc_aligned_std(allocSize, alignof(ElementType));
    };

    auto* mem = memory_allocator<T>(allocFunc, size);

    return AlignedStdPtr<T>(mem);
}

//
// Aligned large page unique ptr
//

template<typename T>
struct LargePageDeleter {
    void operator()(T* mem) const noexcept {
        return memory_deleter<T>(mem, free_aligned_large_page);
    }
};

template<typename T>
struct LargePageArrayDeleter {
    void operator()(T* mem) const noexcept {
        return memory_array_deleter<T>(mem, free_aligned_large_page);
    }
};

template<typename T>
using LargePagePtr =
  std::conditional_t<std::is_array_v<T>,
                     std::unique_ptr<T, LargePageArrayDeleter<std::remove_extent_t<T>>>,
                     std::unique_ptr<T, LargePageDeleter<T>>>;

// make_unique_aligned_large_page() for single objects
template<typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, LargePagePtr<T>>
make_unique_aligned_large_page(Args&&... args) noexcept {
    static_assert(alignof(T) <= 4096,
                  "alloc_aligned_large_page() may fail for such a big alignment requirement of T");

    auto* obj = memory_allocator<T>(alloc_aligned_large_page, std::forward<Args>(args)...);

    return LargePagePtr<T>(obj);
}

// make_unique_aligned_large_page() for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, LargePagePtr<T>> make_unique_aligned_large_page(usize size) {
    using ElementType = std::remove_extent_t<T>;

    static_assert(alignof(ElementType) <= 4096,
                  "alloc_aligned_large_page() may fail for such a big alignment requirement of T");

    auto* mem = memory_allocator<T>(alloc_aligned_large_page, size);

    return LargePagePtr<T>(mem);
}

template<typename T, typename ByteT>
[[nodiscard]] T load_as(const ByteT* buffer) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
    static_assert(sizeof(ByteT) == 1);

    if (reinterpret_cast<uptr>(buffer) % alignof(T) != 0)
    {
        assert(false);
        UNREACHABLE();
    }

    T value;
    std::memcpy(&value, buffer, sizeof(value));

    return value;
}

}  // namespace DON

#endif  // MEMORY_H_INCLUDED
