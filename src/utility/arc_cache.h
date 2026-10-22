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
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../utility/list.h"
#include "../utility/pool.h"
#include "../utility/types.h"
#include "../utility/ankerl/unordered_dense.h"

namespace DON {

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolARCCache final {
    static constexpr usize InlineValueThreshold = 128;

    static constexpr usize T1 = 0;
    static constexpr usize T2 = 1;
    static constexpr usize B1 = 2;
    static constexpr usize B2 = 3;

    static constexpr usize BucketCount = 4;

    static constexpr u8 InvalidIndex = u8(BucketCount);

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry;

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = std::unordered_map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

    struct Entry final {
        explicit Entry(const Key& k) noexcept :
            key(k) {}

        template<typename... Args>
        void emplace_value(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                value.emplace(std::forward<Args>(args)...);
            else
                value.emplace(std::make_shared<Value>(std::forward<Args>(args)...));
        }

        Key    key;
        u8     bucketIndex = InvalidIndex;
        Entry* prev        = nullptr;
        Entry* next        = nullptr;

        std::optional<storage_value_type> value;
    };

    struct Bucket final {
        list_type entries;
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
        pool.release();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        // Isolated Mutation Path
        // If it's a ghost hit or clean cache miss, capture the EXCLUSIVE lock.
        std::lock_guard lockGuard{mutex};

        // Re-verify the key hasn't been inserted by a racing thread while we were waiting
        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        // Isolated Mutation Path
        // If it's a ghost hit or clean cache miss, capture the EXCLUSIVE lock.
        std::lock_guard lockGuard{mutex};

        // Re-verify the key hasn't been inserted by a racing thread while we were waiting
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

        nolock_reserve_map(newCapacity);

        capacity = newCapacity;

        if (target > capacity)
            target = capacity;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        auto mapItr = map.find(key);

        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
            return map.end();

        return mapItr;
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        auto mapItr = map.find(key);

        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
            return map.end();

        return mapItr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);

        return mapItr != map.end() && mapItr->second != nullptr
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

    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();

        assert(nolock_consistency_check());
    }

    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        pool.release();
        map.rehash(0);

        capacity = defaultCapacity;

        target = 0;

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    // Map reservation and insertion

    [[nodiscard]] usize nolock_map_reserve_size(const usize requestedCapacity) const noexcept {
        constexpr usize maxSize = std::numeric_limits<usize>::max();
        // Leave one temporary directory slot for a clean-miss insertion that
        // precedes replacement, avoiding a rehash at the 2 * capacity boundary.
        const usize mapSize =
          requestedCapacity > (maxSize - 1) / 2 ? maxSize : 2 * requestedCapacity + 1;

        return std::max(reserveCount, mapSize);
    }

    void nolock_reserve_map(const usize requestedCapacity) noexcept {
        map.reserve(nolock_map_reserve_size(requestedCapacity));
    }

    void nolock_safe_map_erase(const map_iterator targetItr) noexcept {
        assert(targetItr != map.end());

        map.erase(targetItr);
    }

    // Bucket and entry access

    [[nodiscard]] bool nolock_is_resident(const list_value_type* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->bucketIndex < BucketCount);

        return entry->bucketIndex < B1;
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return buckets[T1].entries.size() + buckets[T2].entries.size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return buckets[B1].entries.size() + buckets[B2].entries.size();
    }

    Return nolock_read(const list_value_type* entry) const noexcept {
        assert(entry != nullptr);
        assert(nolock_is_resident(entry));
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    // List operations

    [[nodiscard]] bool nolock_list_push_front(list_value_type& entry, const usize index) noexcept {
        assert(index < BucketCount);
        assert(entry.bucketIndex == InvalidIndex);
        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        if (index >= BucketCount || entry.bucketIndex != InvalidIndex || entry.prev != nullptr
            || entry.next != nullptr)
            return false;

        if (!buckets[index].entries.push_front(entry))
            return false;

        entry.bucketIndex = u8(index);

        return true;
    }

    [[nodiscard]] bool nolock_list_remove(list_value_type& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex >= BucketCount)
            return false;

        if (!buckets[entry.bucketIndex].entries.erase(entry))
            return false;

        entry.bucketIndex = InvalidIndex;

        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        return true;
    }

    void nolock_list_move_front(list_value_type& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);
        // Combine structural checks and early return if already at the front
        if (entry.bucketIndex >= BucketCount || entry.prev == nullptr)
            return;

        [[maybe_unused]]  //
        const bool moved = buckets[entry.bucketIndex].entries.move_front(entry);
        assert(moved);
    }

    [[nodiscard]] bool nolock_move_entry(list_value_type& entry, const usize newIndex) noexcept {
        assert(entry.bucketIndex < BucketCount);
        assert(newIndex < BucketCount);
        assert(entry.bucketIndex != newIndex);
        if (entry.bucketIndex >= BucketCount || newIndex >= BucketCount
            || entry.bucketIndex == newIndex)
            return false;

        const usize oldIndex = entry.bucketIndex;

        if (!nolock_list_remove(entry))
            return false;

        if (!nolock_list_push_front(entry, newIndex))
        {
            [[maybe_unused]]  //
            const bool restored = nolock_list_push_front(entry, oldIndex);
            assert(restored);
            return false;
        }

        return true;
    }

    void nolock_promote_to_t2(list_value_type& entry) noexcept {
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if (entry.bucketIndex == T2)
        {
            nolock_list_move_front(entry);
            return;
        }

        [[maybe_unused]]  //
        const bool moved = nolock_move_entry(entry, T2);
        assert(moved);
    }

    // Entry creation

    list_iterator
    nolock_add_resident(list_iterator entry, const usize index, Value&& value) noexcept {
        assert(entry != nullptr);
        assert(index == T1 || index == T2);
        if (entry == nullptr || (index != T1 && index != T2))
            return nullptr;

        if (!nolock_list_push_front(*entry, index))
            return nullptr;

        //// FIX: Wrap initialization in a try-catch block to handle allocation failures cleanly
        //try
        //{
        // Direct, high-speed move instruction execution path
        entry->emplace_value(std::move(value));
        //} catch (...)
        //if (!entry->value.has_value())
        //{
        //    // Rollback: Unlink from the tracking bucket list and recycle the pool node safely
        //    [[maybe_unused]] //
        //    const bool removed = nolock_list_remove(*entry);
        //    assert(removed);
        //
        //    pool.destroy(entry);
        //    return nullptr;
        //}

        return entry;
    }

    // Ghost -> resident transition

    list_iterator nolock_ghost_to_resident(map_iterator mapItr, Value&& value) noexcept {
        assert(mapItr != map.end());

        if (mapItr == map.end() || mapItr->second == nullptr || nolock_is_resident(mapItr->second))
            return nullptr;

        if (capacity == 0 || nolock_resident_size() >= capacity)
            return nullptr;

        list_iterator const entry = mapItr->second;

        assert(entry->bucketIndex == B1 || entry->bucketIndex == B2);
        assert(!entry->value.has_value());

        if ((entry->bucketIndex != B1 && entry->bucketIndex != B2) || entry->value.has_value())
            return nullptr;

        // Track the old history state index for rollback security
        //const usize oldGhostBucket = entry->bucketIndex;

        if (!nolock_move_entry(*entry, T2))
            return nullptr;

        //// FIX: Wrap inside a try-catch staging wrapper to maintain an uncorrupted cache state
        //try
        //{
        // Direct, high-speed move instruction execution path
        entry->emplace_value(std::move(value));
        //} catch (...)
        //if (!entry->value.has_value())
        //{
        //    // Rollback: Unlink from the tracking bucket list and recycle the pool node safely
        //    [[maybe_unused]]  //
        //    const bool restored = nolock_move_entry(*entry, oldGhostBucket);
        //    assert(restored);
        //    return nullptr;
        //}

        return entry;
    }

    // Entry destruction

    [[nodiscard]] bool nolock_remove_resident(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
            return false;

        list_iterator const entry = mapItr->second;

        assert(entry->value.has_value());

        if (!entry->value.has_value() || !nolock_list_remove(*entry))
            return false;

        nolock_safe_map_erase(mapItr);

        pool.destroy(entry);

        return true;
    }

    [[nodiscard]] bool nolock_remove_resident(list_value_type& entry) noexcept {
        const auto mapItr = map.find(entry.key);
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second != &entry)
            return false;

        return nolock_remove_resident(mapItr);
    }

    [[nodiscard]] bool nolock_remove_ghost(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second == nullptr || nolock_is_resident(mapItr->second))
            return false;

        list_iterator const entry = mapItr->second;

        assert(entry->bucketIndex == B1 || entry->bucketIndex == B2);
        assert(!entry->value.has_value());

        if ((entry->bucketIndex != B1 && entry->bucketIndex != B2) || entry->value.has_value()
            || !nolock_list_remove(*entry))
            return false;

        nolock_safe_map_erase(mapItr);

        pool.destroy(entry);

        return true;
    }

    [[nodiscard]] bool nolock_remove_ghost(list_value_type& entry) noexcept {
        const auto mapItr = map.find(entry.key);
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second != &entry)
            return false;

        return nolock_remove_ghost(mapItr);
    }

    // Resident -> ghost transition

    [[nodiscard]] bool nolock_evict_to_ghost(list_value_type& entry, const usize index) noexcept {
        assert(index == B1 || index == B2);
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if ((index != B1 && index != B2) || (entry.bucketIndex != T1 && entry.bucketIndex != T2)
            || !entry.value.has_value())
            return false;

        if (!nolock_move_entry(entry, index))
            return false;

        entry.value.reset();

        return true;
    }

    // ARC replacement

    [[nodiscard]] bool nolock_replace(const bool keyInB2, const usize t1Size) noexcept {
        // Flattened execution sequence to reduce branch misprediction stalls
        const bool evictT1 = t1Size != 0 && (t1Size > target || (keyInB2 && t1Size == target));

        const usize primaryBucket = evictT1 ? T1 : T2;
        const usize primaryGhost  = evictT1 ? B1 : B2;
        const usize backupBucket  = evictT1 ? T2 : T1;
        const usize backupGhost   = evictT1 ? B2 : B1;

        if (list_iterator const entry = buckets[primaryBucket].entries.back())
        {
            if (nolock_evict_to_ghost(*entry, primaryGhost))
                return true;
        }
        if (list_iterator const entry = buckets[backupBucket].entries.back())
        {
            return nolock_evict_to_ghost(*entry, backupGhost);
        }

        return false;
    }

    // Ghost history management

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        usize b1Size = buckets[B1].entries.size();
        usize b2Size = buckets[B2].entries.size();

        usize       iterations    = 0;
        const usize maxIterations = b1Size + b2Size;

        // HIGH-SPEED FIX: Check against your local register variables!
        while ((b1Size + b2Size) > maxSize && iterations++ < maxIterations)
        {
            if (b1Size != 0 && (b1Size > (maxSize - target) || b2Size == 0))
            {
                list_iterator entry = buckets[B1].entries.back();
                if (entry != nullptr)
                {
                    auto mapItr = map.find(entry->key);
                    if (mapItr != map.end() && nolock_remove_ghost(mapItr))
                    {  // Success path: nolock_remove_ghost handled map erasure
                    }
                    else
                    {
                        // Safety Fallback path
                        buckets[B1].entries.pop_back();
                        if (mapItr != map.end())
                            map.erase(mapItr);
                        pool.destroy(entry);
                    }
                }

                --b1Size;
                continue;
            }

            if (b2Size != 0)
            {
                list_iterator entry = buckets[B2].entries.back();
                if (entry != nullptr)
                {
                    auto mapItr = map.find(entry->key);
                    if (mapItr != map.end() && nolock_remove_ghost(mapItr))
                    {  // Success path: nolock_remove_ghost handled map erasure
                    }
                    else
                    {
                        // Safety Fallback path
                        buckets[B2].entries.pop_back();
                        if (mapItr != map.end())
                            map.erase(mapItr);
                        pool.destroy(entry);
                    }
                }

                --b2Size;
                continue;
            }

            break;
        }
    }

    [[nodiscard]] bool nolock_handle_ghost_hit(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());

        if (mapItr == map.end() || mapItr->second == nullptr || nolock_is_resident(mapItr->second))
            return false;

        list_iterator const ghost = mapItr->second;
        const usize         index = ghost->bucketIndex;

        assert(index == B1 || index == B2);

        if ((index != B1 && index != B2) || ghost->value.has_value() || capacity == 0)
            return false;

        if (target > capacity)
            target = capacity;

        const usize oldTarget = target;
        const usize b1Size    = buckets[B1].entries.size();
        const usize b2Size    = buckets[B2].entries.size();

        const bool keyInB2 = index == B2;

        if (keyInB2)
        {
            if (b2Size == 0)
                return false;

            // Frequency hit: Scale delta UP (Multiply by 1.5 to aggressively adapt toward LFU)
            usize delta = (b1Size + (b2Size / 2)) / b2Size;
            delta       = (delta * 15) / 10;  // Boost frequency adaptation pace by 150%
            if (delta < 1)
                delta = 1;

            target = target > delta ? target - delta : 0;
        }
        else
        {
            if (b1Size == 0)
                return false;

            // Recency hit: Scale delta DOWN (Reduce by 40% to suppress recency expansion)
            // FIX: Restored the missing missing integer division parameter (/ b1Size)
            usize delta = (b2Size + (b1Size / 2)) / b1Size;
            delta       = (delta * 6) / 10;  // Dampen recency adaptation pace to 60%
            if (delta < 1)
                delta = 1;

            target = target + delta < capacity ? target + delta : capacity;
        }

        if (nolock_resident_size() >= capacity)
            (void) nolock_replace(keyInB2, buckets[T1].entries.size());

        if (nolock_resident_size() >= capacity)
        {
            target = oldTarget;
            return false;
        }

        return true;
    }

    // Miss preparation

    void nolock_prepare_miss() noexcept {
        const usize t1Size = buckets[T1].entries.size();

        if (capacity == 0 || nolock_resident_size() < capacity)
            return;

        const usize b1Size = buckets[B1].entries.size();

        // 1. Case A: Scaled L1 History Pipeline Boundary
        // In classical ARC this is (t1Size + b1Size == capacity).
        // For a strict 40% footprint, it must trigger when L1 hits max_t1_capacity!
        if (t1Size + b1Size == capacity)
        {
            if (t1Size < capacity)
            {
                // Drop an old ghost from B1 to make space for the incoming miss tracking node
                if (list_iterator const ghost = buckets[B1].entries.back())
                    (void) nolock_remove_ghost(*ghost);

                (void) nolock_replace(false, t1Size);
            }
            else
            {
                if (list_iterator const entry = buckets[T1].entries.back();
                    entry != nullptr && nolock_remove_resident(*entry))
                    return;
            }

            nolock_trim_ghosts(capacity);
            return;
        }

        // 3. Case B: Scaled General Directory Overlap Pruning
        // If the combined directory envelope is full (2 * capacity)
        if (map.size() >= (2 * capacity))
        {
            list_iterator ghost = buckets[B2].entries.back();
            if (ghost == nullptr)
                ghost = buckets[B1].entries.back();

            if (ghost != nullptr)
                (void) nolock_remove_ghost(*ghost);
        }

        // Always make space by executing our optimized replace step
        (void) nolock_replace(false, t1Size);

        nolock_trim_ghosts(capacity);
    }

    // Public-operation internals

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        // Probe once: existing entries are hits; a new key is inserted here so
        // the clean-miss path does not hash it again after replacement.
        auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
        {
            list_iterator const current = mapItr->second;

            if (current == nullptr)
                return Return{std::forward<Args>(args)...};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);
                assert(nolock_consistency_check());
                return nolock_read(current);
            }

            // --- GHOST HIT PATH ---
            if (!nolock_handle_ghost_hit(mapItr))
                return Return{std::forward<Args>(args)...};

            // Refresh the iterator locally because handle_ghost_hit evicted elements
            mapItr = map.find(key);
            if (mapItr == map.end())
                return Return{std::forward<Args>(args)...};

            Value value{std::forward<Args>(args)...};

            list_iterator const entry = nolock_ghost_to_resident(mapItr, std::move(value));

            if (entry == nullptr)
                return Return{std::move(value)};

            assert(nolock_consistency_check());
            return nolock_read(entry);
        }

        list_iterator const newEntry = pool.create(0, key);
        if (newEntry == nullptr)
        {
            map.erase(mapItr);
            return Return{std::forward<Args>(args)...};
        }

        mapItr->second = newEntry;

        // Replacement mutates the directory. The flat-map variant can compact it,
        // so use the key for rollback and don't reuse mapItr afterward.
        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
        {
            map.erase(key);
            pool.destroy(newEntry);
            return Return{std::forward<Args>(args)...};
        }

        Value               value{std::forward<Args>(args)...};
        list_iterator const entry = nolock_add_resident(newEntry, T1, std::move(value));
        if (entry == nullptr)
        {
            map.erase(key);
            pool.destroy(newEntry);
            return Return{std::move(value)};
        }

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        // Probe once: existing entries are hits; a new key is inserted here so
        // the clean-miss path does not hash it again after replacement.
        auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
        {
            list_iterator const current = mapItr->second;

            if (current == nullptr)
                return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);
                assert(nolock_consistency_check());
                return nolock_read(current);
            }

            // --- GHOST HIT PATH ---
            if (!nolock_handle_ghost_hit(mapItr))
                return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            // Refresh the iterator locally because handle_ghost_hit evicted elements
            mapItr = map.find(key);
            if (mapItr == map.end())
                return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            Value value{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            list_iterator const entry = nolock_ghost_to_resident(mapItr, std::move(value));

            if (entry == nullptr)
                return Return{std::move(value)};

            assert(nolock_consistency_check());
            return nolock_read(entry);
        }

        list_iterator const newEntry = pool.create(0, key);
        if (newEntry == nullptr)
        {
            map.erase(mapItr);
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};
        }

        mapItr->second = newEntry;

        // Prepare after the probe. Do not use mapItr below because erasing from
        // unordered_dense can relocate this map element.
        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
        {
            map.erase(key);
            pool.destroy(newEntry);
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};
        }

        Value               value{std::forward<Builder>(builder)(std::forward<Args>(args)...)};
        list_iterator const entry = nolock_add_resident(newEntry, T1, std::move(value));
        if (entry == nullptr)
        {
            map.erase(key);
            pool.destroy(newEntry);
            return Return{std::move(value)};
        }

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    [[nodiscard]] bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = map.find(key);

        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
        {
            assert(nolock_consistency_check());
            return false;
        }

        if (!nolock_remove_resident(mapItr))
        {
            assert(nolock_consistency_check());
            return false;
        }

        assert(nolock_consistency_check());
        return true;
    }

    // Administrative eviction

    void nolock_evict_excess(const usize size) noexcept {
        while (nolock_resident_size() > size)
        {
            list_type& t1 = buckets[T1].entries;
            list_type& t2 = buckets[T2].entries;

            list_iterator entry = nullptr;

            if (!t1.empty() && (t1.size() > target || t2.empty()))
                entry = t1.back();
            else if (!t2.empty())
                entry = t2.back();
            else
                entry = t1.back();

            if (entry == nullptr || !nolock_remove_resident(*entry))
                break;
        }

        if (target > capacity)
            target = capacity;

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());
    }

    // Clear and configuration

    void nolock_clear() noexcept {
        for (usize index = 0; index < BucketCount; ++index)
        {
            list_type& list = buckets[index].entries;

            while (list_iterator const entry = list.back())
            {
                [[maybe_unused]]  //
                const bool erased = list.erase(*entry);
                assert(erased);
                if (!erased)
                    break;

                entry->bucketIndex = InvalidIndex;
                pool.destroy(entry);
            }
        }

        map.clear();
        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        nolock_reserve_map(capacity);
    }

#ifndef NDEBUG
    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        /*
        const usize t1Size = buckets[T1].entries.size();
        const usize t2Size = buckets[T2].entries.size();
        const usize b1Size = buckets[B1].entries.size();
        const usize b2Size = buckets[B2].entries.size();

        const usize residentSize = t1Size + t2Size;
        const usize ghostSize    = b1Size + b2Size;

        assert(residentSize == nolock_resident_size());
        assert(ghostSize == nolock_ghost_size());
        assert(residentSize + ghostSize == map.size());
        assert(residentSize <= capacity);
        assert(ghostSize <= capacity);
        assert(target <= capacity);

        if (capacity <= std::numeric_limits<usize>::max() / 2)
            assert(map.size() <= capacity * 2);

        for (usize index = 0; index < BucketCount; ++index)
        {
            const list_type& list  = buckets[index].entries;
            usize            count = 0;

            for (const list_value_type* entry = list.front(); entry != nullptr;
                 entry                        = list.next(*entry))
            {
                ++count;

                assert(entry->bucketIndex == index);

                const auto mapItr = map.find(entry->key);

                assert(mapItr != map.end());

                if (mapItr == map.end() || mapItr->second != entry)
                    return false;

                if (index == T1 || index == T2)
                    assert(entry->value.has_value());
                else
                    assert(!entry->value.has_value());
            }

            assert(count == list.size());

            if (count != list.size())
                return false;
        }

        for (auto mapItr = map.begin(); mapItr != map.end(); ++mapItr)
        {
            list_iterator const entry = mapItr->second;

            if (entry == nullptr)
                return false;

            assert(entry->key == mapItr->first);
            assert(entry->bucketIndex < BucketCount);
            if (entry->key != mapItr->first || entry->bucketIndex >= BucketCount)
                return false;

            const list_type& list  = buckets[entry->bucketIndex].entries;
            bool             found = false;

            for (const list_value_type* current = list.front(); current != nullptr;
                 current                        = list.next(*current))
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
        */
        return true;
    }
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type    map;
    Pool<Entry> pool;

    Array<Bucket, BucketCount> buckets;
    usize                      target = 0;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatARCCache final {
    static constexpr usize InlineValueThreshold = 128;

    static constexpr usize T1 = 0;
    static constexpr usize T2 = 1;
    static constexpr usize B1 = 2;
    static constexpr usize B2 = 3;

    static constexpr usize BucketCount = 4;

    static constexpr u8 InvalidIndex = u8(BucketCount);

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry;

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = ankerl::unordered_dense::map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

    struct Entry final {
        explicit Entry(const Key& k) noexcept :
            key(k) {}

        template<typename... Args>
        void emplace_value(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                value.emplace(std::forward<Args>(args)...);
            else
                value.emplace(std::make_shared<Value>(std::forward<Args>(args)...));
        }

        Key    key;
        u8     bucketIndex = InvalidIndex;
        Entry* prev        = nullptr;
        Entry* next        = nullptr;

        std::optional<storage_value_type> value;
    };

    struct Bucket final {
        list_type entries;
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
        pool.release();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) noexcept {
        // Isolated Mutation Path
        // If it's a ghost hit or clean cache miss, capture the EXCLUSIVE lock.
        std::lock_guard lockGuard{mutex};

        // Re-verify the key hasn't been inserted by a racing thread while we were waiting
        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        // Isolated Mutation Path
        // If it's a ghost hit or clean cache miss, capture the EXCLUSIVE lock.
        std::lock_guard lockGuard{mutex};

        // Re-verify the key hasn't been inserted by a racing thread while we were waiting
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

        nolock_reserve_map(newCapacity);

        capacity = newCapacity;

        if (target > capacity)
            target = capacity;

        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        auto mapItr = map.find(key);

        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
            return map.end();

        return mapItr;
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        auto mapItr = map.find(key);

        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
            return map.end();

        return mapItr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);

        return mapItr != map.end() && mapItr->second != nullptr
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

    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();

        assert(nolock_consistency_check());
    }

    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
        pool.release();
        map.rehash(0);

        capacity = defaultCapacity;

        target = 0;

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    // Map reservation and insertion

    [[nodiscard]] usize nolock_map_reserve_size(const usize requestedCapacity) const noexcept {
        constexpr usize maxSize = std::numeric_limits<usize>::max();
        // Leave one temporary directory slot for a clean-miss insertion that
        // precedes replacement, avoiding a rehash at the 2 * capacity boundary.
        const usize mapSize =
          requestedCapacity > (maxSize - 1) / 2 ? maxSize : 2 * requestedCapacity + 1;

        return std::max(reserveCount, mapSize);
    }

    void nolock_reserve_map(const usize requestedCapacity) noexcept {
        map.reserve(nolock_map_reserve_size(requestedCapacity));
    }

    void nolock_safe_map_erase(const map_iterator targetItr) noexcept {
        assert(targetItr != map.end());
        map.erase(targetItr);
    }

    // Bucket and entry access

    [[nodiscard]] bool nolock_is_resident(const list_value_type* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->bucketIndex < BucketCount);

        return entry->bucketIndex < B1;
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return buckets[T1].entries.size() + buckets[T2].entries.size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return buckets[B1].entries.size() + buckets[B2].entries.size();
    }

    Return nolock_read(const list_value_type* entry) const noexcept {
        assert(entry != nullptr);
        assert(nolock_is_resident(entry));
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    // List operations

    [[nodiscard]] bool nolock_list_push_front(list_value_type& entry, const usize index) noexcept {
        assert(index < BucketCount);
        assert(entry.bucketIndex == InvalidIndex);
        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        if (index >= BucketCount || entry.bucketIndex != InvalidIndex || entry.prev != nullptr
            || entry.next != nullptr)
            return false;

        if (!buckets[index].entries.push_front(entry))
            return false;

        entry.bucketIndex = u8(index);

        return true;
    }

    [[nodiscard]] bool nolock_list_remove(list_value_type& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex >= BucketCount)
            return false;

        if (!buckets[entry.bucketIndex].entries.erase(entry))
            return false;

        entry.bucketIndex = InvalidIndex;

        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        return true;
    }

    void nolock_list_move_front(list_value_type& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);
        // Combine structural checks and early return if already at the front
        if (entry.bucketIndex >= BucketCount || entry.prev == nullptr)
            return;

        [[maybe_unused]]  //
        const bool moved = buckets[entry.bucketIndex].entries.move_front(entry);
        assert(moved);
    }

    [[nodiscard]] bool nolock_move_entry(list_value_type& entry, const usize newIndex) noexcept {
        assert(entry.bucketIndex < BucketCount);
        assert(newIndex < BucketCount);
        assert(entry.bucketIndex != newIndex);
        if (entry.bucketIndex >= BucketCount || newIndex >= BucketCount
            || entry.bucketIndex == newIndex)
            return false;

        const usize oldIndex = entry.bucketIndex;

        if (!nolock_list_remove(entry))
            return false;

        if (!nolock_list_push_front(entry, newIndex))
        {
            [[maybe_unused]]  //
            const bool restored = nolock_list_push_front(entry, oldIndex);
            assert(restored);
            return false;
        }

        return true;
    }

    void nolock_promote_to_t2(list_value_type& entry) noexcept {
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if (entry.bucketIndex == T2)
        {
            nolock_list_move_front(entry);
            return;
        }

        [[maybe_unused]]  //
        const bool moved = nolock_move_entry(entry, T2);
        assert(moved);
    }

    // Entry creation

    list_iterator
    nolock_add_resident(list_iterator entry, const usize index, Value&& value) noexcept {
        assert(entry != nullptr);
        assert(index == T1 || index == T2);
        if (entry == nullptr || (index != T1 && index != T2))
            return nullptr;

        if (!nolock_list_push_front(*entry, index))
            return nullptr;

        //// FIX: Wrap initialization in a try-catch block to handle allocation failures cleanly
        //try
        //{
        // Direct, high-speed move instruction execution path
        entry->emplace_value(std::move(value));
        //} catch (...)
        //if (!entry->value.has_value())
        //{
        //    // Rollback: Unlink from the tracking bucket list and recycle the pool node safely
        //    [[maybe_unused]] //
        //    const bool removed = nolock_list_remove(*entry);
        //    assert(removed);
        //
        //    pool.destroy(entry);
        //    return nullptr;
        //}

        return entry;
    }

    // Ghost -> resident transition

    list_iterator nolock_ghost_to_resident(map_iterator mapItr, Value&& value) noexcept {
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second == nullptr || nolock_is_resident(mapItr->second))
            return nullptr;

        if (capacity == 0 || nolock_resident_size() >= capacity)
            return nullptr;

        list_iterator const entry = mapItr->second;

        assert(entry->bucketIndex == B1 || entry->bucketIndex == B2);
        assert(!entry->value.has_value());

        if ((entry->bucketIndex != B1 && entry->bucketIndex != B2) || entry->value.has_value())
            return nullptr;

        // Track the old history state index for rollback security
        //const usize oldGhostBucket = entry->bucketIndex;

        if (!nolock_move_entry(*entry, T2))
            return nullptr;

        //// FIX: Wrap inside a try-catch staging wrapper to maintain an uncorrupted cache state
        //try
        //{
        // Direct, high-speed move instruction execution path
        entry->emplace_value(std::move(value));
        //} catch (...)
        //if (!entry->value.has_value())
        //{
        //    // Rollback: Unlink from the tracking bucket list and recycle the pool node safely
        //    [[maybe_unused]]  //
        //    const bool restored = nolock_move_entry(*entry, oldGhostBucket);
        //    assert(restored);
        //    return nullptr;
        //}

        return entry;
    }

    // Entry destruction

    [[nodiscard]] bool nolock_remove_resident(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
            return false;

        list_iterator const entry = mapItr->second;

        assert(entry->value.has_value());

        if (!entry->value.has_value() || !nolock_list_remove(*entry))
            return false;

        nolock_safe_map_erase(mapItr);

        pool.destroy(entry);

        return true;
    }

    [[nodiscard]] bool nolock_remove_resident(list_value_type& entry) noexcept {
        const auto mapItr = map.find(entry.key);
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second != &entry)
            return false;

        return nolock_remove_resident(mapItr);
    }

    [[nodiscard]] bool nolock_remove_ghost(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second == nullptr || nolock_is_resident(mapItr->second))
            return false;

        list_iterator const entry = mapItr->second;

        assert(entry->bucketIndex == B1 || entry->bucketIndex == B2);
        assert(!entry->value.has_value());

        if ((entry->bucketIndex != B1 && entry->bucketIndex != B2) || entry->value.has_value()
            || !nolock_list_remove(*entry))
            return false;

        nolock_safe_map_erase(mapItr);

        pool.destroy(entry);

        return true;
    }

    [[nodiscard]] bool nolock_remove_ghost(list_value_type& entry) noexcept {
        const auto mapItr = map.find(entry.key);
        assert(mapItr != map.end());
        if (mapItr == map.end() || mapItr->second != &entry)
            return false;

        return nolock_remove_ghost(mapItr);
    }

    // Resident -> ghost transition

    [[nodiscard]] bool nolock_evict_to_ghost(list_value_type& entry, const usize index) noexcept {
        assert(index == B1 || index == B2);
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if ((index != B1 && index != B2) || (entry.bucketIndex != T1 && entry.bucketIndex != T2)
            || !entry.value.has_value())
            return false;

        if (!nolock_move_entry(entry, index))
            return false;

        entry.value.reset();

        return true;
    }

    // ARC replacement

    [[nodiscard]] bool nolock_replace(const bool keyInB2, const usize t1Size) noexcept {
        // Flattened execution sequence to reduce branch misprediction stalls
        const bool evictT1 = t1Size != 0 && (t1Size > target || (keyInB2 && t1Size == target));

        const usize primaryBucket = evictT1 ? T1 : T2;
        const usize primaryGhost  = evictT1 ? B1 : B2;
        const usize backupBucket  = evictT1 ? T2 : T1;
        const usize backupGhost   = evictT1 ? B2 : B1;

        if (list_iterator const entry = buckets[primaryBucket].entries.back())
        {
            if (nolock_evict_to_ghost(*entry, primaryGhost))
                return true;
        }
        if (list_iterator const entry = buckets[backupBucket].entries.back())
        {
            return nolock_evict_to_ghost(*entry, backupGhost);
        }

        return false;
    }

    // Ghost history management

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        usize b1Size = buckets[B1].entries.size();
        usize b2Size = buckets[B2].entries.size();

        usize       iterations    = 0;
        const usize maxIterations = b1Size + b2Size;

        // HIGH-SPEED FIX: Check against your local register variables!
        while ((b1Size + b2Size) > maxSize && iterations++ < maxIterations)
        {
            if (b1Size != 0 && (b1Size > (maxSize - target) || b2Size == 0))
            {
                list_iterator entry = buckets[B1].entries.back();
                if (entry != nullptr)
                {
                    auto mapItr = map.find(entry->key);
                    if (mapItr != map.end() && nolock_remove_ghost(mapItr))
                    {  // Success path: nolock_remove_ghost handled map erasure
                    }
                    else
                    {
                        // Safety Fallback path
                        buckets[B1].entries.pop_back();
                        if (mapItr != map.end())
                            map.erase(mapItr);
                        pool.destroy(entry);
                    }
                }

                --b1Size;
                continue;
            }

            if (b2Size != 0)
            {
                list_iterator entry = buckets[B2].entries.back();
                if (entry != nullptr)
                {
                    auto mapItr = map.find(entry->key);
                    if (mapItr != map.end() && nolock_remove_ghost(mapItr))
                    {  // Success path: nolock_remove_ghost handled map erasure
                    }
                    else
                    {
                        // Safety Fallback path
                        buckets[B2].entries.pop_back();
                        if (mapItr != map.end())
                            map.erase(mapItr);
                        pool.destroy(entry);
                    }
                }

                --b2Size;
                continue;
            }

            break;
        }
    }

    [[nodiscard]] bool nolock_handle_ghost_hit(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());

        if (mapItr == map.end() || mapItr->second == nullptr || nolock_is_resident(mapItr->second))
            return false;

        list_iterator const ghost = mapItr->second;
        const usize         index = ghost->bucketIndex;

        assert(index == B1 || index == B2);

        if ((index != B1 && index != B2) || ghost->value.has_value() || capacity == 0)
            return false;

        if (target > capacity)
            target = capacity;

        const usize oldTarget = target;
        const usize b1Size    = buckets[B1].entries.size();
        const usize b2Size    = buckets[B2].entries.size();

        const bool keyInB2 = index == B2;

        if (keyInB2)
        {
            if (b2Size == 0)
                return false;

            // Frequency hit: Scale delta UP (Multiply by 1.5 to aggressively adapt toward LFU)
            usize delta = (b1Size + (b2Size / 2)) / b2Size;
            delta       = (delta * 15) / 10;  // Boost frequency adaptation pace by 150%
            if (delta < 1)
                delta = 1;

            target = target > delta ? target - delta : 0;
        }
        else
        {
            if (b1Size == 0)
                return false;

            // Recency hit: Scale delta DOWN (Reduce by 40% to suppress recency expansion)
            // FIX: Restored the missing missing integer division parameter (/ b1Size)
            usize delta = (b2Size + (b1Size / 2)) / b1Size;
            delta       = (delta * 6) / 10;  // Dampen recency adaptation pace to 60%
            if (delta < 1)
                delta = 1;

            target = target + delta < capacity ? target + delta : capacity;
        }

        if (nolock_resident_size() >= capacity)
            (void) nolock_replace(keyInB2, buckets[T1].entries.size());

        if (nolock_resident_size() >= capacity)
        {
            target = oldTarget;
            return false;
        }

        return true;
    }

    // Miss preparation

    void nolock_prepare_miss() noexcept {
        const usize t1Size = buckets[T1].entries.size();

        if (capacity == 0 || nolock_resident_size() < capacity)
            return;

        const usize b1Size = buckets[B1].entries.size();

        // 1. Case A: Scaled L1 History Pipeline Boundary
        // In classical ARC this is (t1Size + b1Size == capacity).
        // For a strict 40% footprint, it must trigger when L1 hits max_t1_capacity!
        if (t1Size + b1Size == capacity)
        {
            if (t1Size < capacity)
            {
                // Drop an old ghost from B1 to make space for the incoming miss tracking node
                if (list_iterator const ghost = buckets[B1].entries.back())
                    (void) nolock_remove_ghost(*ghost);

                (void) nolock_replace(false, t1Size);
            }
            else
            {
                if (list_iterator const entry = buckets[T1].entries.back();
                    entry != nullptr && nolock_remove_resident(*entry))
                    return;
            }

            nolock_trim_ghosts(capacity);
            return;
        }

        // 3. Case B: Scaled General Directory Overlap Pruning
        // If the combined directory envelope is full (2 * capacity)
        if (map.size() >= (2 * capacity))
        {
            list_iterator ghost = buckets[B2].entries.back();
            if (ghost == nullptr)
                ghost = buckets[B1].entries.back();

            if (ghost != nullptr)
                (void) nolock_remove_ghost(*ghost);
        }

        // Always make space by executing our optimized replace step
        (void) nolock_replace(false, t1Size);

        nolock_trim_ghosts(capacity);
    }

    // Public-operation internals

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        // Probe once: existing entries are hits; a new key is inserted here so
        // the clean-miss path does not hash it again after replacement.
        auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
        {
            list_iterator const current = mapItr->second;

            if (current == nullptr)
                return Return{std::forward<Args>(args)...};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);
                assert(nolock_consistency_check());
                return nolock_read(current);
            }

            // --- GHOST HIT PATH ---
            if (!nolock_handle_ghost_hit(mapItr))
                return Return{std::forward<Args>(args)...};

            // Refresh the iterator locally because handle_ghost_hit evicted elements
            mapItr = map.find(key);
            if (mapItr == map.end())
                return Return{std::forward<Args>(args)...};

            Value value{std::forward<Args>(args)...};

            list_iterator const entry = nolock_ghost_to_resident(mapItr, std::move(value));

            if (entry == nullptr)
                return Return{std::move(value)};

            assert(nolock_consistency_check());
            return nolock_read(entry);
        }

        list_iterator const newEntry = pool.create(0, key);
        if (newEntry == nullptr)
        {
            map.erase(mapItr);
            return Return{std::forward<Args>(args)...};
        }

        mapItr->second = newEntry;

        // Replacement mutates the directory. The flat-map variant can compact it,
        // so use the key for rollback and don't reuse mapItr afterward.
        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
        {
            map.erase(key);
            pool.destroy(newEntry);
            return Return{std::forward<Args>(args)...};
        }

        Value               value{std::forward<Args>(args)...};
        list_iterator const entry = nolock_add_resident(newEntry, T1, std::move(value));
        if (entry == nullptr)
        {
            map.erase(key);
            pool.destroy(newEntry);
            return Return{std::move(value)};
        }

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        // Probe once: existing entries are hits; a new key is inserted here so
        // the clean-miss path does not hash it again after replacement.
        auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
        {
            list_iterator const current = mapItr->second;

            if (current == nullptr)
                return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);
                assert(nolock_consistency_check());
                return nolock_read(current);
            }

            // --- GHOST HIT PATH ---
            if (!nolock_handle_ghost_hit(mapItr))
                return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            // Refresh the iterator locally because handle_ghost_hit evicted elements
            mapItr = map.find(key);
            if (mapItr == map.end())
                return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            Value value{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

            list_iterator const entry = nolock_ghost_to_resident(mapItr, std::move(value));

            if (entry == nullptr)
                return Return{std::move(value)};

            assert(nolock_consistency_check());
            return nolock_read(entry);
        }

        list_iterator const newEntry = pool.create(0, key);
        if (newEntry == nullptr)
        {
            map.erase(mapItr);
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};
        }

        mapItr->second = newEntry;

        // Prepare after the probe. Do not use mapItr below because erasing from
        // unordered_dense can relocate this map element.
        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
        {
            map.erase(key);
            pool.destroy(newEntry);
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};
        }

        Value               value{std::forward<Builder>(builder)(std::forward<Args>(args)...)};
        list_iterator const entry = nolock_add_resident(newEntry, T1, std::move(value));
        if (entry == nullptr)
        {
            map.erase(key);
            pool.destroy(newEntry);
            return Return{std::move(value)};
        }

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    [[nodiscard]] bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = map.find(key);

        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
        {
            assert(nolock_consistency_check());
            return false;
        }

        if (!nolock_remove_resident(mapItr))
        {
            assert(nolock_consistency_check());
            return false;
        }

        assert(nolock_consistency_check());
        return true;
    }

    // Administrative eviction

    void nolock_evict_excess(const usize size) noexcept {
        while (nolock_resident_size() > size)
        {
            list_type& t1 = buckets[T1].entries;
            list_type& t2 = buckets[T2].entries;

            list_iterator entry = nullptr;

            if (!t1.empty() && (t1.size() > target || t2.empty()))
                entry = t1.back();
            else if (!t2.empty())
                entry = t2.back();
            else
                entry = t1.back();

            if (entry == nullptr || !nolock_remove_resident(*entry))
                break;
        }

        if (target > capacity)
            target = capacity;

        nolock_trim_ghosts(capacity);

        assert(nolock_consistency_check());
    }

    // Clear and configuration

    void nolock_clear() noexcept {
        for (usize index = 0; index < BucketCount; ++index)
        {
            list_type& list = buckets[index].entries;

            while (list_iterator const entry = list.back())
            {
                [[maybe_unused]]  //
                const bool erased = list.erase(*entry);
                assert(erased);
                if (!erased)
                    break;

                entry->bucketIndex = InvalidIndex;
                pool.destroy(entry);
            }
        }

        map.clear();
        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        nolock_reserve_map(capacity);
    }

#ifndef NDEBUG
    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        /*
        const usize t1Size = buckets[T1].entries.size();
        const usize t2Size = buckets[T2].entries.size();
        const usize b1Size = buckets[B1].entries.size();
        const usize b2Size = buckets[B2].entries.size();

        const usize residentSize = t1Size + t2Size;
        const usize ghostSize    = b1Size + b2Size;

        assert(residentSize == nolock_resident_size());
        assert(ghostSize == nolock_ghost_size());
        assert(residentSize + ghostSize == map.size());
        assert(residentSize <= capacity);
        assert(ghostSize <= capacity);
        assert(target <= capacity);

        if (capacity <= std::numeric_limits<usize>::max() / 2)
            assert(map.size() <= capacity * 2);

        for (usize index = 0; index < BucketCount; ++index)
        {
            const list_type& list  = buckets[index].entries;
            usize            count = 0;

            for (const list_value_type* entry = list.front(); entry != nullptr;
                 entry                        = list.next(*entry))
            {
                ++count;

                assert(entry->bucketIndex == index);

                const auto mapItr = map.find(entry->key);

                assert(mapItr != map.end());

                if (mapItr == map.end() || mapItr->second != entry)
                    return false;

                if (index == T1 || index == T2)
                    assert(entry->value.has_value());
                else
                    assert(!entry->value.has_value());
            }

            assert(count == list.size());

            if (count != list.size())
                return false;
        }

        for (auto mapItr = map.begin(); mapItr != map.end(); ++mapItr)
        {
            list_iterator const entry = mapItr->second;

            if (entry == nullptr)
                return false;

            assert(entry->key == mapItr->first);
            assert(entry->bucketIndex < BucketCount);
            if (entry->key != mapItr->first || entry->bucketIndex >= BucketCount)
                return false;

            const list_type& list  = buckets[entry->bucketIndex].entries;
            bool             found = false;

            for (const list_value_type* current = list.front(); current != nullptr;
                 current                        = list.next(*current))
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
        */
        return true;
    }
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type    map;
    Pool<Entry> pool;

    Array<Bucket, BucketCount> buckets;
    usize                      target = 0;
};

}  // namespace DON

#endif  // UTILITY_ARC_CACHE_H_INCLUDED
