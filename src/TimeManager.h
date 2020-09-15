#pragma once

#include "Type.h"
#include "Searcher.h"
#include "Thread.h"
#include "UCI.h"

/// The TimeManagement class computes the optimal time to think depending on
/// the maximum available time, the game move number and other parameters.
class TimeManager {

public:
    TimeManager();
    TimeManager(TimeManager const&) = delete;
    TimeManager(TimeManager&&) = delete;
    TimeManager& operator=(TimeManager const&) = delete;
    TimeManager& operator=(TimeManager&&) = delete;

    TimePoint optimum() const noexcept;
    TimePoint maximum() const noexcept;
    TimePoint elapsed() const noexcept;

    void clear() noexcept;

    void setup(Color, i16);

    u64 remainingNodes; // Remaining Nodes to play

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
    return(u16(Options["Time Nodes"]) == 0 ?
            now() - Limits.startTime :
            Threadpool.accumulate(&Thread::nodes));
}

inline void TimeManager::clear() noexcept {
    remainingNodes = 0;
}

// Global Time Manager
extern TimeManager TimeMgr;
