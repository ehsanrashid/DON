#pragma once

#include <iomanip>
#include <sstream>

#include "Position.h"
#include "Type.h"

extern const std::string PieceChar;
extern const std::string ColorChar;

extern Color toColor(char);
extern char toChar(Color);

extern File toFile(char);
extern char toChar(File, bool = true);

extern Rank toRank(char);
extern char toChar(Rank);

extern std::string toString(Square);

extern Piece toPiece(char);
extern char toChar(Piece);

extern std::string toString(Value);
extern std::string toString(Score);

extern std::string moveToCAN(Move);
extern Move moveOfCAN(const std::string&, const Position&);

/// Overloading output operators
extern std::ostream& operator<<(std::ostream&, Color);
extern std::ostream& operator<<(std::ostream&, File);
extern std::ostream& operator<<(std::ostream&, Rank);
extern std::ostream& operator<<(std::ostream&, Square);
extern std::ostream& operator<<(std::ostream&, Piece);
extern std::ostream& operator<<(std::ostream&, Value);
extern std::ostream& operator<<(std::ostream&, Score);
extern std::ostream& operator<<(std::ostream&, Move);


extern std::string multipvInfo(const Thread *const&, i16, Value, Value);

extern std::string moveToSAN(Move, Position&);
extern Move moveOfSAN(const std::string&, Position&);

//extern std::string prettyInfo(Thread *const&);
