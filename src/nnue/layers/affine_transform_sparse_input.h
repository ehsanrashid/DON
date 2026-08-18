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

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED

#include <iostream>
#include <type_traits>

#if defined(USE_NEON)
    #include <cstring>
#endif

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../types.h"
#include "../common.h"
#include "../simd.h"  // IWYU pragma: keep
#include "affine_transform.h"

#if defined(USE_SSSE3) || defined(USE_LASX) || defined(USE_LSX) \
  || (defined(USE_NEON) && USE_NEON >= 8)
    #include "../../memory.h"
#endif

// Definition of layer AffineTransformSparseInput of NNUE evaluation function

// Contains the definition for a fully connected layer (aka affine transform) with block sparse input.

namespace DON::NNUE::Layers {

// Sparse input implementation
template<IndexType InDims, IndexType OutDims>
class AffineTransformSparseInput final {
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
#if defined(USE_SSSE3) || defined(USE_LASX) || defined(USE_LSX) \
  || (defined(USE_NEON) && USE_NEON >= 8)
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
#if defined(USE_SSSE3) || defined(USE_LASX) || defined(USE_LSX) \
  || (defined(USE_NEON) && USE_NEON >= 8)
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
    void propagate(const InputType* RESTRICT input, OutputType* RESTRICT output) const noexcept {

#if defined(USE_SSSE3) || defined(USE_LASX) || defined(USE_LSX) \
  || (defined(USE_NEON) && USE_NEON >= 8)
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
    #elif defined(USE_SSSE3)
        using invec_t  = __m128i;
        using outvec_t = __m128i;
        #define vec_set_32 _mm_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m128_add_dpbusd_epi32
    #elif defined(USE_NEON)
        #if defined(USE_NEON_DOTPROD)
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
            #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
            #define vec_add_dpbusd_32 SIMD::neon_m128_add_dpbusd_epi32
        #else
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
            #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
            #define vec_add_dpbusd_32 SIMD::neon_m128_add_dpbusd_epi32
        #endif
    #elif defined(USE_LASX)
        using invec_t  = __m256i;
        using outvec_t = __m256i;
        #define vec_set_32 __lasx_xvreplgr2vr_w
        #define vec_add_dpbusd_32 SIMD::lasx_m256_add_dpbusd_epi32
        #define vec_add_32 __lasx_xvadd_w
    #elif defined(USE_LSX)
        using invec_t  = __m128i;
        using outvec_t = __m128i;
        #define vec_set_32 __lsx_vreplgr2vr_w
        #define vec_add_dpbusd_32 SIMD::lsx_m128_add_dpbusd_epi32
        #define vec_add_32 __lsx_vadd_w
    #endif

        constexpr IndexType OutputSimdWidth = sizeof(outvec_t) / sizeof(OutputType);

        constexpr IndexType ChunkCount =
          ceil_to_multiple<IndexType>(InputDimensions, 8) / ChunkSize;
        constexpr IndexType AccCount = OutputDimensions / OutputSimdWidth;
        // If using high-latency dot product instructions, split the accumulators
        // to create 3 separate dependency chains and merge at the end
        constexpr IndexType RegCount =
    #if defined(USE_VNNI) || defined(USE_LASX) || defined(USE_NEON_DOTPROD)
          AccCount * 3
    #else
          AccCount
    #endif
          ;

        Array<NNZOutput, ChunkCount> nnz;
        IndexType                    count;
        // Find indices of nonzero 32-bit blocks
        find_nnz<ChunkCount>(input, nnz.data(), count);

        const outvec_t* biasVec = reinterpret_cast<const outvec_t*>(biases.data());

        outvec_t acc[RegCount];

        for (IndexType k = 0; k < AccCount; ++k)
            acc[k] = biasVec[k];

        // convince GCC to not do weird pointer arithmetic in the following loops
        const i8* cpWeights = weights.data();

        const auto* RESTRICT       p   = nnz.data();
        const auto* const RESTRICT end = p + count;

            // clang-format off
    #if defined(USE_VNNI) || defined(USE_LASX) || defined(USE_NEON_DOTPROD)

        for (IndexType k = AccCount; k < RegCount; ++k)
            acc[k] =
        #if defined(USE_VNNI) || defined(USE_LASX)
              vec_zero()
        #elif defined(USE_NEON_DOTPROD)
              vdupq_n_s32(0)
        #endif
              ;

        for (; p + 2 < end; p += 3)
        {
            const usize i0 = p[0];
            const usize i1 = p[1];
            const usize i2 = p[2];

            const invec_t in0 = vec_set_32(load_as<i32>(input + i0 * sizeof(i32)));
            const invec_t in1 = vec_set_32(load_as<i32>(input + i1 * sizeof(i32)));
            const invec_t in2 = vec_set_32(load_as<i32>(input + i2 * sizeof(i32)));

            const invec_t* col0 = reinterpret_cast<const invec_t*>(&cpWeights[i0 * OutputDimensions * ChunkSize]);
            const invec_t* col1 = reinterpret_cast<const invec_t*>(&cpWeights[i1 * OutputDimensions * ChunkSize]);
            const invec_t* col2 = reinterpret_cast<const invec_t*>(&cpWeights[i2 * OutputDimensions * ChunkSize]);

            for (IndexType k = 0; k < AccCount; ++k)
            {
                vec_add_dpbusd_32(acc[k + AccCount * 0], in0, col0[k]);
                vec_add_dpbusd_32(acc[k + AccCount * 1], in1, col1[k]);
                vec_add_dpbusd_32(acc[k + AccCount * 2], in2, col2[k]);
            }
        }

        for (IndexType k = 0; k < AccCount; ++k)
            acc[k] =
        #if defined(USE_VNNI) || defined(USE_LASX)
              vec_add_32(vec_add_32(acc[k + AccCount * 0],
                                    acc[k + AccCount * 1]),
                                    acc[k + AccCount * 2])
        #elif defined(USE_NEON_DOTPROD)
              vaddq_s32(vaddq_s32(acc[k + AccCount * 0],
                                  acc[k + AccCount * 1]),
                                  acc[k + AccCount * 2])
        #endif
              ;
    #endif

        for (; p < end; ++p)
        {
            const usize i = *p;

            const invec_t in = vec_set_32(load_as<i32>(input + i * sizeof(i32)));

            const invec_t* col = reinterpret_cast<const invec_t*>(&cpWeights[i * OutputDimensions * ChunkSize]);

            for (IndexType k = 0; k < AccCount; ++k)
                vec_add_dpbusd_32(acc[k], in, col[k]);
        }
        // clang-format on

        outvec_t* outVec = reinterpret_cast<outvec_t*>(output);

        for (IndexType k = 0; k < AccCount; ++k)
            outVec[k] = acc[k];

    #undef vec_set_32
    #undef vec_add_dpbusd_32
    #undef vec_add_32
#else
        // Use dense fallback implementation for the other architectures
        transform_affine_non_ssse3<InputDimensions, PaddedInputDimensions, OutputDimensions>(
          biases, weights, input, output);
#endif
    }

   private:
    // NNZ-specific implementation
#if defined(USE_SSSE3) || defined(USE_LASX) || defined(USE_LSX) \
  || (defined(USE_NEON) && USE_NEON >= 8)
    #if defined(USE_NEON)
    using NNZOutput = std::conditional_t<(InDims <= 1024), u8, u16>;
    #else
    using NNZOutput = u16;
    #endif

    struct OffsetIndices final {
       public:
        static constexpr usize MASK_SIZE  = 256;
        static constexpr u8    INDEX_SIZE = 8;

        Array<NNZOutput, MASK_SIZE, INDEX_SIZE> indices{};

        constexpr OffsetIndices() noexcept {

            for (usize i = 0; i < MASK_SIZE; ++i)
            {
                u8 k = 0;

                Bitboard b = i;
                while (b != 0)
                {
                    indices[i][k] = constexpr_lsb(b);
                    ++k;
                    b &= b - 1;
                }

                for (; k < INDEX_SIZE; ++k)
                    indices[i][k] = 0;
            }
        }
    };

    alignas(CACHE_LINE_SIZE) static inline constexpr OffsetIndices OFFSET_INDICES{};

    // Find indices of nonzero 32-bit values in a packed byte buffer.
    // The input pointer addresses a sequence of 32-bit blocks stored in a u8 array.
    template<IndexType ChunkCount>
    static void
    find_nnz(const u8* RESTRICT input, NNZOutput* RESTRICT outNnz, IndexType& outCount) noexcept {

    #if defined(USE_AVX512ICL)
        constexpr IndexType InSimdWidth  = 64;  // 512 bits
        constexpr IndexType OutSimdWidth = 32;  // 512 bits / 16 bits
        constexpr IndexType SimdChunks   = ChunkCount / OutSimdWidth;

        const __m512i increment = _mm512_set1_epi16(OutSimdWidth);
        __m512i       base      = _mm512_set_epi16(  // Same permute order as _mm512_packus_epi32()
          31, 30, 29, 28, 15, 14, 13, 12, 27, 26, 25, 24, 11, 10, 9, 8, 23, 22, 21, 20, 7, 6, 5, 4,
          19, 18, 17, 16, 3, 2, 1, 0);

        IndexType count = 0;
        for (IndexType i = 0; i < SimdChunks; ++i)
        {
            const __m512i iv0 = _mm512_load_si512(input + i * 2 * InSimdWidth + 0 * InSimdWidth);
            const __m512i iv1 = _mm512_load_si512(input + i * 2 * InSimdWidth + 1 * InSimdWidth);

            // Get a bitmask and gather non-zero indices
            const __m512i   iv01    = _mm512_packs_epi32(iv0, iv1);
            const __mmask32 nnzMask = _mm512_test_epi16_mask(iv01, iv01);
            const __m512i   nnzVal  = _mm512_maskz_compress_epi16(nnzMask, base);

            _mm512_storeu_si512(outNnz + count, nnzVal);

            count += popcount(nnzMask);
            base = _mm512_add_epi16(base, increment);
        }
        outCount = count;
    #elif defined(USE_AVX512)
        constexpr IndexType OutSimdWidth = 16;  // 512 bits / 32 bits
        constexpr IndexType SimdChunks   = ChunkCount / OutSimdWidth;

        const __m512i increment = _mm512_set1_epi32(OutSimdWidth);
        __m512i       base = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);

        IndexType count = 0;
        for (IndexType i = 0; i < SimdChunks; ++i)
        {
            const __m512i iv = _mm512_load_si512(input + i * OutSimdWidth * sizeof(u32));

            // Get a bitmask and gather non-zero indices
            const __mmask16 nnzMask = _mm512_test_epi32_mask(iv, iv);
            const __m512i   nnzVal  = _mm512_maskz_compress_epi32(nnzMask, base);

            _mm512_mask_cvtepi32_storeu_epi16(outNnz + count, 0xFFFF, nnzVal);

            count += popcount(nnzMask);
            base = _mm512_add_epi32(base, increment);
        }
        outCount = count;
    #else
        #if defined(USE_NEON)
        // NEON path using u8 NNZOutput
        if constexpr (std::is_same_v<NNZOutput, u8>)
        {
            static_assert(ChunkCount <= 256, "ChunkCount must be <= 256");

            constexpr Array<u16, 8> nnzMasks{1, 2, 4, 8, 16, 32, 64, 128};

            constexpr IndexType SimdChunks = ChunkCount / 8;

            const auto* inputVector = reinterpret_cast<const uint32x4_t*>(input);

            const u64 increment = u64{0x0808080808080808};
            u64       base      = u64{0};

            IndexType count = 0;
            for (IndexType i = 0; i < SimdChunks; ++i)
            {
                const uint32x4_t iv0 = inputVector[i * 2 + 0];
                const uint32x4_t iv1 = inputVector[i * 2 + 1];

                const uint16x8_t nonzeroMask =
                  vcombine_u16(vqmovn_u32(vtstq_u32(iv0, iv0)), vqmovn_u32(vtstq_u32(iv1, iv1)));
                const uint16_t nnzMask =
                  vaddvq_u16(vandq_u16(nonzeroMask, vld1q_u16(nnzMasks.data())));

                u64 offsets;
                std::memcpy(&offsets, OFFSET_INDICES.indices[nnzMask].data(), sizeof(offsets));
                const u64 indices = offsets + base;
                std::memcpy(outNnz + count, &indices, sizeof(indices));

                count += popcount(nnzMask);
                base += increment;
            }
            outCount = count;
        }
        else
        #endif
        {
            constexpr IndexType InputSimdWidth = sizeof(SIMD::vec_uint_t) / sizeof(u32);
            // Outputs are processed 8 elements at a time, even if the SIMD width is narrower
            constexpr IndexType SimdChunkSize  = 8;
            constexpr IndexType SimdChunks     = ChunkCount / SimdChunkSize;
            constexpr IndexType InputsPerChunk = SimdChunkSize / InputSimdWidth;

            static_assert(InputsPerChunk > 0, "SIMD width too wide");

            const auto* inputVector = reinterpret_cast<const SIMD::vec_uint_t*>(input);

            const SIMD::vec128_t increment = vec128_set_16(8);
            SIMD::vec128_t       base      = vec128_zero;

            IndexType count = 0;
            for (IndexType i = 0; i < SimdChunks; ++i)
            {
                // bitmask of nonzero values in this chunk
                unsigned nnzMask = 0;
                for (IndexType j = 0; j < InputsPerChunk; ++j)
                {
                    const SIMD::vec_uint_t iv = inputVector[i * InputsPerChunk + j];

                    nnzMask |= unsigned(vec_nnz(iv)) << (j * InputSimdWidth);
                }

                const SIMD::vec128_t offsets = vec128_load(
                  reinterpret_cast<const SIMD::vec128_t*>(OFFSET_INDICES.indices[nnzMask].data()));

                vec128_storeu(reinterpret_cast<SIMD::vec128_t*>(outNnz + count),
                              vec128_add(base, offsets));

                count += popcount(nnzMask);
                base = vec128_add(base, increment);
            }
            outCount = count;
        }
    #endif
    }
#endif

    using BiasType   = OutputType;
    using WeightType = i8;

    alignas(CACHE_LINE_SIZE) Array<BiasType, OutputDimensions> biases;
    alignas(CACHE_LINE_SIZE) Array<WeightType, OutputDimensions * PaddedInputDimensions> weights;
};

}  // namespace DON::NNUE::Layers

#endif  // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
