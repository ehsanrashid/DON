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

#ifndef UTILITY_SHARDED_CACHE_H_INCLUDED
#define UTILITY_SHARDED_CACHE_H_INCLUDED

#include <array>
#include <functional>
#include <mutex>
#include <utility>

#include "../utility/types.h"

namespace DON {

template<typename Key,
         typename Value,
         template<typename, typename, typename, typename> class Cache,
         usize ShardCount = 16,
         typename Mutex   = std::mutex,
         typename Return  = Value>
class ShardedCache final {
    static_assert(ShardCount != 0, "ShardCount must be != 0");
    static_assert(is_power_of_2(ShardCount), "ShardCount has to be power of 2");

    static constexpr usize ShardMask = ShardCount - 1;

    using cache_type = Cache<Key, Value, Mutex, Return>;

    template<usize... I>
    static Array<cache_type, ShardCount> make_shards(std::index_sequence<I...>,
                                                     const usize cap,
                                                     const usize reserveCnt,
                                                     const float maxLoadFac) {
        return {(static_cast<void>(I), cache_type{cap, reserveCnt, maxLoadFac})...};
    }

    [[nodiscard]] static constexpr usize shard_capacity(const usize cap) noexcept {
        return ceil_div(cap, ShardCount);
    }

    [[nodiscard]] static constexpr usize total_capacity(const usize cap) noexcept {
        return shard_capacity(cap) * ShardCount;
    }

    [[nodiscard]] usize shard_index(const Key& key) const noexcept {
        return (std::hash<Key>{}(key)) & ShardMask;
    }

   public:
    explicit ShardedCache(const usize cap        = 2 * KB,
                          const usize reserveCnt = 1 * KB,
                          const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(total_capacity(cap)),
        capacity(defaultCapacity),
        shards(make_shards(std::make_index_sequence<ShardCount>{},
                           shard_capacity(cap),
                           shard_capacity(reserveCnt),
                           maxLoadFac)) {}

    ShardedCache(const ShardedCache&) noexcept            = delete;
    ShardedCache& operator=(const ShardedCache&) noexcept = delete;
    ShardedCache(ShardedCache&&) noexcept                 = delete;
    ShardedCache& operator=(ShardedCache&&) noexcept      = delete;

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        return shards[shard_index(key)].read_or_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        return shards[shard_index(key)].read_or_write_with(key, std::forward<Builder>(builder),
                                                           std::forward<Args>(args)...);
    }

    void evict_excess(const usize size) noexcept {
        const usize cap = shard_capacity(size);

        for (auto& shard : shards)
            shard.evict_excess(cap);
    }

    void evict_excess() noexcept {
        for (auto& shard : shards)
            shard.evict_excess();
    }

    [[nodiscard]] usize get_capacity() const noexcept {
        std::lock_guard lockGuard{mutex};

        return capacity;
    }

    void set_capacity(const usize newCapacity) noexcept {
        const usize cap = shard_capacity(newCapacity);

        for (auto& shard : shards)
            shard.set_capacity(cap);

        std::lock_guard lockGuard{mutex};

        capacity = cap * ShardCount;
    }

    // The returned iterator does not retain the underlying shard's lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept { return shards[shard_index(key)].find(key); }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        return shards[shard_index(key)].find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        return shards[shard_index(key)].contains(key);
    }

    [[nodiscard]] usize size() const noexcept {
        usize count = 0;

        for (const auto& shard : shards)
            count += shard.size();

        return count;
    }

    [[nodiscard]] bool empty() const noexcept {
        for (const auto& shard : shards)
            if (!shard.empty())
                return false;

        return true;
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        return shards[shard_index(key)].remove(key);
    }

    void clear() noexcept {
        for (auto& shard : shards)
            shard.clear();
    }

    void configure() noexcept {
        for (auto& shard : shards)
            shard.configure();
    }

    void reset() noexcept {
        for (auto& shard : shards)
            shard.reset();

        std::lock_guard lockGuard{mutex};

        capacity = defaultCapacity;
    }

   private:
    const usize defaultCapacity;

    mutable std::mutex mutex;
    usize              capacity;

    Array<cache_type, ShardCount> shards;
};

}  // namespace DON

#endif  // UTILITY_SHARDED_CACHE_H_INCLUDED
