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

#ifndef BITBOARD_H_INCLUDED
#define BITBOARD_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(USE_AVX512)
    #include <immintrin.h>
#endif
#if defined(USE_BMI2)
    #include <immintrin.h>  // Header for _pext_u64() & _pdep_u64() intrinsic
    // * _pext_u64(src, mask) - Parallel Bits Extract
    // Extracts the bits from the 64-bit 'src' corresponding to the 1-bits in 'mask',
    // and packs them contiguously into the lower bits.
    // * _pdep_u64(src, mask) - Parallel Bits Deposit
    // Deposits the lower bits of 'src' into the positions of the 1-bits in 'mask',
    // leaving all other bits as zero.
#endif
#if defined(_MSC_VER)
    #include <intrin.h>  // Microsoft header for _BitScanForward64() & _BitScanForward()
    #if defined(USE_POPCNT)
        #include <nmmintrin.h>  // Microsoft header for _mm_popcnt_u64()
    #endif
#endif
#if !defined(USE_POPCNT)
    #include <cstring>
#endif

#include "misc.h"
#include "types.h"

namespace DON {

inline constexpr Bitboard FULL_BB = 0xFFFFFFFFFFFFFFFFull;

inline constexpr Bitboard FILE_A_BB = 0x0101010101010101ull;
inline constexpr Bitboard FILE_B_BB = FILE_A_BB << (1 * 1);
inline constexpr Bitboard FILE_C_BB = FILE_A_BB << (2 * 1);
inline constexpr Bitboard FILE_D_BB = FILE_A_BB << (3 * 1);
inline constexpr Bitboard FILE_E_BB = FILE_A_BB << (4 * 1);
inline constexpr Bitboard FILE_F_BB = FILE_A_BB << (5 * 1);
inline constexpr Bitboard FILE_G_BB = FILE_A_BB << (6 * 1);
inline constexpr Bitboard FILE_H_BB = FILE_A_BB << (7 * 1);

inline constexpr Bitboard RANK_1_BB = 0x00000000000000FFull;
inline constexpr Bitboard RANK_2_BB = RANK_1_BB << (1 * 8);
inline constexpr Bitboard RANK_3_BB = RANK_1_BB << (2 * 8);
inline constexpr Bitboard RANK_4_BB = RANK_1_BB << (3 * 8);
inline constexpr Bitboard RANK_5_BB = RANK_1_BB << (4 * 8);
inline constexpr Bitboard RANK_6_BB = RANK_1_BB << (5 * 8);
inline constexpr Bitboard RANK_7_BB = RANK_1_BB << (6 * 8);
inline constexpr Bitboard RANK_8_BB = RANK_1_BB << (7 * 8);

inline constexpr Bitboard EDGE_FILES_BB      = FILE_A_BB | FILE_H_BB;
inline constexpr Bitboard PROMOTION_RANKS_BB = RANK_8_BB | RANK_1_BB;

inline constexpr Bitboard WHITE_BB = 0x55AA55AA55AA55AAull;
inline constexpr Bitboard BLACK_BB = ~WHITE_BB;

#if defined(USE_AVX512)
// clang-format off
inline const __m512i ALL_SQUARES = _mm512_set_epi8(
    63, 62, 61, 60, 59, 58, 57, 56, //
    55, 54, 53, 52, 51, 50, 49, 48, //
    47, 46, 45, 44, 43, 42, 41, 40, //
    39, 38, 37, 36, 35, 34, 33, 32, //
    31, 30, 29, 28, 27, 26, 25, 24, //
    23, 22, 21, 20, 19, 18, 17, 16, //
    15, 14, 13, 12, 11, 10,  9,  8, //
     7,  6,  5,  4,  3,  2,  1,  0);
// clang-format on
#endif

template<Color C>
constexpr Bitboard color_bb() noexcept {
    static_assert(is_ok(C), "Invalid color for color_bb()");
    return C == WHITE ? WHITE_BB : BLACK_BB;
}

constexpr Bitboard square_bb(Square s) noexcept {
    assert(is_ok(s));

    return (1ull << s);
}

// Overloads of bitwise operators between bitboard and square for testing
// whether a given bit is set in bitboard, and for setting and clearing bits.
constexpr Bitboard operator&(Bitboard b, Square s) noexcept { return b & square_bb(s); }
constexpr Bitboard operator|(Bitboard b, Square s) noexcept { return b | square_bb(s); }
constexpr Bitboard operator^(Bitboard b, Square s) noexcept { return b ^ square_bb(s); }
constexpr Bitboard operator&(Square s, Bitboard b) noexcept { return b & s; }
constexpr Bitboard operator|(Square s, Bitboard b) noexcept { return b | s; }
constexpr Bitboard operator^(Square s, Bitboard b) noexcept { return b ^ s; }

constexpr Bitboard& operator&=(Bitboard& b, Square s) noexcept { return b = b & s; }
constexpr Bitboard& operator|=(Bitboard& b, Square s) noexcept { return b = b | s; }
constexpr Bitboard& operator^=(Bitboard& b, Square s) noexcept { return b = b ^ s; }

constexpr Bitboard operator|(Square s1, Square s2) noexcept { return square_bb(s1) | s2; }

// Returns bitboard from list of squares
template<typename... Squares>
constexpr Bitboard make_bb(Squares... squares) noexcept {
    return (square_bb(squares) | ...);
}

// Return a bitboard representing all the squares on the given file
constexpr Bitboard file_bb(File f) noexcept { return FILE_A_BB << (1 * f); }
constexpr Bitboard file_bb(Square s) noexcept { return file_bb(file_of(s)); }

constexpr Bitboard operator&(Bitboard b, File f) noexcept { return b & file_bb(f); }
constexpr Bitboard operator|(Bitboard b, File f) noexcept { return b | file_bb(f); }
constexpr Bitboard operator^(Bitboard b, File f) noexcept { return b ^ file_bb(f); }

// Return a bitboard representing all the squares on the given rank
constexpr Bitboard rank_bb(Rank r) noexcept { return RANK_1_BB << (8 * r); }
constexpr Bitboard rank_bb(Square s) noexcept { return rank_bb(rank_of(s)); }

constexpr Bitboard operator&(Bitboard b, Rank r) noexcept { return b & rank_bb(r); }
constexpr Bitboard operator|(Bitboard b, Rank r) noexcept { return b | rank_bb(r); }
constexpr Bitboard operator^(Bitboard b, Rank r) noexcept { return b ^ rank_bb(r); }

constexpr bool more_than_one(Bitboard b) noexcept { return (b & (b - 1)) != 0; }
constexpr bool exactly_one(Bitboard b) noexcept { return b != 0 && !more_than_one(b); }

template<typename T>
constexpr u8 constexpr_popcount(T v) noexcept {
    static_assert(std::is_integral_v<T>, "constexpr_popcount is undefined for non-integral types");
    static_assert(std::is_unsigned_v<T>, "constexpr_popcount requires an unsigned integral type");

    if constexpr (sizeof(T) <= 8)
    {
        constexpr u64 K1 = 0x5555555555555555ull;
        constexpr u64 K2 = 0x3333333333333333ull;
        constexpr u64 K4 = 0x0F0F0F0F0F0F0F0Full;
        constexpr u64 Kf = 0x0101010101010101ull;

        u64 b = static_cast<std::make_unsigned_t<T>>(v);
        b     = b - ((b >> 1) & K1);
        b     = (b & K2) + ((b >> 2) & K2);
        b     = (b + (b >> 4)) & K4;
        return (b * Kf) >> 56;
    }
    else
    {
        u8 count = 0;

        while (v != 0)
        {
            if ((v & 1) != 0)
                ++count;
            v >>= 1;
        }

        return count;
    }
}

constexpr u8 msb_index(Bitboard b) noexcept {
    constexpr StdArray<u8, SQUARE_NB> MSBIndices{
      0,  47, 1,  56, 48, 27, 2,  60,  //
      57, 49, 41, 37, 28, 16, 3,  61,  //
      54, 58, 35, 52, 50, 42, 21, 44,  //
      38, 32, 29, 23, 17, 11, 4,  62,  //
      46, 55, 26, 59, 40, 36, 15, 53,  //
      34, 51, 20, 43, 31, 22, 10, 45,  //
      25, 39, 14, 33, 19, 30, 9,  24,  //
      13, 18, 8,  12, 7,  6,  5,  63   //
    };

    constexpr u64 Debruijn64 = 0x03F79D71B4CB0A89ull;

    return MSBIndices[(b * Debruijn64) >> 58];
}

// Fills from the MSB down to bit 0.
// e.g. 0001'0010 -> 0001'1111
constexpr Bitboard fill_prefix_bb(Bitboard b) noexcept {
    b |= b >> 1;
    b |= b >> 2;
    b |= b >> 4;
    b |= b >> 8;
    b |= b >> 16;
    b |= b >> 32;
    return b;
}
// Fills from the LSB up to bit 63.
// e.g. 0001'0010 -> 1111'1110
constexpr Bitboard fill_postfix_bb(Bitboard b) noexcept {
    b |= b << 1;
    b |= b << 2;
    b |= b << 4;
    b |= b << 8;
    b |= b << 16;
    b |= b << 32;
    return b;
}

constexpr u8 constexpr_lsb(Bitboard b) noexcept {
    assert(b != 0);

    b ^= b - 1;
    return msb_index(b);
}

constexpr u8 constexpr_msb(Bitboard b) noexcept {
    assert(b != 0);

    b = fill_prefix_bb(b);
    return msb_index(b);
}

#if !defined(USE_POPCNT)

alignas(CACHE_LINE_SIZE) inline const auto POP_CNTS = []() {
    StdArray<u8, 0x10000> popCnts{};

    for (usize i = 0; i < popCnts.size(); ++i)
        popCnts[i] = constexpr_popcount(i);

    return popCnts;
}();

#endif

// Counts the number of non-zero bits in the bitboard
inline u8 popcount(Bitboard b) noexcept {

#if !defined(USE_POPCNT)
    StdArray<u16, 4> b16;
    static_assert(sizeof(b16) == sizeof(b));

    std::memcpy(b16.data(), &b, sizeof(b16));

    return POP_CNTS[b16[0]] + POP_CNTS[b16[1]] + POP_CNTS[b16[2]] + POP_CNTS[b16[3]];
#elif defined(__GNUC__)  // (GCC, Clang, ICX)
    return __builtin_popcountll(b);
#elif defined(_MSC_VER)
    return _mm_popcnt_u64(b);
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation
    return constexpr_popcount(b);
#endif
}

// Returns the least significant bit in the non-zero bitboard
inline Square lsq(Bitboard b) noexcept {
    assert(b != 0);

#if defined(__GNUC__)  // (GCC, Clang, ICX)
    return Square(__builtin_ctzll(b));
#elif defined(_MSC_VER)
    unsigned long idx;
    #if defined(_WIN64)  // (WIN64)
    _BitScanForward64(&idx, b);

    return Square(idx);
    #else                // (WIN32)
    if (auto bb = u32(b); bb != 0)
    {
        _BitScanForward(&idx, bb);
        return Square(idx);
    }

    _BitScanForward(&idx, u32(b >> 32));
    return Square(idx + 32);
    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation
    return Square(constexpr_lsb(b));
#endif
}

// Returns the most significant bit in the non-zero bitboard
inline Square msq(Bitboard b) noexcept {
    assert(b != 0);

#if defined(__GNUC__)  // (GCC, Clang, ICX)
    return Square(__builtin_clzll(b) ^ 63);
#elif defined(_MSC_VER)
    unsigned long idx;
    #if defined(_WIN64)  // (WIN64)
    _BitScanReverse64(&idx, b);

    return Square(idx);
    #else                // (WIN32)
    if (auto bb = u32(b >> 32); bb != 0)
    {
        _BitScanReverse(&idx, bb);
        return Square(idx + 32);
    }

    _BitScanReverse(&idx, u32(b));
    return Square(idx);
    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation
    return Square(constexpr_msb(b));
#endif
}

// Returns and clears the least significant bit in the non-zero bitboard
inline Square pop_lsq(Bitboard& b) noexcept {
    assert(b != 0);

    Square s = lsq(b);

    b &= b - 1;

    return s;
}

// Returns and clears the most significant bit in the non-zero bitboard
inline Square pop_msq(Bitboard& b) noexcept {
    assert(b != 0);

    Square s = msq(b);

    b ^= s;

    return s;
}

std::string pretty_str(Bitboard b) noexcept;

std::string_view pretty(Bitboard b) noexcept;

}  // namespace DON

#endif  // #ifndef BITBOARD_H_INCLUDED
