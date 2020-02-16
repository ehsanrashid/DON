#pragma once

#include "Type.h"

// Threshold for counter moves based pruning
constexpr i32 CounterMovePruneThreshold = 0;

extern Depth TBProbeDepth;
extern i32   TBLimitPiece;
extern bool  TBUseRule50;
extern bool  TBHasRoot;

namespace Searcher
{
    extern void initialize();
}
