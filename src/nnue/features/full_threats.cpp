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

// Definition of input features FullThreats of NNUE evaluation function

#include "full_threats.h"

#include <array>
#include <cassert>
#include <initializer_list>

#include "../../attacks.h"
#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../ntypes.h"

namespace DON::NNUE::Features {

namespace {

// Pawn diagonal threats only target knights and rooks,
// (pawn-pawn relationships are handled by the PP_3Wide feature set),
// so pawns have 2 valid targets.
alignas(CACHE_LINE_SIZE) constexpr Array<u16, PIECE_TYPE_CNT> TARGET_MAX{2, 5, 4, 4, 5, 0};

alignas(CACHE_LINE_SIZE) constexpr Array<i16, PIECE_TYPE_CNT, PIECE_TYPE_CNT> MAP{{
  {-1, +0, -1, +1, -1, -1},  //
  {+0, +1, +2, +3, +4, -1},  //
  {+0, +1, +2, +3, -1, -1},  //
  {+0, +1, +2, +3, -1, -1},  //
  {+0, +1, +2, +3, +4, -1},  //
  {-1, -1, -1, -1, -1, -1}   //
}};

struct PieceThreat final {
   public:
    u32 baseOffset;   // Base index in the global threat table for this piece
    u32 threatCount;  // Total number of threats this piece can generate
};

struct ThreatTable final {
   public:
    Array<PieceThreat, PIECE_NB>    pieceThreats;
    Array<u32, PIECE_NB, SQUARE_NB> squareOffsets;
};

alignas(CACHE_LINE_SIZE) constexpr auto THREAT_TABLE = []() constexpr noexcept {
    ThreatTable threatTable{};

    u32 baseOffset = 0;

    for (const Color c : {WHITE, BLACK})
        for (const PieceType pt : PIECE_TYPES)
        {
            const Piece pc = make_piece(c, pt);

            u32 threatCount = 0;

            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                threatTable.squareOffsets[+pc][s] = threatCount;

                if (pt != PAWN)
                    threatCount += constexpr_popcount(Attacks::pseudo_attacks_bb(s, pt));
                else if (SQ_A2 <= s && s <= SQ_H7)
                    threatCount += constexpr_popcount(Attacks::pseudo_attacks_bb(s, c));
            }

            threatTable.pieceThreats[+pc] = {baseOffset, threatCount};

            baseOffset += 2 * TARGET_MAX[pt - 1] * threatCount;
        }

    return threatTable;
}();

constexpr auto& PIECE_THREATS  = THREAT_TABLE.pieceThreats;
constexpr auto& SQUARE_OFFSETS = THREAT_TABLE.squareOffsets;

constexpr IndexType dimensions() noexcept {
    IndexType dims = 0;
    for (const Color c : {WHITE, BLACK})
        for (const PieceType pt : PIECE_TYPES)
            dims += 2 * TARGET_MAX[pt - 1]  //
                  * PIECE_THREATS[+make_piece(c, pt)].threatCount;

    return dims;
}

static_assert(dimensions() == FullThreats::Dimensions);

constexpr u8  SEMI_EXCLUDED_OFFSET = 31;
constexpr u32 SEMI_EXCLUDED_MASK   = 1u << SEMI_EXCLUDED_OFFSET;
constexpr u32 FEATURE_INDEX_MASK   = SEMI_EXCLUDED_MASK - 1;

// LUT for getting feature base index and exclusion info
// [attackerPc][attackedPc]
alignas(CACHE_LINE_SIZE) constexpr auto LUT_DATAS = []() constexpr noexcept {
    Array<u32, PIECE_NB, PIECE_NB> lutDatas{};

    for (Color attackerC : {WHITE, BLACK})
        for (PieceType attackerPt : PIECE_TYPES)
        {
            Piece attackerPc = make_piece(attackerC, attackerPt);

            for (Color attackedC : {WHITE, BLACK})
                for (PieceType attackedPt : PIECE_TYPES)
                {
                    Piece attackedPc = make_piece(attackedC, attackedPt);

                    auto map = MAP[attackerPt - 1][attackedPt - 1];

                    // Excluded
                    if (map < 0)
                    {
                        lutDatas[+attackerPc][+attackedPc] = FullThreats::Dimensions;
                        continue;
                    }

                    bool enemy = attackerC != attackedC;

                    bool semiExcluded = attackerPt == attackedPt && (enemy || attackerPt != PAWN);

                    u32 featureIndex = PIECE_THREATS[+attackerPc].baseOffset
                                     + (attackedC * TARGET_MAX[attackerPt - 1] + map)
                                         * PIECE_THREATS[+attackerPc].threatCount;

                    lutDatas[+attackerPc][+attackedPc] =
                      (static_cast<u32>(semiExcluded) << SEMI_EXCLUDED_OFFSET) | featureIndex;
                }
        }

    return lutDatas;
}();

// Get if semi-excluded from LUT data
constexpr bool semi_excluded(u32 lutData) noexcept { return (lutData & SEMI_EXCLUDED_MASK) != 0; }
// Get feature base index from LUT data
constexpr IndexType feature_index(u32 lutData) noexcept { return lutData & FEATURE_INDEX_MASK; }

// LUT for getting index within piece threats
// [attackerPt][orgSq][dstSq]
alignas(CACHE_LINE_SIZE) const auto LUT_INDICES = []() noexcept {
    Array<u8, PIECE_TYPE_CNT + 1, SQUARE_NB, SQUARE_NB> lutIndices{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            const Bitboard s2MaskBB = square_bb(s2) - 1;
            // clang-format off
            lutIndices[WHITE ][s1][s2] = constexpr_popcount(s2MaskBB & Attacks::pseudo_attacks_bb(s1, WHITE));
            lutIndices[BLACK ][s1][s2] = constexpr_popcount(s2MaskBB & Attacks::pseudo_attacks_bb(s1, BLACK));
            lutIndices[KNIGHT][s1][s2] = constexpr_popcount(s2MaskBB & Attacks::pseudo_attacks_bb(s1, KNIGHT));
            lutIndices[BISHOP][s1][s2] = constexpr_popcount(s2MaskBB & Attacks::pseudo_attacks_bb(s1, BISHOP));
            lutIndices[ROOK  ][s1][s2] = constexpr_popcount(s2MaskBB & Attacks::pseudo_attacks_bb(s1, ROOK));
            lutIndices[QUEEN ][s1][s2] = constexpr_popcount(s2MaskBB & Attacks::pseudo_attacks_bb(s1, QUEEN));
            lutIndices[KING  ][s1][s2] = constexpr_popcount(s2MaskBB & Attacks::pseudo_attacks_bb(s1, KING));
            // clang-format on
        }

    return lutIndices;
}();

// Get index within piece threats
constexpr u8 lut_index(Piece pc, Square s1, Square s2) noexcept {
    assert(is_ok(pc) && is_ok(s1) && is_ok(s2));

    if (type_of(pc) == PAWN)
        return LUT_INDICES[color_of(pc)][s1][s2];

    return LUT_INDICES[type_of(pc)][s1][s2];
}

// Index of a feature for a given king position and another piece on square
ALWAYS_INLINE constexpr u16 make_index(const Color  perspective,
                                       const Square kingSq,
                                       const Square orgSq,
                                       const Square dstSq,
                                       const Piece  attackerPc,
                                       const Piece  attackedPc) noexcept {
    // Compute perspective-relative squares
    u8 relOrientation = relative_sq(perspective, FullThreats::orientation(kingSq));

    u8 org = static_cast<u8>(orgSq) ^ relOrientation;
    u8 dst = static_cast<u8>(dstSq) ^ relOrientation;

    // Compute perspective-relative pieces
    u8 relAttackerPc = +relative_piece(perspective, attackerPc);
    u8 relAttackedPc = +relative_piece(perspective, attackedPc);

    // Lookup LUT
    auto lutData = LUT_DATAS[relAttackerPc][relAttackedPc];

    if (lutData == FullThreats::Dimensions || (semi_excluded(lutData) && org < dst))
        return FullThreats::Dimensions;

    // Compute index components
    return feature_index(lutData)                                     //
         + lut_index(Piece{relAttackerPc}, Square{org}, Square{dst})  //
         + SQUARE_OFFSETS[relAttackerPc][org];
}

ALWAYS_INLINE void append_pawn_active_indices(Bitboard                  attacksBB,
                                              const Direction           attackDir,
                                              const Color               perspective,
                                              const Position&           pos,
                                              const Square              kingSq,
                                              const Piece               attackerPc,
                                              FullThreats::IndexVector& active) noexcept {
    while (attacksBB != 0)
    {
        const Square dstSq      = pop_lsq(attacksBB);
        const Square orgSq      = dstSq - attackDir;
        const Piece  attackedPc = pos[dstSq];

        const auto index = make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

        active.push_back_if_lt(index, FullThreats::Dimensions);
    }
}

}  // namespace

// Append list of indices for active features in ascending order
void FullThreats::append_active_indices(const Color     perspective,
                                        const Position& pos,
                                        IndexVector&    active) noexcept {
    const Square kingSq = pos.square<KING>(perspective);

    const Bitboard occupancyBB          = pos.pieces_bb();
    const Bitboard pawnTargetsBB        = pos.pieces_bb(KNIGHT, ROOK);
    const Bitboard sliderTargetsBB      = pos.pieces_bb(PAWN, KNIGHT, BISHOP, ROOK);
    const Bitboard knightqueenTargetsBB = pos.pieces_bb(PAWN, KNIGHT, BISHOP, ROOK, QUEEN);

    for (const Color c : {WHITE, BLACK})
    {
        const Color attackerC = Color(perspective ^ c);

        {
            const Piece attackerPc = make_piece(attackerC, PAWN);

            const Bitboard cpawnsBB = pos.pieces_bb(attackerC, PAWN);

            append_pawn_active_indices(
              attackerC == WHITE ? shift_bb<Direction::NORTH_EAST>(cpawnsBB) & pawnTargetsBB
                                 : shift_bb<Direction::SOUTH_WEST>(cpawnsBB) & pawnTargetsBB,
              attackerC == WHITE ? Direction::NORTH_EAST : Direction::SOUTH_WEST,  //
              perspective, pos, kingSq, attackerPc, active);

            append_pawn_active_indices(
              attackerC == WHITE ? shift_bb<Direction::NORTH_WEST>(cpawnsBB) & pawnTargetsBB
                                 : shift_bb<Direction::SOUTH_EAST>(cpawnsBB) & pawnTargetsBB,
              attackerC == WHITE ? Direction::NORTH_WEST : Direction::SOUTH_EAST,  //
              perspective, pos, kingSq, attackerPc, active);
        }

        for (const PieceType pt : NON_PAWN_PIECE_TYPES)
        {
            const Piece attackerPc = make_piece(attackerC, pt);

            const Bitboard targetsBB =
              pt == KNIGHT || pt == QUEEN ? knightqueenTargetsBB : sliderTargetsBB;

            Bitboard cattackerBB = pos.pieces_bb(attackerC, pt);
            while (cattackerBB != 0)
            {
                const Square orgSq = pop_lsq(cattackerBB);

                Bitboard attacksBB = Attacks::attacks_bb(orgSq, pt, occupancyBB) & targetsBB;
                while (attacksBB != 0)
                {
                    const Square dstSq      = pop_lsq(attacksBB);
                    const Piece  attackedPc = pos[dstSq];

                    const auto index =
                      make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

                    active.push_back_if_lt(index, Dimensions);
                }
            }
        }
    }
}

// Append lists of indices for recently changed features
void FullThreats::append_changed_indices(const Color                   perspective,
                                         const Square                  kingSq,
                                         const DirtyType&              dTs,
                                         IndexVector&                  removed,
                                         IndexVector&                  added,
                                         const ThreatWeightType* const pfBase,
                                         const usize                   pfStride) noexcept {
    for (const auto& dT : dTs)
    {
        const auto orgSq      = dT.sq();
        const auto dstSq      = dT.threatened_sq();
        const auto attackerPc = dT.pc();
        const auto attackedPc = dT.threatened_pc();
        const auto add        = dT.add();

        auto& changed = add ? added : removed;

        const auto index = make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

        if (pfBase != nullptr)
            prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(
              reinterpret_cast<const void*>(reinterpret_cast<uptr>(pfBase) + index * pfStride));

        changed.push_back_if_lt(index, Dimensions);
    }
}

}  // namespace DON::NNUE::Features
