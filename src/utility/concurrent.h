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

#ifndef UTILITY_CONCURRENT_H_INCLUDED
#define UTILITY_CONCURRENT_H_INCLUDED

#include <cassert>        // assert()
#include <cstdlib>        // atexit()
#include <functional>     // hash<>
#include <iostream>       // basic_ostream<>
#include <list>           // list<>
#include <memory>         // make_unique<>, unique_ptr<>
#include <mutex>          // lock_guard<>
#include <optional>       // optional<>, nullopt
#include <shared_mutex>   // shared_lock<>, shared_mutex<>
#include <type_traits>    // conditional_t<>
#include <unordered_map>  // unordered_map<>
#include <unordered_set>  // unordered_set<>
#include <utility>        // forward<>, pair<>

#include "../misc.h"
#include "../utility/mutex.h"
#include "../utility/types.h"

namespace DON {

// ConcurrentAllocationTracker: thread-safe allocation and freeing, with allocation size tracking.
class ConcurrentAllocationTracker final {
   public:
    using SizeMap = std::unordered_map<void*, usize>;

    ConcurrentAllocationTracker() noexcept = default;

    template<typename AllocFunc>
    [[nodiscard]] void* alloc(const usize allocSize, AllocFunc&& allocFn) noexcept {
        void* const mem = std::forward<AllocFunc>(allocFn)(allocSize);

        if (mem != nullptr)
        {
            std::lock_guard lockGuard{mutex};

            sizeMap.emplace(mem, allocSize);
        }

        return mem;
    }

    template<typename FreeFunc>
    [[nodiscard]] bool free(void* const mem, FreeFunc&& freeFn) noexcept {
        std::lock_guard lockGuard{mutex};

        if (const auto itr = sizeMap.find(mem); itr != sizeMap.end())
        {
            const usize allocSize = itr->second;

            if (!std::forward<FreeFunc>(freeFn)(mem, allocSize))
            {
                //std::exit(EXIT_FAILURE);
                return false;
            }

            sizeMap.erase(itr);
            return true;
        }

        return false;
    }

    [[nodiscard]] usize size() const noexcept {
        std::shared_lock sharedLock{mutex};

        return sizeMap.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};

        return sizeMap.empty();
    }

    [[nodiscard]] std::optional<usize> find(void* const mem) const noexcept {
        std::shared_lock sharedLock{mutex};

        if (auto itr = sizeMap.find(mem); itr != sizeMap.end())
            return itr->second;

        return std::nullopt;
    }

   private:
    ConcurrentAllocationTracker(const ConcurrentAllocationTracker&) noexcept            = delete;
    ConcurrentAllocationTracker& operator=(const ConcurrentAllocationTracker&) noexcept = delete;
    ConcurrentAllocationTracker(ConcurrentAllocationTracker&&) noexcept                 = delete;
    ConcurrentAllocationTracker& operator=(ConcurrentAllocationTracker&&) noexcept      = delete;

    mutable std::shared_mutex mutex;
    SizeMap                   sizeMap;
};

// ConcurrentRegistry: thread-safe registry preserving true insertion order with pre-reserved storage.
template<typename Value>
class ConcurrentRegistry final {
   private:
    using List = std::list<Value>;

   public:
    explicit ConcurrentRegistry(usize reserveCnt = 1 * KB, float maxLoadFac = 0.75F) noexcept :
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac) {
        std::lock_guard lockGuard{mutex};

        configure(indexMap);
        configure(set);
    }

    // Registers a value in the registry.
    //
    // Returns false if the value is already registered.
    bool register_(const Value& value) noexcept {
        std::lock_guard lockGuard{mutex};

        if (regStopped)
            return false;

        return nolock_register(value);
    }

    // Unregisters a value from the registry.
    //
    // Returns false if the value is not registered.
    bool unregister_(const Value& value) noexcept {
        std::lock_guard lockGuard{mutex};

        return nolock_unregister(value);
    }

    // Detaches the internal list from the registry.
    //
    // Returns the list containing all registered values in true insertion order.
    //
    // All registry containers are cleared before the returned list is
    // processed, allowing callers to safely operate on the values without
    // holding the registry lock.
    List detach_list() noexcept {
        std::lock_guard lockGuard{mutex};

        assert(nolock_is_consistent());

        set.clear();
        indexMap.clear();

        List detachedList;
        detachedList.swap(list);

        assert(set.empty());
        assert(indexMap.empty());
        assert(list.empty());

        return detachedList;
    }

    // Returns the number of currently registered values.
    usize size() const noexcept {
        std::shared_lock sharedLock{mutex};

        assert(nolock_is_consistent());

        return nolock_size();
    }

    // Returns true if the registry contains no values.
    bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};

        assert(nolock_is_consistent());

        return nolock_empty();
    }

    // Prints all registered values in true insertion order.
    void print() const noexcept {
        std::shared_lock sharedLock{mutex};

        assert(nolock_is_consistent());

        std::cout << "Registered values [" << list.size() << "]:\n";

        usize i = 0;
        for (const auto& value : list)
            std::cout << '[' << i++ << "] " << value << '\n';

        std::cout << std::endl;
    }

   private:
    ConcurrentRegistry(const ConcurrentRegistry&) noexcept            = delete;
    ConcurrentRegistry& operator=(const ConcurrentRegistry&) noexcept = delete;
    ConcurrentRegistry(ConcurrentRegistry&&) noexcept                 = delete;
    ConcurrentRegistry& operator=(ConcurrentRegistry&&) noexcept      = delete;

    using IndexMap = std::unordered_map<Value, typename List::iterator>;
    using Set      = std::unordered_set<Value>;

    usize nolock_size() const noexcept { return list.size(); }

    bool nolock_empty() const noexcept { return list.empty(); }

    // Checks whether a value is registered.
    bool nolock_contains(const typename Set::const_iterator setItr) const noexcept {
        return setItr != set.end();
    }

    bool nolock_contains(const Value& value) const noexcept {
        return nolock_contains(set.find(value));
    }

    [[maybe_unused]] usize nolock_count(const Value& value) const noexcept {
        return indexMap.count(value);
    }

    auto nolock_find(const Value& value) noexcept { return indexMap.find(value); }

    auto nolock_find(const Value& value) const noexcept { return indexMap.find(value); }

#if !defined(NDEBUG)
    // Verifies the consistency of all registry containers.
    // The caller must hold 'mutex' in shared or exclusive mode.
    //
    // Registry invariants:
    //  - List, IndexMap, and Set contain the same number of values.
    //  - Every value in List exists in IndexMap and Set.
    //  - Every value in IndexMap exists in List and Set.
    //  - Every value in Set exists in List and IndexMap.
    //  - Every IndexMap entry points to the corresponding node in List.
    bool nolock_is_consistent() const noexcept {
        assert(list.size() == indexMap.size() && "List and IndexMap sizes differ");
        assert(list.size() == set.size() && "List and Set sizes differ");

        // Verify that every List entry is indexed and registered.
        for (auto listItr = list.begin(); listItr != list.end(); ++listItr)
        {
            const Value& value = *listItr;

            const auto indexMapItr = nolock_find(value);
            assert(indexMapItr != indexMap.end() && "List value is missing from IndexMap");
            assert(indexMapItr->second == listItr && "IndexMap points to the wrong List node");

            assert(set.find(value) != set.end() && "List value is missing from Set");
        }

        // Verify that every IndexMap entry points to the correct List node
        // and has a corresponding Set entry.
        for (const auto& [value, listItr] : indexMap)
        {
            assert(listItr != list.end() && "IndexMap contains an invalid List iterator");
            assert(*listItr == value && "IndexMap iterator points to the wrong value");

            assert(set.find(value) != set.end() && "IndexMap value is missing from Set");
        }

        // Verify that every Set entry has a corresponding IndexMap entry
        // whose iterator points to the correct List node.
        for (const auto& value : set)
        {
            const auto indexMapItr = nolock_find(value);
            assert(indexMapItr != indexMap.end() && "Set value is missing from IndexMap");

            const auto listItr = indexMapItr->second;
            assert(listItr != list.end() && "IndexMap contains an invalid List iterator");
            assert(*listItr == value && "IndexMap iterator points to the wrong value");
        }

        return true;
    }
#endif

    // Registers a value in all registry containers.
    // The caller must hold 'mutex' exclusively.
    //
    // Set is checked first to reject duplicate values. The value is then
    // appended to List, IndexMap records its corresponding List iterator,
    // and Set establishes membership.
    bool nolock_register(const Value& value) noexcept {
        if (nolock_contains(value))
            return false;

        // Append to the ordered list and obtain its iterator.
        const auto listItr = list.emplace(list.end(), value);
        assert(listItr != list.end());

        // Associate the value with its corresponding List node.
        [[maybe_unused]] const auto [indexMapItr, inserted] = indexMap.emplace(value, listItr);

        // Set was checked first, so IndexMap must not contain the value.
        assert(inserted);
        assert(indexMapItr->second == listItr);

        // Establish membership after List and IndexMap are updated.
        [[maybe_unused]] const auto [setItr, registered] = set.emplace(value);

        // The initial membership check guarantees that this insertion succeeds.
        assert(registered);
        assert(setItr != set.end());

        assert(nolock_is_consistent());

        return true;
    }

    // Unregisters a value from all registry containers.
    // The caller must hold 'mutex' exclusively.
    //
    // Set is checked first to reject unregistered values. IndexMap then
    // provides the corresponding List iterator for constant-time removal.
    bool nolock_unregister(const Value& value) noexcept {
        const auto setItr = set.find(value);

        // Not registered.
        if (!nolock_contains(setItr))
            return false;

        const auto indexMapItr = nolock_find(value);

        // Set guarantees that IndexMap contains the value.
        assert(indexMapItr != indexMap.end());

        // Retrieve the corresponding List node.
        const auto listItr = indexMapItr->second;

        assert(listItr != list.end());
        assert(*listItr == value);

        // Remove membership.
        set.erase(setItr);

        // Remove the index entry.
        indexMap.erase(indexMapItr);

        // Remove the ordered List node.
        list.erase(listItr);

        assert(nolock_is_consistent());

        return true;
    }

    void configure(IndexMap& indexMap_) const noexcept {
        indexMap_.max_load_factor(maxLoadFactor);
        indexMap_.reserve(reserveCount);
    }

    void configure(Set& set_) const noexcept {
        set_.max_load_factor(maxLoadFactor);
        set_.reserve(reserveCount);
    }

    // Stops accepting new values.
    //
    // Once stopped, register_() rejects new values.
    void stop_registering() noexcept {
        std::lock_guard lockGuard{mutex};

        regStopped = true;
    }

    const usize reserveCount;
    const float maxLoadFactor;

    // Protects access to all registry containers.
    mutable std::shared_mutex mutex;

    // Preserves true insertion order for deterministic iteration.
    List list;

    // Provides average O(1) fast lookup and removal.
    // Maps each value to its corresponding iterator in List.
    IndexMap indexMap;

    // Provides average O(1) fast uniqueness and membership checks.
    Set set;

    bool regStopped = false;

    template<typename>
    friend class RegistryCleanup;
};

// RegistryCleanup: detaches all values from a ConcurrentRegistry and resets each non-null value.
template<typename ConcurrentRegistry>
class RegistryCleanup final {
   public:
    explicit RegistryCleanup(ConcurrentRegistry& reg) noexcept :
        registry(reg) {}

    void cleanup() noexcept {
        registry.stop_registering();

        auto valueList = registry.detach_list();

        for (auto* const value : valueList)
            if (value != nullptr)
                value->reset();
    }

   private:
    RegistryCleanup(const RegistryCleanup&) noexcept            = delete;
    RegistryCleanup& operator=(const RegistryCleanup&) noexcept = delete;
    RegistryCleanup(RegistryCleanup&&) noexcept                 = delete;
    RegistryCleanup& operator=(RegistryCleanup&&) noexcept      = delete;

    ConcurrentRegistry& registry;
};

// RegistryCleanupHook: ensures a RegistryCleanup callback is registered only once with atexit()
// and retries until successfully registered for normal program termination.
template<typename RegistryCleanup>
class RegistryCleanupHook final {
   public:
    explicit RegistryCleanupHook(RegistryCleanup& regCleanup) noexcept :
        registryCleanup(regCleanup) {}

    void ensure_initialized() noexcept {
        while (!hookCallOnce.once_done())
        {
            registryCleanupPtr = &registryCleanup;

            hookCallOnce([]() noexcept -> void { std::atexit(cleanupFunction); });
        }
    }

   private:
    RegistryCleanupHook(const RegistryCleanupHook&) noexcept            = delete;
    RegistryCleanupHook& operator=(const RegistryCleanupHook&) noexcept = delete;
    RegistryCleanupHook(RegistryCleanupHook&&) noexcept                 = delete;
    RegistryCleanupHook& operator=(RegistryCleanupHook&&) noexcept      = delete;

    static void cleanupFunction() noexcept {
        if (registryCleanupPtr != nullptr)
            registryCleanupPtr->cleanup();
    }

    RegistryCleanup& registryCleanup;

    static inline RegistryCleanup* registryCleanupPtr = nullptr;
    static inline CallOnce         hookCallOnce;
};

}  // namespace DON

#endif  // UTILITY_CONCURRENT_H_INCLUDED
