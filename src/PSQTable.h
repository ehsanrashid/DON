#pragma once

#include <array>

#include "Position.h"
#include "Tables.h"
#include "Types.h"

extern Table<Score, MAX_PIECE, SQUARES> PSQ;

extern Score computePSQ(const Position&);

extern void initializePSQ();
