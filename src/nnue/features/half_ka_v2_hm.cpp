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
