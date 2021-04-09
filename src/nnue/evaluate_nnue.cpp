#include "../position.h"
#include "../uci.h"
#include "../type.h"
#include "../helper/memoryhandler.h"

#include "evaluate_nnue.h"

#include <iostream>
#include <set>

namespace Evaluator::NNUE {

    template<typename T>
    inline void AlignedStdDeleter<T>::operator()(T *ptr) const noexcept {
        ptr->~T();
        freeAlignedStd(static_cast<void*>(ptr));
    }
    template<typename T>
    inline void AlignedLPDeleter<T>::operator()(T *ptr) const noexcept {
        ptr->~T();
        freeAlignedLP(static_cast<void*>(ptr));
    }

    /// Initialize the aligned pointer
    template<typename T>
    void initializeAllocator(AlignedStdPtr<T> &pointer) noexcept {
        pointer.reset(reinterpret_cast<T*>(allocAlignedStd(alignof (T), sizeof(T))));
        std::memset(pointer.get(), 0, sizeof(T));
    }
    template<typename T>
    void initializeAllocator(AlignedLargePagePtr<T> &pointer) noexcept {
        static_assert(alignof(T) <= 4096, "aligned_large_pages_alloc() may fail for such a big alignment requirement of T");
        pointer.reset(reinterpret_cast<T*>(allocAlignedLP(sizeof(T))));
        std::memset(pointer.get(), 0, sizeof(T));
    }

    namespace {
        // Input feature converter
        AlignedLargePagePtr<FeatureTransformer> featureTransformer;

        // Evaluation function
        AlignedStdPtr<Network> network;

        /// Initialize the evaluation function parameters
        void initializeParameters() {
            initializeAllocator(featureTransformer);
            initializeAllocator(network);
        }

        /// Read network header
        bool readHeader(std::istream &istream, uint32_t *hashValue, std::string *architecture) {
            uint32_t const version{ readLittleEndian<uint32_t>(istream) };
            *hashValue = readLittleEndian<uint32_t>(istream);
            uint32_t const size{ readLittleEndian<uint32_t>(istream) };

            if (!istream
             || version != Version) {
                return false;
            }
            architecture->resize(size);
            istream.read(&(*architecture)[0], size);
            return !istream.fail();
        }

        /// Read evaluation function parameters
        template<typename T>
        bool readParameters(std::istream &istream, T &reference) {
            uint32_t const header{ readLittleEndian<uint32_t>(istream) };

            if (!istream
             || header != T::getHashValue()) {
                return false;
            }
            return reference.readParameters(istream);
        }

        // Read network parameters
        bool readParameters(std::istream &istream) {
            uint32_t hashValue;
            std::string architecture;
            if (!readHeader(istream, &hashValue, &architecture)
             || hashValue != HashValue
             || !readParameters(istream, *featureTransformer)
             || !readParameters(istream, *network)) {
                return false;
            }
            return istream
                && istream.peek() == std::ios::traits_type::eof();
        }
    }

    // Load the evaluation function file
    bool loadEvalFile(std::istream &istream) {
        initializeParameters();
        return readParameters(istream);
    }

    // Evaluation function. Perform differential calculation.
    Value evaluate(Position const &pos) {
        // We manually align the arrays on the stack because with gcc < 9.3
        // overaligning stack variables with alignas() doesn't work correctly.

        constexpr uint64_t alignment = CacheLineSize;

#if defined(ALIGNAS_ON_STACK_VARIABLES_BROKEN)
        TransformedFeatureType transformedFeaturesUnaligned[FeatureTransformer::BufferSize + alignment / sizeof(TransformedFeatureType)];
        char bufferUnaligned[Network::BufferSize + alignment];

        auto *transformedFeatures{ alignUpPtr<alignment>(&transformedFeaturesUnaligned[0]) };
        auto *buffer{ alignUpPtr<alignment>(&bufferUnaligned[0]) };
#else
        alignas(alignment) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
        alignas(alignment) char buffer[Network::BufferSize];
#endif

        ASSERT_ALIGNED(transformedFeatures, alignment);
        ASSERT_ALIGNED(buffer, alignment);

        featureTransformer->transform(pos, transformedFeatures);
        auto const output{ network->propagate(transformedFeatures, buffer) };

        return static_cast<Value>(output[0] / FVScale);
    }

}
