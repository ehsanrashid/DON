#pragma once

#include "Type.h"

/// The TimeManagement class computes the optimal time to think depending on
/// the maximum available time, the game move number and other parameters.
class TimeManager {

private:
    TimePoint _optimum{ 0 };
    TimePoint _maximum{ 0 };

    u16 _timeNodes{ 0 };
    u64 _nodes{ 0 }; // Available Nodes to play

public:

    TimeManager() = default;
    TimeManager(TimeManager const&) = delete;
    TimeManager& operator=(TimeManager const&) = delete;

    TimePoint optimum() const;
    TimePoint maximum() const;
    TimePoint elapsed() const;
    u16 timeNodes() const;

    void reset();

    void setup(Color, i16);

    void updateNodes(Color c);

};

// Global Time Manager
extern TimeManager TimeMgr;
