#pragma once

#include "type.h"

// Threshold for counter moves based pruning
constexpr i32 CounterMovePruneThreshold{ 0 };

/// Limit stores information sent by GUI after Go command about limit to search
///  - Available Time and Increment
///  - Moves to go
///  - Maximum Move Time
///  - Maximum Depth
///  - Maximum Nodes
///  - Minimum Mate
///  - Infinite analysis mode
///
///  - Start Time
struct Limit {

    // Clock struct stores the time and inc per move in milli-seconds.
    struct Clock {
        TimePoint time{ 0 };
        TimePoint inc{ 0 };
    };

    Clock     clock[COLORS]{};        // Search with Clock

    u08       movestogo{ 0 };       // Search <x> moves to the next time control
    TimePoint moveTime{ 0 };        // Search <x> exact time in milli-seconds
    Depth     depth{ DEPTH_ZERO };  // Search <x> depth(plies) only
    u64       nodes{ 0 };           // Search <x> nodes only
    u08       mate{ 0 };            // Search mate in <x> moves
    bool      infinite{ false };    // Search until the "stop" command
    bool      ponder{ false };      // Search in ponder mode.
    Moves     searchMoves;          // Restrict search to these root moves only

    TimePoint startTime;

    bool useTimeMgmt() const noexcept;

    void clear() noexcept;
};

namespace Searcher {

    extern void initialize() noexcept;

}

extern Limit Limits;

extern u16 PVCount;