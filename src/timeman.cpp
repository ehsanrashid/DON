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
void TimeManager::advance_nodes(std::int64_t usedNodes) noexcept {
    assert(use_nodes_time());
    remainNodes = std::max<std::int64_t>(remain_nodes() - usedNodes, 0) + NODE_OFFSET;
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply. Currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)
void TimeManager::init(Limit& limit, const Position& pos, const Options& options) noexcept {
    // If have no time, no need to fully initialize TM.
    // startTime is used by movetime and nodesTime is used in elapsed calls.
    startTime = limit.startTime;
    nodesTime = options["NodesTime"];

    auto& [time, inc] = limit.clocks[pos.active_color()];
    if (time == 0 && !use_nodes_time())
    {
        clear();
        return;
    }

    TimePoint moveOverhead = options["MoveOverhead"];

    // If have to play in 'Nodes as Time' mode, then convert from time to nodes,
    // and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given nodesTime (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (use_nodes_time())
    {
        // Only once at game start
        if (remainNodes == 0)
            remainNodes = time * nodesTime + NODE_OFFSET;  // Time is in msec

        // Convert from milliseconds to nodes
        time = remain_nodes();
        inc *= nodesTime;
        moveOverhead *= nodesTime;
    }

    const std::int64_t scaleFactor = std::max(nodesTime, 1ll);
    const TimePoint    scaledTime  = time / scaleFactor;
    const TimePoint    scaledInc   = inc / scaleFactor;

    // Maximum move horizon of 50 moves
    std::uint16_t mtg = limit.movesToGo != 0
                        ? std::min(+limit.movesToGo, 50)
                        : std::max(int(std::ceil(50 - 0.1 * std::max(pos.move_num() - 20, 0))), 40);

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000 && mtg > 0.05 * scaledInc)
        mtg = std::clamp<int>(0.05 * scaledTime, 2, mtg);

    assert(mtg != 0);

    // Make sure remainTime > 0 since use it as a divisor
    TimePoint remainTime = std::max(time + (-1 + mtg) * inc - (+2 + mtg) * moveOverhead, 1ll);

    std::uint16_t posPly = pos.ply();

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    // x moves in y time (+ z increment)
    if (limit.movesToGo != 0)
    {
        optimumScale = std::min((0.88 + 8.59106e-3 * posPly) / mtg, 0.88 * time / remainTime);
        maximumScale = std::min(1.5 + 0.11 * mtg, 6.3);
    }
    // x basetime (+ z increment)
    // If there is a healthy increment, remaining time can exceed the actual available
    // game time for the current move, so also cap to a percentage of available game time.
    else
    {
        // Extra time according to initial remainTime
        if (initialAdjust <= MIN_ADJUST)
            initialAdjust = std::max(-0.4830 + 0.3285 * std::log10(remainTime), MIN_ADJUST);
        // Calculate time constants based on current remaining time
        double log10ScaledTime = std::log10(1e-3 * scaledTime);
        double optimumConstant = std::min(3.08e-3 + 3.19e-4 * log10ScaledTime, 5.06e-3);
        double maximumConstant = std::max(3.39 + 3.01 * log10ScaledTime, 2.93);

        optimumScale = initialAdjust
                     * std::min(12.2e-3 + std::pow(2.95 + posPly, 0.462) * optimumConstant,
                                0.213 * time / remainTime);
        maximumScale = std::min(maximumConstant + 83.3333e-3 * posPly, 6.64);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optimumScale * remainTime);
    maximumTime =
      TimePoint((mtg > 1 ? std::min(0.825 * time - moveOverhead, maximumScale * optimumTime)
                         : time - moveOverhead)
                - 10);
    maximumTime = std::max(maximumTime, 1ll);

    if (options["Ponder"])
        optimumTime *= 1.25;
}

}  // namespace DON
