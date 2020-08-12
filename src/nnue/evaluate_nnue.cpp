// Code for calculating NNUE evaluation function

#include <fstream>
#include <iostream>
#include <set>

#include "../Evaluator.h"
#include "../Position.h"
#include "../UCI.h"

#include "evaluate_nnue.h"

ExtPieceSquare kpp_board_index[PIECES] = {
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


namespace Evaluator::NNUE {

    namespace {

        /// Wrappers for systems where the c++17 implementation doesn't guarantee the availability of aligned_alloc.
        /// Memory allocated with stdAlignedAlloc must be freed with stdAlignedFree.

        void* stdAlignedAlloc(size_t alignment, size_t size) {

#if (defined(__APPLE__) && defined(_LIBCPP_HAS_C11_FEATURES)) || defined(__ANDROID__) || defined(__OpenBSD__) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32))
            return aligned_alloc(alignment, size);
#elif (defined(_WIN32) || (defined(__APPLE__) && !defined(_LIBCPP_HAS_C11_FEATURES)))
            return _mm_malloc(size, alignment);
#else
            return std::aligned_alloc(alignment, size);
#endif
        }

        void stdAlignedFree(void *ptr) {

#if (defined(__APPLE__) && defined(_LIBCPP_HAS_C11_FEATURES)) || defined(__ANDROID__) || defined(__OpenBSD__) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32))
            free(ptr);
#elif (defined(_WIN32) || (defined(__APPLE__) && !defined(_LIBCPP_HAS_C11_FEATURES)))
            _mm_free(ptr);
#else
            free(ptr);
#endif
        }

    }


    template<typename T>
    inline void AlignedDeleter<T>::operator()(T *ptr) const {
        ptr->~T();
        stdAlignedFree(ptr);
    }

    // Input feature converter
    AlignedPtr<FeatureTransformer> featureTransformer;

    // Evaluation function
    AlignedPtr<Network> network;

    // Evaluation function file name
    std::string fileName;

    namespace Detail {

        // Initialize the evaluation function parameters
        template <typename T>
        void Initialize(AlignedPtr<T> &pointer) {

            pointer.reset(reinterpret_cast<T*>(stdAlignedAlloc(alignof(T), sizeof(T))));
            std::memset(pointer.get(), 0, sizeof(T));
        }

        // Read evaluation function parameters
        template <typename T>
        bool ReadParameters(std::istream &stream, const AlignedPtr<T> &pointer) {

            u32 header;
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header != T::GetHashValue()) return false;
            return pointer->ReadParameters(stream);
        }

    }  // namespace Detail

    // Initialize the evaluation function parameters
    void Initialize() {

        Detail::Initialize(featureTransformer);
        Detail::Initialize(network);
    }

    // Read network header
    bool ReadHeader(std::istream &stream, u32 *hash_value, std::string *architecture) {

        u32 version, size;
        stream.read(reinterpret_cast<char*>(&version), sizeof(version));
        stream.read(reinterpret_cast<char*>(hash_value), sizeof(*hash_value));
        stream.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (!stream || version != kVersion) return false;
        architecture->resize(size);
        stream.read(&(*architecture)[0], size);
        return !stream.fail();
    }

    // Read network parameters
    bool ReadParameters(std::istream &stream) {

        u32 hash_value;
        std::string architecture;
        if (!ReadHeader(stream, &hash_value, &architecture)) return false;
        if (hash_value != kHashValue) return false;
        if (!Detail::ReadParameters(stream, featureTransformer)) return false;
        if (!Detail::ReadParameters(stream, network)) return false;
        return stream && stream.peek() == std::ios::traits_type::eof();
    }

    // Proceed with the difference calculation if possible
    static void UpdateAccumulatorIfPossible(Position const &pos) {

        featureTransformer->UpdateAccumulatorIfPossible(pos);
    }

    // Calculate the evaluation value
    static Value ComputeScore(Position const &pos, bool refresh) {

        auto& accumulator = pos.state()->accumulator;
        if (!refresh
         && accumulator.computedScore) {
            return accumulator.score;
        }

        alignas(kCacheLineSize) TransformedFeatureType
            transformed_features[FeatureTransformer::kBufferSize];
        featureTransformer->Transform(pos, transformed_features, refresh);
        alignas(kCacheLineSize) char buffer[Network::kBufferSize];
        const auto output = network->Propagate(transformed_features, buffer);

        auto score = static_cast<Value>(output[0] / FV_SCALE);

        accumulator.score = score;
        accumulator.computedScore = true;
        return accumulator.score;
    }

    // Load the evaluation function file
    bool loadEvalFile(std::string const &evalFile) {

        Initialize();
        fileName = evalFile;

        std::ifstream stream(evalFile, std::ios::binary);
        return ReadParameters(stream);
    }

    // Evaluation function. Perform differential calculation.
    Value evaluate(Position const &pos) {
        Value v{ ComputeScore(pos, false) };
        return clamp(v, -VALUE_MATE_2_MAX_PLY + 1, VALUE_MATE_2_MAX_PLY - 1);
    }

    // Evaluation function. Perform full calculation.
    Value computeEval(Position const &pos) {
        return ComputeScore(pos, true);
    }

    // Proceed with the difference calculation if possible
    void updateEval(Position const &pos) {
        UpdateAccumulatorIfPossible(pos);
    }

} // namespace Evaluator::NNUE