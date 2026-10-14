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

#ifndef UTILITY_MAP_H_INCLUDED
#define UTILITY_MAP_H_INCLUDED

#include <functional>  // std::less<>
#include <map>
#include <mutex>
#include <shared_mutex>

#include "../utility/types.h"

namespace DON {

template <class Key, class Value, class Compare = std::less<Key>>
class Map final {
   public:
    using key_type        = Key;
    using mapped_type     = Value;
    using value_type      = std::pair<const Key, Value>;
    using size_type       = usize;
    using difference_type = std::ptrdiff_t;
    using key_compare     = Compare;

    class iterator;
    class const_iterator;

    Map() = default;

    explicit Map(const Compare& compare) :
        map(compare) {}

    [[nodiscard]] bool empty() const {
        std::shared_lock sharedLock{mutex};

        return map.empty();
    }

    [[nodiscard]] size_type size() const {
        std::shared_lock sharedLock{mutex};

        return map.size();
    }

    [[nodiscard]] size_type max_size() const {
        std::shared_lock sharedLock{mutex};

        return map.max_size();
    }

    void clear() {
        std::lock_guard lockGuard{mutex};

        map.clear();
    }

    [[nodiscard]] bool contains(const Key& key) const {
        std::shared_lock sharedLock{mutex};

        return map.find(key) != map.end();
    }

    [[nodiscard]] size_type count(const Key& key) const {
        std::shared_lock sharedLock{mutex};

        return map.count(key);
    }

    [[nodiscard]] key_compare key_comp() const {
        std::shared_lock sharedLock{mutex};

        return map.key_comp();
    }

   private:
    mutable std::shared_mutex mutex;
    std::map<Key, Value, Compare> map;
};

}  // namespace DON

#endif  // UTILITY_MAP_H_INCLUDED
