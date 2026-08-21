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

#ifndef NNZ_HELPER_H_INCLUDED
#define NNZ_HELPER_H_INCLUDED

#include <utility>

#if defined(USE_SSSE3) || defined(USE_LSX)
    #include <cstring>
#endif

#if defined(USE_AVX512)
    #include "../bitboard.h"
#endif

#include "../types.h"
#include "simd.h"  // IWYU pragma: keep

namespace DON::NNUE {

template<usize Dimensions>
struct NNZ final {
#if defined(USE_AVX512)
    #if defined(USE_AVX512ICL)
    alignas(CACHE_LINE_SIZE) static constexpr auto Indices = []() constexpr noexcept {
        Array<u16, COLOR_NB, 32> indices{};

        for (Color p : {WHITE, BLACK})
        {
            indices[p] = {0, 1, 2,  3,  16, 17, 18, 19, 4,  5,  6,  7,  20, 21, 22, 23,
                          8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31};
            for (auto& m : indices[p])
                m += p * Dimensions / 8;
        }

        return indices;
    }();
    #else
    alignas(CACHE_LINE_SIZE) static constexpr auto Indices = []() constexpr noexcept {
        Array<u32, COLOR_NB, 16> indices{};

        for (Color p : {WHITE, BLACK})
        {
            indices[p] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
            for (auto& m : indices[p])
                m += p * Dimensions / 8;
        }

        return indices;
    }();
    #endif

    struct Cursor final {
        Cursor(NNZ& nnz_, Color perspective, unsigned count_) noexcept :
            nnz(nnz_),
            count(count_) {
            indices = _mm512_load_si512(&Indices[perspective]);
        }

        void record(SIMD::vec_t neurons1, SIMD::vec_t neurons2) noexcept {
    #if defined(USE_AVX512ICL)
            const __m512i increment = _mm512_set1_epi16(32);

            // Get a bitmask and gather non-zero indices
            const __m512i   n01     = _mm512_packs_epi32(neurons1, neurons2);
            const __mmask32 nnzMask = _mm512_test_epi16_mask(n01, n01);

            // Avoid _mm512_mask_compressstoreu_epi16() as it's 256 uOps on Zen4
            const __m512i nnzVal = _mm512_maskz_compress_epi16(nnzMask, indices);
            _mm512_storeu_si512(nnz.bitset + count, nnzVal);

            count += popcount(nnzMask);
            indices = _mm512_add_epi16(indices, increment);
    #else
            const __m512i increment = _mm512_set1_epi32(16);

            for (auto neurons : {neurons1, neurons2})
            {
                // Get a bitmask and gather non-zero indices
                const __mmask16 nnzMask = _mm512_test_epi32_mask(neurons, neurons);
                const __m512i   nnzVal  = _mm512_maskz_compress_epi32(nnzMask, indices);
                _mm512_mask_cvtepi32_storeu_epi16(nnz.bitset + count, 0xFFFF, nnzVal);

                count += popcount(nnzMask);
                indices = _mm512_add_epi32(indices, increment);
            }
    #endif
        }

        ~Cursor() noexcept { nnz.count = count; }

        NNZ&     nnz;
        __m512i  indices;
        unsigned count;
    };

    Cursor make_cursor(Color perspective) noexcept { return {*this, perspective, count}; }

    // indices of non-zero chunks
    u16      bitset[Dimensions / 4];
    unsigned count = 0;

#else
    struct Cursor final {
        Cursor(NNZ& nnz, Color perspective) { out = nnz.bitset + perspective * Dimensions / 64; }

    #if defined(USE_SSSE3) || defined(USE_LSX) || (defined(USE_NEON) && USE_NEON >= 8)
        void record(SIMD::vec_t neurons1, SIMD::vec_t neurons2) noexcept {
            using namespace SIMD;

        #if defined(USE_NEON)
            alignas(16) static constexpr u16 Mask8[8]{1, 16, 2, 32, 4, 64, 8, 128};

            uint32x4_t n1 = vreinterpretq_u32_s16(neurons1);
            uint32x4_t n2 = vreinterpretq_u32_s16(neurons2);

            const uint32x4_t t1 = vtstq_u32(n1, n1);
            const uint32x4_t t2 = vtstq_u32(n2, n2);

            const uint16x8_t packed =
              vtrn1q_u16(vreinterpretq_u16_u32(t1), vreinterpretq_u16_u32(t2));
            const uint16x8_t bits = vandq_u16(packed, vld1q_u16(Mask8));

            *out++ = vaddvq_u16(bits);
        #else
            auto m1 = vec_nnz(neurons1);
            auto m2 = vec_nnz(neurons2);

            if (sizeof(neurons1) == 16)
            {
                *out++ = m1 + (m2 << 4);
            }
            else
            {
                usize bytes = sizeof(neurons1) / 32;
                std::memcpy(out, &m1, bytes);
                out += bytes;
                std::memcpy(out, &m2, bytes);
                out += bytes;
            }
        #endif
        }

    #elif defined(VECTOR)
        void record(SIMD::vec_t, SIMD::vec_t) noexcept {}
    #endif

        u8* out;
    };

    Cursor make_cursor(Color perspective) noexcept { return {*this, perspective}; }

    // Each 8-bit chunk
    u8 bitset[ceil_div(Dimensions, 32)];
#endif
};

}  // namespace DON::NNUE

#endif
