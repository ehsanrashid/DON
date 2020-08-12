// header used in NNUE evaluation function

#ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
#define NNUE_EVALUATE_NNUE_H_INCLUDED

#include "nnue_feature_transformer.h"

#include <memory>

namespace Evaluator::NNUE {

    // Hash value of evaluation function structure
    constexpr std::uint32_t kHashValue =
        FeatureTransformer::GetHashValue() ^ Network::GetHashValue();

    // Deleter for automating release of memory area
    template <typename T>
    struct AlignedDeleter {
        void operator()(T* ptr) const;
    };

    template <typename T>
    using AlignedPtr = std::unique_ptr<T, AlignedDeleter<T>>;

}  // namespace Evaluator::NNUE

#endif // #ifndef NNUE_EVALUATE_NNUE_H_INCLUDED
