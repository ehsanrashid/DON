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

#include <atomic>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <tuple>

#include "misc.h"
#include "types.h"

namespace DON {

struct TTEntry;
struct TTCluster;

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

class TTUpdater final {
   public:
    TTUpdater() noexcept                            = delete;
    TTUpdater(const TTUpdater&) noexcept            = delete;
    TTUpdater(TTUpdater&&) noexcept                 = default;
    TTUpdater& operator=(const TTUpdater&) noexcept = delete;
    TTUpdater& operator=(TTUpdater&&) noexcept      = default;

    TTUpdater(TTEntry* te, TTCluster* const tc, Key16 k16, std::uint8_t gen) noexcept :
        tte(te),
        ttc(tc),
        key16(k16),
        generation(gen) {}

    void update(Depth d, bool pv, Bound b, const Move& m, Value v, Value ev) noexcept;

   private:
    TTEntry*         tte;
    TTCluster* const ttc;
    Key16            key16;
    std::uint8_t     generation;
};

class ThreadPool;

using ProbResult = std::tuple<TTData, TTUpdater>;

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

    void increment_generation() noexcept;

    void resize(std::size_t ttSize, ThreadPool& threads) noexcept;
    void init(ThreadPool& threads) noexcept;

    ProbResult probe(Key key) const noexcept;

    std::uint16_t hashfull(std::uint8_t maxAge) const noexcept;
    std::uint16_t hashfull() noexcept;

    bool save(std::string_view hashFile) const noexcept;
    bool load(std::string_view hashFile, ThreadPool& threads) noexcept;

   private:
    void free() noexcept;

    TTCluster* cluster(Key key) const noexcept;

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
