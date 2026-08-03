
#include "shm.h"

namespace DON {

std::string executable_path() noexcept {
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
    ssize_t size = readlink("/proc/curproc/file", executablePath.data(), executablePath.size() - 1);

    if (size >= 0)
    {
        executableSize                 = std::min<usize>(size, executablePath.size() - 1);
        executablePath[executableSize] = '\0';
    }
#elif defined(__NetBSD__) || defined(__DragonFly__)
    ssize_t size = readlink("/proc/curproc/exe", executablePath.data(), executablePath.size() - 1);

    if (size >= 0)
    {
        executableSize                 = std::min<usize>(size, executablePath.size() - 1);
        executablePath[executableSize] = '\0';
    }
#elif defined(__linux__)
    ssize_t size = readlink("/proc/self/exe", executablePath.data(), executablePath.size() - 1);

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

}  // namespace DON
