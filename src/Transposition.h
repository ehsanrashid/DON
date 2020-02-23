#pragma once

#include "Position.h"
#include "Type.h"

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
    Depth     depth() const { return Depth(d08 + DEPTH_OFFSET); }
    u08  generation() const { return u08  (g08 & 0xF8); }
    bool         pv() const { return 0 != (g08 & 0x04); }
    Bound     bound() const { return Bound(g08 & 0x03); }

    void save(u64, Move, Value, Value, Depth, Bound, bool);
};

/// Size of TEntry (10 bytes)
static_assert (sizeof (TEntry) == 10, "Entry size incorrect");

/// Transposition::Cluster needs 32 bytes to be stored
/// 10 x 3 + 2 = 32
class TCluster
{
public:
    // Cluster entry count
    static constexpr u08 EntryCount = 3;

    TEntry entryTable[EntryCount];
    char padding[2]; // Pad to 32 bytes

    TCluster() = default;

    u32 freshEntryCount() const;

    TEntry* probe(u16, bool&);
};

/// Size of TCluster (32 bytes)
static_assert (sizeof (TCluster) == 32, "Cluster size incorrect");

/// Transposition::Table is an array of Cluster, of size clusterCount.
/// Each cluster consists of EntryCount number of TTEntry.
/// Each TTEntry contains information on exactly one position.
/// The size of a Cluster should divide the size of a cache line for best performance,
/// as the cacheline is prefetched when possible.
class TTable
{
private:

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

    void     *mem;
    TCluster *clusterTable;
    u64       clusterCount;

    TTable();
    TTable(TTable const&) = delete;
    TTable& operator=(TTable const&) = delete;
    ~TTable();

    u32 size() const;

    TCluster* cluster(Key) const;

    u32 resize(u32);

    void autoResize(u32);

    void clear();

    TEntry* probe(Key, bool&) const;

    u32 hashFull() const;

    Move extractNextMove(Position&, Move) const;

    void save(std::string const&) const;
    void load(std::string const&);

    friend std::ostream& operator<<(std::ostream&, TTable const&);
    friend std::istream& operator>>(std::istream&, TTable      &);
};

// Global Transposition Table
extern TTable TT;
