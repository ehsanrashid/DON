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

using UBit08 = std::uint8_t;
using UBit16 = std::uint16_t;
using Bit16  = std::int16_t;

// Constants used to manipulate generation bits

// Number of bits reserved for other things
constexpr inline UBit08 GENERATION_BITS = 3;
// Increment for generation field
constexpr inline UBit08 GENERATION_DELTA = 1U << GENERATION_BITS;
// Mask to pull out generation number
constexpr inline UBit08 GENERATION_MASK = (0xFFU << GENERATION_BITS) & 0xFFU;
// Generation cycle length
constexpr inline UBit16 GENERATION_CYCLE = 0xFFU + GENERATION_DELTA;

class TranspositionTable;

// TTEntry struct is the 10 bytes transposition table entry, defined as below:
//
// key        16 bit
// depth       8 bit
// generation  5 bit
// pv node     1 bit
// bound type  2 bit
// move       16 bit
// value      16 bit
// eval value 16 bit
//
// These fields are in the same order as accessed by TT::probe(), since memory is fastest sequentially.
// Equally, the store order in save() matches this order.
struct TTEntry final {

    constexpr Move   move() const noexcept { return move16; }
    constexpr Depth  depth() const noexcept { return Depth(depth8 + DEPTH_OFFSET); }
    constexpr bool   is_pv() const noexcept { return bool(genBound8 & 0x4); }
    constexpr Bound  bound() const noexcept { return Bound(genBound8 & 0x3); }
    constexpr Value  value() const noexcept { return Value(value16); }
    constexpr Value  eval() const noexcept { return Value(eval16); }
    constexpr bool   occupied() const noexcept { return depth8 != 0; }
    constexpr UBit08 generation() const noexcept { return genBound8 & GENERATION_MASK; }

    // Populates the TTEntry with a new node's data, possibly
    // overwriting an old position. The update is not atomic and can be racy.
    FORCE_INLINE void
    save(Key16 k16, Depth d, bool isPv, Bound b, Move m, Value v, Value ev, UBit08 gen) noexcept {

        // Preserve the old move if don't have a new one
        if (m != Move::None())
            move16 = m;

        // Overwrite less valuable entries (cheapest checks first)
        if (b == BOUND_EXACT || k16 != key16 || 4 + d + 2 * isPv - DEPTH_OFFSET > depth8
            || relative_age(gen) != 0)
        {
            assert(d > DEPTH_OFFSET);
            assert(d <= std::numeric_limits<UBit08>::max() + DEPTH_OFFSET);

            key16     = k16;
            depth8    = UBit08(d - DEPTH_OFFSET);
            genBound8 = UBit08(gen | (4 * isPv) | b);
            value16   = Bit16(v);
            eval16    = Bit16(ev);
        }
    }

    // The returned age is a multiple of TranspositionTable::GENERATION_DELTA
    UBit08 relative_age(UBit08 gen) const noexcept {
        // Due to our packed storage format for generation and its cyclic
        // nature add GENERATION_CYCLE (256 is the modulus, plus what
        // is needed to keep the unrelated lowest n bits from affecting
        // the result) to calculate the entry age correctly even after
        // generation8 overflows into the next cycle.
        return (GENERATION_CYCLE - genBound8 + gen) & GENERATION_MASK;
    }

    Bit16 worth(UBit08 gen) const noexcept { return depth8 - 2 * relative_age(gen); }

   private:
    Key16  key16;
    UBit08 depth8;
    UBit08 genBound8;
    Move   move16;
    Bit16  value16;
    Bit16  eval16;

    friend class TranspositionTable;
};

struct TTProbe final {
    bool const     ttHit;
    TTEntry* const tte;
};

struct TTUpdater final {
   public:
    TTUpdater(TTEntry* const            tte,
              const TranspositionTable& tt,
              Key16                     key16,
              std::int16_t              ply) noexcept :
        _tte(tte),
        _tt(tt),
        _key16(key16),
        _ply(ply) {}

    void update(Depth depth, bool isPv, Bound bound, Move move, Value value, Value eval) noexcept;

   private:
    TTEntry* const            _tte;
    const TranspositionTable& _tt;
    Key16                     _key16;
    std::int16_t              _ply;
};

class ThreadPool;

// TranspositionTable is an array of Cluster, of size clusterCount.
// Each cluster consists of EntryCount number of TTEntry.
// Each non-empty TTEntry contains information on exactly one position.
// The size of a Cluster should divide the size of a cache line for best performance,
// as the cache-line is prefetched when possible.
class TranspositionTable final {

    static constexpr std::uint8_t EntryCount = 3;

    struct Cluster final {
        TTEntry entry[EntryCount];
        char    padding[2];  // Pad to 32 bytes
    };

    static_assert(sizeof(Cluster) == 32, "Unexpected Cluster size");

   public:
    TranspositionTable()                                     = default;
    TranspositionTable(const TranspositionTable&)            = delete;
    TranspositionTable& operator=(const TranspositionTable&) = delete;
    ~TranspositionTable() noexcept;

    void update_generation(bool update = true) noexcept {
        generation8 = update * generation8 + GENERATION_DELTA;
    }

    UBit08 generation() const noexcept { return generation8; }

    void resize(std::size_t mbSize, ThreadPool& threads) noexcept;
    void init(ThreadPool& threads) noexcept;

    TTProbe probe(Key key) const noexcept;
    // Prefetch the cacheline which includes this key's entry
    void prefetch_entry(Key key) const noexcept { prefetch(first_entry(key)); }

    std::uint16_t hashfull() const noexcept;

    bool save(const std::string& hashFile) const noexcept;
    bool load(const std::string& hashFile, ThreadPool& threads) noexcept;

   private:
    void free() noexcept;

    TTEntry* first_entry(Key key) const noexcept {
        return &table[mul_hi64(key, clusterCount)].entry[0];
    }

    Cluster*    table        = nullptr;
    std::size_t clusterCount = 0;
    UBit08      generation8  = 0;  // Size must be not bigger than TTEntry::genBound8
};

}  // namespace DON

#endif  // #ifndef TT_H_INCLUDED
