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
#include <array>
#include <cassert>
#include <cmath>

#include "search.h"
#include "ucioption.h"

namespace DON {

namespace {

constexpr std::uint16_t MaxCentiMTG = 5051U;

}  // namespace

void TimeManager::init() noexcept {

    timeAdjust = -1.0;

    timeNodes = -1;
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply.
// Currently support:
//      1) x basetime (sudden death)
//      2) x basetime (+ z increment)
//      3) x moves in y time (+ z increment)
void TimeManager::init(
  Limit& limit, Color ac, std::int16_t ply, std::int32_t moveNum, const Options& options) noexcept {
    // If have no time, no need to fully initialize TM.
    // startTime is used by movetime and nodesTime is used in elapsed calls.
    startTime   = limit.startTime;
    auto& clock = limit.clocks[ac];

    TimePoint nodesTime = options["NodesTime"];
    nodesTimeUse        = nodesTime > 0;

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
    if (nodesTimeUse)
    {
        // Only once at game start
        if (timeNodes == -1)
            timeNodes = clock.time * nodesTime;  // Time is in msec

        // Convert from milliseconds to nodes
        clock.time = timeNodes;
        clock.inc *= nodesTime;
        moveOverhead *= nodesTime;
    }

    const std::uint64_t scaleFactor = std::max(nodesTime, TimePoint(1));

    const TimePoint scaledTime = clock.time / scaleFactor;

    // clang-format off

    // Maximum move horizon
    auto centiMTG = limit.movesToGo == 0
                  ? std::max<std::uint16_t>(MaxCentiMTG - 10 * std::max(moveNum               - 20         , 0), MaxCentiMTG - 1000)
                  : std::min<std::uint16_t>(MaxCentiMTG + 10 * std::max(100 * limit.movesToGo - MaxCentiMTG, 0), 100 * limit.movesToGo);

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000)
        centiMTG = std::max<std::uint16_t>(5.0510 * scaledTime, 200);

    // Make sure remainTime > 0 since use it as a divisor
    const TimePoint remainTime = std::max(clock.time + ((centiMTG - 100) * clock.inc - (centiMTG + 200) * moveOverhead) / 100, TimePoint(1));

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    if (limit.movesToGo == 0)
    {
        // 1) x basetime (sudden death)
        // Sudden death time control
        if (clock.inc == 0)
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust < 0.0)
            timeAdjust = std::max(-0.4126 + 0.2862 * std::log10(remainTime), 1.0e-6);

        // Calculate time constants based on current remaining time
        double logScaledTime = std::log10(scaledTime / 1000.0);

        optimumScale = timeAdjust
                     * std::min(11.29900e-3 + std::min(3.47750e-3 + 28.41880e-5 * logScaledTime, 4.06734e-3)
                                            * std::pow(2.82122 + ply, 0.466422),
                                0.213035 * clock.time / remainTime);
        maximumScale = std::min(std::max(3.66270 + 3.72690 * logScaledTime, 2.75068) + 78.37482e-3 * ply, 6.35772);
        }
        // 2) x basetime (+ z increment)
        // If there is a healthy increment, remaining time can exceed the actual available
        // game time for the current move, so also cap to a percentage of available game time.
        else
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust < 0.0)
            timeAdjust = std::max(-0.4354 + 0.3128 * std::log10(remainTime), 1.0e-6);

        // Calculate time constants based on current remaining time
        double logScaledTime = std::log10(scaledTime / 1000.0);

        optimumScale = timeAdjust
                     * std::min(12.14310e-3 + std::min(3.21160e-3 + 32.11230e-5 * logScaledTime, 5.08017e-3)
                                            * std::pow(2.94693 + ply, 0.461073),
                                0.213035 * clock.time / remainTime);
        maximumScale = std::min(std::max(3.39770 + 3.03950 * logScaledTime, 2.94761) + 83.43972e-3 * ply, 6.67704);
        }
    }
    // 3) x moves in y time (+ z increment)
    else
    {
        optimumScale = std::min((0.88000 + 85.91065e-4 * ply) / (centiMTG / 100.0),
                                 0.88000 * clock.time / remainTime);
        maximumScale = std::min( 1.30000 + 0.11000 * (centiMTG / 100.0), 8.45000);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optimumScale * remainTime);
    maximumTime = std::max(centiMTG > 100 ? TimePoint(std::min(0.825179 * clock.time - moveOverhead, maximumScale * optimumTime)) - 10
                                          : clock.time - moveOverhead, TimePoint(1));

    // clang-format on

    if (options["Ponder"])
        optimumTime *= 1.2500;
}

// When in 'Nodes as Time' mode
void TimeManager::advance_time_nodes(std::uint64_t usedNodes) noexcept {
    assert(nodesTimeUse);
    timeNodes = std::uint64_t(timeNodes) > usedNodes ? timeNodes - usedNodes : 0;
}

}  // namespace DON
