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
#include <thread>

#include "option.h"
#include "search.h"

namespace DON {

namespace {

// Maximum moves to go used by time management formulas.
constexpr u8 MTG_MAX = u8{50};

constexpr double TIME_ADJUST_INIT = -1.0;
constexpr double TIME_ADJUST_MIN  = 1.0e-6;

constexpr i64 TIME_NODES_INIT = i64{-1};

i64 time_nodes(const i64 time, const u64 nodesTime) noexcept {
    assert(nodesTime != 0);

    u64 timeNodes = u64(time) <= u64(TimeManager::TimeMax) / nodesTime  //
                    ? time * nodesTime
                    : TimeManager::TimeMax;
    return i64(timeNodes);
}

}  // namespace

TimePoint TimeManager::optimum() const noexcept { return optimumTime; }

TimePoint TimeManager::maximum() const noexcept { return maximumTime; }

TimePoint TimeManager::elapsed() const noexcept { return now() - startTime; }

bool TimeManager::use_nodes_time() const noexcept { return useNodesTime; }

void TimeManager::reset() noexcept {

    timeAdjust = TIME_ADJUST_INIT;

    timeNodes = TIME_NODES_INIT;
}

void TimeManager::init(Color ac, i16 ply, const Options& options, Limit& limit) noexcept {
    // If have no time, no need to fully initialize TM.
    // start-time is used by move-time and Nodes-Time is used in elapsed calls.
    startTime = limit.startTime;

    auto& clock = limit.clocks[ac];

    const u64 NodesTime = options["NodesTime"];

    useNodesTime = NodesTime != 0;

    if (use_nodes_time())
    {
        // Convert from milliseconds to nodes
        limit.moveTime *= NodesTime;
    }

    if (clock.time == 0)
    {
        optimumTime = NoBound;
        maximumTime = NoBound;
        return;
    }

    TimePoint OverheadTime = options["OverheadTime"];

    // If have to play in 'Nodes as Time' mode, then convert from time to nodes,
    // and use resulting values in time management formulas.
    // WARNING: to avoid time losses, the given Nodes-Time (nodes per millisecond)
    // must be much lower than the real engine speed.
    if (use_nodes_time())
    {
        // Only once at game start
        if (timeNodes == TIME_NODES_INIT)
            timeNodes = time_nodes(clock.time, NodesTime);

        // Convert from milliseconds to nodes
        clock.time = TimePoint(timeNodes);
        clock.inc *= NodesTime;
        OverheadTime *= NodesTime;
    }

    const u64 scaleFactor = use_nodes_time() ? NodesTime : 1;

    const TimePoint scaledTime = std::max<TimePoint>(clock.time / scaleFactor, 1);

    // clang-format off

    // Maximum move horizon
    u8 mtg = limit.movesToGo != 0 ? std::min(limit.movesToGo, MTG_MAX) : MTG_MAX;

    // If less than one second, gradually reduce mtg.
    // In cyclic time controls keep the actual movestogo as horizon.
    if (mtg > 2 && scaledTime < 1000 && limit.movesToGo == 0)
        mtg = u8(std::max(0.05051 * scaledTime, 2.0));

    // Make sure remainTime > 0 since use it as a divisor
    const TimePoint remainTime =
        TimePoint(std::max(
                    std::max(clock.time + (mtg - 1) * clock.inc - (mtg + 2) * OverheadTime, TimePoint{1})
                  * options["TimePercent"] / 100.0, 1.0));

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    if (limit.movesToGo == 0)
    {
        // Calculate time constants based on current remaining time
        const double logScaledTime = std::log10(scaledTime / 1000.0);

        // 1) x base-time (sudden death)
        // Sudden death time control
        if (clock.inc == 0)
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust == TIME_ADJUST_INIT)
            timeAdjust = std::max(-0.4126 + 0.2862 * std::log10(remainTime), TIME_ADJUST_MIN);

        optimumScale = timeAdjust
                     * std::min(11.29900e-3 + std::min(3.47750e-3 + 28.41880e-5 * logScaledTime, 4.06734e-3) * std::pow(2.82122 + ply, 0.46642), 0.19404 * clock.time / remainTime);
        maximumScale = std::min(std::max(3.66270 + 3.72690 * logScaledTime, 2.75068) + ply / 12.7592, 6.35772);
        }
        // 2) x base-time (+ z increment)
        // If there is a healthy increment, remaining time can exceed the actual available
        // game time for the current move, so also cap to a percentage of available game time.
        else
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust == TIME_ADJUST_INIT)
            timeAdjust = std::max(-0.4141 + 0.3272 * std::log10(remainTime), TIME_ADJUST_MIN);

        optimumScale = timeAdjust
                     * std::min(12.11200e-3 + std::min(2.98690e-3 + 33.55400e-5 * logScaledTime, 4.90500e-3) * std::pow(3.22713 + ply, 0.46866), 0.19404 * clock.time / remainTime);
        maximumScale = std::min(std::max(3.37440 + 3.06080 * logScaledTime, 3.14410) + ply / 12.3520, 6.87300);
        }
    }
    // 3) x moves in y time (+ z increment)
    else
    {
        optimumScale = std::min((0.8800 + ply / 116.4) / mtg, 0.8800 * clock.time / remainTime);
        maximumScale = 1.3000 + 0.1100 * mtg;
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(std::max(std::max(optimumScale * remainTime, 1.0), double(options["MinMoveTime"])));
    maximumTime = std::max(
                    mtg < 2
                    ? clock.time
                    : TimePoint(std::min(maximumScale * optimumTime, 0.80970 * clock.time - OverheadTime) - options["BufferTime"]),
                    optimumTime);
    // clang-format on

    if (options["SleepOnStart"])
        std::this_thread::sleep_for(Ms(optimumTime / 2));

    if (options["Ponder"])
        optimumTime = TimePoint(std::min(1.2500 * optimumTime, TimeMaxValue));
}

void TimeManager::advance_time_nodes(i64 nodes) noexcept {
    assert(use_nodes_time());

    timeNodes = std::max(timeNodes - nodes, i64{0});
}

}  // namespace DON
