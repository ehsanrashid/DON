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

#include "../../misc.h"
#include "../../types.h"
#include "../ntypes.h"
#include "full_threats.h"

namespace DON {

class Position;

namespace NNUE::Features {

class PP3Wide final {
   public:
    static constexpr u32 Hash = 0x86F2B1DDu;

    static constexpr u16 PawnIds    = 48 * COLOR_NB;
    static constexpr u16 Dimensions = PawnIds * (PawnIds - 1) / 2;

    // Pawn pair feature indices are concatenated to threats so this must equal ThreatFeatureSet::Dimensions;
    // see nnue/feature_transformer.h
    static constexpr u16 IndexBase = FullThreats::Dimensions;
    // Threats and pawn-pair features are concatenated into one array to allow for a single index to address either.
    // The first pawn-pair feature is at index FullThreats::Dimensions.
    static_assert(IndexBase == FullThreats::Dimensions);

    // Maximum number of simultaneously active features
    static constexpr u16 MaxActiveDimensions = 256;
    using IndexVector                        = FixedVector<u16, MaxActiveDimensions, u16>;
    using DirtyType                          = DirtyPawnPairs;

    static void
    append_active_indices(Color perspective, const Position& pos, IndexVector& active) noexcept;

    static void append_changed_indices(Color                   perspective,
                                       Square                  ksq,
                                       const DirtyType&        dPps,
                                       IndexVector&            removed,
                                       IndexVector&            added,
                                       const ThreatWeightType* pfBase   = nullptr,
                                       usize                   pfStride = 0) noexcept;

   private:
    PP3Wide() noexcept                          = delete;
    ~PP3Wide() noexcept                         = delete;
    PP3Wide(const PP3Wide&) noexcept            = delete;
    PP3Wide& operator=(const PP3Wide&) noexcept = delete;
    PP3Wide(PP3Wide&&) noexcept                 = delete;
    PP3Wide& operator=(PP3Wide&&) noexcept      = delete;
};

}  // namespace NNUE::Features
}  // namespace DON

#endif  // NNUE_FEATURES_PP_3WIDE_INCLUDED
