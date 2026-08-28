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
#include <chrono>
#include <cmath>
#include <thread>

#include "option.h"
#include "search.h"

namespace DON {

namespace {

constexpr u8 MTG_MAX = 50;  // Moves To Go maximum for time management formulas

constexpr double TIME_ADJUST_INIT = -1.0;
constexpr double TIME_ADJUST_MIN  = 1.0e-6;

constexpr i64 TIME_NODES_INIT = -1;

}  // namespace

TimePoint TimeManager::optimum() const noexcept { return optimumTime; }

TimePoint TimeManager::maximum() const noexcept { return maximumTime; }

TimePoint TimeManager::elapsed() const noexcept { return now() - startTime; }

bool TimeManager::use_nodes_time() const noexcept { return useNodesTime; }

void TimeManager::reset() noexcept {

    timeAdjust = TIME_ADJUST_INIT;

    timeNodes = TIME_NODES_INIT;
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply.
// Currently support:
//      1) x base-time (sudden death)
//      2) x base-time (+ z increment)
//      3) x moves in y time (+ z increment)
void TimeManager::init(
  Color ac, i16 ply, i32 moveNum, const Options& options, Limit& limit) noexcept {
    // If have no time, no need to fully initialize TM.
    // start-time is used by move-time and Nodes-Time is used in elapsed calls.
    startTime = limit.startTime;

    auto& clock = limit.clocks[ac];

    u64 NodesTime = options["NodesTime"];

    useNodesTime = NodesTime != 0;

    if (clock.time == 0)
    {
        optimumTime = 0;
        maximumTime = 0;
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
            timeNodes = std::max<TimePoint>(clock.time * NodesTime, 1);

        // Convert from milliseconds to nodes
        clock.time = timeNodes;

        clock.inc *= NodesTime;

        OverheadTime *= NodesTime;
    }

    u64 ScaleFactor = use_nodes_time() ? NodesTime : 1;

    TimePoint ScaledTime = std::max<TimePoint>(clock.time / ScaleFactor, 1);

    // clang-format off

    // Maximum move horizon
    u8 mtg = limit.movesToGo == 0
                  ? std::max<u8>(MTG_MAX - int(0.1 * std::max(moveNum         - 20     , 0)), MTG_MAX - 10)
                  : std::min<u8>(MTG_MAX + int(0.1 * std::max(limit.movesToGo - MTG_MAX, 0)), limit.movesToGo);

    // If less than one second, gradually reduce mtg
    if (mtg > 2 && ScaledTime < 1000 && clock.inc <= OverheadTime)
        mtg = std::max<u8>(constexpr_ceil(0.05051 * ScaledTime), 2);

    // Make sure remainTime > 0 since use it as a divisor
    TimePoint remainTime = std::max<TimePoint>(clock.time + (mtg - 1) * clock.inc - (mtg + 2) * OverheadTime, 1);

    remainTime = std::max<TimePoint>(constexpr_ceil(remainTime * options["TimePercent"] / 100.0), 1);

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    if (limit.movesToGo == 0)
    {
        // Calculate time constants based on current remaining time
        double LogScaledTime = std::log10(ScaledTime / 1000.0);  // NOLINT(bugprone-narrowing-conversions)

        // 1) x base-time (sudden death)
        // Sudden death time control
        if (clock.inc == 0)
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust == TIME_ADJUST_INIT)
            timeAdjust = std::max(-0.4126 + 0.2862 * std::log10(remainTime), TIME_ADJUST_MIN);

        optimumScale = timeAdjust
                     * std::min(11.29900e-3 + std::min(3.47750e-3 + 28.41880e-5 * LogScaledTime, 4.06734e-3) * std::pow(2.82122 + ply, 0.46642), 0.19404 * clock.time / remainTime);
        maximumScale = std::min(std::max(3.66270 + 3.72690 * LogScaledTime, 2.75068) + ply / 12.7592, 6.35772);
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
                     * std::min(12.11200e-3 + std::min(2.98690e-3 + 33.55400e-5 * LogScaledTime, 4.90500e-3) * std::pow(3.22713 + ply, 0.46866), 0.19404 * clock.time / remainTime);
        maximumScale = std::min(std::max(3.37440 + 3.06080 * LogScaledTime, 3.14410) + ply / 12.3520, 6.87300);
        }
    }
    // 3) x moves in y time (+ z increment)
    else
    {
        optimumScale = std::min((0.8800 + ply / 116.4) / mtg, 0.8800 * clock.time / remainTime);
        maximumScale = std::min(1.3000 + mtg / 9.0909, 8.4500 + mtg / 20.0 + ply / 100.0);
    }

    // Limit the maximum possible time for this move
    optimumTime = std::max<TimePoint>(constexpr_ceil(optimumScale * remainTime), std::max<TimePoint>(options["MinMoveTime"], 1));
    maximumTime = std::max<TimePoint>(
                    mtg < 2
                    ? clock.time
                    : std::min<TimePoint>(constexpr_ceil(maximumScale * optimumTime), constexpr_ceil(0.80970 * clock.time) - OverheadTime) - options["BufferTime"],
                    optimumTime);
    // clang-format on

    if (options["SleepOnStart"])
        std::this_thread::sleep_for(std::chrono::milliseconds(optimumTime / 2));

    if (options["Ponder"])
        optimumTime = constexpr_ceil(1.2500 * optimumTime);
}

// When in 'Nodes as Time' mode
void TimeManager::advance_time_nodes(i64 nodes) noexcept {
    assert(use_nodes_time());

    timeNodes = std::max<i64>(timeNodes - nodes, 0);
}

}  // namespace DON
