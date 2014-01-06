#include "Transposition.h"

#include <iostream>
//#include <cmath>

#include "BitScan.h"
#include "Engine.h"

using namespace std;

// Global Transposition Table
TranspositionTable TT;

// resize(mb) sets the size of the table, measured in mega-bytes.
// Transposition table consists of a power of 2 number of clusters and
// each cluster consists of NUM_TENTRY_CLUSTER number of entry.
size_t TranspositionTable::resize (uint32_t size_mb)
{
    //ASSERT (size_mb >= SIZE_MIN_TT);
    //ASSERT (size_mb <= SIZE_MAX_TT);
    if (size_mb < SIZE_MIN_TT) size_mb = SIZE_MIN_TT;
    if (size_mb > SIZE_MAX_TT) size_mb = SIZE_MAX_TT;
    //{
    //    cerr << "ERROR: hash size too large " << size_mb << " MB..." << endl;
    //    return;
    //}

    size_t size_byte    = size_t (size_mb) << 20;
    uint32_t total_entry  = (size_byte) / SIZE_TENTRY;
    //uint32_t total_cluster  = total_entry / NUM_TENTRY_CLUSTER;

    uint8_t bit_hash = scan_msq (total_entry);
    ASSERT (bit_hash < MAX_BIT_HASH);
    if (bit_hash >= MAX_BIT_HASH) bit_hash = MAX_BIT_HASH - 1;

    total_entry     = uint32_t (1) << bit_hash;
    size_t size     = total_entry * SIZE_TENTRY;
    
    if (_hash_mask == (total_entry - NUM_TENTRY_CLUSTER)) return (size >> 20);

    erase ();

    aligned_memory_alloc (size, SIZE_CACHE_LINE); 

    _hash_mask      = (total_entry - NUM_TENTRY_CLUSTER);
    _stored_entry   = 0;
    _generation     = 0;

    return (size >> 20);
}

void TranspositionTable::aligned_memory_alloc (size_t size, uint32_t alignment)
{
    ASSERT (0 == (alignment & (alignment - 1)));

    // We need to use malloc provided by C.
    // First we need to allocate memory of size bytes + alignment + sizeof(void *).
    // We need 'bytes' because user requested it.
    // We need to add 'alignment' because malloc can give us any address and
    // we need to find multiple of 'alignment', so at maximum multiple
    // of alignment will be 'alignment' bytes away from any location.
    // We need 'sizeof(void *)' for implementing 'aligned_free',
    // since we are returning modified memory pointer, not given by malloc ,to the user,
    // we must free the memory allocated by malloc not anything else.
    // So storing address given by malloc just above pointer returning to user.
    // Thats why needed extra space to store that address.
    // Then checking for error returned by malloc, if it returns NULL then 
    // aligned_malloc will fail and return NULL or exit().

    uint32_t offset = 
        //(alignment - 1) + sizeof (void *);
        max<uint32_t> (alignment, sizeof (void *));

    void *mem = calloc (size + offset, 1);
    if (!mem)
    {
        cerr << "ERROR: hash failed to allocate " << size << " byte..." << endl;
        Engine::exit (EXIT_FAILURE);
    }

    void **ptr = 
        //(void **) (uintptr_t (mem) + sizeof (void *) + (alignment - ((uintptr_t (mem) + sizeof (void *)) & uintptr_t (alignment - 1))));
        (void **) ((uintptr_t (mem) + offset) & ~uintptr_t (alignment - 1));
    
    _hash_table = (TranspositionEntry*) (ptr);

    ASSERT (0 == (size & (alignment - 1)));
    ASSERT (0 == (uintptr_t (_hash_table) & (alignment - 1)));
    
    ptr[-1] = mem;
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
void TranspositionTable::store (Key key, Move move, Depth depth, Bound bound, uint16_t nodes, Value value, Value e_value)
{
    uint32_t key32 = uint32_t (key >> 32); // 32 upper-bit of key

    TranspositionEntry *te = get_cluster (key);
    // By default replace first entry
    TranspositionEntry *re = te;

    for (uint8_t i = 0; i < NUM_TENTRY_CLUSTER; ++i, ++te)
    {
        if (!te->key () || te->key () == key32) // Empty or Old then overwrite
        {
            // Do not overwrite when new type is EVAL_LOWER
            //if (te->key () && BND_LOWER == bound) return;

            // Preserve any existing TT move
            if (MOVE_NONE == move) move = te->move ();

            re = te;
            break;
        }
        else
        {
            // replace would be a no-op in this common case
            if (0 == i) continue;
        }

        // implement replacement strategy
        int8_t c1 = ((re->gen () == _generation) ? +2 : 0);
        int8_t c2 = ((te->gen () == _generation) || (te->bound () == BND_EXACT) ? -2 : 0);
        int8_t c3 = ((te->depth () < re->depth ()) ? +1 : 0);
        int8_t c4 = 0;//((te->nodes () < re->nodes ()) ? +1 : 0);

        if ((c1 + c2 + c3 + c4) > 0)
        {
            re = te;
        }
    }

    re->save (key32, move, depth, bound, _generation, nodes/1000, value, e_value);
    ++_stored_entry;
}

// retrieve() looks up the entry in the transposition table.
// Returns a pointer to the entry found or NULL if not found.
const TranspositionEntry* TranspositionTable::retrieve (Key key) const
{
    uint32_t key32 = uint32_t (key >> 32);
    const TranspositionEntry *te = get_cluster (key);
    for (uint8_t i = 0; i < NUM_TENTRY_CLUSTER; ++i, ++te)
    {
        if (te->key () == key32) return te;
    }
    return NULL;
}

// permill_full() returns the per-mille of the all transposition entries
// which have received at least one write during the current search.
// It is used to display the "info hashfull ..." information in UCI.
// "the hash is <x> permill full", the engine should send this info regularly.
// hash, are using <x>%. of the state of full.
double TranspositionTable::permill_full () const
{
    uint32_t total_entry = (_hash_mask + NUM_TENTRY_CLUSTER);

    //return (0 != total_entry) ?
    //    //(1 - exp (_stored_entry * log (1.0 - 1.0/total_entry))) * 1000 :
    //    (1 - exp (log (1.0 - double (_stored_entry) / double (total_entry)))) * 1000 :
    //    //exp (log (1000.0 + _stored_entry - total_entry)) :
    //    0.0;

    return (0 != total_entry) ?
        double (_stored_entry) * 1000 / double (total_entry) : 0.0;

}
