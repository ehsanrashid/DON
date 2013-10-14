//#pragma once
#ifndef SQUARE_H_
#define SQUARE_H_

#include <string>
#include <vector>
#include "Type.h"

inline bool   _ok (File f)
{
    return !(f & 0xF8);
}
inline File operator~ (File f)
{
    return File (f ^ 0x07); // Mirror
}
inline File to_file (char f)
{
    char F = toupper (f);
    //ASSERT (F >= 'A' && F <= 'H');
    return File (F - 'A');
}
inline char to_char (File f, bool lower = true)
{
    return char (f - F_A) + (lower ? 'a' : 'A');
}


template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits>& os, File f)
{
    os << to_char (f);
    return os;
}


inline bool   _ok (Rank r)
{
    return !(r & 0xF8);
}
inline Rank operator~ (Rank r)
{
    return Rank (r ^ 0x07); // Flip
}
inline Rank to_rank (char r)
{
    //ASSERT (r >= '1' && r <= '8');
    return Rank (r - '1');
}
inline char to_char (Rank r)
{
    return char (r - R_1) + '1';
}

template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits>& os, Rank r)
{
    os << to_char (r);
    return os;
}

inline bool _ok (Color c)
{
    return (WHITE == c) || (BLACK == c);
}
inline Color operator~ (Color c)
{
    return Color (c ^ BLACK); // FLIP
}
inline Color to_color (char c)
{
    switch (tolower (c))
    {
    case 'w': return WHITE; break;
    case 'b': return BLACK; break;
    default: return CLR_NO; break;
    }
}

//inline std::string to_string (Color  c)
//{
//    switch (c)
//    {
//    case WHITE: return "WHITE";
//    case BLACK: return "BLACK";
//    }
//    return "PT_NO";
//}

inline char to_char (Color  c)
{
    switch (c)
    {
    case WHITE: return 'w';
    case BLACK: return 'b';
    }
    return '-';
}

template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits>& os, Color c)
{
    os << to_char (c);
    return os;
}


inline Square operator| (File f, Rank r)
{
    ASSERT (_ok (f));
    ASSERT (_ok (r));
    return Square ((r << 3) | f);
}
inline Square operator| (Rank r, File f)
{
    ASSERT (_ok (f));
    ASSERT (_ok (r));
    return Square ((~r << 3) | f);
}
inline Square   _Square (char f, char r)
{
    //ASSERT (f >= 'a' && f <= 'h');
    //ASSERT (r >= '1' && r <= '8');
    return to_file (f) | to_rank (r);
}
inline bool _ok (Square s)
{
    //return (SQ_A1 <= s && s <= SQ_H8);
    return !(s & 0xC0);
}
inline File _file (Square s)
{
    return File (s & 7);
}
inline Rank _rank (Square s)
{
    //return (Rank) ((s >> 3) & 7);
    return Rank (s >> 3);
}
inline Diag _diag18 (Square s)
{
    return Diag ((s >> 3) - (s & 0x07) + 7); // R - F + 7
}
inline Diag _diag81 (Square s)
{
    return Diag ((s >> 3) + (s & 0x07));     // R + F
}
inline Color _color (Square s)
{
    return Color (!((s ^ (s >> 3)) & BLACK));
}
inline Square operator~  (Square s)
{
    return Square (s ^ 0x38);  // FLIP   => SQ_A1 -> SQ_A8
}
inline Square operator!  (Square s)
{
    return Square (s ^ 0x07);  // MIRROR => SQ_A1 -> SQ_H1
}
inline Rank   rel_rank (Color c, Rank r)
{
    return Rank (r ^ (c * 0x07));
}
inline Rank   rel_rank (Color c, Square s)
{
    return rel_rank (c, _rank (s));
}
inline Square rel_sq (Color c, Square s)
{
    return Square (s ^ (c * 0x38));
}
inline bool opposite_colors (Square s1, Square s2)
{
    uint8_t s = uint8_t (s1) ^ uint8_t (s2);
    return ((s >> 3) ^ s) & 1;
}

inline std::string to_string (Square s)
{
    if (_ok (s))
    {
        char sq[] = { to_char (_file (s)), to_char (_rank (s)), '\0' };
        return sq;
        //return { to_char (_file (s)), to_char (_rank (s)), '\0' };
    }
    return "-";
}


template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits>& os, Square s)
{
    os << to_string (s);
    return os;
}


inline Delta pawn_push (Color c)
{
    switch (c)
    {
    case WHITE: return DEL_N; break;
    case BLACK: return DEL_S; break;
    default:    return DEL_O; break;
    }
}

typedef std::vector<Square> SquareList;
//typedef std::list  <Square> SquareList;


template<class charT, class Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits>& os, const SquareList &lst_sq)
{
    std::for_each (lst_sq.cbegin (), lst_sq.cend (), [&os] (Square s) { os << s << std::endl; });
    return os;
}


#endif
