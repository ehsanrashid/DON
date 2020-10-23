#pragma once
// A class template that represents the input feature set of the NNUE evaluation function

#include <array>

#include "../../type.h"
#include "../../position.h"
#include "features_common.h"

namespace Evaluator::NNUE::Features {

    // Class template that represents a list of values
    template<typename T, T... Values>
    struct CompileTimeList;

    template<typename T, T First, T... Remaining>
    struct CompileTimeList<T, First, Remaining...> {

        static constexpr bool contains(T value) {
            return value == First || CompileTimeList<T, Remaining...>::contains(value);
        }

        static constexpr std::array<T, sizeof...(Remaining) + 1> Values{ {First, Remaining...} };
    };

    // Base class of feature set
    template<typename Derived>
    class FeatureSetBase {

    public:

    };

    // Class template that represents the feature set
    template<typename FeatureType>
    class FeatureSet<FeatureType> :
        public FeatureSetBase<FeatureSet<FeatureType>> {

    public:
        // Hash value embedded in the evaluation file
        static constexpr uint32_t HashValue{ FeatureType::HashValue };
        // Number of feature dimensions
        static constexpr IndexType Dimensions{ FeatureType::Dimensions };
        // Maximum number of simultaneously active features
        static constexpr IndexType MaxActiveDimensions{ FeatureType::MaxActiveDimensions };
        // Trigger for full calculation instead of difference calculation
        using SortedTriggerSet = CompileTimeList<TriggerEvent, FeatureType::RefreshTrigger>;

        static constexpr auto RefreshTriggers{ SortedTriggerSet::Values };

    private:

    };

}
