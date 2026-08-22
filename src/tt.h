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

#include <filesystem>

#include "misc.h"
#include "types.h"

namespace DON {

// There is only one global hash table for the engine and all its threads.
// For chess in particular, even allow racy updates between threads to and from the TT,
// as taking the time to synchronize access would cost thinking time and thus Elo.
// As a hash table, collisions are possible and may cause chess playing issues (bizarre blunders, faulty mate reports, etc).
// Fixing these also loses elo; however such risk decreases quickly with larger TT size.
//
// Use separate TTData, a local copy of an entry, from TTUpdater, which writes to the global table.
// A copy of the data already in an entry (possibly collided).
// Probes and reads are racy and non-atomic, possibly resulting in inconsistent data.
struct TTData final {
   public:
    TTData() noexcept                         = delete;
    TTData(const TTData&) noexcept            = delete;
    TTData& operator=(const TTData&) noexcept = delete;
    TTData(TTData&&) noexcept                 = default;
    TTData& operator=(TTData&&) noexcept      = delete;

    static TTData empty() noexcept;

    Move  move;
    Value value;
    Value evalue;
    Depth depth;
    Bound bound;
    bool  hit;
    bool  pv;
};

//static_assert(sizeof(TTData) == 12, "TTData size must be 12 bytes");

struct TTEntry;
struct TTCluster;

// This is used to make racy, non-atomic writes to the global TT.
// Writes are not "guaranteed": for chess reasons, later may decide the new data is less important than the old.
class TTUpdater final {
   public:
    TTUpdater(TTUpdater&&) noexcept = default;

    TTUpdater(TTEntry* te, TTCluster* tc, u16 k, u8 gen) noexcept;

    void update(Move m, Value v, Value ev, Depth d, Bound b, bool pv) noexcept;

   private:
    TTUpdater() noexcept                            = delete;
    TTUpdater& operator=(const TTUpdater&) noexcept = delete;
    TTUpdater(const TTUpdater&) noexcept            = delete;
    TTUpdater& operator=(TTUpdater&&) noexcept      = delete;

    TTEntry*         tte;
    TTCluster* const ttc;
    const u16        key;
    const u8         generation;
};

struct ProbResult final {
   public:
    TTData    data;
    TTUpdater updater;
};

class Threads;

// TranspositionTable is an array of TTCluster, of size clusterCount.
// Each non-empty TTEntry contains information on exactly one position.
class TranspositionTable final {
   public:
    TranspositionTable() noexcept = default;
    ~TranspositionTable() noexcept;

    u8 generation() const noexcept;

    void advance_generation() const noexcept;

    void resize(usize ttSize, const Threads& threads) noexcept;

    void reset(const Threads& threads) noexcept;

    TTCluster* cluster(Key key) const noexcept;

    ProbResult probe(Key key) const noexcept;

    u16 hashfull(u8 maxAge = 0) const noexcept;

    bool load(const std::filesystem::path& hashFile, const Threads& threads) noexcept;
    bool save(const std::filesystem::path& hashFile) const noexcept;

   private:
    TranspositionTable(const TranspositionTable&) noexcept            = delete;
    TranspositionTable& operator=(const TranspositionTable&) noexcept = delete;
    TranspositionTable(TranspositionTable&&) noexcept                 = delete;
    TranspositionTable& operator=(TranspositionTable&&) noexcept      = delete;

    void free() noexcept;

    TTCluster* clusters = nullptr;
    usize      clusterCount;
    mutable u8 generation8;
};

}  // namespace DON

#endif  // TT_H_INCLUDED
