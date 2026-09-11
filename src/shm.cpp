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

#include "shm.h"

#if defined(_WIN32)

#elif defined(USE_UNIX_SHM)
    #include <cstdlib>
    #include <mutex>
    #include <shared_mutex>
#endif

namespace DON {

#if defined(_WIN32)

#elif defined(USE_UNIX_SHM)
// SharedMemoryRegistry
//
// A thread-safe global registry for tracking live shared memory objects
// (BaseSharedMemory) without owning them.
//
// The registry maintains:
//  - True insertion order for deterministic iteration
//  - O(1) registration and unregistration via list + hash map
//
// Key Features:
//  - Thread-safe registration and unregistration
//  - Deterministic iteration order
//  - O(1) lookup and removal
//  - Lightweight: stores raw pointers only; lifetime is managed externally
//
// Implementation:
//  - OrderedList preserves true insertion order
//  - RegistryMap provides O(1) lookup and stores an iterator into OrderedList
//
// Concurrency Model:
//  - shared_mutex protects both registry containers
//  - Shared/read access uses shared locking
//  - Registration and unregistration use exclusive locking
//
// Usage:
//  - Call 'register_memory()' after successful shared memory creation
//  - Call 'unregister_memory()' before destruction
//
// Note:
//  - The registry does not own or release registered shared memory objects.
namespace SharedMemoryRegistry {

namespace {

// Protects both registry containers.
std::shared_mutex sharedMutex;
// Preserves true insertion order for deterministic iteration.
OrderedList orderedList;
// Provides O(1) lookup and removal.
// Each entry stores an iterator into 'orderedList'.
RegistryMap registryMap;

// Insert a shared memory object into both registry containers.
//
// The caller must hold 'sharedMutex' exclusively.
//
// Two-phase insertion:
//  1. Insert the pointer into RegistryMap with a temporary list iterator.
//     This performs the duplicate check and reserves the map entry.
//  2. Append the pointer to OrderedList.
//  3. Replace the temporary iterator with the actual list iterator.
//
// This avoids a second map lookup while keeping both containers synchronized.
bool insert_memory_nolock(SharedMemoryPtr sharedMemory) noexcept {
    auto [insertReg, inserted] = registryMap.emplace(sharedMemory, orderedList.end());

    // Already registered.
    if (!inserted)
        return false;

    //DEBUG_LOG("Registering shared memory: " << sharedMemory->name());

    // Append to the ordered list and obtain a stable iterator.
    auto insertIt = orderedList.emplace(orderedList.end(), sharedMemory);

    // Associate the map entry with its corresponding list node.
    insertReg->second = insertIt;

    return true;
}

// Remove a shared memory object from both registry containers.
//
// The caller must hold 'sharedMutex' exclusively.
//
// RegistryMap stores the corresponding OrderedList iterator, allowing
// O(1) removal from both containers without searching the list.
bool erase_memory_nolock(SharedMemoryPtr sharedMemory) noexcept {
    auto eraseReg = registryMap.find(sharedMemory);

    // Not registered.
    if (eraseReg == registryMap.end())
        return false;

    // Retrieve the stable list iterator associated with this entry.
    auto eraseIt = eraseReg->second;

    // Internal consistency check.
    assert(eraseIt != orderedList.end());

    // Remove the list node first.
    orderedList.erase(eraseIt);

    // Remove the corresponding map entry.
    registryMap.erase(eraseReg);

    //DEBUG_LOG("Unregistered shared memory: " << sharedMemory->name());

    return true;
}

}  // namespace

// Register a shared memory object.
//
// Returns false if:
//  - sharedMemory is nullptr
//  - the object is already registered
bool register_memory(SharedMemoryPtr sharedMemory) noexcept {
    if (sharedMemory == nullptr)
    {
        //DEBUG_LOG("Cannot register <NULL> shared memory.");
        return false;
    }

    // Acquire an exclusive lock because both containers are modified.
    std::lock_guard writeLock(sharedMutex);

    return insert_memory_nolock(sharedMemory);
}

// Unregister a shared memory object from the global registry.
//
// Returns false if the object is nullptr or is not registered.
bool unregister_memory(SharedMemoryPtr sharedMemory) noexcept {
    if (sharedMemory == nullptr)
        return false;

    // Acquire an exclusive lock because both containers are modified.
    std::lock_guard writeLock(sharedMutex);

    return erase_memory_nolock(sharedMemory);
}

// Detach registered shared memory objects from the registry.
//
// Returns the objects in true insertion order.
//
// The registry containers are detached and cleared before the returned
// list is processed, allowing callers to safely operate on the objects
// without holding the registry lock.
OrderedList detach_memories() noexcept {
    std::lock_guard writeLock(sharedMutex);

    OrderedList detachedList = std::move(orderedList);
    registryMap.clear();

    return detachedList;
}

// Returns the number of currently registered shared memory objects.
usize size() noexcept {
    std::shared_lock readLock(sharedMutex);

    return registryMap.size();
}

// Prints all registered shared memory objects in true insertion order.
//
// The registry lock is held only while reading the containers.
void print() noexcept {
    std::shared_lock readLock(sharedMutex);

    std::cout << "Registered shared memories (insertion order) [" << registryMap.size() << "]:\n";

    usize i = 0;
    for (auto* sharedMemory : orderedList)
        std::cout << "[" << i++ << "] "
                  << (sharedMemory != nullptr ? sharedMemory->name() : "<NULL>") << "\n";

    std::cout << std::endl;
}

}  // namespace SharedMemoryRegistry

// SharedMemoryCleanup
//
// Handles cleanup of all currently registered shared memory objects.
//
// Responsibilities:
//  - Detach all registered shared memory objects from the registry
//  - Close each detached shared memory object
//
// The class does not own the shared memory objects and does not manage
// their lifetime beyond invoking 'close()' during cleanup.
//
// Note:
//  - Registry management is handled by SharedMemoryRegistry.
//  - Process-exit hook installation is handled by SharedMemoryCleanupHook.
//  - Cleanup is performed in registry insertion order.
namespace SharedMemoryCleanup {

void cleanup() noexcept {
    auto sharedMemoryList = SharedMemoryRegistry::detach_memories();

    //DEBUG_LOG("Shared memory cleanup started (" << sharedMemoryList.size() << " object(s)).");
    for (auto* sharedMemory : sharedMemoryList)
    {
        if (sharedMemory != nullptr)
            sharedMemory->release();
    }
}

}  // namespace SharedMemoryCleanup

// SharedMemoryCleanupHook
//
// Installs the shared memory cleanup handler for normal program termination.
//
// Usage:
//   Call SharedMemoryCleanupHook::ensure_initialized() early in main().
//
// Key Points:
//   - Uses CallOnce to ensure the cleanup handler is registered only once.
//   - Registers SharedMemoryCleanup::cleanup() with std::atexit().
//   - Does not manage the registry or perform cleanup itself.
//
// Note:
//   - Cleanup via std::atexit() is only guaranteed during normal termination.
//     It will not run after forced termination (SIGKILL), crashes, or abort().
namespace SharedMemoryCleanupHook {

namespace {

CallOnce callOnce;

}  // namespace

// Ensure the shared memory cleanup callback is registered with std::atexit().
void ensure_initialized() noexcept {
    callOnce([]() noexcept {
        //DEBUG_LOG("Initializing SharedMemoryCleanupHook.");

        std::atexit(SharedMemoryCleanup::cleanup);
    });
}

}  // namespace SharedMemoryCleanupHook

#endif

}  // namespace DON
