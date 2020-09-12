#include "Transposition.h"

#include <cstdlib>
#include <cstring> // For std::memset()
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "Helper.h"
#include "MoveGenerator.h"
#include "Thread.h"
#include "UCI.h"

TTable TT;
TTable TTEx;

u08 TEntry::Generation = 0;

void TEntry::refresh() noexcept {
    g08 = u08(Generation | (g08 & 7));
}

void TEntry::save(Key k, Move m, Value v, Value e, Depth d, Bound b, u08 pv) noexcept {

    if (m != MOVE_NONE
     || u16(k) != k16) {
        m16 = u16(m);
    }
    if (b == BOUND_EXACT
     || u16(k) != k16
     || d - DEPTH_OFFSET + 4 > d08) {

        assert(d > DEPTH_OFFSET);
        assert(d < MAX_PLY);

        k16 = u16(k);
        d08 = u08(d - DEPTH_OFFSET);
        g08 = u08(Generation | pv << 2 | b);
        v16 = i16(v);
        e16 = i16(e);
    }
    assert(d08 != 0);
}

u32 TCluster::freshEntryCount() const noexcept {
    return (entry[0].d08 != 0 && entry[0].generation() == TEntry::Generation)
         + (entry[1].d08 != 0 && entry[1].generation() == TEntry::Generation)
         + (entry[2].d08 != 0 && entry[2].generation() == TEntry::Generation);
}

/// TCluster::probe()
/// If the position is found, it returns true and a pointer to the found entry.
/// Otherwise, it returns false and a pointer to an empty or least valuable entry to be replaced later.
TEntry* TCluster::probe(u16 key16, bool &hit) noexcept {
    // Find an entry to be replaced according to the replacement strategy.
    auto *rte{ entry }; // Default first
    for (auto *ite{ entry }; ite < entry + EntryPerCluster; ++ite) {
        if (ite->k16 == key16
         || ite->d08 == 0) {
            // Refresh entry
            ite->refresh();
            hit = ite->d08 != 0;
            return ite;
        }
        // Replacement strategy.
        // Due to packed storage format for generation and its cyclic nature
        // add 263 (256 + 7 [4 + BOUND_EXACT] to keep the unrelated lowest three bits from affecting the result)
        // to calculate the entry age correctly even after generation overflows into the next cycle.
        if (rte->worth() > ite->worth()) {
            rte = ite;
        }
    }
    hit = false;
    return rte;
}

namespace {

    /// allocAlignedMemory will return suitably aligned memory, if possible use large pages.
    /// The returned pointer is the aligned one,
    /// while the mem argument is the one that needs to be passed to free.
    /// With C++17 some of this functionality can be simplified.

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

    void* allocAlignedMemoryLargePages(size_t mSize) noexcept {
        HANDLE processHandle{};
        LUID luid{};
        void *mem{ nullptr };

        const size_t LargePageSize{ GetLargePageMinimum() };
        if (LargePageSize == 0) {
            return nullptr;
        }
        // We need SeLockMemoryPrivilege, so try to enable it for the process
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &processHandle)) {
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
            if (AdjustTokenPrivileges(processHandle, FALSE, &currTP, sizeof (TOKEN_PRIVILEGES), &prevTP, &prevTPLen)
             && GetLastError() == ERROR_SUCCESS) {
                // round up size to full pages and allocate
                mSize = (mSize + LargePageSize - 1) & ~size_t(LargePageSize - 1);
                mem = VirtualAlloc(nullptr, mSize, MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES, PAGE_READWRITE);

                // privilege no longer needed, restore previous state
                AdjustTokenPrivileges(processHandle, FALSE, &prevTP, 0, nullptr, nullptr);
            }
        }
        CloseHandle(processHandle);
        return mem;
    }
    
    void* allocAlignedMemory(void *&mem, size_t mSize) noexcept {
        static bool firstCall{ true };

        // Try to allocate large pages
        mem = allocAlignedMemoryLargePages(mSize);
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
            mem = VirtualAlloc(nullptr, mSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
        }
        return mem;
    }

#elif defined(__linux__) && !defined(__ANDROID__)
    #include <cstdlib>
    #include <sys/mman.h>

    void* allocAlignedMemory(void *&mem, size_t mSize) noexcept {

        constexpr size_t alignment{ 2 * 1024 * 1024 };                      // assumed 2MB page sizes
        size_t size{ ((mSize + alignment - 1) / alignment) * alignment };   // multiple of alignment
        if (posix_memalign(&mem, alignment, size) == 0) {
        #if defined(MADV_HUGEPAGE)
            if (madvise(mem, mSize, MADV_HUGEPAGE) == 0) {
                // HUGEPAGE aligned
            }
        #endif
        }
        else {
            mem = nullptr;
        }
        return mem;
    }
#else
    void* allocAlignedMemory(void *&mem, size_t mSize) noexcept {

        constexpr size_t alignment{ 64 };        // assumed cache line size
        size_t size{ mSize + alignment - 1 };    // allocate some extra space
        mem = malloc(size);
        return mem != nullptr ?
                reinterpret_cast<void*>((uPtr(mem) + alignment - 1) & ~uPtr(alignment - 1)) :
                nullptr;
    }
#endif
 
    /// freeAlignedMemory will free the previously allocated ttmem
#if defined(_WIN64)
    void freeAlignedMemory(void *&mem) noexcept {
        if (mem != nullptr) {
            if (VirtualFree(mem, 0, MEM_RELEASE) == 0) {
                DWORD err = GetLastError();
                std::cerr << "Failed to free transposition table. Error code: 0x" << std::hex << err << std::dec << '\n';
                std::exit(EXIT_FAILURE);
            }
            mem = nullptr;
        }
    }

#else
    void freeAlignedMemory(void *&mem) noexcept {
        if (mem != nullptr) {
            free(mem);
            mem = nullptr;
        }
    }
#endif

    inline u64 mul_hi64(u64 a, u64 b) noexcept {

#if defined(__GNUC__) && defined(IS_64BIT)
        __extension__ typedef unsigned __int128 u128;
        return ((u128)a * (u128)b) >> 64;
#else
        u64 const aL{ (u32)a }, aH{ a >> 32 };
        u64 const bL{ (u32)b }, bH{ b >> 32 };
        u64 const c1{ (aL * bL) >> 32 };
        u64 const c2{ aH * bL + c1 };
        u64 const c3{ aL * bH + (u32)c2 };
        return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif

    }

}


TTable::~TTable() {
    free();
}

/// size() returns hash size in MB
u32 TTable::size() const noexcept {
    return( (clusterCount * sizeof (TCluster)) >> 20 );
}
/// cluster() returns a pointer to the cluster of given a key.
/// Lower 32 bits of the key are used to get the index of the cluster.
TCluster* TTable::cluster(Key posiKey) const noexcept {
    return &clusterTable[mul_hi64(posiKey, clusterCount)];
}

/// TTable::resize() sets the size of the transposition table, measured in MB.
/// Transposition table consists of a power of 2 number of clusters and
/// each cluster consists of EntryPerCluster number of TTEntry.
size_t TTable::resize(size_t memSize) {
    memSize = std::clamp(memSize, MinHashSize, MaxHashSize);

    Threadpool.mainThread()->waitIdle();

    free();

    clusterCount = (memSize << 20) / sizeof (TCluster);
    clusterTable = static_cast<TCluster*>(allocAlignedMemory(mem, clusterCount * sizeof (TCluster)));
    if (mem == nullptr) {
        std::cerr << "ERROR: Hash memory allocation failed for TT " << memSize << " MB" << '\n';
        return 0;
    }

    clear();
    //sync_cout << "info string Hash memory " << memSize << " MB" << sync_endl;
    return memSize;
}

/// TTable::autoResize() set size automatically
void TTable::autoResize(size_t memSize) {
    auto mSize{ memSize != 0 ? memSize : MaxHashSize };
    while (mSize >= MinHashSize) {
        if (resize(mSize) != 0) {
            return;
        }
        mSize >>= 1;
    }
    std::exit(EXIT_FAILURE);
}
/// TTable::clear() clear the entire transposition table in a multi-threaded way.
void TTable::clear() {
    assert(clusterTable != nullptr
        && clusterCount != 0);

    if (Options["Retain Hash"]) {
        return;
    }

    std::vector<std::thread> threads;
    auto const threadCount{ optionThreads() };
    for (u16 index = 0; index < threadCount; ++index) {
        threads.emplace_back(
            [this, index, threadCount]() {

                if (threadCount > 8) {
                    WinProcGroup::bind(index);
                }
                // Each thread will zero its part of the hash table
                auto const stride{ clusterCount / threadCount };
                auto const start{ stride * index };
                auto const count{ index != threadCount - 1 ? stride : clusterCount - start };
                std::memset(&clusterTable[start], 0, count * sizeof (TCluster));
            });
    }

    for (auto &th : threads) {
        th.join();
    }

    threads.clear();
    //sync_cout << "info string Hash cleared" << sync_endl;
}

void TTable::free() noexcept {
    freeAlignedMemory(mem);
}

/// TTable::probe() looks up the entry in the transposition table.
TEntry* TTable::probe(Key posiKey, bool &hit) const noexcept {
    return cluster(posiKey)->probe(u16(posiKey), hit);
}
/// TTable::hashFull() returns an approximation of the per-mille of the
/// all transposition entries during a search which have received
/// at least one write during the current search.
/// It is used to display the "info hashfull ..." information in UCI.
/// "the hash is <x> per mill full", the engine should send this info regularly.
/// hash, are using <x>%. of the state of full.
u32 TTable::hashFull() const noexcept {
    u32 freshEntryCount{ 0 };
    for (auto *itc{ clusterTable }; itc < clusterTable + 1000; ++itc) {
        freshEntryCount += itc->freshEntryCount();
    }
    return freshEntryCount / TCluster::EntryPerCluster;
}

/// TTable::extractNextMove() extracts next move after this move.
Move TTable::extractNextMove(Position &pos, Move m) const noexcept {
    assert(m != MOVE_NONE
        && MoveList<LEGAL>(pos).contains(m));

    StateInfo si;
    pos.doMove(m, si);
    bool ttHit;
    auto const *const tte{ probe(pos.posiKey(), ttHit) };
    auto nm{ ttHit ? tte->move() : MOVE_NONE };
    if (nm != MOVE_NONE
     && !(pos.pseudoLegal(nm)
       && pos.legal(nm))) {
        nm = MOVE_NONE;
    }
    assert(nm == MOVE_NONE
        || MoveList<LEGAL>(pos).contains(nm));
    pos.undoMove(m);

    return nm;
}

/// TTable::save() saves hash to file
void TTable::save(std::string_view hashFile) const {
    if (whiteSpaces(hashFile)) {
        return;
    }
    std::ofstream ofstream{ hashFile.data(), std::ios::out|std::ios::binary };
    if (!ofstream.is_open()) {
        return;
    }
    ofstream << *this;
    ofstream.close();
    sync_cout << "info string Hash saved to file \'" << hashFile << "\'" << sync_endl;
}
/// TTable::load() loads hash from file
void TTable::load(std::string_view hashFile) {
    if (whiteSpaces(hashFile)) {
        return;
    }
    std::ifstream ifstream{ hashFile.data(), std::ios::in|std::ios::binary };
    if (!ifstream.is_open()) {
        return;
    }
    ifstream >> *this;
    ifstream.close();
    sync_cout << "info string Hash loaded from file \'" << hashFile << "\'" << sync_endl;
}

constexpr u32 BufferSize{ 0x1000 };

std::ostream& operator<<(std::ostream &ostream, TTable const &tt) {
    u32 const memSize{ tt.size() };
    u08 dummy{ 0 };
    ostream.write((char const*)(&memSize), sizeof (memSize));
    ostream.write((char const*)(&dummy), sizeof (dummy));
    ostream.write((char const*)(&dummy), sizeof (dummy));
    ostream.write((char const*)(&dummy), sizeof (dummy));
    ostream.write((char const*)(&TEntry::Generation), sizeof (TEntry::Generation));
    for (size_t i = 0; i < tt.clusterCount / BufferSize; ++i) {
        ostream.write((char const*)(&tt.clusterTable[i*BufferSize]), sizeof (TCluster)*BufferSize);
    }
    return ostream;
}

std::istream& operator>>(std::istream &istream, TTable       &tt) {
    u32 memSize;
    u08 dummy;
    istream.read((char*)(&memSize), sizeof (memSize));
    istream.read((char*)(&dummy), sizeof (dummy));
    istream.read((char*)(&dummy), sizeof (dummy));
    istream.read((char*)(&dummy), sizeof (dummy));
    istream.read((char*)(&TEntry::Generation), sizeof (TEntry::Generation));
    tt.resize(memSize);
    for (size_t i = 0; i < tt.clusterCount / BufferSize; ++i) {
        istream.read((char*)(&tt.clusterTable[i*BufferSize]), sizeof (TCluster)*BufferSize);
    }
    return istream;
}
