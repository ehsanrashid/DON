#pragma once

#include <iostream>

#include "RootMove.h"
#include "Type.h"

/// WDL Score
enum WDLScore
{
    LOSS         = -2, // Loss
    BLESSED_LOSS = -1, // Loss, but draw under 50-move rule
    DRAW         =  0, // Draw
    CURSED_WIN   = +1, // Win, but draw under 50-move rule
    WIN          = +2, // Win
};

inline WDLScore operator-(WDLScore wdl) { return WDLScore(-i32(wdl)); }
extern std::ostream& operator<<(std::ostream&, WDLScore);

/// Possible states after a probing operation
enum ProbeState
{
    OPP_SIDE    = -1, // DTZ should check the other side
    FAILURE     =  0, // Probe failure (missing file table)
    SUCCESS     = +1, // Probe success
    ZEROING     = +2  // Best move zeroes DTZ (capture or pawn move)
};

extern std::ostream& operator<<(std::ostream&, ProbeState);

extern std::string PathString;
extern i32         MaxLimitPiece;

extern i32      probeDTZ(Position&, ProbeState&);
extern WDLScore probeWDL(Position&, ProbeState&);

extern bool rootProbeDTZ(Position&, RootMoves&);
extern bool rootProbeWDL(Position&, RootMoves&);

namespace SyzygyTB
{
    extern void initialize(const std::string&);
}
