/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef TT_H_INCLUDED
#define TT_H_INCLUDED

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "misc.h"
#include "types.h"

namespace DON {

using uint08 = std::uint8_t;
using uint16 = std::uint16_t;

// Constants used to manipulate generation bits

// Number of bits reserved for data
constexpr inline uint08 DATA_BITS = 3;
// Increment for generation field
constexpr inline uint08 GENERATION_DELTA = 1u << DATA_BITS;
// Mask to pull out generation field
constexpr inline uint08 GENERATION_MASK = (0xFFu << DATA_BITS) & 0xFFu;
// Generation cycle length
constexpr inline uint16 GENERATION_CYCLE = 0xFFu + GENERATION_DELTA;

class TTUpdater;
class TranspositionTable;

// TTEntry struct is the 10 bytes transposition table entry, defined as below:
//
// key        16 bit
// depth       8 bit
// generation  5 bit
// data:       3 bit
//  - is_pv    1 bit
//  - bound    2 bit
// move       16 bit
// value      16 bit
// eval       16 bit
//
// These fields are in the same order as accessed by TT::probe(), since memory is fastest sequentially.
// Equally, the store order in save() matches this order.
struct TTEntry final {
   public:
    constexpr Move   move() const noexcept { return move16; }
    constexpr Depth  depth() const noexcept { return Depth(depth8 + DEPTH_OFFSET); }
    constexpr uint08 generation() const noexcept { return genData8 & GENERATION_MASK; }
    constexpr bool   is_pv() const noexcept { return bool(genData8 & 0x4); }
    constexpr Bound  bound() const noexcept { return Bound(genData8 & 0x3); }
    constexpr Value  value() const noexcept { return value16; }
    constexpr Value  eval() const noexcept { return eval16; }
    constexpr bool   occupied() const noexcept { return bool(depth8); }

    // Populates the TTEntry with a new node's data, possibly
    // overwriting an old position. The update is not atomic and can be racy.
    FORCE_INLINE void save(const Key16  k16,
                           const Depth  depth,
                           const bool   isPv,
                           const Bound  bound,
                           const Move   move,
                           const Value  value,
                           const Value  eval,
                           const uint08 gen) noexcept {

        // Preserve the old move if don't have a new one
        if (move != Move::None())
            move16 = move;

        // Overwrite less valuable entries (cheapest checks first)
        if (bound == BOUND_EXACT || k16 != key16             //
            || 4 + depth + 2 * isPv - DEPTH_OFFSET > depth8  //
            || relative_age(gen) != 0)
        {
            assert(depth > DEPTH_OFFSET);
            assert(depth <= std::numeric_limits<uint08>::max() + DEPTH_OFFSET);

            key16    = k16;
            depth8   = uint08(depth - DEPTH_OFFSET);
            genData8 = uint08(gen | (4 * isPv) | bound);
            value16  = value;
            eval16   = eval;
        }
    }

    // The returned age is a multiple of TranspositionTable::GENERATION_DELTA
    uint08 relative_age(uint08 gen) const noexcept {
        // Due to our packed storage format for generation and its cyclic
        // nature add GENERATION_CYCLE (256 is the modulus, plus what
        // is needed to keep the unrelated lowest n bits from affecting
        // the result) to calculate the entry age correctly even after
        // generation8 overflows into the next cycle.
        return (GENERATION_CYCLE - genData8 + gen) & GENERATION_MASK;
    }

    std::int16_t worth(uint08 gen) const noexcept { return depth8 - 2 * relative_age(gen); }

   private:
    Key16  key16;
    Move   move16;
    uint08 depth8;
    uint08 genData8;
    Value  value16;
    Value  eval16;

    friend class TTUpdater;
    friend class TranspositionTable;
};

static_assert(sizeof(TTEntry) == 10, "Unexpected TTEntry size");

constexpr inline std::uint8_t TT_CLUSTER_ENTRY_COUNT = 3;

// TTCluster consists of TT_CLUSTER_ENTRY_COUNT number of TTEntry.
// The size of a TTCluster should divide the size of a cache line for best performance,
// as the cache-line is prefetched when possible.
struct TTCluster final {
   public:
    TTEntry entry[TT_CLUSTER_ENTRY_COUNT];
   private:
    char padding[2];  // Pad to 32 bytes
};

static_assert(sizeof(TTCluster) == 32, "Unexpected TTCluster size");

struct TTProbe final {
   public:
    const bool     ttHit;
    TTEntry*       tte;
    TTEntry* const fte;
};

class TTUpdater final {
   public:
    TTUpdater() noexcept                            = delete;
    TTUpdater(const TTUpdater&) noexcept            = delete;
    TTUpdater(TTUpdater&&) noexcept                 = delete;
    TTUpdater& operator=(const TTUpdater&) noexcept = delete;
    TTUpdater& operator=(TTUpdater&&) noexcept      = delete;

    TTUpdater(TTEntry* tte_, TTEntry* const fte_, Key16 k16, std::int16_t ply, uint08 gen) noexcept :
        tte(tte_),
        fte(fte_),
        key16(k16),
        ssPly(ply),
        generation(gen) {}

    void update(const Depth depth, const bool isPv, const Bound bound, const Move move, const Value value, const Value eval) noexcept;

   private:
    TTEntry*           tte;
    TTEntry* const     fte;
    const Key16        key16;
    const std::int16_t ssPly;
    const uint08       generation;
};

class ThreadPool;

// TranspositionTable is an array of TTCluster, of size clusterCount.
// Each non-empty TTEntry contains information on exactly one position.
class TranspositionTable final {
   public:
    TranspositionTable() noexcept                                     = default;
    TranspositionTable(const TranspositionTable&) noexcept            = delete;
    TranspositionTable(TranspositionTable&&) noexcept                 = delete;
    TranspositionTable& operator=(const TranspositionTable&) noexcept = delete;
    TranspositionTable& operator=(TranspositionTable&&) noexcept      = delete;
    ~TranspositionTable() noexcept;

    void update_generation(bool update = true) noexcept {
        generation8 = update * generation8 + GENERATION_DELTA;
    }

    uint08 generation() const noexcept { return generation8; }

    void resize(std::size_t mbSize, ThreadPool& threads) noexcept;
    void init(ThreadPool& threads) noexcept;

    TTProbe probe(const Key key, const Key16 key16) const noexcept;

    // Prefetch the cache line which includes this key's entry
    void prefetch_entry(const Key key) const noexcept { prefetch(first_entry(key)); }

    std::uint16_t hashfull() const noexcept;

    bool save(const std::string& hashFile) const noexcept;
    bool load(const std::string& hashFile, ThreadPool& threads) noexcept;

   private:
    void free() noexcept;

    TTEntry* first_entry(const Key key) const noexcept {
        return &clusters[mul_hi64(key, clusterCount)].entry[0];
    }

    TTCluster*  clusters     = nullptr;
    std::size_t clusterCount = 0;
    uint08      generation8  = 0;  // Size must be not bigger than TTEntry::genData8
};

}  // namespace DON

#endif  // #ifndef TT_H_INCLUDED
