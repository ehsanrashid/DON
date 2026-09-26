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

#ifndef ZOBRIST_H_INCLUDED
#define ZOBRIST_H_INCLUDED

#include <algorithm>  // min()/max(), generate()
#include <cassert>
#include <cstring>

#include "misc.h"
#include "prng.h"
#include "types.h"

namespace DON {

// Zobrist - Hash key generator
//
// Provides precomputed Zobrist keys for the relevant components of a chess position.
// These keys are used to efficiently compute position hashes for transposition tables,
// repetition detection, move ordering, and other chess-engine operations.
//
// Key features:
//  - Generates keys for piece-square positions, piece counts, castling rights,
//    en passant files, side to move, and the 50-move rule counter (MR50).
//  - Each instance owns its own set of Zobrist keys and is initialized with
//    a caller-provided seed.
//  - Provides constant-time access to all generated keys.
//
// Interface summary:
//  - init(seed) - Initializes all Zobrist keys using the specified seed.
//  - piece_square(Color, PieceType, Square) / piece_square(Piece, Square)
//      Returns the Zobrist key for a piece on a specific square.
//  - piece_count(Color, PieceType, u8) - Returns the Zobrist key for a
//      specific piece count.
//  - castling(CastlingRights) - Returns the Zobrist key for castling rights.
//  - enpassant(Square) - Returns the Zobrist key for the en passant file.
//  - turn() - Returns the Zobrist key for the side to move.
//  - mr50(i16) - Returns the Zobrist key for the 50-move rule counter.
//
// Notes:
//  - init() must be called before accessing generated keys.
//  - Accessors perform debug-time assertions to validate input values.
//  - MR50 uses offset and factor constants to map rule-50 counts to its
//    precomputed key table.
//
// Example usage:
//   Zobrist zobrist;
//   zobrist.init(seed);
//   Key key = zobrist.piece_square(WHITE, KNIGHT, SQ_G1);
//   key ^= zobrist.turn();
class Zobrist final {
   public:
    Zobrist() noexcept = default;

    void init(u64 seed) noexcept {
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

    [[nodiscard]] constexpr Key
    piece_square(const Color c, const PieceType pt, const Square s) const noexcept {
        assert(is_ok(c) && is_ok(s));

        return PieceSquare[c][pt][s];
    }
    [[nodiscard]] constexpr Key  //
    piece_square(const Piece pc, const Square s) const noexcept {
        assert(is_ok(s));

        return piece_square(color_of(pc), type_of(pc), s);
    }

    [[nodiscard]] constexpr Key
    piece_count(const Color c, const PieceType pt, const u8 cnt) noexcept {
        const Square s = Square(PAWN_OFFSET + cnt);
        assert(is_ok(s));

        return piece_square(c, pt, s);
    }

    [[nodiscard]] constexpr Key castling(const CastlingRights cr) noexcept {
        assert(+cr < Castling.size());

        return Castling[+cr];
    }

    [[nodiscard]] constexpr Key enpassant(const Square enPassantSq) noexcept {
        return is_ok(enPassantSq) ? Enpassant[file_of(enPassantSq)] : 0;
    }

    [[nodiscard]] constexpr Key turn() const noexcept { return Turn; }

    [[nodiscard]] constexpr Key mr50(const i16 rule50Count) noexcept {
        return rule50Count < R50_OFFSET
               ? 0
               : MR50[std::min(usize((rule50Count - R50_OFFSET) / R50_FACTOR), MR50.size() - 1)];
    }

   private:
    static constexpr u8 PAWN_OFFSET = u8{8};

    static constexpr u8 R50_OFFSET = u8{14};
    static constexpr u8 R50_FACTOR = u8{8};

    Array<Key, COLOR_NB, PIECE_TYPE_CNT + 1, SQUARE_NB> PieceSquare;
    Array<Key, CASTLING_RIGHTS_NB>                      Castling;
    Array<Key, FILE_NB>                                 Enpassant;
    Key                                                 Turn;
    Array<Key, 64>                                      MR50;
};

}  // namespace DON

#endif  // ZOBRIST_H_INCLUDED
