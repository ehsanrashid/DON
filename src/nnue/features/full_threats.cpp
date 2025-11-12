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

#include <initializer_list>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../nnue_common.h"

namespace DON::NNUE::Features {

// Lookup array for indexing threats
IndexType offsets[PIECE_NB][SQUARE_NB + 2];

// Information on a particular pair of pieces and whether they should be excluded
struct PiecePairData final {
   public:
    PiecePairData() {}
    PiecePairData(bool excluded, bool semiExcluded, IndexType featureIndexBase) {
        data = (excluded << 1) | (semiExcluded && !excluded) | (featureIndexBase << 8);
    }
    // lsb: excluded if from < dst; 2nd lsb: always excluded
    uint8_t   excluded_pair_info() const { return data; }
    IndexType feature_index_base() const { return data >> 8; }

    // Layout: bits 8..31 are the index contribution of this piece pair, bits 0 and 1 are exclusion info
    uint32_t data;
};

constexpr StdArray<Piece, 12> Pieces{
  W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
};

// The final index is calculated from summing data found in these two LUTs,
// as well as offsets[attacker][from]
PiecePairData lut1Index[PIECE_NB][PIECE_NB];              // [attacker][attacked]
uint8_t       lut2Index[PIECE_NB][SQUARE_NB][SQUARE_NB];  // [attacker][org][dst]

static void init_index_luts() {
    for (Piece attacker : Pieces)
    {
        for (Piece attacked : Pieces)
        {
            bool      enemy        = (attacker ^ attacked) == 8;
            PieceType attackerType = type_of(attacker);
            PieceType attackedType = type_of(attacked);

            int map = FullThreats::map[attackerType - 1][attackedType - 1];

            bool excluded     = map < 0;
            bool semiExcluded = attackerType == attackedType && (enemy || attackerType != PAWN);
            IndexType feature = offsets[attacker][65]
                              + (color_of(attacked) * (numValidTargets[attacker] / 2) + map)
                                  * offsets[attacker][64];

            lut1Index[attacker][attacked] = PiecePairData(excluded, semiExcluded, feature);
        }
    }

    for (Piece attacker : Pieces)
    {
        for (Square org = SQ_A1; org <= SQ_H8; ++org)
            for (Square dst = SQ_A1; dst <= SQ_H8; ++dst)
                lut2Index[attacker][org][dst] =
                  popcount((square_bb(dst) - 1) & attacks_bb(org, attacker));
    }
}

void init_threat_offsets() {
    int cumulativeOffset = 0;
    for (Piece piece : Pieces)
    {
        int pieceIdx              = piece;
        int cumulativePieceOffset = 0;

        for (Square org = SQ_A1; org <= SQ_H8; ++org)
        {
            offsets[pieceIdx][org] = cumulativePieceOffset;

            if (type_of(piece) != PAWN)
            {
                Bitboard attacks = attacks_bb(org, piece, 0);
                cumulativePieceOffset += popcount(attacks);
            }
            else if (SQ_A2 <= org && org <= SQ_H7)
            {
                Bitboard attacks = (pieceIdx < 8) ? pawn_attacks_bb<WHITE>(square_bb(org))
                                                  : pawn_attacks_bb<BLACK>(square_bb(org));
                cumulativePieceOffset += popcount(attacks);
            }
        }

        offsets[pieceIdx][64] = cumulativePieceOffset;
        offsets[pieceIdx][65] = cumulativeOffset;

        cumulativeOffset += numValidTargets[pieceIdx] * cumulativePieceOffset;
    }

    init_index_luts();
}

// Index of a feature for a given king position and another piece on some square
template<Color Perspective>
IndexType
FullThreats::make_index(Piece attacker, Square org, Square dst, Piece attacked, Square ksq) {
    org = (Square) (int(org) ^ OrientTBL[Perspective][ksq]);
    dst = (Square) (int(dst) ^ OrientTBL[Perspective][ksq]);

    if (Perspective == BLACK)
    {
        attacker = flip_color(attacker);
        attacked = flip_color(attacked);
    }

    auto piecePairData = lut1Index[attacker][attacked];

    // Some threats imply the existence of the corresponding ones in the opposite direction.
    // Filter them here to ensure only one such threat is active.

    // In the below addition, the 2nd lsb gets set iff either the pair is always excluded,
    // or the pair is semi-excluded and org < dst. By using an unsigned compare, the following
    // sequence can use an add-with-carry instruction.
    if ((piecePairData.excluded_pair_info() + (org < dst)) & 2)
        return Dimensions;

    IndexType index =
      piecePairData.feature_index_base() + offsets[attacker][org] + lut2Index[attacker][org][dst];

    ASSUME(index != Dimensions);
    return index;
}

// Get a list of indices for active features in ascending order
template<Color Perspective>
void FullThreats::append_active_indices(const Position& pos, IndexList& active) {
    static constexpr Color order[2][2] = {{WHITE, BLACK}, {BLACK, WHITE}};

    Square   ksq      = pos.square<KING>(Perspective);
    Bitboard occupied = pos.pieces();

    for (Color color : {WHITE, BLACK})
    {
        for (PieceType pt = PAWN; pt <= KING; ++pt)
        {
            Color    c        = order[Perspective][color];
            Piece    attacker = make_piece(c, pt);
            Bitboard bb       = pos.pieces(c, pt);

            if (pt == PAWN)
            {
                auto rDir = c == WHITE ? NORTH_EAST : SOUTH_WEST;
                auto lDir = c == WHITE ? NORTH_WEST : SOUTH_EAST;

                Bitboard lAttacks =
                  (c == WHITE ? shift_bb<NORTH_EAST>(bb) : shift_bb<SOUTH_WEST>(bb)) & occupied;
                Bitboard rAttacks =
                  (c == WHITE ? shift_bb<NORTH_WEST>(bb) : shift_bb<SOUTH_EAST>(bb)) & occupied;

                while (lAttacks)
                {
                    Square    dst      = pop_lsb(lAttacks);
                    Square    org      = dst - rDir;
                    Piece     attacked = pos.piece_on(dst);
                    IndexType index    = make_index<Perspective>(attacker, org, dst, attacked, ksq);

                    if (index < Dimensions)
                        active.push_back(index);
                }

                while (rAttacks)
                {
                    Square    dst      = pop_lsb(rAttacks);
                    Square    org      = dst - lDir;
                    Piece     attacked = pos.piece_on(dst);
                    IndexType index    = make_index<Perspective>(attacker, org, dst, attacked, ksq);

                    if (index < Dimensions)
                        active.push_back(index);
                }
            }
            else
            {
                while (bb)
                {
                    Square   org     = pop_lsb(bb);
                    Bitboard attacks = (attacks_bb(org, pt, occupied)) & occupied;

                    while (attacks)
                    {
                        Square    dst      = pop_lsb(attacks);
                        Piece     attacked = pos.piece_on(dst);
                        IndexType index =
                          make_index<Perspective>(attacker, org, dst, attacked, ksq);

                        if (index < Dimensions)
                            active.push_back(index);
                    }
                }
            }
        }
    }
}

// Explicit template instantiations
template void FullThreats::append_active_indices<WHITE>(const Position& pos, IndexList& active);
template void FullThreats::append_active_indices<BLACK>(const Position& pos, IndexList& active);
template IndexType
FullThreats::make_index<WHITE>(Piece attkr, Square org, Square dst, Piece attkd, Square ksq);
template IndexType
FullThreats::make_index<BLACK>(Piece attkr, Square org, Square dst, Piece attkd, Square ksq);

// Get a list of indices for recently changed features
template<Color Perspective>
void FullThreats::append_changed_indices(Square           ksq,
                                         const DirtyType& dt,
                                         IndexList&       removed,
                                         IndexList&       added,
                                         FusedUpdateData* fusedData,
                                         bool             first) {
    for (const auto dirty : dt.list)
    {
        auto attacker = dirty.pc();
        auto attacked = dirty.threatened_pc();
        auto org      = dirty.pc_sq();
        auto dst      = dirty.threatened_sq();
        auto add      = dirty.add();

        if (fusedData)
        {
            if (org == fusedData->dp2removed)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedOriginBoard |= square_bb(dst);
                        continue;
                    }
                }
                else if (fusedData->dp2removedOriginBoard & square_bb(dst))
                    continue;
            }

            if (dst != SQ_NONE && dst == fusedData->dp2removed)
            {
                if (add)
                {
                    if (first)
                    {
                        fusedData->dp2removedTargetBoard |= square_bb(org);
                        continue;
                    }
                }
                else if (fusedData->dp2removedTargetBoard & square_bb(org))
                    continue;
            }
        }

        IndexType index = make_index<Perspective>(attacker, org, dst, attacked, ksq);

        if (index != Dimensions)
            (add ? added : removed).push_back(index);
    }
}

// Explicit template instantiations
template void FullThreats::append_changed_indices<WHITE>(Square           ksq,
                                                         const DirtyType& dt,
                                                         IndexList&       removed,
                                                         IndexList&       added,
                                                         FusedUpdateData* fd,
                                                         bool             first);
template void FullThreats::append_changed_indices<BLACK>(Square           ksq,
                                                         const DirtyType& dt,
                                                         IndexList&       removed,
                                                         IndexList&       added,
                                                         FusedUpdateData* fd,
                                                         bool             first);

bool FullThreats::requires_refresh(const DirtyType& dt, Color perspective) noexcept {
    return perspective == dt.ac && OrientTBL[dt.ac][dt.kingSq] != OrientTBL[dt.ac][dt.preKingSq];
}

}  // namespace DON::NNUE::Features
