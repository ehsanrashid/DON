#include "timemanager.h"

#include <cfloat>
#include <cmath>

#include "searcher.h"

TimeManager TimeMgr;

constexpr TimeManager::TimeManager() noexcept :
    startTime{ 0 },
    timeNodes{ 0 },
    remainingNodes{ 0, 0 },
    optimumTime{ 0 },
    maximumTime{ 0 } {
}

/// TimeManager::setup() is called at the beginning of the search and calculates the bounds
/// of time allowed for the current game ply.  We currently support:
///   * x basetime (+ z increment)
///   * x moves in y seconds (+ z increment)
void TimeManager::setup(Color c, int16_t ply) noexcept {

    TimePoint overheadMoveTime  { Options["Overhead MoveTime"] };
    uint32_t  moveSlowness      { Options["Move Slowness"] };
    
    timeNodes = uint16_t(Options["Time Nodes"]);

    // When playing in 'Nodes as Time' mode, then convert from time to nodes, and use values in time management.
    // WARNING: Given NodesTime (nodes per milli-seconds) must be much lower then the real engine speed to avoid time losses.
    if (timeNodes != 0) {
        // Only once at after ucinewgame
        if (remainingNodes[c] == 0) {
            remainingNodes[c] = Limits.clock[c].time * timeNodes;
        }
        // Convert from milli-seconds to nodes
        Limits.clock[c].time = remainingNodes[c];
        Limits.clock[c].inc *= timeNodes;
    }

    // Maximum move horizon: Plan time management at most this many moves ahead.
    int32_t const maxMovestogo{ Limits.movestogo != 0 ? std::min(int32_t(Limits.movestogo), 50) : 50 };
    
    // Make sure timeLeft is > 0 since we may use it as a divisor
    TimePoint remainTime{ std::max(Limits.clock[c].time
                                 + Limits.clock[c].inc * (maxMovestogo - 1)
                                 - overheadMoveTime    * (maxMovestogo + 2), { 1 }) };
    // A user may scale time usage by setting UCI option "Slow Mover"
    // Default is 100 and changing this value will probably lose ELO.
    remainTime = remainTime * moveSlowness / 100;

    // optimumScale is a percentage of available time to use for the current move.
    // maximumScale is a multiplier applied to optimumTime.
    double optimumScale;
    double maximumScale;
    // x basetime (+ z increment)
    // If there is a healthy increment, timeLeft can exceed actual available
    // game time for the current move, so also cap to 20% of available game time.
    if (Limits.movestogo == 0) {
        optimumScale = std::min(0.2 * Limits.clock[c].time / double(remainTime),
                                0.0084 + std::pow(ply + 3.0, 0.5) * 0.0042);
        maximumScale = std::min(4.0 + ply / 12.0, 7.0);
    } else {
    // x moves in y seconds (+ z increment)
        optimumScale = std::min(0.8 * Limits.clock[c].time / double(remainTime),
                               (0.8 + ply / 128.0) / double(maxMovestogo));
        maximumScale = std::min(1.5 + 0.11 * maxMovestogo, 6.3);
    }
    // Never use more than 80% of the available time for this move
    optimumTime = TimePoint(optimumScale * remainTime);
    maximumTime = TimePoint(std::min(maximumScale * optimumTime, 0.8 * Limits.clock[c].time - overheadMoveTime));

    if (Options["Ponder"]) {
        optimumTime += optimumTime / 4;
    }
}
