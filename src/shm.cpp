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
    #include <dirent.h>    // closedir(), opendir(), readdir(), DIR, dirent
    #include <poll.h>      // pollfd(), poll(), POLLIN, POLLERR, POLLHUP, POLLNVAL
    #include <sys/file.h>  // flock(), LOCK_EX, LOCK_UN
    #include <sys/time.h>  // timeval
    #include <sys/uio.h>   // iovec

    #include <cstdlib>       // atexit()
    #include <mutex>         // lock_guard
    #include <shared_mutex>  // shared_lock, shared_mutex
#endif

namespace DON {

// argv[0] CANNOT be used because need to identify the executable.
// argv[0] contains the command used to invoke it, which does not involve the full path.
// Just using a path is not fully resilient either, as the executable could have changed
// if it wasn't locked by the OS. If the path is longer than 4095 bytes the hash will be computed
// from an unspecified amount of bytes of the path; in particular it can a hash of an empty string.
std::string executable_path() noexcept {
    Array<char, PATH_MAX> executablePath{};
    usize                 executableSize = 0;

#if defined(_WIN32)
    DWORD size =
      GetModuleFileName(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));

    executableSize                 = std::min<usize>(size, executablePath.size() - 1);
    executablePath[executableSize] = '\0';

#elif defined(__APPLE__)
    u32 size = static_cast<u32>(executablePath.size());

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

#elif defined(__wasm__)

#else
    #error "Unsupported platform"

#endif

    // In case of any error the path will be empty
    return std::string{executablePath.data(), executableSize};
}

std::string normalize_shm_name(const std::string_view shmName) noexcept {
    std::string name(shmName);

#if defined(_WIN32)
    // Windows named shared memory names must start with "Local\" or "Global\"
    constexpr std::string_view LocalPrefix{"Local\\"};
    constexpr std::string_view GlobalPrefix{"Global\\"};

    if ((name.size() < LocalPrefix.size() || name.compare(0, LocalPrefix.size(), LocalPrefix) != 0)
        && (name.size() < GlobalPrefix.size()
            || name.compare(0, GlobalPrefix.size(), GlobalPrefix) != 0))
        name.insert(0, LocalPrefix);

#elif defined(USE_UNIX_SHM)
    // POSIX named shared memory names must start with slash ('/')
    constexpr char Prefix = '/';

    if (name.empty() || name[0] != Prefix)
        name.insert(name.begin(), Prefix);

#endif

    return name;
}

#if defined(_WIN32)


#elif defined(USE_UNIX_SHM)

BaseSharedMemory::BaseSharedMemory(const std::string_view shmName) noexcept :
    name_(normalize_shm_name(shmName)) {}

std::string_view BaseSharedMemory::name() const noexcept { return name_; }

// MemoryRegistry
//
// Provides a thread-safe process-wide registry for tracking registered memory
// objects (BaseSharedMemory) without owning them.
//
// The registry maintains:
//  - True insertion order for deterministic iteration
//  - Average O(1) registration and unregistration via list + hash map
//
// Key Features:
//  - Thread-safe registration and unregistration
//  - Deterministic iteration order
//  - Average O(1) lookup and removal
//  - Lightweight: stores raw pointers only; lifetime is managed externally
//
// Implementation:
//  - List preserves true insertion order
//  - IndexMap provides average O(1) lookup and stores an iterator into List
//
// Concurrency Model:
//  - Mutex protects both registry containers
//  - Read-only access uses shared locking
//  - Registration and unregistration use exclusive locking
//
// Usage:
//  - Call 'register_memory()' after successful memory creation
//  - Call 'unregister_memory()' before destruction
//
// Note:
//  - The registry does not own or release registered memory objects.
namespace MemoryRegistry {

namespace {

// Protects access to both registry containers.
std::shared_mutex Mutex;

// Preserves true insertion order for deterministic iteration.
MemoryList List;

// Provides average O(1) lookup and removal; stores an iterator into 'List'.
MemoryIndexMap IndexMap;

// Insert a memory object into both registry containers.
//
// The caller must hold 'Mutex' exclusively.
//
// Two-phase insertion:
//  1. Insert the pointer into IndexMap with a temporary list iterator.
//     This performs the duplicate check and inserts the map entry.
//  2. Append the pointer to List.
//  3. Replace the temporary iterator with the actual list iterator.
//
// This avoids a second map lookup while keeping both containers synchronized.
bool insert_memory_nolock(Memory memory) noexcept {
    auto [insertReg, inserted] = IndexMap.emplace(memory, List.end());

    // Already registered.
    if (!inserted)
        return false;

    //DEBUG_LOG("Registering memory: " << memory->name());

    // Append to the ordered list and obtain a stable iterator.
    auto insertItr = List.emplace(List.end(), memory);

    // Associate the map entry with its corresponding list node.
    insertReg->second = insertItr;

    return true;
}

// Remove a memory object from both registry containers.
//
// The caller must hold 'Mutex' exclusively.
//
// IndexMap stores the corresponding List iterator, allowing
// average O(1) removal from both containers without searching the list.
bool erase_memory_nolock(Memory memory) noexcept {
    auto eraseReg = IndexMap.find(memory);

    // Not registered.
    if (eraseReg == IndexMap.end())
        return false;

    // Retrieve the stable list iterator associated with this entry.
    auto eraseItr = eraseReg->second;

    // Internal consistency check.
    assert(eraseItr != List.end());

    // Remove the list node first.
    List.erase(eraseItr);

    // Remove the corresponding map entry.
    IndexMap.erase(eraseReg);

    //DEBUG_LOG("Unregistered memory: " << memory->name());

    return true;
}

}  // namespace

// Register a memory object.
//
// Returns false if:
//  - memory is nullptr
//  - the object is already registered
bool register_memory(Memory memory) noexcept {
    if (memory == nullptr)
    {
        //DEBUG_LOG("Cannot register <NULL> memory.");
        return false;
    }

    // Acquire an exclusive lock because both containers are modified.
    std::lock_guard writeLock(Mutex);

    return insert_memory_nolock(memory);
}

// Unregister a memory object from the global registry.
//
// Returns false if the object is nullptr or is not registered.
bool unregister_memory(Memory memory) noexcept {
    if (memory == nullptr)
        return false;

    // Acquire an exclusive lock because both containers are modified.
    std::lock_guard writeLock(Mutex);

    return erase_memory_nolock(memory);
}

// Detach registered memory objects from the registry.
//
// Returns the objects in true insertion order.
//
// List is moved out and IndexMap is cleared before the returned
// list is processed, allowing callers to safely operate on the objects
// without holding the registry lock.
MemoryList detach_memories() noexcept {
    std::lock_guard writeLock(Mutex);

    auto detachedList = std::move(List);
    IndexMap.clear();

    return detachedList;
}

// Returns the number of currently registered memory objects.
usize size() noexcept {
    std::shared_lock readLock(Mutex);

    return IndexMap.size();
}

// Prints the names of all registered memory objects in true insertion order.
//
// The registry lock is held for the duration of the iteration and output.
void print() noexcept {
    std::shared_lock readLock(Mutex);

    std::cout << "Registered memories (insertion order) [" << IndexMap.size() << "]:\n";

    usize i = 0;
    for (auto* memory : List)
        std::cout << "[" << i++ << "] " << (memory != nullptr ? memory->name() : "<NULL>") << "\n";

    std::cout << std::endl;
}

}  // namespace MemoryRegistry

// MemoryCleanup
//
// Provides cleanup of all currently registered memory objects.
//
// Responsibilities:
//  - Detach all registered memory objects from the registry
//  - Release each detached memory object
//
// Note:
//  - Registry management is handled by MemoryRegistry.
//  - Process-exit hook installation is handled by MemoryCleanupHook.
//  - Detached memory objects are released in registry insertion order.
namespace MemoryCleanup {

// Detaches and releases all currently registered memory objects in insertion order.
void cleanup() noexcept {
    auto memoryList = MemoryRegistry::detach_memories();

    //DEBUG_LOG("Memory cleanup started (" << memoryList.size() << " object(s)).");
    for (auto* const memory : memoryList)
        if (memory != nullptr)
            memory->release();
}

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
//   - Uses HookOnce to ensure the cleanup handler is registered only once.
//   - Registers MemoryCleanup::cleanup() with std::atexit().
//   - Does not manage the registry or perform cleanup itself.
//
// Note:
//   - The atexit() handler is guaranteed to be called only during normal program termination.
//     It is not called after SIGKILL, abort(), or other abnormal/forced program termination.
namespace MemoryCleanupHook {

namespace {

CallOnce HookOnce;

}  // namespace

// Ensures the memory cleanup handler is registered with std::atexit() only once.
void ensure_initialized() noexcept {
    HookOnce([]() noexcept {
        //DEBUG_LOG("Initializing MemoryCleanupHook.");

        std::atexit(MemoryCleanup::cleanup);
    });
}

}  // namespace MemoryCleanupHook


TempRoot::TempRoot(std::string path) noexcept :
    path_(std::move(path)) {}

const std::optional<TempRoot>& TempRoot::temp_root() noexcept {
    static const auto tempRoot = []() -> std::optional<TempRoot> {
        const uid_t uid = ::getuid();

        const std::string tempPath{std::string{"/tmp/DON-"} + std::to_string(uid)};

        if (::mkdir(tempPath.c_str(), S_IRWXU) == 0)
            return TempRoot{tempPath};

        if (errno != EEXIST)
            return std::nullopt;

        // Temp root already exists, verify ownership and permissions
        struct stat fileStat = {};

        if (::lstat(tempPath.c_str(), &fileStat) != 0)
            return std::nullopt;

        if (!S_ISDIR(fileStat.st_mode))
            return std::nullopt;

        if (fileStat.st_uid != uid)
            return std::nullopt;

        if ((fileStat.st_mode & ACCESSPERMS) != S_IRWXU)
            return std::nullopt;

        return TempRoot{tempPath};
    }();

    return tempRoot;
}

InitLock::InitLock(UniqueFd fd) noexcept :
    lockFd(std::move(fd)) {}

InitLock InitLock::acquire_lock(const std::string_view path) noexcept {
    UniqueFd fd(::open(path.data(), O_CREAT | O_RDWR | O_CLOEXEC, FILE_MODE));

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

void InitLock::unlock() noexcept {
    if (!lockFd.is_valid())
        return;

    (void) ::flock(lockFd.get(), LOCK_UN);
    lockFd.reset();
}

void* map_shared(const int fd, const usize size) noexcept {
    #if defined(__linux__)
    constexpr usize Alignment = 2 * MB;
    const long      pageSize  = ::sysconf(_SC_PAGESIZE);

    if (size >= Alignment && pageSize > 0)
    {
        // File-backed huge pages require matching virtual-address and file-offset alignment.
        // Reserve the address range first so MAP_FIXED cannot replace an unrelated mapping.
        const usize mappingSize =
          ((size + static_cast<usize>(pageSize) - 1) / static_cast<usize>(pageSize))
          * static_cast<usize>(pageSize);
        const usize reservationSize = mappingSize + Alignment;
        void*       reservation =
          ::mmap(nullptr, reservationSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (reservation != MAP_FAILED)
        {
            char* const base        = static_cast<char*>(reservation);
            char* const alignedBase = align_ptr_up<Alignment>(base);
            void*       mapped =
              ::mmap(alignedBase, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);

            if (mapped != MAP_FAILED)
            {
                const usize prefixSize = static_cast<usize>(alignedBase - base);
                const usize suffixSize = reservationSize - prefixSize - mappingSize;
                if (prefixSize != 0)
                    ::munmap(reservation, prefixSize);
                if (suffixSize != 0)
                    ::munmap(alignedBase + mappingSize, suffixSize);
                return mapped;
            }

            ::munmap(reservation, reservationSize);
        }
    }
    #endif

    return ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

std::string make_sentinel_base(const std::string_view name) noexcept {
    char buf[32];
    // Using std::to_string here causes non-deterministic PGO builds.
    // snprintf, being part of libc, is insensitive to the formatted values.
    std::snprintf(buf, sizeof(buf), "donshm_%016" PRIu64, hash_string(name));
    return buf;
}

void set_cloexec(const int fd) noexcept {
    if (!is_valid_fd(fd))
        return;

    const int flags = ::fcntl(fd, F_GETFD);
    if (flags != -1)
        (void) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

UniqueFd create_unix_socket() noexcept {
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

// Discover all peers in the shared dir
Strings get_peer_sockets(const std::string& sharedDir) noexcept {
    Strings peerSockets;

    DIR* dirPtr = ::opendir(sharedDir.c_str());
    if (dirPtr != nullptr)
    {
        const struct dirent* dirEntryPtr;
        while ((dirEntryPtr = ::readdir(dirPtr)) != nullptr)
        {
            std::string dName{dirEntryPtr->d_name};
            if (dName.size() >= 5 && dName.compare(dName.size() - 5, 5, ".sock") == 0)
                peerSockets.push_back(sharedDir + "/" + dName);
        }
        ::closedir(dirPtr);
    }

    return peerSockets;
}

UniqueFd try_receive_memfd(const std::string& sockPath) noexcept {
    auto peerFd = create_unix_socket();
    if (!peerFd.is_valid())
        return {};

    // 1-second timeout for connect and receive
    struct timeval tv = {1, 0};
    ::setsockopt(peerFd.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(peerFd.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_un addr = {};
    addr.sun_family         = AF_UNIX;
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

        ControlMsg controlMsg{};

        msg.msg_control    = controlMsg.buf;
        msg.msg_controllen = sizeof(controlMsg.buf);

        int flags = 0;
    #if defined(MSG_CMSG_CLOEXEC)
        flags |= MSG_CMSG_CLOEXEC;
    #endif

        ssize_t bytesRecv;

        do
            bytesRecv = ::recvmsg(peerFd.get(), &msg, flags);
        while (bytesRecv < 0 && errno == EINTR);

        if (bytesRecv > 0)
        {
            cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
            // Receive rights to the memFd from the peer; see make_server_thread
            if (cmsg != nullptr && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
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

enum class FD : u8 {
    SERVER,
    SHUTDOWN
};

constexpr usize FD_NB = 2;

// Server thread:
//  - Forwards the file descriptor fd
//  - Exits when shutdownFd is hung up on
//  - Listens on serverFd
std::thread make_server_thread(UniqueFd fd, UniqueFd shutdownFd, UniqueFd serverFd) noexcept {
    return std::thread([fd         = std::move(fd),          //
                        shutdownFd = std::move(shutdownFd),  //
                        serverFd   = std::move(serverFd)]() noexcept {
        struct pollfd fds[FD_NB];
        fds[FD::SERVER].fd     = serverFd.get();
        fds[FD::SERVER].events = POLLIN;

        fds[FD::SHUTDOWN].fd     = shutdownFd.get();
        fds[FD::SHUTDOWN].events = POLLIN;

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
            if ((fds[FD::SHUTDOWN].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0)
                break;

            if ((fds[FD::SERVER].revents & POLLIN) != 0)
            {
                // Another DON wants access
                UniqueFd clientFd
    #if defined(SOCK_CLOEXEC) && !defined(__APPLE__)
                  (::accept4(serverFd.get(), nullptr, nullptr, SOCK_CLOEXEC));
    #else
                  (::accept(serverFd.get(), nullptr, nullptr));
                set_cloexec(clientFd.get());
    #endif
                // ::accept() failed
                if (!clientFd.is_valid())
                    continue;

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
                const int yes = 1;
                ::setsockopt(clientFd.get(), SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    #endif

                int flags = 0;
    #if defined(MSG_NOSIGNAL)
                flags |= MSG_NOSIGNAL;
    #endif
                while (::sendmsg(clientFd.get(), &msg, flags) < 0 && errno == EINTR)
                {}
            }
        }
    });
}

#endif

}  // namespace DON
