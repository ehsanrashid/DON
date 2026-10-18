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

// Definition of input features K32Q2 of NNUE evaluation function

#ifndef NNUE_FEATURES_K32Q2_H_INCLUDED
#define NNUE_FEATURES_K32Q2_H_INCLUDED

#include "../../types.h"
#include "../../nnue/types.h"
#include "../../utility/static_vector.h"
#include "../../utility/types.h"

namespace DON::NNUE::Features {

// Feature K32Q2: Combination of the position of own king (32 buckets), opponent queen presence (2 states),
// and piece-square positions. Position mirrored such that king is always on e..h files.
class K32Q2 final {
   private:
    K32Q2() noexcept                        = delete;
    ~K32Q2() noexcept                       = delete;
    K32Q2(const K32Q2&) noexcept            = delete;
    K32Q2& operator=(const K32Q2&) noexcept = delete;
    K32Q2(K32Q2&&) noexcept                 = delete;
    K32Q2& operator=(K32Q2&&) noexcept      = delete;

   public:
    // Hash value embedded in the evaluation file
    static constexpr u32 Hash = u32{0x32B5E284};

    // Number of feature dimensions (45,056 piece-squares)
    static constexpr Index Dimensions = 11 * SQUARE_NB * SQUARE_NB;

    // Maximum number of simultaneously active features
    static constexpr Index MaxActiveDimensions = 32;

    using IndexList = StaticVector<Index, MaxActiveDimensions, Index>;
    using DirtyType = DirtyPiece;

    // Mirror square to have king always on e..h files
    // (file_of(s) >> 2) is 0 for 0...3, 1 for 4...7
    static constexpr Square orientation(const Square s) noexcept {
        return Square(((file_of(s) >> 2) ^ 1) * FILE_H);
    }

    // Append lists of indices for recently changed features from the piece map
    static void append_map_changed_indices(Color           perspective,
                                           Square          kingSq,
                                           bool            opponentHasQueen,
                                           const PieceMap& oldPieceMap,
                                           const PieceMap& newPieceMap,
                                           Bitboard        removedBB,
                                           Bitboard        addedBB,
                                           IndexList&      removed,
                                           IndexList&      added) noexcept;
    // Append lists of indices for recently changed features
    static void append_changed_indices(Color            perspective,
                                       Square           kingSq,
                                       bool             opponentHasQueen,
                                       const DirtyType& dP,
                                       IndexList&       removed,
                                       IndexList&       added) noexcept;

    static void append_changed_indices_both(Square                      wKingSq,
                                            Square                      bKingSq,
                                            bool                        bHasQueen,
                                            bool                        wHasQueen,
                                            const DirtyType&            dP,
                                            Array<IndexList, COLOR_NB>& removed,
                                            Array<IndexList, COLOR_NB>& added) noexcept;

    // Determine if a full refresh is required based on the dirty piece
    static bool refresh_required(Color perspective, const DirtyType& dP) noexcept;
};

static_assert(K32Q2::orientation(SQ_A1) == SQ_H1);
static_assert(K32Q2::orientation(SQ_D1) == SQ_H1);
static_assert(K32Q2::orientation(SQ_E1) == SQ_A1);
static_assert(K32Q2::orientation(SQ_H1) == SQ_A1);
static_assert(K32Q2::orientation(SQ_A8) == SQ_H1);
static_assert(K32Q2::orientation(SQ_H8) == SQ_A1);

}  // namespace DON::NNUE::Features

#endif  // NNUE_FEATURES_K32Q2_H_INCLUDED
