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

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <type_traits>
#include <utility>

#include "../memory.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_accumulator.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "simd.h"

namespace DON::NNUE {

// A class that converts the input features of the NNUE evaluation function

// Returns the inverse of a permutation
template<typename ArrayType, std::size_t Size = ArrayType{}.size()>
constexpr auto invert_permutation(const ArrayType& order) noexcept -> StdArray<std::size_t, Size> {
    StdArray<std::size_t, order.size()> inverse{};
    for (std::size_t i = 0; i < order.size(); ++i)
        inverse[order[i]] = i;
    return inverse;
}

// Divide a byte region of size TotalSize to chunks of size BlockSize,
// and permute the blocks by a given order
template<std::size_t BlockSize, typename ArrayType, typename OrderArrayType>
constexpr void permute(ArrayType& data, const OrderArrayType& order) noexcept {
    using T = typename ArrayType::value_type;

    constexpr std::size_t TotalSize = data.size() * sizeof(T);
    constexpr std::size_t ChunkSize = BlockSize * order.size();
    static_assert(TotalSize % ChunkSize == 0, "ChunkSize must perfectly divide TotalSize");

    auto* const byts = reinterpret_cast<unsigned char*>(data.data());

    for (std::size_t i = 0; i < TotalSize; i += ChunkSize)
    {
        auto* const values = &byts[i];

        StdArray<unsigned char, ChunkSize> buffer;
        for (std::size_t j = 0; j < order.size(); ++j)
        {
            auto* const chunkValue  = &values[order[j] * BlockSize];
            auto* const chunkBuffer = &buffer[j * BlockSize];
            std::memcpy(chunkBuffer, chunkValue, BlockSize);
        }
        std::memcpy(values, buffer.data(), ChunkSize);
    }
}

// Input feature converter
template<IndexType TransformedFeatureDimensions>
class FeatureTransformer final {

    static constexpr bool UseThreats =
      TransformedFeatureDimensions == BigTransformedFeatureDimensions;

    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions       = PSQFeatureSet::Dimensions;
    static constexpr IndexType ThreatInputDimensions = ThreatFeatureSet::Dimensions;
    static constexpr IndexType TotalInputDimensions =
      InputDimensions + (UseThreats ? ThreatInputDimensions : 0);

    static constexpr IndexType OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr std::size_t BufferSize = OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t hash() noexcept {
        return (UseThreats ? ThreatFeatureSet::Hash : PSQFeatureSet::Hash) ^ (OutputDimensions * 2);
    }

    std::size_t content_hash() const noexcept {
        std::size_t h = 0;
        combine_hash(h, raw_data_hash(biases));
        combine_hash(h, raw_data_hash(weights));
        combine_hash(h, raw_data_hash(psqtWeights));
        combine_hash(h, hash());
        return h;
    }

    // Store the order by which 128-bit blocks of a 1024-bit data must
    // be permuted so that calling packus on adjacent vectors of 16-bit
    // integers loaded from the data results in the pre-permutation order
    static constexpr auto PackusEpi16Order = []() -> StdArray<std::size_t, 8> {
#if defined(USE_AVX512)
        // _mm512_packus_epi16 after permutation:
        // |   0   |   2   |   4   |   6   | // Vector 0
        // |   1   |   3   |   5   |   7   | // Vector 1
        // | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | // Packed Result
        return {0, 2, 4, 6, 1, 3, 5, 7};
#elif defined(USE_AVX2)
        // _mm256_packus_epi16 after permutation:
        // |   0   |   2   |  |   4   |   6   | // Vector 0, 2
        // |   1   |   3   |  |   5   |   7   | // Vector 1, 3
        // | 0 | 1 | 2 | 3 |  | 4 | 5 | 6 | 7 | // Packed Result
        return {0, 2, 1, 3, 4, 6, 5, 7};
#else
        return {0, 1, 2, 3, 4, 5, 6, 7};
#endif
    }();

    static constexpr auto InversePackusEpi16Order = invert_permutation(PackusEpi16Order);


    template<bool Read>
    void permute_weights() noexcept {
        constexpr auto& Order = Read ? PackusEpi16Order : InversePackusEpi16Order;
        permute<16>(biases, Order);
        permute<16>(weights, Order);
        if (UseThreats)
            permute<8>(threatWeights, Order);
    }

    template<bool Read>
    void scale_weights() noexcept {
        for (IndexType i = 0; i < HalfDimensions; ++i)
            if constexpr (Read)
                biases[i] *= 2;
            else
                biases[i] /= 2;

        for (IndexType i = 0; i < HalfDimensions * InputDimensions; ++i)
            if constexpr (Read)
                weights[i] *= 2;
            else
                weights[i] /= 2;
    }

    // Read network parameters
    bool read_parameters(std::istream& istream) noexcept {

        read_leb_128(istream, biases);

        if (UseThreats)
        {
            auto combinedWeights =
              std::make_unique<StdArray<WeightType, TotalInputDimensions * HalfDimensions>>();
            auto combinedPsqtWeights =
              std::make_unique<StdArray<PSQTWeightType, TotalInputDimensions * PSQTBuckets>>();

            read_leb_128(istream, *combinedWeights);

            std::copy(combinedWeights->begin(),
                      combinedWeights->begin() + ThreatInputDimensions * HalfDimensions,
                      threatWeights.begin());

            std::copy(combinedWeights->begin() + ThreatInputDimensions * HalfDimensions,
                      combinedWeights->begin() + TotalInputDimensions * HalfDimensions,
                      weights.begin());

            read_leb_128(istream, *combinedPsqtWeights);

            std::copy(combinedPsqtWeights->begin(),
                      combinedPsqtWeights->begin() + ThreatInputDimensions * PSQTBuckets,
                      threatPsqtWeights.begin());

            std::copy(combinedPsqtWeights->begin() + ThreatInputDimensions * PSQTBuckets,
                      combinedPsqtWeights->begin() + TotalInputDimensions * PSQTBuckets,
                      psqtWeights.begin());
        }
        else
        {
            read_leb_128(istream, weights);
            read_leb_128(istream, psqtWeights);
        }

        permute_weights<true>();
        if (UseThreats)
        {}
        else
        {
            scale_weights<true>();
        }

        return !istream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& ostream) const noexcept {
        std::unique_ptr<FeatureTransformer> copy = std::make_unique<FeatureTransformer>(*this);

        copy->template permute_weights<false>();
        if (UseThreats)
        {}
        else
        {
            copy->template scale_weights<false>();
        }

        write_leb_128(ostream, copy->biases);

        if (UseThreats)
        {
            auto combinedWeights =
              std::make_unique<StdArray<WeightType, TotalInputDimensions * HalfDimensions>>();
            auto combinedPsqtWeights =
              std::make_unique<StdArray<PSQTWeightType, TotalInputDimensions * PSQTBuckets>>();

            std::copy(copy->threatWeights.begin(),
                      copy->threatWeights.begin() + ThreatInputDimensions * HalfDimensions,
                      combinedWeights->begin());

            std::copy(copy->weights.begin(),
                      copy->weights.begin() + InputDimensions * HalfDimensions,
                      combinedWeights->begin() + ThreatInputDimensions * HalfDimensions);

            write_leb_128(ostream, *combinedWeights);

            std::copy(copy->threatPsqtWeights.begin(),
                      copy->threatPsqtWeights.begin() + ThreatInputDimensions * PSQTBuckets,
                      combinedPsqtWeights->begin());

            std::copy(copy->psqtWeights.begin(),
                      copy->psqtWeights.begin() + InputDimensions * PSQTBuckets,
                      combinedPsqtWeights->begin() + ThreatInputDimensions * PSQTBuckets);

            write_leb_128(ostream, *combinedPsqtWeights);
        }
        else
        {
            write_leb_128(ostream, copy->weights);
            write_leb_128(ostream, copy->psqtWeights);
        }

        return !ostream.fail();
    }

    // Convert input features
    std::int32_t transform(const Position&                           pos,
                           AccumulatorStack&                         accStack,
                           AccumulatorCaches::Cache<HalfDimensions>& cache,
                           std::size_t                               bucket,
                           StdArray<OutputType, BufferSize>&         output) const {
        using namespace SIMD;

        const StdArray<Color, COLOR_NB> perspectives{pos.active_color(), ~pos.active_color()};

        accStack.evaluate(pos, *this, cache);

        const auto& accState       = accStack.state<PSQFeatureSet>();
        const auto& threatAccState = accStack.state<ThreatFeatureSet>();

        const auto& psqtAccumulation = (accState.acc<HalfDimensions>()).psqtAccumulation;
        const auto& threatPsqtAccumulation =
          (threatAccState.acc<HalfDimensions>()).psqtAccumulation;

        auto psqt = psqtAccumulation[perspectives[0]][bucket]  //
                  - psqtAccumulation[perspectives[1]][bucket];
        if (UseThreats)
            psqt += threatPsqtAccumulation[perspectives[0]][bucket]
                  - threatPsqtAccumulation[perspectives[1]][bucket];

        psqt /= 2;

        const auto& accumulation       = (accState.acc<HalfDimensions>()).accumulation;
        const auto& threatAccumulation = (threatAccState.acc<HalfDimensions>()).accumulation;

        for (IndexType p = 0; p < 2; ++p)
        {
            IndexType offset = p * (HalfDimensions / 2);

#if defined(VECTOR)
            constexpr IndexType OutputChunkSize = MaxChunkSize;
            static_assert((HalfDimensions / 2) % OutputChunkSize == 0);
            constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

            vec_t* out = reinterpret_cast<vec_t*>(&output[offset]);
            // clang-format off
            const vec_t* in0 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][0]));
            const vec_t* in1 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
            // clang-format on

            // Per the NNUE architecture, here we want to multiply pairs of
            // clipped elements and divide the product by 128. To do this,
            // we can naively perform min/max operation to clip each of the
            // four int16 vectors, mullo pairs together, then pack them into
            // one int8 vector. However, there exists a faster way.

            // The idea here is to use the implicit clipping from packus to
            // save us two vec_max_16 instructions. This clipping works due
            // to the fact that any int16 integer below zero will be zeroed
            // on packus.

            // Consider the case where the second element is negative.
            // If we do standard clipping, that element will be zero, which
            // means our pairwise product is zero. If we perform packus and
            // remove the lower-side clip for the second element, then our
            // product before packus will be negative, and is zeroed on pack.
            // The two operation produce equivalent results, but the second
            // one (using packus) saves one max operation per pair.

            // But here we run into a problem: mullo does not preserve the
            // sign of the multiplication. We can get around this by doing
            // mulhi, which keeps the sign. But that requires an additional
            // tweak.

            // mulhi cuts off the last 16 bits of the resulting product,
            // which is the same as performing a rightward shift of 16 bits.
            // We can use this to our advantage. Recall that we want to
            // divide the final product by 128, which is equivalent to a
            // 7-bit right shift. Intuitively, if we shift the clipped
            // value left by 9, and perform mulhi, which shifts the product
            // right by 16 bits, then we will net a right shift of 7 bits.
            // However, this won't work as intended. Since we clip the
            // values to have a maximum value of 127, shifting it by 9 bits
            // might occupy the signed bit, resulting in some positive
            // values being interpreted as negative after the shift.

            // There is a way, however, to get around this limitation. When
            // loading the network, scale accumulator weights and biases by
            // 2. To get the same pairwise multiplication result as before,
            // we need to divide the product by 128 * 2 * 2 = 512, which
            // amounts to a right shift of 9 bits. So now we only have to
            // shift left by 7 bits, perform mulhi (shifts right by 16 bits)
            // and net a 9 bit right shift. Since we scaled everything by
            // two, the values are clipped at 127 * 2 = 254, which occupies
            // 8 bits. Shifting it by 7 bits left will no longer occupy the
            // signed bit, so we are safe.

            // Note that on NEON processors, we shift left by 6 instead
            // because the instruction "vqdmulhq_s16" also doubles the
            // return value after the multiplication, adding an extra shift
            // to the left by 1, so we compensate by shifting less before
            // the multiplication.
            const vec_t Zero = vec_zero();
            const vec_t One  = vec_set_16(UseThreats ? 255 : 127 * 2);

            constexpr int shift =
    #if defined(USE_SSE2)
              7
    #else
              6
    #endif
              ;

            if (UseThreats)
            {
                // clang-format off
                const vec_t* tin0 = reinterpret_cast<const vec_t*>(&(threatAccumulation[perspectives[p]][0]));
                const vec_t* tin1 = reinterpret_cast<const vec_t*>(&(threatAccumulation[perspectives[p]][HalfDimensions / 2]));
                // clang-format on
                for (IndexType j = 0; j < NumOutputChunks; ++j)
                {
                    vec_t acc00 = vec_add_16(in0[j * 2 + 0], tin0[j * 2 + 0]);
                    vec_t acc01 = vec_add_16(in0[j * 2 + 1], tin0[j * 2 + 1]);
                    vec_t acc10 = vec_add_16(in1[j * 2 + 0], tin1[j * 2 + 0]);
                    vec_t acc11 = vec_add_16(in1[j * 2 + 1], tin1[j * 2 + 1]);

                    vec_t sum00 = vec_slli_16(vec_max_16(vec_min_16(acc00, One), Zero), shift);
                    vec_t sum01 = vec_slli_16(vec_max_16(vec_min_16(acc01, One), Zero), shift);
                    vec_t sum10 = vec_min_16(acc10, One);
                    vec_t sum11 = vec_min_16(acc11, One);

                    vec_t p0 = vec_mulhi_16(sum00, sum10);
                    vec_t p1 = vec_mulhi_16(sum01, sum11);

                    out[j] = vec_packus_16(p0, p1);
                }
            }
            else
            {
                for (IndexType j = 0; j < NumOutputChunks; ++j)
                {
                    vec_t acc00 = in0[j * 2 + 0];
                    vec_t acc01 = in0[j * 2 + 1];
                    vec_t acc10 = in1[j * 2 + 0];
                    vec_t acc11 = in1[j * 2 + 1];

                    vec_t sum00 = vec_slli_16(vec_max_16(vec_min_16(acc00, One), Zero), shift);
                    vec_t sum01 = vec_slli_16(vec_max_16(vec_min_16(acc01, One), Zero), shift);
                    vec_t sum10 = vec_min_16(acc10, One);
                    vec_t sum11 = vec_min_16(acc11, One);

                    vec_t p0 = vec_mulhi_16(sum00, sum10);
                    vec_t p1 = vec_mulhi_16(sum01, sum11);

                    out[j] = vec_packus_16(p0, p1);
                }
            }
#else
            for (IndexType j = 0; j < HalfDimensions / 2; ++j)
            {
                BiasType sum0 = accumulation[perspectives[p]][j + 0];
                BiasType sum1 = accumulation[perspectives[p]][j + HalfDimensions / 2];

                if (UseThreats)
                {
                    BiasType tsum0 = threatAccumulation[perspectives[p]][j + 0];
                    BiasType tsum1 = threatAccumulation[perspectives[p]][j + HalfDimensions / 2];

                    sum0 = std::clamp<BiasType>(sum0 + tsum0, 0, 255);
                    sum1 = std::clamp<BiasType>(sum1 + tsum1, 0, 255);
                }
                else
                {
                    sum0 = std::clamp<BiasType>(sum0, 0, 127 * 2);
                    sum1 = std::clamp<BiasType>(sum1, 0, 127 * 2);
                }

                output[offset + j] = OutputType(unsigned(sum0 * sum1) / 512);
            }
#endif
        }

        return psqt;
    }

    // clang-format off
    alignas(CACHE_LINE_SIZE) StdArray<BiasType, HalfDimensions> biases;
    alignas(CACHE_LINE_SIZE) StdArray<WeightType, InputDimensions * HalfDimensions> weights;
    alignas(CACHE_LINE_SIZE) StdArray<PSQTWeightType, InputDimensions * PSQTBuckets> psqtWeights;
    alignas(CACHE_LINE_SIZE) StdArray<ThreatWeightType, UseThreats ? ThreatInputDimensions * HalfDimensions : 0> threatWeights;
    alignas(CACHE_LINE_SIZE) StdArray<PSQTWeightType, UseThreats ? ThreatInputDimensions * PSQTBuckets : 0> threatPsqtWeights;
    // clang-format on
};

}  // namespace DON::NNUE

template<DON::NNUE::IndexType TransformedFeatureDimensions>
struct std::hash<DON::NNUE::FeatureTransformer<TransformedFeatureDimensions>> {
    std::size_t operator()(
      const DON::NNUE::FeatureTransformer<TransformedFeatureDimensions>& ft) const noexcept {
        return ft.content_hash();
    }
};

#endif  // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
