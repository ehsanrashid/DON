#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _EVALUATOR_H_INC_
#define _EVALUATOR_H_INC_

#include "Type.h"

class Position;

namespace Evaluator {

    // Tempo bonus. Must be visible to search.
    const Value TempoBonus = Value (17);

    extern void initialize ();

    extern Value evaluate    (const Position &pos);

    extern std::string trace (const Position &pos);
            
}

#endif // _EVALUATOR_H_INC_
