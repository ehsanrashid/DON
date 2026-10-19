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

#ifndef UTILITY_SET_H_INCLUDED
#define UTILITY_SET_H_INCLUDED

#include <algorithm>
#include <utility>
#include <vector>

#include "../utility/types.h"

namespace DON {

template<typename T, typename Compare>
class VectorSet final {
    static constexpr usize BlockCapacity = 128;

    using block_type = std::vector<T>;

   public:
    using value_type = T;
    using size_type  = usize;

    explicit VectorSet(const Compare& comp = Compare{}) noexcept :
        compare(comp) {}

    [[nodiscard]] size_type size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] T&       front() noexcept { return blocks.front().front(); }
    [[nodiscard]] const T& front() const noexcept { return blocks.front().front(); }

    [[nodiscard]] T&       back() noexcept { return blocks.back().back(); }
    [[nodiscard]] const T& back() const noexcept { return blocks.back().back(); }

    template<typename Function>
    void for_each(Function&& function) const noexcept {
        for (const block_type& block : blocks)
            for (const T& value : block)
                std::forward<Function>(function)(value);
    }

    void reserve(const size_type capacity) noexcept {
        const usize newCapacity = ceil_div(capacity, BlockCapacity);
        blocks.reserve(newCapacity);
    }

    void clear() noexcept {
        blocks.clear();
        size_ = 0;
    }

    [[nodiscard]] bool insert(const T& value) noexcept {
        auto blockItr = find_block(value);

        if (blockItr == blocks.end())
        {
            if (blocks.empty())
            {
                auto& block = blocks.emplace_back();
                block.reserve(BlockCapacity);
            }

            blockItr = std::prev(blocks.end());
        }

        block_type& block = *blockItr;

        if (block.empty() || compare(block.back(), value))
            block.push_back(value);
        else
        {
            const auto itr = std::lower_bound(block.begin(), block.end(), value, compare);

            if (itr != block.end() && !compare(value, *itr))
                return false;

            block.insert(itr, value);
        }

        ++size_;

        if (block.size() < BlockCapacity)
            return true;

        auto splitItr = block.begin() + (BlockCapacity / 2);

        block_type newBlock;
        newBlock.reserve(BlockCapacity);
        newBlock.insert(newBlock.end(), std::make_move_iterator(splitItr),
                        std::make_move_iterator(block.end()));

        block.erase(splitItr, block.end());
        blocks.insert(std::next(blockItr), std::move(newBlock));

        return true;
    }

    [[nodiscard]] size_type erase(const T& value) noexcept {
        auto blockItr = find_block(value);

        if (blockItr == blocks.end())
            return 0;

        block_type& block = *blockItr;

        const auto itr = std::lower_bound(block.begin(), block.end(), value, compare);

        if (itr == block.end() || compare(value, *itr))
            return 0;

        block.erase(itr);

        --size_;

        if (!block.empty())
            return 1;

        blocks.erase(blockItr);

        return 1;
    }

   private:
    auto find_block(const T& value) noexcept {
        return std::lower_bound(blocks.begin(), blocks.end(), value,
                                [this](const block_type& block, const T& sought) noexcept {
                                    return compare(block.back(), sought);
                                });
    }

    Compare                 compare;
    std::vector<block_type> blocks;
    size_type               size_ = 0;
};

}  // namespace DON

#endif  // UTILITY_SET_H_INCLUDED
