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
#include <optional>
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

    static constexpr usize PoolCount   = 2;
    static constexpr usize BucketCount = 4;

    static constexpr usize T1 = 0;
    static constexpr usize T2 = 1;
    static constexpr usize B1 = 2;
    static constexpr usize B2 = 3;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Bucket;

    struct Entry {
        Key     key;
        usize   poolIndex = 0;
        Bucket* bucket    = nullptr;
        Entry*  prev      = nullptr;
        Entry*  next      = nullptr;

        explicit Entry(const Key& k) noexcept :
            key(k) {}
    };

    using list_type = DoublyIntrusiveList<Entry>;

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

    using directory_map_type = std::unordered_map<Key, Entry*>;

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

        MapGuard(const MapGuard&)            = delete;
        MapGuard& operator=(const MapGuard&) = delete;

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

    PoolARCCache(const PoolARCCache&)            = delete;
    PoolARCCache& operator=(const PoolARCCache&) = delete;
    PoolARCCache(PoolARCCache&&)                 = delete;
    PoolARCCache& operator=(PoolARCCache&&)      = delete;

    ~PoolARCCache() noexcept {
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

        if (target > capacity)
            target = capacity;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        auto mapItr = directoryMap.find(key);

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
            return directoryMap.end();

        return mapItr;
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        auto mapItr = directoryMap.find(key);

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
            return directoryMap.end();

        return mapItr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = directoryMap.find(key);

        return mapItr != directoryMap.end() && mapItr->second != nullptr
            && nolock_is_resident(mapItr->second);
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        assert(nolock_consistency_check());

        return nolock_resident_size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};

        assert(nolock_consistency_check());

        return nolock_resident_size() == 0;
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    // Clears entries but retains pooled storage for reuse.
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();

        assert(nolock_consistency_check());
    }

    // Releases pooled storage and restores the original configuration.
    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        nolock_release_pool();

        directoryMap.rehash(0);

        capacity = defaultCapacity;
        target   = 0;

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    // ---------- Bucket and entry access ----------

    [[nodiscard]] list_type& nolock_list(const usize index) noexcept {
        return buckets[index].entries;
    }

    [[nodiscard]] const list_type& nolock_list(const usize index) const noexcept {
        return buckets[index].entries;
    }

    [[nodiscard]] bool nolock_is_resident(const Entry* entry) const noexcept {
        return entry->bucket == &buckets[T1] || entry->bucket == &buckets[T2];
    }

    [[nodiscard]] usize nolock_bucket_index(const Entry* entry) const noexcept {
        if (entry->bucket == &buckets[T1])
            return T1;

        if (entry->bucket == &buckets[T2])
            return T2;

        if (entry->bucket == &buckets[B1])
            return B1;

        if (entry->bucket == &buckets[B2])
            return B2;

        assert(false);
        return T1;
    }

    [[nodiscard]] ResidentEntry* nolock_resident(Entry* entry) noexcept {
        assert(nolock_is_resident(entry));

        return static_cast<ResidentEntry*>(entry);
    }

    [[nodiscard]] const ResidentEntry* nolock_resident(const Entry* entry) const noexcept {
        assert(nolock_is_resident(entry));

        return static_cast<const ResidentEntry*>(entry);
    }

    [[nodiscard]] GhostEntry* nolock_ghost(Entry* entry) noexcept {
        assert(!nolock_is_resident(entry));

        return static_cast<GhostEntry*>(entry);
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return buckets[T1].entries.size() + buckets[T2].entries.size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return buckets[B1].entries.size() + buckets[B2].entries.size();
    }

    [[nodiscard]] usize nolock_directory_size() const noexcept { return directoryMap.size(); }

    Return nolock_read(const ResidentEntry* entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return entry->value;
        else
            return *entry->value;
    }

    // ---------- List operations ----------

    void nolock_list_push_front(Entry& entry, const usize index) noexcept {
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.bucket == nullptr);
        assert(index < BucketCount);

        [[maybe_unused]] const bool inserted = nolock_list(index).push_front(entry);
        assert(inserted);

        if (inserted)
            entry.bucket = &buckets[index];
    }

    void nolock_list_remove(Entry& entry) noexcept {
        assert(entry.bucket != nullptr);

        if (entry.bucket == nullptr)
            return;

        [[maybe_unused]] const bool erased = entry.bucket->entries.erase(entry);
        assert(erased);

        if (erased)
            entry.bucket = nullptr;
    }

    void nolock_list_move_front(Entry& entry) noexcept {
        assert(entry.bucket != nullptr);

        if (entry.bucket == nullptr)
            return;

        [[maybe_unused]] const bool moved = entry.bucket->entries.move_front(entry);
        assert(moved);
    }

    void nolock_promote_to_t2(ResidentEntry& entry) noexcept {
        if (entry.bucket == &buckets[T2])
        {
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
        assert(index == T1 || index == T2);

        const auto [mapItr, inserted] = directoryMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
        {
            if (mapItr->second != nullptr && nolock_is_resident(mapItr->second))
                return nolock_resident(mapItr->second);

            return nullptr;
        }

        MapGuard<directory_map_type> mapGuard{directoryMap, mapItr};

        const usize poolIndex = index - T1;
        auto&       pool      = residentPools[poolIndex];

        ResidentEntry* const entry = pool.create(0, key, std::forward<Args>(args)...);

        if (entry == nullptr)
            return nullptr;

        entry->poolIndex = poolIndex;
        mapItr->second   = entry;

        nolock_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    GhostEntry* nolock_add_ghost(const Key& key, const usize index) noexcept {
        assert(index == B1 || index == B2);

        const auto [mapItr, inserted] = directoryMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return nullptr;

        MapGuard<directory_map_type> mapGuard{directoryMap, mapItr};

        const usize poolIndex = index - B1;
        auto&       pool      = ghostPools[poolIndex];

        GhostEntry* const entry = pool.create(0, key);

        if (entry == nullptr)
            return nullptr;

        entry->poolIndex = poolIndex;
        mapItr->second   = entry;

        nolock_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    // Convert an existing ghost into a resident without erasing or
    // reinserting its directoryMap node. The caller holds the cache lock.
    template<typename... Args>
    ResidentEntry* nolock_ghost_to_resident(typename directory_map_type::iterator mapItr,
                                            Args&&... args) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return nullptr;

        // Replacement must have freed a resident slot.
        if (capacity == 0 || nolock_resident_size() >= capacity)
            return nullptr;

        GhostEntry* const ghost = nolock_ghost(mapItr->second);

        const usize     ghostPoolIndex    = ghost->poolIndex;
        constexpr usize residentPoolIndex = T2 - T1;

        assert(ghostPoolIndex < PoolCount);

        if (ghostPoolIndex >= PoolCount)
            return nullptr;

        // Allocate before changing the ghost or its directory entry.
        ResidentEntry* const entry =
          residentPools[residentPoolIndex].create(0, ghost->key, std::forward<Args>(args)...);

        if (entry == nullptr)
            return nullptr;

        entry->poolIndex = residentPoolIndex;

        // Preserve the existing directory node.
        nolock_list_remove(*ghost);
        mapItr->second = entry;
        nolock_list_push_front(*entry, T2);

        ghostPools[ghostPoolIndex].destroy(ghost);

        return entry;
    }

    // ---------- Entry destruction ----------

    void nolock_remove_resident(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
            return;

        ResidentEntry* const entry     = nolock_resident(mapItr->second);
        const usize          poolIndex = entry->poolIndex;

        assert(poolIndex < PoolCount);

        if (poolIndex >= PoolCount)
            return;

        nolock_list_remove(*entry);
        directoryMap.erase(mapItr);

        residentPools[poolIndex].destroy(entry);
    }

    void nolock_remove_resident(ResidentEntry& entry) noexcept {
        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return;

        nolock_remove_resident(mapItr);
    }

    void nolock_remove_ghost(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return;

        GhostEntry* const entry     = nolock_ghost(mapItr->second);
        const usize       poolIndex = entry->poolIndex;

        assert(poolIndex < PoolCount);

        if (poolIndex >= PoolCount)
            return;

        nolock_list_remove(*entry);
        directoryMap.erase(mapItr);

        ghostPools[poolIndex].destroy(entry);
    }

    void nolock_remove_ghost(GhostEntry& entry) noexcept {
        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return;

        nolock_remove_ghost(mapItr);
    }

    // ---------- Resident-to-ghost transition ----------

    void nolock_evict_to_ghost(ResidentEntry& entry, const usize index) noexcept {
        assert(index == B1 || index == B2);

        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());
        assert(mapItr != directoryMap.end() && mapItr->second == &entry);

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return;

        const usize ghostPoolIndex = index - B1;
        auto&       ghostPool      = ghostPools[ghostPoolIndex];

        GhostEntry* const ghost = ghostPool.create(0, entry.key);

        assert(ghost != nullptr);

        if (ghost == nullptr)
            return;

        ghost->poolIndex = ghostPoolIndex;

        // Replace the mapped pointer before destroying the resident.
        mapItr->second = ghost;

        const usize residentPoolIndex = entry.poolIndex;

        assert(residentPoolIndex < PoolCount);

        if (residentPoolIndex >= PoolCount)
        {
            mapItr->second = &entry;
            ghostPool.destroy(ghost);
            return;
        }

        nolock_list_remove(entry);
        nolock_list_push_front(*ghost, index);

        residentPools[residentPoolIndex].destroy(&entry);
    }

    // ---------- ARC replacement ----------

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
                nolock_evict_to_ghost(*nolock_resident(entry), B1);

            return;
        }

        if (!t2.empty())
        {
            Entry* const entry = t2.back();

            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*nolock_resident(entry), B2);

            return;
        }

        if (!t1.empty())
        {
            Entry* const entry = t1.back();

            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*nolock_resident(entry), B1);
        }
    }

    // ---------- Ghost history management ----------

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        while (nolock_ghost_size() > maxSize)
        {
            Entry* entry = buckets[B1].entries.back();

            if (entry == nullptr)
                entry = buckets[B2].entries.back();

            assert(entry != nullptr);

            if (entry == nullptr)
                break;

            nolock_remove_ghost(*nolock_ghost(entry));
        }
    }

    void nolock_handle_ghost_hit(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return;

        GhostEntry* const ghost = nolock_ghost(mapItr->second);
        const usize       index = nolock_bucket_index(ghost);

        assert(index == B1 || index == B2);

        if (index != B1 && index != B2)
            return;

        if (target > capacity)
            target = capacity;

        const usize b1Size = buckets[B1].entries.size();
        const usize b2Size = buckets[B2].entries.size();

        const bool keyInB2 = index == B2;

        if (keyInB2)
        {
            if (b2Size == 0)
                return;

            const usize delta = std::max(usize{1}, b1Size / b2Size);

            target = delta < target ? target - delta : usize{0};
        }
        else
        {
            if (b1Size == 0)
                return;

            const usize delta = std::max(usize{1}, b2Size / b1Size);

            target += std::min(delta, capacity - target);
        }

        // Keep the ghost and its directory node alive until conversion.
        // Replacement only changes other entries' mapped pointers.
        if (nolock_resident_size() >= capacity)
            nolock_replace(keyInB2);
    }

    // ---------- Miss preparation ----------

    void nolock_prepare_miss() noexcept {
        if (capacity == 0 || nolock_resident_size() < capacity)
            return;

        const usize t1Size = buckets[T1].entries.size();
        const usize b1Size = buckets[B1].entries.size();

        if (t1Size > capacity || b1Size > capacity || nolock_ghost_size() > capacity)
            return;

        if (t1Size == capacity - b1Size)
        {
            if (t1Size < capacity)
            {
                Entry* const ghost = buckets[B1].entries.back();

                assert(ghost != nullptr);

                if (ghost == nullptr)
                    return;

                nolock_remove_ghost(*nolock_ghost(ghost));
                nolock_replace(false);
            }
            else
            {
                Entry* const entry = buckets[T1].entries.back();

                assert(entry != nullptr);

                if (entry == nullptr)
                    return;

                nolock_remove_resident(*nolock_resident(entry));
            }

            return;
        }

        if (capacity <= std::numeric_limits<usize>::max() / 2
            && nolock_directory_size() >= capacity * 2)
        {
            Entry* ghost = buckets[B1].entries.back();

            if (ghost == nullptr)
                ghost = buckets[B2].entries.back();

            if (ghost != nullptr)
                nolock_remove_ghost(*nolock_ghost(ghost));
        }

        nolock_replace(false);
    }

    // ---------- Public-operation internals ----------

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        if (const auto mapItr = directoryMap.find(key); mapItr != directoryMap.end())
        {
            if (mapItr->second == nullptr)
                return Return{std::forward<Args>(args)...};

            if (nolock_is_resident(mapItr->second))
            {
                ResidentEntry* const entry = nolock_resident(mapItr->second);

                nolock_promote_to_t2(*entry);

                assert(nolock_consistency_check());

                return nolock_read(entry);
            }

            nolock_handle_ghost_hit(mapItr);

            ResidentEntry* const entry =
              nolock_ghost_to_resident(mapItr, std::forward<Args>(args)...);

            assert(entry != nullptr);

            if (entry == nullptr)
                return Return{std::forward<Args>(args)...};

            nolock_trim_ghosts(capacity);

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{std::forward<Args>(args)...};

        ResidentEntry* const entry = nolock_add_resident(key, T1, std::forward<Args>(args)...);

        assert(entry != nullptr);

        if (entry == nullptr)
            return Return{std::forward<Args>(args)...};

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        if (const auto mapItr = directoryMap.find(key); mapItr != directoryMap.end())
        {
            if (mapItr->second == nullptr)
                return Return{};

            if (nolock_is_resident(mapItr->second))
            {
                ResidentEntry* const entry = nolock_resident(mapItr->second);

                nolock_promote_to_t2(*entry);

                assert(nolock_consistency_check());

                return nolock_read(entry);
            }

            nolock_handle_ghost_hit(mapItr);

            ResidentEntry* const entry = nolock_ghost_to_resident(
              mapItr, std::forward<Builder>(builder)(std::forward<Args>(args)...));

            assert(entry != nullptr);

            if (entry == nullptr)
                return Return{};

            nolock_trim_ghosts(capacity);

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{};

        ResidentEntry* const entry =
          nolock_add_resident(key, T1, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        assert(entry != nullptr);

        if (entry == nullptr)
            return Return{};

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());

        return nolock_read(entry);
    }

    [[nodiscard]] bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = directoryMap.find(key);

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
        {
            assert(nolock_consistency_check());
            return false;
        }

        nolock_remove_resident(mapItr);

        assert(nolock_consistency_check());

        return true;
    }

    // Administrative eviction removes resident entries without adding ghosts.
    void nolock_evict_excess(const usize size) noexcept {
        while (nolock_resident_size() > size)
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

            nolock_remove_resident(*nolock_resident(entry));
        }

        if (target > capacity)
            target = capacity;

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());
    }

    void nolock_clear() noexcept {
        for (usize index = T1; index <= T2; ++index)
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                [[maybe_unused]] const bool erased = list.erase(*entry);
                assert(erased);

                const usize poolIndex = entry->poolIndex;

                assert(poolIndex < PoolCount);

                entry->bucket = nullptr;

                if (poolIndex < PoolCount)
                    residentPools[poolIndex].destroy(static_cast<ResidentEntry*>(entry));
            }
        }

        for (usize index = B1; index <= B2; ++index)
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                [[maybe_unused]] const bool erased = list.erase(*entry);
                assert(erased);

                const usize poolIndex = entry->poolIndex;

                assert(poolIndex < PoolCount);

                entry->bucket = nullptr;

                if (poolIndex < PoolCount)
                    ghostPools[poolIndex].destroy(static_cast<GhostEntry*>(entry));
            }
        }

        directoryMap.clear();
        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_release_pool() noexcept {
        for (auto& pool : residentPools)
            pool.release();

        for (auto& pool : ghostPools)
            pool.release();
    }

    void nolock_configure() noexcept {
        directoryMap.max_load_factor(maxLoadFactor);
        directoryMap.reserve(reserveCount);
    }

#ifndef NDEBUG
    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        const usize residentSize = nolock_resident_size();
        const usize ghostSize    = nolock_ghost_size();

        assert(residentSize + ghostSize == directoryMap.size());
        assert(residentSize <= capacity);
        assert(ghostSize <= capacity);
        assert(target <= capacity);

        for (usize index = 0; index < BucketCount; ++index)
        {
            const list_type& list = buckets[index].entries;

            for (const Entry* entry = list.front(); entry != nullptr; entry = list.next(*entry))
            {
                assert(entry->bucket == &buckets[index]);

                const auto mapItr = directoryMap.find(entry->key);

                assert(mapItr != directoryMap.end());

                if (mapItr == directoryMap.end())
                    return false;

                assert(mapItr->second == entry);

                if (mapItr->second != entry)
                    return false;

                assert(entry->poolIndex < PoolCount);

                if (index == T1 || index == T2)
                    assert(nolock_is_resident(entry));
                else
                    assert(!nolock_is_resident(entry));
            }
        }

        return true;
    }
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    directory_map_type                    directoryMap;
    Array<Pool<ResidentEntry>, PoolCount> residentPools;
    Array<Pool<GhostEntry>, PoolCount>    ghostPools;
    Array<Bucket, BucketCount>            buckets;
    usize                                 target = 0;
};


template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatARCCache final {
    static constexpr usize InlineValueThreshold = 128;

    static constexpr usize PoolCount   = 2;
    static constexpr usize BucketCount = 4;

    static constexpr usize T1 = 0;
    static constexpr usize T2 = 1;
    static constexpr usize B1 = 2;
    static constexpr usize B2 = 3;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Bucket;

    struct Entry {
        Key     key;
        usize   poolIndex = 0;
        Bucket* bucket    = nullptr;
        Entry*  prev      = nullptr;
        Entry*  next      = nullptr;

        explicit Entry(const Key& k) noexcept :
            key(k) {}
    };

    using list_type = DoublyIntrusiveList<Entry>;

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

    using directory_map_type = ankerl::unordered_dense::map<Key, Entry*>;

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

        MapGuard(const MapGuard&)            = delete;
        MapGuard& operator=(const MapGuard&) = delete;

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

    PoolFlatARCCache(const PoolFlatARCCache&)            = delete;
    PoolFlatARCCache& operator=(const PoolFlatARCCache&) = delete;
    PoolFlatARCCache(PoolFlatARCCache&&)                 = delete;
    PoolFlatARCCache& operator=(PoolFlatARCCache&&)      = delete;

    ~PoolFlatARCCache() noexcept {
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

        if (target > capacity)
            target = capacity;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        auto mapItr = directoryMap.find(key);

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
            return directoryMap.end();

        return mapItr;
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        auto mapItr = directoryMap.find(key);

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
            return directoryMap.end();

        return mapItr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = directoryMap.find(key);

        return mapItr != directoryMap.end() && mapItr->second != nullptr
            && nolock_is_resident(mapItr->second);
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        assert(nolock_consistency_check());

        return nolock_resident_size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};

        assert(nolock_consistency_check());

        return nolock_resident_size() == 0;
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    // Clears entries but retains pooled storage for reuse.
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();

        assert(nolock_consistency_check());
    }

    // Releases pooled storage and restores the original configuration.
    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        nolock_release_pool();

        directoryMap.rehash(0);

        capacity = defaultCapacity;
        target   = 0;

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    // ---------- Bucket and entry access ----------

    [[nodiscard]] list_type& nolock_list(const usize index) noexcept {
        return buckets[index].entries;
    }

    [[nodiscard]] const list_type& nolock_list(const usize index) const noexcept {
        return buckets[index].entries;
    }

    [[nodiscard]] bool nolock_is_resident(const Entry* entry) const noexcept {
        return entry->bucket == &buckets[T1] || entry->bucket == &buckets[T2];
    }

    [[nodiscard]] usize nolock_bucket_index(const Entry* entry) const noexcept {
        if (entry->bucket == &buckets[T1])
            return T1;

        if (entry->bucket == &buckets[T2])
            return T2;

        if (entry->bucket == &buckets[B1])
            return B1;

        if (entry->bucket == &buckets[B2])
            return B2;

        assert(false);
        return T1;
    }

    [[nodiscard]] ResidentEntry* nolock_resident(Entry* entry) noexcept {
        assert(nolock_is_resident(entry));

        return static_cast<ResidentEntry*>(entry);
    }

    [[nodiscard]] const ResidentEntry* nolock_resident(const Entry* entry) const noexcept {
        assert(nolock_is_resident(entry));

        return static_cast<const ResidentEntry*>(entry);
    }

    [[nodiscard]] GhostEntry* nolock_ghost(Entry* entry) noexcept {
        assert(!nolock_is_resident(entry));

        return static_cast<GhostEntry*>(entry);
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return buckets[T1].entries.size() + buckets[T2].entries.size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return buckets[B1].entries.size() + buckets[B2].entries.size();
    }

    [[nodiscard]] usize nolock_directory_size() const noexcept { return directoryMap.size(); }

    Return nolock_read(const ResidentEntry* entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return entry->value;
        else
            return *entry->value;
    }

    // ---------- List operations ----------

    void nolock_list_push_front(Entry& entry, const usize index) noexcept {
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.bucket == nullptr);
        assert(index < BucketCount);

        [[maybe_unused]] const bool inserted = nolock_list(index).push_front(entry);
        assert(inserted);

        if (inserted)
            entry.bucket = &buckets[index];
    }

    void nolock_list_remove(Entry& entry) noexcept {
        assert(entry.bucket != nullptr);

        if (entry.bucket == nullptr)
            return;

        [[maybe_unused]] const bool erased = entry.bucket->entries.erase(entry);
        assert(erased);

        if (erased)
            entry.bucket = nullptr;
    }

    void nolock_list_move_front(Entry& entry) noexcept {
        assert(entry.bucket != nullptr);

        if (entry.bucket == nullptr)
            return;

        [[maybe_unused]] const bool moved = entry.bucket->entries.move_front(entry);
        assert(moved);
    }

    void nolock_promote_to_t2(ResidentEntry& entry) noexcept {
        if (entry.bucket == &buckets[T2])
        {
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
        assert(index == T1 || index == T2);

        const auto [mapItr, inserted] = directoryMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
        {
            if (mapItr->second != nullptr && nolock_is_resident(mapItr->second))
                return nolock_resident(mapItr->second);

            return nullptr;
        }

        MapGuard<directory_map_type> mapGuard{directoryMap, mapItr};

        const usize poolIndex = index - T1;
        auto&       pool      = residentPools[poolIndex];

        ResidentEntry* const entry = pool.create(0, key, std::forward<Args>(args)...);

        if (entry == nullptr)
            return nullptr;

        entry->poolIndex = poolIndex;
        mapItr->second   = entry;

        nolock_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    GhostEntry* nolock_add_ghost(const Key& key, const usize index) noexcept {
        assert(index == B1 || index == B2);

        const auto [mapItr, inserted] = directoryMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return nullptr;

        MapGuard<directory_map_type> mapGuard{directoryMap, mapItr};

        const usize poolIndex = index - B1;
        auto&       pool      = ghostPools[poolIndex];

        GhostEntry* const entry = pool.create(0, key);

        if (entry == nullptr)
            return nullptr;

        entry->poolIndex = poolIndex;
        mapItr->second   = entry;

        nolock_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    // Convert an existing ghost into a resident without erasing or
    // reinserting its directoryMap node. The caller holds the cache lock.
    template<typename... Args>
    ResidentEntry* nolock_ghost_to_resident(typename directory_map_type::iterator mapItr,
                                            Args&&... args) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return nullptr;

        // Replacement must have freed a resident slot.
        if (capacity == 0 || nolock_resident_size() >= capacity)
            return nullptr;

        GhostEntry* const ghost = nolock_ghost(mapItr->second);

        const usize     ghostPoolIndex    = ghost->poolIndex;
        constexpr usize residentPoolIndex = T2 - T1;

        assert(ghostPoolIndex < PoolCount);

        if (ghostPoolIndex >= PoolCount)
            return nullptr;

        // Allocate before changing the ghost or its directory entry.
        ResidentEntry* const entry =
          residentPools[residentPoolIndex].create(0, ghost->key, std::forward<Args>(args)...);

        if (entry == nullptr)
            return nullptr;

        entry->poolIndex = residentPoolIndex;

        // Preserve the existing directory node.
        nolock_list_remove(*ghost);
        mapItr->second = entry;
        nolock_list_push_front(*entry, T2);

        ghostPools[ghostPoolIndex].destroy(ghost);

        return entry;
    }

    // ---------- Entry destruction ----------

    void nolock_remove_resident(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
            return;

        ResidentEntry* const entry     = nolock_resident(mapItr->second);
        const usize          poolIndex = entry->poolIndex;

        assert(poolIndex < PoolCount);

        if (poolIndex >= PoolCount)
            return;

        nolock_list_remove(*entry);
        directoryMap.erase(mapItr);

        residentPools[poolIndex].destroy(entry);
    }

    void nolock_remove_resident(ResidentEntry& entry) noexcept {
        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return;

        nolock_remove_resident(mapItr);
    }

    void nolock_remove_ghost(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return;

        GhostEntry* const entry     = nolock_ghost(mapItr->second);
        const usize       poolIndex = entry->poolIndex;

        assert(poolIndex < PoolCount);

        if (poolIndex >= PoolCount)
            return;

        nolock_list_remove(*entry);
        directoryMap.erase(mapItr);

        ghostPools[poolIndex].destroy(entry);
    }

    void nolock_remove_ghost(GhostEntry& entry) noexcept {
        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return;

        nolock_remove_ghost(mapItr);
    }

    // ---------- Resident-to-ghost transition ----------

    void nolock_evict_to_ghost(ResidentEntry& entry, const usize index) noexcept {
        assert(index == B1 || index == B2);

        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());
        assert(mapItr != directoryMap.end() && mapItr->second == &entry);

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return;

        const usize ghostPoolIndex = index - B1;
        auto&       ghostPool      = ghostPools[ghostPoolIndex];

        GhostEntry* const ghost = ghostPool.create(0, entry.key);

        assert(ghost != nullptr);

        if (ghost == nullptr)
            return;

        ghost->poolIndex = ghostPoolIndex;

        // Replace the mapped pointer before destroying the resident.
        mapItr->second = ghost;

        const usize residentPoolIndex = entry.poolIndex;

        assert(residentPoolIndex < PoolCount);

        if (residentPoolIndex >= PoolCount)
        {
            mapItr->second = &entry;
            ghostPool.destroy(ghost);
            return;
        }

        nolock_list_remove(entry);
        nolock_list_push_front(*ghost, index);

        residentPools[residentPoolIndex].destroy(&entry);
    }

    // ---------- ARC replacement ----------

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
                nolock_evict_to_ghost(*nolock_resident(entry), B1);

            return;
        }

        if (!t2.empty())
        {
            Entry* const entry = t2.back();

            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*nolock_resident(entry), B2);

            return;
        }

        if (!t1.empty())
        {
            Entry* const entry = t1.back();

            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*nolock_resident(entry), B1);
        }
    }

    // ---------- Ghost history management ----------

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        while (nolock_ghost_size() > maxSize)
        {
            Entry* entry = buckets[B1].entries.back();

            if (entry == nullptr)
                entry = buckets[B2].entries.back();

            assert(entry != nullptr);

            if (entry == nullptr)
                break;

            nolock_remove_ghost(*nolock_ghost(entry));
        }
    }

    void nolock_handle_ghost_hit(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return;

        GhostEntry* const ghost = nolock_ghost(mapItr->second);
        const usize       index = nolock_bucket_index(ghost);

        assert(index == B1 || index == B2);

        if (index != B1 && index != B2)
            return;

        if (target > capacity)
            target = capacity;

        const usize b1Size = buckets[B1].entries.size();
        const usize b2Size = buckets[B2].entries.size();

        const bool keyInB2 = index == B2;

        if (keyInB2)
        {
            if (b2Size == 0)
                return;

            const usize delta = std::max(usize{1}, b1Size / b2Size);

            target = delta < target ? target - delta : usize{0};
        }
        else
        {
            if (b1Size == 0)
                return;

            const usize delta = std::max(usize{1}, b2Size / b1Size);

            target += std::min(delta, capacity - target);
        }

        // Keep the ghost and its directory node alive until conversion.
        // Replacement only changes other entries' mapped pointers.
        if (nolock_resident_size() >= capacity)
            nolock_replace(keyInB2);
    }

    // ---------- Miss preparation ----------

    void nolock_prepare_miss() noexcept {
        if (capacity == 0 || nolock_resident_size() < capacity)
            return;

        const usize t1Size = buckets[T1].entries.size();
        const usize b1Size = buckets[B1].entries.size();

        if (t1Size > capacity || b1Size > capacity || nolock_ghost_size() > capacity)
            return;

        if (t1Size == capacity - b1Size)
        {
            if (t1Size < capacity)
            {
                Entry* const ghost = buckets[B1].entries.back();

                assert(ghost != nullptr);

                if (ghost == nullptr)
                    return;

                nolock_remove_ghost(*nolock_ghost(ghost));
                nolock_replace(false);
            }
            else
            {
                Entry* const entry = buckets[T1].entries.back();

                assert(entry != nullptr);

                if (entry == nullptr)
                    return;

                nolock_remove_resident(*nolock_resident(entry));
            }

            return;
        }

        if (capacity <= std::numeric_limits<usize>::max() / 2
            && nolock_directory_size() >= capacity * 2)
        {
            Entry* ghost = buckets[B1].entries.back();

            if (ghost == nullptr)
                ghost = buckets[B2].entries.back();

            if (ghost != nullptr)
                nolock_remove_ghost(*nolock_ghost(ghost));
        }

        nolock_replace(false);
    }

    // ---------- Public-operation internals ----------

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        if (const auto mapItr = directoryMap.find(key); mapItr != directoryMap.end())
        {
            if (mapItr->second == nullptr)
                return Return{std::forward<Args>(args)...};

            if (nolock_is_resident(mapItr->second))
            {
                ResidentEntry* const entry = nolock_resident(mapItr->second);

                nolock_promote_to_t2(*entry);

                assert(nolock_consistency_check());

                return nolock_read(entry);
            }

            nolock_handle_ghost_hit(mapItr);

            ResidentEntry* const entry =
              nolock_ghost_to_resident(mapItr, std::forward<Args>(args)...);

            assert(entry != nullptr);

            if (entry == nullptr)
                return Return{std::forward<Args>(args)...};

            nolock_trim_ghosts(capacity);

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{std::forward<Args>(args)...};

        ResidentEntry* const entry = nolock_add_resident(key, T1, std::forward<Args>(args)...);

        assert(entry != nullptr);

        if (entry == nullptr)
            return Return{std::forward<Args>(args)...};

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        if (const auto mapItr = directoryMap.find(key); mapItr != directoryMap.end())
        {
            if (mapItr->second == nullptr)
                return Return{};

            if (nolock_is_resident(mapItr->second))
            {
                ResidentEntry* const entry = nolock_resident(mapItr->second);

                nolock_promote_to_t2(*entry);

                assert(nolock_consistency_check());

                return nolock_read(entry);
            }

            nolock_handle_ghost_hit(mapItr);

            ResidentEntry* const entry = nolock_ghost_to_resident(
              mapItr, std::forward<Builder>(builder)(std::forward<Args>(args)...));

            assert(entry != nullptr);

            if (entry == nullptr)
                return Return{};

            nolock_trim_ghosts(capacity);

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{};

        ResidentEntry* const entry =
          nolock_add_resident(key, T1, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        assert(entry != nullptr);

        if (entry == nullptr)
            return Return{};

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());

        return nolock_read(entry);
    }

    [[nodiscard]] bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = directoryMap.find(key);

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
        {
            assert(nolock_consistency_check());
            return false;
        }

        nolock_remove_resident(mapItr);

        assert(nolock_consistency_check());

        return true;
    }

    // Administrative eviction removes resident entries without adding ghosts.
    void nolock_evict_excess(const usize size) noexcept {
        while (nolock_resident_size() > size)
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

            nolock_remove_resident(*nolock_resident(entry));
        }

        if (target > capacity)
            target = capacity;

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());
    }

    void nolock_clear() noexcept {
        for (usize index = T1; index <= T2; ++index)
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                [[maybe_unused]] const bool erased = list.erase(*entry);
                assert(erased);

                const usize poolIndex = entry->poolIndex;

                assert(poolIndex < PoolCount);

                entry->bucket = nullptr;

                if (poolIndex < PoolCount)
                    residentPools[poolIndex].destroy(static_cast<ResidentEntry*>(entry));
            }
        }

        for (usize index = B1; index <= B2; ++index)
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                [[maybe_unused]] const bool erased = list.erase(*entry);
                assert(erased);

                const usize poolIndex = entry->poolIndex;

                assert(poolIndex < PoolCount);

                entry->bucket = nullptr;

                if (poolIndex < PoolCount)
                    ghostPools[poolIndex].destroy(static_cast<GhostEntry*>(entry));
            }
        }

        directoryMap.clear();
        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_release_pool() noexcept {
        for (auto& pool : residentPools)
            pool.release();

        for (auto& pool : ghostPools)
            pool.release();
    }

    void nolock_configure() noexcept {
        directoryMap.max_load_factor(maxLoadFactor);
        directoryMap.reserve(reserveCount);
    }

#ifndef NDEBUG
    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        /*
        const usize residentSize = nolock_resident_size();
        const usize ghostSize    = nolock_ghost_size();

        assert(residentSize + ghostSize == directoryMap.size());
        assert(residentSize <= capacity);
        assert(ghostSize <= capacity);
        assert(target <= capacity);

        for (usize index = 0; index < BucketCount; ++index)
        {
            const list_type& list = buckets[index].entries;

            for (const Entry* entry = list.front(); entry != nullptr; entry = list.next(*entry))
            {
                assert(entry->bucket == &buckets[index]);

                const auto mapItr = directoryMap.find(entry->key);

                assert(mapItr != directoryMap.end());

                if (mapItr == directoryMap.end())
                    return false;

                assert(mapItr->second == entry);

                if (mapItr->second != entry)
                    return false;

                assert(entry->poolIndex < PoolCount);

                if (index == T1 || index == T2)
                    assert(nolock_is_resident(entry));
                else
                    assert(!nolock_is_resident(entry));
            }
        }
        */
        return true;
    }
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    directory_map_type                    directoryMap;
    Array<Pool<ResidentEntry>, PoolCount> residentPools;
    Array<Pool<GhostEntry>, PoolCount>    ghostPools;
    Array<Bucket, BucketCount>            buckets;
    usize                                 target = 0;
};

}  // namespace DON

#endif  // UTILITY_ARC_CACHE_H_INCLUDED
