// Constants used in NNUE evaluation function
#pragma once

#if defined(USE_AVX2)
#include <immintrin.h>

#elif defined(USE_SSE41)
#include <smmintrin.h>

#elif defined(USE_SSSE3)
#include <tmmintrin.h>

#elif defined(USE_SSE2)
#include <emmintrin.h>

#elif defined(USE_MMX)
#include <mmintrin.h>

#elif defined(USE_NEON)
#include <arm_neon.h>
#endif

// HACK: Use _mm256_loadu_si256() instead of _mm256_load_si256. Otherwise a binary
//       compiled with older g++ crashes because the output memory is not aligned
//       even though alignas is specified.
#if defined(USE_AVX2)
#if defined(__GNUC__ ) && (__GNUC__ < 9)
#define _mm256_loadA_si256  _mm256_loadu_si256
#define _mm256_storeA_si256 _mm256_storeu_si256
#else
#define _mm256_loadA_si256  _mm256_load_si256
#define _mm256_storeA_si256 _mm256_store_si256
#endif
#endif

#if defined(USE_AVX512)
#if defined(__GNUC__ ) && (__GNUC__ < 9)
#define _mm512_loadA_si512   _mm512_loadu_si512
#define _mm512_storeA_si512  _mm512_storeu_si512
#else
#define _mm512_loadA_si512   _mm512_load_si512
#define _mm512_storeA_si512  _mm512_store_si512
#endif
#endif

namespace Evaluator::NNUE {

    // Version of the evaluation file
    constexpr u32 kVersion = 0x7AF32F16u;

    // Constant used in evaluation value calculation
    constexpr int FV_SCALE = 16;
    constexpr int kWeightScaleBits = 6;

    // Size of cache line (in bytes)
    constexpr std::size_t kCacheLineSize = 64;

    // SIMD width (in bytes)
#if defined(USE_AVX2)
    constexpr std::size_t kSimdWidth = 32;

#elif defined(USE_SSE2)
    constexpr std::size_t kSimdWidth = 16;

#elif defined(USE_MMX)
    constexpr std::size_t kSimdWidth = 8;

#elif defined(USE_NEON)
    constexpr std::size_t kSimdWidth = 16;
#endif

    constexpr std::size_t kMaxSimdWidth = 32;

    // Type of input feature after conversion
    using TransformedFeatureType = u08;
    using IndexType = u32;

    // Round n up to be a multiple of base
    template <typename IntType>
    constexpr IntType CeilToMultiple(IntType n, IntType base) {
        return (n + base - 1) / base * base;
    }

}  // namespace Evaluator::NNUE
