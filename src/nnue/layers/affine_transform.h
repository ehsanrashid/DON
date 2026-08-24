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

// Definition of layer AffineTransform of NNUE evaluation function

#ifndef NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
#define NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED

#include <iostream>

#include "../../misc.h"
#include "../../types.h"
#include "../ntypes.h"
#include "../serialization.h"
#include "../simd.h"  // IWYU pragma: keep
#include "fallback_affine_transform.h"

#if defined(USE_SSSE3) || defined(USE_LSX) || defined(USE_NEON_DOTPROD)
    #include "../../memory.h"
    #define USE_AFFINE_SIMD
#endif

namespace DON::NNUE::Layers {

// This class defines a fully connected layer (aka affine transform).
//
// Expected use cases:
//   - PaddedInputDimensions == 32 and InputDimensions <= 32.
//   - Small layers.
//   - Inputs are processed in chunks of 4.
//   - The corresponding weights are transposed.
//   - Accumulation is performed directly into int32 values.
//
// AVX-512 support is more difficult to implement because this implementation
// is specifically optimized around these dimensions.
template<IndexType InDims, IndexType OutDims>
class AffineTransform final {
   public:
    // Input/output type
    using InputType  = u8;
    using OutputType = i32;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = OutDims;

    static constexpr IndexType PaddedInputDimensions =
      ceil_to_multiple<IndexType>(InputDimensions, SIMD_WIDTH_MAX);
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, SIMD_WIDTH_MAX);
    static constexpr IndexType ChunkSize =
#if defined(USE_AFFINE_SIMD)
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
#if defined(USE_AFFINE_SIMD)
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

#if defined(USE_AFFINE_SIMD)
        if constexpr (OutputDimensions > 1)
        {
    #if defined(USE_SSSE3)
        #if defined(USE_AVX512)
            using vec_t = __m512i;
            #define vec_set_32 _mm512_set1_epi32
            #define vec_add_dpbusd_32 SIMD::m512_add_dpbusd_epi32
            #define vec_add_32 _mm512_add_epi32
        #elif defined(USE_AVX2)
            using vec_t = __m256i;
            #define vec_set_32 _mm256_set1_epi32
            #define vec_add_dpbusd_32 SIMD::m256_add_dpbusd_epi32
            #define vec_add_32 _mm256_add_epi32
        #elif defined(USE_SSSE3)
            using vec_t = __m128i;
            #define vec_set_32 _mm_set1_epi32
            #define vec_add_dpbusd_32 SIMD::m128_add_dpbusd_epi32
        #endif
    #elif defined(USE_LSX)
        #if defined(USE_LASX)
            using vec_t = __m256i;
            #define vec_set_32 __lasx_xvreplgr2vr_w
            #define vec_add_dpbusd_32 SIMD::lasx_m256_add_dpbusd_epi32
            #define vec_add_32 __lasx_xvadd_w
        #elif defined(USE_LSX)
            using vec_t = __m128i;
            #define vec_set_32 __lsx_vreplgr2vr_w
            #define vec_add_dpbusd_32 SIMD::lsx_m128_add_dpbusd_epi32
            #define vec_add_32 __lsx_vadd_w
        #endif
    #elif defined(USE_NEON_DOTPROD)
            using vec_t = int32x4_t;
        #define vec_set_32 vdupq_n_s32
        #define vec_add_dpbusd_32(acc, a, b) \
            SIMD::neon_m128_add_dpbusd_epi32(acc, vreinterpretq_s8_s32(a), vreinterpretq_s8_s32(b))
    #endif

            constexpr IndexType OutputSimdWidth = sizeof(vec_t) / sizeof(OutputType);

            static_assert(OutputDimensions % OutputSimdWidth == 0);

            constexpr IndexType ChunkCount = ceil_to_multiple<IndexType>(InputDimensions, 8) / 4;
            constexpr IndexType AccCount   = OutputDimensions / OutputSimdWidth;
            constexpr IndexType RegCount =
    #if defined(USE_VNNI) || defined(USE_NEON_DOTPROD)
              AccCount * 2
    #else
              AccCount
    #endif
              ;

            const auto* biasVec = reinterpret_cast<const vec_t*>(biases.data());

            vec_t acc[RegCount];

            for (IndexType k = 0; k < AccCount; ++k)
                acc[k] = biasVec[k];
            for (IndexType k = AccCount; k < RegCount; ++k)
                acc[k] = vec_set_32(0);

            IndexType i = 0;
    #if defined(USE_VNNI) || defined(USE_NEON_DOTPROD)
            for (; i + 1 < ChunkCount; i += 2)
            {
                const vec_t in0 = vec_set_32(load_as<i32>(input + (i + 0) * sizeof(i32)));
                const vec_t in1 = vec_set_32(load_as<i32>(input + (i + 1) * sizeof(i32)));

                const auto* col0 =
                  reinterpret_cast<const vec_t*>(&weights[(i + 0) * OutputDimensions * 4]);
                const auto* col1 =
                  reinterpret_cast<const vec_t*>(&weights[(i + 1) * OutputDimensions * 4]);

                for (IndexType k = 0; k < AccCount; ++k)
                {
                    vec_add_dpbusd_32(acc[k + AccCount * 0], in0, col0[k]);
                    vec_add_dpbusd_32(acc[k + AccCount * 1], in1, col1[k]);
                }
            }

            for (IndexType k = 0; k < AccCount; ++k)
                acc[k] =
        #if defined(USE_VNNI)
                  vec_add_32(acc[k + AccCount * 0], acc[k + AccCount * 1])
        #elif defined(USE_NEON_DOTPROD)
                  vaddq_s32(acc[k + AccCount * 0], acc[k + AccCount * 1])
        #endif
                  ;
    #endif
            for (; i < ChunkCount; ++i)
            {
                const vec_t in = vec_set_32(load_as<i32>(input + i * sizeof(i32)));

                const auto* col =
                  reinterpret_cast<const vec_t*>(&weights[i * OutputDimensions * 4]);

                for (IndexType k = 0; k < AccCount; ++k)
                    vec_add_dpbusd_32(acc[k], in, col[k]);
            }

            auto* outVec = reinterpret_cast<vec_t*>(output);

            for (IndexType k = 0; k < AccCount; ++k)
                outVec[k] = acc[k];

    #undef vec_set_32
    #undef vec_add_dpbusd_32
    #undef vec_add_32
        }
        else if constexpr (OutputDimensions == 1)
        {
    #if defined(USE_SSSE3)
        #if defined(USE_AVX2)
            using vec_t = __m256i;
            #define vec_setzero() _mm256_setzero_si256()
            #define vec_add_dpbusd_32 SIMD::m256_add_dpbusd_epi32
            #define vec_hadd SIMD::m256_hadd
        #elif defined(USE_SSSE3)
            using vec_t = __m128i;
            #define vec_setzero() _mm_setzero_si128()
            #define vec_add_dpbusd_32 SIMD::m128_add_dpbusd_epi32
            #define vec_hadd SIMD::m128_hadd
        #endif
    #elif defined(USE_LSX)
        #if defined(USE_LASX)
            using vec_t = __m256i;
            #define vec_setzero() __lasx_xvldi(0)
            #define vec_add_dpbusd_32 SIMD::lasx_m256_add_dpbusd_epi32
            #define vec_hadd SIMD::lasx_m256_hadd
        #elif defined(USE_LSX)
            using vec_t = __m128i;
            #define vec_setzero() __lsx_vldi(0)
            #define vec_add_dpbusd_32 SIMD::lsx_m128_add_dpbusd_epi32
            #define vec_hadd SIMD::lsx_m128_hadd
        #endif
    #elif defined(USE_NEON_DOTPROD)
            using vec_t = int32x4_t;
        #define vec_setzero() vdupq_n_s32(0)
        #define vec_add_dpbusd_32(acc, a, b) \
            SIMD::neon_m128_add_dpbusd_epi32(acc, vreinterpretq_s8_s32(a), vreinterpretq_s8_s32(b))
        #define vec_hadd SIMD::neon_m128_hadd
    #endif

            const auto* inputVec = reinterpret_cast<const vec_t*>(input);

            constexpr IndexType InputSimdWidth = sizeof(vec_t) / sizeof(InputType);

            static_assert(PaddedInputDimensions % InputSimdWidth == 0);

            constexpr IndexType ChunkCount = PaddedInputDimensions / InputSimdWidth;

            vec_t       sum0 = vec_setzero();
            const auto* row0 = reinterpret_cast<const vec_t*>(&weights[0]);

            for (IndexType i = 0; i < ChunkCount; ++i)
            {
                const vec_t in = inputVec[i];
                vec_add_dpbusd_32(sum0, in, row0[i]);
            }

            output[0] = vec_hadd(sum0, biases[0]);

    #undef vec_setzero
    #undef vec_add_dpbusd_32
    #undef vec_hadd
        }

#else
        // Use fallback implementation for the other architectures
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

#endif  // NNUE_LAYERS_AFFINE_TRANSFORM_H_INCLUDED
