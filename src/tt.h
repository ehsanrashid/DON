/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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
#include <string_view>

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
    TTData() noexcept                         = delete;
    TTData(const TTData&) noexcept            = delete;
    TTData(TTData&&) noexcept                 = default;
    TTData& operator=(const TTData&) noexcept = delete;
    TTData& operator=(TTData&&) noexcept      = delete;

    static TTData empty() noexcept;

    Move  move;
    Value value;
    Value evalValue;
    Depth depth;
    Bound bound;
    bool  hit;
    bool  pv;
};

//static_assert(sizeof(TTData) == 12, "Unexpected TTData size");

struct TTEntry;
struct TTCluster;

class TTUpdater final {
   public:
    TTUpdater() noexcept                            = delete;
    TTUpdater(const TTUpdater&) noexcept            = delete;
    TTUpdater(TTUpdater&&) noexcept                 = default;
    TTUpdater& operator=(const TTUpdater&) noexcept = delete;
    TTUpdater& operator=(TTUpdater&&) noexcept      = delete;

    TTUpdater(TTEntry* te, TTCluster* tc, std::uint16_t k, std::uint8_t gen) noexcept;

    void update(Move m, Value v, Value ev, Depth d, Bound b, bool pv) noexcept;

   private:
    TTEntry*            tte;
    TTCluster* const    ttc;
    const std::uint16_t key;
    const std::uint8_t  generation;
};

class Threads;

struct ProbResult final {
    TTData    data;
    TTUpdater updater;
};

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

    std::uint8_t generation() const noexcept;

    void increment_generation() noexcept;

    void resize(std::size_t ttSize, Threads& threads) noexcept;

    void init(Threads& threads) noexcept;

    TTCluster* cluster(Key key) const noexcept;

    ProbResult probe(Key key) const noexcept;

    std::uint16_t hashfull(std::uint8_t maxAge = 0) const noexcept;

    bool load(std::string_view hashFile, Threads& threads) noexcept;
    bool save(std::string_view hashFile) const noexcept;

   private:
    void free() noexcept;

    TTCluster*   clusters = nullptr;
    std::size_t  clusterCount;
    std::uint8_t generation8;
};

}  // namespace DON

#endif  // #ifndef TT_H_INCLUDED
