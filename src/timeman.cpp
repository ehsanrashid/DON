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

TimeManagement::TimeManagement() noexcept { clear(); }

void TimeManagement::clear() noexcept {
    optimumTime  = 0;
    maximumTime  = 0;
    useNodesTime = false;
    totalNodes   = -1LL;
}
// When in 'Nodes as Time' mode
void TimeManagement::advance(std::int64_t diffNodes) noexcept {
    assert(useNodesTime);
    totalNodes = std::max(totalNodes - diffNodes, 0LL);
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply. Currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)
void TimeManagement::init(Search::Limits&   limits,
                          const Position&   pos,
                          const OptionsMap& options) noexcept {
    // If have no time, no need to fully initialize TM.
    // startTime is used by movetime and useNodesTime is used in elapsed calls.
    startTime           = limits.startTime;
    TimePoint nodesTime = options["NodesTime"];
    useNodesTime        = nodesTime != 0;

    Color stm         = pos.side_to_move();
    auto& [time, inc] = limits.clock[stm];
    if (time <= 0 && !useNodesTime)
    {
        clear();
        return;
    }

    TimePoint moveOverhead = options["Move Overhead"];

    // If have to play in 'Nodes as Time' mode, then convert from time to nodes,
    // and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given nodesTime (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (useNodesTime)
    {
        if (totalNodes == -1LL)             // Only once at game start
            totalNodes = time * nodesTime;  // Time is in msec

        // Convert from milliseconds to nodes
        time = totalNodes;
        inc *= nodesTime;
        moveOverhead *= nodesTime;
    }

    // These numbers are used where multiplications, divisions or comparisons
    // with constants are involved.
    const uint64_t  scaleFactor = useNodesTime ? nodesTime : 1ULL;
    const TimePoint scaledTime  = time / scaleFactor;
    const TimePoint scaledInc   = inc / scaleFactor;

    auto gamePly = pos.game_ply();

    // Maximum move horizon of 50 moves
    auto mtg = limits.movesToGo != 0
               ? std::min<std::uint8_t>(limits.movesToGo, 50U)
               : std::max<std::uint8_t>(std::ceil(50U - 0.2 * pos.game_move()), 40U);

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000 && mtg > 2)
        mtg = std::clamp<std::uint8_t>(0.05 * scaledTime, 2U, mtg);

    assert(mtg != 0);

    // Make sure remainingTime is > 0 since use it as a divisor
    TimePoint remainingTime = std::max(time + (-1 + mtg) * inc - (+2 + mtg) * moveOverhead, 1LL);

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    // x moves in y seconds (+ z increment)
    if (limits.movesToGo != 0)
    {
        optimumScale = std::min((0.88 + 0.008591 * gamePly) / mtg, 0.88 * time / remainingTime);
        maximumScale = std::min(1.5 + 0.11 * mtg, 6.3);
    }
    // x basetime (+ z increment)
    // If there is a healthy increment, remainingTime can exceed the actual available
    // game time for the current move, so also cap to a percentage of available game time.
    else
    {
        // Use extra time with larger increments.
        auto optimumExtra = 1.0 + std::min(0.05 * std::log10(std::max(scaledInc - 500, 1LL)), 0.14);

        // Calculate time constants based on current time left.
        auto log10TimeInSec  = std::log10(0.001 * scaledTime);
        auto optimumConstant = std::min(0.00308 + 0.000319 * log10TimeInSec, 0.00506);
        auto maximumConstant = std::max(3.39 + 3.01 * log10TimeInSec, 2.93);

        optimumScale = std::min(0.0122 + optimumConstant * std::pow(2.95 + gamePly, 0.462),
                                0.213 * time / remainingTime)
                     * optimumExtra;
        maximumScale = std::min(maximumConstant + 0.083333 * gamePly, 6.64);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optimumScale * remainingTime);
    maximumTime = TimePoint(
      std::max(mtg > 1 ? std::min(0.825 * time - moveOverhead, maximumScale * optimumTime) - 10
                       : time - moveOverhead - 10,
               1.0));

    if (options["Ponder"])
        optimumTime *= 1.25;
}

}  // namespace DON
