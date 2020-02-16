#pragma once

#include "Position.h"
#include "Type.h"

namespace Evaluator
{
    // Tempo bonus
    constexpr Value Tempo{Value(28)};

    extern Value evaluate(const Position&);

    extern std::string trace(const Position&);
}
