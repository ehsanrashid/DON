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
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <memory>

#if defined(USE_RVV)
    #include <type_traits>
#endif

#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "accumulator.h"
#include "architecture.h"
#include "ntypes.h"
#include "serialization.h"
#include "simd.h"

#if defined(VECTOR) || defined(USE_RVV)
    #include "nnz.h"
#else
namespace DON::NNUE {
template<Index Dimensions>
struct NNZ;
}
#endif

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
    static constexpr Index HalfDimensions = L1;

   public:
    // Output type
    using OutputType = TransformedFeature;

    // Number of input/output dimensions
    static constexpr usize InputDimensions =
      PSQFeature::Dimensions + ThreatFeature::Dimensions + PairFeature::Dimensions;
    static constexpr Index OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr usize BufferSize = OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr u32 hash() noexcept {
        return combine_hashes({ThreatFeature::Hash, PairFeature::Hash, PSQFeature::Hash})
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

        combine_hash(h, hash_raw_data(threatAndPpWeights));
        combine_hash(h, hash_raw_data(threatAndPpPsqtWeights));

        combine_hash(h, hash());

        return h;
    }

    template<bool Read>
    void permute_weights() noexcept {
        constexpr auto& Order = Read ? PackusEpi16Order : InversePackusEpi16Order;

        permute<16>(biases, Order);

        permute<16>(weights, Order);

        permute<8>(threatAndPpWeights, Order);
    }

    auto threatWeights() noexcept { return threatAndPpWeights.data(); }
    auto threatWeights() const noexcept { return threatAndPpWeights.data(); }
    auto ppWeights() noexcept {
        return threatWeights() + ThreatFeature::Dimensions * HalfDimensions;
    }
    auto ppWeights() const noexcept {
        return threatWeights() + ThreatFeature::Dimensions * HalfDimensions;
    }

    auto threatPsqtWeights() noexcept { return threatAndPpPsqtWeights.data(); }
    auto threatPsqtWeights() const noexcept { return threatAndPpPsqtWeights.data(); }
    auto ppPsqtWeights() noexcept {
        return threatPsqtWeights() + ThreatFeature::Dimensions * PSQT_BUCKETS;
    }
    auto ppPsqtWeights() const noexcept {
        return threatPsqtWeights() + ThreatFeature::Dimensions * PSQT_BUCKETS;
    }

    // Read network parameters
    bool read_parameters(std::istream& is) noexcept {

        read_leb_128(is, biases);

        read_little_endian<ThreatWeight>(is, threatWeights(),
                                         ThreatFeature::Dimensions * HalfDimensions);
        read_leb_128(is, threatPsqtWeights(), ThreatFeature::Dimensions * PSQT_BUCKETS);
        read_little_endian<ThreatWeight>(is, ppWeights(), PairFeature::Dimensions * HalfDimensions);
        read_leb_128(is, ppPsqtWeights(), PairFeature::Dimensions * PSQT_BUCKETS);

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

        write_little_endian<ThreatWeight>(os, copy->threatWeights(),
                                          ThreatFeature::Dimensions * HalfDimensions);
        write_leb_128<PSQTWeight>(os, copy->threatPsqtWeights(),
                                  ThreatFeature::Dimensions * PSQT_BUCKETS);
        write_little_endian<ThreatWeight>(os, copy->ppWeights(),
                                          PairFeature::Dimensions * HalfDimensions);
        write_leb_128<PSQTWeight>(os, copy->ppPsqtWeights(),
                                  PairFeature::Dimensions * PSQT_BUCKETS);

        write_leb_128(os, copy->weights);
        write_leb_128(os, copy->psqtWeights);

        return !os.fail();
    }

    // clang-format off

    // Convert input features
    i32 transform(const Position&                         pos,
                  AccumulatorCache&                       accCache,
                  AccumulatorStack&                       accStack,
                  const usize                             bucket,
                  [[maybe_unused]] NNZ<OutputDimensions>& nnz,
                  Array<OutputType, BufferSize>&          output) const noexcept {

        accStack.evaluate(pos, *this, accCache);

        Array<Color, COLOR_NB> perspectives{pos.active_color(), ~pos.active_color()};

        const auto& accumulator = accStack.top();

        const auto& psqtAccumulation = accumulator.psqtAccumulation;

        const auto psqt = (  psqtAccumulation[perspectives[WHITE]][bucket]
                           - psqtAccumulation[perspectives[BLACK]][bucket]) / 2;

        const auto& accumulation = accumulator.accumulation;

        for (Color p : {WHITE, BLACK})
        {
            Index offset = p * (HalfDimensions / 2);

#if defined(VECTOR)
            [[maybe_unused]] auto cursor = nnz.make_cursor(p);

            constexpr Index OutputChunkSize = MaxChunkSize;
            static_assert(HalfDimensions % (2 * OutputChunkSize) == 0);
            constexpr Index OutputChunkCount = HalfDimensions / (2 * OutputChunkSize);

    #if !(defined(USE_LSX) || defined(USE_NEON) || defined(__wasm__))
            const SIMD::vec_t zero  = vec_zero();
            const SIMD::vec_t ftMax = vec_set_16(FT_MAX);
    #endif

            const auto* in0 = reinterpret_cast<const SIMD::vec_t*>(&(accumulation[perspectives[p]][0]));
            const auto* in1 = reinterpret_cast<const SIMD::vec_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
            auto*       out = reinterpret_cast<SIMD::vec_t*>(&output[offset]);

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

            // There is a way, however, to get around this limitation. When loading
            // the network, scale accumulator weights and biases by 2.
            // To get the same pairwise multiplication result as before,
            // need to divide the product by 128 * 2 * 2 = 512, which amounts
            // to a right shift of 9 bits. So now only have to shift left by 7 bits,
            // perform mulhi (shifts right by 16 bits) and net a 9 bit right shift.
            // Since we scaled everything by two, the values are clipped at 127 * 2 = 254,
            // which occupies 8 bits. Shifting it by 7 bits left will no longer occupy the signed bit.

            for (Index i = 0; i + 1 < OutputChunkCount; i += 2)
            {
                SIMD::vec_t packed[2];
                for (Index j = 0; j < 2; ++j)
                {
                    const Index k = (i + j) * 2;

                    const SIMD::vec_t acc00 = in0[k + 0];
                    const SIMD::vec_t acc01 = in0[k + 1];
                    const SIMD::vec_t acc10 = in1[k + 0];
                    const SIMD::vec_t acc11 = in1[k + 1];

                    SIMD::vec_t pack;

    #if defined(USE_LSX)
                    const SIMD::vec_t p0 = vec_packus_16(acc00, acc01);
                    const SIMD::vec_t p1 = vec_packus_16(acc10, acc11);

                    const SIMD::vec_t hi = vec_mulhi_8(p0, p1);

                    pack = vec_srli_8(hi, 1);

    #elif defined(USE_NEON)
                    const uint16x8_t mul0 = vmull_u8(vqmovun_s16(acc00), vqmovun_s16(acc10));
                    const uint16x8_t mul1 = vmull_u8(vqmovun_s16(acc01), vqmovun_s16(acc11));

                    const uint8x16x2_t uzp = vuzpq_u8(vreinterpretq_u8_u16(mul0), vreinterpretq_u8_u16(mul1));
                    const uint8x16_t   pab = vshrq_n_u8(uzp.val[1], 1);

                    pack = reinterpret_cast<SIMD::vec_t>(pab);

    #elif defined(__wasm__)
                    // _mm_mulhi_epi16 is lowered to 32-bit multiplies, so we take
                    // a similar approach as the NEON path.
                    const SIMD::vec_t mul0 = vec_packus_16(acc00, acc01);
                    const SIMD::vec_t mul1 = vec_packus_16(acc10, acc11);

                    const SIMD::vec_t lo = wasm_u16x8_extmul_low_u8x16(mul0, mul1);
                    const SIMD::vec_t hi = wasm_u16x8_extmul_high_u8x16(mul0, mul1);

                    // equivalent to vuzp2_u8
                    const SIMD::vec_t merged = wasm_i8x16_shuffle(lo, hi, 1, 3, 5, 7, 9, 11, 13, 15,
                                                                  17, 19, 21, 23, 25, 27, 29, 31);

                    pack = wasm_u8x16_shr(merged, 1);

    #else
                    const SIMD::vec_t sum00 = vec_slli_16(vec_max_16(vec_min_16(acc00, ftMax), zero), 7);
                    const SIMD::vec_t sum01 = vec_slli_16(vec_max_16(vec_min_16(acc01, ftMax), zero), 7);
                    const SIMD::vec_t sum10 = vec_min_16(acc10, ftMax);
                    const SIMD::vec_t sum11 = vec_min_16(acc11, ftMax);

                    const SIMD::vec_t p0 = vec_mulhi_16(sum00, sum10);
                    const SIMD::vec_t p1 = vec_mulhi_16(sum01, sum11);

                    pack = vec_packus_16(p0, p1);
    #endif
                    out[i + j] = packed[j] = pack;
                }

                cursor.record(packed[0], packed[1]);
            }

#elif defined(USE_RVV)

            const Index maxVL = __riscv_vsetvlmax_e8m1();

            const auto rvv_propagate = [&](auto vid) noexcept {
                const auto& accp = accumulation[perspectives[p]];

                for (Index i = 0, vl; i < HalfDimensions / 2; i += vl)
                {
                    vl = __riscv_vsetvl_e16m2(HalfDimensions / 2 - i);

                    vint16m2_t acc0 = __riscv_vle16_v_i16m2(&accp[i], vl);
                    vint16m2_t acc1 = __riscv_vle16_v_i16m2(&accp[i + HalfDimensions / 2], vl);

                    acc0 = __riscv_vmax(acc0, 0, vl);
                    acc1 = __riscv_vmax(acc1, 0, vl);

                    const vuint8m1_t p0 = __riscv_vnclipu(__riscv_vreinterpret_u16m2(acc0), 0, 0, vl);
                    const vuint8m1_t p1 = __riscv_vnclipu(__riscv_vreinterpret_u16m2(acc1), 0, 0, vl);

                    const vuint8m1_t hi     = __riscv_vmulhu(p0, p1, vl);
                    const vuint8m1_t scaled = __riscv_vsrl(hi, 1, vl);

                    __riscv_vse8(&output[offset + i], scaled, vl);

                    const vbool8_t m   = __riscv_vmsne(scaled, 0, vl);
                    const unsigned cnt = __riscv_vcpop(m, vl);

                    vuint16m2_t vidx;
                    if constexpr (std::is_same_v<decltype(vid), vuint8m1_t>)
                        vidx = __riscv_vzext_vf2(__riscv_vcompress(vid, m, vl), cnt);
                    else
                        vidx = __riscv_vcompress(vid, m, vl);

                    __riscv_vse16(&nnz.bitset[nnz.count], __riscv_vadd(vidx, offset + i, cnt), cnt);
                    nnz.count += cnt;
                }
            };

            if (maxVL <= 256)
                rvv_propagate(__riscv_vid_v_u8m1(maxVL));  // vuint8m1_t vid8
            else
                rvv_propagate(__riscv_vid_v_u16m2(maxVL));  // vuint16m2_t vid16

#else
            for (Index i = 0; i < HalfDimensions / 2; ++i)
            {
                Bias sum0 = accumulation[perspectives[p]][i + 0];
                Bias sum1 = accumulation[perspectives[p]][i + HalfDimensions / 2];

                sum0 = std::clamp<Bias>(sum0, 0, FT_MAX);
                sum1 = std::clamp<Bias>(sum1, 0, FT_MAX);

                output[offset + i] = static_cast<OutputType>(unsigned(sum0 * sum1) / 512);
            }
#endif
        }

        return psqt;
    }

    alignas(CACHE_LINE_SIZE) Array<ThreatWeight, (ThreatFeature::Dimensions + PairFeature::Dimensions) * HalfDimensions> threatAndPpWeights;
    alignas(CACHE_LINE_SIZE) Array<PSQTWeight  , (ThreatFeature::Dimensions + PairFeature::Dimensions) * PSQT_BUCKETS>   threatAndPpPsqtWeights;

    alignas(CACHE_LINE_SIZE) Array<Weight    , (PSQFeature::Dimensions) * HalfDimensions> weights;
    alignas(CACHE_LINE_SIZE) Array<PSQTWeight, (PSQFeature::Dimensions) * PSQT_BUCKETS>   psqtWeights;

    alignas(CACHE_LINE_SIZE) Array<Bias, HalfDimensions> biases;
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
