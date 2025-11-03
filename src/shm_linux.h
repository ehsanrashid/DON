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

#ifndef SHM_LINUX_H_INCLUDED
#define SHM_LINUX_H_INCLUDED

#if !defined(_WIN32) && !defined(__ANDROID__)
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
    #include <signal.h>
    #include <string>
    #include <sys/file.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <type_traits>
    #include <unistd.h>
    #include <unordered_set>

    #if defined(__NetBSD__) || defined(__DragonFly__) || defined(__linux__)
        #include <limits.h>
        #define MAX_SEM_NAME_LEN NAME_MAX
    #elif defined(__APPLE__)
        #define MAX_SEM_NAME_LEN 31
    #else
        #define MAX_SEM_NAME_LEN 255
    #endif

namespace DON {

namespace internal {

struct ShmHeader final {
    static constexpr std::uint32_t SHM_MAGIC = 0xAD5F1A12U;
    pthread_mutex_t                mutex;
    std::atomic<std::uint32_t>     refCount{0};
    std::atomic<bool>              initialized{false};
    std::uint32_t                  magic = SHM_MAGIC;
};

class BaseSharedMemory {
   public:
    virtual ~BaseSharedMemory()                      = default;
    virtual void               close() noexcept      = 0;
    virtual const std::string& name() const noexcept = 0;
};

class SharedMemoryRegistry final {
   public:
    static void register_instance(BaseSharedMemory* instance) {
        std::scoped_lock scopeLock(mutex);
        activeInstances.insert(instance);
    }

    static void unregister_instance(BaseSharedMemory* instance) {
        std::scoped_lock scopeLock(mutex);
        activeInstances.erase(instance);
    }

    static void cleanup_all() noexcept {
        std::scoped_lock scopeLock(mutex);
        for (auto* instance : activeInstances)
            instance->close();
        activeInstances.clear();
    }

   private:
    static inline std::mutex                            mutex;
    static inline std::unordered_set<BaseSharedMemory*> activeInstances;
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
    int      ret   = fcntl(fd, F_PREALLOCATE, &store);
    if (ret == -1)
    {
        store.fst_flags = F_ALLOCATEALL;
        ret             = fcntl(fd, F_PREALLOCATE, &store);
    }
    if (ret != -1)
        ret = ftruncate(fd, offset + length);
    return ret;
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
        if (this != &sharedMem)
        {
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
        }
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
            _fd             = shm_open(_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);

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

        bool remove_region = false;
        bool file_locked   = lock_file(LOCK_EX);
        bool mutex_locked  = false;

        if (file_locked && shmHeader != nullptr)
            mutex_locked = lock_shared_mutex();

        if (mutex_locked)
        {
            if (shmHeader)
            {
                shmHeader->refCount.fetch_sub(1, std::memory_order_acq_rel);
            }
            remove_sentinel_file();
            remove_region = !has_other_live_sentinels_locked();
            unlock_shared_mutex();
        }
        else
        {
            remove_sentinel_file();
            decrement_refcount_relaxed();
        }

        unmap_region();

        if (remove_region)
            shm_unlink(_name.c_str());

        if (file_locked)
            unlock_file();

        if (_fd != -1)
        {
            ::close(_fd);
            _fd = -1;
        }

        reset();
    }

    const std::string& name() const noexcept override { return _name; }

    [[nodiscard]] bool is_open() const noexcept { return _fd != -1 && mappedPtr && dataPtr; }

    [[nodiscard]] const T& get() const noexcept { return *dataPtr; }

    [[nodiscard]] const T* operator->() const noexcept { return dataPtr; }

    [[nodiscard]] const T& operator*() const noexcept { return *dataPtr; }

    [[nodiscard]] uint32_t ref_count() const noexcept {
        return shmHeader ? shmHeader->refCount.load(std::memory_order_acquire) : 0;
    }

    [[nodiscard]] bool is_initialized() const noexcept {
        return shmHeader ? shmHeader->initialized.load(std::memory_order_acquire) : false;
    }

    static void cleanup_all_instances() noexcept { internal::SharedMemoryRegistry::cleanup_all(); }

   private:
    static constexpr size_t calculate_total_size() noexcept {
        return sizeof(T) + sizeof(internal::ShmHeader);
    }

    static std::string make_sentinel_base(const std::string& name) {
        uint64_t hash = std::hash<std::string>{}(name);
        char     buf[32];
        std::snprintf(buf, sizeof(buf), "sfshm_%016" PRIx64, static_cast<uint64_t>(hash));
        return buf;
    }

    static bool pid_is_alive(pid_t pid) noexcept {
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
        if (mappedPtr != nullptr)
        {
            munmap(mappedPtr, totalSize);
            mappedPtr = nullptr;
            dataPtr   = nullptr;
            shmHeader = nullptr;
        }
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
        if (!shmHeader)
            return;

        uint32_t expected = shmHeader->refCount.load(std::memory_order_relaxed);
        while (expected != 0
               && !shmHeader->refCount.compare_exchange_weak(
                 expected, expected - 1, std::memory_order_acq_rel, std::memory_order_relaxed))
        {}
    }

    bool create_sentinel_file_locked() noexcept {
        if (!shmHeader)
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
        if (!sentinelPath.empty())
        {
            ::unlink(sentinelPath.c_str());
            sentinelPath.clear();
        }
    }

    [[nodiscard]] bool initialize_shared_mutex() noexcept {
        if (!shmHeader)
            return false;

        pthread_mutexattr_t attr;
        if (pthread_mutexattr_init(&attr) != 0)
            return false;

        bool success = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) == 0;
    #if defined(PTHREAD_MUTEX_ROBUST)
        if (success)
            success = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST) == 0;
    #endif

        if (success)
            success = pthread_mutex_init(&shmHeader->mutex, &attr) == 0;

        pthread_mutexattr_destroy(&attr);
        return success;
    }

    [[nodiscard]] bool lock_shared_mutex() noexcept {
        if (!shmHeader)
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
        if (shmHeader)
            pthread_mutex_unlock(&shmHeader->mutex);
    }

    bool has_other_live_sentinels_locked() const noexcept {
        DIR* dir = opendir("/dev/shm");
        if (!dir)
            return false;

        std::string prefix = sentinelBase + ".";
        bool        found  = false;

        while (dirent* entry = readdir(dir))
        {
            std::string name = entry->d_name;
            if (name.rfind(prefix, 0) != 0)
                continue;

            auto  pidStr = name.substr(prefix.size());
            char* end    = nullptr;
            long  value  = std::strtol(pidStr.c_str(), &end, 10);
            if (end == nullptr || *end != '\0')
                continue;

            pid_t pid = static_cast<pid_t>(value);
            if (pid_is_alive(pid))
            {
                found = true;
                break;
            }

            std::string stalePath = std::string("/dev/shm/") + name;
            ::unlink(stalePath.c_str());
            const_cast<SharedMemory*>(this)->decrement_refcount_relaxed();
        }

        closedir(dir);
        return found;
    }

    [[nodiscard]] bool setup_new_region(const T& initialValue) noexcept {
        if (ftruncate(_fd, static_cast<off_t>(totalSize)) == -1)
            return false;

        if (internal::portable_fallocate(_fd, 0, static_cast<off_t>(totalSize)) != 0)
            return false;

        mappedPtr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
        if (mappedPtr == MAP_FAILED)
        {
            mappedPtr = nullptr;
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
        if (static_cast<size_t>(st.st_size) < totalSize)
        {
            headerInvalid = true;
            return false;
        }

        mappedPtr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
        if (mappedPtr == MAP_FAILED)
        {
            mappedPtr = nullptr;
            return false;
        }

        dataPtr   = static_cast<T*>(mappedPtr);
        shmHeader = std::launder(
          reinterpret_cast<internal::ShmHeader*>(static_cast<char*>(mappedPtr) + sizeof(T)));

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

}  // namespace DON

#endif  // !defined(_WIN32) && !defined(__ANDROID__)
#endif  // #ifndef SHM_LINUX_H_INCLUDED
