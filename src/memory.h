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

#include <cstddef>
#include <cstdint>
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
void  free_aligned_lp(void* mem) noexcept;

bool has_large_pages() noexcept;

// Frees memory which was placed there with placement new.
// Works for both single objects and arrays of unknown bound.
template<typename T, typename FreeFunc>
void memory_deleter(T* mem, FreeFunc freeFunc) noexcept {
    if (mem == nullptr)
        return;

    // Explicitly needed to call the destructor
    if constexpr (!std::is_trivially_destructible_v<T>)
        std::destroy_at(mem);

    freeFunc(mem);
    return;
}

// Frees memory which was placed there with placement new.
// Works for both single objects and arrays of unknown bound.
template<typename T, typename FreeFunc>
void memory_array_deleter(T* mem, FreeFunc freeFunc) noexcept {
    if (mem == nullptr)
        return;

    constexpr std::size_t ARRAY_OFFSET = std::max(sizeof(std::size_t), alignof(T));
    // Move back on the pointer to where the size is allocated.
    char* rawMemory = reinterpret_cast<char*>(mem) - ARRAY_OFFSET;

    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        std::size_t size = *reinterpret_cast<std::size_t*>(rawMemory);

        // Explicitly call the destructor for each element in reverse order
        for (std::size_t i = size; i-- > 0;)
            std::destroy_at(&mem[i]);
    }

    freeFunc(rawMemory);
}

// Allocates memory for a single object and places it there with placement new.
template<typename T, typename AllocFunc, typename... Args>
inline std::enable_if_t<!std::is_array_v<T>, T*> memory_allocator(AllocFunc allocFunc,
                                                                  Args&&... args) noexcept {
    void* rawMemory = allocFunc(sizeof(T));
    ASSERT_ALIGNED(rawMemory, alignof(T));
    return new (rawMemory) T(std::forward<Args>(args)...);
}

// Allocates memory for an array of unknown bound and places it there with placement new.
template<typename T, typename AllocFunc>
inline std::enable_if_t<std::is_array_v<T>, std::remove_extent_t<T>*>
memory_allocator(AllocFunc allocFunc, std::size_t num) noexcept {
    using ElementType = std::remove_extent_t<T>;

    constexpr std::size_t ARRAY_OFFSET = std::max(sizeof(std::size_t), alignof(ElementType));

    // Save the array size in the memory location
    char* rawMemory = reinterpret_cast<char*>(allocFunc(ARRAY_OFFSET + num * sizeof(ElementType)));
    ASSERT_ALIGNED(rawMemory, alignof(T));

    new (rawMemory) std::size_t(num);

    for (std::size_t i = 0; i < num; ++i)
        new (rawMemory + ARRAY_OFFSET + i * sizeof(ElementType)) ElementType();

    // Need to return the pointer at the start of the array so that the indexing in unique_ptr<T[]> works
    return reinterpret_cast<ElementType*>(rawMemory + ARRAY_OFFSET);
}

//
// Aligned std unique ptr
//

template<typename T>
struct AlignedStdDeleter final {
    void operator()(T* mem) const noexcept {  //
        return memory_deleter<T>(mem, free_aligned_std);
    }
};

template<typename T>
struct AlignedStdArrayDeleter final {
    void operator()(T* mem) const noexcept {  //
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
    T* obj = memory_allocator<T>(allocFunc, std::forward<Args>(args)...);

    return AlignedStdPtr<T>(obj);
}

// make_unique_aligned_std() for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, AlignedStdPtr<T>>
make_unique_aligned_std(std::size_t num) noexcept {
    using ElementType = std::remove_extent_t<T>;

    const auto allocFunc = [](std::size_t allocSize) {
        return alloc_aligned_std(allocSize, alignof(ElementType));
    };
    ElementType* memory = memory_allocator<T>(allocFunc, num);

    return AlignedStdPtr<T>(memory);
}

//
// Aligned large page unique ptr
//

template<typename T>
struct AlignedLPDeleter final {
    void operator()(T* mem) const noexcept {  //
        return memory_deleter<T>(mem, free_aligned_lp);
    }
};

template<typename T>
struct AlignedLPArrayDeleter final {
    void operator()(T* mem) const noexcept {  //
        return memory_array_deleter<T>(mem, free_aligned_lp);
    }
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

    T* obj = memory_allocator<T>(alloc_aligned_lp, std::forward<Args>(args)...);

    return AlignedLPPtr<T>(obj);
}

// make_unique_aligned_lp() for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, AlignedLPPtr<T>> make_unique_aligned_lp(std::size_t num) {
    using ElementType = std::remove_extent_t<T>;

    static_assert(alignof(ElementType) <= 4096,
                  "alloc_aligned_lp() may fail for such a big alignment requirement of T");

    ElementType* memory = memory_allocator<T>(alloc_aligned_lp, num);

    return AlignedLPPtr<T>(memory);
}


// Get the first aligned element of an array.
// ptr must point to an array of size at least `sizeof(T) * N + alignment` bytes,
// where N is the number of elements in the array.
template<std::uintptr_t Alignment, typename T>
T* align_ptr_up(T* ptr) noexcept {
    static_assert(alignof(T) < Alignment);

    const std::uintptr_t uintPtr = reinterpret_cast<std::uintptr_t>(reinterpret_cast<char*>(ptr));
    return reinterpret_cast<T*>(
      reinterpret_cast<char*>((uintPtr + (Alignment - 1)) / Alignment * Alignment));
}

/*
template<typename T>
struct UniqueArrayWithSize final {
   public:
    UniqueArrayWithSize(std::unique_ptr<T[]> arr, std::size_t sz) noexcept :
        array(std::move(arr)),
        size(sz) {}

    auto* get() const noexcept { return array.get(); }

    auto sizeOf() const noexcept { return size * sizeof(T); }

    std::unique_ptr<T[]> array;
    std::size_t          size;
};

template<typename T, std::size_t N>
auto make_unique_array_with_size(const T (&)[N]) noexcept {
    return UniqueArrayWithSize(std::make_unique<T[]>(N), N);
}
*/
}  // namespace DON

#endif  // #ifndef MEMORY_H_INCLUDED
