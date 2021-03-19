#pragma once

#include <algorithm>
#include <string_view>

#include "position.h"
#include "type.h"

// Constants used to refresh the hash table periodically
constexpr int USED_BITS         = 3;                          // nb of bits reserved
constexpr int GENERATION_DELTA  = (1 << USED_BITS);           // increment for generation field
constexpr int GENERATION_CYCLE  = 0xFF + (1 << USED_BITS);    // cycle length
constexpr int GENERATION_MASK   = (0xFF << USED_BITS) & 0xFF; // mask to pull out generation number

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

    //Key            key() const noexcept { return Key(k16); }
    Depth        depth() const noexcept { return Depth(d08 + DEPTH_OFFSET); }

    uint8_t generation() const noexcept { return uint8_t(g08 & GENERATION_MASK); }
    bool          isPV() const noexcept { return bool   (g08 & 0x04); }
    Bound        bound() const noexcept { return Bound  (g08 & 0x03); }

    Value        value() const noexcept { return Value(v16); }
    Value         eval() const noexcept { return Value(e16); }

    Move          move() const noexcept { return Move(m16); }

    int32_t      worth() const noexcept { return d08 - ((GENERATION_CYCLE + Generation - g08) & GENERATION_MASK); }

    void       refresh() noexcept { g08 = uint8_t(Generation | (g08 & (GENERATION_DELTA - 1))); }

    void save(Key k, Move m, Value v, Value e, Depth d, Bound b, bool pv) noexcept {

        // Preserve any existing move for the same position
        if (m != MOVE_NONE
         || uint16_t(k) != k16) {
            m16 = uint16_t(m);
        }
        // Overwrite less valuable entries
        if (b == BOUND_EXACT
         || uint16_t(k) != k16
         || d - DEPTH_OFFSET > d08 - 4) {

            assert(d > DEPTH_OFFSET);
            assert(d < MAX_PLY);

            k16 = uint16_t(k);
            d08 = uint8_t(d - DEPTH_OFFSET);
            g08 = uint8_t(Generation | uint8_t(pv) << 2 | b);
            v16 = int16_t(v);
            e16 = int16_t(e);
        }
        assert(d08 != 0);
    }

    static void updateGeneration() noexcept {
        Generation += GENERATION_DELTA;
    }

    // "Generation" variable distinguish transposition table entries from different searches.
    static uint8_t Generation;

private:

    uint16_t    k16;
    uint8_t     d08;
    uint8_t     g08;
    uint16_t    m16;
    int16_t     v16;
    int16_t     e16;

    friend struct TCluster;
};
/// Size of TEntry (10 bytes)
static_assert(sizeof(TEntry) == 10, "Entry size incorrect");

/// Transposition::Cluster needs 32 bytes to be stored
/// 10 x 3 + 2 = 32
struct TCluster {

    uint32_t freshEntryCount() const noexcept {
        return std::count_if(std::begin(entry), std::end(entry),
            [](TEntry const &te) noexcept {
                return te.d08 != 0 && te.generation() == TEntry::Generation;
            });
    }

    TEntry* probe(const uint16_t, bool&) noexcept;

    static constexpr uint8_t EntryPerCluster{ 3 };

    TEntry entry[EntryPerCluster];
    char pad[2]; // Pad to 32 bytes
};
/// Size of TCluster (32 bytes)
static_assert(sizeof(TCluster) == 32, "Cluster size incorrect");

/// Transposition::Table is an array of Cluster, of size clusterCount.
/// Each cluster consists of EntryPerCluster number of TTEntry.
/// Each TTEntry contains information on exactly one position.
/// The size of a Cluster should divide the size of a cache line for best performance,
/// as the cacheline is prefetched when possible.
class TTable final {

public:

    constexpr TTable() noexcept;
    TTable(TTable const&) = delete;
    TTable(TTable&&) = delete;
    ~TTable() noexcept;

    TTable& operator=(TTable const&) = delete;
    TTable& operator=(TTable&&) = delete;

    uint32_t size() const noexcept;

    bool resize(size_t);

    void autoResize(size_t);

    void clear();

    void free() noexcept;

    TCluster* cluster(const Key) const noexcept;
    TEntry* probe(const Key, bool&) const noexcept;

    uint32_t hashFull() const noexcept;

    Move extractNextMove(Position&, Move) const noexcept;

    void save(std::string_view) const;
    void load(std::string_view);

    // Minimum size of Table (MB)
    static constexpr size_t MinHashSize{ 4 };
    // Maximum size of Table (MB)
#if defined(IS_64BIT)
    static constexpr size_t MaxHashSize{ 32 << 20 };
#else
    static constexpr size_t MaxHashSize{  2 << 10 };
#endif

private:

    TCluster *clusterTable;
    size_t    clusterCount;

    friend std::ostream& operator<<(std::ostream&, TTable const&);
    friend std::istream& operator>>(std::istream&, TTable      &);
};

constexpr uint64_t mul_hi64(uint64_t a, uint64_t b) noexcept {

#if defined(__GNUC__) && defined(IS_64BIT)
    __extension__ typedef unsigned __int128 uint128;
    return ((uint128)a * (uint128)b) >> 64;
#else
    uint64_t const aL{ (uint32_t)a }, aH{ a >> 32 };
    uint64_t const bL{ (uint32_t)b }, bH{ b >> 32 };
    uint64_t const c1{ (aL * bL) >> 32 };
    uint64_t const c2{ aH * bL + c1 };
    uint64_t const c3{ aL * bH + (uint32_t)c2 };
    return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif

}

/// TTable::cluster() returns a pointer to the cluster of given a key.
/// Lower 32 bits of the key are used to get the index of the cluster.
inline TCluster* TTable::cluster(const Key posiKey) const noexcept {
    return clusterTable + mul_hi64(posiKey, clusterCount);
}
/// TTable::probe() looks up the entry in the transposition table.
inline TEntry* TTable::probe(const Key posiKey, bool &hit) const noexcept {
    return cluster(posiKey)->probe(uint16_t(posiKey), hit);
}

extern std::ostream& operator<<(std::ostream&, TTable const&);
extern std::istream& operator>>(std::istream&, TTable&);

// Global Transposition Table
extern TTable TT;
