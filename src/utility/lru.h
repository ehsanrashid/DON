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

#ifndef UTILITY_LRU_CACHE_H_INCLUDED
#define UTILITY_LRU_CACHE_H_INCLUDED

#include <cassert>
#include <list>           // list<>
#include <memory>         // shared_ptr<>
#include <mutex>          // mutex, lock_guard<>
#include <unordered_map>  // unordered_map<>, unordered_multimap<>
#include <utility>        // move()

#include "../utility/types.h"

namespace DON {

// A least-recently-used container that supports pinning entries to prevent eviction.
// When capacity is reached, the least recently used unpinned entry is evicted.
// Pinned entries are protected from eviction until they are unpinned.
template<class Key, class Value>
class PinnedLRU final {
   public:
    using ValuePtr = std::shared_ptr<Value>;

    explicit PinnedLRU(usize capacity = 128) noexcept :
        capacity_(capacity) {}

    [[nodiscard]] ValuePtr insert(Key key, ValuePtr value) noexcept {
        if (!value)
            return nullptr;

        std::lock_guard lock(mutex);

        if (capacity_ == 0)
            return value;

        if (const auto mapItr = indexMap.find(key); mapItr != indexMap.end())
        {
            const auto listItr = mapItr->second;

            if (listItr->pinCount == 0)
                listItr->value = std::move(value);
            else
            {
                const auto retiredKey = std::make_pair(listItr->key, listItr->value);

                retiredPinIndexMultiMap.emplace(retiredKey, listItr);
                retiredPinList.splice(retiredPinList.end(), list, listItr);

                list.push_front({std::move(key), std::move(value)});
                mapItr->second = list.begin();
            }

            list.splice(list.begin(), list, mapItr->second);

            return mapItr->second->value;
        }

        list.push_front({std::move(key), std::move(value)});

        indexMap.emplace(list.front().key, list.begin());

        nolock_evict_excess();

        return list.front().value;
    }

    [[nodiscard]] bool erase(const Key& key) noexcept {
        std::lock_guard lock(mutex);

        return nolock_erase(key);
    }

    [[nodiscard]] ValuePtr lookup(const Key& key) noexcept {
        std::lock_guard lock(mutex);

        const auto mapItr = indexMap.find(key);

        if (mapItr == indexMap.end())
            return nullptr;

        list.splice(list.begin(), list, mapItr->second);

        return mapItr->second->value;
    }

    [[nodiscard]] ValuePtr lookup_and_pin(const Key& key) noexcept {
        std::lock_guard lock(mutex);

        const auto mapItr = indexMap.find(key);

        if (mapItr == indexMap.end())
            return nullptr;

        ++mapItr->second->pinCount;
        list.splice(list.begin(), list, mapItr->second);

        return mapItr->second->value;
    }

    [[nodiscard]] bool unpin(const Key& key, const ValuePtr& value) noexcept {
        std::lock_guard lock(mutex);

        if (const auto mapItr = indexMap.find(key); mapItr != indexMap.end())
        {
            auto& entry = *mapItr->second;

            if (entry.value == value && entry.pinCount != 0)
            {
                --entry.pinCount;
                nolock_evict_excess();

                return true;
            }
        }

        const auto retiredKey       = std::make_pair(key, value);
        const auto [begItr, endItr] = retiredPinIndexMultiMap.equal_range(retiredKey);

        for (auto retiredPinMultiMapItr = begItr; retiredPinMultiMapItr != endItr;
             ++retiredPinMultiMapItr)
        {
            const auto retiredPinListItr = retiredPinMultiMapItr->second;

            assert(retiredPinListItr->pinCount != 0);

            if (--retiredPinListItr->pinCount == 0)
            {
                retiredPinList.erase(retiredPinListItr);
                retiredPinIndexMultiMap.erase(retiredPinMultiMapItr);
            }

            return true;
        }

        return false;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lock(mutex);

        return indexMap.find(key) != indexMap.end();
    }

    void set_capacity(usize newCapacity) noexcept {
        std::lock_guard lock(mutex);

        capacity_ = newCapacity;
        nolock_evict_excess();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lock(mutex);

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lock(mutex);

        return list.empty();
    }

    [[nodiscard]] usize capacity() const noexcept {
        std::lock_guard lock(mutex);

        return capacity_;
    }

   private:
    struct Entry final {
       public:
        Entry(Key k, ValuePtr v, usize pinCnt = 0) noexcept :
            key(std::move(k)),
            value(std::move(v)),
            pinCount(pinCnt) {}

        Key      key;
        ValuePtr value;
        usize    pinCount;
    };

    struct KeyValueHash final {
       public:
        usize operator()(const std::pair<Key, ValuePtr>& key) const noexcept {
            const usize keyHash   = std::hash<Key>{}(key.first);
            const usize valueHash = std::hash<Value*>{}(key.second.get());

            return keyHash ^ (valueHash + 0x9e3779b9 + (keyHash << 6) + (keyHash >> 2));
        }
    };

    using List        = std::list<Entry>;
    using ListItr     = typename List::iterator;
    using IndexMap    = std::unordered_map<Key, ListItr>;
    using IndexMapItr = typename IndexMap::iterator;

    using HashValueIndexMultiMap =
      std::unordered_multimap<std::pair<Key, ValuePtr>, ListItr, KeyValueHash>;

    // Requires mutex to be locked.
    bool nolock_erase(const Key& key) noexcept {
        const auto mapItr = indexMap.find(key);

        if (mapItr == indexMap.end())
            return false;

        nolock_erase(mapItr);
        return true;
    }

    // Requires mutex to be locked.
    void nolock_erase(IndexMapItr mapItr) noexcept {
        const auto listItr = mapItr->second;

        if (listItr->pinCount == 0)
            list.erase(listItr);
        else
        {
            const auto retiredKey = std::make_pair(listItr->key, listItr->value);

            retiredPinIndexMultiMap.emplace(retiredKey, listItr);
            retiredPinList.splice(retiredPinList.end(), list, listItr);
        }

        indexMap.erase(mapItr);
    }

    // Requires mutex to be locked.
    void nolock_evict_excess() noexcept { nolock_evict_excess(capacity_); }

    // Requires mutex to be locked.
    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            auto removeListItr = list.end();

            // Search from least recently used to most recently used.
            for (auto listItr = list.end(); listItr-- != list.begin();)
                if (listItr->pinCount == 0)
                {
                    removeListItr = listItr;
                    break;
                }

            // All entries are pinned; capacity may be exceeded temporarily.
            if (removeListItr == list.end())
                break;

            nolock_erase(removeListItr->key);
        }
    }

    mutable std::mutex mutex;

    usize    capacity_;
    List     list;
    IndexMap indexMap;

    List                   retiredPinList;
    HashValueIndexMultiMap retiredPinIndexMultiMap;
};

}  // namespace DON

#endif  // UTILITY_LRU_CACHE_H_INCLUDED
