// Class for difference calculation of NNUE evaluation function
#pragma once

#include "nnue_architecture.h"

namespace Evaluator::NNUE {

    // Class that holds the result of affine transformation of input features
    struct alignas(kCacheLineSize) Accumulator {

        i16 accumulation[2][kRefreshTriggers.size()][kTransformedFeatureDimensions];
        Value score;
        bool computedAccumulation;
        bool computedScore;
    };

}  // namespace Evaluator::NNUE
