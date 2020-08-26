// A class template that represents the input feature set of the NNUE evaluation function
#pragma once

#include <array>

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
        // Get a list of indices for active features
        template<typename IndexListType>
        static void appendActiveIndices(Position const &pos, TriggerEvent trigger, IndexListType active[2]) {
            for (auto perspective : { WHITE, BLACK }) {
                Derived::collectActiveIndices(pos, trigger, perspective, &active[perspective]);
            }
        }

        // Get a list of indices for recently changed features
        template<typename PositionType, typename IndexListType>
        static void appendChangedIndices(PositionType const &pos, TriggerEvent trigger, IndexListType removed[2], IndexListType added[2], bool reset[2]) {

            auto const &dp{ pos.state()->dirtyPiece };
            if (dp.dirtyCount == 0) {
                return;
            }
            for (auto perspective : { WHITE, BLACK }) {
                reset[perspective] = false;
                switch (trigger) {
                case TriggerEvent::FRIEND_KING_MOVED:
                    reset[perspective] = dp.piece[0] == (perspective|KING);
                    break;
                default:
                    assert(false);
                    break;
                }
                if (reset[perspective]) {
                    Derived::collectActiveIndices(pos, trigger, perspective, &added[perspective]);
                }
                else {
                    Derived::collectChangedIndices(pos, trigger, perspective, &removed[perspective], &added[perspective]);
                }
            }
        }
    };

    // Class template that represents the feature set
    template<typename FeatureType>
    class FeatureSet<FeatureType> :
        public FeatureSetBase<FeatureSet<FeatureType>> {

    public:
        // Hash value embedded in the evaluation file
        static constexpr u32 HashValue{ FeatureType::HashValue };
        // Number of feature dimensions
        static constexpr IndexType Dimensions{ FeatureType::Dimensions };
        // Maximum number of simultaneously active features
        static constexpr IndexType MaxActiveDimensions{ FeatureType::MaxActiveDimensions };
        // Trigger for full calculation instead of difference calculation
        using SortedTriggerSet = CompileTimeList<TriggerEvent, FeatureType::RefreshTrigger>;
        static constexpr auto RefreshTriggers{ SortedTriggerSet::Values };

    private:
        // Get a list of indices for active features
        static void collectActiveIndices(Position const &pos, TriggerEvent const trigger, Color const perspective, IndexList *const active) {
            if (FeatureType::RefreshTrigger == trigger) {
                FeatureType::appendActiveIndices(pos, perspective, active);
            }
        }

        // Get a list of indices for recently changed features
        static void collectChangedIndices(Position const &pos, TriggerEvent const trigger, Color const perspective, IndexList *const removed, IndexList *const added) {
            if (FeatureType::RefreshTrigger == trigger) {
                FeatureType::appendChangedIndices(pos, perspective, removed, added);
            }
        }

        // Make the base class and the class template that recursively uses itself a friend
        friend class FeatureSetBase<FeatureSet>;
        
        template<typename... FeatureTypes>
        friend class FeatureSet;
    };

}  // namespace Evaluator::NNUE::Features
