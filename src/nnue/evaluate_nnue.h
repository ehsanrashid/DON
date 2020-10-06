#pragma once

#include <memory>

#include "../type.h"
#include "feature_transformer.h"

namespace Evaluator::NNUE {

    // Hash value of evaluation function structure
    constexpr uint32_t HashValue{ FeatureTransformer::getHashValue() ^ Network::getHashValue() };

    // Deleter for automating release of memory area
    template<typename T>
    struct AlignedStdDeleter {
        void operator()(T *) const noexcept;
    };
    template<typename T>
    struct AlignedLargePageDeleter {
        void operator()(T *) const noexcept;
    };

    template<typename T>
    using AlignedStdPtr         = std::unique_ptr<T, AlignedStdDeleter<T>>;
    template<typename T>
    using AlignedLargePagePtr   = std::unique_ptr<T, AlignedLargePageDeleter<T>>;

    template<typename T>
    extern void alignedStdAllocator(AlignedStdPtr<T>&) noexcept;
    template<typename T>
    extern void alignedLargePageAllocator(AlignedLargePagePtr<T> &) noexcept;

}
