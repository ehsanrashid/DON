// Code for calculating NNUE evaluation function

#include <iostream>
#include <set>

#include "../Position.h"
#include "../UCI.h"

#include "EvaluateNNUE.h"

#if defined(__APPLE__) || defined(__ANDROID__) || defined(__OpenBSD__) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32))
    #define POSIX_ALIGNED_MEM
    #include <cstdlib>
#endif

namespace Evaluator::NNUE {

    PieceSquare PP_BoardIndex[PIECES][COLORS] = {
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

    namespace {

        /// allocAligned() is our wrapper for systems where the c++17 implementation
        /// does not guarantee the availability of aligned_alloc().
        /// Memory allocated with allocAligned() must be freed with freeAligned().

        void* allocAligned(size_t alignment, size_t size) noexcept {

        #if defined(POSIX_ALIGNED_MEM)
            void *mem;
            return posix_memalign(&mem, alignment, size) == 0 ? mem : nullptr;
        #elif defined(_WIN32)
            return _mm_malloc(size, alignment);
        #else
            return std::aligned_alloc(alignment, size);
        #endif
        }

        void freeAligned(void *mem) noexcept {

            if (mem != nullptr) {
        #if defined(POSIX_ALIGNED_MEM)
                free(mem);
        #elif defined(_WIN32)
                _mm_free(mem);
        #else
                free(mem);
        #endif
                //mem = nullptr; // need (void *&mem)
            }
        }

    }

    template<typename T>
    inline void AlignedDeleter<T>::operator()(T *ptr) const noexcept {
        ptr->~T();
        freeAligned(static_cast<void*>(ptr));
    }

    /// Initialize the aligned pointer
    template<typename T>
    void alignedAllocator(AlignedPtr<T> &pointer) noexcept {
        pointer.reset(reinterpret_cast<T*>(allocAligned(alignof (T), sizeof (T))));
        std::memset(pointer.get(), 0, sizeof (T));
    }

    namespace {
        // Input feature converter
        AlignedPtr<FeatureTransformer> featureTransformer;

        // Evaluation function
        AlignedPtr<Network> network;

        /// Initialize the evaluation function parameters
        void initialize() {
            alignedAllocator(featureTransformer);
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
        bool readParameters(std::istream &istream, AlignedPtr<T> const &pointer) {
            u32 const header{ readLittleEndian<u32>(istream) };
            return !istream
                || header != T::getHashValue() ?
                false :
                pointer->readParameters(istream);
        }

        // Read network parameters
        bool readParameters(std::istream &istream) {
            u32 hashValue;
            std::string architecture;
            if (!readHeader(istream, &hashValue, &architecture)
             || hashValue != HashValue
             || !readParameters(istream, featureTransformer)
             || !readParameters(istream, network)) {
                return false;
            }
            return istream
                && istream.peek() == std::ios::traits_type::eof();
        }

        // Calculate the evaluation value
        Value computeScore(Position const &pos) {

            alignas(CacheLineSize) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
            featureTransformer->transform(pos, transformedFeatures);
            alignas(CacheLineSize) char buffer[Network::BufferSize];
            auto const output{ network->propagate(transformedFeatures, buffer) };

            return{ static_cast<Value>(output[0] / FVScale) };
        }
    }

    // Load the evaluation function file
    bool loadEvalFile(std::istream &istream) {
        initialize();
        return readParameters(istream);
    }

    // Evaluation function. Perform differential calculation.
    Value evaluate(Position const &pos) {
        auto v{ computeScore(pos) };
        v = v * 5 / 4;
        v += VALUE_TEMPO;
        return v;
    }

} // namespace Evaluator::NNUE
