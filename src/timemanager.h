#pragma once

#include "thread.h"
#include "type.h"
#include "uci.h"

/// The TimeManagement class computes the optimal time to think depending on
/// the maximum available time, the game move number and other parameters.
class TimeManager {

public:

    constexpr TimeManager() noexcept;
    TimeManager(TimeManager const&) = delete;
    TimeManager(TimeManager&&) = delete;

    TimeManager& operator=(TimeManager const&) = delete;
    TimeManager& operator=(TimeManager&&) = delete;

    TimePoint optimum() const noexcept {
        return optimumTime;
    }
    TimePoint maximum() const noexcept {
        return maximumTime;
    }
    /// TimeManager::elapsed()
    TimePoint elapsed() const noexcept {
        return(timeNodes == 0 ?
            now() - startTime : Threadpool.accumulate(&Thread::nodes));
    }

    void clear() noexcept {
        remainingNodes[WHITE] = 0;
        remainingNodes[BLACK] = 0;
    }

    void setup(Color, int16_t) noexcept;

    TimePoint startTime;

    uint16_t  timeNodes;
    uint64_t  remainingNodes[COLORS]; // Remaining Nodes to play

private:

    TimePoint optimumTime;
    TimePoint maximumTime;
};

// Global Time Manager
extern TimeManager TimeMgr;
