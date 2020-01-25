#pragma once

#include <iomanip>
#include <sstream>

#include "Position.h"
#include "Type.h"

const std::string PieceChar("PNBRQK  pnbrqk");
const std::string ColorChar("wb-");

inline char to_char(File f, bool lower = true)
{
    return char((lower ? 'a' : 'A') + i08(f));
}

inline char to_char(Rank r)
{
    return char('1' + i08(r));
}

inline std::string to_string(Square s)
{
    return std::string{ to_char(_file(s)), to_char(_rank(s)) };
}
/// Converts a value to a string suitable for use with the UCI protocol specifications:
///
/// cp   <x>   The score x from the engine's point of view in centipawns.
/// mate <y>   Mate in y moves, not plies.
///            If the engine is getting mated use negative values for y.
inline std::string to_string(Value v)
{
    assert(-VALUE_MATE <= v && v <= +VALUE_MATE);

    std::ostringstream oss;

    if (abs(v) < +VALUE_MATE - i32(DEP_MAX))
    {
        oss << "cp " << value_to_cp(v);
    }
    else
    {
        oss << "mate " << (v > VALUE_ZERO ?
                            +(VALUE_MATE - v + 1) :
                            -(VALUE_MATE + v + 0)) / 2;
    }
    return oss.str();
}

extern std::string move_to_can(Move);
extern Move move_from_can(const std::string&, const Position&);

extern std::string move_to_san(Move, Position&);
extern Move move_from_san(const std::string&, Position&);

//extern std::string move_to_lan(Move, Position&);
//extern Move move_from_lan(const std::string&, Position&);

extern std::string multipv_info(const Thread *const&, i16, Value, Value);

extern std::string pretty_pv_info(Thread *const&);


template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, File f)
{
    os << to_char(f);
    return os;
}

template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, Rank r)
{
    os << to_char(r);
    return os;
}

template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, Square s)
{
    os << to_string(s);
    return os;
}

template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, Move m)
{
    os << move_to_can(m);
    return os;
}

template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, Color c)
{
    os << ColorChar[c];
    return os;
}

template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, Piece p)
{
    os << PieceChar[p];
    return os;
}

template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, Score score)
{
    os << std::showpos << std::showpoint
       << std::setw(5) << value_to_cp(mg_value(score)) / 100.0 << " "
       << std::setw(5) << value_to_cp(eg_value(score)) / 100.0
       << std::noshowpoint << std::noshowpos;
    return os;
}
