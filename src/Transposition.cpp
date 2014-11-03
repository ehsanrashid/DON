#include "Transposition.h"

#include <string>
#include <fstream>
#include "BitScan.h"
#include "Engine.h"

Transposition::TranspositionTable  TT; // Global Transposition Table

namespace Transposition {

    using namespace std;
    
    // Size of Transposition entry (bytes)
    // 16 bytes
    const u08 TranspositionTable::EntrySize   = sizeof (Entry);
    // Size of Transposition cluster in (bytes)  
    // 64 bytes
    const u08 TranspositionTable::ClusterSize = sizeof (Cluster);
    // Maximum bit of hash for cluster
    const u08 TranspositionTable::MaxHashBit  = 36;
    // Minimum size of Transposition table (mega-byte)
    // 4 MB
    const u32 TranspositionTable::MinSize     = 4;
    // Maximum size of Transposition table (mega-byte)
    // 2097152 MB (2048 GB) (2 TB)
    const u32 TranspositionTable::MaxSize     = (U64(1) << (MaxHashBit-1 - 20)) * ClusterSize;
    // Defualt size of Transposition table (mega-byte)
    const u32 TranspositionTable::DefSize     = 16;
    const u32 TranspositionTable::BufferSize  = 0x10000;

    bool TranspositionTable::ClearHash        = true;

    void TranspositionTable::alloc_aligned_memory (u64 mem_size, u32 alignment)
    {
        assert (0 == (alignment & (alignment - 1)));
        assert (0 == (mem_size  & (alignment - 1)));

    #ifdef LPAGES

        u32 offset = max (alignment-1, u32(sizeof (void *)));
       
        Memory::alloc_memory (_mem, mem_size, alignment);
        if (_mem != NULL)
        {
            void *ptr = reinterpret_cast<void*> ((u64(_mem) + offset) & ~u64(offset));
            _clusters = reinterpret_cast<Cluster*> (ptr);
            assert (0 == (u64(_clusters) & (alignment - 1)));
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

        u32 offset = max (alignment, u32(sizeof (void *)));

        void *mem = calloc (mem_size + offset, 1);
        if (mem != NULL)
        {
            sync_cout << "info string Hash " << (mem_size >> 20) << " MB." << sync_endl;

            void **ptr = reinterpret_cast<void**> ((u64(mem) + offset) & ~u64(alignment - 1));
            ptr[-1]    = mem;
            _clusters  = reinterpret_cast<Cluster*> (ptr);
            assert (0 == (u64(_clusters) & (alignment - 1)));
            return;
        }

        cerr << "ERROR: Hash allocate failed " << (mem_size >> 20) << " MB." << endl;
    #endif
        
    }

    // resize(mb) sets the size of the table, measured in mega-bytes.
    // Transposition table consists of a power of 2 number of clusters and
    // each cluster consists of ClusterEntries number of entry.
    u32 TranspositionTable::resize (u64 mem_size_mb, bool force)
    {
        if (mem_size_mb < MinSize) mem_size_mb = MinSize;
        if (mem_size_mb > MaxSize) mem_size_mb = MaxSize;

        u64 mem_size = mem_size_mb << 20;
        u08 hash_bit = BitBoard::scan_msq (mem_size / ClusterSize);

        assert (hash_bit < MaxHashBit);

        u64 cluster_count = u64(1) << hash_bit;

        mem_size  = cluster_count * i32(ClusterSize);

        if (force || cluster_count != _cluster_count)
        {
            free_aligned_memory ();

            alloc_aligned_memory (mem_size, ClusterSize); // Cache Line Size

            if (_clusters == NULL) return 0;

            _cluster_count = cluster_count;
            _cluster_mask  = cluster_count-1;
        }

        return u32(mem_size >> 20);
    }

    u32 TranspositionTable::auto_size (u64 mem_size_mb, bool force)
    {
        if (mem_size_mb == 0) mem_size_mb = MaxSize;

        for (u64 msize_mb = mem_size_mb; msize_mb >= MinSize; msize_mb >>= 1)
        {
            if (resize (msize_mb, force)) return u32(msize_mb);
        }
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
        Entry *fte = cluster_entry (key);
        for (Entry *ite = fte; ite < fte + ClusterEntries; ++ite)
        {
            if (ite->_key == 0)   // Empty entry? then write
            {
                ite->save (key, move, value, eval, depth, bound, _generation);
                return;
            }
            if (ite->_key == key) // Old entry? then overwrite
            {
                // Save preserving any existing TT move
                ite->save (key, move != MOVE_NONE ? move : Move(ite->_move), value, eval, depth, bound, _generation);
                return;
            }
        }

        Entry *rte = fte;
        for (Entry *ite = fte+1; ite < fte + ClusterEntries; ++ite)
        {
            // Implementation of replacement strategy when a collision occurs
            if ( ((ite->gen () == _generation || ite->bound () == BOUND_EXACT)
                - (rte->gen () == _generation)
                - (ite->_depth < rte->_depth)) < 0)
            {
                rte = ite;
            }
        }
        // By default replace first entry and make place in the last
        if (rte == fte)
        {
            memmove (fte, fte+1, (ClusterEntries - 1)*EntrySize);
            rte = fte + (ClusterEntries - 1);
        }
        rte->save (key, move, value, eval, depth, bound, _generation);
    }

    // retrieve() looks up the entry in the transposition table.
    // Returns a pointer to the entry found or NULL if not found.
    const Entry* TranspositionTable::retrieve (Key key) const
    {
        Entry *fte = cluster_entry (key);
        for (Entry *ite = fte; ite < fte + ClusterEntries; ++ite)
        {
            if (ite->_key == key)
            {
                ite->_gen_bnd = u08(_generation | ite->bound ()); // Refresh
                return ite;
            }
            if (ite->_key == 0) return NULL;
        }
        return NULL;
    }

    void TranspositionTable::save (string &hash_fn)
    {
        convert_path (hash_fn);
        if (white_spaces (hash_fn)) return;
        ofstream ofhash (hash_fn.c_str (), ios_base::out|ios_base::binary);
        if (!ofhash.is_open ()) return;
        ofhash << (*this);
        ofhash.close ();
        sync_cout << "info string Hash saved to file \'" << hash_fn << "\'." << sync_endl;
    }

    void TranspositionTable::load (string &hash_fn)
    {
        convert_path (hash_fn);
        if (white_spaces (hash_fn)) return;
        ifstream ifhash (hash_fn.c_str (), ios_base::in|ios_base::binary);
        if (!ifhash.is_open ()) return;
        ifhash >> (*this);
        ifhash.close ();
        sync_cout << "info string Hash loaded from file \'" << hash_fn << "\'." << sync_endl;
    }

}
