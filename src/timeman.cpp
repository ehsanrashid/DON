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

constexpr std::uint16_t MIN_CENTI_MTG = 101;
constexpr std::uint16_t MAX_CENTI_MTG = 5051;

constexpr double INIT_TIME_ADJUST = -1.0;
constexpr double MIN_TIME_ADJUST  = 1.0e-6;

constexpr std::int64_t INIT_TIME_NODES = -1;

}  // namespace

void TimeManager::init() noexcept {

    timeAdjust = INIT_TIME_ADJUST;

    timeNodes = INIT_TIME_NODES;
}

// Called at the beginning of the search and calculates
// the bounds of time allowed for the current game ply.
// Currently support:
//      1) x base-time (sudden death)
//      2) x base-time (+ z increment)
//      3) x moves in y time (+ z increment)
void TimeManager::init(
  Color ac, std::int16_t ply, std::int32_t moveNum, const Options& options, Limit& limit) noexcept {
    // If have no time, no need to fully initialize TM.
    // start-time is used by move-time and Nodes-Time is used in elapsed calls.
    startTime = limit.startTime;

    auto& clock = limit.clocks[ac];

    std::uint64_t NodesTime = options["NodesTime"];

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
        if (timeNodes == INIT_TIME_NODES)
            timeNodes = std::max<TimePoint>(clock.time * NodesTime, TimePoint{1});

        // Convert from milliseconds to nodes
        clock.time = timeNodes;

        clock.inc *= NodesTime;

        OverheadTime *= NodesTime;
    }

    std::uint64_t ScaleFactor = use_nodes_time() ? NodesTime : 1;

    TimePoint ScaledTime = std::max<TimePoint>(clock.time / ScaleFactor, TimePoint{1});

    // clang-format off

    // Maximum move horizon
    std::uint16_t centiMTG = limit.movesToGo == 0
                  ? std::max<std::uint16_t>(MAX_CENTI_MTG - 10 * std::max(moveNum               - 20           , 0), MAX_CENTI_MTG - 1000)
                  : std::min<std::uint16_t>(MAX_CENTI_MTG + 10 * std::max(100 * limit.movesToGo - MAX_CENTI_MTG, 0), 100 * limit.movesToGo);

    // If less than one second, gradually reduce mtg
    if (centiMTG > MIN_CENTI_MTG && ScaledTime < 1000)
        centiMTG = std::max<std::uint16_t>(constexpr_ceil(5.0510 * double(ScaledTime)), MIN_CENTI_MTG);

    // Make sure RemainTime > 0 since use it as a divisor
    TimePoint RemainTime = std::max(clock.time + ((centiMTG - 100) * clock.inc - (centiMTG + 200) * OverheadTime) / 100, TimePoint{1});

    RemainTime = std::max<TimePoint>(constexpr_ceil(double(RemainTime) * double(options["TimePercent"]) / 100.0), TimePoint{1});

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale, maximumScale;

    if (limit.movesToGo == 0)
    {
        // Calculate time constants based on current remaining time
        double LogScaledTime = std::log10(double(ScaledTime) / 1000.0);  // NOLINT(bugprone-narrowing-conversions)

        // 1) x base-time (sudden death)
        // Sudden death time control
        if (clock.inc == 0)
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust == INIT_TIME_ADJUST)
            timeAdjust = std::max(-0.4126 + 0.2862 * std::log10(RemainTime), MIN_TIME_ADJUST);

        optimumScale = timeAdjust
                     * std::min(11.29900e-3 + std::min(3.47750e-3 + 28.41880e-5 * LogScaledTime, 4.06734e-3)
                                            * std::pow(2.82122 + double(ply), 0.466422),
                                0.213035 * double(clock.time) / double(RemainTime));
        maximumScale = std::min(std::max(3.66270 + 3.72690 * LogScaledTime, 2.75068) + 78.37482e-3 * double(ply),
                                6.35772);
        }
        // 2) x base-time (+ z increment)
        // If there is a healthy increment, remaining time can exceed the actual available
        // game time for the current move, so also cap to a percentage of available game time.
        else
        {
        // Extra time according to initial remaining Time (Only once at game start)
        if (timeAdjust == INIT_TIME_ADJUST)
            timeAdjust = std::max(-0.4354 + 0.3128 * std::log10(RemainTime), MIN_TIME_ADJUST);

        optimumScale = timeAdjust
                     * std::min(12.14310e-3 + std::min(3.21160e-3 + 32.11230e-5 * LogScaledTime, 5.08017e-3)
                                            * std::pow(2.94693 + double(ply), 0.461073),
                                0.213035 * double(clock.time) / double(RemainTime));
        maximumScale = std::min(std::max(3.39770 + 3.03950 * LogScaledTime, 2.94761) + 83.43972e-3 * double(ply),
                                6.67704);
        }
    }
    // 3) x moves in y time (+ z increment)
    else
    {
        optimumScale = std::min((0.00880 + 85.91065e-6 * double(ply)) / double(centiMTG),
                                 0.88000 * double(clock.time) / double(RemainTime));
        maximumScale = std::min(1.30000 + 0.00110 * double(centiMTG),
                                8.45000);
    }

    // Limit the maximum possible time for this move
    optimumTime = std::max<TimePoint>(constexpr_ceil(optimumScale * double(RemainTime)), options["MinimumMoveTime"]);

    maximumTime = std::max(
                    centiMTG < MIN_CENTI_MTG
                    ? clock.time
                    : std::min<TimePoint>(constexpr_ceil(maximumScale * double(optimumTime)),
                                          constexpr_ceil(0.825179 * double(clock.time)) - OverheadTime)
                    // Subtract small safety time from the allocated time to compensate for timer granularity, OS scheduling jitter, and measurement latency.
                    // Reduces the risk of accidental time forfeits (flagging) under heavy load or extreme time pressure.
                    - TimePoint(options["BufferTime"]),
                    TimePoint{1});
    // clang-format on

    if (options["Ponder"])
        optimumTime = constexpr_ceil(1.2500 * double(optimumTime));
}

// When in 'Nodes as Time' mode
void TimeManager::advance_time_nodes(std::int64_t nodes) noexcept {
    assert(use_nodes_time());

    timeNodes = std::max(timeNodes - nodes, std::int64_t(0));
}

}  // namespace DON
