#pragma once

#include <cstdint>

void* allocAlignedStd(size_t, size_t) noexcept;
void  freeAlignedStd(void*) noexcept;

void* allocAlignedLargePages(size_t) noexcept;
void  freeAlignedLargePages(void*) noexcept;

/// Win Processors Group
/// Under Windows it is not possible for a process to run on more than one logical processor group.
/// This usually means to be limited to use max 64 cores.
/// To overcome this, some special platform specific API should be called to set group affinity for each thread.
/// Original code from Texel by Peter Osterlund.
namespace WinProcGroup {

    extern void bind(uint16_t);
}
