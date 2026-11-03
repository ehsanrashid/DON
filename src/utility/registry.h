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

#ifndef UTILITY_REGISTRY_H_INCLUDED
#define UTILITY_REGISTRY_H_INCLUDED

#include <cassert>       // assert()
#include <cstdlib>       // atexit()
#include <iostream>      // basic_ostream<>
#include <list>          // list<>
#include <mutex>         // lock_guard<>
#include <shared_mutex>  // shared_lock<>, shared_mutex<>
//#include <unordered_map>  // unordered_map<>

#include "../utility/call_once.h"
#include "../utility/types.h"
#include "../utility/ankerl/unordered_dense.h"

namespace DON {

// Registry: thread-safe registry preserving true insertion order with pre-reserved storage.
template<typename Value>
class Registry final {
   private:
    using List = std::list<Value>;
    using Map  = ankerl::unordered_dense::map<Value, typename List::iterator>;

   public:
    explicit Registry(usize reserveCnt = 1 * KB, float maxLoadFac = 0.75F) :
        reserveCount(reserveCnt),
        maxLoadFactor(maxLoadFac) {
        configure();
    }

    Registry(const Registry&) noexcept            = delete;
    Registry& operator=(const Registry&) noexcept = delete;
    Registry(Registry&&) noexcept                 = delete;
    Registry& operator=(Registry&&) noexcept      = delete;
    ~Registry() noexcept                          = default;

    // Registers a value in the registry.
    //
    // Returns false if the value is already registered.
    bool register_(const Value& value) {
        std::lock_guard lockGuard{mutex};

        if (registrationStopped)
            return false;

        return nolock_register(value);
    }

    // Unregisters a value from the registry.
    //
    // Returns false if the value is not registered.
    bool unregister_(const Value& value) {
        std::lock_guard lockGuard{mutex};

        return nolock_unregister(value);
    }

    // Detaches the internal list from the registry.
    //
    // Returns all registered values in true insertion order.
    List detach_list() noexcept {
        std::lock_guard lockGuard{mutex};

        assert(nolock_is_consistent());

        map.clear();

        List detachedList;
        detachedList.swap(list);

        assert(map.empty());
        assert(list.empty());

        return detachedList;
    }

    // Returns the number of registered values.
    [[nodiscard]] usize size() const noexcept {
        std::shared_lock sharedLock{mutex};

        assert(nolock_is_consistent());

        return list.size();
    }

    // Returns true if the registry is empty.
    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};

        assert(nolock_is_consistent());

        return list.empty();
    }

    // Prints registered values in true insertion order.
    void print() const noexcept {
        std::shared_lock sharedLock{mutex};

        assert(nolock_is_consistent());

        std::cout << "Registered values [" << list.size() << "]:\n";

        for (usize i = 0; i < list.size(); ++i)
            std::cout << '[' << i << "] " << list[i] << '\n';

        std::cout << std::endl;
    }

   private:
    [[nodiscard]] auto nolock_find(const Value& value) noexcept { return map.find(value); }

    [[nodiscard]] auto nolock_find(const Value& value) const noexcept { return map.find(value); }

    [[nodiscard]] bool nolock_contains(const Value& value) const noexcept {
        return nolock_find(value) != map.end();
    }

#if !defined(NDEBUG)
    // Verifies consistency between List and Map.
    // Caller must hold mutex in shared or exclusive mode.
    bool nolock_is_consistent() const noexcept {
        assert(list.size() == map.size() && "List and Map sizes differ");

        for (auto listItr = list.begin(); listItr != list.end(); ++listItr)
        {
            const auto mapItr = nolock_find(*listItr);

            assert(mapItr != map.end() && "List value is missing from Map");
            assert(mapItr->second == listItr && "Map points to the wrong List node");
        }

        for (const auto& [value, listItr] : map)
        {
            assert(listItr != list.end() && "Map contains an invalid List iterator");
            assert(*listItr == value && "Map iterator points to the wrong value");
        }

        return true;
    }
#endif

    // Registers a value.
    // The caller must hold 'mutex' exclusively.
    bool nolock_register(const Value& value) noexcept {
        if (nolock_contains(value))
            return false;

        // Append the value to the ordered list.
        const auto listItr = list.emplace(list.end(), value);
        assert(listItr != list.end());

        // Associate the value with its corresponding List node.
        [[maybe_unused]] const auto [mapItr, inserted] = map.emplace(value, listItr);

        // The initial membership check guarantees that insertion succeeds.
        assert(inserted);
        assert(mapItr->second == listItr);

        assert(nolock_is_consistent());

        return true;
    }

    // Unregisters a value.
    // The caller must hold 'mutex' exclusively.
    bool nolock_unregister(const Value& value) noexcept {
        const auto mapItr = nolock_find(value);

        if (mapItr == map.end())
            return false;

        // Retrieve the corresponding List node.
        const auto listItr = mapItr->second;

        assert(listItr != list.end());
        assert(*listItr == value);

        // Remove the index entry and its corresponding List node.
        map.erase(mapItr);
        list.erase(listItr);

        assert(nolock_is_consistent());

        return true;
    }

    // Stops accepting new values.
    void stop_registering() noexcept {
        std::lock_guard lockGuard{mutex};

        registrationStopped = true;
    }

    void configure() noexcept {
        map.max_load_factor(maxLoadFactor);
        map.reserve(reserveCount);
    }

    const usize reserveCount;
    const float maxLoadFactor;

    mutable std::shared_mutex mutex;

    // Preserves insertion order.
    List list;

    // Provides lookup, uniqueness checking, and removal.
    Map map;

    bool registrationStopped = false;

    template<typename>
    friend class RegistryCleanup;
};

// RegistryCleanup: detaches all values from a Registry and resets each non-null value.
template<typename Registry>
class RegistryCleanup final {
   public:
    explicit RegistryCleanup(Registry& reg) noexcept :
        registry(reg) {}
    RegistryCleanup(const RegistryCleanup&) noexcept            = delete;
    RegistryCleanup& operator=(const RegistryCleanup&) noexcept = delete;
    RegistryCleanup(RegistryCleanup&&) noexcept                 = delete;
    RegistryCleanup& operator=(RegistryCleanup&&) noexcept      = delete;
    ~RegistryCleanup() noexcept                                 = default;

    void cleanup() noexcept {
        registry.stop_registering();

        auto valueList = registry.detach_list();

        for (auto* const value : valueList)
            if (value != nullptr)
                value->reset();
    }

   private:
    Registry& registry;
};

// RegistryCleanupHook: ensures a RegistryCleanup callback is registered only once with atexit()
// and retries until successfully registered for normal program termination.
template<typename RegistryCleanup>
class RegistryCleanupHook final {
   public:
    explicit RegistryCleanupHook(RegistryCleanup& regCleanup) noexcept :
        registryCleanup(regCleanup) {}
    RegistryCleanupHook(const RegistryCleanupHook&) noexcept            = delete;
    RegistryCleanupHook& operator=(const RegistryCleanupHook&) noexcept = delete;
    RegistryCleanupHook(RegistryCleanupHook&&) noexcept                 = delete;
    RegistryCleanupHook& operator=(RegistryCleanupHook&&) noexcept      = delete;
    ~RegistryCleanupHook() noexcept                                     = default;

    void ensure_initialized() noexcept {
        while (!hookCallOnce.once_called())
        {
            registryCleanupPtr = &registryCleanup;

            // Call std::atexit only once in the entire application lifecycle setup phase.
            hookCallOnce([]() noexcept -> void { (void) std::atexit(cleanupFunction); });
        }
    }

   private:
    static void cleanupFunction() noexcept {
        if (registryCleanupPtr != nullptr)
            registryCleanupPtr->cleanup();
    }

    RegistryCleanup& registryCleanup;

    static inline RegistryCleanup* registryCleanupPtr = nullptr;
    static inline CallOnce         hookCallOnce;
};

}  // namespace DON

#endif  // UTILITY_REGISTRY_H_INCLUDED
