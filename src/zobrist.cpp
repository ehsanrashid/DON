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

#include "zobrist.h"

#include <algorithm>  // min()/max(), generate()
#include <cassert>
#include <cstring>

#include "prng.h"

namespace DON {

void Zobrist::init(const u64 seed) noexcept {
    Xoshiro256ss prng(seed);

    const auto prng_rand = [&prng]() noexcept -> Key { return prng.template rand<Key>(); };

    for (const Color c : {WHITE, BLACK})
    {
        for (const auto pt : PIECE_TYPES)
            std::generate(PieceSquare[c][pt].begin(), PieceSquare[c][pt].end(), prng_rand);

        std::memset(&PieceSquare[c][PAWN][SQ_A1], 0, PAWN_OFFSET * sizeof(Key));
        std::memset(&PieceSquare[c][PAWN][SQ_A8], 0, PAWN_OFFSET * sizeof(Key));
    }

    std::generate(Castling.begin(), Castling.end(), prng_rand);

    std::generate(Enpassant.begin(), Enpassant.end(), prng_rand);

    Turn = prng_rand();

    std::generate(MR50.begin(), MR50.end(), prng_rand);
}

Key Zobrist::piece_square(const Color c, const PieceType pt, const Square s) const noexcept {
    assert(is_ok(c) && is_ok(s));

    return PieceSquare[c][pt][s];
}

Key Zobrist::piece_square(const Piece pc, const Square s) const noexcept {
    assert(is_ok(s));

    return piece_square(color_of(pc), type_of(pc), s);
}

Key Zobrist::piece_count(const Color c, const PieceType pt, const u8 cnt) const noexcept {
    const Square s = Square(PAWN_OFFSET + cnt);
    assert(is_ok(s));

    return piece_square(c, pt, s);
}

Key Zobrist::castling(const CastlingRights cr) const noexcept {
    assert(+cr < Castling.size());

    return Castling[+cr];
}

Key Zobrist::enpassant(const Square enPassantSq) const noexcept {
    return is_ok(enPassantSq) ? Enpassant[file_of(enPassantSq)] : 0;
}

Key Zobrist::turn() const noexcept { return Turn; }

Key Zobrist::mr50(const i16 rule50Count) const noexcept {
    return rule50Count < R50_OFFSET
           ? 0
           : MR50[std::min(usize((rule50Count - R50_OFFSET) / R50_FACTOR), MR50.size() - 1)];
}

}  // namespace DON
