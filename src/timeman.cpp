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

namespace {

constexpr inline std::int16_t MaxMovesToGo = 50;
constexpr inline double       MtgFactor    = 0.05051;

}  // namespace

// When in 'Nodes as Time' mode
void TimeManager::update_nodes(std::int64_t usedNodes) noexcept {
    assert(use_nodes_time());
    remainNodes = std::max<std::int64_t>(remain_nodes() - usedNodes, 0) + OFFSET_NODE;
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply.
// Currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)
void TimeManager::init(Limit& limit, const Position& pos, const Options& options) noexcept {
    // If have no time, no need to fully initialize TM.
    // startTime is used by movetime and nodesTime is used in elapsed calls.
    startTime       = limit.startTime;
    auto& clock     = limit.clocks[pos.active_color()];
    auto  movesToGo = limit.movesToGo;

    nodesTime = options["NodesTime"];

    if (clock.time == 0)
    {
        optimumTime = 0;
        maximumTime = 0;
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
            remainNodes = clock.time * nodesTime + OFFSET_NODE;  // Time is in msec

        // Convert from milliseconds to nodes
        clock.time = remain_nodes();
        clock.inc *= nodesTime;
        moveOverhead *= nodesTime;
    }

    std::int64_t scaleFactor = std::max(nodesTime, TimePoint(1));
    TimePoint    scaledTime  = clock.time / scaleFactor;
    TimePoint    scaledInc   = clock.inc / scaleFactor;

    // clang-format off

    // Maximum move horizon
    std::int16_t mtg = movesToGo != 0
                     ? std::min(int(std::floor(MaxMovesToGo + 0.1 * std::max(movesToGo - MaxMovesToGo, 0))), +movesToGo)
                     : std::max(int(std::ceil (MaxMovesToGo - 0.1 * std::max(pos.move_num() - 20, 0))), MaxMovesToGo - 10);

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000 && mtg > MtgFactor * scaledInc)
        mtg = std::clamp(int(MtgFactor * scaledTime), 2, +mtg);

    assert(mtg > 0);

    // Make sure remainTime > 0 since use it as a divisor
    TimePoint remainTime =
      std::max(clock.time + (-1 + mtg) * clock.inc - (+2 + mtg) * moveOverhead, TimePoint(1));

    std::int16_t ply = pos.ply();

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    // x moves in y time (+ z increment)
    if (movesToGo != 0)
    {
        optimumScale = std::min((0.8800 + 85.9106e-4 * ply) / mtg,
                                 0.0000 +  0.8800    * clock.time / remainTime);
        maximumScale = std::min(1.3000 + 0.1100 * mtg, 8.4500);
    }
    // x basetime (+ z increment)
    // If there is a healthy increment, remaining time can exceed the actual available
    // game time for the current move, so also cap to a percentage of available game time.
    else
    {
        // Extra time according to initial remaining Time
        if (initialAdjust < 0)
            initialAdjust = std::max(-0.4354 + 0.3128 * std::log10(1.0000 * remainTime), 1.0000e-6);

        // Calculate time constants based on current remaining time        
        double log10ScaledTime = std::log10(1.0e-3 * scaledTime);
        double optimumConstant = std::min(3.2116e-3 + 32.1123e-5 * log10ScaledTime, 5.08017e-3);
        double maximumConstant = std::max(3.3977    +  3.0395    * log10ScaledTime, 2.94761);

        optimumScale = initialAdjust
                     * std::min(12.1431e-3 + optimumConstant * std::pow(2.94693 + ply, 0.461073),
                                 0.00000   + 0.213035        * clock.time / remainTime);
        maximumScale = std::min(maximumConstant + 83.439719e-3 * ply, 6.67704);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optimumScale * remainTime);
    maximumTime = std::max(mtg > 1 ? TimePoint(std::min(0.82518 * clock.time - moveOverhead, maximumScale * optimumTime)) - 10
                                   : clock.time - moveOverhead,
                           TimePoint(1));

    // clang-format on

    if (options["Ponder"])
        optimumTime *= 1.2500;
}

}  // namespace DON
