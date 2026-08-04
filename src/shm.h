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
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(_WIN32)
    #if !defined(PATH_MAX)
        #define PATH_MAX (2 * 1024)  // 2K bytes, safe for almost all paths
    #endif
    #if !defined(NAME_MAX)
        #define NAME_MAX 255
    #endif

    // Standard portable pattern for spin-wait / CPU pause hint
    #if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
        #include <emmintrin.h>  // x86/x64: SSE2 use _mm_pause()
        #define PAUSE() _mm_pause()
    #else
        // Fallback: portable C++ hint (PowerPC, RISC-V, MIPS, etc.)
        #include <thread>
        #define PAUSE() std::this_thread::yield()
    #endif
    #include "platform_win.h"
#elif defined(__ANDROID__)
    // Android-specific configuration (currently none)
#elif (defined(__linux__) && !defined(__ANDROID__)) /* Linux (non-Android) */ \
  || defined(__APPLE__)                             /* macOS / iOS */ \
  || defined(__sun)                                 /* Solaris */ \
  || defined(__FreeBSD__)                           /* FreeBSD */ \
  || defined(__OpenBSD__)                           /* OpenBSD */ \
  || defined(__NetBSD__)                            /* NetBSD */ \
  || defined(__DragonFly__)                         /* DragonFly BSD */ \
  || defined(_AIX)                                  /* IBM AIX */
    #define USE_UNIX_SHM

    #include <dirent.h>
    #include <fcntl.h>
    #include <limits.h>
    #include <poll.h>
    #include <sys/file.h>
    #include <sys/mman.h>
    #include <sys/socket.h>
    #include <sys/stat.h>
    #include <sys/time.h>
    #include <sys/types.h>
    #include <sys/uio.h>
    #include <sys/un.h>
    #include <unistd.h>

    #include <atomic>
    #include <cassert>
    #include <cerrno>
    #include <chrono>
    #include <condition_variable>
    #include <cstring>
    #include <list>
    #include <mutex>
    #include <optional>
    #include <shared_mutex>
    #include <thread>
    #include <unordered_map>

    // Linux (non-Android)
    #if (defined(__linux__) && !defined(__ANDROID__))
    // macOS / iOS
    #elif defined(__APPLE__)
        #include <mach-o/dyld.h>
        #include <sys/syslimits.h>
    // Solaris / OpenSolaris / illumos
    #elif defined(__sun)
        #include <stdlib.h>
    // FreeBSD
    #elif defined(__FreeBSD__)
        #include <sys/sysctl.h>
    // OpenBSD
    #elif defined(__OpenBSD__)
    // NetBSD
    #elif defined(__NetBSD__)
    // DragonFly BSD
    #elif defined(__DragonFly__)
    // IBM AIX
    #elif defined(_AIX)
    #else
        #error "Unsupported Unix platform"
    #endif
#endif

#include "memory.h"
#include "misc.h"

namespace DON {

inline constexpr usize SHM_NAME_MAX = NAME_MAX > 0 ? NAME_MAX - 1 : 255 - 1;

// argv[0] CANNOT be used because need to identify the executable.
// argv[0] contains the command used to invoke it, which does not involve the full path.
// Just using a path is not fully resilient either, as the executable could have changed
// if it wasn't locked by the OS. If the path is longer than 4095 bytes the hash will be computed
// from an unspecified amount of bytes of the path; in particular it can a hash of an empty string.

enum class SharedMemoryAllocationStatus : u8 {
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
    Array<char, PATH_MAX> executablePath{};
    usize                 executableSize = 0;

#if defined(_WIN32)
    DWORD size = GetModuleFileName(nullptr, executablePath.data(), DWORD(executablePath.size()));

    executableSize                 = std::min<usize>(size, executablePath.size() - 1);
    executablePath[executableSize] = '\0';
#elif defined(__APPLE__)
    u32 size = u32(executablePath.size());

    if (_NSGetExecutablePath(executablePath.data(), &size) == 0)
    {
        executableSize = std::strlen(executablePath.data());
    }
#elif defined(__sun)  // Solaris
    const char* path = ::getexecname();

    if (path != nullptr)
    {
        std::strncpy(executablePath.data(), path, executablePath.size() - 1);

        // Determine actual length copied
        executableSize                 = std::strnlen(path, executablePath.size() - 1);
        executablePath[executableSize] = '\0';
    }
#elif defined(__FreeBSD__)
    constexpr Array<int, 4> MIB{CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};

    usize size = executablePath.size();

    if (::sysctl(MIB.data(), MIB.size(), executablePath.data(), &size, nullptr, 0) == 0)
    {
        executableSize                 = std::min<usize>(size, executablePath.size() - 1);
        executablePath[executableSize] = '\0';
    }
#elif defined(__OpenBSD__)
    ssize_t size =  //
      ::readlink("/proc/curproc/file", executablePath.data(), executablePath.size() - 1);

    if (size >= 0)
    {
        executableSize                 = std::min<usize>(size, executablePath.size() - 1);
        executablePath[executableSize] = '\0';
    }
#elif defined(__NetBSD__) || defined(__DragonFly__)
    ssize_t size =  //
      ::readlink("/proc/curproc/exe", executablePath.data(), executablePath.size() - 1);

    if (size >= 0)
    {
        executableSize                 = std::min<usize>(size, executablePath.size() - 1);
        executablePath[executableSize] = '\0';
    }
#elif defined(__linux__)
    ssize_t size =  //
      ::readlink("/proc/self/exe", executablePath.data(), executablePath.size() - 1);

    if (size >= 0)
    {
        executableSize                 = std::min<usize>(size, executablePath.size() - 1);
        executablePath[executableSize] = '\0';
    }
#else
    #error "Unsupported platform"
#endif

    // In case of any error the path will be empty
    return std::string{executablePath.data(), executableSize};
}

#if defined(_WIN32)
// Utilizes shared memory to store the value. It is reduplicated system-wide (for the single user)
template<typename T>
class BackendSharedMemory final {
   public:
    enum class Status : u8 {
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
        status(Status::NotInitialized) {}

    BackendSharedMemory(std::string_view shmName, const T& value) noexcept :
        name_(shmName),
        status(Status::NotInitialized) {
        // Windows named shared memory names must start with "Local\" or "Global\"
        constexpr std::string_view Prefix{"Local\\"};
        if (name().size() < Prefix.size() || name().compare(0, Prefix.size(), Prefix) != 0)
            name_.insert(0, Prefix);

        //DEBUG_LOG("Creating shared memory with name: " << name());

        initialize(value);
    }

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept :
        name_(backendShm.name()),
        hMapFile(backendShm.hMapFile),
        hMapFileGuard{hMapFile},
        mappedPtr(backendShm.mappedPtr),
        mappedGuard{mappedPtr},
        status(backendShm.status) {
        //DEBUG_LOG("Moving shared memory, name: " << name());

        backendShm.hMapFile  = INVALID_HANDLE;
        backendShm.mappedPtr = INVALID_MMAP_PTR;
        backendShm.status    = Status::NotInitialized;
    }
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept {
        if (this == &backendShm)
            return *this;

        destroy();

        name_     = backendShm.name();
        hMapFile  = backendShm.hMapFile;
        mappedPtr = backendShm.mappedPtr;
        status    = backendShm.status;

        //DEBUG_LOG("Moving shared memory, name: " << name());

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

    std::string_view name() const noexcept { return name_; }

   private:
    void initialize(const T& value) noexcept {
        constexpr usize TotalSize = sizeof(T) + sizeof(SharedState);

        // Try allocating with large page first
        hMapFile = try_with_windows_lock_memory_privilege(
          [&](usize LargePageSize) noexcept {
              // Round up size to full large page
              usize roundedTotalSize = round_up_to_pow2_multiple(TotalSize, LargePageSize);

    #if defined(_WIN64)
              DWORD hiTotalSize = roundedTotalSize >> 32;
              DWORD loTotalSize = roundedTotalSize & 0xFFFFFFFFu;
    #else
              DWORD hiTotalSize = 0;
              DWORD loTotalSize = roundedTotalSize;
    #endif

              //DEBUG_LOG("Allocating large page shared memory, size = " << roundedTotalSize << " bytes");
              return CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
                                       PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,  //
                                       hiTotalSize, loTotalSize, name().data());
          },
          []() { return INVALID_HANDLE; });

        // Fallback to normal allocation if no large page available
        if (!is_valid_handle(hMapFile))
        {
            //DEBUG_LOG("Allocating normal shared memory, size = " << TotalSize << " bytes");
            hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,  //
                                         0, TotalSize, name().data());
        }

        if (!is_valid_handle(hMapFile))
        {
            //DEBUG_LOG("CreateFileMapping() failed, name = " << name() << ", error = " << error_to_string(GetLastError()));
            status = Status::FileMapping;
            return;
        }

        mappedPtr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, TotalSize);

        if (mappedPtr == INVALID_MMAP_PTR)
        {
            //DEBUG_LOG("MapViewOfFile() failed, name = " << name() << ", error = " << error_to_string(GetLastError()));
            status = Status::MapView;
            cleanup();
            return;
        }

        // Use named mutex to ensure only one initializer
        std::string mutexName{name()};
        mutexName.append("$mutex");

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

        auto* sharedState =
          reinterpret_cast<volatile DWORD*>(reinterpret_cast<char*>(mappedPtr) + sizeof(T));

        // Attempt atomic initialization
        if (InterlockedCompareExchange(sharedState, DWORD(SharedState::Initializing),
                                       DWORD(SharedState::Uninitialized))
            == DWORD(SharedState::Uninitialized))
        {
            // this thread is the initializer
            new (object) T{value};

            // Publish fully constructed object
            InterlockedExchange(sharedState, DWORD(SharedState::Initialized));
        }
        else
        {
            // Wait until construction completes
            while (*sharedState != DWORD(SharedState::Initialized))
                PAUSE();  // portable "pause" for any architecture
        }

        if (!ReleaseMutex(hMutex))
        {
            //DEBUG_LOG("ReleaseMutex() failed, name = " << mutexName << ", error = " << error_to_string(GetLastError()));
            status = Status::MutexRelease;
            cleanup();
            return;
        }

        //DEBUG_LOG("Shared memory initialized successfully, name: " << name());
        status = Status::Success;
    }

    void cleanup() noexcept {
        //DEBUG_LOG("Cleaning up shared memory, name: " << name());
        mappedGuard.reset();
        hMapFileGuard.reset();
    }

    void destroy() noexcept {
        //DEBUG_LOG("Destroying shared memory, name: " << name());
        cleanup();
    }

    enum class SharedState : u8 {
        Uninitialized = 0,
        Initializing  = 1,
        Initialized   = 2
    };

    std::string name_;
    HANDLE      hMapFile = INVALID_HANDLE;
    HandleGuard hMapFileGuard{hMapFile};
    void*       mappedPtr = INVALID_MMAP_PTR;
    MMapGuard   mappedGuard{mappedPtr};
    Status      status = Status::NotInitialized;
};
#elif defined(USE_UNIX_SHM)
enum class CloseType : u8 {
    Normal,
    AtExit,
};

class BaseSharedMemory {
   public:
    explicit BaseSharedMemory(std::string_view shmName) noexcept :
        name_(shmName) {
        // POSIX named shared memory names must start with slash ('/')
        constexpr char Prefix = '/';
        if (name().empty() || name()[0] != Prefix)
            name_.insert(name_.begin(), Prefix);
    }

    BaseSharedMemory(const BaseSharedMemory&)            = delete;
    BaseSharedMemory& operator=(const BaseSharedMemory&) = delete;

    BaseSharedMemory(BaseSharedMemory&&) noexcept            = default;
    BaseSharedMemory& operator=(BaseSharedMemory&&) noexcept = default;

    virtual ~BaseSharedMemory() noexcept = default;

    virtual void close(CloseType closeType) noexcept = 0;

    [[nodiscard]] std::string_view name() const noexcept { return name_; }

   protected:
    std::string name_;
};

// SharedMemoryRegistry
//
// A thread-safe global registry for tracking live shared memory objects
// (BaseSharedMemory) without owning them.
//
// The registry maintains:
//  - True insertion order for deterministic iteration and shutdown
//  - O(1) registration and un-registration via list + hash map
//
// Key Features:
//  - Thread-safe registration and un-registration
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
    static void ensure_initialized(usize reserveCount  = 1024,
                                   float maxLoadFactor = 0.75f) noexcept {
        // Only the parameters from the first call are used
        callOnce([reserveCount, maxLoadFactor]() noexcept {
            //DEBUG_LOG("Initializing SharedMemoryRegistry with reserve-count " << reserveCount << " and max-load-factor " << maxLoadFactor);

            registryMap.max_load_factor(max_load_factor(maxLoadFactor));
            registryMap.reserve(reserve_count(reserveCount));
        });
    }

    static bool cleanup_in_progress() noexcept {
        return cleanUpInProgress.load(std::memory_order_acquire);
    }

    // Attempt to register shared memory; waits for cleanup if needed (bounded)
    static void attempt_register_memory(SharedMemoryPtr sharedMemory) noexcept {
        // Bounded wait for cleanup to finish
        using namespace std::chrono_literals;
        constexpr auto MaxWaitTime = 200ms;

        if (sharedMemory == nullptr)
        {
            //DEBUG_LOG("Attempted to register <NULL> shared memory.");
            return;
        }
        {
            std::unique_lock condLock(mutex);

            // Wait for cleanup to finish if in progress (bounded)
            if (!condVar.wait_for(condLock, MaxWaitTime,
                                  []() noexcept { return !cleanup_in_progress(); }))
            {
                //DEBUG_LOG("Timeout waiting for SharedMemoryRegistry cleanup to finish : " << sharedMemory->name());
                // Timeout - silently fail to register (acceptable during shutdown)
                return;
            }
        }

        // Safe insertion under write-lock
        std::lock_guard writeLock(sharedMutex);

        // Recheck after acquiring registry lock
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
    // Thread-safety and concurrency:
    //  - Sets 'cleanUpInProgress' to prevent new registrations during cleanup.
    //  - Uses a temporary local list to store the registry contents,
    //    so that 'close()' can be called safely without
    //    invalidating iterators or causing race conditions.
    //  - Notifies all threads waiting on registration that cleanup is complete.
    static void cleanup() noexcept {
        // Mark cleanup as in-progress so other threads know not to register new memory
        cleanUpInProgress.store(true, std::memory_order_release);

        OrderedList snapOrderedList;
        {
            std::lock_guard cleanLock(sharedMutex);

            // Move all registered shared memories into local list to allow safe iteration
            // and prevent iterator invalidation if close() triggers un-registration.
            // Full cleanup: take ownership and clear registries
            snapOrderedList = std::move(orderedList);
            registryMap.clear();
        }

        // Safe to iterate and close memory without holding the lock in true insertion order
        for (auto* sharedMemory : snapOrderedList)
            if (sharedMemory != nullptr)
                sharedMemory->close(CloseType::AtExit);

        // Mark cleanup done and notify waiting registrants that cleanup has finished
        cleanUpInProgress.store(false, std::memory_order_release);

        condVar.notify_all();
    }

    static usize size() noexcept {
        std::shared_lock readLock(sharedMutex);

        return registryMap.size();
    }

    static void print() noexcept {
        // Acquire shared lock to safely read the registry without blocking writers
        std::shared_lock readLock(sharedMutex);

        std::cout << "Registered shared memories (insertion order) [" << registryMap.size()
                  << "]:\n";
        usize i = 0;
        for (auto* sharedMemory : orderedList)
            std::cout << "[" << i++ << "] "
                      << (sharedMemory != nullptr ? sharedMemory->name() : "<NULL>") << "\n";
        std::cout << std::endl;
    }

   private:
    SharedMemoryRegistry() noexcept                                       = delete;
    ~SharedMemoryRegistry() noexcept                                      = delete;
    SharedMemoryRegistry(const SharedMemoryRegistry&) noexcept            = delete;
    SharedMemoryRegistry& operator=(const SharedMemoryRegistry&) noexcept = delete;
    SharedMemoryRegistry(SharedMemoryRegistry&&) noexcept                 = delete;
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
        // map <-> list consistency explicit and efficient.
        auto [insertReg, inserted] = registryMap.emplace(sharedMemory, orderedList.end());
        // Already registered -> don't insert
        if (!inserted)
            return false;

        //DEBUG_LOG("Registering shared memory: " << sharedMemory->name());

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

        //DEBUG_LOG("Unregistering shared memory: " << sharedMemory->name());

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
// Utility class that manages automatic cleanup of shared memory resources
// during normal program termination.
//
// Usage:
//   Call SharedMemoryCleanupManager::ensure_initialized() early in main().
//   This initializes the shared memory registry and registers a cleanup
//   handler that is invoked automatically when the program exits normally.
//
// Key Points:
//   - Uses CallOnce to ensure initialization happens only once, even if
//     ensure_initialized() is called multiple times.
//   - Prevents instantiation, copying, and moving; this class only provides
//     static functionality.
//
// Note:
//   - Cleanup via std::atexit() is only guaranteed during normal termination.
//     It will not run after forced termination (SIGKILL), crashes, or abort().
//   - The class is static-only; it cannot be instantiated. (Restriction)
class SharedMemoryCleanupManager final {
   public:
    // Ensure the shared memory registry is initialized
    // and the cleanup callback is registered with std::atexit().
    static void ensure_initialized(usize reserveCount  = 1024,
                                   float maxLoadFactor = 0.75f) noexcept {
        // Only the parameters from the first call are used
        callOnce([reserveCount, maxLoadFactor]() noexcept {
            //DEBUG_LOG("Initializing SharedMemoryCleanupManager.");
            // 1. Initialize registry
            SharedMemoryRegistry::ensure_initialized(reserveCount, maxLoadFactor);
            // 2. Register std::atexit() shutdown cleanup
            std::atexit(SharedMemoryRegistry::cleanup);
        });
    }

   private:
    SharedMemoryCleanupManager() noexcept                                             = delete;
    ~SharedMemoryCleanupManager() noexcept                                            = delete;
    SharedMemoryCleanupManager(const SharedMemoryCleanupManager&) noexcept            = delete;
    SharedMemoryCleanupManager& operator=(const SharedMemoryCleanupManager&) noexcept = delete;
    SharedMemoryCleanupManager(SharedMemoryCleanupManager&&) noexcept                 = delete;
    SharedMemoryCleanupManager& operator=(SharedMemoryCleanupManager&&) noexcept      = delete;

    static inline CallOnce callOnce;
};

// TempRoot
//
// Manages a private temporary directory used by the application for storing
// temporary runtime files.
//
// The directory is created under /tmp using the format:
//     /tmp/DON-[uid]
//
// The directory is created with owner-only permissions (0700). If the directory
// already exists, its ownership and permissions are verified before reuse to
// prevent using an unsafe or unexpected directory.
//
// Usage:
//   const auto& root = TempRoot::temp_root();
//
// Key Points:
//   - Initialized lazily on the first call to temp_root().
//   - Uses a static instance to ensure the same validated temporary root is
//     reused throughout the program lifetime.
//   - Returns std::nullopt if the directory cannot be created or fails the
//     ownership/permission checks.
struct TempRoot final {
   public:
    static const std::optional<TempRoot>& temp_root() noexcept {
        static const auto tempRoot = []() -> std::optional<TempRoot> {
            const uid_t uid = ::getuid();

            std::string tempPath{"/tmp/DON-"};
            tempPath += std::to_string(uid);

            if (mkdir(tempPath.c_str(), 0700) == 0)
                return TempRoot{tempPath};

            if (errno != EEXIST)
                return std::nullopt;

            // Temp root already exists, verify ownership and permissions
            struct stat fileStat{};

            if (lstat(tempPath.c_str(), &fileStat) == 0  //
                && S_ISDIR(fileStat.st_mode)             //
                && fileStat.st_uid == uid                //
                && (fileStat.st_mode & 07777) == 0700)
                return TempRoot{tempPath};

            return std::nullopt;
        }();

        return tempRoot;
    }

    [[nodiscard]] std::string_view path() const noexcept { return path_; }

   private:
    explicit TempRoot(std::string path) noexcept :
        path_(std::move(path)) {}

    // /tmp/DON-[uid], with appropriate permissions
    std::string path_;
};

// Wrapper around ::flock() on a file
struct InitLock final {
   public:
    InitLock() noexcept = default;

    InitLock(const InitLock&) noexcept            = delete;
    InitLock& operator=(const InitLock&) noexcept = delete;

    InitLock(InitLock&&) noexcept            = default;
    InitLock& operator=(InitLock&&) noexcept = default;

    ~InitLock() noexcept { unlock(); }

    static InitLock acquire_lock(std::string_view path) noexcept {
        UniqueFd fd(::open(path.data(), O_CREAT | O_RDWR | O_CLOEXEC, 0666));

        if (!fd.is_valid())
            return {};

        // Blocks here if another process is currently initializing
        while (::flock(fd.get(), LOCK_EX) == -1)
        {
            // Failed to acquire
            if (errno != EINTR)
                return {};
        }

        return InitLock(std::move(fd));
    }

    [[nodiscard]] bool is_valid() const noexcept { return lockFd.is_valid(); }

   private:
    explicit InitLock(UniqueFd fd) noexcept :
        lockFd(std::move(fd)) {}

    void unlock() noexcept {
        if (lockFd.is_valid())
            ::flock(lockFd.get(), LOCK_UN);
    }

    UniqueFd lockFd;
};

[[maybe_unused]] inline void set_cloexec(int fd) noexcept {
    if (is_valid_fd(fd))
        (void) ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
}

template<typename T>
class SharedMemory final: public BaseSharedMemory {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   public:
    explicit SharedMemory(std::string_view shmName, const TempRoot& tempRoot) noexcept :
        BaseSharedMemory(shmName),
        sharedDir(std::string{tempRoot.path()} + "/" + make_sentinel_base(name())),
        initLockPath(sharedDir + "/init_lock"),
        socketPath(sharedDir + "/" + std::to_string(::getpid()) + ".sock"),
        serverThread(std::nullopt) {}

    ~SharedMemory() noexcept override { unregister_close(); }

    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& sharedMemory) noexcept :
        BaseSharedMemory(std::move(sharedMemory)),
        mappedPtr(std::exchange(sharedMemory.mappedPtr, nullptr)),
        dataPtr(std::exchange(sharedMemory.dataPtr, nullptr)),
        sharedDir(std::move(sharedMemory.sharedDir)),
        initLockPath(std::move(sharedMemory.initLockPath)),
        socketPath(std::move(sharedMemory.socketPath)),
        serverThread(std::move(sharedMemory.serverThread)),
        shutdownFd(std::move(sharedMemory.shutdownFd)) {

        SharedMemoryRegistry::unregister_memory(&sharedMemory);
        SharedMemoryRegistry::attempt_register_memory(this);
    }
    SharedMemory& operator=(SharedMemory&& sharedMemory) noexcept {
        if (this == &sharedMemory)
            return *this;

        unregister_close();

        BaseSharedMemory::operator=(std::move(sharedMemory));
        mappedPtr    = std::exchange(sharedMemory.mappedPtr, nullptr);
        dataPtr      = std::exchange(sharedMemory.dataPtr, nullptr);
        sharedDir    = std::move(sharedMemory.sharedDir);
        initLockPath = std::move(sharedMemory.initLockPath);
        socketPath   = std::move(sharedMemory.socketPath);
        serverThread = std::move(sharedMemory.serverThread);
        shutdownFd   = std::move(sharedMemory.shutdownFd);

        SharedMemoryRegistry::unregister_memory(&sharedMemory);
        SharedMemoryRegistry::attempt_register_memory(this);

        return *this;
    }

    [[nodiscard]] bool open(const T& value) noexcept {
        if (socketPath.size() >= sizeof(sockaddr_un::sun_path))
            return false;

        if (::mkdir(sharedDir.c_str(), 0700) != 0 && errno != EEXIST)
            return false;

        {
            auto initLock = InitLock::acquire_lock(initLockPath);
            if (!initLock.is_valid())
                return false;

            // Try to receive the shared memFd
            UniqueFd memFd;
            Strings  peerSockets = get_peer_sockets();
            for (const auto& sockPath : peerSockets)
            {
                memFd = try_receive_memfd(sockPath);
                if (memFd.is_valid())
                    break;
            }

            const bool creator = !memFd.is_valid();  // We must create it

            if (creator)
            {
    #if defined(MFD_CLOEXEC)
                // Failed to get it from a peer (no peers, or only dead peers), so create
                memFd.reset(::memfd_create("replicated_data", MFD_CLOEXEC));
                if (!memFd.is_valid())
                    return false;
    #else
                char tempPath[PATH_MAX];
                std::strncpy(tempPath, "/tmp/DON_replicated_data.XXXXXX", PATH_MAX);

                memFd.reset(::mkstemp(tempPath));
                if (!memFd.is_valid())
                    return false;
                set_cloexec(memFd.get());
                ::unlink(tempPath);
    #endif

                if (::ftruncate(memFd.get(), sizeof(T)) != 0)
                    return false;
            }

            assert(memFd.is_valid());

            // Try to map the memFd
            T* mappedMem = static_cast<T*>(
              ::mmap(NULL, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, memFd.get(), 0));
            if (mappedMem == MAP_FAILED)
                return false;

    #if defined(MADV_HUGEPAGE)
            (void) ::madvise(mappedMem, sizeof(T), MADV_HUGEPAGE);
    #endif

            if (creator)
            {
                // Creator is responsible for initialization
                *mappedMem = value;
            }

            mappedPtr = dataPtr = mappedMem;

            SharedMemoryRegistry::attempt_register_memory(this);  // register for cleanup at exit

            int shutdownPipe[2];
    #if !defined(__APPLE__)
            if (::pipe2(shutdownPipe, O_CLOEXEC) != 0)
                return false;
    #else
            if (::pipe(shutdownPipe) != 0)
                return false;
            set_cloexec(shutdownPipe[0]);
            set_cloexec(shutdownPipe[1]);
    #endif
            UniqueFd shutdownReceiver(shutdownPipe[0]);
            shutdownFd = UniqueFd{shutdownPipe[1]};

            auto serverFd = create_unix_socket();
            if (!serverFd.is_valid())
                return false;

            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

            ::unlink(socketPath.c_str());
            if (::bind(serverFd.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))
                  == -1
                || ::listen(serverFd.get(), 5) == -1)
                return false;

            // Don't release the init lock until we've actually made a socket that other DONs can use
            serverThread = make_server_thread(std::move(memFd), std::move(shutdownReceiver),
                                              std::move(serverFd));
        }

        return true;
    }

    void close(CloseType closeType) noexcept override {
        switch (closeType)
        {
        case CloseType::AtExit :
            // Don't unmap on exit as this may cause currently searching threads to segfault.
            // Also, don't join() the server thread on exit.
            if (serverThread)
            {
                serverThread->detach();
                serverThread = std::nullopt;
            }
            break;
        case CloseType::Normal :
        default :
            unmap_region();
            break;
        }

        reset();
    }

    // Unregister SharedMemory object and release resources
    void unregister_close() noexcept {
        // 1. Unregister from registry
        SharedMemoryRegistry::unregister_memory(this);

        // 2. Close and release
        close(CloseType::Normal);
    }

    bool is_mapped() const noexcept { return mappedPtr != nullptr; }

    bool is_serving() const noexcept { return bool(serverThread); }

    const T& get() const noexcept {
        assert(dataPtr != nullptr);

        return *dataPtr;
    }

   private:
    void reset() noexcept {
        if (!socketPath.empty())
        {
            ::unlink(socketPath.c_str());
        }

        shutdownFd.reset();

        if (serverThread && serverThread->joinable())
        {
            serverThread->join();
            serverThread = std::nullopt;
        }

        mappedPtr = nullptr;
        dataPtr   = nullptr;
    }

    void unmap_region() noexcept {
        if (mappedPtr != nullptr)
        {
            ::munmap(mappedPtr, sizeof(T));
            mappedPtr = nullptr;
            dataPtr   = nullptr;
        }
    }

    // Discover all peers in the shared dir
    Strings get_peer_sockets() noexcept {
        Strings peerSockets;

        DIR* ptrDir = ::opendir(sharedDir.c_str());
        if (ptrDir != nullptr)
        {
            struct dirent* ptrDirEntry;
            while ((ptrDirEntry = ::readdir(ptrDir)) != nullptr)
            {
                std::string dName{ptrDirEntry->d_name};
                if (dName.size() >= 5 && dName.compare(dName.size() - 5, 5, ".sock") == 0)
                    peerSockets.push_back(sharedDir + "/" + dName);
            }
            ::closedir(ptrDir);
        }
        return peerSockets;
    }

    static std::string make_sentinel_base(std::string_view name) noexcept {
        char buf[32];
        // Using std::to_string here causes non-deterministic PGO builds.
        // snprintf, being part of libc, is insensitive to the formatted values.
        std::snprintf(buf, sizeof(buf), "donshm_%016" PRIu64, hash_string(name));
        return buf;
    }

    static UniqueFd create_unix_socket() noexcept {
        int domain = AF_UNIX;
        int type   = SOCK_STREAM;
    #if defined(SOCK_CLOEXEC)
        type |= SOCK_CLOEXEC;
    #endif
        int protocol = 0;

        UniqueFd fd(::socket(domain, type, protocol));

    #if !defined(SOCK_CLOEXEC)
        set_cloexec(fd.get());
    #endif

        return fd;
    }

    UniqueFd try_receive_memfd(const std::string& sockPath) noexcept {
        auto peerFd = create_unix_socket();
        if (!peerFd.is_valid())
            return {};

        // 1-second timeout for connect and receive
        struct timeval tv{1, 0};
        ::setsockopt(peerFd.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        ::setsockopt(peerFd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);

        // Connect to peer socket and request access to the memFd
        int ret;
        do
            ret = ::connect(peerFd.get(), reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        while (ret < 0 && errno == EINTR);

        if (ret == 0)
        {
            msghdr msg{};

            char         buf[1];
            struct iovec iov[1];
            iov[0].iov_base = buf;
            iov[0].iov_len  = 1;
            msg.msg_iov     = iov;
            msg.msg_iovlen  = 1;

            union {
                char           buf[CMSG_SPACE(sizeof(int))];
                struct cmsghdr align;
            } controlMsg = {};

            msg.msg_control    = controlMsg.buf;
            msg.msg_controllen = sizeof(controlMsg.buf);

            int flags =
    #if defined(MSG_CMSG_CLOEXEC)
              MSG_CMSG_CLOEXEC
    #else
              0
    #endif
              ;

            ssize_t bytesRecv;

            do
                bytesRecv = ::recvmsg(peerFd.get(), &msg, flags);
            while (bytesRecv < 0 && errno == EINTR);

            if (bytesRecv > 0)
            {
                cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
                // Receive rights to the memFd from the peer; see make_server_thread
                if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
                {
                    int receivedFd;
                    std::memcpy(&receivedFd, CMSG_DATA(cmsg), sizeof(receivedFd));
    #if !defined(MSG_CMSG_CLOEXEC)
                    set_cloexec(receivedFd);
    #endif
                    return UniqueFd{receivedFd};
                }
            }
        }
        else if (errno == ECONNREFUSED || errno == ENOENT)
        {
            // Failed to connect, clean up dead peer
            ::unlink(sockPath.c_str());
        }

        return {};
    }

    // Server thread:
    //  - Forwards the file descriptor fd
    //  - Exits when shutdownReceiver is hung up on
    //  - Listens on serverFd
    static std::thread
    make_server_thread(UniqueFd fd, UniqueFd shutdownReceiver, UniqueFd serverFd) noexcept {
        enum FD : u8 {
            FD_SERVER,
            FD_SHUTDOWN,
        };

        constexpr usize FD_NB = 2;

        union ControlMsg {
            char           buf[CMSG_SPACE(sizeof(int))];
            struct cmsghdr align;
        };

        return std::thread([fd               = std::move(fd),                //
                            shutdownReceiver = std::move(shutdownReceiver),  //
                            serverFd         = std::move(serverFd)]() noexcept {
            struct pollfd fds[FD_NB];
            fds[FD_SERVER].fd     = serverFd.get();
            fds[FD_SERVER].events = POLLIN;

            fds[FD_SHUTDOWN].fd     = shutdownReceiver.get();
            fds[FD_SHUTDOWN].events = POLLIN;

            while (true)
            {
                int ret = ::poll(fds, FD_NB, -1);
                if (ret < 0)
                {
                    if (errno == EINTR)
                        continue;

                    break;
                }

                // Shutdown requested by main thread
                if (fds[FD_SHUTDOWN].revents != 0)
                    break;

                if ((fds[FD_SERVER].revents & POLLIN) != 0)
                {
                        // Another DON wants access
    #if !defined(__APPLE__)
                    UniqueFd clientFd(::accept4(serverFd.get(), nullptr, nullptr, SOCK_CLOEXEC));
    #else
                    UniqueFd clientFd(::accept(serverFd.get(), nullptr, nullptr));
                    set_cloexec(clientFd.get());
    #endif
                    if (!clientFd.is_valid())
                        continue;  // including EINTR

                    msghdr msg{};
                    char   buf[1] = {};
                    iovec  iov[1];
                    iov[0].iov_base = buf;
                    iov[0].iov_len  = 1;
                    msg.msg_iov     = iov;
                    msg.msg_iovlen  = 1;

                    ControlMsg controlMsg{};

                    msg.msg_control    = controlMsg.buf;
                    msg.msg_controllen = sizeof(controlMsg.buf);

                    // Send over rights to the memFd (SCM_RIGHTS). The fd may be given a different number, but
                    // will refer to the same underlying file. Once it's mmapped then it will share physical memory
                    // between the processes.
                    // See https://man7.org/linux/man-pages/man7/unix.7.html for more information on SCM_RIGHTS
                    int             rawFd = fd.get();
                    struct cmsghdr* cmsg  = CMSG_FIRSTHDR(&msg);
                    cmsg->cmsg_level      = SOL_SOCKET;
                    cmsg->cmsg_type       = SCM_RIGHTS;
                    cmsg->cmsg_len        = CMSG_LEN(sizeof(rawFd));
                    std::memcpy(CMSG_DATA(cmsg), &rawFd, sizeof(rawFd));

    #if defined(SO_NOSIGPIPE)
                    int yes = 1;
                    ::setsockopt(clientFd.get(), SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    #endif
                    int flags =
    #if defined(MSG_NOSIGNAL)
                      MSG_NOSIGNAL
    #else
                      0
    #endif
                      ;

                    while (::sendmsg(clientFd.get(), &msg, flags) < 0 && errno == EINTR)
                    {}
                }
            }
        });
    }

    void* mappedPtr = nullptr;
    T*    dataPtr   = nullptr;

    // DONs will put their .sock files in this folder, and each folder is associated with a single underlying
    // shared memFd. Therefore in a NUMA setting, we may have multiple such folders
    std::string sharedDir;

    // Threads need to successfully and exclusively lock this file to initialize the memFd. If another process has
    // a lock on it, then we wait for it to finish initializing (or die) and release the lock
    std::string initLockPath;

    // serve requests for the shared segment on this .sock
    std::string                socketPath;
    std::optional<std::thread> serverThread;
    UniqueFd                   shutdownFd;  // close to signal server thread shutdown
};

template<typename T>
[[nodiscard]] std::optional<SharedMemory<T>> create_shared_memory(std::string_view name,
                                                                  const T&         value) noexcept {
    SharedMemoryCleanupManager::ensure_initialized();

    auto tempRoot = TempRoot::temp_root();
    if (!tempRoot.has_value())
        return std::nullopt;
    SharedMemory<T> shm(name, *tempRoot);
    if (shm.open(value))
        return shm;
    return std::nullopt;
}

template<typename T>
class BackendSharedMemory final {
   public:
    BackendSharedMemory() noexcept = default;

    BackendSharedMemory(std::string_view shmName, const T& value) noexcept :
        shm(create_shared_memory<T>(shmName, value)) {}

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept            = default;
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept = default;

    bool is_valid() const noexcept { return shm && shm->is_mapped() && shm->is_serving(); }

    void* get() const noexcept {
        return is_valid() ? reinterpret_cast<void*>(const_cast<T*>(&shm->get())) : nullptr;
    }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return is_valid() ? SharedMemoryAllocationStatus::SharedMemory
                          : SharedMemoryAllocationStatus::NoAllocation;
    }

    std::string_view get_error_message() const noexcept {
        if (!shm)
            return "Shared memory not initialized.";
        if (!shm->is_mapped())
            return "Shared memory is not mapped.";
        if (!shm->is_serving())
            return "Shared memory is not serving to other processes.";
        return {};
    }

   private:
    std::optional<SharedMemory<T>> shm;
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
    SystemWideSharedMemory(const T& value, u64 discriminator = 0) noexcept {

        std::string shmName{"DON_"};

        // Create a unique name based on the value, executable path, and discriminator
        // 3 hex digits per 64-bit part + 2 dollar signs + null terminator
        constexpr usize BufferSize = 3 * HEX64_SIZE + 2 + 1;
        // Build the three-part hex identifier safely into a temporary buffer
        Array<char, BufferSize> buffer{};

        u64 valueHash      = std::hash<T>{}(value);
        u64 executableHash = hash_string(executable_path());

        // snprintf returns the number of chars that would have been written (excluding NUL)
        int writtenSize = std::snprintf(buffer.data(), buffer.size(),
                                        "%016" PRIX64 "$"  //
                                        "%016" PRIX64 "$"  //
                                        "%016" PRIX64,     //
                                        valueHash, executableHash, discriminator);

        std::string hashName;

        if (writtenSize > 0)
        {
            // Ensure size is within bounds
            // If snprintf truncated, use up to (buf.size() - 1) characters
            usize copySize = std::min<usize>(writtenSize, buffer.size() - 1);
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
            hashName.assign(oss.str());
        }

        shmName.append(hashName);

        // Since std::string::size() does not include '\0', allow at most (MAX - 1) characters,
        // to guarantee space for the terminator ('\0') in fixed-size buffers.
        // Truncate the name if necessary so that it fits within limits including the null terminator
        if (shmName.size() > SHM_NAME_MAX)
            shmName.resize(SHM_NAME_MAX);

        BackendSharedMemory<T> tempBackendShm(shmName, value);

        if (tempBackendShm.is_valid())
            backendShm = std::move(tempBackendShm);
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
