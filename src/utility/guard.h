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

#ifndef UTILITY_GUARD_H_INCLUDED
#define UTILITY_GUARD_H_INCLUDED

#include "../utility/functions.h"
#include "../utility/types.h"
#if defined(_WIN32)
    #include "../utility/platform_win.h"
#endif

namespace DON {

// Erases a newly inserted map entry unless the operation commits successfully.
// The iterator must remain valid until commit() or guard destruction.
template<typename Map>
class MapGuard final {
   public:
    using map_iterator = typename Map::iterator;

    MapGuard(Map& mapRef, map_iterator mapIterator) noexcept :
        map(mapRef),
        mapItr(mapIterator) {}

    MapGuard() noexcept                           = delete;
    MapGuard(const MapGuard&) noexcept            = delete;
    MapGuard& operator=(const MapGuard&) noexcept = delete;
    MapGuard(MapGuard&&) noexcept                 = delete;
    MapGuard& operator=(MapGuard&&) noexcept      = delete;

    ~MapGuard() noexcept {
        if (!committed)
            map.erase(mapItr);
    }

    void commit() noexcept { committed = true; }

   private:
    Map&         map;
    map_iterator mapItr;
    bool         committed = false;
};

#if defined(_WIN32)
struct HandleGuard final {
   public:
    explicit HandleGuard(HANDLE& handleRef) noexcept;

    HandleGuard() noexcept = delete;

    HandleGuard(const HandleGuard&) noexcept            = delete;
    HandleGuard& operator=(const HandleGuard&) noexcept = delete;

    HandleGuard(HandleGuard&&) noexcept            = delete;
    HandleGuard& operator=(HandleGuard&&) noexcept = delete;

    ~HandleGuard() noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] HANDLE get() const noexcept { return handle; }

    void reset(HANDLE newHandle = HANDLE_INVALID) noexcept;

    void dismiss() noexcept;

   private:
    HANDLE& handle;
};

struct MMapGuard final {
   public:
    explicit MMapGuard(void*& ptrRef) noexcept;

    MMapGuard() noexcept = delete;

    MMapGuard(const MMapGuard&) noexcept            = delete;
    MMapGuard& operator=(const MMapGuard&) noexcept = delete;

    MMapGuard(MMapGuard&&) noexcept            = delete;
    MMapGuard& operator=(MMapGuard&&) noexcept = delete;

    ~MMapGuard() noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] void* get() const noexcept;

    void reset(void* newPtr = MMAP_PTR_INVALID) noexcept;

    void dismiss() noexcept;

   private:
    void*& mappedPtr;
};

    #if defined(_WIN64)
struct Advapi final {
   public:
    // clang-format off
    // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocesstoken
    using OpenProcessToken_ = BOOL(WINAPI*)(
      HANDLE  ProcessHandle,    // [in]  Handle to process
      DWORD   DesiredAccess,    // [in]  Access rights for token
      PHANDLE TokenHandle       // [out] Pointer to token handle
    );
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lookupprivilegevaluea
    using LookupPrivilegeValue_ = BOOL(WINAPI*)(
      LPCSTR lpSystemName,      // [in]  System name (NULL for local)
      LPCSTR lpName,            // [in]  Privilege name (e.g., SE_DEBUG_NAME)
      PLUID  lpLuid             // [out] Receives LUID of privilege
    );
    // https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges
    using AdjustTokenPrivileges_ = BOOL(WINAPI*)(
      HANDLE            TokenHandle,          // [in]       Access token handle
      BOOL              DisableAllPrivileges, // [in]       Disable all privileges flag
      PTOKEN_PRIVILEGES NewState,             // [in, opt]  New privilege state
      DWORD             BufferLength,         // [in]       Size of PreviousState buffer
      PTOKEN_PRIVILEGES PreviousState,        // [out, opt] Previous privilege state
      PDWORD            ReturnLength          // [out, opt] Required buffer size
    );
    // clang-format on

    static constexpr LPCSTR ModuleName = TEXT("advapi32.dll");

    ~Advapi() noexcept;
    // The needed Windows API for processor groups could be missed from old Windows versions,
    // so instead of calling them directly (forcing the linker to resolve the calls at compile time),
    // try to load them at runtime.
    bool load() noexcept;

    void free() noexcept;

    OpenProcessToken_      openProcessToken      = nullptr;
    LookupPrivilegeValue_  lookupPrivilegeValue  = nullptr;
    AdjustTokenPrivileges_ adjustTokenPrivileges = nullptr;

   private:
    HMODULE hModule = nullptr;
    bool    loaded  = false;
};
    #endif

template<typename SuccessFunc, typename FailureFunc>
auto try_with_windows_lock_memory_privilege([[maybe_unused]] SuccessFunc&& successFunc,
                                            FailureFunc&&                  failureFunc) noexcept {
    #if defined(_WIN64)
    const SIZE_T largePageSize = ::GetLargePageMinimum();

    if (largePageSize == 0)
        return failureFunc();

    assert(is_power_of_2(largePageSize));

    Advapi advapi;

    if (!advapi.load())
        return failureFunc();

    HANDLE processHandle = HANDLE_INVALID;

    HandleGuard processHandleGuard{processHandle};

    // Need SeLockMemoryPrivilege, so try to enable it for the process
    if (!advapi.openProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                 &processHandle))
        return failureFunc();

    TOKEN_PRIVILEGES newTp{};
    newTp.PrivilegeCount           = 1;
    newTp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Get the luid
    if (!advapi.lookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &newTp.Privileges[0].Luid))
        return failureFunc();

    TOKEN_PRIVILEGES oldTp{};
    DWORD            oldTpLen = 0;

    // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
    // Still need to query GetLastError() to ensure that the privileges were actually obtained.
    ::SetLastError(ERROR_SUCCESS);

    if (!advapi.adjustTokenPrivileges(processHandle, FALSE, &newTp, sizeof(oldTp), &oldTp,
                                      &oldTpLen)
        || ::GetLastError() != ERROR_SUCCESS)
        return failureFunc();

    // Call the provided function with the privilege enabled
    auto&& ret = successFunc(largePageSize);

    // Privilege no longer needed, restore the privileges
    advapi.adjustTokenPrivileges(processHandle, FALSE, &oldTp, 0, nullptr, nullptr);

    return std::forward<decltype(ret)>(ret);
    #else
    return failureFunc();
    #endif
}
#else
struct FdGuard final {
   public:
    explicit FdGuard(int& fdRef) noexcept;

    FdGuard() noexcept = delete;

    FdGuard(const FdGuard&) noexcept            = delete;
    FdGuard& operator=(const FdGuard&) noexcept = delete;

    FdGuard(FdGuard&&) noexcept            = delete;
    FdGuard& operator=(FdGuard&&) noexcept = delete;

    ~FdGuard() noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] int get() const noexcept;

    void reset(int newFd = FD_INVALID) noexcept;

    void dismiss() noexcept;

   private:
    int& fd;
};

struct MMapGuard final {
   public:
    MMapGuard(void*& ptrRef, usize& sizeRef) noexcept;

    MMapGuard() noexcept = delete;

    MMapGuard(const MMapGuard&) noexcept            = delete;
    MMapGuard& operator=(const MMapGuard&) noexcept = delete;

    MMapGuard(MMapGuard&&) noexcept            = delete;
    MMapGuard& operator=(MMapGuard&&) noexcept = delete;

    ~MMapGuard() noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] void* get_ptr() const noexcept;

    [[nodiscard]] usize get_size() const noexcept;

    void reset(void* newPtr = MMAP_PTR_INVALID, usize newSize = MMAP_SIZE_INVALID) noexcept;

    void dismiss() noexcept;

   private:
    void*& mappedPtr;
    usize& mappedSize;
};

struct UniqueFd final {
   public:
    explicit UniqueFd(int fdi) noexcept;

    UniqueFd() noexcept = default;

    UniqueFd(const UniqueFd&)            = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& uniqueFd) noexcept;
    UniqueFd& operator=(UniqueFd&& uniqueFd) noexcept;

    ~UniqueFd() noexcept;

    [[nodiscard]] int get() const noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;

    [[nodiscard]] int release() noexcept;

    void reset(int newFd = FD_INVALID) noexcept;

   private:
    int fd = FD_INVALID;
};
#endif

}  // namespace DON

#endif  // UTILITY_GUARD_H_INCLUDED
