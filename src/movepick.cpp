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

namespace {

// Threshold below which insertion sort is used
constexpr std::size_t INSERTION_SORT_THRESHOLD = 52;
// Threshold for considering a move "good enough" to be sorted to the front
constexpr std::int32_t GOOD_QUIET_THRESHOLD = -14000;

ALWAYS_INLINE constexpr bool always_true() noexcept { return true; }

// Unrolled upper_bound implementation for finding the insertion point
template<typename Iterator, typename T, typename Compare>
Iterator upper_bound_unrolled(Iterator RESTRICT beg,
                              Iterator RESTRICT end,
                              const T&          value,
                              Compare           comp) noexcept {
    std::size_t n = end - beg;

    std::size_t idx = n;  // default = n (not found)

    std::size_t i = n;

    // Unroll 8 elements at a time
    for (; idx == n && i >= UNROLL_8; i -= UNROLL_8)
        idx = comp(value, beg[i - 8]) ? i - 8
            : comp(value, beg[i - 7]) ? i - 7
            : comp(value, beg[i - 6]) ? i - 6
            : comp(value, beg[i - 5]) ? i - 5
            : comp(value, beg[i - 4]) ? i - 4
            : comp(value, beg[i - 3]) ? i - 3
            : comp(value, beg[i - 2]) ? i - 2
            : comp(value, beg[i - 1]) ? i - 1
                                      : idx;

    // Unroll 4 elements at a time
    for (; idx == n && i >= UNROLL_4; i -= UNROLL_4)
        idx = comp(value, beg[i - 4]) ? i - 4
            : comp(value, beg[i - 3]) ? i - 3
            : comp(value, beg[i - 2]) ? i - 2
            : comp(value, beg[i - 1]) ? i - 1
                                      : idx;

    // Handle remaining elements
    while (i > 0)
    {
        --i;
        idx = comp(value, beg[i]) ? i : idx;
    }

    return beg + idx;
}

// Sort elements in descending order.
// Stable for all elements.
template<typename Iterator>
void insertion_sort(Iterator RESTRICT beg, Iterator RESTRICT end) noexcept {
    // Iterate over the range starting from the second element
    for (Iterator p = beg + 1; p < end; ++p)
    {
        // Stability: Skip if already in correct position
        if (!ext_move_descending(p[0], p[-1]))
            continue;
        // Move the current element out to avoid multiple copies during shifting
        auto value = std::move(*p);
        // Find insertion position in the sorted subarray [beg, p) upper_bound ensures stability
        Iterator q = upper_bound_unrolled(beg, p, value, ext_move_descending);
        // Shift elements in sorted subarray (q, p] one step to the right to make room at *q
        std::move_backward(q, p, p + 1);
        // Place value into its correct position
        *q = std::move(value);
    }
}

// Sort elements in descending order up to a threshold 'limit'
// leaving elements < limit untouched at their original positions.
// Stable for elements >= limit.
template<typename Iterator>
void partial_insertion_sort(Iterator RESTRICT beg,
                            Iterator RESTRICT end,
                            int               limit = -INT_LIMIT) noexcept {
    auto ext_move_descending_limit = [limit](const ExtMove& em1, const ExtMove& em2) noexcept {
        // Only compare elements >= limit
        if (em1.value < limit)
            return false;  // treat a as "already after" => never move it
        if (em2.value < limit)
            return true;                       // value >= limit goes before elements < limit
        return ext_move_descending(em1, em2);  // usual descending
    };

    // Iterate over the range starting from the second element
    for (Iterator p = beg + 1; p < end; ++p)
    {
        // Skip elements smaller than the limit
        if (p->value < limit)
            continue;
        // Stability: Skip if already in correct position
        if (!ext_move_descending(p[0], p[-1]))
            continue;
        // Move the current element out to avoid multiple copies during shifting
        auto value = std::move(*p);
        // Find insertion position in the sorted subarray [beg, p) upper_bound ensures stability
        Iterator q = upper_bound_unrolled(beg, p, value, ext_move_descending_limit);
        // Shift elements in sorted subarray (q, p] one step to the right to make room at *q
        std::move_backward(q, p, p + 1);
        // Place value into its correct position
        *q = std::move(value);
    }
}

template<typename Iterator>
inline void stable_sort_adaptive(Iterator beg, Iterator end) noexcept {
    if (std::size_t(end - beg) <= INSERTION_SORT_THRESHOLD)
        insertion_sort(beg, end);
    else
        std::stable_sort(beg, end, ext_move_descending);
}

}  // namespace

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
    assert(continuationHistory != nullptr);

    if (pos.checkers_bb() != 0)
    {
        initStage = Stage::EVA_CAPTURE;
        curStage  = Stage{!(ttMove != Move::None)};
    }
    else if (threshold < 0)
    {
        for (std::size_t i = 0; i < CONT_HISTORY_COUNT; ++i)
            assert(continuationHistory[i] != nullptr && "continuationHistory[i] must not be null");

        initStage = Stage::ENC_GOOD_CAPTURE;
        curStage  = Stage{!(ttMove != Move::None)};
    }
    else
    {
        initStage = Stage::QS_CAPTURE;
        curStage  = Stage{!(ttMove != Move::None && pos.capture_promo(ttMove))};
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

template<GenType GT>
void MovePicker::init_stage() noexcept {
    MoveList<GT> moveList(pos);

    cur    = moves.data();
    endCur = score(moveList);

    stable_sort_adaptive(cur, endCur);
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures moves are ordered by Most Valuable Victim (MVV),
// preferring captures moves with a good history.
// Quiets moves are ordered by using the history tables.
template<>
MovePicker::iterator
MovePicker::score<GenType::ENC_CAPTURE>(MoveList<GenType::ENC_CAPTURE>& moveList) noexcept {

    const auto& captureHistoryRef = *captureHistory;

    iterator itr = cur;

    for (Move move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(pos.capture_promo(m));

        Square dstSq      = m.dst_sq();
        Piece  movedPc    = pos.moved_pc(m);
        auto   capturedPt = pos.captured_pt(m);

        m.value = 7 * piece_value(capturedPt)  //
                + captureHistoryRef[+movedPc][dstSq][capturedPt];
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::ENC_QUIET>(MoveList<GenType::ENC_QUIET>& moveList) noexcept {
    Color ac = pos.active_color();

    Bitboard blockersBB = pos.blockers_bb(~ac);
    Bitboard pinnersBB  = pos.pinners_bb();
    Bitboard threatsBB  = pos.threats_bb();

    const auto&        quietHistoryRef        = *quietHistory;
    const auto&        lowPlyQuietHistoryRef  = *lowPlyQuietHistory;
    const auto* const* continuationHistoryPtr = continuationHistory;
    const auto&        pawnHistory            = histories->pawn(pos.pawn_key());

    iterator itr = cur;

    for (Move move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(!pos.capture_promo(m));

        Square orgSq = m.org_sq(), dstSq = m.dst_sq();
        Piece  movedPc = pos.moved_pc(m);
        auto   movedPt = type_of(movedPc);

        std::int64_t value;

        value = 2 * quietHistoryRef[ac][m.raw()];
        value += 2 * pawnHistory[+movedPc][dstSq];
        // Accumulate continuation history entries
        for (std::size_t i = 0; i < CONT_HISTORY_COUNT; ++i)
            value += (*continuationHistoryPtr[i])[+movedPc][dstSq];

        if (ssPly < LOW_PLY_QUIET_SIZE)
            value += 8 * lowPlyQuietHistoryRef[ssPly][m.raw()] / (1 + ssPly);

        // Bonus for checks
        if (pos.check(m))
            value += int(pos.see(m) >= -75) * 0x4000 + int(pos.dbl_check(m)) * 0x1000;

        value += int(pos.fork(m) && pos.see(m) >= -50) * 0x1000;

        // Penalty for moving to square attacked by lesser piece
        // Bonus for escaping from square attacked by lesser piece
        int weight =
          ((pos.acc_less_attacks_bb(movedPt) & dstSq) != 0   ? int((blockersBB & orgSq) == 0) * -19
           : (threatsBB & orgSq) != 0                        ? +23
           : (pos.acc_less_attacks_bb(movedPt) & orgSq) != 0 ? +20
                                                             : 0);
        value += weight * piece_value(movedPt);

        // Penalty for moving pinner piece
        value -=
          int((pinnersBB & orgSq) != 0 && !aligned(pos.square<KING>(~ac), orgSq, dstSq)) * 0x400;

        m.value = std::clamp(value, -INT_LIMIT, +INT_LIMIT);
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::EVA_CAPTURE>(MoveList<GenType::EVA_CAPTURE>& moveList) noexcept {

    iterator itr = cur;

    for (Move move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(pos.capture_promo(m));
        assert(m.type() != MT::CASTLING);

        auto capturedPt = pos.captured_pt(m);

        m.value = piece_value(capturedPt);
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::EVA_QUIET>(MoveList<GenType::EVA_QUIET>& moveList) noexcept {

    Color ac = pos.active_color();

    const auto&        quietHistoryRef        = *quietHistory;
    const auto* const* continuationHistoryPtr = continuationHistory;

    iterator itr = cur;

    for (Move move : moveList)
    {
        auto& m = *itr++;
        m       = move;

        assert(!pos.capture_promo(m));
        assert(m.type() != MT::CASTLING);

        Square dstSq   = m.dst_sq();
        Piece  movedPc = pos.moved_pc(m);

        m.value = quietHistoryRef[ac][m.raw()]  //
                + (*continuationHistoryPtr[0])[+movedPc][dstSq];
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

ALWAYS_INLINE bool MovePicker::good_capture_or_swap() noexcept {
    if (pos.see(*cur) >= -constexpr_round(55.5555e-3 * cur->value))
        return true;
    // Store bad captures
    std::iter_swap(endBadCapture++, cur);
    return false;
}

ALWAYS_INLINE bool MovePicker::above_threshold_capture() const noexcept {
    return pos.see(*cur) >= threshold;
}

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
            init_stage<GenType::EVA_CAPTURE>();
        }
        else
        {
            if (curStage == Stage::ENC_GOOD_CAPTURE)
                endBadCapture = moves.data();

            init_stage<GenType::ENC_CAPTURE>();
        }

        goto STAGE_SWITCH;

    case Stage::ENC_GOOD_CAPTURE :
        if (select([this]() noexcept -> bool { return good_capture_or_swap(); }))
            return move();

        if (!skipQuiets)
        {
            MoveList<GenType::ENC_QUIET> moveList(pos);

            endBadQuiet = endCur = score(moveList);

            partial_insertion_sort(cur, endCur, GOOD_QUIET_THRESHOLD);
        }

        curStage = Stage::ENC_GOOD_QUIET;
        [[fallthrough]];

    case Stage::ENC_GOOD_QUIET :
        for (; !skipQuiets && !empty(); next())
        {
            if (!valid())
                continue;

            // Good quiet threshold
            if (cur->value >= GOOD_QUIET_THRESHOLD)
                return move();

            // Remaining quiets are bad
            break;
        }

        // Mark the beginning of bad quiets
        begBadQuiet = cur;

        // Prepare the pointers to loop over the bad captures
        cur    = moves.data();
        endCur = endBadCapture;

        curStage = Stage::ENC_BAD_CAPTURE;
        [[fallthrough]];

    case Stage::ENC_BAD_CAPTURE :
        if (select(always_true))
            return move();

        if (!skipQuiets)
        {
            // Prepare the pointers to loop over the bad quiets
            cur    = begBadQuiet;
            endCur = endBadQuiet;

            stable_sort_adaptive(cur, endCur);
        }

        curStage = Stage::ENC_BAD_QUIET;
        [[fallthrough]];

    case Stage::ENC_BAD_QUIET :
        if (!skipQuiets && select(always_true))
            return move();

        return Move::None;

    case Stage::EVA_CAPTURE :
        if (select(always_true))
            return move();
        {
            MoveList<GenType::EVA_QUIET> moveList(pos);

            endCur = score(moveList);

            insertion_sort(cur, endCur);
        }

        curStage = Stage::EVA_QUIET;
        [[fallthrough]];

    case Stage::EVA_QUIET :
    case Stage::QS_CAPTURE :
        if (select(always_true))
            return move();

        return Move::None;

    case Stage::PROBCUT :
        if (select([this]() noexcept -> bool { return above_threshold_capture(); }))
            return move();

        return Move::None;

    default :;
    }
    assert(false);
    return Move::None;  // Silence warning
}

}  // namespace DON
