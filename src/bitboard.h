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

#ifndef BITBOARD_H_INCLUDED
#define BITBOARD_H_INCLUDED

#include <array>
#include <cassert>
#include <cmath>  // IWYU pragma: keep
#include <cstdint>
#include <cstdlib>
#if !defined(USE_POPCNT)
    #include <cstring>
#endif
#if !defined(NDEBUG)
    #include <string>
    #include <string_view>
#endif

#include "misc.h"
#include "types.h"

#if defined(_MSC_VER)
    #include <intrin.h>  // Microsoft header for _BitScanForward64() & _BitScanForward()
    #if defined(USE_POPCNT)
        #include <nmmintrin.h>  // Microsoft header for _mm_popcnt_u64()
    #endif
#endif

#if defined(USE_BMI2)
    #include <immintrin.h>  // Header for _pext_u64() & _pdep_u64() intrinsic

    #define USE_COMPRESS_BB

    // * _pext_u64(src, mask) - Parallel Bits Extract
    // Extracts the bits from the 64-bit 'src' corresponding to the 1-bits in 'mask',
    // and packs them contiguously into the lower bits of the result.

    // * _pdep_u64(src, mask) - Parallel Bits Deposit
    // Deposits the lower bits of 'src' into the positions of the 1-bits in 'mask',
    // leaving all other bits as zero.
#endif

namespace DON {

#if defined(USE_BMI2)
    #if defined(USE_COMPRESS_BB)
using Bitboard16 = std::uint16_t;
    #endif
#endif

namespace BitBoard {

void init() noexcept;

std::string pretty_str(Bitboard b) noexcept;

std::string_view pretty(Bitboard b) noexcept;

}  // namespace BitBoard

inline constexpr Bitboard FULL_BB = 0xFFFFFFFFFFFFFFFFULL;

inline constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
inline constexpr Bitboard FILE_B_BB = FILE_A_BB << (1 * 1);
inline constexpr Bitboard FILE_C_BB = FILE_A_BB << (2 * 1);
inline constexpr Bitboard FILE_D_BB = FILE_A_BB << (3 * 1);
inline constexpr Bitboard FILE_E_BB = FILE_A_BB << (4 * 1);
inline constexpr Bitboard FILE_F_BB = FILE_A_BB << (5 * 1);
inline constexpr Bitboard FILE_G_BB = FILE_A_BB << (6 * 1);
inline constexpr Bitboard FILE_H_BB = FILE_A_BB << (7 * 1);

inline constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;
inline constexpr Bitboard RANK_2_BB = RANK_1_BB << (1 * 8);
inline constexpr Bitboard RANK_3_BB = RANK_1_BB << (2 * 8);
inline constexpr Bitboard RANK_4_BB = RANK_1_BB << (3 * 8);
inline constexpr Bitboard RANK_5_BB = RANK_1_BB << (4 * 8);
inline constexpr Bitboard RANK_6_BB = RANK_1_BB << (5 * 8);
inline constexpr Bitboard RANK_7_BB = RANK_1_BB << (6 * 8);
inline constexpr Bitboard RANK_8_BB = RANK_1_BB << (7 * 8);

inline constexpr Bitboard EDGE_FILES_BB      = FILE_A_BB | FILE_H_BB;
inline constexpr Bitboard PROMOTION_RANKS_BB = RANK_8_BB | RANK_1_BB;

template<Color C>
constexpr Bitboard color_bb() noexcept {
    static_assert(is_ok(C), "Invalid color for color_bb()");
    constexpr Bitboard WhiteBB = 0x55AA55AA55AA55AAULL;
    constexpr Bitboard BlackBB = ~WhiteBB;
    return C == WHITE ? WhiteBB : BlackBB;
}

constexpr std::uint8_t msb_index(Bitboard b) noexcept {
    constexpr StdArray<std::uint8_t, SQUARE_NB> MSBIndices{
      0,  47, 1,  56, 48, 27, 2,  60,  //
      57, 49, 41, 37, 28, 16, 3,  61,  //
      54, 58, 35, 52, 50, 42, 21, 44,  //
      38, 32, 29, 23, 17, 11, 4,  62,  //
      46, 55, 26, 59, 40, 36, 15, 53,  //
      34, 51, 20, 43, 31, 22, 10, 45,  //
      25, 39, 14, 33, 19, 30, 9,  24,  //
      13, 18, 8,  12, 7,  6,  5,  63   //
    };

    constexpr std::uint64_t Debruijn64 = 0x03F79D71B4CB0A89ULL;

    return MSBIndices[(b * Debruijn64) >> 58];
}

// Magic holds all magic bitboards relevant data for a single square
struct Magic final {
   public:
    Magic() noexcept                        = default;
    Magic(const Magic&) noexcept            = delete;
    Magic(Magic&&) noexcept                 = delete;
    Magic& operator=(const Magic&) noexcept = delete;
    Magic& operator=(Magic&&) noexcept      = delete;

#if defined(USE_BMI2)
    #if defined(USE_COMPRESS_BB)
    Bitboard16* attacksBBs;
    Bitboard    maskBB;
    Bitboard    exmaskBB;
    #else
    Bitboard* attacksBBs;
    Bitboard  maskBB;
    #endif
#else
    Bitboard*    attacksBBs;
    Bitboard     maskBB;
    Bitboard     magicBB;
    std::uint8_t shift;
#endif


#if defined(USE_BMI2)
    void attacks_bb(Bitboard occupancyBB, Bitboard referenceBB) noexcept {
        attacksBBs[index(occupancyBB)] =
    #if defined(USE_COMPRESS_BB)
          _pext_u64(referenceBB, exmaskBB)
    #else
          referenceBB
    #endif
          ;
    }
#endif

    Bitboard attacks_bb(Bitboard occupancyBB) const noexcept {
#if defined(USE_BMI2)
    #if defined(USE_COMPRESS_BB)
        return _pdep_u64(attacksBBs[index(occupancyBB)], exmaskBB);
    #else
        return attacksBBs[index(occupancyBB)];
    #endif
#else
        return attacksBBs[index(occupancyBB)];
#endif
    }

    // Compute the attack's index using the 'magic bitboards' approach
    std::uint16_t index(Bitboard occupancyBB) const noexcept {
#if defined(USE_BMI2)
        return _pext_u64(occupancyBB, maskBB);
#else
    #if defined(IS_64BIT)
        return ((occupancyBB & maskBB) * magicBB) >> shift;
    #else
        std::uint32_t lo = std::uint32_t(occupancyBB >> 00) & std::uint32_t(maskBB >> 00);
        std::uint32_t hi = std::uint32_t(occupancyBB >> 32) & std::uint32_t(maskBB >> 32);
        return (lo * std::uint32_t(magicBB >> 00) ^ hi * std::uint32_t(magicBB >> 32)) >> shift;
    #endif
#endif
    }
};

#if !defined(USE_POPCNT)
alignas(CACHE_LINE_SIZE) inline StdArray<std::uint8_t, 1 << 16> PopCnt{};
#endif

// clang-format off
alignas(CACHE_LINE_SIZE) inline StdArray<std::uint8_t, SQUARE_NB, SQUARE_NB> Distances{};

alignas(CACHE_LINE_SIZE) inline StdArray<Bitboard, SQUARE_NB, SQUARE_NB>     LineBBs{};
alignas(CACHE_LINE_SIZE) inline StdArray<Bitboard, SQUARE_NB, SQUARE_NB>     BetweenBBs{};
alignas(CACHE_LINE_SIZE) inline StdArray<Bitboard, SQUARE_NB, SQUARE_NB>     PassRayBBs{};
alignas(CACHE_LINE_SIZE) inline StdArray<Bitboard, SQUARE_NB, PIECE_TYPE_NB> AttacksBBs{};
alignas(CACHE_LINE_SIZE) inline StdArray<Magic   , SQUARE_NB, 2>             Magics{};  // BISHOP or ROOK

alignas(CACHE_LINE_SIZE) inline StdArray<bool, SQUARE_NB, SQUARE_NB, SQUARE_NB> Aligneds{};
// clang-format on

constexpr Bitboard square_bb(Square s) noexcept {
    assert(is_ok(s));
    return (1ULL << s);
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

// Returns a bitboard from a list of squares
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

// Returns a bitboard representing an entire line (from board edge to board edge)
// passing through the squares s1 and s2.
// If the given squares are not on a same file/rank/diagonal, it returns 0.
// For instance, line_bb(SQ_C4, SQ_F7) will return a bitboard with the A2-G8 diagonal.
inline Bitboard line_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return LineBBs[s1][s2];
}

// Returns a bitboard representing the squares in the semi-open segment
// between the squares s1 and s2 (excluding s1 but including s2).
// If the given squares are not on a same file/rank/diagonal, it returns s2.
// For instance, between_bb(SQ_C4, SQ_F7) will return a bitboard with squares D5, E6 and F7,
// but between_bb(SQ_E6, SQ_F8) will return a bitboard with the square F8.
// This trick allows to generate non-king evasion moves faster:
// the defending piece must either interpose itself to cover the check or capture the checking piece.
inline Bitboard between_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return BetweenBBs[s1][s2];
}
// Returns a bitboard between the squares s1 and s2 (excluding s1 and s2).
inline Bitboard between_ex_bb(Square s1, Square s2) noexcept { return between_bb(s1, s2) ^ s2; }

// Returns a bitboard representing a ray from the square s1 passing s2.
inline Bitboard pass_ray_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return PassRayBBs[s1][s2];
}

// Returns true if the squares s1, s2 and s3 are aligned on straight or diagonal line.
inline bool aligned(Square s1, Square s2, Square s3) noexcept { return Aligneds[s1][s2][s3]; }

// Return the distance between s1 and s2, defined as the number of steps for a king in s1 to reach s2.
template<typename T = Square>
inline std::uint8_t distance(Square s1, Square s2) noexcept;

template<>
inline std::uint8_t distance<File>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return std::abs(file_of(s1) - file_of(s2));
}

template<>
inline std::uint8_t distance<Rank>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return std::abs(rank_of(s1) - rank_of(s2));
}

template<>
inline std::uint8_t distance<Square>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return Distances[s1][s2];
}

// Returns the bitboard of target square from the given square for the given step.
// If the step is off the board, returns empty bitboard.
inline Bitboard destination_bb(Square s, Direction d, std::uint8_t dist = 1) noexcept {
    assert(is_ok(s));
    Square sq = s + d;
    return is_ok(sq) && distance(s, sq) <= dist ? square_bb(sq) : 0;
}

// Shifts a bitboard as specified by the direction
template<Direction D>
constexpr Bitboard shift_bb(Bitboard b) noexcept {
    if constexpr (D == NORTH)
        return b << NORTH;
    if constexpr (D == SOUTH)
        return b >> NORTH;
    if constexpr (D == NORTH_2)
        return b << NORTH_2;
    if constexpr (D == SOUTH_2)
        return b >> NORTH_2;
    if constexpr (D == EAST)
        return (b & ~FILE_H_BB) << EAST;
    if constexpr (D == WEST)
        return (b & ~FILE_A_BB) >> EAST;
    if constexpr (D == NORTH_WEST)
        return (b & ~FILE_A_BB) << NORTH_WEST;
    if constexpr (D == SOUTH_EAST)
        return (b & ~FILE_H_BB) >> NORTH_WEST;
    if constexpr (D == NORTH_EAST)
        return (b & ~FILE_H_BB) << NORTH_EAST;
    if constexpr (D == SOUTH_WEST)
        return (b & ~FILE_A_BB) >> NORTH_EAST;
    assert(false);
    return 0;
}

template<Color C>
constexpr Bitboard pawn_push_bb(Bitboard pawns) noexcept {
    static_assert(is_ok(C), "Invalid color for pawn_push_bb()");
    return shift_bb<pawn_spush(C)>(pawns);
}
constexpr Bitboard pawn_push_bb(Bitboard pawns, Color c) noexcept {
    assert(is_ok(c));
    return c == WHITE  //
           ? pawn_push_bb<WHITE>(pawns)
           : pawn_push_bb<BLACK>(pawns);
}

// Returns the squares attacked by pawns of the given color from the given bitboard
template<Color C>
constexpr Bitboard pawn_attacks_bb(Bitboard pawns) noexcept {
    static_assert(is_ok(C), "Invalid color for pawn_attacks_bb()");
    return shift_bb<(C == WHITE ? NORTH_WEST : SOUTH_WEST)>(pawns)
         | shift_bb<(C == WHITE ? NORTH_EAST : SOUTH_EAST)>(pawns);
}
constexpr Bitboard pawn_attacks_bb(Bitboard pawns, Color c) noexcept {
    assert(is_ok(c));
    return c == WHITE  //
           ? pawn_attacks_bb<WHITE>(pawns)
           : pawn_attacks_bb<BLACK>(pawns);
}

// Returns the pseudo attacks of the given piece type assuming an empty board
template<PieceType PT>
constexpr Bitboard attacks_bb(Square s, [[maybe_unused]] Color c = COLOR_NB) noexcept {
    static_assert(is_ok(PT), "Unsupported piece type in attacks_bb()");
    assert(is_ok(s) && (PT != PAWN || is_ok(c)));
    if constexpr (PT == PAWN)
        return AttacksBBs[s][c];
    return AttacksBBs[s][PT];
}

constexpr Bitboard attacks_bb(Square s, Piece pc) noexcept {
    assert(is_ok(s));
    switch (type_of(pc))
    {
    case PAWN :
        return attacks_bb<PAWN>(s, color_of(pc));
    case KNIGHT :
        return attacks_bb<KNIGHT>(s);
    case BISHOP :
        return attacks_bb<BISHOP>(s);
    case ROOK :
        return attacks_bb<ROOK>(s);
    case QUEEN :
        return attacks_bb<QUEEN>(s);
    case KING :
        return attacks_bb<KING>(s);
    default :
        assert(false);
        return 0;
    }
}

template<PieceType PT>
constexpr Bitboard attacks_bb(const StdArray<Magic, 2>& magic, Bitboard occupancyBB) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in attacks_bb()");
    return magic[PT - BISHOP].attacks_bb(occupancyBB);
}

// Returns the attacks by the given piece type.
// Sliding piece attacks do not continue passed an occupied square.
template<PieceType PT>
constexpr Bitboard attacks_bb(Square s, Bitboard occupancyBB) noexcept {
    static_assert(PT != PAWN, "Unsupported piece type in attacks_bb()");
    assert(is_ok(s));
    if constexpr (PT == KNIGHT)
        return attacks_bb<KNIGHT>(s);
    if constexpr (PT == BISHOP)
        return attacks_bb<BISHOP>(Magics[s], occupancyBB);
    if constexpr (PT == ROOK)
        return attacks_bb<ROOK>(Magics[s], occupancyBB);
    if constexpr (PT == QUEEN)
        return attacks_bb<BISHOP>(s, occupancyBB) | attacks_bb<ROOK>(s, occupancyBB);
    if constexpr (PT == KING)
        return attacks_bb<KING>(s);
    assert(false);
    return 0;
}

// Returns the attacks by the given piece type.
// Sliding piece attacks do not continue passed an occupied square.
constexpr Bitboard attacks_bb(Square s, PieceType pt, Bitboard occupancyBB = 0) noexcept {
    assert(pt != PAWN);
    assert(is_ok(s));
    switch (pt)
    {
    case KNIGHT :
        return attacks_bb<KNIGHT>(s);
    case BISHOP :
        return attacks_bb<BISHOP>(s, occupancyBB);
    case ROOK :
        return attacks_bb<ROOK>(s, occupancyBB);
    case QUEEN :
        return attacks_bb<QUEEN>(s, occupancyBB);
    case KING :
        return attacks_bb<KING>(s);
    default :
        assert(false);
        return 0;
    }
}

constexpr Bitboard attacks_bb(Square s, Piece pc, Bitboard occupancyBB) noexcept {
    assert(is_ok(s));
    if (type_of(pc) == PAWN)
        return attacks_bb<PAWN>(s, color_of(pc));
    return attacks_bb(s, type_of(pc), occupancyBB);
}

// Counts the number of non-zero bits in the bitboard
inline std::uint8_t popcount(Bitboard b) noexcept {

#if !defined(USE_POPCNT)
    StdArray<std::uint16_t, 4> b16;
    static_assert(sizeof(b16) == sizeof(b));
    std::memcpy(b16.data(), &b, sizeof(b16));
    return PopCnt[b16[0]] + PopCnt[b16[1]] + PopCnt[b16[2]] + PopCnt[b16[3]];
#elif defined(__GNUC__)  // (GCC, Clang, ICX)
    return __builtin_popcountll(b);
#elif defined(_MSC_VER)
    return _mm_popcnt_u64(b);
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation

    // std::uint8_t count = 0;
    // while (b != 0)
    // {
    //     count += b & 1;
    //     b >>= 1;
    // }
    // return count;

    // asm ("popcnt %0, %0" : "+r" (b) :: "cc");
    // return b;

    b -= ((b >> 1) & 0x5555555555555555ULL);
    b = ((b >> 2) & 0x3333333333333333ULL) + (b & 0x3333333333333333ULL);
    b = ((b >> 4) + b) & 0x0F0F0F0F0F0F0F0FULL;
    return (b * 0x0101010101010101ULL) >> 56;
#endif
}

// Fills from the MSB down to bit 0.
// e.g. 0001'0010 -> 0001'1111
constexpr Bitboard fill_prefix_bb(Bitboard b) noexcept {
    return b |= b >> 1, b |= b >> 2, b |= b >> 4, b |= b >> 8, b |= b >> 16, b |= b >> 32;
}
// Fills from the LSB up to bit 63.
// e.g. 0001'0010 -> 1111'1110
constexpr Bitboard fill_postfix_bb(Bitboard b) noexcept {
    return b |= b << 1, b |= b << 2, b |= b << 4, b |= b << 8, b |= b << 16, b |= b << 32;
}

constexpr std::uint8_t constexpr_lsb(Bitboard b) noexcept {
    assert(b);
    b ^= b - 1;
    return msb_index(b);
}
constexpr std::uint8_t constexpr_msb(Bitboard b) noexcept {
    assert(b);
    b = fill_prefix_bb(b);
    return msb_index(b);
}

// Returns the least significant bit in the non-zero bitboard
inline Square lsq(Bitboard b) noexcept {
    assert(b);

#if defined(__GNUC__)  // (GCC, Clang, ICX)
    return Square(__builtin_ctzll(b));
#elif defined(_MSC_VER)
    unsigned long idx;
    #if defined(_WIN64)  // (MSVC-> WIN64)
    _BitScanForward64(&idx, b);
    return Square(idx);
    #else                // (MSVC-> WIN32)
    if (auto bb = std::uint32_t(b); bb != 0)
    {
        _BitScanForward(&idx, bb);
        return Square(idx);
    }
    else
    {
        _BitScanForward(&idx, std::uint32_t(b >> 32));
        return Square(idx + 32);
    }
    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation

    // std::uint8_t idx = 0;
    // while (!(b & 1))
    // {
    //     ++idx;
    //     b >>= 1;
    // }
    // return Square(idx);

    // asm ("bsfq %0, %0" : "+r" (b) :: "cc");
    // return Square(b);

    return Square(constexpr_lsb(b));
#endif
}

// Returns the most significant bit in the non-zero bitboard
inline Square msq(Bitboard b) noexcept {
    assert(b);

#if defined(__GNUC__)  // (GCC, Clang, ICX)
    return Square(__builtin_clzll(b) ^ 63);
#elif defined(_MSC_VER)
    unsigned long idx;
    #if defined(_WIN64)  // (MSVC-> WIN64)
    _BitScanReverse64(&idx, b);
    return Square(idx);
    #else                // (MSVC-> WIN32)
    if (auto bb = std::uint32_t(b >> 32); bb != 0)
    {
        _BitScanReverse(&idx, bb);
        return Square(idx + 32);
    }
    else
    {
        _BitScanReverse(&idx, std::uint32_t(b));
        return Square(idx);
    }
    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation

    // std::uint8_t idx = 0;
    // while (b >>= 1)
    //     ++idx;
    // return Square(idx);

    // asm ("bsrq %0, %0" : "+r" (b) :: "cc");
    // return Square(b);

    return Square(constexpr_msb(b));
#endif
}

// Returns and clears the least significant bit in the non-zero bitboard
inline Square pop_lsq(Bitboard& b) noexcept {
    assert(b);
    Square s = lsq(b);
    b &= b - 1;
    return s;
}

// Returns and clears the most significant bit in the non-zero bitboard
inline Square pop_msq(Bitboard& b) noexcept {
    assert(b);
    Square s = msq(b);
    b ^= s;
    return s;
}

// Returns the bitboard of the least significant square of the non-zero bitboard.
// It is equivalent to square_bb(lsb(bb)).
constexpr Bitboard lsq_bb(Bitboard b) noexcept {
    assert(b);
    return b & -b;
}

}  // namespace DON

#endif  // #ifndef BITBOARD_H_INCLUDED
