#pragma once

#include "Type.h"

/// The TimeManagement class computes the optimal time to think depending on
/// the maximum available time, the game move number and other parameters.
class TimeManager {

private:
    TimePoint optimumTime{ 0 };
    TimePoint maximumTime{ 0 };

    u16 npmSec{ 0 };
    u64 nodes{ 0 }; // Available Nodes to play

public:

    TimeManager() = default;
    TimeManager(TimeManager const&) = delete;
    TimeManager& operator=(TimeManager const&) = delete;

    TimePoint optimum() const { return optimumTime; }
    TimePoint maximum() const { return maximumTime; }
    TimePoint elapsed() const;
    u16  timeNodes() const { return npmSec; }

    void reset();

    void setup(Color, i16);

    void updateNodes(Color c);

};

// Global Time Manager
extern TimeManager TimeMgr;
