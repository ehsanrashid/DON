#pragma once

#include "Types.h"

namespace Searcher {

    extern Depth TBProbeDepth;
    extern i32   TBLimitPiece;
    extern bool  TBUseRule50;
    extern bool  TBHasRoot;

    extern void initialize();

    extern void clear();

}
