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

    static constexpr usize PoolCount   = 1;
    static constexpr usize BucketCount = 4;

    static constexpr usize T1 = 0;
    static constexpr usize T2 = 1;
    static constexpr usize B1 = 2;
    static constexpr usize B2 = 3;

    static constexpr u8 InvalidBucket = static_cast<u8>(BucketCount);

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry {
        Key    key;
        u8     bucketIndex = InvalidBucket;
        Entry* prev        = nullptr;
        Entry* next        = nullptr;

        std::optional<storage_value_type> value;

        explicit Entry(const Key& k) noexcept :
            key(k) {}

        template<typename... Args>
        void emplace_value(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                value.emplace(std::forward<Args>(args)...);
            else
                value.emplace(std::make_shared<Value>(std::forward<Args>(args)...));
        }
    };

    using list_type = DoublyIntrusiveList<Entry>;

    struct Bucket {
        list_type entries;
    };

    using directory_map_type = std::unordered_map<Key, Entry*>;

    template<typename Map>
    class MapGuard final {
       public:
        MapGuard(Map& mapRef, typename Map::iterator mapItr, const bool shouldErase) noexcept :
            map(mapRef),
            iterator(mapItr),
            active(shouldErase) {}

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
    // -------------------------------------------------------------------------
    // Bucket and entry access
    // -------------------------------------------------------------------------

    [[nodiscard]] bool nolock_is_resident(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->bucketIndex < BucketCount);

        return entry->bucketIndex < B1;
    }

    [[nodiscard]] usize nolock_bucket_index(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->bucketIndex < BucketCount);

        return entry->bucketIndex;
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return buckets[T1].entries.size() + buckets[T2].entries.size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return buckets[B1].entries.size() + buckets[B2].entries.size();
    }

    Return nolock_read(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(nolock_is_resident(entry));
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    // -------------------------------------------------------------------------
    // List operations
    // -------------------------------------------------------------------------

    void nolock_list_push_front(Entry& entry, const usize index) noexcept {
        assert(index < BucketCount);
        assert(entry.bucketIndex == InvalidBucket);
        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        if (index >= BucketCount || entry.bucketIndex != InvalidBucket || entry.prev != nullptr
            || entry.next != nullptr)
            return;

        [[maybe_unused]] const bool inserted = buckets[index].entries.push_front(entry);

        assert(inserted);

        if (inserted)
            entry.bucketIndex = static_cast<u8>(index);
    }

    void nolock_list_remove(Entry& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex >= BucketCount)
            return;

        const usize index = entry.bucketIndex;

        [[maybe_unused]] const bool erased = buckets[index].entries.erase(entry);

        assert(erased);

        if (erased)
            entry.bucketIndex = InvalidBucket;
    }

    void nolock_list_move_front(Entry& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex >= BucketCount)
            return;

        const usize index = entry.bucketIndex;

        [[maybe_unused]] const bool moved = buckets[index].entries.move_front(entry);

        assert(moved);
    }

    void nolock_move_entry(Entry& entry, const usize newIndex) noexcept {
        assert(entry.bucketIndex < BucketCount);
        assert(newIndex < BucketCount);
        assert(entry.bucketIndex != newIndex);

        if (entry.bucketIndex >= BucketCount || newIndex >= BucketCount
            || entry.bucketIndex == newIndex)
            return;

        nolock_list_remove(entry);

        assert(entry.bucketIndex == InvalidBucket);
        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        nolock_list_push_front(entry, newIndex);

        assert(entry.bucketIndex == newIndex);
    }

    void nolock_promote_to_t2(Entry& entry) noexcept {
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if (entry.bucketIndex == T2)
        {
            // A T2 MRU hit requires no list operation.
            if (buckets[T2].entries.front() != &entry)
                nolock_list_move_front(entry);

            return;
        }

        // T1 -> T2 is an O(1) intrusive-list transition.
        nolock_move_entry(entry, T2);
    }

    // -------------------------------------------------------------------------
    // Entry creation
    // -------------------------------------------------------------------------

    // mapItr refers to a newly inserted null directory slot.
    // The caller owns rollback of that slot through MapGuard.
    template<typename... Args>
    Entry* nolock_add_resident(typename directory_map_type::iterator mapItr,
                               const usize                           index,
                               Args&&... args) noexcept {
        assert(mapItr != directoryMap.end());
        assert(mapItr->second == nullptr);
        assert(index == T1 || index == T2);

        if (mapItr == directoryMap.end() || mapItr->second != nullptr
            || (index != T1 && index != T2))
            return nullptr;

        Entry* const entry = entryPools[0].create(0, mapItr->first);

        if (entry == nullptr)
            return nullptr;

        assert(entry->bucketIndex == InvalidBucket);
        assert(entry->prev == nullptr);
        assert(entry->next == nullptr);
        assert(!entry->value.has_value());

        entry->emplace_value(std::forward<Args>(args)...);

        mapItr->second = entry;

        nolock_list_push_front(*entry, index);

        assert(entry->bucketIndex == index);
        assert(entry->value.has_value());

        return entry;
    }

    // -------------------------------------------------------------------------
    // Ghost -> resident transition
    // -------------------------------------------------------------------------

    template<typename... Args>
    Entry* nolock_ghost_to_resident(typename directory_map_type::iterator mapItr,
                                    Args&&... args) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return nullptr;

        if (capacity == 0 || nolock_resident_size() >= capacity)
            return nullptr;

        Entry* const entry = mapItr->second;

        assert(entry->bucketIndex == B1 || entry->bucketIndex == B2);
        assert(!entry->value.has_value());

        if ((entry->bucketIndex != B1 && entry->bucketIndex != B2) || entry->value.has_value())
            return nullptr;

        // Reuse the ghost entry and preserve its directory-map pointer.
        entry->emplace_value(std::forward<Args>(args)...);

        nolock_move_entry(*entry, T2);

        assert(entry->bucketIndex == T2);
        assert(entry->value.has_value());

        return entry;
    }

    // -------------------------------------------------------------------------
    // Entry destruction
    // -------------------------------------------------------------------------

    void nolock_remove_resident(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
            return;

        Entry* const entry = mapItr->second;

        assert(entry->bucketIndex == T1 || entry->bucketIndex == T2);
        assert(entry->value.has_value());

        if ((entry->bucketIndex != T1 && entry->bucketIndex != T2) || !entry->value.has_value())
            return;

        nolock_list_remove(*entry);

        assert(entry->bucketIndex == InvalidBucket);
        assert(entry->prev == nullptr);
        assert(entry->next == nullptr);

        directoryMap.erase(mapItr);

        entryPools[0].destroy(entry);
    }

    void nolock_remove_resident(Entry& entry) noexcept {
        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return;

        nolock_remove_resident(mapItr);
    }

    [[nodiscard]] bool nolock_remove_ghost(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return false;

        Entry* const entry = mapItr->second;

        assert(entry->bucketIndex == B1 || entry->bucketIndex == B2);
        assert(!entry->value.has_value());

        if ((entry->bucketIndex != B1 && entry->bucketIndex != B2) || entry->value.has_value())
            return false;

        nolock_list_remove(*entry);

        assert(entry->bucketIndex == InvalidBucket);
        assert(entry->prev == nullptr);
        assert(entry->next == nullptr);

        directoryMap.erase(mapItr);
        entryPools[0].destroy(entry);

        return true;
    }

    bool nolock_remove_ghost(Entry& entry) noexcept {
        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return false;

        return nolock_remove_ghost(mapItr);
    }

    // -------------------------------------------------------------------------
    // Resident -> ghost transition
    // -------------------------------------------------------------------------

    void nolock_evict_to_ghost(Entry& entry, const usize index) noexcept {
        assert(index == B1 || index == B2);
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if ((index != B1 && index != B2) || (entry.bucketIndex != T1 && entry.bucketIndex != T2)
            || !entry.value.has_value())
            return;

        // Keep the entry and directory-map pointer; discard only its value.
        entry.value.reset();

        nolock_move_entry(entry, index);

        assert(entry.bucketIndex == index);
        assert(!entry.value.has_value());
    }

    // -------------------------------------------------------------------------
    // ARC replacement
    // -------------------------------------------------------------------------

    void nolock_replace(const bool keyInB2, const usize t1Size, const usize t2Size) noexcept {
        list_type& t1 = buckets[T1].entries;
        list_type& t2 = buckets[T2].entries;

        const bool evictT1 = !t1.empty() && (t1Size > target || (keyInB2 && t1Size == target));

        if (evictT1)
        {
            Entry* const entry = t1.back();

            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*entry, B1);

            return;
        }

        if (Entry* const entry = t2.back())
        {
            nolock_evict_to_ghost(*entry, B2);
            return;
        }

        if (Entry* const entry = t1.back())
            nolock_evict_to_ghost(*entry, B1);
    }

    // -------------------------------------------------------------------------
    // Ghost history management
    // -------------------------------------------------------------------------

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        // Cache the total instead of querying both lists on each iteration.
        usize ghostSize = nolock_ghost_size();

        while (ghostSize > maxSize)
        {
            // Preserve the existing policy: discard B2's LRU first.
            Entry* entry = buckets[B2].entries.back();

            if (entry == nullptr)
                entry = buckets[B1].entries.back();

            assert(entry != nullptr);

            if (entry == nullptr)
                break;

            // The entry is destroyed by this call. Do not access it afterward.
            if (!nolock_remove_ghost(*entry))
                break;

            --ghostSize;
        }
    }

    void nolock_handle_ghost_hit(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return;

        Entry* const ghost = mapItr->second;
        const usize  index = nolock_bucket_index(ghost);

        assert(index == B1 || index == B2);
        assert(!ghost->value.has_value());

        if ((index != B1 && index != B2) || ghost->value.has_value())
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

        // Obtain resident sizes once and reuse them in replacement.
        const usize t1Size = buckets[T1].entries.size();
        const usize t2Size = buckets[T2].entries.size();

        if (t1Size + t2Size >= capacity)
            nolock_replace(keyInB2, t1Size, t2Size);
    }

    // -------------------------------------------------------------------------
    // Miss preparation
    // -------------------------------------------------------------------------

    void nolock_prepare_miss() noexcept {
        const usize t1Size = buckets[T1].entries.size();
        const usize t2Size = buckets[T2].entries.size();

        if (capacity == 0 || t1Size + t2Size < capacity)
            return;

        const usize b1Size    = buckets[B1].entries.size();
        const usize ghostSize = nolock_ghost_size();

        if (t1Size > capacity || b1Size > capacity || ghostSize > capacity)
            return;

        if (t1Size == capacity - b1Size)
        {
            if (t1Size < capacity)
            {
                // Standard ARC B1-history special case.
                Entry* const ghost = buckets[B1].entries.back();

                assert(ghost != nullptr);

                if (ghost == nullptr)
                    return;

                if (!nolock_remove_ghost(*ghost))
                    return;

                nolock_replace(false, t1Size, t2Size);
            }
            else
            {
                Entry* const entry = buckets[T1].entries.back();

                assert(entry != nullptr);

                if (entry == nullptr)
                    return;

                nolock_remove_resident(*entry);
            }

            return;
        }

        const usize directorySize = directoryMap.size();

        if (capacity <= std::numeric_limits<usize>::max() / 2 && directorySize >= capacity * 2)
        {
            // Prefer the LRU entry from B2 when trimming overall ARC history.
            Entry* ghost = buckets[B2].entries.back();

            if (ghost == nullptr)
                ghost = buckets[B1].entries.back();

            if (ghost != nullptr)
                nolock_remove_ghost(*ghost);
        }

        nolock_replace(false, t1Size, t2Size);
    }

    // -------------------------------------------------------------------------
    // Public-operation internals
    // -------------------------------------------------------------------------

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        auto mapItr = directoryMap.find(key);

        if (mapItr != directoryMap.end())
        {
            Entry* const current = mapItr->second;

            if (current == nullptr)
                return Return{std::forward<Args>(args)...};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);

                assert(nolock_consistency_check());

                return nolock_read(current);
            }

            nolock_handle_ghost_hit(mapItr);

            Entry* const entry = nolock_ghost_to_resident(mapItr, std::forward<Args>(args)...);

            assert(entry != nullptr);

            if (entry == nullptr)
                return Return{std::forward<Args>(args)...};

            nolock_trim_ghosts(capacity);

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        // Complete potentially erasing operations before retaining a new iterator.
        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{std::forward<Args>(args)...};

        auto [newItr, inserted] = directoryMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return Return{std::forward<Args>(args)...};

        MapGuard mapGuard{directoryMap, newItr, true};

        Entry* const entry = nolock_add_resident(newItr, T1, std::forward<Args>(args)...);

        assert(entry != nullptr);

        if (entry == nullptr)
            return Return{std::forward<Args>(args)...};

        mapGuard.commit();

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        auto mapItr = directoryMap.find(key);

        if (mapItr != directoryMap.end())
        {
            Entry* const current = mapItr->second;

            if (current == nullptr)
                return Return{};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);

                assert(nolock_consistency_check());

                return nolock_read(current);
            }

            nolock_handle_ghost_hit(mapItr);

            Entry* const entry = nolock_ghost_to_resident(
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

        auto [newItr, inserted] = directoryMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return Return{};

        MapGuard mapGuard{directoryMap, newItr, true};

        Entry* const entry = nolock_add_resident(
          newItr, T1, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        assert(entry != nullptr);

        if (entry == nullptr)
            return Return{};

        mapGuard.commit();

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

    // -------------------------------------------------------------------------
    // Administrative eviction
    // -------------------------------------------------------------------------

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

            nolock_remove_resident(*entry);
        }

        if (target > capacity)
            target = capacity;

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());
    }

    // -------------------------------------------------------------------------
    // Clear / pool management
    // -------------------------------------------------------------------------

    void nolock_clear() noexcept {
        for (usize index = 0; index < BucketCount; ++index)
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                assert(entry->bucketIndex == index);

                [[maybe_unused]] const bool erased = list.erase(*entry);

                assert(erased);

                if (!erased)
                    break;

                entry->bucketIndex = InvalidBucket;

                assert(entry->prev == nullptr);
                assert(entry->next == nullptr);

                entryPools[0].destroy(entry);
            }
        }

        directoryMap.clear();
        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_release_pool() noexcept {
        for (auto& pool : entryPools)
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

            usize count = 0;

            for (const Entry* entry = list.front(); entry != nullptr; entry = list.next(*entry))
            {
                ++count;

                assert(entry->bucketIndex == index);

                const auto mapItr = directoryMap.find(entry->key);

                assert(mapItr != directoryMap.end());

                if (mapItr == directoryMap.end())
                    return false;

                assert(mapItr->second == entry);

                if (mapItr->second != entry)
                    return false;

                if (index == T1 || index == T2)
                {
                    assert(nolock_is_resident(entry));
                    assert(entry->value.has_value());
                }
                else
                {
                    assert(!nolock_is_resident(entry));
                    assert(!entry->value.has_value());
                }
            }

            assert(count == list.size());

            if (count != list.size())
                return false;
        }

        for (const auto& [key, entry] : directoryMap)
        {
            assert(entry != nullptr);

            if (entry == nullptr)
                return false;

            assert(entry->key == key);
            assert(entry->bucketIndex < BucketCount);

            if (entry->bucketIndex >= BucketCount)
                return false;

            if (entry->bucketIndex == T1 || entry->bucketIndex == T2)
                assert(entry->value.has_value());
            else
                assert(!entry->value.has_value());

            const list_type& list = buckets[entry->bucketIndex].entries;

            bool found = false;

            for (const Entry* current = list.front(); current != nullptr;
                 current              = list.next(*current))
            {
                if (current == entry)
                {
                    found = true;
                    break;
                }
            }

            assert(found);

            if (!found)
                return false;
        }

        return true;
    }
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    directory_map_type            directoryMap;
    Array<Pool<Entry>, PoolCount> entryPools;
    Array<Bucket, BucketCount>    buckets;
    usize                         target = 0;
};


template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatARCCache final {
    static constexpr usize InlineValueThreshold = 128;

    static constexpr usize PoolCount   = 1;
    static constexpr usize BucketCount = 4;

    static constexpr usize T1 = 0;
    static constexpr usize T2 = 1;
    static constexpr usize B1 = 2;
    static constexpr usize B2 = 3;

    static constexpr u8 InvalidBucket = static_cast<u8>(BucketCount);

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry {
        Key    key;
        u8     bucketIndex = InvalidBucket;
        Entry* prev        = nullptr;
        Entry* next        = nullptr;

        std::optional<storage_value_type> value;

        explicit Entry(const Key& k) noexcept :
            key(k) {}

        template<typename... Args>
        void emplace_value(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                value.emplace(std::forward<Args>(args)...);
            else
                value.emplace(std::make_shared<Value>(std::forward<Args>(args)...));
        }
    };

    using list_type = DoublyIntrusiveList<Entry>;

    struct Bucket {
        list_type entries;
    };

    using directory_map_type = ankerl::unordered_dense::map<Key, Entry*>;

    template<typename Map>
    class MapGuard final {
       public:
        MapGuard(Map& mapRef, typename Map::iterator mapItr, const bool shouldErase) noexcept :
            map(mapRef),
            iterator(mapItr),
            active(shouldErase) {}

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
    // -------------------------------------------------------------------------
    // Bucket and entry access
    // -------------------------------------------------------------------------

    [[nodiscard]] bool nolock_is_resident(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->bucketIndex < BucketCount);

        return entry->bucketIndex < B1;
    }

    [[nodiscard]] usize nolock_bucket_index(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->bucketIndex < BucketCount);

        return entry->bucketIndex;
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return buckets[T1].entries.size() + buckets[T2].entries.size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return buckets[B1].entries.size() + buckets[B2].entries.size();
    }

    Return nolock_read(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(nolock_is_resident(entry));
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    // -------------------------------------------------------------------------
    // List operations
    // -------------------------------------------------------------------------

    void nolock_list_push_front(Entry& entry, const usize index) noexcept {
        assert(index < BucketCount);
        assert(entry.bucketIndex == InvalidBucket);
        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        if (index >= BucketCount || entry.bucketIndex != InvalidBucket || entry.prev != nullptr
            || entry.next != nullptr)
            return;

        [[maybe_unused]] const bool inserted = buckets[index].entries.push_front(entry);

        assert(inserted);

        if (inserted)
            entry.bucketIndex = static_cast<u8>(index);
    }

    void nolock_list_remove(Entry& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex >= BucketCount)
            return;

        const usize index = entry.bucketIndex;

        [[maybe_unused]] const bool erased = buckets[index].entries.erase(entry);

        assert(erased);

        if (erased)
            entry.bucketIndex = InvalidBucket;
    }

    void nolock_list_move_front(Entry& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex >= BucketCount)
            return;

        const usize index = entry.bucketIndex;

        [[maybe_unused]] const bool moved = buckets[index].entries.move_front(entry);

        assert(moved);
    }

    void nolock_move_entry(Entry& entry, const usize newIndex) noexcept {
        assert(entry.bucketIndex < BucketCount);
        assert(newIndex < BucketCount);
        assert(entry.bucketIndex != newIndex);

        if (entry.bucketIndex >= BucketCount || newIndex >= BucketCount
            || entry.bucketIndex == newIndex)
            return;

        nolock_list_remove(entry);

        assert(entry.bucketIndex == InvalidBucket);
        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        nolock_list_push_front(entry, newIndex);

        assert(entry.bucketIndex == newIndex);
    }

    void nolock_promote_to_t2(Entry& entry) noexcept {
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if (entry.bucketIndex == T2)
        {
            // A T2 MRU hit requires no list operation.
            if (buckets[T2].entries.front() != &entry)
                nolock_list_move_front(entry);

            return;
        }

        // T1 -> T2 is an O(1) intrusive-list transition.
        nolock_move_entry(entry, T2);
    }

    // -------------------------------------------------------------------------
    // Entry creation
    // -------------------------------------------------------------------------

    // mapItr refers to a newly inserted null directory slot.
    // The caller owns rollback of that slot through MapGuard.
    template<typename... Args>
    Entry* nolock_add_resident(typename directory_map_type::iterator mapItr,
                               const usize                           index,
                               Args&&... args) noexcept {
        assert(mapItr != directoryMap.end());
        assert(mapItr->second == nullptr);
        assert(index == T1 || index == T2);

        if (mapItr == directoryMap.end() || mapItr->second != nullptr
            || (index != T1 && index != T2))
            return nullptr;

        Entry* const entry = entryPools[0].create(0, mapItr->first);

        if (entry == nullptr)
            return nullptr;

        assert(entry->bucketIndex == InvalidBucket);
        assert(entry->prev == nullptr);
        assert(entry->next == nullptr);
        assert(!entry->value.has_value());

        entry->emplace_value(std::forward<Args>(args)...);

        mapItr->second = entry;

        nolock_list_push_front(*entry, index);

        assert(entry->bucketIndex == index);
        assert(entry->value.has_value());

        return entry;
    }

    // -------------------------------------------------------------------------
    // Ghost -> resident transition
    // -------------------------------------------------------------------------

    template<typename... Args>
    Entry* nolock_ghost_to_resident(typename directory_map_type::iterator mapItr,
                                    Args&&... args) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return nullptr;

        if (capacity == 0 || nolock_resident_size() >= capacity)
            return nullptr;

        Entry* const entry = mapItr->second;

        assert(entry->bucketIndex == B1 || entry->bucketIndex == B2);
        assert(!entry->value.has_value());

        if ((entry->bucketIndex != B1 && entry->bucketIndex != B2) || entry->value.has_value())
            return nullptr;

        // Reuse the ghost entry and preserve its directory-map pointer.
        entry->emplace_value(std::forward<Args>(args)...);

        nolock_move_entry(*entry, T2);

        assert(entry->bucketIndex == T2);
        assert(entry->value.has_value());

        return entry;
    }

    // -------------------------------------------------------------------------
    // Entry destruction
    // -------------------------------------------------------------------------

    void nolock_remove_resident(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || !nolock_is_resident(mapItr->second))
            return;

        Entry* const entry = mapItr->second;

        assert(entry->bucketIndex == T1 || entry->bucketIndex == T2);
        assert(entry->value.has_value());

        if ((entry->bucketIndex != T1 && entry->bucketIndex != T2) || !entry->value.has_value())
            return;

        nolock_list_remove(*entry);

        assert(entry->bucketIndex == InvalidBucket);
        assert(entry->prev == nullptr);
        assert(entry->next == nullptr);

        directoryMap.erase(mapItr);

        entryPools[0].destroy(entry);
    }

    void nolock_remove_resident(Entry& entry) noexcept {
        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return;

        nolock_remove_resident(mapItr);
    }

    [[nodiscard]] bool nolock_remove_ghost(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return false;

        Entry* const entry = mapItr->second;

        assert(entry->bucketIndex == B1 || entry->bucketIndex == B2);
        assert(!entry->value.has_value());

        if ((entry->bucketIndex != B1 && entry->bucketIndex != B2) || entry->value.has_value())
            return false;

        nolock_list_remove(*entry);

        assert(entry->bucketIndex == InvalidBucket);
        assert(entry->prev == nullptr);
        assert(entry->next == nullptr);

        directoryMap.erase(mapItr);
        entryPools[0].destroy(entry);

        return true;
    }

    bool nolock_remove_ghost(Entry& entry) noexcept {
        const auto mapItr = directoryMap.find(entry.key);

        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second != &entry)
            return false;

        return nolock_remove_ghost(mapItr);
    }

    // -------------------------------------------------------------------------
    // Resident -> ghost transition
    // -------------------------------------------------------------------------

    void nolock_evict_to_ghost(Entry& entry, const usize index) noexcept {
        assert(index == B1 || index == B2);
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if ((index != B1 && index != B2) || (entry.bucketIndex != T1 && entry.bucketIndex != T2)
            || !entry.value.has_value())
            return;

        // Keep the entry and directory-map pointer; discard only its value.
        entry.value.reset();

        nolock_move_entry(entry, index);

        assert(entry.bucketIndex == index);
        assert(!entry.value.has_value());
    }

    // -------------------------------------------------------------------------
    // ARC replacement
    // -------------------------------------------------------------------------

    void nolock_replace(const bool keyInB2, const usize t1Size, const usize t2Size) noexcept {
        list_type& t1 = buckets[T1].entries;
        list_type& t2 = buckets[T2].entries;

        const bool evictT1 = !t1.empty() && (t1Size > target || (keyInB2 && t1Size == target));

        if (evictT1)
        {
            Entry* const entry = t1.back();

            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*entry, B1);

            return;
        }

        if (Entry* const entry = t2.back())
        {
            nolock_evict_to_ghost(*entry, B2);
            return;
        }

        if (Entry* const entry = t1.back())
            nolock_evict_to_ghost(*entry, B1);
    }

    // -------------------------------------------------------------------------
    // Ghost history management
    // -------------------------------------------------------------------------

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        // Cache the total instead of querying both lists on each iteration.
        usize ghostSize = nolock_ghost_size();

        while (ghostSize > maxSize)
        {
            // Preserve the existing policy: discard B2's LRU first.
            Entry* entry = buckets[B2].entries.back();

            if (entry == nullptr)
                entry = buckets[B1].entries.back();

            assert(entry != nullptr);

            if (entry == nullptr)
                break;

            // The entry is destroyed by this call. Do not access it afterward.
            if (!nolock_remove_ghost(*entry))
                break;

            --ghostSize;
        }
    }

    void nolock_handle_ghost_hit(typename directory_map_type::iterator mapItr) noexcept {
        assert(mapItr != directoryMap.end());

        if (mapItr == directoryMap.end() || mapItr->second == nullptr
            || nolock_is_resident(mapItr->second))
            return;

        Entry* const ghost = mapItr->second;
        const usize  index = nolock_bucket_index(ghost);

        assert(index == B1 || index == B2);
        assert(!ghost->value.has_value());

        if ((index != B1 && index != B2) || ghost->value.has_value())
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

        // Obtain resident sizes once and reuse them in replacement.
        const usize t1Size = buckets[T1].entries.size();
        const usize t2Size = buckets[T2].entries.size();

        if (t1Size + t2Size >= capacity)
            nolock_replace(keyInB2, t1Size, t2Size);
    }

    // -------------------------------------------------------------------------
    // Miss preparation
    // -------------------------------------------------------------------------

    void nolock_prepare_miss() noexcept {
        const usize t1Size = buckets[T1].entries.size();
        const usize t2Size = buckets[T2].entries.size();

        if (capacity == 0 || t1Size + t2Size < capacity)
            return;

        const usize b1Size    = buckets[B1].entries.size();
        const usize ghostSize = nolock_ghost_size();

        if (t1Size > capacity || b1Size > capacity || ghostSize > capacity)
            return;

        if (t1Size == capacity - b1Size)
        {
            if (t1Size < capacity)
            {
                // Standard ARC B1-history special case.
                Entry* const ghost = buckets[B1].entries.back();

                assert(ghost != nullptr);

                if (ghost == nullptr)
                    return;

                if (!nolock_remove_ghost(*ghost))
                    return;

                nolock_replace(false, t1Size, t2Size);
            }
            else
            {
                Entry* const entry = buckets[T1].entries.back();

                assert(entry != nullptr);

                if (entry == nullptr)
                    return;

                nolock_remove_resident(*entry);
            }

            return;
        }

        const usize directorySize = directoryMap.size();

        if (capacity <= std::numeric_limits<usize>::max() / 2 && directorySize >= capacity * 2)
        {
            // Prefer the LRU entry from B2 when trimming overall ARC history.
            Entry* ghost = buckets[B2].entries.back();

            if (ghost == nullptr)
                ghost = buckets[B1].entries.back();

            if (ghost != nullptr)
                nolock_remove_ghost(*ghost);
        }

        nolock_replace(false, t1Size, t2Size);
    }

    // -------------------------------------------------------------------------
    // Public-operation internals
    // -------------------------------------------------------------------------

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        auto mapItr = directoryMap.find(key);

        if (mapItr != directoryMap.end())
        {
            Entry* const current = mapItr->second;

            if (current == nullptr)
                return Return{std::forward<Args>(args)...};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);

                assert(nolock_consistency_check());

                return nolock_read(current);
            }

            nolock_handle_ghost_hit(mapItr);

            Entry* const entry = nolock_ghost_to_resident(mapItr, std::forward<Args>(args)...);

            assert(entry != nullptr);

            if (entry == nullptr)
                return Return{std::forward<Args>(args)...};

            nolock_trim_ghosts(capacity);

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        // Complete potentially erasing operations before retaining a new iterator.
        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{std::forward<Args>(args)...};

        auto [newItr, inserted] = directoryMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return Return{std::forward<Args>(args)...};

        MapGuard mapGuard{directoryMap, newItr, true};

        Entry* const entry = nolock_add_resident(newItr, T1, std::forward<Args>(args)...);

        assert(entry != nullptr);

        if (entry == nullptr)
            return Return{std::forward<Args>(args)...};

        mapGuard.commit();

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        auto mapItr = directoryMap.find(key);

        if (mapItr != directoryMap.end())
        {
            Entry* const current = mapItr->second;

            if (current == nullptr)
                return Return{};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);

                assert(nolock_consistency_check());

                return nolock_read(current);
            }

            nolock_handle_ghost_hit(mapItr);

            Entry* const entry = nolock_ghost_to_resident(
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

        auto [newItr, inserted] = directoryMap.try_emplace(key, nullptr);

        assert(inserted);

        if (!inserted)
            return Return{};

        MapGuard mapGuard{directoryMap, newItr, true};

        Entry* const entry = nolock_add_resident(
          newItr, T1, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        assert(entry != nullptr);

        if (entry == nullptr)
            return Return{};

        mapGuard.commit();

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

    // -------------------------------------------------------------------------
    // Administrative eviction
    // -------------------------------------------------------------------------

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

            nolock_remove_resident(*entry);
        }

        if (target > capacity)
            target = capacity;

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());
    }

    // -------------------------------------------------------------------------
    // Clear / pool management
    // -------------------------------------------------------------------------

    void nolock_clear() noexcept {
        for (usize index = 0; index < BucketCount; ++index)
        {
            list_type& list = buckets[index].entries;

            while (Entry* const entry = list.back())
            {
                assert(entry->bucketIndex == index);

                [[maybe_unused]] const bool erased = list.erase(*entry);

                assert(erased);

                if (!erased)
                    break;

                entry->bucketIndex = InvalidBucket;

                assert(entry->prev == nullptr);
                assert(entry->next == nullptr);

                entryPools[0].destroy(entry);
            }
        }

        directoryMap.clear();
        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_release_pool() noexcept {
        for (auto& pool : entryPools)
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

            usize count = 0;

            for (const Entry* entry = list.front(); entry != nullptr; entry = list.next(*entry))
            {
                ++count;

                assert(entry->bucketIndex == index);

                const auto mapItr = directoryMap.find(entry->key);

                assert(mapItr != directoryMap.end());

                if (mapItr == directoryMap.end())
                    return false;

                assert(mapItr->second == entry);

                if (mapItr->second != entry)
                    return false;

                if (index == T1 || index == T2)
                {
                    assert(nolock_is_resident(entry));
                    assert(entry->value.has_value());
                }
                else
                {
                    assert(!nolock_is_resident(entry));
                    assert(!entry->value.has_value());
                }
            }

            assert(count == list.size());

            if (count != list.size())
                return false;
        }

        for (const auto& [key, entry] : directoryMap)
        {
            assert(entry != nullptr);

            if (entry == nullptr)
                return false;

            assert(entry->key == key);
            assert(entry->bucketIndex < BucketCount);

            if (entry->bucketIndex >= BucketCount)
                return false;

            if (entry->bucketIndex == T1 || entry->bucketIndex == T2)
                assert(entry->value.has_value());
            else
                assert(!entry->value.has_value());

            const list_type& list = buckets[entry->bucketIndex].entries;

            bool found = false;

            for (const Entry* current = list.front(); current != nullptr;
                 current              = list.next(*current))
            {
                if (current == entry)
                {
                    found = true;
                    break;
                }
            }

            assert(found);

            if (!found)
                return false;
        }

        return true;
    }
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    directory_map_type            directoryMap;
    Array<Pool<Entry>, PoolCount> entryPools;
    Array<Bucket, BucketCount>    buckets;
    usize                         target = 0;
};


}  // namespace DON

#endif  // UTILITY_ARC_CACHE_H_INCLUDED
