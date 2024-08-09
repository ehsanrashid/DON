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

#include "movepick.h"

#include <utility>

#include "bitboard.h"
#include "position.h"

namespace DON {

// Constructors of the MovePicker class. As arguments, pass information
// to decide which class of moves to return, to help sorting the (presumably)
// good moves first, and how important move ordering is at the current node.
MovePicker::MovePicker(const Position&               p,
                       Move                          ttm,
                       const ButterflyHistory*       mh,
                       const CapturePieceDstHistory* cph,
                       const PieceDstHistory**       ch,
                       const PawnHistory*            ph,
                       int                           th,
                       bool                          all) noexcept :
    pos(p),
    ttMove(ttm),
    mainHistory(mh),
    captureHistory(cph),
    continuationHistory(ch),
    pawnHistory(ph),
    threshold(th) {
    assert(ttm == Move::None() || (p.pseudo_legal(ttm) && p.legal(ttm)));

    if (all)
    {
        stage = p.checkers() ? EVASION_TT : MAIN_TT;
        if (ttm == Move::None())
            next_stage();
    }
    else
    {
        stage = PROBCUT_TT;
        if (ttm == Move::None() || !p.capture_stage(ttm) || p.see(ttm) < th)
            next_stage();
    }
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures are ordered by Most Valuable Victim (MVV), preferring captures
// with a good history. Quiets moves are ordered using the history tables.
template<GenType GT>
void MovePicker::score() noexcept {
    static_assert(GT == CAPTURES || GT == QUIETS || GT == EVASIONS, "Unsupported generate type");

    const Color stm  = pos.side_to_move();
    const Color xstm = ~stm;

    const Key16 pawnIndex = pawn_index(pos.pawn_key());

    for (auto& m : *this)
    {
        const Square        org = m.org_sq(), dst = m.dst_sq();
        const std::uint16_t orgDst = m.org_dst();

        const Piece     pc = pos.piece_on(org);
        const PieceType pt = type_of(pc);

        if constexpr (GT == CAPTURES)
        {
            PieceType captured = type_of(pos.captured_piece(m));

            m.value = 7 * PIECE_VALUE[captured] - pt        //
                    + (*captureHistory)[pc][dst][captured]  //
                    + 128 * (pos.cap_square() == dst);
        }

        else if constexpr (GT == QUIETS)
        {
            // Histories
            m.value = (*mainHistory)[stm][orgDst]             //
                    + 2 * (*pawnHistory)[pawnIndex][pc][dst]  //
                    + 2 * (*continuationHistory[0])[pc][dst]  //
                    + (*continuationHistory[1])[pc][dst]      //
                    + (*continuationHistory[2])[pc][dst] / 3  //
                    + (*continuationHistory[3])[pc][dst]      //
                    + (*continuationHistory[5])[pc][dst];

            // Bonus for checks
            m.value += pos.gives_check(m) * 0x4000;

            if (pt == PAWN || pt == KING)
                continue;

            // Bonus for escaping from capture
            if ((pos.threatens(stm) & org))
                m.value += pt == QUEEN ? !(pos.attacks(xstm, ROOK) & dst) * 22000
                                           + !(pos.attacks(xstm, MINOR) & dst) * 15450
                                           + !(pos.attacks(xstm, PAWN) & dst) * 14450
                         : pt == ROOK ? !(pos.attacks(xstm, MINOR) & dst) * 15450
                                          + !(pos.attacks(xstm, PAWN) & dst) * 14450
                                      : !(pos.attacks(xstm, PAWN) & dst) * 14450;

            // Malus for putting piece en-prise
            if (!(pos.blockers(xstm) & org))
                m.value -= pt == QUEEN ? !!(pos.attacks(xstm, ROOK) & dst) * 49000
                         : pt == ROOK  ? !!(pos.attacks(xstm, MINOR) & dst) * 24335
                                       : !!(pos.attacks(xstm, PAWN) & dst) * 14900;

            if ((pos.pinners() & org))
                m.value -= !aligned(org, dst, pos.king_square(xstm)) * 0x400;
        }

        else  //if constexpr (GT == EVASIONS)
        {
            if (pos.capture_stage(m))
            {
                PieceType captured = type_of(pos.captured_piece(m));

                m.value = PIECE_VALUE[captured] - pt + 0x20000000;
            }
            else
            {
                m.value = (*mainHistory)[stm][orgDst]         //
                        + (*continuationHistory[0])[pc][dst]  //
                        + (*pawnHistory)[pawnIndex][pc][dst];
            }
        }
    }
}

// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void MovePicker::sort_partial(int limit) noexcept {

    for (auto endSorted = curExtItr, p = curExtItr + 1; p < endExtItr; ++p)
        if (p->value >= limit)
        {
            auto tmp = *p;

            *p     = *++endSorted;
            auto q = endSorted;
            for (; q != curExtItr && *(q - 1) < tmp; --q)
                *q = *(q - 1);
            *q = tmp;
        }
}

// Most important method of the MovePicker class.
// It returns a new pseudo-legal move every time it is called until there are no more moves left,
// picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move(bool pickQuiets) noexcept {

SWITCH:
    switch (stage)
    {
    case MAIN_TT :
    case EVASION_TT :
    case PROBCUT_TT :
        next_stage();
        return ttMove;

    case CAPTURE_INIT :
    case PROBCUT_INIT :
        generate<CAPTURES>(extMoves, pos);
        curExtItr = extMoves.begin();
        endExtItr = ttMove != Move::None() && pos.capture_stage(ttMove)  //
                    ? extMoves.remove(ttMove)
                    : extMoves.end();

        score<CAPTURES>();
        sort_partial();

        next_stage();
        goto SWITCH;

    case CAPTURE_GOOD :
        while (curExtItr < endExtItr)
        {
            int captureThreshold = -curExtItr->value / (18 + 4 * (curExtItr->value < 0));
            if (pos.see(*curExtItr) >= captureThreshold)
                return *curExtItr++;
            badCaptures += *curExtItr++;
        }

        next_stage();
        [[fallthrough]];

    case QUIET_INIT :
        if (pickQuiets)
        {
            generate<QUIETS>(extMoves, pos);
            curExtItr = extMoves.begin();
            endExtItr = ttMove != Move::None() && !pos.capture_stage(ttMove)
                        ? extMoves.remove(ttMove)
                        : extMoves.end();

            score<QUIETS>();
            sort_partial(threshold);
        }
        else
            assert(curExtItr == endExtItr);

        next_stage();
        [[fallthrough]];

    case QUIET_GOOD :
        if (pickQuiets && curExtItr < endExtItr && curExtItr->value >= threshold)
            return *curExtItr++;
        // Remaining quiets are bad

        // Prepare to loop over the bad captures
        curItr = badCaptures.begin();
        endItr = badCaptures.end();

        next_stage();
        [[fallthrough]];

    case CAPTURE_BAD :
        if (curItr < endItr)
            return *curItr++;

        next_stage();
        [[fallthrough]];

    case QUIET_BAD :
        if (pickQuiets && curExtItr < endExtItr)
        {
            std::iter_swap(curExtItr, std::max_element(curExtItr, endExtItr));
            return *curExtItr++;
        }
        return Move::None();

    case EVASION_INIT :
        generate<EVASIONS>(extMoves, pos);
        curExtItr = extMoves.begin();
        endExtItr = ttMove != Move::None() ? extMoves.remove(ttMove) : extMoves.end();

        score<EVASIONS>();

        next_stage();
        [[fallthrough]];

    case EVASION :
        if (curExtItr < endExtItr)
        {
            std::iter_swap(curExtItr, std::max_element(curExtItr, endExtItr));
            return *curExtItr++;
        }
        return Move::None();

    case PROBCUT :
        while (curExtItr < endExtItr)
        {
            if (pos.see(*curExtItr) >= threshold)
                return *curExtItr++;
            ++curExtItr;
        }
        return Move::None();

    case NO_STAGE :;
    }
    assert(false);
    return Move::None();  // Silence warning
}

}  // namespace DON
