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
#include "../common.h"
#include "../simd.h"  // IWYU pragma: keep

namespace DON::NNUE::Layers {

// Clipped ReLU
template<IndexType InDims>
class ClippedReLU final {
   public:
    // Input/output type
    using InputType  = i32;
    using OutputType = u8;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = InputDimensions;
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, 32);

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
        // clang-format off
#if defined(USE_SSE2)
        constexpr IndexType SimdWidth  = 16;
        constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

    #if !defined(USE_SSE41)
        const __m128i K0x80s = _mm_set1_epi8(-128);
    #endif

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);

        for (IndexType i = 0; i < ChunkCount; ++i)
        {
    #if defined(USE_SSE41)
            const __m128i packed0 = _mm_packus_epi32(_mm_load_si128(&in[i * 4 + 0]),
                                                     _mm_load_si128(&in[i * 4 + 1]));
            const __m128i packed1 = _mm_packus_epi32(_mm_load_si128(&in[i * 4 + 2]),
                                                     _mm_load_si128(&in[i * 4 + 3]));
            const __m128i words0  = _mm_srli_epi16(packed0, WEIGHT_SCALE_BITS);
            const __m128i words1  = _mm_srli_epi16(packed1, WEIGHT_SCALE_BITS);
            _mm_store_si128(&out[i], _mm_packs_epi16(words0, words1));
    #else
            const __m128i packed0 = _mm_packs_epi32(_mm_load_si128(&in[i * 4 + 0]),
                                                    _mm_load_si128(&in[i * 4 + 1]));
            const __m128i packed1 = _mm_packs_epi32(_mm_load_si128(&in[i * 4 + 2]),
                                                    _mm_load_si128(&in[i * 4 + 3]));
            const __m128i words0  = _mm_srai_epi16(packed0, WEIGHT_SCALE_BITS);
            const __m128i words1  = _mm_srai_epi16(packed1, WEIGHT_SCALE_BITS);
            const __m128i packed  = _mm_packs_epi16(words0, words1);
            _mm_store_si128(&out[i], _mm_subs_epi8(_mm_adds_epi8(packed, K0x80s), K0x80s));
    #endif
        }

        constexpr IndexType Start = SimdWidth * ChunkCount;

#elif defined(USE_NEON)
        constexpr IndexType SimdWidth  = SIMD_WIDTH / 2;
        constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

        const SIMD::vec_i8x8_t Zero = {0};

        const auto* in  = reinterpret_cast<const SIMD::vec_i32x4_t*>(input);
        auto*       out = reinterpret_cast<SIMD::vec_i8x8_t*>(output);

        for (IndexType i = 0; i < ChunkCount; ++i)
        {
            const int16x8_t shifted = vcombine_s16(
                                        vqshrn_n_s32(in[i * 2 + 0], WEIGHT_SCALE_BITS),
                                        vqshrn_n_s32(in[i * 2 + 1], WEIGHT_SCALE_BITS));
            out[i]                  = vmax_s8(vqmovn_s16(shifted), Zero);
        }

        constexpr IndexType Start = SimdWidth * ChunkCount;

#elif defined(USE_LASX)
        constexpr IndexType SimdWidth  = SIMD_WIDTH;
        constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m256i*>(input);
        auto*       out = reinterpret_cast<__m256i*>(output);

        for (IndexType i = 0; i < ChunkCount; ++i)
        {
            const __m256i packed0 = SIMD::lasx_packus_32(in[i * 4 + 0], in[i * 4 + 1]);
            const __m256i packed1 = SIMD::lasx_packus_32(in[i * 4 + 2], in[i * 4 + 3]);
            const __m256i words0  = __lasx_xvsrli_h(packed0, WEIGHT_SCALE_BITS);
            const __m256i words1  = __lasx_xvsrli_h(packed1, WEIGHT_SCALE_BITS);
            const __m256i packed  = __lasx_xvssrani_b_h(words1, words0, 0);
            const __m256i swaped  = __lasx_xvpermi_d(packed, 0xD8);
            __lasx_xvst(__lasx_xvshuf4i_w(swaped, 0xD8), out + i, 0);
        }

        constexpr IndexType Start = SimdWidth * ChunkCount;

#elif defined(USE_LSX)
        constexpr IndexType SimdWidth  = SIMD_WIDTH;
        constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);

        for (IndexType i = 0; i < ChunkCount; ++i)
        {
            const __m128i packed0 = SIMD::lsx_packus_32(in[i * 4 + 0], in[i * 4 + 1]);
            const __m128i packed1 = SIMD::lsx_packus_32(in[i * 4 + 2], in[i * 4 + 3]);
            const __m128i words0  = __lsx_vsrli_h(packed0, WEIGHT_SCALE_BITS);
            const __m128i words1  = __lsx_vsrli_h(packed1, WEIGHT_SCALE_BITS);
            out[i]                = __lsx_vssrani_b_h(words1, words0, 0);
        }

        constexpr IndexType Start = SimdWidth * ChunkCount;

#else
        constexpr IndexType Start = 0;
#endif
        // clang-format on

        for (IndexType i = Start; i < InputDimensions; ++i)
            output[i] = static_cast<OutputType>(std::clamp(input[i] >> WEIGHT_SCALE_BITS, 0, 127));
    }
};

}  // namespace DON::NNUE::Layers

#endif  // #ifndef NNUE_LAYERS_CLIPPED_RELU_H_INCLUDED
