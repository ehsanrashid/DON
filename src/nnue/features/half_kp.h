#pragma once
// Definition of input features HalfKP of NNUE evaluation function

#include "../../type.h"
#include "features_common.h"

struct MoveInfo;
class Position;

namespace Evaluator::NNUE::Features {

    // Feature HalfKP: Combination of the position of own king
    // and the position of pieces other than kings
    template<Side AssociatedKing>
    class HalfKP {

    public:
        // Feature name
        static constexpr char const *Name{ "HalfKP(Friend)" };
        // Hash value embedded in the evaluation file
        static constexpr uint32_t HashValue{ 0x5D69D5B9u ^ (AssociatedKing == Side::FRIEND) };
        // Number of feature dimensions
        static constexpr IndexType Dimensions{ static_cast<IndexType>(SQUARES) * static_cast<IndexType>(PS_END) };
        // Maximum number of simultaneously active features
        static constexpr IndexType MaxActiveDimensions{ 30 }; // Kings don't count
        // Trigger for full calculation instead of difference calculation
        static constexpr TriggerEvent RefreshTrigger{ TriggerEvent::FRIEND_KING_MOVED };

        // Get a list of indices for active features
        static void appendActiveIndices(Position const&, Color, IndexList*);

        // Get a list of indices for recently changed features
        static void appendChangedIndices(Position const&, MoveInfo const&, Color, IndexList*, IndexList*);
    };

}
