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

#ifndef NNUE_FEATURES_PP_3WIDE_INCLUDED
#define NNUE_FEATURES_PP_3WIDE_INCLUDED

#include "../../types.h"
#include "../../nnue/types.h"
#include "../../utility/static_vector.h"
#include "../../utility/types.h"

namespace DON {

class Position;

// Feature PP3Wide:
namespace NNUE::Features::PP3Wide {
inline constexpr u32 Hash = u32{0x5C80FA69};

inline constexpr Index PawnIds    = 48 * COLOR_NB;
inline constexpr Index Dimensions = PawnIds * (PawnIds - 1) / 2;

// Pawn pair feature indices are concatenated to threats so this must equal Features::FullThreats::Dimensions;
// see nnue/feature_transformer.h
inline constexpr Index BaseIndex = 59808;
// Threats and pawn-pair features are concatenated into one array to allow for a single index to address either.
// The first pawn-pair feature is at index FullThreats::Dimensions.
static_assert(BaseIndex == 59808);

// Maximum number of simultaneously active features
inline constexpr Index MaxActiveDimensions = 256;

using IndexList = StaticVector<Index, MaxActiveDimensions, Index>;
using DirtyType = DirtyPawnPairs;

void append_active_indices(Color           perspective,  //
                           const Position& pos,
                           IndexList&      active) noexcept;

void append_changed_indices(Color               perspective,
                            Square              ksq,
                            const DirtyType&    dPps,
                            IndexList&          removed,
                            IndexList&          added,
                            const ThreatWeight* pfBase   = nullptr,
                            usize               pfStride = 0) noexcept;

void append_changed_indices_both(Square                      wKingSq,
                                 Square                      bKingSq,
                                 const DirtyType&            dPps,
                                 Array<IndexList, COLOR_NB>& removed,
                                 Array<IndexList, COLOR_NB>& added,
                                 const ThreatWeight*         pfBase   = nullptr,
                                 usize                       pfStride = 0) noexcept;
}  // namespace NNUE::Features::PP3Wide
}  // namespace DON

#endif  // NNUE_FEATURES_PP_3WIDE_INCLUDED
