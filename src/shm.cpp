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

    #include <algorithm>  // min()/max()

#elif defined(__ANDROID__)
    #include <unistd.h>
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

#elif defined(__linux__) || defined(__ANDROID__)
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

TempRoot::TempRoot(std::string path) noexcept :
    path_(std::move(path)) {}

const std::optional<TempRoot>& TempRoot::temp_root() noexcept {
    static const auto tempRoot = []() -> std::optional<TempRoot> {
        const uid_t uid = ::getuid();

        const auto tempPath = std::string{"/tmp/DON-"} + std::to_string(uid);

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
        // Align the mapping to 2 MiB for huge-page-friendly virtual addressing.
        // File-backed huge pages require matching virtual-address and file-offset alignment.
        const usize mappingSize  = ceil_to_multiple(size, usize(pageSize));
        const usize reservedSize = mappingSize + Alignment;
        void*       reservedAddress =
          ::mmap(nullptr, reservedSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (reservedAddress != MAP_FAILED)
        {
            char* const reservationBase = static_cast<char*>(reservedAddress);
            char* const mappingAddress  = align_ptr_up<Alignment>(reservationBase);
            void*       mappedAddress = ::mmap(mappingAddress, mappingSize, PROT_READ | PROT_WRITE,
                                               MAP_SHARED | MAP_FIXED, fd, 0);

            if (mappedAddress != MAP_FAILED)
            {
                const usize prefixSize = usize(mappingAddress - reservationBase);
                const usize suffixSize = reservedSize - prefixSize - mappingSize;

                if (prefixSize != 0)
                    ::munmap(reservedAddress, prefixSize);

                if (suffixSize != 0)
                    ::munmap(mappingAddress + mappingSize, suffixSize);

                return mappedAddress;
            }

            ::munmap(reservedAddress, reservedSize);
        }
    }
    #endif

    return ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

std::string make_sentinel_base(const std::string_view name) noexcept {
    // Using std::to_string here causes non-deterministic PGO builds.
    // snprintf, being part of libc, is insensitive to the formatted values.
    return std::string{"DONSHM_"} + u64_to_hex(hash_string(name));
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

UniqueFd try_create_memfd(const std::string& sockPath) noexcept {
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
    while (ret == -1 && errno == EINTR);

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
        while (bytesRecv == -1 && errno == EINTR);

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
    else if (errno == ENOENT || errno == ECONNREFUSED)
    {
        // Failed to connect, clean up dead peer
        ::unlink(sockPath.c_str());
    }

    return {};
}

namespace {
// Poll Index
enum class PI : u8 {
    SERVER,
    SHUTDOWN
};

constexpr usize PI_NB = 2;

constexpr u8 operator+(const PI pi) noexcept { return u8(pi); }

}  // namespace

NativeThread make_server_thread(UniqueFd fd, UniqueFd shutdownFd, UniqueFd serverFd) noexcept {
    return create_native_thread([fd         = std::move(fd),          //
                                 shutdownFd = std::move(shutdownFd),  //
                                 serverFd   = std::move(serverFd)]() noexcept -> void {
        struct pollfd fds[PI_NB];
        fds[+PI::SERVER].fd     = serverFd.get();
        fds[+PI::SERVER].events = POLLIN;

        fds[+PI::SHUTDOWN].fd     = shutdownFd.get();
        fds[+PI::SHUTDOWN].events = POLLIN;

        while (true)
        {
            int ret = ::poll(fds, PI_NB, -1);
            if (ret == -1)
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
                while (::sendmsg(clientFd.get(), &msg, flags) == -1)
                {
                    if (errno == EINTR)
                        continue;

                    break;
                }
            }
        }
    });
}

#endif

}  // namespace DON
