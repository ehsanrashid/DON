// Constants used in NNUE evaluation function
#pragma once

#include <cstring> // For std::memset(), std::memcmp()
#include <iostream>

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
    #if defined(__GNUC__) && (__GNUC__ < 9) && defined(_WIN32) && !defined(__clang__)
        #define _mm256_loadA_si256  _mm256_loadu_si256
        #define _mm256_storeA_si256 _mm256_storeu_si256
    #else
        #define _mm256_loadA_si256  _mm256_load_si256
        #define _mm256_storeA_si256 _mm256_store_si256
    #endif
#endif

#if defined(USE_AVX512)
    #if defined(__GNUC__) && (__GNUC__ < 9) && defined(_WIN32) && !defined(__clang__)
        #define _mm512_loadA_si512   _mm512_loadu_si512
        #define _mm512_storeA_si512  _mm512_storeu_si512
    #else
        #define _mm512_loadA_si512   _mm512_load_si512
        #define _mm512_storeA_si512  _mm512_store_si512
    #endif
#endif

namespace Evaluator::NNUE {

    // Version of the evaluation file
    constexpr u32 Version{ 0x7AF32F16u };

    // Constant used in evaluation value calculation
    constexpr int FVScale{ 16 };
    constexpr int WeightScaleBits{ 6 };

    // Size of cache line (in bytes)
    constexpr size_t CacheLineSize{ 64 };

    // SIMD width (in bytes)
#if defined(USE_AVX2)
    constexpr size_t SimdWidth{ 32 };
#elif defined(USE_SSE2)
    constexpr size_t SimdWidth{ 16 };
#elif defined(USE_MMX)
    constexpr size_t SimdWidth{ 8 };
#elif defined(USE_NEON)
    constexpr size_t SimdWidth{ 16 };
#endif

    constexpr size_t MaxSimdWidth{ 32 };

    // Unique number for each piece type on each square
    enum PieceSquare : u32 {
        PS_NONE     =  0,
        PS_W_PAWN   =  1,
        PS_B_PAWN   =  1 * SQUARES + 1,
        PS_W_KNIGHT =  2 * SQUARES + 1,
        PS_B_KNIGHT =  3 * SQUARES + 1,
        PS_W_BISHOP =  4 * SQUARES + 1,
        PS_B_BISHOP =  5 * SQUARES + 1,
        PS_W_ROOK   =  6 * SQUARES + 1,
        PS_B_ROOK   =  7 * SQUARES + 1,
        PS_W_QUEEN  =  8 * SQUARES + 1,
        PS_B_QUEEN  =  9 * SQUARES + 1,
        PS_W_KING   = 10 * SQUARES + 1,
        PS_END      = PS_W_KING, // pieces without kings (pawns included)
        PS_B_KING   = 11 * SQUARES + 1,
        PS_END2     = 12 * SQUARES + 1
    };

    // Array for finding the PieceSquare corresponding to the piece on the board
    extern PieceSquare PP_BoardIndex[PIECES][COLORS];

    // Type of input feature after conversion
    using TransformedFeatureType = u08;
    using IndexType = u32;

    // Round n up to be a multiple of base
    template<typename IntType>
    constexpr IntType ceilToMultiple(IntType n, IntType base) {
        return (n + base - 1) / base * base;
    }

    // readLittleEndian() is our utility to read an integer (signed or unsigned, any size)
    // from a stream in little-endian order. We swap the byte order after the read if
    // necessary to return a result with the byte ordering of the compiling machine.
    template <typename IntType>
    inline IntType readLittleEndian(std::istream &istream) {

        // Read the relevant bytes from the stream in little-endian order
        u08 u[sizeof (IntType)];
        istream.read(reinterpret_cast<char*>(u), sizeof (IntType));
        // Use unsigned arithmetic to convert to machine order
        typename std::make_unsigned<IntType>::type v = 0;
        for (size_t i = 0; i < sizeof (IntType); ++i) {
            v = (v << 8) | u[sizeof (IntType) - i - 1];
        }
        // Copy the machine-ordered bytes into a potentially signed value
        IntType w;
        std::memcpy(&w, &v, sizeof (IntType));
        return w;
    }

}  // namespace Evaluator::NNUE
