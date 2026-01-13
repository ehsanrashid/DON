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

#include <cstddef>
#include <cstdint>

#include "history.h"
#include "misc.h"
#include "movegen.h"
#include "types.h"

namespace DON {

class Position;

struct ExtMove final: public Move {
   public:
    using Move::operator=;

    friend constexpr bool operator<(ExtMove em1, ExtMove em2) noexcept {
        return em1.value < em2.value;
    }
    friend constexpr bool operator>(ExtMove em1, ExtMove em2) noexcept { return (em2 < em1); }
    friend constexpr bool operator<=(ExtMove em1, ExtMove em2) noexcept { return !(em2 < em1); }
    friend constexpr bool operator>=(ExtMove em1, ExtMove em2) noexcept { return !(em1 < em2); }

    int value = 0;
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

    enum class Stage : std::uint8_t {
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

    friend constexpr Stage operator+(Stage s, int i) noexcept { return Stage(std::uint8_t(s) + i); }
    friend constexpr Stage& operator++(Stage& s) noexcept { return s = s + 1; }

    MovePicker() noexcept                             = delete;
    MovePicker(const MovePicker&) noexcept            = delete;
    MovePicker(MovePicker&&) noexcept                 = delete;
    MovePicker& operator=(const MovePicker&) noexcept = delete;
    MovePicker& operator=(MovePicker&&) noexcept      = delete;

    MovePicker(const Position&                  p,
               Move                             ttm,
               const Histories*                 hists,
               const History<HType::CAPTURE>*   captureHist,
               const History<HType::QUIET>*     quietHist,
               const History<HType::LOW_QUIET>* lowPlyQuietHist,
               const History<HType::PIECE_SQ>** continuationHist,
               std::int16_t                     ply,
               int                              th = 0) noexcept;

    MovePicker(const Position&                p,
               Move                           ttm,
               const History<HType::CAPTURE>* captureHist,
               int                            th) noexcept;

    [[nodiscard]] size_type size() const noexcept { return end() - begin(); }
    [[nodiscard]] bool      empty() const noexcept { return begin() == end(); }

    Move next_move() noexcept;

    bool skipQuiets = false;

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

    const Position&                  pos;
    Move                             ttMove;
    const Histories*                 histories           = nullptr;
    const History<HType::CAPTURE>*   captureHistory      = nullptr;
    const History<HType::QUIET>*     quietHistory        = nullptr;
    const History<HType::LOW_QUIET>* lowPlyQuietHistory  = nullptr;
    const History<HType::PIECE_SQ>** continuationHistory = nullptr;
    const std::int16_t               ssPly               = LOW_PLY_QUIET_SIZE;
    const int                        threshold;

    Stage initStage;
    Stage curStage;

    StdArray<value_type, MAX_MOVES> moves;
    iterator                        cur, endCur, endBadCapture, begBadQuiet, endBadQuiet;
};

}  // namespace DON

#endif  // #ifndef MOVEPICK_H_INCLUDED
