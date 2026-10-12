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

#ifndef LRU_CACHE_H_INCLUDED
#define LRU_CACHE_H_INCLUDED

#include <list>           // list<>
#include <memory>         // shared_ptr<>
#include <mutex>          // mutex, lock_guard<>
#include <unordered_map>  // unordered_map<>
#include <utility>        // move()

#include "misc.h"

namespace DON {

template<class Key, class Value>
class LRUCache final {
   public:
    using ValuePtr = std::shared_ptr<Value>;

    explicit LRUCache(usize capacity = 128) noexcept :
        capacity_(capacity) {}

    [[nodiscard]] ValuePtr insert(Key key, ValuePtr value) noexcept {
        if (!value)
            return nullptr;

        std::lock_guard lock(mutex);

        if (capacity_ == 0)
            return value;

        if (const auto mapItr = indexMap.find(key); mapItr != indexMap.end())
        {
            mapItr->second->value = std::move(value);

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

        ++mapItr->second->pins;
        list.splice(list.begin(), list, mapItr->second);

        return mapItr->second->value;
    }

    [[nodiscard]] bool unpin(const Key& key, const ValuePtr& value) noexcept {
        std::lock_guard lock(mutex);

        if (const auto mapItr = indexMap.find(key); mapItr != indexMap.end())
        {
            auto& entry = *mapItr->second;

            if (entry.value == value && entry.pins != 0)
            {
                --entry.pins;

                return true;
            }
        }

        for (auto listItr = retiredList.begin(); listItr != retiredList.end(); ++listItr)
        {
            assert(listItr->pins != 0);

            if (listItr->key != key || listItr->value != value)
                continue;

            if (--listItr->pins == 0)
                retiredList.erase(listItr);

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
        Entry(Key key_, ValuePtr value_, usize pins_ = 0) noexcept :
            key(std::move(key_)),
            value(std::move(value_)),
            pins(pins_) {}

        Key      key;
        ValuePtr value;
        usize    pins;
    };

    using List        = std::list<Entry>;
    using PendingPins = std::list<Entry>;
    using ListItr     = typename List::iterator;
    using IndexMap    = std::unordered_map<Key, ListItr>;
    using IndexMapItr = typename IndexMap::iterator;

    // Requires mutex to be locked.
    bool nolock_erase(const Key& key) noexcept {
        const auto mapItr = indexMap.find(key);

        if (mapItr == indexMap.end())
            return false;

        nolock_remove_entry(mapItr);
        return true;
    }

    // Requires mutex to be locked.
    void nolock_remove_entry(IndexMapItr mapItr) noexcept {
        const auto listItr = mapItr->second;

        if (listItr->pins != 0)
            retiredList.splice(retiredList.end(), list, listItr);
        else
            list.erase(listItr);

        indexMap.erase(mapItr);
    }

    // Requires mutex to be locked.
    void nolock_evict_excess() noexcept { nolock_evict_excess(capacity_); }

    // Requires mutex to be locked.
    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            auto listItr = list.end();

            // Search from least recently used to most recently used.
            for (auto current = list.end(); current-- != list.begin();)
                if (current->pins == 0)
                {
                    listItr = current;
                    break;
                }

            // All entries are pinned; capacity may be exceeded temporarily.
            if (listItr == list.end())
                break;

            nolock_erase(listItr->key);
        }
    }

    // Requires mutex to be locked.
    void nolock_retire_entry(ListItr itr) noexcept {
        if (itr->pins != 0)
            retiredList.splice(retiredList.end(), list, itr);
        else
            list.erase(itr);
    }

    mutable std::mutex mutex;

    usize    capacity_;
    List     list;
    IndexMap indexMap;
    List     retiredList;
};

}  // namespace DON

#endif  // LRU_CACHE_H_INCLUDED
