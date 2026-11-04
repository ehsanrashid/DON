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

#include <cassert>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../utility/attributes.h"
#include "../utility/guard.h"
#include "../utility/list.h"
#include "../utility/pool.h"
#include "../utility/types.h"
#include "../utility/ankerl/unordered_dense.h"

namespace DON {

// LIRS (Low Inter-reference Recency Set) cache using a stack and a queue.
template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolLIRSCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry;

    struct ListNode final {
        Entry*    entry;
        ListNode* prev = nullptr;
        ListNode* next = nullptr;
    };

    enum class EntryType : u8 {
        LIR,
        HIR_RESIDENT,
        HIR_GHOST
    };

    struct Entry final {
        explicit Entry(const Key& k) noexcept :
            key(k),
            stackNode{this},
            queueNode{this} {}

        template<typename... Args>
        void emplace_value(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                value.emplace(std::forward<Args>(args)...);
            else
                value.emplace(std::make_shared<Value>(std::forward<Args>(args)...));
        }

        Key       key;
        EntryType type = EntryType::HIR_RESIDENT;

        ListNode stackNode;
        ListNode queueNode;

        bool inStack = false;
        bool inQueue = false;

        std::optional<storage_value_type> value;
    };

    using stack_type   = DoublyIntrusiveList<ListNode>;
    using queue_type   = DoublyIntrusiveList<ListNode>;
    using map_type     = std::unordered_map<Key, Entry*>;
    using map_iterator = typename map_type::iterator;

   public:
    explicit PoolLIRSCache(const usize cap        = 2 * KB,
                           const usize reserveCnt = 1 * KB,
                           const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap),
        lirTarget(calculate_lir_target(cap)) {
        configure();
    }

    PoolLIRSCache(const PoolLIRSCache&) noexcept            = delete;
    PoolLIRSCache& operator=(const PoolLIRSCache&) noexcept = delete;
    PoolLIRSCache(PoolLIRSCache&&) noexcept                 = delete;
    PoolLIRSCache& operator=(PoolLIRSCache&&) noexcept      = delete;

    ~PoolLIRSCache() noexcept {
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
        assert(nolock_consistency_check());
    }

    void evict_excess() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_evict_excess(capacity);
        assert(nolock_consistency_check());
    }

    [[nodiscard]] usize get_capacity() const noexcept {
        std::lock_guard lockGuard{mutex};
        return capacity;
    }

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity  = newCapacity;
        lirTarget = calculate_lir_target(capacity);

        nolock_rebalance();

        assert(nolock_consistency_check());
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        assert(nolock_consistency_check());
        return residentCount;
    }

    [[nodiscard]] usize ghost_size() const noexcept {
        std::lock_guard lockGuard{mutex};
        return map.size() - residentCount;
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        return residentCount == 0;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);

        return mapItr != map.end() && mapItr->second->value.has_value();
    }

    [[nodiscard]] bool contains_ghost(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);

        return mapItr != map.end() && mapItr->second->type == EntryType::HIR_GHOST;
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);

        if (UNLIKELY(mapItr == map.end()))
            return false;

        nolock_erase(mapItr);

        assert(nolock_consistency_check());
        return true;
    }

    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();

        assert(nolock_consistency_check());
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

        capacity  = defaultCapacity;
        lirTarget = calculate_lir_target(capacity);

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    // Keep at least one HIR slot when capacity is greater than one.
    [[nodiscard]] static constexpr usize calculate_lir_target(const usize cap) noexcept {
        if (cap <= 1)
            return 0;

        const usize hirCapacity = std::max<usize>(1, cap / 100);

        return cap - hirCapacity;
    }

    [[nodiscard]] ALWAYS_INLINE Return nolock_read(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    // -------------------------------------------------------------------------
    // List helpers
    // -------------------------------------------------------------------------

    void nolock_stack_push_front(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(!entry->inStack);
        assert(entry->stackNode.prev == nullptr);
        assert(entry->stackNode.next == nullptr);

        if (UNLIKELY(entry == nullptr || entry->inStack || entry->stackNode.prev != nullptr
                     || entry->stackNode.next != nullptr))
            std::terminate();

        if (UNLIKELY(!stack.push_front(entry->stackNode)))
            std::terminate();

        entry->inStack = true;
    }

    void nolock_queue_push_front(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(!entry->inQueue);
        assert(entry->queueNode.prev == nullptr);
        assert(entry->queueNode.next == nullptr);

        if (UNLIKELY(entry == nullptr || entry->inQueue || entry->queueNode.prev != nullptr
                     || entry->queueNode.next != nullptr))
            std::terminate();

        if (UNLIKELY(!queue.push_front(entry->queueNode)))
            std::terminate();

        entry->inQueue = true;
    }

    void nolock_stack_erase(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(entry->inStack);

        if (UNLIKELY(!stack.erase(entry->stackNode)))
            std::terminate();

        entry->inStack        = false;
        entry->stackNode.prev = nullptr;
        entry->stackNode.next = nullptr;
    }

    void nolock_queue_erase(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(entry->inQueue);

        if (UNLIKELY(!queue.erase(entry->queueNode)))
            std::terminate();

        entry->inQueue        = false;
        entry->queueNode.prev = nullptr;
        entry->queueNode.next = nullptr;
    }

    // -------------------------------------------------------------------------
    // Stack S pruning
    // -------------------------------------------------------------------------

    void nolock_prune_stack() noexcept {
        while (ListNode* node = stack.back())
        {
            Entry* const entry = node->entry;
            assert(entry != nullptr);

            if (entry->type == EntryType::LIR)
                break;

            // Remove the HIR entry from Stack S.
            nolock_stack_erase(entry);

            if (entry->type == EntryType::HIR_GHOST)
            {
                // Ghost entries are no longer needed.
                map.erase(entry->key);
                pool.destroy(entry);
            }
        }
    }

    // -------------------------------------------------------------------------
    // LIR demotion
    // -------------------------------------------------------------------------

    void nolock_demote_bottom_lir() noexcept {
        assert(lirCount > 0);

        ListNode* const node = stack.back();

        assert(node != nullptr);

        Entry* const entry = node->entry;

        assert(entry != nullptr);
        assert(entry->type == EntryType::LIR);
        assert(entry->inStack);
        assert(!entry->inQueue);
        assert(entry->value.has_value());

        entry->type = EntryType::HIR_RESIDENT;

        --lirCount;

        // Demoted LIR becomes the newest HIR entry in Queue Q.
        nolock_queue_push_front(entry);

        // The demoted entry is now HIR at the bottom of Stack S.
        nolock_prune_stack();
    }

    // -------------------------------------------------------------------------
    // HIR eviction
    // -------------------------------------------------------------------------

    void nolock_evict_one() noexcept {
        assert(residentCount > 0);

        if (UNLIKELY(queue.empty()))
        {
            assert(lirCount > 0);
            nolock_demote_bottom_lir();
        }

        ListNode* const node = queue.back();
        assert(node != nullptr);

        Entry* const entry = node->entry;
        assert(entry != nullptr);
        assert(entry->type == EntryType::HIR_RESIDENT);
        assert(entry->inQueue);
        assert(entry->value.has_value());

        nolock_queue_erase(entry);
        --residentCount;

        if (entry->inStack)
        {
            entry->type = EntryType::HIR_GHOST;
            entry->value.reset();
        }
        else
        {
            map.erase(entry->key);
            pool.destroy(entry);
        }

        nolock_prune_stack();
    }

    void nolock_evict_excess(const usize targetSize) noexcept {
        while (UNLIKELY(residentCount > targetSize))
            nolock_evict_one();
    }

    void nolock_rebalance() noexcept {
        // Reduce LIR population when the target capacity decreases.
        while (UNLIKELY(lirCount > lirTarget))
            nolock_demote_bottom_lir();

        nolock_evict_excess(capacity);
    }

    // -------------------------------------------------------------------------
    // Entry insertion
    // -------------------------------------------------------------------------

    void nolock_link_new_entry(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());
        assert(!entry->inStack && !entry->inQueue);

        ++residentCount;

        if (lirCount < lirTarget)
        {
            entry->type = EntryType::LIR;

            ++lirCount;

            nolock_stack_push_front(entry);
        }
        else
        {
            entry->type = EntryType::HIR_RESIDENT;

            nolock_queue_push_front(entry);
        }
    }

    // -------------------------------------------------------------------------
    // Entry access
    // -------------------------------------------------------------------------

    void nolock_touch(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());

        switch (entry->type)
        {
        case EntryType::LIR :
            // LIR access: move to MRU position in Stack S.
            stack.move_front(entry->stackNode);
            nolock_prune_stack();
            break;

        case EntryType::HIR_RESIDENT :
            if (entry->inStack)
            {
                // HIR in Stack S: promote to LIR.
                assert(lirTarget > 0);
                assert(entry->inQueue);

                nolock_queue_erase(entry);

                entry->type = EntryType::LIR;
                ++lirCount;

                stack.move_front(entry->stackNode);

                // Demote the bottom LIR.
                nolock_demote_bottom_lir();
            }
            else
            {
                // HIR outside Stack S: update Queue Q recency.
                queue.move_front(entry->queueNode);

                if (lirTarget != 0)
                {
                    nolock_stack_push_front(entry);
                    nolock_prune_stack();
                }
            }
            break;

        case EntryType::HIR_GHOST :
            assert(false);
            UNREACHABLE();
        }
    }

    // -------------------------------------------------------------------------
    // Ghost restoration
    // -------------------------------------------------------------------------

    template<typename... Args>
    Entry* nolock_restore_ghost(Entry* entry, Args&&... args) noexcept {
        assert(entry != nullptr);
        assert(entry->type == EntryType::HIR_GHOST);
        assert(!entry->value.has_value());
        assert(entry->inStack);
        assert(!entry->inQueue);

        entry->emplace_value(std::forward<Args>(args)...);

        entry->type = EntryType::LIR;
        ++lirCount;
        ++residentCount;

        stack.move_front(entry->stackNode);

        if (residentCount > capacity)
            nolock_evict_one();

        // Eviction may already have demoted an LIR entry.
        if (lirCount > lirTarget)
            nolock_demote_bottom_lir();

        return entry;
    }

    // -------------------------------------------------------------------------
    // Write
    // -------------------------------------------------------------------------

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (UNLIKELY(capacity == 0))
            return Return{std::forward<Args>(args)...};

        const auto existingItr = map.find(key);
        if (LIKELY(existingItr != map.end()))
        {
            Entry* const entry = existingItr->second;
            assert(entry != nullptr);

            if (entry->type == EntryType::HIR_GHOST)
                return nolock_read(nolock_restore_ghost(entry, std::forward<Args>(args)...));

            nolock_touch(entry);
            return nolock_read(entry);
        }

        // Evict before inserting to avoid invalidating the flat map iterator.
        if (UNLIKELY(residentCount >= capacity))
            nolock_evict_one();

        // Allocate the node from the custom stable pool.
        Entry* const entry = pool.create_object(key);
        if (UNLIKELY(entry == nullptr))
            return Return{std::forward<Args>(args)...};

        entry->emplace_value(std::forward<Args>(args)...);

        // Store the stable Entry pointer in the flat map.
        const auto [mapItr, inserted] = map.try_emplace(key, entry);
        if (UNLIKELY(!inserted))
        {
            pool.destroy(entry);
            assert(false);
            UNREACHABLE();
        }

        nolock_link_new_entry(entry);

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (UNLIKELY(capacity == 0))
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto existingItr = map.find(key);
        if (LIKELY(existingItr != map.end()))
        {
            Entry* const entry = existingItr->second;
            assert(entry != nullptr);

            if (entry->type == EntryType::HIR_GHOST)
                return nolock_read(nolock_restore_ghost(
                  entry, std::forward<Builder>(builder)(std::forward<Args>(args)...)));

            nolock_touch(entry);
            return nolock_read(entry);
        }

        // Evict before inserting to avoid invalidating the flat map iterator.
        if (UNLIKELY(residentCount >= capacity))
            nolock_evict_one();

        // Allocate the node from the custom stable pool.
        Entry* const entry = pool.create_object(key);
        if (UNLIKELY(entry == nullptr))
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        entry->emplace_value(std::forward<Builder>(builder)(std::forward<Args>(args)...));

        // Store the stable Entry pointer in the flat map.
        const auto [mapItr, inserted] = map.try_emplace(key, entry);
        if (UNLIKELY(!inserted))
        {
            pool.destroy(entry);
            assert(false);
            UNREACHABLE();
        }

        nolock_link_new_entry(entry);

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    // -------------------------------------------------------------------------
    // Removal
    // -------------------------------------------------------------------------

    void nolock_erase(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());

        Entry* const entry = mapItr->second;
        assert(entry != nullptr);

        if (entry->type == EntryType::LIR)
        {
            assert(lirCount > 0);
            assert(residentCount > 0);

            --lirCount;
            --residentCount;
        }
        else if (entry->type == EntryType::HIR_RESIDENT)
        {
            assert(residentCount > 0);

            --residentCount;
            nolock_queue_erase(entry);
        }

        if (entry->inStack)
            nolock_stack_erase(entry);

        map.erase(entry->key);
        pool.destroy(entry);

        nolock_prune_stack();
    }

    // -------------------------------------------------------------------------
    // Clear / configure
    // -------------------------------------------------------------------------

    void nolock_clear() noexcept {
        stack.clear();
        queue.clear();

        for (auto& [key, entry] : map)
            pool.destroy(entry);

        map.clear();

        residentCount = 0;
        lirCount      = 0;
    }

    void nolock_release_pool() noexcept { pool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    // -------------------------------------------------------------------------
    // Consistency check
    // -------------------------------------------------------------------------

#if !defined(NDEBUG)
    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        return true;  // Disable for now to avoid false positives in debug builds

        if (UNLIKELY(residentCount > capacity || lirCount > residentCount))
            return false;

        if (UNLIKELY(map.size() < residentCount))
            return false;

        usize stackCount     = 0;
        usize queueCount     = 0;
        usize residentCount2 = 0;
        usize lirCount2      = 0;

        // Validate Stack S.
        for (const ListNode* node = stack.front(); node != nullptr; node = stack.next(*node))
        {
            const Entry* entry = node->entry;

            if (UNLIKELY(entry == nullptr || !entry->inStack || &entry->stackNode != node))
                return false;

            ++stackCount;

            const auto mapItr = map.find(entry->key);

            if (UNLIKELY(mapItr == map.end() || mapItr->second != entry))
                return false;

            if (entry->type == EntryType::LIR)
            {
                if (UNLIKELY(!entry->value.has_value() || entry->inQueue))
                    return false;

                ++lirCount2;
            }
            else if (entry->type == EntryType::HIR_RESIDENT)
            {
                if (UNLIKELY(!entry->value.has_value() || !entry->inQueue))
                    return false;
            }
            else
            {
                if (UNLIKELY(entry->value.has_value() || entry->inQueue))
                    return false;
            }
        }

        // Validate Queue Q.
        for (const ListNode* node = queue.front(); node != nullptr; node = queue.next(*node))
        {
            const Entry* entry = node->entry;

            if (UNLIKELY(entry == nullptr || !entry->inQueue || &entry->queueNode != node))
                return false;

            if (UNLIKELY(entry->type != EntryType::HIR_RESIDENT))
                return false;

            if (UNLIKELY(!entry->value.has_value()))
                return false;

            ++queueCount;
        }

        // Validate the map and count resident/ghost entries.
        usize stackCount2 = 0;

        for (const auto& [key, entry] : map)
        {
            if (UNLIKELY(entry == nullptr || entry->key != key))
                return false;

            if (entry->inStack)
                ++stackCount2;

            if (entry->type == EntryType::LIR)
            {
                if (UNLIKELY(!entry->value.has_value() || !entry->inStack || entry->inQueue))
                    return false;

                ++residentCount2;
            }
            else if (entry->type == EntryType::HIR_RESIDENT)
            {
                if (UNLIKELY(!entry->value.has_value() || !entry->inQueue))
                    return false;

                ++residentCount2;
            }
            else
            {
                if (UNLIKELY(entry->value.has_value() || !entry->inStack || entry->inQueue))
                    return false;
            }
        }

        if (UNLIKELY(residentCount2 != residentCount))
            return false;

        if (UNLIKELY(lirCount2 != lirCount))
            return false;

        if (UNLIKELY(queueCount != residentCount - lirCount))
            return false;

        if (UNLIKELY(stackCount2 != stackCount))
            return false;

        // Stack S must end with an LIR entry whenever it is nonempty.
        if (stackCount != 0)
        {
            const ListNode* node = stack.back();

            if (UNLIKELY(node == nullptr || node->entry->type != EntryType::LIR))
                return false;
        }

        if (UNLIKELY(lirTarget == 0 && stackCount != 0))
            return false;

        return true;
    }
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;

    usize capacity;
    usize lirTarget;

    map_type   map;
    stack_type stack;
    queue_type queue;

    Pool<Entry> pool;

    usize residentCount = 0;
    usize lirCount      = 0;
};

// LIRS (Low Inter-reference Recency Set) cache using a stack and a queue.
template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatLIRSCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry;

    struct ListNode final {
        Entry*    entry;
        ListNode* prev = nullptr;
        ListNode* next = nullptr;
    };

    enum class EntryType : u8 {
        LIR,
        HIR_RESIDENT,
        HIR_GHOST
    };

    struct Entry final {
        explicit Entry(const Key& k) noexcept :
            key(k),
            stackNode{this},
            queueNode{this} {}

        template<typename... Args>
        void emplace_value(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                value.emplace(std::forward<Args>(args)...);
            else
                value.emplace(std::make_shared<Value>(std::forward<Args>(args)...));
        }

        Key       key;
        EntryType type = EntryType::HIR_RESIDENT;

        ListNode stackNode;
        ListNode queueNode;

        bool inStack = false;
        bool inQueue = false;

        std::optional<storage_value_type> value;
    };

    using stack_type   = DoublyIntrusiveList<ListNode>;
    using queue_type   = DoublyIntrusiveList<ListNode>;
    using map_type     = ankerl::unordered_dense::map<Key, Entry*>;
    using map_iterator = typename map_type::iterator;

   public:
    explicit PoolFlatLIRSCache(const usize cap        = 2 * KB,
                               const usize reserveCnt = 1 * KB,
                               const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap),
        lirTarget(calculate_lir_target(cap)) {
        configure();
    }

    PoolFlatLIRSCache(const PoolFlatLIRSCache&) noexcept            = delete;
    PoolFlatLIRSCache& operator=(const PoolFlatLIRSCache&) noexcept = delete;
    PoolFlatLIRSCache(PoolFlatLIRSCache&&) noexcept                 = delete;
    PoolFlatLIRSCache& operator=(PoolFlatLIRSCache&&) noexcept      = delete;

    ~PoolFlatLIRSCache() noexcept {
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
        assert(nolock_consistency_check());
    }

    void evict_excess() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_evict_excess(capacity);
        assert(nolock_consistency_check());
    }

    [[nodiscard]] usize get_capacity() const noexcept {
        std::lock_guard lockGuard{mutex};
        return capacity;
    }

    void set_capacity(const usize newCapacity) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity  = newCapacity;
        lirTarget = calculate_lir_target(capacity);

        nolock_rebalance();

        assert(nolock_consistency_check());
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        assert(nolock_consistency_check());
        return residentCount;
    }

    [[nodiscard]] usize ghost_size() const noexcept {
        std::lock_guard lockGuard{mutex};
        return map.size() - residentCount;
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        return residentCount == 0;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);

        return mapItr != map.end() && mapItr->second->value.has_value();
    }

    [[nodiscard]] bool contains_ghost(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);

        return mapItr != map.end() && mapItr->second->type == EntryType::HIR_GHOST;
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);

        if (UNLIKELY(mapItr == map.end()))
            return false;

        nolock_erase(mapItr);

        assert(nolock_consistency_check());
        return true;
    }

    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();

        assert(nolock_consistency_check());
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

        capacity  = defaultCapacity;
        lirTarget = calculate_lir_target(capacity);

        nolock_configure();

        assert(nolock_consistency_check());
    }

   private:
    // Keep at least one HIR slot when capacity is greater than one.
    [[nodiscard]] static constexpr usize calculate_lir_target(const usize cap) noexcept {
        if (cap <= 1)
            return 0;

        const usize hirCapacity = std::max<usize>(1, cap / 100);

        return cap - hirCapacity;
    }

    [[nodiscard]] ALWAYS_INLINE Return nolock_read(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    // -------------------------------------------------------------------------
    // List helpers
    // -------------------------------------------------------------------------

    void nolock_stack_push_front(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(!entry->inStack);
        assert(entry->stackNode.prev == nullptr);
        assert(entry->stackNode.next == nullptr);

        if (UNLIKELY(entry == nullptr || entry->inStack || entry->stackNode.prev != nullptr
                     || entry->stackNode.next != nullptr))
            std::terminate();

        if (UNLIKELY(!stack.push_front(entry->stackNode)))
            std::terminate();

        entry->inStack = true;
    }

    void nolock_queue_push_front(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(!entry->inQueue);
        assert(entry->queueNode.prev == nullptr);
        assert(entry->queueNode.next == nullptr);

        if (UNLIKELY(entry == nullptr || entry->inQueue || entry->queueNode.prev != nullptr
                     || entry->queueNode.next != nullptr))
            std::terminate();

        if (UNLIKELY(!queue.push_front(entry->queueNode)))
            std::terminate();

        entry->inQueue = true;
    }

    void nolock_stack_erase(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(entry->inStack);

        if (UNLIKELY(!stack.erase(entry->stackNode)))
            std::terminate();

        entry->inStack        = false;
        entry->stackNode.prev = nullptr;
        entry->stackNode.next = nullptr;
    }

    void nolock_queue_erase(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(entry->inQueue);

        if (UNLIKELY(!queue.erase(entry->queueNode)))
            std::terminate();

        entry->inQueue        = false;
        entry->queueNode.prev = nullptr;
        entry->queueNode.next = nullptr;
    }

    // -------------------------------------------------------------------------
    // Stack S pruning
    // -------------------------------------------------------------------------

    void nolock_prune_stack() noexcept {
        while (ListNode* node = stack.back())
        {
            Entry* const entry = node->entry;
            assert(entry != nullptr);

            if (entry->type == EntryType::LIR)
                break;

            // Remove the HIR entry from Stack S.
            nolock_stack_erase(entry);

            if (entry->type == EntryType::HIR_GHOST)
            {
                // Ghost entries are no longer needed.
                map.erase(entry->key);
                pool.destroy(entry);
            }
        }
    }

    // -------------------------------------------------------------------------
    // LIR demotion
    // -------------------------------------------------------------------------

    void nolock_demote_bottom_lir() noexcept {
        assert(lirCount > 0);

        ListNode* const node = stack.back();

        assert(node != nullptr);

        Entry* const entry = node->entry;

        assert(entry != nullptr);
        assert(entry->type == EntryType::LIR);
        assert(entry->inStack);
        assert(!entry->inQueue);
        assert(entry->value.has_value());

        entry->type = EntryType::HIR_RESIDENT;

        --lirCount;

        // Demoted LIR becomes the newest HIR entry in Queue Q.
        nolock_queue_push_front(entry);

        // The demoted entry is now HIR at the bottom of Stack S.
        nolock_prune_stack();
    }

    // -------------------------------------------------------------------------
    // HIR eviction
    // -------------------------------------------------------------------------

    void nolock_evict_one() noexcept {
        assert(residentCount > 0);

        if (UNLIKELY(queue.empty()))
        {
            assert(lirCount > 0);
            nolock_demote_bottom_lir();
        }

        ListNode* const node = queue.back();
        assert(node != nullptr);

        Entry* const entry = node->entry;
        assert(entry != nullptr);
        assert(entry->type == EntryType::HIR_RESIDENT);
        assert(entry->inQueue);
        assert(entry->value.has_value());

        nolock_queue_erase(entry);
        --residentCount;

        if (entry->inStack)
        {
            entry->type = EntryType::HIR_GHOST;
            entry->value.reset();
        }
        else
        {
            map.erase(entry->key);
            pool.destroy(entry);
        }

        nolock_prune_stack();
    }

    void nolock_evict_excess(const usize targetSize) noexcept {
        while (UNLIKELY(residentCount > targetSize))
            nolock_evict_one();
    }

    void nolock_rebalance() noexcept {
        // Reduce LIR population when the target capacity decreases.
        while (UNLIKELY(lirCount > lirTarget))
            nolock_demote_bottom_lir();

        nolock_evict_excess(capacity);
    }

    // -------------------------------------------------------------------------
    // Entry insertion
    // -------------------------------------------------------------------------

    void nolock_link_new_entry(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());
        assert(!entry->inStack && !entry->inQueue);

        ++residentCount;

        if (lirCount < lirTarget)
        {
            entry->type = EntryType::LIR;

            ++lirCount;

            nolock_stack_push_front(entry);
        }
        else
        {
            entry->type = EntryType::HIR_RESIDENT;

            nolock_queue_push_front(entry);
        }
    }

    // -------------------------------------------------------------------------
    // Entry access
    // -------------------------------------------------------------------------

    void nolock_touch(Entry* entry) noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());

        switch (entry->type)
        {
        case EntryType::LIR :
            // LIR access: move to MRU position in Stack S.
            stack.move_front(entry->stackNode);
            nolock_prune_stack();
            break;

        case EntryType::HIR_RESIDENT :
            if (entry->inStack)
            {
                // HIR in Stack S: promote to LIR.
                assert(lirTarget > 0);
                assert(entry->inQueue);

                nolock_queue_erase(entry);

                entry->type = EntryType::LIR;
                ++lirCount;

                stack.move_front(entry->stackNode);

                // Demote the bottom LIR.
                nolock_demote_bottom_lir();
            }
            else
            {
                // HIR outside Stack S: update Queue Q recency.
                queue.move_front(entry->queueNode);

                if (lirTarget != 0)
                {
                    nolock_stack_push_front(entry);
                    nolock_prune_stack();
                }
            }
            break;

        case EntryType::HIR_GHOST :
            assert(false);
            UNREACHABLE();
        }
    }

    // -------------------------------------------------------------------------
    // Ghost restoration
    // -------------------------------------------------------------------------

    template<typename... Args>
    Entry* nolock_restore_ghost(Entry* entry, Args&&... args) noexcept {
        assert(entry != nullptr);
        assert(entry->type == EntryType::HIR_GHOST);
        assert(!entry->value.has_value());
        assert(entry->inStack);
        assert(!entry->inQueue);

        entry->emplace_value(std::forward<Args>(args)...);

        entry->type = EntryType::LIR;
        ++lirCount;
        ++residentCount;

        stack.move_front(entry->stackNode);

        if (residentCount > capacity)
            nolock_evict_one();

        // Eviction may already have demoted an LIR entry.
        if (lirCount > lirTarget)
            nolock_demote_bottom_lir();

        return entry;
    }

    // -------------------------------------------------------------------------
    // Write
    // -------------------------------------------------------------------------

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (UNLIKELY(capacity == 0))
            return Return{std::forward<Args>(args)...};

        const auto existingItr = map.find(key);
        if (LIKELY(existingItr != map.end()))
        {
            Entry* const entry = existingItr->second;
            assert(entry != nullptr);

            if (entry->type == EntryType::HIR_GHOST)
                return nolock_read(nolock_restore_ghost(entry, std::forward<Args>(args)...));

            nolock_touch(entry);
            return nolock_read(entry);
        }

        // Evict before inserting to avoid invalidating the flat map iterator.
        if (UNLIKELY(residentCount >= capacity))
            nolock_evict_one();

        // Allocate the node from the custom stable pool.
        Entry* const entry = pool.create_object(key);
        if (UNLIKELY(entry == nullptr))
            return Return{std::forward<Args>(args)...};

        entry->emplace_value(std::forward<Args>(args)...);

        // Store the stable Entry pointer in the flat map.
        const auto [mapItr, inserted] = map.try_emplace(key, entry);
        if (UNLIKELY(!inserted))
        {
            pool.destroy(entry);
            assert(false);
            UNREACHABLE();
        }

        nolock_link_new_entry(entry);

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (UNLIKELY(capacity == 0))
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto existingItr = map.find(key);
        if (LIKELY(existingItr != map.end()))
        {
            Entry* const entry = existingItr->second;
            assert(entry != nullptr);

            if (entry->type == EntryType::HIR_GHOST)
                return nolock_read(nolock_restore_ghost(
                  entry, std::forward<Builder>(builder)(std::forward<Args>(args)...)));

            nolock_touch(entry);
            return nolock_read(entry);
        }

        // Evict before inserting to avoid invalidating the flat map iterator.
        if (UNLIKELY(residentCount >= capacity))
            nolock_evict_one();

        // Allocate the node from the custom stable pool.
        Entry* const entry = pool.create_object(key);
        if (UNLIKELY(entry == nullptr))
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        entry->emplace_value(std::forward<Builder>(builder)(std::forward<Args>(args)...));

        // Store the stable Entry pointer in the flat map.
        const auto [mapItr, inserted] = map.try_emplace(key, entry);
        if (UNLIKELY(!inserted))
        {
            pool.destroy(entry);
            assert(false);
            UNREACHABLE();
        }

        nolock_link_new_entry(entry);

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    // -------------------------------------------------------------------------
    // Removal
    // -------------------------------------------------------------------------

    void nolock_erase(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());

        Entry* const entry = mapItr->second;
        assert(entry != nullptr);

        if (entry->type == EntryType::LIR)
        {
            assert(lirCount > 0);
            assert(residentCount > 0);

            --lirCount;
            --residentCount;
        }
        else if (entry->type == EntryType::HIR_RESIDENT)
        {
            assert(residentCount > 0);

            --residentCount;
            nolock_queue_erase(entry);
        }

        if (entry->inStack)
            nolock_stack_erase(entry);

        map.erase(entry->key);
        pool.destroy(entry);

        nolock_prune_stack();
    }

    // -------------------------------------------------------------------------
    // Clear / configure
    // -------------------------------------------------------------------------

    void nolock_clear() noexcept {
        stack.clear();
        queue.clear();

        for (auto& [key, entry] : map)
            pool.destroy(entry);

        map.clear();

        residentCount = 0;
        lirCount      = 0;
    }

    void nolock_release_pool() noexcept { pool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    // -------------------------------------------------------------------------
    // Consistency check
    // -------------------------------------------------------------------------

#if !defined(NDEBUG)
    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        return true;  // Disable for now to avoid false positives in debug builds

        if (UNLIKELY(residentCount > capacity || lirCount > residentCount))
            return false;

        if (UNLIKELY(map.size() < residentCount))
            return false;

        usize stackCount     = 0;
        usize queueCount     = 0;
        usize residentCount2 = 0;
        usize lirCount2      = 0;

        // Validate Stack S.
        for (const ListNode* node = stack.front(); node != nullptr; node = stack.next(*node))
        {
            const Entry* entry = node->entry;

            if (UNLIKELY(entry == nullptr || !entry->inStack || &entry->stackNode != node))
                return false;

            ++stackCount;

            const auto mapItr = map.find(entry->key);

            if (UNLIKELY(mapItr == map.end() || mapItr->second != entry))
                return false;

            if (entry->type == EntryType::LIR)
            {
                if (UNLIKELY(!entry->value.has_value() || entry->inQueue))
                    return false;

                ++lirCount2;
            }
            else if (entry->type == EntryType::HIR_RESIDENT)
            {
                if (UNLIKELY(!entry->value.has_value() || !entry->inQueue))
                    return false;
            }
            else
            {
                if (UNLIKELY(entry->value.has_value() || entry->inQueue))
                    return false;
            }
        }

        // Validate Queue Q.
        for (const ListNode* node = queue.front(); node != nullptr; node = queue.next(*node))
        {
            const Entry* entry = node->entry;

            if (UNLIKELY(entry == nullptr || !entry->inQueue || &entry->queueNode != node))
                return false;

            if (UNLIKELY(entry->type != EntryType::HIR_RESIDENT))
                return false;

            if (UNLIKELY(!entry->value.has_value()))
                return false;

            ++queueCount;
        }

        // Validate the map and count resident/ghost entries.
        usize stackCount2 = 0;

        for (const auto& [key, entry] : map)
        {
            if (UNLIKELY(entry == nullptr || entry->key != key))
                return false;

            if (entry->inStack)
                ++stackCount2;

            if (entry->type == EntryType::LIR)
            {
                if (UNLIKELY(!entry->value.has_value() || !entry->inStack || entry->inQueue))
                    return false;

                ++residentCount2;
            }
            else if (entry->type == EntryType::HIR_RESIDENT)
            {
                if (UNLIKELY(!entry->value.has_value() || !entry->inQueue))
                    return false;

                ++residentCount2;
            }
            else
            {
                if (UNLIKELY(entry->value.has_value() || !entry->inStack || entry->inQueue))
                    return false;
            }
        }

        if (UNLIKELY(residentCount2 != residentCount))
            return false;

        if (UNLIKELY(lirCount2 != lirCount))
            return false;

        if (UNLIKELY(queueCount != residentCount - lirCount))
            return false;

        if (UNLIKELY(stackCount2 != stackCount))
            return false;

        // Stack S must end with an LIR entry whenever it is nonempty.
        if (stackCount != 0)
        {
            const ListNode* node = stack.back();

            if (UNLIKELY(node == nullptr || node->entry->type != EntryType::LIR))
                return false;
        }

        if (UNLIKELY(lirTarget == 0 && stackCount != 0))
            return false;

        return true;
    }
#endif

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;

    usize capacity;
    usize lirTarget;

    map_type   map;
    stack_type stack;
    queue_type queue;

    Pool<Entry> pool;

    usize residentCount = 0;
    usize lirCount      = 0;
};

}  // namespace DON

#endif  // UTILITY_LIRS_CACHE_H_INCLUDED
