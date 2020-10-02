// Class for difference calculation of NNUE evaluation function
#pragma once

#include "architecture.h"

namespace Evaluator::NNUE {

    // Class that holds the result of affine transformation of input features
    struct alignas(CacheLineSize) Accumulator {

        i16 accumulation[2][RefreshTriggers.size()][TransformedFeatureDimensions];
        bool accumulationComputed;
    };

}  // namespace Evaluator::NNUE
