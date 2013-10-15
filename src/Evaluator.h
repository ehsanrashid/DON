//#pragma once
#ifndef EVALUATOR_H_
#define EVALUATOR_H_

#include "Type.h"

class Position;

namespace Evaluator {

    extern void initialize ();

    extern Value evaluate (const Position &pos, Value &margin);

    extern std::string trace (const Position &pos);

}

#endif
