#include "TimeManager.h"

#include <cfloat>
#include <cmath>

#include "Searcher.h"
#include "Thread.h"
#include "UCI.h"

TimeManager TimeMgr;

/// TimeManager::setup() is called at the beginning of the search and calculates the bounds
/// of time allowed for the current game ply.  We currently support:
//   1) x basetime (+z increment)
//   2) x moves in y seconds (+z increment)
void TimeManager::setup(Color c, i16 ply) {

    TimePoint minimumMoveTime{ Options["Minimum MoveTime"] };
    TimePoint overheadMoveTime{ Options["Overhead MoveTime"] };
    u32 moveSlowness{ Options["Move Slowness"] };
    u16 timeNodes{ Options["Time Nodes"] };

    // When playing in 'Nodes as Time' mode, then convert from time to nodes, and use values in time management.
    // WARNING: Given NodesTime (nodes per milli-seconds) must be much lower then the real engine speed to avoid time losses.
    if (timeNodes != 0) {
        // Only once at after ucinewgame
        if (totalNodes == 0) {
            totalNodes = Limits.clock[c].time * timeNodes;
        }
        // Convert from milli-seconds to nodes
        Limits.clock[c].time = totalNodes;
        Limits.clock[c].inc *= timeNodes;
    }

    // Maximum move horizon: Plan time management at most this many moves ahead.
    u08 maxMovestogo{ 50 };
    if (Limits.movestogo != 0) {
        maxMovestogo = std::min(Limits.movestogo, maxMovestogo);
    }
    // Adjust moveOverhead if there are tiny increments
    overheadMoveTime = clamp(overheadMoveTime, TimePoint(10), TimePoint(Limits.clock[c].inc / 2));

    // Make sure timeLeft is > 0 since we may use it as a divisor
    TimePoint leftTime{ std::max(TimePoint(1),
                                 Limits.clock[c].time
                               + Limits.clock[c].inc * (maxMovestogo - 1)
                               - overheadMoveTime * (2 + maxMovestogo)) };
    // A user may scale time usage by setting UCI option "Slow Mover"
    // Default is 100 and changing this value will probably lose elo.
    leftTime = leftTime * moveSlowness / 100;

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale,
           maximumScale;
    // x basetime (+ z increment)
    // If there is a healthy increment, timeLeft can exceed actual available
    // game time for the current move, so also cap to 20% of available game time.
    if (Limits.movestogo == 0)
    {
        optimumScale = std::min(0.007 + std::pow(ply + 3.0, 0.5) / 250.0,
                                0.2 * Limits.clock[c].time / double(leftTime));
        maximumScale = 4 + std::pow(ply + 3, 0.3);
    }
    // x moves in y seconds (+ z increment)
    else
    {
        optimumScale = std::min((0.8 + ply / 128.0) / maxMovestogo,
                                (0.8 * Limits.clock[c].time / double(leftTime)));
        maximumScale = std::min(6.3, 1.5 + 0.11 * maxMovestogo);
    }

    // Never use more than 80% of the available time for this move
    optimum = std::max(minimumMoveTime, TimePoint(optimumScale * leftTime));
    maximum = std::min(0.8 * Limits.clock[c].time - overheadMoveTime, maximumScale * optimum);

    if (Options["Ponder"]) {
        optimum += optimum / 4;
    }
}

/// TimeManager::elapsed()
TimePoint TimeManager::elapsed() const {
    return{ u16(Options["Time Nodes"]) == 0 ?
        now() - Limits.startTime :
        TimePoint(Threadpool.sum(&Thread::nodes)) };
}

void TimeManager::clear() {
    totalNodes = 0;
}
