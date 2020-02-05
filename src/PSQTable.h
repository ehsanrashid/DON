#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

extern std::array<std::array<Score, SQ_NO>, MAX_PIECE> PSQ;

extern Score computePSQ(const Position&);

extern void initializePSQ();
