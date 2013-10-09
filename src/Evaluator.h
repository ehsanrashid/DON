//#pragma once
#ifndef EVALUATOR_H_
#define EVALUATOR_H_

#include "Type.h"

class Position;

namespace Evaluator {

    extern Value evaluate (const Position &pos, Value &margin);

}

#endif
