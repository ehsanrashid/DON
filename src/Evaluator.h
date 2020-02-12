#pragma once

#include "Position.h"
#include "Types.h"

// Tempo bonus
constexpr Value Tempo = Value(28);

extern Value evaluate(const Position&);

extern std::string trace(const Position&);
