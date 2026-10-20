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
#include <array>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../utility/list.h"
#include "../utility/pool.h"
#include "../utility/types.h"
#include "../utility/ankerl/unordered_dense.h"

namespace DON {

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class ListLRUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    // std::pmr::list<list_value_type> is a doubly linked list that uses a polymorphic memory resource for dynamic memory allocation.
    // header needed: <list>, <memory_resource>
    using list_value_type = std::pair<Key, storage_value_type>;
    using list_type       = std::pmr::list<list_value_type>;
    using list_iterator   = typename list_type::iterator;
    using list_pool_type  = std::pmr::unsynchronized_pool_resource;
    using map_type        = std::unordered_map<Key, list_iterator>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit ListLRUCache(const usize cap        = 2 * KB,
                          const usize reserveCnt = 1 * KB,
                          const float maxLoadFac = 0.75F) :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap),
        listPool(),
        list(&listPool) {
        configure();
    }
    ListLRUCache(const ListLRUCache&) noexcept            = delete;
    ListLRUCache& operator=(const ListLRUCache&) noexcept = delete;
    ListLRUCache(ListLRUCache&&) noexcept                 = delete;
    ListLRUCache& operator=(ListLRUCache&&) noexcept      = delete;

    ~ListLRUCache() noexcept {
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

    // The returned iterator does not retain the lock;
    // callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != nolock_map_end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == list.size());

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == list.empty());

        return list.empty();
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

        nolock_release_pool();

        map.rehash(0);

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_list_beg() noexcept { return list.begin(); }
    auto nolock_list_end() noexcept { return list.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nolock_list_end());

        list.splice(nolock_list_beg(), list, listItr);

        return listItr;
    }

    Return nolock_read(list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return listItr->second;
        else
            return *listItr->second;
    }

    template<typename... Args>
    static storage_value_type make_storage(Args&&... args) {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return Value(std::forward<Args>(args)...);
        else
            return std::make_shared<Value>(std::forward<Args>(args)...);
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        return nolock_insert(key, [&] { return make_storage(std::forward<Args>(args)...); });
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        return nolock_insert(key, [&] {
            return make_storage(std::forward<Builder>(builder)(std::forward<Args>(args)...));
        });
    }

    template<typename CreateStorage>
    Return nolock_insert(const Key& key, CreateStorage&& createStorage) noexcept {
        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard           mapGuard{map, mapItr};
        storage_value_type value = std::forward<CreateStorage>(createStorage)();

        // Reuse the LRU node to avoid allocating a list node on a full-cache miss.
        if (!list.empty() && list.size() >= capacity)
        {
            const auto listItr   = std::prev(nolock_list_end());
            const auto oldMapItr = nolock_find(listItr->first);
            assert(oldMapItr != nolock_map_end());

            map.erase(oldMapItr);
            list.splice(nolock_list_beg(), list, listItr);
            std::destroy_at(std::addressof(*listItr));
            new (static_cast<void*>(std::addressof(*listItr)))
              list_value_type(key, std::move(value));
            mapItr->second = listItr;
        }
        else
            mapItr->second = list.emplace(nolock_list_beg(), key, std::move(value));

        mapGuard.commit();
        nolock_evict_excess(capacity);

        return nolock_read(mapItr->second);
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        const auto listItr = mapItr->second;

        map.erase(mapItr);
        list.erase(listItr);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            const auto mapItr = nolock_find(list.back().first);
            assert(mapItr != nolock_map_end());

            nolock_erase(mapItr);
        }
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = nolock_find(key);

        if (mapItr == nolock_map_end())
            return false;

        nolock_erase(mapItr);

        return true;
    }

    void nolock_clear() noexcept {
        list.clear();
        map.clear();
    }

    void nolock_release_pool() noexcept { listPool.release(); }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type       map;
    list_pool_type listPool;
    list_type      list;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class NewDeleteLRUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry final {
       public:
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        Key                key;
        storage_value_type value;
        Entry*             prev = nullptr;
        Entry*             next = nullptr;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = std::unordered_map<Key, list_iterator>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit NewDeleteLRUCache(const usize cap        = 2 * KB,
                               const usize reserveCnt = 1 * KB,
                               const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }
    NewDeleteLRUCache(const NewDeleteLRUCache&) noexcept            = delete;
    NewDeleteLRUCache& operator=(const NewDeleteLRUCache&) noexcept = delete;
    NewDeleteLRUCache(NewDeleteLRUCache&&) noexcept                 = delete;
    NewDeleteLRUCache& operator=(NewDeleteLRUCache&&) noexcept      = delete;

    ~NewDeleteLRUCache() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
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

    // The returned iterator does not retain the lock;
    // callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != nolock_map_end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == list.size());

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == list.empty());

        return list.empty();
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

        map.rehash(0);

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        Entry* const entry = new Entry(key,  //
                                       std::forward<Args>(args)...);
        mapItr->second     = entry;

        [[maybe_unused]] const bool listInserted = list.push_front(*entry);
        assert(listInserted);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        Entry* const entry = new Entry(key,                    //
                                       std::forward<Builder>(  //
                                         builder)(std::forward<Args>(args)...));
        mapItr->second     = entry;

        [[maybe_unused]] const bool listInserted = list.push_front(*entry);
        assert(listInserted);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nullptr);

        list.move_front(*listItr);

        return listItr;
    }

    Return nolock_read(const list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return listItr->value;
        else
            return *listItr->value;
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;

        map.erase(mapItr);

        list.erase(*entry);

        delete entry;
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            Entry* const entry = list.back();
            assert(entry != nullptr);

            const auto mapItr = nolock_find(entry->key);
            assert(mapItr != nolock_map_end());

            nolock_erase(mapItr);
        }
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = nolock_find(key);

        if (mapItr == nolock_map_end())
            return false;

        nolock_erase(mapItr);

        return true;
    }

    void nolock_clear() noexcept {
        while (Entry* const entry = list.back())
        {
            list.erase(*entry);  // T&: requires an existing object

            delete entry;  // T*: pointer identifies the object to destroy
        }

        map.clear();
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity;

    map_type  map;
    list_type list;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolLRUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry final {
       public:
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        Key                key;
        storage_value_type value;
        Entry*             prev = nullptr;
        Entry*             next = nullptr;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = std::unordered_map<Key, list_iterator>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit PoolLRUCache(const usize cap        = 2 * KB,
                          const usize reserveCnt = 1 * KB,
                          const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }
    PoolLRUCache(const PoolLRUCache&) noexcept            = delete;
    PoolLRUCache& operator=(const PoolLRUCache&) noexcept = delete;
    PoolLRUCache(PoolLRUCache&&) noexcept                 = delete;
    PoolLRUCache& operator=(PoolLRUCache&&) noexcept      = delete;

    ~PoolLRUCache() noexcept {
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

    // The returned iterator does not retain the lock;
    // callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != nolock_map_end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == list.size());

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == list.empty());

        return list.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    // Retains pooled storage for reuse.
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();
    }

    // Releases pooled storage and restores the original configuration.
    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();

        nolock_release_pool();

        map.rehash(0);

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }

    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    [[nodiscard]] usize nolock_required_size() const noexcept {
        return usize{1} + (capacity > map.size() ? capacity - map.size() : usize{0});
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        const usize  requiredSize                = nolock_required_size();
        Entry* const entry                       = pool.create(requiredSize, key,  //
                                                               std::forward<Args>(args)...);
        mapItr->second                           = entry;
        [[maybe_unused]] const bool listInserted = list.push_front(*entry);
        assert(listInserted);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        const usize  requiredSize = nolock_required_size();
        Entry* const entry        = pool.create(requiredSize, key,      //
                                                std::forward<Builder>(  //
                                                  builder)(std::forward<Args>(args)...));
        mapItr->second            = entry;
        [[maybe_unused]] const bool listInserted = list.push_front(*entry);
        assert(listInserted);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nullptr);

        list.move_front(*listItr);

        return listItr;
    }

    Return nolock_read(const list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return listItr->value;
        else
            return *listItr->value;
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;

        map.erase(mapItr);

        list.erase(*entry);

        pool.destroy(entry);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            Entry* const entry = list.back();
            assert(entry != nullptr);

            const auto mapItr = nolock_find(entry->key);
            assert(mapItr != nolock_map_end());

            nolock_erase(mapItr);
        }
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = nolock_find(key);

        if (mapItr == nolock_map_end())
            return false;

        nolock_erase(mapItr);

        return true;
    }

    void nolock_clear() noexcept {
        while (Entry* const entry = list.back())
        {
            list.erase(*entry);  // T&: requires an existing object

            pool.destroy(entry);  // T*: pointer identifies the object to destroy
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

    map_type  map;
    list_type list;

    Pool<Entry> pool;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolFlatLRUCache final {
    static constexpr usize InlineValueThreshold = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= InlineValueThreshold, Value, std::shared_ptr<Value>>;

    struct Entry final {
       public:
        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= InlineValueThreshold)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        Key                key;
        storage_value_type value;
        Entry*             prev = nullptr;
        Entry*             next = nullptr;
    };

    using list_value_type = Entry;
    using list_type       = DoublyIntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = ankerl::unordered_dense::map<Key, list_iterator>;

    class MapGuard final {
       public:
        MapGuard(map_type& mapRef, typename map_type::iterator mapItr) noexcept :
            map(mapRef),
            iterator(mapItr) {}

        ~MapGuard() noexcept {
            if (active)
                map.erase(iterator);
        }

        void commit() noexcept { active = false; }

       private:
        map_type&                   map;
        typename map_type::iterator iterator;
        bool                        active = true;
    };

   public:
    explicit PoolFlatLRUCache(const usize cap        = 2 * KB,
                              const usize reserveCnt = 1 * KB,
                              const float maxLoadFac = 0.75F) noexcept :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        configure();
    }
    PoolFlatLRUCache(const PoolFlatLRUCache&) noexcept            = delete;
    PoolFlatLRUCache& operator=(const PoolFlatLRUCache&) noexcept = delete;
    PoolFlatLRUCache(PoolFlatLRUCache&&) noexcept                 = delete;
    PoolFlatLRUCache& operator=(PoolFlatLRUCache&&) noexcept      = delete;

    ~PoolFlatLRUCache() noexcept {
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

    // The returned iterator does not retain the lock;
    // callers must prevent concurrent mutation while using it.
    [[nodiscard]] auto find(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] auto find(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key);
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_find(key) != nolock_map_end();
    }

    [[nodiscard]] usize size() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.size() == list.size());

        return list.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard lockGuard{mutex};
        assert(map.empty() == list.empty());

        return list.empty();
    }

    [[nodiscard]] bool remove(const Key& key) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_remove(key);
    }

    // Retains pooled storage for reuse.
    void clear() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();
    }

    void configure() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_configure();
    }

    // Releases pooled storage and restores the original configuration.
    void reset() noexcept {
        std::lock_guard lockGuard{mutex};

        nolock_clear();

        nolock_release_pool();

        map.rehash(0);

        capacity = defaultCapacity;

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }

    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    [[nodiscard]] usize nolock_required_size() const noexcept {
        return usize{1} + (capacity > map.size() ? capacity - map.size() : usize{0});
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        const usize  requiredSize = nolock_required_size();
        Entry* const entry        = pool.create(requiredSize, key,  //
                                                std::forward<Args>(args)...);
        mapItr->second            = entry;

        [[maybe_unused]] const bool listInserted = list.push_front(*entry);
        assert(listInserted);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        const usize requiredSize = nolock_required_size();

        Entry* const entry = pool.create(requiredSize, key,      //
                                         std::forward<Builder>(  //
                                           builder)(std::forward<Args>(args)...));
        mapItr->second     = entry;

        [[maybe_unused]] const bool listInserted = list.push_front(*entry);
        assert(listInserted);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nullptr);

        list.move_front(*listItr);

        return listItr;
    }

    Return nolock_read(const list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= InlineValueThreshold)
            return listItr->value;
        else
            return *listItr->value;
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;

        map.erase(mapItr);

        list.erase(*entry);

        pool.destroy(entry);
    }

    void nolock_evict_excess(const usize size) noexcept {
        while (list.size() > size)
        {
            Entry* const entry = list.back();
            assert(entry != nullptr);

            const auto mapItr = nolock_find(entry->key);
            assert(mapItr != nolock_map_end());

            nolock_erase(mapItr);
        }
    }

    bool nolock_remove(const Key& key) noexcept {
        const auto mapItr = nolock_find(key);

        if (mapItr == nolock_map_end())
            return false;

        nolock_erase(mapItr);

        return true;
    }

    void nolock_clear() noexcept {
        while (Entry* const entry = list.back())
        {
            list.erase(*entry);  // T&: requires an existing object

            pool.destroy(entry);  // T*: pointer identifies the object to destroy
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

    map_type  map;
    list_type list;

    Pool<Entry> pool;
};

}  // namespace DON

#endif  // UTILITY_LRU_CACHE_H_INCLUDED
