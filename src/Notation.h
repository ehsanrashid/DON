#pragma once

#include <iomanip>
#include <sstream>

#include "Position.h"
#include "Types.h"

extern const std::string PieceChar;
extern const std::string ColorChar;

extern Color toColor(char);
extern char toChar(Color);

extern File toFile(char);
extern char toChar(File, bool = true);

extern Rank toRank(char);
extern char toChar(Rank);

extern std::string toString(Square);

extern char toChar(Piece);

/// Overloading output operators
extern std::ostream& operator<<(std::ostream&, Color);
extern std::ostream& operator<<(std::ostream&, File);
extern std::ostream& operator<<(std::ostream&, Rank);
extern std::ostream& operator<<(std::ostream&, Square);
extern std::ostream& operator<<(std::ostream&, Piece);


/// Converts a value to a string suitable for use with the UCI protocol specifications:
///
/// cp   <x>   The score x from the engine's point of view in centipawns.
/// mate <y>   Mate in y moves, not plies.
///            If the engine is getting mated use negative values for y.
inline std::string toString(Value v)
{
    assert(-VALUE_MATE <= v && v <= +VALUE_MATE);

    std::ostringstream oss;

    if (abs(v) < +VALUE_MATE - i32(MaxDepth))
    {
        oss << "cp " << toCP(v);
    }
    else
    {
        oss << "mate " << (v > VALUE_ZERO ?
                            +(VALUE_MATE - v + 1) :
                            -(VALUE_MATE + v + 0)) / 2;
    }
    return oss.str();
}

extern std::string canMove(Move);
extern Move canMove(const std::string&, const Position&);

extern std::string sanMove(Move, Position&);
extern Move sanMove(const std::string&, Position&);

//extern std::string lanMove(Move, Position&);
//extern Move lanMove(const std::string&, Position&);

extern std::string multipvInfo(const Thread *const&, i16, Value, Value);

//extern std::string prettyInfo(Thread *const&);


template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Move m)
{
    os << canMove(m);
    return os;
}



template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Piece p)
{
    os << toChar(p);
    return os;
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, Score score)
{
    os << std::showpos << std::showpoint
       << std::setw(5) << toCP(mgValue(score)) / 100.0 << " "
       << std::setw(5) << toCP(egValue(score)) / 100.0
       << std::noshowpoint << std::noshowpos;
    return os;
}
