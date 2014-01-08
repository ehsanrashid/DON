//#pragma once
#ifndef PIECE_H_
#define PIECE_H_

#include "Type.h"
#include <string>

inline bool     _ok (PType pt)
{
    return (PAWN <= pt && pt <= KING);
}
inline char to_char (PType pt)
{
    switch (pt)
    {
    case NIHT: return 'N'; break;
    case BSHP: return 'B'; break;
    case ROOK: return 'R'; break;
    case QUEN: return 'Q'; break;
    case KING: return 'K'; break;
    }
    return ' ';
}
inline Piece operator| (Color c, PType pt)
{
    return Piece ((c << 3) | (pt));
}
inline Piece mk_piece  (Color c, PType pt)
{
    return c | pt;
}

inline bool     _ok (Piece p)
{
    return 
        (W_PAWN <= p && p <= W_KING) ||
        (B_PAWN <= p && p <= B_KING);
    //return (p & 0x7) && !(p & ~0xF);
}
inline PType p_type (Piece p)
{
    return PType ((p & PT_ALL));
}
inline Color p_color (Piece p)
{
    //return Color ((p >> 3) & BLACK);
    return Color (p >> 3);
}

inline Piece operator~ (Piece p)
{
    //return (~p_color(p) | p_type(p));
    return Piece (p ^ (BLACK << 3));
}

inline Piece to_piece (char  p)
{
    switch (p)
    {
    case 'P': return W_PAWN; break;
    case 'N': return W_NIHT; break;
    case 'B': return W_BSHP; break;
    case 'R': return W_ROOK; break;
    case 'Q': return W_QUEN; break;
    case 'K': return W_KING; break;

    case 'p': return B_PAWN; break;
    case 'n': return B_NIHT; break;
    case 'b': return B_BSHP; break;
    case 'r': return B_ROOK; break;
    case 'q': return B_QUEN; break;
    case 'k': return B_KING; break;
    }
    return PS_NO;
}

inline char to_char (Piece p)
{
    switch (p)
    {
    case W_PAWN: return 'P'; break;
    case W_NIHT: return 'N'; break;
    case W_BSHP: return 'B'; break;
    case W_ROOK: return 'R'; break;
    case W_QUEN: return 'Q'; break;
    case W_KING: return 'K'; break;

    case B_PAWN: return 'p'; break;
    case B_NIHT: return 'n'; break;
    case B_BSHP: return 'b'; break;
    case B_ROOK: return 'r'; break;
    case B_QUEN: return 'q'; break;
    case B_KING: return 'k'; break;
    }
    return ' ';
}

inline char to_char (Color c, PType pt)
{
    return to_char (c | pt);
}

template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits> &os, const Piece p)
{
    os << to_char (p);
    return os;
}

#endif
