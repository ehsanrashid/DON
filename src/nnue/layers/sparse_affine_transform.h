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

#ifndef NNUE_LAYERS_SPARSE_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_SPARSE_AFFINE_TRANSFORM_H_INCLUDED

#include <iostream>
#include <type_traits>

#if defined(USE_NEON)
    #include <cstring>
#endif

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../types.h"
#include "../nnz.h"
#include "../ntypes.h"
#include "../serialization.h"
#include "../simd.h"  // IWYU pragma: keep
#include "fallback_affine_transform.h"

#if defined(USE_SSSE3) || defined(USE_LSX) || (defined(USE_NEON) && USE_NEON >= 8)
    #include "../../memory.h"
    #define USE_SPARSE_AFFINE_SIMD
#endif

namespace DON::NNUE::Layers {

// This class defines a fully connected layer (aka affine transform) with block-sparse input.
//
// Expected use cases:
//   - Large input dimensions with relatively few active features.
//   - Block-sparse input, where only active blocks are processed.
//   - The first layer of the NNUE evaluation function.
//
// Only active input blocks are processed, avoiding unnecessary computation
// for inactive blocks.
template<IndexType InDims, IndexType OutDims>
class SparseAffineTransform final {
   public:
    // Input/output type
    using InputType  = u8;
    using OutputType = i32;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = OutDims;

    static_assert(OutputDimensions % 16 == 0,
                  "Only implemented for OutputDimensions divisible by 16.");

    static constexpr IndexType PaddedInputDimensions =
      ceil_to_multiple<IndexType>(InputDimensions, SIMD_WIDTH_MAX);
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, SIMD_WIDTH_MAX);

    static constexpr IndexType ChunkSize =
#if defined(USE_SPARSE_AFFINE_SIMD) || defined(USE_RVV)
      4
#else
      1
#endif
      ;

    using OutputBuffer = Array<OutputType, PaddedOutputDimensions>;

    // Hash value embedded in the evaluation file
    static constexpr u32 hash(u32 preHash) noexcept {
        u32 h = 0xCC03DAE4u;
        h += OutputDimensions;
        h ^= preHash >> 1;
        h ^= preHash << 31;
        return h;
    }

    static constexpr IndexType weight_index(IndexType i) noexcept {
#if defined(USE_SPARSE_AFFINE_SIMD)
        return (i / ChunkSize) % (PaddedInputDimensions / ChunkSize) * OutputDimensions * ChunkSize
             + i / PaddedInputDimensions * ChunkSize + i % ChunkSize;
#else
        return i;
#endif
    }

    usize content_hash() const noexcept {
        usize h = 0;
        combine_hash(h, hash_raw_data(biases));
        combine_hash(h, hash_raw_data(weights));
        combine_hash(h, hash(0));
        return h;
    }

    // Read network parameters
    bool read_parameters(std::istream& is) noexcept {

        read_little_endian(is, biases);

        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            weights[weight_index(i)] = read_little_endian<WeightType>(is);

        return !is.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& os) const noexcept {

        write_little_endian(os, biases);

        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            write_little_endian<WeightType>(os, weights[weight_index(i)]);

        return !os.fail();
    }

    // Forward propagation
    void propagate(const InputType* RESTRICT           input,
                   OutputType* RESTRICT                output,
                   [[maybe_unused]] const NNZ<InDims>& nnz) const noexcept {

#if defined(USE_SPARSE_AFFINE_SIMD)
    #if defined(USE_SSSE3)
        #if defined(USE_AVX512)
        using invec_t  = __m512i;
        using outvec_t = __m512i;
            #define vec_set_32 _mm512_set1_epi32
            #define vec_add_dpbusd_32 SIMD::m512_add_dpbusd_epi32
            #define vec_add_32 _mm512_add_epi32
        #elif defined(USE_AVX2)
        using invec_t  = __m256i;
        using outvec_t = __m256i;
            #define vec_set_32 _mm256_set1_epi32
            #define vec_add_dpbusd_32 SIMD::m256_add_dpbusd_epi32
            #define vec_add_32 _mm256_add_epi32
        #else
        using invec_t  = __m128i;
        using outvec_t = __m128i;
            #define vec_set_32 _mm_set1_epi32
            #define vec_add_dpbusd_32 SIMD::m128_add_dpbusd_epi32
        #endif
    #elif defined(USE_LSX)
        #if defined(USE_LASX)
        using invec_t  = __m256i;
        using outvec_t = __m256i;
            #define vec_set_32 __lasx_xvreplgr2vr_w
            #define vec_add_dpbusd_32 SIMD::lasx_m256_add_dpbusd_epi32
            #define vec_add_32 __lasx_xvadd_w
        #else
        using invec_t  = __m128i;
        using outvec_t = __m128i;
            #define vec_set_32 __lsx_vreplgr2vr_w
            #define vec_add_dpbusd_32 SIMD::lsx_m128_add_dpbusd_epi32
            #define vec_add_32 __lsx_vadd_w
        #endif
    #elif defined(USE_NEON) && USE_NEON >= 8
        #if defined(USE_NEON_DOTPROD)
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
            #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
            #define vec_add_dpbusd_32 SIMD::dotprod_m128_add_dpbusd_epi32
        #else
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
            #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
            #define vec_add_dpbusd_32 SIMD::neon8_m128_add_dpbusd_epi32
        #endif
    #endif

        constexpr IndexType OutputSimdWidth = sizeof(outvec_t) / sizeof(OutputType);

        constexpr IndexType AccCount = OutputDimensions / OutputSimdWidth;
        // If using high-latency dot product instructions, split the accumulators
        // to create 3 separate dependency chains and merge at the end
        constexpr IndexType RegCount =
    #if defined(USE_VNNI) && defined(USE_AVX512)
          AccCount * 3
    #else
          AccCount
    #endif
          ;

        const auto* biasVec = reinterpret_cast<const outvec_t*>(biases.data());

        outvec_t acc[RegCount];

        for (IndexType k = 0; k < AccCount; ++k)
            acc[k] = biasVec[k];

        // Convince GCC to not do weird pointer arithmetic in the following loops
        const i8* w = weights.data();
            // clang-format off
    #if defined(USE_AVX512)
        for (IndexType k = AccCount; k < RegCount; ++k)
            acc[k] = vec_zero();

        const auto* RESTRICT       p   = nnz.bitset;
        const auto* const RESTRICT end = p + nnz.count;

        #if defined(USE_VNNI)
        for (; p + 2 < end; p += 3)
        {
            const usize i0 = p[0];
            const usize i1 = p[1];
            const usize i2 = p[2];

            const invec_t in0 = vec_set_32(load_as<i32>(input + i0 * sizeof(i32)));
            const invec_t in1 = vec_set_32(load_as<i32>(input + i1 * sizeof(i32)));
            const invec_t in2 = vec_set_32(load_as<i32>(input + i2 * sizeof(i32)));

            const auto* col0 = reinterpret_cast<const invec_t*>(&w[i0 * OutputDimensions * ChunkSize]);
            const auto* col1 = reinterpret_cast<const invec_t*>(&w[i1 * OutputDimensions * ChunkSize]);
            const auto* col2 = reinterpret_cast<const invec_t*>(&w[i2 * OutputDimensions * ChunkSize]);

            for (IndexType k = 0; k < AccCount; ++k)
            {
                vec_add_dpbusd_32(acc[k + AccCount * 0], in0, col0[k]);
                vec_add_dpbusd_32(acc[k + AccCount * 1], in1, col1[k]);
                vec_add_dpbusd_32(acc[k + AccCount * 2], in2, col2[k]);
            }
        }

        for (IndexType k = 0; k < AccCount; ++k)
            acc[k] = vec_add_32(vec_add_32(acc[k + AccCount * 0],
                                           acc[k + AccCount * 1]),
                                           acc[k + AccCount * 2]);
        #endif

        for (; p < end; ++p)
        {
            const usize i = *p;

            const invec_t in = vec_set_32(load_as<i32>(input + i * sizeof(i32)));

            const auto* col = reinterpret_cast<const invec_t*>(&w[i * OutputDimensions * ChunkSize]);

            for (IndexType k = 0; k < AccCount; ++k)
                vec_add_dpbusd_32(acc[k], in, col[k]);
        }

    #else
        static_assert(InputDimensions % 256 == 0);

        for (IndexType k = 0; k < InputDimensions / 256; ++k)
        {
            u64   bits = load_as<u64>(nnz.bitset + k * 8);
            usize base = k * 64;

            auto* inBase = input + base * sizeof(i32);
            auto* wBase  = &w[base * OutputDimensions * ChunkSize];

        #if defined(__GNUC__) && __GNUC__ >= 15 && !defined(__clang__) && defined(USE_NEON_DOTPROD)
            #define FIX_GCC15_NEON_DOTPROD_MISOPTIMIZATION
        #endif
        // GCC 15 pessimizes the following code on ARM64 by eliding the intermediate
        // computation of key pointers (inBase, wBase, col, inPtr), leading
        // to a lot of redundant indexing arithmetic in the while (bits) loop.
        // The optimization barriers force these pointers to be calculated and used.
        #if defined(FIX_GCC15_NEON_DOTPROD_MISOPTIMIZATION)
            asm("" : "+r"(inBase), "+r"(wBase));  // opt barrier
        #endif

            while (bits != 0)
            {
                const u8    i     = pop_lsq(bits);
                const auto* inPtr = inBase + i * sizeof(i32);
                const auto* col   = reinterpret_cast<const invec_t*>(&wBase[i * OutputDimensions * ChunkSize]);

        #if defined(FIX_GCC15_NEON_DOTPROD_MISOPTIMIZATION)
                asm("" : "+r"(col), "+r"(inPtr));
        #endif

                const invec_t in = vec_set_32(load_as<i32>(inPtr));
                for (IndexType l = 0; l < AccCount; ++l)
                    vec_add_dpbusd_32(acc[l], in, col[l]);
            }
        #undef FIX_GCC15_NEON_DOTPROD_MISOPTIMIZATION
        }

    #endif
        // clang-format on

        auto* outVec = reinterpret_cast<outvec_t*>(output);

        for (IndexType k = 0; k < AccCount; ++k)
            outVec[k] = acc[k];

    #undef vec_set_32
    #undef vec_add_dpbusd_32
    #undef vec_add_32

#elif defined(USE_RVV)
        static_assert(InputDimensions % 256 == 0);

        const i8* w = weights.data();

    #define RVV_SPARSE_PROPAGATE(LMUL) \
        do \
        { \
            const usize blk = __riscv_vsetvlmax_e32m##LMUL(); \
            for (IndexType ob = 0; ob < OutputDimensions; ob += blk) \
            { \
                const usize       vl  = __riscv_vsetvl_e32m##LMUL(OutputDimensions - ob); \
                vint32m##LMUL##_t acc = __riscv_vle32_v_i32m##LMUL(biases + ob, vl); \
                for (IndexType k = 0; k < InputDimensions / 256; ++k) \
                { \
                    u64   bits         = load_as<u64>(nnz.bitset + k * 8); \
                    isize base         = k * 64; \
                    auto* base_addr    = input + base * sizeof(i32); \
                    auto* weights_base = &w[base * OutputDimensions * ChunkSize]; \
                    while (bits) \
                    { \
                        const isize       i = pop_lsq(bits); \
                        vuint8m##LMUL##_t a = __riscv_vreinterpret_v_u32m##LMUL##_u8m##LMUL( \
                          __riscv_vmv_v_x_u32m##LMUL(load_as<u32>(base_addr + i * sizeof(i32)), \
                                                     vl)); \
                        vint8m##LMUL##_t b = __riscv_vle8_v_i8m##LMUL( \
                          &weights_base[i * OutputDimensions * ChunkSize + ob * ChunkSize], \
                          vl * ChunkSize); \
                        acc = \
                          __riscv_vadd_vv_i32m##LMUL(acc, SIMD::rvv_dpbusd_m##LMUL(a, b, vl), vl); \
                    } \
                } \
                __riscv_vse32_v_i32m##LMUL(output + ob, acc, vl); \
            } \
        } while (0)

        // Select LMUL
        if (__riscv_vsetvlmax_e32m1() >= OutputDimensions)
            RVV_SPARSE_PROPAGATE(1);
        else if (__riscv_vsetvlmax_e32m2() >= OutputDimensions)
            RVV_SPARSE_PROPAGATE(2);
        else
            RVV_SPARSE_PROPAGATE(4);

    #undef RVV_SPARSE_PROPAGATE

#else
        // Use dense fallback implementation for the other architectures
        fallback_affine_transform<InputDimensions, PaddedInputDimensions, OutputDimensions>(
          biases, weights, input, output);
#endif
    }

   private:
    using BiasType   = OutputType;
    using WeightType = i8;

    alignas(CACHE_LINE_SIZE) Array<BiasType, OutputDimensions> biases;
    alignas(CACHE_LINE_SIZE) Array<WeightType, OutputDimensions * PaddedInputDimensions> weights;
};

}  // namespace DON::NNUE::Layers

#endif  // NNUE_LAYERS_SPARSE_AFFINE_TRANSFORM_H_INCLUDED
