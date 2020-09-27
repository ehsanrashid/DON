#pragma once

#include <algorithm>
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

public:
    //Key         key() const noexcept { return Key  (k16); }
    Depth     depth() const noexcept { return Depth(d08 + DEPTH_OFFSET); }

    u08  generation() const noexcept { return u08  (g08 & 248); }
    bool         pv() const noexcept { return bool (g08 & 4); }
    Bound     bound() const noexcept { return Bound(g08 & 3); }

    Value     value() const noexcept { return Value(v16); }
    Value      eval() const noexcept { return Value(e16); }
    Move       move() const noexcept { return Move (m16); }

    u16       worth() const noexcept { return d08 - ((263 + Generation - g08) & 248); }

    static void updateGeneration() noexcept;

    void refresh() noexcept;
    void save(Key, Move, Value, Value, Depth, Bound, bool) noexcept;

    // "Generation" variable distinguish transposition table entries from different searches.
    static u08 Generation;

private:
    u16 k16;
    u08 d08;
    u08 g08;
    u16 m16;
    i16 v16;
    i16 e16;

    friend struct TCluster;
};
/// Size of TEntry (10 bytes)
static_assert (sizeof (TEntry) == 10, "Entry size incorrect");

inline void TEntry::updateGeneration() noexcept {
    Generation += 8;
}

inline void TEntry::refresh() noexcept {
    g08 = u08(Generation | (g08 & 7));
}

inline void TEntry::save(Key k, Move m, Value v, Value e, Depth d, Bound b, bool pv) noexcept {

    // Preserve any existing move for the same position
    if (m != MOVE_NONE
     || u16(k) != k16) {
        m16 = u16(m);
    }
    // Overwrite less valuable entries
    if (b == BOUND_EXACT
     || u16(k) != k16
     || d - DEPTH_OFFSET + 4 > d08) {

        assert(d > DEPTH_OFFSET);
        assert(d < MAX_PLY);

        k16 = u16(k);
        d08 = u08(d - DEPTH_OFFSET);
        g08 = u08(Generation | u08(pv) << 2 | b);
        v16 = i16(v);
        e16 = i16(e);
    }
    assert(d08 != 0);
}

/// Transposition::Cluster needs 32 bytes to be stored
/// 10 x 3 + 2 = 32
struct TCluster {

    u32 freshEntryCount() const noexcept;

    TEntry* probe(u16, bool&) noexcept;

    static constexpr u08 EntryPerCluster{ 3 };

    TEntry entry[EntryPerCluster];
    char pad[2]; // Pad to 32 bytes
};
/// Size of TCluster (32 bytes)
static_assert (sizeof (TCluster) == 32, "Cluster size incorrect");

inline u32 TCluster::freshEntryCount() const noexcept {
    return std::count_if(std::begin(entry), std::end(entry),
                        [](auto const &e) noexcept {
                            return e.d08 != 0 && e.generation() == TEntry::Generation;
                        });
}

/// Transposition::Table is an array of Cluster, of size clusterCount.
/// Each cluster consists of EntryPerCluster number of TTEntry.
/// Each TTEntry contains information on exactly one position.
/// The size of a Cluster should divide the size of a cache line for best performance,
/// as the cacheline is prefetched when possible.
class TTable {

public:
    TTable() noexcept;
    TTable(TTable const&) = delete;
    TTable(TTable&&) = delete;
    TTable& operator=(TTable const&) = delete;
    TTable& operator=(TTable&&) = delete;
    ~TTable() noexcept;

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

    // Minimum size of Table (MB)
    static constexpr size_t MinHashSize{ 4 };
    // Maximum size of Table (MB)
    static constexpr size_t MaxHashSize{
#if defined(IS_64BIT)
                                        32 << 20
#else
                                        2 << 10
#endif
    };

private:

    TCluster *clusterTable;
    size_t    clusterCount;

    friend std::ostream& operator<<(std::ostream&, TTable const&);
    friend std::istream& operator>>(std::istream&, TTable      &);
};

inline u64 mul_hi64(u64 a, u64 b) noexcept {

#if defined(__GNUC__) && defined(IS_64BIT)
    __extension__ typedef unsigned __int128 u128;
    return ((u128)a * (u128)b) >> 64;
#else
    u64 const aL{ (u32)a }, aH{ a >> 32 };
    u64 const bL{ (u32)b }, bH{ b >> 32 };
    u64 const c1{ (aL * bL) >> 32 };
    u64 const c2{ aH * bL + c1 };
    u64 const c3{ aL * bH + (u32)c2 };
    return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif

}

/// cluster() returns a pointer to the cluster of given a key.
/// Lower 32 bits of the key are used to get the index of the cluster.
inline TCluster* TTable::cluster(Key posiKey) const noexcept {
    return &clusterTable[mul_hi64(posiKey, clusterCount)];
}

extern std::ostream& operator<<(std::ostream&, TTable const&);
extern std::istream& operator>>(std::istream&, TTable&);

// Global Transposition Table
extern TTable TT;
extern TTable TTEx;
