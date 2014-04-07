#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _NOTATION_H_INC_
#define _NOTATION_H_INC_

#include <string>

#include "Type.h"

class Position;

namespace Notation {

    // Type of the Ambiguity
    enum AmbiguityT : u08
    {
        AMB_NONE = 0,
        AMB_RANK = 1,
        AMB_FILE = 2,
        AMB_SQR  = 3,

    };

    extern AmbiguityT ambiguity (Move m, const Position &pos);

    extern Move move_from_can (const std::string &can, const Position &pos);
    extern Move move_from_san (const std::string &san, Position &pos);
    //extern Move move_from_lan (const std::string &lan, const Position &pos);
    //extern Move move_from_fan (const std::string &fan, const Position &pos);

    extern const std::string move_to_can (Move m, bool c960 = false);
    extern const std::string move_to_san (Move m, Position &pos);
    //extern const std::string move_to_lan (Move m, Position &pos);
    //extern const std::string move_to_fan (Move m, Position &pos);

    extern const std::string score_uci (Value v, Value alpha = -VALUE_INFINITE, Value beta = VALUE_INFINITE);

    extern const std::string pretty_pv (Position &pos, u08 depth, Value value, u64 msecs, const Move pv[]);
    
}

template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
operator<< (std::basic_ostream<charT, Traits> &os, const Move m)
{
    os << Notation::move_to_can (m);
    return os;
}

#endif // _NOTATION_H_INC_
