#ifndef _TRANSPOSITION_H_INC_
#define _TRANSPOSITION_H_INC_

#include <cstring>
#include <cstdlib>

#include "Type.h"
#include "MemoryHandler.h"
#include "Thread.h"

namespace Transposition {

    extern const u08 CacheLineSize;

    // Transposition::Entry needs 16 byte to be stored
    //
    //  Key--------- 16 bits
    //  Move-------- 16 bits
    //  Value------- 16 bits
    //  Evaluation-- 16 bits
    //  Depth------- 08 bits
    //  Generation-- 06 bits
    //  Bound------- 02 bits
    //  ====================
    //  Total------- 80 bits = 10 bytes
    struct Entry
    {

    private:
        u16 _key16;
        u16 _move;
        i16 _value;
        i16 _eval;
        i08 _depth;
        u08 _gen_bnd;

        friend class Table;

    public:
        static const u08 Size;

        Move  move  () const { return Move (_move);  }
        Value value () const { return Value(_value); }
        Value eval  () const { return Value(_eval);  }
        Depth depth () const { return Depth(_depth); }
        Bound bound () const { return Bound(_gen_bnd &  BOUND_EXACT); }
        u08   gen   () const { return u08  (_gen_bnd & ~BOUND_EXACT); }

        void save (u64 k, Move m, Value v, Value e, Depth d, Bound b, u08 g)
        {
            // Preserve any existing move for the position (key)
            if (   m != MOVE_NONE
                || (k >> 0x30) != _key16
               )
            {
                _move       = u16(m);
            }
            // Don't overwrite more valuable entries
            if (   (k >> 0x30) != _key16
                || d > _depth - 2
             /* || g != gen () // Matching non-zero keys are already refreshed by probe() */
                || b == BOUND_EXACT
               )
            {
                _key16      = u64 (k >> 0x30);
                _value      = i16(v);
                _eval       = i16(e);
                _depth      = i08(d);
                _gen_bnd    = u08(g | b);
            }
        }
    };

    // Transposition::Cluster needs 32 bytes to be stored
    // 3 x 10 + 2
    struct Cluster
    {
    public:
        // Cluster entry count
        static const u08 EntryCount = 3;
        static const u08 Size;

        Entry entries[EntryCount];
        char padding[2]; // Align to a divisor of the cache line size
    };

    // Transposition::Table consists of a power of 2 number of clusters
    // and each cluster consists of Cluster::EntryCount number of entry.
    // Each non-empty entry contains information of exactly one position.
    // Size of a cluster should divide the size of a cache line size,
    // to ensure that clusters never cross cache lines.
    // In case it is less, it should be padded to guarantee always aligned accesses.
    // This ensures best cache performance, as the cacheline is prefetched.
    class Table
    {
    public:

    private:

    #ifdef LPAGES
        void    *_mem           = nullptr;
    #endif

        Cluster *_clusters      = nullptr;
        size_t   _cluster_count = 0;
        size_t   _cluster_mask  = 0;
        u08      _generation    = 0;

        // alloc_aligned_memory() alocates the aligned memory
        void alloc_aligned_memory (size_t mem_size, u32 alignment);

        // free_aligned_memory() frees the aligned memory
        void free_aligned_memory ()
        {
            if (_clusters != nullptr)
            {

    #   ifdef LPAGES
                Memory::free_memory (_mem);
                _mem            = nullptr;
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

        // Maximum bit of hash for cluster
        static const u08 MaxHashBit  = 36;
        // Defualt size of Transposition table (mega-byte)
        static const u32 DefSize     = 16;

        static const u32 MinSize;
        static const u32 MaxSize;

        static const u32 BufferSize  = 0x10000;

        bool retain_hash = false;

        Table () = default;

        explicit Table (u32 mem_size_mb)
        {
            resize (mem_size_mb, true);
        }

        ~Table ()
        {
            free_aligned_memory ();
        }

        size_t entries () const
        {
            return _cluster_count * Cluster::EntryCount;
        }

        // size() returns hash size in MB
        u32 size () const
        {
            return u32((_cluster_count * Cluster::Size) >> 20);
        }

        // clear() overwrites the entire transposition table with zeroes.
        // It is called whenever the table is resized,
        // or when the user asks the program to clear the table
        // 'ucinewgame' (from the UCI interface).
        void clear ()
        {
            if (!retain_hash && _clusters != nullptr)
            {
                std::memset (_clusters, 0x00, _cluster_count * Cluster::Size);
                _generation = 0;
                sync_cout << "info string Hash cleared." << sync_endl;
            }
        }

        // generation() set the "Generation" variable, which is used to
        // distinguish transposition table entries from different searches.
        // It is called at the beginning of every new search.
        void generation (i32 ply) { _generation = u08((ply << 2)&0xFC); }
        
        u08 generation () const { return _generation; }

        // cluster_entry() returns a pointer to the first entry of a cluster given a position.
        // The lower order bits of the key are used to get the index of the cluster inside the table.
        Entry* cluster_entry (Key key) const
        {
            return _clusters[size_t(key) & _cluster_mask].entries;
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
            for (const auto *clt = _clusters; clt < _clusters + 1000/Cluster::EntryCount; ++clt)
            {
                const auto *fte = clt->entries;
                for (const auto *ite = fte; ite < fte+Cluster::EntryCount; ++ite)
                {
                    if (ite->gen () == _generation)
                    {
                        ++full_entry_count;
                    }
                }
            }
            return full_entry_count;
        }

        u32 resize (u32 mem_size_mb, bool force = false);

        u32 resize () { return resize (size (), true); }

        void auto_size (u32 mem_size_mb, bool force = false);

        Entry* probe (Key key, bool &hit) const;

        void save (const std::string &hash_fn) const;
        void load (const std::string &hash_fn);

        template<class CharT, class Traits>
        friend std::basic_ostream<CharT, Traits>&
            operator<< (std::basic_ostream<CharT, Traits> &os, const Table &tt)
        {
            u32 mem_size_mb = tt.size ();
            u08 dummy = 0;
            os.write (reinterpret_cast<const CharT*> (&mem_size_mb), sizeof (mem_size_mb));
            os.write (reinterpret_cast<const CharT*> (&dummy), sizeof (dummy));
            os.write (reinterpret_cast<const CharT*> (&dummy), sizeof (dummy));
            os.write (reinterpret_cast<const CharT*> (&dummy), sizeof (dummy));
            os.write (reinterpret_cast<const CharT*> (&tt._generation), sizeof (tt._generation));
            os.write (reinterpret_cast<const CharT*> (&tt._cluster_count), sizeof (tt._cluster_count));
            u32 cluster_bulk = u32(tt._cluster_count / BufferSize);
            for (u32 i = 0; i < cluster_bulk; ++i)
            {
                os.write (reinterpret_cast<const CharT*> (tt._clusters+i*BufferSize), Cluster::Size*BufferSize);
            }
            return os;
        }

        template<class CharT, class Traits>
        friend std::basic_istream<CharT, Traits>&
            operator>> (std::basic_istream<CharT, Traits> &is,       Table &tt)
        {
            u32 mem_size_mb;
            u08 generation;
            u08 dummy;
            is.read (reinterpret_cast<CharT*> (&mem_size_mb), sizeof (mem_size_mb));
            is.read (reinterpret_cast<CharT*> (&dummy), sizeof (dummy));
            is.read (reinterpret_cast<CharT*> (&dummy), sizeof (dummy));
            is.read (reinterpret_cast<CharT*> (&dummy), sizeof (dummy));
            is.read (reinterpret_cast<CharT*> (&generation), sizeof (generation));
            is.read (reinterpret_cast<CharT*> (&tt._cluster_count), sizeof (tt._cluster_count));
            tt.resize (mem_size_mb);
            tt._generation = generation;
            u32 cluster_bulk = u32(tt._cluster_count / BufferSize);
            for (u32 i = 0; i < cluster_bulk; ++i)
            {
                is.read (reinterpret_cast<CharT*> (tt._clusters+i*BufferSize), Cluster::Size*BufferSize);
            }
            return is;
        }
    };

}

extern Transposition::Table TT; // Global Transposition Table

#endif // _TRANSPOSITION_H_INC_
