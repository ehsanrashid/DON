#pragma once

#include "Position.h"
#include "Type.h"

// Tempo bonus
extern const Value Tempo;

extern Value evaluate(const Position&);

extern std::string trace(const Position&);
