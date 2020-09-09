#pragma once

#include <string_view>

#include "Position.h"
#include "Type.h"

/// Transposition::Entry needs 16 byte to be stored
///
///  Key        16 bits
///  Depth      08 bits
///  Generation 05 bits
///  PV Node    01 bits
///  Bound      02 bits
///  Move       16 bits
///  Value      16 bits
///  Evaluation 16 bits

///  ------------------
///  Total      80 bits = 10 bytes
struct TEntry {

private:
    u16 k16;
    u08 d08;
    u08 g08;
    u16 m16;
    i16 v16;
    i16 e16;

    friend struct TCluster;

public:
    // "Generation" variable distinguish transposition table entries from different searches.
    static u08 Generation;

    //Key         key() const noexcept { return Key  (k16); }
    Depth     depth() const noexcept { return Depth(d08 + DEPTH_OFFSET); }

    u08  generation() const noexcept { return u08  (g08 & 248); }
    bool         pv() const noexcept { return bool (g08 & 4); }
    Bound     bound() const noexcept { return Bound(g08 & 3); }

    Value     value() const noexcept { return Value(v16); }
    Value      eval() const noexcept { return Value(e16); }
    Move       move() const noexcept { return Move (m16); }

    u16       worth() const noexcept { return d08 - ((263 + Generation - g08) & 248); }

    void refresh() noexcept;
    void save(Key, Move, Value, Value, Depth, Bound, u08) noexcept;
};

/// Size of TEntry (10 bytes)
static_assert (sizeof (TEntry) == 10, "Entry size incorrect");

/// Transposition::Cluster needs 32 bytes to be stored
/// 10 x 3 + 2 = 32
struct TCluster {

    static constexpr u08 EntryPerCluster{ 3 };

    TEntry entry[EntryPerCluster];
    char pad[2]; // Pad to 32 bytes

    u32 freshEntryCount() const noexcept;

    TEntry* probe(u16, bool&) noexcept;
};

/// Size of TCluster (32 bytes)
static_assert (sizeof (TCluster) == 32, "Cluster size incorrect");

/// Transposition::Table is an array of Cluster, of size clusterCount.
/// Each cluster consists of EntryPerCluster number of TTEntry.
/// Each TTEntry contains information on exactly one position.
/// The size of a Cluster should divide the size of a cache line for best performance,
/// as the cacheline is prefetched when possible.
class TTable {

private:
    void     *mem{ nullptr };
    TCluster *clusterTable{ nullptr };
    size_t    clusterCount{ 0 };

public:
    // Minimum size of Table (MB)
    static constexpr size_t MinHashSize{ 4 };
    // Maximum size of Table (MB)
#if defined(IS_64BIT)
    static constexpr size_t MaxHashSize{ 32 << 20 };
#else
    static constexpr size_t MaxHashSize{  2 << 10 };
#endif

    TTable() = default;
    TTable(TTable const&) = delete;
    TTable(TTable&&) = delete;
    TTable& operator=(TTable const&) = delete;
    TTable& operator=(TTable&&) = delete;
    ~TTable();

    u32 size() const noexcept;

    TCluster* cluster(Key) const noexcept;

    size_t resize(size_t);

    void autoResize(size_t);

    void clear();

    void free() noexcept;

    TEntry* probe(Key, bool&) const noexcept;

    u32 hashFull() const noexcept;

    Move extractNextMove(Position&, Move) const noexcept;

    void save(std::string_view) const;
    void load(std::string_view);

    friend std::ostream& operator<<(std::ostream&, TTable const&);
    friend std::istream& operator>>(std::istream&, TTable      &);
};

// Global Transposition Table
extern TTable TT;
extern TTable TTEx;
