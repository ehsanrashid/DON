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

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <iosfwd>
#include <type_traits>
#include <utility>

#include "../memory.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "accumulator.h"
#include "architecture.h"
#include "nnz.h"
#include "ntypes.h"
#include "serialization.h"
#include "simd.h"

namespace DON::NNUE {

// A class that converts the input features of the NNUE evaluation function

// Returns the inverse of a permutation
template<usize Size>
constexpr Array<usize, Size> invert_permutation(const std::array<usize, Size>& order) noexcept {
    Array<usize, Size> inverse{};
    for (usize i = 0; i < order.size(); ++i)
        inverse[order[i]] = i;
    return inverse;
}

// Divide a byte region of size TotalSize to chunks of size BlockSize,
// and permute the blocks by a given order
template<usize BlockSize, typename T, usize DataSize, usize OrderSize>
constexpr void permute(std::array<T, DataSize>&            data,
                       const std::array<usize, OrderSize>& order) noexcept {
    constexpr usize TotalSize = DataSize * sizeof(T);
    constexpr usize ChunkSize = BlockSize * OrderSize;
    static_assert(TotalSize % ChunkSize == 0, "ChunkSize must perfectly divide TotalSize");

    auto* byts = reinterpret_cast<u8*>(data.data());

    for (usize i = 0; i < TotalSize; i += ChunkSize)
    {
        auto* values = &byts[i];

        Array<u8, ChunkSize> buffer;

        for (usize j = 0; j < OrderSize; ++j)
        {
            auto* valueChunk  = &values[order[j] * BlockSize];
            auto* bufferChunk = &buffer[j * BlockSize];

            std::memcpy(bufferChunk, valueChunk, BlockSize);
        }

        std::memcpy(values, buffer.data(), ChunkSize);
    }
}

// Input feature converter
class FeatureTransformer final {

    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = L1;

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions =
      PSQFeatureSet::Dimensions + ThreatFeatureSet::Dimensions;
    static constexpr IndexType OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr usize BufferSize = OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr u32 hash() noexcept {
        return combine_hashes({ThreatFeatureSet::Hash, PSQFeatureSet::Hash})
             ^ (2 * OutputDimensions);
    }

    // Store the order by which 128-bit blocks of a 1024-bit data must
    // be permuted so that calling packus on adjacent vectors of 16-bit
    // integers loaded from the data results in the pre-permutation order
    static constexpr auto PackusEpi16Order = []() -> Array<usize, 8> {
        return
#if defined(USE_AVX512)
          // _mm512_packus_epi16 after permutation:
          // |   0   |   2   |   4   |   6   | // Vector 0
          // |   1   |   3   |   5   |   7   | // Vector 1
          // | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | // Packed Result
          {0, 2, 4, 6, 1, 3, 5, 7};
#elif defined(USE_AVX2) || defined(USE_LASX)
          // _mm256_packus_epi16 after permutation:
          // |   0   |   2   |  |   4   |   6   | // Vector 0, 2
          // |   1   |   3   |  |   5   |   7   | // Vector 1, 3
          // | 0 | 1 | 2 | 3 |  | 4 | 5 | 6 | 7 | // Packed Result
          {0, 2, 1, 3, 4, 6, 5, 7};
#else
          {0, 1, 2, 3, 4, 5, 6, 7};
#endif
    }();

    static constexpr auto InversePackusEpi16Order = invert_permutation(PackusEpi16Order);

    usize content_hash() const noexcept {
        usize h = 0;

        combine_hash(h, hash_raw_data(biases));
        combine_hash(h, hash_raw_data(weights));
        combine_hash(h, hash_raw_data(psqtWeights));

        combine_hash(h, hash_raw_data(threatWeights));
        combine_hash(h, hash_raw_data(threatPsqtWeights));

        combine_hash(h, hash());

        return h;
    }

    template<bool Read>
    void permute_weights() noexcept {
        constexpr auto& Order = Read ? PackusEpi16Order : InversePackusEpi16Order;

        permute<16>(biases, Order);

        permute<16>(weights, Order);

        permute<8>(threatWeights, Order);
    }

    // Read network parameters
    bool read_parameters(std::istream& is) noexcept {

        read_leb_128(is, biases);

        read_little_endian(is, threatWeights);
        read_leb_128(is, threatPsqtWeights);

        read_leb_128(is, weights);
        read_leb_128(is, psqtWeights);

        permute_weights<true>();

        return !is.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& os) const noexcept {
        auto copy = std::make_unique<FeatureTransformer>(*this);

        copy->template permute_weights<false>();

        write_leb_128(os, copy->biases);

        write_little_endian(os, copy->threatWeights);
        write_leb_128(os, copy->threatPsqtWeights);

        write_leb_128(os, copy->weights);
        write_leb_128(os, copy->psqtWeights);

        return !os.fail();
    }

    // Convert input features
    i32 transform(const Position&                pos,
                  AccumulatorCache&              accCache,
                  AccumulatorStack&              accStack,
                  usize                          bucket,
                  NNZ<OutputDimensions>&         nnz,
                  Array<OutputType, BufferSize>& output) const noexcept {
        using namespace SIMD;

        accStack.evaluate(pos, *this, accCache);

        const auto& psqAccState    = accStack.state<PSQFeatureSet>();
        const auto& threatAccState = accStack.state<ThreatFeatureSet>();

        const auto& psqtAccumulation       = psqAccState.psqtAccumulation;
        const auto& threatPsqtAccumulation = threatAccState.psqtAccumulation;

        Array<Color, COLOR_NB> perspectives{pos.active_color(), ~pos.active_color()};

        auto psqt = psqtAccumulation[perspectives[WHITE]][bucket]
                  - psqtAccumulation[perspectives[BLACK]][bucket];

        psqt += threatPsqtAccumulation[perspectives[WHITE]][bucket]
              - threatPsqtAccumulation[perspectives[BLACK]][bucket];

        psqt /= 2;

        const auto& accumulation       = psqAccState.accumulation;
        const auto& threatAccumulation = threatAccState.accumulation;

        for (Color p : {WHITE, BLACK})
        {
            IndexType offset = p * (HalfDimensions / 2);

            [[maybe_unused]] auto cursor = nnz.make_cursor(p);
            // clang-format off
#if defined(VECTOR)
            constexpr IndexType OutputChunkSize = MaxChunkSize;
            static_assert(HalfDimensions % (2 * OutputChunkSize) == 0);
            constexpr IndexType OutputChunkCount = HalfDimensions / (2 * OutputChunkSize);

    #if defined(USE_NEON)
            constexpr u32 Shift = 1;
    #else
            const vec_t   Zero  = vec_zero();
            const vec_t   FTMax = vec_set_16(FT_MAX);
            constexpr u32 Shift = 7;
    #endif

            const auto* in0 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][0]));
            const auto* in1 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
            auto*       out = reinterpret_cast<vec_t*>(&output[offset]);

            // Per the NNUE architecture, here want to multiply pairs of
            // clipped elements and divide the product by 128. To do this,
            // can naively perform min/max operation to clip each of the
            // four int16 vectors, mullo pairs together, then pack them into
            // one int8 vector. However, there exists a faster way.

            // The idea here is to use the implicit clipping from packus to
            // save two vec_max_16 instructions. This clipping works due to the
            // fact that any int16 integer below zero will be zeroed on packus.

            // Consider the case where the second element is negative.
            // If do standard clipping, that element will be zero, which
            // means pairwise product is zero. If perform packus and remove
            // the lower-side clip for the second element, then product
            // before packus will be negative, and is zeroed on pack.
            // The two operation produce equivalent results, but the second
            // one (using packus) saves one max operation per pair.

            // But here run into a problem: mullo does not preserve the
            // sign of the multiplication. Can get around this by doing mulhi,
            // which keeps the sign. But that requires an additional tweak.

            // mulhi cuts off the last 16 bits of the resulting product,
            // which is the same as performing a rightward shift of 16 bits.
            // Recall that want to divide the final product by 128,
            // which is equivalent to a 7-bit right shift.
            // Intuitively, if shift the clipped value left by 9,
            // and perform mulhi, which shifts the product right by 16 bits,
            // then will net a right shift of 7 bits.
            // However, this won't work as intended. Since clip the values to
            // have a maximum value of 127, shifting it by 9 bits might occupy
            // the signed bit, resulting in some positive values being
            // interpreted as negative after the shift.

            // There is a way, however, to get around this limitation. When
            // loading the network, scale accumulator weights and biases by
            // 2. To get the same pairwise multiplication result as before,
            // need to divide the product by 128 * 2 * 2 = 512, which amounts
            // to a right shift of 9 bits. So now only have to shift left by
            // 7 bits, perform mulhi (shifts right by 16 bits) and net a 
            // 9 bit right shift. Since we scaled everything by two,
            // the values are clipped at 127 * 2 = 254, which occupies 8 bits.
            // Shifting it by 7 bits left will no longer occupy the signed bit, so are safe.

            const auto* tin0 = reinterpret_cast<const vec_t*>(&(threatAccumulation[perspectives[p]][0]));
            const auto* tin1 = reinterpret_cast<const vec_t*>(&(threatAccumulation[perspectives[p]][HalfDimensions / 2]));

            for (IndexType i = 0; i + 1 < OutputChunkCount; i += 2)
            {
                vec_t packed[2];
                for (IndexType j = 0; j < 2; ++j)
                {
                    const IndexType k = (i + j) * 2;

                    const vec_t acc00 = vec_add_16(in0[k + 0], tin0[k + 0]);
                    const vec_t acc01 = vec_add_16(in0[k + 1], tin0[k + 1]);
                    const vec_t acc10 = vec_add_16(in1[k + 0], tin1[k + 0]);
                    const vec_t acc11 = vec_add_16(in1[k + 1], tin1[k + 1]);

    #if defined(USE_NEON)
                    // The NEON path relies on unsigned saturation for crelu
                    const uint16x8_t mul0 = vmull_u8(vqmovun_s16(acc00), vqmovun_s16(acc10));
                    const uint16x8_t mul1 = vmull_u8(vqmovun_s16(acc01), vqmovun_s16(acc11));

                    const uint8x16x2_t uzp = vuzpq_u8(vreinterpretq_u8_u16(mul0), vreinterpretq_u8_u16(mul1));
                    const uint8x16_t pab   = vshrq_n_u8(uzp.val[1], Shift);

                    out[i + j] = packed[j] = reinterpret_cast<vec_t>(pab);

    #else
                    const vec_t sum00 = vec_slli_16(vec_max_16(vec_min_16(acc00, FTMax), Zero), Shift);
                    const vec_t sum01 = vec_slli_16(vec_max_16(vec_min_16(acc01, FTMax), Zero), Shift);
                    const vec_t sum10 = vec_min_16(acc10, FTMax);
                    const vec_t sum11 = vec_min_16(acc11, FTMax);

                    const vec_t p0 = vec_mulhi_16(sum00, sum10);
                    const vec_t p1 = vec_mulhi_16(sum01, sum11);

                    out[i + j] = packed[j] = vec_packus_16(p0, p1);
    #endif
                }

                cursor.record(packed[0], packed[1]);
            }
#else
            for (IndexType j = 0; j < HalfDimensions / 2; ++j)
            {
                BiasType sum0 = accumulation[perspectives[p]][j + 0];
                BiasType sum1 = accumulation[perspectives[p]][j + HalfDimensions / 2];

                sum0 += threatAccumulation[perspectives[p]][j + 0];
                sum1 += threatAccumulation[perspectives[p]][j + HalfDimensions / 2];

                sum0 = std::clamp<BiasType>(sum0, 0, FT_MAX);
                sum1 = std::clamp<BiasType>(sum1, 0, FT_MAX);

                output[offset + j] = static_cast<OutputType>(unsigned(sum0 * sum1) / 512);
            }
#endif
            // clang-format on
        }

        return psqt;
    }

    // clang-format off
    alignas(CACHE_LINE_SIZE) Array<BiasType        , HalfDimensions>                                biases;
    alignas(CACHE_LINE_SIZE) Array<WeightType      , HalfDimensions * PSQFeatureSet::Dimensions>    weights;
    alignas(CACHE_LINE_SIZE) Array<ThreatWeightType, HalfDimensions * ThreatFeatureSet::Dimensions> threatWeights;
    alignas(CACHE_LINE_SIZE) Array<PSQTWeightType  , PSQTBuckets * PSQFeatureSet::Dimensions>       psqtWeights;
    alignas(CACHE_LINE_SIZE) Array<PSQTWeightType  , PSQTBuckets * ThreatFeatureSet::Dimensions>    threatPsqtWeights;
    // clang-format on
};

}  // namespace DON::NNUE

template<>
struct std::hash<DON::NNUE::FeatureTransformer> {
    DON::usize operator()(const DON::NNUE::FeatureTransformer& ft) const noexcept {
        return ft.content_hash();
    }
};

#endif  // NNUE_FEATURE_TRANSFORMER_H_INCLUDED
