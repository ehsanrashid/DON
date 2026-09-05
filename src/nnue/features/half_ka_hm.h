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

// Definition of input features HalfKAHm of NNUE evaluation function

#ifndef NNUE_FEATURES_HALF_KA_HM_H_INCLUDED
#define NNUE_FEATURES_HALF_KA_HM_H_INCLUDED

#include "../../misc.h"
#include "../../types.h"

namespace DON::NNUE::Features {

// Feature HalfKAHm: Combination of the position of own king and the position of pieces.
// Position mirrored such that king is always on e...h files.
class HalfKAHm final {
   private:
    // Unique number for each piece type on each square
    static constexpr u16 PS_NONE     = 0;
    static constexpr u16 PS_W_PAWN   = 0 * SQUARE_NB;
    static constexpr u16 PS_B_PAWN   = 1 * SQUARE_NB;
    static constexpr u16 PS_W_KNIGHT = 2 * SQUARE_NB;
    static constexpr u16 PS_B_KNIGHT = 3 * SQUARE_NB;
    static constexpr u16 PS_W_BISHOP = 4 * SQUARE_NB;
    static constexpr u16 PS_B_BISHOP = 5 * SQUARE_NB;
    static constexpr u16 PS_W_ROOK   = 6 * SQUARE_NB;
    static constexpr u16 PS_B_ROOK   = 7 * SQUARE_NB;
    static constexpr u16 PS_W_QUEEN  = 8 * SQUARE_NB;
    static constexpr u16 PS_B_QUEEN  = 9 * SQUARE_NB;
    static constexpr u16 PS_KING     = 10 * SQUARE_NB;
    static constexpr u16 PS_NB       = 11 * SQUARE_NB;

   public:
    // Hash value embedded in the evaluation file
    static constexpr u32 Hash = 0x7F234CB8u;

    // Number of feature dimensions
    static constexpr u16 Dimensions = PS_NB * SQUARE_NB / 2;

    // Maximum number of simultaneously active features
    static constexpr u16 MaxActiveDimensions = 32;
    using IndexVector                        = FixedVector<u16, MaxActiveDimensions, u16>;
    using DirtyType                          = DirtyPiece;

    // Mirror square to have king always on e..h files
    // (file_of(s) >> 2) is 0 for 0...3, 1 for 4...7
    static constexpr Square orientation(const Square s) noexcept {
        return Square(((file_of(s) >> 2) ^ 1) * FILE_H);
    }

    alignas(CACHE_LINE_SIZE) static constexpr Array<u16, COLOR_NB, PIECE_NB> PIECE_SQUARE_INDICES{{
      // Convention: W - us, B - them
      // Viewed from other side, W and B are reversed
      {PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE,   //
       PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE},  //
      {PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE,   //
       PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE}   //
    }};

#define B(v) (v * PS_NB)
    alignas(CACHE_LINE_SIZE) static constexpr Array<u16, SQUARE_NB> KING_BUCKETS{
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

    static void append_map_changed_indices(Color           perspective,
                                           Square          kingSq,
                                           const PieceMap& oldPieceMap,
                                           const PieceMap& newPieceMap,
                                           Bitboard        removedBB,
                                           Bitboard        addedBB,
                                           IndexVector&    removed,
                                           IndexVector&    added) noexcept;

    static void append_changed_indices(Color            perspective,
                                       Square           kingSq,
                                       const DirtyType& dP,
                                       IndexVector&     removed,
                                       IndexVector&     added) noexcept;

    static bool refresh_required(Color perspective, const DirtyType& dP) noexcept;

   private:
    HalfKAHm() noexcept                           = delete;
    ~HalfKAHm() noexcept                          = delete;
    HalfKAHm(const HalfKAHm&) noexcept            = delete;
    HalfKAHm& operator=(const HalfKAHm&) noexcept = delete;
    HalfKAHm(HalfKAHm&&) noexcept                 = delete;
    HalfKAHm& operator=(HalfKAHm&&) noexcept      = delete;
};

static_assert(HalfKAHm::orientation(SQ_A1) == SQ_H1);
static_assert(HalfKAHm::orientation(SQ_D1) == SQ_H1);
static_assert(HalfKAHm::orientation(SQ_E1) == SQ_A1);
static_assert(HalfKAHm::orientation(SQ_H1) == SQ_A1);
static_assert(HalfKAHm::orientation(SQ_A8) == SQ_H1);
static_assert(HalfKAHm::orientation(SQ_H8) == SQ_A1);

}  // namespace DON::NNUE::Features

#endif  // NNUE_FEATURES_HALF_KA_HM_H_INCLUDED
