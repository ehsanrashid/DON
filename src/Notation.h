//#pragma once
#ifndef NOTATION_H_
#define NOTATION_H_

#include <string>
#include "Type.h"

class Position;

// Type of the Ambiguity
typedef enum AmbType : uint8_t
{
    AMB_NONE = 0,
    AMB_RANK = 1,
    AMB_FILE = 2,
    AMB_SQR  = 3,

} AmbType;

extern AmbType ambiguity (Move m, const Position &pos);

extern Move move_from_can (std::string &can, const Position &pos);
extern Move move_from_san (std::string &san, const Position &pos);
extern Move move_from_lan (std::string &lan, const Position &pos);
//extern Move move_from_fan (std::string &lan, const Position &pos);

extern std::string move_to_can (Move m, bool c960 = false);
extern std::string move_to_san (Move m, Position &pos);
extern std::string move_to_lan (Move m, Position &pos);
//extern Move move_to_fan (std::string &lan, const Position &pos);

#endif
