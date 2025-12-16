/*
  DON, a UCI chess playing engine derived from Stockfish

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
#include <iomanip>
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
    #include <cassert>
    #include <cerrno>
    #include <cstdio>
    #include <cstdlib>
    #include <cstring>
    #include <dirent.h>
    #include <fcntl.h>
    #include <inttypes.h>
    #include <mutex>
    #include <new>
    #include <optional>
    #include <pthread.h>
    #include <semaphore.h>
    #include <signal.h>
    #include <string>
    #include <sys/file.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <type_traits>
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

enum class SystemWideSharedConstantAllocationStatus {
    NoAllocation,
    LocalMemory,
    SharedMemory
};

inline std::string to_string(SystemWideSharedConstantAllocationStatus status) noexcept {
    switch (status)
    {
    case SystemWideSharedConstantAllocationStatus::NoAllocation :
        return "No allocation";
    case SystemWideSharedConstantAllocationStatus::LocalMemory :
        return "Local memory";
    case SystemWideSharedConstantAllocationStatus::SharedMemory :
        return "Shared memory";
    default :
        return "Unknown status";
    }
}

inline std::string executable_path() noexcept {
    char        executablePath[4096] = {'\0'};
    std::size_t executableSize       = 0;

#if defined(_WIN32)
    DWORD size = GetModuleFileName(nullptr, executablePath, sizeof(executablePath));

    executableSize = std::min(std::size_t(size), sizeof(executablePath) - 1);

    executablePath[executableSize] = '\0';
#elif defined(__APPLE__)
    std::uint32_t size = std::uint32_t(sizeof(executablePath));
    if (_NSGetExecutablePath(executablePath, &size) == 0)
    {
        executableSize = std::strlen(executablePath);
    }
#elif defined(__linux__)
    ssize_t size = readlink("/proc/self/exe", executablePath, sizeof(executablePath) - 1);
    if (size >= 0)
    {
        executableSize = size;

        executablePath[executableSize] = '\0';
    }
#elif defined(__NetBSD__) || defined(__DragonFly__)
    ssize_t size = readlink("/proc/curproc/exe", executablePath, sizeof(executablePath) - 1);
    if (size >= 0)
    {
        executableSize = size;

        executablePath[executableSize] = '\0';
    }
#elif defined(__sun)  // Solaris
    const char* path = getexecname();
    if (path != nullptr)
    {
        std::strncpy(executablePath, path, sizeof(executablePath) - 1);

        executableSize = std::strlen(executablePath);
    }
#elif defined(__FreeBSD__)
    constexpr StdArray<int, 4> MIB{CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};

    std::size_t size = sizeof(executablePath);
    if (sysctl(MIB.data(), MIB.size(), executablePath, &size, nullptr, 0) == 0)
    {
        executableSize = std::min(size, sizeof(executablePath) - 1);

        executablePath[executableSize] = '\0';
    }
#endif

    // In case of any error the path will be empty
    return std::string(executablePath, executableSize);
}

#if defined(__ANDROID__)

// For systems that don't have shared memory, or support is troublesome.
// The way fallback is done is that need a dummy backend.
template<typename T>
class SharedMemoryBackend final {
   public:
    SharedMemoryBackend() = default;

    SharedMemoryBackend([[maybe_unused]] const std::string& shmName,
                        [[maybe_unused]] const T&           value) noexcept {}

    bool is_valid() const noexcept { return false; }

    void* get() const noexcept { return nullptr; }

    SystemWideSharedConstantAllocationStatus get_status() const noexcept {
        return SystemWideSharedConstantAllocationStatus::NoAllocation;
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
class SharedMemoryBackend final {
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

    static constexpr DWORD IS_INITIALIZED_VALUE = 1;

    SharedMemoryBackend() noexcept :
        status(Status::NotInitialized) {};

    SharedMemoryBackend(const std::string& shmName, const T& value) noexcept :
        status(Status::NotInitialized) {

        initialize(shmName, value);
    }

    bool is_valid() const noexcept { return status == Status::Success; }

    void* get() const noexcept { return is_valid() ? mapAddress : nullptr; }

    ~SharedMemoryBackend() noexcept { cleanup(); }

    SharedMemoryBackend(const SharedMemoryBackend&) noexcept            = delete;
    SharedMemoryBackend& operator=(const SharedMemoryBackend&) noexcept = delete;

    SharedMemoryBackend(SharedMemoryBackend&& shmBackend) noexcept :
        hMapFile(shmBackend.hMapFile),
        mapAddress(shmBackend.mapAddress),
        status(shmBackend.status),
        lastErrorStr(std::move(shmBackend.lastErrorStr)) {

        shmBackend.mapAddress = nullptr;
        shmBackend.hMapFile   = nullptr;
        shmBackend.status     = Status::NotInitialized;
    }
    SharedMemoryBackend& operator=(SharedMemoryBackend&& shmBackend) noexcept {
        if (this == &shmBackend)
            return *this;

        cleanup();

        hMapFile     = shmBackend.hMapFile;
        mapAddress   = shmBackend.mapAddress;
        status       = shmBackend.status;
        lastErrorStr = std::move(shmBackend.lastErrorStr);

        shmBackend.hMapFile   = nullptr;
        shmBackend.mapAddress = nullptr;
        shmBackend.status     = Status::NotInitialized;

        return *this;
    }

    SystemWideSharedConstantAllocationStatus get_status() const noexcept {
        return status == Status::Success ? SystemWideSharedConstantAllocationStatus::SharedMemory
                                         : SystemWideSharedConstantAllocationStatus::NoAllocation;
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
        std::size_t totalSize = sizeof(T) + sizeof(IS_INITIALIZED_VALUE);

        // Try allocating with large pages first.
        hMapFile = try_with_windows_lock_memory_privilege(
          [&](std::size_t largePageSize) {
              std::size_t roundedTotalSize = round_up_pow2(totalSize, largePageSize);

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

        // Fallback to normal allocation if no large pages available
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

        mapAddress = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);

        if (mapAddress == nullptr)
        {
            status       = Status::MapViewError;
            lastErrorStr = error_to_string(GetLastError());
            cleanup_partial();
            return;
        }

        // Use named mutex to ensure only one initializer
        std::string mutexName = shmName + "$mutex";

        HANDLE hMutex = CreateMutex(nullptr, FALSE, mutexName.c_str());

        if (hMutex == nullptr)
        {
            status       = Status::MutexCreateError;
            lastErrorStr = error_to_string(GetLastError());
            cleanup_partial();
            return;
        }

        if (WaitForSingleObject(hMutex, INFINITE) != WAIT_OBJECT_0)
        {
            status       = Status::MutexWaitError;
            lastErrorStr = error_to_string(GetLastError());
            cleanup_partial();
            CloseHandle(hMutex);
            return;
        }

        // Crucially, place the object first to ensure alignment
        volatile DWORD* isInitialized =
          std::launder(reinterpret_cast<DWORD*>(reinterpret_cast<char*>(mapAddress) + sizeof(T)));

        T* object = std::launder(reinterpret_cast<T*>(mapAddress));

        if (*isInitialized != IS_INITIALIZED_VALUE)
        {
            // First time initialization, message for debug purposes
            new (object) T{value};
            *isInitialized = IS_INITIALIZED_VALUE;
        }

        if (!ReleaseMutex(hMutex))
        {
            status       = Status::MutexReleaseError;
            lastErrorStr = error_to_string(GetLastError());
            cleanup_partial();
            CloseHandle(hMutex);
            return;
        }

        CloseHandle(hMutex);

        status = Status::Success;
    }

    void cleanup_partial() noexcept {
        if (mapAddress != nullptr)
        {
            UnmapViewOfFile(mapAddress);
            mapAddress = nullptr;
        }
        if (hMapFile != nullptr)
        {
            CloseHandle(hMapFile);
            hMapFile = nullptr;
        }
    }

    void cleanup() noexcept { cleanup_partial(); }

    HANDLE      hMapFile   = nullptr;
    void*       mapAddress = nullptr;
    Status      status     = Status::NotInitialized;
    std::string lastErrorStr;
};

#else

namespace internal {

struct ShmHeader final {
    static constexpr std::uint32_t SHM_MAGIC = 0xAD5F1A12U;

    pthread_mutex_t            mutex;
    std::atomic<std::uint32_t> refCount{0};
    std::atomic<bool>          initialized{false};
    std::uint32_t              magic = SHM_MAGIC;
};

class BaseSharedMemory {
   public:
    virtual ~BaseSharedMemory() = default;

    virtual void               close() noexcept      = 0;
    virtual const std::string& name() const noexcept = 0;
};

class SharedMemoryRegistry final {
   public:
    static void register_instance(BaseSharedMemory* memory) {
        std::scoped_lock scopeLock(mutex);
        activeMemories.insert(memory);
    }

    static void unregister_instance(BaseSharedMemory* memory) {
        std::scoped_lock scopeLock(mutex);
        activeMemories.erase(memory);
    }

    static void cleanup_all() noexcept {
        std::scoped_lock scopeLock(mutex);
        for (auto* memory : activeMemories)
            memory->close();
        activeMemories.clear();
    }

   private:
    static inline std::mutex                            mutex;
    static inline std::unordered_set<BaseSharedMemory*> activeMemories;
};

class CleanupHooks final {
   public:
    static void ensure_registered() noexcept {
        std::call_once(registerOnce, register_signal_handlers);
    }

   private:
    static void handle_signal(int sig) noexcept {
        SharedMemoryRegistry::cleanup_all();
        _Exit(128 + sig);
    }

    static void register_signal_handlers() noexcept {
        std::atexit([]() { SharedMemoryRegistry::cleanup_all(); });

        constexpr int signals[]{SIGHUP,  SIGINT,  SIGQUIT, SIGILL, SIGABRT, SIGFPE,
                                SIGSEGV, SIGTERM, SIGBUS,  SIGSYS, SIGXCPU, SIGXFSZ};

        struct sigaction sa;
        sa.sa_handler = handle_signal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;

        for (int sig : signals)
            sigaction(sig, &sa, nullptr);
    }

    static inline std::once_flag registerOnce;
};

inline int portable_fallocate(int fd, off_t offset, off_t length) noexcept {
    #if defined(__APPLE__)
    fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, offset, length, 0};
    int      rc    = fcntl(fd, F_PREALLOCATE, &store);
    if (rc == -1)
    {
        store.fst_flags = F_ALLOCATEALL;
        rc              = fcntl(fd, F_PREALLOCATE, &store);
    }
    if (rc != -1)
        rc = ftruncate(fd, offset + length);
    return rc;
    #else
    return posix_fallocate(fd, offset, length);
    #endif
}

}  // namespace internal

template<typename T>
class SharedMemory final: public internal::BaseSharedMemory {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   public:
    explicit SharedMemory(const std::string& name) noexcept :
        _name(name),
        totalSize(calculate_total_size()),
        sentinelBase(make_sentinel_base(name)) {}

    ~SharedMemory() noexcept override {
        internal::SharedMemoryRegistry::unregister_instance(this);
        close();
    }

    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& sharedMem) noexcept :
        _name(std::move(sharedMem._name)),
        _fd(sharedMem._fd),
        mappedPtr(sharedMem.mappedPtr),
        dataPtr(sharedMem.dataPtr),
        shmHeader(sharedMem.shmHeader),
        totalSize(sharedMem.totalSize),
        sentinelBase(std::move(sharedMem.sentinelBase)),
        sentinelPath(std::move(sharedMem.sentinelPath)) {

        internal::SharedMemoryRegistry::unregister_instance(&sharedMem);
        internal::SharedMemoryRegistry::register_instance(this);
        sharedMem.reset();
    }

    SharedMemory& operator=(SharedMemory&& sharedMem) noexcept {
        if (this == &sharedMem)
            return *this;

        internal::SharedMemoryRegistry::unregister_instance(this);
        close();

        _name        = std::move(sharedMem._name);
        _fd          = sharedMem._fd;
        mappedPtr    = sharedMem.mappedPtr;
        dataPtr      = sharedMem.dataPtr;
        shmHeader    = sharedMem.shmHeader;
        totalSize    = sharedMem.totalSize;
        sentinelBase = std::move(sharedMem.sentinelBase);
        sentinelPath = std::move(sharedMem.sentinelPath);

        internal::SharedMemoryRegistry::unregister_instance(&sharedMem);
        internal::SharedMemoryRegistry::register_instance(this);

        sharedMem.reset();
        return *this;
    }

    [[nodiscard]] bool open(const T& initialValue) noexcept {
        internal::CleanupHooks::ensure_registered();

        bool staleRetried = false;

        while (true)
        {
            if (is_open())
                return false;

            bool newCreated = false;

            _fd = shm_open(_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);

            if (_fd == -1)
            {
                _fd = shm_open(_name.c_str(), O_RDWR, 0666);
                if (_fd == -1)
                    return false;
            }
            else
                newCreated = true;

            if (!lock_file(LOCK_EX))
            {
                ::close(_fd);
                reset();
                return false;
            }

            bool headerInvalid = false;
            bool success =
              newCreated ? setup_new_region(initialValue) : setup_existing_region(headerInvalid);

            if (!success)
            {
                if (newCreated || headerInvalid)
                    shm_unlink(_name.c_str());
                if (mappedPtr != nullptr)
                    unmap_region();
                unlock_file();
                ::close(_fd);
                reset();

                if (!newCreated && headerInvalid && !staleRetried)
                {
                    staleRetried = true;
                    continue;
                }
                return false;
            }

            if (!lock_shared_mutex())
            {
                if (newCreated)
                    shm_unlink(_name.c_str());
                if (mappedPtr != nullptr)
                    unmap_region();
                unlock_file();
                ::close(_fd);
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
                unlock_shared_mutex();
                unmap_region();
                if (newCreated)
                    shm_unlink(_name.c_str());
                unlock_file();
                ::close(_fd);
                reset();
                return false;
            }

            shmHeader->refCount.fetch_add(1, std::memory_order_acq_rel);

            unlock_shared_mutex();
            unlock_file();
            internal::SharedMemoryRegistry::register_instance(this);
            return true;
        }
    }

    void close() noexcept override {
        if (_fd == -1 && mappedPtr == nullptr)
            return;

        bool regionRemove = false;
        bool fileLocked   = lock_file(LOCK_EX);
        bool mutexLocked  = false;

        if (fileLocked && shmHeader != nullptr)
            mutexLocked = lock_shared_mutex();

        if (mutexLocked)
        {
            if (shmHeader != nullptr)
                shmHeader->refCount.fetch_sub(1, std::memory_order_acq_rel);
            remove_sentinel_file();
            regionRemove = !has_other_live_sentinels_locked();
            unlock_shared_mutex();
        }
        else
        {
            remove_sentinel_file();
            decrement_refcount_relaxed();
        }

        unmap_region();

        if (regionRemove)
            shm_unlink(_name.c_str());

        if (fileLocked)
            unlock_file();

        if (_fd != -1)
        {
            ::close(_fd);
            _fd = -1;
        }

        reset();
    }

    const std::string& name() const noexcept override { return _name; }

    [[nodiscard]] bool is_open() const noexcept {
        return _fd != -1 && mappedPtr != nullptr && dataPtr != nullptr;
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

    static void cleanup_all_instances() noexcept { internal::SharedMemoryRegistry::cleanup_all(); }

   private:
    static constexpr std::size_t calculate_total_size() noexcept {
        return sizeof(T) + sizeof(internal::ShmHeader);
    }

    static std::string make_sentinel_base(const std::string& name) noexcept {
        std::string str(32, '\0');

        std::uint64_t hash = std::hash<std::string>{}(name);
        std::snprintf(str.data(), str.size(), "don_shm_%016" PRIx64, hash);
        return str;
    }

    static bool is_pid_alive(pid_t pid) noexcept {
        if (pid <= 0)
            return false;

        if (kill(pid, 0) == 0)
            return true;

        return errno == EPERM;
    }

    void reset() noexcept {
        _fd       = -1;
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
        if (_fd == -1)
            return false;

        while (flock(_fd, operation) == -1)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        return true;
    }

    void unlock_file() noexcept {
        if (_fd == -1)
            return;

        while (flock(_fd, LOCK_UN) == -1)
        {
            if (errno == EINTR)
                continue;
            break;
        }
    }

    std::string sentinel_full_path(pid_t pid) const {
        std::string path = "/dev/shm/";
        path += sentinelBase;
        path.push_back('.');
        path += std::to_string(pid);
        return path;
    }

    void decrement_refcount_relaxed() noexcept {
        if (shmHeader == nullptr)
            return;

        std::uint32_t expected = shmHeader->refCount.load(std::memory_order_relaxed);
        while (expected != 0
               && !shmHeader->refCount.compare_exchange_weak(
                 expected, expected - 1, std::memory_order_acq_rel, std::memory_order_relaxed))
        {}
    }

    bool create_sentinel_file_locked() noexcept {
        if (shmHeader == nullptr)
            return false;

        pid_t selfPid = getpid();
        sentinelPath  = sentinel_full_path(selfPid);

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            int fd = ::open(sentinelPath.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
            if (fd != -1)
            {
                ::close(fd);
                return true;
            }

            if (errno == EEXIST)
            {
                ::unlink(sentinelPath.c_str());
                decrement_refcount_relaxed();
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

    [[nodiscard]] bool initialize_shared_mutex() noexcept {
        if (shmHeader == nullptr)
            return false;

        pthread_mutexattr_t attr;
        if (pthread_mutexattr_init(&attr) != 0)
            return false;

        int rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    #if defined(PTHREAD_MUTEX_ROBUST)
        if (rc == 0)
            rc = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    #endif
        if (rc == 0)
            rc = pthread_mutex_init(&shmHeader->mutex, &attr);

        pthread_mutexattr_destroy(&attr);
        return rc == 0;
    }

    [[nodiscard]] bool lock_shared_mutex() noexcept {
        if (shmHeader == nullptr)
            return false;

        while (true)
        {
            int rc = pthread_mutex_lock(&shmHeader->mutex);
            if (rc == 0)
                return true;

    #if defined(PTHREAD_MUTEX_ROBUST)
            if (rc == EOWNERDEAD)
            {
                if (pthread_mutex_consistent(&shmHeader->mutex) == 0)
                    return true;
                return false;
            }
    #endif

            if (rc == EINTR)
                continue;

            return false;
        }
    }

    void unlock_shared_mutex() noexcept {
        if (shmHeader != nullptr)
            pthread_mutex_unlock(&shmHeader->mutex);
    }

    bool has_other_live_sentinels_locked() const noexcept {
        DIR* dir = opendir("/dev/shm");
        if (dir == nullptr)
            return false;

        std::string prefix = sentinelBase + ".";
        bool        found  = false;

        while (dirent* entry = readdir(dir))
        {
            std::string name = entry->d_name;
            if (name.rfind(prefix, 0) != 0)
                continue;

            auto  pidStr = name.substr(prefix.size());
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

            std::string stalePath = "/dev/shm/" + name;
            ::unlink(stalePath.c_str());
            const_cast<SharedMemory*>(this)->decrement_refcount_relaxed();
        }

        closedir(dir);
        return found;
    }

    [[nodiscard]] bool setup_new_region(const T& initialValue) noexcept {
        if (ftruncate(_fd, off_t(totalSize)) == -1)
            return false;

        if (internal::portable_fallocate(_fd, 0, off_t(totalSize)) != 0)
            return false;

        mappedPtr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
        if (mappedPtr == MAP_FAILED)
        {
            mappedPtr = nullptr;
            perror("mmap");
            return false;
        }

        dataPtr = static_cast<T*>(mappedPtr);
        shmHeader =
          reinterpret_cast<internal::ShmHeader*>(static_cast<char*>(mappedPtr) + sizeof(T));

        new (shmHeader) internal::ShmHeader{};
        new (dataPtr) T{initialValue};

        if (!initialize_shared_mutex())
            return false;

        shmHeader->refCount.store(0, std::memory_order_release);
        shmHeader->initialized.store(true, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool setup_existing_region(bool& headerInvalid) noexcept {
        headerInvalid = false;

        struct stat st;
        fstat(_fd, &st);
        if (std::size_t(st.st_size) < totalSize)
        {
            headerInvalid = true;
            return false;
        }

        mappedPtr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
        if (mappedPtr == MAP_FAILED)
        {
            mappedPtr = nullptr;
            perror("mmap");
            return false;
        }

        dataPtr   = static_cast<T*>(mappedPtr);
        shmHeader = std::launder(
          reinterpret_cast<internal::ShmHeader*>(static_cast<char*>(mappedPtr) + sizeof(T)));

        if (shmHeader == nullptr)
            return false;

        if (!shmHeader->initialized.load(std::memory_order_acquire)
            || shmHeader->magic != internal::ShmHeader::SHM_MAGIC)
        {
            headerInvalid = true;
            unmap_region();
            return false;
        }

        return true;
    }

    std::string _name;
    int         _fd = -1;

    void*                mappedPtr = nullptr;
    T*                   dataPtr   = nullptr;
    internal::ShmHeader* shmHeader = nullptr;
    std::size_t          totalSize = 0;
    std::string          sentinelBase;
    std::string          sentinelPath;
};

template<typename T>
[[nodiscard]] std::optional<SharedMemory<T>> create_shared(const std::string& name,
                                                           const T& initialValue) noexcept {
    SharedMemory<T> shm(name);

    if (shm.open(initialValue))
        return shm;

    return std::nullopt;
}

template<typename T>
class SharedMemoryBackend final {
   public:
    SharedMemoryBackend() = default;

    SharedMemoryBackend(const std::string& shmName, const T& value) noexcept :
        shm(create_shared<T>(shmName, value)) {}

    bool is_valid() const noexcept { return shm && shm->is_open() && shm->is_initialized(); }

    void* get() const noexcept {
        return is_valid() ? reinterpret_cast<void*>(const_cast<T*>(&shm->get())) : nullptr;
    }

    SystemWideSharedConstantAllocationStatus get_status() const noexcept {
        return is_valid() ? SystemWideSharedConstantAllocationStatus::SharedMemory
                          : SystemWideSharedConstantAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const noexcept {
        if (!shm)
            return "Shared memory not initialized";

        if (!shm->is_open())
            return "Shared memory is not open";

        if (!shm->is_initialized())
            return "Not initialized";

        return std::nullopt;
    }

   private:
    std::optional<SharedMemory<T>> shm;
};

#endif

template<typename T>
struct FallbackSharedMemoryBackend final {
    FallbackSharedMemoryBackend() noexcept = default;

    FallbackSharedMemoryBackend(const std::string&, const T& value) noexcept :
        fallbackObj(make_unique_aligned_large_pages<T>(value)) {}

    void* get() const { return fallbackObj.get(); }

    FallbackSharedMemoryBackend(const FallbackSharedMemoryBackend&) noexcept            = delete;
    FallbackSharedMemoryBackend& operator=(const FallbackSharedMemoryBackend&) noexcept = delete;

    FallbackSharedMemoryBackend(FallbackSharedMemoryBackend&& fallbackBackend) noexcept :
        fallbackObj(std::move(fallbackBackend.fallbackObj)) {}
    FallbackSharedMemoryBackend& operator=(FallbackSharedMemoryBackend&& fallbackBackend) noexcept {
        fallbackObj = std::move(fallbackBackend.fallbackObj);
        return *this;
    }

    SystemWideSharedConstantAllocationStatus get_status() const noexcept {
        return fallbackObj == nullptr ? SystemWideSharedConstantAllocationStatus::NoAllocation
                                      : SystemWideSharedConstantAllocationStatus::LocalMemory;
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
struct SystemWideSharedConstant final {
   public:
    // Can't run the destructor because it may be in a completely different process.
    // The object stored must also be obviously in-line but can't check for that,
    // other than some basic checks that cover most cases.
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_trivially_move_constructible_v<T>);
    static_assert(std::is_trivially_copy_constructible_v<T>);

    SystemWideSharedConstant() noexcept = default;

    // Content is addressed by its hash. An additional discriminator can be added to account for differences
    // that are not present in the content, for example NUMA node allocation.
    SystemWideSharedConstant(const T& value, std::size_t discriminator = 0) noexcept {
        std::size_t valueHash      = std::hash<T>{}(value);
        std::size_t executableHash = std::hash<std::string>{}(executable_path());

        std::string shmName = "Local\\don_" + std::to_string(valueHash)  //
                            + "$" + std::to_string(executableHash)       //
                            + "$" + std::to_string(discriminator);
#if !defined(_WIN32)
        // POSIX shared memory names must start with a slash
        shmName = "/don_" + create_hash_string(shmName);

        // Hash name and make sure it is not longer than SHM_NAME_MAX_SIZE
        if (shmName.size() > SHM_NAME_MAX_SIZE)
            shmName = shmName.substr(0, SHM_NAME_MAX_SIZE - 1);
#endif

        SharedMemoryBackend<T> shmBackend(shmName, value);

        if (shmBackend.is_valid())
            backend = std::move(shmBackend);
        else
            backend = FallbackSharedMemoryBackend<T>(shmName, value);
    }

    SystemWideSharedConstant(const SystemWideSharedConstant&) noexcept            = delete;
    SystemWideSharedConstant& operator=(const SystemWideSharedConstant&) noexcept = delete;

    SystemWideSharedConstant(SystemWideSharedConstant&& sysConstant) noexcept :
        backend(std::move(sysConstant.backend)) {}
    SystemWideSharedConstant& operator=(SystemWideSharedConstant&& sysConstant) noexcept {
        backend = std::move(sysConstant.backend);
        return *this;
    }

    const T& operator*() const noexcept {
        return *std::launder(reinterpret_cast<const T*>(get_ptr()));
    }

    bool operator==(std::nullptr_t) const noexcept { return get_ptr() == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return get_ptr() != nullptr; }

    SystemWideSharedConstantAllocationStatus get_status() const noexcept {
        return std::visit(
          [](const auto& end) -> SystemWideSharedConstantAllocationStatus {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
                  return SystemWideSharedConstantAllocationStatus::NoAllocation;
              else
                  return end.get_status();
          },
          backend);
    }

    std::optional<std::string> get_error_message() const noexcept {
        return std::visit(
          [](const auto& end) -> std::optional<std::string> {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
                  return std::nullopt;
              else
                  return end.get_error_message();
          },
          backend);
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
          backend);
    }

    std::variant<std::monostate, SharedMemoryBackend<T>, FallbackSharedMemoryBackend<T>> backend;
};

}  // namespace DON

#endif  // #ifndef SHM_H_INCLUDED
