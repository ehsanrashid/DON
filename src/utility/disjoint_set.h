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

#ifndef UTILITY_DISJOINT_SET_H_INCLUDED
#define UTILITY_DISJOINT_SET_H_INCLUDED

#include <numeric>

#include "../utility/types.h"

namespace DON {

// DisjointSet is essentially implementing Disjoint Set Union (DSU) / Union-Find.
// This is a good candidate for a standalone reusable class.
class DisjointSet final {
   public:
    DisjointSet() noexcept = default;

    explicit DisjointSet(const usize n) noexcept { reset(n); }

    void reset(const usize n) noexcept {
        parents.resize(n);
        std::iota(parents.begin(), parents.end(), 0);
        ranks.assign(n, 0);
        sizes.assign(n, 1);
        components_ = n;
    }

    [[nodiscard]] usize find(const usize x) noexcept {
        if (parents[x] != x)
            parents[x] = find(parents[x]);

        return parents[x];
    }

    bool unite(const usize x, const usize y) noexcept {
        usize px = find(x);
        usize py = find(y);

        if (px == py)
            return false;

        if (ranks[px] < ranks[py])
            std::swap(px, py);

        parents[py] = px;
        sizes[px] += sizes[py];

        ranks[px] += (ranks[px] == ranks[py]);

        --components_;

        return true;
    }

    [[nodiscard]] bool connected(const usize x, const usize y) noexcept {
        return find(x) == find(y);
    }

    [[nodiscard]] usize size(const usize x) noexcept { return sizes[find(x)]; }

    [[nodiscard]] usize components() const noexcept { return components_; }

   private:
    std::vector<usize> parents;
    std::vector<usize> ranks;
    std::vector<usize> sizes;
    usize              components_ = 0;
};

}  // namespace DON

#endif  // UTILITY_DISJOINT_SET_H_INCLUDED
