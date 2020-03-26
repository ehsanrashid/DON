#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

namespace PSQT {

    extern void initialize();

    extern Score computePSQ(Position const&);
}
