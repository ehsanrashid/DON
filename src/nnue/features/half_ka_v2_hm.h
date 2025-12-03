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

// Definition of input features HalfKP of NNUE evaluation function

#ifndef NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED
#define NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED

#include <cstdint>

#include "../../misc.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace DON {

namespace NNUE::Features {

// Feature HalfKAv2_hm: Combination of the position of own king and the position of pieces.
// Position mirrored such that king is always on e..h files.
class HalfKAv2_hm final {
   private:
    HalfKAv2_hm() noexcept                              = delete;
    HalfKAv2_hm(const HalfKAv2_hm&) noexcept            = delete;
    HalfKAv2_hm(HalfKAv2_hm&&) noexcept                 = delete;
    HalfKAv2_hm& operator=(const HalfKAv2_hm&) noexcept = delete;
    HalfKAv2_hm& operator=(HalfKAv2_hm&&) noexcept      = delete;

   public:
    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t Hash = 0x7F234CB8U;

    // Number of feature dimensions -> (PS_NB * SQUARE_NB) / 2
    static constexpr IndexType Dimensions = (11 * SQUARE_NB * SQUARE_NB) / 2;

    // Maximum number of simultaneously active features.
    static constexpr IndexType MaxActiveDimensions = 32;

    using DirtyType = DirtyPiece;
    using IndexList = FixedVector<IndexType, MaxActiveDimensions>;

    // Get a list of indices for active features
    static void append_active_indices(Color                             perspective,
                                      Square                            kingSq,
                                      const StdArray<Piece, SQUARE_NB>& pieceMap,
                                      Bitboard                          occupancyBB,
                                      IndexList&                        active) noexcept;

    // Get a list of indices for recently changed features
    static void append_changed_indices(Color            perspective,
                                       Square           kingSq,
                                       const DirtyType& dt,
                                       IndexList&       removed,
                                       IndexList&       added) noexcept;

    // Returns whether the change stored in this DirtyType means
    // that a full accumulator refresh is required.
    static bool requires_refresh(Color perspective, const DirtyType& dt) noexcept;
};

}  // namespace NNUE::Features
}  // namespace DON

#endif  // #ifndef NNUE_FEATURES_HALF_KA_V2_HM_H_INCLUDED
