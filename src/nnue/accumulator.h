#pragma once
// Class for difference calculation of NNUE evaluation function

#include "architecture.h"

namespace Evaluator::NNUE {

    // The accumulator of a StateInfo without parent is set to the INIT state
    enum AccumulatorState { EMPTY, COMPUTED, INIT };

    // Class that holds the result of affine transformation of input features
    struct alignas(CacheLineSize) Accumulator {

        int16_t accumulation[COLORS][RefreshTriggers.size()][TransformedFeatureDimensions];

        AccumulatorState state[COLORS];
    };

}
