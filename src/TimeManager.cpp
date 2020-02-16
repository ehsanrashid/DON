#include "TimeManager.h"

#include <cfloat>
#include <cmath>

#include "Thread.h"
#include "UCI.h"

namespace
{
    // Skew-logistic function based on naive statistical analysis of
    // "how many games are still undecided after n half-moves".
    // Game is considered "undecided" as long as neither side has >275cp advantage.
    // Data was extracted from the CCRL game database with some simple filtering criteria.
    double moveImportance(i16 ply)
    {
        //                                             Shift    Scale   Skew
        return std::max(std::pow(1.00 + std::exp((ply - 64.50) / 6.85), -0.171), DBL_MIN); // Ensure non-zero
    }

    template<bool Optimum>
    TimePoint remainingTime(TimePoint time, u08 movestogo, i16 ply, double moveSlowness)
    {
        constexpr auto  StepRatio = 7.30 - 6.30 * Optimum; // When in trouble, can step over reserved time with this ratio
        constexpr auto StealRatio = 0.34 - 0.34 * Optimum; // However must not steal time from remaining moves over this ratio

        auto moveImp1{moveImportance(ply) * moveSlowness};
        auto moveImp2{0.0};
        for (u08 i = 1; i < movestogo; ++i)
        {
            moveImp2 += moveImportance(ply + 2 * i);
        }

        auto timeRatio1{(1.0) / (1.0 + moveImp2 / (moveImp1 * StepRatio))};
        auto timeRatio2{(1.0 + (moveImp2 * StealRatio) / moveImp1) / (1.0 + moveImp2 / moveImp1)};

        return TimePoint(time * std::min(timeRatio1, timeRatio2));
    }

}

TimeManager::TimeManager()
{
    reset();
}

/// TimeManager::elapsedTime()
TimePoint TimeManager::elapsedTime() const
{
    return 0 != timeNodes ?
            TimePoint(Threadpool.sum(&Thread::nodes)) :
            now() - startTime;
}

void TimeManager::reset() { availableNodes = 0; }

/// TimeManager::set() calculates the allowed thinking time out of the time control and current game ply.
/// Support four different kind of time controls, passed in 'limit':
///
/// increment == 0, moves to go == 0 => y basetime                             ['sudden death']
/// increment != 0, moves to go == 0 => y basetime + z increment
/// increment == 0, moves to go != 0 => x moves in y basetime                  ['standard']
/// increment != 0, moves to go != 0 => x moves in y basetime + z increment
///
/// Minimum MoveTime = No matter what, use at least this much time before doing the move, in milli-seconds.
/// Overhead MoveTime = Attempt to keep at least this much time for each remaining move, in milli-seconds.
/// Move Slowness = Move Slowness, in %age.
void TimeManager::set(Color c, i16 ply)
{
    TimePoint minimumMoveTime {Options["Minimum MoveTime"]};
    TimePoint overheadMoveTime{Options["Overhead MoveTime"]};
    double moveSlowness       {Options["Move Slowness"]};
    moveSlowness /= 100.0;

    timeNodes = Options["Time Nodes"];

    // When playing in 'Nodes as Time' mode, then convert from time to nodes, and use values in time management.
    // WARNING: Given NodesTime (nodes per milli-seconds) must be much lower then the real engine speed to avoid time losses.
    if (0 != timeNodes)
    {
        // Only once at after ucinewgame
        if (0 == availableNodes)
        {
            availableNodes = Threadpool.limit.clock[c].time * timeNodes;
        }
        // Convert from milli-seconds to nodes
        Threadpool.limit.clock[c].time = TimePoint(availableNodes);
        Threadpool.limit.clock[c].inc *= timeNodes;
    }

    optimumTime =
    maximumTime = std::max(Threadpool.limit.clock[c].time, minimumMoveTime);
    // Move Horizon:
    // Plan time management at most this many moves ahead.
    u08 maxMovestogo{50};
    if (0 != Threadpool.limit.movestogo)
    {
        maxMovestogo = std::min(Threadpool.limit.movestogo, maxMovestogo);
    }
    // Calculate optimum time usage for different hypothetic "moves to go" and
    // choose the minimum of calculated search time values.
    for (u08 movestogo = 1; movestogo <= maxMovestogo; ++movestogo)
    {
        // Calculate thinking time for hypothetical "moves to go"
        auto time = std::max(
                        Threadpool.limit.clock[c].time
                      + Threadpool.limit.clock[c].inc * (movestogo - 1)
                        // ClockTime: Attempt to keep this much time at clock.
                        // MovesTime: Attempt to keep at most this many moves time at clock.
                      - overheadMoveTime * (2 + std::min(movestogo, {40})) // (ClockTime + MovesTime)
                      , {0});

        optimumTime = std::min(optimumTime, minimumMoveTime + remainingTime<true >(time, movestogo, ply, moveSlowness));
        maximumTime = std::min(maximumTime, minimumMoveTime + remainingTime<false>(time, movestogo, ply, moveSlowness));
    }

    if (Options["Ponder"])
    {
        optimumTime += optimumTime / 4;
    }
}

void TimeManager::update(Color c)
{
    // When playing in 'Nodes as Time' mode
    if (0 != timeNodes)
    {
        availableNodes += Threadpool.limit.clock[c].inc - Threadpool.sum(&Thread::nodes);
    }
}

