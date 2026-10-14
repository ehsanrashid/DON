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

#ifndef UTILITY_STATIC_VECTOR_H_INCLUDED
#define UTILITY_STATIC_VECTOR_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <utility>  // move(), swap()

#include "../utility/types.h"

namespace DON {

template<typename T, usize Capacity, typename SizeType = usize>
class StaticVector final {
    static_assert(Capacity != 0, "Capacity must be != 0");

   public:
    [[nodiscard]] static constexpr SizeType capacity() noexcept { return SizeType(Capacity); }

    [[nodiscard]] SizeType size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] bool full() const noexcept { return size() == capacity(); }

    T*       data() noexcept { return buffer.data(); }
    const T* data() const noexcept { return buffer.data(); }

    T*       begin() noexcept { return data(); }
    const T* begin() const noexcept { return data(); }

    T*       end() noexcept { return begin() + size(); }
    const T* end() const noexcept { return begin() + size(); }

    const T* cbegin() const noexcept { return data(); }
    const T* cend() const noexcept { return cbegin() + size(); }

    void push_back(const T& value) noexcept {
        assert(size() < capacity());

        *end() = value;
        ++size_;
    }

    void push_back(T&& value) noexcept {
        assert(size() < capacity());

        *end() = std::move(value);
        ++size_;
    }

    // Append value if cond is true
    void push_back_if(const T& value, const bool cond) noexcept {
        assert(size() < capacity());

        *end() = value;
        size_ += int(cond);
    }

    // Append value if value < max
    void push_back_if_lt(const T& value, const T& maxValue) noexcept {
        assert(size() < capacity());

        *end() = value;
        size_ += int(value < maxValue);
    }

    void pop_back() noexcept {
        assert(size() != 0);

        --size_;
    }

    void pop_front() noexcept {
        assert(size() != 0);

        for (SizeType i = 1; i < size(); ++i)
            buffer[i - 1] = std::move(buffer[i]);

        --size_;
    }

    T& front() noexcept {
        assert(size() != 0);

        return *begin();
    }

    const T& front() const noexcept {
        assert(size() != 0);

        return *begin();
    }

    T& back() noexcept {
        assert(size() != 0);

        return *(end() - 1);
    }

    const T& back() const noexcept {
        assert(size() != 0);

        return *(end() - 1);
    }

    T& operator[](const SizeType idx) noexcept {
        assert(idx < size());

        return buffer[idx];
    }

    const T& operator[](const SizeType idx) const noexcept {
        assert(idx < size());

        return buffer[idx];
    }

    void resize(const SizeType newSize) noexcept {
        // Note: doesn't construct/destroy elements
        size_ = std::min(newSize, capacity());
    }

    T* make_space(const SizeType space) noexcept {
        T* const first = end();

        resize(size() + space);

        return first;
    }

    void swap(StaticVector& staticVec) noexcept {
        if (this == &staticVec)
            return;

        const SizeType commonSize = std::min(size_, staticVec.size_);

        for (SizeType i = 0; i < commonSize; ++i)
            std::swap(buffer[i], staticVec.buffer[i]);

        for (SizeType i = commonSize; i < size_; ++i)
            staticVec.buffer[i] = std::move(buffer[i]);

        for (SizeType i = commonSize; i < staticVec.size_; ++i)
            buffer[i] = std::move(staticVec.buffer[i]);

        std::swap(size_, staticVec.size_);
    }

    void clear() noexcept { size_ = 0; }

   private:
    Array<T, Capacity> buffer;
    SizeType           size_ = 0;
};

}  // namespace DON

#endif  // UTILITY_STATIC_VECTOR_H_INCLUDED
