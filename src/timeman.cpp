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

#include "timeman.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "option.h"
#include "search.h"

namespace DON {

namespace {

// Safety margin subtracted from allocated time to account for
// timer resolution, scheduling jitter, and measurement latency.
// This helps avoid flagging under extreme time pressure.
constexpr TimePoint SAFETY_MARGIN_TIME = 10;

constexpr TimePoint MIN_MAXIMUM_TIME = 1;

constexpr std::uint64_t DEFAULT_SCALE_FACTOR = 1;

constexpr std::uint16_t MIN_CENTI_MTG = 101U;
constexpr std::uint16_t MAX_CENTI_MTG = 5051U;

constexpr double INITIAL_TIME_ADJUST = -1.0;
constexpr double MIN_TIME_ADJUST     = 1.0e-6;

constexpr std::int64_t INITIAL_TIME_NODES = -1;

}  // namespace

void TimeManager::init() noexcept {

    timeAdjust = INITIAL_TIME_ADJUST;

    timeNodes = INITIAL_TIME_NODES;
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply.
// Currently support:
//      1) x basetime (sudden death)
//      2) x basetime (+ z increment)
//      3) x moves in y time (+ z increment)
void TimeManager::init(
  Color ac, std::int16_t ply, std::int32_t moveNum, const Options& options, Limit& limit) noexcept {
    // If have no time, no need to fully initialize TM.
    // startTime is used by movetime and Nodes-Time is used in elapsed calls.
    startTime   = limit.startTime;
    auto& clock = limit.clocks[ac];

    std::uint64_t nodesTime = options["NodesTime"];

    useNodesTime = nodesTime != 0;

    if (clock.time == 0)
    {
        optimumTime = 0;
        maximumTime = 0;
        return;
    }

    TimePoint moveOverhead = options["MoveOverhead"];

    // If have to play in 'Nodes as Time' mode, then convert from time to nodes,
    // and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given Nodes-Time (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (use_nodes_time())
    {
        // Only once at game start
        if (timeNodes == INITIAL_TIME_NODES)
        {
            timeNodes = clock.time * nodesTime;  // Time is in msec

            if (timeNodes < 1)
                timeNodes = 1;
        }

        // Convert from milliseconds to nodes
        clock.time = timeNodes;

        clock.inc *= nodesTime;

        moveOverhead *= nodesTime;
    }

    std::uint64_t scaleFactor =
      nodesTime <= DEFAULT_SCALE_FACTOR ? DEFAULT_SCALE_FACTOR : nodesTime;

    TimePoint scaledTime = clock.time / scaleFactor;

    if (scaledTime < 1)
        scaledTime = 1;

    // clang-format off

    // Maximum move horizon
    auto centiMTG = limit.movesToGo == 0
                  ? std::max<std::uint16_t>(MAX_CENTI_MTG - 10 * std::max(moveNum               - 20           , 0), MAX_CENTI_MTG - 1000)
                  : std::min<std::uint16_t>(MAX_CENTI_MTG + 10 * std::max(100 * limit.movesToGo - MAX_CENTI_MTG, 0), 100 * limit.movesToGo);

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000)
    {
        centiMTG = 5.0510 * scaledTime;

        if (centiMTG < MIN_CENTI_MTG)
            centiMTG = MIN_CENTI_MTG;
    }

    TimePoint remainTime = clock.time + ((centiMTG - 100) * clock.inc - (centiMTG + 200) * moveOverhead) / 100;
    // Make sure remainTime > 0 since use it as a divisor
    if (remainTime < 1)
        remainTime = 1;

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    if (limit.movesToGo == 0)
    {
        // Calculate time constants based on current remaining time
        double logScaledTime = std::log10(scaledTime / 1000.0);

        // 1) x basetime (sudden death)
        // Sudden death time control
        if (clock.inc == 0)
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust == INITIAL_TIME_ADJUST)
        {
            timeAdjust = -0.4126 + 0.2862 * std::log10(remainTime);

            if (timeAdjust < MIN_TIME_ADJUST)
                timeAdjust = MIN_TIME_ADJUST;
        }

        optimumScale = timeAdjust
                     * std::min(11.29900e-3 + std::min(3.47750e-3 + 28.41880e-5 * logScaledTime, 4.06734e-3)
                                            * std::pow(2.82122 + ply, 0.466422),
                                0.213035 * clock.time / remainTime);
        maximumScale = std::min(std::max(3.66270 + 3.72690 * logScaledTime, 2.75068) + 78.37482e-3 * ply,
                                6.35772);
        }
        // 2) x basetime (+ z increment)
        // If there is a healthy increment, remaining time can exceed the actual available
        // game time for the current move, so also cap to a percentage of available game time.
        else
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust == INITIAL_TIME_ADJUST)
        {
            timeAdjust = -0.4354 + 0.3128 * std::log10(remainTime);

            if (timeAdjust < MIN_TIME_ADJUST)
                timeAdjust = MIN_TIME_ADJUST;
        }

        optimumScale = timeAdjust
                     * std::min(12.14310e-3 + std::min(3.21160e-3 + 32.11230e-5 * logScaledTime, 5.08017e-3)
                                            * std::pow(2.94693 + ply, 0.461073),
                                0.213035 * clock.time / remainTime);
        maximumScale = std::min(std::max(3.39770 + 3.03950 * logScaledTime, 2.94761) + 83.43972e-3 * ply,
                                6.67704);
        }
    }
    // 3) x moves in y time (+ z increment)
    else
    {
        optimumScale = std::min((0.00880 + 85.91065e-6 * ply) / centiMTG,
                                 0.88000 * clock.time / remainTime);
        maximumScale = std::min(1.30000 + 0.00110 * centiMTG,
                                8.45000);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optimumScale * remainTime);

    maximumTime = centiMTG >= MIN_CENTI_MTG
                ? TimePoint(std::min(0.825179 * clock.time - moveOverhead, maximumScale * optimumTime)) - SAFETY_MARGIN_TIME
                : clock.time - moveOverhead;
    if (maximumTime < MIN_MAXIMUM_TIME)
        maximumTime = MIN_MAXIMUM_TIME;
    // clang-format on

    if (options["Ponder"])
        optimumTime *= 1.2500;
}

// When in 'Nodes as Time' mode
void TimeManager::advance_time_nodes(std::int64_t nodes) noexcept {
    assert(use_nodes_time());

    timeNodes -= nodes;

    if (timeNodes <= INITIAL_TIME_NODES)
        timeNodes = INITIAL_TIME_NODES + 1;
}

}  // namespace DON
