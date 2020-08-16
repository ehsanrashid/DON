#pragma once

#include <iomanip>
#include <ostream>

#include "Position.h"
#include "Type.h"

extern std::string const PieceChar;
extern std::string const ColorChar;

extern Color toColor(char) noexcept;

constexpr char toChar(Color c) noexcept {
    return isOk(c) ? ColorChar[c] : '-';
}

constexpr File toFile(char f) noexcept {
    return File(f - 'a');
}
constexpr char toChar(File f, bool lower = true) noexcept {
    return char(f + 'A' + 0x20 * lower);
}

constexpr Rank toRank(char r) noexcept {
    return Rank(r - '1');
}
constexpr char toChar(Rank r) noexcept {
    return char(r + '1');
}

extern std::string toString(Square) noexcept;

extern char toChar(PieceType) noexcept;

extern Piece toPiece(char) noexcept;
extern char toChar(Piece) noexcept;

extern std::string toString(Value);
extern std::string toString(Score);

extern std::string moveToCAN(Move);
extern Move moveOfCAN(std::string const&, Position const&);

/// Overloading output operators
extern std::ostream& operator<<(std::ostream&, Color);
extern std::ostream& operator<<(std::ostream&, File);
extern std::ostream& operator<<(std::ostream&, Rank);
extern std::ostream& operator<<(std::ostream&, Square);
extern std::ostream& operator<<(std::ostream&, Piece);
extern std::ostream& operator<<(std::ostream&, Value);
extern std::ostream& operator<<(std::ostream&, Score);
extern std::ostream& operator<<(std::ostream&, Move);

extern std::string moveToSAN(Move, Position&);
extern Move moveOfSAN(std::string const&, Position&);

//extern std::string prettyInfo(Thread*);
