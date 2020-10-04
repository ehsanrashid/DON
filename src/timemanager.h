#pragma once

#include "searcher.h"
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

    TimePoint optimum() const noexcept;
    TimePoint maximum() const noexcept;
    TimePoint elapsed() const noexcept;

    void clear() noexcept;

    void setup(Color, int16_t) noexcept;

    uint64_t remainingNodes; // Remaining Nodes to play

private:

    TimePoint optimumTime;
    TimePoint maximumTime;
};

inline TimePoint TimeManager::optimum() const noexcept {
    return optimumTime;
}

inline TimePoint TimeManager::maximum() const noexcept {
    return maximumTime;
}

/// TimeManager::elapsed()
inline TimePoint TimeManager::elapsed() const noexcept {
    return(uint16_t(Options["Time Nodes"]) == 0 ?
            now() - Limits.startTime :
            Threadpool.accumulate(&Thread::nodes));
}

inline void TimeManager::clear() noexcept {
    remainingNodes = 0;
}

// Global Time Manager
extern TimeManager TimeMgr;
