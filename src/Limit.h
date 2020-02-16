#pragma once

#include "Table.h"
#include "Type.h"

// Clock struct stores the time and inc per move in milli-seconds.
struct Clock
{
    TimePoint time{};
    TimePoint inc{};
};

/// Limit stores information sent by GUI about available time to search the current move.
///  - Time and Increment
///  - Moves to go
///  - Depth
///  - Nodes
///  - Mate
///  - Infinite analysis mode
struct Limit
{
    Array<Clock, COLORS> clock{};           // Search with Clock

    u08       movestogo       {0};          // Search <x> moves to the next time control
    TimePoint moveTime        {0};          // Search <x> exact time in milli-seconds
    Depth     depth           {DEPTH_ZERO}; // Search <x> depth(plies) only
    u64       nodes           {0};          // Search <x> nodes only
    u08       mate            {0};          // Search mate in <x> moves
    bool      infinite        {false};      // Search until the "stop" command

    bool useTimeMgmt() const
    {
        return !infinite
            && 0 == moveTime
            && DEPTH_ZERO == depth
            && 0 == nodes
            && 0 == mate;
    }

};
