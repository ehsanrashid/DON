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

#include "../../nnue/features/full_threats.h"

#include <array>
#include <cassert>
#include <initializer_list>

#include "../../attacks.h"
#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../../nnue/types.h"
#include "../../utility/attributes.h"
#include "../../utility/static_vector.h"
#include "../../utility/types.h"

namespace DON::NNUE::Features {
namespace FullThreats {
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

struct alignas(CACHE_LINE_SIZE) ThreatLUTs final {
   public:
    // [attacker][attacked][from < to]
    Array<Index, PIECE_NB, PIECE_NB, 2> lut1{};
    // [pt][from][to]
    Array<Index, PIECE_TYPE_NB, SQUARE_NB, SQUARE_NB> lut2{};
};

constexpr auto init_threat_luts() noexcept {
    ThreatLUTs                threatLuts{};
    Array<u16, PIECE_TYPE_NB> offsets{};

    // Initialize lut2: index within each piece's threat features.
    for (const auto pt : PIECE_TYPES)
    {
        u16 offset = 0;

        for (Square org = SQ_A1; org <= SQ_H8; ++org)
        {
            const Bitboard pieceAttacks = (pt != PAWN) ? Attacks::pseudo_attacks_bb(pt, org) : 0;
            const Bitboard whiteAttacks = (pt == PAWN) ? Attacks::pseudo_attacks_bb(WHITE, org) : 0;
            const Bitboard blackAttacks = (pt == PAWN) ? Attacks::pseudo_attacks_bb(BLACK, org) : 0;

            for (Square dst = SQ_A1; dst <= SQ_H8; ++dst)
            {
                const Bitboard attacks = (pt != PAWN) ? pieceAttacks
                                       : (dst < org)  ? blackAttacks
                                                      : whiteAttacks;

                threatLuts.lut2[pt][org][dst] =
                  offset + constexpr_popcount((square_bb(dst) - 1) & attacks);
            }

            if (pt != PAWN)
                offset += constexpr_popcount(pieceAttacks);
            else if (SQ_A2 <= org && org <= SQ_H7)
                offset += constexpr_popcount(whiteAttacks);
        }

        offsets[pt] = offset;
    }

    // Initialize lut1: feature base index and exclusion handling.
    Index cumulativeOffset = 0;

    for (const Color attackerC : {WHITE, BLACK})
        for (const auto attackerPt : PIECE_TYPES)
        {
            const Piece attackerPc = make_piece(attackerC, attackerPt);

            const Index pieceOffset = offsets[attackerPt];
            const Index targetMax   = TARGET_MAX[attackerPt - 1];

            for (const Color attackedC : {WHITE, BLACK})
                for (const auto attackedPt : PIECE_TYPES)
                {
                    const Piece attackedPc = make_piece(attackedC, attackedPt);

                    const auto map = MAP[attackerPt - 1][attackedPt - 1];

                    // Excluded
                    if (map < 0)
                    {
                        threatLuts.lut1[+attackerPc][+attackedPc][0] = Dimensions;
                        threatLuts.lut1[+attackerPc][+attackedPc][1] = Dimensions;
                        continue;
                    }

                    const bool semiExcluded = attackerPt == attackedPt  //
                                           && (attackerC != attackedC || attackerPt != PAWN);

                    const Index feature = cumulativeOffset  //
                                        + (attackedC * targetMax + map) * pieceOffset;

                    threatLuts.lut1[+attackerPc][+attackedPc][0] = feature;
                    threatLuts.lut1[+attackerPc][+attackedPc][1] = semiExcluded  //
                                                                   ? Dimensions
                                                                   : feature;
                }

            cumulativeOffset += 2 * targetMax * pieceOffset;
        }

    assert(cumulativeOffset == Dimensions);

    return threatLuts;
}

constexpr auto THREAT_LUTS = init_threat_luts();

ALWAYS_INLINE constexpr Index threat_index(const Index lut1, const Index lut2) noexcept {
    return lut1 != Dimensions ? lut1 + lut2 : Dimensions;
}

// Index of a feature for a given king position and another piece on square
ALWAYS_INLINE constexpr Index make_index(const Color  perspective,
                                         const Square kingSq,
                                         const Square orgSq,
                                         const Square dstSq,
                                         const Piece  attackerPc,
                                         const Piece  attackedPc) noexcept {
    // Compute perspective-relative squares
    const u8 orient = u8(orientation(kingSq)) ^ (56 * perspective);

    const u8 org = u8(orgSq) ^ orient;
    const u8 dst = u8(dstSq) ^ orient;

    u8       swap     = 8 * perspective;
    unsigned attacker = (+attackerPc) ^ swap;
    unsigned attacked = (+attackedPc) ^ swap;

    return threat_index(THREAT_LUTS.lut1[attacker][attacked][org < dst],
                        THREAT_LUTS.lut2[type_of(attackerPc)][org][dst]);
}

ALWAYS_INLINE void append_pawn_active_indices(Bitboard        attacksBB,
                                              const Direction attackDir,
                                              const Color     perspective,
                                              const Position& pos,
                                              const Square    kingSq,
                                              const Piece     attackerPc,
                                              IndexList&      active) noexcept {
    while (attacksBB != 0)
    {
        const Square dstSq      = pop_lsq(attacksBB);
        const Square orgSq      = dstSq - attackDir;
        const Piece  attackedPc = pos[dstSq];

        active.push_back_if_lt(
          make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc), Dimensions);
    }
}

}  // namespace

void append_active_indices(const Color     perspective,
                           const Position& pos,
                           IndexList&      active) noexcept {
    const Square kingSq = pos.square(perspective, KING);

    const Bitboard occupancyBB = pos.pieces_bb();
    const Bitboard pTargetsBB  = pos.pieces_bb(KNIGHT, ROOK);
    const Bitboard brTargetsBB = pos.pieces_bb(PAWN, BISHOP) | pTargetsBB;
    const Bitboard kqTargetsBB = pos.pieces_bb(QUEEN) | brTargetsBB;

    for (const Color c : {WHITE, BLACK})
    {
        {
            const Piece attackerPc = make_piece(c, PAWN);

            const Bitboard pawnsBB = pos.pieces_bb(c, PAWN);

            const auto lDir = c == WHITE ? Direction::NORTH_WEST : Direction::SOUTH_EAST;
            const auto rDir = c == WHITE ? Direction::NORTH_EAST : Direction::SOUTH_WEST;

            const Bitboard lBB = shift_bb(lDir, pawnsBB) & pTargetsBB;
            const Bitboard rBB = shift_bb(rDir, pawnsBB) & pTargetsBB;

            append_pawn_active_indices(lBB, lDir, perspective, pos, kingSq, attackerPc, active);
            append_pawn_active_indices(rBB, rDir, perspective, pos, kingSq, attackerPc, active);
        }

        for (const auto pt : NON_PAWN_PIECE_TYPES)
        {
            const Piece attackerPc = make_piece(c, pt);

            const Bitboard targetsBB = pt == KNIGHT || pt == QUEEN ? kqTargetsBB : brTargetsBB;

            Bitboard attackerBB = pos.pieces_bb(c, pt);
            while (attackerBB != 0)
            {
                const Square orgSq = pop_lsq(attackerBB);

                Bitboard attacksBB = Attacks::attacks_bb(pt, orgSq, occupancyBB) & targetsBB;
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

void append_changed_indices(const Color               perspective,
                            const Square              kingSq,
                            const DirtyType&          dTs,
                            IndexList&                removed,
                            IndexList&                added,
                            const ThreatWeight* const pfBase,
                            const usize               pfStride) noexcept {
    const unsigned orient    = u8(orientation(kingSq)) ^ (56 * perspective);
    const unsigned pair_swap = 0x88 * perspective;

    const auto* pLut1 = &THREAT_LUTS.lut1[0][0];

    for (const auto& dT : dTs)
    {
        const u32      raw   = dT.raw();
        const unsigned org   = u8(raw >> Threat::SqShift) ^ orient;
        const unsigned dst   = u8(raw >> Threat::ThreatenedSqShift) ^ orient;
        const unsigned pair  = u8(raw >> Threat::ThreatenedPcShift);
        const auto&    pLut2 = THREAT_LUTS.lut2[(pair >> 4) & 7];

        const Index index = threat_index(pLut1[pair ^ pair_swap][org < dst], pLut2[org][dst]);

        if (pfBase != nullptr)
            prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(
              reinterpret_cast<const void*>(reinterpret_cast<uptr>(pfBase) + index * pfStride));

        const bool add = raw >> 31;

        auto& insert = add ? added : removed;

        insert.push_back_if_lt(index, Dimensions);
    }
}

void append_changed_indices_both(const Square                wKingSq,
                                 const Square                bKingSq,
                                 const DirtyType&            dTs,
                                 Array<IndexList, COLOR_NB>& removed,
                                 Array<IndexList, COLOR_NB>& added,
                                 const ThreatWeight* const   pfBase,
                                 const usize                 pfStride) noexcept {

    const unsigned wOrient = u8(orientation(wKingSq));
    const unsigned bOrient = u8(orientation(bKingSq)) ^ 56;

    const auto* pLut1 = &THREAT_LUTS.lut1[0][0];

    for (const auto& dT : dTs)
    {
        const u32      raw   = dT.raw();
        const unsigned org   = u8(raw >> Threat::SqShift);
        const unsigned dst   = u8(raw >> Threat::ThreatenedSqShift);
        const unsigned pair  = u8(raw >> Threat::ThreatenedPcShift);
        const auto&    pLut2 = THREAT_LUTS.lut2[(pair >> 4) & 7];

        const unsigned wOrg = org ^ wOrient;
        const unsigned wDst = dst ^ wOrient;

        const Index wIndex = threat_index(pLut1[pair][wOrg < wDst], pLut2[wOrg][wDst]);

        const unsigned bOrg = org ^ bOrient;
        const unsigned bDst = dst ^ bOrient;

        const Index bIndex = threat_index(pLut1[pair ^ 0x88][bOrg < bDst], pLut2[bOrg][bDst]);

        if (pfBase != nullptr)
        {
            prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(
              reinterpret_cast<const void*>(reinterpret_cast<uptr>(pfBase) + wIndex * pfStride));
            prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(
              reinterpret_cast<const void*>(reinterpret_cast<uptr>(pfBase) + bIndex * pfStride));
        }

        const bool add = raw >> 31;

        auto& wInsert = add ? added[WHITE] : removed[WHITE];
        auto& bInsert = add ? added[BLACK] : removed[BLACK];

        wInsert.push_back_if_lt(wIndex, Dimensions);
        bInsert.push_back_if_lt(bIndex, Dimensions);
    }
}
}  // namespace FullThreats
}  // namespace DON::NNUE::Features
