// NNUE evaluation function layer InputSlice definition
#pragma once

#include "../nnue_common.h"

namespace Evaluator::NNUE::Layers {

    // Input layer
    template <IndexType OutputDimensions, IndexType Offset = 0>
    class InputSlice {
    public:
        // Need to maintain alignment
        static_assert(Offset %kMaxSimdWidth == 0, "");

        // Output type
        using OutputType = TransformedFeatureType;

        // Output dimensionality
        static constexpr IndexType kOutputDimensions = OutputDimensions;

        // Size of forward propagation buffer used from the input layer to this layer
        static constexpr size_t kBufferSize = 0;

        // Hash value embedded in the evaluation file
        static constexpr u32 GetHashValue() {
            u32 hash_value = 0xEC42E90Du;
            hash_value ^= kOutputDimensions ^ (Offset << 10);
            return hash_value;
        }

        // Read network parameters
        bool readParameters(std::istream & /*stream*/) {
            return true;
        }

        // Forward propagation
        const OutputType *Propagate(
            const TransformedFeatureType *transformed_features,
            char * /*buffer*/) const {
            return transformed_features + Offset;
        }

    private:
    };

}  // namespace Layers
