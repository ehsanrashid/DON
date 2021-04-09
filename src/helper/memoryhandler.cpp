#include "memoryhandler.h"

#include <cassert>
#include <memory>
#include <iostream>
#include <vector>

#include "../type.h"

#if defined(_WIN32)
    // Force to include needed API prototypes
    #if (_WIN32_WINNT < 0x0601)
        #undef  _WIN32_WINNT // Prevent redefinition compiler warning
        #define _WIN32_WINNT _WIN32_WINNT_WIN7
    #endif
    // Disable macros min() and max()
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    // Excludes APIs such as Cryptography, DDE, RPC, Socket
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <Windows.h>

    #undef NOMINMAX
    #undef WIN32_LEAN_AND_MEAN
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    #include <cstdlib>
    #include <sys/mman.h>
#endif

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__OpenBSD__) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32))
    #define POSIX_ALIGNED_MEM
    #include <cstdlib>
#endif

/// allocAlignedStd() is wrapper for systems where the c++17 implementation
/// does not guarantee the availability of std::aligned_alloc().
/// Memory allocated with allocAlignedStd() must be freed with freeAlignedStd().
void* allocAlignedStd(size_t alignment, size_t size) noexcept {

#if defined(POSIX_ALIGNED_MEM)
    void *mem;
    return posix_memalign(&mem, alignment, size) == 0 ? mem : nullptr;
#elif defined(_WIN32)
    return _mm_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif
}

/// freeAlignedStd() free aligned memory
void freeAlignedStd(void *mem) noexcept {

    if (mem == nullptr) return;
#if defined(POSIX_ALIGNED_MEM)
    free(mem);
#elif defined(_WIN32)
    _mm_free(mem);
#else
    free(mem);
#endif
}

#if defined(_WIN32)

namespace {

    void* allocAlignedStdWin(size_t mSize) noexcept {

        return VirtualAlloc(nullptr, mSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }

    #if defined(_WIN64)
    void* allocAlignedLargePagesWin(size_t mSize) noexcept {

        void *mem{ nullptr };

        HANDLE processHandle{};
        const size_t largePageSize{ GetLargePageMinimum() };
        if (largePageSize == 0) {
            return nullptr;
        }
        // We need SeLockMemoryPrivilege, so try to enable it for the process
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &processHandle)) {
            return nullptr;
        }
        // SeLockMemoryPrivilege
        TOKEN_PRIVILEGES currTP{};
        currTP.PrivilegeCount = 1;
        currTP.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; // SE_PRIVILEGE_DISABLED = 0
        if (LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &currTP.Privileges[0].Luid)) {
            TOKEN_PRIVILEGES prevTP{};
            DWORD prevTPLen{ 0 };
            // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
            // we still need to query GetLastError() to ensure that the privileges were actually obtained...
            if (AdjustTokenPrivileges(processHandle, FALSE, &currTP, sizeof(TOKEN_PRIVILEGES), &prevTP, &prevTPLen)) {
                if (GetLastError() == ERROR_SUCCESS) {
                    // Round up size to full pages and allocate
                    mSize = (mSize + largePageSize - 1) & ~size_t(largePageSize - 1);
                    mem = VirtualAlloc(nullptr, mSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);

                    // privilege no longer needed, restore previous state
                    AdjustTokenPrivileges(processHandle, FALSE, &prevTP, 0, nullptr, nullptr);
                }
            }
        }
        if (!CloseHandle(processHandle)) {
            return nullptr;
        }

        return mem;
    }
    #endif
}

#endif

/// allocAlignedLP() will return suitably aligned memory, if possible using large pages.
void* allocAlignedLP(size_t mSize) noexcept {

#if defined(_WIN32)
    void *mem = nullptr;
    #if defined(_WIN64)
    // Try to allocate large pages
    mem = allocAlignedLargePagesWin(mSize);
    #endif
    // Fall back to regular, page aligned, allocation if necessary
    if (mem == nullptr) {
        mem = allocAlignedStdWin(mSize);
    }
    //#if !defined(NDEBUG)
    //else {
    //    std::cerr << "info string Hash table allocation: Windows large pages used. (" << mSize << ")\n";
    //}
    //#endif
#else

    #if defined(__linux__)
    constexpr size_t alignment{ 2 * 1024 * 1024 }; // assumed 2MB page size
    #else
    constexpr size_t alignment{ 4096 };            // assumed small page size
    #endif

    // Round up to multiples of alignment
    size_t size = ((mSize + alignment - 1) / alignment) * alignment;
    void *mem = allocAlignedStd(alignment, size);
    ASSERT_ALIGNED(mem, alignment);
    if (mem != nullptr) {
    #if defined(MADV_HUGEPAGE)
        if (madvise(mem, size, MADV_HUGEPAGE) < 0) {
            // HUGEPAGE aligned error
        }
    #endif
    }

#endif

    return mem;
}

/// freeAlignedLP() will free the previously allocated ttmem
void freeAlignedLP(void *mem) noexcept {

    if (mem == nullptr) return;
#if defined(_WIN32)
    if (!VirtualFree(mem, 0, MEM_RELEASE)) {
        std::cerr << "ERROR: Failed to free memory. code: 0x" << std::hex << GetLastError() << std::dec << std::endl;
        std::exit(EXIT_FAILURE);
    }
#else
    freeAlignedStd(mem);
#endif
}

/// Win Processors Group
/// Under Windows it is not possible for a process to run on more than one logical processor group.
/// This usually means to be limited to use max 64 cores.
/// To overcome this, some special platform specific API should be called to set group affinity for each thread.
/// Original code from Texel by Peter Osterlund.
namespace WinProcGroup {

#if defined(_WIN32)

    /// The needed Windows API for processor groups could be missed from old Windows versions,
    /// so instead of calling them directly (forcing the linker to resolve the calls at compile time),
    /// try to load them at runtime. To do this first define the corresponding function pointers.
    extern "C" {

        using GLPIE  = BOOL(*)(LOGICAL_PROCESSOR_RELATIONSHIP, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD);
        //using GLPIE  = std::add_pointer<BOOL(LOGICAL_PROCESSOR_RELATIONSHIP, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD)>::type;
        using GNNPME = BOOL(*)(USHORT, PGROUP_AFFINITY);
        //using GNNPME = std::add_pointer<BOOL(USHORT, PGROUP_AFFINITY)>::type;
        using STGA   = BOOL(*)(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY);
        //using STGA   = std::add_pointer<BOOL(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY)>::type;
    }

    namespace {

        /// bestGroup() retrieves logical processor information from specific API
        int16_t bestGroup(uint16_t index) {

            // Early exit if the needed API is not available at runtime
            HMODULE pKernel32{ GetModuleHandle(TEXT("Kernel32.dll")) };
            if (pKernel32 == nullptr) {
                return -1;
            }
            auto *pGLPIE{ (GLPIE)(void(*)())GetProcAddress(pKernel32, "GetLogicalProcessorInformationEx") };
            if (pGLPIE == nullptr) {
                return -1;
            }

            DWORD buffSize;
            // First call to get size, expect it to fail due to null buffer
            if (pGLPIE(LOGICAL_PROCESSOR_RELATIONSHIP::RelationAll, nullptr, &buffSize)) {
                return -1;
            }
            // Once know size, allocate the buffer
            auto *pSLPI{ (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) malloc(buffSize) };
            if (pSLPI == nullptr) {
                return -1;
            }
            // Second call, now expect to succeed
            if (!pGLPIE(LOGICAL_PROCESSOR_RELATIONSHIP::RelationAll, pSLPI, &buffSize)) {
                free(pSLPI);
                return -1;
            }

            uint16_t nodeCount{ 0 };
            uint16_t coreCount{ 0 };
            uint16_t threadCount{ 0 };

            DWORD byteOffset{ 0UL };
            auto *iSLPI{ pSLPI };
            while (byteOffset < buffSize) {
                assert(iSLPI->Size != 0);

                switch (iSLPI->Relationship) {
                case LOGICAL_PROCESSOR_RELATIONSHIP::RelationProcessorCore: {
                    coreCount += 1;
                    threadCount += 1 + 1 * (iSLPI->Processor.Flags == LTP_PC_SMT);
                }
                    break;
                case LOGICAL_PROCESSOR_RELATIONSHIP::RelationNumaNode: {
                    nodeCount += 1;
                }
                    break;
                default:
                    break;
                }
                byteOffset += iSLPI->Size;
                iSLPI = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) (((char*)iSLPI) + iSLPI->Size);
            }
            free(pSLPI);

            std::vector<int16_t> groups;
            // Run as many threads as possible on the same node until core limit is
            // reached, then move on filling the next node.
            for (uint16_t n = 0; n < nodeCount; ++n) {
                for (uint16_t i = 0; i < coreCount / nodeCount; ++i) {
                    groups.push_back(n);
                }
            }
            // In case a core has more than one logical processor (we assume 2) and
            // have still threads to allocate, then spread them evenly across available nodes.
            for (uint16_t t = 0; t < threadCount - coreCount; ++t) {
                groups.push_back(t % nodeCount);
            }

            // If we still have more threads than the total number of logical processors
            // then return -1 and let the OS to decide what to do.
            return index < groups.size() ? groups[index] : -1;
        }

    }

    /// bind() set the group affinity for the thread index.
    void bind(uint16_t index) {

        // Use only local variables to be thread-safe
        int16_t const group{ bestGroup(index) };
        // If we still have more threads than the total number of logical processors then let the OS to decide what to do.
        if (group == -1) {
            return;
        }

        auto pKernel32{ GetModuleHandle("Kernel32.dll") };
        if (pKernel32 == nullptr) {
            return;
        }

        auto pGNNPME{ (GNNPME)(void(*)())GetProcAddress(pKernel32, "GetNumaNodeProcessorMaskEx") };
        if (pGNNPME == nullptr) {
            return;
        }
        auto pSTGA{ (STGA)(void(*)())GetProcAddress(pKernel32, "SetThreadGroupAffinity") };
        if (pSTGA == nullptr) {
            return;
        }

        GROUP_AFFINITY groupAffinity;
        if (pGNNPME(group, &groupAffinity)) {
            pSTGA(GetCurrentThread(), &groupAffinity, nullptr);
        }
    }
#else
    void bind(uint16_t) {}
#endif

}
