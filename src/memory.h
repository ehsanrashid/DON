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

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

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

    #include <psapi.h>
#endif

#define ASSERT_ALIGNED(ptr, alignment) \
    assert(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0)

namespace DON {

void* alloc_aligned_std(std::size_t allocSize, std::size_t alignment) noexcept;

void free_aligned_std(void* mem) noexcept;

// memory aligned by page size, min alignment: 4096 bytes
void* alloc_aligned_large_page(std::size_t allocSize) noexcept;

bool free_aligned_large_page(void* mem) noexcept;

bool has_large_page() noexcept;

// Round up to multiples of alignment
template<typename T>
[[nodiscard]] constexpr T round_up_pow2(T size, T alignment) noexcept {
    static_assert(std::is_unsigned_v<T>, "round_up_pow2 requires an unsigned type");
    // Alignment must be non-zero power of 2
    assert(alignment != 0 && (alignment & (alignment - 1)) == 0);

    T mask = alignment - 1;
    // Avoid overflow: if size + mask overflows, it wraps around
    return (size + mask) & ~mask;
}

// Frees memory which was placed there with placement new.
// Works for both single objects and arrays of unknown bound.
template<typename T, typename FreeFunc>
void memory_deleter(T* mem, FreeFunc&& freeFunc) noexcept {
    if (mem == nullptr)
        return;

    // Explicitly needed to call the destructor
    if constexpr (!std::is_trivially_destructible_v<T>)
        std::destroy_at(mem);

    freeFunc(mem);
}

// Frees memory which was placed there with placement new.
// Works for both single objects and arrays of unknown bound.
template<typename T, typename FreeFunc>
void memory_array_deleter(T* mem, FreeFunc&& freeFunc) noexcept {
    if (mem == nullptr)
        return;

    constexpr std::size_t ArrayOffset = std::max(sizeof(std::size_t), alignof(T));
    // Move back on the pointer to where the size is allocated.
    auto* rawMem = reinterpret_cast<char*>(mem) - ArrayOffset;

    if constexpr (!std::is_trivially_destructible_v<T>)
    {
        std::size_t size = *reinterpret_cast<std::size_t*>(rawMem);

        //// Explicitly call the destructor for each element in reverse order
        //for (std::size_t i = size; i-- > 0;)
        //    std::destroy_at(&mem[i]);  // mem[i].~T();

        //// Forward order
        //std::destroy(mem, mem + size);
        // Reverse order
        std::destroy(std::make_reverse_iterator(mem + size), std::make_reverse_iterator(mem));
    }

    freeFunc(rawMem);
}

// Allocates memory for a single object and places it there with placement new.
template<typename T, typename AllocFunc, typename... Args>
inline std::enable_if_t<!std::is_array_v<T>, T*> memory_allocator(AllocFunc&& allocFunc,
                                                                  Args&&... args) noexcept {
    void* rawMem = allocFunc(sizeof(T));
    ASSERT_ALIGNED(rawMem, alignof(T));
    return new (rawMem) T(std::forward<Args>(args)...);
}

// Allocates memory for an array of unknown bound and places it there with placement new.
template<typename T, typename AllocFunc>
inline std::enable_if_t<std::is_array_v<T>, std::remove_extent_t<T>*>
memory_allocator(AllocFunc&& allocFunc, std::size_t size) noexcept {
    using ElementType = std::remove_extent_t<T>;

    constexpr std::size_t ArrayOffset = std::max(sizeof(std::size_t), alignof(ElementType));

    // Save the array size in the memory location
    auto* rawMem = reinterpret_cast<char*>(allocFunc(ArrayOffset + size * sizeof(ElementType)));
    ASSERT_ALIGNED(rawMem, alignof(T));

    new (rawMem) std::size_t(size);

    for (std::size_t i = 0; i < size; ++i)
        new (rawMem + ArrayOffset + i * sizeof(ElementType)) ElementType();

    // Need to return the pointer at the start of the array so that the indexing in unique_ptr<T[]> works
    return reinterpret_cast<ElementType*>(rawMem + ArrayOffset);
}

//
// Aligned std unique ptr
//

template<typename T>
struct AlignedStdDeleter final {
    void operator()(T* mem) const noexcept { return memory_deleter<T>(mem, free_aligned_std); }
};

template<typename T>
struct AlignedStdArrayDeleter final {
    void operator()(T* mem) const noexcept {
        return memory_array_deleter<T>(mem, free_aligned_std);
    }
};

template<typename T>
using AlignedStdPtr =
  std::conditional_t<std::is_array_v<T>,
                     std::unique_ptr<T, AlignedStdArrayDeleter<std::remove_extent_t<T>>>,
                     std::unique_ptr<T, AlignedStdDeleter<T>>>;

// make_unique_aligned_std() for single objects
template<typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, AlignedStdPtr<T>>
make_unique_aligned_std(Args&&... args) noexcept {
    const auto allocFunc = [](std::size_t allocSize) {
        return alloc_aligned_std(allocSize, alignof(T));
    };

    auto* obj = memory_allocator<T>(allocFunc, std::forward<Args>(args)...);

    return AlignedStdPtr<T>(obj);
}

// make_unique_aligned_std() for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, AlignedStdPtr<T>>
make_unique_aligned_std(std::size_t size) noexcept {
    using ElementType = std::remove_extent_t<T>;

    const auto allocFunc = [](std::size_t allocSize) {
        return alloc_aligned_std(allocSize, alignof(ElementType));
    };

    auto* mem = memory_allocator<T>(allocFunc, size);

    return AlignedStdPtr<T>(mem);
}

//
// Aligned large page unique ptr
//

template<typename T>
struct LargePageDeleter final {
    void operator()(T* mem) const noexcept {
        return memory_deleter<T>(mem, free_aligned_large_page);
    }
};

template<typename T>
struct LargePageArrayDeleter final {
    void operator()(T* mem) const noexcept {
        return memory_array_deleter<T>(mem, free_aligned_large_page);
    }
};

template<typename T>
using LargePagePtr =
  std::conditional_t<std::is_array_v<T>,
                     std::unique_ptr<T, LargePageArrayDeleter<std::remove_extent_t<T>>>,
                     std::unique_ptr<T, LargePageDeleter<T>>>;

// make_unique_aligned_large_page() for single objects
template<typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, LargePagePtr<T>>
make_unique_aligned_large_page(Args&&... args) noexcept {
    static_assert(alignof(T) <= 4096,
                  "alloc_aligned_large_page() may fail for such a big alignment requirement of T");

    auto* obj = memory_allocator<T>(alloc_aligned_large_page, std::forward<Args>(args)...);

    return LargePagePtr<T>(obj);
}

// make_unique_aligned_large_page() for arrays of unknown bound
template<typename T>
std::enable_if_t<std::is_array_v<T>, LargePagePtr<T>>
make_unique_aligned_large_page(std::size_t size) {
    using ElementType = std::remove_extent_t<T>;

    static_assert(alignof(ElementType) <= 4096,
                  "alloc_aligned_large_page() may fail for such a big alignment requirement of T");

    auto* mem = memory_allocator<T>(alloc_aligned_large_page, size);

    return LargePagePtr<T>(mem);
}

// Get the first aligned element of an array.
// ptr must point to an array of size at least 'sizeof(T) * N + alignment' bytes,
// where N is the number of elements in the array.
template<std::size_t Alignment, typename T>
[[nodiscard]] constexpr T* align_ptr_up(T* ptr) noexcept {
    static_assert(Alignment != 0 && (Alignment & (Alignment - 1)) == 0,
                  "Alignment must be non-zero power of 2");
    static_assert(Alignment >= alignof(T), "Alignment must be >= alignof(T)");

    auto ptrInt = reinterpret_cast<std::uintptr_t>(ptr);
    ptrInt      = round_up_pow2(ptrInt, static_cast<std::uintptr_t>(Alignment));
    return reinterpret_cast<T*>(ptrInt);
}

template<std::size_t Alignment, typename T>
[[nodiscard]] constexpr const T* align_ptr_up(const T* ptr) noexcept {
    return reinterpret_cast<const T*>(align_ptr_up<Alignment>(const_cast<T*>(ptr)));
}

#if defined(_WIN32)

struct Advapi final {
   public:
    // clang-format off
    // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocesstoken
    using OpenProcessToken_      = BOOL(WINAPI*)(HANDLE, DWORD, PHANDLE);
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lookupprivilegevaluea
    using LookupPrivilegeValue_  = BOOL(WINAPI*)(LPCSTR, LPCSTR, PLUID);
    // https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges
    using AdjustTokenPrivileges_ = BOOL(WINAPI*)(HANDLE, BOOL, PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
    // clang-format on

    static constexpr LPCSTR MODULE_NAME = TEXT("advapi32.dll");

    constexpr Advapi() noexcept = default;

    ~Advapi() noexcept { free(); }

    // The needed Windows API for processor groups could be missed from old Windows versions,
    // so instead of calling them directly (forcing the linker to resolve the calls at compile time),
    // try to load them at runtime.
    bool load() noexcept {

        hModule = GetModuleHandle(MODULE_NAME);

        if (hModule == nullptr)
        {
            hModule = LoadLibraryEx(MODULE_NAME, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

            if (hModule == nullptr)
                hModule = LoadLibrary(MODULE_NAME);  // optional last resort

            if (hModule == nullptr)
                return false;

            loaded = true;
        }

        openProcessToken =
          OpenProcessToken_((void (*)()) GetProcAddress(hModule, "OpenProcessToken"));

        if (openProcessToken == nullptr)
            return false;

        lookupPrivilegeValue =
          LookupPrivilegeValue_((void (*)()) GetProcAddress(hModule, "LookupPrivilegeValueA"));

        if (lookupPrivilegeValue == nullptr)
            return false;

        adjustTokenPrivileges =
          AdjustTokenPrivileges_((void (*)()) GetProcAddress(hModule, "AdjustTokenPrivileges"));

        if (adjustTokenPrivileges == nullptr)
            return false;

        return true;
    }

    void free() noexcept {
        if (loaded && hModule != nullptr)
            FreeLibrary(hModule);

        hModule = nullptr;
        loaded  = false;
    }

    OpenProcessToken_      openProcessToken      = nullptr;
    LookupPrivilegeValue_  lookupPrivilegeValue  = nullptr;
    AdjustTokenPrivileges_ adjustTokenPrivileges = nullptr;

   private:
    HMODULE hModule = nullptr;
    bool    loaded  = false;
};

template<typename SuccessFunc, typename FailureFunc>
auto try_with_windows_lock_memory_privilege([[maybe_unused]] SuccessFunc&& successFunc,
                                            FailureFunc&&                  failureFunc) noexcept {
    #if !defined(_WIN64)
    return failureFunc();
    #else

    const std::size_t largePageSize = GetLargePageMinimum();

    if (largePageSize == 0)
        return failureFunc();

    assert((largePageSize & (largePageSize - 1)) == 0);

    Advapi advapi;

    if (!advapi.load())
        return failureFunc();

    HANDLE hProcess = nullptr;

    // Need SeLockMemoryPrivilege, so try to enable it for the process
    if (!advapi.openProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                 &hProcess))
        return failureFunc();

    TOKEN_PRIVILEGES newTp;
    newTp.PrivilegeCount           = 1;
    newTp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Get the luid
    if (!advapi.lookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &newTp.Privileges[0].Luid))
        return failureFunc();

    TOKEN_PRIVILEGES oldTp;
    DWORD            oldTpLen = 0;

    // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
    // Still need to query GetLastError() to ensure that the privileges were actually obtained.
    SetLastError(ERROR_SUCCESS);

    if (!advapi.adjustTokenPrivileges(hProcess, FALSE, &newTp, sizeof(oldTp), &oldTp, &oldTpLen)
        || GetLastError() != ERROR_SUCCESS)
        return failureFunc();

    // Call the provided function with the privilege enabled
    auto&& ret = successFunc(largePageSize);

    // Privilege no longer needed, restore the privileges
    //if (oldTp.PrivilegeCount > 0)
    advapi.adjustTokenPrivileges(hProcess, FALSE, &oldTp, 0, nullptr, nullptr);

    if (hProcess != nullptr)
        CloseHandle(hProcess);

    advapi.free();

    return std::forward<decltype(ret)>(ret);
    #endif
}

#endif

}  // namespace DON

#endif  // #ifndef MEMORY_H_INCLUDED
