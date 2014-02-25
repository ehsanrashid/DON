#include "Transposition.h"

#include "BitScan.h"
#include "Engine.h"

using namespace std;

const uint8_t  TranspositionTable::TENTRY_SIZE      = sizeof (TranspositionEntry);  // 16
const uint8_t  TranspositionTable::CLUSTER_SIZE     = 4;

#ifdef _64BIT
const uint32_t TranspositionTable::MAX_HASH_BIT     = 0x20; // 32
//const uint32_t TranspositionTable::MAX_HASH_BIT     = 0x24; // 36
#else
const uint32_t TranspositionTable::MAX_HASH_BIT     = 0x20; // 32
#endif

const uint32_t TranspositionTable::DEF_TT_SIZE      = 128;
const uint32_t TranspositionTable::MIN_TT_SIZE      = 4;

const uint32_t TranspositionTable::MAX_TT_SIZE      = (uint32_t (1) << (MAX_HASH_BIT - 20 - 1)) * TENTRY_SIZE;

const uint8_t  TranspositionTable::CACHE_LINE_SIZE  = 0x40; // 64


void TranspositionTable::aligned_memory_alloc (uint64_t mem_size_b, uint8_t alignment)
{
    ASSERT (0 == (alignment & (alignment - 1)));

    // We need to use malloc provided by C.
    // First we need to allocate memory of mem_size_b + max (alignment, sizeof (void *)).
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
    // aligned_memory_alloc will fail and return NULL or exit().

    uint8_t offset = max (alignment, uint8_t (sizeof (void *)));

    void *mem = calloc (mem_size_b + offset, 1);
    if (!mem)
    {
        cerr << "ERROR: hash failed to allocate " << mem_size_b << " byte..." << endl;
        Engine::exit (EXIT_FAILURE);
    }

    void **ptr = 
        //(void **) (uintptr_t (mem) + sizeof (void *) + (alignment - ((uintptr_t (mem) + sizeof (void *)) & uintptr_t (alignment - 1))));
        (void **) ((uintptr_t (mem) + offset) & ~uintptr_t (alignment - 1));

    _hash_table = (TranspositionEntry *) (ptr);

    ASSERT (0 == (mem_size_b & (alignment - 1)));
    ASSERT (0 == (uintptr_t (_hash_table) & (alignment - 1)));

    ptr[-1] = mem;
}

// resize(mb) sets the size of the table, measured in mega-bytes.
// Transposition table consists of a power of 2 number of clusters and
// each cluster consists of CLUSTER_SIZE number of entry.
uint32_t TranspositionTable::resize (uint32_t mem_size_mb)
{
    if (mem_size_mb < MIN_TT_SIZE) mem_size_mb = MIN_TT_SIZE;
    if (mem_size_mb > MAX_TT_SIZE) mem_size_mb = MAX_TT_SIZE;

    uint64_t mem_size_b   = uint64_t (mem_size_mb) << 20;
    uint32_t _entry_count = (mem_size_b) / TENTRY_SIZE;
    //uint32_t cluster_count = _entry_count / CLUSTER_SIZE;

    uint8_t bit_hash = scan_msq (_entry_count);
    ASSERT (bit_hash < MAX_HASH_BIT);
    if (bit_hash >= MAX_HASH_BIT) bit_hash = MAX_HASH_BIT - 1;

    _entry_count = uint32_t (1) << bit_hash;
    mem_size_b   = _entry_count * TENTRY_SIZE;

    if (_hash_mask != (_entry_count - CLUSTER_SIZE))
    {
        erase ();
        aligned_memory_alloc (mem_size_b, CACHE_LINE_SIZE); 
        _hash_mask  = (_entry_count - CLUSTER_SIZE);
    }

    return (mem_size_b >> 20);
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
    uint32_t key32 = uint32_t (key >> 32); // 32 upper-bit of key

    TranspositionEntry *te = get_cluster (key);
    // By default replace first entry
    TranspositionEntry *re = te;

    for (uint8_t i = 0; i < CLUSTER_SIZE; ++i, ++te)
    {
        if (!te->key () || te->key () == key32) // Empty or Old then overwrite
        {
            // Preserve any existing TT move
            if (MOVE_NONE == move)
            {
                move = te->move ();
            }

            re = te;
            break;
        }
        else
        {
            // Replace would be a no-op in this common case
            if (0 == i) continue;
        }

        // Implement replacement strategy when a collision occurs
        int8_t gc1 = ((re->gen () == _generation) ? +2 : 0);
        int8_t gc2 = ((te->gen () == _generation) || (te->bound () == BND_EXACT) ? -2 : 0);

        if ((gc1 + gc2) < 0) continue;

        int8_t rc = ((te->depth () < re->depth ()) ? +1
                   : (te->depth () > re->depth ()) ? -1
                   : (te->nodes () < re->nodes ()) ? +1 : 0);

        if ((rc) > 0)
        {
            re = te;
        }
    }

    if (!re->move () &&  move) ++_store_count;
    if ( re->move () && !move) --_store_count;

    re->save (key32, move, depth, bound, nodes/1000, value, eval, _generation);
}

// retrieve() looks up the entry in the transposition table.
// Returns a pointer to the entry found or NULL if not found.
const TranspositionEntry* TranspositionTable::retrieve (Key key) const
{
    uint32_t key32 = uint32_t (key >> 32);
    const TranspositionEntry *te = get_cluster (key);
    for (uint8_t i = 0; i < CLUSTER_SIZE; ++i, ++te)
    {
        if (te->key () == key32) return te;
    }
    return NULL;
}

// Global Transposition Table
TranspositionTable TT;
