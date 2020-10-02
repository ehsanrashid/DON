#include "../position.h"
#include "evaluate_nnue.h"

#include <iostream>
#include <set>

#include "../helper/memoryhandler.h"
#include "../uci.h"

namespace Evaluator::NNUE {

    const PieceSquare PP_BoardIndex[PIECES][COLORS] = {
        // convention: W - us, B - them
        // viewed from other side, W and B are reversed
        { PS_NONE,     PS_NONE     },
        { PS_W_PAWN,   PS_B_PAWN   },
        { PS_W_KNIGHT, PS_B_KNIGHT },
        { PS_W_BISHOP, PS_B_BISHOP },
        { PS_W_ROOK,   PS_B_ROOK   },
        { PS_W_QUEEN,  PS_B_QUEEN  },
        { PS_W_KING,   PS_B_KING   },
        { PS_NONE,     PS_NONE     },
        { PS_NONE,     PS_NONE     },
        { PS_B_PAWN,   PS_W_PAWN   },
        { PS_B_KNIGHT, PS_W_KNIGHT },
        { PS_B_BISHOP, PS_W_BISHOP },
        { PS_B_ROOK,   PS_W_ROOK   },
        { PS_B_QUEEN,  PS_W_QUEEN  },
        { PS_B_KING,   PS_W_KING   },
        { PS_NONE,     PS_NONE     }
    };

    template<typename T>
    inline void AlignedDeleter<T>::operator()(T *ptr) const noexcept {
        ptr->~T();
        freeAlignedStd(static_cast<void*>(ptr));
    }
    template<typename T>
    inline void AlignedLargePageDeleter<T>::operator()(T *ptr) const noexcept {
        ptr->~T();
        freeAlignedLargePages(static_cast<void*>(ptr));
    }

    /// Initialize the aligned pointer
    template<typename T>
    void alignedAllocator(AlignedPtr<T> &pointer) noexcept {
        pointer.reset(reinterpret_cast<T*>(allocAlignedStd(alignof (T), sizeof (T))));
        std::memset(pointer.get(), 0, sizeof (T));
    }
    template<typename T>
    void alignedLargePageAllocator(AlignedLargePagePtr<T> &pointer) noexcept {
        static_assert (alignof(T) <= 4096, "aligned_large_pages_alloc() may fail for such a big alignment requirement of T");
        pointer.reset(reinterpret_cast<T*>(allocAlignedLargePages(sizeof (T))));
        std::memset(pointer.get(), 0, sizeof (T));
    }

    namespace {
        // Input feature converter
        AlignedLargePagePtr<FeatureTransformer> featureTransformer;

        // Evaluation function
        AlignedPtr<Network> network;

        /// Initialize the evaluation function parameters
        void initializeParameters() {
            alignedLargePageAllocator(featureTransformer);
            alignedAllocator(network);
        }

        /// Read network header
        bool readHeader(std::istream &istream, u32 *hashValue, std::string *architecture) {
            u32 const version{ readLittleEndian<u32>(istream) };
            *hashValue = readLittleEndian<u32>(istream);
            u32 const size{ readLittleEndian<u32>(istream) };

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
            u32 const header{ readLittleEndian<u32>(istream) };
            return !istream
                || header != T::getHashValue() ?
                false :
                reference.readParameters(istream);
        }

        // Read network parameters
        bool readParameters(std::istream &istream) {
            u32 hashValue;
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

        alignas(CacheLineSize) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
        featureTransformer->transform(pos, transformedFeatures);
        alignas(CacheLineSize) char buffer[Network::BufferSize];
        auto const output{ network->propagate(transformedFeatures, buffer) };

        return static_cast<Value>(output[0] / FVScale);
    }

}
