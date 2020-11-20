#pragma once

#include "type.h"

class Position;

namespace PSQT {

    extern void initialize() noexcept;

    extern Score computePSQ(Position const&) noexcept;
}

extern Score PSQ[PIECES][SQUARES];
