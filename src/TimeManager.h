#pragma once

#include "Types.h"

/// TimeManager class is used to computes the optimal time to think depending on the
/// maximum available time, the move game number and other parameters.
class TimeManager
{
private:
    u16 timeNodes;

public:
    TimePoint startTime;
    TimePoint optimumTime;
    TimePoint maximumTime;

    u64 availableNodes;

    TimeManager();

    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;

    TimePoint elapsedTime() const;

    void reset() { availableNodes = 0; }
    void set(Color, i16);
    void update(Color);
};


