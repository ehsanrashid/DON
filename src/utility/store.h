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

#ifndef UTILITY_STORE_H_INCLUDED
#define UTILITY_STORE_H_INCLUDED

#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../utility/types.h"

namespace DON {

template<typename Key, typename Value, typename Return = Value, typename Mutex = std::shared_mutex>
class Store final {
    static constexpr usize ThresholdSize = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::shared_ptr<Value>>;

    using list_type     = std::list<storage_value_type>;
    using list_iterator = typename list_type::iterator;
    using map_type      = std::unordered_map<Key, list_iterator>;

   public:
    explicit Store(const usize reserveCnt = 1024, const float maxLoadFtr = 0.75F) :
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFtr) {
        std::lock_guard lockGuard{mutex};

        nolock_configure();
    }

    template<typename... Args>
    Return read_or_write(const Key& key, Args&&... args) {
        // Fast path: shared lock for an existing key.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto itr = nolock_find(key); itr != map.end())
                return nolock_read(*itr->second);
        }

        // Slow path: exclusive lock to insert a missing key.
        std::lock_guard lockGuard{mutex};

        return nolock_write(key, std::forward<Args>(args)...);
    }

    template<typename Builder, typename... Args>
    Return read_or_write_with(const Key& key, Builder&& builder, Args&&... args) {
        // Fast path: shared lock for an existing key.
        {
            std::shared_lock sharedLock{mutex};

            if (const auto itr = nolock_find(key); itr != map.end())
                return nolock_read(*itr->second);
        }

        // Slow path: exclusive lock to insert a missing key.
        std::lock_guard lockGuard{mutex};

        return nolock_write_with(key, std::forward<Builder>(builder), std::forward<Args>(args)...);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return nolock_find(key) != map.end();
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] usize size() const noexcept {
        std::shared_lock sharedLock{mutex};
        assert(map.size() == list.size());

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};
        assert(map.empty() == list.empty());

        return list.empty();
    }

    bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        const auto mapItr = nolock_find(key);

        if (mapItr == map.end())
            return false;

        const auto listItr = mapItr->second;

        map.erase(mapItr);
        list.erase(listItr);

        return true;
    }

    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void reset() {
        std::lock_guard lockGuard{mutex};

        nolock_clear();

        nolock_configure();
    }

   private:
    auto nolock_find(const Key& key) const { return map.find(key); }

    Return nolock_read(storage_value_type& value) const noexcept {
        if constexpr (sizeof(Value) <= ThresholdSize)
            return value;
        else
            return *value;
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        const auto [itr, inserted] = map.try_emplace(key, list.end());

        if (!inserted)
            return nolock_read(*itr->second);

        if constexpr (sizeof(Value) <= ThresholdSize)
            itr->second = list.emplace(list.end(), std::forward<Args>(args)...);
        else
            itr->second = list.emplace(list.end(), std::make_shared<Value>(  //
                                                     std::forward<Args>(args)...));

        return nolock_read(*itr->second);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        const auto [itr, inserted] = map.try_emplace(key, list.end());

        if (!inserted)
            return nolock_read(*itr->second);

        if constexpr (sizeof(Value) <= ThresholdSize)
            itr->second =
              list.emplace(list.end(), std::forward<Builder>(builder)(std::forward<Args>(args)...));
        else
            itr->second = list.emplace(list.end(), std::make_shared<Value>(std::forward<Builder>(
                                                     builder)(std::forward<Args>(args)...)));

        return nolock_read(*itr->second);
    }

    void nolock_clear() noexcept {
        map.clear();
        list.clear();
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    map_type      map;
    list_type     list;
};

}  // namespace DON

#endif  // UTILITY_STORE_H_INCLUDED
