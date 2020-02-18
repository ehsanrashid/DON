#pragma once

#include <array>

#include "Position.h"
#include "Table.h"
#include "Type.h"

namespace PSQT {
    
    extern void initialize();

    extern Score computePSQ(const Position&);
}

extern Array<Score, PIECES, SQUARES> PSQ;
