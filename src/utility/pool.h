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

#include <cassert>
#include <cstddef>      // std::byte, offsetof()
#include <memory>       // unique_ptr, make_unique
#include <new>          // placement new
#include <type_traits>  // is_nothrow_destructible_v, if used
#include <utility>      // forward, move()
#include <exception>    // terminate()

#define USE_STATIC_VECTOR

#if defined(USE_STATIC_VECTOR)
    #include "../utility/static_vector.h"
#else
    #include <vector>
#endif

#include "../utility/types.h"

namespace DON {


template<typename T, usize BlockBytesMax = 64 * KB>
class Pool final {
    struct Slot;

    struct ActiveNode {
        Slot*       slot = nullptr;
        ActiveNode* prev = nullptr;
        ActiveNode* next = nullptr;

        explicit ActiveNode(Slot* slotPtr) noexcept :
            slot(slotPtr) {}
    };

    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];
        Slot*      next = nullptr;
        ActiveNode activeNode{this};
    };

    struct Block {
        std::unique_ptr<Slot[]> slots;
        usize                   size = 0;
    };

    static constexpr usize BaseBlockSize = []() constexpr {
        constexpr usize count = BlockBytesMax / sizeof(Slot);

        usize size = 1;

        while (size <= count / 2)
            size *= 2;

        return size;
    }();

#ifdef USE_STATIC_VECTOR
    static constexpr usize BlockMax = 1024;

    using block_container_type = StaticVector<Block, BlockMax>;
#else
    using block_container_type = std::vector<Block>;
#endif

   public:
    Pool()                                = default;
    Pool(const Pool&) noexcept            = delete;
    Pool& operator=(const Pool&) noexcept = delete;
    Pool(Pool&&) noexcept                 = delete;
    Pool& operator=(Pool&&) noexcept      = delete;

    ~Pool() noexcept { release(); }

    template<typename... Args>
    [[nodiscard]] T* create(usize requiredSize, Args&&... args) noexcept {
        return create_(requiredSize, std::forward<Args>(args)...);
    }

    template<typename... Args>
    [[nodiscard]] T* create_object(Args&&... args) noexcept {
        return create_(1, std::forward<Args>(args)...);
    }

    void destroy(T* value) noexcept {
        assert(value != nullptr);

        if (value == nullptr)
            std::terminate();

        Slot* slot = slot_from_value(value);

        detach_active(slot);
        value->~T();

        slotList.push_front(*slot);
    }

    void release() noexcept {
        while (ActiveNode* node = activeList.front())
        {
            Slot* slot = node->slot;

            detach_active(slot);

            T* value = std::launder(reinterpret_cast<T*>(slot->storage));
            value->~T();
        }

        slotList.clear();
        blocks.clear();

        nextBlockSize = 1;
    }

    [[nodiscard]] bool empty() const noexcept { return slotList.empty(); }

    [[nodiscard]] usize active_size() const noexcept { return activeList.size(); }

    [[nodiscard]] bool has_active_objects() const noexcept { return !activeList.empty(); }

    [[nodiscard]] usize block_count() const noexcept { return blocks.size(); }

   private:
    [[nodiscard]] usize block_size(usize requiredSize) noexcept {
        usize size = nextBlockSize;

        while (size < requiredSize && size < BaseBlockSize)
        {
            if (size > BaseBlockSize / 2)
            {
                size = BaseBlockSize;
                break;
            }

            size *= 2;
        }

        if (size < requiredSize)
            size = requiredSize;

        if (size < BaseBlockSize)
            nextBlockSize = size > BaseBlockSize / 2 ? BaseBlockSize : size * 2;

        return size;
    }

    void allocate_block(usize requiredSize) {
        const usize size = block_size(requiredSize);

#ifdef USE_STATIC_VECTOR
        if (blocks.full())
            std::terminate();
#endif

        auto  slots = std::make_unique<Slot[]>(size);
        Slot* first = slots.get();

        blocks.push_back(Block{std::move(slots), size});

        for (usize i = 0; i < size; ++i)
            slotList.push_front(first[i]);
    }

    [[nodiscard]] static Slot* slot_from_value(T* value) noexcept {
        constexpr usize offset = offsetof(Slot, storage);

        return reinterpret_cast<Slot*>(reinterpret_cast<std::byte*>(value) - offset);
    }

    void attach_active(Slot* slot) noexcept {
        const bool inserted = activeList.push_back(slot->activeNode);
        assert(inserted);
        (void) inserted;
    }

    void detach_active(Slot* slot) noexcept {
        const bool erased = activeList.erase(slot->activeNode);
        assert(erased);
        (void) erased;
    }

    template<typename... Args>
    [[nodiscard]] T* create_(usize requiredSize, Args&&... args) {
        if (slotList.empty())
            allocate_block(requiredSize);

        Slot* slot = slotList.pop_front();

        T* value;

        //try
        //{
        value = ::new (static_cast<void*>(slot->storage)) T(std::forward<Args>(args)...);
        //} catch (...)
        //{
        //    slotList.push_front(*slot);
        //    throw;
        //}

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
