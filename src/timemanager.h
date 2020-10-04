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
        return(uint16_t(Options["Time Nodes"]) == 0 ?
            now() - startTime :
            Threadpool.accumulate(&Thread::nodes));
    }

    void clear() noexcept {
        remainingNodes = 0;
    }

    void setup(Color, int16_t) noexcept;

    uint64_t remainingNodes; // Remaining Nodes to play
    TimePoint startTime;

private:

    TimePoint optimumTime;
    TimePoint maximumTime;
};

// Global Time Manager
extern TimeManager TimeMgr;
