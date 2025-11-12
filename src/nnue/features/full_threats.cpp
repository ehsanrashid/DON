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
// Information on a particular pair of pieces and whether they should be excluded
struct PiecePairData final {
   public:
    PiecePairData() {}
    PiecePairData(IndexType featureIndexBase, bool excluded, bool semiExcluded) noexcept {
        data = (featureIndexBase << 8) | (excluded << 1) | (semiExcluded && !excluded);
    }

    // lsb: excluded if from < dst; 2nd lsb: always excluded
    uint8_t   excluded_pair_info() const { return (data >> 0) & 0xFF; }
    IndexType feature_index_base() const { return (data >> 8); }

    // Layout: bits 8..31 are the index contribution of this piece pair, bits 0 and 1 are exclusion info
    uint32_t data;
};

// Lookup array for indexing threats
StdArray<IndexType, PIECE_NB, SQUARE_NB + 2> Offsets;

// The final index is calculated from summing data found in these two LUTs,
// as well as Offsets[attacker][from]
StdArray<PiecePairData, PIECE_NB, PIECE_NB>       Lut1Index;  // [attacker][attacked]
StdArray<uint8_t, PIECE_NB, SQUARE_NB, SQUARE_NB> Lut2Index;  // [attacker][org][dst]

}  // namespace

void FullThreats::init() noexcept {
    constexpr StdArray<int, PIECE_NB> TargetCount{0, 6, 12, 10, 10, 12, 8, 0,
                                                  0, 6, 12, 10, 10, 12, 8, 0};

    int cumulativeOffset = 0;
    for (Color c : {WHITE, BLACK})
        for (Piece pc : Pieces[c])
        {
            int cumulativePieceOffset = 0;

            for (Square org = SQ_A1; org <= SQ_H8; ++org)
            {
                Offsets[pc][org] = cumulativePieceOffset;

                if (type_of(pc) != PAWN)
                {
                    Bitboard attacks = attacks_bb(org, pc, 0);
                    cumulativePieceOffset += popcount(attacks);
                }
                else if (SQ_A2 <= org && org <= SQ_H7)
                {
                    Bitboard attacks = color_of(pc) == WHITE
                                       ? pawn_attacks_bb<WHITE>(square_bb(org))
                                       : pawn_attacks_bb<BLACK>(square_bb(org));
                    cumulativePieceOffset += popcount(attacks);
                }
            }

            Offsets[pc][64] = cumulativePieceOffset;
            Offsets[pc][65] = cumulativeOffset;

            cumulativeOffset += TargetCount[pc] * cumulativePieceOffset;
        }

    // Initialize lut index
    for (Color attackerC : {WHITE, BLACK})
        for (Piece attacker : Pieces[attackerC])
        {
            auto attackerType = type_of(attacker);

            for (Color attackedC : {WHITE, BLACK})
                for (Piece attacked : Pieces[attackedC])
                {
                    bool enemy        = (attacker ^ attacked) == 8;
                    auto attackedType = type_of(attacked);

                    int map = Map[attackerType - 1][attackedType - 1];

                    bool excluded = map < 0;
                    bool semiExcluded =
                      attackerType == attackedType && (enemy || attackerType != PAWN);
                    IndexType feature =
                      Offsets[attacker][65]
                      + (attackedC * (TargetCount[attacker] / 2) + map) * Offsets[attacker][64];

                    Lut1Index[attacker][attacked] = PiecePairData(feature, excluded, semiExcluded);
                }
        }

    for (Color c : {WHITE, BLACK})
        for (Piece attacker : Pieces[c])
        {
            for (Square org = SQ_A1; org <= SQ_H8; ++org)
                for (Square dst = SQ_A1; dst <= SQ_H8; ++dst)
                    Lut2Index[attacker][org][dst] =
                      popcount((square_bb(dst) - 1) & attacks_bb(org, attacker));
        }
}

// Index of a feature for a given king position and another piece on some square
IndexType FullThreats::make_index(Color  perspective,
                                  Piece  attacker,
                                  Square org,
                                  Square dst,
                                  Piece  attacked,
                                  Square kingSq) noexcept {

    org = Square(int(org) ^ OrientTBL[perspective][kingSq]);
    dst = Square(int(dst) ^ OrientTBL[perspective][kingSq]);

    if (perspective == BLACK)
    {
        attacker = flip_color(attacker);
        attacked = flip_color(attacked);
    }

    auto& piecePairData = Lut1Index[attacker][attacked];

    // Some threats imply the existence of the corresponding ones in the opposite direction.
    // Filter them here to ensure only one such threat is active.

    // In the below addition, the 2nd lsb gets set iff either the pair is always excluded,
    // or the pair is semi-excluded and org < dst. By using an unsigned compare, the following
    // sequence can use an add-with-carry instruction.
    if ((piecePairData.excluded_pair_info() + (org < dst)) & 0x2)
        return Dimensions;

    IndexType index =
      piecePairData.feature_index_base() + Offsets[attacker][org] + Lut2Index[attacker][org][dst];

    ASSUME(index != Dimensions);
    return index;
}

// Get a list of indices for active features in ascending order
void FullThreats::append_active_indices(Color           perspective,
                                        const Position& pos,
                                        IndexList&      active) noexcept {
    constexpr StdArray<Color, 2, 2> Order{{{WHITE, BLACK}, {BLACK, WHITE}}};

    Square kingSq = pos.king_sq(perspective);

    Bitboard occupied = pos.pieces();
    for (Color color : {WHITE, BLACK})
        for (Piece pc : Pieces[color])
        {
            Color    c        = Order[perspective][color];
            Piece    attacker = make_piece(c, type_of(pc));
            Bitboard bb       = pos.pieces(c, type_of(pc));

            if (type_of(pc) == PAWN)
            {
                Bitboard lAttacks =
                  (c == WHITE ? shift_bb<NORTH_EAST>(bb) : shift_bb<SOUTH_WEST>(bb)) & occupied;
                auto rDir = c == WHITE ? NORTH_EAST : SOUTH_WEST;
                while (lAttacks)
                {
                    Square    dst      = pop_lsb(lAttacks);
                    Square    org      = dst - rDir;
                    Piece     attacked = pos.piece_on(dst);
                    IndexType index = make_index(perspective, attacker, org, dst, attacked, kingSq);

                    if (index < Dimensions)
                        active.push_back(index);
                }

                Bitboard rAttacks =
                  (c == WHITE ? shift_bb<NORTH_WEST>(bb) : shift_bb<SOUTH_EAST>(bb)) & occupied;
                auto lDir = c == WHITE ? NORTH_WEST : SOUTH_EAST;
                while (rAttacks)
                {
                    Square    dst      = pop_lsb(rAttacks);
                    Square    org      = dst - lDir;
                    Piece     attacked = pos.piece_on(dst);
                    IndexType index = make_index(perspective, attacker, org, dst, attacked, kingSq);

                    if (index < Dimensions)
                        active.push_back(index);
                }
            }
            else
            {
                while (bb)
                {
                    Square   org     = pop_lsb(bb);
                    Bitboard attacks = attacks_bb(org, type_of(pc), occupied) & occupied;

                    while (attacks)
                    {
                        Square    dst      = pop_lsb(attacks);
                        Piece     attacked = pos.piece_on(dst);
                        IndexType index =
                          make_index(perspective, attacker, org, dst, attacked, kingSq);

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
        auto attacker = dirty.pc();
        auto attacked = dirty.threatened_pc();
        auto org      = dirty.sq();
        auto dst      = dirty.threatened_sq();
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
                        fusedData->dp2removedTargetBB |= square_bb(org);
                        continue;
                    }
                }
                else if (fusedData->dp2removedTargetBB & square_bb(org))
                    continue;
            }
        }

        IndexType index = make_index(perspective, attacker, org, dst, attacked, kingSq);

        if (index != Dimensions)
            (add ? added : removed).push_back(index);
    }
}

bool FullThreats::requires_refresh(Color perspective, const DirtyType& dt) noexcept {
    return perspective == dt.ac && OrientTBL[dt.ac][dt.kingSq] != OrientTBL[dt.ac][dt.preKingSq];
}

}  // namespace DON::NNUE::Features
