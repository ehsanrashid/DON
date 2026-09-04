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

#ifndef NNUE_LAYERS_FALLBACK_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_FALLBACK_AFFINE_TRANSFORM_H_INCLUDED

#include <cstring>

#include "../../misc.h"
#include "../ntypes.h"
#include "../simd.h"  // IWYU pragma: keep

namespace DON::NNUE::Layers {

// Generic fallback implementation for architectures without a specialized SIMD path.
// Requires the input to be padded to at least 16 values.
template<IndexType InputDimensions, IndexType PaddedInputDimensions, IndexType OutputDimensions>
void fallback_affine_transform(const Array<i32, OutputDimensions>&                        biases,
                               const Array<i8, OutputDimensions * PaddedInputDimensions>& weights,
                               const u8* RESTRICT                                         input,
                               i32* RESTRICT output) noexcept {
#if defined(USE_SSE2) || defined(USE_NEON) || defined(USE_RVV)
    #if defined(USE_SSE2) || defined(USE_NEON)
    // At least a multiple of 16
    constexpr IndexType ChunkCount =
      ceil_to_multiple<IndexType>(InputDimensions, SIMD::WIDTH_MIN) / SIMD::WIDTH_MIN;

        #if defined(USE_SSE2)
    constexpr int Shuffle1032 = _MM_SHUFFLE(1, 0, 3, 2);
    const __m128i Zeros       = _mm_setzero_si128();

    const auto* inputVec = reinterpret_cast<const __m128i*>(input);

        #elif defined(USE_NEON)
    const auto* inputVec = reinterpret_cast<const int8x8_t*>(input);

        #endif
    #endif

    for (IndexType i = 0; i < OutputDimensions; ++i)
    {
        const usize offset = i * PaddedInputDimensions;

    #if defined(USE_SSE2)
        __m128i loSum = _mm_cvtsi32_si128(biases[i]);
        __m128i hiSum = Zeros;

        const auto* rowVec = reinterpret_cast<const __m128i*>(&weights[offset]);

        for (IndexType j = 0; j < ChunkCount; ++j)
        {
            const __m128i row       = _mm_load_si128(&rowVec[j]);
            const __m128i in        = _mm_load_si128(&inputVec[j]);
            const __m128i loExtRow  = _mm_srai_epi16(_mm_unpacklo_epi8(row, row), 8);
            const __m128i hiExtRow  = _mm_srai_epi16(_mm_unpackhi_epi8(row, row), 8);
            const __m128i loExtIn   = _mm_unpacklo_epi8(in, Zeros);
            const __m128i hiExtIn   = _mm_unpackhi_epi8(in, Zeros);
            const __m128i loProduct = _mm_madd_epi16(loExtRow, loExtIn);
            const __m128i hiProduct = _mm_madd_epi16(hiExtRow, hiExtIn);
            loSum                   = _mm_add_epi32(loSum, loProduct);
            hiSum                   = _mm_add_epi32(hiSum, hiProduct);
        }

        __m128i sum        = _mm_add_epi32(loSum, hiSum);
        __m128i hiShuffled = _mm_shuffle_epi32(sum, Shuffle1032);
        sum                = _mm_add_epi32(sum, hiShuffled);
        __m128i loShuffled = _mm_shufflelo_epi16(sum, Shuffle1032);
        sum                = _mm_add_epi32(sum, loShuffled);
        output[i]          = _mm_cvtsi128_si32(sum);

    #elif defined(USE_NEON)
        int32x4_t sum = {biases[i]};

        const auto* rowVec = reinterpret_cast<const SIMD::vec_i8x8_t*>(&weights[offset]);

        for (IndexType j = 0; j < ChunkCount; ++j)
        {
            const IndexType k = j * 2;

            int16x8_t product = vmull_s8(inputVec[k + 0], rowVec[k + 0]);
            product           = vmlal_s8(product, inputVec[k + 1], rowVec[k + 1]);
            sum               = vpadalq_s16(sum, product);
        }

        output[i] = SIMD::neon_m128_reduce_add_epi32(sum);

    #elif defined(USE_RVV)
        const i8*  row  = &weights[offset];
        vint32m1_t vsum = __riscv_vmv_v_x_i32m1(0, __riscv_vsetvlmax_e32m1());

        for (usize j = 0; j < InputDimensions;)
        {
            const usize vl = __riscv_vsetvl_e8m4(InputDimensions - j);

            const vint8m4_t  w    = __riscv_vle8_v_i8m4(&row[j], vl);
            const vuint8m4_t x    = __riscv_vle8_v_u8m4(&input[j], vl);
            const vint16m8_t prod = __riscv_vwmulsu_vv_i16m8(w, x, vl);

            vsum = __riscv_vwredsum_vs_i16m8_i32m1(prod, vsum, vl);
            j += vl;
        }

        output[i] = biases[i] + __riscv_vmv_x_s_i32m1_i32(vsum);
    #endif
    }

#else
    std::memcpy(output, biases.data(), OutputDimensions * sizeof(i32));

    // Traverse weights in transpose order to take advantage of input sparsity
    for (IndexType i = 0; i < InputDimensions; ++i)
        if (const int in = input[i]; in != 0)
        {
            const i8* w = &weights[i];

            for (IndexType j = 0; j < OutputDimensions; ++j)
                output[j] += in * w[j * PaddedInputDimensions];
        }

#endif
}

}  // namespace DON::NNUE::Layers

#endif  // NNUE_LAYERS_FALLBACK_AFFINE_TRANSFORM_H_INCLUDED
