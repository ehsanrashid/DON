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

#ifndef UTILITY_LFU_CACHE_H_INCLUDED
#define UTILITY_LFU_CACHE_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../utility/list.h"
#include "../utility/pool.h"
#include "../utility/types.h"
#include "../utility/ankerl/unordered_dense.h"

namespace DON {

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class ListLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    // Entries are ordered by descending frequency, with MRU first for equal frequencies.
    using list_value_type = struct Entry {
        Key                key;
        storage_value_type value;
        usize              frequency = 1;
    };

    using list_type      = std::pmr::list<Entry>;
    using list_iterator  = typename list_type::iterator;
    using list_pool_type = std::pmr::unsynchronized_pool_resource;
    using map_type       = std::unordered_map<Key, list_iterator>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit ListLFUCache(const usize cap        = 2 * KB,
                          const usize reserveCnt = 1 * KB,
                          const float maxLoadFac = 0.75F) :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap),
        listPool(),
        list(&listPool) {
        configure();
    }

    ListLFUCache(const ListLFUCache&) noexcept            = delete;
    ListLFUCache& operator=(const ListLFUCache&) noexcept = delete;
    ListLFUCache(ListLFUCache&&) noexcept                 = delete;
    ListLFUCache& operator=(ListLFUCache&&) noexcept      = delete;

    ~ListLFUCache() noexcept {
        std::lock_guard lockGuard{mutex};
        nolock_clear();
        nolock_release_pool();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
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
        std::lock_guard lockGuard{mutex};

        return capacity;
    }

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;
        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock;
    // callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != nolock_map_end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == list.size());

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == list.empty());

        return list.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
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

        nolock_release_pool();

        map.rehash(0);

        capacity = defaultCapacity;
        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }
    auto nolock_list_end() noexcept { return list.end(); }
    auto nolock_list_pos() noexcept { return list.begin(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != list.end());

        if (listItr->frequency < std::numeric_limits<usize>::max())
            ++listItr->frequency;

        // Move before entries with an equal frequency (MRU-first),
        // but after entries with a higher frequency.
        auto pos = listItr;

        while (pos != list.begin())
        {
            const auto prev = std::prev(pos);

            if (prev->frequency > listItr->frequency)
                break;

            pos = prev;
        }

        list.splice(pos, list, listItr);

        return listItr;
    }

    Return nolock_read(list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return listItr->value;
        else
            return *listItr->value;
    }

    template<typename... Args>
    static storage_value_type make_storage(Args&&... args) {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return Value(std::forward<Args>(args)...);
        else
            return std::make_shared<Value>(std::forward<Args>(args)...);
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        return nolock_insert(key, [&] { return make_storage(std::forward<Args>(args)...); });
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        return nolock_insert(key, [&] {
            return make_storage(std::forward<Builder>(builder)(std::forward<Args>(args)...));
        });
    }

    list_iterator nolock_frequency_pos(const usize frequency = 1) noexcept {
        auto listItr = list.begin();

        while (listItr != list.end() && listItr->frequency > frequency)
            ++listItr;

        return listItr;
    }

    template<typename CreateStorage>
    Return nolock_insert(const Key& key, CreateStorage&& createStorage) noexcept {
        const auto [mapItr, inserted] = map.try_emplace(key, list.end());

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard           mapGuard{map, mapItr};
        storage_value_type value = std::forward<CreateStorage>(createStorage)();

        if (!list.empty() && list.size() >= capacity)
        {
            // Reuse the least-frequent, least-recently-used entry's node.
            const auto listItr   = std::prev(list.end());
            const auto oldMapItr = nolock_find(listItr->key);
            assert(oldMapItr != nolock_map_end());

            map.erase(oldMapItr);

            std::destroy_at(std::addressof(*listItr));
            new (static_cast<void*>(std::addressof(*listItr))) Entry{key, std::move(value), 1};

            // Insert the new frequency-1 entry in the correct position.
            list.splice(nolock_frequency_pos(), list, listItr);

            mapItr->second = listItr;
        }
        else
        {
            mapItr->second = list.emplace(nolock_frequency_pos(), Entry{key, std::move(value), 1});
        }

        mapGuard.commit();
        nolock_evict_excess(capacity);

        return nolock_read(mapItr->second);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        const auto listItr = mapItr->second;
        map.erase(mapItr);
        list.erase(listItr);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            const auto mapItr = nolock_find(list.back().key);
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

    void nolock_release_pool() noexcept { listPool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex  mutex;
    usize          capacity;
    map_type       map;
    list_pool_type listPool;
    list_type      list;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class SetLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry {
        storage_value_type value;
        usize              frequency = 1;
        std::uint64_t      recency   = 0;
        const Key*         key       = nullptr;
    };

    using map_type = std::unordered_map<Key, Entry>;

    struct EntryCompare {
        bool operator()(const Entry* lhs, const Entry* rhs) const noexcept {
            if (lhs->frequency != rhs->frequency)
                return lhs->frequency > rhs->frequency;

            return lhs->recency > rhs->recency;
        }
    };

    using set_type = std::set<Entry*, EntryCompare>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit SetLFUCache(const usize cap        = 2 * KB,
                         const usize reserveCnt = 1 * KB,
                         const float maxLoadFac = 0.75F) :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    SetLFUCache(const SetLFUCache&) noexcept            = delete;
    SetLFUCache& operator=(const SetLFUCache&) noexcept = delete;
    SetLFUCache(SetLFUCache&&) noexcept                 = delete;
    SetLFUCache& operator=(SetLFUCache&&) noexcept      = delete;

    ~SetLFUCache() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
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
        std::lock_guard lockGuard{mutex};

        return capacity;
    }

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;

        nolock_evict_excess(capacity);
    }

    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != map.end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == entries.size());

        return map.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == entries.empty());

        return map.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
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

        map.rehash(0);

        capacity = defaultCapacity;

        recencyCounter = 0;

        nolock_configure();
    }

   private:
    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    std::uint64_t nolock_next_recency() noexcept {
        if (recencyCounter == std::numeric_limits<std::uint64_t>::max())
            nolock_renumber_recency();

        return ++recencyCounter;
    }

    void nolock_renumber_recency() noexcept {
        // Preserve the current order while rebuilding the sequence numbers.
        std::vector<Entry*> orderedEntries;
        orderedEntries.reserve(entries.size());

        for (Entry* const entry : entries)
            orderedEntries.push_back(entry);

        entries.clear();

        for (usize i = 0; i < orderedEntries.size(); ++i)
        {
            orderedEntries[i]->recency = orderedEntries.size() - i;
            entries.insert(orderedEntries[i]);
        }

        recencyCounter = orderedEntries.size();
    }

    void nolock_touch(Entry& entry) noexcept {
        // Ordering fields must not change while the entry is in the set.
        entries.erase(&entry);

        if (entry.frequency < std::numeric_limits<usize>::max())
            ++entry.frequency;

        entry.recency = nolock_next_recency();

        entries.insert(&entry);
    }

    Return nolock_read(const Entry& entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return entry.value;
        else
            return *entry.value;
    }

    template<typename... Args>
    static storage_value_type make_storage(Args&&... args) {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return Value(std::forward<Args>(args)...);
        else
            return std::make_shared<Value>(std::forward<Args>(args)...);
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        return nolock_insert(key, [&] { return make_storage(std::forward<Args>(args)...); });
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        return nolock_insert(key, [&] {
            return make_storage(std::forward<Builder>(builder)(std::forward<Args>(args)...));
        });
    }

    template<typename CreateStorage>
    Return nolock_insert(const Key& key, CreateStorage&& createStorage) noexcept {
        if (auto mapItr = nolock_find(key); mapItr != map.end())
        {
            nolock_touch(mapItr->second);
            return nolock_read(mapItr->second);
        }

        storage_value_type value = std::forward<CreateStorage>(createStorage)();

        const auto [mapItr, inserted] =
          map.try_emplace(key, Entry{std::move(value), 1, 0, nullptr});

        assert(inserted);

        MapGuard mapGuard{map, mapItr};

        Entry& entry  = mapItr->second;
        entry.key     = std::addressof(mapItr->first);
        entry.recency = nolock_next_recency();

        entries.insert(std::addressof(entry));

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        entries.erase(std::addressof(mapItr->second));
        map.erase(mapItr);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (map.size() > size)
        {
            assert(!entries.empty());

            // Highest frequency / newest entry is at the front.
            // Evict the lowest frequency / oldest entry at the back.
            const auto setItr = std::prev(entries.end());
            const auto mapItr = nolock_find(*(*setItr)->key);

            assert(mapItr != map.end());

            nolock_erase(mapItr);
        }
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = nolock_find(key);

        if (mapItr == map.end())
            return false;

        nolock_erase(mapItr);

        return true;
    }

    void nolock_clear() noexcept {
        entries.clear();
        map.clear();
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type map;
    set_type entries;

    u64 recencyCounter = 0;
};


template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class BucketLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry {
        Key                key;
        storage_value_type value;
        usize              frequency = 1;
        Entry*             prev      = nullptr;
        Entry*             next      = nullptr;
    };

    using list_type      = std::pmr::list<Entry>;
    using list_iterator  = typename list_type::iterator;
    using list_pool_type = std::pmr::unsynchronized_pool_resource;
    using map_type       = std::unordered_map<Key, list_iterator>;

    struct Bucket {
        DoublyIntrusiveList<Entry> entries;
    };

    using bucket_map_type = std::unordered_map<usize, std::unique_ptr<Bucket>>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit BucketLFUCache(const usize cap        = 2 * KB,
                            const usize reserveCnt = 1 * KB,
                            const float maxLoadFac = 0.75F) :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap),
        listPool(),
        list(&listPool) {
        configure();
    }

    BucketLFUCache(const BucketLFUCache&) noexcept            = delete;
    BucketLFUCache& operator=(const BucketLFUCache&) noexcept = delete;
    BucketLFUCache(BucketLFUCache&&) noexcept                 = delete;
    BucketLFUCache& operator=(BucketLFUCache&&) noexcept      = delete;

    ~BucketLFUCache() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        nolock_release_pool();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
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
        std::lock_guard lockGuard{mutex};

        return capacity;
    }

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock;
    // callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != map.end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == list.size());

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == list.empty());

        return list.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
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
        nolock_release_pool();

        map.rehash(0);
        buckets.rehash(0);

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_find(const Key& key) noexcept { return map.find(key); }

    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    void nolock_update_frequency_bounds() noexcept {
        if (buckets.empty())
        {
            minFrequency = 0;
            maxFrequency = 0;
            return;
        }

        minFrequency = std::numeric_limits<usize>::max();
        maxFrequency = 0;

        for (const auto& [frequency, bucket] : buckets)
        {
            if (bucket->entries.empty())
                continue;

            if (minFrequency > frequency)
                minFrequency = frequency;
            if (maxFrequency < frequency)
                maxFrequency = frequency;
        }

        assert(minFrequency != std::numeric_limits<usize>::max());
    }

    Bucket& nolock_bucket(const usize frequency) {
        if (auto itr = buckets.find(frequency); itr != buckets.end())
            return *itr->second;

        auto        bucket    = std::make_unique<Bucket>();
        auto* const bucketPtr = bucket.get();

        buckets.emplace(frequency, std::move(bucket));

        return *bucketPtr;
    }

    void nolock_erase_bucket(const usize frequency) noexcept {
        const auto bucketItr = buckets.find(frequency);
        assert(bucketItr != buckets.end());
        assert(bucketItr->second->entries.empty());

        buckets.erase(bucketItr);

        if (minFrequency == frequency || frequency == maxFrequency)
            nolock_update_frequency_bounds();
    }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != list.end());

        auto&       entry        = *listItr;
        const usize oldFrequency = entry.frequency;

        auto bucketItr = buckets.find(oldFrequency);
        assert(bucketItr != buckets.end());

        auto& oldBucket = *bucketItr->second;

        // At maximum frequency, only update recency.
        if (oldFrequency == std::numeric_limits<usize>::max())
        {
            const bool moved = oldBucket.entries.move_back(entry);
            assert(moved);
            (void) moved;

            return listItr;
        }

        const bool erased = oldBucket.entries.erase(entry);
        assert(erased);
        (void) erased;

        const bool bucketEmptied = oldBucket.entries.empty();

        if (bucketEmptied)
            nolock_erase_bucket(oldFrequency);

        ++entry.frequency;

        auto&      newBucket = nolock_bucket(entry.frequency);
        const bool inserted  = newBucket.entries.push_back(entry);
        assert(inserted);
        (void) inserted;

        if (bucketEmptied && oldFrequency == minFrequency)
            minFrequency = entry.frequency;

        if (maxFrequency < entry.frequency)
            maxFrequency = entry.frequency;

        return listItr;
    }

    Return nolock_read(list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return listItr->value;
        else
            return *listItr->value;
    }

    template<typename... Args>
    static storage_value_type make_storage(Args&&... args) {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return Value(std::forward<Args>(args)...);
        else
            return std::make_shared<Value>(std::forward<Args>(args)...);
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        return nolock_insert(key, [&] { return make_storage(std::forward<Args>(args)...); });
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        return nolock_insert(key, [&] {
            return make_storage(std::forward<Builder>(builder)(std::forward<Args>(args)...));
        });
    }

    template<typename CreateStorage>
    Return nolock_insert(const Key& key, CreateStorage&& createStorage) noexcept {
        const auto [mapItr, inserted] = map.try_emplace(key, list.end());

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        storage_value_type value = std::forward<CreateStorage>(createStorage)();

        auto listItr   = list.emplace(list.end(), Entry{key, std::move(value), 1});
        mapItr->second = listItr;

        auto&      bucket = nolock_bucket(1);
        const bool linked = bucket.entries.push_back(*listItr);
        assert(linked);
        (void) linked;

        minFrequency = 1;
        if (maxFrequency < 1)
            maxFrequency = 1;

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(mapItr->second);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        const auto listItr   = mapItr->second;
        const auto frequency = listItr->frequency;

        auto bucketItr = buckets.find(frequency);
        assert(bucketItr != buckets.end());

        const bool erased = bucketItr->second->entries.erase(*listItr);
        assert(erased);
        (void) erased;

        map.erase(mapItr);
        list.erase(listItr);

        if (bucketItr->second->entries.empty())
            nolock_erase_bucket(frequency);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            assert(minFrequency != 0);

            const auto bucketItr = buckets.find(minFrequency);
            assert(bucketItr != buckets.end());

            // Evict the oldest entry from the minimum-frequency bucket.
            auto* const victim = bucketItr->second->entries.front();
            assert(victim != nullptr);

            const auto mapItr = nolock_find(victim->key);
            assert(mapItr != map.end());

            nolock_erase(mapItr);
        }
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = nolock_find(key);

        if (mapItr == map.end())
            return false;

        nolock_erase(mapItr);

        return true;
    }

    void nolock_clear() noexcept {
        map.clear();
        buckets.clear();
        list.clear();

        minFrequency = 0;
        maxFrequency = 0;
    }

    void nolock_release_pool() noexcept { listPool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);

        buckets.max_load_factor(maxLoadFactor);
        buckets.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type       map;
    list_pool_type listPool;
    list_type      list;

    bucket_map_type buckets;
    usize           minFrequency = 0;
    usize           maxFrequency = 0;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry {
       public:
        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        Key                key;
        storage_value_type value;
        usize              frequency = 1;
        Entry*             prev      = nullptr;
        Entry*             next      = nullptr;
    };

    using list_type       = DoublyIntrusiveList<Entry>;
    using list_iterator   = Entry*;
    using map_type        = std::unordered_map<Key, list_iterator>;
    using bucket_type     = std::unique_ptr<list_type>;
    using bucket_map_type = std::unordered_map<usize, bucket_type>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit PoolLFUCache(const usize cap        = 2 * KB,
                          const usize reserveCnt = 1 * KB,
                          const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    PoolLFUCache(const PoolLFUCache&) noexcept            = delete;
    PoolLFUCache& operator=(const PoolLFUCache&) noexcept = delete;
    PoolLFUCache(PoolLFUCache&&) noexcept                 = delete;
    PoolLFUCache& operator=(PoolLFUCache&&) noexcept      = delete;

    ~PoolLFUCache() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        nolock_release_pool();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
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
        std::lock_guard lockGuard{mutex};

        return capacity;
    }

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock;
    // callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != nolock_map_end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == nolock_total_size());

        return map.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == (nolock_total_size() == 0));

        return map.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    // Retains pooled storage for reuse.
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();
    }

    // Releases pooled storage and restores the original configuration.
    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        nolock_release_pool();

        map.rehash(0);
        buckets.rehash(0);

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }

    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    [[nodiscard]] usize nolock_total_size() const noexcept {
        usize count = 0;

        for (const auto& [frequency, bucket] : buckets)
        {
            (void) frequency;
            count += bucket->size();
        }

        return count;
    }

    void nolock_update_frequency_bounds() noexcept {
        if (map.empty())
        {
            minFrequency = 0;
            maxFrequency = 0;
            return;
        }

        minFrequency = std::numeric_limits<usize>::max();
        maxFrequency = 0;

        for (const auto& [frequency, bucket] : buckets)
        {
            if (bucket->empty())
                continue;

            if (minFrequency > frequency)
                minFrequency = frequency;
            if (maxFrequency < frequency)
                maxFrequency = frequency;
        }

        assert(minFrequency != std::numeric_limits<usize>::max());
    }

    list_type& nolock_bucket(const usize frequency) {
        auto itr = buckets.find(frequency);

        if (itr == buckets.end())
        {
            auto bucket = std::make_unique<list_type>();
            itr         = buckets.emplace(frequency, std::move(bucket)).first;
        }

        return *itr->second;
    }

    void nolock_add_to_bucket(Entry& entry) {
        const bool inserted = nolock_bucket(entry.frequency).push_front(entry);
        assert(inserted);
        (void) inserted;
    }

    // Returns true when removing the entry empties its bucket.
    bool nolock_remove_from_bucket(Entry& entry, const bool updateBounds = true) noexcept {
        const usize frequency = entry.frequency;
        const auto  itr       = buckets.find(frequency);
        assert(itr != buckets.end());

        list_type& bucket = *itr->second;
        const bool erased = bucket.erase(entry);
        assert(erased);
        (void) erased;

        if (!bucket.empty())
            return false;

        buckets.erase(itr);

        if (updateBounds)
        {
            if (minFrequency == frequency || frequency == maxFrequency)
                nolock_update_frequency_bounds();
        }

        return true;
    }

    list_iterator nolock_touch(list_iterator entry) noexcept {
        assert(entry != nullptr);

        if (entry->frequency == std::numeric_limits<usize>::max())
        {
            const auto itr = buckets.find(entry->frequency);
            assert(itr != buckets.end());

            itr->second->move_front(*entry);
            return entry;
        }

        const bool wasMinimum    = entry->frequency == minFrequency;
        const bool bucketEmptied = nolock_remove_from_bucket(*entry, false);

        ++entry->frequency;

        nolock_add_to_bucket(*entry);

        if (bucketEmptied && wasMinimum)
            minFrequency = entry->frequency;

        if (maxFrequency < entry->frequency)
            maxFrequency = entry->frequency;

        return entry;
    }

    Return nolock_read(const list_iterator entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return entry->value;
        else
            return *entry->value;
    }

    [[nodiscard]] usize nolock_required_size() const noexcept {
        return usize{1} + (capacity > map.size() ? capacity - map.size() : usize{0});
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(requiredSize, key, std::forward<Args>(args)...);

        mapItr->second = entry;

        nolock_add_to_bucket(*entry);

        minFrequency = 1;
        if (maxFrequency < 1)
            maxFrequency = 1;

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(
          requiredSize, key, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        mapItr->second = entry;

        nolock_add_to_bucket(*entry);

        minFrequency = 1;
        if (maxFrequency < 1)
            maxFrequency = 1;

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;

        nolock_remove_from_bucket(*entry);
        map.erase(mapItr);

        pool.destroy(entry);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (map.size() > size)
        {
            assert(minFrequency != 0);

            const auto bucketItr = buckets.find(minFrequency);
            assert(bucketItr != buckets.end());

            // MRU is at the front; evict the LRU from the back.
            Entry* const entry = bucketItr->second->back();
            assert(entry != nullptr);

            const auto mapItr = nolock_find(entry->key);
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
        for (auto& [frequency, bucket] : buckets)
        {
            (void) frequency;

            while (Entry* const entry = bucket->back())
            {
                bucket->erase(*entry);
                pool.destroy(entry);
            }
        }

        buckets.clear();
        map.clear();

        minFrequency = 0;
        maxFrequency = 0;
    }

    void nolock_release_pool() noexcept { pool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);

        buckets.max_load_factor(maxLoadFactor);
        buckets.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type    map;
    Pool<Entry> pool;

    bucket_map_type buckets;
    usize           minFrequency = 0;
    usize           maxFrequency = 0;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry {
       public:
        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        Key                key;
        storage_value_type value;
        usize              frequency = 1;
        Entry*             prev      = nullptr;
        Entry*             next      = nullptr;
    };

    using list_type       = DoublyIntrusiveList<Entry>;
    using list_iterator   = Entry*;
    using map_type        = ankerl::unordered_dense::map<Key, list_iterator>;
    using bucket_type     = std::unique_ptr<list_type>;
    using bucket_map_type = std::unordered_map<usize, bucket_type>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit PoolFlatLFUCache(const usize cap        = 2 * KB,
                              const usize reserveCnt = 1 * KB,
                              const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    PoolFlatLFUCache(const PoolFlatLFUCache&) noexcept            = delete;
    PoolFlatLFUCache& operator=(const PoolFlatLFUCache&) noexcept = delete;
    PoolFlatLFUCache(PoolFlatLFUCache&&) noexcept                 = delete;
    PoolFlatLFUCache& operator=(PoolFlatLFUCache&&) noexcept      = delete;

    ~PoolFlatLFUCache() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        nolock_release_pool();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
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
        std::lock_guard lockGuard{mutex};

        return capacity;
    }

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock;
    // callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != nolock_map_end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == nolock_total_size());

        return map.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == (nolock_total_size() == 0));

        return map.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    // Retains pooled storage for reuse.
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();
    }

    // Releases pooled storage and restores the original configuration.
    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        nolock_release_pool();

        map.rehash(0);
        buckets.rehash(0);

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }

    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    [[nodiscard]] usize nolock_total_size() const noexcept {
        usize count = 0;

        for (const auto& [frequency, bucket] : buckets)
        {
            (void) frequency;
            count += bucket->size();
        }

        return count;
    }

    void nolock_update_frequency_bounds() noexcept {
        if (map.empty())
        {
            minFrequency = 0;
            maxFrequency = 0;
            return;
        }

        minFrequency = std::numeric_limits<usize>::max();
        maxFrequency = 0;

        for (const auto& [frequency, bucket] : buckets)
        {
            if (bucket->empty())
                continue;

            if (minFrequency > frequency)
                minFrequency = frequency;
            if (maxFrequency < frequency)
                maxFrequency = frequency;
        }

        assert(minFrequency != std::numeric_limits<usize>::max());
    }

    list_type& nolock_bucket(const usize frequency) {
        auto itr = buckets.find(frequency);

        if (itr == buckets.end())
        {
            auto bucket = std::make_unique<list_type>();
            itr         = buckets.emplace(frequency, std::move(bucket)).first;
        }

        return *itr->second;
    }

    void nolock_add_to_bucket(Entry& entry) {
        const bool inserted = nolock_bucket(entry.frequency).push_front(entry);
        assert(inserted);
        (void) inserted;
    }

    // Returns true when removing the entry empties its bucket.
    bool nolock_remove_from_bucket(Entry& entry, const bool updateBounds = true) noexcept {
        const usize frequency = entry.frequency;
        const auto  itr       = buckets.find(frequency);
        assert(itr != buckets.end());

        list_type& bucket = *itr->second;
        const bool erased = bucket.erase(entry);
        assert(erased);
        (void) erased;

        if (!bucket.empty())
            return false;

        buckets.erase(itr);

        if (updateBounds)
        {
            if (minFrequency == frequency || frequency == maxFrequency)
                nolock_update_frequency_bounds();
        }

        return true;
    }

    list_iterator nolock_touch(list_iterator entry) noexcept {
        assert(entry != nullptr);

        if (entry->frequency == std::numeric_limits<usize>::max())
        {
            const auto itr = buckets.find(entry->frequency);
            assert(itr != buckets.end());

            itr->second->move_front(*entry);
            return entry;
        }

        const bool wasMinimum    = entry->frequency == minFrequency;
        const bool bucketEmptied = nolock_remove_from_bucket(*entry, false);

        ++entry->frequency;

        nolock_add_to_bucket(*entry);

        if (bucketEmptied && wasMinimum)
            minFrequency = entry->frequency;

        if (maxFrequency < entry->frequency)
            maxFrequency = entry->frequency;

        return entry;
    }

    Return nolock_read(const list_iterator entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return entry->value;
        else
            return *entry->value;
    }

    [[nodiscard]] usize nolock_required_size() const noexcept {
        return usize{1} + (capacity > map.size() ? capacity - map.size() : usize{0});
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(requiredSize, key, std::forward<Args>(args)...);

        mapItr->second = entry;

        nolock_add_to_bucket(*entry);

        minFrequency = 1;
        if (maxFrequency < 1)
            maxFrequency = 1;

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(
          requiredSize, key, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        mapItr->second = entry;

        nolock_add_to_bucket(*entry);

        minFrequency = 1;
        if (maxFrequency < 1)
            maxFrequency = 1;

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;

        map.erase(mapItr);

        nolock_remove_from_bucket(*entry);

        pool.destroy(entry);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (map.size() > size)
        {
            assert(minFrequency != 0);

            const auto bucketItr = buckets.find(minFrequency);
            assert(bucketItr != buckets.end());

            // MRU is at the front; evict the LRU from the back.
            Entry* const entry = bucketItr->second->back();
            assert(entry != nullptr);

            const auto mapItr = nolock_find(entry->key);
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
        for (auto& [frequency, bucket] : buckets)
        {
            (void) frequency;

            while (Entry* const entry = bucket->back())
            {
                bucket->erase(*entry);
                pool.destroy(entry);
            }
        }

        buckets.clear();
        map.clear();

        minFrequency = 0;
        maxFrequency = 0;
    }

    void nolock_release_pool() noexcept { pool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);

        buckets.max_load_factor(maxLoadFactor);
        buckets.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type    map;
    Pool<Entry> pool;

    bucket_map_type buckets;
    usize           minFrequency = 0;
    usize           maxFrequency = 0;
};

}  // namespace DON

#endif  // UTILITY_LFU_CACHE_H_INCLUDED
