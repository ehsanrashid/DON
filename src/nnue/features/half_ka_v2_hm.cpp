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

//Definition of input features HalfKAv2_hm of NNUE evaluation function

#include "half_ka_v2_hm.h"

#include "../../bitboard.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_accumulator.h"

namespace DON::NNUE::Features {

// clang-format off

// Index of a feature for a given king position and another piece on some square
template<Color Perspective>
IndexType HalfKAv2_hm::make_index(Square s, Piece pc, Square ksq) noexcept {
    return (int(s) ^ ORIENT_TABLE[Perspective][ksq])
           + PIECE_SQUARE_INDEX[Perspective][pc]
           + KING_BUCKETS[Perspective][ksq];
}

// Get a list of indices for active features
template<Color Perspective>
void HalfKAv2_hm::append_active_indices(const Position& pos, IndexList& active) noexcept {
    Square ksq = pos.king_square(Perspective);

    Bitboard occupied = pos.pieces();
    while (occupied)
    {
        Square s = pop_lsb(occupied);
        active.push_back(make_index<Perspective>(s, pos.piece_on(s), ksq));
    }
}

// Explicit template instantiations
template IndexType HalfKAv2_hm::make_index<WHITE>(Square s, Piece pc, Square ksq) noexcept;
template IndexType HalfKAv2_hm::make_index<BLACK>(Square s, Piece pc, Square ksq) noexcept;

template void HalfKAv2_hm::append_active_indices<WHITE>(const Position& pos, IndexList& active) noexcept;
template void HalfKAv2_hm::append_active_indices<BLACK>(const Position& pos, IndexList& active) noexcept;

// Get a list of indices for recently changed features
template<Color Perspective>
void HalfKAv2_hm::append_changed_indices(Square ksq, const DirtyPiece& dp, IndexList& removed, IndexList& added) noexcept {
    for (int i = 0; i < dp.dirtyNum; ++i)
    {
        if (dp.org[i] != SQ_NONE)
            removed.push_back(make_index<Perspective>(dp.org[i], dp.piece[i], ksq));
        if (dp.dst[i] != SQ_NONE)
            added.push_back(make_index<Perspective>(dp.dst[i], dp.piece[i], ksq));
    }
}

// Explicit template instantiations
template void HalfKAv2_hm::append_changed_indices<WHITE>(Square ksq, const DirtyPiece& dp, IndexList& removed, IndexList& added) noexcept;
template void HalfKAv2_hm::append_changed_indices<BLACK>(Square ksq, const DirtyPiece& dp, IndexList& removed, IndexList& added) noexcept;
// clang-format on

int HalfKAv2_hm::update_cost(const State* st) noexcept { return st->dirtyPiece.dirtyNum; }

int HalfKAv2_hm::refresh_cost(const Position& pos) noexcept { return pos.count<ALL_PIECE>(); }

bool HalfKAv2_hm::requires_refresh(const State* st, Color perspective) noexcept {
    return st->dirtyPiece.piece[0] == make_piece(perspective, KING);
}

}  // namespace DON::NNUE::Features