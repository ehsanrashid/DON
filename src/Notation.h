#ifndef _NOTATION_H_INC_
#define _NOTATION_H_INC_

#include <cstring>
#include <iostream>

#include "Type.h"

class Position;

namespace Notation {

    inline char to_char   (File f, bool lower = true) { return char (i08(f) - i08(F_A)) + (lower ? 'a' : 'A'); }

    inline char to_char   (Rank r) { return char (i08(r) - i08(R_1)) + '1'; }

    inline std::string to_string (Square s)
    {
        char sq[3] = { to_char (_file (s)), to_char (_rank (s)), '\0' };
        return sq;
    }

    extern Move move_from_can (const std::string &can, const Position &pos);
    extern Move move_from_san (const std::string &san, Position &pos);
    //extern Move move_from_lan (const std::string &lan, const Position &pos);
    //extern Move move_from_fan (const std::string &fan, const Position &pos);

    extern const std::string move_to_can (Move m, bool c960 = false);
    extern const std::string move_to_san (Move m, Position &pos);
    //extern const std::string move_to_lan (Move m, Position &pos);
    //extern const std::string move_to_fan (Move m, Position &pos);

    extern const std::string pretty_score (Value v, Value alpha = -VALUE_INFINITE, Value beta = VALUE_INFINITE);

    extern const std::string pretty_pv (Position &pos, i32 depth, Value value, u64 msecs, const Move pv[]);
    
}

//template<class CharT, class Traits>
//inline std::basic_ostream<CharT, Traits>&
//    operator<< (std::basic_ostream<CharT, Traits> &os, Color c)
//{
//    os << ColorChar[c];
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
//    operator<< (std::basic_ostream<CharT, Traits> &os, const Piece p)
//{
//    os << PieceChar[p];
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
//operator<< (std::basic_ostream<CharT, Traits> &os, const CRight cr)
//{
//    os << to_string (cr);
//    return os;
//}

template<class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>&
operator<< (std::basic_ostream<CharT, Traits> &os, const Move m)
{
    os << Notation::move_to_can (m);
    return os;
}

#endif // _NOTATION_H_INC_
