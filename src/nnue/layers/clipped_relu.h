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

#ifndef NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
#define NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED

#include <algorithm>
#include <iosfwd>

#include "../../misc.h"
#include "../ntypes.h"
#include "../simd.h"

namespace DON::NNUE::Layers {

// This class defines a Clipped ReLU activation layer.
//
// The activation clips each input value to the range [0, 127].
// It introduces non-linearity while keeping the output within the
// range supported by subsequent quantized operations.
template<Index InDims, u8 WeightScaleBits = WEIGHT_SCALE_BITS>
class ClippedReLU final {
   public:
    // Input/output type
    using Input  = i32;
    using Output = u8;

    // Number of input/output dimensions
    static constexpr Index InputDimensions  = InDims;
    static constexpr Index OutputDimensions = InputDimensions;
    static constexpr Index PaddedOutputDimensions =
      ceil_to_multiple<Index>(OutputDimensions, SIMD::WIDTH_MAX);

    using OutputBuffer = Array<Output, PaddedOutputDimensions>;

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
    void propagate(const Input* RESTRICT input, Output* RESTRICT output) const noexcept {

#if defined(USE_SSE2)
        constexpr Index SimdWidth  = SIMD::WIDTH_MIN;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

    #if !(defined(USE_SSE41))
        const __m128i K0x80s = _mm_set1_epi8(-128);
    #endif

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 4;
                // clang-format off
    #if defined(USE_SSE41)
            const __m128i packed0 = _mm_packus_epi32(_mm_load_si128(&in[j + 0]), _mm_load_si128(&in[j + 1]));
            const __m128i packed1 = _mm_packus_epi32(_mm_load_si128(&in[j + 2]), _mm_load_si128(&in[j + 3]));
            const __m128i words0  = _mm_srli_epi16(packed0, WeightScaleBits);
            const __m128i words1  = _mm_srli_epi16(packed1, WeightScaleBits);
            _mm_store_si128(&out[i], _mm_packs_epi16(words0, words1));

    #else
            const __m128i packed0 = _mm_packs_epi32(_mm_load_si128(&in[j + 0]), _mm_load_si128(&in[j + 1]));
            const __m128i packed1 = _mm_packs_epi32(_mm_load_si128(&in[j + 2]), _mm_load_si128(&in[j + 3]));
            const __m128i words0  = _mm_srai_epi16(packed0, WeightScaleBits);
            const __m128i words1  = _mm_srai_epi16(packed1, WeightScaleBits);
            const __m128i packed  = _mm_packs_epi16(words0, words1);
            _mm_store_si128(&out[i], _mm_subs_epi8(_mm_adds_epi8(packed, K0x80s), K0x80s));
    
    #endif
            // clang-format on
        }

        constexpr Index Start = SimdWidth * ChunkCount;

#elif defined(USE_LSX)
    #if defined(USE_LASX)
        constexpr Index SimdWidth  = SIMD::WIDTH;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m256i*>(input);
        auto*       out = reinterpret_cast<__m256i*>(output);
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 4;

            const __m256i packed0 = vec_packus_32(in[j + 0], in[j + 1]);
            const __m256i packed1 = vec_packus_32(in[j + 2], in[j + 3]);
            const __m256i packed  = __lasx_xvssrlni_b_h(packed1, packed0, WeightScaleBits);
            __lasx_xvst(packed, out + i, 0);
        }

        constexpr Index Start = SimdWidth * ChunkCount;

    #else
        constexpr Index SimdWidth  = SIMD::WIDTH;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 4;

            const __m128i packed0 = vec_packus_32(in[j + 0], in[j + 1]);
            const __m128i packed1 = vec_packus_32(in[j + 2], in[j + 3]);
            out[i]                = __lsx_vssrlni_b_h(packed1, packed0, WeightScaleBits);
        }

        constexpr Index Start = SimdWidth * ChunkCount;

    #endif

#elif defined(USE_NEON)
        constexpr Index SimdWidth  = SIMD::WIDTH / 2;
        constexpr Index ChunkCount = InputDimensions / SimdWidth;

        const SIMD::vec_i8x8_t Zero = {0};

        const auto* in  = reinterpret_cast<const SIMD::vec_i32x4_t*>(input);
        auto*       out = reinterpret_cast<SIMD::vec_i8x8_t*>(output);
        for (Index i = 0; i < ChunkCount; ++i)
        {
            const Index j = i * 2;

            int16x8_t shifted;
            auto*     pack = reinterpret_cast<int16x4_t*>(&shifted);
            pack[0]        = vqshrn_n_s32(in[j + 0], WeightScaleBits);
            pack[1]        = vqshrn_n_s32(in[j + 1], WeightScaleBits);
            out[i]         = vmax_s8(vqmovn_s16(shifted), Zero);
        }

        constexpr Index Start = SimdWidth * ChunkCount;

#elif defined(USE_RVV)
        for (Index i = 0, vl; i < InputDimensions; i += vl)
        {
            // clang-format off
            vl                       = __riscv_vsetvl_e32m4(InputDimensions - i);
            vint32m4_t  in           = __riscv_vle32_v_i32m4(&input[i], vl);
            in                       = __riscv_vmax_vx_i32m4(in, 0, vl);
            const vint16m2_t words   = __riscv_vnclip_wx_i16m2(in, WeightScaleBits, __RISCV_VXRM_RDN, vl);
            const vint8m1_t narrowed = __riscv_vnclip_wx_i8m1(words, 0, __RISCV_VXRM_RDN, vl);
            __riscv_vse8_v_u8m1(&output[i], __riscv_vreinterpret_v_i8m1_u8m1(narrowed), vl);
            // clang-format on
        }

        constexpr Index Start = InputDimensions;

#else
        constexpr Index Start = 0;

#endif

        for (Index i = Start; i < InputDimensions; ++i)
            output[i] = static_cast<Output>(std::clamp(input[i] >> WeightScaleBits, 0, 127));
    }
};

}  // namespace DON::NNUE::Layers

#endif  // NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
