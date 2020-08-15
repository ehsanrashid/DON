// NNUE evaluation function layer InputSlice definition
#pragma once

#include "../nnue_common.h"

namespace Evaluator::NNUE::Layers {

    // Input layer
    template<IndexType OutputDimensions, IndexType Offset = 0>
    class InputSlice {
    private:

    public:
        // Need to maintain alignment
        static_assert(Offset %kMaxSimdWidth == 0, "");

        // Output type
        using OutputType = TransformedFeatureType;

        // Output dimensionality
        static constexpr IndexType kOutputDimensions{ OutputDimensions };

        // Size of forward propagation buffer used from the input layer to this layer
        static constexpr size_t kBufferSize{ 0 };

        // Hash value embedded in the evaluation file
        static constexpr u32 getHashValue() {
            u32 hashValue{ 0xEC42E90Du };
            hashValue ^= kOutputDimensions ^ (Offset << 10);
            return hashValue;
        }

        // Read network parameters
        bool readParameters(std::istream&) {
            return true;
        }

        // Forward propagation
        OutputType const* propagate(const TransformedFeatureType *transformedFeatures, char*) const {
            return transformedFeatures + Offset;
        }
    };

}  // namespace Layers
