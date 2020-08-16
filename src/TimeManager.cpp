#include "TimeManager.h"

#include <cfloat>
#include <cmath>

#include "Searcher.h"
#include "Thread.h"
#include "UCI.h"

TimeManager TimeMgr;

/// TimeManager::setup() is called at the beginning of the search and calculates the bounds
/// of time allowed for the current game ply.  We currently support:
///   * x basetime (+ z increment)
///   * x moves in y seconds (+ z increment)
void TimeManager::setup(Color c, i16 ply) {

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
    // Make sure timeLeft is > 0 since we may use it as a divisor
    TimePoint remainTime{ std::max(Limits.clock[c].time
                                 + Limits.clock[c].inc * (maxMovestogo - 1)
                                 - overheadMoveTime    * (maxMovestogo + 2), { 1 }) };
    // A user may scale time usage by setting UCI option "Slow Mover"
    // Default is 100 and changing this value will probably lose elo.
    remainTime = (remainTime * moveSlowness) / 100;

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale;
    double maximumScale;
    // x basetime (+ z increment)
    // If there is a healthy increment, timeLeft can exceed actual available
    // game time for the current move, so also cap to 20% of available game time.
    if (Limits.movestogo == 0) {
        optimumScale = std::min((0.2 * Limits.clock[c].time) / remainTime,
                                0.008 + std::pow(ply + 3.0, 0.5) / 250.0);
        maximumScale = std::min(4.0 + ply / 12.0, 7.0);
    }
    // x moves in y seconds (+ z increment)
    else {
        optimumScale = std::min((0.8 * Limits.clock[c].time) / remainTime,
                                (0.8 + ply / 128.0) / maxMovestogo);
        maximumScale = std::min(1.5 + 0.11 * maxMovestogo, 6.3);
    }
    // Never use more than 80% of the available time for this move
    optimum = TimePoint(optimumScale * remainTime);
    maximum = TimePoint(std::min(maximumScale * optimum, 0.8 * Limits.clock[c].time - overheadMoveTime));

    if (Options["Ponder"]) {
        optimum += optimum / 4;
    }
}

/// TimeManager::elapsed()
TimePoint TimeManager::elapsed() const noexcept {
    return{ u16(Options["Time Nodes"]) == 0 ?
                now() - Limits.startTime :
                TimePoint(Threadpool.sum(&Thread::nodes)) };
}

void TimeManager::clear() noexcept {
    totalNodes = 0;
}
