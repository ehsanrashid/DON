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
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

#if !defined(USE_POPCNT)
    #include <cstring>
#endif

#if defined(_MSC_VER)
    #include <intrin.h>  // Microsoft header for _BitScanForward64() & _BitScanForward()
    #if defined(USE_POPCNT)
        #include <nmmintrin.h>  // Microsoft header for _mm_popcnt_u64()
    #endif
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

#include "misc.h"
#include "types.h"

namespace DON {

#if defined(USE_BMI2)
    #if defined(USE_COMPRESSED)
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

// Magic holds all magic bitboards relevant data for a single square
struct Magic final {
   public:
    Magic() noexcept                        = default;
    Magic(const Magic&) noexcept            = delete;
    Magic(Magic&&) noexcept                 = delete;
    Magic& operator=(const Magic&) noexcept = delete;
    Magic& operator=(Magic&&) noexcept      = delete;

#if defined(USE_BMI2)
    void attacks_bb(Bitboard occupancyBB, Bitboard referenceBB) noexcept {
    #if defined(USE_COMPRESSED)
        attacksBBs[index(occupancyBB)] = _pext_u64(referenceBB, reMaskBB);
    #else
        attacksBBs[index(occupancyBB)] = referenceBB;
    #endif
    }
#endif

    Bitboard attacks_bb(Bitboard occupancyBB) const noexcept {
#if defined(USE_BMI2)
    #if defined(USE_COMPRESSED)
        return _pdep_u64(attacksBBs[index(occupancyBB)], reMaskBB);
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
        std::uint32_t lo  = std::uint32_t(occupancyBB >> 00) & std::uint32_t(maskBB >> 00);
        std::uint32_t hi  = std::uint32_t(occupancyBB >> 32) & std::uint32_t(maskBB >> 32);
        std::uint32_t mlo = std::uint32_t(magicBB >> 00);
        std::uint32_t mhi = std::uint32_t(magicBB >> 32);
        return ((lo * mlo) ^ (hi * mhi)) >> shift;
    #endif
#endif
    }

#if defined(USE_BMI2)
    #if defined(USE_COMPRESSED)
    Bitboard16* attacksBBs;
    Bitboard    maskBB;
    Bitboard    reMaskBB;
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
};

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

// Return the distance between s1 and s2, defined as the number of steps for a king in s1 to reach s2.
template<typename T = Square>
constexpr std::uint8_t distance(Square, Square) noexcept {
    static_assert(sizeof(T) == 0, "Unsupported distance type");
    return 0;
}

constexpr std::uint8_t constexpr_abs(int x) noexcept { return x < 0 ? -x : x; }

template<>
constexpr std::uint8_t distance<File>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return constexpr_abs(int(file_of(s1)) - int(file_of(s2)));
}

template<>
constexpr std::uint8_t distance<Rank>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return constexpr_abs(int(rank_of(s1)) - int(rank_of(s2)));
}

alignas(CACHE_LINE_SIZE) inline constexpr auto DISTANCES = []() constexpr noexcept {
    StdArray<std::uint8_t, SQUARE_NB, SQUARE_NB> distances{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            distances[s1][s2] = std::max(distance<File>(s1, s2), distance<Rank>(s1, s2));

    return distances;
}();

template<>
constexpr std::uint8_t distance<Square>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return DISTANCES[s1][s2];
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

    return c == WHITE ? pawn_push_bb<WHITE>(pawns) : pawn_push_bb<BLACK>(pawns);
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

    return c == WHITE ? pawn_attacks_bb<WHITE>(pawns) : pawn_attacks_bb<BLACK>(pawns);
}

// Returns the bitboard of target square from the given square for the given step.
// If the step is off the board, returns empty bitboard.
constexpr Bitboard destination_bb(Square s, Direction d, std::uint8_t dist = 1) noexcept {
    assert(is_ok(s));

    Square sq = s + d;
    return is_ok(sq) && distance(s, sq) <= dist ? square_bb(sq) : 0;
}

// Computes sliding attack
template<PieceType PT>
constexpr Bitboard sliding_attacks_bb(Square s, Bitboard occupancyBB = 0) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in sliding_attacks_bb()");
    assert(is_ok(s));

    constexpr StdArray<Direction, 2, 4> Directions{{
      {SOUTH_WEST, SOUTH_EAST, NORTH_WEST, NORTH_EAST},  //
      {SOUTH, WEST, EAST, NORTH}                         //
    }};

    Bitboard attacksBB = 0;

    for (Direction d : Directions[PT - BISHOP])
    {
        Square sq = s;

        while (true)
        {
            Square nextSq = sq + d;

            // Stop if next square is off-board or not adjacent (wrap-around)
            if (!is_ok(nextSq) || distance(sq, nextSq) > 1)
                break;

            // Move to next square
            sq = nextSq;

            attacksBB |= sq;

            // Stop if occupied - sliding blocked
            if ((occupancyBB & sq) != 0)
                break;
        }
    }

    return attacksBB;
}

constexpr Bitboard knight_attacks_bb(Square s) noexcept {
    assert(is_ok(s));

    Bitboard attacksBB = 0;

    for (auto dir : {SOUTH_2 + WEST, SOUTH_2 + EAST, WEST_2 + SOUTH, EAST_2 + SOUTH, WEST_2 + NORTH,
                     EAST_2 + NORTH, NORTH_2 + WEST, NORTH_2 + EAST})
        attacksBB |= destination_bb(s, dir, 2);

    return attacksBB;
}

constexpr Bitboard king_attacks_bb(Square s) noexcept {
    assert(is_ok(s));

    Bitboard attacksBB = 0;

    for (auto dir : {SOUTH_WEST, SOUTH, SOUTH_EAST, WEST, EAST, NORTH_WEST, NORTH, NORTH_EAST})
        attacksBB |= destination_bb(s, dir);

    return attacksBB;
}

alignas(CACHE_LINE_SIZE) inline constexpr auto ATTACKS_BBs = []() constexpr noexcept {
    StdArray<Bitboard, SQUARE_NB, 1 + PIECE_TYPE_CNT> attacksBBs{};

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        attacksBBs[s][WHITE]  = pawn_attacks_bb<WHITE>(square_bb(s));
        attacksBBs[s][BLACK]  = pawn_attacks_bb<BLACK>(square_bb(s));
        attacksBBs[s][KNIGHT] = knight_attacks_bb(s);
        attacksBBs[s][BISHOP] = sliding_attacks_bb<BISHOP>(s, 0);
        attacksBBs[s][ROOK]   = sliding_attacks_bb<ROOK>(s, 0);
        attacksBBs[s][QUEEN]  = attacksBBs[s][BISHOP] | attacksBBs[s][ROOK];
        attacksBBs[s][KING]   = king_attacks_bb(s);
    }

    return attacksBBs;
}();

constexpr Bitboard attacks_bb(Square s, std::size_t idx) noexcept {
    assert(is_ok(s));

    return ATTACKS_BBs[s][idx];
}

// Returns the pseudo attacks of the given piece type assuming an empty board
template<PieceType PT>
constexpr Bitboard attacks_bb(Square s, [[maybe_unused]] Color c = NONE) noexcept {
    static_assert(is_ok(PT), "Unsupported piece type in attacks_bb()");
    assert(is_ok(s) && (PT != PAWN || is_ok(c)));

    if constexpr (PT == PAWN)
        return attacks_bb(s, c);

    return attacks_bb(s, PT);
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

alignas(CACHE_LINE_SIZE) inline StdArray<Magic, SQUARE_NB, 2> MAGICS;  // BISHOP or ROOK

template<PieceType PT>
constexpr Bitboard attacks_bb(const StdArray<Magic, 2>& magic, Bitboard occupancyBB) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in attacks_bb()");

    return magic[PT - BISHOP].attacks_bb(occupancyBB);
}

// Returns the attacks by the given piece type.
// Sliding piece attacks do not continue passed an occupied square.
template<PieceType PT>
constexpr Bitboard attacks_bb(Square s, [[maybe_unused]] Bitboard occupancyBB) noexcept {
    static_assert(PT != PAWN, "Unsupported piece type in attacks_bb()");
    assert(is_ok(s));

    if constexpr (PT == KNIGHT)
        return attacks_bb<KNIGHT>(s);
    if constexpr (PT == BISHOP)
        return attacks_bb<BISHOP>(MAGICS[s], occupancyBB);
    if constexpr (PT == ROOK)
        return attacks_bb<ROOK>(MAGICS[s], occupancyBB);
    if constexpr (PT == QUEEN)
        return attacks_bb<BISHOP>(s, occupancyBB) | attacks_bb<ROOK>(s, occupancyBB);
    if constexpr (PT == KING)
        return attacks_bb<KING>(s);
    assert(false);
    return 0;
}

// Returns the attacks by the given piece type.
// Sliding piece attacks do not continue passed an occupied square.
constexpr Bitboard attacks_bb(Square s, PieceType pt, Bitboard occupancyBB) noexcept {
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

alignas(CACHE_LINE_SIZE) inline constexpr auto LINE_BBs = []() constexpr noexcept {
    StdArray<Bitboard, SQUARE_NB, SQUARE_NB> lineBBs{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            for (PieceType pt : {BISHOP, ROOK})
                if ((attacks_bb(s1, pt) & s2) != 0)
                    lineBBs[s1][s2] = (attacks_bb(s1, pt) & attacks_bb(s2, pt)) | s1 | s2;

    return lineBBs;
}();

// Returns a bitboard representing an entire line (from board edge to board edge)
// passing through the squares s1 and s2.
// If the given squares are not on a same file/rank/diagonal, it returns 0.
// For instance, line_bb(SQ_C4, SQ_F7) will return a bitboard with the A2-G8 diagonal.
constexpr Bitboard line_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return LINE_BBs[s1][s2];
}

// Returns true if the squares s1, s2 and s3 are aligned on straight or diagonal line.
constexpr bool aligned(Square s1, Square s2, Square s3) noexcept {
    assert(is_ok(s3));

    return (line_bb(s1, s2) & s3) != 0;
}

alignas(CACHE_LINE_SIZE) inline StdArray<Bitboard, SQUARE_NB, SQUARE_NB> BETWEEN_BBs;

// Returns a bitboard representing the squares in the semi-open segment
// between the squares s1 and s2 (excluding s1 but including s2).
// If the given squares are not on a same file/rank/diagonal, it returns s2.
// For instance, between_bb(SQ_C4, SQ_F7) will return a bitboard with squares D5, E6 and F7,
// but between_bb(SQ_E6, SQ_F8) will return a bitboard with the square F8.
// This trick allows to generate non-king evasion moves faster:
// the defending piece must either interpose itself to cover the check or capture the checking piece.
constexpr Bitboard between_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return BETWEEN_BBs[s1][s2];
}

// Returns a bitboard between the squares s1 and s2 (excluding s1 and s2).
constexpr Bitboard between_ex_bb(Square s1, Square s2) noexcept { return between_bb(s1, s2) ^ s2; }

alignas(CACHE_LINE_SIZE) inline StdArray<Bitboard, SQUARE_NB, SQUARE_NB> PASS_RAY_BBs;

// Returns a bitboard representing a ray from the square s1 passing s2.
constexpr Bitboard pass_ray_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return PASS_RAY_BBs[s1][s2];
}

constexpr std::uint8_t constexpr_popcount(Bitboard b) noexcept {

    // std::uint8_t count = 0;
    // while (b != 0)
    // {
    //     count += b & 1;
    //     b >>= 1;
    // }
    // return count;

    // asm ("popcnt %0, %0" : "+r" (b) :: "cc");
    // return b;

    constexpr Bitboard K1 = 0x5555555555555555ULL;
    constexpr Bitboard K2 = 0x3333333333333333ULL;
    constexpr Bitboard K4 = 0x0F0F0F0F0F0F0F0FULL;
    constexpr Bitboard Kf = 0x0101010101010101ULL;

    b = b - ((b >> 1) & K1);
    b = (b & K2) + ((b >> 2) & K2);
    b = (b + (b >> 4)) & K4;
    return (b * Kf) >> 56;
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

    constexpr Bitboard Debruijn64 = 0x03F79D71B4CB0A89ULL;

    return MSBIndices[(b * Debruijn64) >> 58];
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
    assert(b != 0);

    // std::uint8_t idx = 0;
    // while (!(b & 1))
    // {
    //     ++idx;
    //     b >>= 1;
    // }
    // return Square(idx);

    // asm ("bsfq %0, %0" : "+r" (b) :: "cc");
    // return Square(b);

    b ^= b - 1;
    return msb_index(b);
}

constexpr std::uint8_t constexpr_msb(Bitboard b) noexcept {
    assert(b != 0);

    // std::uint8_t idx = 0;
    // while (b >>= 1)
    //     ++idx;
    // return Square(idx);

    // asm ("bsrq %0, %0" : "+r" (b) :: "cc");
    // return Square(b);

    b = fill_prefix_bb(b);
    return msb_index(b);
}

#if !defined(USE_POPCNT)

constexpr std::uint8_t constexpr_popcount16(std::uint16_t x) noexcept {
    constexpr std::uint16_t K1 = 0x5555U;
    constexpr std::uint16_t K2 = 0x3333U;
    constexpr std::uint16_t K4 = 0x0F0FU;
    constexpr std::uint16_t Kf = 0x0101U;

    x = x - ((x >> 1) & K1);
    x = (x & K2) + ((x >> 2) & K2);
    x = (x + (x >> 4)) & K4;
    return (x * Kf) >> 8;
}

alignas(CACHE_LINE_SIZE) inline const auto POP_CNTS = []() {
    StdArray<std::uint8_t, 0x10000> popCnts{};

    for (std::size_t i = 0; i < popCnts.size(); ++i)
        popCnts[i] = constexpr_popcount16(i);

    return popCnts;
}();

#endif

// Counts the number of non-zero bits in the bitboard
inline std::uint8_t popcount(Bitboard b) noexcept {

#if !defined(USE_POPCNT)
    StdArray<std::uint16_t, 4> b16;
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

// Returns the bitboard of the least significant square of the non-zero bitboard.
// It is equivalent to square_bb(lsb(bb)).
constexpr Bitboard lsq_bb(Bitboard b) noexcept {
    assert(b != 0);

    return b & -b;
}

}  // namespace DON

#endif  // #ifndef BITBOARD_H_INCLUDED
