#ifndef _EVALUATOR_H_INC_
#define _EVALUATOR_H_INC_

#include "Type.h"

class Position;

namespace Evaluate {

    // Tempo bonus. Must be visible to search.
    const Value TempoBonus = Value(17);

    extern std::string trace (const Position &pos);

    extern Value evaluate    (const Position &pos);

    extern void configure ();
}

#endif // _EVALUATOR_H_INC_
