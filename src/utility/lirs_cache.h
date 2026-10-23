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

#ifndef UTILITY_LIRS_CACHE_H_INCLUDED
#define UTILITY_LIRS_CACHE_H_INCLUDED

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
class PoolLIRSCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry;

    struct Link final {
        explicit Link(Entry* ownerPtr) noexcept :
            owner(ownerPtr) {}

        Entry* owner = nullptr;
        Link*  prev  = nullptr;
        Link*  next  = nullptr;
    };

    using list_type     = DoublyIntrusiveList<Link>;
    using list_iterator = Entry*;
    using map_type      = std::unordered_map<Key, list_iterator>;
    using map_iterator  = typename map_type::iterator;

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

        Key                               key;
        bool                              is_lir   = false;
        bool                              in_stack = false;
        bool                              in_queue = false;
        Link                              s_link{this};
        Link                              q_link{this};
        std::optional<storage_value_type> value;
    };

   public:
    explicit PoolLIRSCache(const usize cap        = 2 * KB,
                           const usize reserveCnt = 1 * KB,
                           const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    PoolLIRSCache(const PoolLIRSCache&)            = delete;
    PoolLIRSCache& operator=(const PoolLIRSCache&) = delete;
    PoolLIRSCache(PoolLIRSCache&&)                 = delete;
    PoolLIRSCache& operator=(PoolLIRSCache&&)      = delete;

    ~PoolLIRSCache() noexcept {
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

        capacity = newCapacity;
        nolock_reserve_map(newCapacity);
        nolock_clamp_lir_size();
        while (queue_q.size() > nolock_hir_capacity())
            (void) nolock_evict_oldest_hir();
        nolock_evict_excess(capacity);
        nolock_stack_prune();
        nolock_trim_stack_limit();
        assert(nolock_consistency_check());
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

    [[nodiscard]] bool contains_ghost(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr
            && !nolock_is_resident(mapItr->second);
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
        lirSize  = 0;

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    [[nodiscard]] usize nolock_hir_capacity() const noexcept {
        if (capacity == 0)
            return 0;

        // Round 5% upward so every non-empty cache can admit at least one HIR.
        const usize value = capacity / 20 + (capacity % 20 != 0);
        return std::min(capacity, std::max(usize{1}, value));
    }

    [[nodiscard]] usize nolock_lir_capacity() const noexcept {
        return capacity - nolock_hir_capacity();
    }

    [[nodiscard]] usize nolock_map_reserve_size(const usize requestedCapacity) const noexcept {
        constexpr usize maxSize = std::numeric_limits<usize>::max();
        const usize     requested =
          requestedCapacity > (maxSize - 1) / 2 ? maxSize : 2 * requestedCapacity + 1;
        return std::max(reserveCount, requested);
    }

    void nolock_reserve_map(const usize requestedCapacity) noexcept {
        map.reserve(nolock_map_reserve_size(requestedCapacity));
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        nolock_reserve_map(capacity);
    }

    [[nodiscard]] bool nolock_is_resident(const Entry* entry) const noexcept {
        return entry != nullptr && entry->value.has_value();
    }

    [[nodiscard]] usize nolock_resident_size() const noexcept { return lirSize + queue_q.size(); }

    [[nodiscard]] Return nolock_read(const Entry* entry) const noexcept {
        assert(nolock_is_resident(entry));
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    void nolock_stack_push_front(Entry& entry) noexcept {
        assert(!entry.in_stack);
        [[maybe_unused]] const bool inserted = stack_s.push_front(entry.s_link);
        assert(inserted);
        entry.in_stack = true;
    }

    void nolock_stack_move_front(Entry& entry) noexcept {
        if (!entry.in_stack)
            nolock_stack_push_front(entry);
        else
        {
            [[maybe_unused]] const bool moved = stack_s.move_front(entry.s_link);
            assert(moved);
        }
    }

    void nolock_stack_remove(Entry& entry) noexcept {
        if (!entry.in_stack)
            return;
        [[maybe_unused]] const bool erased = stack_s.erase(entry.s_link);
        assert(erased);
        entry.in_stack = false;
    }

    void nolock_queue_push_back(Entry& entry) noexcept {
        assert(!entry.in_queue);
        [[maybe_unused]] const bool inserted = queue_q.push_back(entry.q_link);
        assert(inserted);
        entry.in_queue = true;
    }

    void nolock_queue_move_back(Entry& entry) noexcept {
        if (!entry.in_queue)
            nolock_queue_push_back(entry);
        else
        {
            [[maybe_unused]] const bool moved = queue_q.move_back(entry.q_link);
            assert(moved);
        }
    }

    void nolock_queue_remove(Entry& entry) noexcept {
        if (!entry.in_queue)
            return;
        [[maybe_unused]] const bool erased = queue_q.erase(entry.q_link);
        assert(erased);
        entry.in_queue = false;
    }

    [[nodiscard]] bool nolock_map_erase(Entry& entry) noexcept {
        const auto mapItr = map.find(entry.key);
        if (mapItr == map.end() || mapItr->second != &entry)
            return false;
        map.erase(mapItr);
        return true;
    }

    void nolock_destroy_entry(Entry& entry) noexcept {
        nolock_stack_remove(entry);
        nolock_queue_remove(entry);
        [[maybe_unused]] const bool erased = nolock_map_erase(entry);
        assert(erased);
        pool.destroy(&entry);
    }

    void nolock_stack_prune() noexcept {
        while (Link* tail = stack_s.back())
        {
            Entry* const entry = tail->owner;
            if (entry->is_lir)
                break;

            nolock_stack_remove(*entry);
            if (!nolock_is_resident(entry))
                nolock_destroy_entry(*entry);
        }
    }

    // Bound metadata during long scans while retaining the newest HIR history.
    void nolock_trim_stack_limit() noexcept {
        const usize maxSize = capacity > std::numeric_limits<usize>::max() / 2
                              ? std::numeric_limits<usize>::max()
                              : 2 * capacity;

        while (stack_s.size() > maxSize)
        {
            Link* candidate = nullptr;
            for (Link* link = stack_s.back(); link != nullptr; link = link->prev)
            {
                if (!link->owner->is_lir)
                {
                    candidate = link;
                    break;
                }
            }
            if (candidate == nullptr)
                break;

            Entry* const entry = candidate->owner;
            nolock_stack_remove(*entry);
            if (!nolock_is_resident(entry))
                nolock_destroy_entry(*entry);
        }
    }

    [[nodiscard]] Entry* nolock_bottom_lir(Entry* except = nullptr) noexcept {
        for (Link* link = stack_s.back(); link != nullptr; link = link->prev)
        {
            Entry* const entry = link->owner;
            if (entry != except && entry->is_lir)
                return entry;
        }
        return nullptr;
    }

    void nolock_demote_bottom_lir(Entry* except = nullptr) noexcept {
        Entry* const entry = nolock_bottom_lir(except);
        if (entry == nullptr)
            return;

        entry->is_lir = false;
        assert(lirSize != 0);
        --lirSize;
        nolock_queue_push_back(*entry);
        nolock_stack_prune();
    }

    // Q is oldest-first at front. An evicted HIR remains as a nonresident
    // ghost only while it is still represented in Stack S.
    [[nodiscard]] bool nolock_evict_oldest_hir() noexcept {
        Link* const front = queue_q.front();
        if (front == nullptr)
            return false;

        Entry* const entry = front->owner;
        nolock_queue_remove(*entry);
        entry->value.reset();

        if (!entry->in_stack)
            nolock_destroy_entry(*entry);
        else
            nolock_stack_prune();
        return true;
    }

    [[nodiscard]] bool nolock_remove_bottom_lir() noexcept {
        Entry* const entry = nolock_bottom_lir();
        if (entry == nullptr)
            return false;
        assert(lirSize != 0);
        --lirSize;
        nolock_destroy_entry(*entry);
        nolock_stack_prune();
        return true;
    }

    [[nodiscard]] bool nolock_make_room() noexcept {
        if (capacity == 0)
            return false;
        if (nolock_resident_size() < capacity)
            return true;
        if (nolock_evict_oldest_hir())
            return nolock_resident_size() < capacity;
        if (nolock_remove_bottom_lir())
            return nolock_resident_size() < capacity;
        return false;
    }

    void nolock_touch_lir(Entry& entry) noexcept {
        nolock_stack_move_front(entry);
        nolock_stack_prune();
        nolock_trim_stack_limit();
    }

    void nolock_touch_hir(Entry& entry) noexcept {
        const bool wasInStack = entry.in_stack;

        if (wasInStack && nolock_lir_capacity() != 0)
        {
            nolock_queue_remove(entry);
            entry.is_lir = true;
            ++lirSize;
            nolock_stack_move_front(entry);
            if (lirSize > nolock_lir_capacity())
                nolock_demote_bottom_lir(&entry);
        }
        else
        {
            nolock_queue_move_back(entry);
            nolock_stack_move_front(entry);
        }

        nolock_stack_prune();
        nolock_trim_stack_limit();
    }

    void nolock_reactivate_hir(Entry& entry, const bool wasInStack) noexcept {
        entry.is_lir = wasInStack && nolock_lir_capacity() != 0;
        if (entry.is_lir)
        {
            ++lirSize;
            nolock_stack_move_front(entry);
            if (lirSize > nolock_lir_capacity())
                nolock_demote_bottom_lir(&entry);
        }
        else
        {
            nolock_stack_move_front(entry);
            nolock_queue_push_back(entry);
        }

        nolock_stack_prune();
        nolock_trim_stack_limit();
    }

    void nolock_admit_new(Entry& entry) noexcept {
        if (lirSize < nolock_lir_capacity())
        {
            entry.is_lir = true;
            ++lirSize;
            nolock_stack_push_front(entry);
        }
        else
        {
            entry.is_lir = false;
            nolock_stack_push_front(entry);
            nolock_queue_push_back(entry);
        }

        nolock_stack_prune();
        nolock_trim_stack_limit();
    }

    void nolock_clamp_lir_size() noexcept {
        const usize limit = nolock_lir_capacity();
        while (lirSize > limit)
        {
            const usize before = lirSize;
            nolock_demote_bottom_lir();
            if (lirSize == before)
                break;
        }
    }

    void nolock_evict_excess(const usize targetSize) noexcept {
        while (nolock_resident_size() > targetSize)
        {
            if (!nolock_evict_oldest_hir() && !nolock_remove_bottom_lir())
                break;
        }
        nolock_stack_prune();
        nolock_trim_stack_limit();
        assert(nolock_consistency_check());
    }

    [[nodiscard]] bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = map.find(key);
        if (mapItr == map.end() || mapItr->second == nullptr)
            return false;

        Entry* const entry = mapItr->second;
        if (entry->is_lir)
        {
            assert(lirSize != 0);
            --lirSize;
        }
        nolock_destroy_entry(*entry);
        nolock_stack_prune();
        nolock_trim_stack_limit();
        assert(nolock_consistency_check());
        return true;
    }

    void nolock_clear() noexcept {
        for (auto& [key, entry] : map)
        {
            (void) key;
            if (entry == nullptr)
                continue;
            nolock_stack_remove(*entry);
            nolock_queue_remove(*entry);
            pool.destroy(entry);
        }
        map.clear();
        lirSize = 0;
    }

    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        usize lirCount = 0;

        for (const Link* link = stack_s.front(); link != nullptr; link = stack_s.next(*link))
        {
            const Entry* const entry = link->owner;
            if (entry == nullptr || !entry->in_stack || &entry->s_link != link)
                return false;
            if (entry->is_lir)
                ++lirCount;
        }

        usize queueCount = 0;
        for (const Link* link = queue_q.front(); link != nullptr; link = queue_q.next(*link))
        {
            const Entry* const entry = link->owner;
            if (entry == nullptr || !entry->in_queue || &entry->q_link != link || entry->is_lir
                || !nolock_is_resident(entry))
                return false;
            ++queueCount;
        }

        if (lirCount != lirSize || queueCount != queue_q.size() || lirSize > nolock_lir_capacity()
            || queueCount > nolock_hir_capacity() || nolock_resident_size() > capacity)
            return false;
        if (lirSize != 0 && (stack_s.back() == nullptr || !stack_s.back()->owner->is_lir))
            return false;

        for (const auto& [key, entry] : map)
        {
            (void) key;
            if (entry == nullptr)
                return false;
            if (entry->is_lir && (!entry->in_stack || entry->in_queue || !entry->value))
                return false;
            if (entry->in_queue && (entry->is_lir || !entry->value))
                return false;
            if (entry->value && !entry->is_lir && !entry->in_queue)
                return false;
            if (!entry->value && (entry->is_lir || entry->in_queue || !entry->in_stack))
                return false;
        }
        return true;
    }

    template<typename... Args>
    Return nolock_insert_new(const Key& key, Args&&... args) noexcept {
        if (!nolock_make_room())
            return Return{std::forward<Args>(args)...};

        Entry* const entry = pool.create(0, key);
        entry->emplace_value(std::forward<Args>(args)...);
        const auto [mapItr, inserted] = map.try_emplace(key, entry);
        (void) mapItr;
        assert(inserted);
        nolock_admit_new(*entry);
        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    template<typename... Args>
    Return nolock_reactivate(const Key& key, map_iterator mapItr, Args&&... args) noexcept {
        if (!nolock_make_room())
            return Return{std::forward<Args>(args)...};

        mapItr = map.find(key);
        if (mapItr == map.end() || mapItr->second == nullptr)
            return nolock_insert_new(key, std::forward<Args>(args)...);

        Entry* const entry = mapItr->second;
        if (nolock_is_resident(entry))
        {
            if (entry->is_lir)
                nolock_touch_lir(*entry);
            else
                nolock_touch_hir(*entry);
            return nolock_read(entry);
        }

        const bool wasInStack = entry->in_stack;
        entry->emplace_value(std::forward<Args>(args)...);
        nolock_reactivate_hir(*entry, wasInStack);
        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto mapItr = map.find(key);
        if (mapItr != map.end() && mapItr->second != nullptr)
        {
            Entry* const entry = mapItr->second;
            if (nolock_is_resident(entry))
            {
                if (entry->is_lir)
                    nolock_touch_lir(*entry);
                else
                    nolock_touch_hir(*entry);
                assert(nolock_consistency_check());
                return nolock_read(entry);
            }
            return nolock_reactivate(key, mapItr, std::forward<Args>(args)...);
        }
        return nolock_insert_new(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto mapItr = map.find(key);
        if (mapItr != map.end() && mapItr->second != nullptr && nolock_is_resident(mapItr->second))
        {
            Entry* const entry = mapItr->second;
            if (entry->is_lir)
                nolock_touch_lir(*entry);
            else
                nolock_touch_hir(*entry);
            assert(nolock_consistency_check());
            return nolock_read(entry);
        }

        Value value = std::forward<Builder>(builder)(std::forward<Args>(args)...);
        if (mapItr != map.end() && mapItr->second != nullptr)
            return nolock_reactivate(key, map.find(key), std::move(value));
        return nolock_insert_new(key, std::move(value));
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;
    usize         lirSize = 0;

    map_type    map;
    Pool<Entry> pool;
    list_type   stack_s;
    list_type   queue_q;
};

}  // namespace DON

#endif  // UTILITY_LIRS_CACHE_H_INCLUDED
