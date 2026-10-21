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

    static constexpr usize T1 = 0;
    static constexpr usize T2 = 1;
    static constexpr usize B1 = 2;
    static constexpr usize B2 = 3;

    static constexpr usize BucketCount = 4;

    static constexpr u8 InvalidBucket = u8(BucketCount);

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry;

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = std::unordered_map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

    struct Entry {
        Key          key;
        u8           bucketIndex = InvalidBucket;
        Entry*       prev        = nullptr;
        Entry*       next        = nullptr;
        map_iterator mapItr{};

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

    struct Bucket {
        list_type entries;
    };

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, map_iterator mapItr) noexcept :
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
        map_type&    map;
        map_iterator iterator;
        bool         active = true;
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

        nolock_reserve_directory(newCapacity);

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
        target   = 0;

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    // Directory reservation and iterator maintenance

    [[nodiscard]] usize
    nolock_directory_reserve_size(const usize requestedCapacity) const noexcept {
        const usize maxSize     = std::numeric_limits<usize>::max();
        const usize historySize = requestedCapacity > maxSize / 2 ? maxSize : requestedCapacity * 2;

        return std::max(reserveCount, historySize);
    }

    void nolock_refresh_map_iterators() noexcept {
        for (auto mapItr = map.begin(); mapItr != map.end(); ++mapItr)
        {
            if (list_iterator const entry = mapItr->second)
                entry->mapItr = mapItr;
        }
    }

    void nolock_reserve_directory(const usize requestedCapacity) noexcept {
        map.reserve(nolock_directory_reserve_size(requestedCapacity));
        nolock_refresh_map_iterators();
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
        assert(entry.bucketIndex == InvalidBucket);
        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        if (index >= BucketCount || entry.bucketIndex != InvalidBucket || entry.prev != nullptr
            || entry.next != nullptr)
            return false;

        if (!buckets[index].entries.push_front(entry))
            return false;

        entry.bucketIndex = static_cast<u8>(index);

        return true;
    }

    [[nodiscard]] bool nolock_list_remove(list_value_type& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex >= BucketCount)
            return false;

        if (!buckets[entry.bucketIndex].entries.erase(entry))
            return false;

        entry.bucketIndex = InvalidBucket;

        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        return true;
    }

    void nolock_list_move_front(list_value_type& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex < BucketCount)
        {
            const bool moved = buckets[entry.bucketIndex].entries.move_front(entry);

            assert(moved);
            (void) moved;
        }
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

        if (nolock_list_push_front(entry, newIndex))
            return true;

        const bool restored = nolock_list_push_front(entry, oldIndex);

        assert(restored);

        return false;
    }

    void nolock_promote_to_t2(list_value_type& entry) noexcept {
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if (entry.bucketIndex == T2)
        {
            if (entry.prev != nullptr)
                nolock_list_move_front(entry);

            return;
        }

        const bool moved = nolock_move_entry(entry, T2);

        assert(moved);
        (void) moved;
    }

    // Entry creation

    template<typename... Args>
    list_iterator
    nolock_add_resident(map_iterator mapItr, const usize index, Args&&... args) noexcept {
        assert(mapItr != map.end());
        assert(mapItr->second == nullptr);
        assert(index == T1 || index == T2);

        if (mapItr == map.end() || mapItr->second != nullptr || (index != T1 && index != T2))
            return nullptr;

        list_iterator const entry = pool.create(0, mapItr->first);

        if (entry == nullptr)
            return nullptr;

        entry->emplace_value(std::forward<Args>(args)...);

        if (!nolock_list_push_front(*entry, index))
        {
            entry->value.reset();
            pool.destroy(entry);

            return nullptr;
        }

        entry->mapItr  = mapItr;
        mapItr->second = entry;

        return entry;
    }

    // Ghost -> resident transition

    template<typename... Args>
    list_iterator nolock_ghost_to_resident(map_iterator mapItr, Args&&... args) noexcept {
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

        entry->emplace_value(std::forward<Args>(args)...);

        if (!nolock_move_entry(*entry, T2))
        {
            entry->value.reset();
            return nullptr;
        }

        return entry;
    }

    // Entry destruction

    void nolock_remove_resident(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());

        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
            return;

        list_iterator const entry = mapItr->second;

        assert(entry->value.has_value());

        if (!entry->value.has_value() || !nolock_list_remove(*entry))
            return;

        map.erase(mapItr);
        pool.destroy(entry);
    }

    void nolock_remove_resident(list_value_type& entry) noexcept {
        const map_iterator mapItr = entry.mapItr;

        assert(mapItr != map.end());

        if (mapItr != map.end() && mapItr->second == &entry)
            nolock_remove_resident(mapItr);
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

        map.erase(mapItr);
        pool.destroy(entry);

        return true;
    }

    [[nodiscard]] bool nolock_remove_ghost(list_value_type& entry) noexcept {
        const map_iterator mapItr = entry.mapItr;

        assert(mapItr != map.end());

        return mapItr != map.end() && mapItr->second == &entry && nolock_remove_ghost(mapItr);
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
        list_type& t1 = buckets[T1].entries;
        list_type& t2 = buckets[T2].entries;

        const bool evictT1 = !t1.empty() && (t1Size > target || (keyInB2 && t1Size == target));

        if (evictT1)
        {
            if (list_iterator const entry = t1.back())
            {
                if (nolock_evict_to_ghost(*entry, B1))
                    return true;
            }
        }

        if (list_iterator const entry = t2.back())
        {
            if (nolock_evict_to_ghost(*entry, B2))
                return true;
        }

        if (list_iterator const entry = t1.back())
            return nolock_evict_to_ghost(*entry, B1);

        return false;
    }

    // Ghost history management

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        usize ghostSize = nolock_ghost_size();

        while (ghostSize > maxSize)
        {
            list_iterator entry = buckets[B2].entries.back();

            if (entry == nullptr)
                entry = buckets[B1].entries.back();

            if (entry == nullptr || !nolock_remove_ghost(*entry))
                break;

            --ghostSize;
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

            usize delta = b1Size / b2Size;
            if (delta < 1)
                delta = 1;

            target = delta < target ? target - delta : usize{0};
        }
        else
        {
            if (b1Size == 0)
                return false;

            usize delta = b2Size / b1Size;
            if (delta < 1)
                delta = 1;

            target += std::min(delta, capacity - target);
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

        assert(t1Size <= capacity);
        assert(b1Size <= capacity);
        assert(nolock_ghost_size() <= capacity);

        if (t1Size == capacity - b1Size)
        {
            if (t1Size < capacity)
            {
                list_iterator const ghost = buckets[B1].entries.back();

                if (ghost == nullptr || !nolock_remove_ghost(*ghost))
                    return;

                (void) nolock_replace(false, t1Size);
            }
            else if (list_iterator const entry = buckets[T1].entries.back())
                nolock_remove_resident(*entry);

            return;
        }

        if (capacity <= std::numeric_limits<usize>::max() / 2 && map.size() >= capacity * 2)
        {
            list_iterator ghost = buckets[B2].entries.back();

            if (ghost == nullptr)
                ghost = buckets[B1].entries.back();

            if (ghost != nullptr)
                (void) nolock_remove_ghost(*ghost);
        }

        (void) nolock_replace(false, t1Size);
    }

    // Public-operation internals

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        auto mapItr = map.find(key);

        if (mapItr != map.end())
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

            if (!nolock_handle_ghost_hit(mapItr))
                return Return{std::forward<Args>(args)...};

            list_iterator const entry =
              nolock_ghost_to_resident(mapItr, std::forward<Args>(args)...);

            if (entry == nullptr)
                return Return{std::forward<Args>(args)...};

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{std::forward<Args>(args)...};

        auto [newItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return Return{std::forward<Args>(args)...};

        MapGuard mapGuard{map, newItr};

        list_iterator const entry = nolock_add_resident(newItr, T1, std::forward<Args>(args)...);

        if (entry == nullptr)
            return Return{std::forward<Args>(args)...};

        mapGuard.commit();

        assert(nolock_consistency_check());

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        auto mapItr = map.find(key);

        if (mapItr != map.end())
        {
            list_iterator const current = mapItr->second;

            if (current == nullptr)
                return Return{};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);

                assert(nolock_consistency_check());

                return nolock_read(current);
            }

            if (!nolock_handle_ghost_hit(mapItr))
                return Return{};

            list_iterator const entry = nolock_ghost_to_resident(
              mapItr, std::forward<Builder>(builder)(std::forward<Args>(args)...));

            if (entry == nullptr)
                return Return{};

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{};

        auto [newItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return Return{};

        MapGuard mapGuard{map, newItr};

        list_iterator const entry = nolock_add_resident(
          newItr, T1, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        if (entry == nullptr)
            return Return{};

        mapGuard.commit();

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

        nolock_remove_resident(mapItr);

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

            if (entry == nullptr)
                break;

            nolock_remove_resident(*entry);
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
                const bool erased = list.erase(*entry);

                assert(erased);

                if (!erased)
                    break;

                entry->bucketIndex = InvalidBucket;
                pool.destroy(entry);
            }
        }

        map.clear();
        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        nolock_reserve_directory(capacity);
    }

#ifndef NDEBUG
    [[nodiscard]] bool nolock_consistency_check() const noexcept {
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

                assert(entry->mapItr == mapItr);

                if (entry->mapItr != mapItr)
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
            assert(entry->mapItr == mapItr);
            assert(entry->bucketIndex < BucketCount);

            if (entry->mapItr != mapItr || entry->bucketIndex >= BucketCount)
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

    static constexpr u8 InvalidBucket = u8(BucketCount);

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry;

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = ankerl::unordered_dense::map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

    struct Entry {
        Key          key;
        u8           bucketIndex = InvalidBucket;
        Entry*       prev        = nullptr;
        Entry*       next        = nullptr;
        map_iterator mapItr{};

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

    struct Bucket {
        list_type entries;
    };

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, map_iterator mapItr) noexcept :
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
        map_type&    map;
        map_iterator iterator;
        bool         active = true;
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

        nolock_reserve_directory(newCapacity);

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
        target   = 0;

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    // Directory reservation and iterator maintenance

    [[nodiscard]] usize
    nolock_directory_reserve_size(const usize requestedCapacity) const noexcept {
        const usize maxSize     = std::numeric_limits<usize>::max();
        const usize historySize = requestedCapacity > maxSize / 2 ? maxSize : requestedCapacity * 2;

        return std::max(reserveCount, historySize);
    }

    void nolock_refresh_map_iterators() noexcept {
        for (auto mapItr = map.begin(); mapItr != map.end(); ++mapItr)
        {
            if (list_iterator const entry = mapItr->second)
                entry->mapItr = mapItr;
        }
    }

    void nolock_reserve_directory(const usize requestedCapacity) noexcept {
        map.reserve(nolock_directory_reserve_size(requestedCapacity));
        nolock_refresh_map_iterators();
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
        assert(entry.bucketIndex == InvalidBucket);
        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        if (index >= BucketCount || entry.bucketIndex != InvalidBucket || entry.prev != nullptr
            || entry.next != nullptr)
            return false;

        if (!buckets[index].entries.push_front(entry))
            return false;

        entry.bucketIndex = static_cast<u8>(index);

        return true;
    }

    [[nodiscard]] bool nolock_list_remove(list_value_type& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex >= BucketCount)
            return false;

        if (!buckets[entry.bucketIndex].entries.erase(entry))
            return false;

        entry.bucketIndex = InvalidBucket;

        assert(entry.prev == nullptr);
        assert(entry.next == nullptr);

        return true;
    }

    void nolock_list_move_front(list_value_type& entry) noexcept {
        assert(entry.bucketIndex < BucketCount);

        if (entry.bucketIndex < BucketCount)
        {
            const bool moved = buckets[entry.bucketIndex].entries.move_front(entry);

            assert(moved);
            (void) moved;
        }
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

        if (nolock_list_push_front(entry, newIndex))
            return true;

        const bool restored = nolock_list_push_front(entry, oldIndex);

        assert(restored);

        return false;
    }

    void nolock_promote_to_t2(list_value_type& entry) noexcept {
        assert(entry.bucketIndex == T1 || entry.bucketIndex == T2);
        assert(entry.value.has_value());

        if (entry.bucketIndex == T2)
        {
            if (entry.prev != nullptr)
                nolock_list_move_front(entry);

            return;
        }

        const bool moved = nolock_move_entry(entry, T2);

        assert(moved);
        (void) moved;
    }

    // Entry creation

    template<typename... Args>
    list_iterator
    nolock_add_resident(map_iterator mapItr, const usize index, Args&&... args) noexcept {
        assert(mapItr != map.end());
        assert(mapItr->second == nullptr);
        assert(index == T1 || index == T2);

        if (mapItr == map.end() || mapItr->second != nullptr || (index != T1 && index != T2))
            return nullptr;

        list_iterator const entry = pool.create(0, mapItr->first);

        if (entry == nullptr)
            return nullptr;

        entry->emplace_value(std::forward<Args>(args)...);

        if (!nolock_list_push_front(*entry, index))
        {
            entry->value.reset();
            pool.destroy(entry);

            return nullptr;
        }

        entry->mapItr  = mapItr;
        mapItr->second = entry;

        return entry;
    }

    // Ghost -> resident transition

    template<typename... Args>
    list_iterator nolock_ghost_to_resident(map_iterator mapItr, Args&&... args) noexcept {
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

        entry->emplace_value(std::forward<Args>(args)...);

        if (!nolock_move_entry(*entry, T2))
        {
            entry->value.reset();
            return nullptr;
        }

        return entry;
    }

    // Entry destruction

    void nolock_remove_resident(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());

        if (mapItr == map.end() || mapItr->second == nullptr || !nolock_is_resident(mapItr->second))
            return;

        list_iterator const entry = mapItr->second;

        assert(entry->value.has_value());

        if (!entry->value.has_value() || !nolock_list_remove(*entry))
            return;

        map.erase(mapItr);
        pool.destroy(entry);
    }

    void nolock_remove_resident(list_value_type& entry) noexcept {
        const map_iterator mapItr = entry.mapItr;

        assert(mapItr != map.end());

        if (mapItr != map.end() && mapItr->second == &entry)
            nolock_remove_resident(mapItr);
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

        map.erase(mapItr);
        pool.destroy(entry);

        return true;
    }

    [[nodiscard]] bool nolock_remove_ghost(list_value_type& entry) noexcept {
        const map_iterator mapItr = entry.mapItr;

        assert(mapItr != map.end());

        return mapItr != map.end() && mapItr->second == &entry && nolock_remove_ghost(mapItr);
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
        list_type& t1 = buckets[T1].entries;
        list_type& t2 = buckets[T2].entries;

        const bool evictT1 = !t1.empty() && (t1Size > target || (keyInB2 && t1Size == target));

        if (evictT1)
        {
            if (list_iterator const entry = t1.back())
            {
                if (nolock_evict_to_ghost(*entry, B1))
                    return true;
            }
        }

        if (list_iterator const entry = t2.back())
        {
            if (nolock_evict_to_ghost(*entry, B2))
                return true;
        }

        if (list_iterator const entry = t1.back())
            return nolock_evict_to_ghost(*entry, B1);

        return false;
    }

    // Ghost history management

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        usize ghostSize = nolock_ghost_size();

        while (ghostSize > maxSize)
        {
            list_iterator entry = buckets[B2].entries.back();

            if (entry == nullptr)
                entry = buckets[B1].entries.back();

            if (entry == nullptr || !nolock_remove_ghost(*entry))
                break;

            --ghostSize;
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

            usize delta = b1Size / b2Size;
            if (delta < 1)
                delta = 1;

            target = delta < target ? target - delta : usize{0};
        }
        else
        {
            if (b1Size == 0)
                return false;

            usize delta = b2Size / b1Size;
            if (delta < 1)
                delta = 1;

            target += std::min(delta, capacity - target);
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

        assert(t1Size <= capacity);
        assert(b1Size <= capacity);
        assert(nolock_ghost_size() <= capacity);

        if (t1Size == capacity - b1Size)
        {
            if (t1Size < capacity)
            {
                list_iterator const ghost = buckets[B1].entries.back();

                if (ghost == nullptr || !nolock_remove_ghost(*ghost))
                    return;

                (void) nolock_replace(false, t1Size);
            }
            else if (list_iterator const entry = buckets[T1].entries.back())
                nolock_remove_resident(*entry);

            return;
        }

        if (capacity <= std::numeric_limits<usize>::max() / 2 && map.size() >= capacity * 2)
        {
            list_iterator ghost = buckets[B2].entries.back();

            if (ghost == nullptr)
                ghost = buckets[B1].entries.back();

            if (ghost != nullptr)
                (void) nolock_remove_ghost(*ghost);
        }

        (void) nolock_replace(false, t1Size);
    }

    // Public-operation internals

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        auto mapItr = map.find(key);

        if (mapItr != map.end())
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

            if (!nolock_handle_ghost_hit(mapItr))
                return Return{std::forward<Args>(args)...};

            list_iterator const entry =
              nolock_ghost_to_resident(mapItr, std::forward<Args>(args)...);

            if (entry == nullptr)
                return Return{std::forward<Args>(args)...};

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{std::forward<Args>(args)...};

        auto [newItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return Return{std::forward<Args>(args)...};

        MapGuard mapGuard{map, newItr};

        list_iterator const entry = nolock_add_resident(newItr, T1, std::forward<Args>(args)...);

        if (entry == nullptr)
            return Return{std::forward<Args>(args)...};

        mapGuard.commit();

        assert(nolock_consistency_check());

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        auto mapItr = map.find(key);

        if (mapItr != map.end())
        {
            list_iterator const current = mapItr->second;

            if (current == nullptr)
                return Return{};

            if (nolock_is_resident(current))
            {
                nolock_promote_to_t2(*current);

                assert(nolock_consistency_check());

                return nolock_read(current);
            }

            if (!nolock_handle_ghost_hit(mapItr))
                return Return{};

            list_iterator const entry = nolock_ghost_to_resident(
              mapItr, std::forward<Builder>(builder)(std::forward<Args>(args)...));

            if (entry == nullptr)
                return Return{};

            assert(nolock_consistency_check());

            return nolock_read(entry);
        }

        nolock_prepare_miss();

        if (nolock_resident_size() >= capacity)
            return Return{};

        auto [newItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return Return{};

        MapGuard mapGuard{map, newItr};

        list_iterator const entry = nolock_add_resident(
          newItr, T1, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        if (entry == nullptr)
            return Return{};

        mapGuard.commit();

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

        nolock_remove_resident(mapItr);

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

            if (entry == nullptr)
                break;

            nolock_remove_resident(*entry);
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
                const bool erased = list.erase(*entry);

                assert(erased);

                if (!erased)
                    break;

                entry->bucketIndex = InvalidBucket;
                pool.destroy(entry);
            }
        }

        map.clear();
        target = 0;

        assert(nolock_consistency_check());
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        nolock_reserve_directory(capacity);
    }

#ifndef NDEBUG
    [[nodiscard]] bool nolock_consistency_check() const noexcept {
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

                assert(entry->mapItr == mapItr);

                if (entry->mapItr != mapItr)
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
            assert(entry->mapItr == mapItr);
            assert(entry->bucketIndex < BucketCount);

            if (entry->mapItr != mapItr || entry->bucketIndex >= BucketCount)
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
