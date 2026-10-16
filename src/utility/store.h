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

#include <cassert>
#include <array>
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
class Store final {
    static constexpr usize ThresholdSize = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::shared_ptr<Value>>;

    using list_type     = std::list<storage_value_type>;
    using list_iterator = typename list_type::iterator;
    using map_type      = std::unordered_map<Key, list_iterator>;

   public:
    explicit Store(const usize reserveCnt = 1 * KB, const float maxLoadFac = 0.75F) :
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
                return nolock_read(*mapItr->second);
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
                return nolock_read(*mapItr->second);
        }

        // Slow path: exclusive lock to insert a missing key.
        std::lock_guard lockGuard{mutex};

        return nolock_write_with(key, std::forward<Builder>(builder), std::forward<Args>(args)...);
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

    auto nolock_find(const Key& key) const { return map.find(key); }

    Return nolock_read(storage_value_type& value) const noexcept {
        if constexpr (sizeof(Value) <= ThresholdSize)
            return value;
        else
            return *value;
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
            return nolock_read(*mapItr->second);

        if constexpr (sizeof(Value) <= ThresholdSize)
            mapItr->second = list.emplace(nolock_list_pos(),  //
                                          std::forward<Args>(args)...);
        else
            mapItr->second = list.emplace(nolock_list_pos(),        //
                                          std::make_shared<Value>(  //
                                            std::forward<Args>(args)...));

        return nolock_read(*mapItr->second);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
            return nolock_read(*mapItr->second);

        if constexpr (sizeof(Value) <= ThresholdSize)
            mapItr->second = list.emplace(nolock_list_pos(),      //
                                          std::forward<Builder>(  //
                                            builder)(std::forward<Args>(args)...));
        else
            mapItr->second = list.emplace(nolock_list_pos(),        //
                                          std::make_shared<Value>(  //
                                            std::forward<Builder>(  //
                                              builder)(std::forward<Args>(args)...)));

        return nolock_read(*mapItr->second);
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = nolock_find(key);

        if (mapItr == nolock_map_end())
            return false;

        const auto listItr = mapItr->second;

        map.erase(mapItr);
        list.erase(listItr);

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
class SharedStore final {
    static_assert(ShardCount != 0, "ShardCount has to be != 0");
    static_assert(is_power_of_2(ShardCount), "ShardCount has to be power of 2");

    static constexpr usize ThresholdSize = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::shared_ptr<Value>>;

    using list_type     = std::list<storage_value_type>;
    using list_iterator = typename list_type::iterator;
    using map_type      = std::unordered_map<Key, list_iterator>;

    struct Shard final {
       public:
        mutable Mutex mutex;
        map_type      map;
        list_type     list;

        bool remove(const Key& key) noexcept {
            std::lock_guard lockGuard{mutex};

            return nolock_remove(key);
        }

        void clear() noexcept {
            std::lock_guard lockGuard{mutex};

            nolock_clear();
        }

        void configure(const usize reserveCount, const float maxLoadFactor) noexcept {
            std::lock_guard lockGuard{mutex};

            nolock_configure(reserveCount, maxLoadFactor);
        }

        auto nolock_map_end() const noexcept { return map.end(); }

        auto nolock_list_end() noexcept { return list.end(); }

        auto nolock_list_pos() noexcept { return list.begin(); }

        auto nolock_find(const Key& key) const { return map.find(key); }

       private:
        bool nolock_remove(const Key& key) noexcept {
            const auto mapItr = nolock_find(key);

            if (mapItr == nolock_map_end())
                return false;

            const auto listItr = mapItr->second;

            map.erase(mapItr);
            list.erase(listItr);

            return true;
        }

        void nolock_clear() noexcept {
            map.clear();
            list.clear();
        }

        void nolock_configure(const usize reserveCount, const float maxLoadFactor) noexcept {
            map.max_load_factor(maxLoadFactor);
            map.reserve(reserveCount);
        }
    };

    using shard_array_type = std::array<Shard, ShardCount>;

   public:
    explicit SharedStore(const usize reserveCnt = 1 * KB, const float maxLoadFac = 0.75F) :
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac) {
        configure();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) {
        auto& shard = nolock_shard(key);

        // Fast path: shared lock for an existing key.
        {
            std::shared_lock sharedLock{shard.mutex};

            if (const auto mapItr = shard.nolock_find(key); mapItr != shard.nolock_map_end())
                return nolock_read(*mapItr->second);
        }

        // Slow path: exclusive lock to insert a missing key.
        std::lock_guard lockGuard{shard.mutex};

        return nolock_write(shard, key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) {
        auto& shard = nolock_shard(key);

        // Fast path: shared lock for an existing key.
        {
            std::shared_lock sharedLock{shard.mutex};

            if (const auto mapItr = shard.nolock_find(key); mapItr != shard.nolock_map_end())
                return nolock_read(*mapItr->second);
        }

        // Slow path: exclusive lock to insert a missing key.
        std::lock_guard lockGuard{shard.mutex};

        return nolock_write_with(shard, key, std::forward<Builder>(builder),
                                 std::forward<Args>(args)...);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        const auto& shard = shards[shard_index(key)];

        std::shared_lock sharedLock{shard.mutex};

        return shard.nolock_find(key) != shard.nolock_map_end();
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        const auto& shard = shards[shard_index(key)];

        std::shared_lock sharedLock{shard.mutex};

        return shard.nolock_find(key);
    }

    [[nodiscard]] usize size() const noexcept {
        usize size = 0;

        for (const auto& shard : shards)
        {
            std::shared_lock sharedLock{shard.mutex};
            assert(shard.map.size() == shard.list.size());

            size += shard.list.size();
        }

        return size;
    }

    [[nodiscard]] bool empty() const noexcept {
        for (const auto& shard : shards)
        {
            std::shared_lock sharedLock{shard.mutex};
            assert(shard.map.empty() == shard.list.empty());

            if (!shard.list.empty())
                return false;
        }

        return true;
    }

    bool remove(const Key& key) noexcept {
        auto& shard = nolock_shard(key);

        std::lock_guard lockGuard{shard.mutex};

        const auto mapItr = shard.nolock_find(key);

        if (mapItr == shard.nolock_map_end())
            return false;

        const auto listItr = mapItr->second;

        shard.map.erase(mapItr);
        shard.list.erase(listItr);

        return true;
    }

    void clear() noexcept {
        for (auto& shard : shards)
        {
            std::lock_guard lockGuard{shard.mutex};

            shard.map.clear();
            shard.list.clear();
        }
    }

    void configure() noexcept {
        for (auto& shard : shards)
            shard.configure(reserveCount, maxLoadFactor);
    }

    void reset() noexcept {
        clear();

        configure();
    }

   private:
    static constexpr usize ShardMask = ShardCount - 1;

    // General-purpose key hasher.
    static usize hash_key(const Key& key) noexcept { return std::hash<Key>{}(key); }

    // Select the shard associated with the key.
    static usize shard_index(const Key& key) noexcept { return hash_key(key) & ShardMask; }

    [[nodiscard]] Shard& nolock_shard(const Key& key) noexcept { return shards[shard_index(key)]; }

    [[nodiscard]] const Shard& nolock_shard(const Key& key) const noexcept {
        return shards[shard_index(key)];
    }

    Return nolock_read(storage_value_type& value) const noexcept {
        if constexpr (sizeof(Value) <= ThresholdSize)
            return value;
        else
            return *value;
    }

    template<typename... Args>
    Return nolock_write(Shard& shard, const Key& key, Args&&... args) noexcept {
        const auto [mapItr, inserted] = shard.map.try_emplace(key, shard.nolock_list_end());

        if (!inserted)
            return nolock_read(*mapItr->second);

        if constexpr (sizeof(Value) <= ThresholdSize)
            mapItr->second = shard.list.emplace(shard.nolock_list_pos(),  //
                                                std::forward<Args>(args)...);
        else
            mapItr->second = shard.list.emplace(shard.nolock_list_pos(),  //
                                                std::make_shared<Value>(  //
                                                  std::forward<Args>(args)...));

        return nolock_read(*mapItr->second);
    }

    template<typename Builder, typename... Args>
    Return
    nolock_write_with(Shard& shard, const Key& key, Builder&& builder, Args&&... args) noexcept {
        const auto [mapItr, inserted] = shard.map.try_emplace(key, shard.nolock_list_end());

        if (!inserted)
            return nolock_read(*mapItr->second);

        if constexpr (sizeof(Value) <= ThresholdSize)
            mapItr->second = shard.list.emplace(shard.nolock_list_pos(),  //
                                                std::forward<Builder>(    //
                                                  builder)(std::forward<Args>(args)...));
        else
            mapItr->second = shard.list.emplace(shard.nolock_list_pos(),  //
                                                std::make_shared<Value>(  //
                                                  std::forward<Builder>(  //
                                                    builder)(std::forward<Args>(args)...)));

        return nolock_read(*mapItr->second);
    }

    const usize reserveCount;
    const float maxLoadFactor;

    shard_array_type shards;
};

}  // namespace DON

#endif  // UTILITY_STORE_H_INCLUDED
