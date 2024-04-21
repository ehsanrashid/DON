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

TimePoint TimeManagement::optimum() const noexcept { return optimumTime; }
TimePoint TimeManagement::maximum() const noexcept { return maximumTime; }

// When in 'Nodes as Time' mode
void TimeManagement::clear_nodes_time() noexcept {
    assert(useNodesTime);
    availableNodes = 0;
}
void TimeManagement::advance_nodes_time(std::uint64_t nodes) noexcept {
    assert(useNodesTime);
    availableNodes += nodes;
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply. We currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)
void TimeManagement::init(Search::Limits&   limits,
                          const Position&   pos,
                          const OptionsMap& options) noexcept {
    // If we have no time, no need to initialize TM, except for the start time,
    // which is used by movetime.
    startTime = limits.startTime;

    Color stm         = pos.side_to_move();
    auto& [time, inc] = limits.clock[stm];
    if (time <= 0)
    {
        optimumTime = 0;
        maximumTime = 0;
        return;
    }

    // If we have to play in 'Nodes as Time' mode, then convert from time to nodes,
    // and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given nodesTime (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (TimePoint nodesTime = options["NodesTime"])
    {
        useNodesTime = true;

        if (availableNodes == 0)                // Only once at game start
            availableNodes = time * nodesTime;  // Time is in msec

        // Convert from milliseconds to nodes
        time = TimePoint(availableNodes);
        inc *= nodesTime;
    }

    TimePoint moveOverhead = options["MoveOverhead"];

    std::int16_t gamePly = pos.game_ply();

    // Maximum move horizon of 50 moves
    std::uint16_t mtg = limits.movesToGo != 0
                        ? std::min<std::uint16_t>(limits.movesToGo, 50)
                        : std::max<std::uint16_t>(std::ceil(50 - 0.2 * pos.game_move()), 40);

    // If less than one second, gradually reduce mtg
    if (time < 1000 && mtg > 2)
        mtg = std::clamp<std::uint16_t>(0.05 * time, 2, mtg);

    assert(mtg != 0);

    // Make sure remainingTime is > 0 since we use it as a divisor
    TimePoint remainingTime =
      std::max<TimePoint>(time + (mtg - 1) * inc - (mtg + 2) * moveOverhead, 1);

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
        double optimumExtra = 1.0 + std::min(0.0001 * std::max<TimePoint>(inc - 500, 0), 0.13);

        // Calculate time constants based on current time left.
        double optimumConstant = std::min(0.00308 + 0.000319 * std::log10(0.001 * time), 0.00506);
        double maximumConstant = std::max(3.39 + 3.01 * std::log10(0.001 * time), 2.93);

        optimumScale = std::min(0.0122 + optimumConstant * std::pow(2.95 + gamePly, 0.462),
                                0.213 * time / remainingTime)
                     * optimumExtra;
        maximumScale = std::min(maximumConstant + 0.083333 * gamePly, 6.64);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optimumScale * remainingTime);
    maximumTime = std::max<TimePoint>(
      mtg > 1 ? std::min(0.825 * time - moveOverhead, maximumScale * optimumTime) - 10
              : time - moveOverhead - 10,
      1);

    if (options["Ponder"])
        optimumTime *= 1.25;
}

}  // namespace DON
