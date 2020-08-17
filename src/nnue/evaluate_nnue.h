// Header used in NNUE evaluation function
#pragma once

#include <memory>

#include "nnue_feature_transformer.h"

namespace Evaluator::NNUE {

    // Hash value of evaluation function structure
    constexpr u32 HashValue{ FeatureTransformer::getHashValue() ^ Network::getHashValue() };

    // Deleter for automating release of memory area
    template<typename T>
    struct AlignedDeleter {
        void operator()(T*) const noexcept;
    };

    template<typename T>
    using AlignedPtr = std::unique_ptr<T, AlignedDeleter<T>>;

}  // namespace Evaluator::NNUE
