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
 *   SegmentTreeMin seqTree(n);
 *
 * Creates an empty tree containing N positions, all initialized to
 * `Infinity`.
 *
 * Alternatively, construct directly from an array:
 *
 *   SegmentTreeMin seqTree(values);
 *
 * The values are copied into the leaves and the internal nodes are
 * built in O(N).
 *
 * Example:
 *
 *   SegmentTreeMin seqTree(std::vector<int>{6, 5, 8, 4});
 *
 *   seqTree.min(0, 4);     // minimum of [6, 5, 8, 4] -> 4
 *   seqTree.min(1, 3);     // minimum of [5, 8]       -> 5
 *
 *   seqTree.update(2, 2);  // [6, 5, 2, 4]
 *   seqTree.min(0, 4);     // -> 2
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
 *   Construction with N slots  : O(N)
 *   Point update               : O(log N)
 *   Range minimum query        : O(log N)
 *   firstle / lastle           : O(log N)
 *   Memory                     : O(N)
 */
class SegmentTreeMin final {
   public:
    using value_type = int;

    static constexpr value_type Infinity = std::numeric_limits<value_type>::max();

    explicit SegmentTreeMin(const usize n) :
        N(n),
        H(leaf_count(n)),
        M(checked_tree_size(H)),
        tree(M, Infinity) {}

    explicit SegmentTreeMin(const std::vector<value_type>& values) :
        SegmentTreeMin(values.size()) {
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
            throw std::out_of_range("SegmentTreeMin: invalid range");

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
            throw std::length_error("SegmentTreeMin: size exceeds ptrdiff_t");

        usize h = 1;

        while (h < n)
        {
            if (h > std::numeric_limits<usize>::max() / 2)
                throw std::length_error("SegmentTreeMin: size too large");

            h *= 2;
        }

        return h;
    }

    [[nodiscard]] static usize checked_tree_size(const usize h) {
        const usize maxSize = std::vector<value_type>{}.max_size();

        if (h > maxSize / 2)
            throw std::length_error("SegmentTreeMin: tree exceeds vector max_size");

        return h * 2;
    }

    void check_position(usize pos) const {
        if (pos >= N)
            throw std::out_of_range("SegmentTreeMin: invalid position");
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

class SegmentTreeSum final {
   public:
    using value_type = int;

    static constexpr value_type Identity = 0;

    explicit SegmentTreeSum(const usize n) :
        N(n),
        H(leaf_count(n)),
        M(checked_tree_size(H)),
        tree(M, Identity) {}

    explicit SegmentTreeSum(const std::vector<value_type>& values) :
        SegmentTreeSum(values.size()) {
        std::copy(values.begin(), values.end(), tree.begin() + H);

        for (usize i = H; i-- > 1;)
            propagate(i);
    }

    // Assign a value to position pos.
    void update(const usize pos, const value_type value) {
        check_position(pos);

        usize i = H + pos;
        tree[i] = value;

        while ((i >>= 1) != 0)
            propagate(i);
    }

    // Return the sum over the half-open range [l, r).
    // An empty range returns 0.
    [[nodiscard]] value_type sum(usize l, usize r) const {
        if (l > r || r > N)
            throw std::out_of_range("SegmentTreeSum: invalid range");

        value_type resultLeft  = Identity;
        value_type resultRight = Identity;

        l += H;
        r += H;

        while (l < r)
        {
            if (l & 1)
                resultLeft += tree[l++];

            if (r & 1)
                resultRight += tree[--r];

            l >>= 1;
            r >>= 1;
        }

        return resultLeft + resultRight;
    }

    [[nodiscard]] value_type operator[](const usize i) const noexcept { return tree[H + i]; }

    [[nodiscard]] value_type& operator[](const usize i) noexcept { return tree[H + i]; }

    [[nodiscard]] usize size() const noexcept { return N; }

   private:
    [[nodiscard]] static usize leaf_count(const usize n) {
        if (n > static_cast<usize>(std::numeric_limits<isize>::max()))
            throw std::length_error("SegmentTreeSum: size exceeds isize");

        usize h = 1;

        while (h < n)
        {
            if (h > std::numeric_limits<usize>::max() / 2)
                throw std::length_error("SegmentTreeSum: size too large");

            h *= 2;
        }

        return h;
    }

    [[nodiscard]] static usize checked_tree_size(const usize h) {
        const usize maxSize = std::vector<value_type>{}.max_size();

        if (h > maxSize / 2)
            throw std::length_error("SegmentTreeSum: tree exceeds vector max_size");

        return h * 2;
    }

    void check_position(const usize pos) const {
        if (pos >= N)
            throw std::out_of_range("SegmentTreeSum: invalid position");
    }

    void propagate(const usize i) noexcept { tree[i] = tree[2 * i] + tree[2 * i + 1]; }

    usize N = 0;
    usize H = 0;
    usize M = 0;

    std::vector<value_type> tree;
};

struct NestedSegment final {
    usize l;
    usize r;
    usize id;
};

// Requires your existing SegmentTreeSum class.
std::vector<int> count_nested_segments(const std::vector<int>& sequence) {
    if (sequence.size() % 2 != 0)
        throw std::invalid_argument("Sequence size must be even");

    const usize n = sequence.size() / 2;

    std::vector<NestedSegment> segments(
      n, {std::numeric_limits<usize>::max(), std::numeric_limits<usize>::max(), 0});

    // Find the first and second occurrence of every number.
    for (usize i = 0; i < sequence.size(); ++i)
    {
        const int x = sequence[i];

        if (x < 1 || static_cast<usize>(x) > n)
            throw std::invalid_argument("Invalid sequence value");

        NestedSegment& segment = segments[usize(x - 1)];

        if (segment.l == std::numeric_limits<usize>::max())
        {
            segment.l  = i;
            segment.id = usize(x - 1);
        }
        else if (segment.r == std::numeric_limits<usize>::max())
        {
            segment.r = i;
        }
        else
        {
            throw std::invalid_argument("A number occurs more than twice");
        }
    }

    for (const NestedSegment& segment : segments)
    {
        if (segment.r == std::numeric_limits<usize>::max())
            throw std::invalid_argument("A number occurs fewer than twice");
    }

    // Process intervals in increasing order of their right endpoint.
    std::sort(segments.begin(), segments.end(),
              [](const NestedSegment& a, const NestedSegment& b) noexcept { return a.r < b.r; });

    // Each position is initially absent (0).
    SegmentTreeSum sqgTree(sequence.size());

    std::vector<int> answer(n, 0);

    for (const NestedSegment& segment : segments)
    {
        // Your tree uses [l, r), while the source uses [l, r].
        // Previously marked left endpoints inside this interval
        // correspond to segments nested inside the current segment.
        answer[segment.id] = sqgTree.sum(segment.l, segment.r + 1);

        // Mark this segment's left endpoint as present.
        sqgTree.update(segment.l, 1);
    }

    return answer;
}

}  // namespace DON

#endif  // UTILITY_SEGMENT_TREE_H_INCLUDED
