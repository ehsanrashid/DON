#pragma once

#include "Position.h"
#include "Type.h"

namespace Evaluator {

    extern Value evaluate(Position const&);

    extern std::string trace(Position const&);
}
