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

#include "timeman.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "position.h"
#include "search.h"
#include "ucioption.h"

namespace DON {

// When in 'Nodes as Time' mode
void TimeManager::advance(std::int64_t usedNodes) noexcept {
    assert(use_nodes_time());
    remainNodes = 1ULL + std::max<std::int64_t>(remain_nodes() - usedNodes, 0);
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply. Currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)
void TimeManager::init(Search::Limits&   limits,
                       const Position&   pos,
                       const OptionsMap& options) noexcept {
    // If have no time, no need to fully initialize TM.
    // initialTime is used by movetime and nodesTime is used in elapsed calls.
    initialTime = limits.initialTime;
    nodesTime   = options["NodesTime"];

    Color stm         = pos.side_to_move();
    auto& [time, inc] = limits.clock[stm];
    if (time <= 0 && !use_nodes_time())
    {
        clear();
        return;
    }

    TimePoint moveOverhead = options["Move Overhead"];

    // If have to play in 'Nodes as Time' mode, then convert from time to nodes,
    // and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given nodesTime (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (use_nodes_time())
    {
        if (remainNodes == 0)                       // Only once at game start
            remainNodes = 1ULL + time * nodesTime;  // Time is in msec

        // Convert from milliseconds to nodes
        time = remain_nodes();
        inc *= nodesTime;
        moveOverhead *= nodesTime;
    }

    const std::int64_t scaleFactor = std::max(nodesTime, 1LL);
    const TimePoint    scaledTime  = time / scaleFactor;
    const TimePoint    scaledInc   = inc / scaleFactor;

    // Maximum move horizon of 50 moves
    std::uint8_t mtg = limits.movesToGo != 0
                       ? std::min<std::uint8_t>(limits.movesToGo, 50)
                       : std::max<std::uint8_t>(std::ceil(50 - 0.2 * pos.game_move()), 40);

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000 && mtg > std::max(0.05 * scaledInc, 2.0))
        mtg = std::clamp<std::uint8_t>(0.05 * scaledTime, 2, mtg);

    assert(mtg != 0);

    // Make sure remainTime > 0 since use it as a divisor
    TimePoint remainTime = std::max(time + (-1 + mtg) * inc - (+2 + mtg) * moveOverhead, 1LL);

    if (initialAdjust == -1.0)
        initialAdjust = 0.2078 + 0.1623 * std::log10(remainTime);

    std::int16_t gamePly = pos.game_ply();

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    // x moves in y seconds (+ z increment)
    if (limits.movesToGo != 0)
    {
        optimumScale = std::min((0.88 + 8.59106e-3 * gamePly) / mtg, 0.88 * time / remainTime);
        maximumScale = std::min(1.5 + 0.11 * mtg, 6.3);
    }
    // x basetime (+ z increment)
    // If there is a healthy increment, remaining time can exceed the actual available
    // game time for the current move, so also cap to a percentage of available game time.
    else
    {
        // Use extra time with larger increments.
        double log10ScaledInc = std::log10(std::max(scaledInc - 500LL, 1LL));
        double optimumExtra   = initialAdjust * (1.0 + std::min(50e-3 * log10ScaledInc, 0.14));
        // Calculate time constants based on current remaining time.
        double log10ScaledTime = std::log10(1e-3 * scaledTime);
        double optimumConstant = std::min(3.08e-3 + 3.19e-4 * log10ScaledTime, 5.06e-3);
        double maximumConstant = std::max(3.39 + 3.01 * log10ScaledTime, 2.93);

        optimumScale = optimumExtra
                     * std::min(12.2e-3 + optimumConstant * std::pow(2.95 + gamePly, 0.462),
                                0.213 * time / remainTime);
        maximumScale = std::min(maximumConstant + 83.3333e-3 * gamePly, 6.64);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optimumScale * remainTime);
    maximumTime = TimePoint(
      std::max(mtg > 1 ? std::min(0.825 * time - moveOverhead, maximumScale * optimumTime) - 10
                       : time - moveOverhead - 10,
               1.0));

    if (options["Ponder"])
        optimumTime *= 1.25;
}

}  // namespace DON
