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

#include <array>
#include <cstdint>
#include <cstring>
#include <iosfwd>

#include "../misc.h"
#include "common.h"
#include "features/full_threats.h"
#include "features/half_ka_v2_hm.h"
#include "layers/affine_transform.h"
#include "layers/affine_transform_sparse_input.h"
#include "layers/clipped_relu.h"
#include "layers/sqr_clipped_relu.h"

namespace DON::NNUE {

// Input features used in evaluation function
using PSQFeatureSet    = Features::HalfKAv2_hm;
using ThreatFeatureSet = Features::FullThreats;

// Number of input feature dimensions after conversion
inline constexpr IndexType     BigTransformedFeatureDimensions = 1024;
inline constexpr std::uint32_t BigL2                           = 15;
inline constexpr std::uint32_t BigL3                           = 32;

inline constexpr IndexType     SmallTransformedFeatureDimensions = 128;
inline constexpr std::uint32_t SmallL2                           = 15;
inline constexpr std::uint32_t SmallL3                           = 32;

inline constexpr IndexType PSQTBuckets = 8;
inline constexpr IndexType LayerStacks = 8;

static_assert(LayerStacks == PSQTBuckets);

// If vector instructions are enabled, we update and refresh the
// accumulator tile by tile such that each tile fits in the CPU's
// vector registers.
static_assert(PSQTBuckets % 8 == 0,
              "Per feature PSQT values cannot be processed at granularity lower than 8 at a time.");

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
    static constexpr std::uint32_t hash() noexcept {
        // input slice hash
        std::uint32_t h = 0xEC42E90DU;
        h ^= 2 * TransformedFeatureDimensions;

        h = decltype(fc_0)::hash(h);
        h = decltype(ac_0)::hash(h);
        h = decltype(fc_1)::hash(h);
        h = decltype(ac_1)::hash(h);
        h = decltype(fc_2)::hash(h);
        return h;
    }

    std::size_t content_hash() const noexcept {
        std::size_t h = 0;
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
    std::int32_t propagate(const StdArray<TransformedFeatureType, TransformedFeatureDimensions>&
                             transformedFeatures) const noexcept {

        struct alignas(CACHE_LINE_SIZE) Buffer final {
            alignas(CACHE_LINE_SIZE) typename decltype(fc_0)::OutputBuffer fc_0_out;
            alignas(CACHE_LINE_SIZE)
              StdArray<typename decltype(ac_sqr_0)::OutputType,
                       ceil_to_multiple<IndexType>(FC_0_Outputs * 2, 32)> ac_sqr_0_out;
            alignas(CACHE_LINE_SIZE) typename decltype(ac_0)::OutputBuffer ac_0_out;
            alignas(CACHE_LINE_SIZE) typename decltype(fc_1)::OutputBuffer fc_1_out;
            alignas(CACHE_LINE_SIZE) typename decltype(ac_1)::OutputBuffer ac_1_out;
            alignas(CACHE_LINE_SIZE) typename decltype(fc_2)::OutputBuffer fc_2_out;

            Buffer() noexcept { std::memset(this, 0, sizeof(*this)); }
        };

#if defined(__clang__) && defined(__APPLE__)
        // workaround for a bug reported with xcode 12
        static thread_local auto bufferPtr = std::make_unique<Buffer>();
        // Access TLS only once, cache result.
        Buffer& buffer = *bufferPtr;
#else
        alignas(CACHE_LINE_SIZE) static thread_local Buffer buffer;
#endif

        fc_0.propagate(transformedFeatures.data(), buffer.fc_0_out.data());
        ac_sqr_0.propagate(buffer.fc_0_out.data(), buffer.ac_sqr_0_out.data());
        ac_0.propagate(buffer.fc_0_out.data(), buffer.ac_0_out.data());
        std::memcpy(&buffer.ac_sqr_0_out[FC_0_Outputs], buffer.ac_0_out.data(),
                    FC_0_Outputs * sizeof(typename decltype(ac_0)::OutputType));
        fc_1.propagate(buffer.ac_sqr_0_out.data(), buffer.fc_1_out.data());
        ac_1.propagate(buffer.fc_1_out.data(), buffer.ac_1_out.data());
        fc_2.propagate(buffer.ac_1_out.data(), buffer.fc_2_out.data());

        // buffer.fc_0_out[FC_0_OUTPUTS] is such that 1.0 is equal to 127 * (1 << WEIGHT_SCALE_BITS)
        // in quantized form, but want 1.0 to be equal to 600 * OUTPUT_SCALE
        std::int32_t fwdOut =
          (buffer.fc_0_out[FC_0_Outputs]) * (600 * OUTPUT_SCALE) / (127 * (1 << WEIGHT_SCALE_BITS));
        std::int32_t outputValue = buffer.fc_2_out[0] + fwdOut;

        return outputValue;
    }
};

}  // namespace DON::NNUE

template<DON::NNUE::IndexType L1, std::uint32_t L2, std::uint32_t L3>
struct std::hash<DON::NNUE::NetworkArchitecture<L1, L2, L3>> {
    std::size_t operator()(const DON::NNUE::NetworkArchitecture<L1, L2, L3>& arch) const noexcept {
        return arch.content_hash();
    }
};

#endif  // #ifndef NNUE_ARCHITECTURE_H_INCLUDED
