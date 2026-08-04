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

#ifndef ATTACKS_H_INCLUDED
#define ATTACKS_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <initializer_list>

#if defined(USE_BMI2)
    #include <immintrin.h>  // Header for _pext_u64() & _pdep_u64() intrinsic
    // * _pext_u64(src, mask) - Parallel Bits Extract
    // Extracts the bits from the 64-bit 'src' corresponding to the 1-bits in 'mask',
    // and packs them contiguously into the lower bits.
    // * _pdep_u64(src, mask) - Parallel Bits Deposit
    // Deposits the lower bits of 'src' into the positions of the 1-bits in 'mask',
    // leaving all other bits as zero.
#endif

#if defined(__aarch64__)
    #include <arm_acle.h>
    #define USE_HYPERBOLA_QUINT
#elif defined(__loongarch__) && (__loongarch_grlen == 64)
    #define USE_HYPERBOLA_QUINT
#elif defined(USE_AVX2)
    #include <immintrin.h>
    #define USE_DUAL_HYPERBOLA_QUINT
#endif

#include "bitboard.h"
#include "misc.h"
#include "types.h"

namespace DON {

#if defined(USE_BMI2) && defined(USE_CMP)
using MagicMask = u16;
#else
using MagicMask = Bitboard;
#endif

// Attacks
namespace Attacks {

void init() noexcept;

}  // namespace Attacks

// Return the distance between s1 and s2, defined as the number of steps for a king in s1 to reach s2.
template<typename T = Square>
constexpr u8 distance(Square, Square) noexcept {
    static_assert(sizeof(T) == 0, "Unsupported distance type");
    return 0;
}

template<>
constexpr u8 distance<File>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return constexpr_abs(int(file_of(s1)) - int(file_of(s2)));
}

template<>
constexpr u8 distance<Rank>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return constexpr_abs(int(rank_of(s1)) - int(rank_of(s2)));
}

alignas(CACHE_LINE_SIZE) inline constexpr auto DISTANCES = []() constexpr noexcept {
    Array<u8, SQUARE_NB, SQUARE_NB> distances{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            distances[s1][s2] = std::max(distance<File>(s1, s2), distance<Rank>(s1, s2));

    return distances;
}();

template<>
constexpr u8 distance<Square>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return DISTANCES[s1][s2];
}

// Shifts a bitboard as specified by the direction
template<Direction D>
constexpr Bitboard shift_bb(Bitboard b) noexcept {
    if constexpr (D == Direction::NORTH)
        return b << +Direction::NORTH;
    if constexpr (D == Direction::SOUTH)
        return b >> +Direction::NORTH;
    if constexpr (D == Direction::NORTH_2)
        return b << +Direction::NORTH_2;
    if constexpr (D == Direction::SOUTH_2)
        return b >> +Direction::NORTH_2;
    if constexpr (D == Direction::EAST)
        return (b & ~FILE_H_BB) << +Direction::EAST;
    if constexpr (D == Direction::WEST)
        return (b & ~FILE_A_BB) >> +Direction::EAST;
    if constexpr (D == Direction::NORTH_WEST)
        return (b & ~FILE_A_BB) << +Direction::NORTH_WEST;
    if constexpr (D == Direction::SOUTH_EAST)
        return (b & ~FILE_H_BB) >> +Direction::NORTH_WEST;
    if constexpr (D == Direction::NORTH_EAST)
        return (b & ~FILE_H_BB) << +Direction::NORTH_EAST;
    if constexpr (D == Direction::SOUTH_WEST)
        return (b & ~FILE_A_BB) >> +Direction::NORTH_EAST;
    assert(false);
    UNREACHABLE();
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

    return shift_bb<(C == WHITE ? Direction::NORTH_WEST : Direction::SOUTH_WEST)>(pawns)
         | shift_bb<(C == WHITE ? Direction::NORTH_EAST : Direction::SOUTH_EAST)>(pawns);
}
constexpr Bitboard pawn_attacks_bb(Bitboard pawns, Color c) noexcept {
    assert(is_ok(c));

    return c == WHITE ? pawn_attacks_bb<WHITE>(pawns) : pawn_attacks_bb<BLACK>(pawns);
}

// Returns the bitboard of target square from the given square for the given step.
// If the step is off the board, returns empty bitboard.
constexpr Bitboard destination_bb(Square s, Direction d, u8 dist = 1) noexcept {
    assert(is_ok(s));

    Square nextSq = s + d;

    return is_ok(nextSq) && distance(s, nextSq) <= dist ? square_bb(nextSq) : 0;
}

// Computes sliding attack
template<PieceType PT>
constexpr Bitboard sliding_attacks_bb(Square s, Bitboard occupancyBB = 0) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in sliding_attacks_bb()");
    assert(is_ok(s));

    constexpr Array<Direction, 2, 4> Directions{{
      {Direction::SOUTH_WEST, Direction::SOUTH_EAST, Direction::NORTH_WEST, Direction::NORTH_EAST},
      {Direction::SOUTH, Direction::WEST, Direction::EAST, Direction::NORTH}  //
    }};

    Bitboard attacksBB = 0;

    for (Direction d : Directions[PT - BISHOP])
    {
        Square curSq = s;

        Bitboard destBB = 0;
        while ((destBB = destination_bb(curSq, d)) != 0)
        {
            attacksBB |= destBB;

            // Stop if occupied - sliding blocked
            if ((occupancyBB & destBB) != 0)
                break;

            curSq += d;
        }
    }

    return attacksBB;
}

constexpr Bitboard knight_attacks_bb(Square s) noexcept {
    assert(is_ok(s));

    Bitboard attacksBB = 0;

    for (Direction d : {Direction::SOUTH_2 + Direction::WEST,  //
                        Direction::SOUTH_2 + Direction::EAST,  //
                        Direction::WEST_2 + Direction::SOUTH,  //
                        Direction::EAST_2 + Direction::SOUTH,  //
                        Direction::WEST_2 + Direction::NORTH,  //
                        Direction::EAST_2 + Direction::NORTH,  //
                        Direction::NORTH_2 + Direction::WEST,  //
                        Direction::NORTH_2 + Direction::EAST})
        attacksBB |= destination_bb(s, d, 2);

    return attacksBB;
}

constexpr Bitboard king_attacks_bb(Square s) noexcept {
    assert(is_ok(s));

    Bitboard attacksBB = 0;

    for (Direction d : {Direction::SOUTH_WEST, Direction::SOUTH,  //
                        Direction::SOUTH_EAST, Direction::WEST,   //
                        Direction::EAST, Direction::NORTH_WEST,   //
                        Direction::NORTH, Direction::NORTH_EAST})
        attacksBB |= destination_bb(s, d);

    return attacksBB;
}

alignas(CACHE_LINE_SIZE) inline constexpr auto ATTACKS_BBs = []() constexpr noexcept {
    Array<Bitboard, SQUARE_NB, 1 + PIECE_TYPE_CNT> attacksBBs{};

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

constexpr Bitboard attacks_bb(Square s, usize idx) noexcept {
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
    default :;
    }
    assert(false);
    UNREACHABLE();
    return 0;
}

// Magic holds all magic bitboards relevant data for a single square
struct Magic final {
   public:
    Magic() noexcept                        = default;
    Magic(const Magic&) noexcept            = delete;
    Magic& operator=(const Magic&) noexcept = delete;
    Magic(Magic&&) noexcept                 = delete;
    Magic& operator=(Magic&&) noexcept      = delete;

#if defined(USE_BMI2)
    void attacks_bb(Bitboard occupancyBB, Bitboard referenceBB) noexcept {
    #if defined(USE_CMP)
        attacksBBs[index(occupancyBB)] = _pext_u64(referenceBB, pseudoAttacksBB);
    #else
        attacksBBs[index(occupancyBB)] = referenceBB;
    #endif
    }
#endif

    Bitboard attacks_bb(Bitboard occupancyBB) const noexcept {
#if defined(USE_BMI2)
    #if defined(USE_CMP)
        return _pdep_u64(attacksBBs[index(occupancyBB)], pseudoAttacksBB);
    #else
        return attacksBBs[index(occupancyBB)];
    #endif
#else
        return attacksBBs[index(occupancyBB)];
#endif
    }

    // Compute the attack's index using the 'magic bitboards' approach
    u16 index(Bitboard occupancyBB) const noexcept {
#if defined(USE_BMI2)
        return _pext_u64(occupancyBB, maskBB);
#else
    #if defined(IS_64BIT)
        return ((occupancyBB & maskBB) * magicBB) >> shift;
    #else
        u32 loO = u32(occupancyBB >> 00) & u32(maskBB >> 00);
        u32 hiO = u32(occupancyBB >> 32) & u32(maskBB >> 32);
        u32 loM = u32(magicBB >> 00);
        u32 hiM = u32(magicBB >> 32);
        return ((loO * loM) ^ (hiO * hiM)) >> shift;
    #endif
#endif
    }

    Bitboard   maskBB;
    MagicMask* attacksBBs;
#if defined(USE_BMI2) && defined(USE_CMP)
    Bitboard pseudoAttacksBB;
#else
    Bitboard magicBB;
    u8       shift;
#endif
};

alignas(CACHE_LINE_SIZE) inline Array<Magic, SQUARE_NB, 2> MAGICS;  // BISHOP or ROOK

template<PieceType PT>
constexpr Bitboard attacks_bb(const Array<Magic, 2>& magic, Bitboard occupancyBB) noexcept {
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
    UNREACHABLE();
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
    default :;
    }
    assert(false);
    UNREACHABLE();
    return 0;
}

constexpr Bitboard attacks_bb(Square s, Piece pc, Bitboard occupancyBB) noexcept {
    assert(is_ok(s));

    if (type_of(pc) == PAWN)
        return attacks_bb<PAWN>(s, color_of(pc));

    return attacks_bb(s, type_of(pc), occupancyBB);
}

alignas(CACHE_LINE_SIZE) inline constexpr auto LINE_BBs = []() constexpr noexcept {
    Array<Bitboard, SQUARE_NB, SQUARE_NB> lineBBs{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            for (PieceType pt : {BISHOP, ROOK})
                if ((attacks_bb(s1, pt) & s2) != 0)
                    lineBBs[s1][s2] =
                      (attacks_bb(s1, pt) & attacks_bb(s2, pt)) | square_bb(s1) | square_bb(s2);

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

alignas(CACHE_LINE_SIZE) inline Array<Bitboard, SQUARE_NB, SQUARE_NB> BETWEEN_BBs;

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

alignas(CACHE_LINE_SIZE) inline Array<Bitboard, SQUARE_NB, SQUARE_NB> PASS_RAY_BBs;

// Returns a bitboard representing a ray from the square s1 passing s2.
constexpr Bitboard pass_ray_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return PASS_RAY_BBs[s1][s2];
}

}  // namespace DON

#endif  // #ifndef ATTACKS_H_INCLUDED
