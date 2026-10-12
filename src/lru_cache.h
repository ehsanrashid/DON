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

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

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

        if (const auto iter = indexMap.find(key); iter != indexMap.end())
        {
            iter->second->value = std::move(value);
            list.splice(list.begin(), list, iter->second);
            return iter->second->value;
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

        const auto iter = indexMap.find(key);

        if (iter == indexMap.end())
            return nullptr;

        list.splice(list.begin(), list, iter->second);
        return iter->second->value;
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
        Key      key;
        ValuePtr value;
    };

    using List     = std::list<Entry>;
    using ListIter = typename List::iterator;
    using IndexMap = std::unordered_map<Key, ListIter>;

    // Requires mutex to be locked.
    bool nolock_erase(const Key& key) noexcept {
        const auto iter = indexMap.find(key);

        if (iter == indexMap.end())
            return false;

        list.erase(iter->second);
        indexMap.erase(iter);
        return true;
    }

    // Requires mutex to be locked.
    void nolock_evict_excess() noexcept { nolock_evict_excess(capacity_); }

    // Requires mutex to be locked.
    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
            nolock_erase(list.back().key);
    }

    mutable std::mutex mutex;

    usize    capacity_;
    List     list;
    IndexMap indexMap;
};

}  // namespace DON

#endif  // LRU_CACHE_H_INCLUDED
