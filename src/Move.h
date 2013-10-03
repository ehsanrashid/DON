//#pragma once
#ifndef MOVE_H_
#define MOVE_H_

#include <vector>
#include <stack>
#include "Square.h"
#include "Piece.h"
#include "BitBoard.h"
#include "Notation.h"

inline Square sq_org (Move m)
{
    return Square ((m >> 6) & 0x3F);
}
inline Square sq_dst (Move m)
{
    return Square ((m >> 0) & 0x3F);
}
// promote type
inline PType prom_type (Move m)
{
    return PType (((m >> 12) & 0x03) + NIHT);
}
inline MType _mtype (Move m)
{
    return MType (PROMOTE & m);
}

inline void sq_org (Move &m, Square org)
{
    m &= 0xF03F;
    m |= (org << 6);
}
inline void sq_dst (Move &m, Square dst)
{
    m &= 0xFFC0;
    m |= (dst << 0);
}
inline void prom_type (Move &m, PType pt)
{
    m &= 0x0FFF;
    m |= (PROMOTE | ((pt - NIHT) & 0x03) << 12);
}

inline void _mtype (Move &m, MType mt)
{
    m &= ~PROMOTE;
    m |= mt;
}

inline Move operator~ (Move m)
{
    Move mm = m;
    sq_org (mm, ~sq_org (m));
    sq_dst (mm, ~sq_dst (m));
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
    Square org = sq_org (m);
    Square dst = sq_dst (m);
    if (org == dst) return false;

    uint8_t del_f = BitBoard::dist_file (org, dst);
    uint8_t del_r = BitBoard::dist_rank (org, dst);
    if (del_f == del_r) return true;
    if (0 == del_f || 0 == del_r) return true;
    if (5 == del_f*del_f + del_r*del_r) return true;
    return false;
}

//inline std::string to_string (Move m, bool chess960 = false)
//{
//    if (MOVE_NONE == m) return "(none)";
//    if (MOVE_NULL == m) return "(0000)";
//    if (!_ok (m)) return "(xxxx)";
//
//    Square org = sq_org (m);
//    Square dst = sq_dst (m);
//    if (!chess960)
//    {
//        if (CASTLE == _mtype (m))
//        {
//            dst = (dst > org ? F_G : F_C) | _rank (org);
//        }
//    }
//
//    std::string smove = to_string (org) + to_string (dst);
//    //ASSERT (4 == smove.length ());
//    MType mt = _mtype (m);
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
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, Move m)
{
    os << move_to_can (m); //to_string (m);
    return os;
}


//typedef union MoveParts
//{
//    Move m;
//
//    struct {
//
//        uint8_t type : 2;
//        uint8_t prom : 2;
//        uint8_t      : 0;
//        uint8_t dst  : 6;
//        uint8_t org  : 6;
//    };
//
//    MoveParts ()
//    {
//        m = MOVE_NONE;
//    }
//
//} MoveParts;



typedef std::vector<Move>   MoveList;
typedef std::stack <Move>   MoveStack;


template<class charT, class Traits>
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, const MoveList &lst_move)
{
    MoveList::const_iterator itr = lst_move.cbegin ();
    while (itr != lst_move.cend ())
    {
        os << *itr << std::endl;
        ++itr;
    }
    return os;
}


template<class charT, class Traits>
inline ::std::basic_ostream<charT, Traits>&
    operator<< (::std::basic_ostream<charT, Traits>& os, const MoveStack &stk_move)
{
    MoveStack stk_dup = stk_move;
    while (!stk_dup.empty ())
    {
        os << stk_dup.top () << ::std::endl;
        stk_dup.pop ();
    }
    return os;
}


#endif

