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

#ifndef UTILITY_MULTI_ARRAY_H_INCLUDED
#define UTILITY_MULTI_ARRAY_H_INCLUDED

#include "../utility/types.h"

namespace DON {

template<typename T, usize Size, usize... Sizes>
class MultiArray;

namespace Internal {

// Recursive template to define multi-dimensional array
template<typename T, usize Size, usize... Sizes>
struct MultiArrayDef final {
    using Type = MultiArray<T, Sizes...>;
};

// Base case: single-dimensional array
template<typename T, usize Size>
struct MultiArrayDef<T, Size> final {
    using Type = T;
};

}  // namespace Internal

// MultiArray is a generic N-dimensional array.
// T is the base element type; Size and Sizes are the dimensions.
template<typename T, usize Size, usize... Sizes>
class MultiArray final {
    using ElementType = typename Internal::MultiArrayDef<T, Size, Sizes...>::Type;
    using ArrayType   = Array<ElementType, Size>;

   public:
    using value_type             = typename ArrayType::value_type;
    using size_type              = typename ArrayType::size_type;
    using difference_type        = typename ArrayType::difference_type;
    using reference              = typename ArrayType::reference;
    using const_reference        = typename ArrayType::const_reference;
    using pointer                = typename ArrayType::pointer;
    using const_pointer          = typename ArrayType::const_pointer;
    using iterator               = typename ArrayType::iterator;
    using const_iterator         = typename ArrayType::const_iterator;
    using reverse_iterator       = typename ArrayType::reverse_iterator;
    using const_reverse_iterator = typename ArrayType::const_reverse_iterator;

    constexpr auto begin() noexcept { return data_.begin(); }
    constexpr auto end() noexcept { return data_.end(); }
    constexpr auto begin() const noexcept { return data_.begin(); }
    constexpr auto end() const noexcept { return data_.end(); }

    constexpr auto cbegin() const noexcept { return data_.cbegin(); }
    constexpr auto cend() const noexcept { return data_.cend(); }

    constexpr auto rbegin() noexcept { return data_.rbegin(); }
    constexpr auto rend() noexcept { return data_.rend(); }
    constexpr auto rbegin() const noexcept { return data_.rbegin(); }
    constexpr auto rend() const noexcept { return data_.rend(); }

    constexpr auto crbegin() const noexcept { return data_.crbegin(); }
    constexpr auto crend() const noexcept { return data_.crend(); }

    constexpr auto&       front() noexcept { return data_.front(); }
    constexpr const auto& front() const noexcept { return data_.front(); }
    constexpr auto&       back() noexcept { return data_.back(); }
    constexpr const auto& back() const noexcept { return data_.back(); }

    constexpr auto*       data() noexcept { return data_.data(); }
    constexpr const auto* data() const noexcept { return data_.data(); }

    constexpr auto max_size() const noexcept { return data_.max_size(); }

    constexpr auto size() const noexcept { return data_.size(); }
    constexpr auto empty() const noexcept { return data_.empty(); }

    constexpr const auto& at(const size_type idx) const { return data_.at(idx); }
    constexpr auto&       at(const size_type idx) { return data_.at(idx); }

    constexpr const auto& operator[](const size_type idx) const noexcept {
        assert(idx < size());

        return data_[idx];
    }

    constexpr auto& operator[](const size_type idx) noexcept {
        assert(idx < size());

        return data_[idx];
    }

    template<typename U>
    void fill(const U& value) noexcept {
        static_assert(is_strictly_assignable_v<T, U>, "Cannot assign fill value to element type");

        for (auto& element : data_)
        {
            if constexpr (sizeof...(Sizes) == 0)
                element = value;
            else
                element.fill(value);
        }
    }

    template<typename U>
    void fill_n(const usize beg, const usize count, const U& value) noexcept {
        static_assert(is_strictly_assignable_v<T, U>, "Cannot assign fill value to element type");

        assert(beg <= size());

        const usize end = beg + std::min(count, size() - beg);
        assert(end <= size());

        for (usize idx = beg; idx < end; ++idx)
        {
            if constexpr (sizeof...(Sizes) == 0)
                data_[idx] = value;
            else
                data_[idx].fill(value);
        }
    }

    //void print() const noexcept {
    //    std::cout << Size << ':' << sizeof...(Sizes) << '\n';
    //
    //    for (auto& element : data_)
    //    {
    //        if constexpr (sizeof...(Sizes) == 0)
    //            std::cout << element << ' ';
    //        else
    //            element.print();
    //    }
    //
    //    std::cout << std::endl;
    //}

    constexpr void swap(MultiArray<T, Size, Sizes...>& multiArr) noexcept {
        data_.swap(multiArr.data_);
    }

    template<bool NoExtraDimension = sizeof...(Sizes) == 0,
             typename              = std::enable_if_t<NoExtraDimension, bool>>
    constexpr operator Array<T, Size>&() noexcept {
        return data_;
    }

    template<bool NoExtraDimension = sizeof...(Sizes) == 0,
             typename              = std::enable_if_t<NoExtraDimension, bool>>
    constexpr operator const Array<T, Size>&() const noexcept {
        return data_;
    }

    constexpr MultiArray& operator=(const ArrayType& stdArr) noexcept {
        data_ = stdArr;
        return *this;
    }

   private:
    ArrayType data_;
};

}  // namespace DON

#endif  // UTILITY_MULTI_ARRAY_H_INCLUDED
