#pragma once

#include "Position.h"
#include "Type.h"

namespace Evaluator {

    extern Value evaluate(const Position&);

    extern std::string trace(const Position&);
}
