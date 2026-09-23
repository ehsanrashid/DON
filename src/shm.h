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

#include <cstddef>     // nullptr_t
#include <functional>  // hash<>
#include <iostream>    // IWYU pragma: keep: cout, cerr
#include <new>         // launder()
#include <string>
#include <string_view>
#include <type_traits>  // decay_t<>
#include <utility>      // move(), exchange()
#include <variant>      // monostate, visit(), variant<>

#if !defined(_WIN32)                                /* Non-Windows */ \
  && ((defined(__linux__) && !defined(__ANDROID__)) /* Linux (Non-Android) */ \
      || defined(__APPLE__)                         /* macOS / iOS */ \
      || defined(__sun)                             /* Solaris */ \
      || defined(__FreeBSD__)                       /* FreeBSD */ \
      || defined(__OpenBSD__)                       /* OpenBSD */ \
      || defined(__NetBSD__)                        /* NetBSD */ \
      || defined(__DragonFly__)                     /* DragonFly BSD */ \
      || defined(__e2k__)                           /* Elbrus 2000 */ \
      || defined(_AIX))                             /* IBM AIX */
    #define USE_UNIX_SHM
#endif

#if defined(_WIN32)
    #if !defined(PATH_MAX)
        #define PATH_MAX (2 * 1024)  // 2K bytes, safe for almost all paths
    #endif
    #if !defined(NAME_MAX)
        #define NAME_MAX 255
    #endif

    #include "platform_win.h"

#elif defined(USE_UNIX_SHM)
    #include <fcntl.h>  // open(), fcntl(), FD_CLOEXEC
    #include <limits.h>
    #include <sys/mman.h>    // munmap(), memfd_create(), MFD_CLOEXEC
    #include <sys/socket.h>  // socket(), bind(), listen(), accept(), connect(), send(), recv()
    #include <sys/stat.h>
    #include <sys/types.h>  // IWYU pragma: keep
    #include <sys/un.h>     // sockaddr_un
    #include <unistd.h>  // close(), read()/write(), unlink(), sleep(), getpid(), pipe()/pipe2(), fsync()

    #include <cassert>
    #include <cerrno>
    #include <cstring>  // strncpy
    #include <list>
    #include <optional>
    #include <unordered_map>
    #include <unordered_set>

    // Linux (non-Android)
    #if defined(__linux__) && !defined(__ANDROID__)
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

    #if !defined(ACCESSPERMS)
        #define ACCESSPERMS (S_IRWXU | S_IRWXG | S_IRWXO) /* 0777 */
    #endif
    #if !defined(ALLPERMS)
        #define ALLPERMS (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO) /* 07777 */
    #endif
    #if !defined(DEFFILEMODE)
        #define DEFFILEMODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) /* 0666*/
    #endif
    #if !defined(S_BLKSIZE)
        #define S_BLKSIZE 512 /* Block size for `st_blocks' */
    #endif
    #if (defined(__linux__))
        #if !defined(MADV_COLLAPSE)
            #define MADV_COLLAPSE 25
        #endif
    #endif
#endif

#include "memory.h"  // LargePagePtr<>, make_unique_aligned_large_page()
#include "misc.h"
#include "native_thread.h"

namespace DON {

inline constexpr usize SHM_NAME_MAX = NAME_MAX - 1;

enum class SharedMemoryAllocationStatus : u8 {
    NoAllocation,
    LocalMemory,
    SharedMemory
};

[[nodiscard]] constexpr std::string_view
to_string(const SharedMemoryAllocationStatus status) noexcept {
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

// argv[0] CANNOT be used because need to identify the executable.
// argv[0] contains the command used to invoke it, which does not involve the full path.
// Just using a path is not fully resilient either, as the executable could have changed
// if it wasn't locked by the OS. If the path is longer than 4095 bytes the hash will be computed
// from an unspecified amount of bytes of the path; in particular it can a hash of an empty string.
std::string executable_path() noexcept;

std::string normalize_shm_name(std::string_view shmName) noexcept;

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
        name_(normalize_shm_name(shmName)),
        status(Status::NotInitialized) {
        //DEBUG_LOG("Creating shared memory with name: " << name());

        initialize(value);
    }

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept :
        mapFileHandleGuard{mapFileHandle},
        mappedGuard{mappedPtr} {
        move(std::move(backendShm));
    }
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept {
        if (this == &backendShm)
            return *this;

        reset();

        move(std::move(backendShm));

        return *this;
    }

    ~BackendSharedMemory() noexcept { reset(); }

    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    [[nodiscard]] bool is_valid() const noexcept { return status == Status::Success; }

    [[nodiscard]] void* get() const noexcept { return is_valid() ? mappedPtr : MMAP_PTR_INVALID; }

    [[nodiscard]] SharedMemoryAllocationStatus get_status() const noexcept {
        return status == Status::Success ? SharedMemoryAllocationStatus::SharedMemory
                                         : SharedMemoryAllocationStatus::NoAllocation;
    }

    [[nodiscard]] std::string_view get_error_message() const noexcept {
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

   private:
    void initialize(const T& value) noexcept {
        constexpr usize TotalSize = sizeof(T) + sizeof(SharedState);

        // Try allocating with large page first
        mapFileHandle = try_with_windows_lock_memory_privilege(
          [&](const usize largePageSize) noexcept {
              // Round up size to full large page
              const usize roundedTotalSize = round_up_to_multiple(TotalSize, largePageSize);

    #if defined(_WIN64)
              DWORD hiTotalSize = roundedTotalSize >> 32;
              DWORD loTotalSize = roundedTotalSize & 0xFFFFFFFFu;
    #else
              DWORD hiTotalSize = 0;
              DWORD loTotalSize = roundedTotalSize;
    #endif

              //DEBUG_LOG("Allocating large page shared memory, size = " << roundedTotalSize << " bytes");
              return ::CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
                                         PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,  //
                                         hiTotalSize, loTotalSize, name().data());
          },
          []() { return HANDLE_INVALID; });

        // Fallback to normal allocation if no large page available
        if (!mapFileHandleGuard.is_valid())
        {
            //DEBUG_LOG("Allocating normal shared memory, size = " << TotalSize << " bytes");
            mapFileHandle = ::CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,  //
                                                0, TotalSize, name().data());
        }

        if (!mapFileHandleGuard.is_valid())
        {
            //DEBUG_LOG("CreateFileMapping() failed: name = " << name() << ", error = " << error_to_string(GetLastError()));
            status = Status::FileMapping;
            return;
        }

        mappedPtr = ::MapViewOfFile(mapFileHandleGuard.get(), FILE_MAP_ALL_ACCESS, 0, 0, TotalSize);

        if (!mappedGuard.is_valid())
        {
            //DEBUG_LOG("MapViewOfFile() failed: name = " << name() << ", error = " << error_to_string(GetLastError()));
            status = Status::MapView;
            reset();
            return;
        }

        // Use named mutex to ensure serialize initialization
        const auto mutexName = std::string{name()} + "$mutex";

        HANDLE mutexHandle = ::CreateMutex(nullptr, FALSE, mutexName.c_str());

        HandleGuard mutexHandleGuard{mutexHandle};

        if (!mutexHandleGuard.is_valid())
        {
            //DEBUG_LOG("CreateMutex() failed: name = " << mutexName << ", error = " << error_to_string(GetLastError()));
            status = Status::MutexCreate;
            reset();
            return;
        }

        // Wait for mutex ownership
        const DWORD waitResult = ::WaitForSingleObject(mutexHandleGuard.get(), INFINITE);

        if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED)
        {
            //DEBUG_LOG("WaitForSingleObject() failed: name = " << mutexName << ", error = " << error_to_string(GetLastError()));
            status = Status::MutexWait;
            reset();
            return;
        }

        [[maybe_unused]] const bool mutexAbandoned = waitResult == WAIT_ABANDONED;

        // The mapped base is page-aligned. Align the state after T for Interlocked access
        constexpr usize StateAlignment = alignof(LONG);
        constexpr usize StateOffset    = (sizeof(T) + StateAlignment - 1) & ~(StateAlignment - 1);

        auto* object = reinterpret_cast<T*>(mappedGuard.get());

        auto* sharedState = reinterpret_cast<volatile LONG*>(
          reinterpret_cast<char*>(mappedGuard.get()) + StateOffset);

        // Atomically read the initialization state.
        const LONG state = ::InterlockedCompareExchange(sharedState, 0, 0);

        switch (state)
        {
        case LONG(SharedState::Initialized) :
            // Already initialized

            //DEBUG_LOG("Shared memory already initialized: name = " << name());
            status = Status::Success;
            break;

        case LONG(SharedState::Uninitialized) :
            // Mark initialization before constructing T
            ::InterlockedExchange(sharedState, LONG(SharedState::Initializing));

            // Initialize the object
            new (object) T{value};

            // Publish the fully constructed object
            ::InterlockedExchange(sharedState, LONG(SharedState::Initialized));

            // Obtain a pointer to the constructed object if needed
            object = std::launder(object);

            //DEBUG_LOG("Shared memory initialized successfully: name = " << name());
            status = Status::Success;
            break;

        default :
            // Initialization was interrupted.
            // Do not blindly reconstruct over a potentially partially constructed object.

            //DEBUG_LOG("Shared memory initialization incomplete: name = " << name());
            status = Status::MutexWait;
            break;
        }

        if (!::ReleaseMutex(mutexHandleGuard.get()))
        {
            //DEBUG_LOG("ReleaseMutex() failed: name = " << mutexName << ", error = " << error_to_string(GetLastError()));
            status = Status::MutexRelease;
            reset();
            return;
        }

        if (status != Status::Success)
        {
            reset();
            return;
        }

        //DEBUG_LOG("Shared memory initialization done, name: " << name());
    }

    void move(BackendSharedMemory&& backendShm) noexcept {
        name_         = std::move(backendShm.name_);
        mapFileHandle = std::exchange(backendShm.mapFileHandle, HANDLE_INVALID);
        mappedPtr     = std::exchange(backendShm.mappedPtr, MMAP_PTR_INVALID);
        status        = std::exchange(backendShm.status, Status::NotInitialized);
    }

    void reset() noexcept {
        //DEBUG_LOG("Cleaning up shared memory, name: " << name());
        mappedGuard.reset();
        mapFileHandleGuard.reset();
    }

    enum class SharedState : u8 {
        Uninitialized = 0,
        Initializing  = 1,
        Initialized   = 2
    };

    std::string name_;
    HANDLE      mapFileHandle = HANDLE_INVALID;
    HandleGuard mapFileHandleGuard{mapFileHandle};
    void*       mappedPtr = MMAP_PTR_INVALID;
    MMapGuard   mappedGuard{mappedPtr};
    Status      status = Status::NotInitialized;
};

#elif defined(USE_UNIX_SHM)
class BaseSharedMemory {
   public:
    explicit BaseSharedMemory(std::string_view shmName) noexcept;

    BaseSharedMemory(const BaseSharedMemory&)            = delete;
    BaseSharedMemory& operator=(const BaseSharedMemory&) = delete;

    BaseSharedMemory(BaseSharedMemory&&) noexcept            = default;
    BaseSharedMemory& operator=(BaseSharedMemory&&) noexcept = default;

    virtual ~BaseSharedMemory() noexcept = default;

    virtual void reset() noexcept = 0;

    [[nodiscard]] std::string_view name() const noexcept;

   protected:
    std::string name_;
};

// MemoryRegistry
//
// Provides a thread-safe process-wide registry for tracking registered memory
// objects (BaseSharedMemory) without owning them.
//
// The registry provides:
//  - True insertion order through List
//  - Average O(1) fast lookup, count and removal through IndexMap
//  - Average O(1) fast membership validation through Set
//  - Average O(1) fast registration and unregistration by maintaining all containers
//
// Key Features:
//  - Thread-safe registration and unregistration
//  - Deterministic iteration order
//  - Average O(1) fast lookup, count and removal
//  - Lightweight: stores raw pointers only; lifetime is managed externally
//
// Implementation:
//  - List preserves true insertion order for deterministic iteration
//  - IndexMap provides average O(1) lookup and maps each memory to its
//    corresponding iterator in List
//  - Set provides uniqueness and membership validation
//
// Concurrency Model:
//  - Mutex protects all registry containers
//  - Read-only access uses shared locking
//  - Registration and unregistration use exclusive locking
//
// Usage:
//  - Call 'register_memory()' after successful memory creation
//  - Call 'unregister_memory()' before destruction
//
// Note:
//  - The registry does not own or reset registered memory objects.
namespace MemoryRegistry {

using Memory         = BaseSharedMemory*;
using MemoryList     = std::list<Memory>;
using MemoryIndexMap = std::unordered_map<Memory, MemoryList::iterator>;
using MemorySet      = std::unordered_set<Memory>;

// Register a memory object in the registry.
//
// Returns false if:
//  - memory is nullptr
//  - the object is already registered
bool register_memory(Memory memory) noexcept;

// Unregister a memory object from the registry.
//
// Returns false if:
//  - memory is nullptr
//  - the object is not registered
bool unregister_memory(Memory memory) noexcept;

// Detach all registered memory objects from the registry.
//
// Returns the objects in true insertion order.
//
// All registry containers are cleared before the returned list is processed,
// allowing callers to safely operate on the objects without holding 'Mutex'.
MemoryList detach_memories() noexcept;

// Returns the number of currently registered memory objects.
usize size() noexcept;
bool  empty() noexcept;

// Prints the addresses and names of all registered memory objects
// in true insertion order.
//
// The registry lock is held for the duration of the iteration and output.
void print() noexcept;

}  // namespace MemoryRegistry

// MemoryCleanup
//
// Provides cleanup of all currently registered memory objects.
//
// Responsibilities:
//  - Detach all registered memory objects from the registry
//  - Reset each detached memory object
//
// Note:
//  - Registry management is handled by MemoryRegistry.
//  - Process-exit hook installation is handled by MemoryCleanupHook.
//  - Detached memory objects are reset in registry insertion order.
namespace MemoryCleanup {

// Detaches and reset all currently registered memory objects in insertion order.
void cleanup() noexcept;

}  // namespace MemoryCleanup

// MemoryCleanupHook
//
// Provides one-time installation of the memory cleanup handler for normal
// program termination.
//
// Usage:
//   Call MemoryCleanupHook::ensure_initialized() early in main().
//
// Key Features:
//   - Uses HookCallOnce to ensure the cleanup handler is registered only once.
//   - Retries initialization until the cleanup handler is successfully registered.
//   - Registers MemoryCleanup::cleanup() with std::atexit().
//   - Does not manage the registry or perform cleanup itself.
//
// Note:
//   - The atexit() handler is guaranteed to be called only during normal program termination.
//     It is not called after SIGKILL, abort(), or other abnormal/forced program termination.
namespace MemoryCleanupHook {

// Ensures the memory cleanup handler is successfully registered with std::atexit().
// Initialization is retried until successful; subsequent calls return immediately.
void ensure_initialized() noexcept;

}  // namespace MemoryCleanupHook

// TempRoot
//
// Manages a private temporary directory used by the application for storing
// temporary runtime files.
//
// The directory is created under /tmp using the format:
//     /tmp/DON-[uid]
//
// The directory is created with owner-only permissions (S_IRWXU = 0700). If the directory
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
    static const std::optional<TempRoot>& temp_root() noexcept;

    [[nodiscard]] std::string_view path() const noexcept { return path_; }

   private:
    explicit TempRoot(std::string path) noexcept;

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

    static InitLock acquire_lock(std::string_view path) noexcept;

    [[nodiscard]] bool is_valid() const noexcept { return lockFd.is_valid(); }

   private:
    explicit InitLock(UniqueFd fd) noexcept;

    void unlock() noexcept;

    UniqueFd lockFd;
};

union ControlMsg final {
    char           buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr align;
};

void* map_shared(int fd, usize size) noexcept;

std::string make_sentinel_base(std::string_view name) noexcept;

void set_cloexec(const int fd) noexcept;

UniqueFd create_unix_socket() noexcept;

// Discover all peers in the shared dir
Strings get_peer_sockets(const std::string& sharedDir) noexcept;

UniqueFd try_create_memfd(const std::string& sockPath) noexcept;

// Server thread:
//  - Forwards the file descriptor fd
//  - Exits when shutdownFd is hung up on
//  - Listens on serverFd
NativeThread make_server_thread(UniqueFd fd, UniqueFd shutdownFd, UniqueFd serverFd) noexcept;

template<typename T>
class SharedMemory final: public BaseSharedMemory {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "T cannot be a pointer type");

   public:
    explicit SharedMemory(std::string_view shmName, const TempRoot& tempRoot) noexcept :
        BaseSharedMemory(shmName),
        sharedDir(std::string{tempRoot.path()} + "/" + make_sentinel_base(name())),
        initLockPath(sharedDir + "/init_lock"),
        socketPath(sharedDir + "/" + std::to_string(::getpid()) + ".sock") {}

    ~SharedMemory() noexcept override { reset_with_registry(); }

    SharedMemory(const SharedMemory&)            = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    SharedMemory(SharedMemory&& sharedMemory) noexcept :
        BaseSharedMemory(std::move(sharedMemory)) {
        move_with_registry(std::move(sharedMemory));
    }
    SharedMemory& operator=(SharedMemory&& sharedMemory) noexcept {
        if (this == &sharedMemory)
            return *this;

        [[maybe_unused]] const bool unregistered = MemoryRegistry::unregister_memory(this);
        assert(unregistered);

        reset();

        BaseSharedMemory::operator=(std::move(sharedMemory));
        move_with_registry(std::move(sharedMemory));

        return *this;
    }

    [[nodiscard]] static std::optional<SharedMemory<T>> create(std::string_view name,
                                                               const T&         value) noexcept {
        MemoryCleanupHook::ensure_initialized();

        const auto& tempRoot = TempRoot::temp_root();

        if (!tempRoot)
            return std::nullopt;

        SharedMemory<T> shm(name, *tempRoot);

        if (!shm.open(value))
            return std::nullopt;

        return shm;
    }

    [[nodiscard]] bool open(const T& value) noexcept {
        if (socketPath.size() >= sizeof(sockaddr_un::sun_path))
            return false;

        if (::mkdir(sharedDir.c_str(), S_IRWXU) != 0)
            if (errno != EEXIST)
                return false;

        auto initLock = InitLock::acquire_lock(initLockPath);
        if (!initLock.is_valid())
            return false;

        // Try to receive the shared memFd
        UniqueFd memFd;
        Strings  peerSockets = get_peer_sockets(sharedDir);
        for (const auto& sockPath : peerSockets)
        {
            memFd = try_create_memfd(sockPath);
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
            std::string tempPath = "/tmp/DON_replicated_data.XXXXXX";

            memFd.reset(::mkstemp(tempPath.data()));
            if (!memFd.is_valid())
                return false;
            set_cloexec(memFd.get());
            ::unlink(tempPath.c_str());
    #endif

            if (::ftruncate(memFd.get(), sizeof(T)) != 0)
                return false;
        }

        assert(memFd.is_valid());

        // Try to map the memFd
        T* mappedMem = static_cast<T*>(map_shared(memFd.get(), sizeof(T)));
        if (mappedMem == MAP_FAILED)
            return false;

    #if defined(MADV_HUGEPAGE)
        (void) ::madvise(mappedMem, sizeof(T), MADV_HUGEPAGE);
    #endif

        if (creator)
        {
            // Creator is responsible for initialization
            *mappedMem = value;

    #if defined(MADV_COLLAPSE)
            (void) ::madvise(mappedMem, sizeof(T), MADV_COLLAPSE);
    #endif
        }

        mappedPtr = dataPtr = mappedMem;

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

        UniqueFd receiverShutdownFd(shutdownPipe[0]);
        shutdownFd = UniqueFd{shutdownPipe[1]};

        // Create the server socket
        auto serverFd = create_unix_socket();
        if (!serverFd.is_valid())
            return false;

        // Prepare the Unix socket address
        struct sockaddr_un sockAddr{};
        sockAddr.sun_family = AF_UNIX;
        std::strncpy(sockAddr.sun_path, socketPath.c_str(), sizeof(sockAddr.sun_path) - 1);

        // Remove any stale socket path before binding
        unlink_socket_path();

        const auto sFd = serverFd.get();

        // Bind the socket to the Unix socket address
        if (::bind(sFd, reinterpret_cast<struct sockaddr*>(&sockAddr), sizeof(sockAddr)) == -1)
            return false;

        // Start listening for incoming connections
        if (::listen(sFd, 5) == -1)
            return false;

        // Start the server thread with ownership of the communication resources
        serverThread =
          make_server_thread(std::move(memFd), std::move(receiverShutdownFd), std::move(serverFd));
        assert(serverThread.joinable());
        if (!serverThread.joinable())
            return false;

        // Register for cleanup at exit
        [[maybe_unused]] const bool registered = MemoryRegistry::register_memory(this);
        assert(registered);

        return true;
    }

    [[nodiscard]] bool is_mapped() const noexcept { return mappedPtr != nullptr; }

    [[nodiscard]] bool is_serving() const noexcept { return serverThread.joinable(); }

    [[nodiscard]] const T& get() const noexcept {
        assert(dataPtr != nullptr);

        return *dataPtr;
    }

   private:
    // Move the resources from another SharedMemory object.
    //
    // The registry tracks SharedMemory object addresses, not the resources
    // they own. Moving the resources therefore transfers ownership from
    // 'sharedMemory' to 'this', so the registry entry must be moved as well:
    //  - unregister the source object
    //  - register the destination object
    void move_with_registry(SharedMemory&& sharedMemory) noexcept {
        [[maybe_unused]] const bool unregistered = MemoryRegistry::unregister_memory(&sharedMemory);
        assert(unregistered);

        mappedPtr    = std::exchange(sharedMemory.mappedPtr, nullptr);
        dataPtr      = std::exchange(sharedMemory.dataPtr, nullptr);
        sharedDir    = std::move(sharedMemory.sharedDir);
        initLockPath = std::move(sharedMemory.initLockPath);
        socketPath   = std::move(sharedMemory.socketPath);
        serverThread = std::move(sharedMemory.serverThread);
        shutdownFd   = std::move(sharedMemory.shutdownFd);

        [[maybe_unused]] const bool registered = MemoryRegistry::register_memory(this);
        assert(registered);
    }

    // Unregister SharedMemory object and reset resources
    bool reset_with_registry() noexcept {
        if (!MemoryRegistry::unregister_memory(this))
            return false;

        reset();
        return true;
    }

    // Swap the resources between two SharedMemory objects.
    //
    // No registry update is required because the registry tracks the
    // SharedMemory object addresses, not the resources owned by them.
    // Swapping the resources therefore leaves both registry entries valid.
    void swap(SharedMemory& sharedMemory) noexcept {
        std::swap(name_, sharedMemory.name_);
        std::swap(mappedPtr, sharedMemory.mappedPtr);
        std::swap(dataPtr, sharedMemory.dataPtr);
        std::swap(sharedDir, sharedMemory.sharedDir);
        std::swap(initLockPath, sharedMemory.initLockPath);
        std::swap(socketPath, sharedMemory.socketPath);
        std::swap(serverThread, sharedMemory.serverThread);
        std::swap(shutdownFd, sharedMemory.shutdownFd);
    }

    // Unlink the socket path without clearing the stored path
    void unlink_socket_path() noexcept {
        if (!socketPath.empty())
            ::unlink(socketPath.c_str());
    }

    // Unmap region
    void unmap_region() noexcept {
        if (mappedPtr != nullptr)
            ::munmap(mappedPtr, sizeof(T));
        mappedPtr = nullptr;
        dataPtr   = nullptr;
    }

    // Reset all resources and reset the object state
    void reset() noexcept override {
        shutdownFd.reset();

        if (serverThread.joinable())
            serverThread.join();

        unmap_region();

        unlink_socket_path();
        socketPath.clear();
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
    std::string  socketPath;
    NativeThread serverThread;
    UniqueFd     shutdownFd;  // close to signal server thread shutdown
};

template<typename T>
class BackendSharedMemory final {
   public:
    BackendSharedMemory() noexcept = default;

    BackendSharedMemory(std::string_view shmName, const T& value) noexcept :
        shm(SharedMemory<T>::create(shmName, value)) {}

    BackendSharedMemory(const BackendSharedMemory&) noexcept            = delete;
    BackendSharedMemory& operator=(const BackendSharedMemory&) noexcept = delete;

    BackendSharedMemory(BackendSharedMemory&& backendShm) noexcept            = default;
    BackendSharedMemory& operator=(BackendSharedMemory&& backendShm) noexcept = default;

    [[nodiscard]] bool is_valid() const noexcept {
        return shm && shm->is_mapped() && shm->is_serving();
    }

    [[nodiscard]] void* get() const noexcept {
        return is_valid() ? reinterpret_cast<void*>(const_cast<T*>(&shm->get())) : nullptr;
    }

    [[nodiscard]] SharedMemoryAllocationStatus get_status() const noexcept {
        return is_valid() ? SharedMemoryAllocationStatus::SharedMemory
                          : SharedMemoryAllocationStatus::NoAllocation;
    }

    [[nodiscard]] std::string_view get_error_message() const noexcept {
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

    [[nodiscard]] bool is_valid() const noexcept { return false; }

    [[nodiscard]] void* get() const noexcept { return nullptr; }

    [[nodiscard]] SharedMemoryAllocationStatus get_status() const noexcept {
        return SharedMemoryAllocationStatus::NoAllocation;
    }

    [[nodiscard]] std::string_view get_error_message() const noexcept {
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

    [[nodiscard]] void* get() const noexcept { return fallbackObj.get(); }

    [[nodiscard]] SharedMemoryAllocationStatus get_status() const noexcept {
        return fallbackObj != nullptr ? SharedMemoryAllocationStatus::LocalMemory
                                      : SharedMemoryAllocationStatus::NoAllocation;
    }

    [[nodiscard]] std::string_view get_error_message() const noexcept {
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
    SystemWideSharedMemory(const T& value, const u64 discriminatorHash = 0) noexcept {

        const usize valueHash      = std::hash<T>{}(value);
        const u64   executableHash = hash_string(executable_path());

        // Create a unique name based on the value, executable path, and discriminator
        // Hex hashes separated by dollar signs
        const auto hashName = usize_to_hex(valueHash) + '$'     //
                            + u64_to_hex(executableHash) + '$'  //
                            + u64_to_hex(discriminatorHash);

        auto shmName = std::string{"DON_"} + hashName;

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

    [[nodiscard]] SharedMemoryAllocationStatus get_status() const noexcept {
        return std::visit(
          [](const auto& end) -> SharedMemoryAllocationStatus {
              if constexpr (std::is_same_v<std::decay_t<decltype(end)>, std::monostate>)
                  return SharedMemoryAllocationStatus::NoAllocation;
              else
                  return end.get_status();
          },
          backendShm);
    }

    [[nodiscard]] std::string_view get_error_message() const noexcept {
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

#endif  // SHM_H_INCLUDED
