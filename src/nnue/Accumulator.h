// Class for difference calculation of NNUE evaluation function
#pragma once

#include "Architecture.h"

namespace Evaluator::NNUE {

    // Class that holds the result of affine transformation of input features
    struct alignas(CacheLineSize) Accumulator {

        i16 accumulation[2][RefreshTriggers.size()][TransformedFeatureDimensions];
        Value score;
        bool accumulationComputed;
        bool scoreComputed;
    };

}  // namespace Evaluator::NNUE
