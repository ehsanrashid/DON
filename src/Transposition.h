#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TRANSPOSITION_H_INC_
#define _TRANSPOSITION_H_INC_

#include <cstring>
#include <cstdlib>
#include <algorithm>

#include "Type.h"
#include "MemoryHandler.h"

#ifdef _MSC_VER
#   pragma warning (push)
#   pragma warning (disable : 4244)
#endif

// Transposition Entry needs the 16 byte to be stored
//
//  Key          4
//  Move         2
//  Depth        2
//  Bound        1
//  Generation   1
//  Nodes        2
//  Value        2
//  Eval Value   2
// ----------------
//  total        16 byte
struct TTEntry
{

private:

    u32 _key;
    u16 _move;
    i16 _depth;
    u08 _bound;
    u08 _gen;
    u16 _nodes;
    i16 _value;
    i16 _eval;

    friend class TranspositionTable;

public:

    u32   key   () const { return u32   (_key);   }
    Move  move  () const { return Move  (_move);  }
    Depth depth () const { return Depth (_depth); }
    Bound bound () const { return Bound (_bound); }
    u08   gen   () const { return u08   (_gen);   }
    u16   nodes () const { return u16   (_nodes); }
    Value value () const { return Value (_value); }
    Value eval  () const { return Value (_eval);  }

    void save (u32 k, Move m, Depth d, Bound b, u16 n, Value v, Value e, u08 g)
    {
        _key   = u32 (k);
        _move  = u16 (m);
        _depth = u16 (d);
        _bound = u08 (b);
        _nodes = u16 (n);
        _value = u16 (v);
        _eval  = u16 (e);
        _gen   = u08 (g);
    }

};

// A Transposition Table consists of a 2^power number of clusters
// and each cluster consists of CLUSTER_ENTRIES number of entry.
// Each non-empty entry contains information of exactly one position.
// Size of a cluster shall not be bigger than a CACHE_LINE_SIZE.
// In case it is less, it should be padded to guarantee always aligned accesses.
class TranspositionTable
{

private:

#ifdef LPAGES
    void    *_mem;
#endif

    TTEntry *_hash_table;
    u64      _hash_mask;
    u08      _generation;

    void alloc_aligned_memory (u64 mem_size, u08 alignment);

    // free_aligned_memory() free the allocated memory
    void free_aligned_memory ()
    {
        if (_hash_table != NULL)
        {

#   ifdef LPAGES
            MemoryHandler::free_memory (_mem);
            _mem =
#   else
            free (((void **) _hash_table)[-1]);
#   endif

            _hash_table = NULL;
            _hash_mask  = 0;
            _generation = 0;
            clear_hash  = false;
        }
    }

public:
    // Number of entries in a cluster
    static const u08 CLUSTER_ENTRIES;

    // Total size for Transposition entry in byte
    static const u08 TTENTRY_SIZE;

    // Maximum bit of hash for cluster
    static const u32 MAX_HASH_BIT;

    // Minimum size for Transposition table in mega-byte
    static const u32 MIN_TT_SIZE;
    // Maximum size for Transposition table in mega-byte
    // 524288 MB = 512 GB   -> 64 Bit
    // 032768 MB = 032 GB   -> 32 Bit
    static const u32 MAX_TT_SIZE;

    bool clear_hash;

    TranspositionTable ()
        : _hash_table (NULL)
        , _hash_mask (0)
        , _generation (0)
        , clear_hash (false)
    {}

    TranspositionTable (u32 mem_size_mb)
        : _hash_table (NULL)
        , _hash_mask (0)
        , _generation (0)
        , clear_hash (false)
    {
        resize (mem_size_mb, true);
    }

   ~TranspositionTable ()
    {
        free_aligned_memory ();
    }

    inline u64 entries () const
    {
        return (_hash_mask + CLUSTER_ENTRIES);
    }

    // Returns size in MB
    inline u32 size () const
    {
        return ((entries () * TTENTRY_SIZE) >> 20);
    }

    // clear() overwrites the entire transposition table with zeroes.
    // It is called whenever the table is resized,
    // or when the user asks the program to clear the table
    // 'ucinewgame' (from the UCI interface).
    inline void clear ()
    {
        if (clear_hash && _hash_table != NULL)
        {
            memset (_hash_table, 0x00, entries () * TTENTRY_SIZE);
            _generation = 0;
            std::cout << "info string Hash cleared." << std::endl;
        }
        clear_hash = false;
    }

    inline void master_clear ()
    {
        clear_hash = true;
        clear ();
    }

    // new_gen() is called at the beginning of every new search.
    // It increments the "Generation" variable, which is used to distinguish
    // transposition table entries from previous searches from entries from the current search.
    inline void new_gen () { ++_generation; }

    // cluster_entry() returns a pointer to the first entry of a cluster given a position.
    // The upper order bits of the key are used to get the index of the cluster.
    inline TTEntry* cluster_entry (const Key key) const
    {
        return _hash_table + (key & _hash_mask);
    }

    // permill_full() returns an approximation of the per-mille of the 
    // all transposition entries during a search which have received
    // at least one write during the current search.
    // It is used to display the "info hashfull ..." information in UCI.
    // "the hash is <x> permill full", the engine should send this info regularly.
    // hash, are using <x>%. of the state of full.
    inline u16 permill_full () const
    {
        u32 full_count = 0;
        return full_count;      // TODO::
        const TTEntry *tte = _hash_table;
        u16 total_count = std::min (10000, i32 (entries ()));
        for (u16 i = 0; i < total_count; ++i, ++tte)
        {
            if (tte->_gen == _generation)
            {
                ++full_count;
            }
        }

        return (full_count * 1000) / total_count;
    }

    u32 resize (u32 mem_size_mb, bool force = false);

    inline u32 resize () { return resize (size (), true); }

    // store() writes a new entry in the transposition table.
    void store (Key key, Move move, Depth depth, Bound bound, u16 nodes, Value value, Value eval);

    // retrieve() looks up the entry in the transposition table.
    const TTEntry* retrieve (Key key) const;

    template<class charT, class Traits>
    friend std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const TranspositionTable &tt)
    {
            u32 mem_size_mb = tt.size ();
            u08 dummy = 0;
            os.write ((const charT *) &mem_size_mb, sizeof (mem_size_mb));
            os.write ((const charT *) &TTENTRY_SIZE, sizeof (dummy));
            os.write ((const charT *) &CLUSTER_ENTRIES, sizeof (dummy));
            os.write ((const charT *) &dummy, sizeof (dummy));
            os.write ((const charT *) &tt._generation, sizeof (tt._generation));
            os.write ((const charT *) &tt._hash_mask, sizeof (tt._hash_mask));
            os.write ((const charT *)  tt._hash_table, u64 (mem_size_mb) << 20);
            return os;
    }

    template<class charT, class Traits>
    friend std::basic_istream<charT, Traits>&
        operator>> (std::basic_istream<charT, Traits> &is, TranspositionTable &tt)
    {
            u32 mem_size_mb;
            is.read ((charT *) &mem_size_mb, sizeof (mem_size_mb));
            u08 dummy;
            is.read ((charT *) &dummy, sizeof (dummy));
            is.read ((charT *) &dummy, sizeof (dummy));
            is.read ((charT *) &dummy, sizeof (dummy));
            is.read ((charT *) &dummy, sizeof (dummy));
            is.read ((charT *) &tt._hash_mask, sizeof (tt._hash_mask));
            tt.resize (mem_size_mb);
            tt._generation = dummy;
            is.read ((charT *)  tt._hash_table, u64 (mem_size_mb) << 20);
            return is;
    }

};

#ifdef _MSC_VER
#   pragma warning (pop)
#endif


extern TranspositionTable TT; // Global Transposition Table

#endif // _TRANSPOSITION_H_INC_
