#pragma once

#include "Type.h"

/// The TimeManagement class computes the optimal time to think depending on
/// the maximum available time, the game move number and other parameters.
class TimeManager {

private:
    TimePoint optimumTime{ 0 };
    TimePoint maximumTime{ 0 };

public:

    u16 timeNodes{ 0 };
    u64 availableNodes{ 0 };

    TimeManager() = default;

    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;

    TimePoint optimum() const { return optimumTime; }
    TimePoint maximum() const { return maximumTime; }
    TimePoint elapsed() const;

    void setup(Color, i16);
    void reset();

};

// Global Time Manager
extern TimeManager TimeMgr;
