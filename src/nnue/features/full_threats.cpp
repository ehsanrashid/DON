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

// Definition of input features FullThreats of NNUE evaluation function

#include "full_threats.h"

#include <array>
#include <cassert>
#include <initializer_list>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../common.h"

namespace DON::NNUE::Features {

namespace {

constexpr StdArray<std::uint8_t, PIECE_TYPE_CNT> MAX_TARGETS{6, 12, 10, 10, 12, 8};

constexpr StdArray<std::int8_t, PIECE_TYPE_CNT, PIECE_TYPE_CNT> MAP{{
  {0, +1, -1, +2, -1, -1},  //
  {0, +1, +2, +3, +4, +5},  //
  {0, +1, +2, +3, -1, +4},  //
  {0, +1, +2, +3, -1, +4},  //
  {0, +1, +2, +3, +4, +5},  //
  {0, +1, +2, +3, -1, -1}   //
}};

struct PieceThreat final {
    std::uint32_t threatCount;  // Total number of threats this piece can generate
    std::uint32_t baseOffset;   // Base index in the global threat table for this piece
};

struct ThreatTable final {
    StdArray<PieceThreat, PIECE_NB>              pieceThreats;
    StdArray<std::uint32_t, PIECE_NB, SQUARE_NB> squareOffsets;
};

alignas(CACHE_LINE_SIZE) constexpr auto THREAT_TABLE = []() constexpr noexcept {
    ThreatTable threatTable{};

    std::uint32_t baseOffset = 0;

    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            Piece pc = make_piece(c, pt);

            std::uint32_t threatCount = 0;

            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                threatTable.squareOffsets[+pc][s] = threatCount;

                Bitboard threatsBB = pt != PAWN               ? attacks_bb(s, pt)
                                   : SQ_A2 <= s && s <= SQ_H7 ? attacks_bb(s, c)
                                                              : 0;

                threatCount += constexpr_popcount(threatsBB);
            }

            threatTable.pieceThreats[+pc] = {threatCount, baseOffset};

            baseOffset += MAX_TARGETS[pt - 1] * threatCount;
        }

    return threatTable;
}();

constexpr auto& PIECE_THREATS  = THREAT_TABLE.pieceThreats;
constexpr auto& SQUARE_OFFSETS = THREAT_TABLE.squareOffsets;

constexpr std::uint8_t  EXCLUDED_OFFSET    = 31;
constexpr std::uint32_t EXCLUDED_MASK      = 1U << EXCLUDED_OFFSET;
constexpr std::uint32_t FEATURE_INDEX_MASK = EXCLUDED_MASK - 1U;

// LUT for getting feature base index and exclusion info
// [attackerPc][attackedPc]
alignas(CACHE_LINE_SIZE) constexpr auto LUT_DATAS = []() constexpr noexcept {
    StdArray<std::uint32_t, PIECE_NB, PIECE_NB> lutDatas{};

    for (Color attackerC : {WHITE, BLACK})
        for (PieceType attackerPt : PIECE_TYPES)
        {
            Piece attackerPc = make_piece(attackerC, attackerPt);

            for (Color attackedC : {WHITE, BLACK})
                for (PieceType attackedPt : PIECE_TYPES)
                {
                    Piece attackedPc = make_piece(attackedC, attackedPt);

                    bool enemy = int(attackerPc ^ attackedPc) == 8;

                    int map = MAP[attackerPt - 1][attackedPt - 1];

                    bool excluded = map < 0;

                    if (excluded)
                    {
                        lutDatas[+attackerPc][+attackedPc] = FullThreats::Dimensions;

                        continue;
                    }

                    bool semiExcluded = attackerPt == attackedPt && (enemy || attackerPt != PAWN);

                    std::uint32_t featureIndex =
                      PIECE_THREATS[+attackerPc].baseOffset
                      + PIECE_THREATS[+attackerPc].threatCount
                          * (attackedC * (MAX_TARGETS[attackerPt - 1] / 2) + map);

                    lutDatas[+attackerPc][+attackedPc] =
                      (std::uint32_t(semiExcluded) << EXCLUDED_OFFSET) | featureIndex;
                }
        }

    return lutDatas;
}();

// LUT for getting index within piece threats
// [attackerPt][orgSq][dstSq]
alignas(CACHE_LINE_SIZE) const auto LUT_INDICES = []() noexcept {
    StdArray<std::uint8_t, 1 + PIECE_TYPE_CNT, SQUARE_NB, SQUARE_NB> lutIndices{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            Bitboard s2MaskBB = square_bb(s2) - 1;
            // clang-format off
            lutIndices[WHITE ][s1][s2] = constexpr_popcount(s2MaskBB & attacks_bb(s1, WHITE));
            lutIndices[BLACK ][s1][s2] = constexpr_popcount(s2MaskBB & attacks_bb(s1, BLACK));
            lutIndices[KNIGHT][s1][s2] = constexpr_popcount(s2MaskBB & attacks_bb(s1, KNIGHT));
            lutIndices[BISHOP][s1][s2] = constexpr_popcount(s2MaskBB & attacks_bb(s1, BISHOP));
            lutIndices[ROOK  ][s1][s2] = constexpr_popcount(s2MaskBB & attacks_bb(s1, ROOK));
            lutIndices[QUEEN ][s1][s2] = constexpr_popcount(s2MaskBB & attacks_bb(s1, QUEEN));
            lutIndices[KING  ][s1][s2] = constexpr_popcount(s2MaskBB & attacks_bb(s1, KING));
            // clang-format on
        }

    return lutIndices;
}();

// Get index within piece threats
constexpr std::uint8_t lut_index(Piece pc, Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

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
ALWAYS_INLINE IndexType make_index(Color  perspective,
                                   Square kingSq,
                                   Square orgSq,
                                   Square dstSq,
                                   Piece  attackerPc,
                                   Piece  attackedPc) noexcept {
    int relOrientation = relative_sq(perspective, orientation(kingSq));

    orgSq = Square(int(orgSq) ^ relOrientation);
    dstSq = Square(int(dstSq) ^ relOrientation);

    attackerPc = relative_piece(perspective, attackerPc);
    attackedPc = relative_piece(perspective, attackedPc);

    std::uint32_t lutData = LUT_DATAS[+attackerPc][+attackedPc];

    if (  // Fully-excluded (fast path)
      lutData == FullThreats::Dimensions
      // Semi-excluded && Direction-dependent exclusion
      || (((lutData & EXCLUDED_MASK) >> EXCLUDED_OFFSET) != 0 && orgSq < dstSq))
        return FullThreats::Dimensions;

    // Compute final index
    return (lutData & FEATURE_INDEX_MASK)       //
         + lut_index(attackerPc, orgSq, dstSq)  //
         + SQUARE_OFFSETS[+attackerPc][orgSq];
}

}  // namespace

// Get a list of indices for active features in ascending order
void FullThreats::append_active_indices(Color           perspective,
                                        const Position& pos,
                                        IndexList&      active) noexcept {
    Square kingSq = pos.square<KING>(perspective);

    Bitboard occupancyBB = pos.pieces_bb();

    for (Color color : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            Color    c          = Color(perspective ^ color);
            Piece    attackerPc = make_piece(c, pt);
            Bitboard pcBB       = pos.pieces_bb(c, pt);

            if (pt == PAWN)
            {
                Bitboard lAttacksBB = c == WHITE  //
                                      ? shift_bb<NORTH_EAST>(pcBB)
                                      : shift_bb<SOUTH_WEST>(pcBB);
                lAttacksBB &= occupancyBB;

                auto rDir = c == WHITE ? NORTH_EAST : SOUTH_WEST;

                while (lAttacksBB != 0)
                {
                    Square dstSq      = pop_lsq(lAttacksBB);
                    Square orgSq      = dstSq - rDir;
                    Piece  attackedPc = pos[dstSq];

                    IndexType index =
                      make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

                    if (index < Dimensions)
                        active.push_back(index);
                }

                Bitboard rAttacksBB = c == WHITE  //
                                      ? shift_bb<NORTH_WEST>(pcBB)
                                      : shift_bb<SOUTH_EAST>(pcBB);
                rAttacksBB &= occupancyBB;

                auto lDir = c == WHITE ? NORTH_WEST : SOUTH_EAST;

                while (rAttacksBB != 0)
                {
                    Square dstSq      = pop_lsq(rAttacksBB);
                    Square orgSq      = dstSq - lDir;
                    Piece  attackedPc = pos[dstSq];

                    IndexType index =
                      make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

                    if (index < Dimensions)
                        active.push_back(index);
                }
            }
            else
            {
                while (pcBB != 0)
                {
                    Square   orgSq     = pop_lsq(pcBB);
                    Bitboard attacksBB = attacks_bb(orgSq, pt, occupancyBB);
                    attacksBB &= occupancyBB;

                    while (attacksBB != 0)
                    {
                        Square dstSq      = pop_lsq(attacksBB);
                        Piece  attackedPc = pos[dstSq];

                        IndexType index =
                          make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

                        if (index < Dimensions)
                            active.push_back(index);
                    }
                }
            }
        }
}

// Get a list of indices for recently changed features
void FullThreats::append_changed_indices(Color            perspective,
                                         Square           kingSq,
                                         const DirtyType& dts,
                                         IndexList&       removed,
                                         IndexList&       added,
                                         FusedData*       fusedData,
                                         bool             first) noexcept {
    for (const auto& dt : dts.dtList)
    {
        auto orgSq      = dt.sq();
        auto dstSq      = dt.threatened_sq();
        auto attackerPc = dt.pc();
        auto attackedPc = dt.threatened_pc();
        auto add        = dt.add();

        if (fusedData != nullptr)
        {
            if (orgSq == fusedData->dp2removedSq)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedOriginBB |= dstSq;
                        continue;
                    }
                }
                else if ((fusedData->dp2removedOriginBB & dstSq) != 0)
                    continue;
            }

            if (is_ok(dstSq) && dstSq == fusedData->dp2removedSq)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedTargetBB |= orgSq;
                        continue;
                    }
                }
                else if ((fusedData->dp2removedTargetBB & orgSq) != 0)
                    continue;
            }
        }

        IndexList& changed = add ? added : removed;

        IndexType index = make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

        if (index < Dimensions)
            changed.push_back(index);
    }
}

// Determine if a full refresh is required based on the dirty threats
bool FullThreats::refresh_required(Color perspective, const DirtyType& dts) noexcept {
    return dts.ac == perspective && orientation(dts.kingSq) != orientation(dts.preKingSq);
}

}  // namespace DON::NNUE::Features
