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

// Input features and network structure used in NNUE evaluation function

#ifndef NNUE_ARCHITECTURE_H_INCLUDED
#define NNUE_ARCHITECTURE_H_INCLUDED

#include <cstdint>
#include <cstring>
#include <iosfwd>

#include "nnue_common.h"
#include "features/half_ka_v2_hm.h"
#include "layers/affine_transform.h"
#include "layers/affine_transform_sparse_input.h"
#include "layers/clipped_relu.h"
#include "layers/sqr_clipped_relu.h"

namespace DON::NNUE {

// Input features used in evaluation function
using FeatureSet = Features::HalfKAv2_hm;

// Number of input feature dimensions after conversion
constexpr IndexType     BigTransformedFeatureDimensions = 3072;
constexpr std::uint32_t BigL2                           = 15;
constexpr std::uint32_t BigL3                           = 32;

constexpr IndexType     SmallTransformedFeatureDimensions = 128;
constexpr std::uint32_t SmallL2                           = 15;
constexpr std::uint32_t SmallL3                           = 32;

constexpr IndexType PSQTBuckets = 8;
constexpr IndexType LayerStacks = 8;

static_assert(LayerStacks == PSQTBuckets);

template<IndexType L1, std::uint32_t L2, std::uint32_t L3>
struct NetworkArchitecture final {
    static constexpr IndexType     TransformedFeatureDimensions = L1;
    static constexpr std::uint32_t FC_0_Outputs                 = L2;
    static constexpr std::uint32_t FC_1_Outputs                 = L3;

    Layers::AffineTransformSparseInput<TransformedFeatureDimensions, FC_0_Outputs + 1> fc_0;
    Layers::SqrClippedReLU<FC_0_Outputs + 1>                                           ac_sqr_0;
    Layers::ClippedReLU<FC_0_Outputs + 1>                                              ac_0;
    Layers::AffineTransform<FC_0_Outputs * 2, FC_1_Outputs>                            fc_1;
    Layers::ClippedReLU<FC_1_Outputs>                                                  ac_1;
    Layers::AffineTransform<FC_1_Outputs, 1>                                           fc_2;

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() noexcept {
        // input slice hash
        std::uint32_t hashValue = 0xEC42E90Du;
        hashValue ^= 2 * TransformedFeatureDimensions;

        hashValue = decltype(fc_0)::get_hash_value(hashValue);
        hashValue = decltype(ac_0)::get_hash_value(hashValue);
        hashValue = decltype(fc_1)::get_hash_value(hashValue);
        hashValue = decltype(ac_1)::get_hash_value(hashValue);
        hashValue = decltype(fc_2)::get_hash_value(hashValue);

        return hashValue;
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) noexcept {
        return fc_0.read_parameters(stream) && ac_0.read_parameters(stream)
            && fc_1.read_parameters(stream) && ac_1.read_parameters(stream)
            && fc_2.read_parameters(stream);
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const noexcept {
        return fc_0.write_parameters(stream) && ac_0.write_parameters(stream)
            && fc_1.write_parameters(stream) && ac_1.write_parameters(stream)
            && fc_2.write_parameters(stream);
    }

    // Forward propagation
    std::int32_t propagate(const TransformedFeatureType* transformedFeatures) noexcept {
        struct alignas(CACHE_LINE_SIZE) Buffer final {
            alignas(CACHE_LINE_SIZE) typename decltype(fc_0)::OutputBuffer fc_0_out;
            alignas(CACHE_LINE_SIZE) typename decltype(ac_sqr_0)::OutputType
              ac_sqr_0_out[ceil_to_multiple<IndexType>(FC_0_Outputs * 2, 32)];
            alignas(CACHE_LINE_SIZE) typename decltype(ac_0)::OutputBuffer ac_0_out;
            alignas(CACHE_LINE_SIZE) typename decltype(fc_1)::OutputBuffer fc_1_out;
            alignas(CACHE_LINE_SIZE) typename decltype(ac_1)::OutputBuffer ac_1_out;
            alignas(CACHE_LINE_SIZE) typename decltype(fc_2)::OutputBuffer fc_2_out;

            Buffer() noexcept { std::memset(this, 0, sizeof(Buffer)); }
        };

#if defined(__clang__) && defined(__APPLE__)
        // workaround for a bug reported with xcode 12
        static thread_local auto bufferPtr = std::make_unique<Buffer>();
        // Access TLS only once, cache result.
        Buffer& buffer = *bufferPtr;
#else
        alignas(CACHE_LINE_SIZE) static thread_local Buffer buffer;
#endif

        fc_0.propagate(transformedFeatures, buffer.fc_0_out);
        ac_sqr_0.propagate(buffer.fc_0_out, buffer.ac_sqr_0_out);
        ac_0.propagate(buffer.fc_0_out, buffer.ac_0_out);
        std::memcpy(buffer.ac_sqr_0_out + FC_0_Outputs, buffer.ac_0_out,
                    FC_0_Outputs * sizeof(typename decltype(ac_0)::OutputType));
        fc_1.propagate(buffer.ac_sqr_0_out, buffer.fc_1_out);
        ac_1.propagate(buffer.fc_1_out, buffer.ac_1_out);
        fc_2.propagate(buffer.ac_1_out, buffer.fc_2_out);

        // buffer.fc_0_out[FC_0_OUTPUTS] is such that 1.0 is equal to 127 * (1 << WEIGHT_SCALE_BITS)
        // in quantized form, but want 1.0 to be equal to 600 * OUTPUT_SCALE
        std::int32_t fwdOut =
          (buffer.fc_0_out[FC_0_Outputs]) * (600 * OUTPUT_SCALE) / (127 * (1 << WEIGHT_SCALE_BITS));
        std::int32_t outputValue = buffer.fc_2_out[0] + fwdOut;

        return outputValue;
    }
};

}  // namespace DON::NNUE

#endif  // #ifndef NNUE_ARCHITECTURE_H_INCLUDED
