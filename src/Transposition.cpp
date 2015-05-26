#include "Transposition.h"

#include <string>
#include <fstream>

#include "BitScan.h"
#include "Engine.h"

Transposition::TranspositionTable  TT; // Global Transposition Table

namespace Transposition {

    using namespace std;

    bool TranspositionTable::ClearHash        = true;

    void TranspositionTable::alloc_aligned_memory (u64 mem_size, u32 alignment)
    {
        assert (0 == (alignment & (alignment-1)));
        assert (0 == (mem_size  & (alignment-1)));

    #ifdef LPAGES

        Memory::alloc_memory (_mem, mem_size, alignment);
        if (_mem != NULL)
        {
            void *ptr = reinterpret_cast<void*> ((uintptr_t(_mem) + alignment-1) & ~u64(alignment-1));
            _clusters = reinterpret_cast<Cluster*> (ptr);
            assert (0 == (uintptr_t(_clusters) & (alignment-1)));
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

        alignment = max (u32(sizeof (void *)), alignment);

        void *mem = calloc (mem_size + alignment, 1);
        if (mem != NULL)
        {
            sync_cout << "info string Hash " << (mem_size >> 20) << " MB." << sync_endl;

            void **ptr = reinterpret_cast<void**> ((uintptr_t(mem) + alignment-1) & ~u64(alignment-1));
            ptr[-1]    = mem;
            _clusters  = reinterpret_cast<Cluster*> (ptr);
            assert (0 == (uintptr_t(_clusters) & (alignment-1)));
            return;
        }

        cerr << "ERROR: Hash allocate failed " << (mem_size >> 20) << " MB." << endl;
    #endif

    }

    // resize(mb) sets the size of the table, measured in mega-bytes.
    // Transposition table consists of a power of 2 number of clusters and
    // each cluster consists of ClusterEntryCount number of entry.
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

    // probe() looks up the entry in the transposition table.
    // Returns a pointer to the entry found or NULL if not found.
    TTEntry* TranspositionTable::probe (Key key, bool &hit) const
    {
        assert (key != U64(0));

        TTEntry *const fte = cluster_entry (key);
        for (TTEntry *ite = fte+0; ite < fte + ClusterEntryCount; ++ite)
        {
            if (ite->_key == U64(0))
            {
                return hit = false, ite;
            }
            if (ite->_key == key)
            {
                ite->_gen_bnd = u08(_generation | ite->bound ()); // Refresh
                
                return hit = true, ite;
            }
        }

        TTEntry *rte = fte;
        for (TTEntry *ite = fte+1; ite < fte + ClusterEntryCount; ++ite)
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
            memmove (fte, fte+1, (ClusterEntryCount - 1)*EntrySize);
            rte = fte + (ClusterEntryCount - 1);
        }

        return hit = false, rte;
    }

    void TranspositionTable::save (string &hash_fn) const
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
