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

#include "../nnue/feature_transformer.h"

#if defined(USE_RVV)
    #include <type_traits>
#endif

#include "../nnue/nnz.h"
#include "../utility/types.h"

namespace DON::NNUE {

void FeatureTransformer::transform_perspective(
  const Color                             perspective,
  const Array<i16, HalfDimensions>&       accumulation,
  Array<Output, BufferSize>&              output,
  [[maybe_unused]] NNZ<OutputDimensions>& nnz) noexcept {
    Index offset = perspective * (HalfDimensions / 2);

    // clang-format off
#if defined(VECTOR)
        [[maybe_unused]] auto cursor = nnz.make_cursor(perspective);

        constexpr Index OutputChunkSize = SIMD::CHUNK_SIZE_MAX;
        static_assert(HalfDimensions % (2 * OutputChunkSize) == 0);
        constexpr Index OutputChunkCount = HalfDimensions / (2 * OutputChunkSize);

    #if !(defined(USE_LSX) || defined(USE_NEON) || defined(__wasm__))
        const SIMD::vec_t zero  = vec_zero();
        const SIMD::vec_t ftMax = vec_set_16(FT_MAX);
    #endif

        const auto* in0 = reinterpret_cast<const SIMD::vec_t*>(&(accumulation[0]));
        const auto* in1 = reinterpret_cast<const SIMD::vec_t*>(&(accumulation[HalfDimensions / 2]));
        auto*       out = reinterpret_cast<SIMD::vec_t*>(&output[offset]);

        // Per the NNUE architecture, here want to multiply pairs of
        // clipped elements and divide the product by 512. To do this,
        // can naively perform min/max operation to clip each of the
        // four int16 vectors, mullo pairs together, then pack them into
        // one int8 vector. However, there exists a faster way.

        // The idea here is to use the implicit clipping from packus to
        // save two vec_max_16 instructions. This clipping works due to
        // the fact that any int16 integer below zero will be zeroed on packus.

        // Consider the case where the second element is negative.
        // If do standard clipping, that element will be zero, which
        // means our pairwise product is zero. If perform packus and
        // remove the lower-side clip for the second element, then
        // product before packus will be negative, and is zeroed on pack.
        // The two operations produce equivalent results, but the second
        // one (using packus) saves one max operation per pair.

        // But here run into a problem: mullo does not preserve the
        // sign of the multiplication. Can get around this by doing mulhi,
        // which keeps the sign. But that requires an additional tweak.

        // mulhi cuts off the last 16 bits of the resulting product,
        // which is the same as performing a rightward shift of 16 bits.
        // Can use this to advantage. Recall that want to divide the final
        // product by 512, which is equivalent to a 9-bit right shift.
        // Intuitively, if we shift the clipped value left by 7, and
        // perform mulhi, which shifts the product right by 16 bits,
        // then will net a right shift of 9 bits.

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

                const SIMD::vec_t hi = SIMD::vec_mulhi_8(p0, p1);

                pack = SIMD::vec_srli_8(hi, 1);

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
                const SIMD::vec_t merged = wasm_i8x16_shuffle(lo, hi,
                                             1,  3,  5,  7,  9, 11, 13, 15,
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
            for (Index i = 0, vl; i < HalfDimensions / 2; i += vl)
            {
                vl = __riscv_vsetvl_e16m2(HalfDimensions / 2 - i);

                vint16m2_t acc0 = __riscv_vle16_v_i16m2(&accumulation[i], vl);
                vint16m2_t acc1 = __riscv_vle16_v_i16m2(&accumulation[i + HalfDimensions / 2], vl);

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
            Bias sum0 = accumulation[i];
            Bias sum1 = accumulation[i + HalfDimensions / 2];

            sum0 = std::clamp<Bias>(sum0, 0, FT_MAX);
            sum1 = std::clamp<Bias>(sum1, 0, FT_MAX);

            output[offset + i] = static_cast<Output>(unsigned(sum0 * sum1) / 512);
        }
#endif
    // clang-format on
}
}  // namespace DON::NNUE
