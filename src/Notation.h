#ifndef _NOTATION_H_INC_
#define _NOTATION_H_INC_

#include <string>

#include "Type.h"

class Position;

namespace Notation {

    extern Move move_from_can (const std::string &can, const Position &pos);
    extern Move move_from_san (const std::string &san, Position &pos);
    //extern Move move_from_lan (const std::string &lan, const Position &pos);
    //extern Move move_from_fan (const std::string &fan, const Position &pos);

    extern const std::string move_to_can (Move m, bool c960 = false);
    extern const std::string move_to_san (Move m, Position &pos);
    //extern const std::string move_to_lan (Move m, Position &pos);
    //extern const std::string move_to_fan (Move m, Position &pos);

    extern const std::string score_uci (Value v, Value alpha = -VALUE_INFINITE, Value beta = VALUE_INFINITE);

    extern const std::string pretty_pv (Position &pos, i32 depth, Value value, u64 msecs, const Move pv[]);
    
}

template<class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>&
operator<< (std::basic_ostream<CharT, Traits> &os, const Move m)
{
    os << Notation::move_to_can (m);
    return os;
}

#endif // _NOTATION_H_INC_
