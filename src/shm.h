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
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__ANDROID__)
#elif defined(_WIN32)
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
    #include <atomic>
    #include <cassert>
    #include <cerrno>
    #include <chrono>
    #include <cstdio>
    #include <cstdlib>
    #include <dirent.h>
    #include <fcntl.h>
    #include <inttypes.h>
    #include <mutex>
    #include <pthread.h>
    #include <semaphore.h>
    #include <signal.h>
    #include <sys/file.h>
    #include <sys/mman.h>  // mmap, munmap, MAP_*, PROT_*
    #include <sys/stat.h>
    #include <thread>
    #include <unistd.h>
    #include <unordered_map>
    #include <vector>

    #if defined(__APPLE__)
        #include <mach-o/dyld.h>
        #include <sys/syslimits.h>
    #elif defined(__sun)  // Solaris
        #include <cstdlib>
        #include <libgen.h>
    #elif defined(__FreeBSD__)
        #include <sys/sysctl.h>
        #include <sys/types.h>
    #elif defined(__NetBSD__) || defined(__DragonFly__)
    #elif defined(__linux__)
    #endif

    #define SHM_NAME_MAX_SIZE NAME_MAX
#endif

#include "memory.h"
#include "misc.h"

namespace DON {

// argv[0] CANNOT be used because need to identify the executable.
// argv[0] contains the command used to invoke it, which does not involve the full path.
// Just using a path is not fully resilient either, as the executable could
// have changed if it wasn't locked by the OS.
// If the path is longer than 4095 bytes the hash will be computed from an unspecified
// amount of bytes of the path; in particular it can a hash of an empty string.

enum class SharedMemoryAllocationStatus {
    NoAllocation,
    LocalMemory,
    SharedMemory
};

inline std::string to_string(SharedMemoryAllocationStatus status) noexcept {
    switch (status)
    {
    case SharedMemoryAllocationStatus::NoAllocation :
        return "No allocation";
    case SharedMemoryAllocationStatus::LocalMemory :
        return "Local memory";
    case SharedMemoryAllocationStatus::SharedMemory :
        return "Shared memory";
    default :
        return "Unknown status";
    }
}

inline std::string executable_path() noexcept {
    StdArray<char, 4096> executablePath;
    executablePath.fill('\0');

    std::size_t executableSize = 0;

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

#if defined(__ANDROID__)

// For systems that don't have shared memory, or support is troublesome.
// The way fallback is done is that need a dummy backend.
template<typename T>
class BackendSharedMemory final {
   public:
    BackendSharedMemory() = default;

    BackendSharedMemory([[maybe_unused]] std::string_view shmName,
                        [[maybe_unused]] const T&         value) noexcept {}

    bool is_valid() const noexcept { return false; }

    void* get() const noexcept { return nullptr; }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return SharedMemoryAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const noexcept {
        return "Dummy Shared Memory Backend";
    }
};

#elif defined(_WIN32)

// Get the error message string, if any
inline std::string error_to_string(DWORD errorId) noexcept {
    if (errorId == 0)
        return {};

    LPSTR buffer = nullptr;
    // Ask Win32 to give us the string version of that message ID.
    // The parameters pass in, tell Win32 to create the buffer that holds the message
    // (because don't yet know how long the message string will be).
    std::size_t len = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, errorId, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buffer),  // must pass pointer to buffer pointer
      0, NULL);

    // Copy the error message into a std::string
    std::string message(buffer, len);
    // Trim trailing CR/LF that many system messages include
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
        message.pop_back();
    // Free the Win32's string's buffer
    LocalFree(buffer);

    return message;
}

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
        name.insert(0, "Local\\");

        initialize(value);
    }

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept :
        name(std::move(backendShm.name)),
        hMapFile(backendShm.hMapFile),
        hMapFileGuard(hMapFile),
        mappedPtr(backendShm.mappedPtr),
        mappedGuard(mappedPtr),
        status(backendShm.status),
        lastErrorStr(std::move(backendShm.lastErrorStr)) {

        backendShm.hMapFile  = INVALID_HANDLE;
        backendShm.mappedPtr = INVALID_MMAP_PTR;
        backendShm.status    = Status::NotInitialized;
    }
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept {
        if (this == &backendShm)
            return *this;

        cleanup();

        name         = std::move(backendShm.name);
        hMapFile     = backendShm.hMapFile;
        mappedPtr    = backendShm.mappedPtr;
        status       = backendShm.status;
        lastErrorStr = std::move(backendShm.lastErrorStr);

        backendShm.hMapFile  = INVALID_HANDLE;
        backendShm.mappedPtr = INVALID_MMAP_PTR;
        backendShm.status    = Status::NotInitialized;

        return *this;
    }

    ~BackendSharedMemory() noexcept { cleanup(); }

    bool is_valid() const noexcept { return status == Status::Success; }

    void* get() const noexcept { return is_valid() ? mappedPtr : INVALID_MMAP_PTR; }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return status == Status::Success ? SharedMemoryAllocationStatus::SharedMemory
                                         : SharedMemoryAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const noexcept {
        switch (status)
        {
        case Status::Success :
            return std::nullopt;
        case Status::NotInitialized :
            return "Not initialized";
        case Status::FileMapping :
            return "Failed to create file mapping: " + lastErrorStr;
        case Status::MapView :
            return "Failed to map view: " + lastErrorStr;
        case Status::MutexCreate :
            return "Failed to create mutex: " + lastErrorStr;
        case Status::MutexWait :
            return "Failed to wait on mutex: " + lastErrorStr;
        case Status::MutexRelease :
            return "Failed to release mutex: " + lastErrorStr;
        case Status::LargePageAllocation :
            return "Failed to allocate large page memory";
        default :;
        }
        return "Unknown error";
    }

   private:
    void initialize(const T& value) noexcept {
        constexpr std::size_t TotalSize = sizeof(T) + sizeof(IS_INITIALIZED);

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

              return CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
                                       PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,  //
                                       hiTotalSize, loTotalSize, name.c_str());
          },
          []() { return INVALID_HANDLE; });

        // Fallback to normal allocation if no large page available
        if (hMapFile == INVALID_HANDLE)
            hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,  //
                                         0, TotalSize, name.c_str());

        if (hMapFile == INVALID_HANDLE)
        {
            status       = Status::FileMapping;
            lastErrorStr = error_to_string(GetLastError());

            return;
        }

        mappedPtr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, TotalSize);

        if (mappedPtr == INVALID_MMAP_PTR)
        {
            status       = Status::MapView;
            lastErrorStr = error_to_string(GetLastError());

            partial_cleanup();
            return;
        }

        // Use named mutex to ensure only one initializer
        std::string mutexName{name};
        mutexName += "$mutex";

        HANDLE hMutex = CreateMutex(nullptr, FALSE, mutexName.c_str());

        HandleGuard hMutexGuard{hMutex};

        if (hMutex == nullptr)
        {
            status       = Status::MutexCreate;
            lastErrorStr = error_to_string(GetLastError());

            partial_cleanup();
            return;
        }

        if (WaitForSingleObject(hMutex, INFINITE) != WAIT_OBJECT_0)
        {
            status       = Status::MutexWait;
            lastErrorStr = error_to_string(GetLastError());

            partial_cleanup();
            return;
        }

        // Crucially, place the object first to ensure alignment
        volatile DWORD* isInitialized =
          std::launder(reinterpret_cast<DWORD*>(reinterpret_cast<char*>(mappedPtr) + sizeof(T)));

        T* object = std::launder(reinterpret_cast<T*>(mappedPtr));

        if (*isInitialized != IS_INITIALIZED)
        {
            // First time initialization, message for debug purposes
            new (object) T{value};

            *isInitialized = IS_INITIALIZED;
        }

        if (!ReleaseMutex(hMutex))
        {
            status       = Status::MutexRelease;
            lastErrorStr = error_to_string(GetLastError());

            partial_cleanup();
            return;
        }

        status = Status::Success;
    }

    void partial_cleanup() noexcept {

        mappedGuard.close();

        hMapFileGuard.close();
    }

    void cleanup() noexcept { partial_cleanup(); }

    static constexpr DWORD IS_INITIALIZED = 1;

    std::string name;
    HANDLE      hMapFile = INVALID_HANDLE;
    HandleGuard hMapFileGuard{hMapFile};
    void*       mappedPtr = INVALID_MMAP_PTR;
    MMapGuard   mappedGuard{mappedPtr};
    Status      status = Status::NotInitialized;
    std::string lastErrorStr;
};

#else

class BaseSharedMemory {
   public:
    virtual ~BaseSharedMemory() noexcept = default;

    virtual void close(bool skipUnmapRegion = false) noexcept = 0;
};

// SharedMemoryRegistry
//
// A thread-safe global registry for managing shared memory objects (BaseSharedMemory).
// This class allows registering and unregistering shared memory instances and provides
// a centralized cleanup mechanism to safely close all registered memory.
//
// Key Features:
//  - Thread-safe: all operations are protected by a mutex.
//  - Automatic cleanup: 'clean()' closes all registered objects safely, even if unregistering occurs during cleanup.
//  - Lightweight: stores only pointers, avoids ownership management; actual memory management is handled by BaseSharedMemory.
//  - Implementation: classic vector + index map (swap-and-pop) pattern.
//
// Usage:
//  - Call 'register_memory()' when a new shared memory object is created.
//  - Call 'unregister_memory()' when the object is no longer needed.
//  - Call 'clean()' to close and clean up all registered objects, optionally skipping actual memory unmapping.
//
// Note:
//  - The class is static-only; it cannot be instantiated. (Restriction)
class SharedMemoryRegistry final {
   private:
    enum class Result : std::uint8_t {
        Success,
        AlreadyRegistered,
        CleanupInProgress
    };

   public:
    static void ensure_initialized() noexcept {
        callOnce([]() noexcept {
            orderedSharedMemories.reserve(ReserveCount);
            sharedMemoryIndices.max_load_factor(LoadFactor);
            std::size_t bucketCount = std::size_t(ReserveCount / LoadFactor) + 1;
            sharedMemoryIndices.rehash(bucketCount);
        });
    }

    static std::size_t size() noexcept {
        std::scoped_lock lock(mutex);

        return orderedSharedMemories.size();
    }

    static bool cleanup_in_progress() noexcept {
        return cleanUpInProgress.load(std::memory_order_acquire);
    }

    // Try to register, retry only if cleanup is in progress
    static void attempt_register_memory(BaseSharedMemory* sharedMemory) noexcept {
        constexpr std::size_t MaxAttempt   = 10;
        constexpr auto        AttemptDelay = std::chrono::microseconds(50);

        for (std::size_t attempt = 0;; ++attempt)
        {
            auto result = register_memory(sharedMemory);

            if (result == Result::Success)
                return;

            if (result == Result::AlreadyRegistered)
            {
                assert(false && "SharedMemory double registration");
                return;
            }

            if (attempt >= MaxAttempt)
                break;

            // Cleanup in progress, wait a bit
            auto attemptDelay = AttemptDelay * (1U << attempt);
            std::this_thread::yield();
            std::this_thread::sleep_for(attemptDelay);
        }

        // Max attempts reached: fail silently to register (acceptable during shutdown)
    }
    // Register a shared memory object in the global registry.
    // Thread-safe: locks the registry while inserting.
    static Result register_memory(BaseSharedMemory* sharedMemory) noexcept {
        // Lazy initialization
        if (!callOnce.initialized())
            ensure_initialized();

        std::scoped_lock lock(mutex);

        // Don't register during cleanup
        if (cleanup_in_progress())
            return Result::CleanupInProgress;

        return insert_nolock(sharedMemory);
    }
    // Unregister a shared memory object from the global registry.
    // Thread-safe: locks the registry while erasing.
    static bool unregister_memory(BaseSharedMemory* sharedMemory) noexcept {
        std::scoped_lock lock(mutex);

        return erase_nolock(sharedMemory);
    }
    // Close and clean all registered shared memory objects.
    // If skipUnmapRegion is true, the actual memory unmapping can be skipped.
    // Thread-safe: swaps the registry into a local set to avoid iterator invalidation
    // if any close() call triggers unregister_memory().
    static void clean(bool skipUnmapRegion = false) noexcept {
        std::vector<BaseSharedMemory*> copiedOrderedSharedMemories;

        {
            std::scoped_lock lock(mutex);

            // Mark cleanup as in-progress so other threads know not to register new memory
            cleanUpInProgress.store(true, std::memory_order_release);

            // Efficiently transfer all registered shared memories to a local vector
            // Use move to avoid copying large vector contents.
            // This allows us to safely iterate and close memories outside the lock
            // without invalidating iterators if close() calls unregister_memory().
            copiedOrderedSharedMemories.reserve(orderedSharedMemories.size());
            copiedOrderedSharedMemories = std::move(orderedSharedMemories);
            // Clear the lookup map now that all memories are removed from the main registry
            sharedMemoryIndices.clear();
        }

        // Will reset flag on exit
        FlagGuard cleanUpInProgressGuard{cleanUpInProgress};

        // Safe to iterate and close memory without holding the lock
        for (BaseSharedMemory* sharedMemory : copiedOrderedSharedMemories)
            sharedMemory->close(skipUnmapRegion);
    }

   private:
    SharedMemoryRegistry() noexcept                                       = delete;
    ~SharedMemoryRegistry() noexcept                                      = delete;
    SharedMemoryRegistry(const SharedMemoryRegistry&) noexcept            = delete;
    SharedMemoryRegistry(SharedMemoryRegistry&&) noexcept                 = delete;
    SharedMemoryRegistry& operator=(const SharedMemoryRegistry&) noexcept = delete;
    SharedMemoryRegistry& operator=(SharedMemoryRegistry&&) noexcept      = delete;

    static Result insert_nolock(BaseSharedMemory* sharedMemory) noexcept {
        // Only insert if not already present
        if (sharedMemoryIndices.find(sharedMemory) != sharedMemoryIndices.end())
            return Result::AlreadyRegistered;

        std::size_t newIndex = orderedSharedMemories.size();
        orderedSharedMemories.push_back(sharedMemory);
        sharedMemoryIndices[sharedMemory] = newIndex;

        return Result::Success;
    }
    static bool erase_nolock(BaseSharedMemory* sharedMemory) noexcept {
        // Only erase if already present
        auto itr = sharedMemoryIndices.find(sharedMemory);

        if (itr == sharedMemoryIndices.end())
            return false;

        std::size_t victimIndex = itr->second;

        assert(!orderedSharedMemories.empty());
        assert(victimIndex < orderedSharedMemories.size());

        // Perform the swap-and-pop operation
        // Swap the last element into the removed spot to avoid shifting all elements
        BaseSharedMemory* lastSharedMemory = orderedSharedMemories.back();
        if (victimIndex != orderedSharedMemories.size() - 1)
        {
            orderedSharedMemories[victimIndex]    = lastSharedMemory;
            sharedMemoryIndices[lastSharedMemory] = victimIndex;
        }

        orderedSharedMemories.pop_back();
        sharedMemoryIndices.erase(itr);

        return true;
    }

    static constexpr std::size_t ReserveCount = 1024;
    static constexpr float       LoadFactor   = 0.75f;

    static inline CallOnce          callOnce;
    static inline std::atomic<bool> cleanUpInProgress{false};
    // Protects access to SharedMemories for thread safety
    static inline std::mutex mutex;
    // Preserves insertion order for SharedMemories iteration
    static inline std::vector<BaseSharedMemory*> orderedSharedMemories;
    // Fast lookup for registered SharedMemories index
    static inline std::unordered_map<BaseSharedMemory*, std::size_t> sharedMemoryIndices;
};

// SharedMemoryCleanupManager
//
// A utility class that ensures **automatic cleanup of shared memory** when the program exits
// or when certain signals (termination, fatal errors) are received.
//
// Usage:
//   Call SharedMemoryCleanupManager::ensure_initialized() early in main()
//   to register cleanup hooks and signal handlers.
//   This guarantees that SharedMemoryRegistry::clean() will be
//   invoked automatically on program exit or abnormal termination.
//
// Key Points:
//   - Uses std::call_once to register hooks only once, even if called multiple times.
//   - Registers both atexit handler (normal program termination) and POSIX signal handlers.
//   - Signal handler performs minimal, safe cleanup and then re-raises the signal with default behavior.
//   - Prevents instantiation and copying (all constructors/destructor deleted).
//
// Note:
//  - The class is static-only; it cannot be instantiated. (Restriction)
class SharedMemoryCleanupManager final {
   public:
    // Ensures signal handlers and atexit cleanup are registered only once
    static void ensure_initialized() noexcept {
        callOnce([]() noexcept {
            // 1. Create async-signal-safe pipe
            int pipeFds[2];
    #if defined(__linux__)
            // Linux: use pipe2 (atomic)
            if (pipe2(pipeFds, O_CLOEXEC | O_NONBLOCK) != 0)
    #else
            // macOS/BSD: use pipe + fcntl
            if (pipe(pipeFds) != 0)
    #endif
            {
                std::cerr << "Failed to create signal pipe: " << std::strerror(errno) << std::endl;

                close_signal_pipe();
                return;
            }
    #if !defined(__linux__)
            // Set flags manually (portable alternative to pipe2)
            if (fcntl(pipeFds[0], F_SETFD, FD_CLOEXEC) == -1     //
                || fcntl(pipeFds[1], F_SETFD, FD_CLOEXEC) == -1  //
                || fcntl(pipeFds[0], F_SETFL, O_NONBLOCK) == -1  //
                || fcntl(pipeFds[1], F_SETFL, O_NONBLOCK) == -1)
            {
                std::cerr << "Failed to set pipe flags: " << std::strerror(errno) << std::endl;

                close_signal_pipe();
                return;
            }
    #endif
            // Store signal pipe fds atomically
            signalPipeFds[0].store(pipeFds[0], std::memory_order_relaxed);
            signalPipeFds[1].store(pipeFds[1], std::memory_order_relaxed);

            if (!valid_signal_pipe())
            {
                std::cerr << "Pipe creation failed, aborting monitor thread." << std::endl;
                return;  // Skip starting the thread
            }
            // 2. Start monitor thread SECOND
            start_monitor_thread();
            // 3. Register signal handlers (now pipe and thread are ready)
            register_signal_handlers();
            // 4. Initialize registry (might trigger signals, but now pipe, thread, handlers all ready)
            SharedMemoryRegistry::ensure_initialized();
            // 5. Register std::atexit() shutdown cleanup
            std::atexit(cleanup_on_exit);
        });
    }

   private:
    // Register all signals with the deferred handler
    static void register_signal_handlers() noexcept {
        sigset_t sigSet;

        sigemptyset(&sigSet);

        for (int signal : SIGNALS)
            sigaddset(&sigSet, signal);

        // Block all signals handlers about to register
        if (pthread_sigmask(SIG_BLOCK, &sigSet, nullptr) != 0)
            std::cerr << "Failed to block signals." << std::endl;

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
                std::cerr << "Failed to register handler for signal " << signal << ": "
                          << std::strerror(errno) << std::endl;
        }

        // Unblock all signals handlers are registered
        if (pthread_sigmask(SIG_UNBLOCK, &sigSet, nullptr) != 0)
            std::cerr << "Failed to unblock signals." << std::endl;
    }

    // Signal handler: deferred handling
    // NOTE: If multiple signals arrive rapidly, all are preserved in pendingSignals.
    static void signal_handler(int signal) noexcept {
        // Don't process signals until initialized
        if (!callOnce.initialized())
            return;

        int bitPos = signal_to_bit(signal);
        // Unknown signal
        if (bitPos == INVALID_SIGNAL)
            return;

        // Set the signal bit
        pendingSignals.fetch_or(bit(bitPos), std::memory_order_release);

        // Guard against uninitialized pipe before writing (Additional safety)
        int fd1 = signalPipeFds[1].load(std::memory_order_relaxed);
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
        {
            write_to_stderr("Failed to write to signal pipe\n");
        }
    }

    // Monitor thread: waits for pipe, cleans memory, restores default, re-raises
    static void start_monitor_thread() noexcept {
        monitorThread = std::thread([]() noexcept {
            FlagsGuard pendingSignalsGuard(pendingSignals);

            while (!shuttingDown.load(std::memory_order_acquire))
            {
                // Pipe closed, exit thread
                int fd0 = signalPipeFds[0].load(std::memory_order_relaxed);
                if (fd0 == -1)
                    break;

                char byte;
                // Block wait for notification
                ssize_t n = read(fd0, &byte, 1);
                if (n == -1)
                {
                    if (errno == EAGAIN || errno == EINTR)
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

                // Process all set bits (handle all pending signals)
                for (std::size_t bitPos = 0; bitPos < SIGNALS.size(); ++bitPos)
                {
                    if ((signals & bit(bitPos)) == 0)
                        continue;

                    int signal = SIGNALS[bitPos];

                    if (signal_graceful(signal))
                        // Perform safe partial cleanup (once per batch)
                        SharedMemoryRegistry::clean(true);

                    // Restore default handler
                    struct sigaction sigAction{};

                    sigAction.sa_handler = SIG_DFL;

                    sigemptyset(&sigAction.sa_mask);

                    sigAction.sa_flags = 0;

                    if (sigaction(signal, &sigAction, nullptr) != 0)
                    {
                        std::cerr << "Failed to restore default handler for signal " << signal
                                  << ": " << std::strerror(errno) << std::endl;
                        // Exit with appropriate code
                        _Exit(128 + signal);
                    }

                    // Re-raise the first signal found
                    ::raise(signal);
                    // Fallback: In case ::raise() returns, exit with appropriate code
                    _Exit(128 + signal);
                }
            }
        });

        // Simple and safe: detach the monitor thread.
        // Thread is designed to live for the lifetime of the program.
        // No join is required since it only accesses static/global data.
        //monitorThread.detach();
    }

    // Wake monitor thread
    static void wake_monitor_thread() noexcept {
        int fd1 = signalPipeFds[1].load(std::memory_order_relaxed);
        // Pipe not initialized, skip notification
        if (fd1 == -1)
            return;

        char byte = 0;
        // Best-effort wakeup
        ssize_t r = write(fd1, &byte, 1);
        if (r == -1 && errno != EAGAIN && errno != EINTR)
        {
            write_to_stderr("Failed to wake monitor thread\n");
        }
    }

    static void stop_monitor_thread() noexcept {
        // 1. Signal shutdown
        shuttingDown.store(true, std::memory_order_release);
        // 2. Wake monitor thread
        wake_monitor_thread();
        // 3. Join monitor thread (wait for exit)
        if (monitorThread.joinable())
            monitorThread.join();
    }

    static void cleanup_on_exit() noexcept {
        stop_monitor_thread();
        close_signal_pipe();
        SharedMemoryRegistry::clean();
    }

    static void close_signal_pipe() noexcept {

        // 1. Close pipe safely
        int fd0 = signalPipeFds[0].load(std::memory_order_relaxed);
        int fd1 = signalPipeFds[1].load(std::memory_order_relaxed);
        if (fd0 != -1)
            close(fd0);
        if (fd1 != -1)
            close(fd1);

        // 2. Reset pipe descriptors
        reset_signal_pipe();
    }

    static void reset_signal_pipe() noexcept {
        signalPipeFds[0].store(-1, std::memory_order_relaxed);
        signalPipeFds[1].store(-1, std::memory_order_relaxed);
    }

    static bool valid_signal_pipe() noexcept {
        return signalPipeFds[0].load(std::memory_order_relaxed) != -1
            && signalPipeFds[1].load(std::memory_order_relaxed) != -1;
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
        default :
            return false;
            // clang-format on
        }
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

    // All handled signals, available at compile-time
    static constexpr StdArray<int, 12> SIGNALS{SIGHUP,  SIGINT,  SIGQUIT, SIGILL, SIGABRT, SIGFPE,
                                               SIGSEGV, SIGTERM, SIGBUS,  SIGSYS, SIGXCPU, SIGXFSZ};

    static constexpr int INVALID_SIGNAL = -1;

    static inline CallOnce                      callOnce;
    static inline std::atomic<bool>             shuttingDown{false};
    static inline std::atomic<std::uint64_t>    pendingSignals{0};
    static inline StdArray<std::atomic<int>, 2> signalPipeFds{-1, -1};
    static inline std::thread                   monitorThread;
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
class BackendSharedMemory final: public BaseSharedMemory {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   public:
    explicit BackendSharedMemory(std::string_view shmName, const T& value) noexcept :
        name(shmName),
        mappedSize(mapped_size()),
        sentinelBase(shmName) {
        // POSIX named shared memory names must start with slash ('/')
        name.insert(0, "/");

        SharedMemoryCleanupManager::ensure_initialized();

        open_register(value);
    }

    ~BackendSharedMemory() noexcept override { unregister_close(); }

    BackendSharedMemory(const BackendSharedMemory&)            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept {
        move_with_registry(backendShm);
    }
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept {
        if (this == &backendShm)
            return *this;

        unregister_close();

        move_with_registry(backendShm);

        return *this;
    }

    void open_register(const T& value) noexcept {

        if (SharedMemoryRegistry::cleanup_in_progress())
            return;

        bool staleRetried = false;

        while (true)
        {
            if (is_open())
                break;

            bool newCreated = false;

            fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);

            if (fd <= INVALID_FD)
            {
                fd = shm_open(name.c_str(), O_RDWR, 0666);

                if (fd <= INVALID_FD)
                    break;
            }
            else
            {
                newCreated = true;
            }

            bool lockFile = lock_file(LOCK_EX);

            if (!lockFile)
            {
                cleanup(false, lockFile);

                break;
            }

            bool headerInvalid = false;

            bool success = newCreated  //
                           ? setup_new_region(value)
                           : setup_existing_region(headerInvalid);

            if (!success)
            {
                cleanup(newCreated || headerInvalid, lockFile);

                if (!newCreated && headerInvalid && !staleRetried)
                {
                    staleRetried = true;
                    continue;
                }

                break;
            }

            if (shmHeader == nullptr)
            {
                cleanup(newCreated, lockFile);

                if (!newCreated && !staleRetried)
                {
                    staleRetried = true;
                    continue;
                }

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
                        staleRetried = true;
                        continue;
                    }

                    break;
                }

                if (!sentinel_file_locked_created())
                {
                    shmHeaderGuard.unlock();

                    cleanup(newCreated, lockFile);

                    break;
                }

                increment_ref_count();

            }  // <-- mutex automatically unlocked here safely

            unlock_file();

            // Register this new resource
            SharedMemoryRegistry::attempt_register_memory(this);

            available = true;

            break;
        }
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

    [[nodiscard]] bool is_open() const noexcept {
        return fd >= 0 && mappedPtr != nullptr && dataPtr != nullptr;
    }

    [[nodiscard]] bool is_initialized() const noexcept {
        return shmHeader != nullptr ? shmHeader->is_initialized() : false;
    }

    [[nodiscard]] std::uint32_t ref_count() const noexcept {
        return shmHeader != nullptr ? shmHeader->ref_count() : 0;
    }

    bool is_valid() const noexcept { return available && is_open() && is_initialized(); }

    void* get() const noexcept { return is_valid() ? reinterpret_cast<void*>(dataPtr) : nullptr; }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return is_valid() ? SharedMemoryAllocationStatus::SharedMemory
                          : SharedMemoryAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const noexcept {
        if (!available)
            return "Shared memory not available";

        if (!is_open())
            return "Shared memory is not open";

        if (!is_initialized())
            return "Shared memory not initialized";

        return std::nullopt;
    }

   private:
    static constexpr std::size_t mapped_size() noexcept { return sizeof(T) + sizeof(ShmHeader); }

    static bool is_pid_alive(pid_t pid) noexcept {
        if (pid <= 0)
            return false;

        if (kill(pid, 0) == 0)
            return true;

        return errno == EPERM;
    }

    // Unregister SharedMemory object and release resources
    void unregister_close() noexcept {
        // 1. Unregister from registry
        SharedMemoryRegistry::unregister_memory(this);

        // 2. Close and release
        close();
    }

    // Move the contents of another SharedMemory object into this one, updating the registry accordingly
    void move_with_registry(BackendSharedMemory& sharedMem) noexcept {
        // 1. Unregister source while it's intact
        [[maybe_unused]] bool unregistered = SharedMemoryRegistry::unregister_memory(&sharedMem);
        //assert(unregistered && "SharedMemory not registered");

        // 2. Move members
        name         = std::move(sharedMem.name);
        fd           = sharedMem.fd;
        mappedPtr    = sharedMem.mappedPtr;
        dataPtr      = sharedMem.dataPtr;
        shmHeader    = sharedMem.shmHeader;
        mappedSize   = sharedMem.mappedSize;
        sentinelBase = std::move(sharedMem.sentinelBase);
        sentinelPath = std::move(sharedMem.sentinelPath);

        // 3. Reset source
        sharedMem.reset();

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
        sentinelPath.reserve(DIRECTORY.size() + sentinelBase.size() + 1 + MAX_PID_CHARS);

        sentinelPath = DIRECTORY;
        sentinelPath += sentinelBase;
        sentinelPath += ".";
        sentinelPath += std::to_string(pid);
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
            int tmpFd = ::open(sentinelPath.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);

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

        while (dirent* entry = readdir(dir))
        {
            std::string entryName = entry->d_name;

            // Check if entryName starts with prefix
            if (entryName.size() < prefix.size()
                || entryName.compare(0, prefix.size(), prefix) != 0)
                continue;

            auto pidStr = entryName.substr(prefix.size());

            char* endPtr = nullptr;
            long  pidVal = std::strtol(pidStr.c_str(), &endPtr, 10);

            if (endPtr == nullptr || *endPtr != '\0')
                continue;

            pid_t pid = pid_t(pidVal);

            if (is_pid_alive(pid))
            {
                found = true;

                break;
            }

            std::string stalePath{DIRECTORY};
            stalePath += entryName;

            ::unlink(stalePath.c_str());

            const_cast<BackendSharedMemory*>(this)->decrement_ref_count();
        }

        closedir(dir);

        return found;
    }

    [[nodiscard]] bool setup_new_region(const T& value) noexcept {
        if (ftruncate(fd, off_t(mappedSize)) == -1)
            return false;

        off_t offset = 0;

    #if defined(__APPLE__)
        fstore_t store{};
        store.fst_flags   = F_ALLOCATECONTIG;
        store.fst_posmode = F_PEOFPOSMODE;
        store.fst_offset  = offset;
        store.fst_length  = mappedSize;

        int rc = fcntl(fd, F_PREALLOCATE, &store);

        if (rc == -1)
        {
            store.fst_flags = F_ALLOCATEALL;

            rc = fcntl(fd, F_PREALLOCATE, &store);
        }

        if (rc == -1)
            return false;

        if (ftruncate(fd, off_t(offset + mappedSize)) == -1)
            return false;
    #else
        if (posix_fallocate(fd, offset, mappedSize) != 0)
            return false;
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
            shm_unlink(name.c_str());

        fdGuard.close();

        if (!skipUnmapRegion)
            reset();
    }

    static constexpr std::string_view DIRECTORY{"/dev/shm/"};
    static constexpr std::size_t      MAX_PID_CHARS = 10;

    std::string name;
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

#endif

template<typename T>
struct FallbackBackendSharedMemory final {
   public:
    FallbackBackendSharedMemory() noexcept = default;

    FallbackBackendSharedMemory([[maybe_unused]] const std::string& shmName,
                                const T&                            value) noexcept :
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
        return fallbackObj == nullptr ? SharedMemoryAllocationStatus::NoAllocation
                                      : SharedMemoryAllocationStatus::LocalMemory;
    }

    std::optional<std::string> get_error_message() const noexcept {
        if (fallbackObj == nullptr)
            return "Not initialized";

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

        std::string shmName;

#if defined(__ANDROID__)
        // Do nothing special on Android, just use a fixed name
        ;
#else
        constexpr std::size_t Size = HEX64_SIZE + 1 + HEX64_SIZE + 1 + HEX64_SIZE + 1;

        std::string hashName(Size, '\0');

        std::uint64_t valueHash      = std::hash<T>{}(value);
        std::uint64_t executableHash = hash_string(executable_path());

        int size = std::snprintf(hashName.data(), hashName.size(),
                                 "%016" PRIX64 "$"  // valueHash
                                 "%016" PRIX64 "$"  // executableHash
                                 "%016" PRIX64,     // discriminator
                                 valueHash, executableHash, discriminator);
        // Shrink to actual string length
        if (size >= 0)
        {
            // Ensure size is within bounds
            if (std::size_t(size) >= hashName.size())
                // Truncate to maximum size, leaving space for null terminator
                size = int(hashName.size() - 1);

            // Shrink to actual content
            hashName.resize(size);
        }

        shmName.reserve(256);

        shmName = std::string{"DON_"};
        shmName += hashName;

    #if defined(_WIN32)
        constexpr std::size_t MaxNameSize = 255 - 1;
    #else
        // POSIX APIs expect a fixed-size C string where the maximum length excluding the terminating null character ('\0').
        // Since std::string::size() does not include '\0', allow at most (MAX - 1) characters
        // to guarantee space for the terminator ('\0') in fixed-size buffers.
        constexpr std::size_t MaxNameSize = SHM_NAME_MAX_SIZE > 0 ? SHM_NAME_MAX_SIZE - 1 : 255 - 1;
    #endif

        // Truncate the name if necessary so that it fits within limits including the null terminator
        if (shmName.size() > MaxNameSize)
            shmName.resize(MaxNameSize);
#endif
        BackendSharedMemory<T> tmpBackendShm(shmName, value);

        if (tmpBackendShm.is_valid())
            backendShm = std::move(tmpBackendShm);
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

    std::optional<std::string> get_error_message() const noexcept {
        return std::visit(
          [](const auto& end) -> std::optional<std::string> {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
                  return std::nullopt;
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
