#include "MemoryHandler.h"

#include <memory>

#include "Thread.h"

#if defined(_WIN64)
    #if (_WIN32_WINNT < 0x0601)
        #undef  _WIN32_WINNT
        #define _WIN32_WINNT _WIN32_WINNT_WIN7 // Force to include needed API prototypes
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX // Disable macros min() and max()
    #endif
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN // Excludes APIs such as Cryptography, DDE, RPC, Socket
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


//    /// allocAlignedMemory will return suitably aligned memory, if possible use large pages.
//    /// The returned pointer is the aligned one,
//    /// while the mem argument is the one that needs to be passed to free.
//    /// With C++17 some of this functionality can be simplified.

/// allocAlignedStd() is our wrapper for systems where the c++17 implementation
/// does not guarantee the availability of std::aligned_alloc().
/// Memory allocated with allocAlignedStd() must be freed with freeAlignedStd().
void *allocAlignedStd(size_t alignment, size_t size) {

#if defined(POSIX_ALIGNED_MEM)
    void *mem;
    return posix_memalign(&mem, alignment, size) ? nullptr : mem;
#elif defined(_WIN32)
    return _mm_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif

    return nullptr;
}

/// freeAlignedStd() free aligned memory
void freeAlignedStd(void *mem) {

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

    void *allocAlignedLargePagesWin(size_t mSize) {

        HANDLE processHandle{};
        LUID luid{};
        void *mem{ nullptr };

        const size_t LargePageSize{ GetLargePageMinimum() };
        if (LargePageSize == 0) {
            return nullptr;
        }
        // We need SeLockMemoryPrivilege, so try to enable it for the process
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &processHandle)) {
            return nullptr;
        }

        if (LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &luid)) {
            TOKEN_PRIVILEGES currTP{};
            TOKEN_PRIVILEGES prevTP{};
            DWORD prevTPLen{ 0 };

            currTP.PrivilegeCount = 1;
            currTP.Privileges[0].Luid = luid;
            currTP.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

            // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
            // we still need to query GetLastError() to ensure that the privileges were actually obtained...
            if (AdjustTokenPrivileges(processHandle, FALSE, &currTP, sizeof(TOKEN_PRIVILEGES), &prevTP, &prevTPLen)
                && GetLastError() == ERROR_SUCCESS) {
                // round up size to full pages and allocate
                mSize = (mSize + LargePageSize - 1) & ~size_t(LargePageSize - 1);
                mem = VirtualAlloc(nullptr, mSize, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);

                // privilege no longer needed, restore previous state
                AdjustTokenPrivileges(processHandle, FALSE, &prevTP, 0, nullptr, nullptr);
            }
        }
        CloseHandle(processHandle);
        return mem;
    }
}

#endif

/// allocAlignedLargePages() will return suitably aligned memory, if possible using large pages.
void *allocAlignedLargePages(size_t mSize) {

#if defined(_WIN32)
    static bool firstCall{ true };

    // Try to allocate large pages
    void *mem = allocAlignedLargePagesWin(mSize);
    if (!firstCall) {
        if (mem != nullptr) {
            sync_cout << "info string Hash table allocation: Windows large pages used." << sync_endl;
        }
        else {
            sync_cout << "info string Hash table allocation: Windows large pages not used." << sync_endl;
        }
    }
    firstCall = false;

    // Fall back to regular, page aligned, allocation if necessary
    if (mem == nullptr) {
        mem = VirtualAlloc(nullptr, mSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
#else

    constexpr size_t alignment =
    #if defined(__linux__)
        2 * 1024 * 1024; // assumed 2MB page size
    #else
        4096; // assumed small page size
    #endif

  // round up to multiples of alignment
    size_t size = ((mSize + alignment - 1) / alignment) * alignment;
    void *mem = allocAlignedStd(alignment, size);
    if (mem != nullptr) {
    #if defined(MADV_HUGEPAGE)
        if (madvise(mem, size, MADV_HUGEPAGE) == 0) {
            // HUGEPAGE aligned
        }
    #endif
    }
#endif
    return mem;
}

/// freeAlignedLargePages() will free the previously allocated ttmem
void freeAlignedLargePages(void *mem) {

    if (mem == nullptr) return;
#if defined(_WIN32)
    if (!VirtualFree(mem, 0, MEM_RELEASE)) {
        DWORD err{ GetLastError() };
        std::cerr << "Failed to free transposition table. Error code: 0x" <<
            std::hex << err << std::dec << std::endl;
        exit(EXIT_FAILURE);
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

        //using GLPIE  = bool (*)(LOGICAL_PROCESSOR_RELATIONSHIP LogicalProcRelationship, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX PtrSysLogicalProcInfo, PDWORD PtrLength);
        using GLPIE  = std::add_pointer<bool(LOGICAL_PROCESSOR_RELATIONSHIP, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD)>::type;
        //using GNNPME = bool (*)(USHORT Node, PGROUP_AFFINITY PtrGroupAffinity);
        using GNNPME = std::add_pointer<bool(USHORT, PGROUP_AFFINITY)>::type;
        //using STGA   = bool (*)(HANDLE Thread, CONST GROUP_AFFINITY *GroupAffinity, PGROUP_AFFINITY PtrGroupAffinity);
        using STGA   = std::add_pointer<bool(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY)>::type;
    }

    namespace {

        /// bestGroup() retrieves logical processor information from specific API
        i16 bestGroup(u16 index) {

            // Early exit if the needed API is not available at runtime
            auto pKernel32{ GetModuleHandle("Kernel32.dll") };
            if (pKernel32 == nullptr) {
                return -1;
            }
            auto pGLPIE{ (GLPIE)(void(*)())GetProcAddress(pKernel32, "GetLogicalProcessorInformationEx") };
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

            u16 nodeCount{ 0 };
            u16 coreCount{ 0 };
            u16 threadCount{ 0 };

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

            std::vector<i16> groups;
            // Run as many threads as possible on the same node until core limit is
            // reached, then move on filling the next node.
            for (u16 n = 0; n < nodeCount; ++n) {
                for (u16 i = 0; i < coreCount / nodeCount; ++i) {
                    groups.push_back(n);
                }
            }
            // In case a core has more than one logical processor (we assume 2) and
            // have still threads to allocate, then spread them evenly across available nodes.
            for (u16 t = 0; t < threadCount - coreCount; ++t) {
                groups.push_back(t % nodeCount);
            }

            // If we still have more threads than the total number of logical processors
            // then return -1 and let the OS to decide what to do.
            return index < groups.size() ? groups[index] : -1;
        }

    }

    /// bind() set the group affinity for the thread index.
    void bind(u16 index) {

        // Use only local variables to be thread-safe
        i16 const group{ bestGroup(index) };
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
    void bind(u16) {}
#endif

}

