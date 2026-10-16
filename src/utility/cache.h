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

#ifndef UTILITY_CACHE_H_INCLUDED
#define UTILITY_CACHE_H_INCLUDED

#include <cassert>
#include <array>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../utility/types.h"

namespace DON {

template<typename Key, typename Value, typename Mutex = std::shared_mutex, typename Return = Value>
class LRUCache final {
    static constexpr usize ThresholdSize = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::shared_ptr<Value>>;

    using list_value_type = std::pair<Key, storage_value_type>;
    using list_type       = std::list<list_value_type>;
    using list_iterator   = typename list_type::iterator;
    using map_type        = std::unordered_map<Key, list_iterator>;

   public:
    explicit LRUCache(const usize cap        = 2 * KB,
                      const usize reserveCnt = 1 * KB,
                      const float maxLoadFac = 0.75F) :
        capacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac) {
        configure();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) {
        // Fast path: shared lock for an existing key.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto mapItr = nolock_find(key); mapItr != nolock_map_end())
            {
                // Release the shared lock before acquiring the exclusive lock.
                sharedLock.unlock();

                std::lock_guard lockGuard{mutex};

                return nolock_read(
                  nolock_write_touch(mapItr->second, key, std::forward<Args>(args)...));
            }
        }

        // Slow path: exclusive lock to insert a missing key.
        std::lock_guard lockGuard{mutex};

        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) {
        // Fast path: shared lock for an existing key.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto mapItr = nolock_find(key); mapItr != nolock_map_end())
            {
                // Release the shared lock before acquiring the exclusive lock.
                sharedLock.unlock();

                std::lock_guard lockGuard{mutex};

                return nolock_read(nolock_write_touch_with(mapItr->second, key,
                                                           std::forward<Builder>(builder),
                                                           std::forward<Args>(args)...));
            }
        }

        // Slow path: exclusive lock to insert a missing key.
        std::lock_guard lockGuard{mutex};

        return nolock_write_with(key, std::forward<Builder>(builder), std::forward<Args>(args)...);
    }

    void evict_excess(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_evict_excess(size);
    }

    void evict_excess() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_evict_excess(capacity);
    }

    [[nodiscard]] usize get_capacity() const noexcept {
        std::shared_lock sharedLock{mutex};

        return capacity;
    }

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;
        nolock_evict_excess(capacity);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return nolock_find(key) != nolock_map_end();
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] usize size() const noexcept {
        std::shared_lock sharedLock{mutex};
        assert(map.size() == list.size());

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};
        assert(map.empty() == list.empty());

        return list.empty();
    }

    bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();
    }

    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_list_end() noexcept { return list.end(); }

    auto nolock_list_pos() noexcept { return list.begin(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nolock_list_end());

        list.splice(nolock_list_pos(), list, listItr);

        return listItr;
    }

    template<typename... Args>
    list_iterator
    nolock_write_touch(list_iterator listItr, const Key& key, Args&&... args) noexcept {
        if (listItr == nolock_list_end())
        {
            nolock_write(key, std::forward<Args>(args)...);

            return nolock_find(key)->second;
        }

        list.splice(nolock_list_pos(), list, listItr);

        return listItr;
    }

    template<typename Builder, typename... Args>
    list_iterator nolock_write_touch_with(list_iterator listItr,
                                          const Key&    key,
                                          Builder&&     builder,
                                          Args&&... args) noexcept {
        if (listItr == nolock_list_end())
        {
            nolock_write_with(key, std::forward<Builder>(builder), std::forward<Args>(args)...);

            return nolock_find(key)->second;
        }

        list.splice(nolock_list_pos(), list, listItr);

        return listItr;
    }

    Return nolock_read(list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= ThresholdSize)
            return listItr->second;
        else
            return *listItr->second;
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        if constexpr (sizeof(Value) <= ThresholdSize)
            mapItr->second = list.emplace(nolock_list_pos(),  //
                                          key,                //
                                          std::forward<Args>(args)...);
        else
            mapItr->second = list.emplace(nolock_list_pos(),        //
                                          key,                      //
                                          std::make_shared<Value>(  //
                                            std::forward<Args>(args)...));
        assert(capacity != 0);
        nolock_evict_excess(capacity);

        return nolock_read(mapItr->second);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        if constexpr (sizeof(Value) <= ThresholdSize)
            mapItr->second = list.emplace(nolock_list_pos(),      //
                                          key,                    //
                                          std::forward<Builder>(  //
                                            builder)(std::forward<Args>(args)...));
        else
            mapItr->second = list.emplace(nolock_list_pos(),        //
                                          key,                      //
                                          std::make_shared<Value>(  //
                                            std::forward<Builder>(  //
                                              builder)(std::forward<Args>(args)...)));
        assert(capacity != 0);
        nolock_evict_excess(capacity);

        return nolock_read(mapItr->second);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        const auto listItr = mapItr->second;

        map.erase(mapItr);
        list.erase(listItr);
    }

    void nolock_evict_excess(const usize size) noexcept {

        for (auto listSize = list.size(); listSize > size; --listSize)
        {
            const auto mapItr = nolock_find(std::prev(nolock_list_end())->first);
            assert(mapItr != nolock_map_end());

            nolock_erase(mapItr);
        }
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = nolock_find(key);

        if (mapItr == nolock_map_end())
            return false;

        nolock_erase(mapItr);

        return true;
    }

    void nolock_clear() noexcept {
        map.clear();
        list.clear();
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    usize       capacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    map_type      map;
    list_type     list;
};

template<typename Key,
         typename Value,
         usize ShardCount = 16,
         typename Mutex   = std::shared_mutex,
         typename Return  = Value>
class SharedLRUCache final {
    static_assert(ShardCount != 0, "ShardCount has to be != 0");
    static_assert(is_power_of_2(ShardCount), "ShardCount has to be power of 2");

    static constexpr usize ThresholdSize = 128;
    static constexpr usize ShardMask     = ShardCount - 1;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::shared_ptr<Value>>;

    using list_value_type = std::pair<Key, storage_value_type>;
    using list_type       = std::list<list_value_type>;
    using list_iterator   = typename list_type::iterator;
    using map_type        = std::unordered_map<Key, list_iterator>;

    struct Shard final {
        mutable Mutex mutex;
        map_type      map;
        list_type     list;
        usize         capacity;

        template<typename... Args>
        Return read_or_write(const Key& key, Args&&... args) noexcept {
            // Fast path: shared lock for an existing key.
            {
                std::shared_lock sharedLock{mutex};

                if (const auto mapItr = nolock_find(key); mapItr != nolock_map_end())
                {
                    // Release the shared lock before acquiring the exclusive lock.
                    sharedLock.unlock();

                    std::lock_guard lockGuard{mutex};

                    return nolock_read(
                      nolock_write_touch(mapItr->second, key, std::forward<Args>(args)...));
                }
            }

            // Slow path: exclusive lock to insert a missing key.
            std::lock_guard lockGuard{mutex};

            return nolock_write(key, std::forward<Args>(args)...);
        }

        template<typename Builder, typename... Args>
        Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
            // Fast path: shared lock for an existing key.
            {
                std::shared_lock sharedLock{mutex};

                if (const auto mapItr = nolock_find(key); mapItr != nolock_map_end())
                {
                    // Release the shared lock before acquiring the exclusive lock.
                    sharedLock.unlock();

                    std::lock_guard lockGuard{mutex};

                    return nolock_read(nolock_write_touch_with(mapItr->second, key,
                                                               std::forward<Builder>(builder),
                                                               std::forward<Args>(args)...));
                }
            }

            // Slow path: exclusive lock to insert a missing key.
            std::lock_guard lockGuard{mutex};

            return nolock_write_with(key, std::forward<Builder>(builder),
                                     std::forward<Args>(args)...);
        }

        void evict_excess(const usize size) noexcept {
            std::lock_guard lockGuard{mutex};

            nolock_evict_excess(size);
        }

        bool contains(const Key& key) const noexcept {
            std::shared_lock lockGuard{mutex};

            return nolock_find(key) != nolock_map_end();
        }

        bool remove(const Key& key) noexcept {
            std::lock_guard lockGuard{mutex};

            return nolock_remove(key);
        }

        void clear() noexcept {
            std::lock_guard lockGuard{mutex};

            nolock_clear();
        }

        usize size() const noexcept {
            std::shared_lock lockGuard{mutex};

            return list.size();
        }

        bool empty() const noexcept {
            std::shared_lock lockGuard{mutex};

            return list.empty();
        }

        void configure(const usize shardCapacity,
                       const usize reserveCnt,
                       const float maxLoadFac) noexcept {
            std::lock_guard lockGuard{mutex};

            capacity = shardCapacity;
            nolock_configure(reserveCnt, maxLoadFac);
            nolock_evict_excess(capacity);
        }

       private:
        list_iterator nolock_touch(list_iterator listItr) noexcept {
            assert(listItr != nolock_list_end());

            list.splice(nolock_list_pos(), list, listItr);

            return listItr;
        }

        template<typename... Args>
        list_iterator
        nolock_write_touch(list_iterator listItr, const Key& key, Args&&... args) noexcept {
            if (listItr == nolock_list_end())
            {
                nolock_write(key, std::forward<Args>(args)...);

                return nolock_find(key)->second;
            }

            list.splice(nolock_list_pos(), list, listItr);

            return listItr;
        }

        template<typename Builder, typename... Args>
        list_iterator nolock_write_touch_with(list_iterator listItr,
                                              const Key&    key,
                                              Builder&&     builder,
                                              Args&&... args) noexcept {
            if (listItr == nolock_list_end())
            {
                nolock_write_with(key, std::forward<Builder>(builder), std::forward<Args>(args)...);

                return nolock_find(key)->second;
            }

            list.splice(nolock_list_pos(), list, listItr);

            return listItr;
        }

        Return nolock_read(list_iterator listItr) const noexcept {
            if constexpr (sizeof(Value) <= ThresholdSize)
                return listItr->second;
            else
                return *listItr->second;
        }

        template<typename... Args>
        Return nolock_write(const Key& key, Args&&... args) noexcept {
            if (capacity == 0)
                return Return{std::forward<Args>(args)...};

            const auto [itr, inserted] = map.try_emplace(key, nolock_list_end());

            if (!inserted)
                return nolock_read(nolock_touch(itr->second));

            if constexpr (sizeof(Value) <= ThresholdSize)
                itr->second = list.emplace(nolock_list_pos(),  //
                                           key,                //
                                           std::forward<Args>(args)...);
            else
                itr->second = list.emplace(nolock_list_pos(),  //
                                           key,                //
                                           std::make_shared<Value>(std::forward<Args>(args)...));

            nolock_evict_excess(capacity);

            return nolock_read(itr->second);
        }

        template<typename Builder, typename... Args>
        Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
            if (capacity == 0)
                return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            const auto [itr, inserted] = map.try_emplace(key, nolock_list_end());

            if (!inserted)
                return nolock_read(nolock_touch(itr->second));

            if constexpr (sizeof(Value) <= ThresholdSize)
                itr->second = list.emplace(nolock_list_pos(),      //
                                           key,                    //
                                           std::forward<Builder>(  //
                                             builder)(std::forward<Args>(args)...));
            else
                itr->second = list.emplace(nolock_list_pos(),                 //
                                           key,                               //
                                           std::make_shared<Value>(           //
                                             std::forward<Builder>(builder)(  //
                                               std::forward<Args>(args)...)));

            nolock_evict_excess(capacity);

            return nolock_read(itr->second);
        }

        auto nolock_map_end() const noexcept { return map.end(); }

        auto nolock_list_end() noexcept { return list.end(); }

        auto nolock_list_pos() noexcept { return list.begin(); }

        auto nolock_find(const Key& key) noexcept { return map.find(key); }
        auto nolock_find(const Key& key) const noexcept { return map.find(key); }

        void nolock_erase(typename map_type::iterator mapItr) noexcept {
            list.erase(mapItr->second);
            map.erase(mapItr);
        }

        void nolock_evict_excess(const usize size) noexcept {
            for (auto listSize = list.size(); listSize > size; --listSize)
            {
                const auto mapItr = nolock_find(std::prev(nolock_list_end())->first);
                assert(mapItr != nolock_map_end());

                nolock_erase(mapItr);
            }
        }

        bool nolock_remove(const Key& key) noexcept {
            const auto itr = nolock_find(key);

            if (itr == nolock_map_end())
                return false;

            nolock_erase(itr);

            return true;
        }

        void nolock_clear() noexcept {
            map.clear();
            list.clear();
        }

        void nolock_configure(const usize reserveCnt, const float maxLoadFac) noexcept {
            map.max_load_factor(maxLoadFac);
            map.reserve(reserveCnt);
        }
    };

    using shard_array_type = std::array<Shard, ShardCount>;

   public:
    explicit SharedLRUCache(const usize cap        = 2 * KB,
                            const usize reserveCnt = 1 * KB,
                            const float maxLoadFac = 0.75F) :
        capacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac) {
        configure();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) {
        return nolock_shard(key).read_or_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) {
        return nolock_shard(key).read_or_write_with(key, std::forward<Builder>(builder),
                                                    std::forward<Args>(args)...);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        return nolock_shard(key).contains(key);
    }

    bool remove(const Key& key) noexcept { return nolock_shard(key).remove(key); }

    void evict_excess(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        for (auto& shard : shards)
            shard.evict_excess(ceil_div(size, ShardCount));
    }

    void evict_excess() noexcept { evict_excess(get_capacity()); }

    void set_capacity(const usize cap) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = cap;

        for (auto& shard : shards)
            shard.configure(ceil_div(capacity, ShardCount), ceil_div(reserveCount, ShardCount),
                            maxLoadFactor);
    }

    [[nodiscard]] usize get_capacity() const noexcept {
        std::lock_guard lockGuard{mutex};

        return capacity;
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

    void clear() noexcept {
        for (auto& shard : shards)
            shard.clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        for (auto& shard : shards)
            shard.configure(ceil_div(capacity, ShardCount), ceil_div(reserveCount, ShardCount),
                            maxLoadFactor);
    }

    void reset() noexcept {
        clear();
        configure();
    }

   private:
    static usize hash_key(const Key& key) noexcept { return std::hash<Key>{}(key); }

    [[nodiscard]] Shard& nolock_shard(const Key& key) noexcept {
        return shards[hash_key(key) & ShardMask];
    }

    [[nodiscard]] const Shard& nolock_shard(const Key& key) const noexcept {
        return shards[hash_key(key) & ShardMask];
    }

    mutable Mutex mutex;
    usize         capacity;
    const usize   reserveCount;
    const float   maxLoadFactor;

    shard_array_type shards;
};

}  // namespace DON

#endif  // UTILITY_CACHE_H_INCLUDED
