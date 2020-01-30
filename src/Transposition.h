#pragma once

#include "Position.h"
#include "Type.h"
#include "Util.h"

/// Transposition::Entry needs 16 byte to be stored
///
///  Key        16 bits
///  Move       16 bits
///  Value      16 bits
///  Evaluation 16 bits
///  Depth      08 bits
///  Generation 05 bits
///  PV Node    01 bits
///  Bound      02 bits
///  ------------------
///  Total      80 bits = 10 bytes
class TEntry
{
private:
    u16 k16;
    u16 m16;
    i16 v16;
    i16 e16;
    u08 d08;
    u08 g08;

    friend class TCluster;

public:
    // "Generation" variable distinguish transposition table entries from different searches.
    static u08 Generation;

    TEntry() = default;

    Move       move() const { return Move (m16); }
    Value     value() const { return Value(v16); }
    Value      eval() const { return Value(e16); }
    Depth     depth() const { return Depth(d08 + DEP_OFFSET); }
    u08  generation() const { return u08  (g08 & 0xF8); }
    bool      is_pv() const { return 0 != (g08 & 0x04); }
    Bound     bound() const { return Bound(g08 & 0x03); }

    void save(u64 k, Move m, Value v, Value e, Depth d, Bound b, bool pv)
    {
        // Preserve more valuable entries
        if (   MOVE_NONE != m
            || k16 != (k >> 0x30))
        {
            m16 = u16(m);
        }
        u08 pb = u08(g08 & 0x07);
        if (   k16 != (k >> 0x30)
            || d08 < d - DEP_OFFSET + 4
            || BOUND_EXACT == b)
        {
            assert(d > DEP_OFFSET);

            k16 = u16(k >> 0x30);
            v16 = i16(v);
            e16 = i16(e);
            d08 = u08(d - DEP_OFFSET);
            pb  = u08(u08(pv) << 2 | b);
        }
        g08 = u08(Generation | pb);
    }
};

/// Size of Transposition entry (10 bytes)
static_assert (sizeof (TEntry) == 10, "Entry size incorrect");

constexpr u32 CacheLineSize = 64;

/// Transposition::Cluster needs 32 bytes to be stored
/// 10 x 3 + 2 = 32
class TCluster
{
public:
    // Cluster entry count
    static constexpr u08 EntryCount = 3;

    TEntry entries[EntryCount];
    char padding[2]; // Align to a divisor of the cache line size

    TCluster() = default;

    u32 fresh_entry_count() const;

    TEntry* probe(u16, bool&);
};

/// Size of Transposition cluster(32 bytes)
static_assert (CacheLineSize % sizeof (TCluster) == 0, "Cluster size incorrect");

/// Transposition::Table consists of a power of 2 number of clusters
/// and each cluster consists of Cluster::EntryCount number of entry.
/// Each non-empty entry contains information of exactly one position.
/// Size of a cluster should divide the size of a cache line size,
/// to ensure that clusters never cross cache lines.
/// In case it is less, it should be padded to guarantee always aligned accesses.
/// This ensures best cache performance, as the cache line is pre-fetched.
class TTable
{
private:
    void alloc_aligned_memory(size_t, u32);
    void free_aligned_memory();

public:
    // Minimum size of Table (MB)
    static constexpr u32 MinHashSize = 4;
    // Maximum size of Table (MB)
    static constexpr u32 MaxHashSize =
#       if defined(BIT64)
            128 * 1024
#       else
            2 * 1024
#       endif
        ;

    static constexpr u32 BufferSize = 0x10000;

    void     *mem;
    TCluster *clusters;
    size_t    cluster_count;
    u16       hashfull_count;

    TTable()
        : mem(nullptr)
        , clusters(nullptr)
        , cluster_count(0)
        , hashfull_count(0)
    {}
    TTable(const TTable&) = delete;
    TTable& operator=(const TTable&) = delete;
    virtual ~TTable()
    {
        free_aligned_memory();
    }

    /// size() returns hash size in MB
    u32 size() const
    {
        return u32((u64(cluster_count) * sizeof (TCluster)) >> 20);
    }

    /// cluster() returns a pointer to the cluster of given a key.
    /// Lower 32 bits of the key are used to get the index of the cluster.
    TCluster* cluster(Key key) const
    {
        return &clusters[(u32(key) * u64(cluster_count)) >> 32];
    }

    u32 resize(u32);

    void auto_resize(u32);

    void clear();

    TEntry* probe(Key, bool&) const;

    u32 hash_full() const;

    Move extract_next_move(Position&, Move) const;

    void save(const std::string&) const;
    void load(const std::string&);

    template<typename Elem, typename Traits>
    friend std::basic_ostream<Elem, Traits>&
        operator<<(std::basic_ostream<Elem, Traits> &os, const TTable &tt)
    {
        u32 mem_size = tt.size();
        u08 dummy = 0;
        os.write((const Elem*)(&mem_size), sizeof (mem_size));
        os.write((const Elem*)(&dummy), sizeof (dummy));
        os.write((const Elem*)(&dummy), sizeof (dummy));
        os.write((const Elem*)(&dummy), sizeof (dummy));
        os.write((const Elem*)(&TEntry::Generation), sizeof (TEntry::Generation));
        for (size_t i = 0; i < tt.cluster_count / BufferSize; ++i)
        {
            os.write((const Elem*)(tt.clusters+i*BufferSize), sizeof (TCluster)*BufferSize);
        }
        return os;
    }

    template<typename Elem, typename Traits>
    friend std::basic_istream<Elem, Traits>&
        operator>>(std::basic_istream<Elem, Traits> &is, TTable &tt)
    {
        u32 mem_size;
        u08 dummy;
        is.read((Elem*)(&mem_size), sizeof (mem_size));
        is.read((Elem*)(&dummy), sizeof (dummy));
        is.read((Elem*)(&dummy), sizeof (dummy));
        is.read((Elem*)(&dummy), sizeof (dummy));
        is.read((Elem*)(&TEntry::Generation), sizeof (TEntry::Generation));
        tt.resize(mem_size);
        for (size_t i = 0; i < tt.cluster_count / BufferSize; ++i)
        {
            is.read((Elem*)(tt.clusters+i*BufferSize), sizeof (TCluster)*BufferSize);
        }
        return is;
    }
};

// Global Transposition Table
extern TTable TT;
