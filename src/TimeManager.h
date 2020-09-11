#pragma once

#include "Type.h"

/// The TimeManagement class computes the optimal time to think depending on
/// the maximum available time, the game move number and other parameters.
class TimeManager {

public:
    TimeManager() = default;
    TimeManager(TimeManager const&) = delete;
    TimeManager(TimeManager&&) = delete;
    TimeManager& operator=(TimeManager const&) = delete;
    TimeManager& operator=(TimeManager&&) = delete;

    TimePoint elapsed() const noexcept;

    void clear() noexcept;

    void setup(Color, i16);

    TimePoint optimum{ 0 };
    TimePoint maximum{ 0 };

    u64 totalNodes{ 0 }; // Available Nodes to play

};

// Global Time Manager
extern TimeManager TimeMgr;
