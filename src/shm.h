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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#if defined(__ANDROID__)
    #include <limits.h>
    #define SHM_NAME_MAX_SIZE NAME_MAX
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
    #include <cerrno>
    #include <cstdio>
    #include <cstdlib>
    #include <dirent.h>
    #include <fcntl.h>
    #include <inttypes.h>
    #include <mutex>
    #include <pthread.h>
    #include <semaphore.h>
    #include <signal.h>
    #include <csignal>
    #include <sys/file.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <unordered_set>

    #if defined(__APPLE__)
        #include <mach-o/dyld.h>
        #include <sys/syslimits.h>
        #define SHM_NAME_MAX_SIZE 31
    #elif defined(__linux__) || defined(__NetBSD__) || defined(__DragonFly__)
        #include <limits.h>
        #include <unistd.h>
        #define SHM_NAME_MAX_SIZE NAME_MAX
    #elif defined(__sun)  // Solaris
        #include <stdlib.h>
        #define SHM_NAME_MAX_SIZE 255
    #elif defined(__FreeBSD__)
        #include <sys/sysctl.h>
        #include <sys/types.h>
        #include <unistd.h>
        #define SHM_NAME_MAX_SIZE 255
    #else
        #define SHM_NAME_MAX_SIZE 255
    #endif
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

    executableSize = std::min(std::size_t(size), executablePath.size() - 1);

    executablePath[executableSize] = '\0';
#elif defined(__APPLE__)
    std::uint32_t size = std::uint32_t(executablePath.size());
    if (_NSGetExecutablePath(executablePath.data(), &size) == 0)
    {
        executableSize = std::strlen(executablePath.data());
    }
#elif defined(__linux__)
    ssize_t size = readlink("/proc/self/exe", executablePath.data(), executablePath.size() - 1);
    if (size >= 0)
    {
        executableSize = size;

        executablePath[executableSize] = '\0';
    }
#elif defined(__NetBSD__) || defined(__DragonFly__)
    ssize_t size = readlink("/proc/curproc/exe", executablePath.data(), executablePath.size() - 1);
    if (size >= 0)
    {
        executableSize = size;

        executablePath[executableSize] = '\0';
    }
#elif defined(__sun)  // Solaris
    const char* path = getexecname();
    if (path != nullptr)
    {
        std::strncpy(executablePath.data(), path, executablePath.size() - 1);

        executableSize = std::strlen(executablePath.data());
    }
#elif defined(__FreeBSD__)
    constexpr StdArray<int, 4> MIB{CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};

    std::size_t size = executablePath.size();
    if (sysctl(MIB.data(), MIB.size(), executablePath.data(), &size, nullptr, 0) == 0)
    {
        executableSize = std::min(size, executablePath.size() - 1);

        executablePath[executableSize] = '\0';
    }
#endif

    // In case of any error the path will be empty
    return std::string(executablePath.data(), executableSize);
}

#if defined(__ANDROID__)

// For systems that don't have shared memory, or support is troublesome.
// The way fallback is done is that need a dummy backend.
template<typename T>
class BackendSharedMemory final {
   public:
    BackendSharedMemory() = default;

    BackendSharedMemory([[maybe_unused]] const std::string& shmName,
                        [[maybe_unused]] const T&           value) noexcept {}

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

    // Copy the error message into a std::string.
    std::string message(buffer, len);
    // Trim trailing CR/LF that many system messages include
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
        message.pop_back();
    // Free the Win32's string's buffer.
    LocalFree(buffer);

    return message;
}

// Utilizes shared memory to store the value. It is deduplicated system-wide (for the single user)
template<typename T>
class BackendSharedMemory final {
   public:
    enum class Status {
        Success,
        NotInitialized,
        FileMappingError,
        MapViewError,
        MutexCreateError,
        MutexWaitError,
        MutexReleaseError,
        LargePageAllocationError,
    };

    static constexpr DWORD IS_INITIALIZED = 1;

    BackendSharedMemory() noexcept :
        status(Status::NotInitialized) {};

    BackendSharedMemory(const std::string& shmName, const T& value) noexcept :
        status(Status::NotInitialized) {

        initialize(shmName, value);
    }

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept :
        hMapFile(backendShm.hMapFile),
        mappedPtr(backendShm.mappedPtr),
        status(backendShm.status),
        lastErrorStr(std::move(backendShm.lastErrorStr)) {

        backendShm.mappedPtr = nullptr;
        backendShm.hMapFile  = nullptr;
        backendShm.status    = Status::NotInitialized;
    }
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept {
        if (this == &backendShm)
            return *this;

        cleanup();

        hMapFile     = backendShm.hMapFile;
        mappedPtr    = backendShm.mappedPtr;
        status       = backendShm.status;
        lastErrorStr = std::move(backendShm.lastErrorStr);

        backendShm.hMapFile  = nullptr;
        backendShm.mappedPtr = nullptr;
        backendShm.status    = Status::NotInitialized;

        return *this;
    }

    ~BackendSharedMemory() noexcept { cleanup(); }

    bool is_valid() const noexcept { return status == Status::Success; }

    void* get() const noexcept { return is_valid() ? mappedPtr : nullptr; }

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
        case Status::FileMappingError :
            return "Failed to create file mapping: " + lastErrorStr;
        case Status::MapViewError :
            return "Failed to map view: " + lastErrorStr;
        case Status::MutexCreateError :
            return "Failed to create mutex: " + lastErrorStr;
        case Status::MutexWaitError :
            return "Failed to wait on mutex: " + lastErrorStr;
        case Status::MutexReleaseError :
            return "Failed to release mutex: " + lastErrorStr;
        case Status::LargePageAllocationError :
            return "Failed to allocate large page memory";
        default :
            return "Unknown error";
        }
    }

   private:
    void initialize(const std::string& shmName, const T& value) noexcept {
        std::size_t totalSize = sizeof(T) + sizeof(IS_INITIALIZED);

        // Try allocating with large page first
        hMapFile = try_with_windows_lock_memory_privilege(
          [&](std::size_t LargePageSize) {
              // Round up size to full large page
              std::size_t roundedTotalSize = round_up_to_pow2_multiple(totalSize, LargePageSize);

    #if defined(_WIN64)
              DWORD hiTotalSize = roundedTotalSize >> 32;
              DWORD loTotalSize = roundedTotalSize & 0xFFFFFFFFU;
    #else
              DWORD hiTotalSize = 0;
              DWORD loTotalSize = roundedTotalSize;
    #endif

              return CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
                                       PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,  //
                                       hiTotalSize, loTotalSize, shmName.c_str());
          },
          []() { return (void*) nullptr; });

        // Fallback to normal allocation if no large page available
        if (hMapFile == nullptr)
            hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,  //
                                         PAGE_READWRITE,                 //
                                         0, totalSize, shmName.c_str());

        if (hMapFile == nullptr)
        {
            status       = Status::FileMappingError;
            lastErrorStr = error_to_string(GetLastError());

            return;
        }

        mappedPtr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);

        if (mappedPtr == nullptr)
        {
            status       = Status::MapViewError;
            lastErrorStr = error_to_string(GetLastError());

            partial_cleanup();
            return;
        }

        // Use named mutex to ensure only one initializer
        std::string mutexName = shmName + "$mutex";

        HANDLE hMutex = CreateMutex(nullptr, FALSE, mutexName.c_str());

        if (hMutex == nullptr)
        {
            status       = Status::MutexCreateError;
            lastErrorStr = error_to_string(GetLastError());

            partial_cleanup();
            return;
        }

        if (WaitForSingleObject(hMutex, INFINITE) != WAIT_OBJECT_0)
        {
            status       = Status::MutexWaitError;
            lastErrorStr = error_to_string(GetLastError());

            partial_cleanup();
            CloseHandle(hMutex);
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
            status       = Status::MutexReleaseError;
            lastErrorStr = error_to_string(GetLastError());

            partial_cleanup();
            CloseHandle(hMutex);
            return;
        }

        CloseHandle(hMutex);

        status = Status::Success;
    }

    void partial_cleanup() noexcept {
        if (mappedPtr != nullptr)
        {
            UnmapViewOfFile(mappedPtr);
            mappedPtr = nullptr;
        }
        if (hMapFile != nullptr)
        {
            CloseHandle(hMapFile);
            hMapFile = nullptr;
        }
    }

    void cleanup() noexcept { partial_cleanup(); }

    HANDLE      hMapFile  = nullptr;
    void*       mappedPtr = nullptr;
    Status      status    = Status::NotInitialized;
    std::string lastErrorStr;
};

#else

class BaseSharedMemory {
   public:
    virtual ~BaseSharedMemory() = default;

    virtual void close(bool skipUnmapRegion = false) noexcept = 0;
};

class SharedMemoryRegistry final {
   public:
    static void register_memory(BaseSharedMemory* sharedMemory) {
        std::scoped_lock lock(mutex);

        sharedMemories.insert(sharedMemory);
    }

    static void unregister_memory(BaseSharedMemory* sharedMemory) {
        std::scoped_lock lock(mutex);

        sharedMemories.erase(sharedMemory);
    }

    static void clean(bool skipUnmapRegion = false) noexcept {
        std::scoped_lock lock(mutex);

        for (auto* sharedMemory : sharedMemories)
            sharedMemory->close(skipUnmapRegion);

        sharedMemories.clear();
    }

   private:
    SharedMemoryRegistry() noexcept                                       = delete;
    SharedMemoryRegistry(const SharedMemoryRegistry&) noexcept            = delete;
    SharedMemoryRegistry(SharedMemoryRegistry&&) noexcept                 = delete;
    SharedMemoryRegistry& operator=(const SharedMemoryRegistry&) noexcept = delete;
    SharedMemoryRegistry& operator=(SharedMemoryRegistry&&) noexcept      = delete;

    static inline std::mutex                            mutex;
    static inline std::unordered_set<BaseSharedMemory*> sharedMemories;
};

class CleanupHooks final {
   public:
    static void ensure_registered() noexcept {
        std::call_once(registerOnce, register_signal_handlers);
    }

   private:
    CleanupHooks() noexcept                               = delete;
    CleanupHooks(const CleanupHooks&) noexcept            = delete;
    CleanupHooks(CleanupHooks&&) noexcept                 = delete;
    CleanupHooks& operator=(const CleanupHooks&) noexcept = delete;
    CleanupHooks& operator=(CleanupHooks&&) noexcept      = delete;

    static void signal_handler(int Signal) noexcept {
        // Minimal cleanup; avoid non-signal-safe calls if possible
        // The memory mappings will be released on exit.
        SharedMemoryRegistry::clean(true);

        // Restore default and re-raise
        struct sigaction SigAction{};
        SigAction.sa_handler = SIG_DFL;
        sigemptyset(&SigAction.sa_mask);
        SigAction.sa_flags = SA_RESETHAND | SA_NODEFER;

        sigaction(Signal, &SigAction, nullptr);

        std::raise(Signal);
    }

    static void register_signal_handlers() noexcept {
        std::atexit([]() { SharedMemoryRegistry::clean(); });

        constexpr StdArray<int, 12> Signals{SIGHUP,  SIGINT,  SIGQUIT, SIGILL, SIGABRT, SIGFPE,
                                            SIGSEGV, SIGTERM, SIGBUS,  SIGSYS, SIGXCPU, SIGXFSZ};

        struct sigaction SigAction{};
        SigAction.sa_handler = signal_handler;
        sigemptyset(&SigAction.sa_mask);
        SigAction.sa_flags = 0;

        for (int Signal : Signals)
            sigaction(Signal, &SigAction, nullptr);
    }

    static inline std::once_flag registerOnce;
};

inline int portable_fallocate(int fd, off_t offset, off_t length) noexcept {
    #if defined(__APPLE__)
    fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, offset, length, 0};

    int rc = fcntl(fd, F_PREALLOCATE, &store);
    if (rc == -1)
    {
        store.fst_flags = F_ALLOCATEALL;

        rc = fcntl(fd, F_PREALLOCATE, &store);
    }

    if (rc != -1)
        rc = ftruncate(fd, offset + length);

    return rc;
    #else
    return posix_fallocate(fd, offset, length);
    #endif
}

struct ShmHeader final {
   public:
    [[nodiscard]] bool initialize_mutex() noexcept {
        pthread_mutexattr_t mutexattr;

        if (pthread_mutexattr_init(&mutexattr) != 0)
            return false;

        const auto clean_mutexattr = [&mutexattr] { pthread_mutexattr_destroy(&mutexattr); };

        if (pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED) != 0)
        {
            clean_mutexattr();
            return false;
        }

    #if _POSIX_C_SOURCE >= 200809L
        if (pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST) != 0)
        {
            clean_mutexattr();
            return false;
        }
    #endif

        if (pthread_mutex_init(&mutex, &mutexattr) != 0)
        {
            clean_mutexattr();
            return false;
        }

        clean_mutexattr();
        return true;
    }

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

    void unlock_mutex() noexcept { pthread_mutex_unlock(&mutex); }

    void increment_ref_count() noexcept { refCount.fetch_sub(1, std::memory_order_acq_rel); }

    void decrement_ref_count() noexcept {

        for (auto expected = refCount.load(std::memory_order_relaxed);
             expected != 0
             && !refCount.compare_exchange_weak(expected, expected - 1, std::memory_order_acq_rel,
                                                std::memory_order_relaxed);)
        {}
    }

    static constexpr std::uint32_t MAGIC = 0xAD5F1A12U;

    const std::uint32_t magic = MAGIC;

    pthread_mutex_t mutex;

    alignas(64) std::atomic<bool> initialized{false};
    alignas(64) std::atomic<std::uint32_t> refCount{0};
};

template<typename T>
class SharedMemory final: public BaseSharedMemory {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   public:
    explicit SharedMemory(const std::string& shmName) noexcept :
        name(shmName),
        totalSize(calculate_total_size()) {
        sentinelBase = "don_" + create_hash_string(name);
    }

    ~SharedMemory() noexcept override {
        SharedMemoryRegistry::unregister_memory(this);
        close();
    }

    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& sharedMem) noexcept :
        name(std::move(sharedMem.name)),
        fd(sharedMem.fd),
        mappedPtr(sharedMem.mappedPtr),
        dataPtr(sharedMem.dataPtr),
        shmHeader(sharedMem.shmHeader),
        totalSize(sharedMem.totalSize),
        sentinelBase(std::move(sharedMem.sentinelBase)),
        sentinelPath(std::move(sharedMem.sentinelPath)) {

        SharedMemoryRegistry::unregister_memory(&sharedMem);
        SharedMemoryRegistry::register_memory(this);
        sharedMem.reset();
    }

    SharedMemory& operator=(SharedMemory&& sharedMem) noexcept {
        if (this == &sharedMem)
            return *this;

        SharedMemoryRegistry::unregister_memory(this);
        close();

        name         = std::move(sharedMem.name);
        fd           = sharedMem.fd;
        mappedPtr    = sharedMem.mappedPtr;
        dataPtr      = sharedMem.dataPtr;
        shmHeader    = sharedMem.shmHeader;
        totalSize    = sharedMem.totalSize;
        sentinelBase = std::move(sharedMem.sentinelBase);
        sentinelPath = std::move(sharedMem.sentinelPath);

        SharedMemoryRegistry::unregister_memory(&sharedMem);
        SharedMemoryRegistry::register_memory(this);

        sharedMem.reset();
        return *this;
    }

    [[nodiscard]] bool open(const T& value) noexcept {
        CleanupHooks::ensure_registered();

        bool staleRetried = false;

        while (true)
        {
            if (is_open())
                return false;

            bool newCreated = false;

            fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);

            if (fd < 0)
            {
                fd = shm_open(name.c_str(), O_RDWR, 0666);

                if (fd < 0)
                    return false;
            }
            else
                newCreated = true;

            if (!lock_file(LOCK_EX))
            {
                if (newCreated)
                    shm_unlink(name.c_str());

                ::close(fd);

                reset();

                return false;
            }

            bool headerInvalid = false;

            bool success = newCreated  //
                           ? setup_new_region(value)
                           : setup_existing_region(headerInvalid);

            if (!success)
            {
                unmap_region();

                unlock_file();

                if (newCreated || headerInvalid)
                    shm_unlink(name.c_str());

                ::close(fd);

                reset();

                if (!newCreated && headerInvalid && !staleRetried)
                {
                    staleRetried = true;
                    continue;
                }

                return false;
            }

            if (shmHeader == nullptr || !shmHeader->lock_mutex())
            {
                unmap_region();

                unlock_file();

                if (newCreated)
                    shm_unlink(name.c_str());

                ::close(fd);

                reset();

                if (!newCreated && !staleRetried)
                {
                    staleRetried = true;
                    continue;
                }

                return false;
            }

            if (!create_sentinel_file_locked())
            {
                if (shmHeader != nullptr)
                    shmHeader->unlock_mutex();

                unmap_region();

                unlock_file();

                if (newCreated)
                    shm_unlink(name.c_str());

                ::close(fd);

                reset();

                return false;
            }

            shmHeader->refCount.fetch_add(1, std::memory_order_acq_rel);

            shmHeader->unlock_mutex();

            unlock_file();

            SharedMemoryRegistry::register_memory(this);

            return true;
        }
    }

    void close(bool skipUnmapRegion = false) noexcept override {
        if (fd < 0 && mappedPtr == nullptr)
            return;

        bool removeRegion = false;
        bool fileLocked   = lock_file(LOCK_EX);
        bool mutexLocked  = false;

        if (fileLocked)
            mutexLocked = shmHeader != nullptr && shmHeader->lock_mutex();

        if (mutexLocked)
        {
            increment_ref_count();

            remove_sentinel_file();

            removeRegion = !has_other_live_sentinels_locked();

            if (shmHeader != nullptr)
                shmHeader->unlock_mutex();
        }
        else
        {
            remove_sentinel_file();

            decrement_ref_count();
        }

        if (!skipUnmapRegion)
            unmap_region();

        if (fileLocked)
            unlock_file();

        if (removeRegion)
            shm_unlink(name.c_str());

        if (fd >= 0)
            ::close(fd);

        if (!skipUnmapRegion)
            reset();
    }

    [[nodiscard]] bool is_open() const noexcept {
        return fd >= 0 && mappedPtr != nullptr && dataPtr != nullptr;
    }

    [[nodiscard]] const T& get() const noexcept { return *dataPtr; }

    [[nodiscard]] const T* operator->() const noexcept { return dataPtr; }

    [[nodiscard]] const T& operator*() const noexcept { return *dataPtr; }

    [[nodiscard]] uint32_t ref_count() const noexcept {
        return shmHeader != nullptr ? shmHeader->refCount.load(std::memory_order_acquire) : 0;
    }

    [[nodiscard]] bool is_initialized() const noexcept {
        return shmHeader != nullptr ? shmHeader->initialized.load(std::memory_order_acquire)
                                    : false;
    }

   private:
    static constexpr std::size_t calculate_total_size() noexcept {
        return sizeof(T) + sizeof(ShmHeader);
    }

    static bool is_pid_alive(pid_t pid) noexcept {
        if (pid <= 0)
            return false;

        if (kill(pid, 0) == 0)
            return true;

        return errno == EPERM;
    }

    void reset() noexcept {
        fd        = -1;
        mappedPtr = nullptr;
        dataPtr   = nullptr;
        shmHeader = nullptr;
        sentinelPath.clear();
    }

    void unmap_region() noexcept {
        if (mappedPtr == nullptr)
            return;

        munmap(mappedPtr, totalSize);
        mappedPtr = nullptr;
        dataPtr   = nullptr;
        shmHeader = nullptr;
    }

    [[nodiscard]] bool lock_file(int operation) noexcept {
        if (fd < 0)
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
        if (fd < 0)
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
        sentinelPath.reserve(11 + sentinelBase.size() + 1 + 10);

        sentinelPath += "/dev/shm/";
        sentinelPath += sentinelBase;
        sentinelPath += '.';
        sentinelPath += std::to_string(pid);
    }

    void increment_ref_count() noexcept {
        if (shmHeader != nullptr)
            shmHeader->increment_ref_count();
    }

    void decrement_ref_count() noexcept {
        if (shmHeader != nullptr)
            shmHeader->decrement_ref_count();
    }

    bool create_sentinel_file_locked() noexcept {
        if (shmHeader == nullptr)
            return false;

        pid_t selfPid = getpid();

        set_sentinel_path(selfPid);

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            int fd_ = ::open(sentinelPath.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);

            if (fd_ != -1)
            {
                ::close(fd_);

                return true;
            }

            if (errno == EEXIST)
            {
                ::unlink(sentinelPath.c_str());

                decrement_ref_count();

                continue;
            }

            break;
        }

        sentinelPath.clear();

        return false;
    }

    void remove_sentinel_file() noexcept {
        if (sentinelPath.empty())
            return;

        ::unlink(sentinelPath.c_str());

        sentinelPath.clear();
    }

    bool has_other_live_sentinels_locked() const noexcept {
        DIR* dir = opendir("/dev/shm");

        if (dir == nullptr)
            return false;

        std::string prefix = sentinelBase + ".";

        bool found = false;

        while (dirent* entry = readdir(dir))
        {
            std::string entryName = entry->d_name;

            if (entryName.rfind(prefix, 0) != 0)
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

            std::string stalePath = "/dev/shm/" + entryName;

            ::unlink(stalePath.c_str());

            const_cast<SharedMemory*>(this)->decrement_ref_count();
        }

        closedir(dir);

        return found;
    }

    [[nodiscard]] bool setup_new_region(const T& value) noexcept {
        if (ftruncate(fd, off_t(totalSize)) == -1)
            return false;

        if (portable_fallocate(fd, 0, off_t(totalSize)) != 0)
            return false;

        mappedPtr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

        if (mappedPtr == MAP_FAILED)
        {
            std::cerr << "mmap() failed" << std::endl;

            mappedPtr = nullptr;

            return false;
        }

        dataPtr = static_cast<T*>(mappedPtr);

        shmHeader = reinterpret_cast<ShmHeader*>(static_cast<char*>(mappedPtr) + sizeof(T));

        new (shmHeader) ShmHeader{};

        new (dataPtr) T{value};

        if (shmHeader == nullptr || !shmHeader->initialize_mutex())
            return false;

        shmHeader->initialized.store(true, std::memory_order_release);
        shmHeader->refCount.store(0, std::memory_order_release);

        return true;
    }

    [[nodiscard]] bool setup_existing_region(bool& headerInvalid) noexcept {
        headerInvalid = false;

        struct stat Stat{};

        if (fstat(fd, &Stat) == -1)
        {
            std::cerr << "fstat failed: " << strerror(errno) << std::endl;

            return false;
        }

        if (std::size_t(Stat.st_size) < totalSize)
        {
            headerInvalid = true;

            return false;
        }

        mappedPtr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

        if (mappedPtr == MAP_FAILED)
        {
            std::cerr << "mmap() failed" << std::endl;

            mappedPtr = nullptr;

            return false;
        }

        dataPtr = static_cast<T*>(mappedPtr);
        shmHeader =
          std::launder(reinterpret_cast<ShmHeader*>(static_cast<char*>(mappedPtr) + sizeof(T)));

        if (shmHeader == nullptr)
            return false;

        if (!shmHeader->initialized.load(std::memory_order_acquire)
            || shmHeader->magic != ShmHeader::MAGIC)
        {
            headerInvalid = true;

            return false;
        }

        return true;
    }

    std::string name;
    int         fd = -1;

    void*       mappedPtr = nullptr;
    T*          dataPtr   = nullptr;
    ShmHeader*  shmHeader = nullptr;
    std::size_t totalSize = 0;
    std::string sentinelBase;
    std::string sentinelPath;
};

template<typename T>
class BackendSharedMemory final {
   public:
    BackendSharedMemory() noexcept = default;

    BackendSharedMemory(const std::string& shmName, const T& value) noexcept {
        shm.emplace(shmName);

        if (!shm->open(value))
            shm.reset();
    }

    bool is_valid() const noexcept { return shm && shm->is_open() && shm->is_initialized(); }

    void* get() const noexcept {
        return is_valid() ? reinterpret_cast<void*>(const_cast<T*>(&shm->get())) : nullptr;
    }

    SharedMemoryAllocationStatus get_status() const noexcept {
        return is_valid() ? SharedMemoryAllocationStatus::SharedMemory
                          : SharedMemoryAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const noexcept {
        if (!shm)
            return "Shared memory not available";

        if (!shm->is_open())
            return "Shared memory is not open";

        if (!shm->is_initialized())
            return "Shared memory not initialized";

        return std::nullopt;
    }

   private:
    std::optional<SharedMemory<T>> shm;
};

#endif

template<typename T>
struct FallbackBackendSharedMemory final {
   public:
    FallbackBackendSharedMemory() noexcept = default;

    FallbackBackendSharedMemory(const std::string&, const T& value) noexcept :
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

    // Content is addressed by its hash. An additional discriminator can be added to account for differences
    // that are not present in the content, for example NUMA node allocation.
    SystemWideSharedMemory(const T& value, std::size_t discriminator = 0) noexcept {
        std::size_t valueHash      = std::hash<T>{}(value);
        std::size_t executableHash = std::hash<std::string>{}(executable_path());

        std::string shmName = "Local\\don_" + std::to_string(valueHash)  //
                            + "$" + std::to_string(executableHash)       //
                            + "$" + std::to_string(discriminator);
#if !defined(_WIN32)
        // POSIX shared memory names must start with a slash
        // then add name hashing to avoid length limits
        shmName = "/don_" + create_hash_string(shmName);

        // POSIX APIs expect a fixed-size C string where the maximum length excluding the terminating null character ('\0').
        // Since std::string::size() does not include '\0', allow at most (MAX - 1) characters
        // to guarantee space for the terminator ('\0') in fixed-size buffers.
        constexpr std::size_t MaxNameSize = SHM_NAME_MAX_SIZE > 0 ? SHM_NAME_MAX_SIZE - 1 : 254;

        // Truncate the name if necessary so that shmName.c_str() always fits
        // within SHM_NAME_MAX_SIZE bytes including the null terminator.
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
