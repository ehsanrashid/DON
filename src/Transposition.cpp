#include "Transposition.h"

#include "BitScan.h"
#include "Engine.h"

TranspositionTable  TT; // Global Transposition Table

using namespace std;

const u08  TranspositionTable::CLUSTER_ENTRIES = 4;

const u08  TranspositionTable::TTENTRY_SIZE = sizeof (TTEntry);  // 16

#ifdef _64BIT
const u32 TranspositionTable::MAX_HASH_BIT  = 32; // 36
#else
const u32 TranspositionTable::MAX_HASH_BIT  = 32;
#endif

const u32 TranspositionTable::MIN_TT_SIZE   = 4;

const u32 TranspositionTable::MAX_TT_SIZE   = (U64 (1) << (MAX_HASH_BIT-1 - 20)) * TTENTRY_SIZE;

void TranspositionTable::alloc_aligned_memory (u64 mem_size, u08 alignment)
{

    ASSERT (0 == (alignment & (alignment - 1)));
    ASSERT (0 == (mem_size  & (alignment - 1)));

#ifdef LPAGES

    u08 offset = max<i08> (alignment-1, sizeof (void *));

    MemoryHandler::create_memory (_mem, mem_size, alignment);

    void *ptr = (void *) ((uintptr_t (_mem) + offset) & ~uintptr_t (offset));
    _hash_table = (TTEntry *) (ptr);

#else

    // We need to use malloc provided by C.
    // First we need to allocate memory of mem_size + max (alignment, sizeof (void *)).
    // We need 'bytes' because user requested it.
    // We need to add 'alignment' because malloc can give us any address and
    // we need to find multiple of 'alignment', so at maximum multiple
    // of alignment will be 'alignment' bytes away from any location.
    // We need 'sizeof(void *)' for implementing 'aligned_free',
    // since we are returning modified memory pointer, not given by malloc, to the user,
    // we must free the memory allocated by malloc not anything else.
    // So storing address given by malloc just above pointer returning to user.
    // Thats why needed extra space to store that address.
    // Then checking for error returned by malloc, if it returns NULL then 
    // alloc_aligned_memory will fail and return NULL or exit().

    u08 offset = max<i08> (alignment, sizeof (void *));

    void *mem = calloc (mem_size + offset, 1);
    if (mem == NULL)
    {
        cerr << "ERROR: Failed to allocate Hash " << (mem_size >> 20) << " MB." << endl;
        Engine::exit (EXIT_FAILURE);
    }

    std::cout << "info string Hash " << (mem_size >> 20) << " MB." << std::endl;

    void **ptr =
        //(void **) (uintptr_t (mem) + sizeof (void *) + (alignment - ((uintptr_t (mem) + sizeof (void *)) & uintptr_t (alignment - 1))));
        (void **) ((uintptr_t (mem) + offset) & ~uintptr_t (alignment - 1));

    ptr[-1]     = mem;
    _hash_table = (TTEntry *) (ptr[0]);

#endif

    ASSERT (0 == (uintptr_t (_hash_table) & (alignment - 1)));

}

// resize(mb) sets the size of the table, measured in mega-bytes.
// Transposition table consists of a power of 2 number of clusters and
// each cluster consists of CLUSTER_ENTRIES number of entry.
u32 TranspositionTable::resize (u32 mem_size_mb, bool force)
{
    if (mem_size_mb < MIN_TT_SIZE) mem_size_mb = MIN_TT_SIZE;
    if (mem_size_mb > MAX_TT_SIZE) mem_size_mb = MAX_TT_SIZE;

    u64 mem_size      = u64 (mem_size_mb) << 20;
    u64 cluster_count = (mem_size) / sizeof (TTEntry[CLUSTER_ENTRIES]);
    u64   entry_count = u64 (CLUSTER_ENTRIES) << scan_msq (cluster_count);

    ASSERT (scan_msq (entry_count) < MAX_HASH_BIT);

    mem_size  = entry_count * TTENTRY_SIZE;

    if (force || entry_count != entries ())
    {
        free_aligned_memory ();

        alloc_aligned_memory (mem_size, CACHE_LINE_SIZE);

        _hash_mask = (entry_count - CLUSTER_ENTRIES);
    }

    return (mem_size >> 20);
}

// store() writes a new entry in the transposition table.
// It contains folowing valuable information.
//  - Key
//  - Move.
//  - Score.
//  - Depth.
//  - Bound.
//  - Nodes.
// The lower order bits of position key are used to decide on which cluster the position will be placed.
// The upper order bits of position key are used to store in entry.
// When a new entry is written and there are no empty entries available in cluster,
// it replaces the least valuable of these entries.
// An entry e1 is considered to be more valuable than a entry e2
// * if e1 is from the current search and e2 is from a previous search.
// * if e1 & e2 is from a current search then EXACT bound is valuable.
// * if the depth of e1 is bigger than the depth of e2.
void TranspositionTable::store (Key key, Move move, Depth depth, Bound bound, u16 nodes, Value value, Value eval)
{
    u32 key32 = (key >> 32); // 32 upper-bit of key inside cluster
    TTEntry *tte = cluster_entry (key);
    // By default replace first entry
    TTEntry *rte = tte;

    for (u08 i = 0; i < CLUSTER_ENTRIES; ++i, ++tte)
    {
        if (tte->_key == 0 || tte->_key == key32) // Empty or Old then overwrite
        {
            // Preserve any existing TT move
            if (move == MOVE_NONE)
            {
                move = Move (tte->_move);
            }

            rte = tte;
            break;
        }

        // Replace would be a no-op in this common case
        if (0 == i) continue;

        // Implement replacement strategy when a collision occurs

        /*
        if ( ((tte->_gen == _generation || tte->_bound == BND_EXACT)
            - (rte->_gen == _generation)
            - (tte->_depth < rte->_depth)) < 0)
        {
            rte = tte;
        }
        */

        i08 gc = (rte->_gen == _generation) - ((tte->_gen == _generation) || (tte->_bound == BND_EXACT));
        if (gc != 0)
        {
            if (gc > 0) rte = tte;
            continue;
        }
        // gc == 0
        i16 dc = (rte->_depth - tte->_depth);
        if (dc != 0)
        {
            if (dc > 0) rte = tte;
            continue;
        }
        // dc == 0
        i16 nc = (rte->_nodes - tte->_nodes);
        if (nc > 0) rte = tte;
        //continue;
    }

    rte->save (key32, move, depth, bound, (nodes >> 10), value, eval, _generation);
}

// retrieve() looks up the entry in the transposition table.
// Returns a pointer to the entry found or NULL if not found.
const TTEntry* TranspositionTable::retrieve (Key key) const
{
    u32 key32 = (key >> 32);
    TTEntry *tte = cluster_entry (key);
    for (u08 i = 0; i < CLUSTER_ENTRIES; ++i, ++tte)
    {
        if (tte->_key == key32)
        {
            tte->_gen = _generation;
            return tte;
        }
    }
    return NULL;
}
