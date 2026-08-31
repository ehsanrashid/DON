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
#include "../simd.h"  // IWYU pragma: keep

namespace DON::NNUE::Layers {

// This class defines a squared Clipped ReLU activation layer.
//
// Inputs are squared and scaled using a right shift to avoid division,
// then clipped to [0, 127] and stored as u8.
// The scaling must be accounted for during training.
template<IndexType InDims, u8 WeightScaleBits = WEIGHT_SCALE_BITS>
class SqrClippedReLU final {
   public:
    // Input/output type
    using InputType  = i32;
    using OutputType = u8;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = InputDimensions;
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, SIMD_WIDTH_MAX);

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

    // Forward propagation
    void propagate(const InputType* RESTRICT input, OutputType* RESTRICT output) const noexcept {
        static_assert(5 <= WeightScaleBits && WeightScaleBits <= 8,
                      "SqrClippedReLU requires WeightScaleBits between 5 and 8");
        // After squaring, need shift right by 7 + 2 * WeightScaleBits.
        // MulHi already removes the lower 16 bits, so only the remaining bits need to be shifted out.
        constexpr u8                  BaseShift = 7 + 2 * WeightScaleBits;
        [[maybe_unused]] constexpr u8 SimdShift = BaseShift - 16;

        // clang-format off
#if defined(USE_SSE2)
        constexpr IndexType SimdWidth  = SIMD_WIDTH_MIN;
        constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);

        for (IndexType i = 0; i < ChunkCount; ++i)
        {
            const IndexType j = i * 4;

            __m128i words0 = _mm_packs_epi32(_mm_load_si128(&in[j + 0]), _mm_load_si128(&in[j + 1]));
            __m128i words1 = _mm_packs_epi32(_mm_load_si128(&in[j + 2]), _mm_load_si128(&in[j + 3]));

            words0 = _mm_srli_epi16(_mm_mulhi_epi16(words0, words0), SimdShift);
            words1 = _mm_srli_epi16(_mm_mulhi_epi16(words1, words1), SimdShift);

            _mm_store_si128(&out[i], _mm_packs_epi16(words0, words1));
        }

        constexpr IndexType Start = SimdWidth * ChunkCount;

#elif defined(USE_LSX)
    #if defined(USE_LASX)
        constexpr IndexType SimdWidth  = SIMD_WIDTH;
        constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m256i*>(input);
        auto*       out = reinterpret_cast<__m256i*>(output);

        for (IndexType i = 0; i < ChunkCount; ++i)
        {
            const IndexType j = i * 4;

            const __m256i words0 = __lasx_xvssrani_h_w(in[j + 1], in[j + 0], 0);
            const __m256i words1 = __lasx_xvssrani_h_w(in[j + 3], in[j + 2], 0);
            const __m256i sqr0   = __lasx_xvmuh_h(words0, words0);
            const __m256i sqr1   = __lasx_xvmuh_h(words1, words1);

            const __m256i packed = __lasx_xvssrlni_b_h(sqr1, sqr0, SimdShift);
            const __m256i permed = __lasx_xvpermi_d(packed, 0xD8);

            __lasx_xvst(__lasx_xvshuf4i_w(permed, 0xD8), out + i, 0);
        }

        constexpr IndexType Start = SimdWidth * ChunkCount;

    #else
        constexpr IndexType SimdWidth  = SIMD_WIDTH;
        constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);

        for (IndexType i = 0; i < ChunkCount; ++i)
        {
            const IndexType j = i * 4;

            const __m128i words0 = __lsx_vssrani_h_w(in[j + 1], in[j + 0], 0);
            const __m128i words1 = __lsx_vssrani_h_w(in[j + 3], in[j + 2], 0);
            const __m128i sqr0   = __lsx_vmuh_h(words0, words0);
            const __m128i sqr1   = __lsx_vmuh_h(words1, words1);

            out[i]               = __lsx_vssrlni_b_h(sqr1, sqr0, SimdShift);
        }

        constexpr IndexType Start = SimdWidth * ChunkCount;

    #endif

#elif defined(USE_RVV)

        for (usize j = 0; j < InputDimensions;)
        {
            const usize vl = __riscv_vsetvl_e32m4(InputDimensions - j);
            vint32m4_t  in = __riscv_vle32_v_i32m4(&input[j], vl);

            vint16m2_t words   = __riscv_vnclip_wx_i16m2(in, 0, __RISCV_VXRM_RDN, vl);
            vint16m2_t sqr     = __riscv_vmulh_vv_i16m2(words, words, vl);
            vint8m1_t narrowed = __riscv_vnclip_wx_i8m1(sqr, SimdShiftAmount, __RISCV_VXRM_RDN, vl);

            __riscv_vse8_v_u8m1(&output[j], __riscv_vreinterpret_v_i8m1_u8m1(narrowed), vl);
            j += vl;
        }

        constexpr IndexType Start = InputDimensions;

#elif defined(USE_NEON)
        constexpr IndexType SimdWidth  = SIMD_WIDTH;
        constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const int32x4_t*>(input);
        auto*       out = reinterpret_cast<int8x16_t*>(output);

        for (IndexType i = 0; i < ChunkCount; ++i)
        {
            const IndexType j = i * 4;

            const int16x8_t words0 = vcombine_s16(vqmovn_s32(in[j + 0]), vqmovn_s32(in[j + 1]));
            const int16x8_t words1 = vcombine_s16(vqmovn_s32(in[j + 2]), vqmovn_s32(in[j + 3]));

            // NEON needs to shift by one more since the used simd instruction does
            // `Saturating Doubling Multiply High` (doubling before shift by 16).
            const int16x8_t sqr0 = vshrq_n_s16(vqdmulhq_s16(words0, words0), SimdShift + 1);
            const int16x8_t sqr1 = vshrq_n_s16(vqdmulhq_s16(words1, words1), SimdShift + 1);

            out[i] = vcombine_s8(vqmovn_s16(sqr0), vqmovn_s16(sqr1));
        }

        constexpr IndexType Start = SimdWidth * ChunkCount;

#else
        constexpr IndexType Start = 0;

#endif
        // clang-format on

        for (IndexType i = Start; i < InputDimensions; ++i)
        {
            // The extra 7-bit right-shift approximates division by 127 while avoiding the more expensive integer division.
            // The resulting scale must be accounted for by the trainer.
            output[i] = static_cast<OutputType>(
              std::min((static_cast<i64>(input[i]) * input[i]) >> BaseShift, i64{127}));
        }
    }
};

}  // namespace DON::NNUE::Layers

#endif  // NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
