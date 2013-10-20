//#pragma once
#ifndef CASTLE_H_
#define CASTLE_H_

#include "Type.h"
//#include <string>

inline CRight mk_castle_right (Color c)
{
    return CRight (CR_W << (c << BLACK));
}
inline CRight mk_castle_right (Color c, CSide cs)
{
    return CRight (CR_W_K << ((CS_Q == cs) + (c << BLACK)));
}

inline CRight operator~ (CRight cr)
{
    return CRight (((cr >> 2) & 0x3) | ((cr << 2) & 0xC));
}

inline CRight can_castle (CRight cr, CRight crx)
{
    return (cr & crx);
}
inline CRight can_castle (CRight cr, Color c)
{
    return (cr & mk_castle_right (c));
}
inline CRight can_castle (CRight cr, Color c, CSide cs)
{
    return (cr & mk_castle_right (c, cs));
}

inline ::std::string to_string (CRight cr)
{
    ::std::string scastle;
    if (can_castle (cr, CR_A))
    {
        if (can_castle (cr, CR_W))
        {
            scastle += "W:";
            if (can_castle (cr, CR_W_K)) scastle += " OO";
            if (can_castle (cr, CR_W_Q)) scastle += " OOO";
            scastle += " - ";
        }
        if (can_castle (cr, CR_B))
        {
            scastle += "B:";
            if (can_castle (cr, CR_B_K)) scastle += " OO";
            if (can_castle (cr, CR_B_Q)) scastle += " OOO";
        }
    }
    else
    {
        scastle = "-";
    }
    return scastle;
}


template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
operator<< (std::basic_ostream<charT, Traits> &os, const CRight cr)
{
    os << to_string (cr);
    return os;
}

#endif
