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

#ifndef MOVEPICK_H_INCLUDED
#define MOVEPICK_H_INCLUDED

#include "history.h"
#include "misc.h"
#include "movegen.h"
#include "types.h"

namespace DON {

class Position;

// History size for continuation moves
inline constexpr u8 CONT_HISTORY_COUNT = 8;

struct ExtMove final: public Move {
   public:
    using Move::operator=;

    constexpr bool operator<(const ExtMove& em) const noexcept { return value < em.value; }
    constexpr bool operator>(const ExtMove& em) const noexcept { return (em < *this); }
    constexpr bool operator<=(const ExtMove& em) const noexcept { return !(em < *this); }
    constexpr bool operator>=(const ExtMove& em) const noexcept { return !(*this < em); }

    i32 value;
};

static_assert(sizeof(ExtMove) == 8, "ExtMove size must be Move + int = 8 bytes");

constexpr bool ext_move_descending(const ExtMove& em1, const ExtMove& em2) noexcept {
    return em1 > em2;
}

// MovePicker class is used to pick one pseudo-legal move at a time from the given current position.
// The most important method is next_move(), which returns a new legal move each time it is called,
// until there are no moves left, when Move::None is returned. In order to improve the efficiency of the
// alpha-beta algorithm, MovePicker attempts to return the moves which are most likely to get a cut-off first.
class MovePicker final {
   public:
    using value_type      = ExtMove;
    using pointer         = value_type*;
    using const_pointer   = const value_type*;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using iterator        = pointer;
    using const_iterator  = const_pointer;
    using size_type       = usize;

    enum class Stage : u8 {
        TT,
        INIT,

        ENC_GOOD_CAPTURE,
        ENC_GOOD_QUIET,
        ENC_BAD_CAPTURE,
        ENC_BAD_QUIET,

        EVA_CAPTURE,
        EVA_QUIET,

        QS_CAPTURE,

        PROBCUT
    };

    // Constructors of the MovePicker class. As arguments, pass information
    // to decide which class of moves to return, to help sorting the (presumably)
    // good moves first, and how important move ordering is at the current node.

    // MovePicker constructor for the main search and for the quiescence search.
    MovePicker(const Position&           p,
               Move                      ttm,
               const CaptureHistory*     captureHist,
               const QuietHistory*       quietHist,
               const LowPlyQuietHistory* lowPlyQuietHist,
               const PieceSqHistory**    continuationHist,
               const AtomicHistories*    atomicHists,
               u16                       ply,
               int                       th = 0) noexcept;

    // MovePicker constructor for ProbCut:
    // Generate captures with Static Exchange Evaluation (SEE) >= threshold.
    MovePicker(const Position& p, Move ttm, const CaptureHistory* captureHist, int th) noexcept;

    // Most important method of the MovePicker class.
    // It emits a new legal move every time it is called until there are no more moves left,
    // picking the move with the highest score from a list of generated moves.
    [[nodiscard]] Move next_move() noexcept;

    [[nodiscard]] Stage cur_stage() const noexcept;
    [[nodiscard]] int   threshold_value() const noexcept;

    template<typename Predicate>
    void update_quiets_skip(const Predicate& pred) noexcept {
        quietsSkip = quietsSkip || pred();
    }

   private:
    MovePicker() noexcept                             = delete;
    MovePicker(const MovePicker&) noexcept            = delete;
    MovePicker& operator=(const MovePicker&) noexcept = delete;
    MovePicker(MovePicker&&) noexcept                 = delete;
    MovePicker& operator=(MovePicker&&) noexcept      = delete;

    template<GenType GT>
    void init() noexcept;

    // Assigns a numerical value to each move in a list, used for sorting.
    // Captures moves are ordered by Most Valuable Victim (MVV),
    // preferring captures moves with a good history.
    // Quiet moves are ordered by using the history tables.
    template<GenType GT>
    iterator score(const MoveList<GT>& moveList) noexcept;

    template<typename Predicate>
    bool select(const Predicate& pred) noexcept;

    [[nodiscard]] bool good_capture_or_swap() noexcept;

    [[nodiscard]] bool above_threshold_capture() const noexcept;

    const Position&                 pos;
    Move                            ttMove;
    const CaptureHistory* const     captureHistory      = nullptr;
    const QuietHistory* const       quietHistory        = nullptr;
    const LowPlyQuietHistory* const lowPlyQuietHistory  = nullptr;
    const PieceSqHistory** const    continuationHistory = nullptr;
    const AtomicHistories* const    atomicHistories     = nullptr;
    const u16                       ssPly               = LOW_PLY_SIZE;
    int                             threshold;

    Stage initStage;
    Stage curStage;

    bool quietsSkip = false;

    Array<value_type, MOVE_MAX> moves;

    iterator                    //
      cur           = nullptr,  //
      curEnd        = nullptr,  //
      badCaptureEnd = nullptr,  //
      badQuietBeg   = nullptr,  //
      badQuietEnd   = nullptr;
};

}  // namespace DON

#endif  // MOVEPICK_H_INCLUDED
