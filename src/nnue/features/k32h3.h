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

// Definition of input features K32H3 of NNUE evaluation function

#ifndef NNUE_FEATURES_K32H3_H_INCLUDED
#define NNUE_FEATURES_K32H3_H_INCLUDED

#include "../../types.h"
#include "../../nnue/types.h"
#include "../../utility/static_vector.h"
#include "../../utility/types.h"

namespace DON {

class Position;

// Feature K32H3: Combination of the position of own king (32 buckets), a piece-square block (3 states),
// and piece-square positions. Position mirrored such that king is always on e..h files.
namespace NNUE::Features::K32H3 {
// Hash value embedded in the evaluation file
inline constexpr u32 Hash = u32{0xAB005CEF};

// Piece-square block of a perspective, in order of increasing heavy material:
//  0: no rook or queen on the board,
//  1: some rook or queen on the board, but the opponent has no queen,
//  2: the opponent has a queen.
inline constexpr u8 BlockCount = 3;

struct Block final {
   public:
    Array<u8, COLOR_NB> queens{};
    u8                  queensRooks = 0;
};

constexpr u8 block(const Block& m, Color perspective) noexcept {
    return m.queens[~perspective] != 0 ? 2 : m.queensRooks != 0;
}

Block block_state(const Position& pos) noexcept;

u8 block(const Position& pos, Color perspective) noexcept;

// Number of features per block (22,528 piece-squares)
inline constexpr Index BlockDimensions = 11 * SQUARE_NB * SQUARE_NB / 2;

constexpr Index block_offset(const u8 block) { return Index(block) * BlockDimensions; }

// Number of feature dimensions (67,584 piece-squares)
inline constexpr u32 Dimensions = BlockCount * BlockDimensions;

// Maximum number of simultaneously active features
inline constexpr Index MaxActiveDimensions = 32;

using IndexList = StaticVector<Index, MaxActiveDimensions, Index>;
using DirtyType = DirtyPiece;

// Append lists of indices for recently changed features from the piece map
void append_map_changed_indices(Color           perspective,
                                Square          kingSq,
                                const PieceMap& oldPieceMap,
                                const PieceMap& newPieceMap,
                                Bitboard        removedBB,
                                Bitboard        addedBB,
                                IndexList&      removed,
                                IndexList&      added) noexcept;
// Append lists of indices for recently changed features
void append_changed_indices(Color            perspective,
                            Square           kingSq,
                            const DirtyType& dP,
                            IndexList&       removed,
                            IndexList&       added) noexcept;

void append_changed_indices_both(Square                      wKingSq,
                                 Square                      bKingSq,
                                 const DirtyType&            dP,
                                 Array<IndexList, COLOR_NB>& removed,
                                 Array<IndexList, COLOR_NB>& added) noexcept;

// Determine if a full refresh is required based on the dirty piece
bool refresh_required(Color            perspective,
                      const DirtyType& dP,
                      const Block&     beforeBlock,
                      const Block&     afterBlock) noexcept;
}  // namespace NNUE::Features::K32H3
}  // namespace DON

#endif  // NNUE_FEATURES_K32H3_H_INCLUDED
