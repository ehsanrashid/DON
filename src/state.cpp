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

#include "state.h"

#include "bitboard.h"

namespace DON {

void State::clear() noexcept {
    std::memset(this, 0, sizeof(*this));

    enPassantSq = capturedSq = SQ_NONE;
}

void State::dump(std::ostream& os) const noexcept {

    os << "Pawn Keys:\n";
    for (Color c : {WHITE, BLACK})
    {
        os << (c == WHITE ? 'W' : 'B') << ": ";
        os << u64_to_hex_prefix(pawnKeys[c]) << '\n';
    }

    os << "Non-Pawn Keys:\n";
    for (Color c : {WHITE, BLACK})
    {
        os << (c == WHITE ? 'W' : 'B') << ": ";
        os << u64_to_hex_prefix(nonPawnKeys[c][0]) << ' ';
        os << u64_to_hex_prefix(nonPawnKeys[c][1]) << '\n';
    }

    os << "En-Passant Square: " << (is_ok(enPassantSq) ? to_string(enPassantSq) : "-");
    os << '\n';
    os << "Captured Square: " << (is_ok(capturedSq) ? to_string(capturedSq) : "-");
    os << '\n';

    os << "Pinner Bitboards:\n";
    for (const Color c : {WHITE, BLACK})
    {
        os << (c == WHITE ? 'W' : 'B') << ':';
        os << pretty(pinnersBB[c]) << '\n';
    }

    os << "Blocker Bitboards:\n";
    for (const Color c : {WHITE, BLACK})
    {
        os << (c == WHITE ? 'W' : 'B') << ':';
        os << pretty(blockersBB[c]) << '\n';
    }

    os << "Check Bitboards:\n";
    for (const auto pt : PIECE_TYPES)
    {
        os << to_char(pt) << ':';
        os << pretty(checksBB[pt]) << '\n';
    }

    os << "Attack Bitboards:\n";
    for (PieceType pt = PAWN; pt <= ALL; ++pt)
    {
        os << to_char(pt) << ':';
        os << pretty(accAttacksBB[pt]) << '\n';
    }

    os << "Repetition: " << repetition;
    os << '\n';
    os << "Captured Piece: " << (capturedPc != Piece::NO_PIECE ? to_char(capturedPc) : '-');
    os << '\n';
    os << "Promoted Piece: " << (promotedPc != Piece::NO_PIECE ? to_char(promotedPc) : '-');
    os << '\n';

    os.flush();
}

}  // namespace DON
