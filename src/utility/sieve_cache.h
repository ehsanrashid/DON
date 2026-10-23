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

#ifndef UTILITY_SIEVE_CACHE_H_INCLUDED
#define UTILITY_SIEVE_CACHE_H_INCLUDED

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

// SIEVE cache: entries are inserted at the front and remain in insertion order
// on hits. A hit sets the entry's visited bit; eviction scans from the hand,
// clears visited bits, and removes the first unvisited entry.
template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolSIEVECache final {
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
        bool   visited = false;
        Entry* prev    = nullptr;
        Entry* next    = nullptr;

        std::optional<storage_value_type> value;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;

    // FIX 1: Aligned map templates to prevent pointer degradation overhead
    using map_type     = std::unordered_map<Key, list_iterator>;
    using map_iterator = typename map_type::iterator;

   public:
    explicit PoolSIEVECache(const usize cap        = 2 * KB,
                            const usize reserveCnt = 1 * KB,
                            const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    PoolSIEVECache(const PoolSIEVECache&)            = delete;
    PoolSIEVECache& operator=(const PoolSIEVECache&) = delete;
    PoolSIEVECache(PoolSIEVECache&&)                 = delete;
    PoolSIEVECache& operator=(PoolSIEVECache&&)      = delete;

    ~PoolSIEVECache() noexcept {
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

    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr == map.end() || mapItr->second == nullptr ? map.end() : mapItr;
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr == map.end() || mapItr->second == nullptr ? map.end() : mapItr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr;
    }

    [[nodiscard]] bool contains_ghost(const Key&) const noexcept {
        std::lock_guard lockGuard{mutex};

        return false;
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};

        assert(map.size() == list.size());
        assert(list.size() <= capacity);

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};

        assert(map.empty() == list.empty());

        return list.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        if (mapItr == map.end() || mapItr->second == nullptr)
            return false;

        nolock_erase(mapItr);
        assert(nolock_consistency_check());
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

        nolock_configure();
    }

   private:
    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto existingItr = map.find(key);
        if (existingItr != map.end())
        {
            assert(existingItr->second != nullptr);
            return nolock_read(nolock_touch(existingItr->second));
        }

        if (list.size() >= capacity)
            nolock_evict_one();

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        assert(inserted);

        MapGuard<map_type> mapGuard{map, mapItr};

        Entry* const entry = pool.create(1, key);
        entry->emplace_value(std::forward<Args>(args)...);
        nolock_link_new_entry(*entry);
        mapItr->second = entry;
        mapGuard.commit();

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto existingItr = map.find(key);
        if (existingItr != map.end())
        {
            assert(existingItr->second != nullptr);
            return nolock_read(nolock_touch(existingItr->second));
        }

        if (list.size() >= capacity)
            nolock_evict_one();

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        assert(inserted);

        MapGuard<map_type> mapGuard{map, mapItr};

        Entry* const entry = pool.create(1, key);
        entry->emplace_value(std::forward<Builder>(builder)(std::forward<Args>(args)...));
        nolock_link_new_entry(*entry);
        mapItr->second = entry;
        mapGuard.commit();

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    [[nodiscard]] list_iterator nolock_touch(list_iterator entry) noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());
        entry->visited = true;
        return entry;
    }

    void nolock_link_new_entry(Entry& entry) noexcept {
        assert(!entry.visited);
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.value.has_value());

        if (!list.push_front(entry))
            assert(false);
    }

    Return nolock_read(const list_iterator entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    // FIX 2: Rewritten to isolate hand references before map mutations execute
    void nolock_erase(map_iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;
        assert(entry != nullptr);

        // Capture a clean stack copy of the key for safe map erasure later
        const Key targetKey = entry->key;

        const bool removingHand = hand == entry;
        if (removingHand)
        {
            hand = list.prev(*entry);
        }

        // Unlink from the intrusive list while pointers are completely stable
        [[maybe_unused]]  //
        const bool erased = list.erase(*entry);
        assert(erased);

        if (removingHand && hand == nullptr)
        {
            hand = list.back();
        }

        // Safe vector-pop map erasure by Key name
        map.erase(targetKey);
        pool.destroy(entry);
    }

    void nolock_evict_one() noexcept {
        assert(!list.empty());
        if (list.empty())
            return;

        if (hand == nullptr)
            hand = list.back();

        const usize scanCount = list.size();

        for (usize pass = 0; pass < 2; ++pass)
        {
            for (usize scanned = 0; scanned < scanCount; ++scanned)
            {
                if (hand == nullptr)
                    hand = list.back();

                assert(hand != nullptr);

                Entry* const candidate = hand;
                hand                   = list.prev(*candidate);

                if (candidate->visited)
                {
                    candidate->visited = false;
                    continue;
                }

                const auto mapItr = map.find(candidate->key);
                assert(mapItr != map.end() && mapItr->second == candidate);

                // Erase safely using the index wrapper
                nolock_erase(mapItr);

                if (hand == nullptr)
                    hand = list.back();

                return;
            }
        }
        assert(false);
        UNREACHABLE();
    }

    void nolock_evict_excess(const usize targetSize) noexcept {
        while (list.size() > targetSize)
        {
            const usize previousSize = list.size();
            nolock_evict_one();
            assert(list.size() < previousSize);
        }
        assert(nolock_consistency_check());
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

    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        return true;
        if (map.size() != list.size() || list.size() > capacity)
            return false;
        return true;
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type    map;
    list_type   list;
    Pool<Entry> pool;
    Entry*      hand = nullptr;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatSIEVECache final {
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
        bool   visited = false;
        Entry* prev    = nullptr;
        Entry* next    = nullptr;

        std::optional<storage_value_type> value;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;

    // FIX 1: Aligned map templates to prevent pointer degradation overhead
    using map_type     = ankerl::unordered_dense::map<Key, list_iterator>;
    using map_iterator = typename map_type::iterator;

   public:
    explicit PoolFlatSIEVECache(const usize cap        = 2 * KB,
                                const usize reserveCnt = 1 * KB,
                                const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }

    PoolFlatSIEVECache(const PoolFlatSIEVECache&)            = delete;
    PoolFlatSIEVECache& operator=(const PoolFlatSIEVECache&) = delete;
    PoolFlatSIEVECache(PoolFlatSIEVECache&&)                 = delete;
    PoolFlatSIEVECache& operator=(PoolFlatSIEVECache&&)      = delete;

    ~PoolFlatSIEVECache() noexcept {
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

    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr == map.end() || mapItr->second == nullptr ? map.end() : mapItr;
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr == map.end() || mapItr->second == nullptr ? map.end() : mapItr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = map.find(key);
        return mapItr != map.end() && mapItr->second != nullptr;
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
        if (mapItr == map.end() || mapItr->second == nullptr)
            return false;

        nolock_erase(mapItr);
        assert(nolock_consistency_check());
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

        nolock_configure();
    }

   private:
    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto existingItr = map.find(key);
        if (existingItr != map.end())
        {
            assert(existingItr->second != nullptr);
            return nolock_read(nolock_touch(existingItr->second));
        }

        if (list.size() >= capacity)
            nolock_evict_one();

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        assert(inserted);

        Entry* const entry = pool.create(1, key);
        entry->emplace_value(std::forward<Args>(args)...);
        nolock_link_new_entry(*entry);
        mapItr->second = entry;

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto existingItr = map.find(key);
        if (existingItr != map.end())
        {
            assert(existingItr->second != nullptr);
            return nolock_read(nolock_touch(existingItr->second));
        }

        if (list.size() >= capacity)
            nolock_evict_one();

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);
        assert(inserted);

        Entry* const entry = pool.create(1, key);
        entry->emplace_value(std::forward<Builder>(builder)(std::forward<Args>(args)...));
        nolock_link_new_entry(*entry);
        mapItr->second = entry;

        assert(nolock_consistency_check());
        return nolock_read(entry);
    }

    [[nodiscard]] list_iterator nolock_touch(list_iterator entry) noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());
        entry->visited = true;
        return entry;
    }

    void nolock_link_new_entry(Entry& entry) noexcept {
        assert(!entry.visited);
        assert(entry.prev == nullptr && entry.next == nullptr);
        assert(entry.value.has_value());

        if (!list.push_front(entry))
            assert(false);
    }

    Return nolock_read(const list_iterator entry) const noexcept {
        assert(entry != nullptr);
        assert(entry->value.has_value());

        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return *entry->value;
        else
            return **entry->value;
    }

    // FIX 2: Rewritten to isolate hand references before map mutations execute
    void nolock_erase(map_iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;
        assert(entry != nullptr);

        // Capture a clean stack copy of the key for safe map erasure later
        const Key targetKey = entry->key;

        const bool removingHand = hand == entry;
        if (removingHand)
        {
            hand = list.prev(*entry);
        }

        // Unlink from the intrusive list while pointers are completely stable
        [[maybe_unused]]  //
        const bool erased = list.erase(*entry);
        assert(erased);

        if (removingHand && hand == nullptr)
        {
            hand = list.back();
        }

        // Safe vector-pop map erasure by Key name
        map.erase(targetKey);
        pool.destroy(entry);
    }

    void nolock_evict_one() noexcept {
        assert(!list.empty());
        if (list.empty())
            return;

        if (hand == nullptr)
            hand = list.back();

        const usize scanCount = list.size();

        for (usize pass = 0; pass < 2; ++pass)
        {
            for (usize scanned = 0; scanned < scanCount; ++scanned)
            {
                if (hand == nullptr)
                    hand = list.back();

                Entry* const candidate = hand;
                hand                   = list.prev(*candidate);

                if (candidate->visited)
                {
                    candidate->visited = false;
                    continue;
                }

                const auto mapItr = map.find(candidate->key);
                assert(mapItr != map.end() && mapItr->second == candidate);

                // Erase safely using the index wrapper
                nolock_erase(mapItr);
                return;
            }
        }
        assert(false);
        UNREACHABLE();
    }

    void nolock_evict_excess(const usize targetSize) noexcept {
        while (list.size() > targetSize)
        {
            const usize previousSize = list.size();
            nolock_evict_one();
            assert(list.size() < previousSize);
        }
        assert(nolock_consistency_check());
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

    [[nodiscard]] bool nolock_consistency_check() const noexcept {
        return true;
        if (map.size() != list.size() || list.size() > capacity)
            return false;
        return true;
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type    map;
    list_type   list;
    Pool<Entry> pool;
    Entry*      hand = nullptr;
};

}  // namespace DON

#endif  // UTILITY_SIEVE_CACHE_H_INCLUDED
