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

#ifndef UTILITY_SEGMENT_TREE_H_INCLUDED
#define UTILITY_SEGMENT_TREE_H_INCLUDED

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

#include "../utility/types.h"

namespace DON {

/**
 * Segment tree for range-minimum queries (RMQ) with point updates.
 *
 * The tree stores `int` values and supports:
 *
 *   - update(pos, value)
 *       Set the value at a single position in O(log N).
 *
 *   - min(l, r)
 *       Return the minimum value in the half-open range [l, r)
 *       in O(log N).
 *
 *       An empty range (l == r) returns 0. This behavior is intentional
 *       and matches the original Java implementation; callers that depend
 *       on this behavior should not change it to `Infinity`.
 *
 *   - firstle(l, value)
 *       Find the first index i >= l whose value is <= `value`
 *       in O(log N).
 *       Returns -1 when no such index exists.
 *
 *   - lastle(l, value)
 *       Find the last index i <= l whose value is <= `value`
 *       in O(log N).
 *       Returns -1 when no such index exists.
 *
 * Internally, the leaves are stored at [H, H + N), where H is the
 * smallest power of two greater than or equal to N. Unused leaves are
 * initialized to `Infinity`, so they do not affect range-minimum queries.
 *
 * Construction:
 *
 *   SegmentTreeRMQ st(n);
 *
 * Creates an empty tree containing N positions, all initialized to
 * `Infinity`.
 *
 * Alternatively, construct directly from an array:
 *
 *   SegmentTreeRMQ st(values);
 *
 * The values are copied into the leaves and the internal nodes are
 * built in O(N).
 *
 * Example:
 *
 *   SegmentTreeRMQ st(std::vector<int>{6, 5, 8, 4});
 *
 *   st.min(0, 4);       // minimum of [6, 5, 8, 4] -> 4
 *   st.min(1, 3);       // minimum of [5, 8]       -> 5
 *
 *   st.update(2, 2);    // [6, 5, 2, 4]
 *   st.min(0, 4);       // -> 2
 *
 * The tree is particularly useful when values are updated incrementally
 * and range minimum queries are required between updates.
 * For example,
 * it can be used to compute dynamic DP states where the minimum over a
 * previously processed range determines the next state.
 *
 * Complexity:
 *
 *   Construction from N values : O(N)
 *   Construction with N slots   : O(N)
 *   Point update                : O(log N)
 *   Range minimum query         : O(log N)
 *   firstle / lastle            : O(log N)
 *   Memory                      : O(N)
 */
class SegmentTreeRMQ final {
   public:
    using value_type = int;

    static constexpr value_type Infinity = std::numeric_limits<value_type>::max();

    explicit SegmentTreeRMQ(const usize n) :
        N(n),
        H(leaf_count(n)),
        M(checked_tree_size(H)),
        tree(M, Infinity) {}

    explicit SegmentTreeRMQ(const std::vector<value_type>& values) :
        SegmentTreeRMQ(values.size()) {
        std::copy(values.begin(), values.end(), tree.begin() + H);

        for (usize i = H; i-- > 1;)
            propagate(i);
    }

    // Update the value at position pos.
    void update(usize pos, value_type value) noexcept {
        check_position(pos);

        usize i = H + pos;
        tree[i] = value;

        while ((i >>= 1) != 0)
            propagate(i);
    }

    // Minimum over the half-open range [l, r).
    [[nodiscard]] value_type min(usize l, usize r) const {
        if (l > r || r > N)
            throw std::out_of_range("SegmentTreeRMQ: invalid range");

        if (l == r)
            return 0;

        l += H;
        r += H;

        value_type result = Infinity;

        while (l < r)
        {
            if (l & 1)
                result = std::min(result, tree[l++]);

            if (r & 1)
                result = std::min(result, tree[--r]);

            l >>= 1;
            r >>= 1;
        }

        return result;
    }

    // First index i >= l where st[H + i] <= value.
    [[nodiscard]] isize firstle(usize l, value_type value) const {
        if (l >= N)
            return -1;

        return firstle_(1, 0, H, l, value);
    }

    // Last index i <= l where st[H + i] <= value.
    [[nodiscard]] isize lastle(usize l, value_type value) const {
        if (N == 0 || l >= N)
            return -1;

        return lastle_(1, 0, H, l + 1, value);
    }

    [[nodiscard]] value_type& operator[](const usize i) noexcept { return tree[i]; }

    [[nodiscard]] const value_type& operator[](const usize i) const noexcept { return tree[i]; }

   private:
    [[nodiscard]] static usize leaf_count(const usize n) {
        if (n > static_cast<usize>(std::numeric_limits<isize>::max()))
            throw std::length_error("SegmentTreeRMQ: size exceeds ptrdiff_t");

        usize h = 1;

        while (h < n)
        {
            if (h > std::numeric_limits<usize>::max() / 2)
                throw std::length_error("SegmentTreeRMQ: size too large");

            h *= 2;
        }

        return h;
    }

    [[nodiscard]] static usize checked_tree_size(const usize h) {
        const usize maxSize = std::vector<value_type>{}.max_size();

        if (h > maxSize / 2)
            throw std::length_error("SegmentTreeRMQ: tree exceeds vector max_size");

        return h * 2;
    }

    void check_position(usize pos) const {
        if (pos >= N)
            throw std::out_of_range("SegmentTreeRMQ: invalid position");
    }

    void propagate(usize i) noexcept { tree[i] = std::min(tree[2 * i], tree[2 * i + 1]); }

    [[nodiscard]] isize firstle_(
      usize node, usize left, usize right, usize queryLeft, value_type value) const noexcept {
        if (right <= queryLeft || tree[node] > value)
            return -1;

        if (right - left == 1)
            return left < N ? static_cast<isize>(left) : -1;

        const usize mid = left + (right - left) / 2;

        const isize result = firstle_(node * 2, left, mid, queryLeft, value);

        if (result != -1)
            return result;

        return firstle_(node * 2 + 1, mid, right, queryLeft, value);
    }

    [[nodiscard]] isize lastle_(
      usize node, usize left, usize right, usize queryRight, value_type value) const noexcept {
        if (left >= queryRight || tree[node] > value)
            return -1;

        if (right - left == 1)
            return left < N ? static_cast<isize>(left) : -1;

        const usize mid = left + (right - left) / 2;

        const isize result = lastle_(node * 2 + 1, mid, right, queryRight, value);

        if (result != -1)
            return result;

        return lastle_(node * 2, left, mid, queryRight, value);
    }

    usize N = 0;
    usize H = 0;
    usize M = 0;

    std::vector<value_type> tree;
};

}  // namespace DON

#endif  // UTILITY_SEGMENT_TREE_H_INCLUDED
