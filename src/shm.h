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

#ifndef SHM_H_INCLUDED
#define SHM_H_INCLUDED

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <iostream>  // Only in DEBUG

#if defined(_WIN32)
    // Standard portable pattern for spin-wait / CPU pause hint
    #if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
        // x86/x64: use _mm_pause() from <emmintrin.h>
        #include <emmintrin.h>  // SSE2
        #define PAUSE() _mm_pause()
    #elif defined(__arm__) || defined(__aarch64__)
        // ARM CPUs: use inline "yield" instruction to other hardware threads
        #define PAUSE() __asm__ volatile("yield" ::: "memory")
    #else
        // Fallback: portable C++ hint (PowerPC, RISC-V, MIPS, etc.)
        #include <thread>
        #define PAUSE() std::this_thread::yield()
    #endif

    #if !defined(NOMINMAX)
        #define NOMINMAX  // Disable min()/max() macros
    #endif
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <sdkddkver.h>
    #if defined(_WIN32_WINNT) && _WIN32_WINNT < _WIN32_WINNT_WIN7
        #undef _WIN32_WINNT
    #endif
    #if !defined(_WIN32_WINNT)
        // Force to include needed API prototypes
        #define _WIN32_WINNT _WIN32_WINNT_WIN7  // or _WIN32_WINNT_WIN10
    #endif
    #undef UNICODE
    #include <windows.h>
    #if defined(small)
        #undef small
    #endif
#else
    #if defined(__linux__) && !defined(__ANDROID__)
        #include <atomic>
        #include <cassert>
        #include <cerrno>
        #include <chrono>
        #include <climits>
        #include <condition_variable>
        #include <cstdlib>
        #include <dirent.h>
        #include <fcntl.h>
        #include <filesystem>
        #include <inttypes.h>
        #include <list>
        #include <mutex>
        #include <pthread.h>
        #include <semaphore.h>
        #include <shared_mutex>
        #include <signal.h>
        #include <sys/file.h>
        #include <sys/mman.h>  // mmap, munmap, MAP_*, PROT_*
        #include <sys/stat.h>
        #include <thread>
        #include <unistd.h>
        #include <unordered_map>

        #define SHM_NAME_MAX_SIZE NAME_MAX
    #endif

    #if defined(__APPLE__)
        // macOS / iOS
        #include <mach-o/dyld.h>
        #include <sys/syslimits.h>
    #elif defined(__sun)
        // Solaris / OpenSolaris / illumos
        #include <cstdlib>
        #include <libgen.h>
    #elif defined(__FreeBSD__)
        // FreeBSD
        #include <sys/sysctl.h>
        #include <sys/types.h>
    #elif defined(__NetBSD__)
        // NetBSD
        #include <climits>
    #elif defined(__DragonFly__)
        // DragonFly BSD
        #include <climits>
    #elif defined(__linux__)
        // Linux
    #elif defined(_AIX)
        // IBM AIX
    #elif defined(__arm__) || defined(__aarch64__)
        // ARM 32-bit / 64-bit
    #elif defined(__i386__) || defined(__x86_64__)
        // x86 32-bit / x86-64
    #elif defined(__ANDROID__)
        // Andriod
    #else
        #error "Unsupported platform"
    #endif
#endif

#include "memory.h"
#include "misc.h"

namespace DON {

// argv[0] CANNOT be used because need to identify the executable.
// argv[0] contains the command used to invoke it, which does not involve the full path.
// Just using a path is not fully resilient either, as the executable could have changed
// if it wasn't locked by the OS. If the path is longer than 4095 bytes the hash will be computed
// from an unspecified amount of bytes of the path; in particular it can a hash of an empty string.

enum class SharedMemoryAllocationStatus {
    NoAllocation,
    LocalMemory,
    SharedMemory
};

[[nodiscard]] constexpr std::string_view to_string(SharedMemoryAllocationStatus status) noexcept {
    switch (status)
    {
    case SharedMemoryAllocationStatus::NoAllocation :
        return "No allocation.";
    case SharedMemoryAllocationStatus::LocalMemory :
        return "Local memory.";
    case SharedMemoryAllocationStatus::SharedMemory :
        return "Shared memory.";
    }
    return "Allocation status unknown.";
}

inline std::string executable_path() noexcept {
    StdArray<char, 4096> executablePath{};
    std::size_t          executableSize = 0;

#if defined(_WIN32)
    DWORD size = GetModuleFileName(nullptr, executablePath.data(), DWORD(executablePath.size()));

    executableSize = std::min<std::size_t>(size, executablePath.size() - 1);

    executablePath[executableSize] = '\0';
#elif defined(__APPLE__)
    std::uint32_t size = std::uint32_t(executablePath.size());

    if (_NSGetExecutablePath(executablePath.data(), &size) == 0)
        executableSize = std::strlen(executablePath.data());
    else
    {
        // Buffer too small
        if (size < executablePath.size())
            if (_NSGetExecutablePath(executablePath.data(), &size) == 0)
                executableSize = std::strlen(executablePath.data());
    }
#elif defined(__sun)  // Solaris
    const char* path = getexecname();

    if (path != nullptr)
    {
        std::strncpy(executablePath.data(), path, executablePath.size() - 1);

        // Determine actual length copied
        executableSize = std::strnlen(path, executablePath.size() - 1);

        executablePath[executableSize] = '\0';
    }
#elif defined(__FreeBSD__)
    constexpr StdArray<int, 4> MIB{CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};

    std::size_t size = executablePath.size();

    if (sysctl(MIB.data(), MIB.size(), executablePath.data(), &size, nullptr, 0) == 0)
    {
        executableSize = std::min<std::size_t>(size, executablePath.size() - 1);

        executablePath[executableSize] = '\0';
    }
#elif defined(__NetBSD__) || defined(__DragonFly__)
    ssize_t size = readlink("/proc/curproc/exe", executablePath.data(), executablePath.size() - 1);

    if (size >= 0)
    {
        executableSize = std::min<std::size_t>(size, executablePath.size() - 1);

        executablePath[executableSize] = '\0';
    }
#elif defined(__linux__)
    ssize_t size = readlink("/proc/self/exe", executablePath.data(), executablePath.size() - 1);

    if (size >= 0)
    {
        executableSize = std::min<std::size_t>(size, executablePath.size() - 1);

        executablePath[executableSize] = '\0';
    }
#endif

    // In case of any error the path will be empty
    return std::string{executablePath.data(), executableSize};
}

#if defined(_WIN32)
// Utilizes shared memory to store the value. It is deduplicated system-wide (for the single user)
template<typename T>
class BackendSharedMemory final {
   public:
    enum class Status : std::uint8_t {
        Success,
        NotInitialized,
        FileMapping,
        MapView,
        MutexCreate,
        MutexWait,
        MutexRelease,
        LargePageAllocation
    };

    BackendSharedMemory() noexcept :
        status(Status::NotInitialized) {};

    BackendSharedMemory(std::string_view shmName, const T& value) noexcept :
        name(shmName),
        status(Status::NotInitialized) {
        // Windows named shared memory names must start with "Local\" or "Global\"
        constexpr std::string_view Prefix{"Local\\"};
        if (name_().size() < Prefix.size() || name_().compare(0, Prefix.size(), Prefix) != 0)
            name.insert(0, Prefix);

        //DEBUG_LOG("Creating shared memory with name: " << name_());

        initialize(value);
    }

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept :
        name(backendShm.name_()),
        hMapFile(backendShm.hMapFile),
        hMapFileGuard(hMapFile),
        mappedPtr(backendShm.mappedPtr),
        mappedGuard(mappedPtr),
        status(backendShm.status) {
        //DEBUG_LOG("Moving shared memory, name: " << name_());

        backendShm.hMapFile  = INVALID_HANDLE;
        backendShm.mappedPtr = INVALID_MMAP_PTR;
        backendShm.status    = Status::NotInitialized;
    }
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept {
        if (this == &backendShm)
            return *this;

        destroy();

        name      = backendShm.name_();
        hMapFile  = backendShm.hMapFile;
        mappedPtr = backendShm.mappedPtr;
        status    = backendShm.status;

        //DEBUG_LOG("Moving shared memory, name: " << name_());

        backendShm.hMapFile  = INVALID_HANDLE;
        backendShm.mappedPtr = INVALID_MMAP_PTR;
        backendShm.status    = Status::NotInitialized;

        return *this;
    }

    ~BackendSharedMemory() noexcept { destroy(); }

    bool is_valid() const noexcept { return status == Status::Success; }

    void* get() const noexcept { return is_valid() ? mappedPtr : INVALID_MMAP_PTR; }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return status == Status::Success ? SharedMemoryAllocationStatus::SharedMemory
                                         : SharedMemoryAllocationStatus::NoAllocation;
    }

    std::string_view get_error_message() const noexcept {
        switch (status)
        {
        case Status::Success :
            return {};
        case Status::NotInitialized :
            return "Shared memory not initialized.";
        case Status::FileMapping :
            return "Shared memory: Failed to create file mapping.";
        case Status::MapView :
            return "Shared memory: Failed to map view.";
        case Status::MutexCreate :
            return "Shared memory: Failed to create mutex.";
        case Status::MutexWait :
            return "Shared memory: Failed to wait on mutex.";
        case Status::MutexRelease :
            return "Shared memory: Failed to release mutex.";
        case Status::LargePageAllocation :
            return "Shared memory: Failed to allocate large page memory.";
        }
        return "Shared memory: unknown error.";
    }

    std::string_view name_() const noexcept { return name; }

   private:
    void initialize(const T& value) noexcept {
        constexpr std::size_t TotalSize = sizeof(T) + sizeof(InitSharedState);

        // Try allocating with large page first
        hMapFile = try_with_windows_lock_memory_privilege(
          [&](std::size_t LargePageSize) noexcept {
              // Round up size to full large page
              std::size_t roundedTotalSize = round_up_to_pow2_multiple(TotalSize, LargePageSize);

    #if defined(_WIN64)
              DWORD hiTotalSize = roundedTotalSize >> 32;
              DWORD loTotalSize = roundedTotalSize & 0xFFFFFFFFU;
    #else
              DWORD hiTotalSize = 0;
              DWORD loTotalSize = roundedTotalSize;
    #endif

              //DEBUG_LOG("Allocating large page shared memory, size = " << roundedTotalSize << " bytes");

              return CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
                                       PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,  //
                                       hiTotalSize, loTotalSize, name_().data());
          },
          []() { return INVALID_HANDLE; });

        // Fallback to normal allocation if no large page available
        if (hMapFile == INVALID_HANDLE)
        {
            //DEBUG_LOG("Allocating normal shared memory, size = " << TotalSize << " bytes");

            hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,  //
                                         0, TotalSize, name_().data());
        }

        if (hMapFile == INVALID_HANDLE)
        {
            //DEBUG_LOG("CreateFileMapping() failed, name = " << name_() << , error = " << error_to_string(GetLastError()));
            status = Status::FileMapping;

            return;
        }

        mappedPtr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, TotalSize);

        if (mappedPtr == INVALID_MMAP_PTR)
        {
            //DEBUG_LOG("MapViewOfFile() failed, name = " << name_() << ", error = " << error_to_string(GetLastError()));
            status = Status::MapView;

            cleanup();
            return;
        }

        // Use named mutex to ensure only one initializer
        std::string mutexName{name_()};
        mutexName += "$mutex";

        HANDLE hMutex = CreateMutex(nullptr, FALSE, mutexName.c_str());

        HandleGuard hMutexGuard{hMutex};

        if (hMutex == nullptr)
        {
            //DEBUG_LOG("CreateMutex() failed, name = " << mutexName << ", error = " << error_to_string(GetLastError()));
            status = Status::MutexCreate;

            cleanup();
            return;
        }
        // Wait for ownership
        if (WaitForSingleObject(hMutex, INFINITE) != WAIT_OBJECT_0)
        {
            //DEBUG_LOG("WaitForSingleObject() failed, name = " << mutexName << ", error = " << error_to_string(GetLastError()));
            status = Status::MutexWait;

            cleanup();
            return;
        }

        // Object lives first to ensure alignment
        T* object = reinterpret_cast<T*>(mappedPtr);

        auto* initState =
          reinterpret_cast<volatile DWORD*>(reinterpret_cast<char*>(mappedPtr) + sizeof(T));

        // Attempt atomic initialization
        if (InterlockedCompareExchange(initState, DWORD(InitSharedState::Initializing),
                                       DWORD(InitSharedState::Uninitialized))
            == DWORD(InitSharedState::Uninitialized))
        {
            // this thread is the initializer
            new (object) T{value};

            // Publish fully constructed object
            InterlockedExchange(initState, DWORD(InitSharedState::Initialized));
        }
        else
        {
            // Wait until construction completes
            while (*initState != DWORD(InitSharedState::Initialized))
                PAUSE();  // portable "pause" for any architecture
        }

        if (!ReleaseMutex(hMutex))
        {
            //DEBUG_LOG("ReleaseMutex() failed, name = " << mutexName << ", error = " << error_to_string(GetLastError()));
            status = Status::MutexRelease;

            cleanup();
            return;
        }

        //DEBUG_LOG("Shared memory initialized successfully, name: " << name_());
        status = Status::Success;
    }

    void cleanup() noexcept {
        //DEBUG_LOG("Cleaning up shared memory, name: " << name_());

        mappedGuard.close();

        hMapFileGuard.close();
    }

    void destroy() noexcept {
        //DEBUG_LOG("Destroying shared memory, name: " << name_());

        cleanup();
    }

    enum class InitSharedState : DWORD {
        Uninitialized = 0,
        Initializing  = 1,
        Initialized   = 2
    };

    std::string name;
    HANDLE      hMapFile = INVALID_HANDLE;
    HandleGuard hMapFileGuard{hMapFile};
    void*       mappedPtr = INVALID_MMAP_PTR;
    MMapGuard   mappedGuard{mappedPtr};
    Status      status = Status::NotInitialized;
};
#elif defined(__linux__) && !defined(__ANDROID__)
class BaseSharedMemory {
   public:
    explicit BaseSharedMemory(std::string_view shmName) noexcept :
        name(shmName) {
        // POSIX named shared memory names must start with slash ('/')
        constexpr char Prefix = '/';
        if (name_().empty() || name_()[0] != Prefix)
            name.insert(name.begin(), Prefix);
    }

    virtual ~BaseSharedMemory() noexcept = default;

    virtual void close(bool skipUnmapRegion = false) noexcept = 0;

    std::string_view name_() const noexcept { return name; }

    std::string name;
};

// SharedMemoryRegistry
//
// A thread-safe global registry for tracking live shared memory objects
// (BaseSharedMemory) without owning them.
//
// The registry maintains:
//  - True insertion order for deterministic iteration and shutdown
//  - O(1) registration and unregistration via list + hash map
//
// Key Features:
//  - Thread-safe registration and unregistration
//  - Bounded waiting during cleanup to avoid shutdown deadlocks
//  - Deterministic cleanup order (preserves insertion order)
//  - Safe bulk cleanup without iterator invalidation
//  - Lightweight: stores raw pointers only; lifetime is managed externally
//
// Implementation:
//  - OrderedList preserves insertion order
//  - RegistryMap provides O(1) lookup
//
// Concurrency Model:
//  - shared_mutex protects registry containers (readers/writers)
//  - mutex + condition_variable coordinate waiting during cleanup
//  - atomic flag signals cleanup-in-progress state
//
// Usage:
//  - Call 'attempt_register_memory()' after successful shared memory creation
//  - Call 'unregister_memory()' before destruction
//  - Call 'clean()' during shutdown to close all registered memories
//
// Note:
//  - The class is static-only; it cannot be instantiated. (Restriction)
//  - close() implementations may safely call unregister_memory()
class SharedMemoryRegistry final {
   private:
    using SharedMemoryPtr = BaseSharedMemory*;
    using OrderedList     = std::list<SharedMemoryPtr>;
    using RegistryMap     = std::unordered_map<SharedMemoryPtr, OrderedList::iterator>;

   public:
    // Ensure internal containers are ready
    static void ensure_initialized() noexcept {
        callOnce([]() noexcept {
            constexpr std::size_t ReserveCount = 1024;
            constexpr float       LoadFactor   = 0.75f;

            //DEBUG_LOG("Initializing SharedMemoryRegistry with reserve-count " << ReserveCount << " and load-factor " << LoadFactor);

            // Prepare the registryMap: set load factor and pre-allocate buckets.
            // 'orderedList' is a std::list, which does not support reserve().
            // Memory allocation happens per-node dynamically, so no pre-allocation is needed.
            registryMap.max_load_factor(LoadFactor);
            std::size_t bucketCount = std::size_t(ReserveCount / LoadFactor) + 1;
            registryMap.rehash(bucketCount);
        });
    }

    static bool cleanup_in_progress() noexcept {
        return cleanUpInProgress.load(std::memory_order_acquire);
    }

    // Attempt to register shared memory; waits for cleanup if needed (bounded)
    static void attempt_register_memory(SharedMemoryPtr sharedMemory) noexcept {
        // Bounded wait for cleanup to finish
        constexpr auto MaxWaitTime = std::chrono::milliseconds(200);

        if (!callOnce.initialized())
            ensure_initialized();

        if (sharedMemory == nullptr)
        {
            //DEBUG_LOG("Attempted to register <NULL> shared memory.");
            return;
        }

        std::unique_lock condLock(mutex);

        // Wait for cleanup to finish if in progress (bounded)
        if (!condVar.wait_for(condLock, MaxWaitTime,
                              []() noexcept { return !cleanup_in_progress(); }))
        {
            //DEBUG_LOG("Timeout waiting for SharedMemoryRegistry cleanup to finish : " << sharedMemory->name_());
            // Timeout - silently fail to register (acceptable during shutdown)
            return;
        }

        condLock.unlock();

        // Safe insertion under write-lock
        std::lock_guard writeLock(sharedMutex);

        // RECHECK after acquiring registry lock
        if (cleanup_in_progress())
            return;

        insert_memory_nolock(sharedMemory);
    }

    // Unregister a shared memory object from the global registry.
    // Thread-safe: locks the registry while erasing.
    static bool unregister_memory(SharedMemoryPtr sharedMemory) noexcept {
        std::lock_guard writeLock(sharedMutex);

        return erase_memory_nolock(sharedMemory);
    }

    // Cleans up all registered shared memory objects in the registry.
    //
    // Performs a bulk shutdown of all currently registered shared memories.
    // Preserves true insertion order during cleanup.
    // Parameters:
    //  - skipUnmapRegion: if true, the actual unmapping of memory regions
    //    is skipped (useful during controlled shutdown or testing).
    // Thread-safety and concurrency:
    //  - Sets 'cleanUpInProgress' to prevent new registrations during cleanup.
    //  - Uses a temporary local list to store the registry contents, so that
    //    'close()' can safely call 'unregister_memory()' without invalidating
    //    iterators or causing race conditions.
    //  - Notifies all threads waiting on registration that cleanup is complete.
    static void cleanup(bool skipUnmapRegion = false) noexcept {
        if (!callOnce.initialized())
            ensure_initialized();

        // Mark cleanup as in-progress so other threads know not to register new memory
        cleanUpInProgress.store(true, std::memory_order_release);

        OrderedList copiedOrderedList;
        {
            std::lock_guard cleanLock(sharedMutex);

            // Move all registered shared memories into local list to allow safe iteration
            // and prevent iterator invalidation if close() triggers unregistration.
            if (skipUnmapRegion)
            {
                // Partial cleanup: just snapshot, keep registries intact
                copiedOrderedList = orderedList;
            }
            else
            {
                // Full cleanup: take ownership and clear registries
                copiedOrderedList = std::move(orderedList);
                orderedList.clear();
                registryMap.clear();
            }
        }

        // Safe to iterate and close memory without holding the lock in true insertion order
        for (auto* sharedMemory : copiedOrderedList)
            if (sharedMemory != nullptr)
                sharedMemory->close(skipUnmapRegion);

        // Mark cleanup done and notify waiting registrants that cleanup has finished
        cleanUpInProgress.store(false, std::memory_order_release);
        condVar.notify_all();
    }

    static std::size_t size() noexcept {
        std::shared_lock readLock(sharedMutex);

        return registryMap.size();
    }

    static void print() noexcept {
        // Acquire shared lock to safely read the registry without blocking writers
        std::shared_lock readLock(sharedMutex);

        DEBUG_LOG("Registered shared memories (insertion order) [" << registryMap.size() << "]:");
        [[maybe_unused]] std::size_t i = 0;
        for ([[maybe_unused]] auto* sharedMemory : orderedList)
            DEBUG_LOG("[" << i++ << "] "
                          << (sharedMemory != nullptr ? sharedMemory->name_() : "<NULL>"));
        DEBUG_LOG("");
    }

   private:
    SharedMemoryRegistry() noexcept                                       = delete;
    ~SharedMemoryRegistry() noexcept                                      = delete;
    SharedMemoryRegistry(const SharedMemoryRegistry&) noexcept            = delete;
    SharedMemoryRegistry(SharedMemoryRegistry&&) noexcept                 = delete;
    SharedMemoryRegistry& operator=(const SharedMemoryRegistry&) noexcept = delete;
    SharedMemoryRegistry& operator=(SharedMemoryRegistry&&) noexcept      = delete;

    static bool insert_memory_nolock(SharedMemoryPtr sharedMemory) noexcept {
        // Fast-path insert with a single registry lookup.
        //
        // - Insert into the map using a placeholder iterator (orderedList.end()).
        //   This reserves the key and detects duplicates without touching the list.
        // - Create the actual list node to preserve insertion order.
        // - Patch the map entry with the real list iterator.
        //
        // This two-phase approach avoids a second map lookup and keeps
        // map ↔ list consistency explicit and efficient.
        auto [insertReg, inserted] = registryMap.emplace(sharedMemory, orderedList.end());
        // Already registered -> don't insert
        if (!inserted)
            return false;

        //DEBUG_LOG("Registering shared memory: " << sharedMemory->name_());

        // Append to the ordered list and obtain a stable list iterator
        auto insertId = orderedList.emplace(orderedList.end(), sharedMemory);
        // Replace the placeholder with the real stable list iterator
        insertReg->second = insertId;
        return true;
    }

    static bool erase_memory_nolock(SharedMemoryPtr sharedMemory) noexcept {
        // Fast-path erase using the registry lookup.
        //
        // The map stores a direct iterator into the ordered list, allowing
        // O(1) removal from both containers without searching the list.
        auto eraseReg = registryMap.find(sharedMemory);
        // Not registered -> nothing to erase
        if (eraseReg == registryMap.end())
            return false;

        //DEBUG_LOG("Unregistering shared memory: " << sharedMemory->name_());

        // Retrieve the stable list iterator associated with this entry
        auto eraseId = eraseReg->second;
        // Internal consistency check:
        //  - list must not be empty
        //  - iterator must be valid
        assert(!orderedList.empty() && eraseId != orderedList.end());

        // Remove from the ordered list first (iterator remains valid until erased)
        orderedList.erase(eraseId);
        // Remove the corresponding registry entry
        registryMap.erase(eraseReg);
        return true;
    }

    static inline CallOnce          callOnce;
    static inline std::atomic<bool> cleanUpInProgress{false};
    // For condition_variable wait
    static inline std::mutex              mutex;
    static inline std::condition_variable condVar;
    // For general access to shared memory registry for thread safety
    static inline std::shared_mutex sharedMutex;
    // Preserves insertion order for registered SharedMemories
    static inline OrderedList orderedList;
    // Provides O(1) fast lookup for registered SharedMemories
    static inline RegistryMap registryMap;
};

// SharedMemoryCleanupManager
//
// Utility class that ensures **automatic cleanup of shared memory** when the program exits
// or when certain signals (termination, fatal errors) are received.
//
// Usage:
//   Call SharedMemoryCleanupManager::ensure_initialized() early in main()
//   to register cleanup hooks and signal handlers.
//   This guarantees that SharedMemoryRegistry::cleanup() will be
//   invoked automatically on program exit or abnormal termination.
//
// Key Points:
//   - Uses CallOnce to register hooks only once, even if called multiple times.
//   - Registers both atexit handler (normal program termination) and POSIX signal handlers.
//   - Signal handler performs minimal, safe cleanup and then re-raises the signal with default behavior.
//   - Prevents instantiation and copying (all constructors/destructor deleted).
//
// Note:
//  - The class is static-only; it cannot be instantiated. (Restriction)
class SharedMemoryCleanupManager final {
   public:
    // Ensures that the SharedMemoryCleanupManager is initialized only once.
    //   1. Creating an async-signal-safe pipe for communication between signal
    //      handlers and the monitor thread.
    //   2. Registering signal handlers.
    //   3. Starting the monitor thread to handle signals.
    //   4. Initializing the shared memory registry.
    //   5. Registering cleanup via std::atexit().
    //
    // Note: If pipe creation fails, signal handlers and monitor thread are skipped, to avoid unsafe signal handling
    //       but registry initialization and atexit registration still occur safely.
    static void ensure_initialized() noexcept {
        callOnce([]() noexcept {
            //DEBUG_LOG("Initializing SharedMemoryCleanupManager.");

            // 1. Create async-signal-safe pipe
            int  pipeFds[2]{-1, -1};  // initialize to -1 for safety
            bool pipeInvalid = true;
    #if defined(__linux__)
            // Linux: use pipe2 (atomic)
            if (pipe2(pipeFds, O_CLOEXEC | O_NONBLOCK) != 0)
    #else
            // macOS/BSD: use pipe + fcntl
            if (pipe(pipeFds) != 0)
    #endif
            {
                //DEBUG_LOG("Failed to create signal pipe, error = " << std::strerror(errno));

                pipeInvalid = false;

                if (pipeFds[0] != -1)
                    close(pipeFds[0]);
                if (pipeFds[1] != -1)
                    close(pipeFds[1]);
            }
    #if !defined(__linux__)
            // Set flags manually (portable alternative to pipe2)
            if (!pipeInvalid
                && (fcntl(pipeFds[0], F_SETFD, FD_CLOEXEC) == -1     //
                    || fcntl(pipeFds[1], F_SETFD, FD_CLOEXEC) == -1  //
                    || fcntl(pipeFds[0], F_SETFL, O_NONBLOCK) == -1  //
                    || fcntl(pipeFds[1], F_SETFL, O_NONBLOCK) == -1))
            {
                //DEBUG_LOG("Failed to set pipe flags, error = " << std::strerror(errno));

                pipeInvalid = false;

                if (pipeFds[0] != -1)
                    close(pipeFds[0]);
                if (pipeFds[1] != -1)
                    close(pipeFds[1]);
            }
    #endif
            // Store signal pipe fds atomically
            if (!pipeInvalid)
            {
                signalPipeFds[0].store(pipeFds[0], std::memory_order_release);
                signalPipeFds[1].store(pipeFds[1], std::memory_order_release);

                if (valid_signal_pipe())
                {
                    // 2. Register signal handlers
                    register_signal_handlers();
                    // 3. Start monitor thread
                    start_monitor_thread();
                }
            }

            // Always do registry initialization + atexit registration, even if pipe failed

            // 4. Initialize registry (might trigger signals, now safe all is ready)
            SharedMemoryRegistry::ensure_initialized();
            // 5. Register std::atexit() shutdown cleanup
            std::atexit(cleanup_on_exit);
        });
    }

   private:
    // Register all signals with the deferred handler
    static void register_signal_handlers() noexcept {
        //DEBUG_LOG("Registering signal handlers.");

        sigset_t sigSet;

        sigemptyset(&sigSet);

        for (int signal : SIGNALS)
            sigaddset(&sigSet, signal);

        // Block all signals handlers about to register
        if (pthread_sigmask(SIG_BLOCK, &sigSet, nullptr) != 0)
        {
            //DEBUG_LOG("Failed to block signals, error = " << std::strerror(errno));
        }

        // Now register handlers
        for (int signal : SIGNALS)
        {
            struct sigaction sigAction{};
            sigAction.sa_handler = signal_handler;
            sigemptyset(&sigAction.sa_mask);
            // Choose flags depending on signal type
            switch (signal)
            {
                // clang-format off
            // Normal termination/interruption signals
            case SIGHUP : case SIGINT : case SIGQUIT : case SIGTERM : case SIGSYS : case SIGXCPU : case SIGXFSZ :
                sigAction.sa_flags = SA_RESTART;
                break;
            // Fatal signals
            case SIGSEGV : case SIGILL : case SIGABRT : case SIGFPE : case SIGBUS :
                sigAction.sa_flags = 0;
                break;
            // Safe fallback
            default :
                sigAction.sa_flags = 0;
                // clang-format on
            }

            if (sigaction(signal, &sigAction, nullptr) != 0)
            {
                //DEBUG_LOG("Failed to register handler for signal " << signal << ", error = " << std::strerror(errno));
            }
        }

        // Unblock all signals handlers are registered
        if (pthread_sigmask(SIG_UNBLOCK, &sigSet, nullptr) != 0)
        {
            //DEBUG_LOG("Failed to unblock signals, error = " << std::strerror(errno));
        }
    }

    // Signal handler: deferred handling
    // NOTE: If multiple signals arrive rapidly, all are preserved in pendingSignals.
    static void signal_handler(int signal) noexcept {
        // Don't process signals until initialized
        if (monitorThreadState.load(std::memory_order_acquire) != ThreadState::Running)
            return;

        int bitPos = signal_to_bit(signal);
        // Unknown signal
        if (bitPos == INVALID_SIGNAL)
            return;

        // Set the signal bit
        pendingSignals.fetch_or(bit(bitPos), std::memory_order_release);

        // Guard against uninitialized pipe before writing (Additional safety)
        int fd1 = signalPipeFds[1].load(std::memory_order_acquire);
        if (fd1 < 0)
            return;  // Pipe not initialized yet, skip notification

        // Always notify (idempotent, safe)
        // Notify via pipe
        ssize_t r;
        do
        {
            char byte = 1;

            r = write(fd1, &byte, 1);
        } while (r == -1 && errno == EINTR);

        // Ignore EAGAIN (pipe full) - pendingSignals still tracks signals
        if (r == -1 && errno != EAGAIN)
            write_to_stderr("Failed to write to signal pipe\n");
    }

    // Monitor thread: waits for pipe, cleans memory, restores default, re-raises
    static void start_monitor_thread() noexcept {
        //DEBUG_LOG("Starting shared memory cleanup monitor thread.");

        ThreadState expected = ThreadState::NotStarted;
        if (!monitorThreadState.compare_exchange_strong(expected, ThreadState::Running,
                                                        std::memory_order_acq_rel))
            // Thread already started or shutting down
            return;

        monitorThread = std::thread([]() noexcept {
            FlagsGuard pendingSignalsGuard(pendingSignals);

            while (monitorThreadState.load(std::memory_order_acquire) != ThreadState::Shutdown)
            {
                // Pipe closed, exit thread
                int fd0 = signalPipeFds[0].load(std::memory_order_acquire);
                if (fd0 == -1)
                    break;

                char byte;
                // Block wait for notification
                ssize_t n = read(fd0, &byte, 1);
                if (n == -1)
                {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN)
                    {
                        std::this_thread::yield();
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }
                    break;
                }
                /*
                // Better error handling
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;

                    break;  // Real error or pipe closed
                }
                */
                if (n == 0)
                    break;  // EOF

                // Get and clear all pending signals atomically
                // Multiple signals of the same type are coalesced; all signals are processed in batches
                std::uint64_t signals = pendingSignals.exchange(0, std::memory_order_acquire);

                if (signals == 0)
                    continue;

                // Process all pending signals for cleanup, but only re-raise the first one
                bool first = true;
                for (std::size_t bitPos = 0; bitPos < SIGNALS.size(); ++bitPos)
                {
                    if ((signals & bit(bitPos)) == 0)
                        continue;

                    int signal = SIGNALS[bitPos];

                    if (signal_graceful(signal))
                        if (!cleanupDone.exchange(true, std::memory_order_acq_rel))
                            // Perform safe partial cleanup (once per batch)
                            SharedMemoryRegistry::cleanup(true);

                    // Restore default handler
                    struct sigaction sigAction{};
                    sigAction.sa_handler = SIG_DFL;
                    sigemptyset(&sigAction.sa_mask);
                    sigAction.sa_flags = 0;

                    if (sigaction(signal, &sigAction, nullptr) != 0)
                    {
                        //DEBUG_LOG("Failed to restore default handler for signal " << signal << ", error = " << std::strerror(errno));

                        // Exit with appropriate exit code
                        _Exit(128 + signal);
                    }

                    // Only re-raise the first signal found
                    if (first)
                    {
                        //DEBUG_LOG("Re-raise for signal " << signal);

                        first = false;
                        // Re-raise
                        ::raise(signal);
                        // Fallback exit if ::raise() returns, ensures proper termination with appropriate exit code intentionally
                        _Exit(128 + signal);
                    }
                }
            }

            // "Final publish" pattern
            // Publish final stopped state, regardless of whether it was told to shutdown early or finished naturally
            monitorThreadState.store(ThreadState::Shutdown, std::memory_order_release);
        });
    }

    // Wake monitor thread
    static void wake_monitor_thread() noexcept {
        constexpr std::size_t MaxAttempt = 4;

        int fd1 = signalPipeFds[1].load(std::memory_order_acquire);
        // Pipe not initialized, skip notification
        if (fd1 == -1)
            return;

        for (std::size_t attempt = 0;; ++attempt)
        {
            char byte = 0;

            ssize_t r = write(fd1, &byte, 1);
            if (r != -1)
                break;  // Success

            if (attempt >= MaxAttempt)
                break;
            if (errno == EINTR)
                continue;  // Retry
            if (errno == EAGAIN)
            {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            // Unexpected error: print once, then stop trying
            write_to_stderr("Failed to wake monitor thread\n");
            break;
        }
    }

    static void cleanup_on_exit() noexcept {
        //DEBUG_LOG("SharedMemoryCleanupManager: Performing atexit cleanup.");

        // No more work is allowed, monitor thread must stop as soon as possible
        monitorThreadState.store(ThreadState::Shutdown, std::memory_order_release);

        wake_monitor_thread();  // unblock read

        if (monitorThread.joinable())
            monitorThread.join();

        close_signal_pipe();

        //if (!
        cleanupDone.exchange(true, std::memory_order_acq_rel);
        //)
        SharedMemoryRegistry::cleanup();
    }

    static void close_signal_pipe() noexcept {

        // 1. Close pipe safely
        int fd0 = signalPipeFds[0].load(std::memory_order_acquire);
        int fd1 = signalPipeFds[1].load(std::memory_order_acquire);
        if (fd0 != -1)
            close(fd0);
        if (fd1 != -1)
            close(fd1);

        // 2. Reset pipe descriptors
        reset_signal_pipe();
    }

    static void reset_signal_pipe() noexcept {
        signalPipeFds[0].store(-1, std::memory_order_release);
        signalPipeFds[1].store(-1, std::memory_order_release);
    }

    static bool valid_signal_pipe() noexcept {
        return signalPipeFds[0].load(std::memory_order_acquire) != -1
            && signalPipeFds[1].load(std::memory_order_acquire) != -1;
    }

    // Map signal numbers to bit positions (0-11 for your 12 signals)
    static constexpr int signal_to_bit(int signal) noexcept {
        for (std::size_t bitPos = 0; bitPos < SIGNALS.size(); ++bitPos)
            if (SIGNALS[bitPos] == signal)
                return bitPos;
        // Not listed
        return INVALID_SIGNAL;
    }

    static bool signal_graceful(int signal) noexcept {
        switch (signal)
        {
            // clang-format off
        case SIGHUP : case SIGINT : case SIGTERM : case SIGQUIT :
            return true;
        default :;
            // clang-format on
        }
        return false;
    }

    static void write_to_stderr(const char* msg) noexcept {
        ssize_t n = write(STDERR_FILENO, msg, std::size_t(std::strlen(msg)));
        if (n == -1)
        {}  // handle error, or ignore safely
    }

    SharedMemoryCleanupManager() noexcept                                             = delete;
    ~SharedMemoryCleanupManager() noexcept                                            = delete;
    SharedMemoryCleanupManager(const SharedMemoryCleanupManager&) noexcept            = delete;
    SharedMemoryCleanupManager(SharedMemoryCleanupManager&&) noexcept                 = delete;
    SharedMemoryCleanupManager& operator=(const SharedMemoryCleanupManager&) noexcept = delete;
    SharedMemoryCleanupManager& operator=(SharedMemoryCleanupManager&&) noexcept      = delete;

    // Thread state machine:
    // NotStarted -> Running (on thread creation)
    // Running -> Shutdown (on thread exit OR atexit cleanup)
    enum class ThreadState : std::uint8_t {
        NotStarted,
        Running,
        Shutdown
    };

    // All handled signals, available at compile-time
    static constexpr StdArray<int, 12> SIGNALS{SIGHUP,  SIGINT,  SIGQUIT, SIGILL, SIGABRT, SIGFPE,
                                               SIGSEGV, SIGTERM, SIGBUS,  SIGSYS, SIGXCPU, SIGXFSZ};

    static constexpr int INVALID_SIGNAL = -1;

    static inline CallOnce                      callOnce;
    static inline std::atomic<std::uint64_t>    pendingSignals{0};
    static inline StdArray<std::atomic<int>, 2> signalPipeFds{-1, -1};
    static inline std::atomic<ThreadState>      monitorThreadState{ThreadState::NotStarted};
    static inline std::thread                   monitorThread;
    static inline std::atomic<bool>             cleanupDone{false};
};

struct MutexAttrGuard final {
   public:
    explicit MutexAttrGuard(pthread_mutexattr_t& mutexAttrRef) noexcept :
        mutexAttr(mutexAttrRef) {}

    ~MutexAttrGuard() noexcept { destroy(); }

    void destroy() noexcept { pthread_mutexattr_destroy(&mutexAttr); }

   private:
    pthread_mutexattr_t& mutexAttr;
};

struct ShmHeader final {
   public:
    ~ShmHeader() noexcept {
        if (!is_initialized())
            return;

        unlock_mutex();

        destroy_mutex();
    }

    // Initialize the mutex
    [[nodiscard]] bool initialize_mutex() noexcept {
        pthread_mutexattr_t mutexAttr;

        if (pthread_mutexattr_init(&mutexAttr) != 0)
            return false;

        MutexAttrGuard mutexAttrGuard{mutexAttr};

        if (pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_SHARED) != 0)
            return false;

    #if _POSIX_C_SOURCE >= 200809L
        if (pthread_mutexattr_setrobust(&mutexAttr, PTHREAD_MUTEX_ROBUST) != 0)
            return false;
    #endif

        if (pthread_mutex_init(&mutex, &mutexAttr) != 0)
            return false;

        set_initialized(true);

        set_ref_count(0);

        return true;
    }

    // Destroy the mutex
    void destroy_mutex() noexcept { pthread_mutex_destroy(&mutex); }

    // Lock the mutex
    [[nodiscard]] bool lock_mutex() noexcept {

        while (true)
        {
            int rc = pthread_mutex_lock(&mutex);

            // Locked successfully
            if (rc == 0)
                return true;

    #if _POSIX_C_SOURCE >= 200809L
            if (rc == EOWNERDEAD)
            {
                // Previous owner died, try to make mutex consistent
                if (pthread_mutex_consistent(&mutex) == 0)
                    return true;

                break;
            }
    #endif
            // Some real error occurred
            if (rc != EINTR)
                break;
        }

        return false;
    }

    // Unlock the mutex
    void unlock_mutex() noexcept {
        [[maybe_unused]] int rc = pthread_mutex_unlock(&mutex);
        assert(rc == 0 || rc == EPERM || rc == EOWNERDEAD);
    }

    [[nodiscard]] bool is_initialized() const noexcept {
        return initialized.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint32_t ref_count() const noexcept {
        return refCount.load(std::memory_order_acquire);
    }

    void set_initialized(bool init) noexcept { initialized.store(init, std::memory_order_release); }

    void set_ref_count(std::uint32_t count) noexcept {
        refCount.store(count, std::memory_order_release);
    }
    void increment_ref_count() noexcept { refCount.fetch_add(1, std::memory_order_acq_rel); }
    void decrement_ref_count() noexcept { refCount.fetch_sub(1, std::memory_order_acq_rel); }

    static constexpr std::uint32_t MAGIC = 0xAD5F1A12U;

    const std::uint32_t magic = MAGIC;

   private:
    pthread_mutex_t            mutex{};
    std::atomic<bool>          initialized{false};
    std::atomic<std::uint32_t> refCount{0};
};

struct ShmHeaderGuard final {
   public:
    explicit ShmHeaderGuard(ShmHeader& shmHeaderRef) noexcept :
        shmHeader(shmHeaderRef),
        ownsLock(shmHeader.lock_mutex()) {
        assert(ownsLock);
    }

    ~ShmHeaderGuard() noexcept { unlock(); }

    bool owns_lock() const noexcept { return ownsLock; }

    void unlock() noexcept {
        if (owns_lock())
        {
            shmHeader.unlock_mutex();
            ownsLock = false;
        }
    }

   private:
    ShmHeaderGuard(const ShmHeaderGuard&)            = delete;
    ShmHeaderGuard(ShmHeaderGuard&&)                 = delete;
    ShmHeaderGuard& operator=(const ShmHeaderGuard&) = delete;
    ShmHeaderGuard& operator=(ShmHeaderGuard&&)      = delete;

    ShmHeader& shmHeader;
    bool       ownsLock;
};

template<typename T>
class SharedMemory final: public BaseSharedMemory {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   public:
    explicit SharedMemory(std::string_view shmName) noexcept :
        BaseSharedMemory(shmName),
        mappedSize(mapped_size()),
        sentinelBase(shmName) {}

    ~SharedMemory() noexcept override { unregister_close(); }

    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& sharedMemory) noexcept :
        BaseSharedMemory(sharedMemory.name_()) {
        move_with_registry(sharedMemory);
    }
    SharedMemory& operator=(SharedMemory&& sharedMemory) noexcept {
        if (this == &sharedMemory)
            return *this;

        unregister_close();

        // Move-assign the base class
        name = sharedMemory.name_();
        move_with_registry(sharedMemory);

        return *this;
    }

    // Open or create the shared memory region and register it
    [[nodiscard]] bool open_register(const T& value) noexcept {

        if (SharedMemoryRegistry::cleanup_in_progress())
        {
            //DEBUG_LOG("Shared memory registry cleanup in progress, cannot open shared memory.");
            return available;
        }

        // Try to open or create the shared memory region
        bool staleRetried = false;

        while (true)
        {
            if (is_open())
            {
                //DEBUG_LOG("Shared memory already open.");
                break;
            }

            // Try to create new shared memory region
            bool newCreated = false;

            mode_t mode;
            int    oflag;

            oflag = O_CREAT | O_EXCL | O_RDWR;
            mode  = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
            fd    = shm_open(name_().data(), oflag, mode);

            if (fd <= INVALID_FD)
            {
                oflag = O_RDWR;
                fd    = shm_open(name_().data(), oflag, mode);

                if (fd <= INVALID_FD)
                {
                    //DEBUG_LOG("Failed to open shared memory, error = " << std::strerror(errno));
                    break;
                }
            }
            else
            {
                //DEBUG_LOG("Created new shared memory region: " << name_());
                newCreated = true;
            }

            // Resize the shared memory region if newly created
            bool lockFile = lock_file(LOCK_EX);

            if (!lockFile)
            {
                //DEBUG_LOG("Failed to lock shared memory file, error = " << std::strerror(errno));

                cleanup(false, lockFile);

                break;
            }

            // Track if header is invalid
            bool headerInvalid = false;

            // Resize only if newly created
            bool success = newCreated  //
                           ? setup_new_region(value)
                           : setup_existing_region(headerInvalid);

            if (!success)
            {
                cleanup(newCreated || headerInvalid, lockFile);

                if (!newCreated && headerInvalid && !staleRetried)
                {
                    //DEBUG_LOG("Retrying due to stale shared memory region.");

                    staleRetried = true;

                    continue;
                }

                //DEBUG_LOG("Failed to setup shared memory region.");
                break;
            }

            if (shmHeader == nullptr)
            {
                cleanup(newCreated, lockFile);

                if (!newCreated && !staleRetried)
                {
                    //DEBUG_LOG("Retrying due to null shared memory header.");

                    staleRetried = true;

                    continue;
                }

                //DEBUG_LOG("Shared memory header is null.");
                break;
            }

            // RAII mutex scope lock
            {
                ShmHeaderGuard shmHeaderGuard(*shmHeader);

                if (!shmHeaderGuard.owns_lock())
                {
                    cleanup(newCreated, lockFile);

                    if (!newCreated && !staleRetried)
                    {
                        //DEBUG_LOG("Retrying due to mutex lock failure.");

                        staleRetried = true;

                        continue;
                    }

                    //DEBUG_LOG("Failed to lock shared memory header mutex.");
                    break;
                }

                if (!sentinel_file_locked_created())
                {
                    //DEBUG_LOG("Failed to create sentinel file.");

                    shmHeaderGuard.unlock();

                    cleanup(newCreated, lockFile);
                    break;
                }

                increment_ref_count();

            }  // <-- mutex automatically unlocked here safely

            unlock_file();

            available = true;

            // Register this new resource
            SharedMemoryRegistry::attempt_register_memory(this);

            break;
        }

        return available;
    }

    void close(bool skipUnmapRegion = false) noexcept override {
        if (fd <= INVALID_FD && mappedPtr == INVALID_MMAP_PTR)
            return;

        bool removeRegion = false;
        bool lockFile     = lock_file(LOCK_EX);

        if (lockFile && shmHeader != nullptr)
        {
            // RAII mutex lock
            ShmHeaderGuard shmHeaderGuard(*shmHeader);

            handle_ref_count_and_sentinel_file();

            if (shmHeaderGuard.owns_lock())
            {
                // Mutex locked: check if the region should be removed
                removeRegion = !has_other_live_sentinels_locked();
            }
        }
        else
        {
            // File lock failed or no header
            handle_ref_count_and_sentinel_file();
        }

        cleanup(removeRegion, lockFile, skipUnmapRegion);
    }

    [[nodiscard]] bool is_available() const noexcept { return available; }

    [[nodiscard]] bool is_open() const noexcept {
        return fd > INVALID_FD && mappedPtr != nullptr && dataPtr != nullptr;
    }

    [[nodiscard]] const T& get() const noexcept { return *dataPtr; }

    [[nodiscard]] const T* operator->() const noexcept { return dataPtr; }

    [[nodiscard]] const T& operator*() const noexcept { return *dataPtr; }

    [[nodiscard]] bool is_initialized() const noexcept {
        return shmHeader != nullptr ? shmHeader->is_initialized() : false;
    }

    [[nodiscard]] std::uint32_t ref_count() const noexcept {
        return shmHeader != nullptr ? shmHeader->ref_count() : 0;
    }

    bool is_valid() const noexcept { return is_open() && is_initialized(); }

   private:
    static constexpr std::size_t mapped_size() noexcept { return sizeof(T) + sizeof(ShmHeader); }

    static bool is_pid_alive(pid_t pid) noexcept {
        if (pid <= 0)
            return false;

        if (kill(pid, 0) == 0)
            return true;

        // If kill() failed, ESRCH means the pid dead or does not exist;
        // any other errno (EPERM etc) indicates the pid may still exist
        // but lack permission to query it.
        return errno != ESRCH;
    }

    // Unregister SharedMemory object and release resources
    void unregister_close() noexcept {
        // 1. Unregister from registry
        SharedMemoryRegistry::unregister_memory(this);

        // 2. Close and release
        close();
    }

    // Move the contents of another SharedMemory object into this one, updating the registry accordingly
    void move_with_registry(SharedMemory& sharedMemory) noexcept {
        // 1. Unregister source while it's intact
        [[maybe_unused]] bool unregistered = SharedMemoryRegistry::unregister_memory(&sharedMemory);
        //assert(unregistered && "SharedMemory not registered");

        // 2. Move members
        fd           = sharedMemory.fd;
        mappedPtr    = sharedMemory.mappedPtr;
        dataPtr      = sharedMemory.dataPtr;
        shmHeader    = sharedMemory.shmHeader;
        mappedSize   = sharedMemory.mappedSize;
        sentinelBase = std::move(sharedMemory.sentinelBase);
        sentinelPath = std::move(sharedMemory.sentinelPath);

        // 3. Reset source
        sharedMemory.reset();

        // 4. Register this new resource
        SharedMemoryRegistry::attempt_register_memory(this);
    }

    void reset() noexcept {

        fdGuard.release();

        mappedGuard.release();

        dataPtr   = nullptr;
        shmHeader = nullptr;

        clear_sentinel_path();
    }

    void unmap_region() noexcept {
        if (mappedPtr == INVALID_MMAP_PTR)
            return;

        mappedGuard.close();

        dataPtr   = nullptr;
        shmHeader = nullptr;
    }

    [[nodiscard]] bool lock_file(int operation) noexcept {
        if (fd <= INVALID_FD)
            return false;

        while (true)
        {
            if (flock(fd, operation) == 0)
                return true;

            if (errno == EINTR)
                continue;  // retry if interrupted by signal

            if (errno == EWOULDBLOCK || errno == EAGAIN)  // for LOCK_NB: lock is busy
                return false;

            break;  // real error
        }

        return false;
    }

    void unlock_file() noexcept {
        if (fd <= INVALID_FD)
            return;

        while (true)
        {
            if (flock(fd, LOCK_UN) == 0)
                break;

            if (errno == EINTR)
                continue;  // retry on signal

            break;  // ignore other errors (nothing useful to do)
        }
    }

    void set_sentinel_path(pid_t pid) noexcept {
        std::filesystem::path p(DIRECTORY);
        p /= sentinelBase;
        p += ".";
        p += std::to_string(pid);

        sentinelPath = p.string();
    }

    void clear_sentinel_path() noexcept { sentinelPath.clear(); }

    void increment_ref_count() noexcept {
        if (shmHeader != nullptr)
            shmHeader->increment_ref_count();
    }

    void decrement_ref_count() noexcept {
        if (shmHeader != nullptr)
            shmHeader->decrement_ref_count();
    }

    bool sentinel_file_locked_created() noexcept {
        constexpr std::size_t MaxAttempt = 4;

        if (shmHeader == nullptr)
            return false;

        pid_t selfPid = getpid();

        set_sentinel_path(selfPid);

        for (std::size_t attempt = 0;; ++attempt)
        {
            int    oflag = O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC;
            mode_t mode  = S_IRUSR | S_IWUSR;
            int    tmpFd = ::open(sentinelPath.c_str(), oflag, mode);

            FdGuard tmpFdGuard(tmpFd);

            if (tmpFd > INVALID_FD)
                return true;

            if (errno != EEXIST)
                break;

            ::unlink(sentinelPath.c_str());

            decrement_ref_count();

            if (attempt >= MaxAttempt)
                break;
        }

        clear_sentinel_path();

        return false;
    }

    void remove_sentinel_file() noexcept {
        if (sentinelPath.empty())
            return;

        ::unlink(sentinelPath.c_str());

        clear_sentinel_path();
    }

    void handle_ref_count_and_sentinel_file() noexcept {

        decrement_ref_count();

        remove_sentinel_file();
    }

    bool has_other_live_sentinels_locked() const noexcept {
        DIR* dir = opendir(DIRECTORY.data());

        if (dir == nullptr)
            return false;

        std::string prefix;
        prefix.reserve(sentinelBase.size() + 1);

        prefix = sentinelBase;
        prefix += '.';

        bool found = false;

        // Iterate directory entries
        while (dirent* entry = readdir(dir))
        {
            std::string entryName = entry->d_name;

            // Check if entryName starts with prefix
            if (entryName.size() < prefix.size()
                || entryName.compare(0, prefix.size(), prefix) != 0)
                continue;

            // Extract PID part
            auto pidStr = entryName.substr(prefix.size());

            char* endPtr = nullptr;
            long  pidVal = std::strtol(pidStr.c_str(), &endPtr, 10);

            if (endPtr == nullptr || *endPtr != '\0')
                continue;

            // Get PID
            pid_t pid = pid_t(pidVal);

            // Check if process is alive
            if (is_pid_alive(pid))
            {
                found = true;

                break;
            }

            std::string stalePath{DIRECTORY};
            stalePath += entryName;

            ::unlink(stalePath.c_str());

            const_cast<SharedMemory*>(this)->decrement_ref_count();
        }

        // Close directory
        closedir(dir);

        return found;
    }

    // Setup new shared memory region
    [[nodiscard]] bool setup_new_region(const T& value) noexcept {
        off_t offset = 0;

    #if defined(__APPLE__)
        // macOS: Preallocate space, then set file size
        fstore_t store{};
        // First, try contiguous
        store.fst_flags   = F_ALLOCATECONTIG;
        store.fst_posmode = F_PEOFPOSMODE;
        store.fst_offset  = offset;
        store.fst_length  = mappedSize;

        int rc = fcntl(fd, F_PREALLOCATE, &store);
        // Contiguous allocation failed
        if (rc == -1)
        {
            // Now, try non-contiguous
            store.fst_flags = F_ALLOCATEALL;

            rc = fcntl(fd, F_PREALLOCATE, &store);
            // Non-contiguous allocation failed
            if (rc == -1)
            {
                //DEBUG_LOG("fcntl() failed, error = " << std::strerror(errno));
                return false;
            }
        }

        // Actually set the file size (F_PREALLOCATE doesn't do this)
        if (ftruncate(fd, off_t(offset + mappedSize)) == -1)
        {
            //DEBUG_LOG("ftruncate() failed, error = " << std::strerror(errno));
            return false;
        }
    #else
        // Linux/POSIX: Use posix_fallocate (atomically allocates and sets size)
        if (posix_fallocate(fd, offset, mappedSize) != 0)
        {
            //DEBUG_LOG("posix_fallocate() failed, error = " << std::strerror(errno));
            return false;
        }
    #endif

        mappedPtr = mmap(nullptr, mappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

        if (mappedPtr == MAP_FAILED)
        {
            mappedPtr = INVALID_MMAP_PTR;

            return false;
        }

        dataPtr = static_cast<T*>(mappedPtr);

        shmHeader = reinterpret_cast<ShmHeader*>(static_cast<char*>(mappedPtr) + sizeof(T));

        new (shmHeader) ShmHeader{};

        new (dataPtr) T{value};

        if (shmHeader == nullptr || !shmHeader->initialize_mutex())
            return false;

        return true;
    }

    // Setup existing shared memory region
    [[nodiscard]] bool setup_existing_region(bool& headerInvalid) noexcept {
        headerInvalid = false;

        struct stat Stat{};

        if (fstat(fd, &Stat) == -1)
            return false;

        if (std::size_t(Stat.st_size) < mappedSize)
            return false;

        mappedPtr = mmap(nullptr, mappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

        if (mappedPtr == MAP_FAILED)
        {
            mappedPtr = INVALID_MMAP_PTR;

            return false;
        }

        dataPtr = static_cast<T*>(mappedPtr);

        shmHeader =
          std::launder(reinterpret_cast<ShmHeader*>(static_cast<char*>(mappedPtr) + sizeof(T)));

        if (shmHeader == nullptr)
            return false;

        if (!shmHeader->is_initialized() || shmHeader->magic != ShmHeader::MAGIC)
        {
            headerInvalid = true;

            return false;
        }

        return true;
    }

    void
    cleanup(bool removeRegion = true, bool lockFile = true, bool skipUnmapRegion = false) noexcept {

        if (!skipUnmapRegion)
            unmap_region();

        if (lockFile)
            unlock_file();

        if (removeRegion)
            shm_unlink(name_().data());

        fdGuard.close();

        if (!skipUnmapRegion)
            reset();
    }

    static constexpr std::string_view DIRECTORY{"/dev/shm/"};

    bool        available = false;
    int         fd        = INVALID_FD;
    FdGuard     fdGuard{fd};
    void*       mappedPtr  = INVALID_MMAP_PTR;
    std::size_t mappedSize = INVALID_MMAP_SIZE;
    MMapGuard   mappedGuard{mappedPtr, mappedSize};
    T*          dataPtr   = nullptr;
    ShmHeader*  shmHeader = nullptr;
    std::string sentinelBase;
    std::string sentinelPath;
};

template<typename T>
class BackendSharedMemory final {
   public:
    BackendSharedMemory() noexcept { SharedMemoryCleanupManager::ensure_initialized(); }

    BackendSharedMemory(std::string_view shmName, const T& value) noexcept :
        shm(shmName) {
        SharedMemoryCleanupManager::ensure_initialized();

        initialized = shm.open_register(value);
    }

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept            = default;
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept = default;

    bool is_valid() const noexcept { return initialized && shm.is_valid(); }

    void* get() const noexcept {
        return is_valid() ? reinterpret_cast<void*>(const_cast<T*>(&shm.get())) : nullptr;
    }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return is_valid() ? SharedMemoryAllocationStatus::SharedMemory
                          : SharedMemoryAllocationStatus::NoAllocation;
    }

    std::string_view get_error_message() const noexcept {
        if (!initialized)
            return "Shared memory not created.";
        if (!shm.is_available())
            return "Shared memory not available.";
        if (!shm.is_open())
            return "Shared memory is not open.";
        if (!shm.is_initialized())
            return "Shared memory is not initialized.";
        return {};
    }

   private:
    SharedMemory<T> shm;
    bool            initialized = false;
};
#else
// For systems that don't have shared memory, or support is troublesome.
// The way fallback is done is that need a dummy backend.
template<typename T>
class BackendSharedMemory final {
   public:
    BackendSharedMemory() = default;

    BackendSharedMemory([[maybe_unused]] std::string_view shmName,
                        [[maybe_unused]] const T&         value) noexcept {}

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept            = default;
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept = default;

    bool is_valid() const noexcept { return false; }

    void* get() const noexcept { return nullptr; }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return SharedMemoryAllocationStatus::NoAllocation;
    }

    std::string_view get_error_message() const noexcept {
        return "Shared memory: [Dummy] (non-functional).";
    }
};
#endif

template<typename T>
struct FallbackBackendSharedMemory final {
   public:
    FallbackBackendSharedMemory() noexcept = default;

    FallbackBackendSharedMemory([[maybe_unused]] std::string_view shmName, const T& value) noexcept
        :
        fallbackObj(make_unique_aligned_large_page<T>(value)) {}

    FallbackBackendSharedMemory(const FallbackBackendSharedMemory&) noexcept            = delete;
    FallbackBackendSharedMemory& operator=(const FallbackBackendSharedMemory&) noexcept = delete;

    FallbackBackendSharedMemory(FallbackBackendSharedMemory&& fallbackBackendShm) noexcept :
        fallbackObj(std::move(fallbackBackendShm.fallbackObj)) {}
    FallbackBackendSharedMemory&
    operator=(FallbackBackendSharedMemory&& fallbackBackendShm) noexcept {
        fallbackObj = std::move(fallbackBackendShm.fallbackObj);
        return *this;
    }

    void* get() const noexcept { return fallbackObj.get(); }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return fallbackObj != nullptr ? SharedMemoryAllocationStatus::LocalMemory
                                      : SharedMemoryAllocationStatus::NoAllocation;
    }

    std::string_view get_error_message() const noexcept {
        if (fallbackObj == nullptr)
            return "Shared memory not created.";
        return "Shared memory not supported by the OS. Local allocation fallback.";
    }

   private:
    LargePagePtr<T> fallbackObj;
};

// Platform-independent wrapper
template<typename T>
struct SystemWideSharedMemory final {
   public:
    // Can't run the destructor because it may be in a completely different process.
    // The object stored must also be obviously in-line but can't check for that,
    // other than some basic checks that cover most cases.
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_trivially_move_constructible_v<T>);
    static_assert(std::is_trivially_copy_constructible_v<T>);

    SystemWideSharedMemory() noexcept = default;

    // Content is addressed by its hash.
    // An additional discriminator can be added to account for differences
    // that are not present in the content, for example NUMA node allocation.
    SystemWideSharedMemory(const T& value, std::uint64_t discriminator = 0) noexcept {

        std::string shmName{"DON_"};

        // Create a unique name based on the value, executable path, and discriminator
        // 3 hex digits per 64-bit part + 2 dollar signs + null terminator
        constexpr std::size_t BufferSize = 3 * HEX64_SIZE + 2 + 1;
        // Build the three-part hex identifier safely into a temporary buffer
        StdArray<char, BufferSize> buffer{};

        std::uint64_t valueHash      = std::hash<T>{}(value);
        std::uint64_t executableHash = hash_string(executable_path());

        // snprintf returns the number of chars that would have been written (excluding NUL)
        int writtenSize = std::snprintf(buffer.data(), buffer.size(),
                                        "%016" PRIX64 "$"  //
                                        "%016" PRIX64 "$"  //
                                        "%016" PRIX64,     //
                                        valueHash, executableHash, discriminator);

        std::string hashName;

        if (writtenSize >= 0)
        {
            // Ensure size is within bounds
            // If snprintf truncated, use up to (buf.size() - 1) characters
            std::size_t copySize = std::min<std::size_t>(writtenSize, buffer.size() - 1);
            // Shrink to actual content
            hashName.assign(buffer.data(), copySize);
        }
        else
        {
            // snprintf failed - use fallback format
            // This should never happen, but handle it anyway
            //DEBUG_LOG("snprintf() failed, using fallback hash name");

            // Fallback: use hex representation directly
            std::ostringstream oss{};
            oss << std::hex << std::setfill('0')           //
                << std::setw(16) << valueHash << '$'       //
                << std::setw(16) << executableHash << '$'  //
                << std::setw(16) << discriminator;
            hashName = oss.str();
        }

        shmName += hashName;

#if defined(__linux__) && !defined(__ANDROID__)
        // POSIX APIs expect a fixed-size C string where the maximum length excluding the terminating null character ('\0').
        // Since std::string::size() does not include '\0', allow at most (MAX - 1) characters,
        // to guarantee space for the terminator ('\0') in fixed-size buffers.
        constexpr std::size_t MaxNameSize = SHM_NAME_MAX_SIZE > 0 ? SHM_NAME_MAX_SIZE - 1 : 255 - 1;
        // Truncate the name if necessary so that it fits within limits including the null terminator
        if (shmName.size() > MaxNameSize)
            shmName.resize(MaxNameSize);
#endif

        BackendSharedMemory<T> backendShmT(shmName, value);

        if (backendShmT.is_valid())
            backendShm = std::move(backendShmT);
        else
            backendShm = FallbackBackendSharedMemory<T>(shmName, value);
    }

    SystemWideSharedMemory(const SystemWideSharedMemory&) noexcept            = delete;
    SystemWideSharedMemory& operator=(const SystemWideSharedMemory&) noexcept = delete;

    SystemWideSharedMemory(SystemWideSharedMemory&& systemWideShm) noexcept :
        backendShm(std::move(systemWideShm.backendShm)) {}
    SystemWideSharedMemory& operator=(SystemWideSharedMemory&& systemWideShm) noexcept {
        backendShm = std::move(systemWideShm.backendShm);
        return *this;
    }

    const T& operator*() const noexcept {
        return *std::launder(reinterpret_cast<const T*>(get_ptr()));
    }

    bool operator==(std::nullptr_t) const noexcept { return get_ptr() == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return !(*this == nullptr); }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return std::visit(
          [](const auto& end) -> SharedMemoryAllocationStatus {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
                  return SharedMemoryAllocationStatus::NoAllocation;
              else
                  return end.get_status();
          },
          backendShm);
    }

    std::string_view get_error_message() const noexcept {
        return std::visit(
          [](const auto& end) -> std::string_view {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
                  return {};
              else
                  return end.get_error_message();
          },
          backendShm);
    }

   private:
    auto get_ptr() const noexcept {
        return std::visit(
          [](const auto& end) -> void* {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
                  return nullptr;
              else
                  return end.get();
          },
          backendShm);
    }

    std::variant<std::monostate, BackendSharedMemory<T>, FallbackBackendSharedMemory<T>> backendShm;
};

}  // namespace DON

#endif  // #ifndef SHM_H_INCLUDED
