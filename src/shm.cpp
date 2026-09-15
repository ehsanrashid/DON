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

namespace MemoryRegistry {

namespace {

// Protects access to all registry containers.
std::shared_mutex Mutex;

// Preserves true insertion order for deterministic iteration.
MemoryList List;

// Provides average O(1) fast lookup, count and removal.
// Maps each memory to its corresponding iterator in List.
MemoryIndexMap IndexMap;

// Provides average O(1) fast uniqueness and membership checks.
MemorySet Set;

usize nolock_size() noexcept { return List.size(); }
bool  nolock_empty() noexcept { return List.empty(); }

// Check memory registry membership.
bool nolock_contains(const MemorySet::iterator setItr) noexcept { return setItr != Set.end(); }
bool nolock_contains(const Memory memory) noexcept { return nolock_contains(Set.find(memory)); }

[[maybe_unused]] usize nolock_count(const Memory memory) noexcept { return IndexMap.count(memory); }

auto nolock_find(const Memory memory) noexcept { return IndexMap.find(memory); }

    #if !defined(NDEBUG)
// Verifies the consistency of all registry containers.
// The caller must hold 'Mutex' in shared or exclusive mode.
// Returns true if all registry invariants hold.
// The following invariants must hold:
//  - All three containers have the same size.
//  - Every memory in List exists in IndexMap and Set.
//  - Every memory in IndexMap exists in List and Set.
//  - Every memory in Set exists in List and IndexMap.
//  - Every IndexMap entry points to its corresponding node in List.
bool nolock_is_consistent() noexcept {
    assert(List.size() == IndexMap.size() && "List and IndexMap sizes differ");
    assert(List.size() == Set.size() && "List and Set sizes differ");

    for ([[maybe_unused]] auto listItr = List.begin(); listItr != List.end(); ++listItr)
    {
        [[maybe_unused]] const Memory memory = *listItr;
        assert(memory != nullptr && "List contains a null memory pointer");

        [[maybe_unused]] auto indexMapItr = nolock_find(memory);
        assert(indexMapItr != IndexMap.end() && "List memory is missing from IndexMap");
        assert(indexMapItr->second == listItr && "IndexMap points to the wrong List node");

        assert(Set.find(memory) != Set.end() && "List memory is missing from Set");
    }

    for ([[maybe_unused]] const auto& [memory, listItr] : IndexMap)
    {
        assert(memory != nullptr && "IndexMap contains a null memory pointer");
        assert(listItr != List.end() && "IndexMap contains an invalid List iterator");
        assert(*listItr == memory && "IndexMap iterator points to the wrong memory");

        assert(Set.find(memory) != Set.end() && "IndexMap memory is missing from Set");
    }

    for ([[maybe_unused]] const Memory memory : Set)
    {
        assert(memory != nullptr && "Set contains a null memory pointer");

        [[maybe_unused]] const auto indexMapItr = nolock_find(memory);
        assert(indexMapItr != IndexMap.end() && "Set memory is missing from IndexMap");

        const auto listItr = indexMapItr->second;
        assert(listItr != List.end() && "IndexMap contains an invalid List iterator");
        assert(*listItr == memory && "IndexMap iterator points to the wrong memory");
    }

    return true;
}
    #endif

// Registers a memory object in all registry containers.
// The caller must hold 'Mutex' exclusively.
// Set is checked first for membership to reject duplicate memory objects.
// The memory is then appended to List, IndexMap records the corresponding
// List iterator, and Set is finally updated to establish membership.
// Set and IndexMap provide average O(1) lookup and insertion.
bool nolock_register_memory(const Memory memory) noexcept {
    if (nolock_contains(memory))
        return false;

    //DEBUG_LOG("Registering memory: " << static_cast<const void*>(memory) << ' ' << memory->name());

    // Append to the ordered list and obtain its iterator.
    const auto listItr = List.emplace(List.end(), memory);
    assert(listItr != List.end());

    // Associate the memory with its corresponding list node.
    [[maybe_unused]] const auto [indexMapItr, inserted] = IndexMap.emplace(memory, listItr);
    // Set was checked first, so IndexMap must not contain the memory.
    assert(inserted);
    assert(indexMapItr->second == listItr);

    // Establish membership after List and IndexMap are successfully updated.
    [[maybe_unused]] const auto [setItr, registered] = Set.emplace(memory);
    // The initial membership check guarantees that this insertion succeeds.
    assert(registered);
    assert(setItr != Set.end());

    assert(nolock_is_consistent());

    return true;
}

// Unregisters a memory object from all registry containers.
// The caller must hold 'Mutex' exclusively.
// Set is checked first for membership to reject unregistered memory objects.
// The corresponding List node is then retrieved through IndexMap.
// The memory is then removed from Set, IndexMap, and List.
// Set and IndexMap provide average O(1) lookup and removal.
bool nolock_unregister_memory(const Memory memory) noexcept {
    const auto setItr = Set.find(memory);

    // Not registered.
    if (!nolock_contains(setItr))
        return false;

    const auto indexMapItr = nolock_find(memory);
    // Set guarantees that IndexMap contains the memory.
    assert(indexMapItr != IndexMap.end());

    // Retrieve the corresponding list node.
    const auto listItr = indexMapItr->second;
    // Internal consistency checks.
    assert(listItr != List.end());
    assert(*listItr == memory);

    // Remove the membership entry.
    Set.erase(setItr);

    // Remove the index entry.
    IndexMap.erase(indexMapItr);

    // Remove the list node.
    List.erase(listItr);

    //DEBUG_LOG("Unregistered memory: " << static_cast<const void*>(memory) << ' ' << memory->name());

    assert(nolock_is_consistent());

    return true;
}

}  // namespace

bool register_memory(const Memory memory) noexcept {
    if (memory == nullptr)
    {
        //DEBUG_LOG("Cannot register <NULL> memory.");
        return false;
    }

    // Acquire an exclusive lock because all registry containers are modified.
    std::lock_guard writeLock(Mutex);

    return nolock_register_memory(memory);
}

bool unregister_memory(const Memory memory) noexcept {
    if (memory == nullptr)
    {
        //DEBUG_LOG("Cannot unregister <NULL> memory.");
        return false;
    }

    // Acquire an exclusive lock because all registry containers are modified.
    std::lock_guard writeLock(Mutex);

    return nolock_unregister_memory(memory);
}

MemoryList detach_memories() noexcept {
    std::lock_guard writeLock(Mutex);

    assert(List.size() == IndexMap.size());
    assert(List.size() == Set.size());

    auto detachedList = std::move(List);
    IndexMap.clear();
    Set.clear();

    assert(List.empty());
    assert(IndexMap.empty());
    assert(Set.empty());

    return detachedList;
}

usize size() noexcept {
    std::shared_lock readLock(Mutex);

    assert(List.size() == IndexMap.size());
    assert(List.size() == Set.size());

    return nolock_size();
}

bool empty() noexcept {
    std::shared_lock readLock(Mutex);

    assert(List.size() == IndexMap.size());
    assert(List.size() == Set.size());

    return nolock_empty();
}

void print() noexcept {
    std::shared_lock readLock(Mutex);

    assert(List.size() == IndexMap.size());
    assert(List.size() == Set.size());

    std::cout << "Registered memories [" << List.size() << "]:\n";

    usize i = 0;
    for (const auto* const memory : List)
        std::cout << '[' << i++ << "] " << static_cast<const void*>(memory) << ' '
                  << (memory != nullptr ? memory->name() : "<NULL>") << '\n';

    std::cout << std::endl;
}

}  // namespace MemoryRegistry

namespace MemoryCleanup {

void cleanup() noexcept {
    auto memoryList = MemoryRegistry::detach_memories();

    //DEBUG_LOG("Memory cleanup started (" << memoryList.size() << " object(s)).");
    for (auto* const memory : memoryList)
        if (memory != nullptr)
            memory->reset();
}

}  // namespace MemoryCleanup

namespace MemoryCleanupHook {

namespace {

CallOnce HookCallOnce;

}  // namespace

void ensure_initialized() noexcept {
    while (!HookCallOnce.once_done())
    {
        HookCallOnce([]() noexcept {
            //DEBUG_LOG("Initializing MemoryCleanupHook.");

            std::atexit(MemoryCleanup::cleanup);
        });
    }
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
    UniqueFd fd(::open(path.data(), O_CREAT | O_RDWR | O_CLOEXEC, DEFFILEMODE));

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

std::thread make_server_thread(UniqueFd fd, UniqueFd shutdownFd, UniqueFd serverFd) noexcept {
    return std::thread([fd         = std::move(fd),          //
                        shutdownFd = std::move(shutdownFd),  //
                        serverFd   = std::move(serverFd)]() noexcept {
        struct pollfd fds[PI_NB];
        fds[+PI::SERVER].fd     = serverFd.get();
        fds[+PI::SERVER].events = POLLIN;

        fds[+PI::SHUTDOWN].fd     = shutdownFd.get();
        fds[+PI::SHUTDOWN].events = POLLIN;

        while (true)
        {
            int ret = ::poll(fds, PI_NB, -1);
            if (ret < 0)
            {
                if (errno == EINTR)
                    continue;

                break;
            }

            // Shutdown requested by main thread
            if ((fds[+PI::SHUTDOWN].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0)
                break;

            if ((fds[+PI::SERVER].revents & POLLIN) != 0)
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
                while (::sendmsg(clientFd.get(), &msg, flags) < 0)
                {
                    if (errno != EINTR)
                        break;
                }
            }
        }
    });
}

#endif

}  // namespace DON
