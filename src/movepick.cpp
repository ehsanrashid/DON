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
#include <cmath>
#include <functional>
#include <utility>

#include "bitboard.h"
#include "position.h"

namespace DON {

// History
extern History<HCapture>      CaptureHistory;
extern History<HQuiet>        QuietHistory;
extern History<HPawn>         PawnHistory;
extern History<HContinuation> ContinuationHistory[2][2];
extern History<HLowPlyQuiet>  LowPlyQuietHistory;

// Constructors of the MovePicker class. As arguments, pass information
// to decide which class of moves to return, to help sorting the (presumably)
// good moves first, and how important move ordering is at the current node.
MovePicker::MovePicker(const Position&           p,
                       const Move&               ttm,
                       const History<HPieceSq>** continuationHist,
                       std::int16_t              ply,
                       int                       th) noexcept :
    pos(p),
    ttMove(ttm),
    continuationHistory(continuationHist),
    ssPly(ply),
    threshold(th) {
    assert(ttMove == Move::None || pos.pseudo_legal(ttMove));

    if (pos.checkers())
        stage = STG_EVA_TT + !(ttMove != Move::None);
    else
        stage = (threshold < 0 ? STG_ENC_TT : STG_QS_TT) + !(ttMove != Move::None);
}

MovePicker::MovePicker(const Position& p,  //
                       const Move&     ttm,
                       int             th) noexcept :
    pos(p),
    ttMove(ttm),
    continuationHistory(nullptr),
    ssPly(0),
    threshold(th) {
    assert(!pos.checkers());
    assert(ttMove == Move::None || pos.pseudo_legal(ttMove));

    stage = STG_PROBCUT_TT + !(ttMove != Move::None && pos.capture_promo(ttMove));
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures moves are ordered by Most Valuable Victim (MVV),
// preferring captures moves with a good history.
// Quiets moves are ordered by using the history tables.
template<>
MovePicker::iterator MovePicker::score<ENC_CAPTURE>(MoveList<ENC_CAPTURE>& moveList) noexcept {

    iterator itr = cur;
    for (const auto& move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        Square dst      = m.dst_sq();
        auto   pc       = pos.moved_piece(m);
        auto   captured = pos.captured(m);

        m.value = 7 * (PIECE_VALUE[captured] + promotion_value<true>(m))  //
                + CaptureHistory[pc][dst][captured]                       //
                + 0x400 * pos.check(m)                                    //
                + 0x100 * (pos.cap_sq() == dst);
    }
    return itr;
}

template<>
MovePicker::iterator MovePicker::score<ENC_QUIET>(MoveList<ENC_QUIET>& moveList) noexcept {
    constexpr int Bonus[PIECE_TYPE_NB]{0, 0, 144, 144, 256, 517, 10000};

    Color ac        = pos.active_color();
    auto  pawnIndex = pawn_index(pos.pawn_key());

    iterator itr = cur;
    for (const auto& move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(m.type_of() != PROMOTION);

        Square org = m.org_sq(), dst = m.dst_sq();
        auto   pc = pos.moved_piece(m);
        auto   pt = type_of(pc);

        m.value = 2 * QuietHistory[ac][m.org_dst()]    //
                + 2 * PawnHistory[pawnIndex][pc][dst]  //
                + (*continuationHistory[0])[pc][dst]   //
                + (*continuationHistory[1])[pc][dst]   //
                + (*continuationHistory[2])[pc][dst]   //
                + (*continuationHistory[3])[pc][dst]   //
                + (*continuationHistory[4])[pc][dst]   //
                + (*continuationHistory[5])[pc][dst]   //
                + (*continuationHistory[6])[pc][dst]   //
                + (*continuationHistory[7])[pc][dst];

        if (ssPly < LOW_PLY_SIZE)
            m.value += 8 * LowPlyQuietHistory[ssPly][m.org_dst()] / (1 + ssPly);

        // Bonus for checks
        if (pos.check(m))
            m.value += 0x4000 * (pos.see(m) >= -75) + 0x1000 * pos.dbl_check(m);

        m.value += 0x1000 * (pos.fork(m) && pos.see(m) >= -50);

        // Penalty for moving to a square threatened by a lesser piece or
        // Bonus for escaping an attack by a lesser piece.
        m.value += Bonus[pt]
                 * (((pos.attacks_lesser(~ac, pt) & dst) && !(pos.blockers(~ac) & org)) ? -95
                    : (pos.attacks_lesser(~ac, pt) & org)                               ? 100
                                                                                        : 0);

        if (pt == KING)
            continue;

        // Penalty for moving a pinner piece.
        m.value -= 0x400 * ((pos.pinners() & org) && !aligned(pos.king_sq(~ac), org, dst));
    }
    return itr;
}

template<>
MovePicker::iterator MovePicker::score<EVA_CAPTURE>(MoveList<EVA_CAPTURE>& moveList) noexcept {

    iterator itr = cur;
    for (const auto& move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(m.type_of() != CASTLING);

        auto captured = pos.captured(m);

        m.value = PIECE_VALUE[captured] + promotion_value<true>(m);
    }
    return itr;
}

template<>
MovePicker::iterator MovePicker::score<EVA_QUIET>(MoveList<EVA_QUIET>& moveList) noexcept {

    Color ac = pos.active_color();

    iterator itr = cur;
    for (const auto& move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(m.type_of() != PROMOTION);
        assert(m.type_of() != CASTLING);

        Square dst = m.dst_sq();
        auto   pc  = pos.moved_piece(m);

        m.value = QuietHistory[ac][m.org_dst()] + (*continuationHistory[0])[pc][dst];

        if (ssPly < LOW_PLY_SIZE)
            m.value += LowPlyQuietHistory[ssPly][m.org_dst()];
    }
    return itr;
}

template<typename Predicate>
bool MovePicker::select(Predicate pred) noexcept {

    for (; !empty(); next())
        if (valid() && pred())
            return true;
    return false;
}

// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void MovePicker::sort_partial(int limit) noexcept {

    for (iterator s = begin(), p = begin() + 1; p < end(); ++p)
        if (p->value >= limit)
        {
            auto m = *p;

            *p = *++s;

            // Find the correct position for 'm' using binary search
            iterator q = std::upper_bound(begin(), s, m, std::greater<>{});
            // Move elements to make space for 'm'
            std::move_backward(q, s, s + 1);
            // Insert the element in its correct position
            *q = m;
        }
}

// Most important method of the MovePicker class.
// It emits a new pseudo-legal move every time it is called until there are no more moves left,
// picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move() noexcept {

STAGE_SWITCH:
    switch (stage)
    {
    case STG_ENC_TT :
    case STG_EVA_TT :
    case STG_QS_TT :
    case STG_PROBCUT_TT :
        ++stage;
        return ttMove;

    case STG_ENC_CAPTURE_INIT :
    case STG_QS_CAPTURE_INIT :
    case STG_PROBCUT_INIT : {
        MoveList<ENC_CAPTURE> moveList(pos);

        cur = endBadCaptures = moves;
        // NOTE:: endMoves is not defined here, it will be set later
        endCur = /* endMoves =*/score<ENC_CAPTURE>(moveList);

        sort_partial();
    }

        ++stage;
        goto STAGE_SWITCH;

    case STG_ENC_CAPTURE_GOOD :
        if (select([&]() {
                if (pos.see(*cur) >= std::round(-55.5555e-3 * cur->value))
                    return true;
                // Store bad captures
                std::swap(*endBadCaptures++, *cur);
                return false;
            }))
            return move();

        ++stage;
        [[fallthrough]];

    case STG_ENC_QUIET_INIT :
        if (quietPick)
        {
            MoveList<ENC_QUIET> moveList(pos);

            endCur = endMoves = score<ENC_QUIET>(moveList);

            sort_partial(threshold);
        }

        ++stage;
        [[fallthrough]];

    case STG_ENC_QUIET_GOOD :
        if (quietPick)
        {
            for (; !empty(); next())
                if (valid())
                {
                    // Good quiet threshold
                    if (cur->value >= (-13500 + threshold / 8))
                        return move();
                    // Remaining quiets are bad
                    break;
                }

            // Mark the beginning of bad quiets
            begBadQuiets = cur;
        }

        // Prepare the pointers to loop over the bad captures
        cur    = moves;
        endCur = endBadCaptures;

        ++stage;
        [[fallthrough]];

    case STG_ENC_CAPTURE_BAD :
        if (select([]() { return true; }))
            return move();

        if (quietPick)
        {
            // Prepare the pointers to loop over the bad quiets
            cur    = begBadQuiets;
            endCur = endMoves;

            sort_partial();
        }

        ++stage;
        [[fallthrough]];

    case STG_ENC_QUIET_BAD :
        if (quietPick && select([]() { return true; }))
            return move();

        return Move::None;

    case STG_EVA_CAPTURE_INIT : {
        MoveList<EVA_CAPTURE> moveList(pos);

        cur    = moves;
        endCur = endMoves = score<EVA_CAPTURE>(moveList);

        sort_partial();
    }

        ++stage;
        [[fallthrough]];

    case STG_EVA_CAPTURE :
        if (select([]() { return true; }))
            return move();

        ++stage;
        [[fallthrough]];

    case STG_EVA_QUIET_INIT : {
        MoveList<EVA_QUIET> moveList(pos);

        endCur = endMoves = score<EVA_QUIET>(moveList);

        sort_partial();
    }

        ++stage;
        [[fallthrough]];

    case STG_EVA_QUIET :
    case STG_QS_CAPTURE :
        if (select([]() { return true; }))
            return move();

        return Move::None;

    case STG_PROBCUT :
        if (select([&]() { return pos.see(*cur) >= threshold; }))
            return move();

        return Move::None;

    case STG_NONE :;
    }
    assert(false);
    return Move::None;  // Silence warning
}

}  // namespace DON
