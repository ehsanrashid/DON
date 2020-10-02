#pragma once

#include <iostream>
#include <string>
#include <string_view>

#include "position.h"
#include "rootmove.h"
#include "type.h"

namespace SyzygyTB {

    constexpr int16_t TBPIECES{ 7 };

    /// WDL Score
    enum WDLScore {
        WDL_LOSS         = -2, // Loss
        WDL_BLESSED_LOSS = -1, // Loss, but draw under 50-move rule
        WDL_DRAW         =  0, // Draw
        WDL_CURSED_WIN   = +1, // Win, but draw under 50-move rule
        WDL_WIN          = +2, // Win
        //WDL_NONE         = -1000
    };

    extern WDLScore operator-(WDLScore wdl);
    extern std::ostream& operator<<(std::ostream&, WDLScore);

    /// Possible states after a probing operation
    enum ProbeState {
        PS_OPP_SIDE = -1, // DTZ should check the other side
        PS_FAILURE  =  0, // Probe failure (missing file table)
        PS_SUCCESS  = +1, // Probe success
        PS_ZEROING  = +2  // Best move zeroes DTZ (capture or pawn move)
    };

    extern std::ostream& operator<<(std::ostream&, ProbeState);

    extern int16_t MaxPieceLimit;

    extern WDLScore probeWDL(Position&, ProbeState&);
    extern int32_t  probeDTZ(Position&, ProbeState&);

    extern bool rootProbeWDL(Position&, RootMoves&);
    extern bool rootProbeDTZ(Position&, RootMoves&);

    extern void rankRootMoves(Position&, RootMoves&);

    extern void initialize(std::string_view);

}
