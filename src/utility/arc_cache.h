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

    struct ResidentEntry;

    using resident_list_value_type = ResidentEntry;
    using resident_list_type       = DoublyIntrusiveList<resident_list_value_type>;
    using resident_list_iterator   = resident_list_value_type*;
    using resident_map_type        = std::unordered_map<Key, resident_list_iterator>;

    struct ResidentEntry final {
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit ResidentEntry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        Key                 key;
        storage_value_type  value;
        resident_list_type* list = nullptr;
        ResidentEntry*      prev = nullptr;
        ResidentEntry*      next = nullptr;
    };

    struct GhostEntry;

    using ghost_list_value_type = GhostEntry;
    using ghost_list_type       = DoublyIntrusiveList<ghost_list_value_type>;
    using ghost_list_iterator   = ghost_list_value_type*;
    using ghost_map_type        = std::unordered_map<Key, ghost_list_iterator>;

    struct GhostEntry final {
        explicit GhostEntry(const Key& k) noexcept :
            key(k) {}

        Key              key;
        ghost_list_type* list = nullptr;
        GhostEntry*      prev = nullptr;
        GhostEntry*      next = nullptr;
    };

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

        // Also trims ghosts and checks consistency.
        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return residentMap.find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return residentMap.find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return residentMap.find(key) != residentMap.end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_consistency_check();

        return residentMap.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_consistency_check();

        return residentMap.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    // Clears resident and ghost entries but retains pooled storage.
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();

        nolock_consistency_check();
    }

    // Releases pooled storage and restores the original configuration.
    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();

        nolock_release_pool();

        residentMap.rehash(0);

        ghostMap.rehash(0);

        capacity = defaultCapacity;

        target = 0;

        nolock_configure();

        nolock_consistency_check();
    }

   private:
    // ---------- List access ----------

    [[nodiscard]] resident_list_type& nolock_resident_list(const usize index) noexcept {
        return residentLists[index];
    }

    [[nodiscard]] const resident_list_type& nolock_resident_list(const usize index) const noexcept {
        return residentLists[index];
    }

    [[nodiscard]] ghost_list_type& nolock_ghost_list(const usize index) noexcept {
        return ghostLists[index];
    }

    [[nodiscard]] const ghost_list_type& nolock_ghost_list(const usize index) const noexcept {
        return ghostLists[index];
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return residentLists[0].size() + residentLists[1].size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return ghostLists[0].size() + ghostLists[1].size();
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

    // ---------- Resident list operations ----------

    void nolock_resident_list_push_front(ResidentEntry& entry, const usize index) noexcept {
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.list == nullptr);

        resident_list_type& list = nolock_resident_list(index);

        [[maybe_unused]]  //
        const bool inserted = list.push_front(entry);
        assert(inserted);

        if (inserted)
            entry.list = &list;
    }

    void nolock_resident_list_remove(ResidentEntry& entry) noexcept {
        assert(entry.list != nullptr);
        if (entry.list == nullptr)
            return;

        [[maybe_unused]]  //
        const bool erased = entry.list->erase(entry);
        assert(erased);

        if (erased)
            entry.list = nullptr;
    }

    void nolock_resident_list_move_front(ResidentEntry& entry) noexcept {
        assert(entry.list != nullptr);
        if (entry.list == nullptr)
            return;

        [[maybe_unused]]  //
        const bool moved = entry.list->move_front(entry);
        assert(moved);
    }

    void nolock_promote_to_t2(ResidentEntry& entry) noexcept {
        if (entry.list == &residentLists[1])
        {
            nolock_resident_list_move_front(entry);
            return;
        }

        assert(entry.list == &residentLists[0]);

        nolock_resident_list_remove(entry);
        nolock_resident_list_push_front(entry, 1);
    }

    // ---------- Ghost list operations ----------

    void nolock_ghost_list_push_front(GhostEntry& entry, const usize index) noexcept {
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.list == nullptr);

        ghost_list_type& list = nolock_ghost_list(index);

        [[maybe_unused]]  //
        const bool inserted = list.push_front(entry);
        assert(inserted);

        if (inserted)
            entry.list = &list;
    }

    void nolock_ghost_list_move_front(GhostEntry& entry) noexcept {
        assert(entry.list != nullptr);
        if (entry.list == nullptr)
            return;

        [[maybe_unused]]  //
        const bool moved = entry.list->move_front(entry);
        assert(moved);
    }

    void nolock_ghost_list_remove(GhostEntry& entry) noexcept {
        assert(entry.list != nullptr);
        if (entry.list == nullptr)
            return;

        [[maybe_unused]]  //
        const bool erased = entry.list->erase(entry);
        assert(erased);

        if (erased)
            entry.list = nullptr;
    }

    // ---------- Node creation ----------

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

        mapItr->second = entry;

        nolock_resident_list_push_front(*entry, index);

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

        mapItr->second = entry;

        nolock_ghost_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    // ---------- Node destruction ----------

    void nolock_remove_resident(typename resident_map_type::iterator mapItr) noexcept {
        assert(mapItr != residentMap.end());
        if (mapItr == residentMap.end())
            return;

        ResidentEntry* const entry = mapItr->second;
        assert(entry != nullptr);
        if (entry == nullptr)
            return;

        nolock_resident_list_remove(*entry);

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

        nolock_ghost_list_remove(*entry);

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

    // Move the LRU resident entry into B1 or B2.
    void nolock_evict_to_ghost(ResidentEntry& entry, const usize index) noexcept {
        GhostEntry* const ghost = nolock_add_ghost(entry.key, index);

        assert(ghost != nullptr);
        if (ghost == nullptr)
            return;

        nolock_remove_resident(entry);
    }

    // keyInB2 selects the tie-breaking rule from the ARC algorithm.
    void nolock_replace(const bool keyInB2) noexcept {
        resident_list_type& t1 = residentLists[0];
        resident_list_type& t2 = residentLists[1];

        const bool evictT1 =
          !t1.empty() && (t1.size() > target || (keyInB2 && t1.size() == target));

        if (evictT1)
        {
            ResidentEntry* const entry = t1.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*entry, 0);

            return;
        }

        if (!t2.empty())
        {
            ResidentEntry* const entry = t2.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*entry, 1);

            return;
        }

        // Defensive fallback for an empty T2.
        if (!t1.empty())
        {
            ResidentEntry* const entry = t1.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*entry, 0);
        }
    }

    // ---------- Ghost history management ----------

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        // Maintain T1 + B1 <= maxSize.
        while (residentLists[0].size() + ghostLists[0].size() > maxSize)
        {
            GhostEntry* const entry = ghostLists[0].back();
            assert(entry != nullptr);
            if (entry == nullptr)
                break;

            nolock_remove_ghost(*entry);
        }

        // Maintain T2 + B2 <= maxSize.
        while (residentLists[1].size() + ghostLists[1].size() > maxSize)
        {
            GhostEntry* const entry = ghostLists[1].back();
            assert(entry != nullptr);
            if (entry == nullptr)
                break;

            nolock_remove_ghost(*entry);
        }

        // Maintain the total ghost limit.
        while (ghostMap.size() > maxSize)
        {
            GhostEntry* entry = ghostLists[0].back();
            if (entry == nullptr)
                entry = ghostLists[1].back();
            assert(entry != nullptr);
            if (entry == nullptr)
                break;

            nolock_remove_ghost(*entry);
        }
    }

    // Adjust target and handle a hit in B1 or B2.
    void nolock_handle_ghost_hit(typename ghost_map_type::iterator mapItr) noexcept {
        assert(mapItr != ghostMap.end());
        if (mapItr == ghostMap.end())
            return;

        GhostEntry* const ghost = mapItr->second;
        assert(ghost != nullptr);
        if (ghost == nullptr)
            return;

        ghost_list_type* const sourceList = ghost->list;
        assert(sourceList == &ghostLists[0] || sourceList == &ghostLists[1]);
        if (sourceList != &ghostLists[0] && sourceList != &ghostLists[1])
            return;

        assert(target <= capacity);
        if (target > capacity)
            target = capacity;

        // Capture the original sizes before removing the ghost.
        const usize b1Size = ghostLists[0].size();
        const usize b2Size = ghostLists[1].size();

        const bool keyInB2 = sourceList == &ghostLists[1];

        if (keyInB2)
        {
            assert(b2Size != 0);
            if (b2Size == 0)
                return;

            usize delta = b1Size / b2Size;
            if (delta < 1)
                delta = 1;

            target = delta < target ? target - delta : usize{0};
        }
        else
        {
            assert(b1Size != 0);

            if (b1Size == 0)
                return;

            usize delta = b2Size / b1Size;
            if (delta < 1)
                delta = 1;

            target += std::min(delta, capacity - target);
        }

        // Remove before REPLACE can insert into ghostMap and rehash it.
        nolock_remove_ghost(mapItr);

        // Administrative removals can leave unused resident capacity.
        if (residentMap.size() >= capacity)
            nolock_replace(keyInB2);
    }

    // Prepare for a genuinely new key (not present in either map).
    void nolock_prepare_miss() noexcept {
        if (capacity == 0)
            return;

        const usize residentSize = residentMap.size();

        assert(residentSize <= capacity);

        // Administrative removals can leave the resident cache underfull.
        // The caller trims ghosts after adding the new resident.
        if (residentSize < capacity)
            return;

        const usize t1Size = residentLists[0].size();
        const usize b1Size = ghostLists[0].size();

        assert(t1Size <= capacity);
        assert(b1Size <= capacity);
        assert(ghostMap.size() <= capacity);

        if (t1Size > capacity || b1Size > capacity || ghostMap.size() > capacity)
            return;

        // Avoid overflow in T1 + B1.
        const usize remaining = capacity - b1Size;

        assert(t1Size <= remaining);

        if (t1Size > remaining)
            return;

        if (t1Size == remaining)
        {
            if (t1Size < capacity)
            {
                // |T1| + |B1| == capacity:
                // discard LRU(B1), then REPLACE.
                GhostEntry* const ghost = ghostLists[0].back();

                assert(ghost != nullptr);

                if (ghost == nullptr)
                    return;

                nolock_remove_ghost(*ghost);
                nolock_replace(false);
            }
            else
            {
                // |T1| == capacity: discard LRU(T1) without a ghost.
                ResidentEntry* const entry = residentLists[0].back();

                assert(entry != nullptr);

                if (entry == nullptr)
                    return;

                nolock_remove_resident(*entry);
            }

            return;
        }

        // Here, |T1| + |B1| < capacity.
        // The resident cache is full, so REPLACE is required.
        // At a total directory size of 2 * capacity, discard LRU(B2).
        if (ghostMap.size() == capacity)
        {
            GhostEntry* const ghost = ghostLists[1].back();

            assert(ghost != nullptr);

            if (ghost == nullptr)
                return;

            nolock_remove_ghost(*ghost);
        }

        nolock_replace(false);
    }

    // ---------- Public-operation internals ----------

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
        {
            nolock_consistency_check();
            return Return{std::forward<Args>(args)...};
        }

        if (const auto itr = residentMap.find(key); itr != residentMap.end())
        {
            ResidentEntry* const entry = itr->second;

            nolock_promote_to_t2(*entry);

            nolock_consistency_check();
            return nolock_read(entry);
        }

        if (const auto itr = ghostMap.find(key); itr != ghostMap.end())
        {
            nolock_handle_ghost_hit(itr);

            ResidentEntry* const entry = nolock_add_resident(key, 1, std::forward<Args>(args)...);

            nolock_consistency_check();
            return nolock_read(entry);
        }

        nolock_prepare_miss();

        ResidentEntry* const entry = nolock_add_resident(key, 0, std::forward<Args>(args)...);

        nolock_trim_ghosts(capacity);

        nolock_consistency_check();
        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
        {
            nolock_consistency_check();
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};
        }

        if (const auto itr = residentMap.find(key); itr != residentMap.end())
        {
            ResidentEntry* const entry = itr->second;

            nolock_promote_to_t2(*entry);

            nolock_consistency_check();
            return nolock_read(entry);
        }

        if (const auto itr = ghostMap.find(key); itr != ghostMap.end())
        {
            nolock_handle_ghost_hit(itr);

            ResidentEntry* const entry = nolock_add_resident(
              key, 1, std::forward<Builder>(builder)(std::forward<Args>(args)...));

            nolock_consistency_check();
            return nolock_read(entry);
        }

        nolock_prepare_miss();

        ResidentEntry* const entry =
          nolock_add_resident(key, 0, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        nolock_trim_ghosts(capacity);

        nolock_consistency_check();
        return nolock_read(entry);
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = residentMap.find(key);

        if (mapItr == residentMap.end())
        {
            nolock_consistency_check();
            return false;
        }

        // Reuse the iterator from find() instead of searching the map again.
        nolock_remove_resident(mapItr);

        nolock_consistency_check();
        return true;
    }

    // Administrative eviction: remove residents without creating ghosts.
    void nolock_evict_excess(const usize size) noexcept {
        while (residentMap.size() > size)
        {
            resident_list_type& t1 = residentLists[0];
            resident_list_type& t2 = residentLists[1];

            ResidentEntry* entry = nullptr;

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
        nolock_consistency_check();
    }

    void nolock_clear() noexcept {
        for (usize i = 0; i < 2; ++i)
        {
            while (ResidentEntry* const entry = residentLists[i].back())
            {
                [[maybe_unused]]  //
                const bool erased = residentLists[i].erase(*entry);
                assert(erased);

                entry->list = nullptr;
                residentPool.destroy(entry);
            }
        }

        for (usize i = 0; i < 2; ++i)
        {
            while (GhostEntry* const entry = ghostLists[i].back())
            {
                [[maybe_unused]]  //
                const bool erased = ghostLists[i].erase(*entry);
                assert(erased);

                entry->list = nullptr;
                ghostPool.destroy(entry);
            }
        }

        residentMap.clear();
        ghostMap.clear();

        target = 0;

        nolock_consistency_check();
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
    // Must be called only while the cache mutex is held and the cache
    // is in a stable state (not during an intermediate ARC transition).
    void nolock_consistency_check() const noexcept {
        const resident_list_type& t1 = residentLists[0];
        const resident_list_type& t2 = residentLists[1];
        const ghost_list_type&    b1 = ghostLists[0];
        const ghost_list_type&    b2 = ghostLists[1];

        const usize t1Size = t1.size();
        const usize t2Size = t2.size();
        const usize b1Size = b1.size();
        const usize b2Size = b2.size();

        // 1. Resident lists and resident map must agree.
        assert(t1Size <= std::numeric_limits<usize>::max() - t2Size);
        const usize residentSize = t1Size + t2Size;

        assert(residentSize == residentMap.size());

        // 2. Ghost lists and ghost map must agree.
        assert(b1Size <= std::numeric_limits<usize>::max() - b2Size);
        const usize ghostSize = b1Size + b2Size;

        assert(ghostSize == ghostMap.size());

        // 3. Resident entries must not exceed capacity.
        assert(residentSize <= capacity);

        // 4. Ghost entries must not exceed capacity.
        assert(ghostSize <= capacity);

        // 5. Total directory size must not exceed 2 * capacity.
        assert(residentSize <= std::numeric_limits<usize>::max() - ghostSize);

        const usize directorySize = residentSize + ghostSize;

        if (capacity <= std::numeric_limits<usize>::max() / 2)
            assert(directorySize <= 2 * capacity);

        // 6. ARC's target must remain within capacity.
        assert(target <= capacity);

        // Verify T1.
        for (const ResidentEntry* entry = t1.front(); entry != nullptr; entry = t1.next(*entry))
        {
            assert(entry->list == &residentLists[0]);

            const auto itr = residentMap.find(entry->key);
            assert(itr != residentMap.end());
            assert(itr->second == entry);

            assert(ghostMap.find(entry->key) == ghostMap.end());
        }

        // Verify T2.
        for (const ResidentEntry* entry = t2.front(); entry != nullptr; entry = t2.next(*entry))
        {
            assert(entry->list == &residentLists[1]);

            const auto itr = residentMap.find(entry->key);
            assert(itr != residentMap.end());
            assert(itr->second == entry);

            assert(ghostMap.find(entry->key) == ghostMap.end());
        }

        // Verify B1.
        for (const GhostEntry* ghost = b1.front(); ghost != nullptr; ghost = b1.next(*ghost))
        {
            assert(ghost->list == &ghostLists[0]);

            const auto itr = ghostMap.find(ghost->key);
            assert(itr != ghostMap.end());
            assert(itr->second == ghost);

            assert(residentMap.find(ghost->key) == residentMap.end());
        }

        // Verify B2.
        for (const GhostEntry* ghost = b2.front(); ghost != nullptr; ghost = b2.next(*ghost))
        {
            assert(ghost->list == &ghostLists[1]);

            const auto itr = ghostMap.find(ghost->key);
            assert(itr != ghostMap.end());
            assert(itr->second == ghost);

            assert(residentMap.find(ghost->key) == residentMap.end());
        }
    }
#else
    void nolock_consistency_check() const noexcept {}
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;
    usize         target = 0;

    resident_map_type residentMap;
    ghost_map_type    ghostMap;

    Pool<ResidentEntry> residentPool;
    Pool<GhostEntry>    ghostPool;

    Array<resident_list_type, 2> residentLists;
    Array<ghost_list_type, 2>    ghostLists;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatARCCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct ResidentEntry;

    using resident_list_value_type = ResidentEntry;
    using resident_list_type       = DoublyIntrusiveList<resident_list_value_type>;
    using resident_list_iterator   = resident_list_value_type*;
    using resident_map_type        = ankerl::unordered_dense::map<Key, resident_list_iterator>;

    struct ResidentEntry final {
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit ResidentEntry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        Key                 key;
        storage_value_type  value;
        resident_list_type* list = nullptr;
        ResidentEntry*      prev = nullptr;
        ResidentEntry*      next = nullptr;
    };

    struct GhostEntry;

    using ghost_list_value_type = GhostEntry;
    using ghost_list_type       = DoublyIntrusiveList<ghost_list_value_type>;
    using ghost_list_iterator   = ghost_list_value_type*;
    using ghost_map_type        = ankerl::unordered_dense::map<Key, ghost_list_iterator>;

    struct GhostEntry final {
        explicit GhostEntry(const Key& k) noexcept :
            key(k) {}

        Key              key;
        ghost_list_type* list = nullptr;
        GhostEntry*      prev = nullptr;
        GhostEntry*      next = nullptr;
    };

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

        // Also trims ghosts and checks consistency.
        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock.
    // Callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return residentMap.find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return residentMap.find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return residentMap.find(key) != residentMap.end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_consistency_check();

        return residentMap.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_consistency_check();

        return residentMap.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    // Clears resident and ghost entries but retains pooled storage.
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();

        nolock_consistency_check();
    }

    // Releases pooled storage and restores the original configuration.
    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();

        nolock_release_pool();

        residentMap.rehash(0);

        ghostMap.rehash(0);

        capacity = defaultCapacity;

        target = 0;

        nolock_configure();

        nolock_consistency_check();
    }

   private:
    // ---------- List access ----------

    [[nodiscard]] resident_list_type& nolock_resident_list(const usize index) noexcept {
        return residentLists[index];
    }

    [[nodiscard]] const resident_list_type& nolock_resident_list(const usize index) const noexcept {
        return residentLists[index];
    }

    [[nodiscard]] ghost_list_type& nolock_ghost_list(const usize index) noexcept {
        return ghostLists[index];
    }

    [[nodiscard]] const ghost_list_type& nolock_ghost_list(const usize index) const noexcept {
        return ghostLists[index];
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept {
        return residentLists[0].size() + residentLists[1].size();
    }

    [[nodiscard]] usize nolock_ghost_size() const noexcept {
        return ghostLists[0].size() + ghostLists[1].size();
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

    // ---------- Resident list operations ----------

    void nolock_resident_list_push_front(ResidentEntry& entry, const usize index) noexcept {
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.list == nullptr);

        resident_list_type& list = nolock_resident_list(index);

        [[maybe_unused]]  //
        const bool inserted = list.push_front(entry);
        assert(inserted);

        if (inserted)
            entry.list = &list;
    }

    void nolock_resident_list_remove(ResidentEntry& entry) noexcept {
        assert(entry.list != nullptr);
        if (entry.list == nullptr)
            return;

        [[maybe_unused]]  //
        const bool erased = entry.list->erase(entry);
        assert(erased);

        if (erased)
            entry.list = nullptr;
    }

    void nolock_resident_list_move_front(ResidentEntry& entry) noexcept {
        assert(entry.list != nullptr);
        if (entry.list == nullptr)
            return;

        [[maybe_unused]]  //
        const bool moved = entry.list->move_front(entry);
        assert(moved);
    }

    void nolock_promote_to_t2(ResidentEntry& entry) noexcept {
        if (entry.list == &residentLists[1])
        {
            nolock_resident_list_move_front(entry);
            return;
        }

        assert(entry.list == &residentLists[0]);

        nolock_resident_list_remove(entry);
        nolock_resident_list_push_front(entry, 1);
    }

    // ---------- Ghost list operations ----------

    void nolock_ghost_list_push_front(GhostEntry& entry, const usize index) noexcept {
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.list == nullptr);

        ghost_list_type& list = nolock_ghost_list(index);

        [[maybe_unused]]  //
        const bool inserted = list.push_front(entry);
        assert(inserted);

        if (inserted)
            entry.list = &list;
    }

    void nolock_ghost_list_move_front(GhostEntry& entry) noexcept {
        assert(entry.list != nullptr);
        if (entry.list == nullptr)
            return;

        [[maybe_unused]]  //
        const bool moved = entry.list->move_front(entry);
        assert(moved);
    }

    void nolock_ghost_list_remove(GhostEntry& entry) noexcept {
        assert(entry.list != nullptr);
        if (entry.list == nullptr)
            return;

        [[maybe_unused]]  //
        const bool erased = entry.list->erase(entry);
        assert(erased);

        if (erased)
            entry.list = nullptr;
    }

    // ---------- Node creation ----------

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

        mapItr->second = entry;

        nolock_resident_list_push_front(*entry, index);

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

        mapItr->second = entry;

        nolock_ghost_list_push_front(*entry, index);

        mapGuard.commit();

        return entry;
    }

    // ---------- Node destruction ----------

    void nolock_remove_resident(typename resident_map_type::iterator mapItr) noexcept {
        assert(mapItr != residentMap.end());
        if (mapItr == residentMap.end())
            return;

        ResidentEntry* const entry = mapItr->second;
        assert(entry != nullptr);
        if (entry == nullptr)
            return;

        nolock_resident_list_remove(*entry);

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

        nolock_ghost_list_remove(*entry);

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

    // Move the LRU resident entry into B1 or B2.
    void nolock_evict_to_ghost(ResidentEntry& entry, const usize index) noexcept {
        GhostEntry* const ghost = nolock_add_ghost(entry.key, index);

        assert(ghost != nullptr);
        if (ghost == nullptr)
            return;

        nolock_remove_resident(entry);
    }

    // keyInB2 selects the tie-breaking rule from the ARC algorithm.
    void nolock_replace(const bool keyInB2) noexcept {
        resident_list_type& t1 = residentLists[0];
        resident_list_type& t2 = residentLists[1];

        const bool evictT1 =
          !t1.empty() && (t1.size() > target || (keyInB2 && t1.size() == target));

        if (evictT1)
        {
            ResidentEntry* const entry = t1.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*entry, 0);

            return;
        }

        if (!t2.empty())
        {
            ResidentEntry* const entry = t2.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*entry, 1);

            return;
        }

        // Defensive fallback for an empty T2.
        if (!t1.empty())
        {
            ResidentEntry* const entry = t1.back();
            assert(entry != nullptr);

            if (entry != nullptr)
                nolock_evict_to_ghost(*entry, 0);
        }
    }

    // ---------- Ghost history management ----------

    void nolock_trim_ghosts(const usize maxSize) noexcept {
        // Maintain T1 + B1 <= maxSize.
        while (residentLists[0].size() + ghostLists[0].size() > maxSize)
        {
            GhostEntry* const entry = ghostLists[0].back();
            assert(entry != nullptr);
            if (entry == nullptr)
                break;

            nolock_remove_ghost(*entry);
        }

        // Maintain T2 + B2 <= maxSize.
        while (residentLists[1].size() + ghostLists[1].size() > maxSize)
        {
            GhostEntry* const entry = ghostLists[1].back();
            assert(entry != nullptr);
            if (entry == nullptr)
                break;

            nolock_remove_ghost(*entry);
        }

        // Maintain the total ghost limit.
        while (ghostMap.size() > maxSize)
        {
            GhostEntry* entry = ghostLists[0].back();
            if (entry == nullptr)
                entry = ghostLists[1].back();
            assert(entry != nullptr);
            if (entry == nullptr)
                break;

            nolock_remove_ghost(*entry);
        }
    }

    // Adjust target and handle a hit in B1 or B2.
    void nolock_handle_ghost_hit(typename ghost_map_type::iterator mapItr) noexcept {
        assert(mapItr != ghostMap.end());
        if (mapItr == ghostMap.end())
            return;

        GhostEntry* const ghost = mapItr->second;
        assert(ghost != nullptr);
        if (ghost == nullptr)
            return;

        ghost_list_type* const sourceList = ghost->list;
        assert(sourceList == &ghostLists[0] || sourceList == &ghostLists[1]);
        if (sourceList != &ghostLists[0] && sourceList != &ghostLists[1])
            return;

        assert(target <= capacity);
        if (target > capacity)
            target = capacity;

        // Capture the original sizes before removing the ghost.
        const usize b1Size = ghostLists[0].size();
        const usize b2Size = ghostLists[1].size();

        const bool keyInB2 = sourceList == &ghostLists[1];

        if (keyInB2)
        {
            assert(b2Size != 0);
            if (b2Size == 0)
                return;

            usize delta = b1Size / b2Size;
            if (delta < 1)
                delta = 1;

            target = delta < target ? target - delta : usize{0};
        }
        else
        {
            assert(b1Size != 0);

            if (b1Size == 0)
                return;

            usize delta = b2Size / b1Size;
            if (delta < 1)
                delta = 1;

            target += std::min(delta, capacity - target);
        }

        // Remove before REPLACE can insert into ghostMap and rehash it.
        nolock_remove_ghost(mapItr);

        // Administrative removals can leave unused resident capacity.
        if (residentMap.size() >= capacity)
            nolock_replace(keyInB2);
    }

    // Prepare for a genuinely new key (not present in either map).
    void nolock_prepare_miss() noexcept {
        if (capacity == 0)
            return;

        const usize residentSize = residentMap.size();

        assert(residentSize <= capacity);

        // Administrative removals can leave the resident cache underfull.
        // The caller trims ghosts after adding the new resident.
        if (residentSize < capacity)
            return;

        const usize t1Size = residentLists[0].size();
        const usize b1Size = ghostLists[0].size();

        assert(t1Size <= capacity);
        assert(b1Size <= capacity);
        assert(ghostMap.size() <= capacity);

        if (t1Size > capacity || b1Size > capacity || ghostMap.size() > capacity)
            return;

        // Avoid overflow in T1 + B1.
        const usize remaining = capacity - b1Size;

        assert(t1Size <= remaining);

        if (t1Size > remaining)
            return;

        if (t1Size == remaining)
        {
            if (t1Size < capacity)
            {
                // |T1| + |B1| == capacity:
                // discard LRU(B1), then REPLACE.
                GhostEntry* const ghost = ghostLists[0].back();

                assert(ghost != nullptr);

                if (ghost == nullptr)
                    return;

                nolock_remove_ghost(*ghost);
                nolock_replace(false);
            }
            else
            {
                // |T1| == capacity: discard LRU(T1) without a ghost.
                ResidentEntry* const entry = residentLists[0].back();

                assert(entry != nullptr);

                if (entry == nullptr)
                    return;

                nolock_remove_resident(*entry);
            }

            return;
        }

        // Here, |T1| + |B1| < capacity.
        // The resident cache is full, so REPLACE is required.
        // At a total directory size of 2 * capacity, discard LRU(B2).
        if (ghostMap.size() == capacity)
        {
            GhostEntry* const ghost = ghostLists[1].back();

            assert(ghost != nullptr);

            if (ghost == nullptr)
                return;

            nolock_remove_ghost(*ghost);
        }

        nolock_replace(false);
    }

    // ---------- Public-operation internals ----------

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
        {
            nolock_consistency_check();
            return Return{std::forward<Args>(args)...};
        }

        if (const auto itr = residentMap.find(key); itr != residentMap.end())
        {
            ResidentEntry* const entry = itr->second;

            nolock_promote_to_t2(*entry);

            nolock_consistency_check();
            return nolock_read(entry);
        }

        if (const auto itr = ghostMap.find(key); itr != ghostMap.end())
        {
            nolock_handle_ghost_hit(itr);

            ResidentEntry* const entry = nolock_add_resident(key, 1, std::forward<Args>(args)...);

            nolock_consistency_check();
            return nolock_read(entry);
        }

        nolock_prepare_miss();

        ResidentEntry* const entry = nolock_add_resident(key, 0, std::forward<Args>(args)...);

        nolock_trim_ghosts(capacity);

        nolock_consistency_check();
        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
        {
            nolock_consistency_check();
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};
        }

        if (const auto itr = residentMap.find(key); itr != residentMap.end())
        {
            ResidentEntry* const entry = itr->second;

            nolock_promote_to_t2(*entry);

            nolock_consistency_check();
            return nolock_read(entry);
        }

        if (const auto itr = ghostMap.find(key); itr != ghostMap.end())
        {
            nolock_handle_ghost_hit(itr);

            ResidentEntry* const entry = nolock_add_resident(
              key, 1, std::forward<Builder>(builder)(std::forward<Args>(args)...));

            nolock_consistency_check();
            return nolock_read(entry);
        }

        nolock_prepare_miss();

        ResidentEntry* const entry =
          nolock_add_resident(key, 0, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        nolock_trim_ghosts(capacity);

        nolock_consistency_check();
        return nolock_read(entry);
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = residentMap.find(key);

        if (mapItr == residentMap.end())
        {
            nolock_consistency_check();
            return false;
        }

        // Reuse the iterator from find() instead of searching the map again.
        nolock_remove_resident(mapItr);

        nolock_consistency_check();
        return true;
    }

    // Administrative eviction: remove residents without creating ghosts.
    void nolock_evict_excess(const usize size) noexcept {
        while (residentMap.size() > size)
        {
            resident_list_type& t1 = residentLists[0];
            resident_list_type& t2 = residentLists[1];

            ResidentEntry* entry = nullptr;

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
        nolock_consistency_check();
    }

    void nolock_clear() noexcept {
        for (usize i = 0; i < 2; ++i)
        {
            while (ResidentEntry* const entry = residentLists[i].back())
            {
                [[maybe_unused]]  //
                const bool erased = residentLists[i].erase(*entry);
                assert(erased);

                entry->list = nullptr;
                residentPool.destroy(entry);
            }
        }

        for (usize i = 0; i < 2; ++i)
        {
            while (GhostEntry* const entry = ghostLists[i].back())
            {
                [[maybe_unused]]  //
                const bool erased = ghostLists[i].erase(*entry);
                assert(erased);

                entry->list = nullptr;
                ghostPool.destroy(entry);
            }
        }

        residentMap.clear();
        ghostMap.clear();

        target = 0;

        nolock_consistency_check();
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
    // Must be called only while the cache mutex is held and the cache
    // is in a stable state (not during an intermediate ARC transition).
    void nolock_consistency_check() const noexcept {
        const resident_list_type& t1 = residentLists[0];
        const resident_list_type& t2 = residentLists[1];
        const ghost_list_type&    b1 = ghostLists[0];
        const ghost_list_type&    b2 = ghostLists[1];

        const usize t1Size = t1.size();
        const usize t2Size = t2.size();
        const usize b1Size = b1.size();
        const usize b2Size = b2.size();

        // 1. Resident lists and resident map must agree.
        assert(t1Size <= std::numeric_limits<usize>::max() - t2Size);
        const usize residentSize = t1Size + t2Size;

        assert(residentSize == residentMap.size());

        // 2. Ghost lists and ghost map must agree.
        assert(b1Size <= std::numeric_limits<usize>::max() - b2Size);
        const usize ghostSize = b1Size + b2Size;

        assert(ghostSize == ghostMap.size());

        // 3. Resident entries must not exceed capacity.
        assert(residentSize <= capacity);

        // 4. Ghost entries must not exceed capacity.
        assert(ghostSize <= capacity);

        // 5. Total directory size must not exceed 2 * capacity.
        assert(residentSize <= std::numeric_limits<usize>::max() - ghostSize);

        const usize directorySize = residentSize + ghostSize;

        if (capacity <= std::numeric_limits<usize>::max() / 2)
            assert(directorySize <= 2 * capacity);

        // 6. ARC's target must remain within capacity.
        assert(target <= capacity);

        // Verify T1.
        for (const ResidentEntry* entry = t1.front(); entry != nullptr; entry = t1.next(*entry))
        {
            assert(entry->list == &residentLists[0]);

            const auto itr = residentMap.find(entry->key);
            assert(itr != residentMap.end());
            assert(itr->second == entry);

            assert(ghostMap.find(entry->key) == ghostMap.end());
        }

        // Verify T2.
        for (const ResidentEntry* entry = t2.front(); entry != nullptr; entry = t2.next(*entry))
        {
            assert(entry->list == &residentLists[1]);

            const auto itr = residentMap.find(entry->key);
            assert(itr != residentMap.end());
            assert(itr->second == entry);

            assert(ghostMap.find(entry->key) == ghostMap.end());
        }

        // Verify B1.
        for (const GhostEntry* ghost = b1.front(); ghost != nullptr; ghost = b1.next(*ghost))
        {
            assert(ghost->list == &ghostLists[0]);

            const auto itr = ghostMap.find(ghost->key);
            assert(itr != ghostMap.end());
            assert(itr->second == ghost);

            assert(residentMap.find(ghost->key) == residentMap.end());
        }

        // Verify B2.
        for (const GhostEntry* ghost = b2.front(); ghost != nullptr; ghost = b2.next(*ghost))
        {
            assert(ghost->list == &ghostLists[1]);

            const auto itr = ghostMap.find(ghost->key);
            assert(itr != ghostMap.end());
            assert(itr->second == ghost);

            assert(residentMap.find(ghost->key) == residentMap.end());
        }
    }
#else
    void nolock_consistency_check() const noexcept {}
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;
    usize         target = 0;

    resident_map_type residentMap;
    ghost_map_type    ghostMap;

    Pool<ResidentEntry> residentPool;
    Pool<GhostEntry>    ghostPool;

    Array<resident_list_type, 2> residentLists;
    Array<ghost_list_type, 2>    ghostLists;
};

}  // namespace DON

#endif  // UTILITY_ARC_CACHE_H_INCLUDED
