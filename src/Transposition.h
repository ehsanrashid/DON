#ifndef _TRANSPOSITION_H_INC_
#define _TRANSPOSITION_H_INC_

#include <cstring>
#include <cstdlib>

#include "Type.h"
#include "MemoryHandler.h"
#include "UCI.h"

// Transposition Entry needs the 16 byte to be stored
//
//  Key--------->16 bits
//  Move-------->16 bits
//  Depth------->08 bits
//  Bound------->02 bits
//  Generation-->06 bits
//  Value------->16 bits
//  Eval Value-->16 bits
// ----------------
//  Total------->80 bits = 10 bytes
struct TTEntry
{

private:

    u16 _key;
    u16 _move;
    i16 _value;
    i16 _eval;
    u08 _depth;
    u08 _gen_bnd;

    friend class TranspositionTable;

public:

    inline Move  move  () const { return Move  (_move);  }
    inline Value value () const { return Value (_value); }
    inline Value eval  () const { return Value (_eval);  }
    inline Depth depth () const { return Depth (_depth) + DEPTH_NONE; }
    inline Bound bound () const { return Bound (_gen_bnd & 0x03); }
    inline u08   gen   () const { return u08   (_gen_bnd & 0xFC); }

    inline void save (u16 k, Move m, Value v, Value e, Depth d, Bound b, u08 g)
    {
        _key   = u16 (k);
        _move  = u16 (m);
        _value = u16 (v);
        _eval  = u16 (e);
        _depth = u08 (d - DEPTH_NONE);
        _gen_bnd = g | u08 (b);
    }

};

// Number of entries in a cluster
const u08 NUM_CLUSTER_ENTRY = 3;

// TTCluster is a 32 bytes cluster of TT entries consisting of:
//
// 3 x TTEntry (3 x 10 bytes)
// padding     (2 bytes)
struct TTCluster
{
    TTEntry entry[NUM_CLUSTER_ENTRY];
    u08     padding[2];
};

// A Transposition Table consists of a 2^power number of clusters
// and each cluster consists of NUM_CLUSTER_ENTRY number of entry.
// Each non-empty entry contains information of exactly one position.
// Size of a cluster shall not be bigger than a CACHE_LINE_SIZE.
// In case it is less, it should be padded to guarantee always aligned accesses.
class TranspositionTable
{

private:

#ifdef LPAGES
    void    *_mem;
#endif

    TTCluster *_hash_table;
    u32        _cluster_count;
    u08        _generation;

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

            _hash_table     = NULL;
            _cluster_count  = 0;
            _generation     = 0;
        }
    }

public:

    // Size for Transposition entry in byte
    static const u08 TTENTRY_SIZE;
    // Size for Transposition Cluster in byte  
    static const u08 TTCLUSTER_SIZE;

    static const u32 BUFFER_SIZE;

    // Maximum bit of hash for cluster
    static const u08 MAX_HASH_BIT;

    // Minimum size for Transposition table in mega-byte
    static const u32 MIN_TT_SIZE;
    // Maximum size for Transposition table in mega-byte
    // 524288 MB = 512 GB   -> 64 Bit
    // 032768 MB = 032 GB   -> 32 Bit
    static const u32 MAX_TT_SIZE;

    static bool Clear_Hash;

    TranspositionTable ()
        : _hash_table (NULL)
        , _cluster_count (0)
        , _generation (0)
    {}

    TranspositionTable (u32 mem_size_mb)
        : _hash_table (NULL)
        , _cluster_count (0)
        , _generation (0)
    {
        resize (mem_size_mb, true);
    }

   ~TranspositionTable ()
    {
        free_aligned_memory ();
    }

    //inline u64 entries () const
    //{
    //    return (_cluster_count * NUM_CLUSTER_ENTRY);
    //}

    // Returns size in MB
    inline u32 size () const
    {
        return u32 ((_cluster_count * TTCLUSTER_SIZE) >> 20);
    }

    // clear() overwrites the entire transposition table with zeroes.
    // It is called whenever the table is resized,
    // or when the user asks the program to clear the table
    // 'ucinewgame' (from the UCI interface).
    inline void clear ()
    {
        if (Clear_Hash && _hash_table != NULL)
        {
            memset (_hash_table, 0x00, _cluster_count * TTCLUSTER_SIZE);
            _generation = 0;
            sync_cout << "info string Hash cleared." << sync_endl;
        }
    }

    // new_gen() is called at the beginning of every new search.
    // It increments the "Generation" variable, which is used to distinguish
    // transposition table entries from previous searches from entries from the current search.
    inline void new_gen () { _generation += 4; }

    // cluster_entry() returns a pointer to the first entry of a cluster given a position.
    // The upper order bits of the key are used to get the index of the cluster inside the table.
    inline TTEntry* cluster_entry (const Key key) const
    {
        return _hash_table[u32 (key) & (_cluster_count - 1)].entry;
    }

    // permill_full() returns an approximation of the per-mille of the 
    // all transposition entries during a search which have received
    // at least one write during the current search.
    // It is used to display the "info hashfull ..." information in UCI.
    // "the hash is <x> permill full", the engine should send this info regularly.
    // hash, are using <x>%. of the state of full.
    inline u32 permill_full () const
    {
        u32 full_count = 0;
        const TTCluster *ttc = _hash_table;
        u32 total_count = std::min<u32> (10000, _cluster_count);
        for (u32 i = 0; i < total_count; ++i, ++ttc)
        {
            const TTEntry *tte = ttc->entry;
            if (tte->gen () == _generation)
            {
                ++full_count;
            }
        }

        return u32 ((full_count * 1000) / total_count);
    }

    u32 resize (u32 mem_size_mb, bool force = false);

    inline u32 resize () { return resize (size (), true); }

    // store() writes a new entry in the transposition table.
    void store (Key key, Move move, Depth depth, Bound bound, u16 nodes, Value value, Value eval);

    // retrieve() looks up the entry in the transposition table.
    const TTEntry* retrieve (Key key) const;

    void save (std::string &hash_fn);
    void load (std::string &hash_fn);

    template<class CharT, class Traits>
    friend std::basic_ostream<CharT, Traits>&
        operator<< (std::basic_ostream<CharT, Traits> &os, const TranspositionTable &tt)
    {
            u32 mem_size_mb = tt.size ();
            u08 dummy = 0;
            os.write ((const CharT *) &mem_size_mb   , sizeof (mem_size_mb));
            os.write ((const CharT *) &dummy, sizeof (dummy));
            os.write ((const CharT *) &dummy, sizeof (dummy));
            os.write ((const CharT *) &dummy, sizeof (dummy));
            os.write ((const CharT *) &dummy, sizeof (dummy));
            os.write ((const CharT *) &tt._cluster_count , sizeof (tt._cluster_count));
            os.write ((const CharT *) &dummy, sizeof (dummy));
            os.write ((const CharT *) &dummy, sizeof (dummy));
            os.write ((const CharT *) &dummy, sizeof (dummy));
            os.write ((const CharT *) &tt._generation, sizeof (tt._generation));
            u32 cluster_bulk = tt._cluster_count / BUFFER_SIZE;
            for (u32 i = 0; i < cluster_bulk; ++i)
            {
                os.write ((const CharT *) (tt._hash_table+i*BUFFER_SIZE), TTCLUSTER_SIZE*BUFFER_SIZE);
            }
            return os;
    }

    template<class CharT, class Traits>
    friend std::basic_istream<CharT, Traits>&
        operator>> (std::basic_istream<CharT, Traits> &is, TranspositionTable &tt)
    {
            u32 mem_size_mb;
            u08 generation;
            u08 dummy;
            is.read ((CharT *) &mem_size_mb  , sizeof (mem_size_mb));
            is.read ((CharT *) &dummy, sizeof (dummy));
            is.read ((CharT *) &dummy, sizeof (dummy));
            is.read ((CharT *) &dummy, sizeof (dummy));
            is.read ((CharT *) &dummy, sizeof (dummy));
            is.read ((CharT *) &tt._cluster_count, sizeof (tt._cluster_count));
            is.read ((CharT *) &dummy, sizeof (dummy));
            is.read ((CharT *) &dummy, sizeof (dummy));
            is.read ((CharT *) &dummy, sizeof (dummy));
            is.read ((CharT *) &generation   , sizeof (generation));
            tt.resize (mem_size_mb);
            tt._generation = (generation > 0 ? generation - 1 : 0);
            u32 cluster_bulk = tt._cluster_count / BUFFER_SIZE;
            for (u32 i = 0; i < cluster_bulk; ++i)
            {
                is.read ((CharT *) (tt._hash_table+i*BUFFER_SIZE), TTCLUSTER_SIZE*BUFFER_SIZE);
            }
            return is;
    }

};


extern TranspositionTable TT; // Global Transposition Table

#endif // _TRANSPOSITION_H_INC_
