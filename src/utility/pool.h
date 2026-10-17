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

//#define USE_STATIC_VECTOR

#if defined(USE_STATIC_VECTOR)
    #include "../utility/static_vector.h"
#else
    #include <vector>
#endif

#include "../utility/types.h"

namespace DON {

template<typename T, usize BlockBytesMax = 64 * KB>
class Pool final {
    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];
        Slot* next = nullptr;
    };

    struct Block {
        std::unique_ptr<Slot[]> slots;
    };

    static constexpr usize BaseBlockSize = []() constexpr {
        constexpr usize count = BlockBytesMax / sizeof(Slot);
        usize           size  = 1;

        while (size <= count / 2)
            size *= 2;

        return size;
    }();

   public:
    explicit Pool() noexcept
#if defined(USE_STATIC_VECTOR)
      = default;
#else
    {

        constexpr usize reserveCapacity = 16;
        // Reserve blocks
        blocks.reserve(reserveCapacity);
    }
#endif
    Pool(const Pool&)            = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&)                 = delete;
    Pool& operator=(Pool&&)      = delete;

    template<typename... Args>
    T* create(const usize requiredSize, Args&&... args) noexcept {
        if (freeSlotList.empty())
            allocate_block(requiredSize);

        Slot* const slot = freeSlotList.pop_front();
        assert(slot != nullptr);

        return ::new (static_cast<void*>(slot->storage)) T(std::forward<Args>(args)...);
    }

    void destroy(T* const object) noexcept {
        assert(object != nullptr);

        Slot* const slot =
          reinterpret_cast<Slot*>(reinterpret_cast<std::byte*>(object) - offsetof(Slot, storage));
        object->~T();
        freeSlotList.push_front(*slot);
    }

    void release() noexcept {
        freeSlotList.clear();
#if defined(USE_STATIC_VECTOR)
        blocks.clear_and_release();
#else
        blocks.clear();
#endif
    }

   private:
    [[nodiscard]] static constexpr usize block_size(const usize requiredSize) noexcept {
        if (requiredSize >= BaseBlockSize)
            return BaseBlockSize;

        usize size = 1;
        while (size < requiredSize)
            size *= 2;

        return size;
    }

    void allocate_block(const usize requiredSize) noexcept {
        const usize count = block_size(requiredSize == 0 ? 1 : requiredSize);
        auto        slots = std::make_unique<Slot[]>(count);
        Slot* const first = slots.get();

        blocks.push_back(Block{std::move(slots)});

        for (usize index = 0; index < count; ++index)
            freeSlotList.push_front(first[index]);
    }

#if defined(USE_STATIC_VECTOR)
    static constexpr usize BlockMax = 256;

    StaticVector<Block, BlockMax>
#else
    std::vector<Block>
#endif
      blocks;

    SinglyIntrusiveList<Slot> freeSlotList;
};

}  // namespace DON

#endif  // UTILITY_POOL_H_INCLUDED
