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
struct TEntry {

private:
    u16 k16;
    u16 m16;
    i16 v16;
    i16 e16;
    u08 d08;
    u08 g08;

    friend struct TCluster;

public:
    // "Generation" variable distinguish transposition table entries from different searches.
    static u08 Generation;

    Move       move() const { return Move (m16); }
    Value     value() const { return Value(v16); }
    Value      eval() const { return Value(e16); }
    Depth     depth() const { return Depth(d08 + DEPTH_OFFSET); }
    Bound     bound() const { return Bound(g08 & 3); }
    bool         pv() const { return bool (g08 & 4); }
    u08  generation() const { return u08  (g08 & 248); }

    u16       worth() const { return d08 - ((263 + Generation - g08) & 248); }

    void refresh();
    void save(Key, Move, Value, Value, Depth, Bound, u08);
};

/// Size of TEntry (10 bytes)
static_assert (sizeof (TEntry) == 10, "Entry size incorrect");

/// Transposition::Cluster needs 32 bytes to be stored
/// 10 x 3 + 2 = 32
struct TCluster {

    // Cluster entry count
    static constexpr u08 EntryCount{ 3 };

    TEntry entryTable[EntryCount];
    char padding[2]; // Pad to 32 bytes

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
class TTable {

private:
    void     *mem{ nullptr };
    TCluster *clusterTable{ nullptr };
    u64       clusterCount{ 0 };

public:
    // Minimum size of Table (MB)
    static constexpr u32 MinHashSize{ 4 };
    // Maximum size of Table (MB)
#if defined(BIT64)
    static constexpr u32 MaxHashSize{ 128 << 10 };
#else
    static constexpr u32 MaxHashSize{   2 << 10 };
#endif

    TTable() = default;
    TTable(TTable const&) = delete;
    TTable(TTable&&) = delete;
    TTable& operator=(TTable const&) = delete;
    TTable& operator=(TTable&&) = delete;
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
extern TTable TTEx;
