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

#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "../utility/types.h"

namespace DON {

template<typename Key,
         typename Value,
         typename Map   = std::unordered_map<Key, Value>,
         typename Mutex = std::shared_mutex>
class Store final {
   public:
    using key_type        = Key;
    using mapped_type     = Value;
    using value_type      = typename Map::value_type;
    using size_type       = usize;
    using difference_type = std::ptrdiff_t;
    using map_type        = Map;

    explicit Store(usize reserveCnt = 1 * KB, float maxLoadFac = 0.75f) {
        configure_map(map, reserveCnt, maxLoadFac, 0);
    }
    Store(const Store&) noexcept            = delete;
    Store& operator=(const Store&) noexcept = delete;
    Store(Store&&) noexcept                 = delete;
    Store& operator=(Store&&) noexcept      = delete;

    // 1. Default-construct Value if the key is absent.
    [[nodiscard]] Value& read_or_write_default(const Key& key) noexcept {
        // Fast path: check for an existing value under a shared lock.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto itr = map.find(key); itr != map.end())
                return read(itr->second);
        }

        // Slow path: acquire exclusive lock, then insert and construct if missing.
        std::lock_guard lockGuard{mutex};

        return write(key);
    }

    // 2. Construct Value from arguments if the key is absent.
    template<typename... Args>
    [[nodiscard]] Value& read_or_write(const Key& key, Args&&... args) noexcept {
        // Fast path: check for an existing value under a shared lock.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto itr = map.find(key); itr != map.end())
                return read(itr->second);
        }

        // Slow path: acquire exclusive lock, then insert and construct if missing.
        std::lock_guard lockGuard{mutex};

        return write(key, std::forward<Args>(args)...);
    }

    // 3. Construct Value using a builder if the key is absent.
    template<typename Builder, typename... Args>
    [[nodiscard]] Value&
    read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        // Fast path: check for an existing value under a shared lock.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto itr = map.find(key); itr != map.end())
                return read(itr->second);
        }

        // Slow path: acquire exclusive lock, then insert and construct if missing.
        std::lock_guard lockGuard{mutex};

        return write_with(key, std::forward<Builder>(builder), std::forward<Args>(args)...);
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
    template<typename T>
    static auto configure_map(T& map, usize reserveCount, float maxLoadFactor, int) noexcept
      -> decltype(map.max_load_factor(maxLoadFactor), map.reserve(reserveCount), void()) {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    [[nodiscard]] Value& read(Value& value) noexcept { return value; }

    template<typename... Args>
    [[nodiscard]] Value& write(const Key& key, Args&&... args) noexcept {
        return read(map.try_emplace(key, std::forward<Args>(args)...).first->second);
    }

    template<typename Builder, typename... Args>
    [[nodiscard]] Value& write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (const auto itr = map.find(key); itr != map.end())
            return read(itr->second);

        return write(key, std::forward<Builder>(builder)(std::forward<Args>(args)...));
    }

    static void configure_map(Map&, usize, float, long) noexcept {}

    mutable Mutex mutex;
    Map           map;
};

}  // namespace DON

#endif  // UTILITY_STORE_H_INCLUDED
