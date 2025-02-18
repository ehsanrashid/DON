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

#include <cassert>
#include <iterator>

#include "bitboard.h"
#include "misc.h"
#include "position.h"

namespace DON {

// Constructors of the MovePicker class. As arguments, pass information
// to decide which class of moves to return, to help sorting the (presumably)
// good moves first, and how important move ordering is at the current node.
MovePicker::MovePicker(const Position&              p,
                       const Move&                  ttm,
                       const History<HCapture>*     captureHist,
                       const History<HQuiet>*       quietHist,
                       const History<HPawn>*        pawnHist,
                       const History<HPieceSq>**    continuationHist,
                       const History<HLowPlyQuiet>* lowPlyQuietHist,
                       std::int16_t                 ply,
                       int                          th) noexcept :
    pos(p),
    ttMove(ttm),
    captureHistory(captureHist),
    quietHistory(quietHist),
    pawnHistory(pawnHist),
    continuationHistory(continuationHist),
    lowPlyQuietHistory(lowPlyQuietHist),
    ssPly(ply),
    threshold(th) {
    assert(ttMove == Move::None || pos.pseudo_legal(ttMove));

    stage = pos.checkers() ? STG_EVA_TT : STG_ENC_TT;
    if (ttMove == Move::None || !(pos.checkers() || threshold < 0 || pos.capture_promo(ttMove)))
        next_stage();
}

MovePicker::MovePicker(const Position&          p,
                       const Move&              ttm,
                       const History<HCapture>* captureHist,
                       int                      th) noexcept :
    pos(p),
    ttMove(ttm),
    captureHistory(captureHist),
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
template<GenType GT>
void MovePicker::score() noexcept {
    static_assert(GT == ENC_CAPTURE || GT == ENC_QUIET || GT == EVA_CAPTURE || GT == EVA_QUIET,
                  "Unsupported generate type");

    Color ac = pos.active_color();

    auto pawnIndex = pawn_index(pos.pawn_key());

    for (auto& m : *this)
        if constexpr (GT == ENC_CAPTURE)
        {
            Square dst = m.dst_sq();

            auto pc       = pos.moved_piece(m);
            auto captured = pos.captured(m);

            m.value = 7 * PIECE_VALUE[captured] + 3 * promotion_value(m, true)  //
                    + (*captureHistory)[pc][dst][captured]                      //
                    + 0x100 * (pos.cap_square() == dst);
        }

        else if constexpr (GT == ENC_QUIET)
        {
            assert(m.type_of() != PROMOTION);

            Square org = m.org_sq(), dst = m.dst_sq();

            auto pc = pos.moved_piece(m);
            auto pt = type_of(pc);

            // Histories
            m.value = 2 * (*quietHistory)[ac][m.org_dst()]    //
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
                m.value += 8 * (*lowPlyQuietHistory)[ssPly][m.org_dst()] / (1 + 2 * ssPly);

            // Bonus for checks
            if (pos.check(m))
                m.value += 0x4000 + 0x1000 * pos.dbl_check(m);

            m.value += 0x1000 * pos.fork(m);

            if (pt == PAWN || pt == KING)
                continue;

            // Bonus for escaping from capture
            m.value += (pos.threatens(ac) & org)
                       ? (pt == QUEEN ? 21200 * !(pos.attacks<ROOK>(~ac) & dst)
                                          + 16150 * !(pos.attacks<MINOR>(~ac) & dst)
                                          + 14450 * !(pos.attacks<PAWN>(~ac) & dst)
                          : pt == ROOK ? 16150 * !(pos.attacks<MINOR>(~ac) & dst)
                                           + 14450 * !(pos.attacks<PAWN>(~ac) & dst)
                                       : 14450 * !(pos.attacks<PAWN>(~ac) & dst))
                       : 0;

            // Malus for putting in en-prise
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

        else if constexpr (GT == EVA_CAPTURE)
        {
            assert(m.type_of() != CASTLING);

            Square dst = m.dst_sq();

            auto pc       = pos.moved_piece(m);
            auto captured = pos.captured(m);

            m.value = 2 * PIECE_VALUE[captured] + promotion_value(m, true)
                    + (*captureHistory)[pc][dst][captured];
        }

        else  //if constexpr (GT == EVA_QUIET)
        {
            assert(m.type_of() != CASTLING);

            Square dst = m.dst_sq();

            auto pc = pos.moved_piece(m);

            m.value = (*quietHistory)[ac][m.org_dst()]    //
                    + (*pawnHistory)[pawnIndex][pc][dst]  //
                    + (*continuationHistory[0])[pc][dst];
        }
}

// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void MovePicker::sort_partial(int limit) noexcept {
    if (begin() == end())
        return;
    for (auto s = begin(), p = std::next(begin()); p != end(); ++p)
        if (p->value >= limit)
        {
            auto em = *p;

            *p = *++s;

            // Shift elements until the correct position for 'em' is found
            auto q = s;
            // Unroll 8x
            for (; q - 8 >= begin() && *(q - 8) < em; q -= 8)
            {
                *(q)     = *(q - 1);
                *(q - 1) = *(q - 2);
                *(q - 2) = *(q - 3);
                *(q - 3) = *(q - 4);
                *(q - 4) = *(q - 5);
                *(q - 5) = *(q - 6);
                *(q - 6) = *(q - 7);
                *(q - 7) = *(q - 8);
            }
            // Unroll 4x
            for (; q - 4 >= begin() && *(q - 4) < em; q -= 4)
            {
                *(q)     = *(q - 1);
                *(q - 1) = *(q - 2);
                *(q - 2) = *(q - 3);
                *(q - 3) = *(q - 4);
            }
            // Handle remaining shift safely
            for (; q - 1 >= begin() && *(q - 1) < em; --q)
                *(q) = *(q - 1);

            *q = em;  // Insert the element in its correct position
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
        extBeg = extMoves.begin();

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
            extBeg = extMoves.begin();

            score<ENC_QUIET>();
            assert(threshold < 0);
            sort_partial(threshold);
        }

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
        badCapBeg = badCapMoves.begin();

        next_stage();
        [[fallthrough]];

    case STG_ENC_CAPTURE_BAD :
        if (badCapBeg != badCapMoves.end())
        {
            assert(is_ok(*badCapBeg));
            return *badCapBeg++;
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
        extBeg = extMoves.begin();

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
            extBeg = extMoves.begin();

            score<EVA_QUIET>();
            sort_partial();
        }

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
