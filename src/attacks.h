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
#include <utility>

#if defined(USE_BMI2)
    #include <immintrin.h>  // BMI2 [_pext_u64() & _pdep_u64()] intrinsics
#endif
#if defined(USE_AVX2)
    #include <immintrin.h>
    #define USE_DUAL_HYPERBOLA_QUINT
#elif defined(__loongarch__) && __loongarch_grlen == 64
    #define USE_HYPERBOLA_QUINT
#elif defined(__aarch64__)
    #include <arm_acle.h>
    #define USE_HYPERBOLA_QUINT
#endif

#include "bitboard.h"
#include "misc.h"
#include "types.h"

namespace DON {

// Attacks
namespace Attacks {

void init() noexcept;

}  // namespace Attacks

#if defined(USE_DUAL_HYPERBOLA_QUINT)

struct alignas(32) DualMagic final {
   public:
    // Always compute [bishop, rook] attacks at once, then rely on
    // compiler's DCE and CSE to eliminate unneeded re-computations or extractions.
    //
    // When using hyperbola quintessence, file, diagonal and antidiagonal attacks
    // can use a byte reversal rather than a full bit reversal (because all squares
    // reside in different bytes). Rank attacks cannot. Thus, for rank attacks
    // only, we use a compact lookup table indexed by the 6 inner bits of the rank's
    // occupancy (the edge squares never affect the attack set).
    std::pair<Bitboard, Bitboard> attacks_bb_pair(const Bitboard occupancyBB) const noexcept {
        // Byteswap within 128-bit elements
        const auto bswap = [](const __m256i v) noexcept {
            return _mm256_shuffle_epi8(v, _mm256_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                                          13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                                          10, 11, 12, 13, 14, 15));
        };

        // Each lane contains a mask and we follow the same HQ algorithm as
        // given above in the ARM64 code path
        const __m256i mask = _mm256_load_si256(reinterpret_cast<const __m256i*>(this));
        const __m256i rs   = _mm256_set1_epi64x(rBB);
        const __m256i rrs  = _mm256_set1_epi64x(rrBB);

        __m256i o      = _mm256_and_si256(mask, _mm256_set1_epi64x(occupancyBB));
        __m256i fwd    = _mm256_sub_epi64(o, rs);
        __m256i rev    = bswap(_mm256_sub_epi64(bswap(o), rrs));
        __m256i attack = _mm256_and_si256(_mm256_xor_si256(fwd, rev), mask);

        // Lane 0: rook attacks (file only); lane 1: bishop attacks
        __m128i rookBishop =
          _mm_or_si128(_mm256_extracti128_si256(attack, 1), _mm256_castsi256_si128(attack));

        Bitboard rowOccupancy = rankAttacksLookup[(occupancyBB >> (shift + 1)) & 0x3F];
        Bitboard rankAttacks  = rowOccupancy << shift;

        // [bishop, rook]
        return {_mm_extract_epi64(rookBishop, 1), _mm_cvtsi128_si64(rookBishop) + rankAttacks};
    }

    // file, diagonal, unused, antidiagonal
    Bitboard maskFileBB, maskDiagBB, maskNoneBB, maskAntidiagBB;
    // Precomputed 2 * square_bb(sq), 2 * reverse(square_bb(sq))
    Bitboard rBB, rrBB;

    const u8* RESTRICT rankAttacksLookup;
    // 8 * rank_of(sq)
    int shift;
};

#elif defined(USE_HYPERBOLA_QUINT)

inline Bitboard reverse_bb(const Bitboard bb) noexcept {
    Bitboard rbb;

    #if defined(__has_builtin) && __has_builtin(__builtin_bitreverse64)
    rbb = __builtin_bitreverse64(bb);

    #elif defined(__loongarch__) && __loongarch_grlen == 64
    asm("bitrev.d %0, %1" : "=r"(rbb) : "r"(bb));

    #elif defined(__aarch64__)
        // GCC before 12.2 does not provide __rbitll() in arm_acle.h
        #if defined(__GNUC__) && !defined(__clang__) \
          && (__GNUC__ < 12 || (__GNUC__ == 12 && __GNUC_MINOR__ < 2))
    asm("rbit %0, %1" : "=r"(rbb) : "r"(bb));

        #else
    rbb = __rbitll(bb);

        #endif
    #else
        #error "reverse_bb(): unsupported architecture/compiler"
    #endif

    return rbb;
}

// Hyperbola quintessence implementation for ARM
// thanks to the availability of an efficient bit reversal instruction.
// See https://www.chessprogramming.org/Hyperbola_Quintessence
struct Magic final {
   public:
    Bitboard
    hyperbola(const Square s, const Bitboard occupancyBB, const Bitboard maskBB) const noexcept {
        Bitboard occBB = occupancyBB & maskBB;
        Bitboard fwdBB = occBB - square_bb(s);
        Bitboard revBB = reverse_bb(occBB) - square_bb(reverse_sq(s));
        return (fwdBB ^ reverse_bb(revBB)) & maskBB;
    }

    Bitboard attacks_bb(const Square s, const Bitboard occupancyBB) const noexcept {
        return hyperbola(s, occupancyBB, mask1BB) | hyperbola(s, occupancyBB, mask2BB);
    }

    // For rooks: file/rank attacks
    // For bishops: diagonal/anti-diagonal attacks
    Bitboard mask1BB, mask2BB;
};

#else

    #if defined(USE_BMI2) && defined(USE_CMP)
using MagicMask = u16;
    #else
using MagicMask = Bitboard;
    #endif

// Magic holds all magic bitboards relevant data for a single square
struct Magic final {
   public:
    Magic() noexcept                        = default;
    Magic(const Magic&) noexcept            = delete;
    Magic& operator=(const Magic&) noexcept = delete;
    Magic(Magic&&) noexcept                 = delete;
    Magic& operator=(Magic&&) noexcept      = delete;

    #if defined(USE_BMI2)
    void attacks_bb(const Bitboard occupancyBB, const Bitboard referenceBB) noexcept {
        #if defined(USE_CMP)
        attacksBBs[index(occupancyBB)] = _pext_u64(referenceBB, pseudoAttacksBB);
        #else
        attacksBBs[index(occupancyBB)] = referenceBB;
        #endif
    }
    #endif

    Bitboard attacks_bb([[maybe_unused]] const Square s,
                        const Bitboard                occupancyBB) const noexcept {
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
    u16 index(const Bitboard occupancyBB) const noexcept {
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

#endif

// Return the distance between s1 and s2, defined as the number of steps for a king in s1 to reach s2.
template<typename T = Square>
constexpr u8 distance(const Square, const Square) noexcept {
    static_assert(sizeof(T) == 0, "Unsupported distance type");
    return 0;
}

template<>
constexpr u8 distance<File>(const Square s1, const Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return constexpr_abs(int(file_of(s1)) - int(file_of(s2)));
}

template<>
constexpr u8 distance<Rank>(const Square s1, const Square s2) noexcept {
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
constexpr u8 distance<Square>(const Square s1, const Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return DISTANCES[s1][s2];
}

// Shifts a bitboard as specified by the direction
template<Direction D>
constexpr Bitboard shift_bb(const Bitboard b) noexcept {
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
constexpr Bitboard pawn_push_bb(const Bitboard pawns) noexcept {
    static_assert(is_ok(C), "Invalid color for pawn_push_bb()");

    return shift_bb<pawn_spush(C)>(pawns);
}
constexpr Bitboard pawn_push_bb(const Bitboard pawns, const Color c) noexcept {
    assert(is_ok(c));

    return c == WHITE ? pawn_push_bb<WHITE>(pawns) : pawn_push_bb<BLACK>(pawns);
}

// Returns the squares attacked by pawns of the given color from the given bitboard
template<Color C>
constexpr Bitboard pawn_attacks_bb(const Bitboard pawns) noexcept {
    static_assert(is_ok(C), "Invalid color for pawn_attacks_bb()");

    return shift_bb<(C == WHITE ? Direction::NORTH_WEST : Direction::SOUTH_WEST)>(pawns)
         | shift_bb<(C == WHITE ? Direction::NORTH_EAST : Direction::SOUTH_EAST)>(pawns);
}
constexpr Bitboard pawn_attacks_bb(const Bitboard pawns, const Color c) noexcept {
    assert(is_ok(c));

    return c == WHITE ? pawn_attacks_bb<WHITE>(pawns) : pawn_attacks_bb<BLACK>(pawns);
}

template<Color C>
constexpr Bitboard pawn_push_attacks_bb(const Bitboard pawns) noexcept {
    static_assert(is_ok(C), "Invalid color for pawn_push_attacks_bb()");

    return pawn_push_bb<C>(pawns) | pawn_attacks_bb<C>(pawns);
}
constexpr Bitboard pawn_push_attacks_bb(const Bitboard pawns, const Color c) noexcept {
    assert(is_ok(c));

    return c == WHITE ? pawn_push_attacks_bb<WHITE>(pawns) : pawn_push_attacks_bb<BLACK>(pawns);
}

// Returns the bitboard of target square from the given square for the given step.
// If the step is off the board, returns empty bitboard.
constexpr Bitboard destination_bb(const Square s, const Direction d, const u8 dist = 1) noexcept {
    assert(is_ok(s));

    Square destSq = s + d;

    return is_ok(destSq) && distance(s, destSq) <= dist ? square_bb(destSq) : 0;
}

template<typename... Directions>
constexpr Bitboard ray_bb(const Square s, const Directions... ds) noexcept {
    static_assert((std::is_same_v<Directions, Direction> && ...),
                  "All arguments must be Direction");

    assert(is_ok(s));

    Bitboard rayBB = 0;

    const auto add_ray_bb = [&](Direction d) noexcept {
        Square curSq = s;

        Bitboard destBB = 0;
        while ((destBB = destination_bb(curSq, d)) != 0)
        {
            rayBB |= destBB;
            curSq += d;
        }
    };

    (add_ray_bb(ds), ...);

    return rayBB;
}

constexpr Bitboard knight_attacks_bb(const Square s) noexcept {
    assert(is_ok(s));

    constexpr Array<Direction, 8> Directions{
      Direction::SOUTH_2 + Direction::WEST, Direction::SOUTH_2 + Direction::EAST,
      Direction::WEST_2 + Direction::SOUTH, Direction::EAST_2 + Direction::SOUTH,
      Direction::WEST_2 + Direction::NORTH, Direction::EAST_2 + Direction::NORTH,
      Direction::NORTH_2 + Direction::WEST, Direction::NORTH_2 + Direction::EAST  //
    };

    Bitboard attacksBB = 0;

    for (Direction d : Directions)
        attacksBB |= destination_bb(s, d, 2);

    return attacksBB;
}

constexpr Bitboard king_attacks_bb(const Square s) noexcept {
    assert(is_ok(s));

    constexpr Array<Direction, 8> Directions{
      Direction::SOUTH_WEST, Direction::SOUTH,      Direction::SOUTH_EAST, Direction::WEST,
      Direction::EAST,       Direction::NORTH_WEST, Direction::NORTH,      Direction::NORTH_EAST  //
    };

    Bitboard attacksBB = 0;

    for (Direction d : Directions)
        attacksBB |= destination_bb(s, d);

    return attacksBB;
}

// Computes sliding attack
template<PieceType PT>
constexpr Bitboard sliding_attacks_bb(const Square s, const Bitboard occupancyBB = 0) noexcept {
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

template<PieceType PT>
constexpr Bitboard pseudo_attacks_bb(const Square s) noexcept {
    if constexpr (PT == KNIGHT)
        return knight_attacks_bb(s);
    if constexpr (PT == BISHOP)
        return sliding_attacks_bb<BISHOP>(s, 0);
    if constexpr (PT == ROOK)
        return sliding_attacks_bb<ROOK>(s, 0);
    if constexpr (PT == QUEEN)
        return pseudo_attacks_bb<BISHOP>(s) | pseudo_attacks_bb<ROOK>(s);
    if constexpr (PT == KING)
        return king_attacks_bb(s);
    assert(false);
    UNREACHABLE();
    return 0;
}

alignas(CACHE_LINE_SIZE) inline constexpr auto PSEUDO_ATTACKS_BBs = []() constexpr noexcept {
    Array<Bitboard, SQUARE_NB, PIECE_TYPE_CNT + 1> pseudoAttacksBB{};

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        pseudoAttacksBB[s][WHITE]  = pawn_attacks_bb<WHITE>(square_bb(s));
        pseudoAttacksBB[s][BLACK]  = pawn_attacks_bb<BLACK>(square_bb(s));
        pseudoAttacksBB[s][KNIGHT] = pseudo_attacks_bb<KNIGHT>(s);
        pseudoAttacksBB[s][BISHOP] = pseudo_attacks_bb<BISHOP>(s);
        pseudoAttacksBB[s][ROOK]   = pseudo_attacks_bb<ROOK>(s);
        pseudoAttacksBB[s][QUEEN]  = pseudoAttacksBB[s][BISHOP] | pseudoAttacksBB[s][ROOK];
        pseudoAttacksBB[s][KING]   = pseudo_attacks_bb<KING>(s);
    }

    return pseudoAttacksBB;
}();

constexpr Bitboard pseudo_attacks_bb(Square s, usize idx) noexcept {
    assert(is_ok(s));

    return PSEUDO_ATTACKS_BBs[s][idx];
}

// Returns the pseudo attacks of the given piece type assuming an empty board
template<PieceType PT>
constexpr Bitboard attacks_bb(const Square s, [[maybe_unused]] const Color c = NONE) noexcept {
    static_assert(is_ok(PT), "Unsupported piece type in attacks_bb()");
    assert(is_ok(s) && (PT != PAWN || is_ok(c)));

    if constexpr (PT == PAWN)
        return pseudo_attacks_bb(s, c);

    return pseudo_attacks_bb(s, PT);
}

constexpr Bitboard attacks_bb(const Square s, const Piece pc) noexcept {
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

#if defined(USE_DUAL_HYPERBOLA_QUINT)

// Sliding attacks within a rank, indexed by the slider's file and the
// 6 inner bits of the rank occupancy (edge squares never affect the
// attack set), yielding the 8-bit attack set on that rank
alignas(CACHE_LINE_SIZE) inline constexpr auto RANK_ATTACKS = []() constexpr noexcept {
    Array<u8, FILE_NB, SQUARE_NB> rankAttacks{};

    for (Square f = SQ_A1; f <= SQ_H1; ++f)
        for (u16 occ6 = 0; occ6 < 64; ++occ6)
            rankAttacks[f][occ6] = u8(sliding_attacks_bb<ROOK>(f, Bitboard{occ6} << 1));

    return rankAttacks;
}();

alignas(CACHE_LINE_SIZE) inline constexpr auto DUAL_MAGICS = []() constexpr noexcept {
    Array<DualMagic, SQUARE_NB> dualMagics{};

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        auto& dm             = dualMagics[s];
        dm.maskFileBB        = ray_bb(s, Direction::NORTH, Direction::SOUTH);
        dm.maskDiagBB        = ray_bb(s, Direction::NORTH_EAST, Direction::SOUTH_WEST);
        dm.maskNoneBB        = 0;
        dm.maskAntidiagBB    = ray_bb(s, Direction::NORTH_WEST, Direction::SOUTH_EAST);
        dm.rBB               = 2 * square_bb(s);
        dm.rrBB              = 2 * square_bb(reverse_sq(s));
        dm.rankAttacksLookup = RANK_ATTACKS[file_of(s)].data();
        dm.shift             = 8 * int(rank_of(s));
    }

    return dualMagics;
}();

constexpr const DualMagic& dual_magic(const Square s) { return DUAL_MAGICS[s]; }

#else

alignas(CACHE_LINE_SIZE) inline Array<Magic, SQUARE_NB, 2> MAGICS;  // BISHOP or ROOK

template<PieceType PT>
constexpr const Magic& magic(const Square s) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in magic()");
    assert(is_ok(s));

    return MAGICS[s][PT - BISHOP];
}

#endif

// Returns the attacks by the given piece type.
// Sliding piece attacks do not continue past an occupied square.
template<PieceType PT>
constexpr Bitboard attacks_bb(const Square                    s,
                              [[maybe_unused]] const Bitboard occupancyBB) noexcept {
    static_assert(PT != PAWN, "Unsupported piece type in attacks_bb()");
    assert(is_ok(s));

    if constexpr (PT == KNIGHT)
        return attacks_bb<KNIGHT>(s);
    if constexpr (PT == KING)
        return attacks_bb<KING>(s);

#if defined(USE_DUAL_HYPERBOLA_QUINT)
    [[maybe_unused]] const auto [bAttacksBB, rAttacksBB] =
      dual_magic(s).attacks_bb_pair(occupancyBB);

    if constexpr (PT == BISHOP)
        return bAttacksBB;
    if constexpr (PT == ROOK)
        return rAttacksBB;
    if constexpr (PT == QUEEN)
        return bAttacksBB | rAttacksBB;
#else
    if constexpr (PT == BISHOP || PT == ROOK)
        return magic<PT>(s).attacks_bb(s, occupancyBB);
    if constexpr (PT == QUEEN)
        return attacks_bb<BISHOP>(s, occupancyBB) | attacks_bb<ROOK>(s, occupancyBB);
#endif

    assert(false);
    UNREACHABLE();
    return 0;
}

// Returns the attacks by the given piece type.
// Sliding piece attacks do not continue past an occupied square.
constexpr Bitboard
attacks_bb(const Square s, const PieceType pt, const Bitboard occupancyBB) noexcept {
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

constexpr Bitboard attacks_bb(const Square s, const Piece pc, const Bitboard occupancyBB) noexcept {
    assert(is_ok(s));

    if (type_of(pc) == PAWN)
        return attacks_bb<PAWN>(s, color_of(pc));

    return attacks_bb(s, type_of(pc), occupancyBB);
}

constexpr std::pair<Bitboard, Bitboard> attacks_bb_pair(const Square s) noexcept {
    return {attacks_bb<BISHOP>(s), attacks_bb<ROOK>(s)};
}

inline std::pair<Bitboard, Bitboard> attacks_bb_pair(const Square   s,
                                                     const Bitboard occupancyBB) noexcept {
#if defined(USE_DUAL_HYPERBOLA_QUINT)
    return dual_magic(s).attacks_bb_pair(occupancyBB);
#else
    return {attacks_bb<BISHOP>(s, occupancyBB), attacks_bb<ROOK>(s, occupancyBB)};
#endif
}

alignas(CACHE_LINE_SIZE) inline constexpr auto LINE_BBs = []() constexpr noexcept {
    Array<Bitboard, SQUARE_NB, SQUARE_NB> lineBBs{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            for (PieceType pt : {BISHOP, ROOK})
                if ((pseudo_attacks_bb(s1, pt) & s2) != 0)
                    lineBBs[s1][s2] = (pseudo_attacks_bb(s1, pt) & pseudo_attacks_bb(s2, pt))
                                    | square_bb(s1) | square_bb(s2);

    return lineBBs;
}();

// Returns a bitboard representing an entire line (from board edge to board edge)
// passing through the squares s1 and s2.
// If the given squares are not on a same file/rank/diagonal, it returns 0.
// For instance, line_bb(SQ_C4, SQ_F7) will return a bitboard with the A2-G8 diagonal.
constexpr Bitboard line_bb(const Square s1, const Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return LINE_BBs[s1][s2];
}

// Returns true if the squares s1, s2 and s3 are aligned on straight or diagonal line.
constexpr bool aligned(const Square s1, const Square s2, const Square s3) noexcept {
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
constexpr Bitboard between_bb(const Square s1, const Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return BETWEEN_BBs[s1][s2];
}

// Returns a bitboard between the squares s1 and s2 (excluding s1 and s2).
constexpr Bitboard between_ex_bb(const Square s1, const Square s2) noexcept {
    return between_bb(s1, s2) ^ s2;
}

alignas(CACHE_LINE_SIZE) inline Array<Bitboard, SQUARE_NB, SQUARE_NB> PASS_RAY_BBs;

// Returns a bitboard representing a ray from the square s1 passing s2.
constexpr Bitboard pass_ray_bb(const Square s1, const Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    return PASS_RAY_BBs[s1][s2];
}

}  // namespace DON

#endif  // ATTACKS_H_INCLUDED
