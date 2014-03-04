#include "Transposition.h"

#include "BitScan.h"
#include "Engine.h"

// Global Transposition Table
TranspositionTable TT;

using namespace std;

const uint8_t  TranspositionTable::TENTRY_SIZE      = sizeof (TTEntry);  // 16

const uint8_t  TranspositionTable::CLUSTER_ENTRY    = 4;

#ifdef _64BIT
const uint32_t TranspositionTable::MAX_HASH_BIT     = 32; // 36
#else
const uint32_t TranspositionTable::MAX_HASH_BIT     = 32;
#endif

const uint32_t TranspositionTable::DEF_TT_SIZE      = 128;

const uint32_t TranspositionTable::MIN_TT_SIZE      = 4;

const uint32_t TranspositionTable::MAX_TT_SIZE      = (U64 (1) << (MAX_HASH_BIT - 20 - 1)) * TENTRY_SIZE;

void TranspositionTable::alloc_aligned_memory (uint64_t mem_size, uint8_t alignment)
{

    ASSERT (0 == (alignment & (alignment - 1)));
    ASSERT (0 == (mem_size & (alignment - 1)));

#ifdef LPAGES
    
    uint8_t offset = max<int8_t> (alignment-1, sizeof (void *));

    MemoryHandler::create_memory (_mem, mem_size, alignment);
    if (!_mem)
    {
        cerr << "ERROR: Failed to allocate " << (mem_size >> 20) << " MB Hash..." << endl;
        Engine::exit (EXIT_FAILURE);
    }

    //memset (_mem, 0, mem_size);

    void **ptr = (void **) ((uintptr_t (_mem) + offset) & ~uintptr_t (offset));
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

    uint8_t offset = max<int8_t> (alignment, sizeof (void *));

    void *mem = calloc (mem_size + offset, 1);
    if (!mem)
    {
        cerr << "ERROR: Failed to allocate Hash " << (mem_size >> 20) << " MB..." << endl;
        Engine::exit (EXIT_FAILURE);
    }

    std::cout << "info string Hash size " << (mem_size >> 20) << " MB..." << std::endl;

    void **ptr =
        //(void **) (uintptr_t (mem) + sizeof (void *) + (alignment - ((uintptr_t (mem) + sizeof (void *)) & uintptr_t (alignment - 1))));
        (void **) ((uintptr_t (mem) + offset) & ~uintptr_t (alignment - 1));

    ptr[-1]     = mem;
    _hash_table = (TTEntry *) (ptr);

#endif

    ASSERT (0 == (uintptr_t (_hash_table) & (alignment - 1)));

}

// resize(mb) sets the size of the table, measured in mega-bytes.
// Transposition table consists of a power of 2 number of clusters and
// each cluster consists of CLUSTER_ENTRY number of entry.
uint32_t TranspositionTable::resize (uint32_t mem_size_mb, bool force)
{
    if (mem_size_mb < MIN_TT_SIZE) mem_size_mb = MIN_TT_SIZE;
    if (mem_size_mb > MAX_TT_SIZE) mem_size_mb = MAX_TT_SIZE;

    uint64_t mem_size      = uint64_t (mem_size_mb) << 20;
    uint64_t cluster_count = (mem_size) / sizeof (TTEntry[CLUSTER_ENTRY]);
    uint64_t   entry_count = uint64_t (CLUSTER_ENTRY) << scan_msq (cluster_count);

    ASSERT (scan_msq (entry_count) < MAX_HASH_BIT);

    mem_size  = entry_count * TENTRY_SIZE;
    
    if (force || entry_count != entries ())
    {
        free_aligned_memory ();

        alloc_aligned_memory (mem_size, CACHE_LINE_SIZE);
        
        _hash_mask = (entry_count - CLUSTER_ENTRY);
    }

    return (mem_size >> 20);
}

// store() writes a new entry in the transposition table.
// It contains folowing valuable information.
//  - key
//  - move.
//  - score.
//  - depth.
//  - bound.
//  - nodes.
// The lower order bits of position key are used to decide on which cluster the position will be placed.
// The upper order bits of position key are used to store in entry.
// When a new entry is written and there are no empty entries available in cluster,
// it replaces the least valuable of these entries.
// An entry e1 is considered to be more valuable than a entry e2
// * if e1 is from the current search and e2 is from a previous search.
// * if e1 & e2 is from a current search then EXACT bound is valuable.
// * if the depth of e1 is bigger than the depth of e2.
void TranspositionTable::store (Key key, Move move, Depth depth, Bound bound, uint16_t nodes, Value value, Value eval)
{
    uint32_t key32 = key >> 32; // 32 upper-bit of key inside cluster

    TTEntry *tte = get_cluster (key);
    // By default replace first entry
    TTEntry *rte = tte;

    for (uint8_t i = 0; i < CLUSTER_ENTRY; ++i, ++tte)
    {
        if (!tte->key () || tte->key () == key32) // Empty or Old then overwrite
        {
            // Preserve any existing TT move
            if (MOVE_NONE == move)
            {
                move = tte->move ();
            }

            rte = tte;
            break;
        }

        // Replace would be a no-op in this common case
        if (0 == i) continue;

        // Implement replacement strategy when a collision occurs
        int8_t gc;
        gc  = ((rte->gen () == _generation) ? +2 : 0);
        gc += ((tte->gen () == _generation)
            || (tte->bound () == BND_EXACT) ? -2 : 0);

        if (gc < 0) continue;
        if (gc > 0)
        {
            rte = tte;
            continue;
        }
        // gc == 0
        int8_t rc = ((tte->depth () < rte->depth ()) ? +1
                   : (tte->depth () > rte->depth ()) ? -1
                   : (tte->nodes () < rte->nodes ()) ? +1 : 0);

        if (rc > 0)
        {
            rte = tte;
            continue;
        }
    }

    rte->save (key32, move, depth, bound, (nodes >> 16), value, eval, _generation);
}

// retrieve() looks up the entry in the transposition table.
// Returns a pointer to the entry found or NULL if not found.
const TTEntry* TranspositionTable::retrieve (Key key) const
{
    uint32_t key32 = key >> 32;

    const TTEntry *tte = get_cluster (key);
    for (uint8_t i = 0; i < CLUSTER_ENTRY; ++i, ++tte)
    {
        if (tte->key () == key32) return tte;
    }
    return NULL;
}
