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

#include <algorithm>
#include <cassert>
#include <utility>

#include "bitboard.h"
#include "position.h"

namespace DON {

// Constructors of the MovePicker class. As arguments, pass information
// to decide which class of moves to return, to help sorting the (presumably)
// good moves first, and how important move ordering is at the current node.
MovePicker::MovePicker(const Position&            p,
                       Move                       ttm,
                       const History<HButterfly>* mainHist,
                       const History<HCapture>*   capHist,
                       const History<HPawn>*      pawnHist,
                       const History<HPieceSq>**  psqHist,
                       const History<HLowPly>*    lpHist,
                       std::int16_t               ply,
                       Value                      th) noexcept :
    pos(p),
    ttMove(ttm),
    mainHistory(mainHist),
    captureHistory(capHist),
    pawnHistory(pawnHist),
    pieceSqHistory(psqHist),
    lowPlyHistory(lpHist),
    ssPly(ply),
    threshold(th) {
    assert(ttm == Move::None() || (p.pseudo_legal(ttm) && p.legal(ttm)));

    stage = p.checkers() ? EVASION_TT : MAIN_TT;
    if (ttm == Move::None())
        next_stage();
}

MovePicker::MovePicker(const Position&          p,
                       Move                     ttm,
                       const History<HCapture>* capHist,
                       Value                    th) noexcept :
    pos(p),
    ttMove(ttm),
    captureHistory(capHist),
    threshold(th) {
    assert(!p.checkers());
    assert(ttm == Move::None() || (p.pseudo_legal(ttm) && p.legal(ttm)));

    stage = PROBCUT_TT;
    if (ttm == Move::None() || !p.capture_promo(ttm) || p.see(ttm) < th)
        next_stage();
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
            m.value = 2 * (*mainHistory)[ac][m.org_dst()]     //
                    + 2 * (*pawnHistory)[pawnIndex][pc][dst]  //
                    + (*pieceSqHistory[0])[pc][dst]           //
                    + (*pieceSqHistory[1])[pc][dst]           //
                    + (*pieceSqHistory[2])[pc][dst]           //
                    + (*pieceSqHistory[3])[pc][dst]           //
                    + (*pieceSqHistory[5])[pc][dst];

            if (ssPly < LOW_PLY_SIZE)
                m.value += 8 * (*lowPlyHistory)[ssPly][m.org_dst()] / (1 + 2 * ssPly);

            // Bonus for checks
            m.value += 0x4000 * pos.check(m);
            m.value += 0x2000 * pos.fork(m);

            if (pt == PAWN || pt == KING)
                continue;

            // Bonus for escaping from capture
            if ((pos.threatens(ac) & org))
                m.value += pt == QUEEN ? 51700 * !(pos.attacks(~ac, ROOK) & dst)
                         : pt == ROOK  ? 25600 * !(pos.attacks(~ac, MINOR) & dst)
                                       : 14450 * !(pos.attacks(~ac, PAWN) & dst);

            // Malus for putting piece en-prise
            if (!(pos.blockers(~ac) & org))
                m.value -= pt == QUEEN ? 49000 * !!(pos.attacks(~ac, ROOK) & dst)
                         : pt == ROOK  ? 24335 * !!(pos.attacks(~ac, MINOR) & dst)
                                       : 14900 * !!(pos.attacks(~ac, PAWN) & dst);

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
                        + (*pawnHistory)[pawnIndex][pc][dst]  //
                        + (*pieceSqHistory[0])[pc][dst];
            }
        }
    }
}

// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void MovePicker::sort_partial(int limit) noexcept {

    auto b = begin(), e = end();
    for (auto p = b + 1, s = b; p < e; ++p)
    {
        if (p->value < limit)
            continue;

        auto cur = *p;
        auto q   = ++s;

        *p = *q;
        while (q != b && *(q - 1) < cur)
        {
            *q = *(q - 1);
            --q;
        }
        *q = cur;
    }
}

void MovePicker::swap_best(int tolerance) noexcept {

    if (curValue > minValue + 2 && curValue > current().value + tolerance)
    {
        std::iter_swap(begin(), std::max_element(begin(), end()));
        curValue = current().value;
    }
}

// Most important method of the MovePicker class.
// It returns a new pseudo-legal move every time it is called until there are no more moves left,
// picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move() noexcept {

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
        begExtItr = extMoves.begin();
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
            auto cur = current_next();

            Value curThreshold = -(55.5555e-3 + 6.9444e-3 * (cur.value < 0)) * cur.value;
            if (pos.see(cur) >= curThreshold)
                return cur;
            badCapMoves += cur;
        }

        next_stage();
        [[fallthrough]];

    case QUIET_INIT :
        if (pickQuiets)
        {
            extMoves.clear();
            generate<QUIETS>(extMoves, pos);
            begExtItr = extMoves.begin();
            endExtItr = ttMove != Move::None() && !pos.capture_promo(ttMove)
                        ? extMoves.remove(ttMove)
                        : extMoves.end();

            assert(std::find(begin(), end(), ttMove) == end());

            score<QUIETS>();
            sort_partial(threshold);

            minValue = std::min_element(begin(), end())->value;
        }

        next_stage();
        [[fallthrough]];

    case QUIET_GOOD :
        if (pickQuiets && begin() < end() && current().value >= threshold)
            return current_next();
        // Remaining quiets are bad

        // Prepare to loop over the bad captures
        begItr = badCapMoves.begin();
        endItr = badCapMoves.end();

        next_stage();
        [[fallthrough]];

    case CAPTURE_BAD :
        if (begItr < endItr)
            return *begItr++;

        next_stage();
        [[fallthrough]];

    case QUIET_BAD :
        if (pickQuiets && begin() < end())
        {
            swap_best(1);
            return current_next();
        }
        return Move::None();

    case EVASION_INIT :
        generate<EVASIONS>(extMoves, pos);
        begExtItr = extMoves.begin();
        endExtItr = ttMove != Move::None() ? extMoves.remove(ttMove) : extMoves.end();

        score<EVASIONS>();

        minValue = std::min_element(begin(), end())->value;

        next_stage();
        [[fallthrough]];

    case EVASION :
        if (begin() < end())
        {
            swap_best(0);
            return current_next();
        }
        return Move::None();

    case PROBCUT :
        while (begin() < end())
        {
            auto cur = current_next();

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
