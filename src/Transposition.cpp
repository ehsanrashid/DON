#include "Transposition.h"

#include <fstream>
#include "BitScan.h"
#include "Engine.h"

TranspositionTable  TT; // Global Transposition Table

using namespace std;

const u08 TranspositionTable::NUM_CLUSTER_ENTRY = 4;

const u08 TranspositionTable::TTENTRY_SIZE = sizeof (TTEntry);  // 16

const u08 TranspositionTable::TTCLUSTER_SIZE = NUM_CLUSTER_ENTRY*TTENTRY_SIZE;

const u32 TranspositionTable::BUFFER_SIZE = 0x10000;

const u08 TranspositionTable::MAX_HASH_BIT  = 32;

const u32 TranspositionTable::MIN_TT_SIZE   = 4;

const u32 TranspositionTable::MAX_TT_SIZE   = (U64 (1) << (MAX_HASH_BIT-1 - 20)) * TTENTRY_SIZE;

bool TranspositionTable::Clear_Hash = true;

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

    // Need to use malloc provided by C.
    // First need to allocate memory of mem_size + max (alignment, sizeof (void *)).
    // Need 'bytes' because user requested it.
    // Need to add 'alignment' because malloc can give us any address and
    // Need to find multiple of 'alignment', so at maximum multiple
    // of alignment will be 'alignment' bytes away from any location.
    // Need 'sizeof(void *)' for implementing 'aligned_free',
    // since returning modified memory pointer, not given by malloc, to the user,
    // must free the memory allocated by malloc not anything else.
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

    sync_cout << "info string Hash " << (mem_size >> 20) << " MB." << sync_endl;

    void **ptr = (void **) ((uintptr_t (mem) + offset) & ~uintptr_t (alignment - 1));
    
    ptr[-1]     = mem;
    _hash_table = (TTEntry *) (ptr);

#endif

    ASSERT (0 == (uintptr_t (_hash_table) & (alignment - 1)));

}

// resize(mb) sets the size of the table, measured in mega-bytes.
// Transposition table consists of a power of 2 number of clusters and
// each cluster consists of NUM_CLUSTER_ENTRY number of entry.
u32 TranspositionTable::resize (u32 mem_size_mb, bool force)
{
    if (mem_size_mb < MIN_TT_SIZE) mem_size_mb = MIN_TT_SIZE;
    if (mem_size_mb > MAX_TT_SIZE) mem_size_mb = MAX_TT_SIZE;

    u64 mem_size      = u64 (mem_size_mb) << 20;
    u32 cluster_count = u32 ((mem_size) / TTCLUSTER_SIZE);
    u32   entry_count = u32 (NUM_CLUSTER_ENTRY) << scan_msq (cluster_count);

    ASSERT (scan_msq (entry_count) < MAX_HASH_BIT);

    mem_size  = u64 (entry_count) * TTENTRY_SIZE;

    if (force || entry_count != entries ())
    {
        free_aligned_memory ();

        alloc_aligned_memory (mem_size, CACHE_LINE_SIZE);

        _hash_mask = (entry_count - NUM_CLUSTER_ENTRY);
    }

    return u32 (mem_size >> 20);
}

// store() writes a new entry in the transposition table.
// It contains folowing valuable information.
//  - Key
//  - Move
//  - Depth
//  - Bound
//  - Nodes
//  - Value
//  - Eval Value
// The lower order bits of position key are used to decide on which cluster the position will be placed.
// The upper order bits of position key are used to store in entry.
// When a new entry is written and there are no empty entries available in cluster,
// it replaces the least valuable of these entries.
// An entry e1 is considered to be more valuable than a entry e2
// * if e1 is from the current search and e2 is from a previous search.
// * if e1 & e2 is from a current search then EXACT bound is valuable.
// * if the depth of e1 is bigger than the depth of e2.
void TranspositionTable::store (Key key, Move move, Depth depth, Bound bound, u16 /*nodes*/, Value value, Value eval)
{
    u32 key32 = (key >> 32); // 32 upper-bit of key inside cluster
    TTEntry *fte = cluster_entry (key);
    TTEntry *lte = fte + NUM_CLUSTER_ENTRY - 1;
    TTEntry *rte = fte;
    for (TTEntry *ite = fte; ite <= lte; ++ite)
    {
        if (ite->_key == 0)     // Empty entry? then write
        {
            ite->save (key32, move, depth, bound, 0/*(nodes >> 10)*/, value, eval, _generation);
            return;
        }
        if (ite->_key == key32) // Old entry? then overwrite
        {
            // Preserve any existing TT move
            if (move == MOVE_NONE && ite->_move != MOVE_NONE)
            {
                move = Move (ite->_move);
            }
            ite->save (key32, move, depth, bound, 0/*(nodes >> 10)*/, value, eval, _generation);
            return;
        }
        
        if (ite == fte) continue;

        // Implement replacement strategy when a collision occurs
        if ( ((ite->_gen == _generation || ite->_bound == BND_EXACT)
            - (rte->_gen == _generation)
            - (ite->_depth < rte->_depth)) < 0)
        {
            rte = ite;
        }
    }

    // By default replace first entry
    rte->save (key32, move, depth, bound, 0/*(nodes >> 10)*/, value, eval, _generation);
}

// retrieve() looks up the entry in the transposition table.
// Returns a pointer to the entry found or NULL if not found.
const TTEntry* TranspositionTable::retrieve (Key key) const
{
    u32 key32 = (key >> 32);
    TTEntry *fte = cluster_entry (key);
    TTEntry *lte = fte + NUM_CLUSTER_ENTRY - 1;
    for (TTEntry *ite = fte; ite <= lte; ++ite)
    {
        if (ite->_key == key32)
        {
            ite->_gen = _generation;
            return ite;
        }
        if (ite->_key == 0) return NULL;
    }
    return NULL;
}

void TranspositionTable::save (string &hash_fn)
{
    convert_path (hash_fn);
    if (hash_fn.empty ()) return;
    ofstream ofhash (hash_fn.c_str (), ios_base::out|ios_base::binary);
    if (!ofhash.is_open ()) return;
    ofhash << (*this);
    ofhash.close ();
    sync_cout << "info string Hash saved to file \'" << hash_fn << "\'." << sync_endl;
}

void TranspositionTable::load (string &hash_fn)
{
    convert_path (hash_fn);
    if (hash_fn.empty ()) return;
    ifstream ifhash (hash_fn.c_str (), ios_base::in|ios_base::binary);
    if (!ifhash.is_open ()) return;
    ifhash >> (*this);
    ifhash.close ();
    sync_cout << "info string Hash loaded from file \'" << hash_fn << "\'." << sync_endl;
}
