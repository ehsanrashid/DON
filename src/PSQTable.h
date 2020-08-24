#pragma once

#include "Type.h"

class Position;

namespace PSQT {

    extern void initialize();

    extern Score computePSQ(Position const&);
}

extern Score PSQ[PIECES][SQUARES];
