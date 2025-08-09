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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <type_traits>
#include <utility>

#include "../memory.h"
#include "../position.h"
#include "../types.h"
#include "nnue_accumulator.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "simd.h"

namespace DON::NNUE {

// A class that converts the input features of the NNUE evaluation function

// Returns the inverse of a permutation
template<std::size_t Size>
constexpr std::array<std::size_t, Size>
invert_permutation(const std::array<std::size_t, Size>& order) noexcept {
    std::array<std::size_t, Size> inverse{};
    for (std::size_t i = 0; i < order.size(); ++i)
        inverse[order[i]] = i;
    return inverse;
}

// Divide a byte region of size TotalSize to chunks of size BlockSize,
// and permute the blocks by a given order
template<std::size_t BlockSize, typename T, std::size_t N, std::size_t OrderSize>
void permute(T (&data)[N], const std::array<std::size_t, OrderSize>& order) noexcept {
    constexpr std::size_t TotalSize = N * sizeof(T);
    constexpr std::size_t ChunkSize = BlockSize * OrderSize;
    static_assert(TotalSize % ChunkSize == 0, "ChunkSize must perfectly divide TotalSize");

    std::byte* const byts = reinterpret_cast<std::byte*>(data);

    for (std::size_t i = 0; i < TotalSize; i += ChunkSize)
    {
        std::array<std::byte, ChunkSize> buffer{};

        std::byte* const values = &byts[i];

        for (std::size_t j = 0; j < OrderSize; ++j)
        {
            auto* const bufferChunk = &buffer[j * BlockSize];
            auto* const valueChunk  = &values[order[j] * BlockSize];

            std::copy(valueChunk, valueChunk + BlockSize, bufferChunk);
        }

        std::copy(std::begin(buffer), std::end(buffer), values);
    }
}

// Input feature converter
template<IndexType TransformedFeatureDimensions>
class FeatureTransformer final {

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = FeatureSet::Dimensions;
    static constexpr IndexType OutputDimensions = TransformedFeatureDimensions;

    // Size of forward propagation buffer
    static constexpr std::size_t BufferSize = OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() noexcept {
        return FeatureSet::HashValue ^ (2 * OutputDimensions);
    }

    // Store the order by which 128-bit blocks of a 1024-bit data must
    // be permuted so that calling packus on adjacent vectors of 16-bit
    // integers loaded from the data results in the pre-permutation order
    static constexpr auto PackusEpi16Order = []() -> std::array<std::size_t, 8> {
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
        permute<16>(biases, Read ? PackusEpi16Order : InversePackusEpi16Order);
        permute<16>(weights, Read ? PackusEpi16Order : InversePackusEpi16Order);
    }

    template<bool Read>
    void scale_weights() noexcept {
        for (IndexType i = 0; i < TransformedFeatureDimensions; ++i)
            biases[i] *= (Read ? 2.0f : 0.5f);

        for (IndexType j = 0; j < InputDimensions; ++j)
            for (IndexType i = 0; i < TransformedFeatureDimensions; ++i)
                weights[j * TransformedFeatureDimensions + i] *= (Read ? 2.0f : 0.5f);
    }

    // Read network parameters
    bool read_parameters(std::istream& istream) noexcept {

        read_leb_128(istream, biases, std::size(biases));
        read_leb_128(istream, weights, std::size(weights));
        read_leb_128(istream, psqtWeights, std::size(psqtWeights));

        permute_weights<true>();
        scale_weights<true>();

        return !istream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& ostream) noexcept {

        permute_weights<false>();
        scale_weights<false>();

        write_leb_128(ostream, biases, std::size(biases));
        write_leb_128(ostream, weights, std::size(weights));
        write_leb_128(ostream, psqtWeights, std::size(psqtWeights));

        permute_weights<true>();
        scale_weights<true>();

        return !ostream.fail();
    }

    // Convert input features
    std::int32_t transform(const Position&                      pos,
                           AccumulatorStack&                    accStack,
                           Cache<TransformedFeatureDimensions>* cache,
                           OutputType*                          output,
                           int                                  bucket) const noexcept {
        using namespace SIMD;

        accStack.evaluate(pos, *this, *cache);
        const auto& accumulatorState = accStack.clatest_state();

        const Color perspectives[COLOR_NB]{pos.active_color(), ~pos.active_color()};

        const auto& accumulation =
          (accumulatorState.acc<TransformedFeatureDimensions>()).accumulation;
        const auto& psqtAccumulation =
          (accumulatorState.acc<TransformedFeatureDimensions>()).psqtAccumulation;

        std::int32_t psqt =
          (psqtAccumulation[perspectives[0]][bucket] - psqtAccumulation[perspectives[1]][bucket])
          / 2;

        for (auto p = 0; p < COLOR_NB; ++p)
        {
            IndexType offset = p * (TransformedFeatureDimensions / 2);

#if defined(VECTOR)
            constexpr IndexType OutputChunkSize = MaxChunkSize;
            static_assert((TransformedFeatureDimensions / 2) % OutputChunkSize == 0);
            constexpr IndexType NumOutputChunks =
              TransformedFeatureDimensions / 2 / OutputChunkSize;

            // clang-format off
            const auto* in0 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][0]));
            const auto* in1 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][TransformedFeatureDimensions / 2]));
            auto*       out = reinterpret_cast<      vec_t*>(output + offset);
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
            const vec_t One  = vec_set_16(127 * 2);

            constexpr int shift =
    #if defined(USE_SSE2)
              7
    #else
              6
    #endif
              ;

            for (IndexType j = 0; j < NumOutputChunks; ++j)
            {
                vec_t sum0a = vec_slli_16(vec_max_16(vec_min_16(in0[j * 2 + 0], One), Zero), shift);
                vec_t sum0b = vec_slli_16(vec_max_16(vec_min_16(in0[j * 2 + 1], One), Zero), shift);
                vec_t sum1a = vec_min_16(in1[j * 2 + 0], One);
                vec_t sum1b = vec_min_16(in1[j * 2 + 1], One);

                vec_t pa = vec_mulhi_16(sum0a, sum1a);
                vec_t pb = vec_mulhi_16(sum0b, sum1b);

                out[j] = vec_packus_16(pa, pb);
            }
#else
            for (IndexType j = 0; j < TransformedFeatureDimensions / 2; ++j)
            {
                BiasType sum0 = accumulation[perspectives[p]][j + 0];
                BiasType sum1 = accumulation[perspectives[p]][j + TransformedFeatureDimensions / 2];

                sum0               = std::clamp<BiasType>(sum0, 0, 127 * 2);
                sum1               = std::clamp<BiasType>(sum1, 0, 127 * 2);
                output[offset + j] = unsigned(sum0 * sum1) / 512;
            }
#endif
        }

        return psqt;
    }

    alignas(CACHE_LINE_SIZE) BiasType biases[TransformedFeatureDimensions];
    alignas(CACHE_LINE_SIZE) WeightType weights[TransformedFeatureDimensions * InputDimensions];
    alignas(CACHE_LINE_SIZE) PSQTWeightType psqtWeights[PSQTBuckets * InputDimensions];
};

}  // namespace DON::NNUE

#endif  // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
