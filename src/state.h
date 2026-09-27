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

#ifndef STATE_H_INCLUDED
#define STATE_H_INCLUDED

#include <cassert>
#include <cstring>  // memcpy()/memset()
#include <deque>
#include <iostream>
#include <memory>       // unique_ptr<>
#include <type_traits>  // is_standard_layout_v<>

#include "misc.h"
#include "types.h"

namespace DON {

// State struct stores information needed to restore Position object
// to its previous state when retract any move. (Size = 272)
struct State final {
   public:
    // A list to keep track of the position states along the setup moves
    // (from the start position to the position just before the search starts).
    // Needed by 'draw by repetition' detection.
    // Use std::deque because pointers and references to existing elements remain valid when appending.
    using List    = std::deque<State>;
    using ListPtr = std::unique_ptr<List>;

    State() noexcept                           = default;
    State(const State&) noexcept               = default;
    State& operator=(const State& st) noexcept = default;
    State(State&&) noexcept                    = delete;
    State& operator=(State&&) noexcept         = delete;

    void clear() noexcept;

    void dump(std::ostream& os = std::cout) const noexcept;

    // Copy relevant fields from the state.
    // excluding those that will recomputed from scratch anyway and
    // then switch the state pointer to point to the new state.
    template<typename T = Bitboard>
    void switch_to_prefix(const State* st, T State::* member = &State::checkersBB) noexcept {
        // Compute offset dynamically for this object
        const usize size = reinterpret_cast<const char*>(&(st->*member))  //
                         - reinterpret_cast<const char*>(st);
        assert(size <= sizeof(*this) && "size exceeds object size");

        std::memcpy(this, st, size);

        preSt = st;
    }

    // --- Copied when making a move
    Key                     key;
    Array<Key, COLOR_NB>    pawnKeys;
    Array<Key, COLOR_NB, 2> nonPawnKeys;
    Array<Key, COLOR_NB>    materialKeys;
    Array<bool, COLOR_NB>   hasCastleds;

    u16            rule50Count;
    u16            nullPly;  // Plies from Null-Move
    Square         enPassantSq;
    Square         capturedSq;
    CastlingRights castlingRights;
    bool           hasRule50High;

    // --- Not copied when making a move (will be recomputed anyhow)
    Bitboard                       checkersBB;
    Array<Bitboard, COLOR_NB>      pinnersBB;
    Array<Bitboard, COLOR_NB>      blockersBB;
    Array<Bitboard, PIECE_TYPE_NB> checksBB;
    Array<Bitboard, PIECE_TYPE_NB> accAttacksBB;
    i16                            repetition;
    Piece                          capturedPc;
    Piece                          promotedPc;
    const State*                   preSt;
};

static_assert(std::is_standard_layout_v<State> && std::is_trivially_copyable_v<State>,
              "State must be standard-layout and trivially copyable");

}  // namespace DON

#endif  // STATE_H_INCLUDED
