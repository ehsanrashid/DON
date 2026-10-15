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

#ifndef UTILITY_UNORDERED_MAP_H_INCLUDED
#define UTILITY_UNORDERED_MAP_H_INCLUDED

#include <functional>  // std::hash<>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "../utility/access.h"
#include "../utility/types.h"

namespace DON {

template<typename Key,
         typename Value,
         typename Mutex     = std::shared_mutex,
         typename Hash      = std::hash<Key>,
         typename KeyEqual  = std::equal_to<Key>,
         typename Allocator = std::allocator<std::pair<const Key, Value>>>
class UnorderedMap final {
   public:
    using key_type        = Key;
    using mapped_type     = Value;
    using value_type      = std::pair<const Key, Value>;
    using size_type       = usize;
    using difference_type = std::ptrdiff_t;
    using hasher          = Hash;
    using key_equal       = KeyEqual;
    using allocator_type  = Allocator;

    using map_type          = std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>;
    using read_access_type  = ReadAccess<map_type, Mutex>;
    using write_access_type = WriteAccess<map_type, Mutex>;

    UnorderedMap() noexcept = default;

    explicit UnorderedMap(size_type        bucketCount,
                          const Hash&      hash      = Hash{},
                          const KeyEqual&  equal     = KeyEqual{},
                          const Allocator& allocator = Allocator{}) noexcept :
        map(bucketCount, hash, equal, allocator) {}

    explicit UnorderedMap(const Hash&      hash,
                          const KeyEqual&  equal     = KeyEqual{},
                          const Allocator& allocator = Allocator{}) noexcept :
        map(0, hash, equal, allocator) {}

    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.empty();
    }

    [[nodiscard]] size_type size() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.size();
    }

    [[nodiscard]] size_type max_size() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.max_size();
    }

    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        map.clear();
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.find(key) != map.end();
    }

    [[nodiscard]] size_type count(const Key& key) const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.count(key);
    }

    [[nodiscard]] hasher hash_function() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.hash_function();
    }

    [[nodiscard]] key_equal key_eq() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.key_eq();
    }

    [[nodiscard]] allocator_type get_allocator() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.get_allocator();
    }

    [[nodiscard]] size_type bucket_count() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.bucket_count();
    }

    [[nodiscard]] size_type max_bucket_count() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.max_bucket_count();
    }

    [[nodiscard]] float load_factor() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.load_factor();
    }

    [[nodiscard]] float max_load_factor() const noexcept {
        std::shared_lock sharedLock{mutex};

        return map.max_load_factor();
    }

    void max_load_factor(float factor) noexcept {
        std::lock_guard lockGuard{mutex};

        map.max_load_factor(factor);
    }

    void rehash(size_type count) noexcept {
        std::lock_guard lockGuard{mutex};

        map.rehash(count);
    }

    void reserve(size_type count) noexcept {
        std::lock_guard lockGuard{mutex};

        map.reserve(count);
    }

    [[nodiscard]] read_access_type read() const noexcept { return read_access_type{map, mutex}; }

    [[nodiscard]] write_access_type write() noexcept { return write_access_type{map, mutex}; }

   private:
    mutable Mutex mutex;
    map_type      map;
};

}  // namespace DON

#endif  // UTILITY_UNORDERED_MAP_H_INCLUDED
