#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _EVALUATOR_H_INC_
#define _EVALUATOR_H_INC_

#include "Type.h"

class Position;

namespace Evaluator {

    extern void initialize ();

    extern Value evaluate (const Position &pos);

    extern std::string trace (const Position &pos);

}

#endif // _EVALUATOR_H_INC_
