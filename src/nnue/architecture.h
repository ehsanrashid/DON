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

#include <cstring>
#include <functional>
#include <iosfwd>

#include "../misc.h"
#include "../types.h"
#include "features/full_threats.h"
#include "features/half_ka_hm.h"
#include "layers/affine_transform.h"
#include "layers/sparse_affine_transform.h"
#include "layers/clipped_relu.h"
#include "layers/sqr_clipped_relu.h"
#include "nnz.h"
#include "ntypes.h"

namespace DON::NNUE {

// Input features used in evaluation function
using ThreatFeatureSet = Features::FullThreats;
using PSQFeatureSet    = Features::HalfKA_hm;

struct NetworkArchitecture final {
   public:
    static constexpr IndexType TransformedFeatureDimensions = L1;
    static constexpr u32       FC_0_Outputs                 = L2;
    static constexpr u32       FC_1_Outputs                 = L3;

    // Hash value embedded in the evaluation file
    static constexpr u32 hash() noexcept {
        // input slice hash
        u32 h = 0xEC42E90Du;
        h ^= 2 * TransformedFeatureDimensions;

        h = decltype(fc_0)::hash(h);
        h = decltype(ac_0)::hash(h);
        h = decltype(fc_1)::hash(h);
        h = decltype(ac_1)::hash(h);
        h = decltype(fc_2)::hash(h);
        return h;
    }

    usize content_hash() const noexcept {
        usize h = 0;
        combine_hash(h, fc_0.content_hash());
        combine_hash(h, ac_sqr_0.content_hash());
        combine_hash(h, ac_0.content_hash());
        combine_hash(h, fc_1.content_hash());
        combine_hash(h, ac_1.content_hash());
        combine_hash(h, fc_2.content_hash());
        combine_hash(h, hash());
        return h;
    }

    // Read network parameters
    bool read_parameters(std::istream& is) noexcept {
        return fc_0.read_parameters(is)  //
            && ac_0.read_parameters(is)  //
            && fc_1.read_parameters(is)  //
            && ac_1.read_parameters(is)  //
            && fc_2.read_parameters(is);
    }

    // Write network parameters
    bool write_parameters(std::ostream& os) const noexcept {
        return fc_0.write_parameters(os)  //
            && ac_0.write_parameters(os)  //
            && fc_1.write_parameters(os)  //
            && ac_1.write_parameters(os)  //
            && fc_2.write_parameters(os);
    }

    // Forward propagation
    i32 propagate(
      const Array<TransformedFeatureType, TransformedFeatureDimensions>& transformedFeatures,
      const NNZ<L1>&                                                     nnz) const noexcept {

        struct alignas(CACHE_LINE_SIZE) Buffer final {
            alignas(CACHE_LINE_SIZE) typename decltype(fc_0)::OutputBuffer fc_0_out;
            alignas(CACHE_LINE_SIZE)
              Array<typename decltype(ac_sqr_0)::OutputType,
                    ceil_to_multiple<IndexType>(FC_0_Outputs * 2, 32)> ac_sqr_0_out;
            alignas(CACHE_LINE_SIZE) typename decltype(ac_0)::OutputBuffer ac_0_out;
            alignas(CACHE_LINE_SIZE) typename decltype(fc_1)::OutputBuffer fc_1_out;
            alignas(CACHE_LINE_SIZE) typename decltype(ac_1)::OutputBuffer ac_1_out;
            alignas(CACHE_LINE_SIZE) typename decltype(fc_2)::OutputBuffer fc_2_out;

            Buffer() noexcept { std::memset(ac_sqr_0_out.data(), 0, sizeof(ac_sqr_0_out)); }
        };

        Buffer buffer;

        fc_0.propagate(transformedFeatures.data(), buffer.fc_0_out.data(), nnz);
        ac_sqr_0.propagate(buffer.fc_0_out.data(), buffer.ac_sqr_0_out.data());
        ac_0.propagate(buffer.fc_0_out.data(), buffer.ac_0_out.data());
        std::memcpy(&buffer.ac_sqr_0_out[FC_0_Outputs], buffer.ac_0_out.data(),
                    FC_0_Outputs * sizeof(typename decltype(ac_0)::OutputType));
        fc_1.propagate(buffer.ac_sqr_0_out.data(), buffer.fc_1_out.data());
        ac_1.propagate(buffer.fc_1_out.data(), buffer.ac_1_out.data());
        fc_2.propagate(buffer.ac_1_out.data(), buffer.fc_2_out.data());

        // Max value for fwdOut is (L1 + L3) * 127 * WeightMax
        // for int8 activations and weights this is (L1 + L3) * 16129 making
        // fwdOut safe from overflow until (L1 + L3) > 133,144
        // first layer and last layer use WEIGHT_SCALE_BITS + 1.
        i32 fwdOut = buffer.fc_0_out[FC_0_Outputs] + buffer.fc_2_out[0];
        // fwdOut is such that 1.0 is equal to (1 << WEIGHT_SCALE_BITS) * HIDDEN_ONE *2
        // in quantized form, but want 1.0 to be equal to 600 * OUTPUT_SCALE
        // to make overflow impossible cast to i64.
        constexpr i64 Multiplier  = 600 * OUTPUT_SCALE;
        constexpr i64 Denominator = (i64{1} << WEIGHT_SCALE_BITS) * HIDDEN_ONE * 2;

        return static_cast<i32>((static_cast<i64>(fwdOut) * Multiplier) / Denominator);
    }

   private:
    Layers::SparseAffineTransform<TransformedFeatureDimensions, FC_0_Outputs + 1> fc_0;
    Layers::SqrClippedReLU<FC_0_Outputs + 1, WEIGHT_SCALE_BITS + 1>               ac_sqr_0;
    Layers::ClippedReLU<FC_0_Outputs + 1, WEIGHT_SCALE_BITS + 1>                  ac_0;
    Layers::AffineTransform<FC_0_Outputs * 2, FC_1_Outputs>                       fc_1;
    Layers::ClippedReLU<FC_1_Outputs, WEIGHT_SCALE_BITS>                          ac_1;
    Layers::AffineTransform<FC_1_Outputs, 1>                                      fc_2;
};

}  // namespace DON::NNUE

template<>
struct std::hash<DON::NNUE::NetworkArchitecture> {
    DON::usize operator()(const DON::NNUE::NetworkArchitecture& arch) const noexcept {
        return arch.content_hash();
    }
};

#endif  // NNUE_ARCHITECTURE_H_INCLUDED
