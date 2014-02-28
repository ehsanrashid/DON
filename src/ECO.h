#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _ECO_H_INC_
#define _ECO_H_INC_

#include "Zobrist.h"

typedef struct ECO
{
    Key         posi_key;
    Key         pawn_key;
    int8_t      length;
    std::string code;
    std::string name;
    std::string moves;
    //std::vector<Move>    moves;

} ECO;


extern void get_eco ();

#endif // _ECO_H_INC_
