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

#include "../utility/guard.h"
#include "../utility/list.h"
#include "../utility/pool.h"
#include "../utility/set.h"
#include "../utility/types.h"
#include "../utility/ankerl/unordered_dense.h"

namespace DON {

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class ListLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    // Entries are ordered by descending frequency, with MRU first for equal frequencies
    struct Entry final {
       public:
        storage_value_type value;
        usize              frequency = 1;
        usize              recency   = 0;
        const Key*         key       = nullptr;
    };

    using list_value_type = Entry;
    using list_type       = std::pmr::list<list_value_type>;
    using list_iterator   = typename list_type::iterator;
    using list_pool_type  = std::pmr::unsynchronized_pool_resource;
    using map_type        = std::unordered_map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

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

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = newCapacity;
        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
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

        capacity       = defaultCapacity;
        recencyCounter = 0;
        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_list_beg() noexcept { return list.begin(); }
    auto nolock_list_end() noexcept { return list.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    void nolock_renumber_recency() noexcept {
        usize recency = 0;

        for (auto itr = nolock_list_beg(); itr != nolock_list_end(); ++itr)
            itr->recency = ++recency;

        recencyCounter = recency;
    }

    usize nolock_next_recency() noexcept {
        if (recencyCounter == std::numeric_limits<usize>::max())
            nolock_renumber_recency();

        return ++recencyCounter;
    }

    void nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nolock_list_end());

        if (listItr->frequency < std::numeric_limits<usize>::max())
            ++listItr->frequency;

        listItr->recency = nolock_next_recency();
    }

    Return nolock_read(list_iterator listItr) const noexcept {
        return nolock_read_value(listItr->value);
    }

    Return nolock_read_value(const storage_value_type& value) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return value;
        else
            return *value;
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

    // Select the least-frequent, least-recently-used entry.
    list_iterator nolock_lfu_lru_pos() noexcept {
        assert(!list.empty());

        auto victim = nolock_list_beg();

        for (auto itr = std::next(victim); itr != nolock_list_end(); ++itr)
            if (itr->frequency != victim->frequency  //
                  ? itr->frequency < victim->frequency
                  : itr->recency < victim->recency)
                victim = itr;

        return victim;
    }

    template<typename CreateStorage>
    Return nolock_insert(const Key& key, CreateStorage&& createStorage) noexcept {
        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
        {
            nolock_touch(mapItr->second);
            return nolock_read(mapItr->second);
        }

        MapGuard<map_type> mapGuard{map, mapItr};

        storage_value_type value = std::forward<CreateStorage>(createStorage)();

        if (list.size() >= capacity)
        {
            const auto victim = nolock_lfu_lru_pos();

            // Preserve the original cache-admission policy.
            if (victim->frequency > 1)
                return nolock_read_value(value);

            const auto oldMapItr = nolock_find(*victim->key);
            assert(oldMapItr != map.end());

            // Destroy the entry before invalidating its key pointer.
            std::destroy_at(std::addressof(*victim));
            map.erase(oldMapItr);

            new (static_cast<void*>(std::addressof(*victim)))
              Entry{std::move(value), 1, nolock_next_recency(), std::addressof(mapItr->first)};

            mapItr->second = victim;
        }
        else
        {
            mapItr->second =
              list.emplace(nolock_list_end(), Entry{std::move(value), 1, nolock_next_recency(),
                                                    std::addressof(mapItr->first)});
        }

        mapGuard.commit();

        return nolock_read(mapItr->second);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        const auto listItr = mapItr->second;

        // Entry::key points to the key owned by map.
        list.erase(listItr);
        map.erase(mapItr);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            const auto victim = nolock_lfu_lru_pos();
            const auto mapItr = nolock_find(*victim->key);

            assert(mapItr != map.end());
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
        // Destroy entries before clearing the keys they point to.
        list.clear();
        map.clear();
    }

    void nolock_release_pool() noexcept { listPool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type       map;
    list_pool_type listPool;
    list_type      list;

    usize recencyCounter = 0;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class SetLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry final {
       public:
        storage_value_type value;
        usize              frequency = 1;
        usize              recency   = 0;
        const Key*         key       = nullptr;
    };

    struct EntryCompare final {
        bool operator()(const Entry* const e1, const Entry* const e2) const noexcept {
            return e1->frequency != e2->frequency  //
                   ? e1->frequency > e2->frequency
                   : e1->recency > e2->recency;
        }
    };

    using set_value_type = Entry*;
    //using set_type       = std::set<set_value_type, EntryCompare>;
    using set_type       = VectorSet<set_value_type, EntryCompare>;
    using map_value_type = Entry;
    using map_type       = std::unordered_map<Key, map_value_type>;
    using map_iterator   = typename map_type::iterator;

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

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = newCapacity;

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
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    void nolock_renumber_recency() noexcept {
        usize recency = 0;

        for (Entry* const entry : entries)
            entry->recency = ++recency;

        recencyCounter = recency;
    }

    usize nolock_next_recency() noexcept {
        if (recencyCounter == std::numeric_limits<usize>::max())
            nolock_renumber_recency();

        return ++recencyCounter;
    }

    void nolock_touch(Entry& entry) noexcept {
        if (recencyCounter == std::numeric_limits<usize>::max())
        {
            [[maybe_unused]]  //
            const usize erased = entries.erase(&entry);
            assert(erased == 1);

            if (entry.frequency != std::numeric_limits<usize>::max())
                ++entry.frequency;

            entry.recency = nolock_next_recency();

            [[maybe_unused]]  //
            const bool inserted = entries.insert(&entry);
            assert(inserted);
            return;
        }

        const auto position = entries.find_position(&entry);
        assert(position.has_value());

        if (entry.frequency != std::numeric_limits<usize>::max())
            ++entry.frequency;

        entry.recency = ++recencyCounter;

        [[maybe_unused]]  //
        const bool promoted = entries.promote(*position);
        assert(promoted);
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

        const auto [mapItr, inserted] = map.try_emplace(key,  //
                                                        Entry{std::move(value), 1, 0, nullptr});
        assert(inserted);

        MapGuard<map_type> mapGuard{map, mapItr};

        Entry& entry  = mapItr->second;
        entry.key     = std::addressof(mapItr->first);
        entry.recency = nolock_next_recency();

        [[maybe_unused]]  //
        //const bool ordered = entries.insert(std::addressof(entry)).second;
        const bool ordered = entries.insert(std::addressof(entry));
        assert(ordered);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        [[maybe_unused]]  //
        const usize erased = entries.erase(std::addressof(mapItr->second));
        assert(erased == 1);
        map.erase(mapItr);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (entries.size() > size)
        {
            assert(!entries.empty());

            // Highest frequency / newest entry is at the front.
            // Evict the lowest frequency / oldest entry at the back.
            //const Entry* const entry = *entries.rbegin();
            const Entry* const entry = entries.back();

            const auto mapItr = nolock_find(*entry->key);

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
        entries.reserve(std::min(capacity, reserveCount));
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type map;
    set_type entries;

    usize recencyCounter = 0;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class BucketListLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Bucket;

    struct Entry final {
       public:
        Key                key;
        storage_value_type value;
        Bucket*            bucket = nullptr;
        Entry*             prev   = nullptr;
        Entry*             next   = nullptr;
    };

    using list_value_type = Entry;
    using list_type       = std::pmr::list<list_value_type>;
    using list_iterator   = typename list_type::iterator;
    using list_pool_type  = std::pmr::unsynchronized_pool_resource;
    using map_type        = std::unordered_map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

    struct Bucket final {
       public:
        explicit Bucket(const usize freq) noexcept :
            frequency(freq) {}

        usize                      frequency;
        DoublyIntrusiveList<Entry> entries;
        Bucket*                    prev = nullptr;
        Bucket*                    next = nullptr;
    };

   public:
    explicit BucketListLFUCache(const usize cap        = 2 * KB,
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

    BucketListLFUCache(const BucketListLFUCache&) noexcept            = delete;
    BucketListLFUCache& operator=(const BucketListLFUCache&) noexcept = delete;
    BucketListLFUCache(BucketListLFUCache&&) noexcept                 = delete;
    BucketListLFUCache& operator=(BucketListLFUCache&&) noexcept      = delete;

    ~BucketListLFUCache() noexcept {
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

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = newCapacity;

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

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_list_beg() noexcept { return list.begin(); }
    auto nolock_list_end() noexcept { return list.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    void nolock_destroy_bucket(Bucket& bucket) noexcept {
        assert(bucket.entries.empty());

        [[maybe_unused]]  //
        const bool erased = buckets.erase(bucket);
        assert(erased);

        bucketPool.destroy(&bucket);
    }

    void nolock_add_to_bucket(Entry& entry) noexcept {
        Bucket* bucket = buckets.back();

        if (bucket == nullptr || bucket->frequency != 1)
        {
            bucket = bucketPool.create(1, 1);
            buckets.push_back(*bucket);
        }

        entry.bucket = bucket;
        [[maybe_unused]]  //
        const bool inserted = bucket->entries.push_front(entry);
        assert(inserted);
    }

    void nolock_remove_from_bucket(Entry& entry) noexcept {
        Bucket* const bucket = entry.bucket;
        assert(bucket != nullptr);
        [[maybe_unused]]  //
        const bool erased = bucket->entries.erase(entry);
        assert(erased);

        entry.bucket = nullptr;
        if (bucket->entries.empty())
            nolock_destroy_bucket(*bucket);
    }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nolock_list_end());

        Entry& entry = *listItr;

        Bucket* const oldBucket = entry.bucket;
        assert(oldBucket != nullptr);

        const usize oldFrequency = oldBucket->frequency;

        if (oldFrequency == std::numeric_limits<usize>::max())
        {
            [[maybe_unused]]  //
            const bool moved = oldBucket->entries.move_front(entry);
            assert(moved);
            return listItr;
        }

        const usize nextFrequency = oldFrequency + 1;

        Bucket* const higherBucket = buckets.prev(*oldBucket);

        if (higherBucket != nullptr && higherBucket->frequency == nextFrequency)
        {
            [[maybe_unused]]  //
            const bool erased = oldBucket->entries.erase(entry);
            assert(erased);

            entry.bucket = higherBucket;

            [[maybe_unused]]  //
            const bool inserted = higherBucket->entries.push_front(entry);
            assert(inserted);

            if (oldBucket->entries.empty())
                nolock_destroy_bucket(*oldBucket);

            return listItr;
        }

        if (oldBucket->entries.size() == 1)
        {
            oldBucket->frequency = nextFrequency;
            return listItr;
        }

        Bucket* const newBucket = bucketPool.create(1, nextFrequency);

        [[maybe_unused]]  //
        const bool linked = buckets.insert_before(*newBucket, *oldBucket);
        assert(linked);

        [[maybe_unused]]  //
        const bool erased = oldBucket->entries.erase(entry);
        assert(erased);

        entry.bucket = newBucket;

        [[maybe_unused]]  //
        const bool inserted = newBucket->entries.push_front(entry);
        assert(inserted);

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
        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard<map_type> mapGuard{map, mapItr};

        storage_value_type value = std::forward<CreateStorage>(createStorage)();

        auto listItr   = list.emplace(nolock_list_end(), Entry{key, std::move(value), nullptr});
        mapItr->second = listItr;

        nolock_add_to_bucket(*listItr);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(mapItr->second);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        const auto listItr = mapItr->second;

        map.erase(mapItr);
        nolock_remove_from_bucket(*listItr);
        list.erase(listItr);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            Bucket* const leastFrequencyBucket = buckets.back();
            assert(leastFrequencyBucket != nullptr);

            // The LRU entry in the least-frequent bucket is at the back.
            auto* const victim = leastFrequencyBucket->entries.back();
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
        while (Bucket* const bucket = buckets.back())
        {
            while (Entry* const entry = bucket->entries.back())
            {
                bucket->entries.erase(*entry);
                entry->bucket = nullptr;
            }

            nolock_destroy_bucket(*bucket);
        }

        list.clear();
        map.clear();
    }

    void nolock_release_pool() noexcept {
        listPool.release();
        bucketPool.release();
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

    map_type       map;
    list_pool_type listPool;
    list_type      list;

    Pool<Bucket>                bucketPool;
    DoublyIntrusiveList<Bucket> buckets;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Bucket;

    struct Entry final {
       public:
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        Key                key;
        storage_value_type value;
        Bucket*            bucket = nullptr;
        Entry*             prev   = nullptr;
        Entry*             next   = nullptr;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = std::unordered_map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

    struct Bucket final {
       public:
        explicit Bucket(const usize freq) noexcept :
            frequency(freq) {}

        usize     frequency;
        list_type entries;
        Bucket*   prev = nullptr;
        Bucket*   next = nullptr;
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

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = newCapacity;

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

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }

    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    [[nodiscard]] usize nolock_total_size() const noexcept {
        usize size = 0;

        for (const Bucket* bucket = buckets.front(); bucket != nullptr;
             bucket               = buckets.next(*bucket))
            size += bucket->entries.size();

        return size;
    }

    void nolock_destroy_bucket(Bucket& bucket) noexcept {
        assert(bucket.entries.empty());

        [[maybe_unused]]  //
        const bool erased = buckets.erase(bucket);
        assert(erased);

        bucketPool.destroy(&bucket);
    }

    void nolock_add_to_bucket(Entry& entry) noexcept {
        Bucket* bucket = buckets.back();

        if (bucket == nullptr || bucket->frequency != 1)
        {
            bucket = bucketPool.create(1, 1);
            buckets.push_back(*bucket);
        }

        entry.bucket = bucket;
        [[maybe_unused]]  //
        const bool inserted = bucket->entries.push_front(entry);
        assert(inserted);
    }

    void nolock_remove_from_bucket(Entry& entry) noexcept {
        Bucket* const bucket = entry.bucket;
        assert(bucket != nullptr);
        [[maybe_unused]]  //
        const bool erased = bucket->entries.erase(entry);
        assert(erased);

        entry.bucket = nullptr;
        if (bucket->entries.empty())
            nolock_destroy_bucket(*bucket);
    }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nullptr);

        Entry& entry = *listItr;

        Bucket* const oldBucket = entry.bucket;
        assert(oldBucket != nullptr);

        const usize oldFrequency = oldBucket->frequency;

        if (oldFrequency == std::numeric_limits<usize>::max())
        {
            [[maybe_unused]]  //
            const bool moved = oldBucket->entries.move_front(entry);
            assert(moved);
            return listItr;
        }

        const usize nextFrequency = oldFrequency + 1;

        Bucket* const higherBucket = buckets.prev(*oldBucket);

        if (higherBucket != nullptr && higherBucket->frequency == nextFrequency)
        {
            [[maybe_unused]]  //
            const bool erased = oldBucket->entries.erase(entry);
            assert(erased);

            entry.bucket = higherBucket;

            [[maybe_unused]]  //
            const bool inserted = higherBucket->entries.push_front(entry);
            assert(inserted);

            if (oldBucket->entries.empty())
                nolock_destroy_bucket(*oldBucket);

            return listItr;
        }

        if (oldBucket->entries.size() == 1)
        {
            oldBucket->frequency = nextFrequency;
            return listItr;
        }

        Bucket* const newBucket = bucketPool.create(1, nextFrequency);

        [[maybe_unused]]  //
        const bool linked = buckets.insert_before(*newBucket, *oldBucket);
        assert(linked);

        [[maybe_unused]]  //
        const bool erased = oldBucket->entries.erase(entry);
        assert(erased);

        entry.bucket = newBucket;

        [[maybe_unused]]  //
        const bool inserted = newBucket->entries.push_front(entry);
        assert(inserted);

        return listItr;
    }

    Return nolock_read(const list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return listItr->value;
        else
            return *listItr->value;
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

        MapGuard<map_type> mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(requiredSize, key,  //
                                         std::forward<Args>(args)...);

        mapItr->second = entry;

        nolock_add_to_bucket(*entry);

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

        MapGuard<map_type> mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(requiredSize, key,      //
                                         std::forward<Builder>(  //
                                           builder)(std::forward<Args>(args)...));

        mapItr->second = entry;

        nolock_add_to_bucket(*entry);

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
            Bucket* const leastFrequencyBucket = buckets.back();
            assert(leastFrequencyBucket != nullptr);

            // The LRU entry in the least-frequent bucket is at the back.
            Entry* const entry = leastFrequencyBucket->entries.back();
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
        while (Bucket* const bucket = buckets.back())
        {
            while (Entry* const entry = bucket->entries.back())
            {
                bucket->entries.erase(*entry);
                entry->bucket = nullptr;
                pool.destroy(entry);
            }

            nolock_destroy_bucket(*bucket);
        }

        map.clear();
    }

    void nolock_release_pool() noexcept {
        pool.release();
        bucketPool.release();
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

    map_type    map;
    Pool<Entry> pool;

    Pool<Bucket>                bucketPool;
    DoublyIntrusiveList<Bucket> buckets;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatLFUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Bucket;

    struct Entry final {
       public:
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        Key                key;
        storage_value_type value;
        Bucket*            bucket = nullptr;
        Entry*             prev   = nullptr;
        Entry*             next   = nullptr;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = ankerl::unordered_dense::map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

    struct Bucket {
       public:
        explicit Bucket(const usize freq) noexcept :
            frequency(freq) {}

        usize     frequency;
        list_type entries;
        Bucket*   prev = nullptr;
        Bucket*   next = nullptr;
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

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = newCapacity;

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

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }

    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    [[nodiscard]] usize nolock_total_size() const noexcept {
        usize size = 0;

        for (const Bucket* bucket = buckets.front(); bucket != nullptr;
             bucket               = buckets.next(*bucket))
            size += bucket->entries.size();

        return size;
    }

    void nolock_destroy_bucket(Bucket& bucket) noexcept {
        assert(bucket.entries.empty());

        [[maybe_unused]]  //
        const bool erased = buckets.erase(bucket);
        assert(erased);

        bucketPool.destroy(&bucket);
    }

    void nolock_add_to_bucket(Entry& entry) noexcept {
        Bucket* bucket = buckets.back();

        if (bucket == nullptr || bucket->frequency != 1)
        {
            bucket = bucketPool.create(1, 1);
            buckets.push_back(*bucket);
        }

        entry.bucket = bucket;
        [[maybe_unused]]  //
        const bool inserted = bucket->entries.push_front(entry);
        assert(inserted);
    }

    void nolock_remove_from_bucket(Entry& entry) noexcept {
        Bucket* const bucket = entry.bucket;
        assert(bucket != nullptr);
        [[maybe_unused]]  //
        const bool erased = bucket->entries.erase(entry);
        assert(erased);

        entry.bucket = nullptr;
        if (bucket->entries.empty())
            nolock_destroy_bucket(*bucket);
    }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nullptr);

        Entry& entry = *listItr;

        Bucket* const oldBucket = entry.bucket;
        assert(oldBucket != nullptr);

        const usize oldFrequency = oldBucket->frequency;

        if (oldFrequency == std::numeric_limits<usize>::max())
        {
            [[maybe_unused]]  //
            const bool moved = oldBucket->entries.move_front(entry);
            assert(moved);
            return listItr;
        }

        const usize nextFrequency = oldFrequency + 1;

        Bucket* const higherBucket = buckets.prev(*oldBucket);

        if (higherBucket != nullptr && higherBucket->frequency == nextFrequency)
        {
            [[maybe_unused]]  //
            const bool erased = oldBucket->entries.erase(entry);
            assert(erased);

            entry.bucket = higherBucket;

            [[maybe_unused]]  //
            const bool inserted = higherBucket->entries.push_front(entry);
            assert(inserted);

            if (oldBucket->entries.empty())
                nolock_destroy_bucket(*oldBucket);

            return listItr;
        }

        if (oldBucket->entries.size() == 1)
        {
            oldBucket->frequency = nextFrequency;
            return listItr;
        }

        Bucket* const newBucket = bucketPool.create(1, nextFrequency);

        [[maybe_unused]]  //
        const bool linked = buckets.insert_before(*newBucket, *oldBucket);
        assert(linked);

        [[maybe_unused]]  //
        const bool erased = oldBucket->entries.erase(entry);
        assert(erased);

        entry.bucket = newBucket;

        [[maybe_unused]]  //
        const bool inserted = newBucket->entries.push_front(entry);
        assert(inserted);

        return listItr;
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

        MapGuard<map_type> mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(requiredSize, key,  //
                                         std::forward<Args>(args)...);

        mapItr->second = entry;

        nolock_add_to_bucket(*entry);

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

        MapGuard<map_type> mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(requiredSize, key,      //
                                         std::forward<Builder>(  //
                                           builder)(std::forward<Args>(args)...));

        mapItr->second = entry;

        nolock_add_to_bucket(*entry);

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
            Bucket* const leastFrequencyBucket = buckets.back();
            assert(leastFrequencyBucket != nullptr);

            // The LRU entry in the least-frequent bucket is at the back.
            Entry* const entry = leastFrequencyBucket->entries.back();
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
        while (Bucket* const bucket = buckets.back())
        {
            while (Entry* const entry = bucket->entries.back())
            {
                bucket->entries.erase(*entry);
                pool.destroy(entry);
            }

            nolock_destroy_bucket(*bucket);
        }

        map.clear();
    }

    void nolock_release_pool() noexcept {
        pool.release();
        bucketPool.release();
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

    map_type    map;
    Pool<Entry> pool;

    Pool<Bucket>                bucketPool;
    DoublyIntrusiveList<Bucket> buckets;
};

}  // namespace DON

#endif  // UTILITY_LFU_CACHE_H_INCLUDED
