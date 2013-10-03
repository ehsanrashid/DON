//#pragma once
#ifndef TRANSPOSITION_H_
#define TRANSPOSITION_H_

#include <cstring>
#include <cstdlib>
#include "Type.h"

#pragma warning (push) 
#pragma warning (disable : 4244) 

// Transposition Entry needs the 16 byte to be stored
//
//  Key          4
//  Move         2
//  Depth        2
//  Bound        1
////  Nodes          2
//  Gen          1
//  Score        2
//  static value 2
//  static mrgin 2
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
    //uint16_t  _nodes;
    uint8_t  _gen;
    int16_t
        _score
        , _score_eval
        , _margin_eval
        ;

public:

    uint32_t  key () const { return uint32_t (_key); }
    Move     move () const { return Move (_move); }
    Depth   depth () const { return Depth (_depth); }
    Bound   bound () const { return Bound (_bound); }
    //uint16_t nodes () const { return uint16_t (_nodes); }
    uint8_t   gen () const { return _gen; }
    Score   score () const { return Score (_score); }
    //Score score_eval () const  { return Score (_score_eval); }
    //Score margin_eval () const { return Score (_margin_eval); }

    void save (
        uint32_t key   = U32 (0),
        Move     move  = (MOVE_NONE),
        Depth    depth = (DEPTH_ZERO),
        Bound    bound = (UNKNOWN),
        uint16_t nodes = (0),
        uint8_t  gen   = (0),
        Score    score = (SCORE_ZERO),
        Score    score_eval     = (SCORE_DRAW),
        Score    margin_eval    = (SCORE_DRAW))
    {
        _key   = key;
        _move  = move;
        _score = score;
        _depth = depth;
        _bound = bound;
        //_nodes  = nodes;
        _gen   = gen;
        //_score_eval = score_eval;
        //_margin_eval = margin_eval;
    }

    void gen (uint8_t gen)
    {
        _gen = gen;
    }

} TranspositionEntry;

#pragma pack (pop)

// A Transposition Table consists of a 2^power number of clusters
// and each cluster consists of NUM_TENTRY_CLUSTER number of Transposition Entry.
// Each non-empty entry contains information of exactly one position.
// Size of a cluster shall not be bigger than a SIZE_CACHE_LINE.
// In case it is less, it should be padded to guarantee always aligned accesses.
typedef class TranspositionTable sealed
{

private:

    void               *_mem;
    TranspositionEntry *_table_entry;
    uint64_t            _mask_hash;
    uint64_t            _store_entry;
    uint8_t             _generation;

    void aligned_memory_alloc (uint64_t size, uint32_t alignment);

    // erase() free the allocated memory
    void erase ()
    {
        if (_mem)
        {
            std::free (_mem);
            _mem = _table_entry = NULL;
            _mask_hash      = 0;
            _store_entry    = 0;
            _generation     = 0;
        }
    }

public:

    static const uint8_t SIZE_TENTRY        = sizeof (TranspositionEntry);  // 16
    // Number of entry in a cluster
    static const uint8_t NUM_TENTRY_CLUSTER = 0x04; // 4

    // Max power of hash for cluster
    static const uint8_t MAX_BIT_HASH       = 0x24; // 36
    // Max size for Transposition table in mega-byte
    // 524288 MB = 512 GB
    static const uint32_t MAX_SIZE_TT       = ((1) << (MAX_BIT_HASH - 20 - 1)) * SIZE_TENTRY;

    static const uint32_t SIZE_CACHE_LINE   = 0x40; // 64


    TranspositionTable ()
        : _mem (NULL)
        , _table_entry (NULL)
        , _mask_hash (0)
        , _store_entry (0)
        , _generation (0)
    {}

    TranspositionTable (uint32_t size_mb)
        : _mem (NULL)
        , _table_entry (NULL)
        , _mask_hash (0)
        , _store_entry (0)
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
        if (_table_entry)
        {
            std::memset (_table_entry, 0, size_t ((_mask_hash + NUM_TENTRY_CLUSTER) * SIZE_TENTRY));
            _store_entry    = 0;
            _generation     = 0;
        }
    }

    // inc_age() is called at the beginning of every new search.
    // It increments the "Generation" variable, which is used to distinguish
    // transposition table entries from previous searches from entries from the current search.
    void inc_age ()
    {
        _store_entry    = 0;
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
        return _table_entry + (uint32_t (key) & _mask_hash);
    }

    // store() writes a new entry in the transposition table.
    void store (Key key, Move move, Depth depth, Bound bound, Score score, uint16_t nodes);

    // retrieve() looks up the entry in the transposition table.
    const TranspositionEntry* TranspositionTable::retrieve (Key key) const;

    double permill_full () const;

} TranspositionTable;

#pragma warning (pop)


extern TranspositionTable TT;

#endif
