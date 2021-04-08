#pragma once

#include <cstdint>
#include <cstddef>

// `ptr` must point to an array of size at least
// `sizeof(T) * N + alignment` bytes, where `N` is the
// number of elements in the array.
template <uintptr_t Alignment, typename T>
T* alignUpPtr(T *ptr) {
    static_assert(alignof(T) < Alignment);

    const uintptr_t uintPtr{ reinterpret_cast<uintptr_t>(reinterpret_cast<char*>(ptr)) };
    return reinterpret_cast<T*>(reinterpret_cast<char*>((uintPtr + (Alignment - 1)) / Alignment * Alignment));
}

extern void* allocAlignedStd(size_t, size_t) noexcept;
extern void  freeAlignedStd(void*) noexcept;

extern void* allocAlignedLargePages(size_t) noexcept;
extern void  freeAlignedLargePages(void*) noexcept;

/// Win Processors Group
/// Under Windows it is not possible for a process to run on more than one logical processor group.
/// This usually means to be limited to use max 64 cores.
/// To overcome this, some special platform specific API should be called to set group affinity for each thread.
/// Original code from Texel by Peter Osterlund.
namespace WinProcGroup {

    extern void bind(uint16_t);
}
