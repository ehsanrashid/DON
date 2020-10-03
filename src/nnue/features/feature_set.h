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

            auto collectOne = [&](MoveInfo const &mi) {
                for (Color perspective : { WHITE, BLACK }) {
                    switch (trigger) {
                    case TriggerEvent::FRIEND_KING_MOVED:
                        reset[perspective] = mi.piece[0] == (perspective|KING);
                        break;
                    default:
                        assert(false);
                        break;
                    }
                    if (reset[perspective]) {
                        Derived::collectActiveIndices(pos, trigger, perspective, &added[perspective]);
                    }
                    else {
                        Derived::collectChangedIndices(pos, mi, trigger, perspective, &removed[perspective], &added[perspective]);
                    }
                }
            };

            auto collectTwo = [&](MoveInfo const &mi1, MoveInfo const &mi2) {
                for (Color perspective : { WHITE, BLACK }) {
                    switch (trigger) {
                    case TriggerEvent::FRIEND_KING_MOVED:
                        reset[perspective] = mi1.piece[0] == (perspective|KING)
                                          || mi2.piece[0] == (perspective|KING);
                        break;
                    default:
                        assert(false);
                        break;
                    }
                    if (reset[perspective]) {
                        Derived::collectActiveIndices(pos, trigger, perspective, &added[perspective]);
                    }
                    else {
                        Derived::collectChangedIndices(pos, mi1, trigger, perspective, &removed[perspective], &added[perspective]);
                        Derived::collectChangedIndices(pos, mi2, trigger, perspective, &removed[perspective], &added[perspective]);
                    }
                }
            };

            if (pos.state()->prevState->accumulator.accumulationComputed) {
                const auto &prevMI = pos.state()->moveInfo;
                if (prevMI.pieceCount == 0) return;
                collectOne(prevMI);
            }
            else {
                const auto &prevMI1 = pos.state()->prevState->moveInfo;
                if (prevMI1.pieceCount == 0) {
                    const auto &prevMI2 = pos.state()->moveInfo;
                    if (prevMI2.pieceCount == 0) return;
                    collectOne(prevMI2);
                }
                else {
                    const auto &prevMI2 = pos.state()->moveInfo;
                    if (prevMI2.pieceCount == 0) {
                        collectOne(prevMI1);
                    }
                    else {
                        collectTwo(prevMI1, prevMI2);
                    }
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
        static constexpr uint32_t HashValue{ FeatureType::HashValue };
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
        static void collectChangedIndices(Position const &pos, MoveInfo const &mi, TriggerEvent const trigger, Color const perspective, IndexList *const removed, IndexList *const added) {
            if (FeatureType::RefreshTrigger == trigger) {
                FeatureType::appendChangedIndices(pos, mi, perspective, removed, added);
            }
        }

        // Make the base class and the class template that recursively uses itself a friend
        friend class FeatureSetBase<FeatureSet>;
        
        template<typename... FeatureTypes>
        friend class FeatureSet;
    };

}
