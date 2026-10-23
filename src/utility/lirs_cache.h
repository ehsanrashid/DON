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

    using list_type    = DoublyIntrusiveList<Link>;
    using map_type     = std::unordered_map<Key, Entry*>;
    using map_iterator = typename map_type::iterator;

    struct Entry final {
        explicit Entry(const Key& key) noexcept :
            key(key) {}

        template<typename... Args>
        void emplaceValue(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                value.emplace(std::forward<Args>(args)...);
            else
                value.emplace(std::make_shared<Value>(std::forward<Args>(args)...));
        }

        Key  key;
        bool isLir      = false;
        bool inStack    = false;
        bool inQueue    = false;
        bool inHirStack = false;

        Link sLink{this};
        Link qLink{this};
        Link hLink{this};

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

    ~PoolLIRSCache() noexcept {
        std::lock_guard lockGuard{mutex};
        nolock_clear();
        pool.release();
    }

    PoolLIRSCache(const PoolLIRSCache&)            = delete;
    PoolLIRSCache& operator=(const PoolLIRSCache&) = delete;
    PoolLIRSCache(PoolLIRSCache&&)                 = delete;
    PoolLIRSCache& operator=(PoolLIRSCache&&)      = delete;

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

        nolock_reserve_map(capacity);
        nolock_clamp_lir_size();

        while (queueQ.size() > nolock_hir_capacity())
        {
            if (!nolock_evict_oldest_hir())
                break;
        }

        nolock_evict_excess(capacity);
        nolock_stack_prune();
        nolock_trim_stack_limit();
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};
        const auto      mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr
            && nolock_is_resident(mapItr->second);
    }

    [[nodiscard]] bool contains_ghost(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};
        const auto      mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr
            && !nolock_is_resident(mapItr->second);
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        return nolock_resident_size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
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
    }

    void reset() noexcept {
        std::lock_guard lockGuard{mutex};
        nolock_clear();
        pool.release();
        map.clear();
        capacity = defaultCapacity;
        lirSize  = 0;
        nolock_configure();
    }

   private:
    [[nodiscard]] inline usize nolock_hir_capacity() const noexcept {
        if (capacity == 0)
            return 0;
        const usize hirCapacity = capacity / 20 + (capacity % 20 != 0);
        return std::min(capacity, std::max(usize{1}, hirCapacity));
    }

    [[nodiscard]] inline usize nolock_lir_capacity() const noexcept {
        return capacity - nolock_hir_capacity();
    }

    [[nodiscard]] usize nolock_map_reserve_size(const usize requestedCapacity) const noexcept {
        constexpr usize maxSize     = std::numeric_limits<usize>::max();
        const usize     hirCapacity = requestedCapacity / 20 + (requestedCapacity % 20 != 0);
        const usize     requested   = requestedCapacity > (maxSize - hirCapacity - 1) / 2
                                      ? maxSize
                                      : 2 * requestedCapacity + hirCapacity + 1;
        return std::max(reserveCount, requested);
    }

    void nolock_reserve_map(const usize requestedCapacity) noexcept {
        map.reserve(nolock_map_reserve_size(requestedCapacity));
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        nolock_reserve_map(capacity);
    }

    [[nodiscard]] inline bool nolock_is_resident(const Entry* entry) const noexcept {
        return entry != nullptr && entry->value.has_value();
    }

    [[nodiscard]] inline usize nolock_resident_size() const noexcept {
        return lirSize + queueQ.size();
    }

    [[nodiscard]] Return nolock_read(const Entry* entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    inline void nolock_hir_stack_push_front(Entry& entry) noexcept {
        if (!entry.inHirStack)
        {
            hirStack.push_front(entry.hLink);
            entry.inHirStack = true;
        }
    }

    inline void nolock_hir_stack_move_front(Entry& entry) noexcept {
        if (entry.inHirStack)
        {
            hirStack.move_front(entry.hLink);
        }
    }

    inline void nolock_hir_stack_remove(Entry& entry) noexcept {
        if (entry.inHirStack)
        {
            hirStack.erase(entry.hLink);
            entry.inHirStack = false;
        }
    }

    void nolock_stack_push_front(Entry& entry) noexcept {
        if (!entry.inStack)
        {
            stackS.push_front(entry.sLink);
            entry.inStack = true;
            if (!entry.isLir)
            {
                nolock_hir_stack_push_front(entry);
            }
        }
    }

    void nolock_stack_move_front(Entry& entry) noexcept {
        if (!entry.inStack)
        {
            nolock_stack_push_front(entry);
            return;
        }
        stackS.move_front(entry.sLink);
        if (!entry.isLir)
        {
            nolock_hir_stack_move_front(entry);
        }
    }

    void nolock_stack_remove(Entry& entry) noexcept {
        if (!entry.inStack)
            return;
        if (!entry.isLir)
        {
            nolock_hir_stack_remove(entry);
        }
        stackS.erase(entry.sLink);
        entry.inStack = false;
    }

    inline void nolock_queue_push_back(Entry& entry) noexcept {
        if (!entry.inQueue)
        {
            queueQ.push_back(entry.qLink);
            entry.inQueue = true;
        }
    }

    inline void nolock_queue_move_back(Entry& entry) noexcept {
        if (!entry.inQueue)
        {
            queueQ.push_back(entry.qLink);
            return;
        }
        queueQ.move_back(entry.qLink);
    }

    inline void nolock_queue_remove(Entry& entry) noexcept {
        if (entry.inQueue)
        {
            queueQ.erase(entry.qLink);
            entry.inQueue = false;
        }
    }

    inline void nolock_destroy_unlinked_entry(Entry& entry) noexcept {
        map.erase(entry.key);
        pool.destroy(&entry);
    }

    void nolock_destroy_entry(Entry& entry) noexcept {
        nolock_stack_remove(entry);
        nolock_hir_stack_remove(entry);
        nolock_queue_remove(entry);
        nolock_destroy_unlinked_entry(entry);
    }

    void nolock_stack_prune() noexcept {
        while (Link* tail = stackS.back())
        {
            Entry* const entry = tail->owner;
            if (entry->isLir)
                break;

            nolock_stack_remove(*entry);
            if (!nolock_is_resident(entry))
            {
                nolock_destroy_unlinked_entry(*entry);
            }
        }
    }

    // Bug Fixed: Clean execution bounding without scan traps
    void nolock_trim_stack_limit() noexcept {
        const usize maxSize = capacity > std::numeric_limits<usize>::max() / 2
                              ? std::numeric_limits<usize>::max()
                              : 2 * capacity;

        if (stackS.size() <= maxSize)
            return;
        const usize targetSize = maxSize - capacity / 2;
        while (stackS.size() > targetSize)
        {
            Link* const tail = hirStack.back();
            if (tail == nullptr)
                break;
            Entry* const entry = tail->owner;
            nolock_stack_remove(*entry);
            if (!nolock_is_resident(entry))
            {
                nolock_destroy_unlinked_entry(*entry);
            }
        }
    }
    [[nodiscard]] inline Entry* nolock_bottom_lir(Entry* except = nullptr) noexcept {
        Link* tail = stackS.back();
        if (tail == nullptr)
            return nullptr;
        Entry* entry = tail->owner;
        if (entry == except)
        {
            tail = tail->prev;
            if (tail == nullptr)
                return nullptr;
            entry = tail->owner;
        }
        return entry->isLir ? entry : nullptr;
    }
    void nolock_demote_bottom_lir(Entry* except = nullptr) noexcept {
        Entry* const entry = nolock_bottom_lir(except);
        if (entry == nullptr)
            return;
        nolock_stack_remove(*entry);
        entry->isLir = false;
        assert(lirSize != 0);
        --lirSize;
        nolock_queue_push_back(*entry);
        nolock_stack_prune();
    }
    [[nodiscard]] bool nolock_evict_oldest_hir() noexcept {
        Link* const front = queueQ.front();
        if (front == nullptr)
            return false;
        Entry* const entry = front->owner;
        nolock_queue_remove(*entry);
        entry->value.reset();
        if (!entry->inStack)
        {
            nolock_destroy_unlinked_entry(*entry);
        }
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
        const bool wasStackBottom = stackS.back() == &entry.sLink;
        nolock_stack_move_front(entry);
        if (wasStackBottom)
        {
            nolock_stack_prune();
        }
    }
    void nolock_touch_hir(Entry& entry) noexcept {
        const bool wasInStack = entry.inStack;
        if (wasInStack && nolock_lir_capacity() != 0)
        {
            nolock_queue_remove(entry);
            nolock_hir_stack_remove(entry);
            entry.isLir = true;
            ++lirSize;
            nolock_stack_move_front(entry);
            if (lirSize > nolock_lir_capacity())
            {
                nolock_demote_bottom_lir(&entry);
            }
            else
            {
                nolock_stack_prune();
            }
        }
        else
        {
            const bool wasStackBottom = wasInStack && stackS.back() == &entry.sLink;
            nolock_queue_move_back(entry);
            nolock_stack_move_front(entry);
            if (wasStackBottom)
            {
                nolock_stack_prune();
            }
        }
        if (!wasInStack)
        {
            nolock_trim_stack_limit();
        }
    }
    void nolock_reactivate_hir(Entry& entry, const bool wasInStack) noexcept {
        const bool promoteToLir = wasInStack && nolock_lir_capacity() != 0;
        if (promoteToLir)
        {
            nolock_hir_stack_remove(entry);
        }
        entry.isLir = promoteToLir;
        if (entry.isLir)
        {
            ++lirSize;
            nolock_stack_move_front(entry);
            if (lirSize > nolock_lir_capacity())
            {
                nolock_demote_bottom_lir(&entry);
            }
            else
            {
                nolock_stack_prune();
            }
        }
        else
        {
            nolock_stack_move_front(entry);
            nolock_queue_push_back(entry);
            nolock_stack_prune();
        }
        if (!wasInStack)
        {
            nolock_trim_stack_limit();
        }
    }
    void nolock_admit_new(Entry& entry) noexcept {
        if (lirSize < nolock_lir_capacity())
        {
            entry.isLir = true;
            ++lirSize;
            nolock_stack_push_front(entry);
        }
        else
        {
            entry.isLir = false;
            nolock_stack_push_front(entry);
            nolock_queue_push_back(entry);
        }
        if (lirSize == 0)
        {
            nolock_stack_prune();
        }
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
        nolock_trim_stack_limit();
    }
    [[nodiscard]] bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = map.find(key);
        if (mapItr == map.end() || mapItr->second == nullptr)
            return false;
        Entry* const entry = mapItr->second;
        if (entry->isLir)
        {
            assert(lirSize != 0);
            --lirSize;
        }
        nolock_destroy_entry(*entry);
        nolock_stack_prune();
        return true;
    }
    void nolock_clear() noexcept {
        for (auto& [key, entry] : map)
        {
            if (entry == nullptr)
                continue;
            pool.destroy(entry);
        }
        map.clear();
        stackS.clear();
        queueQ.clear();
        hirStack.clear();
        lirSize = 0;
    }
    template<typename... Args>
    Return nolock_insert_new_after_room(const Key& key, Args&&... args) noexcept {
        auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        if (!inserted)
            return nolock_write(key, std::forward<Args>(args)...);
        Entry* const entry = pool.create_object(key);
        entry->emplaceValue(std::forward<Args>(args)...);
        mapItr->second = entry;
        nolock_admit_new(*entry);
        return nolock_read(entry);
    }
    template<typename... Args>
    Return nolock_insert_new(const Key& key, Args&&... args) noexcept {
        if (!nolock_make_room())
            return Return{std::forward<Args>(args)...};
        return nolock_insert_new_after_room(key, std::forward<Args>(args)...);
    }
    template<typename... Args>
    inline Return nolock_reactivate_direct(Entry* const entry, Args&&... args) noexcept {
        if (!nolock_make_room())
            return Return{std::forward<Args>(args)...};
        const bool wasInStack = entry->inStack;
        entry->emplaceValue(std::forward<Args>(args)...);
        nolock_reactivate_hir(*entry, wasInStack);
        return nolock_read(entry);
    }
    template<typename... Args>
    Return nolock_reactivate(const Key& key, Args&&... args) noexcept {
        if (!nolock_make_room())
            return Return{std::forward<Args>(args)...};
        const auto mapItr = map.find(key);
        if (mapItr == map.end() || mapItr->second == nullptr)
            return nolock_insert_new_after_room(key, std::forward<Args>(args)...);
        Entry* const entry = mapItr->second;
        if (nolock_is_resident(entry))
        {
            if (entry->isLir)
                nolock_touch_lir(*entry);
            else
                nolock_touch_hir(*entry);
            return nolock_read(entry);
        }
        return nolock_reactivate_direct(entry, std::forward<Args>(args)...);
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
                if (entry->isLir)
                    nolock_touch_lir(*entry);
                else
                    nolock_touch_hir(*entry);
                return nolock_read(entry);
            }
            return nolock_reactivate_direct(entry, std::forward<Args>(args)...);
        }
        return nolock_insert_new(key, std::forward<Args>(args)...);
    }
    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward(builder)(std::forward<Args>(args)...)};
        const auto mapItr = map.find(key);
        if (mapItr != map.end() && mapItr->second != nullptr && nolock_is_resident(mapItr->second))
        {
            Entry* const entry = mapItr->second;
            if (entry->isLir)
                nolock_touch_lir(*entry);
            else
                nolock_touch_hir(*entry);
            return nolock_read(entry);
        }
        Value value = std::forward(builder)(std::forward<Args>(args)...);
        if (mapItr != map.end() && mapItr->second != nullptr)
        {
            return nolock_reactivate_direct(mapItr->second, std::move(value));
        }
        return nolock_insert_new_after_room(key, std::move(value));
    }
    const usize   defaultCapacity;
    const usize   reserveCount;
    const float   maxLoadFactor;
    mutable Mutex mutex;
    usize         capacity;
    usize         lirSize = 0;
    map_type      map;
    Pool<Entry>   pool;
    list_type     stackS;
    list_type     queueQ;
    list_type     hirStack;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatLIRSCache final {
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

    using list_type    = DoublyIntrusiveList<Link>;
    using map_type     = ankerl::unordered_dense::map<Key, Entry*>;
    using map_iterator = typename map_type::iterator;

    struct Entry final {
        explicit Entry(const Key& key) noexcept :
            key(key) {}

        template<typename... Args>
        void emplaceValue(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                value.emplace(std::forward<Args>(args)...);
            else
                value.emplace(std::make_shared<Value>(std::forward<Args>(args)...));
        }

        Key  key;
        bool isLir      = false;
        bool inStack    = false;
        bool inQueue    = false;
        bool inHirStack = false;

        Link sLink{this};
        Link qLink{this};
        Link hLink{this};

        std::optional<storage_value_type> value;
    };

   public:
    explicit PoolFlatLIRSCache(const usize cap        = 2 * KB,
                               const usize reserveCnt = 1 * KB,
                               const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    ~PoolFlatLIRSCache() noexcept {
        std::lock_guard lockGuard{mutex};
        nolock_clear();
        pool.release();
    }

    PoolFlatLIRSCache(const PoolFlatLIRSCache&)            = delete;
    PoolFlatLIRSCache& operator=(const PoolFlatLIRSCache&) = delete;
    PoolFlatLIRSCache(PoolFlatLIRSCache&&)                 = delete;
    PoolFlatLIRSCache& operator=(PoolFlatLIRSCache&&)      = delete;

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

        nolock_reserve_map(capacity);
        nolock_clamp_lir_size();

        while (queueQ.size() > nolock_hir_capacity())
        {
            if (!nolock_evict_oldest_hir())
                break;
        }

        nolock_evict_excess(capacity);
        nolock_stack_prune();
        nolock_trim_stack_limit();
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};
        const auto      mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr
            && nolock_is_resident(mapItr->second);
    }

    [[nodiscard]] bool contains_ghost(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};
        const auto      mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr
            && !nolock_is_resident(mapItr->second);
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        return nolock_resident_size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
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
    }

    void reset() noexcept {
        std::lock_guard lockGuard{mutex};
        nolock_clear();
        pool.release();
        map.clear();
        capacity = defaultCapacity;
        lirSize  = 0;
        nolock_configure();
    }

   private:
    [[nodiscard]] inline usize nolock_hir_capacity() const noexcept {
        if (capacity == 0)
            return 0;
        const usize hirCapacity = capacity / 20 + (capacity % 20 != 0);
        return std::min(capacity, std::max(usize{1}, hirCapacity));
    }

    [[nodiscard]] inline usize nolock_lir_capacity() const noexcept {
        return capacity - nolock_hir_capacity();
    }

    [[nodiscard]] usize nolock_map_reserve_size(const usize requestedCapacity) const noexcept {
        constexpr usize maxSize     = std::numeric_limits<usize>::max();
        const usize     hirCapacity = requestedCapacity / 20 + (requestedCapacity % 20 != 0);
        const usize     requested   = requestedCapacity > (maxSize - hirCapacity - 1) / 2
                                      ? maxSize
                                      : 2 * requestedCapacity + hirCapacity + 1;
        return std::max(reserveCount, requested);
    }

    void nolock_reserve_map(const usize requestedCapacity) noexcept {
        map.reserve(nolock_map_reserve_size(requestedCapacity));
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        nolock_reserve_map(capacity);
    }

    [[nodiscard]] inline bool nolock_is_resident(const Entry* entry) const noexcept {
        return entry != nullptr && entry->value.has_value();
    }

    [[nodiscard]] inline usize nolock_resident_size() const noexcept {
        return lirSize + queueQ.size();
    }

    [[nodiscard]] Return nolock_read(const Entry* entry) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    inline void nolock_hir_stack_push_front(Entry& entry) noexcept {
        if (!entry.inHirStack)
        {
            hirStack.push_front(entry.hLink);
            entry.inHirStack = true;
        }
    }

    inline void nolock_hir_stack_move_front(Entry& entry) noexcept {
        if (entry.inHirStack)
        {
            hirStack.move_front(entry.hLink);
        }
    }

    inline void nolock_hir_stack_remove(Entry& entry) noexcept {
        if (entry.inHirStack)
        {
            hirStack.erase(entry.hLink);
            entry.inHirStack = false;
        }
    }

    void nolock_stack_push_front(Entry& entry) noexcept {
        if (!entry.inStack)
        {
            stackS.push_front(entry.sLink);
            entry.inStack = true;
            if (!entry.isLir)
            {
                nolock_hir_stack_push_front(entry);
            }
        }
    }

    void nolock_stack_move_front(Entry& entry) noexcept {
        if (!entry.inStack)
        {
            nolock_stack_push_front(entry);
            return;
        }
        stackS.move_front(entry.sLink);
        if (!entry.isLir)
        {
            nolock_hir_stack_move_front(entry);
        }
    }

    void nolock_stack_remove(Entry& entry) noexcept {
        if (!entry.inStack)
            return;
        if (!entry.isLir)
        {
            nolock_hir_stack_remove(entry);
        }
        stackS.erase(entry.sLink);
        entry.inStack = false;
    }

    inline void nolock_queue_push_back(Entry& entry) noexcept {
        if (!entry.inQueue)
        {
            queueQ.push_back(entry.qLink);
            entry.inQueue = true;
        }
    }

    inline void nolock_queue_move_back(Entry& entry) noexcept {
        if (!entry.inQueue)
        {
            queueQ.push_back(entry.qLink);
            return;
        }
        queueQ.move_back(entry.qLink);
    }

    inline void nolock_queue_remove(Entry& entry) noexcept {
        if (entry.inQueue)
        {
            queueQ.erase(entry.qLink);
            entry.inQueue = false;
        }
    }

    inline void nolock_destroy_unlinked_entry(Entry& entry) noexcept {
        map.erase(entry.key);
        pool.destroy(&entry);
    }

    void nolock_destroy_entry(Entry& entry) noexcept {
        nolock_stack_remove(entry);
        nolock_hir_stack_remove(entry);
        nolock_queue_remove(entry);
        nolock_destroy_unlinked_entry(entry);
    }

    void nolock_stack_prune() noexcept {
        while (Link* tail = stackS.back())
        {
            Entry* const entry = tail->owner;
            if (entry->isLir)
                break;

            nolock_stack_remove(*entry);
            if (!nolock_is_resident(entry))
            {
                nolock_destroy_unlinked_entry(*entry);
            }
        }
    }

    // Bug Fixed: Clean execution bounding without scan traps
    void nolock_trim_stack_limit() noexcept {
        const usize maxSize = capacity > std::numeric_limits<usize>::max() / 2
                              ? std::numeric_limits<usize>::max()
                              : 2 * capacity;

        if (stackS.size() <= maxSize)
            return;
        const usize targetSize = maxSize - capacity / 2;
        while (stackS.size() > targetSize)
        {
            Link* const tail = hirStack.back();
            if (tail == nullptr)
                break;
            Entry* const entry = tail->owner;
            nolock_stack_remove(*entry);
            if (!nolock_is_resident(entry))
            {
                nolock_destroy_unlinked_entry(*entry);
            }
        }
    }
    [[nodiscard]] inline Entry* nolock_bottom_lir(Entry* except = nullptr) noexcept {
        Link* tail = stackS.back();
        if (tail == nullptr)
            return nullptr;
        Entry* entry = tail->owner;
        if (entry == except)
        {
            tail = tail->prev;
            if (tail == nullptr)
                return nullptr;
            entry = tail->owner;
        }
        return entry->isLir ? entry : nullptr;
    }
    void nolock_demote_bottom_lir(Entry* except = nullptr) noexcept {
        Entry* const entry = nolock_bottom_lir(except);
        if (entry == nullptr)
            return;
        nolock_stack_remove(*entry);
        entry->isLir = false;
        assert(lirSize != 0);
        --lirSize;
        nolock_queue_push_back(*entry);
        nolock_stack_prune();
    }
    [[nodiscard]] bool nolock_evict_oldest_hir() noexcept {
        Link* const front = queueQ.front();
        if (front == nullptr)
            return false;
        Entry* const entry = front->owner;
        nolock_queue_remove(*entry);
        entry->value.reset();
        if (!entry->inStack)
        {
            nolock_destroy_unlinked_entry(*entry);
        }
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
        const bool wasStackBottom = stackS.back() == &entry.sLink;
        nolock_stack_move_front(entry);
        if (wasStackBottom)
        {
            nolock_stack_prune();
        }
    }
    void nolock_touch_hir(Entry& entry) noexcept {
        const bool wasInStack = entry.inStack;
        if (wasInStack && nolock_lir_capacity() != 0)
        {
            nolock_queue_remove(entry);
            nolock_hir_stack_remove(entry);
            entry.isLir = true;
            ++lirSize;
            nolock_stack_move_front(entry);
            if (lirSize > nolock_lir_capacity())
            {
                nolock_demote_bottom_lir(&entry);
            }
            else
            {
                nolock_stack_prune();
            }
        }
        else
        {
            const bool wasStackBottom = wasInStack && stackS.back() == &entry.sLink;
            nolock_queue_move_back(entry);
            nolock_stack_move_front(entry);
            if (wasStackBottom)
            {
                nolock_stack_prune();
            }
        }
        if (!wasInStack)
        {
            nolock_trim_stack_limit();
        }
    }
    void nolock_reactivate_hir(Entry& entry, const bool wasInStack) noexcept {
        const bool promoteToLir = wasInStack && nolock_lir_capacity() != 0;
        if (promoteToLir)
        {
            nolock_hir_stack_remove(entry);
        }
        entry.isLir = promoteToLir;
        if (entry.isLir)
        {
            ++lirSize;
            nolock_stack_move_front(entry);
            if (lirSize > nolock_lir_capacity())
            {
                nolock_demote_bottom_lir(&entry);
            }
            else
            {
                nolock_stack_prune();
            }
        }
        else
        {
            nolock_stack_move_front(entry);
            nolock_queue_push_back(entry);
            nolock_stack_prune();
        }
        if (!wasInStack)
        {
            nolock_trim_stack_limit();
        }
    }
    void nolock_admit_new(Entry& entry) noexcept {
        if (lirSize < nolock_lir_capacity())
        {
            entry.isLir = true;
            ++lirSize;
            nolock_stack_push_front(entry);
        }
        else
        {
            entry.isLir = false;
            nolock_stack_push_front(entry);
            nolock_queue_push_back(entry);
        }
        if (lirSize == 0)
        {
            nolock_stack_prune();
        }
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
        nolock_trim_stack_limit();
    }
    [[nodiscard]] bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = map.find(key);
        if (mapItr == map.end() || mapItr->second == nullptr)
            return false;
        Entry* const entry = mapItr->second;
        if (entry->isLir)
        {
            assert(lirSize != 0);
            --lirSize;
        }
        nolock_destroy_entry(*entry);
        nolock_stack_prune();
        return true;
    }
    void nolock_clear() noexcept {
        for (auto& [key, entry] : map)
        {
            if (entry == nullptr)
                continue;
            pool.destroy(entry);
        }
        map.clear();
        stackS.clear();
        queueQ.clear();
        hirStack.clear();
        lirSize = 0;
    }
    template<typename... Args>
    Return nolock_insert_new_after_room(const Key& key, Args&&... args) noexcept {
        auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        if (!inserted)
            return nolock_write(key, std::forward<Args>(args)...);
        Entry* const entry = pool.create_object(key);
        entry->emplaceValue(std::forward<Args>(args)...);
        mapItr->second = entry;
        nolock_admit_new(*entry);
        return nolock_read(entry);
    }
    template<typename... Args>
    Return nolock_insert_new(const Key& key, Args&&... args) noexcept {
        if (!nolock_make_room())
            return Return{std::forward<Args>(args)...};
        return nolock_insert_new_after_room(key, std::forward<Args>(args)...);
    }
    template<typename... Args>
    inline Return nolock_reactivate_direct(Entry* const entry, Args&&... args) noexcept {
        if (!nolock_make_room())
            return Return{std::forward<Args>(args)...};
        const bool wasInStack = entry->inStack;
        entry->emplaceValue(std::forward<Args>(args)...);
        nolock_reactivate_hir(*entry, wasInStack);
        return nolock_read(entry);
    }
    template<typename... Args>
    Return nolock_reactivate(const Key& key, Args&&... args) noexcept {
        if (!nolock_make_room())
            return Return{std::forward<Args>(args)...};
        const auto mapItr = map.find(key);
        if (mapItr == map.end() || mapItr->second == nullptr)
            return nolock_insert_new_after_room(key, std::forward<Args>(args)...);
        Entry* const entry = mapItr->second;
        if (nolock_is_resident(entry))
        {
            if (entry->isLir)
                nolock_touch_lir(*entry);
            else
                nolock_touch_hir(*entry);
            return nolock_read(entry);
        }
        return nolock_reactivate_direct(entry, std::forward<Args>(args)...);
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
                if (entry->isLir)
                    nolock_touch_lir(*entry);
                else
                    nolock_touch_hir(*entry);
                return nolock_read(entry);
            }
            return nolock_reactivate_direct(entry, std::forward<Args>(args)...);
        }
        return nolock_insert_new(key, std::forward<Args>(args)...);
    }
    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward(builder)(std::forward<Args>(args)...)};
        const auto mapItr = map.find(key);
        if (mapItr != map.end() && mapItr->second != nullptr && nolock_is_resident(mapItr->second))
        {
            Entry* const entry = mapItr->second;
            if (entry->isLir)
                nolock_touch_lir(*entry);
            else
                nolock_touch_hir(*entry);
            return nolock_read(entry);
        }
        Value value = std::forward(builder)(std::forward<Args>(args)...);
        if (mapItr != map.end() && mapItr->second != nullptr)
        {
            return nolock_reactivate_direct(mapItr->second, std::move(value));
        }
        return nolock_insert_new_after_room(key, std::move(value));
    }
    const usize   defaultCapacity;
    const usize   reserveCount;
    const float   maxLoadFactor;
    mutable Mutex mutex;
    usize         capacity;
    usize         lirSize = 0;
    map_type      map;
    Pool<Entry>   pool;
    list_type     stackS;
    list_type     queueQ;
    list_type     hirStack;
};

}  // namespace DON

#endif  // UTILITY_LIRS_CACHE_H_INCLUDED
