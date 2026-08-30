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
#include <limits>

#if defined(USE_AVX512)
    #include <immintrin.h>
#endif

#include "attacks.h"
#include "bitboard.h"
#include "position.h"

namespace DON {

namespace {

// Threshold below which insertion sort is used
constexpr usize INSERTION_SORT_THRESHOLD = 52;
// Threshold for considering a move "good enough" to be sorted to the front
constexpr i32 GOOD_QUIET_THRESHOLD = -14000;

ALWAYS_INLINE constexpr bool always_true() noexcept { return true; }

#if defined(USE_AVX512)
// Broadcast an ExtMove's move and value across all 16 lanes
void splat_extmove(const ExtMove& em, __m512i& moves, __m512i& values) noexcept {
    moves  = _mm512_set1_epi32(em.raw());
    values = _mm512_set1_epi32(em.value);
}

// Maintains up to 16 ExtMoves in descending order by value using AVX-512
struct ExtMoveSorter final {
   public:
    static_assert(sizeof(ExtMove) == 8);

    explicit ExtMoveSorter(const ExtMove& em) noexcept {
        splat_extmove(em, sortedMoves, sortedValues);

        // Initialize unused lanes with the lowest possible value
        sortedValues =
          _mm512_mask_set1_epi32(sortedValues, ~__mmask16(1), std::numeric_limits<int>::min());
    }

    void insert(const ExtMove& em) noexcept {
        __m512i moves, values;
        splat_extmove(em, moves, values);

        // Find the insertion position and create a mask for the lanes to shift right
        assert(em.value != std::numeric_limits<int>::min());
        const __mmask16 expand = _kadd_mask16(_mm512_cmplt_epi32_mask(sortedValues, values), -1);

        sortedValues = _mm512_mask_expand_epi32(values, expand, sortedValues);
        sortedMoves  = _mm512_mask_expand_epi32(moves, expand, sortedMoves);
    }

    void write_sorted(ExtMove* const ems, const isize count) const noexcept {
        assert(0 <= count && count <= ElementMax);

        write_chunk(ems, count, 0,
                    _mm512_setr_epi32(0, 16, 1, 17, 2, 18, 3, 19,  //
                                      4, 20, 5, 21, 6, 22, 7, 23));
        write_chunk(ems, count, 8,
                    _mm512_setr_epi32(8, 24, 9, 25, 10, 26, 11, 27,  //
                                      12, 28, 13, 29, 14, 30, 15, 31));
    }

    static constexpr int ElementMax = 16;

   private:
    // Moves and values are stored separately, so interleave them back into ExtMoves
    void write_chunk(ExtMove* const ems,
                     const isize    count,
                     const isize    offset,
                     const __m512i  indices) const noexcept {
        const __m512i extMoves = _mm512_permutex2var_epi32(sortedMoves, indices, sortedValues);

        const isize storeCount = count - offset;

        if (storeCount > 0)
            _mm512_mask_storeu_epi64(ems + offset, __mmask8((1u << storeCount) - 1), extMoves);
    }

    __m512i sortedMoves;
    __m512i sortedValues;
};
#endif

// Unrolled upper_bound implementation for finding the insertion point
template<typename Iterator, typename T, typename Compare>
Iterator upper_bound_unrolled(const Iterator beg,
                              const Iterator end,
                              const T&       value,
                              const Compare  comp) noexcept {
    const usize n = end - beg;

    Iterator ins = end;  // default = end (not found)

    usize i = n;

    // Process blocks of 8 elements
    while (ins == end && i >= BLOCK_8)
    {
        i -= BLOCK_8;

        const Iterator base = beg + i;

        ins = comp(value, base[0]) ? base + 0
            : comp(value, base[1]) ? base + 1
            : comp(value, base[2]) ? base + 2
            : comp(value, base[3]) ? base + 3
            : comp(value, base[4]) ? base + 4
            : comp(value, base[5]) ? base + 5
            : comp(value, base[6]) ? base + 6
            : comp(value, base[7]) ? base + 7
                                   : ins;
    }
    // Process blocks of 4 elements
    while (ins == end && i >= BLOCK_4)
    {
        i -= BLOCK_4;

        const Iterator base = beg + i;

        ins = comp(value, base[0]) ? base + 0
            : comp(value, base[1]) ? base + 1
            : comp(value, base[2]) ? base + 2
            : comp(value, base[3]) ? base + 3
                                   : ins;
    }
    // Handle remaining elements
    while (i >= 1)
    {
        --i;

        const Iterator base = beg + i;

        if (comp(value, *base))
            ins = base;
    }

    return ins;
}

// Sort elements in descending order.
// Stable for all elements.
template<typename Iterator>
void insertion_sort(const Iterator beg, const Iterator end) noexcept {
    if (end - beg <= 1)
        return;

    Iterator p = beg + 1;

#if defined(USE_AVX512)
    ExtMoveSorter sorter(*beg);

    for (; p != end && p - beg < ExtMoveSorter::ElementMax; ++p)
        sorter.insert(*p);

    sorter.write_sorted(beg, p - beg);
#endif

    // Insert remaining elements into the sorted prefix
    for (; p != end; ++p)
    {
        // Stability: Skip if already in correct position
        if (!ext_move_descending(p[0], p[-1]))
            continue;
        // Copy the current element before shifting the sorted range
        const auto value = *p;
        // Find insertion position in the sorted subarray [beg, p). upper_bound preserves stability
        Iterator q = upper_bound_unrolled(beg, p, value, ext_move_descending);
        // Shift elements in sorted subarray [q, p) one step to the right to make room at *q
        std::copy_backward(q, p, p + 1);
        // Place the element into its correct position
        *q = value;
    }
}

// Partially sort elements >= limit in descending order,
// preserving the relative order of elements below limit.
template<typename Iterator>
void partial_insertion_sort(const Iterator beg,
                            const Iterator end,
                            const int      limit = std::numeric_limits<int>::min()) noexcept {
    if (end - beg <= 1)
        return;

    Iterator p = beg + 1;

#if defined(USE_AVX512)
    ExtMoveSorter sorter(*beg);

    Iterator sortedEnd = beg;
    // Sort qualifying elements with AVX-512 while the sorter has capacity.
    for (; p != end && sortedEnd - beg + 1 < ExtMoveSorter::ElementMax; ++p)
    {
        if (p->value < limit)
            continue;

        const auto value = *p;

        ++sortedEnd;
        std::copy_backward(sortedEnd, p, p + 1);
        *sortedEnd = value;

        sorter.insert(value);
    }

    sorter.write_sorted(beg, sortedEnd - beg + 1);
#endif

    const auto ext_move_descending_limit = [limit](const ExtMove& em1,
                                                   const ExtMove& em2) noexcept {
        // Place elements below the limit after qualifying elements
        if (em1.value < limit)
            return false;
        // Qualifying elements always come before elements below the limit
        if (em2.value < limit)
            return true;
        // Otherwise, use the normal descending-order comparison
        return ext_move_descending(em1, em2);
    };

    // Insert remaining qualifying elements into the sorted prefix
    for (; p != end; ++p)
    {
        // Skip elements below the limit
        if (p->value < limit)
            continue;
        // Stability: Skip if already in correct position
        if (!ext_move_descending(p[0], p[-1]))
            continue;
        // Copy the current element before shifting the sorted range
        const auto value = *p;
        // Find insertion position in the sorted subarray [beg, p). upper_bound preserves stability
        Iterator q = upper_bound_unrolled(beg, p, value, ext_move_descending_limit);
        // Shift elements in sorted subarray [q, p) one step to the right to make room at *q
        std::copy_backward(q, p, p + 1);
        // Place value into its correct position
        *q = value;
    }
}

template<typename Iterator>
ALWAYS_INLINE void adaptive_stable_sort(const Iterator beg, const Iterator end) noexcept {
    if (static_cast<usize>(end - beg) <= INSERTION_SORT_THRESHOLD)
        insertion_sort(beg, end);
    else
        std::stable_sort(beg, end, ext_move_descending);
}

}  // namespace

// Constructors of the MovePicker class. As arguments, pass information
// to decide which class of moves to return, to help sorting the (presumably)
// good moves first, and how important move ordering is at the current node.

// MovePicker constructor for the main search and for the quiescence search
MovePicker::MovePicker(const Position&                 p,
                       const Move                      ttm,
                       const CaptureHistory* const     captureHist,
                       const QuietHistory* const       quietHist,
                       const LowPlyQuietHistory* const lowPlyQuietHist,
                       const PieceSqHistory** const    continuationHist,
                       const AtomicHistories* const    atomicHists,
                       const u16                       ply,
                       const int                       th) noexcept :
    pos(p),
    ttMove(ttm),
    captureHistory(captureHist),
    quietHistory(quietHist),
    lowPlyQuietHistory(lowPlyQuietHist),
    continuationHistory(continuationHist),
    atomicHistories(atomicHists),
    ssPly(ply),
    threshold(th),
    moves() {
    assert(ttMove == Move::None || pos.legal(ttMove));
    assert(continuationHistory != nullptr);

    if (pos.checkers_bb() != 0)
    {
        initStage = Stage::EVA_CAPTURE;
        curStage  = Stage{!(ttMove != Move::None)};
    }
    else if (threshold < 0)
    {
        for (usize i = 0; i < CONT_HISTORY_COUNT; ++i)
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
MovePicker::MovePicker(const Position&             p,
                       const Move                  ttm,
                       const CaptureHistory* const captureHist,
                       const int                   th) noexcept :
    pos(p),
    ttMove(ttm),
    captureHistory(captureHist),
    threshold(th),
    moves() {
    assert(pos.checkers_bb() == 0);
    assert(ttMove == Move::None || pos.legal(ttMove));

    initStage = Stage::PROBCUT;
    curStage  = Stage(!(ttMove != Move::None && pos.capture_promo(ttMove)));
}

template<GenType GT>
void MovePicker::init() noexcept {
    MoveList<GT> moveList(pos);

    cur    = moves.data();
    curEnd = score(moveList);

    adaptive_stable_sort(cur, curEnd);
}

// Assigns a numerical value to each move in a list, used for sorting.
// Captures moves are ordered by Most Valuable Victim (MVV),
// preferring captures moves with a good history.
// Quiet moves are ordered by using the history tables.
template<>
MovePicker::iterator
MovePicker::score<GenType::ENC_CAPTURE>(const MoveList<GenType::ENC_CAPTURE>& moveList) noexcept {

    const auto& captureHistoryRef = *captureHistory;

    auto itr = cur;

    for (const Move m : moveList)
    {
        assert(pos.capture_promo(m));

        const Square dstSq      = m.dst_sq();
        const Piece  movedPc    = pos.moved_pc(m);
        const auto   capturedPt = pos.captured_pt(m);

        auto& em = *itr++;
        em       = m;
        em.value = 7 * piece_value(capturedPt)  //
                 + captureHistoryRef[+movedPc][dstSq][capturedPt];
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::ENC_QUIET>(const MoveList<GenType::ENC_QUIET>& moveList) noexcept {
    const Color ac = pos.active_color();

    const Bitboard blockersBB = pos.blockers_bb(~ac);
    const Bitboard pinnersBB  = pos.pinners_bb();
    const Bitboard threatsBB  = pos.threats_bb();

    const auto&        quietHistoryRef        = *quietHistory;
    const auto&        lowPlyQuietHistoryRef  = *lowPlyQuietHistory;
    const auto** const continuationHistoryPtr = continuationHistory;
    const auto&        pawnEntryRef           = (*atomicHistories).pawn_entry(pos);

    auto itr = cur;

    for (const Move m : moveList)
    {
        assert(!pos.capture_promo(m));

        const Square orgSq = m.org_sq(), dstSq = m.dst_sq();
        const Piece  movedPc = pos.moved_pc(m);
        const auto   movedPt = type_of(movedPc);

        i64 value;

        value = 2 * quietHistoryRef[ac][m.raw()];

        if (ssPly < LOW_PLY_SIZE)
            value += 8 * lowPlyQuietHistoryRef[ssPly][m.raw()] / (1 + ssPly);

        // Accumulate continuation history entries
        for (usize i = 0; i < CONT_HISTORY_COUNT; ++i)
            value += (*continuationHistoryPtr[i])[+movedPc][dstSq];

        value += 2 * pawnEntryRef[+movedPc][dstSq];

        // Bonus for checks
        if (pos.check(m))
            value += int(pos.see(m) >= -75) * 0x4000 + int(pos.dbl_check(m)) * 0x1000;

        value += int(pos.fork(m) && pos.see(m) >= -50) * 0x1000;

        // Penalty for moving to square attacked by lesser piece
        // Bonus for escaping from square attacked by lesser piece
        const Bitboard accLessAttacksBB = pos.acc_less_attacks_bb(movedPt);

        const int weight = (accLessAttacksBB & dstSq) != 0 ? ((blockersBB & orgSq) != 0 ? -3 : -20)
                         : (threatsBB & orgSq) != 0        ? +23
                         : (accLessAttacksBB & orgSq) != 0 ? +20
                                                           : 0;
        value += weight * piece_value(movedPt);

        // Penalty for moving pinner piece
        value -=
          int((pinnersBB & orgSq) != 0 && !aligned(pos.square<KING>(~ac), orgSq, dstSq)) * 0x400;

        auto& em = *itr++;
        em       = m;
        em.value = std::clamp(value, -INT_LIMIT, +INT_LIMIT);
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::EVA_CAPTURE>(const MoveList<GenType::EVA_CAPTURE>& moveList) noexcept {

    auto itr = cur;

    for (const Move m : moveList)
    {
        assert(pos.capture_promo(m));
        assert(m.type() != MT::CASTLING);

        const auto capturedPt = pos.captured_pt(m);

        auto& em = *itr++;
        em       = m;
        em.value = piece_value(capturedPt);
    }

    return itr;
}

template<>
MovePicker::iterator
MovePicker::score<GenType::EVA_QUIET>(const MoveList<GenType::EVA_QUIET>& moveList) noexcept {

    const Color ac = pos.active_color();

    const auto&        quietHistoryRef        = *quietHistory;
    const auto* const* continuationHistoryPtr = continuationHistory;

    auto itr = cur;

    for (const Move m : moveList)
    {
        assert(!pos.capture_promo(m));
        assert(m.type() != MT::CASTLING);

        const Square dstSq   = m.dst_sq();
        const Piece  movedPc = pos.moved_pc(m);

        auto& em = *itr++;
        em       = m;
        em.value = quietHistoryRef[ac][m.raw()]  //
                 + (*continuationHistoryPtr[0])[+movedPc][dstSq];
    }

    return itr;
}

template<typename Predicate>
bool MovePicker::select(const Predicate& pred) noexcept {

    for (; cur != curEnd; ++cur)
        if (*cur != ttMove && pred())
            return true;

    return false;
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
            init<GenType::EVA_CAPTURE>();
        }
        else
        {
            if (curStage == Stage::ENC_GOOD_CAPTURE)
                badCaptureEnd = moves.data();

            init<GenType::ENC_CAPTURE>();
        }

        // Init done, now dispatch
        goto STAGE_SWITCH;

    case Stage::ENC_GOOD_CAPTURE :
        if (select([this]() noexcept -> bool { return good_capture_or_swap(); }))
            return *cur++;

        if (!quietsSkip)
        {
            MoveList<GenType::ENC_QUIET> moveList(pos);

            badQuietEnd = curEnd = score(moveList);

            partial_insertion_sort(cur, curEnd, GOOD_QUIET_THRESHOLD);
        }

        curStage = Stage::ENC_GOOD_QUIET;
        [[fallthrough]];

    case Stage::ENC_GOOD_QUIET :
        for (; !quietsSkip && cur != curEnd; ++cur)
            if (*cur != ttMove)
            {
                // Good quiet threshold
                if (cur->value < GOOD_QUIET_THRESHOLD)
                    // Remaining quiets are bad
                    break;
                return *cur++;
            }

        // Mark the beginning of bad quiets
        badQuietBeg = cur;

        // Prepare the pointers to loop over the bad captures
        cur    = moves.data();
        curEnd = badCaptureEnd;

        curStage = Stage::ENC_BAD_CAPTURE;
        [[fallthrough]];

    case Stage::ENC_BAD_CAPTURE :
        if (select(always_true))
            return *cur++;

        if (!quietsSkip)
        {
            // Prepare the pointers to loop over the bad quiets
            cur    = badQuietBeg;
            curEnd = badQuietEnd;

            adaptive_stable_sort(cur, curEnd);
        }

        curStage = Stage::ENC_BAD_QUIET;
        [[fallthrough]];

    case Stage::ENC_BAD_QUIET :
        if (!quietsSkip && select(always_true))
            return *cur++;

        return Move::None;

    case Stage::EVA_CAPTURE :
        if (select(always_true))
            return *cur++;

        {
            MoveList<GenType::EVA_QUIET> moveList(pos);

            curEnd = score(moveList);

            insertion_sort(cur, curEnd);
        }

        curStage = Stage::EVA_QUIET;
        [[fallthrough]];

    case Stage::EVA_QUIET :
    case Stage::QS_CAPTURE :
        if (select(always_true))
            return *cur++;

        return Move::None;

    case Stage::PROBCUT :
        if (select([this]() noexcept -> bool { return above_threshold_capture(); }))
            return *cur++;

        return Move::None;
    }
    assert(false);
    UNREACHABLE();
    return Move::None;  // Silence warning
}

MovePicker::Stage MovePicker::cur_stage() const noexcept { return curStage; }

int MovePicker::threshold_value() const noexcept { return threshold; }

ALWAYS_INLINE bool MovePicker::good_capture_or_swap() noexcept {
    threshold = constexpr_round(cur->value / 18.0);
    if (pos.see(*cur) >= -threshold)
        return true;
    // Store bad captures
    std::iter_swap(badCaptureEnd++, cur);
    return false;
}

ALWAYS_INLINE bool MovePicker::above_threshold_capture() const noexcept {
    return pos.see(*cur) >= threshold;
}

}  // namespace DON
