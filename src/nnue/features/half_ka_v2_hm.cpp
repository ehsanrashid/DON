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

// Definition of input features HalfKAv2_hm of NNUE evaluation function

#include "half_ka_v2_hm.h"

#include <array>

#include "../../bitboard.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace DON::NNUE::Features {

namespace {

// Unique number for each piece type on each square
enum PS : std::uint16_t {
    PS_NONE     = 0,
    PS_W_PAWN   = 0 * SQUARE_NB,
    PS_B_PAWN   = 1 * SQUARE_NB,
    PS_W_KNIGHT = 2 * SQUARE_NB,
    PS_B_KNIGHT = 3 * SQUARE_NB,
    PS_W_BISHOP = 4 * SQUARE_NB,
    PS_B_BISHOP = 5 * SQUARE_NB,
    PS_W_ROOK   = 6 * SQUARE_NB,
    PS_B_ROOK   = 7 * SQUARE_NB,
    PS_W_QUEEN  = 8 * SQUARE_NB,
    PS_B_QUEEN  = 9 * SQUARE_NB,
    PS_KING     = 10 * SQUARE_NB,
    PS_NB       = 11 * SQUARE_NB
};

constexpr StdArray<IndexType, COLOR_NB, PIECE_NB> PieceSquareIndex{{
  // Convention: W - us, B - them
  // Viewed from other side, W and B are reversed
  {PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE,   //
   PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE},  //
  {PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE,   //
   PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE}   //
}};

#define B(v) (v * PS_NB)
constexpr StdArray<IndexType, SQUARE_NB> KingBuckets{
  B(28), B(29), B(30), B(31), B(31), B(30), B(29), B(28),  //
  B(24), B(25), B(26), B(27), B(27), B(26), B(25), B(24),  //
  B(20), B(21), B(22), B(23), B(23), B(22), B(21), B(20),  //
  B(16), B(17), B(18), B(19), B(19), B(18), B(17), B(16),  //
  B(12), B(13), B(14), B(15), B(15), B(14), B(13), B(12),  //
  B(8),  B(9),  B(10), B(11), B(11), B(10), B(9),  B(8),   //
  B(4),  B(5),  B(6),  B(7),  B(7),  B(6),  B(5),  B(4),   //
  B(0),  B(1),  B(2),  B(3),  B(3),  B(2),  B(1),  B(0)    //
};
#undef B

// Orient a square according to perspective (rotates by 180 for black)
constexpr StdArray<IndexType, SQUARE_NB> OrientTable{
  SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,  //
  SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,  //
  SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,  //
  SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,  //
  SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,  //
  SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,  //
  SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,  //
  SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1   //
};

}  // namespace

// Index of a feature for king position and piece on square
IndexType HalfKAv2_hm::make_index(Color perspective, Square kingSq, Square s, Piece pc) noexcept {
    return PieceSquareIndex[perspective][pc]  //
         + KingBuckets[relative_sq(perspective, kingSq)]
         + (OrientTable[kingSq] ^ IndexType(relative_sq(perspective, s)));
}

// Get a list of indices for active features
void HalfKAv2_hm::append_active_indices(Color           perspective,
                                        const Position& pos,
                                        IndexList&      active) noexcept {
    Square kingSq = pos.king_sq(perspective);

    Bitboard occupied = pos.pieces();
    while (occupied)
    {
        Square s = pop_lsb(occupied);
        active.push_back(make_index(perspective, kingSq, s, pos.piece_on(s)));
    }
}

// Get a list of indices for recently changed features
void HalfKAv2_hm::append_changed_indices(Color            perspective,
                                         Square           kingSq,
                                         const DirtyType& dt,
                                         IndexList&       removed,
                                         IndexList&       added) noexcept {
    removed.push_back(make_index(perspective, kingSq, dt.org, dt.pc));

    if (is_ok(dt.dst))
        added.push_back(make_index(perspective, kingSq, dt.dst, dt.pc));

    if (is_ok(dt.removeSq))
        removed.push_back(make_index(perspective, kingSq, dt.removeSq, dt.removePc));

    if (is_ok(dt.addSq))
        added.push_back(make_index(perspective, kingSq, dt.addSq, dt.addPc));
}

bool HalfKAv2_hm::requires_refresh(Color perspective, const DirtyType& dt) noexcept {
    return dt.pc == make_piece(perspective, KING);
}

}  // namespace DON::NNUE::Features
