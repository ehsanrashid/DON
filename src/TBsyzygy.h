#pragma once

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

    /// Possible states after a probing operation
    enum ProbeState
    {
        CHANGE_STM        = -1, // DTZ should check the other side
        FAILURE           =  0, // Probe failure (missing file table)
        SUCCESS           = +1, // Probe success
        ZEROING_BEST_MOVE = +2  // Best move zeroes DTZ (capture or pawn move)
    };

    extern std::string PathString;
    extern i32         MaxLimitPiece;

    extern i32      probeDTZ(Position&, ProbeState&);
    extern WDLScore probeWDL(Position&, ProbeState&);

    extern bool rootProbeDTZ(Position&, RootMoves&);
    extern bool rootProbeWDL(Position&, RootMoves&);

    extern void initialize(const std::string&);

    template<typename Elem, typename Traits>
    inline std::basic_ostream<Elem, Traits>&
        operator<<(std::basic_ostream<Elem, Traits> &os, WDLScore wdl)
    {
        switch (wdl)
        {
        case WDLScore::LOSS:         os << "Loss";         break;
        case WDLScore::BLESSED_LOSS: os << "Blessed Loss"; break;
        case WDLScore::DRAW:         os << "Draw";         break;
        case WDLScore::CURSED_WIN:   os << "Cursed win";   break;
        case WDLScore::WIN:          os << "Win";          break;
        default:                     os << "None";         break;
        }
        return os;
    }

    template<typename Elem, typename Traits>
    inline std::basic_ostream<Elem, Traits>&
        operator<<(std::basic_ostream<Elem, Traits> &os, ProbeState ps)
    {
        switch (ps)
        {
        case ProbeState::CHANGE_STM:        os << "Probed opponent side"; break;
        case ProbeState::FAILURE:           os << "Failure";              break;
        case ProbeState::SUCCESS:           os << "Success";              break;
        case ProbeState::ZEROING_BEST_MOVE: os << "Best move zeroes DTZ"; break;
        default:                            os << "None";                 break;
        }
        return os;
    }
}
