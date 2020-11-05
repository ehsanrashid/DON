#pragma once
// Constants used in NNUE evaluation function

#include <cstring> // For memcpy()
#include <iostream>

#include "../type.h"


//#if defined(USE_AVX512)
//    #include <zmmintrin.h>
//#endif
#if defined(USE_AVX2)
    #include <immintrin.h>
#elif defined(USE_AES)
    #include <wmmintrin.h>
#elif defined(USE_SSE4A)
    #include <ammintrin.h>
#elif defined(USE_SSE42)
    #include <nmmintrin.h>
#elif defined(USE_SSE41)
    #include <smmintrin.h>
#elif defined(USE_SSSE3)
    #include <tmmintrin.h>
#elif defined(USE_SSE3)
    #include <pmmintrin.h>
#elif defined(USE_SSE2)
    #include <emmintrin.h>
#elif defined(USE_MMX)
    #include <mmintrin.h>
#elif defined(USE_NEON)
    #include <arm_neon.h>
#endif

namespace Evaluator::NNUE {

    // Version of the evaluation file
    constexpr uint32_t Version{ 0x7AF32F16u };

    // Constant used in evaluation value calculation
    constexpr int FVScale{ 16 };
    constexpr int WeightScaleBits{ 6 };

    // Size of cache line (in bytes)
    constexpr size_t CacheLineSize{ 64 };

    // SIMD width (in bytes)
#if defined(USE_AVX2) //|| defined(USE_BMI2) || defined(USE_AVX512)
    constexpr size_t SimdWidth{ 32 };
#elif defined(USE_SSE2) //|| defined(USE_SSSE3)
    constexpr size_t SimdWidth{ 16 };
#elif defined(USE_MMX)
    constexpr size_t SimdWidth{  8 };
#elif defined(USE_NEON)
    constexpr size_t SimdWidth{ 16 };
#endif

    constexpr size_t MaxSimdWidth{ 32 };

    // Unique number for each piece type on each square
    enum PieceSquare : uint32_t {
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
    extern const PieceSquare PP_BoardIndex[PIECES][COLORS];

    // Type of input feature after conversion
    using TransformedFeatureType = uint8_t;
    using IndexType = uint32_t;

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
        uint8_t u[sizeof(IntType)];
        istream.read(reinterpret_cast<char*>(u), sizeof(IntType));
        // Use unsigned arithmetic to convert to machine order
        typename std::make_unsigned<IntType>::type v = 0;
        for (size_t i = 0; i < sizeof(IntType); ++i) {
            v = (v << 8) | u[sizeof(IntType) - 1 - i];
        }
        // Copy the machine-ordered bytes into a potentially signed value
        IntType result;
        std::memcpy(&result, &v, sizeof(IntType));
        return result;
    }

}
