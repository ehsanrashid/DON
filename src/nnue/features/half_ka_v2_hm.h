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

//Definition of input features HalfKP of NNUE evaluation function

#ifndef NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED
#define NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED

#include <cstddef>
#include <cstdint>

#include "../../types.h"
#include "../nnue_common.h"

namespace DON {

class Position;

namespace NNUE::Features {

template<typename T, std::size_t Size>
class ArrayList final {

   public:
    void push_back(const T& value) noexcept { data[count++] = value; }

    std::size_t size() const noexcept { return count; }

    const T* begin() const noexcept { return data; }
    const T* end() const noexcept { return data + count; }

    const T& operator[](std::size_t index) const noexcept { return data[index]; }
    T&       operator[](std::size_t index) noexcept { return data[index]; }

   private:
    T           data[Size];
    std::size_t count = 0;
};

// Feature HalfKAv2_hm: Combination of the position of own king and the
// position of pieces. Position mirrored such that king is always on e..h files.
class HalfKAv2_hm final {

    // Unique number for each piece type on each square
    enum PS {
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

    static constexpr IndexType PieceSquareIndex[COLOR_NB][PIECE_NB] = {
      // Convention: W - us, B - them
      // Viewed from other side, W and B are reversed
      {PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE,
       PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE},
      {PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE,
       PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE}};

   public:
    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t HashValue = 0x7F234CB8u;

    // Number of feature dimensions
    static constexpr IndexType Dimensions = (PS_NB * SQUARE_NB) / 2;

    // clang-format off
#define B(v) (v * PS_NB)
    static constexpr int KingBuckets[COLOR_NB][SQUARE_NB] = {
      { B(28), B(29), B(30), B(31), B(31), B(30), B(29), B(28),
        B(24), B(25), B(26), B(27), B(27), B(26), B(25), B(24),
        B(20), B(21), B(22), B(23), B(23), B(22), B(21), B(20),
        B(16), B(17), B(18), B(19), B(19), B(18), B(17), B(16),
        B(12), B(13), B(14), B(15), B(15), B(14), B(13), B(12),
        B( 8), B( 9), B(10), B(11), B(11), B(10), B( 9), B( 8),
        B( 4), B( 5), B( 6), B( 7), B( 7), B( 6), B( 5), B( 4),
        B( 0), B( 1), B( 2), B( 3), B( 3), B( 2), B( 1), B( 0) },
      { B( 0), B( 1), B( 2), B( 3), B( 3), B( 2), B( 1), B( 0),
        B( 4), B( 5), B( 6), B( 7), B( 7), B( 6), B( 5), B( 4),
        B( 8), B( 9), B(10), B(11), B(11), B(10), B( 9), B( 8),
        B(12), B(13), B(14), B(15), B(15), B(14), B(13), B(12),
        B(16), B(17), B(18), B(19), B(19), B(18), B(17), B(16),
        B(20), B(21), B(22), B(23), B(23), B(22), B(21), B(20),
        B(24), B(25), B(26), B(27), B(27), B(26), B(25), B(24),
        B(28), B(29), B(30), B(31), B(31), B(30), B(29), B(28) }
    };
#undef B

    // Orient a square according to perspective (rotates by 180 for black)
    static constexpr int OrientTable[COLOR_NB][SQUARE_NB] = {
      { SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1,
        SQ_H1, SQ_H1, SQ_H1, SQ_H1, SQ_A1, SQ_A1, SQ_A1, SQ_A1 },
      { SQ_H8, SQ_H8, SQ_H8, SQ_H8, SQ_A8, SQ_A8, SQ_A8, SQ_A8,
        SQ_H8, SQ_H8, SQ_H8, SQ_H8, SQ_A8, SQ_A8, SQ_A8, SQ_A8,
        SQ_H8, SQ_H8, SQ_H8, SQ_H8, SQ_A8, SQ_A8, SQ_A8, SQ_A8,
        SQ_H8, SQ_H8, SQ_H8, SQ_H8, SQ_A8, SQ_A8, SQ_A8, SQ_A8,
        SQ_H8, SQ_H8, SQ_H8, SQ_H8, SQ_A8, SQ_A8, SQ_A8, SQ_A8,
        SQ_H8, SQ_H8, SQ_H8, SQ_H8, SQ_A8, SQ_A8, SQ_A8, SQ_A8,
        SQ_H8, SQ_H8, SQ_H8, SQ_H8, SQ_A8, SQ_A8, SQ_A8, SQ_A8,
        SQ_H8, SQ_H8, SQ_H8, SQ_H8, SQ_A8, SQ_A8, SQ_A8, SQ_A8 }
    };
    // clang-format on

    // Maximum number of simultaneously active features.
    static constexpr IndexType MaxActiveDimensions = 32;

    using IndexList = ArrayList<IndexType, MaxActiveDimensions>;

    // Index of a feature for a given king position and another piece on some square
    template<Color Perspective>
    static IndexType make_index(Square s, Piece pc, Square kingSq) noexcept;

    // Get a list of indices for active features
    template<Color Perspective>
    static void append_active_indices(const Position& pos, IndexList& active) noexcept;

    // Get a list of indices for recently changed features
    template<Color Perspective>
    static void append_changed_indices(Square            kingSq,
                                       const DirtyPiece& dp,
                                       IndexList&        removed,
                                       IndexList&        added) noexcept;

    // Returns whether the change stored in this State means
    // that a full accumulator refresh is required.
    static bool requires_refresh(const DirtyPiece& dp, Color perspective) noexcept;
};

}  // namespace NNUE::Features
}  // namespace DON

#endif  // #ifndef NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED
