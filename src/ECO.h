//#pragma once
#ifndef ECO_H_
#define ECO_H_

#include "Zobrist.h"

typedef struct ECO
{
    Key         key_posi;
    Key         key_pawn;
    int8_t      length;
    ::std::string code;
    ::std::string name;
    ::std::string moves;
    //MoveList    moves;

} ECO;


extern void get_eco ();

#endif
