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

// Definition of input features FullThreats of NNUE evaluation function

#ifndef NNUE_FEATURES_FULL_THREATS_INCLUDED
#define NNUE_FEATURES_FULL_THREATS_INCLUDED

#include <cstdint>

#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace DON {

namespace NNUE::Features {

// Feature FullThreats:
class FullThreats final {
   private:
    FullThreats() noexcept                              = delete;
    FullThreats(const FullThreats&) noexcept            = delete;
    FullThreats(FullThreats&&) noexcept                 = delete;
    FullThreats& operator=(const FullThreats&) noexcept = delete;
    FullThreats& operator=(FullThreats&&) noexcept      = delete;

   public:
    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t Hash = 0x8F234CB8U;

    // Number of feature dimensions
    static constexpr IndexType Dimensions = 79856;

    struct FusedData final {
        Bitboard dp2removedOriginBB = 0;
        Bitboard dp2removedTargetBB = 0;

        Square dp2removedSq;
    };

    // Maximum number of simultaneously active features.
    static constexpr IndexType MaxActiveDimensions = 128;

    using DirtyType = DirtyThreats;
    using IndexList = FixedVector<IndexType, MaxActiveDimensions>;

    static void init() noexcept;

    // Get a list of indices for active features
    static void
    append_active_indices(Color perspective, const Position& pos, IndexList& active) noexcept;

    // Get a list of indices for recently changed features
    static void append_changed_indices(Color            perspective,
                                       Square           kingSq,
                                       const DirtyType& dt,
                                       IndexList&       removed,
                                       IndexList&       added,
                                       FusedData*       fd    = nullptr,
                                       bool             first = false) noexcept;

    // Returns whether the change stored in this DirtyType means
    // that a full accumulator refresh is required.
    static bool requires_refresh(Color perspective, const DirtyType& dt) noexcept;
};

}  // namespace NNUE::Features
}  // namespace DON

#endif  // #ifndef NNUE_FEATURES_FULL_THREATS_INCLUDED
