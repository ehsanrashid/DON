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
#include <utility>

#include "../utility/access.h"
#include "../utility/types.h"

namespace DON {

template<typename Key,
         typename Value,
         typename Mutex     = std::shared_mutex,
         typename Compare   = std::less<Key>,
         typename Allocator = std::allocator<std::pair<const Key, Value>>>
class Map final {
   public:
    using key_type        = Key;
    using mapped_type     = Value;
    using value_type      = std::pair<const Key, Value>;
    using size_type       = usize;
    using difference_type = std::ptrdiff_t;
    using key_compare     = Compare;
    using allocator_type  = Allocator;

    class value_compare {
        friend class Map;

       protected:
        explicit value_compare(key_compare compare) :
            compare(compare) {}

       public:
        [[nodiscard]] bool operator()(const value_type& v1, const value_type& v2) const {
            return compare(v1.first, v2.first);
        }

       private:
        key_compare compare;
    };

    using map_type          = std::map<Key, Value, Compare, Allocator>;
    using read_access_type  = ReadAccess<map_type, Mutex>;
    using write_access_type = WriteAccess<map_type, Mutex>;

    Map() noexcept = default;

    explicit Map(const Compare& compare, const Allocator& allocator = Allocator{}) noexcept :
        map(compare, allocator) {}

    explicit Map(const Allocator& allocator) noexcept :
        map(allocator) {}

    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.empty();
    }

    [[nodiscard]] size_type size() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.size();
    }

    [[nodiscard]] size_type max_size() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.max_size();
    }

    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        map.clear();
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.find(key) != map.end();
    }

    [[nodiscard]] size_type count(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.count(key);
    }

    [[nodiscard]] key_compare key_comp() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.key_comp();
    }

    [[nodiscard]] value_compare value_comp() const noexcept {
        std::shared_lock sharedLock{mutex};

        return value_compare{map.key_comp()};
    }

    [[nodiscard]] allocator_type get_allocator() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.get_allocator();
    }

    [[nodiscard]] read_access_type read() const noexcept { return read_access_type{mutex, map}; }

    [[nodiscard]] write_access_type write() noexcept { return write_access_type{mutex, map}; }

   private:
    mutable Mutex mutex;

    map_type map;
};

}  // namespace DON

#endif  // UTILITY_MAP_H_INCLUDED
