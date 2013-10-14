//#pragma once
#ifndef ECO_H_
#define ECO_H_

#include "Zobrist.h"

typedef struct ECO
{
    Key         posi_key;
    Key         pawn_key;
    int8_t      length;
    ::std::string code;
    ::std::string name;
    ::std::string moves;
    //MoveList    moves;

} ECO;


extern void get_eco ();

#endif
