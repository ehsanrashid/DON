#pragma once

#include <array>

#include "Position.h"
#include "Table.h"
#include "Type.h"

namespace PSQT {

    extern Score computePSQ(const Position&);

    extern void initialize();

}

extern Array<Score, PIECES, SQUARES> PSQ;
