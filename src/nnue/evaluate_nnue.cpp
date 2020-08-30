// Code for calculating NNUE evaluation function

#include <iostream>
#include <set>

#include "../Position.h"
#include "../UCI.h"

#include "evaluate_nnue.h"

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
            return posix_memalign(&mem, alignment, size) == 0 ?
                    mem :
                    nullptr;
#elif defined(_WIN32)
            return _mm_malloc(size, alignment);
#else
            return std::aligned_alloc(alignment, size);
#endif
        }

        void freeAligned(void *mem) noexcept {

#if defined(POSIX_ALIGNED_MEM)
            free(mem);
#elif defined(_WIN32)
            _mm_free(mem);
#else
            free(mem);
#endif
        }

    }

    template<typename T>
    inline void AlignedDeleter<T>::operator()(T *ptr) const noexcept {
        ptr->~T();
        freeAligned(ptr);
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
        bool readHeader(std::istream &is, u32 *hashValue, std::string *architecture) {
            u32 const version{ readLittleEndian<u32>(is) };
            *hashValue = readLittleEndian<u32>(is);
            u32 const size{ readLittleEndian<u32>(is) };

            if (!is
             || version != Version) {
                return false;
            }
            architecture->resize(size);
            is.read(&(*architecture)[0], size);
            return !is.fail();
        }

        /// Read evaluation function parameters
        template<typename T>
        bool readParameters(std::istream &is, AlignedPtr<T> const &pointer) {
            u32 const header{ readLittleEndian<u32>(is) };
            return !is
                || header != T::getHashValue() ?
                false :
                pointer->readParameters(is);
        }

        // Read network parameters
        bool readParameters(std::istream &is) {
            u32 hashValue;
            std::string architecture;
            if (!readHeader(is, &hashValue, &architecture)
             || hashValue != HashValue
             || !readParameters(is, featureTransformer)
             || !readParameters(is, network)) {
                return false;
            }
            return is
                && is.peek() == std::ios::traits_type::eof();
        }

        // Calculate the evaluation value
        Value computeScore(Position const &pos, bool refresh) {
            auto &accumulator{ pos.state()->accumulator };
            if (refresh
             || !accumulator.scoreComputed) {

                alignas(CacheLineSize) TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
                featureTransformer->transform(pos, transformedFeatures, refresh);
                alignas(CacheLineSize) char buffer[Network::BufferSize];
                auto const output{ network->propagate(transformedFeatures, buffer) };

                auto const score{ static_cast<Value>(output[0] / FVScale) };

                accumulator.score = score;
                accumulator.scoreComputed = true;
            }
            return accumulator.score;
        }
    }

    // Load the evaluation function file
    bool loadEvalFile(std::istream &stream) {
        initialize();
        return readParameters(stream);
    }

    // Evaluation function. Perform differential calculation.
    Value evaluate(Position const &pos) {
        auto v{ computeScore(pos, false) };
        v = v * 5 / 4;
        v += VALUE_TEMPO;
        return v;
    }

    //// Evaluation function. Perform full calculation.
    //Value computeEval(Position const &pos) {
    //    return computeScore(pos, true);
    //}

    //// Proceed with the difference calculation if possible
    //void updateEval(Position const &pos) {
    //    featureTransformer->updateAccumulatorIfPossible(pos);
    //}

} // namespace Evaluator::NNUE
