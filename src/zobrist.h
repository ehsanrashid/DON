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

#include "misc.h"
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

    void init(u64 seed) noexcept;

    [[nodiscard]] Key piece_square(Color c, PieceType pt, Square s) const noexcept;

    [[nodiscard]] Key piece_square(Piece pc, Square s) const noexcept;

    [[nodiscard]] Key piece_count(Color c, PieceType pt, u8 cnt) const noexcept;

    [[nodiscard]] Key castling(CastlingRights cr) const noexcept;

    [[nodiscard]] Key enpassant(Square enPassantSq) const noexcept;

    [[nodiscard]] Key turn() const noexcept;

    [[nodiscard]] Key mr50(i16 rule50Count) const noexcept;

   private:
    Zobrist(const Zobrist&) noexcept            = delete;
    Zobrist& operator=(const Zobrist&) noexcept = delete;
    Zobrist(Zobrist&&) noexcept                 = delete;
    Zobrist& operator=(Zobrist&&) noexcept      = delete;

    static constexpr u8 PAWN_OFFSET = u8{8};

    static constexpr u8 R50_OFFSET = u8{14};
    static constexpr u8 R50_FACTOR = u8{8};

    Array<Key, COLOR_NB, 1 + PIECE_TYPE_CNT, SQUARE_NB> PieceSquare;
    Array<Key, CASTLING_RIGHTS_NB>                      Castling;
    Array<Key, FILE_NB>                                 Enpassant;
    Key                                                 Turn;
    Array<Key, 64>                                      MR50;
};

}  // namespace DON

#endif  // ZOBRIST_H_INCLUDED
