// A class that converts the input features of the NNUE evaluation function
#pragma once

#include "nnue_common.h"
#include "architecture.h"
#include "features/index_list.h"

namespace Evaluator::NNUE {

    // If vector instructions are enabled, we update and refresh the
    // accumulator tile by tile such that each tile fits in the CPU's vector registers.
    #define TILING

    #ifdef USE_AVX512
        using vec_t = __m512i;
        #define vec_load(a)     _mm512_loadA_si512(a)
        #define vec_store(a,b)  _mm512_storeA_si512(a,b)
        #define vec_add_16(a,b) _mm512_add_epi16(a,b)
        #define vec_sub_16(a,b) _mm512_sub_epi16(a,b)
        static constexpr IndexType NumRegs = 8; // only 8 are needed

    #elif USE_AVX2
        using vec_t = __m256i;
        #define vec_load(a)     _mm256_loadA_si256(a)
        #define vec_store(a,b)  _mm256_storeA_si256(a,b)
        #define vec_add_16(a,b) _mm256_add_epi16(a,b)
        #define vec_sub_16(a,b) _mm256_sub_epi16(a,b)
        static constexpr IndexType NumRegs = 16;

    #elif USE_SSE2
        using vec_t = __m128i;
        #define vec_load(a)     (*(a))
        #define vec_store(a,b)  *(a)=(b)
        #define vec_add_16(a,b) _mm_add_epi16(a,b)
        #define vec_sub_16(a,b) _mm_sub_epi16(a,b)
        static constexpr IndexType NumRegs =
        #if defined(IS_64BIT)
            16;
        #else
            8;
        #endif  
    #elif USE_MMX
        using vec_t = __m64;
        #define vec_load(a)     (*(a))
        #define vec_store(a,b)  *(a)=(b)
        #define vec_add_16(a,b) _mm_add_pi16(a,b)
        #define vec_sub_16(a,b) _mm_sub_pi16(a,b)
        static constexpr IndexType NumRegs = 8;

    #elif USE_NEON
        using vec_t = int16x8_t;
        #define vec_load(a)     (*(a))
        #define vec_store(a,b)  *(a)=(b)
        #define vec_add_16(a,b) vaddq_s16(a,b)
        #define vec_sub_16(a,b) vsubq_s16(a,b)
        static constexpr IndexType NumRegs = 16;

    #else

        #undef TILING

    #endif

    // Input feature converter
    class FeatureTransformer {

    private:
        // Number of output dimensions for one side
        static constexpr IndexType HalfDimensions{ TransformedFeatureDimensions };

    #if defined(TILING)
        static constexpr IndexType TileHeight = NumRegs * sizeof (vec_t) / 2;
        static_assert (HalfDimensions % TileHeight == 0, "TileHeight must divide HalfDimensions");
    #endif

    public:
        // Output type
        using OutputType = TransformedFeatureType;

        // Number of input/output dimensions
        static constexpr IndexType InputDimensions{ RawFeatures::Dimensions };
        static constexpr IndexType OutputDimensions{ HalfDimensions * 2 };

        // Size of forward propagation buffer
        static constexpr size_t BufferSize{ OutputDimensions * sizeof (OutputType) };

        // Hash value embedded in the evaluation file
        static constexpr uint32_t getHashValue() {
            return RawFeatures::HashValue ^ OutputDimensions;
        }

        // Read network parameters
        bool readParameters(std::istream &istream) {
            for (size_t i = 0; i < HalfDimensions; ++i) {
                _biases[i] = readLittleEndian<BiasType>(istream);
            }
            for (size_t i = 0; i < HalfDimensions * InputDimensions; ++i) {
                _weights[i] = readLittleEndian<WeightType>(istream);
            }
            return !istream.fail();
        }

        // Proceed with the difference calculation if possible
        bool updateAccumulatorIfPossible(Position const &pos) const {
            auto const currState{ pos.state() };
            if (currState->accumulator.accumulationComputed) {
                return true;
            }
            auto const prevState{ currState->prevState };
            if (prevState != nullptr) {
                if (prevState->accumulator.accumulationComputed) {
                    updateAccumulator(pos);
                    return true;
                }
                else
                if (prevState->prevState != nullptr) {
                    if (prevState->prevState->accumulator.accumulationComputed) {
                        updateAccumulator(pos);
                        return true;
                    }
                    
                }
            }
            return false;
        }

        // Convert input features
        void transform(Position const &pos, OutputType *output) const {
            if (!updateAccumulatorIfPossible(pos)) {
                refreshAccumulator(pos);
            }
            auto const &accumulation = pos.state()->accumulator.accumulation;

        #if defined(USE_AVX2)
            constexpr IndexType NumChunks{ HalfDimensions / SimdWidth };
            constexpr int kControl{ 0b11011000 };
            __m256i const kZero{ _mm256_setzero_si256() };
        #elif defined(USE_SSE2)
            constexpr IndexType NumChunks{ HalfDimensions / SimdWidth };
            #if defined(USE_SSE41)
            __m128i const kZero{ _mm_setzero_si128() };
            #else
            __m128i const k0x80s{ _mm_set1_epi8(-128) };
            #endif
        #elif defined(USE_MMX)
            constexpr IndexType NumChunks{ HalfDimensions / SimdWidth };
            __m64 const k0x80s{ _mm_set1_pi8(-128) };
        #elif defined(USE_NEON)
            constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
            int8x8_t const kZero{ 0 };
        #endif

            Color const perspectives[2]{ pos.activeSide(), ~pos.activeSide() };
            for (IndexType p = 0; p < 2; ++p) {
                IndexType const offset{ HalfDimensions * p };

        #if defined(USE_AVX2)
                auto out{ reinterpret_cast<__m256i*>(&output[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m256i sum0{ _mm256_loadA_si256(&reinterpret_cast<__m256i const*>(accumulation[perspectives[p]][0])[j * 2 + 0]) };
                    __m256i sum1{ _mm256_loadA_si256(&reinterpret_cast<__m256i const*>(accumulation[perspectives[p]][0])[j * 2 + 1]) };
                    _mm256_storeA_si256(&out[j], _mm256_permute4x64_epi64(_mm256_max_epi8(_mm256_packs_epi16(sum0, sum1), kZero), kControl));
                }

        #elif defined(USE_SSE2)
                auto out{ reinterpret_cast<__m128i*>(&output[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m128i sum0{ _mm_load_si128(&reinterpret_cast<__m128i const*>(accumulation[perspectives[p]][0])[j * 2 + 0]) };
                    __m128i sum1{ _mm_load_si128(&reinterpret_cast<__m128i const*>(accumulation[perspectives[p]][0])[j * 2 + 1]) };
                    __m128i const packedbytes{ _mm_packs_epi16(sum0, sum1) };

                    _mm_store_si128(&out[j],

            #if defined(USE_SSE41)
                        _mm_max_epi8(packedbytes, kZero)
            #else
                        _mm_subs_epi8(_mm_adds_epi8(packedbytes, k0x80s), k0x80s)
            #endif

                    );
                }

        #elif defined(USE_MMX)
                auto out{ reinterpret_cast<__m64*>(&output[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m64 sum0{ *(&reinterpret_cast<__m64 const*>(accumulation[perspectives[p]][0])[j * 2 + 0]) };
                    __m64 sum1{ *(&reinterpret_cast<__m64 const*>(accumulation[perspectives[p]][0])[j * 2 + 1]) };
                    __m64 const packedbytes{ _mm_packs_pi16(sum0, sum1) };
                    out[j] = _mm_subs_pi8(_mm_adds_pi8(packedbytes, k0x80s), k0x80s);
                }

        #elif defined(USE_NEON)
                auto const out{ reinterpret_cast<int8x8_t*>(&output[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    int16x8_t sum{ reinterpret_cast<int16x8_t const*>(accumulation[perspectives[p]][0])[j] };
                    out[j] = vmax_s8(vqmovn_s16(sum), kZero);
                }

        #else
                for (IndexType j = 0; j < HalfDimensions; ++j) {
                    BiasType sum{ accumulation[static_cast<int>(perspectives[p])][0][j] };
                    output[offset + j] = static_cast<OutputType>(std::max<int>(0, std::min<int>(127, sum)));
                }
        #endif

            }
        #if defined(USE_MMX)
            _mm_empty();
        #endif
        }

    private:
        // Calculate cumulative value without using difference calculation
        void refreshAccumulator(Position const &pos) const {
            auto &accumulator{ pos.state()->accumulator };
            IndexType i{ 0 };
            Features::IndexList activeIndices[2];
            RawFeatures::appendActiveIndices(pos, RefreshTriggers[i], activeIndices);
            for (Color perspective : { WHITE, BLACK }) {

        #if defined(TILING)
                for (unsigned j = 0; j < HalfDimensions / TileHeight; ++j) {

                    auto biasesTile = reinterpret_cast<vec_t const*>(&_biases[j * TileHeight]);
                    auto accTile = reinterpret_cast<vec_t*>(&accumulator.accumulation[perspective][i][j * TileHeight]);
                    vec_t acc[NumRegs];

                    for (unsigned k = 0; k < NumRegs; ++k) {
                        acc[k] = biasesTile[k];
                    }
                    for (auto const index : activeIndices[perspective]) {
                        const IndexType offset = HalfDimensions * index + j * TileHeight;
                        auto column = reinterpret_cast<vec_t const*>(&_weights[offset]);

                        for (unsigned k = 0; k < NumRegs; ++k) {
                            acc[k] = vec_add_16(acc[k], column[k]);
                        }
                    }

                    for (unsigned k = 0; k < NumRegs; ++k) {
                        vec_store(&accTile[k], acc[k]);
                    }
                }
        #else

                std::memcpy(accumulator.accumulation[perspective][i], _biases, HalfDimensions * sizeof (BiasType));
                for (auto const index : activeIndices[perspective]) {
                    IndexType const offset{ HalfDimensions * index };

                    for (IndexType j = 0; j < HalfDimensions; ++j) {
                        accumulator.accumulation[perspective][i][j] += _weights[offset + j];
                    }
                }
        #endif

            }
        #if defined(USE_MMX)
            _mm_empty();
        #endif

            accumulator.accumulationComputed = true;
        }

        // Calculate cumulative value using difference calculation
        void updateAccumulator(Position const &pos) const {

            Accumulator *prevAccumulator;
            assert(pos.state()->prevState != nullptr);
            if (pos.state()->prevState->accumulator.accumulationComputed) {
                prevAccumulator = &pos.state()->prevState->accumulator;
            }
            else {
                assert(pos.state()->prevState->prevState != nullptr
                    && pos.state()->prevState->prevState->accumulator.accumulationComputed);
                prevAccumulator = &pos.state()->prevState->prevState->accumulator;
            }

            
            auto &accumulator{ pos.state()->accumulator };
            IndexType i{ 0 };
            Features::IndexList removedIndices[2], addedIndices[2];
            bool reset[2]{false, false};
            RawFeatures::appendChangedIndices(pos, RefreshTriggers[i], removedIndices, addedIndices, reset);

        #if defined(TILING)
            for (IndexType j = 0; j < HalfDimensions / TileHeight; ++j) {
                for (Color perspective : { WHITE, BLACK }) {
                    auto accTile = reinterpret_cast<vec_t *>(&accumulator.accumulation[perspective][i][j * TileHeight]);
                    vec_t acc[NumRegs];

                    if (reset[perspective]) {
                        auto biasesTile = reinterpret_cast<vec_t const*>(&_biases[j * TileHeight]);
                        for (unsigned k = 0; k < NumRegs; ++k) {
                            acc[k] = biasesTile[k];
                        }
                    }
                    else {
                        auto prevAccTile = reinterpret_cast<vec_t const*>(&prevAccumulator->accumulation[perspective][i][j * TileHeight]);
                        for (IndexType k = 0; k < NumRegs; ++k) {
                            acc[k] = vec_load(&prevAccTile[k]);
                        }

                        // Difference calculation for the deactivated features
                        for (auto const index : removedIndices[perspective]) {
                            IndexType const offset = HalfDimensions * index + j * TileHeight;
                            auto column = reinterpret_cast<vec_t const*>(&_weights[offset]);

                            for (IndexType k = 0; k < NumRegs; ++k) {
                                acc[k] = vec_sub_16(acc[k], column[k]);
                            }
                        }
                    }
                    {   // Difference calculation for the activated features
                        for (auto const index : addedIndices[perspective]) {
                            IndexType const offset = HalfDimensions * index + j * TileHeight;
                            auto column = reinterpret_cast<vec_t const*>(&_weights[offset]);

                            for (IndexType k = 0; k < NumRegs; ++k) {
                                acc[k] = vec_add_16(acc[k], column[k]);
                            }
                        }
                    }

                    for (IndexType k = 0; k < NumRegs; ++k) {
                        vec_store(&accTile[k], acc[k]);
                    }
                }
            }
            #if defined(USE_MMX)
            _mm_empty();
            #endif

        #else

            for (Color perspective : { WHITE, BLACK }) {

                if (reset[perspective]) {
                    std::memcpy(accumulator.accumulation[perspective][i], _biases, HalfDimensions * sizeof (BiasType));
                }
                else {
                    std::memcpy(accumulator.accumulation[perspective][i], prevAccumulator->accumulation[perspective][i], HalfDimensions * sizeof (BiasType));
                    // Difference calculation for the deactivated features
                    for (auto const index : removedIndices[perspective]) {
                        IndexType const offset{ HalfDimensions * index };

                        for (IndexType j = 0; j < HalfDimensions; ++j) {
                            accumulator.accumulation[perspective][i][j] -= _weights[offset + j];
                        }
                    }
                }
                {   // Difference calculation for the activated features
                    for (auto const index : addedIndices[perspective]) {
                        IndexType const offset{ HalfDimensions * index };

                        for (IndexType j = 0; j < HalfDimensions; ++j) {
                            accumulator.accumulation[perspective][i][j] += _weights[offset + j];
                        }
                    }
                }
            }

        #endif

            accumulator.accumulationComputed = true;
        }

        using BiasType = int16_t;
        using WeightType = int16_t;

        alignas(CacheLineSize) BiasType _biases[HalfDimensions];
        alignas(CacheLineSize) WeightType _weights[HalfDimensions * InputDimensions];

    };

}
