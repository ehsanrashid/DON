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

// Definition of input features FullThreats of NNUE evaluation function

#ifndef NNUE_FEATURES_FULL_THREATS_H_INCLUDED
#define NNUE_FEATURES_FULL_THREATS_H_INCLUDED

#include "../../types.h"
#include "../../nnue/types.h"
#include "../../utility/static_vector.h"
#include "../../utility/types.h"

namespace DON {

class Position;

namespace NNUE::Features {
// Feature FullThreats: Threats posed by pieces to opponent's pieces
namespace FullThreats {

// Hash value embedded in the evaluation file
inline constexpr u32 Hash = u32{0x2C41EAEA};

// Number of feature dimensions
inline constexpr Index Dimensions = 59808;

static_assert(Dimensions < u32{1} << 16);

// Maximum number of simultaneously active features
inline constexpr Index MaxActiveDimensions = 256;

using IndexList = StaticVector<Index, MaxActiveDimensions, Index>;
using DirtyType = DirtyThreats;

// Append list of indices for active features in ascending order
void append_active_indices(Color           perspective,  //
                           const Position& pos,
                           IndexList&      active) noexcept;
// Append lists of indices for recently changed features
void append_changed_indices(Color               perspective,
                            Square              kingSq,
                            const DirtyType&    dTs,
                            IndexList&          removed,
                            IndexList&          added,
                            const ThreatWeight* pfBase   = nullptr,
                            usize               pfStride = 0) noexcept;

void append_changed_indices_both(Square                      wKingSq,
                                 Square                      bKingSq,
                                 const DirtyType&            dTs,
                                 Array<IndexList, COLOR_NB>& removed,
                                 Array<IndexList, COLOR_NB>& added,
                                 const ThreatWeight*         pfBase   = nullptr,
                                 usize                       pfStride = 0) noexcept;
}  // namespace FullThreats
}  // namespace NNUE::Features
}  // namespace DON

#endif  // NNUE_FEATURES_FULL_THREATS_H_INCLUDED
