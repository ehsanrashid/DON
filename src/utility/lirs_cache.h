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

// OPTIMIZED LIRS Cache: Low-In-Recency-Set eviction strategy
//
// Uses SIEVE-style hand pointer for efficient recency tracking:
// - Single hand pointer scans list backward without moving entries
// - Accessed flag distinguishes LIR (accessed) vs HIR (unaccessed) entries
// - On hit: set accessed = true (no list reorganization needed)
// - On eviction: scan from hand backward, clear and evict unvisited HIR entries
// - Hand cursor preserves position for O(1) amortized eviction overhead
template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolLIRSCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

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
        bool   accessed = false;
        Entry* prev     = nullptr;
        Entry* next     = nullptr;

        std::optional<storage_value_type> value;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = std::unordered_map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

   public:
    explicit PoolLIRSCache(const usize cap        = 2 * KB,
                           const usize reserveCnt = 1 * KB,
                           const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap),
        hand(nullptr) {
        configure();
    }

    PoolLIRSCache(const PoolLIRSCache&)            = delete;
    PoolLIRSCache& operator=(const PoolLIRSCache&) = delete;
    PoolLIRSCache(PoolLIRSCache&&)                 = delete;
    PoolLIRSCache& operator=(PoolLIRSCache&&)      = delete;

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

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr
            && nolock_is_resident(mapItr->second);
    }

    [[nodiscard]] bool contains_ghost(const Key&) const noexcept {
        std::lock_guard lockGuard{mutex};

        return false;
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};

        return list.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        if (UNLIKELY(mapItr == map.end() || mapItr->second == nullptr))
            return false;

        nolock_erase(mapItr);
        return true;
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

        hand = nullptr;

        nolock_configure();
    }

   private:
    [[nodiscard]] bool nolock_is_resident(const Entry* entry) const noexcept {
        return entry != nullptr && entry->value.has_value();
    }

    ALWAYS_INLINE Return nolock_read(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (UNLIKELY(capacity == 0))
            return Return{std::forward<Args>(args)...};

        const auto existingItr = map.find(key);
        if (LIKELY(existingItr != map.end()))
        {
            assert(existingItr->second != nullptr);
            Entry* const entry = existingItr->second;

            // Hot path: just mark accessed, no list reorganization
            entry->accessed = true;

            return nolock_read(entry);
        }

        if (UNLIKELY(list.size() >= capacity))
            nolock_evict_one();

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        assert(inserted);

        MapGuard<map_type> mapGuard{map, mapItr};

        Entry* const entry = pool.create_object(key);

        if (UNLIKELY(entry == nullptr))
            return Return{std::forward<Args>(args)...};

        entry->emplace_value(std::forward<Args>(args)...);

        // New entries inserted at front (most recent)
        [[maybe_unused]]  //
        const bool linked = list.push_front(*entry);
        assert(linked);

        mapItr->second = entry;

        mapGuard.commit();

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (UNLIKELY(capacity == 0))
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto existingItr = map.find(key);
        if (LIKELY(existingItr != map.end()))
        {
            assert(existingItr->second != nullptr);
            Entry* const entry = existingItr->second;

            // Hot path: just mark accessed, no list reorganization
            entry->accessed = true;

            return nolock_read(entry);
        }

        if (UNLIKELY(list.size() >= capacity))
            nolock_evict_one();

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        assert(inserted);

        MapGuard<map_type> mapGuard{map, mapItr};

        Entry* const entry = pool.create_object(key);

        if (UNLIKELY(entry == nullptr))
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        entry->emplace_value(std::forward<Builder>(builder)(std::forward<Args>(args)...));

        [[maybe_unused]]  //
        const bool linked = list.push_front(*entry);
        assert(linked);

        mapItr->second = entry;

        mapGuard.commit();

        return nolock_read(entry);
    }

    void nolock_erase(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());

        Entry* const entry = mapItr->second;
        assert(entry != nullptr);

        // Update hand if we're erasing it
        if (hand == entry)
            hand = list.prev(*entry);

        [[maybe_unused]]  //
        const bool erased = list.erase(*entry);
        assert(erased);

        map.erase(mapItr);

        pool.destroy(entry);

        // Restore hand if needed
        if (hand == nullptr)
            hand = list.back();
    }

    // SIEVE-style hand-pointer eviction
    void nolock_evict_one() noexcept {
        assert(!list.empty());
        if (UNLIKELY(list.empty()))
            return;

        if (hand == nullptr)
            hand = list.back();

        const usize listSize = list.size();

        for (usize pass = 0; pass < 2; ++pass)
        {
            for (usize scanned = 0; scanned < listSize; ++scanned)
            {
                if (hand == nullptr)
                    hand = list.back();

                assert(hand != nullptr);

                list_iterator const candidate = hand;
                hand                          = list.prev(*candidate);

                // Unaccessed entry (HIR) -> evict it
                if (!candidate->accessed)
                {
                    const auto mapItr = map.find(candidate->key);
                    assert(mapItr != map.end() && mapItr->second == candidate);

                    nolock_erase(mapItr);

                    if (hand == nullptr)
                        hand = list.back();

                    return;
                }

                // Mark as unaccessed for second pass (demote LIR to HIR)
                candidate->accessed = false;
            }
        }

        assert(false);
        UNREACHABLE();
    }

    void nolock_evict_excess(const usize targetSize) noexcept {
        while (UNLIKELY(list.size() > targetSize))
            nolock_evict_one();
    }

    void nolock_clear() noexcept {
        hand = nullptr;
        while (Entry* const entry = list.back())
        {
            [[maybe_unused]]  //
            const bool erased = list.erase(*entry);
            assert(erased);
            pool.destroy(entry);
        }

        map.clear();
    }

    void nolock_release_pool() noexcept { pool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;
    list_type     list;
    map_type      map;
    Pool<Entry>   pool;
    list_iterator hand;
};

// OPTIMIZED LIRS Cache using dense map for better cache locality
template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatLIRSCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

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
        bool   accessed = false;
        Entry* prev     = nullptr;
        Entry* next     = nullptr;

        std::optional<storage_value_type> value;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = ankerl::unordered_dense::map<Key, list_iterator>;
    using map_iterator    = typename map_type::iterator;

   public:
    explicit PoolFlatLIRSCache(const usize cap        = 2 * KB,
                               const usize reserveCnt = 1 * KB,
                               const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap),
        hand(nullptr) {
        configure();
    }

    PoolFlatLIRSCache(const PoolFlatLIRSCache&)            = delete;
    PoolFlatLIRSCache& operator=(const PoolFlatLIRSCache&) = delete;
    PoolFlatLIRSCache(PoolFlatLIRSCache&&)                 = delete;
    PoolFlatLIRSCache& operator=(PoolFlatLIRSCache&&)      = delete;

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

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr
            && nolock_is_resident(mapItr->second);
    }

    [[nodiscard]] bool contains_ghost(const Key&) const noexcept {
        std::lock_guard lockGuard{mutex};

        return false;
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};

        return list.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        if (UNLIKELY(mapItr == map.end() || mapItr->second == nullptr))
            return false;

        nolock_erase(mapItr);
        return true;
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

        map.clear();

        capacity = defaultCapacity;

        hand = nullptr;

        nolock_configure();
    }

   private:
    [[nodiscard]] bool nolock_is_resident(const Entry* entry) const noexcept {
        return entry != nullptr && entry->value.has_value();
    }

    ALWAYS_INLINE Return nolock_read(const Entry* entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (UNLIKELY(capacity == 0))
            return Return{std::forward<Args>(args)...};

        const auto existingItr = map.find(key);
        if (LIKELY(existingItr != map.end()))
        {
            assert(existingItr->second != nullptr);
            Entry* const entry = existingItr->second;

            entry->accessed = true;

            return nolock_read(entry);
        }

        if (UNLIKELY(list.size() >= capacity))
            nolock_evict_one();

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        assert(inserted);

        MapGuard<map_type> mapGuard{map, mapItr};

        Entry* const entry = pool.create_object(key);

        if (UNLIKELY(entry == nullptr))
            return Return{std::forward<Args>(args)...};

        entry->emplace_value(std::forward<Args>(args)...);

        [[maybe_unused]]  //
        const bool linked = list.push_front(*entry);
        assert(linked);

        mapItr->second = entry;

        mapGuard.commit();

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (UNLIKELY(capacity == 0))
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto existingItr = map.find(key);
        if (LIKELY(existingItr != map.end()))
        {
            assert(existingItr->second != nullptr);
            Entry* const entry = existingItr->second;

            entry->accessed = true;

            return nolock_read(entry);
        }

        if (UNLIKELY(list.size() >= capacity))
            nolock_evict_one();

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        assert(inserted);

        MapGuard<map_type> mapGuard{map, mapItr};

        Entry* const entry = pool.create_object(key);

        if (UNLIKELY(entry == nullptr))
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        entry->emplace_value(std::forward<Builder>(builder)(std::forward<Args>(args)...));

        [[maybe_unused]]  //
        const bool linked = list.push_front(*entry);
        assert(linked);

        mapItr->second = entry;

        mapGuard.commit();

        return nolock_read(entry);
    }

    void nolock_erase(map_iterator mapItr) noexcept {
        assert(mapItr != map.end());

        Entry* const entry = mapItr->second;
        assert(entry != nullptr);

        // Update hand if we're erasing it
        if (hand == entry)
            hand = list.prev(*entry);

        [[maybe_unused]]  //
        const bool erased = list.erase(*entry);
        assert(erased);

        map.erase(mapItr);

        pool.destroy(entry);

        // Restore hand if needed
        if (hand == nullptr)
            hand = list.back();
    }

    void nolock_evict_one() noexcept {
        assert(!list.empty());
        if (UNLIKELY(list.empty()))
            return;

        if (hand == nullptr)
            hand = list.back();

        const usize listSize = list.size();

        for (usize pass = 0; pass < 2; ++pass)
        {
            for (usize scanned = 0; scanned < listSize; ++scanned)
            {
                if (hand == nullptr)
                    hand = list.back();

                assert(hand != nullptr);

                list_iterator const candidate = hand;
                hand                          = list.prev(*candidate);

                // Unaccessed entry (HIR) -> evict it
                if (!candidate->accessed)
                {
                    const auto mapItr = map.find(candidate->key);
                    assert(mapItr != map.end() && mapItr->second == candidate);

                    nolock_erase(mapItr);

                    if (hand == nullptr)
                        hand = list.back();

                    return;
                }

                // Mark as unaccessed for second pass (demote LIR to HIR)
                candidate->accessed = false;
            }
        }
        assert(false);
        UNREACHABLE();
    }

    void nolock_evict_excess(const usize targetSize) noexcept {
        while (UNLIKELY(list.size() > targetSize))
            nolock_evict_one();
    }

    void nolock_clear() noexcept {
        hand = nullptr;
        while (Entry* const entry = list.back())
        {
            [[maybe_unused]]  //
            const bool erased = list.erase(*entry);
            assert(erased);
            pool.destroy(entry);
        }

        map.clear();
    }

    void nolock_release_pool() noexcept { pool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;
    list_type     list;
    map_type      map;
    Pool<Entry>   pool;
    list_iterator hand;
};

}  // namespace DON

#endif  // UTILITY_LIRS_CACHE_H_INCLUDED
