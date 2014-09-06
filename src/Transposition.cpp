#include "Transposition.h"

#include <string>
#include <fstream>
#include "BitScan.h"
#include "Engine.h"

Transpose::TranspositionTable  TT; // Global Transposition Table

namespace Transpose {


    using namespace std;

    const u08 TranspositionTable::TTEntrySize   = sizeof (TTEntry);   // 10

    const u08 TranspositionTable::TTClusterSize = sizeof (TTCluster); // 32

    const u32 TranspositionTable::BufferSize = 0x10000;

    const u08 TranspositionTable::MaxHashBit  = 36;
    // 4 MB
    const u32 TranspositionTable::MinTTSize   = 4;
    // 1048576 MB (1024 GB) (1 TB)
    const u32 TranspositionTable::MaxTTSize   = (U64(1) << (MaxHashBit-1 - 20)) * TTClusterSize;

    const u32 TranspositionTable::DefTTSize   = 16;

    bool TranspositionTable::ClearHash        = true;

    void TranspositionTable::alloc_aligned_memory (size_t mem_size, size_t alignment)
    {
        ASSERT (0 == (alignment & (alignment - 1)));
        ASSERT (0 == (mem_size  & (alignment - 1)));

    #ifdef LPAGES

        size_t offset = max (alignment-1, sizeof (void *));

        Memory::create_memory (_mem, mem_size, alignment);
        if (_mem != NULL)
        {
            void *ptr = (void *) ((uintptr_t(_mem) + offset) & ~uintptr_t(offset));
            _hash_table = (TTCluster *) (ptr);
            ASSERT (0 == (uintptr_t(_hash_table) & (alignment - 1)));
            return;
        }

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

        size_t offset = max (alignment, sizeof (void *));

        void *mem = calloc (mem_size + offset, 1);
        if (mem != NULL)
        {
            sync_cout << "info string Hash " << (mem_size >> 20) << " MB." << sync_endl;

            void **ptr = (void **) ((uintptr_t(mem) + offset) & ~uintptr_t(alignment - 1));
            ptr[-1]     = mem;
            _hash_table = (TTCluster *) (ptr);
            ASSERT (0 == (uintptr_t(_hash_table) & (alignment - 1)));
            return;
        }

        cerr << "ERROR: failed to allocate Hash " << (mem_size >> 20) << " MB." << endl;
    #endif
        
    }

    // resize(mb) sets the size of the table, measured in mega-bytes.
    // Transposition table consists of a power of 2 number of clusters and
    // each cluster consists of ClusterEntries number of entry.
    u32 TranspositionTable::resize (size_t mem_size_mb, bool force)
    {
        if (mem_size_mb < 1         ) mem_size_mb = 1;
        if (mem_size_mb > MaxTTSize) mem_size_mb = MaxTTSize;

        size_t mem_size = mem_size_mb << 20;
        u08 hash_bit    = BitBoard::scan_msq (mem_size / TTClusterSize);

        ASSERT (hash_bit < MaxHashBit);

        size_t cluster_count = size_t(1) << hash_bit;

        mem_size  = cluster_count * i32(TTClusterSize);

        if (force || cluster_count != _cluster_count)
        {
            free_aligned_memory ();

            alloc_aligned_memory (mem_size, TTClusterSize); // Cache Line Size

            if (_hash_table == NULL) return 0;

            _cluster_count = cluster_count;
            _cluster_mask  = cluster_count-1;
        }

        return u32(mem_size >> 20);
    }

    u32 TranspositionTable::auto_size (size_t mem_size_mb, bool force)
    {
        if (mem_size_mb == 0) mem_size_mb = MaxTTSize;

        size_t msize_mb;
        for (msize_mb = mem_size_mb; msize_mb != 0; msize_mb >>= 1)
        {
            if (resize (msize_mb, force)) return u32(msize_mb);
        }
        //if (!msize_mb)
        //{
        //    return resize (MinTTSize, force);
        //}
        Engine::exit (EXIT_FAILURE);
        return 0;
    }

    // store() writes a new entry in the transposition table.
    // It contains folowing valuable information.
    //  - Key
    //  - Move
    //  - Depth
    //  - Bound
    //  - Nodes
    //  - Value
    //  - Evaluation
    // The lower order bits of position key are used to decide on which cluster the position will be placed.
    // The upper order bits of position key are used to store in entry.
    // When a new entry is written and there are no empty entries available in cluster,
    // it replaces the least valuable of these entries.
    // An entry e1 is considered to be more valuable than a entry e2
    // * if e1 is from the current search and e2 is from a previous search.
    // * if e1 & e2 is from a current search then EXACT bound is valuable.
    // * if the depth of e1 is bigger than the depth of e2.
    void TranspositionTable::store (Key key, Move move, Depth depth, Bound bound, Value value, Value eval)
    {
        u16 key16    = (key >> (64-16)); // 16 upper-bit of key inside cluster
        TTEntry *fte = cluster_entry (key);
        TTEntry *rte = fte;
        for (TTEntry *ite = fte; ite < fte + ClusterEntries; ++ite)
        {
            if (ite->_key == 0)     // Empty entry? then write
            {
                ite->save (key16, move, value, eval, depth, bound, _generation);
                return;
            }
            if (ite->_key == key16) // Old entry? then overwrite
            {
                // Preserve any existing TT move
                if (move == MOVE_NONE && ite->_move != MOVE_NONE)
                {
                    move = Move(ite->_move);
                }
                ite->save (key16, move, value, eval, depth, bound, _generation);
                return;
            }
        
            //if (ite == fte) continue;

            // Implementation of replacement strategy when a collision occurs
            if ( ((ite->gen () == _generation || ite->bound () == BND_EXACT)
                - (rte->gen () == _generation)
                - (ite->_depth < rte->_depth)) < 0)
            {
                rte = ite;
            }
        }

        // By default replace first entry
        rte->save (key16, move, value, eval, depth, bound, _generation);
    }

    // retrieve() looks up the entry in the transposition table.
    // Returns a pointer to the entry found or NULL if not found.
    const TTEntry* TranspositionTable::retrieve (Key key) const
    {
        u16 key16    = (key >> (64-16));
        TTEntry *fte = cluster_entry (key);
        for (TTEntry *ite = fte; ite < fte + ClusterEntries; ++ite)
        {
            if (ite->_key == key16)
            {
                ite->_gen_bnd = _generation | ite->bound (); // Refresh
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

}
