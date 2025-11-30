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
#include <functional>
#include <iterator>

#include "bitboard.h"
#include "position.h"

namespace DON {

// Constructors of the MovePicker class. As arguments, pass information
// to decide which class of moves to return, to help sorting the (presumably)
// good moves first, and how important move ordering is at the current node.

// MovePicker constructor for the main search and for the quiescence search
MovePicker::MovePicker(const Position&              p,
                       Move                         ttm,
                       const History<HCapture>*     captureHist,
                       const History<HQuiet>*       quietHist,
                       const History<HPawn>*        pawnHist,
                       const History<HLowPlyQuiet>* lowPlyQuietHist,
                       const History<HPieceSq>**    continuationHist,
                       std::int16_t                 ply,
                       int                          th) noexcept :
    pos(p),
    ttMove(ttm),
    captureHistory(captureHist),
    quietHistory(quietHist),
    pawnHistory(pawnHist),
    lowPlyQuietHistory(lowPlyQuietHist),
    continuationHistory(continuationHist),
    ssPly(ply),
    threshold(th) {
    assert(ttMove == Move::None || pos.pseudo_legal(ttMove));

    stage = pos.checkers() ? STG_EVA_TT + int(!(ttMove != Move::None))
          : threshold < 0
            ? STG_ENC_TT + int(!(ttMove != Move::None))
            : STG_QS_TT + int(!(ttMove != Move::None && pos.capture_queenpromo(ttMove)));
}

// MovePicker constructor for ProbCut:
// Generate captures with Static Exchange Evaluation (SEE) >= threshold.
MovePicker::MovePicker(const Position&          p,
                       Move                     ttm,
                       const History<HCapture>* captureHist,
                       int                      th) noexcept :
    pos(p),
    ttMove(ttm),
    captureHistory(captureHist),
    threshold(th) {
    assert(!pos.checkers());
    assert(ttMove == Move::None || pos.pseudo_legal(ttMove));

    stage = STG_PROBCUT_TT + int(!(ttMove != Move::None && pos.capture_queenpromo(ttMove)));
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures moves are ordered by Most Valuable Victim (MVV),
// preferring captures moves with a good history.
// Quiets moves are ordered by using the history tables.
template<>
MovePicker::iterator MovePicker::score<ENC_CAPTURE>(MoveList<ENC_CAPTURE>& moveList) noexcept {

    iterator itr = cur;
    for (auto move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(pos.capture_queenpromo(m));

        Square dst      = m.dst_sq();
        auto   pc       = pos.moved_piece(m);
        auto   captured = pos.captured(m);

        m.value = 7 * piece_value(captured)  //
                + (*captureHistory)[pc][dst][captured];
    }
    return itr;
}

template<>
MovePicker::iterator MovePicker::score<ENC_QUIET>(MoveList<ENC_QUIET>& moveList) noexcept {

    Color ac = pos.active_color();

    Square   kingSq   = pos.square<KING>(~ac);
    Bitboard threats  = pos.threats(~ac);
    Bitboard blockers = pos.blockers(~ac);
    Bitboard pinners  = pos.pinners();

    std::uint16_t pawnIndex = pawn_index(pos.pawn_key());

    iterator itr = cur;
    for (auto move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(!pos.capture_queenpromo(m));

        Square org = m.org_sq(), dst = m.dst_sq();
        auto   pc = pos.moved_piece(m);
        auto   pt = type_of(pc);

        m.value = 2 * (*quietHistory)[ac][m.raw()]        //
                + 2 * (*pawnHistory)[pawnIndex][pc][dst]  //
                + (*continuationHistory[0])[pc][dst]      //
                + (*continuationHistory[1])[pc][dst]      //
                + (*continuationHistory[2])[pc][dst]      //
                + (*continuationHistory[3])[pc][dst]      //
                + (*continuationHistory[4])[pc][dst]      //
                + (*continuationHistory[5])[pc][dst]      //
                + (*continuationHistory[6])[pc][dst]      //
                + (*continuationHistory[7])[pc][dst];

        if (ssPly < LOW_PLY_SIZE)
            m.value += 8 * (*lowPlyQuietHistory)[ssPly][m.raw()] / (1 + ssPly);

        // Bonus for checks
        if (pos.check(m))
            m.value += 0x4000 * (pos.see(m) >= -75) + 0x1000 * pos.dbl_check(m);

        m.value += 0x1000 * (pos.fork(m) && pos.see(m) >= -50);

        if (pt == KING)
            continue;

        // Penalty for moving to square attacked by lesser piece
        // Bonus for escaping from square attacked by lesser piece
        m.value += piece_value(pt)
                 * ((pos.less_attacks(~ac, pt) & dst)   ? -19 * !(blockers & org)
                    : (threats & org)                   ? +23
                    : (pos.less_attacks(~ac, pt) & org) ? +20
                                                        : 0);

        // Penalty for moving pinner piece
        m.value -= 0x400 * ((pinners & org) && !aligned(kingSq, org, dst));
    }
    return itr;
}

template<>
MovePicker::iterator MovePicker::score<EVA_CAPTURE>(MoveList<EVA_CAPTURE>& moveList) noexcept {

    iterator itr = cur;
    for (auto move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(pos.capture_queenpromo(m));
        assert(m.type_of() != CASTLING);

        auto captured = pos.captured(m);

        m.value = piece_value(captured);
    }
    return itr;
}

template<>
MovePicker::iterator MovePicker::score<EVA_QUIET>(MoveList<EVA_QUIET>& moveList) noexcept {

    Color ac = pos.active_color();

    iterator itr = cur;
    for (auto move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(!pos.capture_queenpromo(m));
        assert(m.type_of() != CASTLING);

        Square dst = m.dst_sq();
        auto   pc  = pos.moved_piece(m);

        m.value = (*quietHistory)[ac][m.raw()]  //
                + (*continuationHistory[0])[pc][dst];

        if (ssPly < LOW_PLY_SIZE)
            m.value += (*lowPlyQuietHistory)[ssPly][m.raw()];
    }
    return itr;
}

template<typename Predicate>
bool MovePicker::select(Predicate&& pred) noexcept {

    for (; !empty(); next())
        if (valid() && pred())
            return true;
    return false;
}

namespace {

template<typename Iterator, typename T, typename Compare>
Iterator
exponential_upper_bound(Iterator begin, Iterator end, const T& value, Compare comp) noexcept {
    Iterator lo_bound = end - 1;

    // Insert at end, if value > last element
    if (comp(*lo_bound, value))
        return end;

    using DiffType = typename std::iterator_traits<Iterator>::difference_type;

    // Exponential backward search
    DiffType step = 1;
    while (!comp(*(lo_bound - 1), value))
    {
        if (step >= (lo_bound - begin))
        {
            lo_bound = begin;
            break;
        }

        lo_bound -= step;
        step <<= 1;
    }

    // Now [lo_bound..end) is a sorted subrange containing the insertion point.
    // binary search inside [lo_bound, end)
    return std::upper_bound(lo_bound, end, value, comp);
}

// Sort moves in descending order.
template<typename Iterator>
void insertion_sort(Iterator begin, Iterator end) noexcept {

    for (Iterator p = begin + 1; p < end; ++p)
    {
        auto value = *p;

        // Find the correct position for 'value' using binary search
        Iterator q = exponential_upper_bound(begin, p, value, std::greater<>{});
        // Move elements to make space for 'value'
        for (Iterator r = p; r != q; --r)
            *r = *(r - 1);
        // Insert the 'value' in its correct position
        *q = value;
    }
}

}  // namespace

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

        cur = endBadCapture = moves.data();
        // NOTE:: endMove is not defined here, it will be set later
        endCur = /* endMove =*/score<ENC_CAPTURE>(moveList);

        insertion_sort(cur, endCur);
    }

        ++stage;
        goto STAGE_SWITCH;

    case STG_ENC_CAPTURE_GOOD :
        if (select([&]() {
                if (pos.see(*cur) >= -cur->value / 18)
                    return true;
                // Store bad captures
                std::iter_swap(endBadCapture++, cur);
                return false;
            }))
            return move();

        ++stage;
        [[fallthrough]];

    case STG_ENC_QUIET_INIT :
        if (quietAllowed)
        {
            MoveList<ENC_QUIET> moveList(pos);

            endCur = endMove = score<ENC_QUIET>(moveList);

            insertion_sort(cur, endCur);
        }

        ++stage;
        [[fallthrough]];

    case STG_ENC_QUIET_GOOD :
        if (quietAllowed)
        {
            for (; !empty(); next())
                if (valid())
                {
                    // Good quiet threshold
                    if (cur->value >= -14000)
                        return move();
                    // Remaining quiets are bad
                    break;
                }
        }

        // Mark the beginning of bad quiets
        begBadQuiet = cur;

        // Prepare the pointers to loop over the bad captures
        cur    = moves.data();
        endCur = endBadCapture;

        ++stage;
        [[fallthrough]];

    case STG_ENC_CAPTURE_BAD :
        if (select([]() { return true; }))
            return move();

        if (quietAllowed)
        {
            // Prepare the pointers to loop over the bad quiets
            cur    = begBadQuiet;
            endCur = endMove;
        }

        ++stage;
        [[fallthrough]];

    case STG_ENC_QUIET_BAD :
        if (quietAllowed && select([]() { return true; }))
            return move();

        return Move::None;

    case STG_EVA_CAPTURE_INIT : {
        MoveList<EVA_CAPTURE> moveList(pos);

        cur    = moves.data();
        endCur = endMove = score<EVA_CAPTURE>(moveList);

        insertion_sort(cur, endCur);
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

        endCur = endMove = score<EVA_QUIET>(moveList);

        insertion_sort(cur, endCur);
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
