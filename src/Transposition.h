//#pragma once
#ifndef TRANSPOSITION_H_
#define TRANSPOSITION_H_

#include <iostream>
#include <cstdlib>
#include "Type.h"
//#include "LeakDetector.h"

#pragma warning (push)
#pragma warning (disable : 4244)

// Transposition Entry needs the 16 byte to be stored
//
//  Key          4
//  Move         2
//  Depth        2
//  Bound        1
//  Gen          1
////  Nodes          2
//  Score        2
//  eval value   2
//  eval mrgin   2
// ----------------
//  total        16 byte

//#pragma pack( [ show ] | [ push | pop ] [, identifier ] , n  )
#pragma pack (push, 2)
typedef struct TranspositionEntry sealed
{

private:

    uint32_t _key;
    uint16_t _move;
    int16_t  _depth;
    uint8_t  _bound;
    uint8_t  _gen;
    //uint16_t  _nodes;
    int16_t
        _value
        , _eval_value
        , _eval_margn
        ;

public:

    uint32_t  key () const { return uint32_t (_key); }
    Move     move () const { return Move (_move); }
    Depth   depth () const { return Depth (_depth); }
    Bound   bound () const { return Bound (_bound); }
    uint8_t   gen () const { return _gen; }
    //uint16_t nodes () const { return uint16_t (_nodes); }
    Value   value () const { return Value (_value); }
    Value eval_value () const { return Value (_eval_value); }
    Value eval_margn () const { return Value (_eval_margn); }

    void save (
        uint32_t key, Move move, Depth depth, Bound bound, uint8_t gen, 
        //uint16_t nodes,
        Value value, Value e_value, Value e_margn)
    {
        _key        = uint32_t (key);
        _move       = uint16_t (move);
        _depth      = uint16_t (depth);
        _bound      = uint8_t (bound);
        _gen        = uint8_t (gen);
        //_nodes      = uint16_t (nodes);
        _value      = uint16_t (value);
        _eval_value = uint16_t (e_value);
        _eval_margn = uint16_t (e_margn);
    }

    void gen (uint8_t gen)
    {
        _gen = gen;
    }

} TranspositionEntry;

#pragma pack (pop)

// A Transposition Table consists of a 2^power number of clusters
// and each cluster consists of NUM_TENTRY_CLUSTER number of entry.
// Each non-empty entry contains information of exactly one position.
// Size of a cluster shall not be bigger than a SIZE_CACHE_LINE.
// In case it is less, it should be padded to guarantee always aligned accesses.
typedef class TranspositionTable sealed
{

private:

    TranspositionEntry *_hash_table;
    uint64_t            _hash_mask;
    uint64_t            _stored_entry;
    uint8_t             _generation;

    void aligned_memory_alloc (size_t size, uint32_t alignment);

    // erase() free the allocated memory
    void erase ()
    {
        if (_hash_table)
        {
            void *mem = ((void **) _hash_table)[-1];
            free (mem);
            mem = _hash_table = NULL;
        }

        _hash_mask      = 0;
        _stored_entry   = 0;
        _generation     = 0;
    }

public:

    // Total size for Transposition entry in byte
    static const uint8_t SIZE_TENTRY        = sizeof (TranspositionEntry);  // 16
    // Number of entry in a cluster
    static const uint8_t NUM_TENTRY_CLUSTER = 4;

    // Max power of hash for cluster
#ifdef _64BIT
    static const uint8_t MAX_BIT_HASH       = 0x24; // 36
#else
    static const uint8_t MAX_BIT_HASH       = 0x20; // 32
#endif


    // Minimum size for Transposition table in mega-byte
    static const size_t DEF_SIZE_TT         = 128;

    // Minimum size for Transposition table in mega-byte
    static const size_t MIN_SIZE_TT         = 4;

    // Maximum size for Transposition table in mega-byte
    // 524288 MB = 512 GB   -> WIN64
    // 032768 MB = 032 GB   -> WIN32
    static const size_t MAX_SIZE_TT         = (size_t (1) << (MAX_BIT_HASH - 20 - 1)) * SIZE_TENTRY;

    static const uint32_t SIZE_CACHE_LINE   = 0x40; // 64


    TranspositionTable ()
        : _hash_table (NULL)
        , _hash_mask (0)
        , _stored_entry (0)
        , _generation (0)
    {
        //resize (DEF_SIZE_TT);
    }

    TranspositionTable (uint32_t size_mb)
        : _hash_table (NULL)
        , _hash_mask (0)
        , _stored_entry (0)
        , _generation (0)
    {
        resize (size_mb);
    }

    ~TranspositionTable ()
    {
        erase ();
    }

    void resize (uint32_t size_mb);

    // clear() overwrites the entire transposition table with zeroes.
    // It is called whenever the table is resized,
    // or when the user asks the program to clear the table
    // 'ucinewgame' (from the UCI interface).
    void clear ()
    {
        if (_hash_table)
        {
            size_t size_byte  = (_hash_mask + NUM_TENTRY_CLUSTER) * SIZE_TENTRY;
            std::memset (_hash_table, 0, size_byte);
            _stored_entry    = 0;
            _generation     = 0;
        }
    }

    // inc_age() is called at the beginning of every new search.
    // It increments the "Generation" variable, which is used to distinguish
    // transposition table entries from previous searches from entries from the current search.
    void inc_age ()
    {
        _stored_entry    = 0;
        ++_generation;
    }

    // refresh() updates the 'Generation' of the entry to avoid aging.
    // Normally called after a TranspositionTable hit.
    void refresh (const TranspositionEntry &te) const
    {
        const_cast<TranspositionEntry&> (te).gen (_generation);
    }
    void refresh (const TranspositionEntry *te) const
    {
        const_cast<TranspositionEntry*> (te)->gen (_generation);
    }

    // get_cluster() returns a pointer to the first entry of a cluster given a position.
    // The upper order bits of the key are used to get the index of the cluster.
    TranspositionEntry* get_cluster (Key key) const
    {
        return _hash_table + (key & _hash_mask);
    }

    // store() writes a new entry in the transposition table.
    void store (Key key, Move move, Depth depth, Bound bound, Value value, Value e_value, Value e_margn);

    // retrieve() looks up the entry in the transposition table.
    const TranspositionEntry* TranspositionTable::retrieve (Key key) const;

    double permill_full () const;

    template<class charT, class Traits>
    friend std::basic_ostream<charT, Traits>&
        operator<< (std::basic_ostream<charT, Traits> &os, const TranspositionTable &tt);
    template<class charT, class Traits>
    friend std::basic_istream<charT, Traits>&
        operator>> (std::basic_istream<charT, Traits> &is, TranspositionTable &tt);

} TranspositionTable;

#pragma warning (pop)


template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits> &os, const TranspositionTable &tt)
{
    size_t size_byte  = ((tt._hash_mask + TranspositionTable::NUM_TENTRY_CLUSTER) * TranspositionTable::SIZE_TENTRY);
    uint32_t size_mb  = size_byte >> 20;
    os.write ((char *) &size_mb, sizeof (size_mb));
    os.write ((char *) &TranspositionTable::SIZE_TENTRY, sizeof (TranspositionTable::SIZE_TENTRY));
    os.write ((char *) &TranspositionTable::NUM_TENTRY_CLUSTER, sizeof (TranspositionTable::NUM_TENTRY_CLUSTER));
    uint8_t dummy = 0;
    os.write ((char *) &dummy, sizeof (dummy));
    os.write ((char *) &tt._generation, sizeof (tt._generation));
    os.write ((char *) &tt._hash_mask, sizeof (tt._hash_mask));
    os.write ((char *) tt._hash_table, size_byte);
    return os;
}

template<class charT, class Traits>
inline std::basic_istream<charT, Traits>&
    operator>> (std::basic_istream<charT, Traits> &is, TranspositionTable &tt)
{
    uint32_t size_mb;
    is.read ((char *) &size_mb, sizeof (size_mb));
    uint8_t dummy;
    is.read ((char *) &dummy, sizeof (dummy));
    is.read ((char *) &dummy, sizeof (dummy));
    is.read ((char *) &dummy, sizeof (dummy));
    is.read ((char *) &dummy, sizeof (dummy));
    //is.read ((char *) &tt._generation, sizeof (tt._generation));
    is.read ((char *) &tt._hash_mask, sizeof (tt._hash_mask));
    tt.resize (size_mb);
    tt._generation = dummy;
    is.read ((char *) tt._hash_table, size_mb << 20);
    return is;
}

// Global Transposition Table
extern TranspositionTable TT;

#endif
