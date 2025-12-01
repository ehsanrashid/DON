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

struct HelperOffset {
    IndexType cumulativePieceOffset, cumulativeOffset;
};

StdArray<HelperOffset, PIECE_NB> HelperOffsets;

// Information on a particular pair of pieces and whether they should be excluded
struct PiecePairData final {
   public:
    PiecePairData() {}
    PiecePairData(IndexType featureBaseIndex, bool excluded, bool semiExcluded) noexcept {
        data = (featureBaseIndex << 8) | (excluded << 1) | (semiExcluded && !excluded);
    }

    // lsb: excluded if org < dst; 2nd lsb: always excluded
    std::uint8_t excluded_pair_info() const { return (data >> 0) & 0xFF; }
    IndexType    feature_base_index() const { return (data >> 8); }

    // Layout: bits 8..31 are the index contribution of this piece pair, bits 0 and 1 are exclusion info
    std::uint32_t data;
};

// The final index is calculated from summing data found in these two LUTs,
// as well as Offsets[attacker][from]
StdArray<PiecePairData, PIECE_NB, PIECE_NB>            LutData;   // [attacker][attacked]
StdArray<std::uint8_t, PIECE_NB, SQUARE_NB, SQUARE_NB> LutIndex;  // [attacker][org][dst]

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
                                   Square org,
                                   Square dst,
                                   Piece  attacker,
                                   Piece  attacked) noexcept {
    int relOrientation = relative_sq(perspective, orientation(kingSq));

    org = Square(int(org) ^ relOrientation);
    dst = Square(int(dst) ^ relOrientation);

    attacker = relative_piece(perspective, attacker);
    attacked = relative_piece(perspective, attacked);

    auto& piecePairData = LutData[attacker][attacked];

    // Some threats imply the existence of the corresponding ones in the opposite direction.
    // Filter them here to ensure only one such threat is active.

    // In the below addition, the 2nd lsb gets set iff either the pair is always excluded,
    // or the pair is semi-excluded and org < dst. By using an unsigned compare, the following
    // sequence can use an add-with-carry instruction.
    if ((piecePairData.excluded_pair_info() + (org < dst)) & 0x2)
        return FullThreats::Dimensions;

    return piecePairData.feature_base_index() + LutIndex[attacker][org][dst]
         + Offsets[attacker][org];
}

}  // namespace

void FullThreats::init() noexcept {

    constexpr StdArray<int, PIECE_TYPE_NB> MaxTargets{0, 6, 12, 10, 10, 12, 8, 0};

    IndexType cumulativeOffset = 0;

    for (Color c : {WHITE, BLACK})
        for (PieceType pt : PieceTypes)
        {
            auto pc = make_piece(c, pt);

            IndexType cumulativePieceOffset = 0;

            for (Square org = SQ_A1; org <= SQ_H8; ++org)
            {
                Offsets[pc][org] = cumulativePieceOffset;

                if (pt != PAWN)
                {
                    Bitboard attacks = attacks_bb(org, pc, 0);
                    cumulativePieceOffset += popcount(attacks);
                }
                else if (SQ_A2 <= org && org <= SQ_H7)
                {
                    Bitboard attacks = c == WHITE ? pawn_attacks_bb<WHITE>(square_bb(org))
                                                  : pawn_attacks_bb<BLACK>(square_bb(org));
                    cumulativePieceOffset += popcount(attacks);
                }
            }

            HelperOffsets[pc] = {cumulativePieceOffset, cumulativeOffset};

            cumulativeOffset += MaxTargets[pt] * cumulativePieceOffset;
        }

    // Initialize Lut data & index
    for (Color attackerC : {WHITE, BLACK})
        for (PieceType attackerType : PieceTypes)
        {
            auto attacker = make_piece(attackerC, attackerType);

            for (Color attackedC : {WHITE, BLACK})
                for (PieceType attackedType : PieceTypes)
                {
                    auto attacked = make_piece(attackedC, attackedType);

                    bool enemy = (attacker ^ attacked) == 8;

                    int map = Map[attackerType - 1][attackedType - 1];

                    IndexType feature = HelperOffsets[attacker].cumulativeOffset
                                      + (attackedC * (MaxTargets[attackerType] / 2) + map)
                                          * HelperOffsets[attacker].cumulativePieceOffset;

                    bool excluded     = map < 0;
                    bool semiExcluded = attackerType == attackedType  //
                                     && (enemy || attackerType != PAWN);

                    LutData[attacker][attacked] = PiecePairData(feature, excluded, semiExcluded);
                }

            for (Square org = SQ_A1; org <= SQ_H8; ++org)
                for (Square dst = SQ_A1; dst <= SQ_H8; ++dst)
                    LutIndex[attacker][org][dst] =
                      popcount((square_bb(dst) - 1) & attacks_bb(org, attacker));
        }
}

// Get a list of indices for active features in ascending order
void FullThreats::append_active_indices(Color           perspective,
                                        const Position& pos,
                                        IndexList&      active) noexcept {
    Square kingSq = pos.square<KING>(perspective);

    Bitboard occupied = pos.pieces();
    for (Color color : {WHITE, BLACK})
        for (PieceType pt : PieceTypes)
        {
            Color    c        = Color(perspective ^ color);
            Piece    attacker = make_piece(c, pt);
            Bitboard bb       = pos.pieces(c, pt);

            if (pt == PAWN)
            {
                Bitboard lAttacks =
                  (c == WHITE ? shift_bb<NORTH_EAST>(bb) : shift_bb<SOUTH_WEST>(bb)) & occupied;
                auto rDir = c == WHITE ? NORTH_EAST : SOUTH_WEST;
                while (lAttacks)
                {
                    Square dst      = pop_lsb(lAttacks);
                    Square org      = dst - rDir;
                    Piece  attacked = pos[dst];

                    IndexType index = make_index(perspective, kingSq, org, dst, attacker, attacked);
                    if (index < Dimensions)
                        active.push_back(index);
                }

                Bitboard rAttacks =
                  (c == WHITE ? shift_bb<NORTH_WEST>(bb) : shift_bb<SOUTH_EAST>(bb)) & occupied;
                auto lDir = c == WHITE ? NORTH_WEST : SOUTH_EAST;
                while (rAttacks)
                {
                    Square dst      = pop_lsb(rAttacks);
                    Square org      = dst - lDir;
                    Piece  attacked = pos[dst];

                    IndexType index = make_index(perspective, kingSq, org, dst, attacker, attacked);
                    if (index < Dimensions)
                        active.push_back(index);
                }
            }
            else
            {
                while (bb)
                {
                    Square   org     = pop_lsb(bb);
                    Bitboard attacks = attacks_bb(org, pt, occupied) & occupied;

                    while (attacks)
                    {
                        Square dst      = pop_lsb(attacks);
                        Piece  attacked = pos[dst];

                        IndexType index =
                          make_index(perspective, kingSq, org, dst, attacker, attacked);
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
        auto org      = dirty.sq();
        auto dst      = dirty.threatened_sq();
        auto attacker = dirty.pc();
        auto attacked = dirty.threatened_pc();
        auto add      = dirty.add();

        if (fusedData != nullptr)
        {
            if (org == fusedData->dp2removedSq)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedOriginBB |= dst;
                        continue;
                    }
                }
                else if (fusedData->dp2removedOriginBB & dst)
                    continue;
            }

            if (is_ok(dst) && dst == fusedData->dp2removedSq)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedTargetBB |= org;
                        continue;
                    }
                }
                else if (fusedData->dp2removedTargetBB & org)
                    continue;
            }
        }

        IndexType index = make_index(perspective, kingSq, org, dst, attacker, attacked);
        if (index < Dimensions)
            (add ? added : removed).push_back(index);
    }
}

bool FullThreats::requires_refresh(Color perspective, const DirtyType& dt) noexcept {
    return dt.ac == perspective && orientation(dt.kingSq) != orientation(dt.preKingSq);
}

}  // namespace DON::NNUE::Features
