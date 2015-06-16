#ifndef _NOTATION_H_INC_
#define _NOTATION_H_INC_

#include <cstring>
#include <iostream>

#include "Type.h"

class Position;

namespace Notation {

    inline char to_char (File f, bool low_case = true) { return char (i08(f) - i08(F_A)) + (low_case ? 'a' : 'A'); }

    inline char to_char (Rank r) { return char (i08(r) - i08(R_1)) + '1'; }

    inline std::string to_string (Square s)
    {
        return std::string{ to_char (_file (s)), to_char (_rank (s)) };
    }

    extern Move move_from_can (const std::string &can, const Position &pos);
    extern Move move_from_san (const std::string &san,       Position &pos);
    //extern Move move_from_lan (const std::string &lan, const Position &pos);

    extern std::string move_to_can (Move m, bool c960 = false);
    extern std::string move_to_san (Move m, Position &pos);
    //extern std::string move_to_lan (Move m, Position &pos);

    extern std::string to_string (Value v);

    extern std::string pretty_pv (Position &pos, i32 depth, Value value, TimePoint time, const Move *pv);
    
}

template<class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>&
operator<< (std::basic_ostream<CharT, Traits> &os, Move m)
{
    os << Notation::move_to_can (m);
    return os;
}

//template<class CharT, class Traits>
//inline std::basic_ostream<CharT, Traits>&
//    operator<< (std::basic_ostream<CharT, Traits> &os, Color c)
//{
//    os << COLOR_CHAR[c];
//    return os;
//}

//template<class CharT, class Traits>
//inline std::basic_ostream<CharT, Traits>&
//    operator<< (std::basic_ostream<CharT, Traits> &os, File f)
//{
//    os << Notation::to_char (f);
//    return os;
//}

//template<class CharT, class Traits>
//inline std::basic_ostream<CharT, Traits>&
//    operator<< (std::basic_ostream<CharT, Traits> &os, Rank r)
//{
//    os << Notation::to_char (r);
//    return os;
//}

//template<class CharT, class Traits>
//inline std::basic_ostream<CharT, Traits>&
//    operator<< (std::basic_ostream<CharT, Traits> &os, Square s)
//{
//    os << Notation::to_string (s);
//    return os;
//}

//template<class CharT, class Traits>
//inline std::basic_ostream<CharT, Traits>&
//    operator<< (std::basic_ostream<CharT, Traits> &os, const std::vector<Square> &sq_list)
//{
//    std::for_each (sq_list.begin (), sq_list.end (), [&os] (Square s) { os << s << std::endl; });
//    return os;
//}

//template<class CharT, class Traits>
//inline std::basic_ostream<CharT, Traits>&
//    operator<< (std::basic_ostream<CharT, Traits> &os, Piece p)
//{
//    os << PIECE_CHAR[p];
//    return os;
//}

//inline std::string to_string (CRight cr)
//{
//    std::string scastle;
//    if (can_castle (cr, CR_A))
//    {
//        if (can_castle (cr, CR_W))
//        {
//            scastle += "W:";
//            if (can_castle (cr, CR_WK)) scastle += " OO";
//            if (can_castle (cr, CR_WQ)) scastle += " OOO";
//            scastle += " - ";
//        }
//        if (can_castle (cr, CR_B))
//        {
//            scastle += "B:";
//            if (can_castle (cr, CR_BK)) scastle += " OO";
//            if (can_castle (cr, CR_BQ)) scastle += " OOO";
//        }
//    }
//    else
//    {
//        scastle = "-";
//    }
//    return scastle;
//}
//
//template<class CharT, class Traits>
//inline std::basic_ostream<CharT, Traits>&
//operator<< (std::basic_ostream<CharT, Traits> &os, CRight cr)
//{
//    os << to_string (cr);
//    return os;
//}

#endif // _NOTATION_H_INC_
