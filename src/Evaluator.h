#ifndef _EVALUATOR_H_INC_
#define _EVALUATOR_H_INC_

#include "Type.h"
#include "UCI.h"

class Position;

namespace Evaluate {

    // Tempo bonus. Must be visible to search.
    const Value TEMPO_VALUE = Value(17);

    extern Value evaluate    (const Position &pos);

    extern std::string trace (const Position &pos);

    extern void configure (const UCI::Option &);
}

#endif // _EVALUATOR_H_INC_
