#pragma once

#include "type.h"

// Threshold for counter moves based pruning
constexpr int32_t CounterMovePruneThreshold{ 0 };

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
        TimePoint time;
        TimePoint inc;
    };

    Limit() noexcept {
        clear();
    }

    bool useTimeMgmt() const noexcept {
        return clock[WHITE].time != 0
            || clock[BLACK].time != 0;
    }

    void clear() noexcept {
        clock[WHITE].time = 0; clock[WHITE].inc = 0;
        clock[BLACK].time = 0; clock[BLACK].inc = 0;

        movestogo   = 0;
        moveTime    = 0;
        depth       = DEPTH_ZERO;
        nodes       = 0;
        mate        = 0;
        infinite    = false;

        searchMoves.clear();
    }

    Clock     clock[COLORS];// Search with Clock

    uint8_t   movestogo;    // Search <x> moves to the next time control
    TimePoint moveTime;     // Search <x> exact time in milli-seconds
    Depth     depth;        // Search <x> depth(plies) only
    uint64_t  nodes;        // Search <x> nodes only
    uint8_t   mate;         // Search mate in <x> moves
    bool      infinite;     // Search until the "stop" command
    Moves     searchMoves;  // Restrict search to these root moves only
};

namespace Searcher {

    extern void initialize() noexcept;
}

// Global Limit
extern Limit Limits;
