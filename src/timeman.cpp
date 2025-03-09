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

#include "position.h"
#include "search.h"
#include "ucioption.h"

namespace DON {

namespace {

constexpr std::int16_t MaxMovesToGo = 50;
constexpr float        MtgFactor    = 0.05051;

}  // namespace

// When in 'Nodes as Time' mode
void TimeManager::update_nodes(std::int64_t usedNodes) noexcept {
    assert(use_nodes_time());
    remainNodes = std::max<std::int64_t>(remain_nodes() - usedNodes, 0) + OffsetNode;
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
            remainNodes = clock.time * nodesTime + OffsetNode;  // Time is in msec

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
                     ? std::min<std::int16_t>(std::floor(MaxMovesToGo + 0.1f * std::max(movesToGo - MaxMovesToGo, 0)), movesToGo)
                     : std::max<std::int16_t>(std::ceil (MaxMovesToGo - 0.1f * std::max(pos.move_num() - 20     , 0)), MaxMovesToGo - 10);

    // If less than one second, gradually reduce mtg
    if (scaledTime < 1000 && mtg > MtgFactor * scaledInc)
        mtg = std::clamp<std::int16_t>(MtgFactor * scaledTime, 2, mtg);

    assert(mtg > 0);

    // Make sure remainTime > 0 since use it as a divisor
    TimePoint remainTime =
      std::max(clock.time + (-1 + mtg) * clock.inc - (+2 + mtg) * moveOverhead, TimePoint(1));

    std::int16_t ply = pos.ply();

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    float optimumScale, maximumScale;

    // x moves in y time (+ z increment)
    if (movesToGo != 0)
    {
        optimumScale = std::min((0.88000f + 85.91060e-4f * ply) / mtg,
                                 0.00000f +  0.88000f    * clock.time / remainTime);
        maximumScale = std::min( 1.30000f + 0.11000f * mtg, 8.45000f);
    }
    // x basetime (+ z increment)
    // If there is a healthy increment, remaining time can exceed the actual available
    // game time for the current move, so also cap to a percentage of available game time.
    else
    {
        // Extra time according to initial remaining Time (Only once at game start)
        if (initialAdjust < 0.0f)
            initialAdjust = std::max(-0.4354f + 0.3128f * std::log10(1.0000f * remainTime), 1.0000e-6f);

        // Calculate time constants based on current remaining time
        auto logScaledTime = std::log10(1.0000e-3f * scaledTime);

        auto optimumOffset = 12.14310e-3f;
        auto optimumFactor = std::min(3.21160e-3f + 32.11230e-5f * logScaledTime, 5.08017e-3f);
        optimumScale = initialAdjust
                     * std::min(optimumOffset + optimumFactor * std::pow(2.94693f + ply, 0.461073f),
                                0.00000f + 0.213035f * clock.time / remainTime);

        auto maximumOffset = std::max(3.39770f    +  3.03950f    * logScaledTime, 2.94761f);
        auto maximumFactor = 83.43972e-3f;
        maximumScale = std::min(maximumOffset + maximumFactor * ply, 6.67704f);
    }

    // Limit the maximum possible time for this move
    optimumTime = TimePoint(optimumScale * remainTime);
    maximumTime = std::max(mtg > 1 ? TimePoint(std::min(0.825179f * clock.time - moveOverhead, maximumScale * optimumTime)) - 10
                                   : clock.time - moveOverhead,
                           TimePoint(1));

    // clang-format on

    if (options["Ponder"])
        optimumTime *= 1.2500f;
}

}  // namespace DON
