#pragma once

#include <iostream>

#include "RootMove.h"
#include "Type.h"

namespace TBSyzygy {

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

    inline std::ostream& operator<<(std::ostream &os, WDLScore wdlScore)
    {
        switch (wdlScore)
        {
        case LOSS:         os << "Loss";         break;
        case BLESSED_LOSS: os << "Blessed Loss"; break;
        case DRAW:         os << "Draw";         break;
        case CURSED_WIN:   os << "Cursed win";   break;
        case WIN:          os << "Win";          break;
        }
        return os;
    }

    /// Possible states after a probing operation
    enum ProbeState
    {
        OPP_SIDE    = -1, // DTZ should check the other side
        FAILURE     =  0, // Probe failure (missing file table)
        SUCCESS     = +1, // Probe success
        ZEROING     = +2  // Best move zeroes DTZ (capture or pawn move)
    };

    inline std::ostream& operator<<(std::ostream &os, ProbeState pState)
    {
        switch (pState)
        {
        case OPP_SIDE:  os << "Opponent side";        break;
        case FAILURE:   os << "Failure";              break;
        case SUCCESS:   os << "Success";              break;
        case ZEROING:   os << "Best move zeroes DTZ"; break;
        }
        return os;
    }

    extern std::string PathString;
    extern i32         MaxLimitPiece;

    extern i32      probeDTZ(Position&, ProbeState&);
    extern WDLScore probeWDL(Position&, ProbeState&);

    extern bool rootProbeDTZ(Position&, RootMoves&);
    extern bool rootProbeWDL(Position&, RootMoves&);

    extern void initialize(const std::string&);




}
