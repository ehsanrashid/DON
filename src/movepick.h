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

#include <cstdint>
#include <limits>

#include "history.h"
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
    STG_EVA_CAPTURE_ALL,
    STG_EVA_QUIET_INIT,
    STG_EVA_QUIET_ALL,

    // Generate probcut moves
    STG_PROBCUT_TT,
    STG_PROBCUT_INIT,
    STG_PROBCUT_ALL,
};

constexpr Stage operator+(Stage s, int i) noexcept { return Stage(int(s) + i); }
inline Stage&   operator++(Stage& s) noexcept { return s = s + 1; }

// MovePicker class is used to pick one pseudo-legal move at a time from the given current position.
// The most important method is next_move(), which returns a new pseudo-legal move each time it is called,
// until there are no moves left, when Move::None is returned. In order to improve the efficiency of the
// alpha-beta algorithm, MovePicker attempts to return the moves which are most likely to get a cut-off first.
class MovePicker final {
   public:
    MovePicker(const Position&              p,
               const Move&                  ttm,
               const History<HCapture>*     captureHist,
               const History<HQuiet>*       quietHist,
               const History<HPawn>*        pawnHist,
               const History<HPieceSq>**    continuationHist,
               const History<HLowPlyQuiet>* lowPlyQuietHist,
               std::int16_t                 ply,
               int                          th = 0) noexcept;
    MovePicker(const Position&          p,
               const Move&              ttm,
               const History<HCapture>* captureHist,
               int                      th) noexcept;
    MovePicker() noexcept                             = delete;
    MovePicker(const MovePicker&) noexcept            = delete;
    MovePicker(MovePicker&&) noexcept                 = delete;
    MovePicker& operator=(const MovePicker&) noexcept = delete;
    MovePicker& operator=(MovePicker&&) noexcept      = delete;

    Move next_move() noexcept;

    Stage stage = STG_NONE;

    bool quietPick = false;

   private:
    void next_stage() noexcept { ++stage; }

    template<GenType GT>
    void score() noexcept;

    void sort_partial(int limit = std::numeric_limits<int>::min()) noexcept;

    auto begin() noexcept { return extCur; }
    auto end() noexcept { return extEnd; }

    void next() noexcept { ++extCur; }

    const auto& current() const noexcept { return *extCur; }

    bool is_ok(const Move& move) const noexcept { return move != ttMove; }

    const Position&              pos;
    const Move&                  ttMove;
    const History<HCapture>*     captureHistory;
    const History<HQuiet>*       quietHistory;
    const History<HPawn>*        pawnHistory;
    const History<HPieceSq>**    continuationHistory;
    const History<HLowPlyQuiet>* lowPlyQuietHistory;
    const std::int16_t           ssPly;
    const int                    threshold;

    Moves      badCapMoves;
    Moves::Itr badCapCur, badCapEnd;

    ExtMoves      extMoves;
    ExtMoves::Itr extCur, extEnd;
};

}  // namespace DON

#endif  // #ifndef MOVEPICK_H_INCLUDED
