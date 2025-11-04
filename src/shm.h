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

#include "shm_linux.h"

#if defined(__ANDROID__)
    #include <limits.h>
    #define MAX_SEM_NAME_LEN NAME_MAX
#endif

#if defined(_WIN32)

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
    #include <cstring>
    #include <fcntl.h>
    #include <pthread.h>
    #include <semaphore.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif


#if defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <sys/syslimits.h>

#elif defined(__sun)
    #include <stdlib.h>

#elif defined(__FreeBSD__)
    #include <sys/sysctl.h>
    #include <sys/types.h>
    #include <unistd.h>

#elif defined(__NetBSD__) || defined(__DragonFly__) || defined(__linux__)
    #include <limits.h>
    #include <unistd.h>
#endif

#include "memory.h"
#include "misc.h"

namespace DON {

// argv[0] CANNOT be used because we need to identify the executable.
// argv[0] contains the command used to invoke it, which does not involve the full path.
// Just using a path is not fully resilient either, as the executable could
// have changed if it wasn't locked by the OS. Ideally we would hash the executable
// but it's not really that important at this point.
// If the path is longer than 4095 bytes the hash will be computed from an unspecified
// amount of bytes of the path; in particular it can a hash of an empty string.

inline std::string executable_path_hash() noexcept {
    char        executablePath[4096] = {0};
    std::size_t pathLength           = 0;

#if defined(_WIN32)
    pathLength = GetModuleFileName(NULL, executablePath, sizeof(executablePath));

#elif defined(__APPLE__)
    std::uint32_t size = sizeof(executablePath);
    if (_NSGetExecutablePath(executablePath, &size) == 0)
    {
        pathLength = std::strlen(executablePath);
    }

#elif defined(__sun)  // Solaris
    const char* path = getexecname();
    if (path)
    {
        std::strncpy(executablePath, path, sizeof(executablePath) - 1);
        pathLength = std::strlen(executablePath);
    }

#elif defined(__FreeBSD__)
    size_t size   = sizeof(executablePath);
    int    mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    if (sysctl(mib, 4, executablePath, &size, NULL, 0) == 0)
    {
        pathLength = std::strlen(executablePath);
    }

#elif defined(__NetBSD__) || defined(__DragonFly__)
    ssize_t len = readlink("/proc/curproc/exe", executablePath, sizeof(executablePath) - 1);
    if (len >= 0)
    {
        executablePath[len] = '\0';
        pathLength          = len;
    }

#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", executablePath, sizeof(executablePath) - 1);
    if (len >= 0)
    {
        executablePath[len] = '\0';
        pathLength          = len;
    }

#endif

    // In case of any error the path will be empty.
    return std::string(executablePath, pathLength);
}

enum class SystemWideSharedConstantAllocationStatus {
    NoAllocation,
    LocalMemory,
    SharedMemory
};

#if defined(_WIN32)

// Get the error message string, if any.
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

// Utilizes shared memory to store the value. It is deduplicated system-wide (for the single user).
template<typename T>
class SharedMemoryBackend final {
   public:
    enum class Status {
        Success,
        LargePageAllocationError,
        FileMappingError,
        MapViewError,
        MutexCreateError,
        MutexWaitError,
        MutexReleaseError,
        NotInitialized
    };

    static constexpr DWORD IS_INITIALIZED_VALUE = 1;

    SharedMemoryBackend() noexcept :
        status(Status::NotInitialized) {};

    SharedMemoryBackend(const std::string& shmName, const T& value) noexcept :
        status(Status::NotInitialized) {

        initialize(shmName, value);
    }

    bool is_valid() const noexcept { return status == Status::Success; }

    std::optional<std::string> get_error_message() const noexcept {
        switch (status)
        {
        case Status::Success :
            return std::nullopt;
        case Status::LargePageAllocationError :
            return "Failed to allocate large page memory";
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
        case Status::NotInitialized :
            return "Not initialized";
        default :
            return "Unknown error";
        }
    }

    void* get() const noexcept { return is_valid() ? pMapAddr : nullptr; }

    ~SharedMemoryBackend() noexcept { cleanup(); }

    SharedMemoryBackend(const SharedMemoryBackend&) noexcept            = delete;
    SharedMemoryBackend& operator=(const SharedMemoryBackend&) noexcept = delete;

    SharedMemoryBackend(SharedMemoryBackend&& shmBackend) noexcept :
        hMapFile(shmBackend.hMapFile),
        pMapAddr(shmBackend.pMapAddr),
        status(shmBackend.status),
        lastErrorStr(std::move(shmBackend.lastErrorStr)) {

        shmBackend.pMapAddr = nullptr;
        shmBackend.hMapFile = nullptr;
        shmBackend.status   = Status::NotInitialized;
    }
    SharedMemoryBackend& operator=(SharedMemoryBackend&& shmBackend) noexcept {
        if (this == &shmBackend)
            return *this;

        cleanup();
        hMapFile     = shmBackend.hMapFile;
        pMapAddr     = shmBackend.pMapAddr;
        status       = shmBackend.status;
        lastErrorStr = std::move(shmBackend.lastErrorStr);

        shmBackend.pMapAddr = nullptr;
        shmBackend.hMapFile = nullptr;
        shmBackend.status   = Status::NotInitialized;
        return *this;
    }

    SystemWideSharedConstantAllocationStatus get_status() const noexcept {
        return status == Status::Success ? SystemWideSharedConstantAllocationStatus::SharedMemory
                                         : SystemWideSharedConstantAllocationStatus::NoAllocation;
    }

   private:
    void initialize(const std::string& shmName, const T& value) noexcept {
        std::size_t totalSize = sizeof(T) + sizeof(IS_INITIALIZED_VALUE);

        // Try allocating with large pages first.
        hMapFile = try_with_windows_large_page_privileges(
          [&](std::size_t largePageSize) {
              std::size_t roundedTotalSize = round_up_pow2(totalSize, largePageSize);

    #if defined(_WIN64)
              DWORD hiTotalSize = roundedTotalSize >> 32;
              DWORD loTotalSize = roundedTotalSize & 0xFFFFFFFFU;
    #else
              DWORD hiTotalSize = 0;
              DWORD loTotalSize = roundedTotalSize;
    #endif

              return CreateFileMapping(INVALID_HANDLE_VALUE, NULL,
                                       PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,  //
                                       hiTotalSize, loTotalSize, shmName.c_str());
          },
          []() { return (void*) nullptr; });

        // Fallback to normal allocation if no large pages available.
        if (hMapFile == nullptr)
            hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,  //
                                         totalSize, shmName.c_str());

        if (hMapFile == nullptr)
        {
            status       = Status::FileMappingError;
            lastErrorStr = error_to_string(GetLastError());
            return;
        }

        pMapAddr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, totalSize);
        if (pMapAddr == nullptr)
        {
            status       = Status::MapViewError;
            lastErrorStr = error_to_string(GetLastError());
            cleanup_partial();
            return;
        }

        // Use named mutex to ensure only one initializer
        std::string mutexName = shmName + "$mutex";
        HANDLE      hMutex    = CreateMutex(NULL, FALSE, mutexName.c_str());
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

        // Crucially, we place the object first to ensure alignment.
        volatile DWORD* isInitialized =
          std::launder(reinterpret_cast<DWORD*>(reinterpret_cast<char*>(pMapAddr) + sizeof(T)));
        T* object = std::launder(reinterpret_cast<T*>(pMapAddr));

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
        if (pMapAddr != nullptr)
        {
            UnmapViewOfFile(pMapAddr);
            pMapAddr = nullptr;
        }
        if (hMapFile != nullptr)
        {
            CloseHandle(hMapFile);
            hMapFile = nullptr;
        }
    }

    void cleanup() noexcept {
        if (pMapAddr != nullptr)
        {
            UnmapViewOfFile(pMapAddr);
            pMapAddr = nullptr;
        }
        if (hMapFile != nullptr)
        {
            CloseHandle(hMapFile);
            hMapFile = nullptr;
        }
    }

    HANDLE      hMapFile = nullptr;
    void*       pMapAddr = nullptr;
    Status      status   = Status::NotInitialized;
    std::string lastErrorStr;
};

#elif !defined(__ANDROID__)

template<typename T>
class SharedMemoryBackend {
   public:
    SharedMemoryBackend() = default;

    SharedMemoryBackend(const std::string& shmName, const T& value) :
        shm(create_shared<T>(shmName, value)) {}

    void* get() const {
        const T* ptr = &shm->get();
        return reinterpret_cast<void*>(const_cast<T*>(ptr));
    }

    bool is_valid() const { return shm && shm->is_open() && shm->is_initialized(); }

    SystemWideSharedConstantAllocationStatus get_status() const {
        return is_valid() ? SystemWideSharedConstantAllocationStatus::SharedMemory
                          : SystemWideSharedConstantAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const {
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

#else

// For systems that don't have shared memory, or support is troublesome.
// The way fallback is done is that we need a dummy backend.

template<typename T>
class SharedMemoryBackend {
   public:
    SharedMemoryBackend() = default;

    SharedMemoryBackend(const std::string& shmName, const T& value) {}

    void* get() const { return nullptr; }

    bool is_valid() const { return false; }

    SystemWideSharedConstantAllocationStatus get_status() const {
        return SystemWideSharedConstantAllocationStatus::NoAllocation;
    }

    std::optional<std::string> get_error_message() const { return "Dummy SharedMemoryBackend"; }
};

#endif

template<typename T>
struct SharedMemoryBackendFallback final {
    SharedMemoryBackendFallback() noexcept = default;

    SharedMemoryBackendFallback(const std::string&, const T& value) noexcept :
        fallbackObj(make_unique_aligned_large_pages<T>(value)) {}

    void* get() const { return fallbackObj.get(); }

    SharedMemoryBackendFallback(const SharedMemoryBackendFallback&) noexcept            = delete;
    SharedMemoryBackendFallback& operator=(const SharedMemoryBackendFallback&) noexcept = delete;

    SharedMemoryBackendFallback(SharedMemoryBackendFallback&& shmFallback) noexcept :
        fallbackObj(std::move(shmFallback.fallbackObj)) {}
    SharedMemoryBackendFallback& operator=(SharedMemoryBackendFallback&& shmFallback) noexcept {
        fallbackObj = std::move(shmFallback.fallbackObj);
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
        std::size_t contentHash    = std::hash<T>{}(value);
        std::size_t executableHash = std::hash<std::string>{}(executable_path_hash());

        std::string shmName = "Local\\don_" + std::to_string(contentHash)  //
                            + "$" + std::to_string(executableHash)         //
                            + "$" + std::to_string(discriminator);
#if !defined(_WIN32)
        // POSIX shared memory names must start with a slash
        shmName = "/don_" + create_hash_string(shmName);

        // hash name and make sure it is not longer than MAX_SEM_NAME_LEN
        if (shmName.size() > MAX_SEM_NAME_LEN)
            shmName = shmName.substr(0, MAX_SEM_NAME_LEN - 1);
#endif

        SharedMemoryBackend<T> shmBackend(shmName, value);

        if (shmBackend.is_valid())
            backend = std::move(shmBackend);
        else
            backend = SharedMemoryBackendFallback<T>(shmName, value);
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

    std::variant<std::monostate, SharedMemoryBackend<T>, SharedMemoryBackendFallback<T>> backend;
};

}  // namespace DON

#endif  // #ifndef SHM_H_INCLUDED
