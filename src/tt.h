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

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <tuple>

#include "misc.h"
#include "types.h"

namespace DON {

extern std::uint8_t DrawMoveCount;

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
    TTData(bool ht, bool pv, Bound b, const Move& m, Depth d, Value v, Value ev) noexcept :
        hit(ht),
        pvHit(pv),
        bound(b),
        padding(0),
        move(m),
        depth(d),
        value(v),
        eval(ev) {}

    bool         hit;
    bool         pvHit;
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
constexpr std::uint8_t DATA_BITS = 3;
// Increment for generation field
constexpr std::uint8_t GENERATION_DELTA = 1 << DATA_BITS;
// Mask to pull out generation field
constexpr std::uint8_t GENERATION_MASK = (0xFF << DATA_BITS) & 0xFF;
// Generation cycle length
constexpr std::uint16_t GENERATION_CYCLE = 0xFF + GENERATION_DELTA;

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
    constexpr auto pv_hit() const noexcept { return bool(genData8 & 0x4); }
    constexpr auto bound() const noexcept { return Bound(genData8 & 0x3); }
    constexpr auto generation() const noexcept { return std::uint8_t(genData8 & GENERATION_MASK); }
    constexpr auto value() const noexcept { return value16; }
    constexpr auto eval() const noexcept { return eval16; }

    // Convert internal bitfields to TTData
    TTData read() const noexcept {
        return {occupied(), pv_hit(), bound(), move(), depth(), value(), eval()};
    }

    // Populates the TTEntry with a new node's data, possibly
    // overwriting an old position. The update is not atomic and can be racy.
    void save(Key16        k16,
              Depth        d,
              bool         pv,
              Bound        b,
              const Move&  m,
              Value        v,
              Value        ev,
              std::uint8_t gen) noexcept {
        assert(d > DEPTH_OFFSET);
        assert(d <= std::numeric_limits<std::uint8_t>::max() + DEPTH_OFFSET);

        // Preserve the old move if don't have a new one
        if (key16 != k16 || m != Move::None)
            move16 = m;
        // Overwrite less valuable entries (cheapest checks first)
        if (key16 != k16 || b == BOUND_EXACT           //
            || depth8 < 4 + d - DEPTH_OFFSET + 2 * pv  //
            || relative_age(gen))
        {
            key16    = k16;
            depth8   = std::uint8_t(d - DEPTH_OFFSET);
            genData8 = std::uint8_t(gen | std::uint8_t(pv) << 2 | b);
            value16  = v;
            eval16   = ev;
        }
        else if (depth8 + DEPTH_OFFSET >= 5 && bound() != BOUND_EXACT)
            --depth8;
    }

    void clear() noexcept { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }

    // The returned age is a multiple of GENERATION_DELTA
    std::uint8_t relative_age(std::uint8_t gen) const noexcept {
        // Due to packed storage format for generation and its cyclic nature
        // add GENERATION_CYCLE (256 is the modulus, plus what is needed to keep
        // the unrelated lowest n bits from affecting the relative age)
        // to calculate the entry age correctly even after gen overflows into the next cycle.
        return (GENERATION_CYCLE + gen - genData8) & GENERATION_MASK;
    }

    std::int16_t worth(std::uint8_t gen) const noexcept { return depth8 - relative_age(gen); }

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

// TTCluster consists of EntryCount number of TTEntry.
// The size of a TTCluster should divide the size of a cache-line for best performance,
// as the cache-line is prefetched when possible.
struct TTCluster final {
   public:
    static constexpr std::uint8_t EntryCount = 3;

    TTEntry entry[EntryCount];
    char    padding[2];  // Pad to 32 bytes
};

static_assert(sizeof(TTCluster) == 32, "Unexpected TTCluster size");

// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
constexpr Value value_to_tt(Value v, std::int16_t ply) noexcept {

    if (!is_valid(v))
        return v;
    assert(is_ok(v));
    return is_win(v)  ? std::min(v + ply, +VALUE_MATE)
         : is_loss(v) ? std::max(v - ply, -VALUE_MATE)
                      : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated from
// current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, return the highest non-TB score instead.
constexpr Value value_from_tt(Value v, std::int16_t ply, std::int16_t rule50Count) noexcept {

    if (!is_valid(v))
        return v;
    assert(is_ok(v));
    // Handle TB win or better
    if (is_win(v))
    {
        // Downgrade a potentially false mate value
        if (is_mate_win(v) && VALUE_MATE - v > 2 * DrawMoveCount - rule50Count)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB - v > 2 * DrawMoveCount - rule50Count)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        return v - ply;
    }
    // Handle TB loss or worse
    if (is_loss(v))
    {
        // Downgrade a potentially false mate value
        if (is_mate_loss(v) && VALUE_MATE + v > 2 * DrawMoveCount - rule50Count)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB + v > 2 * DrawMoveCount - rule50Count)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        return v + ply;
    }

    return v;
}

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

    void update(Depth d, bool pv, Bound b, const Move& m, Value v, Value ev) noexcept {

        if (tte->key16 != key16)
        {
            tte = &ttc->entry[0];
            for (auto& entry : ttc->entry)
            {
                if (entry.key16 == key16)
                {
                    tte = &entry;
                    break;
                }
                // Find an entry to be replaced according to the replacement strategy
                if (tte->worth(generation) > entry.worth(generation))
                    tte = &entry;
            }
        }
        else
        {
            for (; tte > &ttc->entry[0] && (tte - 1)->key16 == key16; --tte)
                tte->clear();
        }

        tte->save(key16, d, pv, b, m, value_to_tt(v, ssPly), ev, generation);
    }

   private:
    TTEntry*         tte;
    TTCluster* const ttc;
    Key16            key16;
    std::int16_t     ssPly;
    std::uint8_t     generation;
};

class ThreadPool;

using ProbResult = std::tuple<TTData, TTEntry*, TTCluster* const>;

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

    void increment_generation() noexcept { generation8 += GENERATION_DELTA; }

    void resize(std::size_t ttSize, ThreadPool& threads) noexcept;
    void init(ThreadPool& threads) noexcept;

    ProbResult probe(Key key, Key16 key16) const noexcept;
    ProbResult probe(Key key) const noexcept;

    std::uint16_t hashfull(std::uint8_t maxAge) const noexcept;
    std::uint16_t hashfull() noexcept;

    bool save(std::string_view hashFile) const noexcept;
    bool load(std::string_view hashFile, ThreadPool& threads) noexcept;

   private:
    void free() noexcept;

    auto* cluster(Key key) const noexcept { return &clusters[mul_hi64(key, clusterCount)]; }

   public:
    // Prefetch the cache line which includes this key's entry
    void prefetch_key(Key key) const noexcept { prefetch(cluster(key)); }

    std::atomic<std::uint16_t> hashFull;

   private:
    TTCluster*   clusters = nullptr;
    std::size_t  clusterCount;
    std::uint8_t generation8;  // Size must be not bigger than TTEntry::genData8
};

}  // namespace DON

#endif  // #ifndef TT_H_INCLUDED
