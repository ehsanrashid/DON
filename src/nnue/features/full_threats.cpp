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
#include <cstdint>
#include <initializer_list>

#include "../../attacks.h"
#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../ntypes.h"

namespace DON::NNUE::Features {

namespace {

alignas(CACHE_LINE_SIZE) constexpr Array<u16, PIECE_TYPE_CNT> TARGET_MAX{3, 5, 4, 4, 5, 0};

alignas(CACHE_LINE_SIZE) constexpr Array<i16, PIECE_TYPE_CNT, PIECE_TYPE_CNT> MAP{{
  {+0, +1, -1, +2, -1, -1},  //
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
                    threatCount += constexpr_popcount(pseudo_attacks_bb(s, pt));
                else if (SQ_A2 <= s && s <= SQ_H7)
                {
                    const Bitboard pPushAttacksBB = pawn_push_attacks_bb(square_bb(s), c);
                    threatCount += constexpr_popcount(pPushAttacksBB);
                }
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
                      (u32(semiExcluded) << SEMI_EXCLUDED_OFFSET) | featureIndex;
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
    {
        const Bitboard s1BB = square_bb(s1);

        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            const Bitboard s2MaskBB = square_bb(s2) - 1;
            // clang-format off
            lutIndices[WHITE ][s1][s2] = constexpr_popcount(s2MaskBB & pawn_push_attacks_bb(s1BB, WHITE));
            lutIndices[BLACK ][s1][s2] = constexpr_popcount(s2MaskBB & pawn_push_attacks_bb(s1BB, BLACK));
            lutIndices[KNIGHT][s1][s2] = constexpr_popcount(s2MaskBB & pseudo_attacks_bb(s1, KNIGHT));
            lutIndices[BISHOP][s1][s2] = constexpr_popcount(s2MaskBB & pseudo_attacks_bb(s1, BISHOP));
            lutIndices[ROOK  ][s1][s2] = constexpr_popcount(s2MaskBB & pseudo_attacks_bb(s1, ROOK));
            lutIndices[QUEEN ][s1][s2] = constexpr_popcount(s2MaskBB & pseudo_attacks_bb(s1, QUEEN));
            lutIndices[KING  ][s1][s2] = constexpr_popcount(s2MaskBB & pseudo_attacks_bb(s1, KING));
            // clang-format on
        }
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

// Mirror square to have king always on e..h files
// (file_of(s) >> 2) is 0 for 0...3, 1 for 4...7
constexpr Square orientation(Square s) noexcept { return Square(((file_of(s) >> 2) ^ 0) * FILE_H); }

static_assert(orientation(SQ_A1) == SQ_A1);
static_assert(orientation(SQ_D1) == SQ_A1);
static_assert(orientation(SQ_E1) == SQ_H1);
static_assert(orientation(SQ_H1) == SQ_H1);
static_assert(orientation(SQ_A8) == SQ_A1);
static_assert(orientation(SQ_H8) == SQ_H1);

// Index of a feature for a given king position and another piece on square
ALWAYS_INLINE IndexType make_index(const Color  perspective,
                                   const Square kingSq,
                                   const Square orgSq,
                                   const Square dstSq,
                                   const Piece  attackerPc,
                                   const Piece  attackedPc) noexcept {
    // Compute perspective-relative squares
    u8 relOrientation = relative_sq(perspective, orientation(kingSq));

    u8 org = u8(orgSq) ^ relOrientation;
    u8 dst = u8(dstSq) ^ relOrientation;

    // Compute perspective-relative pieces
    u8 relAttackerPc = +relative_piece(perspective, attackerPc);
    u8 relAttackedPc = +relative_piece(perspective, attackedPc);

    // Lookup LUT
    auto lutData = LUT_DATAS[relAttackerPc][relAttackedPc];

    if (lutData == FullThreats::Dimensions || (semi_excluded(lutData) && org < dst))
        return FullThreats::Dimensions;

    // Compute index components
    return feature_index(lutData)                                     //
         + lut_index(Piece(relAttackerPc), Square(org), Square(dst))  //
         + SQUARE_OFFSETS[relAttackerPc][org];
}

ALWAYS_INLINE void append_pawn_active_indices(Bitboard                attacksBB,
                                              const Direction         attackDir,
                                              const Color             perspective,
                                              const Position&         pos,
                                              const Square            kingSq,
                                              const Piece             attackerPc,
                                              FullThreats::IndexList& active) noexcept {
    while (attacksBB != 0)
    {
        const Square dstSq      = pop_lsq(attacksBB);
        const Square orgSq      = dstSq - attackDir;
        const Piece  attackedPc = pos[dstSq];

        assert(file_of(orgSq) != file_of(dstSq) || type_of(attackedPc) == PAWN);

        const auto index = make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

        active.push_back_if_lt(index, FullThreats::Dimensions);
    }
}

}  // namespace

// Append list of indices for active features in ascending order
void FullThreats::append_active_indices(const Color     perspective,
                                        const Position& pos,
                                        IndexList&      active) noexcept {
    const Square kingSq = pos.square<KING>(perspective);

    const Bitboard occupancyBB = pos.pieces_bb();
    const Bitboard pawnsBB     = pos.pieces_bb(PAWN);

    for (const Color c : {WHITE, BLACK})
    {
        const Color attackerC = Color(perspective ^ c);

        {
            const Piece attackerPc = make_piece(attackerC, PAWN);

            const Bitboard attackerBB = pos.pieces_bb(attackerC, PAWN);

            append_pawn_active_indices(
              (attackerC == WHITE ? shift_bb<Direction::NORTH_EAST>(attackerBB)
                                  : shift_bb<Direction::SOUTH_WEST>(attackerBB))
                & occupancyBB,
              attackerC == WHITE ? Direction::NORTH_EAST : Direction::SOUTH_WEST,  //
              perspective, pos, kingSq, attackerPc, active);

            append_pawn_active_indices(
              (attackerC == WHITE ? shift_bb<Direction::NORTH_WEST>(attackerBB)
                                  : shift_bb<Direction::SOUTH_EAST>(attackerBB))
                & occupancyBB,
              attackerC == WHITE ? Direction::NORTH_WEST : Direction::SOUTH_EAST,  //
              perspective, pos, kingSq, attackerPc, active);

            // Set of pawns which are prevented from movement by a pawn in front of them
            const Bitboard pushersBB = attackerBB & pawn_push_bb(pawnsBB, ~attackerC);
            append_pawn_active_indices((attackerC == WHITE ? shift_bb<Direction::NORTH>(pushersBB)
                                                           : shift_bb<Direction::SOUTH>(pushersBB)),
                                       pawn_spush(attackerC),  //
                                       perspective, pos, kingSq, attackerPc, active);
        }

        for (const PieceType pt : NON_PAWN_PIECE_TYPES)
        {
            const Piece attackerPc = make_piece(attackerC, pt);

            Bitboard attackerBB = pos.pieces_bb(attackerC, pt);
            while (attackerBB != 0)
            {
                const Square orgSq = pop_lsq(attackerBB);

                Bitboard attacksBB = attacks_bb(orgSq, pt, occupancyBB) & occupancyBB;

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
void FullThreats::append_changed_indices(const Color             perspective,
                                         const Square            kingSq,
                                         const DirtyType&        dts,
                                         IndexList&              removed,
                                         IndexList&              added,
                                         const ThreatWeightType* pfBase,
                                         const usize             pfStride) noexcept {
    for (const auto& dt : dts.dtList)
    {
        const auto orgSq      = dt.sq();
        const auto dstSq      = dt.threatened_sq();
        const auto attackerPc = dt.pc();
        const auto attackedPc = dt.threatened_pc();
        const auto add        = dt.add();

        auto& changed = add ? added : removed;

        const auto index = make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

        if (pfBase != nullptr)
            prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(
              reinterpret_cast<const void*>(reinterpret_cast<uptr>(pfBase) + index * pfStride));

        changed.push_back_if_lt(index, Dimensions);
    }
}

// Determine if a full refresh is required based on the dirty threats
bool FullThreats::refresh_required(const Color perspective, const DirtyType& dts) noexcept {
    return dts.ac == perspective && orientation(dts.kingSq) != orientation(dts.preKingSq);
}

}  // namespace DON::NNUE::Features
