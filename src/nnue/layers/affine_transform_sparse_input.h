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

// Definition of layer AffineTransformSparseInput of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_SPARSE_INPUT_H_INCLUDED

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../nnue_common.h"
#include "affine_transform.h"
#include "simd.h"

/*
  This file contains the definition for a fully connected layer (aka affine transform) with block sparse input.
*/

namespace DON::NNUE::Layers {

#if (USE_SSSE3 | (USE_NEON >= 8))
alignas(CACHE_LINE_SIZE) static inline const std::array2d<std::uint16_t, 256, 8> LookupIndices =
  []() {
      std::array2d<std::uint16_t, 256, 8> v{};
      for (std::size_t i = 0; i < v.size(); ++i)
      {
          std::size_t k = 0;

          Bitboard b = i;
          while (b)
              v[i][k++] = pop_lsb(b);
      }
      return v;
  }();

// Find indices of nonzero numbers in an std::int32_t array
template<IndexType InputDimensions>
void find_nnz(const std::int32_t* input, std::uint16_t* outNnz, IndexType& outCount) noexcept {
    #if defined(USE_SSSE3)
        #if defined(USE_AVX512)
    using vec_t = __m512i;
            #define vec_nnz(a) _mm512_cmpgt_epi32_mask(a, _mm512_setzero_si512())
        #elif defined(USE_AVX2)
    using vec_t = __m256i;
            #if defined(USE_VNNI) && !defined(USE_AVXVNNI)
                #define vec_nnz(a) _mm256_cmpgt_epi32_mask(a, _mm256_setzero_si256())
            #else
                #define vec_nnz(a) \
                    _mm256_movemask_ps( \
                      _mm256_castsi256_ps(_mm256_cmpgt_epi32(a, _mm256_setzero_si256())))
            #endif
        #elif defined(USE_SSSE3)
    using vec_t = __m128i;
            #define vec_nnz(a) \
                _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpgt_epi32(a, _mm_setzero_si128())))
        #endif
    using vec128_t = __m128i;
        #define vec128_zero _mm_setzero_si128()
        #define vec128_set_16(a) _mm_set1_epi16(a)
        #define vec128_load(a) _mm_load_si128(a)
        #define vec128_storeu(a, b) _mm_storeu_si128(a, b)
        #define vec128_add(a, b) _mm_add_epi16(a, b)
    #elif defined(USE_NEON)
    using vec_t = uint32x4_t;
    static const std::uint32_t Mask[4]{1, 2, 4, 8};
        #define vec_nnz(a) vaddvq_u32(vandq_u32(vtstq_u32(a, a), vld1q_u32(Mask)))
    using vec128_t = uint16x8_t;
        #define vec128_zero vdupq_n_u16(0)
        #define vec128_set_16(a) vdupq_n_u16(a)
        #define vec128_load(a) vld1q_u16(reinterpret_cast<const std::uint16_t*>(a))
        #define vec128_storeu(a, b) vst1q_u16(reinterpret_cast<std::uint16_t*>(a), b)
        #define vec128_add(a, b) vaddq_u16(a, b)
    #endif
    constexpr IndexType InputSimdWidth = sizeof(vec_t) / sizeof(std::int32_t);
    // Inputs are processed InputSimdWidth at a time and outputs are processed 8 at a time so process in chunks of max(InputSimdWidth, 8)
    constexpr IndexType CHUNK_SIZE        = std::max<IndexType>(InputSimdWidth, 8);
    constexpr IndexType CHUNK_COUNT       = InputDimensions / CHUNK_SIZE;
    constexpr IndexType INPUTS_PER_CHUNK  = CHUNK_SIZE / InputSimdWidth;
    constexpr IndexType OUTPUTS_PER_CHUNK = CHUNK_SIZE / 8;

    auto      inputVector = reinterpret_cast<const vec_t*>(input);
    IndexType count       = 0;
    vec128_t  base        = vec128_zero;
    vec128_t  increment   = vec128_set_16(8);
    for (IndexType i = 0; i < CHUNK_COUNT; ++i)
    {
        // Bitmask of nonzero values in this chunk
        unsigned nnz = 0;
        for (IndexType j = 0; j < INPUTS_PER_CHUNK; ++j)
        {
            vec_t inputChunk = inputVector[i * INPUTS_PER_CHUNK + j];
            nnz |= unsigned(vec_nnz(inputChunk)) << (j * InputSimdWidth);
        }
        for (IndexType j = 0; j < OUTPUTS_PER_CHUNK; ++j)
        {
            auto lookup = (nnz >> (j * 8)) & 0xFF;
            auto offset = vec128_load(reinterpret_cast<const vec128_t*>(&LookupIndices[lookup]));
            vec128_storeu(reinterpret_cast<vec128_t*>(outNnz + count), vec128_add(base, offset));
            count += popcount(lookup);
            base = vec128_add(base, increment);
        }
    }
    outCount = count;
}
    #undef vec_nnz
    #undef vec128_zero
    #undef vec128_set_16
    #undef vec128_load
    #undef vec128_storeu
    #undef vec128_add
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

    static constexpr IndexType CHUNK_SIZE =
#if (USE_SSSE3 | (USE_NEON >= 8))
      4;
#else
      1;
#endif

    using OutputBuffer = OutputType[PaddedOutputDimensions];

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value(std::uint32_t prevHash) noexcept {
        std::uint32_t hashValue = 0xCC03DAE4u;
        hashValue += OutputDimensions;
        hashValue ^= prevHash >> 1;
        hashValue ^= prevHash << 31;
        return hashValue;
    }

    static constexpr IndexType get_weight_index_scrambled(IndexType i) noexcept {
        // clang-format off
        return (i / CHUNK_SIZE) % (PaddedInputDimensions / CHUNK_SIZE) * OutputDimensions * CHUNK_SIZE
             + i / PaddedInputDimensions * CHUNK_SIZE + i % CHUNK_SIZE;
        // clang-format on
    }

    static constexpr IndexType get_weight_index(IndexType i) noexcept {
#if (USE_SSSE3 | (USE_NEON >= 8))
        return get_weight_index_scrambled(i);
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

#if (USE_SSSE3 | (USE_NEON >= 8))
    #if defined(USE_AVX512)
        using invec_t  = __m512i;
        using outvec_t = __m512i;
        #define vec_set_32 _mm512_set1_epi32
        #define vec_add_dpbusd_32 Simd::m512_add_dpbusd_epi32
    #elif defined(USE_AVX2)
        using invec_t  = __m256i;
        using outvec_t = __m256i;
        #define vec_set_32 _mm256_set1_epi32
        #define vec_add_dpbusd_32 Simd::m256_add_dpbusd_epi32
    #elif defined(USE_SSSE3)
        using invec_t  = __m128i;
        using outvec_t = __m128i;
        #define vec_set_32 _mm_set1_epi32
        #define vec_add_dpbusd_32 Simd::m128_add_dpbusd_epi32
    #elif defined(USE_NEON_DOTPROD)
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
        #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
        #define vec_add_dpbusd_32 Simd::dotprod_m128_add_dpbusd_epi32
    #elif defined(USE_NEON)
        using invec_t  = int8x16_t;
        using outvec_t = int32x4_t;
        #define vec_set_32(a) vreinterpretq_s8_u32(vdupq_n_u32(a))
        #define vec_add_dpbusd_32 Simd::neon_m128_add_dpbusd_epi32
    #endif

        static constexpr IndexType OutputSimdWidth = sizeof(outvec_t) / sizeof(OutputType);

        constexpr IndexType CHUNK_COUNT =
          ceil_to_multiple<IndexType>(InputDimensions, 8) / CHUNK_SIZE;
        constexpr IndexType REG_COUNT = OutputDimensions / OutputSimdWidth;

        std::uint16_t nnz[CHUNK_COUNT];
        IndexType     count;

        auto input32 = reinterpret_cast<const std::int32_t*>(input);

        // Find indices of nonzero 32-bit blocks
        find_nnz<CHUNK_COUNT>(input32, nnz, count);

        auto     biasvec = reinterpret_cast<const outvec_t*>(biases);
        outvec_t acc[REG_COUNT];
        for (IndexType k = 0; k < REG_COUNT; ++k)
            acc[k] = biasvec[k];

        for (IndexType j = 0; j < count; ++j)
        {
            auto    i  = nnz[j];
            invec_t in = vec_set_32(input32[i]);
            auto    col =
              reinterpret_cast<const invec_t*>(&weights[i * OutputDimensions * CHUNK_SIZE]);
            for (IndexType k = 0; k < REG_COUNT; ++k)
                vec_add_dpbusd_32(acc[k], in, col[k]);
        }

        auto outptr = reinterpret_cast<outvec_t*>(output);
        for (IndexType k = 0; k < REG_COUNT; ++k)
            outptr[k] = acc[k];
    #undef vec_set_32
    #undef vec_add_dpbusd_32
#else
        // Use dense implementation for the other architectures.
        affine_transform_non_ssse3<InputDimensions, PaddedInputDimensions, OutputDimensions>(
          output, weights, biases, input);
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