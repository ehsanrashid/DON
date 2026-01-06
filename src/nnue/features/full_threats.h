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

#ifndef NNUE_FEATURES_FULL_THREATS_H_INCLUDED
#define NNUE_FEATURES_FULL_THREATS_H_INCLUDED

#include <cstdint>

#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../common.h"

namespace DON {

namespace NNUE::Features {

// Feature FullThreats: Threats posed by pieces to opponent's pieces
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
        FusedData() = delete;
        FusedData(Square remSq) noexcept :
            dp2removedSq(remSq) {}

        Square   dp2removedSq;
        Bitboard dp2removedOriginBB = 0;
        Bitboard dp2removedTargetBB = 0;
    };

    // Maximum number of simultaneously active features.
    static constexpr IndexType MaxActiveDimensions = 128;

    using DirtyType = DirtyThreats;
    using IndexList = FixedVector<IndexType, MaxActiveDimensions>;

    static void append_active_indices(Color           perspective,  //
                                      const Position& pos,
                                      IndexList&      active) noexcept;

    static void append_changed_indices(Color            perspective,
                                       Square           kingSq,
                                       const DirtyType& dts,
                                       IndexList&       removed,
                                       IndexList&       added,
                                       FusedData*       fusedData = nullptr,
                                       bool             first     = false) noexcept;

    static bool refresh_required(Color perspective, const DirtyType& dts) noexcept;
};

}  // namespace NNUE::Features
}  // namespace DON

#endif  // #ifndef NNUE_FEATURES_FULL_THREATS_H_INCLUDED
