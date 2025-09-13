/*
  DON, a UCI chess playing engine derived from Stockfish

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
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../nnue_common.h"
#include "../simd.h"

// Definition of layer AffineTransformSparseInput of NNUE evaluation function

// Contains the definition for a fully connected layer (aka affine transform) with block sparse input.

namespace DON::NNUE::Layers {

#if defined(USE_SSSE3) || (defined(USE_NEON) && (USE_NEON >= 8))
    #if defined(__GNUC__) || defined(__clang__)
        #define RESTRICT __restrict__
    #elif defined(_MSC_VER)
        #define RESTRICT __restrict
    #else
        #define RESTRICT
    #endif

namespace {

constexpr std::uint8_t LsbIndices[64]{0,  47, 1,  56, 48, 27, 2,  60,  //
                                      57, 49, 41, 37, 28, 16, 3,  61,  //
                                      54, 58, 35, 52, 50, 42, 21, 44,  //
                                      38, 32, 29, 23, 17, 11, 4,  62,  //
                                      46, 55, 26, 59, 40, 36, 15, 53,  //
                                      34, 51, 20, 43, 31, 22, 10, 45,  //
                                      25, 39, 14, 33, 19, 30, 9,  24,  //
                                      13, 18, 8,  12, 7,  6,  5,  63};

constexpr std::uint8_t constexpr_lsb(std::uint64_t bb) noexcept {
    assert(bb != 0);
    constexpr std::uint64_t Debruijn64 = 0x03F79D71B4CB0A89ull;
    return LsbIndices[((bb ^ (bb - 1)) * Debruijn64) >> 58];
}

struct Lookup final {

    static constexpr std::size_t  Size      = 256;
    static constexpr std::uint8_t IndexSize = 8;

    std::uint16_t indices[Size][IndexSize]{};
    std::uint8_t  popcounts[Size]{};

    constexpr Lookup() noexcept {
        for (std::size_t i = 0; i < Size; ++i)
        {
            std::uint8_t c = 0;
            // Bitmask
            std::uint64_t b = i;
            while (b)
            {
                indices[i][c++] = constexpr_lsb(b);
                b &= b - 1;
            }

            popcounts[i] = c;

            while (c < IndexSize)
                indices[i][c++] = 0;
        }
    }
};

// Single shared instance across all TUs
alignas(CACHE_LINE_SIZE) constexpr Lookup LookupInstance{};

// Find indices of nonzero numbers in an std::int32_t array
template<IndexType InputDimensions>
void find_nnz(const std::int32_t* RESTRICT input,
              std::uint16_t* RESTRICT      outNnz,
              IndexType&                   outCount) noexcept {

    #if defined(USE_AVX512ICL)

    constexpr IndexType SimdWidthIn  = 16;  // 512 bits / 32 bits
    constexpr IndexType SimdWidthOut = 32;  // 512 bits / 16 bits
    constexpr IndexType NumChunks    = InputDimensions / SimdWidthOut;

    __m512i base      = _mm512_set_epi16(  // Same permute order as _mm512_packus_epi32()
      31, 30, 29, 28, 15, 14, 13, 12, 27, 26, 25, 24, 11, 10, 9, 8, 23, 22, 21, 20, 7, 6, 5, 4, 19,
      18, 17, 16, 3, 2, 1, 0);
    __m512i increment = _mm512_set1_epi16(SimdWidthOut);

    IndexType count = 0;
    for (IndexType i = 0; i < NumChunks; ++i)
    {
        __m512i inputV0 = _mm512_load_si512(input + i * 2 * SimdWidthIn);
        __m512i inputV1 = _mm512_load_si512(input + i * 2 * SimdWidthIn + SimdWidthIn);

        // Get a bitmask and gather non zero indices
        __m512i   inputV01 = _mm512_packus_epi32(inputV0, inputV1);
        __mmask32 nnzMask  = _mm512_test_epi16_mask(inputV01, inputV01);

        // Avoid _mm512_mask_compressstoreu_epi16() as it's 256 uOps on Zen4
        __m512i nnz = _mm512_maskz_compress_epi16(nnzMask, base);
        _mm512_storeu_si512(outNnz + count, nnz);

        count += LookupInstance.popcounts[nnzMask];
        base = _mm512_add_epi16(base, increment);
    }
    outCount = count;

    #elif defined(USE_AVX512)

    constexpr IndexType SimdWidth = 16;  // 512 bits / 32 bits
    constexpr IndexType NumChunks = InputDimensions / SimdWidth;

    __m512i base      = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    __m512i increment = _mm512_set1_epi32(SimdWidth);

    IndexType count = 0;
    for (IndexType i = 0; i < NumChunks; ++i)
    {
        __m512i inputV = _mm512_load_si512(input + i * SimdWidth);

        // Get a bitmask and gather non zero indices
        __mmask16 nnzMask = _mm512_test_epi32_mask(inputV, inputV);
        __m512i   nnzV    = _mm512_maskz_compress_epi32(nnzMask, base);
        _mm512_mask_cvtepi32_storeu_epi16(outNnz + count, 0xFFFF, nnzV);
        count += LookupInstance.popcounts[nnzMask];
        base = _mm512_add_epi32(base, increment);
    }
    outCount = count;

    #else

    using namespace SIMD;

    constexpr IndexType InputSimdWidth = sizeof(vec_uint_t) / sizeof(std::int32_t);
    // Inputs are processed InputSimdWidth at a time and outputs are processed 8 at a time so we process in chunks of max(InputSimdWidth, 8)
    constexpr IndexType ChunkSize       = std::max<IndexType>(InputSimdWidth, 8);
    constexpr IndexType NumChunks       = InputDimensions / ChunkSize;
    constexpr IndexType InputsPerChunk  = ChunkSize / InputSimdWidth;
    constexpr IndexType OutputsPerChunk = ChunkSize / 8;

    const auto* inputVector = reinterpret_cast<const vec_uint_t*>(input);

    vec128_t base      = vec128_zero;
    vec128_t increment = vec128_set_16(8);

    IndexType count = 0;
    for (IndexType i = 0; i < NumChunks; ++i)
    {
        // bitmask of nonzero values in this chunk
        unsigned nnz = 0;
        for (IndexType j = 0; j < InputsPerChunk; ++j)
        {
            vec_uint_t inputChunk = inputVector[i * InputsPerChunk + j];
            nnz |= unsigned(vec_nnz(inputChunk)) << (j * InputSimdWidth);
        }
        for (IndexType j = 0; j < OutputsPerChunk; ++j)
        {
            unsigned nnzMask = (nnz >> (j * 8)) & 0xFF;
            vec128_t offsets =
              vec128_load(reinterpret_cast<const vec128_t*>(&LookupInstance.indices[nnzMask]));
            vec128_storeu(reinterpret_cast<vec128_t*>(outNnz + count), vec128_add(base, offsets));
            count += LookupInstance.popcounts[nnzMask];
            base = vec128_add(base, increment);
        }
    }
    outCount = count;
    #endif
}

}  // namespace
#endif

// Sparse input implementation
template<IndexType InDims, IndexType OutDims>
class AffineTransformSparseInput {
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
#if defined(USE_SSSE3) || (defined(USE_NEON) && (USE_NEON >= 8))
      4
#else
      1
#endif
      ;

    using OutputBuffer = OutputType[PaddedOutputDimensions];

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value(std::uint32_t preHashValue) noexcept {
        std::uint32_t hashValue = 0xCC03DAE4u;
        hashValue += OutputDimensions;
        hashValue ^= preHashValue >> 1;
        hashValue ^= preHashValue << 31;
        return hashValue;
    }

    static constexpr IndexType get_weight_index(IndexType i) noexcept {
#if defined(USE_SSSE3) || (defined(USE_NEON) && (USE_NEON >= 8))
        return (i / ChunkSize) % (PaddedInputDimensions / ChunkSize) * OutputDimensions * ChunkSize
             + i / PaddedInputDimensions * ChunkSize + i % ChunkSize;
#else
        return i;
#endif
    }

    // Read network parameters
    bool read_parameters(std::istream& stream) noexcept {
        read_little_endian<BiasType>(stream, biases, OutputDimensions);
        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            weights[get_weight_index(i)] = read_little_endian<WeightType>(stream);

        return !stream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& stream) const noexcept {
        write_little_endian<BiasType>(stream, biases, OutputDimensions);
        for (IndexType i = 0; i < OutputDimensions * PaddedInputDimensions; ++i)
            write_little_endian<WeightType>(stream, weights[get_weight_index(i)]);

        return !stream.fail();
    }

    // Forward propagation
    void propagate(const InputType* input, OutputType* output) const noexcept {

#if defined(USE_SSSE3) || (defined(USE_NEON) && (USE_NEON >= 8))
    #if defined(USE_AVX512)
        using invec_t  = __m512i;
        using outvec_t = __m512i;
        #define vec_set_32 _mm512_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m512_add_dpbusd_epi32
    #elif defined(USE_AVX2)
        using invec_t  = __m256i;
        using outvec_t = __m256i;
        #define vec_set_32 _mm256_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m256_add_dpbusd_epi32
    #elif defined(USE_SSSE3)
        using invec_t  = __m128i;
        using outvec_t = __m128i;
        #define vec_set_32 _mm_set1_epi32
        #define vec_add_dpbusd_32 SIMD::m128_add_dpbusd_epi32
    #elif defined(USE_NEON_DOTPROD)
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
        #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
        #define vec_add_dpbusd_32 SIMD::dotprod_m128_add_dpbusd_epi32
    #elif defined(USE_NEON)
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
        #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
        #define vec_add_dpbusd_32 SIMD::neon_m128_add_dpbusd_epi32
    #endif

        constexpr IndexType OutputSimdWidth = sizeof(outvec_t) / sizeof(OutputType);

        constexpr IndexType ChunkCount =
          ceil_to_multiple<IndexType>(InputDimensions, 8) / ChunkSize;
        constexpr IndexType RegCount = OutputDimensions / OutputSimdWidth;

        const auto* input32 = reinterpret_cast<const std::int32_t*>(input);

        std::uint16_t nnz[ChunkCount];
        IndexType     count;
        // Find indices of nonzero 32-bit blocks
        find_nnz<ChunkCount>(input32, nnz, count);

        const auto* biasVec = reinterpret_cast<const outvec_t*>(biases);
        outvec_t    acc[RegCount];
        for (IndexType k = 0; k < RegCount; ++k)
            acc[k] = biasVec[k];

        for (IndexType j = 0; j < count; ++j)
        {
            auto    i  = nnz[j];
            invec_t in = vec_set_32(input32[i]);

            const auto* col =
              reinterpret_cast<const invec_t*>(&weights[i * OutputDimensions * ChunkSize]);
            for (IndexType k = 0; k < RegCount; ++k)
                vec_add_dpbusd_32(acc[k], in, col[k]);
        }

        auto* outVec = reinterpret_cast<outvec_t*>(output);
        for (IndexType k = 0; k < RegCount; ++k)
            outVec[k] = acc[k];

    #undef vec_set_32
    #undef vec_add_dpbusd_32
#else
        // Use dense implementation for the other architectures.
        affine_transform_non_ssse3<InputDimensions, PaddedInputDimensions, OutputDimensions>(
          biases, weights, input, output);
#endif
    }

   private:
    using BiasType   = OutputType;
    using WeightType = std::int8_t;

    alignas(CACHE_LINE_SIZE) BiasType biases[OutputDimensions];
    alignas(CACHE_LINE_SIZE) WeightType weights[OutputDimensions * PaddedInputDimensions];
};

}  // namespace DON::NNUE::Layers

#endif  // #ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
