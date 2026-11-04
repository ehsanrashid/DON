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

#ifndef UTILITY_POOL_H_INCLUDED
#define UTILITY_POOL_H_INCLUDED

#include <algorithm>    // min()/max()
#include <cstddef>      // std::byte, offsetof()
#include <exception>    // terminate()
#include <limits>       // numeric_limits<>
#include <memory>       // unique_ptr, make_unique
#include <new>          // placement new
#include <type_traits>  // is_nothrow_destructible_v, if used
#include <utility>      // forward, move()

#define USE_STATIC_VECTOR

#if defined(USE_STATIC_VECTOR)
    #include "../utility/static_vector.h"
#else
    #include <vector>
#endif

#include "../utility/attributes.h"
#include "../utility/list.h"
#include "../utility/types.h"

namespace DON {

template<typename T, usize BlockBytesMax = 64 * KB>
class Pool final {
    struct Slot;

    struct ActiveNode final {
       public:
        explicit ActiveNode(Slot* slotPtr) noexcept :
            slot(slotPtr) {}

        Slot*       slot = nullptr;
        ActiveNode* prev = nullptr;
        ActiveNode* next = nullptr;
    };

    struct Slot final {
       public:
        alignas(T) std::byte storage[sizeof(T)];
        ActiveNode activeNode{this};
        Slot*      next = nullptr;
    };

    struct Block final {
       public:
        std::unique_ptr<Slot[]> slots;
        usize                   size = 0;
    };

    // Default block size: the largest power of two whose Slot array fits within BlockBytesMax.
    // It is at least one slot, even if one Slot exceeds that limit.
    static constexpr usize BaseBlockSize = []() constexpr {
        constexpr usize count = BlockBytesMax / sizeof(Slot);

        usize size = 1;

        while (size <= count / 2)
            size *= 2;

        return size;
    }();

#if defined(USE_STATIC_VECTOR)
    static constexpr usize BlockMax = 1024;

    using block_container_type = StaticVector<Block, BlockMax>;
#else
    using block_container_type = std::vector<Block>;
#endif

   public:
    Pool()
#if defined(USE_STATIC_VECTOR)
      = default;
#else
    {
        blocks.reserve(16);
    }
#endif
    Pool(const Pool&) noexcept            = delete;
    Pool& operator=(const Pool&) noexcept = delete;
    Pool(Pool&&) noexcept                 = delete;
    Pool& operator=(Pool&&) noexcept      = delete;

    ~Pool() noexcept { release(); }

    // These creation functions are noexcept. If allocating a block or constructing T throws,
    // the process terminates rather than returning a recoverable failure.
    template<typename... Args>
    [[nodiscard]] T* create(usize requiredSize, Args&&... args) noexcept {
        return create_(requiredSize, std::forward<Args>(args)...);
    }

    template<typename... Args>
    [[nodiscard]] T* create_object(Args&&... args) noexcept {
        return create_(1, std::forward<Args>(args)...);
    }

    void destroy(T* value) noexcept {
        // Defensive null check - prevents undefined behavior if called with nullptr
        if (UNLIKELY(value == nullptr))
            std::terminate();

        Slot* const slot = slot_from_value(value);

        detach_active(slot);

        value->~T();

        slotList.push_front(*slot);
    }

    void release() noexcept {
        while (ActiveNode* node = activeList.front())
        {
            Slot* const slot = node->slot;

            detach_active(slot);

            T* value = std::launder(reinterpret_cast<T*>(slot->storage));
            value->~T();
        }

        slotList.clear();
        blocks.clear();

        nextBlockSize = 1;
    }

    // True when there are no free slots available for reuse; active objects may still exist.
    [[nodiscard]] bool empty() const noexcept { return slotList.empty(); }

    // Number of currently constructed objects tracked by the active-object list.
    [[nodiscard]] usize active_size() const noexcept { return activeList.size(); }

    // True when at least one object is currently constructed in this pool.
    [[nodiscard]] bool has_active_objects() const noexcept { return !activeList.empty(); }

    // Number of allocated storage blocks, including blocks with no active objects.
    [[nodiscard]] usize block_count() const noexcept { return blocks.size(); }

   private:
    [[nodiscard]] static Slot* slot_from_value(T* value) noexcept {
        constexpr usize offset = offsetof(Slot, storage);

        return reinterpret_cast<Slot*>(reinterpret_cast<std::byte*>(value) - offset);
    }

    // Choose at least requiredSize slots. Ordinary blocks grow geometrically up to
    // BaseBlockSize; requests larger than that are allocated at their requested size.
    [[nodiscard]] usize block_size(usize requiredSize) noexcept {
        usize size = nextBlockSize;

        while (size < std::min(requiredSize, BaseBlockSize))
            size = std::min(2 * size, BaseBlockSize);

        if (size < requiredSize)
            size = requiredSize;

        if (size < BaseBlockSize)
            nextBlockSize = std::min(2 * size, BaseBlockSize);

        return size;
    }

    // Allocate and retain a stable Slot array, then put every slot on the free list.
    // With StaticVector, exceeding BlockMax retained blocks terminates the process.
    void allocate_block(usize requiredSize) {
        constexpr usize MaxBlockSize = usize(std::numeric_limits<isize>::max()) / sizeof(Slot);

#if defined(USE_STATIC_VECTOR)
        if (blocks.full())
            std::terminate();
#endif
        // Check for arithmetic overflow and bounds
        if (UNLIKELY(requiredSize > MaxBlockSize))
            std::terminate();

        const usize size = block_size(requiredSize);

        if (UNLIKELY(size == 0 || size > MaxBlockSize))
            std::terminate();

        blocks.push_back(Block{std::make_unique<Slot[]>(size), size});

        Slot* const slots = blocks.back().slots.get();
        // Ensure allocated memory is valid before use
        if (UNLIKELY(slots == nullptr))
            std::terminate();

        // Loop directly using pointer limits for absolute code minimization
        for (Slot* slot = slots; slot < slots + size; ++slot)
            slotList.push_front(*slot);
    }

    void attach_active(Slot* const slot) noexcept {
        // Ensure slot is valid
        if (UNLIKELY(slot == nullptr))
            std::terminate();

        if (UNLIKELY(!activeList.push_back(slot->activeNode)))
            std::terminate();
    }

    void detach_active(Slot* const slot) noexcept {
        // Ensure slot is valid
        if (UNLIKELY(slot == nullptr))
            std::terminate();

        if (UNLIKELY(!activeList.erase(slot->activeNode)))
            std::terminate();
    }

    template<typename... Args>
    [[nodiscard]] T* create_(usize requiredSize, Args&&... args) {
        if (slotList.empty())
            allocate_block(requiredSize);

        Slot* const slot = slotList.pop_front();

        // Ensure slot allocation succeeded
        if (UNLIKELY(slot == nullptr))
            std::terminate();

        T* value = nullptr;

        // Construction can throw; cleanup on failure (noexcept by contract)
        //try
        //{
        value = ::new (static_cast<void*>(slot->storage)) T(std::forward<Args>(args)...);
        //} catch (...)
        //{
        //    // Return slot to free list on construction failure
        //    slotList.push_front(*slot);
        //    throw;
        //}

        if (UNLIKELY(value == nullptr))
        {
            slotList.push_front(*slot);
            std::terminate();
        }

        attach_active(slot);

        return value;
    }

    block_container_type            blocks;
    SinglyIntrusiveList<Slot>       slotList;
    DoublyIntrusiveList<ActiveNode> activeList;
    usize                           nextBlockSize = 1;
};

}  // namespace DON

#endif  // UTILITY_POOL_H_INCLUDED
