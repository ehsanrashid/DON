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

#include "../../misc.h"
#include "../../types.h"
#include "../ntypes.h"

namespace DON {

class Position;

namespace NNUE::Features {

// Feature FullThreats: Threats posed by pieces to opponent's pieces
class FullThreats final {
   public:
    // Hash value embedded in the evaluation file
    static constexpr u32 Hash = 0x2E6B9D04u;

    // Number of feature dimensions
    static constexpr IndexType Dimensions = 59808;

    // Maximum number of simultaneously active features
    static constexpr IndexType MaxActiveDimensions = 256;
    using IndexVector                              = FixedVector<u16, MaxActiveDimensions>;
    using DirtyType                                = DirtyThreats;

    // Mirror square to have king always on e..h files
    // (file_of(s) >> 2) is 0 for 0...3, 1 for 4...7
    static constexpr Square orientation(const Square s) noexcept {
        return Square(((file_of(s) >> 2) ^ 0) * FILE_H);
    }

    static void append_active_indices(Color           perspective,  //
                                      const Position& pos,
                                      IndexVector&    active) noexcept;

    static void append_changed_indices(Color                   perspective,
                                       Square                  kingSq,
                                       const DirtyType&        dTs,
                                       IndexVector&            removed,
                                       IndexVector&            added,
                                       const ThreatWeightType* pfBase   = nullptr,
                                       usize                   pfStride = 0) noexcept;

   private:
    FullThreats() noexcept                              = delete;
    ~FullThreats() noexcept                             = delete;
    FullThreats(const FullThreats&) noexcept            = delete;
    FullThreats& operator=(const FullThreats&) noexcept = delete;
    FullThreats(FullThreats&&) noexcept                 = delete;
    FullThreats& operator=(FullThreats&&) noexcept      = delete;
};

static_assert(FullThreats::orientation(SQ_A1) == SQ_A1);
static_assert(FullThreats::orientation(SQ_D1) == SQ_A1);
static_assert(FullThreats::orientation(SQ_E1) == SQ_H1);
static_assert(FullThreats::orientation(SQ_H1) == SQ_H1);
static_assert(FullThreats::orientation(SQ_A8) == SQ_A1);
static_assert(FullThreats::orientation(SQ_H8) == SQ_H1);

}  // namespace NNUE::Features
}  // namespace DON

#endif  // NNUE_FEATURES_FULL_THREATS_H_INCLUDED
