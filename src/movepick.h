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
#include <limits>

#include "history.h"
#include "movegen.h"
#include "types.h"

namespace DON {

class Position;

enum Stage : std::uint8_t {
    STAGE_NONE,

    // Generate main-search moves
    MAIN_TT,
    CAPTURE_INIT,
    CAPTURE_GOOD,
    QUIET_INIT,
    QUIET_GOOD,
    CAPTURE_BAD,
    QUIET_BAD,

    // Generate evasion moves
    EVASION_TT,
    EVASION_INIT,
    EVASION,

    // Generate probcut moves
    PROBCUT_TT,
    PROBCUT_INIT,
    PROBCUT,
};

constexpr Stage operator+(Stage s, int i) noexcept { return Stage(int(s) + i); }
inline Stage&   operator++(Stage& s) noexcept { return s = s + 1; }

// MovePicker class is used to pick one pseudo-legal move at a time from the given current position.
// The most important method is next_move(), which returns a new pseudo-legal move each time it is called,
// until there are no moves left, when Move::None() is returned. In order to improve the efficiency of the
// alpha-beta algorithm, MovePicker attempts to return the moves which are most likely to get a cut-off first.
class MovePicker final {
   public:
    MovePicker(const Position&            p,
               Move                       ttm,
               const History<HButterfly>* mainHist,
               const History<HCapture>*   capHist,
               const History<HPawn>*      pawnHist,
               const History<HPieceSq>**  psqHist,
               const History<HLowPly>*    lpHist,
               std::int16_t               ply,
               Value                      th = 0) noexcept;
    MovePicker(const Position&          p,  //
               Move                     ttm,
               const History<HCapture>* capHist,
               Value                    th) noexcept;
    MovePicker() noexcept                             = delete;
    MovePicker(const MovePicker&) noexcept            = delete;
    MovePicker(MovePicker&&) noexcept                 = delete;
    MovePicker& operator=(const MovePicker&) noexcept = delete;
    MovePicker& operator=(MovePicker&&) noexcept      = delete;

    Move next_move() noexcept;

    Stage stage = STAGE_NONE;

    bool pickQuiets = false;

   private:
    void next_stage() noexcept { ++stage; }

    template<GenType GT>
    void score() noexcept;

    void sort_partial(int limit = std::numeric_limits<int>::min()) noexcept;

    auto begin() const noexcept { return begExtItr; }
    auto end() const noexcept { return endExtItr; }
    auto next() noexcept { return ++begExtItr; }

    auto current() const noexcept { return *begExtItr; }
    auto current_next() noexcept { return *begExtItr++; }

    void swap_best(int tolerance = 0) noexcept;

    //auto size() const noexcept { return std::distance(begin(), end()); }

    const Position&            pos;
    const Move                 ttMove;
    const History<HButterfly>* mainHistory    = nullptr;
    const History<HCapture>*   captureHistory = nullptr;
    const History<HPawn>*      pawnHistory    = nullptr;
    const History<HPieceSq>**  pieceSqHistory = nullptr;
    const History<HLowPly>*    lowPlyHistory  = nullptr;
    const std::int16_t         ssPly          = LOW_PLY_SIZE;
    const Value                threshold      = 0;

    int minValue = std::numeric_limits<int>::min();
    int curValue = std::numeric_limits<int>::max();

    ExtMoves            extMoves;
    ExtMoves::NormalItr begExtItr, endExtItr;

    Moves            badCapMoves;
    Moves::NormalItr begItr, endItr;
};

}  // namespace DON

#endif  // #ifndef MOVEPICK_H_INCLUDED
