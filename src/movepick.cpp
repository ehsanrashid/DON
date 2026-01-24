/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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

// MovePicker constructor for the main search and for the quiescence search
MovePicker::MovePicker(const Position&                  p,
                       Move                             ttm,
                       const Histories*                 hists,
                       const History<HType::CAPTURE>*   captureHist,
                       const History<HType::QUIET>*     quietHist,
                       const History<HType::LOW_QUIET>* lowPlyQuietHist,
                       const History<HType::PIECE_SQ>** continuationHist,
                       std::int16_t                     ply,
                       int                              th) noexcept :
    pos(p),
    ttMove(ttm),
    histories(hists),
    captureHistory(captureHist),
    quietHistory(quietHist),
    lowPlyQuietHistory(lowPlyQuietHist),
    continuationHistory(continuationHist),
    ssPly(ply),
    threshold(th) {
    assert(ttMove == Move::None || pos.legal(ttMove));

    if (pos.checkers_bb() != 0)
    {
        initStage = Stage::EVA_CAPTURE;
        curStage  = Stage(!(ttMove != Move::None));
    }
    else if (threshold < 0)
    {
        initStage = Stage::ENC_GOOD_CAPTURE;
        curStage  = Stage(!(ttMove != Move::None));
    }
    else
    {
        initStage = Stage::QS_CAPTURE;
        curStage  = Stage(!(ttMove != Move::None && pos.capture_promo(ttMove)));
    }
}

// MovePicker constructor for ProbCut:
// Generate captures with Static Exchange Evaluation (SEE) >= threshold.
MovePicker::MovePicker(const Position&                p,
                       Move                           ttm,
                       const History<HType::CAPTURE>* captureHist,
                       int                            th) noexcept :
    pos(p),
    ttMove(ttm),
    captureHistory(captureHist),
    threshold(th) {
    assert(pos.checkers_bb() == 0);
    assert(ttMove == Move::None || pos.legal(ttMove));

    initStage = Stage::PROBCUT;
    curStage  = Stage(!(ttMove != Move::None && pos.capture_promo(ttMove)));
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures moves are ordered by Most Valuable Victim (MVV),
// preferring captures moves with a good history.
// Quiets moves are ordered by using the history tables.
template<>
MovePicker::iterator
MovePicker::score<GenType::ENC_CAPTURE>(MoveList<GenType::ENC_CAPTURE>& moveList) noexcept {

    iterator itr = cur;

    for (const auto move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(pos.capture_promo(m));

        const Square dstSq      = m.dst_sq();
        const auto   movedPc    = pos.moved_pc(m);
        const auto   capturedPt = pos.captured_pt(m);

        m.value = 7 * piece_value(capturedPt)  //
                + (*captureHistory)[+movedPc][dstSq][capturedPt];
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::ENC_QUIET>(MoveList<GenType::ENC_QUIET>& moveList) noexcept {

    const Color ac = pos.active_color();

    const Bitboard blockersBB = pos.blockers_bb(~ac);
    const Bitboard pinnersBB  = pos.pinners_bb();
    const Bitboard threatsBB  = pos.threats_bb();

    Key pawnKey = pos.pawn_key();

    iterator itr = cur;

    for (const auto move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(!pos.capture_promo(m));

        const Square orgSq = m.org_sq(), dstSq = m.dst_sq();
        const Piece  movedPc = pos.moved_pc(m);
        const auto   movedPt = type_of(movedPc);

        m.value = 2 * (*quietHistory)[ac][m.raw()]               //
                + 2 * histories->pawn(pawnKey)[+movedPc][dstSq]  //
                + (*continuationHistory[0])[+movedPc][dstSq]     //
                + (*continuationHistory[1])[+movedPc][dstSq]     //
                + (*continuationHistory[2])[+movedPc][dstSq]     //
                + (*continuationHistory[3])[+movedPc][dstSq]     //
                + (*continuationHistory[4])[+movedPc][dstSq]     //
                + (*continuationHistory[5])[+movedPc][dstSq]     //
                + (*continuationHistory[6])[+movedPc][dstSq]     //
                + (*continuationHistory[7])[+movedPc][dstSq];

        if (ssPly < LOW_PLY_QUIET_SIZE)
            m.value += 8 * (*lowPlyQuietHistory)[ssPly][m.raw()] / (1 + ssPly);

        // Bonus for checks
        if (pos.check(m))
            m.value += int(pos.see(m) >= -75) * 0x4000 + int(pos.dbl_check(m)) * 0x1000;

        m.value += int(pos.fork(m) && pos.see(m) >= -50) * 0x1000;

        // Penalty for moving to square attacked by lesser piece
        // Bonus for escaping from square attacked by lesser piece
        int weight =
          ((pos.acc_less_attacks_bb(movedPt) & dstSq) != 0   ? int((blockersBB & orgSq) == 0) * -19
           : (threatsBB & orgSq) != 0                        ? +23
           : (pos.acc_less_attacks_bb(movedPt) & orgSq) != 0 ? +20
                                                             : 0);
        m.value += weight * piece_value(movedPt);

        // Penalty for moving pinner piece
        m.value -=
          int((pinnersBB & orgSq) != 0 && !aligned(pos.square<KING>(~ac), orgSq, dstSq)) * 0x400;
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::EVA_CAPTURE>(MoveList<GenType::EVA_CAPTURE>& moveList) noexcept {

    iterator itr = cur;

    for (const auto move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(pos.capture_promo(m));
        assert(m.type() != MT::CASTLING);

        const auto capturedPt = pos.captured_pt(m);

        m.value = piece_value(capturedPt);
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::EVA_QUIET>(MoveList<GenType::EVA_QUIET>& moveList) noexcept {

    const Color ac = pos.active_color();

    iterator itr = cur;

    for (const auto move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(!pos.capture_promo(m));
        assert(m.type() != MT::CASTLING);

        const Square dstSq   = m.dst_sq();
        const Piece  movedPc = pos.moved_pc(m);

        m.value = (*quietHistory)[ac][m.raw()]  //
                + (*continuationHistory[0])[+movedPc][dstSq];
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

constexpr auto ext_move_descending(const ExtMove& em1, const ExtMove& em2) noexcept {
    return em1 > em2;
}

template<typename Iterator, typename T, typename Compare>
Iterator
exponential_upper_bound(Iterator beg, Iterator end, const T& value, Compare comp) noexcept {
    // Handle empty range
    //if (beg == end)
    //    return end;

    Iterator lst = end - 1;
    // Special case: If value goes at the very end
    //if (!comp(value, *lst))
    //    return end;

    // Exponential backward search from end
    Iterator low, hig;

    low = hig = lst;

    std::size_t step = 1;

    while (low != beg)
    {
        Iterator pre = step < std::size_t(low - beg) ? low - step : beg;

        // If *pre <= value, found the range
        if (!comp(value, *pre))
        {
            hig = low;
            low = pre + 1;
            break;
        }

        // Continue searching backward
        low = pre;
        step <<= 1;
    }

    // Now [low..hig) is a sorted subrange containing the insertion point.
    // Binary search in the found range [low, hig)
    return std::upper_bound(low, hig, value, comp);
}

template<typename Iterator>
void insertion_sort(Iterator beg, Iterator end) noexcept {
    for (Iterator p = beg + 1; p < end; ++p)
    {
        // Stability: Early exit if already in correct position
        if (/* p == beg || */ !ext_move_descending(*p, *(p - 1)))
            continue;

        auto value = std::move(*p);

        // Find the correct position for 'value' using binary search
        const Iterator q = exponential_upper_bound(beg, p, value, ext_move_descending);

        // Shift elements using move
        for (Iterator r = p; r != q; --r)
            *r = std::move(*(r - 1));

        // Insert the 'value' in its correct position
        *q = std::move(value);
    }
}

}  // namespace

// Most important method of the MovePicker class.
// It emits a new legal move every time it is called until there are no more moves left,
// picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move() noexcept {

STAGE_SWITCH:
    switch (curStage)
    {
    case Stage::TT :
        curStage = Stage::INIT;

        return ttMove;

    case Stage::INIT :
        curStage = initStage;

        if (curStage == Stage::EVA_CAPTURE)
        {
            MoveList<GenType::EVA_CAPTURE> moveList(pos);

            cur    = moves.data();
            endCur = score<GenType::EVA_CAPTURE>(moveList);
        }
        else
        {
            MoveList<GenType::ENC_CAPTURE> moveList(pos);

            cur = endBadCapture = moves.data();
            endCur              = score<GenType::ENC_CAPTURE>(moveList);
        }

        insertion_sort(cur, endCur);

        goto STAGE_SWITCH;

    case Stage::ENC_GOOD_CAPTURE :
        if (select([&]() {
                if (pos.see(*cur) >= -cur->value / 18)
                    return true;
                // Store bad captures
                std::iter_swap(endBadCapture++, cur);
                return false;
            }))
            return move();

        if (!skipQuiets)
        {
            MoveList<GenType::ENC_QUIET> moveList(pos);

            endCur = endBadQuiet = score<GenType::ENC_QUIET>(moveList);

            insertion_sort(cur, endCur);
        }

        curStage = Stage::ENC_GOOD_QUIET;
        [[fallthrough]];

    case Stage::ENC_GOOD_QUIET :
        if (!skipQuiets)
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

        curStage = Stage::ENC_BAD_CAPTURE;
        [[fallthrough]];

    case Stage::ENC_BAD_CAPTURE :
        if (select([]() { return true; }))
            return move();

        if (!skipQuiets)
        {
            // Prepare the pointers to loop over the bad quiets
            cur    = begBadQuiet;
            endCur = endBadQuiet;
        }

        curStage = Stage::ENC_BAD_QUIET;
        [[fallthrough]];

    case Stage::ENC_BAD_QUIET :
        if (!skipQuiets && select([]() { return true; }))
            return move();

        return Move::None;

    case Stage::EVA_CAPTURE :
        if (select([]() { return true; }))
            return move();

        {
            MoveList<GenType::EVA_QUIET> moveList(pos);

            endCur = score<GenType::EVA_QUIET>(moveList);

            insertion_sort(cur, endCur);
        }

        curStage = Stage::EVA_QUIET;
        [[fallthrough]];

    case Stage::EVA_QUIET :
    case Stage::QS_CAPTURE :
        if (select([]() { return true; }))
            return move();

        return Move::None;

    case Stage::PROBCUT :
        if (select([&]() { return pos.see(*cur) >= threshold; }))
            return move();

        return Move::None;

    default :;
    }
    assert(false);
    return Move::None;  // Silence warning
}

}  // namespace DON
