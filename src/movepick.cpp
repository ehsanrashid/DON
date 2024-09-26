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
MovePicker::MovePicker(const Position&         p,
                       Move                    ttm,
                       const ButterflyHistory* mainHist,
                       const CaptureHistory*   capHist,
                       const PieceSqHistory**  conHist,
                       const PawnHistory*      pawnHist,
                       const ButterflyHistory* rootHist,
                       Value                   th) noexcept :
    pos(p),
    ttMove(ttm),
    mainHistory(mainHist),
    captureHistory(capHist),
    continuationHistory(conHist),
    pawnHistory(pawnHist),
    rootHistory(rootHist),
    threshold(th) {
    assert(ttm == Move::None() || (p.pseudo_legal(ttm) && p.legal(ttm)));

    if (mainHist)
    {
        stage = p.checkers() ? EVASION_TT : MAIN_TT;
        if (ttm == Move::None())
            next_stage();
    }
    else
    {
        stage = PROBCUT_TT;
        if (ttm == Move::None() || !p.capture_promo(ttm) || p.see(ttm) < th)
            next_stage();
    }
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures are ordered by Most Valuable Victim (MVV), preferring captures
// with a good history. Quiets moves are ordered using the history tables.
template<GenType GT>
void MovePicker::score() noexcept {
    static_assert(GT == CAPTURES || GT == QUIETS || GT == EVASIONS, "Unsupported generate type");

    Color ac = pos.active_color();

    Key16 pawnIndex = pawn_index(pos.pawn_key());

    for (auto& m : *this)
    {
        Square org = m.org_sq(), dst = m.dst_sq();

        auto pc = pos.piece_on(org);
        auto pt = type_of(pc);

        if constexpr (GT == CAPTURES)
        {
            auto captured = pos.ex_captured(m);

            m.value = 7 * PIECE_VALUE[captured] - pt        //
                    + (*captureHistory)[pc][dst][captured]  //
                    + 0x100 * (pos.cap_square() == dst);
        }

        else if constexpr (GT == QUIETS)
        {
            // Histories
            m.value = (*mainHistory)[ac][m.org_dst()]         //
                    + (*pawnHistory)[pawnIndex][pc][dst] * 2  //
                    + (*continuationHistory[0])[pc][dst] * 2  //
                    + (*continuationHistory[1])[pc][dst]      //
                    + (*continuationHistory[2])[pc][dst] / 3  //
                    + (*continuationHistory[3])[pc][dst]      //
                    + (*continuationHistory[5])[pc][dst];

            if (rootHistory)
                m.value += 8 * (*rootHistory)[ac][m.org_dst()];

            // Bonus for checks
            m.value += 0x4000 * pos.check(m);
            m.value += 0x2000 * pos.fork(m);

            if (pt == KING)
                continue;

            // Bonus for escaping from capture
            if ((pos.threatens(ac) & org))
                m.value += pt == QUEEN ? !(pos.attacks(~ac, ROOK) & dst) * 51700
                         : pt == ROOK  ? !(pos.attacks(~ac, MINOR) & dst) * 25600
                         : pt != PAWN  ? !(pos.attacks(~ac, PAWN) & dst) * 14450
                                       : !(pos.attacks(~ac, PAWN) & dst) * 12450;

            // Malus for putting piece en-prise
            if (!(pos.blockers(~ac) & org))
                m.value -= pt == QUEEN ? !!(pos.attacks(~ac, ROOK) & dst) * 49000
                         : pt == ROOK  ? !!(pos.attacks(~ac, MINOR) & dst) * 24335
                         : pt != PAWN  ? !!(pos.attacks(~ac, PAWN) & dst) * 14900
                                       : !!(pos.attacks(~ac, PAWN) & dst) * 11900;

            if ((pos.pinners() & org))
                m.value -= 0x400 * !aligned(org, dst, pos.king_square(~ac));

            if (pt == KNIGHT && relative_rank(ac, rank_of(dst)) == RANK_1)
                m.value -= 0x400 * (1 + !!(EDGE_FILE_BB & dst));
        }

        else  //if constexpr (GT == EVASIONS)
        {
            if (pos.capture_promo(m))
            {
                auto captured = pos.ex_captured(m);

                m.value = PIECE_VALUE[captured] - pt + 0x20000000;
            }
            else
            {
                m.value = (*mainHistory)[ac][m.org_dst()]     //
                        + (*continuationHistory[0])[pc][dst]  //
                        + (*pawnHistory)[pawnIndex][pc][dst];
            }
        }
    }
}

// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void MovePicker::sort_partial(int limit) noexcept {

    for (auto s = begin(), p = s + 1; p < end(); ++p)
    {
        if (p->value < limit)
            continue;

        auto cur = *p;

        auto q = ++s;

        *p = *q;
        for (; q > begin() && (q - 1)->value < cur.value; --q)
            *q = *(q - 1);
        *q = cur;
    }
}

void MovePicker::swap_maximum(int tolerance) noexcept {

    if (curMax > curMin + 2 && curMax > current().value + tolerance)
    {
        std::iter_swap(begin(), std::max_element(begin(), end()));
        curMax = current().value;
    }
}

// Most important method of the MovePicker class.
// It returns a new pseudo-legal move every time it is called until there are no more moves left,
// picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move(bool pickQuiets) noexcept {

STAGE_SWITCH:
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
        endExtItr = ttMove != Move::None() && pos.capture_promo(ttMove)  //
                    ? extMoves.remove(ttMove)
                    : extMoves.end();

        assert(std::find(begin(), end(), ttMove) == end());

        score<CAPTURES>();
        sort_partial();

        next_stage();
        goto STAGE_SWITCH;

    case CAPTURE_GOOD :
        while (begin() < end())
        {
            auto cur = current();

            next();

            Value curThreshold = -(0.05555 + 0.00694 * (cur.value < 0)) * cur.value;
            if (pos.see(cur) >= curThreshold)
                return cur;
            badCaptures += cur;
        }

        next_stage();
        [[fallthrough]];

    case QUIET_INIT :
        if (pickQuiets)
        {
            extMoves.clear();
            generate<QUIETS>(extMoves, pos);
            curExtItr = extMoves.begin();
            endExtItr = ttMove != Move::None() && !pos.capture_promo(ttMove)
                        ? extMoves.remove(ttMove)
                        : extMoves.end();

            assert(std::find(begin(), end(), ttMove) == end());

            score<QUIETS>();
            sort_partial(threshold);

            curMin = std::min_element(begin(), end())->value;
        }
        else
            assert(begin() == end());

        next_stage();
        [[fallthrough]];

    case QUIET_GOOD :
        if (pickQuiets && begin() < end() && current().value >= threshold)
            return current_next();
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
        if (pickQuiets && begin() < end())
        {
            swap_maximum(1);
            return current_next();
        }
        return Move::None();

    case EVASION_INIT :
        generate<EVASIONS>(extMoves, pos);
        curExtItr = extMoves.begin();
        endExtItr = ttMove != Move::None() ? extMoves.remove(ttMove) : extMoves.end();

        score<EVASIONS>();

        curMin = std::min_element(begin(), end())->value;

        next_stage();
        [[fallthrough]];

    case EVASION :
        if (begin() < end())
        {
            swap_maximum(0);
            return current_next();
        }
        return Move::None();

    case PROBCUT :
        while (begin() < end())
        {
            auto cur = current();

            next();

            if (pos.see(cur) >= threshold)
                return cur;
        }
        return Move::None();

    case STAGE_NONE :;
    }
    assert(false);
    return Move::None();  // Silence warning
}

}  // namespace DON
