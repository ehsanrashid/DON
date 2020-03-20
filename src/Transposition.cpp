#include "Transposition.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#include "Helper.h"
#include "MoveGenerator.h"
#include "Thread.h"
#include "UCI.h"

TTable TT;

u08 TEntry::Generation = 0;

void TEntry::save(u64 k, Move m, Value v, Value e, Depth d, Bound b, bool pv) {
    // Preserve more valuable entries
    if (m != MOVE_NONE
     || k16 != u16(k >> 0x30)) {
        m16 = u16(m);
    }
    if (k16 != u16(k >> 0x30)
     || d08 < d - DEPTH_OFFSET + 4
     || b == BOUND_EXACT) {
        assert(d > DEPTH_OFFSET);

        k16 = u16(k >> 0x30);
        v16 = i16(v);
        e16 = i16(e);
        d08 = u08(d - DEPTH_OFFSET);
        g08 = u08(Generation | (u08(pv) << 2) | b);
    }
}


u32 TCluster::freshEntryCount() const {
    return (entryTable[0].generation() == TEntry::Generation)
         + (entryTable[1].generation() == TEntry::Generation)
         + (entryTable[2].generation() == TEntry::Generation);
}

/// TCluster::probe()
/// If the position is found, it returns true and a pointer to the found entry.
/// Otherwise, it returns false and a pointer to an empty or least valuable entry to be replaced later.
TEntry* TCluster::probe(u16 key16, bool &hit) {
    // Find an entry to be replaced according to the replacement strategy.
    auto *rte = entryTable; // Default first
    for (auto *ite = entryTable; ite < entryTable + EntryCount; ++ite) {
        if (ite->d08 == 0
         || ite->k16 == key16) {
            // Refresh entry
            ite->g08 = u08(TEntry::Generation | (ite->g08 & 7));
            hit = (ite->d08 != 0);
            return ite;
        }
        // Replacement strategy.
        // Due to packed storage format for generation and its cyclic nature
        // add 263 (256 + 7 [4 + BOUND_EXACT] to keep the unrelated lowest three bits from affecting the result)
        // to calculate the entry age correctly even after generation overflows into the next cycle.
        if ((rte->d08 - ((263 + TEntry::Generation - rte->g08) & 248))
          > (ite->d08 - ((263 + TEntry::Generation - ite->g08) & 248))) {
            rte = ite;
        }
    }
    hit = false;
    return rte;
}

namespace {

#if defined(__linux__) && !defined(__ANDROID__)

#   include <cstdlib>
#   include <sys/mman.h>

#endif

    /// allocAlignedMemory will return suitably aligned memory, and if possible use large pages.
    /// The returned pointer is the aligned one, while the mem argument is the one that needs to be passed to free.
    /// With c++17 some of this functionality can be simplified.

    void* allocAlignedMemory(void *&mem, size_t mSize) {

#   if defined(__linux__) && !defined(__ANDROID__)

        constexpr size_t alignment = 2 * 1024 * 1024; // assumed 2MB page sizes
        size_t size = ((mSize + alignment - 1) / alignment) * alignment; // multiple of alignment
        mem = aligned_alloc(alignment, size);
        madvise(mem, mSize, MADV_HUGEPAGE);
        return mem;

#   else

        constexpr size_t alignment = 64;        // assumed cache line size
        size_t size = mSize + alignment - 1;    // allocate some extra space
        mem = malloc(size);
        return reinterpret_cast<void*>((uPtr(mem) + alignment - 1) & ~uPtr(alignment - 1));

#   endif
        //assert(new_mem != nullptr
        //    && (uPtr(new_mem) & (alignment - 1)) == 0);
    }

}


TTable::~TTable() {
    free(_mem);
    //_mem = nullptr;
}

/// size() returns hash size in MB
u32 TTable::size() const {
    return u32((_clusterCount * sizeof (TCluster)) >> 20);
}
/// cluster() returns a pointer to the cluster of given a key.
/// Lower 32 bits of the key are used to get the index of the cluster.
TCluster* TTable::cluster(Key posiKey) const {
    return &_clusterTable[(u32(posiKey) * _clusterCount) >> 0x20];
}

/// TTable::resize() sets the size of the transposition table, measured in MB.
/// Transposition table consists of a power of 2 number of clusters and
/// each cluster consists of EntryCount number of TTEntry.
u32 TTable::resize(u32 memSize) {
    memSize = clamp(memSize, MinHashSize, MaxHashSize);

    Threadpool.mainThread()->waitIdle();

    free(_mem);

    _clusterCount = (size_t(memSize) << 20) / sizeof (TCluster);
    _clusterTable = static_cast<TCluster*>(allocAlignedMemory(_mem, size_t(_clusterCount * sizeof (TCluster))));
    if (_mem == nullptr) {
        std::cerr << "ERROR: Hash memory allocation failed for TT " << memSize << " MB" << std::endl;
        return 0;
    }

    clear();
    sync_cout << "info string Hash memory " << memSize << " MB" << sync_endl;
    return memSize;
}

/// TTable::autoResize() set size automatically
void TTable::autoResize(u32 memSize) {
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
    assert(_clusterTable != nullptr
        && _clusterCount != 0);

    if (Options["Retain Hash"]) {
        return;
    }

    std::vector<std::thread> threads;
    auto threadCount{ optionThreads() };
    for (u16 idx = 0; idx < threadCount; ++idx) {
        threads.emplace_back(
            [this, idx, threadCount]() {
                if (threadCount > 8) {
                    WinProcGroup::bind(idx);
                }
                auto const stride{ _clusterCount / threadCount };
                auto const start{ stride * idx };
                auto const count{ idx != threadCount - 1 ?
                                    stride :
                                    _clusterCount - start };
                std::memset(_clusterTable + start, 0, count * sizeof (TCluster));
            });
    }

    for (auto &th : threads) {
        th.join();
    }

    threads.clear();
    //sync_cout << "info string Hash cleared" << sync_endl;
}

/// TTable::probe() looks up the entry in the transposition table.
TEntry* TTable::probe(Key posiKey, bool &hit) const {
    return cluster(posiKey)->probe(u16(posiKey >> 0x30), hit);
}
/// TTable::hashFull() returns an approximation of the per-mille of the
/// all transposition entries during a search which have received
/// at least one write during the current search.
/// It is used to display the "info hashfull ..." information in UCI.
/// "the hash is <x> per mill full", the engine should send this info regularly.
/// hash, are using <x>%. of the state of full.
u32 TTable::hashFull() const {
    u32 freshEntryCount{ 0 };
    for (auto *itc = _clusterTable; itc < _clusterTable + 1000; ++itc) {
        freshEntryCount += itc->freshEntryCount();
    }
    return freshEntryCount / TCluster::EntryCount;
}

/// TTable::extractNextMove() extracts next move after this move.
Move TTable::extractNextMove(Position &pos, Move m) const {
    assert(m != MOVE_NONE
        && MoveList<GenType::LEGAL>(pos).contains(m));

    StateInfo si;
    pos.doMove(m, si);
    bool ttHit;
    auto *tte{ probe(pos.posiKey(), ttHit) };
    auto nm{ ttHit ?
                tte->move() : MOVE_NONE };
    if (nm != MOVE_NONE
     && !(pos.pseudoLegal(nm)
       && pos.legal(nm))) {
        nm = MOVE_NONE;
    }
    assert(nm == MOVE_NONE
        || MoveList<GenType::LEGAL>(pos).contains(nm));
    pos.undoMove(m);

    return nm;
}

/// TTable::save() saves hash to file
void TTable::save(std::string const &hashFn) const {
    if (whiteSpaces(hashFn)) {
        return;
    }
    std::ofstream ofs{ hashFn, std::ios::out|std::ios::binary };
    if (!ofs.is_open()) {
        return;
    }
    ofs << *this;
    ofs.close();
    sync_cout << "info string Hash saved to file \'" << hashFn << "\'" << sync_endl;
}
/// TTable::load() loads hash from file
void TTable::load(std::string const &hashFn) {
    if (whiteSpaces(hashFn)) {
        return;
    }
    std::ifstream ifs{ hashFn, std::ios::in|std::ios::binary };
    if (!ifs.is_open()) {
        return;
    }
    ifs >> *this;
    ifs.close();
    sync_cout << "info string Hash loaded from file \'" << hashFn << "\'" << sync_endl;
}

namespace {

    constexpr u32 BufferSize = 0x1000;

}

std::ostream& operator<<(std::ostream &os, TTable const &tt) {
    u32 memSize = tt.size();
    u08 dummy = 0;
    os.write((char const*)(&memSize), sizeof (memSize));
    os.write((char const*)(&dummy), sizeof (dummy));
    os.write((char const*)(&dummy), sizeof (dummy));
    os.write((char const*)(&dummy), sizeof (dummy));
    os.write((char const*)(&TEntry::Generation), sizeof (TEntry::Generation));
    for (size_t i = 0; i < tt._clusterCount / BufferSize; ++i) {
        os.write((char const*)(tt._clusterTable + i*BufferSize), sizeof (TCluster)*BufferSize);
    }
    return os;
}

std::istream& operator>>(std::istream &is, TTable       &tt) {
    u32 memSize;
    u08 dummy;
    is.read((char*)(&memSize), sizeof (memSize));
    is.read((char*)(&dummy), sizeof (dummy));
    is.read((char*)(&dummy), sizeof (dummy));
    is.read((char*)(&dummy), sizeof (dummy));
    is.read((char*)(&TEntry::Generation), sizeof (TEntry::Generation));
    tt.resize(memSize);
    for (size_t i = 0; i < tt._clusterCount / BufferSize; ++i) {
        is.read((char*)(tt._clusterTable + i*BufferSize), sizeof (TCluster)*BufferSize);
    }
    return is;
}
