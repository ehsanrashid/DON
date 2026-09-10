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

// Definition of layer ClippedReLU of NNUE evaluation function

#ifndef NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
#define NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED

#include <algorithm>
#include <iosfwd>

#include "../../misc.h"
#include "../ntypes.h"
#include "../simd.h"

namespace DON::NNUE::Layers {

// This class defines a squared Clipped ReLU activation layer.
//
// Inputs are squared and scaled using a right shift to avoid division,
// then clipped to [0, 127] and stored as u8.
// The scaling must be accounted for during training.
template<Index InDims, u8 WeightScaleBits = WEIGHT_SCALE_BITS>
class SqrClippedReLU final {
   public:
    // Input/output type
    using InputType  = i32;
    using OutputType = u8;

    // Number of input/output dimensions
    static constexpr Index InputDimensions  = InDims;
    static constexpr Index OutputDimensions = InputDimensions;
    static constexpr Index PaddedOutputDimensions =
      ceil_to_multiple<Index>(OutputDimensions, SIMD::WIDTH_MAX);

    using OutputBuffer = Array<OutputType, PaddedOutputDimensions>;

    // Hash value embedded in the evaluation file
    static constexpr u32 hash(u32 preHash) noexcept {
        u32 h = 0x538D24C7u;
        h += preHash;
        return h;
    }

    usize content_hash() const noexcept {
        usize h = 0;
        combine_hash(h, hash(0));
        return h;
    }

    // Read network parameters
    bool read_parameters(std::istream&) noexcept { return true; }

    // Write network parameters
    bool write_parameters(std::ostream&) const noexcept { return true; }

#if defined(USE_PAIR_ACTIVATIONS)
    // Produce the squared and linear clipped activations together, sharing the input loads and
    // the initial signed 32-to-16-bit saturating narrowing.
    void propagate_pair(const InputType* RESTRICT input,
                        OutputType* RESTRICT      squared,
                        OutputType* RESTRICT      clipped) const noexcept {
        static_assert(5 <= WeightScaleBits && WeightScaleBits <= 8,
                      "SqrClippedReLU only support WeightScaleBits between 5 and 8");
        static_assert(InputDimensions % 32 == 0);

        constexpr u8 BaseShift = 7 + 2 * WeightScaleBits;
        constexpr u8 SimdShift = BaseShift - 16;

        constexpr Index ChunkCount = InputDimensions / 32;

    #if defined(USE_AVX512)
        const auto* in      = reinterpret_cast<const __m512i*>(input);
        auto*       sqrOut  = reinterpret_cast<__m256i*>(squared);
        auto*       clipOut = reinterpret_cast<__m256i*>(clipped);

        const __m512i zero = _mm512_setzero_si512();
        // clang-format off
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const __m256i words0 = _mm512_cvtsepi32_epi16(_mm512_load_si512(&in[i * 2 + 0]));
            const __m256i words1 = _mm512_cvtsepi32_epi16(_mm512_load_si512(&in[i * 2 + 1]));
            const __m512i words  = _mm512_inserti64x4(_mm512_castsi256_si512(words0), words1, 1);

            const __m512i sqrWords = _mm512_srli_epi16(_mm512_mulhi_epi16(words, words), SimdShift);
            _mm256_store_si256(&sqrOut[i], _mm512_cvtsepi16_epi8(sqrWords));

            const __m512i clipWords = _mm512_srli_epi16(_mm512_max_epi16(words, zero), WeightScaleBits);
            _mm256_store_si256(&clipOut[i], _mm512_cvtsepi16_epi8(clipWords));
        }
                // clang-format on
    #elif defined(USE_AVX2_PAIR_ACTIVATIONS)
        const auto* in      = reinterpret_cast<const __m256i*>(input);
        auto*       sqrOut  = reinterpret_cast<__m256i*>(squared);
        auto*       clipOut = reinterpret_cast<__m256i*>(clipped);

        const __m256i zero = _mm256_setzero_si256();
        // clang-format off
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 4;

            const __m256i words0 = _mm256_packs_epi32(_mm256_load_si256(&in[j + 0]),
                                                      _mm256_load_si256(&in[j + 1]));
            const __m256i words1 = _mm256_packs_epi32(_mm256_load_si256(&in[j + 2]),
                                                      _mm256_load_si256(&in[j + 3]));

            const __m256i sqr0      = _mm256_srli_epi16(_mm256_mulhi_epi16(words0, words0), SimdShift);
            const __m256i sqr1      = _mm256_srli_epi16(_mm256_mulhi_epi16(words1, words1), SimdShift);
            const __m256i sqrPacked = _mm256_packs_epi16(sqr0, sqr1);
            _mm256_store_si256(&sqrOut[i], sqrPacked);

            const __m256i clip0      = _mm256_srli_epi16(_mm256_max_epi16(words0, zero), WeightScaleBits);
            const __m256i clip1      = _mm256_srli_epi16(_mm256_max_epi16(words1, zero), WeightScaleBits);
            const __m256i clipPacked = _mm256_packs_epi16(clip0, clip1);
            _mm256_store_si256(&clipOut[i], clipPacked);
        }
                // clang-format on
    #endif
    }

#else
    // Forward propagation
    void propagate(const InputType* RESTRICT input, OutputType* RESTRICT output) const noexcept {
        static_assert(5 <= WeightScaleBits && WeightScaleBits <= 8,
                      "SqrClippedReLU requires WeightScaleBits between 5 and 8");
        // After squaring, need shift right by 7 + 2 * WeightScaleBits.
        // MulHi already removes the lower 16 bits, so only the remaining bits need to be shifted out.
        constexpr u8                  BaseShift = 7 + 2 * WeightScaleBits;
        [[maybe_unused]] constexpr u8 SimdShift = BaseShift - 16;

    #if defined(USE_SSE2)
        #if defined(USE_AVX512)
        static_assert(InputDimensions % 32 == 0);

        constexpr Index SimdWidth  = SIMD::WIDTH;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m512i*>(input);
        auto*       out = reinterpret_cast<__m256i*>(output);
        // clang-format off
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 2;

            const __m256i words0 = _mm512_cvtsepi32_epi16(_mm512_load_si512(&in[j + 0]));
            const __m256i words1 = _mm512_cvtsepi32_epi16(_mm512_load_si512(&in[j + 1]));
            const __m512i words  = _mm512_inserti64x4(_mm512_castsi256_si512(words0), words1, 1);
            const __m512i packed = _mm512_srli_epi16(_mm512_mulhi_epi16(words, words), SimdShift);
            _mm256_store_si256(&out[i], _mm512_cvtsepi16_epi8(packed));
        }
        // clang-format on
        constexpr Index Start = SimdWidth * ChunkCount;

        #else
        constexpr Index SimdWidth  = SIMD::WIDTH_MIN;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);
        // clang-format off
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 4;

            const __m128i words0  = _mm_packs_epi32(_mm_load_si128(&in[j + 0]), _mm_load_si128(&in[j + 1]));
            const __m128i words1  = _mm_packs_epi32(_mm_load_si128(&in[j + 2]), _mm_load_si128(&in[j + 3]));
            const __m128i packed0 = _mm_srli_epi16(_mm_mulhi_epi16(words0, words0), SimdShift);
            const __m128i packed1 = _mm_srli_epi16(_mm_mulhi_epi16(words1, words1), SimdShift);
            _mm_store_si128(&out[i], _mm_packs_epi16(packed0, packed1));
        }
        // clang-format on
        constexpr Index Start = SimdWidth * ChunkCount;
        #endif

    #elif defined(USE_LSX)
        #if defined(USE_LASX)
        constexpr Index SimdWidth  = SIMD::WIDTH;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m256i*>(input);
        auto*       out = reinterpret_cast<__m256i*>(output);
        // clang-format off
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 4;

            const __m256i words0 = __lasx_xvssrani_h_w(in[j + 1], in[j + 0], 0);
            const __m256i words1 = __lasx_xvssrani_h_w(in[j + 3], in[j + 2], 0);
            const __m256i sqr0   = __lasx_xvmuh_h(words0, words0);
            const __m256i sqr1   = __lasx_xvmuh_h(words1, words1);
            const __m256i packed = __lasx_xvssrlni_b_h(sqr1, sqr0, SimdShift);
            __lasx_xvst(packed, out + i, 0);
        }
        // clang-format on
        constexpr Index Start = SimdWidth * ChunkCount;

        #else
        constexpr Index SimdWidth  = SIMD::WIDTH;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);
        // clang-format off
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 4;

            const __m128i words0 = __lsx_vssrani_h_w(in[j + 1], in[j + 0], 0);
            const __m128i words1 = __lsx_vssrani_h_w(in[j + 3], in[j + 2], 0);
            const __m128i sqr0   = __lsx_vmuh_h(words0, words0);
            const __m128i sqr1   = __lsx_vmuh_h(words1, words1);
            out[i]               = __lsx_vssrlni_b_h(sqr1, sqr0, SimdShift);
        }
        // clang-format on
        constexpr Index Start = SimdWidth * ChunkCount;

        #endif

    #elif defined(USE_NEON)
        constexpr Index SimdWidth  = SIMD::WIDTH;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const int32x4_t*>(input);
        auto*       out = reinterpret_cast<int8x16_t*>(output);
        // clang-format off
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 4;

            const int16x8_t words0 = vcombine_s16(vqmovn_s32(in[j + 0]), vqmovn_s32(in[j + 1]));
            const int16x8_t words1 = vcombine_s16(vqmovn_s32(in[j + 2]), vqmovn_s32(in[j + 3]));

            // NEON needs to shift by one more since the used simd instruction does
            // `Saturating Doubling Multiply High` (doubling before shift by 16).
            const int16x8_t sqr0 = vshrq_n_s16(vqdmulhq_s16(words0, words0), SimdShift + 1);
            const int16x8_t sqr1 = vshrq_n_s16(vqdmulhq_s16(words1, words1), SimdShift + 1);
            out[i] = vcombine_s8(vqmovn_s16(sqr0), vqmovn_s16(sqr1));
        }
        // clang-format on
        constexpr Index Start = SimdWidth * ChunkCount;

    #elif defined(USE_RVV)
        // clang-format off
        for (Index i = 0, vl; i < InputDimensions; i += vl)
        {
            vl = __riscv_vsetvl_e32m4(InputDimensions - i);

            const vint32m4_t  in = __riscv_vle32_v_i32m4(&input[i], vl);

            const vint16m2_t words   = __riscv_vnclip_wx_i16m2(in, 0, __RISCV_VXRM_RDN, vl);
            const vint16m2_t sqr     = __riscv_vmulh_vv_i16m2(words, words, vl);
            const vint8m1_t narrowed = __riscv_vnclip_wx_i8m1(sqr, SimdShift, __RISCV_VXRM_RDN, vl);
            __riscv_vse8_v_u8m1(&output[i], __riscv_vreinterpret_v_i8m1_u8m1(narrowed), vl);
        }
        // clang-format on
        constexpr Index Start = InputDimensions;

    #else
        constexpr Index Start = 0;

    #endif

        for (Index i = Start; i < InputDimensions; ++i)
        {
            // The extra 7-bit right-shift approximates division by 127 while avoiding the more expensive integer division.
            // The resulting scale must be accounted for by the trainer.
            output[i] = static_cast<OutputType>(
              std::min((static_cast<i64>(input[i]) * input[i]) >> BaseShift, i64{127}));
        }
    }

#endif
};

}  // namespace DON::NNUE::Layers

#endif  // NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
