//#pragma once
#ifndef MOVE_H_
#define MOVE_H_

#include <vector>
//#include <stack>

#include "Square.h"
#include "Piece.h"
#include "BitBoard.h"
#include "Notation.h"

inline Square org_sq (Move m)
{
    return Square ((m >> 6) & 0x3F);
}
inline Square dst_sq (Move m)
{
    return Square ((m >> 0) & 0x3F);
}

// Promote type
inline PType prom_type (Move m)
{
    return PType (((m >> 12) & 0x03) + NIHT);
}
// Move type
inline MType m_type (Move m)
{
    return MType (PROMOTE & m);
}

inline void org_sq (Move &m, Square org)
{
    m &= 0xF03F;
    m |= (org << 6);
}
inline void dst_sq (Move &m, Square dst)
{
    m &= 0xFFC0;
    m |= (dst << 0);
}
inline void prom_type (Move &m, PType pt)
{
    m &= 0x0FFF;
    m |= (PROMOTE | ((pt - NIHT) & 0x03) << 12);
}

inline void m_type (Move &m, MType mt)
{
    m &= ~PROMOTE;
    m |= mt;
}

inline Move operator~ (Move m)
{
    Move mm = m;
    org_sq (mm, ~org_sq (m));
    dst_sq (mm, ~dst_sq (m));
    return mm;
}

template<MType M>
extern Move mk_move (Square org, Square dst, PType pt);
template<MType M>
extern Move mk_move (Square org, Square dst);

template<>
inline Move mk_move<PROMOTE> (Square org, Square dst, PType pt)
{
    return Move (PROMOTE | ((pt - NIHT) << 12) | (org << 6) | (dst << 0));
}
template<MType M>
inline Move mk_move (Square org, Square dst)
{
    return Move (M | (org << 6) | (dst << 0));
}
// --------------------------------
// explicit template instantiations
template Move mk_move<NORMAL> (Square org, Square dst);
template Move mk_move<CASTLE> (Square org, Square dst);
template Move mk_move<ENPASSANT> (Square org, Square dst);
// --------------------------------
template<>
inline Move mk_move<PROMOTE> (Square org, Square dst)
{
    return mk_move<PROMOTE> (org, dst, QUEN);
}
inline Move mk_move (Square org, Square dst)
{
    return mk_move<NORMAL> (org, dst);
}

inline bool _ok (Move m)
{
    if (MOVE_NONE == m) return false;
    if (MOVE_NULL == m) return false;
    Square org = org_sq (m);
    Square dst = dst_sq (m);
    if (org == dst) return false;

    uint8_t del_f = BitBoard::file_dist (org, dst);
    uint8_t del_r = BitBoard::rank_dist (org, dst);
    if (del_f == del_r || 0 == del_f || 0 == del_r || 5 == del_f*del_f + del_r*del_r) return true;
    return false;
}

//inline std::string to_string (Move m, bool c960 = false)
//{
//    if (MOVE_NONE == m) return "(none)";
//    if (MOVE_NULL == m) return "(0000)";
//    if (!_ok (m)) return "(xxxx)";
//
//    Square org = org_sq (m);
//    Square dst = dst_sq (m);
//    if (!c960)
//    {
//        if (CASTLE == m_type (m))
//        {
//            dst = (dst > org ? F_G : F_C) | _rank (org);
//        }
//    }
//
//    std::string smove = to_string (org) + to_string (dst);
//    //ASSERT (4 == smove.length ());
//    MType mt = m_type (m);
//    switch (mt)
//    {
//    case PROMOTE:
//        smove += to_char (BLACK | prom_type (m)); // lower case
//        //ASSERT (5 == smove.length ());
//        break;
//#ifndef NDEBUG
//    case CASTLE:   smove += "oo"; break;
//    case ENPASSANT: smove += "ep"; break;
//#endif
//    }
//
//    return smove;
//}


template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits> &os, const Move m)
{
    os << move_to_can (m); //to_string (m);
    return os;
}

// ----------------------------------

template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits> &os, const MoveList &mov_lst)
{
    std::for_each (mov_lst.cbegin (), mov_lst.cend (), [&os] (Move m) { os << m << std::endl; });
    return os;
}

//typedef std::stack <Move>   MoveStack;

//template<class charT, class Traits>
//inline std::basic_ostream<charT, Traits>&
//    operator<< (std::basic_ostream<charT, Traits> &os, const MoveStack &stk_move)
//{
//    MoveStack stk_dup = stk_move;
//    while (!stk_dup.empty ())
//    {
//        os << stk_dup.top () << ::std::endl;
//        stk_dup.pop ();
//    }
//    return os;
//}

#endif