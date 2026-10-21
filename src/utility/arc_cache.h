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

#ifndef UTILITY_ARC_CACHE_H_INCLUDED
#define UTILITY_ARC_CACHE_H_INCLUDED

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
#include <unordered_set>
#include <utility>
#include <vector>

#include "../utility/list.h"
#include "../utility/pool.h"
#include "../utility/set.h"
#include "../utility/types.h"
#include "../utility/ankerl/unordered_dense.h"

namespace DON {

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolARCCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Bucket;

    struct Entry {
        Key     key;
        Bucket* bucket = nullptr;
        Entry*  prev   = nullptr;
        Entry*  next   = nullptr;

        explicit Entry(const Key& k) noexcept :
            key(k) {}
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;

    struct ResidentEntry final: Entry {
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit ResidentEntry(const Key& k, Args&&... args) noexcept :
            Entry(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        storage_value_type value;
    };

    struct GhostEntry final: Entry {
        explicit GhostEntry(const Key& k) noexcept :
            Entry(k) {}
    };

    struct Bucket {
        list_type entries;
    };

    using resident_map_type = std::unordered_map<Key, ResidentEntry*>;
    using ghost_map_type    = std::unordered_map<Key, GhostEntry*>;

    template<typename Map>
    class MapGuard final {
       public:
        MapGuard(Map& mapRef, typename Map::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        Map&                   map;
        typename Map::iterator iterator;
        bool                   active = true;
    };

   public:
    explicit PoolARCCache(const usize cap        = 2 * KB,
                          const usize reserveCnt = 1 * KB,
                          const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    PoolARCCache(const PoolARCCache&) noexcept            = delete;
    PoolARCCache& operator=(const PoolARCCache&) noexcept = delete;
    PoolARCCache(PoolARCCache&&) noexcept                 = delete;
    PoolARCCache& operator=(PoolARCCache&&) noexcept      = delete;

    ~PoolARCCache() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_clear();

        nolock_release_pool();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        std::lock_guard residentLock{residentMutex};

        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto mapItr = residentMap.find(key);

        if (mapItr != residentMap.end())
        {
            ResidentEntry* const entry = mapItr->second;

            nolock_promote_to_t2(*entry);

            return nolock_read(entry);
        }

        std::lock_guard ghostLock{ghostMutex};

        return nolock_write_after_resident_miss(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        std::lock_guard residentLock{residentMutex};

        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto mapItr = residentMap.find(key);

        if (mapItr != residentMap.end())
        {
            ResidentEntry* const entry = mapItr->second;

            nolock_promote_to_t2(*entry);

            return nolock_read(entry);
        }

        std::lock_guard ghostLock{ghostMutex};

        return nolock_write_with_after_resident_miss(key, std::forward<Builder>(builder),
                                                     std::forward<Args>(args)...);
    }

    void evict_excess(const usize size) noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_evict_excess(size);
    }

    void evict_excess() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_evict_excess(capacity);
    }

    [[nodiscard]] usize get_capacity() const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return capacity;
    }

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        capacity = newCapacity;

        if (target > capacity)
            target = capacity;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.find(key) != residentMap.end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        return nolock_remove(key);
    }

    void clear() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_configure();
        assert(nolock_consistency_check());
    }

    void reset() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_clear();
        nolock_release_pool();

        residentMap.rehash(0);
        ghostMap.rehash(0);

        capacity = defaultCapacity;
        target   = 0;

        nolock_configure();
        assert(nolock_consistency_check());
    }

   private:
    // ---------- Bucket access ----------

    [[nodiscard]] Bucket& nolock_bucket(const usize index) noexcept { return buckets[index]; }

    [[nodiscard]] const Bucket& nolock_bucket(const usize index) const noexcept {
        return buckets[index];
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return buckets[T1].entries.size() + buckets[T2].entries.size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return buckets[B1].entries.size() + buckets[B2].entries.size();
    }

    [[nodiscard]] usize nolock_directory_size() const noexcept {
        return nolock_resident_size() + nolock_ghost_size();
    }

    [[nodiscard]] usize nolock_required_size() const noexcept {
        return usize{1}
             + (capacity > residentMap.size() ? capacity - residentMap.size() : usize{0});
    }

    [[nodiscard]] usize nolock_ghost_required_size() const noexcept {
        return usize{1} + (capacity > ghostMap.size() ? capacity - ghostMap.size() : usize{0});
    }

    Return nolock_read(const ResidentEntry* entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return entry->value;
        else
            return *entry->value;
    }

    // ---------- Shared intrusive-list operations ----------

    void nolock_list_push_front(Entry& entry, const usize index) noexcept {
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.bucket == nullptr);

        Bucket& bucket = nolock_bucket(index);

        [[maybe_unused]]
        const bool inserted = bucket.entries.push_front(entry);
        assert(inserted);

        if (inserted)
            entry.bucket = &bucket;
    }

    void nolock_list_remove(Entry& entry) noexcept {
        assert(entry.bucket != nullptr);

        if (entry.bucket == nullptr)
            return;

        [[maybe_unused]]
        const bool erased = entry.bucket->entries.erase(entry);
        assert(erased);

        if (erased)
            entry.bucket = nullptr;
    }

    void nolock_list_move_front(Entry& entry) noexcept {
        assert(entry.bucket != nullptr);

        if (entry.bucket == nullptr)
            return;

        [[maybe_unused]]
        const bool moved = entry.bucket->entries.move_front(entry);
        assert(moved);
    }

    // T2 is MRU-first. Avoid touching the list when the entry is
    // already at the front; this is the common repeated-hot-key case.
    void nolock_promote_to_t2(ResidentEntry& entry) noexcept {
        if (entry.bucket == &buckets[T2])
        {
            if (entry.prev != nullptr)
                nolock_list_move_front(entry);

            return;
        }

        assert(entry.bucket == &buckets[T1]);

        if (entry.bucket != &buckets[T1])
            return;

        nolock_list_remove(entry);
        nolock_list_push_front(entry, T2);
    }

    // ---------- Entry creation ----------

    template<typename... Args>
    ResidentEntry* nolock_add_resident(const Key& key, const usize index, Args&&... args) noexcept {
        const auto [mapItr, inserted] = residentMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return mapItr->second;

        MapGuard<resident_map_type> mapGuard{residentMap, mapItr};

        const usize requiredSize = residentPool.empty() ? nolock_required_size() : 0;

        ResidentEntry* const entry =
          residentPool.create(requiredSize, key, std::forward<Args>(args)...);

        if (entry == nullptr)
            return nullptr;

        mapItr->second = entry;

        nolock_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    GhostEntry* nolock_add_ghost(const Key& key, const usize index) noexcept {
        const auto [mapItr, inserted] = ghostMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return nullptr;

        MapGuard<ghost_map_type> mapGuard{ghostMap, mapItr};

        const usize requiredSize = ghostPool.empty() ? nolock_ghost_required_size() : 0;

        GhostEntry* const entry = ghostPool.create(requiredSize, key);

        if (entry == nullptr)
            return nullptr;

        mapItr->second = entry;

        nolock_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    // ---------- Entry destruction ----------

    void nolock_remove_resident(typename resident_map_type::iterator mapItr) noexcept {
        assert(mapItr != residentMap.end());

        if (mapItr == residentMap.end())
            return;

        ResidentEntry* const entry = mapItr->second;

        assert(entry != nullptr);

        if (entry == nullptr)
            return;

        nolock_list_remove(*entry);

        residentMap.erase(mapItr);
        residentPool.destroy(entry);
    }

    void nolock_remove_resident(ResidentEntry& entry) noexcept {
        const auto mapItr = residentMap.find(entry.key);

        assert(mapItr != residentMap.end());

        if (mapItr == residentMap.end())
            return;

        assert(mapItr->second == &entry);

        if (mapItr->second != &entry)
            return;

        nolock_remove_resident(mapItr);
    }

    void nolock_remove_ghost(typename ghost_map_type::iterator mapItr) noexcept {
        assert(mapItr != ghostMap.end());

        if (mapItr == ghostMap.end())
            return;

        GhostEntry* const entry = mapItr->second;

        assert(entry != nullptr);

        if (entry == nullptr)
            return;

        nolock_list_remove(*entry);

        ghostMap.erase(mapItr);
        ghostPool.destroy(entry);
    }

    void nolock_remove_ghost(GhostEntry& entry) noexcept {
        const auto mapItr = ghostMap.find(entry.key);

        assert(mapItr != ghostMap.end());

        if (mapItr == ghostMap.end())
            return;

        assert(mapItr->second == &entry);

        if (mapItr->second != &entry)
            return;

        nolock_remove_ghost(mapItr);
    }

    // ---------- ARC replacement ----------

    void nolock_evict_to_ghost(ResidentEntry& entry, const usize index) noexcept {
        GhostEntry* const ghost = nolock_add_ghost(entry.key, index);

        assert(ghost != nullptr);

        if (ghost == nullptr)
            return;

        nolock_remove_resident(entry);
    }

    void nolock_replace(const bool keyInB2) noexcept {
        list_type& t1 = buckets[T1].entries;
        list_type& t2 = buckets[T2].entries;

        const bool evictT1 =
          !t1.empty() && (t1.size() > target || (keyInB2 && t1.size() == target));

        if (evictT1)
        {
            Entry* const entry = t1.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*static_cast<ResidentEntry*>(entry), B1);

            return;
        }

        if (!t2.empty())
        {
            Entry* const entry = t2.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*static_cast<ResidentEntry*>(entry), B2);

            return;
        }

        if (!t1.empty())
        {
            Entry* const entry = t1.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*static_cast<ResidentEntry*>(entry), B1);
        }
    }

    // Keep only the total ghost-history limit.
    // No T1+B1 or T2+B2 pairwise capacity restriction is imposed.
    void nolock_trim_ghosts(const usize maxSize) noexcept {
        while (ghostMap.size() > maxSize)
        {
            Entry* entry = buckets[B1].entries.back();

            if (entry == nullptr)
                entry = buckets[B2].entries.back();

            assert(entry != nullptr);

            if (entry == nullptr)
                break;

            nolock_remove_ghost(*static_cast<GhostEntry*>(entry));
        }
    }

    void nolock_handle_ghost_hit(typename ghost_map_type::iterator mapItr) noexcept {
        assert(mapItr != ghostMap.end());

        if (mapItr == ghostMap.end())
            return;

        GhostEntry* const ghost = mapItr->second;

        assert(ghost != nullptr);

        if (ghost == nullptr)
            return;

        Bucket* const sourceBucket = ghost->bucket;

        assert(sourceBucket == &buckets[B1] || sourceBucket == &buckets[B2]);

        if (sourceBucket != &buckets[B1] && sourceBucket != &buckets[B2])
            return;

        assert(target <= capacity);

        if (target > capacity)
            target = capacity;

        const usize b1Size = buckets[B1].entries.size();
        const usize b2Size = buckets[B2].entries.size();

        const bool keyInB2 = sourceBucket == &buckets[B2];

        if (keyInB2)
        {
            assert(b2Size != 0);

            if (b2Size == 0)
                return;

            const usize delta = std::max(usize{1}, b1Size / b2Size);

            target = delta < target ? target - delta : usize{0};
        }
        else
        {
            assert(b1Size != 0);

            if (b1Size == 0)
                return;

            const usize delta = std::max(usize{1}, b2Size / b1Size);

            target += std::min(delta, capacity - target);
        }

        // Erase before replacement can insert another ghost and rehash ghostMap.
        nolock_remove_ghost(mapItr);

        if (residentMap.size() >= capacity && capacity != 0)
            nolock_replace(keyInB2);
    }

    void nolock_prepare_miss() noexcept {
        if (capacity != 0 && residentMap.size() >= capacity)
            nolock_replace(false);
    }

    // ---------- Read/write internals ----------

    // The caller has already checked residentMap while holding residentMutex.
    // ghostMutex is held by the caller.
    template<typename... Args>
    Return nolock_write_after_resident_miss(const Key& key, Args&&... args) noexcept {
        if (const auto itr = ghostMap.find(key); itr != ghostMap.end())
        {
            nolock_handle_ghost_hit(itr);

            ResidentEntry* const entry = nolock_add_resident(key, T2, std::forward<Args>(args)...);

            if (entry == nullptr)
                return Return{std::forward<Args>(args)...};

            nolock_trim_ghosts(capacity);
            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        ResidentEntry* const entry = nolock_add_resident(key, T1, std::forward<Args>(args)...);

        nolock_trim_ghosts(capacity);
        assert(nolock_consistency_check());

        if (entry == nullptr)
            return Return{std::forward<Args>(args)...};

        return nolock_read(entry);
    }

    // The caller has already checked residentMap while holding residentMutex.
    // ghostMutex is held by the caller.
    template<typename Builder, typename... Args>
    Return nolock_write_with_after_resident_miss(const Key& key,
                                                 Builder&&  builder,
                                                 Args&&... args) noexcept {
        // Evaluate the builder once and retain the value for allocation failure.
        Value value(std::forward<Builder>(builder)(std::forward<Args>(args)...));

        if (const auto itr = ghostMap.find(key); itr != ghostMap.end())
        {
            nolock_handle_ghost_hit(itr);

            ResidentEntry* const entry = nolock_add_resident(key, T2, std::move(value));

            if (entry == nullptr)
                return Return{std::move(value)};

            nolock_trim_ghosts(capacity);
            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        ResidentEntry* const entry = nolock_add_resident(key, T1, std::move(value));

        nolock_trim_ghosts(capacity);
        assert(nolock_consistency_check());

        if (entry == nullptr)
            return Return{std::move(value)};

        return nolock_read(entry);
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        if (const auto itr = residentMap.find(key); itr != residentMap.end())
        {
            ResidentEntry* const entry = itr->second;

            nolock_promote_to_t2(*entry);

            return nolock_read(entry);
        }

        return nolock_write_after_resident_miss(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        if (const auto itr = residentMap.find(key); itr != residentMap.end())
        {
            ResidentEntry* const entry = itr->second;

            nolock_promote_to_t2(*entry);

            return nolock_read(entry);
        }

        return nolock_write_with_after_resident_miss(key, std::forward<Builder>(builder),
                                                     std::forward<Args>(args)...);
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = residentMap.find(key);

        if (mapItr == residentMap.end())
        {
            assert(nolock_consistency_check());
            return false;
        }

        nolock_remove_resident(mapItr);

        assert(nolock_consistency_check());

        return true;
    }

    // Administrative eviction removes residents without creating ghosts.
    void nolock_evict_excess(const usize size) noexcept {
        while (residentMap.size() > size)
        {
            list_type& t1 = buckets[T1].entries;
            list_type& t2 = buckets[T2].entries;

            Entry* entry = nullptr;

            if (!t1.empty() && (t1.size() > target || t2.empty()))
                entry = t1.back();
            else if (!t2.empty())
                entry = t2.back();
            else
                entry = t1.back();

            assert(entry != nullptr);

            if (entry == nullptr)
                break;

            nolock_remove_resident(*static_cast<ResidentEntry*>(entry));
        }

        if (target > capacity)
            target = capacity;

        nolock_trim_ghosts(capacity);
        assert(nolock_consistency_check());
    }

    void nolock_clear() noexcept {
        for (const usize index : {T1, T2})
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                [[maybe_unused]]
                const bool erased = list.erase(*entry);
                assert(erased);

                entry->bucket = nullptr;
                residentPool.destroy(static_cast<ResidentEntry*>(entry));
            }
        }

        for (const usize index : {B1, B2})
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                [[maybe_unused]]
                const bool erased = list.erase(*entry);
                assert(erased);

                entry->bucket = nullptr;
                ghostPool.destroy(static_cast<GhostEntry*>(entry));
            }
        }

        residentMap.clear();
        ghostMap.clear();

        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_release_pool() noexcept {
        residentPool.release();
        ghostPool.release();
    }

    void nolock_configure() noexcept {
        residentMap.max_load_factor(maxLoadFactor);
        residentMap.reserve(reserveCount);

        ghostMap.max_load_factor(maxLoadFactor);
        ghostMap.reserve(reserveCount);
    }

#ifndef NDEBUG
    bool nolock_consistency_check() const noexcept {
        std::unordered_set<const Entry*> visitedEntries;
        usize                            residentCount = 0;
        usize                            ghostCount    = 0;

        for (usize bucketIndex = 0; bucketIndex < BucketCount; ++bucketIndex)
        {
            const Bucket& bucket      = buckets[bucketIndex];
            const Entry*  previous    = nullptr;
            usize         bucketCount = 0;

            for (const Entry* entry = bucket.entries.front(); entry != nullptr;
                 entry              = bucket.entries.next(*entry))
            {
                assert(entry->bucket == &bucket);
                assert(entry->prev == previous);

                if (entry->next != nullptr)
                    assert(entry->next->prev == entry);

                assert(visitedEntries.insert(entry).second);

                if (bucketIndex == T1 || bucketIndex == T2)
                {
                    const auto mapItr = residentMap.find(entry->key);

                    assert(mapItr != residentMap.end());
                    assert(mapItr->second == static_cast<const ResidentEntry*>(entry));

                    assert(ghostMap.find(entry->key) == ghostMap.end());
                    ++residentCount;
                }
                else
                {
                    const auto mapItr = ghostMap.find(entry->key);

                    assert(mapItr != ghostMap.end());
                    assert(mapItr->second == static_cast<const GhostEntry*>(entry));

                    assert(residentMap.find(entry->key) == residentMap.end());
                    ++ghostCount;
                }

                previous = entry;
                ++bucketCount;
            }

            assert(bucketCount == bucket.entries.size());
            assert(bucket.entries.empty() == (bucket.entries.front() == nullptr));

            if (previous != nullptr)
                assert(previous->next == nullptr);
        }

        assert(residentCount == residentMap.size());
        assert(ghostCount == ghostMap.size());

        assert(residentCount <= capacity);
        assert(ghostCount <= capacity);

        return true;
    }
#endif

    // ---------- Configuration ----------

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    // ---------- Synchronization ----------

    mutable Mutex residentMutex;
    mutable Mutex ghostMutex;

    // ---------- Maps and pools ----------

    resident_map_type residentMap;
    ghost_map_type    ghostMap;

    Pool<ResidentEntry> residentPool;
    Pool<GhostEntry>    ghostPool;

    // ---------- ARC state ----------

    usize capacity;
    usize target = 0;

    static constexpr usize BucketCount = 4;

    static constexpr usize B1 = 0;
    static constexpr usize T1 = 1;
    static constexpr usize T2 = 2;
    static constexpr usize B2 = 3;

    Array<Bucket, BucketCount> buckets;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatARCCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Bucket;

    struct Entry {
        Key     key;
        Bucket* bucket = nullptr;
        Entry*  prev   = nullptr;
        Entry*  next   = nullptr;

        explicit Entry(const Key& k) noexcept :
            key(k) {}
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;

    struct ResidentEntry final: Entry {
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit ResidentEntry(const Key& k, Args&&... args) noexcept :
            Entry(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        storage_value_type value;
    };

    struct GhostEntry final: Entry {
        explicit GhostEntry(const Key& k) noexcept :
            Entry(k) {}
    };

    struct Bucket {
        list_type entries;
    };

    using resident_map_type = ankerl::unordered_dense::map<Key, ResidentEntry*>;
    using ghost_map_type    = ankerl::unordered_dense::map<Key, GhostEntry*>;

    template<typename Map>
    class MapGuard final {
       public:
        MapGuard(Map& mapRef, typename Map::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        Map&                   map;
        typename Map::iterator iterator;
        bool                   active = true;
    };

   public:
    explicit PoolFlatARCCache(const usize cap        = 2 * KB,
                              const usize reserveCnt = 1 * KB,
                              const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    PoolFlatARCCache(const PoolFlatARCCache&) noexcept            = delete;
    PoolFlatARCCache& operator=(const PoolFlatARCCache&) noexcept = delete;
    PoolFlatARCCache(PoolFlatARCCache&&) noexcept                 = delete;
    PoolFlatARCCache& operator=(PoolFlatARCCache&&) noexcept      = delete;

    ~PoolFlatARCCache() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_clear();

        nolock_release_pool();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        std::lock_guard residentLock{residentMutex};

        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto mapItr = residentMap.find(key);

        if (mapItr != residentMap.end())
        {
            ResidentEntry* const entry = mapItr->second;

            nolock_promote_to_t2(*entry);

            return nolock_read(entry);
        }

        std::lock_guard ghostLock{ghostMutex};

        return nolock_write_after_resident_miss(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        std::lock_guard residentLock{residentMutex};

        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto mapItr = residentMap.find(key);

        if (mapItr != residentMap.end())
        {
            ResidentEntry* const entry = mapItr->second;

            nolock_promote_to_t2(*entry);

            return nolock_read(entry);
        }

        std::lock_guard ghostLock{ghostMutex};

        return nolock_write_with_after_resident_miss(key, std::forward<Builder>(builder),
                                                     std::forward<Args>(args)...);
    }

    void evict_excess(const usize size) noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_evict_excess(size);
    }

    void evict_excess() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_evict_excess(capacity);
    }

    [[nodiscard]] usize get_capacity() const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return capacity;
    }

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        capacity = newCapacity;

        if (target > capacity)
            target = capacity;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.find(key) != residentMap.end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{residentMutex};

        return residentMap.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        return nolock_remove(key);
    }

    void clear() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_configure();
        assert(nolock_consistency_check());
    }

    void reset() noexcept {
        std::lock_guard residentLock{residentMutex};
        std::lock_guard ghostLock{ghostMutex};

        nolock_clear();
        nolock_release_pool();

        residentMap.rehash(0);
        ghostMap.rehash(0);

        capacity = defaultCapacity;
        target   = 0;

        nolock_configure();
        assert(nolock_consistency_check());
    }

   private:
    // ---------- Bucket access ----------

    [[nodiscard]] Bucket& nolock_bucket(const usize index) noexcept { return buckets[index]; }

    [[nodiscard]] const Bucket& nolock_bucket(const usize index) const noexcept {
        return buckets[index];
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return buckets[T1].entries.size() + buckets[T2].entries.size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return buckets[B1].entries.size() + buckets[B2].entries.size();
    }

    [[nodiscard]] usize nolock_directory_size() const noexcept {
        return nolock_resident_size() + nolock_ghost_size();
    }

    [[nodiscard]] usize nolock_required_size() const noexcept {
        return usize{1}
             + (capacity > residentMap.size() ? capacity - residentMap.size() : usize{0});
    }

    [[nodiscard]] usize nolock_ghost_required_size() const noexcept {
        return usize{1} + (capacity > ghostMap.size() ? capacity - ghostMap.size() : usize{0});
    }

    Return nolock_read(const ResidentEntry* entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return entry->value;
        else
            return *entry->value;
    }

    // ---------- Shared intrusive-list operations ----------

    void nolock_list_push_front(Entry& entry, const usize index) noexcept {
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.bucket == nullptr);

        Bucket& bucket = nolock_bucket(index);

        [[maybe_unused]]
        const bool inserted = bucket.entries.push_front(entry);
        assert(inserted);

        if (inserted)
            entry.bucket = &bucket;
    }

    void nolock_list_remove(Entry& entry) noexcept {
        assert(entry.bucket != nullptr);

        if (entry.bucket == nullptr)
            return;

        [[maybe_unused]]
        const bool erased = entry.bucket->entries.erase(entry);
        assert(erased);

        if (erased)
            entry.bucket = nullptr;
    }

    void nolock_list_move_front(Entry& entry) noexcept {
        assert(entry.bucket != nullptr);

        if (entry.bucket == nullptr)
            return;

        [[maybe_unused]]
        const bool moved = entry.bucket->entries.move_front(entry);
        assert(moved);
    }

    // T2 is MRU-first. Avoid touching the list when the entry is
    // already at the front; this is the common repeated-hot-key case.
    void nolock_promote_to_t2(ResidentEntry& entry) noexcept {
        if (entry.bucket == &buckets[T2])
        {
            if (entry.prev != nullptr)
                nolock_list_move_front(entry);

            return;
        }

        assert(entry.bucket == &buckets[T1]);

        if (entry.bucket != &buckets[T1])
            return;

        nolock_list_remove(entry);
        nolock_list_push_front(entry, T2);
    }

    // ---------- Entry creation ----------

    template<typename... Args>
    ResidentEntry* nolock_add_resident(const Key& key, const usize index, Args&&... args) noexcept {
        const auto [mapItr, inserted] = residentMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return mapItr->second;

        MapGuard<resident_map_type> mapGuard{residentMap, mapItr};

        const usize requiredSize = residentPool.empty() ? nolock_required_size() : 0;

        ResidentEntry* const entry =
          residentPool.create(requiredSize, key, std::forward<Args>(args)...);

        if (entry == nullptr)
            return nullptr;

        mapItr->second = entry;

        nolock_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    GhostEntry* nolock_add_ghost(const Key& key, const usize index) noexcept {
        const auto [mapItr, inserted] = ghostMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return nullptr;

        MapGuard<ghost_map_type> mapGuard{ghostMap, mapItr};

        const usize requiredSize = ghostPool.empty() ? nolock_ghost_required_size() : 0;

        GhostEntry* const entry = ghostPool.create(requiredSize, key);

        if (entry == nullptr)
            return nullptr;

        mapItr->second = entry;

        nolock_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    // ---------- Entry destruction ----------

    void nolock_remove_resident(typename resident_map_type::iterator mapItr) noexcept {
        assert(mapItr != residentMap.end());

        if (mapItr == residentMap.end())
            return;

        ResidentEntry* const entry = mapItr->second;

        assert(entry != nullptr);

        if (entry == nullptr)
            return;

        nolock_list_remove(*entry);

        residentMap.erase(mapItr);
        residentPool.destroy(entry);
    }

    void nolock_remove_resident(ResidentEntry& entry) noexcept {
        const auto mapItr = residentMap.find(entry.key);

        assert(mapItr != residentMap.end());

        if (mapItr == residentMap.end())
            return;

        assert(mapItr->second == &entry);

        if (mapItr->second != &entry)
            return;

        nolock_remove_resident(mapItr);
    }

    void nolock_remove_ghost(typename ghost_map_type::iterator mapItr) noexcept {
        assert(mapItr != ghostMap.end());

        if (mapItr == ghostMap.end())
            return;

        GhostEntry* const entry = mapItr->second;

        assert(entry != nullptr);

        if (entry == nullptr)
            return;

        nolock_list_remove(*entry);

        ghostMap.erase(mapItr);
        ghostPool.destroy(entry);
    }

    void nolock_remove_ghost(GhostEntry& entry) noexcept {
        const auto mapItr = ghostMap.find(entry.key);

        assert(mapItr != ghostMap.end());

        if (mapItr == ghostMap.end())
            return;

        assert(mapItr->second == &entry);

        if (mapItr->second != &entry)
            return;

        nolock_remove_ghost(mapItr);
    }

    // ---------- ARC replacement ----------

    void nolock_evict_to_ghost(ResidentEntry& entry, const usize index) noexcept {
        GhostEntry* const ghost = nolock_add_ghost(entry.key, index);

        assert(ghost != nullptr);

        if (ghost == nullptr)
            return;

        nolock_remove_resident(entry);
    }

    void nolock_replace(const bool keyInB2) noexcept {
        list_type& t1 = buckets[T1].entries;
        list_type& t2 = buckets[T2].entries;

        const bool evictT1 =
          !t1.empty() && (t1.size() > target || (keyInB2 && t1.size() == target));

        if (evictT1)
        {
            Entry* const entry = t1.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*static_cast<ResidentEntry*>(entry), B1);

            return;
        }

        if (!t2.empty())
        {
            Entry* const entry = t2.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*static_cast<ResidentEntry*>(entry), B2);

            return;
        }

        if (!t1.empty())
        {
            Entry* const entry = t1.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*static_cast<ResidentEntry*>(entry), B1);
        }
    }

    // Keep only the total ghost-history limit.
    // No T1+B1 or T2+B2 pairwise capacity restriction is imposed.
    void nolock_trim_ghosts(const usize maxSize) noexcept {
        while (ghostMap.size() > maxSize)
        {
            Entry* entry = buckets[B1].entries.back();

            if (entry == nullptr)
                entry = buckets[B2].entries.back();

            assert(entry != nullptr);

            if (entry == nullptr)
                break;

            nolock_remove_ghost(*static_cast<GhostEntry*>(entry));
        }
    }

    void nolock_handle_ghost_hit(typename ghost_map_type::iterator mapItr) noexcept {
        assert(mapItr != ghostMap.end());

        if (mapItr == ghostMap.end())
            return;

        GhostEntry* const ghost = mapItr->second;

        assert(ghost != nullptr);

        if (ghost == nullptr)
            return;

        Bucket* const sourceBucket = ghost->bucket;

        assert(sourceBucket == &buckets[B1] || sourceBucket == &buckets[B2]);

        if (sourceBucket != &buckets[B1] && sourceBucket != &buckets[B2])
            return;

        assert(target <= capacity);

        if (target > capacity)
            target = capacity;

        const usize b1Size = buckets[B1].entries.size();
        const usize b2Size = buckets[B2].entries.size();

        const bool keyInB2 = sourceBucket == &buckets[B2];

        if (keyInB2)
        {
            assert(b2Size != 0);

            if (b2Size == 0)
                return;

            const usize delta = std::max(usize{1}, b1Size / b2Size);

            target = delta < target ? target - delta : usize{0};
        }
        else
        {
            assert(b1Size != 0);

            if (b1Size == 0)
                return;

            const usize delta = std::max(usize{1}, b2Size / b1Size);

            target += std::min(delta, capacity - target);
        }

        // Erase before replacement can insert another ghost and rehash ghostMap.
        nolock_remove_ghost(mapItr);

        if (residentMap.size() >= capacity && capacity != 0)
            nolock_replace(keyInB2);
    }

    void nolock_prepare_miss() noexcept {
        if (capacity != 0 && residentMap.size() >= capacity)
            nolock_replace(false);
    }

    // ---------- Read/write internals ----------

    // The caller has already checked residentMap while holding residentMutex.
    // ghostMutex is held by the caller.
    template<typename... Args>
    Return nolock_write_after_resident_miss(const Key& key, Args&&... args) noexcept {
        if (const auto itr = ghostMap.find(key); itr != ghostMap.end())
        {
            nolock_handle_ghost_hit(itr);

            ResidentEntry* const entry = nolock_add_resident(key, T2, std::forward<Args>(args)...);

            if (entry == nullptr)
                return Return{std::forward<Args>(args)...};

            nolock_trim_ghosts(capacity);
            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        ResidentEntry* const entry = nolock_add_resident(key, T1, std::forward<Args>(args)...);

        nolock_trim_ghosts(capacity);
        assert(nolock_consistency_check());

        if (entry == nullptr)
            return Return{std::forward<Args>(args)...};

        return nolock_read(entry);
    }

    // The caller has already checked residentMap while holding residentMutex.
    // ghostMutex is held by the caller.
    template<typename Builder, typename... Args>
    Return nolock_write_with_after_resident_miss(const Key& key,
                                                 Builder&&  builder,
                                                 Args&&... args) noexcept {
        // Evaluate the builder once and retain the value for allocation failure.
        Value value(std::forward<Builder>(builder)(std::forward<Args>(args)...));

        if (const auto itr = ghostMap.find(key); itr != ghostMap.end())
        {
            nolock_handle_ghost_hit(itr);

            ResidentEntry* const entry = nolock_add_resident(key, T2, std::move(value));

            if (entry == nullptr)
                return Return{std::move(value)};

            nolock_trim_ghosts(capacity);
            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        ResidentEntry* const entry = nolock_add_resident(key, T1, std::move(value));

        nolock_trim_ghosts(capacity);
        assert(nolock_consistency_check());

        if (entry == nullptr)
            return Return{std::move(value)};

        return nolock_read(entry);
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        if (const auto itr = residentMap.find(key); itr != residentMap.end())
        {
            ResidentEntry* const entry = itr->second;

            nolock_promote_to_t2(*entry);

            return nolock_read(entry);
        }

        return nolock_write_after_resident_miss(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        if (const auto itr = residentMap.find(key); itr != residentMap.end())
        {
            ResidentEntry* const entry = itr->second;

            nolock_promote_to_t2(*entry);

            return nolock_read(entry);
        }

        return nolock_write_with_after_resident_miss(key, std::forward<Builder>(builder),
                                                     std::forward<Args>(args)...);
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = residentMap.find(key);

        if (mapItr == residentMap.end())
        {
            assert(nolock_consistency_check());
            return false;
        }

        nolock_remove_resident(mapItr);

        assert(nolock_consistency_check());

        return true;
    }

    // Administrative eviction removes residents without creating ghosts.
    void nolock_evict_excess(const usize size) noexcept {
        while (residentMap.size() > size)
        {
            list_type& t1 = buckets[T1].entries;
            list_type& t2 = buckets[T2].entries;

            Entry* entry = nullptr;

            if (!t1.empty() && (t1.size() > target || t2.empty()))
                entry = t1.back();
            else if (!t2.empty())
                entry = t2.back();
            else
                entry = t1.back();

            assert(entry != nullptr);

            if (entry == nullptr)
                break;

            nolock_remove_resident(*static_cast<ResidentEntry*>(entry));
        }

        if (target > capacity)
            target = capacity;

        nolock_trim_ghosts(capacity);
        assert(nolock_consistency_check());
    }

    void nolock_clear() noexcept {
        for (const usize index : {T1, T2})
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                [[maybe_unused]]
                const bool erased = list.erase(*entry);
                assert(erased);

                entry->bucket = nullptr;
                residentPool.destroy(static_cast<ResidentEntry*>(entry));
            }
        }

        for (const usize index : {B1, B2})
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                [[maybe_unused]]
                const bool erased = list.erase(*entry);
                assert(erased);

                entry->bucket = nullptr;
                ghostPool.destroy(static_cast<GhostEntry*>(entry));
            }
        }

        residentMap.clear();
        ghostMap.clear();

        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_release_pool() noexcept {
        residentPool.release();
        ghostPool.release();
    }

    void nolock_configure() noexcept {
        residentMap.max_load_factor(maxLoadFactor);
        residentMap.reserve(reserveCount);

        ghostMap.max_load_factor(maxLoadFactor);
        ghostMap.reserve(reserveCount);
    }

#ifndef NDEBUG
    bool nolock_consistency_check() const noexcept {
        std::unordered_set<const Entry*> visitedEntries;
        usize                            residentCount = 0;
        usize                            ghostCount    = 0;

        for (usize bucketIndex = 0; bucketIndex < BucketCount; ++bucketIndex)
        {
            const Bucket& bucket      = buckets[bucketIndex];
            const Entry*  previous    = nullptr;
            usize         bucketCount = 0;

            for (const Entry* entry = bucket.entries.front(); entry != nullptr;
                 entry              = bucket.entries.next(*entry))
            {
                assert(entry->bucket == &bucket);
                assert(entry->prev == previous);

                if (entry->next != nullptr)
                    assert(entry->next->prev == entry);

                assert(visitedEntries.insert(entry).second);

                if (bucketIndex == T1 || bucketIndex == T2)
                {
                    const auto mapItr = residentMap.find(entry->key);

                    assert(mapItr != residentMap.end());
                    assert(mapItr->second == static_cast<const ResidentEntry*>(entry));

                    assert(ghostMap.find(entry->key) == ghostMap.end());
                    ++residentCount;
                }
                else
                {
                    const auto mapItr = ghostMap.find(entry->key);

                    assert(mapItr != ghostMap.end());
                    assert(mapItr->second == static_cast<const GhostEntry*>(entry));

                    assert(residentMap.find(entry->key) == residentMap.end());
                    ++ghostCount;
                }

                previous = entry;
                ++bucketCount;
            }

            assert(bucketCount == bucket.entries.size());
            assert(bucket.entries.empty() == (bucket.entries.front() == nullptr));

            if (previous != nullptr)
                assert(previous->next == nullptr);
        }

        assert(residentCount == residentMap.size());
        assert(ghostCount == ghostMap.size());

        assert(residentCount <= capacity);
        assert(ghostCount <= capacity);

        return true;
    }
#endif

    // ---------- Configuration ----------

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    // ---------- Synchronization ----------

    mutable Mutex residentMutex;
    mutable Mutex ghostMutex;

    // ---------- Maps and pools ----------

    resident_map_type residentMap;
    ghost_map_type    ghostMap;

    Pool<ResidentEntry> residentPool;
    Pool<GhostEntry>    ghostPool;

    // ---------- ARC state ----------

    usize capacity;
    usize target = 0;

    static constexpr usize BucketCount = 4;

    static constexpr usize B1 = 0;
    static constexpr usize T1 = 1;
    static constexpr usize T2 = 2;
    static constexpr usize B2 = 3;

    Array<Bucket, BucketCount> buckets;
};

}  // namespace DON

#endif  // UTILITY_ARC_CACHE_H_INCLUDED
