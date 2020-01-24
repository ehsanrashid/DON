#pragma once

#include <array>
#include "Type.h"

class Position;

extern std::array<std::array<Score, SQ_NO>, MAX_PIECE> PSQ;

extern Score compute_psq(const Position&);

extern void psq_initialize();
