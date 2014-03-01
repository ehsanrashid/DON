#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TRANSPOSITION_H_INC_
#define _TRANSPOSITION_H_INC_

#include <cstring>
#include <cstdlib>

#include "Type.h"
#include "MemoryHandler.h"
#include "LeakDetector.h"

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

#ifdef _MSC_VER
//#   pragma pack( [ show ] | [ push | pop ] [, identifier ] , n  )
#   pragma pack (push, 2)
#endif

typedef struct TranspositionEntry
{

private:

    uint32_t _key;
    uint16_t _move;
    int16_t  _depth;
    uint8_t  _bound;
    uint8_t  _gen;
    uint16_t _nodes;
    int16_t  _value;
    int16_t  _eval;

public:

    uint32_t     key () const { return uint32_t (_key);   }
    Move        move () const { return Move     (_move);  }
    Depth      depth () const { return Depth    (_depth); }
    Bound      bound () const { return Bound    (_bound); }
    uint8_t      gen () const { return uint8_t  (_gen);   }
    uint16_t   nodes () const { return uint16_t (_nodes); }
    Value      value () const { return Value    (_value); }
    Value       eval () const { return Value    (_eval);  }

    void save (uint32_t k, Move m, Depth d, Bound b, uint16_t n, Value v, Value e, uint8_t g)
    {
        _key   = uint32_t (k);
        _move  = uint16_t (m);
        _depth = uint16_t (d);
        _bound =  uint8_t (b);
        _nodes = uint16_t (n);
        _value = uint16_t (v);
        _eval  = uint16_t (e);
        _gen   =  uint8_t (g);
    }

    void gen (uint8_t g)
    {
        _gen = g;
    }

} TranspositionEntry;

#ifdef _MSC_VER
#   pragma pack (pop)
#endif

// A Transposition Table consists of a 2^power number of clusters
// and each cluster consists of CLUSTER_SIZE number of entry.
// Each non-empty entry contains information of exactly one position.
// Size of a cluster shall not be bigger than a CACHE_LINE_SIZE.
// In case it is less, it should be padded to guarantee always aligned accesses.
typedef class TranspositionTable
{

private:

#ifdef LPAGES

    void               * _mem;

#endif

    TranspositionEntry *_hash_table;
    uint32_t            _hash_mask;
    uint8_t             _generation;

    void alloc_aligned_memory (uint64_t size, uint8_t alignment);

    // free_aligned_memory() free the allocated memory
    void free_aligned_memory ()
    {
        if (_hash_table)
        {

#   ifdef LPAGES

            MemoryHandler::free_memory (_mem);
            _mem =

#   else

            void *mem = ((void **) _hash_table)[-1];
            free (mem);
            mem =

#   endif

            _hash_table = NULL;

        }

        _hash_mask      = 0;
        _generation     = 0;
        clear_hash      = false;
    }

public:

    // Total size for Transposition entry in byte
    static const uint8_t  TENTRY_SIZE;
    // Number of entry in a cluster
    static const uint8_t  CLUSTER_SIZE;

    // Max power of hash for cluster
    static const uint32_t MAX_HASH_BIT;

    // Default size for Transposition table in mega-byte
    static const uint32_t DEF_TT_SIZE;

    // Minimum size for Transposition table in mega-byte
    static const uint32_t MIN_TT_SIZE;

    // Maximum size for Transposition table in mega-byte
    // 524288 MB = 512 GB   -> 64 Bit
    // 032768 MB = 032 GB   -> 32 Bit
    static const uint32_t MAX_TT_SIZE;

    bool clear_hash;


    TranspositionTable ()
        : _hash_table (NULL)
        , _hash_mask (0)
        , _generation (0)
        , clear_hash (false)
    {
        //resize (DEF_TT_SIZE, true);
    }

    TranspositionTable (uint32_t mem_size_mb)
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

    inline uint32_t size () const { return (uint64_t (_hash_mask + CLUSTER_SIZE) * TENTRY_SIZE) >> 20; }

    inline void master_clear ()
    {
        clear_hash = true;
        clear ();
    }

    // clear() overwrites the entire transposition table with zeroes.
    // It is called whenever the table is resized,
    // or when the user asks the program to clear the table
    // 'ucinewgame' (from the UCI interface).
    inline void clear ()
    {
        if (_hash_table && clear_hash)
        {
            uint64_t mem_size_b  = (_hash_mask + CLUSTER_SIZE) * TENTRY_SIZE;
            std::memset (_hash_table, 0, mem_size_b);

            _generation  = 0;
            std::cout << "info string Hash cleared." << std::endl;
        }
        clear_hash = false;
    }

    // new_gen() is called at the beginning of every new search.
    // It increments the "Generation" variable, which is used to distinguish
    // transposition table entries from previous searches from entries from the current search.
    inline void new_gen () { ++_generation; }

    // refresh() updates the 'Generation' of the entry to avoid aging.
    // Normally called after a TranspositionTable hit.
    inline void refresh (const TranspositionEntry &te) const
    {
        const_cast<TranspositionEntry&> (te).gen (_generation);
    }
    inline void refresh (const TranspositionEntry *te) const
    {
        const_cast<TranspositionEntry*> (te)->gen (_generation);
    }

    // get_cluster() returns a pointer to the first entry of a cluster given a position.
    // The upper order bits of the key are used to get the index of the cluster.
    inline TranspositionEntry* get_cluster (const Key key) const
    {
        return _hash_table + (uint32_t (key) & _hash_mask);
    }

    // permill_full() returns an approximation of the per-mille of the 
    // all transposition entries during a search which have received
    // at least one write during the current search.
    // It is used to display the "info hashfull ..." information in UCI.
    // "the hash is <x> permill full", the engine should send this info regularly.
    // hash, are using <x>%. of the state of full.
    inline uint16_t permill_full () const
    {
        uint16_t full_count = 0;
        uint16_t loop_count = std::min (U64 (10000), uint64_t (_hash_mask + CLUSTER_SIZE));
        for (uint16_t i = 0; i < loop_count; ++i)
        {
            if (_hash_table[i].gen () == _generation)
            {
                ++full_count;
            }
        }
        return (full_count * 1000) / loop_count;
    }

    uint32_t resize (uint32_t mem_size_mb, bool force = false);
    inline uint32_t resize ()
    {
        return resize (size (), true);
    }

    // store() writes a new entry in the transposition table.
    void store (Key key, Move move, Depth depth, Bound bound, uint16_t nodes, Value value, Value eval);

    // retrieve() looks up the entry in the transposition table.
    const TranspositionEntry* retrieve (Key key) const;

    template<class charT, class Traits>
    friend std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const TranspositionTable &tt)
    {
            uint32_t mem_size_mb = tt.size ();
            uint8_t dummy = 0;
            os.write ((const char *) &mem_size_mb, sizeof (mem_size_mb));
            os.write ((const char *) &TranspositionTable::TENTRY_SIZE, sizeof (dummy));
            os.write ((const char *) &TranspositionTable::CLUSTER_SIZE, sizeof (dummy));
            os.write ((const char *) &dummy, sizeof (dummy));
            os.write ((const char *) &tt._generation, sizeof (tt._generation));
            os.write ((const char *) &tt._hash_mask, sizeof (tt._hash_mask));
            os.write ((const char *) tt._hash_table, mem_size_mb << 20);
            return os;
    }

    template<class charT, class Traits>
    friend std::basic_istream<charT, Traits>&
        operator>> (std::basic_istream<charT, Traits> &is, TranspositionTable &tt)
    {
            uint32_t mem_size_mb;
            is.read ((char *) &mem_size_mb, sizeof (mem_size_mb));
            uint8_t dummy;
            is.read ((char *) &dummy, sizeof (dummy));
            is.read ((char *) &dummy, sizeof (dummy));
            is.read ((char *) &dummy, sizeof (dummy));
            is.read ((char *) &dummy, sizeof (dummy));
            is.read ((char *) &tt._hash_mask, sizeof (tt._hash_mask));
            tt.resize (mem_size_mb);
            tt._generation = dummy;
            is.read ((char *) tt._hash_table, mem_size_mb << 20);
            return is;
    }

} TranspositionTable;

#ifdef _MSC_VER
#   pragma warning (pop)
#endif

// Global Transposition Table
extern TranspositionTable TT;

#endif // _TRANSPOSITION_H_INC_
