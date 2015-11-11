#ifndef _EVALUATOR_H_INC_
#define _EVALUATOR_H_INC_

#include "Type.h"

class Position;

namespace Evaluator {

    // Tempo bonus. Must be visible to search.
    const Value TEMPO = Value(20);

    extern Value evaluate    (const Position &pos);

    extern std::string trace (const Position &pos);

    extern void initialize ();
}

#endif // _EVALUATOR_H_INC_
