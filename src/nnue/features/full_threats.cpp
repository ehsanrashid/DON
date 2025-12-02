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
#include <initializer_list>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace DON::NNUE::Features {

namespace {

constexpr StdArray<int, PIECE_TYPE_NB - 2, PIECE_TYPE_NB - 2> Map{{
  {0, +1, -1, +2, -1, -1},  //
  {0, +1, +2, +3, +4, +5},  //
  {0, +1, +2, +3, -1, +4},  //
  {0, +1, +2, +3, -1, +4},  //
  {0, +1, +2, +3, +4, +5},  //
  {0, +1, +2, +3, -1, -1}   //
}};

// Lookup array for indexing threats
StdArray<IndexType, PIECE_NB, SQUARE_NB> Offsets;

struct ExtraOffset final {
   public:
    IndexType cumulativePieceOffset;
    IndexType cumulativeOffset;
};

StdArray<ExtraOffset, PIECE_NB> ExtraOffsets;

// Information on a particular pair of pieces and whether they should be excluded
struct PiecePairData final {
   public:
    PiecePairData() {}
    PiecePairData(IndexType featureBaseIndex, bool excluded, bool semiExcluded) noexcept {
        data = (featureBaseIndex << 8) | (excluded << 1) | (semiExcluded && !excluded);
    }

    // lsb: excluded if orgSq < dstSq; 2nd lsb: always excluded
    std::uint8_t excluded_pair_info() const { return (data >> 0) & 0xFF; }
    IndexType    feature_base_index() const { return (data >> 8); }

    // Layout: bits 8..31 are the index contribution of this piece pair, bits 0 and 1 are exclusion info
    std::uint32_t data;
};

// The final index is calculated from summing data found in these two LUTs,
// as well as Offsets[attacker][from]
StdArray<PiecePairData, PIECE_NB, PIECE_NB>            LutData;   // [attacker][attacked]
StdArray<std::uint8_t, PIECE_NB, SQUARE_NB, SQUARE_NB> LutIndex;  // [attacker][orgSq][dstSq]

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

    auto& piecePairData = LutData[attacker][attacked];

    // Some threats imply the existence of the corresponding ones in the opposite direction.
    // Filter them here to ensure only one such threat is active.

    // In the below addition, the 2nd lsb gets set iff either the pair is always excluded,
    // or the pair is semi-excluded and orgSq < dstSq.
    // By using an unsigned compare, the following sequence can use an add-with-carry instruction.
    if ((piecePairData.excluded_pair_info() + (orgSq < dstSq)) & 0x2)
        return FullThreats::Dimensions;

    return piecePairData.feature_base_index() + LutIndex[attacker][orgSq][dstSq]
         + Offsets[attacker][orgSq];
}

}  // namespace

void FullThreats::init() noexcept {

    constexpr StdArray<int, PIECE_TYPE_NB> MaxTargets{0, 6, 12, 10, 10, 12, 8, 0};

    IndexType cumulativeOffset = 0;

    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PIECE_TYPES)
        {
            Piece pc = make_piece(c, pt);

            IndexType cumulativePieceOffset = 0;

            for (Square orgSq = SQ_A1; orgSq <= SQ_H8; ++orgSq)
            {
                Offsets[pc][orgSq] = cumulativePieceOffset;

                Bitboard attacksBB = 0;

                if (pt != PAWN)
                    attacksBB = attacks_bb(orgSq, pt, 0);
                else if (SQ_A2 <= orgSq && orgSq <= SQ_H7)
                    attacksBB = c == WHITE ? pawn_attacks_bb<WHITE>(square_bb(orgSq))
                                           : pawn_attacks_bb<BLACK>(square_bb(orgSq));

                cumulativePieceOffset += popcount(attacksBB);
            }

            ExtraOffsets[pc] = {cumulativePieceOffset, cumulativeOffset};

            cumulativeOffset += MaxTargets[pt] * cumulativePieceOffset;
        }

    // Initialize Lut data & index
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

                    IndexType featureBaseIndex = ExtraOffsets[attackerPc].cumulativeOffset
                                               + (attackedC * (MaxTargets[attackerPt] / 2) + map)
                                                   * ExtraOffsets[attackerPc].cumulativePieceOffset;

                    bool excluded     = map < 0;
                    bool semiExcluded = attackerPt == attackedPt  //
                                     && (enemy || attackerPt != PAWN);

                    LutData[attackerPc][attackedPc] =
                      PiecePairData(featureBaseIndex, excluded, semiExcluded);
                }

            for (Square orgSq = SQ_A1; orgSq <= SQ_H8; ++orgSq)
                for (Square dstSq = SQ_A1; dstSq <= SQ_H8; ++dstSq)
                {
                    Bitboard attacksBB = attacks_bb(orgSq, attackerPc);
                    LutIndex[attackerPc][orgSq][dstSq] =
                      popcount((square_bb(dstSq) - 1) & attacksBB);
                }
        }
}

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
                Bitboard lAttacks =
                  (c == WHITE ? shift_bb<NORTH_EAST>(pcBB) : shift_bb<SOUTH_WEST>(pcBB))
                  & occupancyBB;
                auto rDir = c == WHITE ? NORTH_EAST : SOUTH_WEST;
                while (lAttacks != 0)
                {
                    Square dstSq    = pop_lsq(lAttacks);
                    Square orgSq    = dstSq - rDir;
                    Piece  attacked = pos[dstSq];

                    IndexType index =
                      make_index(perspective, kingSq, orgSq, dstSq, attackerPc, attacked);
                    if (index < Dimensions)
                        active.push_back(index);
                }

                Bitboard rAttacks =
                  (c == WHITE ? shift_bb<NORTH_WEST>(pcBB) : shift_bb<SOUTH_EAST>(pcBB))
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
