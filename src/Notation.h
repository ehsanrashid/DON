#pragma once

#include <iomanip>
#include <sstream>

#include "Position.h"
#include "Type.h"

const std::string PieceChar{ "PNBRQK  pnbrqk" };
const std::string ColorChar{ "wb-" };

inline char toChar(File f, bool lower = true)
{
    return char('A' + i08(f) + 0x20 * lower);
}

inline char toChar(Rank r)
{
    return char('1' + i08(r));
}

inline std::string toString(Square s)
{
    return std::string{ toChar(fileOf(s)), toChar(rankOf(s)) };
}
/// Converts a value to a string suitable for use with the UCI protocol specifications:
///
/// cp   <x>   The score x from the engine's point of view in centipawns.
/// mate <y>   Mate in y moves, not plies.
///            If the engine is getting mated use negative values for y.
inline std::string toString(Value v)
{
    assert(-VALUE_MATE <= v && v <= +VALUE_MATE);

    std::ostringstream oss;

    if (abs(v) < +VALUE_MATE - i32(DEP_MAX))
    {
        oss << "cp " << valueCP(v);
    }
    else
    {
        oss << "mate " << (v > VALUE_ZERO ?
                            +(VALUE_MATE - v + 1) :
                            -(VALUE_MATE + v + 0)) / 2;
    }
    return oss.str();
}

extern std::string moveCAN(Move);
extern Move moveCAN(const std::string&, const Position&);

extern std::string moveSAN(Move, Position&);
extern Move moveSAN(const std::string&, Position&);

//extern std::string moveLAN(Move, Position&);
//extern Move moveLAN(const std::string&, Position&);

extern std::string multipvInfo(const Thread *const&, i16, Value, Value);

//extern std::string pretty_pv_info(Thread *const&);


template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, File f)
{
    os << toChar(f);
    return os;
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Rank r)
{
    os << toChar(r);
    return os;
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Square s)
{
    os << toString(s);
    return os;
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Move m)
{
    os << moveCAN(m);
    return os;
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Color c)
{
    os << ColorChar[c];
    return os;
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Piece p)
{
    os << PieceChar[p];
    return os;
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Score score)
{
    os << std::showpos << std::showpoint
       << std::setw(5) << valueCP(mgValue(score)) / 100.0 << " "
       << std::setw(5) << valueCP(egValue(score)) / 100.0
       << std::noshowpoint << std::noshowpos;
    return os;
}
