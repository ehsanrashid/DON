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
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>

#include "misc.h"
#include "types.h"

namespace DON {

// There is only one global hash table for the engine and all its threads.
// For chess in particular, even allow racy updates between threads to and from the TT,
// as taking the time to synchronize access would cost thinking time and thus elo.
// As a hash table, collisions are possible and may cause chess playing issues (bizarre blunders, faulty mate reports, etc).
// Fixing these also loses elo; however such risk decreases quickly with larger TT size.
//
// `probe` is the primary method: given a board position (key),
// lookup its entry in the table, and return TTProbe:
//   1) copy of the entry data (if any) (may be inconsistent due to read races)
//   2) pointer to this entry
//   3) pointer to this cluster
// The copied data and the updater are separated to maintain clear boundaries between local vs global objects.
// A copy of the data already in the entry (possibly collided). `probe` may be racy, resulting in inconsistent data.
struct TTData final {
   public:
    TTData() noexcept = delete;
    TTData(bool h, bool p, Bound b, const Move& m, Depth d, Value v, Value ev) noexcept :
        hit(h),
        pv(p),
        bound(b),
        padding(0),
        move(m),
        depth(d),
        value(v),
        eval(ev) {}

    bool         hit;
    bool         pv;
    Bound        bound;
    std::uint8_t padding;
    Move         move;
    Depth        depth;
    Value        value;
    Value        eval;
};

static_assert(sizeof(TTData) == 12, "Unexpected TTData size");

// Constants used to manipulate generation bits

// Number of bits reserved for data
constexpr inline std::uint8_t DATA_BITS = 3;
// Increment for generation field
constexpr inline std::uint8_t GENERATION_DELTA = 1 << DATA_BITS;
// Mask to pull out generation field
constexpr inline std::uint8_t GENERATION_MASK = (0xFF << DATA_BITS) & 0xFF;
// Generation cycle length
constexpr inline std::uint16_t GENERATION_CYCLE = 0xFF + GENERATION_DELTA;

class TTUpdater;
class TranspositionTable;

// TTEntry struct is the 10 bytes transposition table entry, defined as below:
//
// key        16 bit
// depth       8 bit
// generation  5 bit
// data        3 bit
//  - pv       1 bit
//  - bound    2 bit
// move       16 bit
// value      16 bit
// eval       16 bit
//
// These fields are in the same order as accessed by TT::probe(), since memory is fastest sequentially.
// Equally, the store order in save() matches this order.
struct TTEntry final {
   public:
    TTEntry() noexcept                          = delete;
    TTEntry(const TTEntry&) noexcept            = delete;
    TTEntry(TTEntry&&) noexcept                 = delete;
    TTEntry& operator=(const TTEntry&) noexcept = delete;
    TTEntry& operator=(TTEntry&&) noexcept      = delete;

    constexpr auto move() const noexcept { return move16; }

   private:
    constexpr auto occupied() const noexcept { return bool(depth8); }
    constexpr auto depth() const noexcept { return Depth(depth8 + DEPTH_OFFSET); }
    constexpr auto pv() const noexcept { return bool(genData8 & 0x4); }
    constexpr auto bound() const noexcept { return Bound(genData8 & 0x3); }
    constexpr auto generation() const noexcept { return std::uint8_t(genData8 & GENERATION_MASK); }
    constexpr auto value() const noexcept { return value16; }
    constexpr auto eval() const noexcept { return eval16; }

    // Convert internal bitfields to TTData
    TTData read() const noexcept {
        return {occupied(), pv(), bound(), move(), depth(), value(), eval()};
    }

    // Populates the TTEntry with a new node's data, possibly
    // overwriting an old position. The update is not atomic and can be racy.
    void save(Key16        k16,
              Depth        depth,
              bool         pv,
              Bound        bound,
              const Move&  move,
              Value        value,
              Value        eval,
              std::uint8_t gen) noexcept {
        assert(depth > DEPTH_OFFSET);
        assert(depth <= std::numeric_limits<std::uint8_t>::max() + DEPTH_OFFSET);

        // Preserve the old move if don't have a new one
        if (key16 != k16 || move != Move::None())
            move16 = move;
        // Overwrite less valuable entries (cheapest checks first)
        if (key16 != k16 || bound == BOUND_EXACT           //
            || 4 + depth - DEPTH_OFFSET + 2 * pv > depth8  //
            || relative_age(gen))
        {
            key16    = k16;
            depth8   = std::uint8_t(depth - DEPTH_OFFSET);
            genData8 = std::uint8_t(gen | std::uint8_t(pv) << 2 | bound);
            value16  = value;
            eval16   = eval;
        }
    }

    void clear() noexcept {  //
        std::memset(static_cast<void*>(this), 0, sizeof(TTEntry));
    }

    // The returned age is a multiple of GENERATION_DELTA
    std::uint8_t relative_age(std::uint8_t gen) const noexcept {
        // Due to packed storage format for generation and its cyclic nature
        // add GENERATION_CYCLE (256 is the modulus, plus what is needed to keep
        // the unrelated lowest n bits from affecting the relative age)
        // to calculate the entry age correctly even after gen overflows into the next cycle.
        return (GENERATION_CYCLE + gen - genData8) & GENERATION_MASK;
    }

    std::int16_t worth(std::uint8_t gen) const noexcept { return depth8 - 2 * relative_age(gen); }

   private:
    Key16        key16;
    Move         move16;
    std::uint8_t depth8;
    std::uint8_t genData8;
    Value        value16;
    Value        eval16;

    friend class TTUpdater;
    friend class TranspositionTable;
};

static_assert(sizeof(TTEntry) == 10, "Unexpected TTEntry size");

constexpr inline std::size_t TT_CLUSTER_ENTRY_COUNT = 3;

// TTCluster consists of TT_CLUSTER_ENTRY_COUNT number of TTEntry.
// The size of a TTCluster should divide the size of a cache line for best performance,
// as the cache-line is prefetched when possible.
struct TTCluster final {
   public:
    TTEntry entry[TT_CLUSTER_ENTRY_COUNT];

    Move move;  // Pad to 32 bytes
};

static_assert(sizeof(TTCluster) == 32, "Unexpected TTCluster size");

class TTUpdater final {
   public:
    TTUpdater() noexcept                            = delete;
    TTUpdater(const TTUpdater&) noexcept            = delete;
    TTUpdater(TTUpdater&&) noexcept                 = delete;
    TTUpdater& operator=(const TTUpdater&) noexcept = delete;
    TTUpdater& operator=(TTUpdater&&) noexcept      = delete;

    TTUpdater(
      TTEntry* te, TTCluster* const tc, Key16 k16, std::int16_t ply, std::uint8_t gen) noexcept :
        tte(te),
        ttc(tc),
        key16(k16),
        ssPly(ply),
        generation(gen) {}

    void
    update(Depth depth, bool pv, Bound bound, const Move& move, Value value, Value eval) noexcept;

   private:
    TTEntry*         tte;
    TTCluster* const ttc;
    Key16            key16;
    std::int16_t     ssPly;
    std::uint8_t     generation;
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

    std::uint8_t generation() const noexcept { return generation8; }

    void update_generation(bool update = true) noexcept {
        generation8 = update * generation8 + GENERATION_DELTA;
    }

    void resize(std::size_t ttSize, ThreadPool& threads) noexcept;
    void init(ThreadPool& threads) noexcept;

    std::tuple<TTData, TTEntry*, TTCluster* const> probe(Key key, Key16 key16) const noexcept;
    std::tuple<TTData, TTEntry*, TTCluster* const> probe(Key key) const noexcept;

    std::uint16_t hashFull(std::uint8_t maxAge) const noexcept;
    std::uint16_t hashFull() noexcept;

    bool save(const std::string& hashFile) const noexcept;
    bool load(const std::string& hashFile, ThreadPool& threads) noexcept;

   private:
    void free() noexcept;

    auto* cluster(Key key) const noexcept { return &clusters[mul_hi64(key, clusterCount)]; }

   public:
    // Prefetch the cache line which includes this key's entry
    void prefetch_key(Key key) const noexcept { prefetch(cluster(key)); }

    std::uint16_t lastHashFull = 0;

   private:
    TTCluster*   clusters     = nullptr;
    std::size_t  clusterCount = 0;
    std::uint8_t generation8  = 0;  // Size must be not bigger than TTEntry::genData8
};

}  // namespace DON

#endif  // #ifndef TT_H_INCLUDED
