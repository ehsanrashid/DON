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
        struct stat fileStat{};

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

InitLock InitLock::acquire_lock(std::string_view path) noexcept {
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

void* map_shared(int fd, usize size) noexcept {
#if defined(__linux__)
    constexpr usize Alignment = 2 * 1024 * 1024;
    const long      pageSize  = sysconf(_SC_PAGESIZE);

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

std::string make_sentinel_base(std::string_view name) noexcept {
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

        ControlMsg controlMsg{};

        msg.msg_control    = controlMsg.buf;
        msg.msg_controllen = sizeof(controlMsg.buf);

        int flags = 0;
#if defined(MSG_CMSG_CLOEXEC)
        flags = MSG_CMSG_CLOEXEC;
#endif

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
//  - Exits when shutdownFd is hung up on
//  - Listens on serverFd
std::thread make_server_thread(UniqueFd fd, UniqueFd shutdownFd, UniqueFd serverFd) noexcept {
    enum FD : u8 {
        FD_SERVER,
        FD_SHUTDOWN,
    };

    constexpr usize FD_NB = 2;

    return std::thread([fd         = std::move(fd),          //
                        shutdownFd = std::move(shutdownFd),  //
                        serverFd   = std::move(serverFd)]() noexcept {
        struct pollfd fds[FD_NB];
        fds[FD_SERVER].fd     = serverFd.get();
        fds[FD_SERVER].events = POLLIN;

        fds[FD_SHUTDOWN].fd     = shutdownFd.get();
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
            if ((fds[FD_SHUTDOWN].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0)
                break;

            if ((fds[FD_SERVER].revents & POLLIN) != 0)
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
                int yes = 1;
                ::setsockopt(clientFd.get(), SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
                int flags = 0;
#if defined(MSG_NOSIGNAL)
                flags = MSG_NOSIGNAL;
#endif

                while (::sendmsg(clientFd.get(), &msg, flags) < 0 && errno == EINTR)
                {}
            }
        }
    });
}

}  // namespace DON
