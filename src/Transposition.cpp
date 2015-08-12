#include "Transposition.h"

#include <string>
#include <fstream>

#include "BitScan.h"
#include "Engine.h"

Transposition::TranspositionTable  TT; // Global Transposition Table

namespace Transposition {

    using namespace std;

    bool TranspositionTable::RetainHash = false;

    void TranspositionTable::alloc_aligned_memory (u64 mem_size, u32 alignment)
    {
        assert (0 == (alignment & (alignment-1)));
        assert (0 == (mem_size  & (alignment-1)));

    #ifdef LPAGES

        Memory::alloc_memory (_mem, mem_size, alignment);
        if (_mem != nullptr)
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
        if (mem != nullptr)
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

            if (_clusters == nullptr) return 0;

            _cluster_count = cluster_count;
            _cluster_mask  = cluster_count-1;
        }

        return u32(mem_size >> 20);
    }

    void TranspositionTable::auto_size (u64 mem_size_mb, bool force)
    {
        if (mem_size_mb == 0) mem_size_mb = MaxSize;

        for (u64 msize_mb = mem_size_mb; msize_mb >= MinSize; msize_mb >>= 1)
        {
            if (resize (msize_mb, force) != 0) return;
        }
        Engine::exit (EXIT_FAILURE);
    }

    // probe() looks up the entry in the transposition table.
    // Returns a pointer to the entry found or NULL if not found.
    Entry* TranspositionTable::probe (Key key, bool &hit) const
    {
        assert (key != U64(0));

        auto *const fte = cluster_entry (key);
        for (auto *ite = fte+0; ite < fte+ClusterEntryCount; ++ite)
        {
            if (ite->_key == U64(0) || ite->_key == key)
            {
                hit = ite->_key == key;
                if (hit) ite->_gen_bnd = u08(_generation | ite->bound ()); // Refresh
                return ite;
            }
        }

        auto *rte = fte;
        Depth rem = (rte->gen () == _generation)*MAX_DEPTH*DEPTH_ONE + (rte->bound () == BOUND_EXACT)*0x02*DEPTH_ONE + rte->depth ();
        for (auto *ite = fte+1; ite < fte+ClusterEntryCount; ++ite)
        {
            // Implementation of replacement strategy when a collision occurs
            Depth iem = (ite->gen() == _generation)*MAX_DEPTH*DEPTH_ONE + (ite->bound() == BOUND_EXACT)*0x02*DEPTH_ONE + ite->depth();
            if (rem > iem)
            {
                rem = iem;
                rte = ite;
            }
        }
        //// By default replace first entry and make place in the last
        //if (rte == fte)
        //{
        //    copy (fte+1, fte+ClusterEntryCount, fte);
        //    rte = fte+ClusterEntryCount-1;
        //}

        hit = false;
        return rte;
    }

    void TranspositionTable::save (string &hash_fn) const
    {
        ofstream ofhash (hash_fn, ios_base::out|ios_base::binary);
        if (!ofhash.is_open ()) return;
        ofhash << (*this);
        ofhash.close ();
        sync_cout << "info string Hash saved to file \'" << hash_fn << "\'." << sync_endl;
    }

    void TranspositionTable::load (string &hash_fn)
    {
        ifstream ifhash (hash_fn, ios_base::in|ios_base::binary);
        if (!ifhash.is_open ()) return;
        ifhash >> (*this);
        ifhash.close ();
        sync_cout << "info string Hash loaded from file \'" << hash_fn << "\'." << sync_endl;
    }

}
