#include "Transposition.h"

#include <cstring>
#include <fstream>

#include "Engine.h"
#include "MoveGenerator.h"
#include "Thread.h"

TTable TT;

using namespace std;

u08 TEntry::Generation;

u32 TCluster::freshEntryCount() const
{
    return (entries[0].generation() == TEntry::Generation)
         + (entries[1].generation() == TEntry::Generation)
         + (entries[2].generation() == TEntry::Generation);
}

/// TCluster::probe()
/// If the position is found, it returns true and a pointer to the found entry.
/// Otherwise, it returns false and a pointer to an empty or least valuable entry to be replaced later.
TEntry* TCluster::probe(u16 key16, bool &hit)
{
    // Find an entry to be replaced according to the replacement strategy.
    auto *rte = entries; // Default first
    for (auto *ite = entries; ite < entries + EntryCount; ++ite)
    {
        if (   ite->d08 == 0
            || ite->k16 == key16)
        {
            // Refresh entry
            ite->g08 = u08(TEntry::Generation | (ite->g08 & 0x07));

            return hit = ite->d08 != 0, ite;
        }
        // Replacement strategy.
        // Due to packed storage format for generation and its cyclic nature
        // add 0x107 (0x100 + 7 [4 + BOUND_EXACT] to keep the unrelated lowest three bits from affecting the result)
        // to calculate the entry age correctly even after generation overflows into the next cycle.
        if (  (rte->d08 - ((0x107 + TEntry::Generation - rte->g08) & 0xF8))
            > (ite->d08 - ((0x107 + TEntry::Generation - ite->g08) & 0xF8)))
        {
            rte = ite;
        }
    }
    return hit = false, rte;
}

namespace {

#if defined(__linux__) && !defined(__ANDROID__)
#   include <stdlib.h>
#   include <sys/mman.h>
#endif

    /// allocAlignedMemory will return suitably aligned memory, and if possible use large pages.
    /// The returned pointer is the aligned one, while the mem argument is the one that needs to be passed to free.
    /// With c++17 some of this functionality can be simplified.

    void* allocAlignedMemory(size_t msize, void *&mem)
    {

#   if defined(__linux__) && !defined(__ANDROID__)

        constexpr size_t alignment = 2 * 1024 * 1024; // assumed 2MB page sizes
        size_t size = ((msize + alignment - 1) / alignment) * alignment; // multiple of alignment
        mem = aligned_alloc(alignment, size);
        madvise(mem, msize, MADV_HUGEPAGE);
        return mem;

#   else

        constexpr size_t alignment = 64;        // assumed cache line size
        size_t size = msize + alignment - 1;    // allocate some extra space
        mem = malloc(size);
        return reinterpret_cast<void*>((uintptr_t(mem) + alignment - 1) & ~uintptr_t(alignment - 1));

#   endif
        //assert(nullptr != new_mem
        //    && 0 == (uintptr_t(new_mem) & (alignment-1)));
    }

}

TTable::~TTable()
{
    free(mem);
}

/// TTable::resize() sets the size of the transposition table, measured in MB.
/// Transposition table consists of a power of 2 number of clusters and
/// each cluster consists of EntryCount number of TTEntry.
u32 TTable::resize(u32 mem_size)
{
    mem_size = clamp(mem_size, MinHashSize, MaxHashSize);

    Threadpool.mainThread()->waitIdle();

    free(mem);

    clusterCount = (size_t(mem_size) << 20) / sizeof(TCluster);
    clusters = static_cast<TCluster*>(allocAlignedMemory(clusterCount * sizeof(TCluster), mem));
    if (nullptr == mem)
    {
        cerr << "ERROR: Hash memory allocation failed for TT " << mem_size << " MB" << endl;
        return 0;
    }

    clear();
    sync_cout << "info string Hash memory " << mem_size << " MB" << sync_endl;
    return mem_size;
}

/// TTable::autoResize() set size automatically
void TTable::autoResize(u32 mem_size)
{
    auto msize = 0 != mem_size ? mem_size : MaxHashSize;
    while (msize >= MinHashSize)
    {
        if (0 != resize(msize))
        {
            return;
        }
        msize /= 2;
    }
    stop(EXIT_FAILURE);
}
/// TTable::clear() clear the entire transposition table in a multi-threaded way.
void TTable::clear()
{
    assert(0 != clusterCount);
    if (bool(Options["Retain Hash"]))
    {
        return;
    }

    vector<thread> threads;
    auto threadCount = optionThreads();
    for (size_t idx = 0; idx < threadCount; ++idx)
    {
        threads.emplace_back([this, idx, threadCount]()
                             {
                                 if (8 < threadCount)
                                 {
                                     WinProcGroup::bind(idx);
                                 }
                                 size_t stride = clusterCount / threadCount;
                                 auto *pcluster = clusters + idx * stride;
                                 size_t count = idx != (threadCount - 1) ?
                                                 stride :
                                                 clusterCount - idx * stride;
                                 std::memset(pcluster, 0, count * sizeof (*pcluster));
                             });
    }
    for (auto &th : threads)
    {
        th.join();
    }
    threads.clear();
    //sync_cout << "info string Hash cleared" << sync_endl;
}

/// TTable::probe() looks up the entry in the transposition table.
TEntry* TTable::probe(Key key, bool &hit) const
{
    return cluster(key)->probe(u16(key >> 0x30), hit);
}
/// TTable::hashFull() returns an approximation of the per-mille of the
/// all transposition entries during a search which have received
/// at least one write during the current search.
/// It is used to display the "info hashfull ..." information in UCI.
/// "the hash is <x> per mill full", the engine should send this info regularly.
/// hash, are using <x>%. of the state of full.
u32 TTable::hashFull() const
{
    u32 freshEntryCount = 0;
    for (auto *itc = clusters; itc < clusters + 1000; ++itc)
    {
        freshEntryCount += itc->freshEntryCount();
    }
    return freshEntryCount / TCluster::EntryCount;
}

/// TTable::extractNextMove() extracts next move after current move.
Move TTable::extractNextMove(Position &pos, Move cm) const
{
    assert(MOVE_NONE != cm
        && MoveList<GenType::LEGAL>(pos).contains(cm));

    StateInfo si;
    pos.doMove(cm, si);
    bool ttHit;
    auto *tte = probe(pos.si->posiKey, ttHit);
    auto nm = ttHit ?
                tte->move() :
                MOVE_NONE;
    if (   MOVE_NONE != nm
        && !(   pos.pseudoLegal(nm)
             && pos.legal(nm)))
    {
        nm = MOVE_NONE;
    }
    assert(MOVE_NONE == nm
        || MoveList<GenType::LEGAL>(pos).contains(nm));
    pos.undoMove(cm);

    return nm;
}

/// TTable::save() saves hash to file
void TTable::save(const string &hash_fn) const
{
    if (whiteSpaces(hash_fn))
    {
        return;
    }
    ofstream ofs(hash_fn, ios_base::out|ios_base::binary);
    if (!ofs.is_open())
    {
        return;
    }
    ofs << *this;
    ofs.close();
    sync_cout << "info string Hash saved to file \'" << hash_fn << "\'" << sync_endl;
}
/// TTable::load() loads hash from file
void TTable::load(const string &hash_fn)
{
    if (whiteSpaces(hash_fn))
    {
        return;
    }
    ifstream ifs(hash_fn, ios_base::in|ios_base::binary);
    if (!ifs.is_open())
    {
        return;
    }
    ifs >> *this;
    ifs.close();
    sync_cout << "info string Hash loaded from file \'" << hash_fn << "\'" << sync_endl;
}
