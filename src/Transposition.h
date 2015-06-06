#ifndef _TRANSPOSITION_H_INC_
#define _TRANSPOSITION_H_INC_

#include <cstring>
#include <cstdlib>

#include "Type.h"
#include "Thread.h"
#include "MemoryHandler.h"
#include "UCI.h"

namespace Transposition {

    // TTEntry needs 16 byte to be stored
    //
    //  Key--------- 64 bits
    //  Move-------- 16 bits
    //  Value------- 16 bits
    //  Evaluation-- 16 bits
    //  Depth------- 08 bits
    //  Generation-- 06 bits
    //  Bound------- 02 bits
    //  ====================
    //  Total-------128 bits = 16 bytes
    struct TTEntry
    {

    private:

        u64 _key;
        u16 _move;
        i16 _value;
        i16 _eval;
        i08 _depth;
        u08 _gen_bnd;

        friend class TranspositionTable;

    public:

        Move  move  () const { return Move (_move);  }
        Value value () const { return Value(_value); }
        Value eval  () const { return Value(_eval);  }
        Depth depth () const { return Depth(_depth); }
        Bound bound () const { return Bound(_gen_bnd & 0x03); }
        u08   gen   () const { return u08  (_gen_bnd & 0xFC); }

        void save (u64 k, Move m, Value v, Value e, Depth d, Bound b, u08 g)
        {
            // Preserve any existing move for the same key
            if (  m != MOVE_NONE
               || k != _key
               )
            {
                _move       = u16(m);
            }

            // Don't overwrite more valuable entries
            if (  k != _key
               || d > _depth - 2
               || g != gen ()
               || b == BOUND_EXACT
               )
            {
                _key        = u64(k);
                _value      = u16(v);
                _eval       = u16(e);
                _depth      = i08(d);
                _gen_bnd    = u08(g | b);
            }
        }

    };

    // A Transposition Table consists of a 2^power number of clusters
    // and each cluster consists of ClusterEntryCount number of entry.
    // Each non-empty entry contains information of exactly one position.
    // Size of a cluster shall not be bigger than a CACHE_LINE_SIZE.
    // In case it is less, it should be padded to guarantee always aligned accesses.
    class TranspositionTable
    {

    private:

        // Cluster entries count
        static const u08 ClusterEntryCount = 4;

        // Cluster is a 64 bytes cluster of TT entries
        //
        // 4 x Entry (4 x 16 bytes)
        struct Cluster
        {
            TTEntry entries[ClusterEntryCount];
        };


    #ifdef LPAGES
        void    *_mem;
    #endif

        Cluster *_clusters;
        u64      _cluster_count;
        u64      _cluster_mask;
        u08      _generation;

        // alloc_aligned_memory() alocates the aligned memory
        void alloc_aligned_memory (u64 mem_size, u32 alignment);

        // free_aligned_memory() frees the aligned memory
        void free_aligned_memory ()
        {
            if (_clusters != nullptr)
            {

    #   ifdef LPAGES
                Memory::free_memory (_mem);
                _mem =
    #   else
                free (((void **) _clusters)[-1]);
    #   endif

                _clusters       = nullptr;
                _cluster_count  = 0;
                _cluster_mask   = 0;
                _generation     = 0;
            }
        }

    public:

        // Size of Transposition entry (bytes)
        // 16 bytes
        static const u08 EntrySize   = sizeof (TTEntry);
        static_assert (EntrySize == 16, "TTEntry size incorrect");

        // Size of Transposition cluster in (bytes)
        // 64 bytes
        static const u08 ClusterSize = sizeof (Cluster);
        static_assert (ClusterSize == 64, "Cluster size incorrect");

        // Maximum bit of hash for cluster
        static const u08 MaxHashBit  = 36;
        // Minimum size of Transposition table (mega-byte)
        // 4 MB
        static const u32 MinSize     = 4;
        // Maximum size of Transposition table (mega-byte)
        // 2097152 MB (2048 GB) (2 TB)
        static const u32 MaxSize     =
        #ifdef BIT64
            (U64(1) << (MaxHashBit-1 - 20)) * ClusterSize;
        #else
            2048;
        #endif
        // Defualt size of Transposition table (mega-byte)
        static const u32 DefSize     = 16;
        
        static const u32 BufferSize  = 0x10000;

        static bool ClearHash;

        TranspositionTable ()
            : _clusters (nullptr)
            , _cluster_count (0)
            , _cluster_mask (0)
            , _generation (0)
        {}

        explicit TranspositionTable (u32 mem_size_mb)
            : _clusters (nullptr)
            , _cluster_count (0)
            , _cluster_mask (0)
            , _generation (0)
        {
            resize (mem_size_mb, true);
        }

       ~TranspositionTable ()
        {
            free_aligned_memory ();
        }

        u64 entries () const
        {
            return (_cluster_count * ClusterEntryCount);
        }

        // size() returns hash size in MB
        u32 size () const
        {
            return u32((_cluster_count * ClusterSize) >> 20);
        }

        // clear() overwrites the entire transposition table with zeroes.
        // It is called whenever the table is resized,
        // or when the user asks the program to clear the table
        // 'ucinewgame' (from the UCI interface).
        void clear ()
        {
            if (ClearHash && _clusters != nullptr)
            {
                memset (_clusters, 0x00, _cluster_count * ClusterSize);
                _generation = 0;
                sync_cout << "info string Hash cleared." << sync_endl;
            }
        }

        // refresh() increments the "Generation" variable, which is used to
        // distinguish transposition table entries from different searches.
        // It is called at the beginning of every new search.
        void refresh () { _generation += 4; }
        
        u08 generation () const { return _generation; }

        // cluster_entry() returns a pointer to the first entry of a cluster given a position.
        // The lower order bits of the key are used to get the index of the cluster inside the table.
        TTEntry* cluster_entry (const Key key) const
        {
            return _clusters[key & _cluster_mask].entries;
        }

        // hash_full() returns an approximation of the per-mille of the 
        // all transposition entries during a search which have received
        // at least one write during the current search.
        // It is used to display the "info hashfull ..." information in UCI.
        // "the hash is <x> permill full", the engine should send this info regularly.
        // hash, are using <x>%. of the state of full.
        u32 hash_full () const
        {
            u32 full_entry_count = 0;
            for (const Cluster *clt = _clusters; clt < _clusters + 1000/ClusterEntryCount; ++clt)
            {
                const TTEntry *fte = clt->entries;
                for (const TTEntry *ite = fte; ite < fte + ClusterEntryCount; ++ite)
                {
                    if (ite->gen () == _generation)
                    {
                        ++full_entry_count;
                    }
                }
            }
            return full_entry_count;
        }

        u32 resize (u64 mem_size_mb, bool force = false);

        u32 resize () { return resize (size (), true); }

        void auto_size (u64 mem_size_mb, bool force = false);

        TTEntry* probe (Key key, bool &hit) const;

        void save (std::string &hash_fn) const;
        void load (std::string &hash_fn);

        template<class CharT, class Traits>
        friend std::basic_ostream<CharT, Traits>&
            operator<< (std::basic_ostream<CharT, Traits> &os, const TranspositionTable &tt)
        {
            u32 mem_size_mb = tt.size ();
            u08 dummy = 0;
            os.write (reinterpret_cast<const CharT*> (&mem_size_mb)   , sizeof (mem_size_mb));
            os.write (reinterpret_cast<const CharT*> (&dummy), sizeof (dummy));
            os.write (reinterpret_cast<const CharT*> (&dummy), sizeof (dummy));
            os.write (reinterpret_cast<const CharT*> (&dummy), sizeof (dummy));
            os.write (reinterpret_cast<const CharT*> (&tt._generation), sizeof (tt._generation));
            os.write (reinterpret_cast<const CharT*> (&tt._cluster_count) , sizeof (tt._cluster_count));
            u32 cluster_bulk = u32(tt._cluster_count / BufferSize);
            for (u32 i = 0; i < cluster_bulk; ++i)
            {
                os.write (reinterpret_cast<const CharT*> (tt._clusters+i*BufferSize), ClusterSize*BufferSize);
            }
            return os;
        }

        template<class CharT, class Traits>
        friend std::basic_istream<CharT, Traits>&
            operator>> (std::basic_istream<CharT, Traits> &is,       TranspositionTable &tt)
        {
            u32 mem_size_mb;
            u08 generation;
            u08 dummy;
            is.read (reinterpret_cast<CharT*> (&mem_size_mb)  , sizeof (mem_size_mb));
            is.read (reinterpret_cast<CharT*> (&dummy), sizeof (dummy));
            is.read (reinterpret_cast<CharT*> (&dummy), sizeof (dummy));
            is.read (reinterpret_cast<CharT*> (&dummy), sizeof (dummy));
            is.read (reinterpret_cast<CharT*> (&generation)   , sizeof (generation));
            is.read (reinterpret_cast<CharT*> (&tt._cluster_count), sizeof (tt._cluster_count));
            tt.resize (mem_size_mb);
            tt._generation = generation != 0 ? generation - 4 : 0;
            u32 cluster_bulk = u32(tt._cluster_count / BufferSize);
            for (u32 i = 0; i < cluster_bulk; ++i)
            {
                is.read (reinterpret_cast<CharT*> (tt._clusters+i*BufferSize), ClusterSize*BufferSize);
            }
            return is;
        }

    };

}

extern Transposition::TranspositionTable TT; // Global Transposition Table

#endif // _TRANSPOSITION_H_INC_
