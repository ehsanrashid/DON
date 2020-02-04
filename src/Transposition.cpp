#include "Transposition.h"

#include <cstring>
#include <fstream>

#include "Engine.h"
#include "MoveGenerator.h"
#include "Thread.h"

TTable TT;

using namespace std;

u08 TEntry::Generation;

u32 TCluster::fresh_entry_count() const
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
            hit = ite->d08 != 0;
            return ite;
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
    hit = false;
    return rte;
}

namespace {

#if defined(__linux__) && !defined(__ANDROID__)
#   include <stdlib.h>
#   include <sys/mman.h>
#endif

    /// alloc_aligned_memory will return suitably aligned memory, and if possible use large pages.
    /// The returned pointer is the aligned one, while the mem argument is the one that needs to be passed to free.
    /// With c++17 some of this functionality can be simplified.

    void* alloc_aligned_memory(size_t msize, void *&mem)
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
    mem = nullptr;
}

/// TTable::resize() sets the size of the transposition table, measured in MB.
/// Transposition table consists of a power of 2 number of clusters and
/// each cluster consists of EntryCount number of TTEntry.
u32 TTable::resize(u32 mem_size)
{
    mem_size = clamp(mem_size, MinHashSize, MaxHashSize);
    size_t msize = size_t(mem_size) << 20;

    Threadpool.main_thread()->wait_while_busy();

    free(mem);
    clusters = static_cast<TCluster*>(alloc_aligned_memory(msize, mem));
    if (nullptr == mem)
    {
        cerr << "ERROR: Hash memory allocation failed for TT " << mem_size << " MB" << endl;
        return 0;
    }

    cluster_count = msize / sizeof (TCluster);
    hashfull_count = u16(std::min(size_t(1000), cluster_count));
    clear();
    sync_cout << "info string Hash memory " << mem_size << " MB" << sync_endl;
    return mem_size;
}

/// TTable::auto_resize() set size automatically
void TTable::auto_resize(u32 mem_size)
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
    assert(0 != cluster_count);
    if (bool(Options["Retain Hash"]))
    {
        return;
    }

    vector<thread> threads;
    auto thread_count = option_threads();
    for (size_t idx = 0; idx < thread_count; ++idx)
    {
        threads.emplace_back([this, idx, thread_count]()
                             {
                                 if (8 < thread_count)
                                 {
                                     WinProcGroup::bind(idx);
                                 }
                                 size_t stride = cluster_count / thread_count;
                                 auto *pcluster = clusters + idx * stride;
                                 size_t count = idx != (thread_count - 1) ?
                                                 stride :
                                                 cluster_count - idx * stride;
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
/// TTable::hash_full() returns an approximation of the per-mille of the
/// all transposition entries during a search which have received
/// at least one write during the current search.
/// It is used to display the "info hashfull ..." information in UCI.
/// "the hash is <x> per mill full", the engine should send this info regularly.
/// hash, are using <x>%. of the state of full.
u32 TTable::hash_full() const
{
    u32 entry_count = 0;
    for (auto *itc = clusters; itc < clusters + hashfull_count; ++itc)
    {
        entry_count += itc->fresh_entry_count();
    }
    return u32((entry_count / TCluster::EntryCount) * 1000 / hashfull_count);
}

/// TTable::extract_next_move() extracts next move after current move.
Move TTable::extract_next_move(Position &pos, Move cm) const
{
    assert(MOVE_NONE != cm
        && MoveList<GenType::LEGAL>(pos).contains(cm));

    StateInfo si;
    pos.do_move(cm, si);
    bool tt_hit;
    auto *tte = probe(pos.si->posi_key, tt_hit);
    auto nm = tt_hit ?
                tte->move() :
                MOVE_NONE;
    if (   MOVE_NONE != nm
        && !(   pos.pseudo_legal(nm)
             && pos.legal(nm)))
    {
        nm = MOVE_NONE;
    }
    assert(MOVE_NONE == nm
        || MoveList<GenType::LEGAL>(pos).contains(nm));
    pos.undo_move(cm);

    return nm;
}

/// TTable::save() saves hash to file
void TTable::save(const string &hash_fn) const
{
    if (white_spaces(hash_fn))
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
    if (white_spaces(hash_fn))
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
