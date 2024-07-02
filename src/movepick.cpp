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
// to help it return the (presumably) good moves first, to decide which
// moves to return (in the quiescence search, for instance, only want to
// search captures, promotions, and some checks) and how important a good
// move ordering is at the current node.

// MovePicker constructor for the main search
MovePicker::MovePicker(const Position&               p,
                       Move                          ttm,
                       Depth                         d,
                       const ButterflyHistory*       mh,
                       const CapturePieceDstHistory* cph,
                       const PieceDstHistory**       ch,
                       const PawnHistory*            ph,
                       const KillerMoves&            km,
                       Move                          cm) noexcept :
    pos(p),
    mainHistory(mh),
    captureHistory(cph),
    continuationHistory(ch),
    pawnHistory(ph),
    ttMove(ttm),
    depth(d) {
    assert(d > DEPTH_ZERO);
    assert(ttm == Move::None() || (pos.pseudo_legal(ttm) && pos.legal(ttm)));
    assert((km[0] == Move::None() && km[1] == Move::None()) || km[0] != km[1]);
    refutations.clear();
    // If the counterMove is the same as a killerMoves, skip it
    if (km[0] != Move::None() && km[0] != ttm && !p.capture_stage(km[0]) && p.pseudo_legal(km[0]))
        refutations += km[0];
    if (km[1] != Move::None() && km[1] != ttm && !p.capture_stage(km[1]) && p.pseudo_legal(km[1]))
        refutations += km[1];
    if (cm != Move::None() && cm != ttm && cm != km[0] && cm != km[1] && !p.capture_stage(cm)
        && p.pseudo_legal(cm))
        refutations += cm;
    badCaptures.clear();

    stage = (p.checkers() != 0 ? EVASION_TT : MAIN_TT) + (ttm == Move::None());
}

// Constructor for quiescence search
MovePicker::MovePicker(const Position&               p,
                       Move                          ttm,
                       Depth                         d,
                       const ButterflyHistory*       mh,
                       const CapturePieceDstHistory* cph,
                       const PieceDstHistory**       ch,
                       const PawnHistory*            ph) noexcept :
    pos(p),
    mainHistory(mh),
    captureHistory(cph),
    continuationHistory(ch),
    pawnHistory(ph),
    ttMove(ttm),
    depth(d) {
    assert(d <= DEPTH_ZERO);
    assert(ttm == Move::None() || (pos.pseudo_legal(ttm) && pos.legal(ttm)));

    stage = (p.checkers() != 0 ? EVASION_TT : QS_TT) + (ttm == Move::None());
}

// Constructor for ProbCut: generate captures with SEE greater
// than or equal to the given threshold.
MovePicker::MovePicker(const Position&               p,
                       Move                          ttm,
                       int                           th,
                       const CapturePieceDstHistory* cph) noexcept :
    pos(p),
    captureHistory(cph),
    ttMove(ttm),
    threshold(th) {
    assert(!p.checkers());
    assert(ttm == Move::None() || (pos.pseudo_legal(ttm) && pos.legal(ttm)));

    stage = PROBCUT_TT + !(ttm != Move::None() && p.capture_stage(ttm) && p.see_ge(ttm, th));
}

// Assigns a numerical value to each move in a list, used
// for sorting. Captures are ordered by Most Valuable Victim (MVV), preferring
// captures with a good history. Quiets moves are ordered using the history tables.
template<GenType GT>
void MovePicker::score() noexcept {

    static_assert(GT == CAPTURES || GT == QUIETS || GT == EVASIONS, "Wrong type");

    Color stm  = pos.side_to_move();
    Color xstm = ~stm;

    for (auto& m : *this)
    {
        const Square org = m.org_sq(), dst = m.dst_sq();

        const Piece     pc = pos.piece_on(org);
        const PieceType pt = type_of(pc);

        if constexpr (GT == CAPTURES)
        {
            PieceType captured = type_of(pos.captured_piece(m));

            m.value = 7 * PieceValue[captured] - pt         //
                    + (*captureHistory)[pc][dst][captured]  //
                    + 128 * (pos.cap_square() == dst);
        }

        else if constexpr (GT == QUIETS)
        {
            // Histories
            m.value = (*mainHistory)[stm][m.org_dst()]
                    + 2 * (*pawnHistory)[pawn_index(pos.pawn_key())][pc][dst]
                    + 2 * (*continuationHistory[0])[pc][dst]  //
                    + (*continuationHistory[1])[pc][dst]      //
                    + (*continuationHistory[2])[pc][dst] / 3  //
                    + (*continuationHistory[3])[pc][dst]      //
                    + (*continuationHistory[5])[pc][dst];

            // Bonus for checks
            m.value += pos.gives_check(m) * 16384;

            if (pt == PAWN || pt == KING)
                continue;

            // Bonus for escaping from capture
            if ((pos.threatens(stm) & org))
                m.value += pt == QUEEN ? !(pos.attacks(xstm, ROOK) & dst) * 21000
                                           + !(pos.attacks(xstm, PAWN) & dst) * 16450
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
                m.value -= !aligned(org, dst, pos.king_square(xstm)) * 1024;
        }

        else  // GT == EVASIONS
        {
            m.value = pos.capture(m)  //
                      ? PieceValue[pos.captured_piece(m)] - pt + (1 << 28)
                      : (*mainHistory)[stm][m.org_dst()]        //
                          + (*continuationHistory[0])[pc][dst]  //
                          + (*pawnHistory)[pawn_index(pos.pawn_key())][pc][dst];
        }
    }
}

// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void MovePicker::partial_sort(int limit) noexcept {

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
// It returns a new pseudo-legal move every time it is called until there are no more
// moves left, picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move() noexcept {

    auto quiet_threshold = [](Depth d) noexcept -> int { return -3560 * d; };

SWITCH:
    switch (stage)
    {
    case MAIN_TT :
    case EVASION_TT :
    case QS_TT :
    case PROBCUT_TT :
        ++stage;
        return ttMove;

    case CAPTURE_INIT :
    case PROBCUT_INIT :
    case QS_CAPTURE_INIT :
        extMoves.clear();
        endExtItr = generate<CAPTURES>(extMoves, pos);
        if (ttMove != Move::None())
            endExtItr = extMoves.remove(ttMove);
        curExtItr = extMoves.begin();

        score<CAPTURES>();
        partial_sort(std::numeric_limits<int>::min());

        ++stage;
        goto SWITCH;

    case CAPTURE_GOOD :
        while (curExtItr < endExtItr)
        {
            if (pos.see_ge(*curExtItr, -curExtItr->value / 18))
                return *curExtItr++;
            badCaptures += *curExtItr++;
        }

        // Prepare to loop over the refutations moves
        curItr = refutations.begin();
        endItr = refutations.end();
        assert(curItr <= endItr);

        ++stage;
        [[fallthrough]];

    case REFUTATION :
        if (curItr < endItr)
            return *curItr++;

        ++stage;
        [[fallthrough]];

    case QUIET_INIT :
        if (pickQuiets)
        {
            extMoves.clear();
            generate<QUIETS>(extMoves, pos);
            curItr    = refutations.begin();
            endExtItr = extMoves.remove_if([&](Move m) noexcept -> bool {
                return m == ttMove || std::find(curItr, endItr, m) != endItr;
            });
            curExtItr = extMoves.begin();

            score<QUIETS>();
            partial_sort(quiet_threshold(depth));
        }

        ++stage;
        [[fallthrough]];

    case QUIET_GOOD :
        if (pickQuiets && curExtItr < endExtItr)
        {
            assert(std::find(curItr, endItr, *curExtItr) == endItr);
            if (curExtItr->value > -7998 || curExtItr->value <= quiet_threshold(depth))
                return *curExtItr++;
            // Remaining quiets are bad
        }

        // Prepare to loop over the bad captures
        curItr = badCaptures.begin();
        endItr = badCaptures.end();
        assert(curItr <= endItr);

        ++stage;
        [[fallthrough]];

    case CAPTURE_BAD :
        if (curItr < endItr)
            return *curItr++;

        ++stage;
        [[fallthrough]];

    case QUIET_BAD :
        if (pickQuiets && curExtItr < endExtItr)
            return *curExtItr++;
        return Move::None();

    case EVASION_INIT :
        extMoves.clear();
        endExtItr = generate<EVASIONS>(extMoves, pos);
        if (ttMove != Move::None())
            endExtItr = extMoves.remove(ttMove);
        curExtItr = extMoves.begin();

        score<EVASIONS>();

        ++stage;
        [[fallthrough]];

    case EVASION :
        if (curExtItr < endExtItr)
        {
            std::swap(*curExtItr, *std::max_element(curExtItr, endExtItr));
            return *curExtItr++;
        }
        return Move::None();

    case PROBCUT :
        while (curExtItr < endExtItr)
        {
            if (pos.see_ge(*curExtItr, threshold))
                return *curExtItr++;
            ++curExtItr;
        }
        return Move::None();

    case QS_CAPTURE :
        if (curExtItr < endExtItr)
            return *curExtItr++;

        // If did not find any move and the depth is too low to try checks, then finished
        if (depth <= DEPTH_QS_NORMAL)
            return Move::None();

        ++stage;
        [[fallthrough]];

    case QS_CHECK_INIT :
        extMoves.clear();
        endExtItr = generate<QUIET_CHECKS>(extMoves, pos);
        if (ttMove != Move::None())
            endExtItr = extMoves.remove(ttMove);
        curExtItr = extMoves.begin();

        ++stage;
        [[fallthrough]];

    case QS_CHECK :
        if (curExtItr < endExtItr)
            return *curExtItr++;
        return Move::None();

    case NO_STAGE :;
    }
    assert(false);
    return Move::None();  // Silence warning
}

}  // namespace DON
