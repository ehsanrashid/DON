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

#ifndef MOVEPICK_H_INCLUDED
#define MOVEPICK_H_INCLUDED

#include <cstddef>
#include <cstdint>

#include "history.h"
#include "misc.h"
#include "movegen.h"
#include "types.h"

namespace DON {

class Position;

enum Stage : std::uint8_t {
    STG_NONE,

    // Generate encounter moves
    STG_ENC_TT,
    STG_ENC_CAPTURE_INIT,
    STG_ENC_CAPTURE_GOOD,
    STG_ENC_QUIET_INIT,
    STG_ENC_QUIET_GOOD,
    STG_ENC_CAPTURE_BAD,
    STG_ENC_QUIET_BAD,

    // Generate evasion moves
    STG_EVA_TT,
    STG_EVA_CAPTURE_INIT,
    STG_EVA_CAPTURE,
    STG_EVA_QUIET_INIT,
    STG_EVA_QUIET,

    // Generate qsearch moves
    STG_QS_TT,
    STG_QS_CAPTURE_INIT,
    STG_QS_CAPTURE,

    // Generate probcut moves
    STG_PROBCUT_TT,
    STG_PROBCUT_INIT,
    STG_PROBCUT,
};

constexpr Stage  operator+(Stage s, int i) noexcept { return Stage(int(s) + i); }
constexpr Stage& operator++(Stage& s) noexcept { return s = s + 1; }

struct ExtMove final: public Move {
   public:
    using Move::operator=;

    friend constexpr bool operator<(ExtMove em1, ExtMove em2) noexcept {
        return em1.value < em2.value;
    }
    friend constexpr bool operator>(ExtMove em1, ExtMove em2) noexcept { return (em2 < em1); }
    friend constexpr bool operator<=(ExtMove em1, ExtMove em2) noexcept { return !(em2 < em1); }
    friend constexpr bool operator>=(ExtMove em1, ExtMove em2) noexcept { return !(em1 < em2); }

    int value;
};

static_assert(sizeof(ExtMove) == 8, "Unexpected ExtMove size");

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
    using size_type       = std::size_t;

    MovePicker(const Position&                 p,
               Move                            ttm,
               const History<H_CAPTURE>*       captureHist,
               const History<H_QUIET>*         quietHist,
               const History<H_PAWN>*          pawnHist,
               const History<H_LOW_PLY_QUIET>* lowPlyQuietHist,
               const History<H_PIECE_SQ>**     continuationHist,
               std::int16_t                    ply,
               int                             th = 0) noexcept;

    MovePicker(const Position& p, Move ttm, const History<H_CAPTURE>* captureHist, int th) noexcept;

    MovePicker() noexcept                             = delete;
    MovePicker(const MovePicker&) noexcept            = delete;
    MovePicker(MovePicker&&) noexcept                 = delete;
    MovePicker& operator=(const MovePicker&) noexcept = delete;
    MovePicker& operator=(MovePicker&&) noexcept      = delete;

    [[nodiscard]] size_type size() const noexcept { return end() - begin(); }
    [[nodiscard]] bool      empty() const noexcept { return begin() == end(); }

    Move next_move() noexcept;

    Stage stage = STG_NONE;

    bool quietAllowed = true;

   private:
    template<GenType GT>
    iterator score(MoveList<GT>& moveList) noexcept;

    template<typename Predicate>
    bool select(Predicate&& pred) noexcept;

    [[nodiscard]] iterator       begin() noexcept { return cur; }
    [[nodiscard]] iterator       end() noexcept { return endCur; }
    [[nodiscard]] const_iterator begin() const noexcept { return cur; }
    [[nodiscard]] const_iterator end() const noexcept { return endCur; }

    bool valid() const noexcept { return *cur != ttMove; }
    void next() noexcept { ++cur; }

    Move move() noexcept { return *cur++; }

    [[nodiscard]] pointer       data() noexcept { return moves.data(); }
    [[nodiscard]] const_pointer data() const noexcept { return moves.data(); }

    const Position&                 pos;
    Move                            ttMove;
    const History<H_CAPTURE>*       captureHistory      = nullptr;
    const History<H_QUIET>*         quietHistory        = nullptr;
    const History<H_PAWN>*          pawnHistory         = nullptr;
    const History<H_LOW_PLY_QUIET>* lowPlyQuietHistory  = nullptr;
    const History<H_PIECE_SQ>**     continuationHistory = nullptr;
    const std::int16_t              ssPly               = LOW_PLY_SIZE;
    const int                       threshold;

    StdArray<value_type, MAX_MOVES> moves;
    iterator                        cur, endCur, endBadCapture, begBadQuiet, endMove = nullptr;
};

}  // namespace DON

#endif  // #ifndef MOVEPICK_H_INCLUDED
