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
#include <type_traits>
#include <utility>

#include "bitboard.h"
#include "misc.h"
#include "position.h"

namespace DON {

// History
History<HCapture>      CaptureHistory;
History<HQuiet>        QuietHistory;
History<HPawn>         PawnHistory;
History<HContinuation> ContinuationHistory[2][2];
History<HLowPlyQuiet>  LowPlyQuietHistory;

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

    stage = pos.checkers() ? STG_EVA_TT : STG_ENC_TT;
    if (ttMove == Move::None || !(pos.checkers() || threshold < 0 || pos.capture_promo(ttMove)))
        next_stage();
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

    stage = STG_PROBCUT_TT;
    if (ttMove == Move::None || !(pos.capture_promo(ttMove) && pos.see(ttMove) >= threshold))
        next_stage();
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures moves are ordered by Most Valuable Victim (MVV),
// preferring captures moves with a good history.
// Quiets moves are ordered by using the history tables.
template<>
void MovePicker::score<ENC_CAPTURE>() noexcept {

    for (auto& m : *this)
    {
        Square dst      = m.dst_sq();
        auto   pc       = pos.moved_piece(m);
        auto   captured = pos.captured(m);

        m.value = 7 * PIECE_VALUE[captured] + 3 * promotion_value(m, true)  //
                + CaptureHistory[pc][dst][captured]                         //
                + 0x100 * (pos.cap_square() == dst);
    }
}

template<>
void MovePicker::score<ENC_QUIET>() noexcept {
    Color ac        = pos.active_color();
    auto  pawnIndex = pawn_index(pos.pawn_key());

    for (auto& m : *this)
    {
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
            m.value += 8 * LowPlyQuietHistory[ssPly][m.org_dst()] / (1 + 2 * ssPly);

        if (pos.check(m))
            m.value += 0x4000 + 0x1000 * pos.dbl_check(m);

        m.value += 0x1000 * pos.fork(m);

        if (pt == PAWN || pt == KING)
            continue;

        m.value += (pos.threatens(ac) & org)
                   ? (pt == QUEEN ? 21200 * !(pos.attacks<ROOK>(~ac) & dst)
                                      + 16150 * !(pos.attacks<MINOR>(~ac) & dst)
                                      + 14450 * !(pos.attacks<PAWN>(~ac) & dst)
                      : pt == ROOK ? 16150 * !(pos.attacks<MINOR>(~ac) & dst)
                                       + 14450 * !(pos.attacks<PAWN>(~ac) & dst)
                                   : 14450 * !(pos.attacks<PAWN>(~ac) & dst))
                   : 0;

        m.value -= !(pos.blockers(~ac) & org)
                   ? (pt == QUEEN ? 24665 * !!(pos.attacks<ROOK>(~ac) & dst)
                                      + 13435 * !!(pos.attacks<MINOR>(~ac) & dst)
                                      + 10900 * !!(pos.attacks<PAWN>(~ac) & dst)
                      : pt == ROOK ? 13435 * !!(pos.attacks<MINOR>(~ac) & dst)
                                       + 10900 * !!(pos.attacks<PAWN>(~ac) & dst)
                                   : 10900 * !!(pos.attacks<PAWN>(~ac) & dst))
                   : 0;

        m.value -= 0x400 * ((pos.pinners() & org) && !aligned(pos.king_square(~ac), org, dst));
    }
}

template<>
void MovePicker::score<EVA_CAPTURE>() noexcept {

    for (auto& m : *this)
    {
        assert(m.type_of() != CASTLING);

        Square dst      = m.dst_sq();
        auto   pc       = pos.moved_piece(m);
        auto   captured = pos.captured(m);

        m.value = 2 * PIECE_VALUE[captured] + promotion_value(m, true)  //
                + CaptureHistory[pc][dst][captured];
    }
}

template<>
void MovePicker::score<EVA_QUIET>() noexcept {
    Color ac        = pos.active_color();
    auto  pawnIndex = pawn_index(pos.pawn_key());

    for (auto& m : *this)
    {
        assert(m.type_of() != CASTLING);

        Square dst = m.dst_sq();
        auto   pc  = pos.moved_piece(m);

        m.value = QuietHistory[ac][m.org_dst()]    //
                + PawnHistory[pawnIndex][pc][dst]  //
                + (*continuationHistory[0])[pc][dst];
    }
}

// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void MovePicker::sort_partial(int limit) noexcept {
    auto b = begin(), e = end();
    if (b == e)
        return;
    for (auto s = b, p = b + 1; p != e; ++p)
        if (p->value >= limit)
        {
            auto m = std::move(*p);

            *p = std::move(*++s);

            // Find the correct position for 'm' using binary search
            auto q = std::upper_bound(b, s, m, std::greater<>{});
            // Move elements to make space for 'm'
            std::move_backward(q, s, s + 1);
            // Insert the element in its correct position
            *q = std::move(m);
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
    case STG_PROBCUT_TT :
        next_stage();
        return ttMove;

    case STG_ENC_CAPTURE_INIT :
    case STG_PROBCUT_INIT :
        extEnd = generate<ENC_CAPTURE>(extMoves, pos);
        extCur = extMoves.begin();

        score<ENC_CAPTURE>();
        sort_partial();

        next_stage();
        goto STAGE_SWITCH;

    case STG_ENC_CAPTURE_GOOD :
        while (begin() != end())
        {
            auto& cur = current();
            next();
            if (is_ok(cur))
            {
                if (threshold == 0 || pos.see(cur) >= -55.5555e-3 * cur.value)
                    return cur;
                // Store bad captures
                badCapMoves.push_back(cur);
            }
        }

        next_stage();
        [[fallthrough]];

    case STG_ENC_QUIET_INIT :
        extMoves.clear();
        if (quietPick)
        {
            extEnd = generate<ENC_QUIET>(extMoves, pos);
            extCur = extMoves.begin();

            score<ENC_QUIET>();
            assert(threshold < 0);
            sort_partial(threshold);
        }
        else
            extCur = extEnd;

        next_stage();
        [[fallthrough]];

    case STG_ENC_QUIET_GOOD :
        if (quietPick)
            while (begin() != end())
            {
                auto& cur = current();
                if (is_ok(cur))
                {
                    if (cur.value >= threshold)
                    {
                        next();
                        return cur;
                    }
                    // Remaining quiets are bad
                    break;
                }
                next();
            }

        // Prepare to loop over the bad captures
        badCapCur = badCapMoves.begin();
        badCapEnd = badCapMoves.end();

        next_stage();
        [[fallthrough]];

    case STG_ENC_CAPTURE_BAD :
        if (badCapCur != badCapEnd)
        {
            assert(is_ok(*badCapCur));
            return *badCapCur++;
        }

        if (quietPick)
            sort_partial();

        next_stage();
        [[fallthrough]];

    case STG_ENC_QUIET_BAD :
        if (quietPick)
            while (begin() != end())
            {
                Move cur = current();
                next();
                if (is_ok(cur))
                    return cur;
            }
        return Move::None;

    case STG_EVA_CAPTURE_INIT :
        extEnd = generate<EVA_CAPTURE>(extMoves, pos);
        extCur = extMoves.begin();

        score<EVA_CAPTURE>();
        sort_partial();

        next_stage();
        [[fallthrough]];

    case STG_EVA_CAPTURE_ALL :
        while (begin() != end())
        {
            Move cur = current();
            next();
            if (is_ok(cur))
                return cur;
        }

        next_stage();
        [[fallthrough]];

    case STG_EVA_QUIET_INIT :
        extMoves.clear();
        if (quietPick)
        {
            extEnd = generate<EVA_QUIET>(extMoves, pos);
            extCur = extMoves.begin();

            score<EVA_QUIET>();
            sort_partial();
        }
        else
            extCur = extEnd;

        next_stage();
        [[fallthrough]];

    case STG_EVA_QUIET_ALL :
        if (quietPick)
            while (begin() != end())
            {
                Move cur = current();
                next();
                if (is_ok(cur))
                    return cur;
            }
        return Move::None;

    case STG_PROBCUT_ALL :
        while (begin() != end())
        {
            Move cur = current();
            next();
            if (is_ok(cur))
                if (pos.see(cur) >= threshold)
                    return cur;
        }
        return Move::None;

    case STG_NONE :;
    }
    assert(false);
    return Move::None;  // Silence warning
}

}  // namespace DON
