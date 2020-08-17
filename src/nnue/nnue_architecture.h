// Input features and network structure used in NNUE evaluation function
#pragma once

// Defines the network structure
#include "architectures/halfkp_256x2-32-32.h"

namespace Evaluator::NNUE {

    static_assert (kTransformedFeatureDimensions % MaxSimdWidth == 0, "");
    static_assert (Network::OutputDimensions == 1, "");
    static_assert (std::is_same<Network::OutputType, i32>::value, "");

    // Trigger for full calculation instead of difference calculation
    constexpr auto RefreshTriggers{ RawFeatures::RefreshTriggers };

}  // namespace Evaluator::NNUE
