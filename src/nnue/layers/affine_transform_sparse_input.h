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

#include <algorithm>
#include <cstdint>
#include <iostream>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../common.h"
#include "../simd.h"

// Definition of layer AffineTransformSparseInput of NNUE evaluation function

// Contains the definition for a fully connected layer (aka affine transform) with block sparse input.

namespace DON::NNUE::Layers {

#if defined(USE_SSSE3) || (defined(USE_NEON) && USE_NEON >= 8)
namespace {

struct Lookup final {

    static constexpr std::size_t  SIZE       = 256;
    static constexpr std::uint8_t INDEX_SIZE = 8;

    StdArray<std::uint16_t, SIZE, INDEX_SIZE> indices{};
    StdArray<std::uint8_t, SIZE>              popcounts{};

    constexpr Lookup() noexcept {

        for (std::size_t i = 0; i < SIZE; ++i)
        {
            std::uint8_t c = 0;

            Bitboard b = i;
            while (b != 0)
            {
                indices[i][c++] = constexpr_lsb(b);

                b &= b - 1;
            }

            popcounts[i] = c;

            while (c < INDEX_SIZE)
                indices[i][c++] = 0;
        }
    }
};

alignas(CACHE_LINE_SIZE) constexpr Lookup LOOKUP{};


// Find indices of nonzero 32-bit values in a packed byte buffer.
// The input pointer addresses a sequence of 32-bit blocks stored in a std::uint8_t array.
template<IndexType InputDimensions>
void find_nnz(const std::uint8_t* RESTRICT input,
              std::uint16_t* RESTRICT      outNnz,
              IndexType&                   outCount) noexcept {

    #if defined(USE_AVX512ICL)
    constexpr IndexType SimdWidthIn  = 64;  // 512 bits
    constexpr IndexType SimdWidthOut = 32;  // 512 bits / 16 bits
    constexpr IndexType ChunkCount   = InputDimensions / SimdWidthOut;

    __m512i increment = _mm512_set1_epi16(SimdWidthOut);
    __m512i base      = _mm512_set_epi16(  // Same permute order as _mm512_packus_epi32()
      31, 30, 29, 28, 15, 14, 13, 12, 27, 26, 25, 24, 11, 10, 9, 8, 23, 22, 21, 20, 7, 6, 5, 4, 19,
      18, 17, 16, 3, 2, 1, 0);

    IndexType count = 0;
    for (IndexType i = 0; i < ChunkCount; ++i)
    {
        __m512i inputV0 = _mm512_load_si512(input + i * 2 * SimdWidthIn);
        __m512i inputV1 = _mm512_load_si512(input + i * 2 * SimdWidthIn + SimdWidthIn);

        // Get a bitmask and gather non zero indices
        __m512i   inputV01 = _mm512_packus_epi32(inputV0, inputV1);
        __mmask32 nnzMask  = _mm512_test_epi16_mask(inputV01, inputV01);
        __m512i   nnzVal   = _mm512_maskz_compress_epi16(nnzMask, base);

        _mm512_storeu_si512(outNnz + count, nnzVal);

        count += LOOKUP.popcounts[nnzMask];
        base = _mm512_add_epi16(base, increment);
    }
    outCount = count;
    #elif defined(USE_AVX512)
    constexpr IndexType SimdWidth  = 16;  // 512 bits / 32 bits
    constexpr IndexType ChunkCount = InputDimensions / SimdWidth;

    __m512i increment = _mm512_set1_epi32(SimdWidth);
    __m512i base      = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);

    IndexType count = 0;
    for (IndexType i = 0; i < ChunkCount; ++i)
    {
        __m512i inputV = _mm512_load_si512(input + i * SimdWidth * sizeof(std::uint32_t));

        // Get a bitmask and gather non zero indices
        __mmask16 nnzMask = _mm512_test_epi32_mask(inputV, inputV);
        __m512i   nnzVal  = _mm512_maskz_compress_epi32(nnzMask, base);

        _mm512_mask_cvtepi32_storeu_epi16(outNnz + count, 0xFFFF, nnzVal);

        count += LOOKUP.popcounts[nnzMask];
        base = _mm512_add_epi32(base, increment);
    }
    outCount = count;
    #else
    using namespace SIMD;

    constexpr IndexType InputSimdWidth = sizeof(vec_uint_t) / sizeof(std::int32_t);
    // Outputs are processed 8 elements at a time, even if the SIMD width is narrower
    constexpr IndexType ChunkSize      = 8;
    constexpr IndexType ChunkCount     = InputDimensions / ChunkSize;
    constexpr IndexType InputsPerChunk = ChunkSize / InputSimdWidth;

    static_assert(InputsPerChunk > 0 && "SIMD width too wide");

    const auto* inputVector = reinterpret_cast<const vec_uint_t*>(input);

    vec128_t increment = vec128_set_16(8);
    vec128_t base      = vec128_zero;

    IndexType count = 0;
    for (IndexType i = 0; i < ChunkCount; ++i)
    {
        // bitmask of nonzero values in this chunk
        unsigned nnz = 0;
        for (IndexType j = 0; j < InputsPerChunk; ++j)
        {
            vec_uint_t inputChunk = inputVector[i * InputsPerChunk + j];

            nnz |= unsigned(vec_nnz(inputChunk)) << (j * InputSimdWidth);
        }

        vec128_t offsets = vec128_load(reinterpret_cast<const vec128_t*>(&LOOKUP.indices[nnz]));

        vec128_storeu(reinterpret_cast<vec128_t*>(outNnz + count), vec128_add(base, offsets));

        count += LOOKUP.popcounts[nnz];
        base = vec128_add(base, increment);
    }
    outCount = count;
    #endif
}

}  // namespace
#endif

// Sparse input implementation
template<IndexType InDims, IndexType OutDims>
class AffineTransformSparseInput final {
   public:
    // Input/output type
    using InputType  = std::uint8_t;
    using OutputType = std::int32_t;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = OutDims;

    static_assert(OutputDimensions % 16 == 0,
                  "Only implemented for OutputDimensions divisible by 16.");

    static constexpr IndexType PaddedInputDimensions =
      ceil_to_multiple<IndexType>(InputDimensions, MAX_SIMD_WIDTH);
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, MAX_SIMD_WIDTH);

    static constexpr IndexType ChunkSize =
#if defined(USE_SSSE3) || (defined(USE_NEON) && USE_NEON >= 8)
      4
#else
      1
#endif
      ;

    using OutputBuffer = StdArray<OutputType, PaddedOutputDimensions>;

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t hash(std::uint32_t preHash) noexcept {
        std::uint32_t h = 0xCC03DAE4U;
        h += OutputDimensions;
        h ^= preHash >> 1;
        h ^= preHash << 31;
        return h;
    }

    static constexpr IndexType weight_index(IndexType i) noexcept {
#if defined(USE_SSSE3) || (defined(USE_NEON) && USE_NEON >= 8)
        return (i / ChunkSize) % (PaddedInputDimensions / ChunkSize) * OutputDimensions * ChunkSize
             + i / PaddedInputDimensions * ChunkSize + i % ChunkSize;
#else
        return i;
#endif
    }

    std::size_t content_hash() const noexcept {
        std::size_t h = 0;
        combine_hash(h, hash_raw_data(biases));
        combine_hash(h, hash_raw_data(weights));
        combine_hash(h, hash(0));
        return h;
    }

    // Read network parameters
    bool read_parameters(std::istream& is) noexcept {

        read_little_endian<BiasType>(is, biases);

        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            weights[weight_index(i)] = read_little_endian<WeightType>(is);

        return !is.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& os) const noexcept {

        write_little_endian<BiasType>(os, biases);

        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            write_little_endian<WeightType>(os, weights[weight_index(i)]);

        return !os.fail();
    }

    // Forward propagation
    void propagate(const InputType* RESTRICT input, OutputType* RESTRICT output) const noexcept {

#if defined(USE_SSSE3) || (defined(USE_NEON) && USE_NEON >= 8)
    #if defined(USE_AVX512)
        using invec_t  = __m512i;
        using outvec_t = __m512i;
        #define vec_add_32 _mm512_add_epi32
        #define vec_set_32 _mm512_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m512_add_dpbusd_epi32
    #elif defined(USE_AVX2)
        using invec_t  = __m256i;
        using outvec_t = __m256i;
        #define vec_add_32 _mm256_add_epi32
        #define vec_set_32 _mm256_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m256_add_dpbusd_epi32
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
            #define vec_add_dpbusd_32 SIMD::dotprod_m128_add_dpbusd_epi32
        #else
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
            #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
            #define vec_add_dpbusd_32 SIMD::neon_m128_add_dpbusd_epi32
        #endif
    #endif

        constexpr IndexType OutputSimdWidth = sizeof(outvec_t) / sizeof(OutputType);

        constexpr IndexType ChunkCount =
          ceil_to_multiple<IndexType>(InputDimensions, 8) / ChunkSize;
        constexpr IndexType AccCount = OutputDimensions / OutputSimdWidth;
        // If using high-latency dot product instructions, split the accumulators
        // to create 3 separate dependency chains and merge at the end
        constexpr IndexType RegCount =
    #if defined(USE_VNNI)
          AccCount * 3
    #else
          AccCount
    #endif
          ;

        std::uint16_t nnz[ChunkCount];
        IndexType     count;
        // Find indices of nonzero 32-bit blocks
        find_nnz<ChunkCount>(input, nnz, count);

        const outvec_t* biasVec = reinterpret_cast<const outvec_t*>(biases.data());

        outvec_t acc[RegCount];

        for (IndexType k = 0; k < AccCount; ++k)
            acc[k] = biasVec[k];

        const auto* RESTRICT beg = nnz;
        const auto* RESTRICT end = nnz + count;

            // clang-format off
    #if defined(USE_VNNI)

        for (IndexType k = AccCount; k < RegCount; ++k)
            acc[k] = vec_zero();

        while (beg + 3 <= end)
        {
            std::size_t i0 = beg[0];
            std::size_t i1 = beg[1];
            std::size_t i2 = beg[2];

            invec_t in0 = vec_set_32(load_as<std::int32_t>(input + i0 * sizeof(std::int32_t)));
            invec_t in1 = vec_set_32(load_as<std::int32_t>(input + i1 * sizeof(std::int32_t)));
            invec_t in2 = vec_set_32(load_as<std::int32_t>(input + i2 * sizeof(std::int32_t)));

            const invec_t* col0 = reinterpret_cast<const invec_t*>(&weights[i0 * OutputDimensions * ChunkSize]);
            const invec_t* col1 = reinterpret_cast<const invec_t*>(&weights[i1 * OutputDimensions * ChunkSize]);
            const invec_t* col2 = reinterpret_cast<const invec_t*>(&weights[i2 * OutputDimensions * ChunkSize]);

            for (IndexType k = 0; k < AccCount; ++k)
            {
                vec_add_dpbusd_32(acc[k + AccCount * 0], in0, col0[k]);
                vec_add_dpbusd_32(acc[k + AccCount * 1], in1, col1[k]);
                vec_add_dpbusd_32(acc[k + AccCount * 2], in2, col2[k]);
            }

            beg += 3;
        }

        for (IndexType k = 0; k < AccCount; ++k)
            acc[k] = vec_add_32(vec_add_32(acc[k + AccCount * 0],
                                           acc[k + AccCount * 1]),
                                           acc[k + AccCount * 2]);
    #endif

        while (beg < end)
        {
            std::size_t i = *beg;

            invec_t in = vec_set_32(load_as<std::int32_t>(input + i * sizeof(std::int32_t)));

            const invec_t* col = reinterpret_cast<const invec_t*>(&weights[i * OutputDimensions * ChunkSize]);

            for (IndexType k = 0; k < AccCount; ++k)
                vec_add_dpbusd_32(acc[k], in, col[k]);

            ++beg;
        }
        // clang-format on

        outvec_t* outVec = reinterpret_cast<outvec_t*>(output);

        for (IndexType k = 0; k < AccCount; ++k)
            outVec[k] = acc[k];

    #undef vec_add_32
    #undef vec_set_32
    #undef vec_add_dpbusd_32
#else
        // Use dense implementation for the other architectures
        transform_affine_non_ssse3<InputDimensions, PaddedInputDimensions, OutputDimensions>(
          biases, weights, input, output);
#endif
    }

   private:
    using BiasType   = OutputType;
    using WeightType = std::int8_t;

    alignas(CACHE_LINE_SIZE) StdArray<BiasType, OutputDimensions> biases;
    alignas(CACHE_LINE_SIZE) StdArray<WeightType, OutputDimensions * PaddedInputDimensions> weights;
};

}  // namespace DON::NNUE::Layers

#endif  // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
