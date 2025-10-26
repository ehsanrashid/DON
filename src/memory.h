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

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "types.h"

namespace DON {

void* alloc_aligned_std(std::size_t allocSize, std::size_t alignment) noexcept;
void  free_aligned_std(void* mem) noexcept;
// memory aligned by page size, min alignment: 4096 bytes
void* alloc_aligned_lp(std::size_t allocSize) noexcept;
bool  free_aligned_lp(void* mem) noexcept;

bool has_lp() noexcept;

// Round up to multiples of alignment
template<typename T>
[[nodiscard]] constexpr T round_up_pow2(T size, T alignment) noexcept {
    static_assert(std::is_unsigned_v<T>);
    assert(alignment && !(alignment & (alignment - 1)));
    std::size_t mask = alignment - 1;
    return (size + mask) & ~mask;
}

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

    constexpr std::size_t ArrayOffset = std::max(sizeof(std::size_t), alignof(T));
    // Move back on the pointer to where the size is allocated.
    auto* rawMem = reinterpret_cast<char*>(mem) - ArrayOffset;

    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        std::size_t size = *reinterpret_cast<std::size_t*>(rawMem);

        //// Explicitly call the destructor for each element in reverse order
        //for (std::size_t i = size; i-- > 0;)
        //    std::destroy_at(&mem[i]);  // mem[i].~T();

        //// Forward order
        //std::destroy(mem, mem + size);
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
memory_allocator(AllocFunc&& allocFunc, std::size_t size) noexcept {
    using ElementType = std::remove_extent_t<T>;

    constexpr std::size_t ArrayOffset = std::max(sizeof(std::size_t), alignof(ElementType));

    // Save the array size in the memory location
    auto* rawMem = reinterpret_cast<char*>(allocFunc(ArrayOffset + size * sizeof(ElementType)));
    ASSERT_ALIGNED(rawMem, alignof(T));

    new (rawMem) std::size_t(size);

    for (std::size_t i = 0; i < size; ++i)
        new (rawMem + ArrayOffset + i * sizeof(ElementType)) ElementType();

    // Need to return the pointer at the start of the array so that the indexing in unique_ptr<T[]> works
    return reinterpret_cast<ElementType*>(rawMem + ArrayOffset);
}

//
// Aligned std unique ptr
//

template<typename T>
struct AlignedStdDeleter final {
    void operator()(T* mem) const noexcept { return memory_deleter<T>(mem, free_aligned_std); }
};

template<typename T>
struct AlignedStdArrayDeleter final {
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
    const auto allocFunc = [](std::size_t allocSize) {
        return alloc_aligned_std(allocSize, alignof(T));
    };

    auto* obj = memory_allocator<T>(allocFunc, std::forward<Args>(args)...);

    return AlignedStdPtr<T>(obj);
}

// make_unique_aligned_std() for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, AlignedStdPtr<T>>
make_unique_aligned_std(std::size_t size) noexcept {
    using ElementType = std::remove_extent_t<T>;

    const auto allocFunc = [](std::size_t allocSize) {
        return alloc_aligned_std(allocSize, alignof(ElementType));
    };

    auto* memory = memory_allocator<T>(allocFunc, size);

    return AlignedStdPtr<T>(memory);
}

//
// Aligned large page unique ptr
//

template<typename T>
struct AlignedLPDeleter final {
    void operator()(T* mem) const noexcept { return memory_deleter<T>(mem, free_aligned_lp); }
};

template<typename T>
struct AlignedLPArrayDeleter final {
    void operator()(T* mem) const noexcept { return memory_array_deleter<T>(mem, free_aligned_lp); }
};

template<typename T>
using AlignedLPPtr =
  std::conditional_t<std::is_array_v<T>,
                     std::unique_ptr<T, AlignedLPArrayDeleter<std::remove_extent_t<T>>>,
                     std::unique_ptr<T, AlignedLPDeleter<T>>>;

// make_unique_aligned_lp() for single objects
template<typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, AlignedLPPtr<T>>
make_unique_aligned_lp(Args&&... args) noexcept {
    static_assert(alignof(T) <= 4096,
                  "alloc_aligned_lp() may fail for such a big alignment requirement of T");

    auto* obj = memory_allocator<T>(alloc_aligned_lp, std::forward<Args>(args)...);

    return AlignedLPPtr<T>(obj);
}

// make_unique_aligned_lp() for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, AlignedLPPtr<T>> make_unique_aligned_lp(std::size_t size) {
    using ElementType = std::remove_extent_t<T>;

    static_assert(alignof(ElementType) <= 4096,
                  "alloc_aligned_lp() may fail for such a big alignment requirement of T");

    auto* memory = memory_allocator<T>(alloc_aligned_lp, size);

    return AlignedLPPtr<T>(memory);
}

// Get the first aligned element of an array.
// ptr must point to an array of size at least `sizeof(T) * N + alignment` bytes,
// where N is the number of elements in the array.
template<std::size_t Alignment, typename T>
[[nodiscard]] constexpr T* align_ptr_up(T* ptr) noexcept {
    static_assert(Alignment && !(Alignment & (Alignment - 1)),
                  "Alignment must be a non-zero power of two");
    static_assert(Alignment >= alignof(T), "Alignment must be >= alignof(T)");

    auto ptrInt = reinterpret_cast<std::uintptr_t>(ptr);
    ptrInt      = round_up_pow2(ptrInt, static_cast<std::uintptr_t>(Alignment));
    return reinterpret_cast<T*>(ptrInt);
}
template<std::size_t Alignment, typename T>
[[nodiscard]] constexpr const T* align_ptr_up(const T* ptr) noexcept {
    using U = std::remove_const_t<T>;
    return const_cast<const T*>(align_ptr_up<Alignment>(const_cast<U*>(ptr)));
}

}  // namespace DON

#endif  // #ifndef MEMORY_H_INCLUDED
