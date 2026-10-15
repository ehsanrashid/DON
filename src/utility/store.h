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

#ifndef UTILITY_STORE_H_INCLUDED
#define UTILITY_STORE_H_INCLUDED

#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "../utility/types.h"

namespace DON {

template<typename Key, typename Value, typename Map, typename Mutex = std::shared_mutex>
class Store final {
   public:
    using key_type        = Key;
    using mapped_type     = Value;
    using value_type      = typename Map::value_type;
    using size_type       = usize;
    using difference_type = std::ptrdiff_t;
    using map_type        = Map;

    explicit Store(usize reserveCnt = 1 * KB, float maxLoadFac = 0.75f) noexcept {
        configure_map(map, reserveCnt, maxLoadFac, 0);
    }

    // Returns the existing value or inserts a default-constructed value.
    Value& get(const Key& key) noexcept {
        // Fast path: Finds the existing value under a shared lock.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto itr = map.find(key); itr != map.end())
                return itr->second;
        }

        // Slow path: Acquires an exclusive lock and inserts the value if absent.
        std::lock_guard lockGuard{mutex};

        return map.try_emplace(key).first->second;
    }

    // Returns the existing value or constructs a new value from the arguments.
    template<typename... Args>
    Value& get(const Key& key, Args&&... args) noexcept {
        // Fast path: Finds the existing value under a shared lock.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto itr = map.find(key); itr != map.end())
                return itr->second;
        }

        // Slow path: Acquires an exclusive lock and inserts the value if absent.
        std::lock_guard lockGuard{mutex};

        return map.try_emplace(key, std::forward<Args>(args)...).first->second;
    }

    // Returns true if the map contains the key
    bool contains(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.find(key) != map.end();
    }

    // Returns the number of entries in the map
    usize size() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.size();
    }

    // Returns true if the map contains no entries
    bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.empty();
    }

    // Removes the entry associated with the key.
    bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return map.erase(key) != 0;
    }

    // Removes all entries from the map
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        map.clear();
    }

   private:
    Store(const Store&) noexcept            = delete;
    Store& operator=(const Store&) noexcept = delete;
    Store(Store&&) noexcept                 = delete;
    Store& operator=(Store&&) noexcept      = delete;

    template<typename T>
    static auto configure_map(T& map, usize reserveCount, float maxLoadFactor, int) noexcept
      -> decltype(map.max_load_factor(maxLoadFactor), map.reserve(reserveCount), void()) {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    static void configure_map(Map&, usize, float, long) noexcept {}

    mutable Mutex mutex;

    Map map;
};

}  // namespace DON

#endif  // UTILITY_STORE_H_INCLUDED
