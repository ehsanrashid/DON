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
#include <functional>
//#include <iostream>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../utility/list.h"
#include "../utility/static_vector.h"
#include "../utility/types.h"

namespace DON {

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class ListLRUCache final {
    static constexpr usize ThresholdSize = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::shared_ptr<Value>>;

    using list_value_type = std::pair<Key, storage_value_type>;
    using list_type       = std::list<list_value_type>;
    using list_iterator   = typename list_type::iterator;
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
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac) {
        set_capacity(cap);
        configure();
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

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;
        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock; callers must prevent concurrent mutation
    // while using it.
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

        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_list_end() noexcept { return list.end(); }

    auto nolock_list_pos() noexcept { return list.begin(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nolock_list_end());

        list.splice(nolock_list_pos(), list, listItr);

        return listItr;
    }

    Return nolock_read(list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= ThresholdSize)
            return listItr->second;
        else
            return *listItr->second;
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        if constexpr (sizeof(Value) <= ThresholdSize)
            mapItr->second = list.emplace(nolock_list_pos(),  //
                                          key,                //
                                          std::forward<Args>(args)...);
        else
            mapItr->second = list.emplace(nolock_list_pos(),        //
                                          key,                      //
                                          std::make_shared<Value>(  //
                                            std::forward<Args>(args)...));
        mapGuard.commit();
        nolock_evict_excess(capacity);

        return nolock_read(mapItr->second);
    }

    template<typename Builder, typename... Args>
    Return nolock_write_with(const Key& key, Builder&& builder, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Builder>(builder)(std::forward<Args>(args)...)};

        const auto [mapItr, inserted] = map.try_emplace(key, nolock_list_end());

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        if constexpr (sizeof(Value) <= ThresholdSize)
            mapItr->second = list.emplace(nolock_list_pos(),      //
                                          key,                    //
                                          std::forward<Builder>(  //
                                            builder)(std::forward<Args>(args)...));
        else
            mapItr->second = list.emplace(nolock_list_pos(),        //
                                          key,                      //
                                          std::make_shared<Value>(  //
                                            std::forward<Builder>(  //
                                              builder)(std::forward<Args>(args)...)));
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
            const auto mapItr = nolock_find(std::prev(nolock_list_end())->first);
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
    usize         capacity;
    map_type      map;
    list_type     list;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class IntrusiveLRUCache final {
    static constexpr usize ThresholdSize = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::shared_ptr<Value>>;

    struct Entry {
        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) {
            if constexpr (sizeof(Value) <= ThresholdSize)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        Key                key;
        storage_value_type value;
        Entry*             prev = nullptr;
        Entry*             next = nullptr;
    };

    using list_value_type = Entry;
    using list_type       = IntrusiveList<list_value_type>;
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
    explicit IntrusiveLRUCache(const usize cap        = 2 * KB,
                               const usize reserveCnt = 1 * KB,
                               const float maxLoadFac = 0.75F) noexcept :
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac) {
        set_capacity(cap);
        configure();
    }

    IntrusiveLRUCache(const IntrusiveLRUCache&) noexcept            = delete;
    IntrusiveLRUCache& operator=(const IntrusiveLRUCache&) noexcept = delete;

    ~IntrusiveLRUCache() noexcept { clear(); }

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

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;
        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock; callers must prevent concurrent mutation
    // while using it.
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
        nolock_configure();
    }

   private:
    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }
    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nullptr);

        (void) list.move_front(*listItr);

        return listItr;
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        Entry* const entry = new Entry(key, std::forward<Args>(args)...);

        mapItr->second = entry;

        [[maybe_unused]] const bool entryInserted = list.push_front(*entry);
        assert(entryInserted);

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

        Entry* const entry =
          new Entry(key, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        mapItr->second = entry;
        (void) list.push_front(*entry);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    Return nolock_read(const list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= ThresholdSize)
            return listItr->value;
        else
            return *listItr->value;
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;

        map.erase(mapItr);
        (void) list.erase(*entry);

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
            (void) list.erase(*entry);
            delete entry;
        }

        map.clear();
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity = 0;
    map_type      map;
    list_type     list;
};

template<typename Key, typename Value, typename Mutex = std::mutex, typename Return = Value>
class PoolLRUCache final {
    static constexpr usize ThresholdSize = 128;

    using storage_value_type =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::shared_ptr<Value>>;

    struct Entry {
       public:
        template<typename... Args>
        explicit Entry(const Key& k, Args&&... args) noexcept :
            key(k),
            value(make_storage(std::forward<Args>(args)...)) {}

        template<typename... Args>
        static storage_value_type make_storage(Args&&... args) noexcept {
            if constexpr (sizeof(Value) <= ThresholdSize)
                return Value(std::forward<Args>(args)...);
            else
                return std::make_shared<Value>(std::forward<Args>(args)...);
        }

        Key                key;
        storage_value_type value;
        Entry*             prev = nullptr;
        Entry*             next = nullptr;
    };

    struct Slot {
       public:
        alignas(Entry) std::byte storage[sizeof(Entry)];
        Slot* next = nullptr;
    };

    struct Block {
       public:
        std::unique_ptr<Slot[]> slots;
        usize                   size;
    };

    using list_value_type = Entry;
    using list_type       = IntrusiveList<list_value_type>;
    using list_iterator   = list_value_type*;
    using map_type        = std::unordered_map<Key, list_iterator>;

    static constexpr usize BaseBlockSize = []() constexpr {
        constexpr usize TargetBytes = 64 * KB;

        const usize count = TargetBytes / sizeof(Slot);
        usize       size  = 1;

        while (size <= count / 2)
            size *= 2;

        return size;
    }();

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
                          const float maxLoadFac = 0.75F) :
        defaultCapacity(cap),
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac),
        capacity(cap) {
        nolock_configure();
        // std::cout << "sizeof(Entry): " << sizeof(Entry) << '\n'
        //           << "sizeof(Slot): " << sizeof(Slot) << '\n'
        //           << "BaseBlockSize: " << BaseBlockSize << '\n'
        //           << "Block bytes: " << BaseBlockSize * sizeof(Slot) << '\n';
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

    void set_capacity(const usize size) noexcept {
        std::lock_guard lockGuard{mutex};

        capacity = size;
        nolock_evict_excess(capacity);
    }

    // The returned iterator does not retain the lock; callers must prevent concurrent
    // mutation while using it.
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
    [[nodiscard]] static constexpr usize block_size(const usize requiredSize) noexcept {
        usize size = 1;

        while (size < requiredSize && size < BaseBlockSize)
            size *= 2;

        return size;
    }

    [[nodiscard]] static Slot* slot_from_entry(Entry* entry) noexcept {
        constexpr usize StorageOffset = offsetof(Slot, storage);

        return reinterpret_cast<Slot*>(reinterpret_cast<std::byte*>(entry) - StorageOffset);
    }

    auto nolock_map_end() const noexcept { return map.end(); }

    auto nolock_find(const Key& key) noexcept { return map.find(key); }

    auto nolock_find(const Key& key) const noexcept { return map.find(key); }

    list_iterator nolock_touch(list_iterator listItr) noexcept {
        assert(listItr != nullptr);

        (void) list.move_front(*listItr);

        return listItr;
    }

    Return nolock_read(const list_iterator listItr) const noexcept {
        if constexpr (sizeof(Value) <= ThresholdSize)
            return listItr->value;
        else
            return *listItr->value;
    }

    template<typename... Args>
    Return nolock_write(const Key& key, Args&&... args) noexcept {
        if (capacity == 0)
            return Return{std::forward<Args>(args)...};

        const auto [mapItr, inserted] = map.try_emplace(key, nullptr);

        if (!inserted)
            return nolock_read(nolock_touch(mapItr->second));

        MapGuard mapGuard{map, mapItr};

        Entry* const entry = nolock_construct(key, std::forward<Args>(args)...);

        mapItr->second = entry;
        (void) list.push_front(*entry);

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

        Entry* const entry =
          nolock_construct(key, std::forward<Builder>(builder)(std::forward<Args>(args)...));

        mapItr->second = entry;
        (void) list.push_front(*entry);

        mapGuard.commit();

        nolock_evict_excess(capacity);

        return nolock_read(entry);
    }

    [[nodiscard]] Entry* nolock_acquire() noexcept {
        if (slotList == nullptr)
            nolock_allocate_block();

        Slot* const slot = slotList;

        assert(slot != nullptr);

        slotList   = slot->next;
        slot->next = nullptr;

        return reinterpret_cast<Entry*>(slot->storage);
    }

    void nolock_allocate_block() noexcept {
        // Called only when no reusable slots remain.
        if (blocks.full())
            std::terminate();

        const usize requiredSize = capacity > list.size() ? capacity - list.size() : usize{1};
        const usize blockSize    = block_size(requiredSize);

        blocks.push_back(Block{std::unique_ptr<Slot[]>{new Slot[blockSize]}, blockSize});

        Slot* const slots = blocks.back().slots.get();

        for (usize i = 0; i < blockSize; ++i)
        {
            slots[i].next = slotList;
            slotList      = &slots[i];
        }
    }

    template<typename... Args>
    Entry* nolock_construct(const Key& key, Args&&... args) noexcept {
        Entry* const storage = nolock_acquire();

        return ::new (static_cast<void*>(storage)) Entry(key, std::forward<Args>(args)...);
    }

    void nolock_recycle(Entry* entry) noexcept {
        assert(entry != nullptr);

        Slot* const slot = slot_from_entry(entry);

        entry->~Entry();

        slot->next = slotList;
        slotList   = slot;
    }

    void nolock_erase(typename map_type::iterator mapItr) noexcept {
        Entry* const entry = mapItr->second;

        map.erase(mapItr);
        (void) list.erase(*entry);

        nolock_recycle(entry);
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
            (void) list.erase(*entry);
            nolock_recycle(entry);
        }

        map.clear();
    }

    void nolock_release_pool() noexcept {
        slotList = nullptr;
        blocks.clear_and_release();
    }

    void nolock_configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize defaultCapacity;
    const usize reserveCount;
    const float maxLoadFactor;

    mutable Mutex mutex;
    usize         capacity = 0;
    map_type      map;
    list_type     list;

    static constexpr usize MaxBlocks = 128;

    StaticVector<Block, MaxBlocks> blocks;
    Slot*                          slotList = nullptr;
};

}  // namespace DON

#endif  // UTILITY_LRU_CACHE_H_INCLUDED
