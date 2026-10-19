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
#include <optional>
#include <utility>
#include <vector>

#include "../utility/types.h"

namespace DON {

template<typename T, typename Compare>
class VectorSet final {
    static constexpr usize BlockCapacity = 128;
    static constexpr usize SplitSize     = BlockCapacity / 2;

    using block_type     = std::vector<T>;
    using blocks_type    = std::vector<block_type>;
    using block_iterator = typename std::vector<block_type>::iterator;
    using iterator       = typename block_type::iterator;

   public:
    using value_type = T;
    using size_type  = usize;

    // Identifies an element and the block containing it.
    struct Position final {
        block_iterator block;
        iterator       element;
    };

    explicit VectorSet(const Compare& comp = Compare{}) noexcept :
        compare(comp) {}

    [[nodiscard]] size_type block_count() const noexcept { return blocks.size(); }

    [[nodiscard]] size_type block_size(const size_type index) const noexcept {
        return blocks[index].size();
    }

    [[nodiscard]] const T& block_content(const size_type index,
                                         const size_type idx) const noexcept {
        return blocks[index][idx];
    }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] bool      empty() const noexcept { return size() == 0; }

    [[nodiscard]] T&       front() noexcept { return blocks.front().front(); }
    [[nodiscard]] const T& front() const noexcept { return blocks.front().front(); }

    [[nodiscard]] T&       back() noexcept { return blocks.back().back(); }
    [[nodiscard]] const T& back() const noexcept { return blocks.back().back(); }

    template<typename Function>
    void for_each(Function&& function) const noexcept {
        for (const block_type& block : blocks)
            for (const T& value : block)
                function(value);
    }

    void reserve(const size_type capacity) noexcept {
        blocks.reserve(ceil_div(capacity, SplitSize));
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

                block.push_back(value);

                ++size_;

                return true;
            }

            blockItr = std::prev(blocks.end());
        }

        block_type& block = *blockItr;

        // Fast path: append when the value is greater than the last element
        if (/*block.empty() || Not need b/c above blocks.empty()*/ compare(block.back(), value))
            block.push_back(value);
        else
        {
            const auto itr = std::lower_bound(block.begin(), block.end(), value, compare);

            // Equivalent values are duplicates under Compare.
            if (itr != block.end() && !compare(value, *itr))
                return false;

            block.insert(itr, value);
        }

        [[maybe_unused]] const size_type blockIndex = split_block(blockItr);

        ++size_;

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
        if (block.empty())
            blocks.erase(blockItr);

        --size_;

        return 1;
    }

    [[nodiscard]] std::optional<Position> find_position(const T& value) noexcept {
        auto blockItr = find_block(value);

        if (blockItr == blocks.end())
            return std::nullopt;

        const auto elementItr =
          std::lower_bound(blockItr->begin(), blockItr->end(), value, compare);

        if (elementItr == blockItr->end() || compare(value, *elementItr))
            return std::nullopt;

        return Position{blockItr, elementItr};
    }

    [[nodiscard]] bool promote(const Position& position) noexcept {
        size_type blockIndex =
          static_cast<size_type>(std::distance(blocks.begin(), position.block));

        size_type elementIndex =
          static_cast<size_type>(std::distance(position.block->begin(), position.element));

        const T value = *position.element;

        while (true)
        {
            block_type& block   = blocks[blockIndex];
            iterator    element = block.begin() + elementIndex;

            const auto positionItr = std::upper_bound(
              block.begin(), element, value,
              [this](const T& lhs, const T& rhs) noexcept { return compare(lhs, rhs); });

            if (positionItr != element)
            {
                const bool atFront = positionItr == block.begin();

                std::rotate(positionItr, element, std::next(element));

                if (!atFront)
                    return true;

                elementIndex = 0;
            }
            else if (elementIndex != 0)
            {
                return true;
            }

            if (blockIndex == 0)
                return true;

            const size_type previousIndex = blockIndex - 1;
            block_type&     previous      = blocks[previousIndex];

            if (!compare(value, previous.back()))
                return true;

            block.erase(block.begin());

            if (block.empty())
                blocks.erase(blocks.begin() + blockIndex);

            block_type& target = blocks[previousIndex];

            const auto targetItr = std::lower_bound(target.begin(), target.end(), value, compare);

            const size_type targetIndex =
              static_cast<size_type>(std::distance(target.begin(), targetItr));

            target.insert(targetItr, value);

            // May split the block and invalidate outer-vector iterators
            [[maybe_unused]] const size_type splitIndex =
              split_block(blocks.begin() + previousIndex);

            // Only an insertion at index zero can need further promotion
            if (targetIndex != 0)
                return true;

            blockIndex   = previousIndex;
            elementIndex = 0;
        }
    }

   private:
    [[nodiscard]] auto find_block(const T& value) noexcept {
        return std::lower_bound(blocks.begin(), blocks.end(), value,
                                [this](const block_type& block, const T& sought) noexcept {
                                    return compare(block.back(), sought);
                                });
    }

    template<typename Predicate>
    [[nodiscard]] iterator find_if(const size_type blockIndex, Predicate&& predicate) noexcept {
        block_type& block = blocks[blockIndex];

        return std::find_if(block.begin(), block.end(), std::forward<Predicate>(predicate));
    }

    [[nodiscard]] size_type split_block(const block_iterator blockItr) noexcept {
        const size_type blockIndex = size_type(std::distance(blocks.begin(), blockItr));

        block_type& block = blocks[blockIndex];

        if (block.size() < BlockCapacity)
            return blockIndex;

        const size_type splitIndex = SplitSize;

        block_type newBlock;
        newBlock.reserve(BlockCapacity);

        newBlock.insert(newBlock.end(), std::make_move_iterator(block.begin() + splitIndex),
                        std::make_move_iterator(block.end()));

        block.erase(block.begin() + splitIndex, block.end());

        blocks.insert(blocks.begin() + blockIndex + 1, std::move(newBlock));

        return blockIndex;
    }

    Compare     compare;
    blocks_type blocks;
    size_type   size_ = 0;
};

}  // namespace DON

#endif  // UTILITY_SET_H_INCLUDED
