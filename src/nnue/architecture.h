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

// Input features and network structure used in NNUE evaluation function

#ifndef NNUE_ARCHITECTURE_H_INCLUDED
#define NNUE_ARCHITECTURE_H_INCLUDED

#include <functional>
#include <iosfwd>

#include "../nnue/layers/affine_transform.h"
#include "../nnue/layers/clipped_relu.h"
#include "../nnue/layers/sparse_affine_transform.h"
#include "../nnue/layers/sqr_clipped_relu.h"
#include "../nnue/types.h"
#include "../utility/types.h"

namespace DON::NNUE {

template<Index Dimensions>
struct NNZ;

struct NetworkArchitecture final {
   public:
    // Hash value embedded in the evaluation file
    static u32 hash() noexcept;

    usize content_hash() const noexcept;

    // Read network parameters
    bool read_parameters(std::istream& is) noexcept;

    // Write network parameters
    bool write_parameters(std::ostream& os) const noexcept;

    static constexpr Index TransformedFeatureDimensions = L1;
    static constexpr Index FC0Outputs                   = L2;
    static constexpr Index FC1Outputs                   = L3;

    // Forward propagation
    i32
    propagate(const Array<TransformedFeature, TransformedFeatureDimensions>& transformedFeatures,
              const NNZ<L1>&                                                 nnz) const noexcept;

   private:
    // clang-format off
    Layers::SparseAffineTransform<TransformedFeatureDimensions , FC0Outputs           > fc0;
    Layers::SqrClippedReLU       <FC0Outputs                   , WEIGHT_SCALE_BITS + 1> sqr_ac0;
    Layers::ClippedReLU          <FC0Outputs                   , WEIGHT_SCALE_BITS + 1> ac0;
    Layers::AffineTransform      <FC0Outputs * 2               , FC1Outputs           > fc1;
    Layers::SqrClippedReLU       <FC1Outputs                   , WEIGHT_SCALE_BITS    > sqr_ac1;
    Layers::ClippedReLU          <FC1Outputs                   , WEIGHT_SCALE_BITS    > ac1;
    Layers::AffineTransform      <(FC0Outputs + FC1Outputs) * 2, 1                    > fc2;

    struct alignas(CACHE_LINE_SIZE) Buffer final {
        alignas(CACHE_LINE_SIZE) typename decltype(fc0)::OutputBuffer fc_0_out;
        alignas(CACHE_LINE_SIZE) typename decltype(fc1)::OutputBuffer fc_1_out;
        alignas(CACHE_LINE_SIZE) typename decltype(fc2)::OutputBuffer fc_2_out;
        alignas(CACHE_LINE_SIZE) Array<typename decltype(sqr_ac0)::Output, ceil_to_multiple<Index>((FC0Outputs + FC1Outputs) * 2, 32)> concat_buffer;
    };
    // clang-format on
};

}  // namespace DON::NNUE

template<>
struct std::hash<DON::NNUE::NetworkArchitecture> {
    DON::usize operator()(const DON::NNUE::NetworkArchitecture& arch) const noexcept {
        return arch.content_hash();
    }
};

#endif  // NNUE_ARCHITECTURE_H_INCLUDED
