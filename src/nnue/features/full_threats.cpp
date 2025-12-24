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

constexpr StdArray<int, 1 + PIECE_CNT> MaxTargets{0, 6, 12, 10, 10, 12, 8};

constexpr StdArray<int, PIECE_CNT, PIECE_CNT> Map{{
  {0, +1, -1, +2, -1, -1},  //
  {0, +1, +2, +3, +4, +5},  //
  {0, +1, +2, +3, -1, +4},  //
  {0, +1, +2, +3, -1, +4},  //
  {0, +1, +2, +3, +4, +5},  //
  {0, +1, +2, +3, -1, -1}   //
}};

struct PieceThreat final {
    IndexType threatCount;  // Total number of threats this piece can generate
    IndexType baseOffset;   // Base index in the global threat table for this piece
};

struct ThreatTable final {
    StdArray<PieceThreat, PIECE_NB>          pieceThreats;
    StdArray<IndexType, PIECE_NB, SQUARE_NB> squareOffsets;
};

alignas(CACHE_LINE_SIZE) constexpr auto THREAT_TABLE = []() constexpr noexcept {
    ThreatTable threatTable{};

    IndexType baseOffset = 0;

    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            Piece pc = make_piece(c, pt);

            IndexType threatCount = 0;

            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                threatTable.squareOffsets[pc][s] = threatCount;

                Bitboard threatsBB = pt != PAWN               ? attacks_bb(s, pt)
                                   : SQ_A2 <= s && s <= SQ_H7 ? attacks_bb(s, c)
                                                              : 0;

                threatCount += constexpr_popcount(threatsBB);
            }

            threatTable.pieceThreats[pc] = {threatCount, baseOffset};

            baseOffset += MaxTargets[pt] * threatCount;
        }

    return threatTable;
}();

constexpr auto& PIECE_THREATS  = THREAT_TABLE.pieceThreats;
constexpr auto& SQUARE_OFFSETS = THREAT_TABLE.squareOffsets;

// Information on a particular pair of pieces and whether they should be excluded
struct PiecePairData final {
   public:
    constexpr PiecePairData() noexcept :
        data(0) {}
    constexpr PiecePairData(IndexType featureBaseIndex, bool excluded, bool semiExcluded) noexcept :
        data((featureBaseIndex << 8) | (excluded << 1) | (semiExcluded && !excluded)) {}

    // lsb: excluded if orgSq < dstSq; 2nd lsb: always excluded
    constexpr std::uint8_t excluded_pair_info() const noexcept { return (data >> 0) & 0xFF; }
    constexpr IndexType    feature_base_index() const noexcept { return (data >> 8); }

    // Layout: bits 8..31 are the index contribution of this piece pair, bits 0 and 1 are exclusion info
    std::uint32_t data;
};

alignas(CACHE_LINE_SIZE) constexpr auto LUT_DATAS = []() constexpr noexcept {
    StdArray<PiecePairData, PIECE_NB, PIECE_NB> lutDatas{};

    for (Color attackerC : {WHITE, BLACK})
        for (PieceType attackerPt : PIECE_TYPES)
        {
            Piece attackerPc = make_piece(attackerC, attackerPt);

            for (Color attackedC : {WHITE, BLACK})
                for (PieceType attackedPt : PIECE_TYPES)
                {
                    Piece attackedPc = make_piece(attackedC, attackedPt);

                    bool enemy = (attackerPc ^ attackedPc) == 8;

                    int map = Map[attackerPt - 1][attackedPt - 1];

                    IndexType featureBaseIndex = PIECE_THREATS[attackerPc].baseOffset
                                               + (attackedC * (MaxTargets[attackerPt] / 2) + map)
                                                   * PIECE_THREATS[attackerPc].threatCount;
                    bool excluded     = map < 0;
                    bool semiExcluded = attackerPt == attackedPt && (enemy || attackerPt != PAWN);

                    lutDatas[attackerPc][attackedPc] =
                      PiecePairData(featureBaseIndex, excluded, semiExcluded);
                }
        }

    return lutDatas;
}();

alignas(CACHE_LINE_SIZE) const auto LUT_INDICES = []() noexcept {
    StdArray<std::uint8_t, SQUARE_NB, SQUARE_NB, 1 + PIECE_CNT> lutIndices{};

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            Bitboard s2MaskBB = square_bb(s2) - 1;
            // clang-format off
            lutIndices[s1][s2][WHITE ] = constexpr_popcount(s2MaskBB & attacks_bb(s1, WHITE));
            lutIndices[s1][s2][BLACK ] = constexpr_popcount(s2MaskBB & attacks_bb(s1, BLACK));
            lutIndices[s1][s2][KNIGHT] = constexpr_popcount(s2MaskBB & attacks_bb(s1, KNIGHT));
            lutIndices[s1][s2][BISHOP] = constexpr_popcount(s2MaskBB & attacks_bb(s1, BISHOP));
            lutIndices[s1][s2][ROOK  ] = constexpr_popcount(s2MaskBB & attacks_bb(s1, ROOK));
            lutIndices[s1][s2][QUEEN ] = constexpr_popcount(s2MaskBB & attacks_bb(s1, QUEEN));
            lutIndices[s1][s2][KING  ] = constexpr_popcount(s2MaskBB & attacks_bb(s1, KING));
            // clang-format on
        }

    return lutIndices;
}();

constexpr std::uint8_t lut_index(Piece pc, Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));

    if (type_of(pc) == PAWN)
        return LUT_INDICES[s1][s2][color_of(pc)];

    return LUT_INDICES[s1][s2][type_of(pc)];
}

// The final index is calculated from summing data found in 2 LUTs,
// as well as SQUARE_OFFSETS[attacker][orgSq]

// (file_of(s) >> 2) is 0 for 0...3, 1 for 4...7
constexpr Square orientation(Square s) noexcept {
    return Square(((file_of(s) >> 2) ^ 0) * int(FILE_H));
}

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
                                   Piece  attacker,
                                   Piece  attacked) noexcept {
    int relOrientation = relative_sq(perspective, orientation(kingSq));

    orgSq = Square(int(orgSq) ^ relOrientation);
    dstSq = Square(int(dstSq) ^ relOrientation);

    attacker = relative_piece(perspective, attacker);
    attacked = relative_piece(perspective, attacked);

    const auto& piecePairData = LUT_DATAS[attacker][attacked];

    // Some threats imply the existence of the corresponding ones in the opposite direction.
    // Filter them here to ensure only one such threat is active.

    // In the below addition, the 2nd lsb gets set iff either the pair is always excluded,
    // or the pair is semi-excluded and orgSq < dstSq.
    // By using an unsigned compare, the following sequence can use an add-with-carry instruction.
    if (((piecePairData.excluded_pair_info() + int(orgSq < dstSq)) & 0x2) != 0)
        return FullThreats::Dimensions;

    return piecePairData.feature_base_index() + SQUARE_OFFSETS[attacker][orgSq]
         + lut_index(attacker, orgSq, dstSq);
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
                Bitboard lAttacks = (c == WHITE  //
                                       ? shift_bb<NORTH_EAST>(pcBB)
                                       : shift_bb<SOUTH_WEST>(pcBB))
                                  & occupancyBB;

                auto rDir = c == WHITE ? NORTH_EAST : SOUTH_WEST;

                while (lAttacks != 0)
                {
                    Square dstSq      = pop_lsq(lAttacks);
                    Square orgSq      = dstSq - rDir;
                    Piece  attackedPc = pos[dstSq];

                    IndexType index =
                      make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);

                    if (index < Dimensions)
                        active.push_back(index);
                }

                Bitboard rAttacks = (c == WHITE  //
                                       ? shift_bb<NORTH_WEST>(pcBB)
                                       : shift_bb<SOUTH_EAST>(pcBB))
                                  & occupancyBB;

                auto lDir = c == WHITE ? NORTH_WEST : SOUTH_EAST;

                while (rAttacks != 0)
                {
                    Square dstSq      = pop_lsq(rAttacks);
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
                    Bitboard attacksBB = attacks_bb(orgSq, pt, occupancyBB) & occupancyBB;

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
                                         const DirtyType& dt,
                                         IndexList&       removed,
                                         IndexList&       added,
                                         FusedData*       fusedData,
                                         bool             first) noexcept {
    for (const auto& dirty : dt.list)
    {
        auto orgSq      = dirty.sq();
        auto dstSq      = dirty.threatened_sq();
        auto attackerPc = dirty.pc();
        auto attackedPc = dirty.threatened_pc();
        auto add        = dirty.add();

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
                else if (fusedData->dp2removedOriginBB & dstSq)
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
                else if (fusedData->dp2removedTargetBB & orgSq)
                    continue;
            }
        }

        IndexType index = make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attackedPc);
        if (index < Dimensions)
            (add ? added : removed).push_back(index);
    }
}

bool FullThreats::requires_refresh(Color perspective, const DirtyType& dt) noexcept {
    return dt.ac == perspective && orientation(dt.kingSq) != orientation(dt.preKingSq);
}

}  // namespace DON::NNUE::Features
