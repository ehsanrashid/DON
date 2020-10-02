// Common header of input features of NNUE evaluation function
#pragma once

#include "../nnue_common.h"

namespace Evaluator::NNUE::Features {

    class IndexList;

    template<typename... FeatureTypes>
    class FeatureSet;

    // Trigger to perform full calculations instead of difference only
    enum class TriggerEvent {
        FRIEND_KING_MOVED // calculate full evaluation when own king moves
    };

    enum class Side {
        FRIEND // side to move
    };

}  // namespace Evaluator::NNUE::Features
