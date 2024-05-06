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

#include <cstddef>
#include <cstdint>
#include <string>

#include "misc.h"
#include "types.h"

namespace DON {

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
struct TTEntry final {

    constexpr Move  move() const noexcept { return move16; }
    constexpr Depth depth() const noexcept { return Depth(depth8 + DEPTH_OFFSET); }
    constexpr bool  is_pv() const noexcept { return bool(genBound8 & 0x4); }
    constexpr Bound bound() const noexcept { return Bound(genBound8 & 0x3); }
    constexpr Value value() const noexcept { return Value(value16); }
    constexpr Value eval() const noexcept { return Value(eval16); }

    void
    save(Key k, Value v, bool pv, Bound b, Depth d, Move m, Value ev, std::uint8_t gen) noexcept;
    // The returned age is a multiple of TranspositionTable::GENERATION_DELTA
    std::uint8_t relative_age(std::uint8_t gen) const noexcept;

   private:
    std::uint16_t key16;
    std::uint8_t  depth8;
    std::uint8_t  genBound8;
    Move          move16;
    std::int16_t  value16;
    std::int16_t  eval16;

    friend class TranspositionTable;
};

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
    // Constants used to refresh the hash table periodically
    // Number of bits reserved for other things
    static constexpr std::uint8_t GENERATION_BITS = 3;
    // Increment for generation field
    static constexpr std::uint8_t GENERATION_DELTA = (1U << GENERATION_BITS);
    // Cycle length
    static constexpr std::uint16_t GENERATION_CYCLE = 0xFFU + GENERATION_DELTA;
    // Mask to pull out generation number
    static constexpr std::uint8_t GENERATION_MASK = (0xFFU << GENERATION_BITS) & 0xFFU;

    TranspositionTable()                                     = default;
    TranspositionTable(const TranspositionTable&)            = delete;
    TranspositionTable& operator=(const TranspositionTable&) = delete;
    ~TranspositionTable() noexcept;

    void update_generation(bool infinite = false) noexcept {
        generation8 = !infinite * generation8 + GENERATION_DELTA;
    }

    constexpr TTEntry* first_entry(Key key) const noexcept {
        return &table[mul_hi64(key, clusterCount)].entry[0];
    }

    constexpr auto generation() const noexcept { return generation8; }

    void resize(std::size_t mbSize, std::uint16_t threadCount) noexcept;
    void resize(std::size_t mbSize) noexcept;
    void clear() noexcept;

    TTEntry*      probe(Key key, bool& ttHit) const noexcept;
    std::uint16_t hashfull() const noexcept;

    bool save(const std::string& fname) const noexcept;
    bool load(const std::string& fname) noexcept;

   private:
    void free() noexcept;

    Cluster*      table        = nullptr;
    std::size_t   clusterCount = 0;
    std::uint8_t  generation8  = 0;  // Size must be not bigger than TTEntry::genBound8
    std::uint16_t _threadCount = 1;
};

}  // namespace DON

#endif  // #ifndef TT_H_INCLUDED
