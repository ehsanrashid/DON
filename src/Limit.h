#pragma once

#include "Table.h"
#include "Type.h"

// Clock struct stores the time and inc per move in milli-seconds.
struct Clock
{
    TimePoint time;
    TimePoint inc;

    Clock()
        : time{0}
        , inc{0}
    {}
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
public:
    Array<Clock, COLORS> clock; // Search with Clock

    u08       movestogo;   // Search <x> moves to the next time control
    TimePoint moveTime;    // Search <x> exact time in milli-seconds
    Depth     depth;       // Search <x> depth(plies) only
    u64       nodes;       // Search <x> nodes only
    u08       mate;        // Search mate in <x> moves
    bool      infinite;    // Search until the "stop" command

    Limit()
        : clock{}
        , movestogo{0}
        , moveTime{0}
        , depth{DEPTH_ZERO}
        , nodes{0}
        , mate{0}
        , infinite{false}
    {}

    bool useTimeMgr() const
    {
        return !infinite
            && 0 == moveTime
            && DEPTH_ZERO == depth
            && 0 == nodes
            && 0 == mate;
    }

};
