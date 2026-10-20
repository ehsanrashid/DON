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

#include "../utility/guard.h"

#include "../utility/functions.h"
#include "../utility/types.h"

#if defined(_WIN32)
    #include "../utility/platform_win.h"
#else
    #include <unistd.h>    // close()
    #include <sys/mman.h>  // munmap()
#endif

namespace DON {

#if defined(_WIN32)
HandleGuard::HandleGuard(HANDLE& handleRef) noexcept :
    handle(handleRef) {}

HandleGuard::~HandleGuard() noexcept { reset(); }

bool HandleGuard::is_valid() const noexcept { return is_valid_handle(handle); }

void HandleGuard::reset(const HANDLE newHandle) noexcept {
    if (handle != newHandle)
    {
        if (is_valid())
            CloseHandle(handle);

        handle = newHandle;
    }
}

void HandleGuard::dismiss() noexcept { handle = HANDLE_INVALID; }

MMapGuard::MMapGuard(void*& ptrRef) noexcept :
    mappedPtr(ptrRef) {}

MMapGuard::~MMapGuard() noexcept { reset(); }

bool MMapGuard::is_valid() const noexcept { return mappedPtr != MMAP_PTR_INVALID; }

void* MMapGuard::get() const noexcept { return mappedPtr; }

void MMapGuard::reset(void* newPtr) noexcept {
    if (mappedPtr != newPtr)
    {
        if (is_valid())
            UnmapViewOfFile(mappedPtr);

        mappedPtr = newPtr;
    }
}

void MMapGuard::dismiss() noexcept { mappedPtr = MMAP_PTR_INVALID; }

    #if defined(_WIN64)
Advapi::~Advapi() noexcept { free(); }

bool Advapi::load() noexcept {

    hModule = GetModuleHandle(ModuleName);

    if (hModule == nullptr)
    {
        hModule = LoadLibraryEx(ModuleName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        // Optional last resort
        if (hModule == nullptr)
            hModule = LoadLibrary(ModuleName);

        if (hModule == nullptr)
            return false;

        loaded = true;
    }

    openProcessToken = OpenProcessToken_(  //
      (void (*)())::GetProcAddress(hModule, "OpenProcessToken"));

    lookupPrivilegeValue = LookupPrivilegeValue_(  //
      (void (*)())::GetProcAddress(hModule, "LookupPrivilegeValueA"));

    adjustTokenPrivileges = AdjustTokenPrivileges_(  //
      (void (*)())::GetProcAddress(hModule, "AdjustTokenPrivileges"));

    if (openProcessToken == nullptr         //
        || lookupPrivilegeValue == nullptr  //
        || adjustTokenPrivileges == nullptr)
    {
        free();

        return false;
    }

    return true;
}

void Advapi::free() noexcept {
    if (loaded)
    {
        assert(hModule != nullptr);

        ::FreeLibrary(hModule);
    }

    hModule = nullptr;
    loaded  = false;
}

    #endif

#else

FdGuard::FdGuard(int& fdRef) noexcept :
    fd(fdRef) {}

FdGuard::~FdGuard() noexcept { reset(); }

bool FdGuard::is_valid() const noexcept { return is_valid_fd(fd); }

int FdGuard::get() const noexcept { return fd; }

void FdGuard::reset(const int newFd) noexcept {
    if (fd != newFd)
    {
        if (is_valid())
            ::close(fd);

        fd = newFd;
    }
}

void FdGuard::dismiss() noexcept { fd = FD_INVALID; }

MMapGuard::MMapGuard(void*& ptrRef, usize& sizeRef) noexcept :
    mappedPtr(ptrRef),
    mappedSize(sizeRef) {}

MMapGuard::~MMapGuard() noexcept { reset(); }

bool MMapGuard::is_valid() const noexcept { return mappedPtr != MMAP_PTR_INVALID; }

void* MMapGuard::get_ptr() const noexcept { return mappedPtr; }

usize MMapGuard::get_size() const noexcept { return mappedSize; }

void MMapGuard::reset(void* const newPtr, const usize newSize) noexcept {
    if (mappedPtr != newPtr)
    {
        if (is_valid())
            ::munmap(mappedPtr, mappedSize);

        mappedPtr  = newPtr;
        mappedSize = newSize;
    }
}

void MMapGuard::dismiss() noexcept {
    mappedPtr  = MMAP_PTR_INVALID;
    mappedSize = MMAP_SIZE_INVALID;
}

UniqueFd::UniqueFd(const int fd_) noexcept :
    fd{fd_} {}

UniqueFd::UniqueFd(UniqueFd&& uniqueFd) noexcept :
    UniqueFd{uniqueFd.release()} {}

UniqueFd& UniqueFd::operator=(UniqueFd&& uniqueFd) noexcept {
    if (this == &uniqueFd)
        return *this;

    reset(uniqueFd.release());

    return *this;
}

UniqueFd::~UniqueFd() noexcept { reset(); }

int UniqueFd::get() const noexcept { return fd; }

bool UniqueFd::is_valid() const noexcept { return is_valid_fd(fd); }

UniqueFd::operator bool() const noexcept { return is_valid(); }

int UniqueFd::release() noexcept { return std::exchange(fd, FD_INVALID); }

void UniqueFd::reset(const int newFd) noexcept {
    if (fd != newFd)
    {
        if (is_valid())
            ::close(fd);

        fd = newFd;
    }
}

#endif

}  // namespace DON
