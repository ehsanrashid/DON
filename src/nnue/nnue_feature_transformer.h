// A class that converts the input features of the NNUE evaluation function
#pragma once

#include "nnue_common.h"
#include "nnue_architecture.h"
#include "features/index_list.h"

namespace Evaluator::NNUE {

    // Input feature converter
    class FeatureTransformer {

    private:
        // Number of output dimensions for one side
        static constexpr IndexType HalfDimensions{ TransformedFeatureDimensions };

    public:
        // Number of input/output dimensions
        static constexpr IndexType InputDimensions{ RawFeatures::Dimensions };
        static constexpr IndexType OutputDimensions{ HalfDimensions * 2 };

    private:
        using BiasType = i16;
        using WeightType = i16;

        alignas(CacheLineSize) BiasType _biases[HalfDimensions];
        alignas(CacheLineSize) WeightType _weights[HalfDimensions * InputDimensions];

    public:
        // Output type
        using OutputType = TransformedFeatureType;
        // Size of forward propagation buffer
        static constexpr size_t BufferSize{ OutputDimensions * sizeof (OutputType) };

        // Hash value embedded in the evaluation file
        static constexpr u32 getHashValue() {
            return RawFeatures::HashValue ^ OutputDimensions;
        }

        // Read network parameters
        bool readParameters(std::istream &is) {
            for (size_t i = 0; i < HalfDimensions; ++i) {
                _biases[i] = readLittleEndian<BiasType>(is);
            }
            for (size_t i = 0; i < HalfDimensions * InputDimensions; ++i) {
                _weights[i] = readLittleEndian<WeightType>(is);
            }
            return !is.fail();
        }

        // Proceed with the difference calculation if possible
        bool updateAccumulatorIfPossible(Position const &pos) const {
            auto const currState{ pos.state() };
            if (currState->accumulator.accumulationComputed) {
                return true;
            }
            auto const prevState{ currState->prevState };
            if (prevState != nullptr
             && prevState->accumulator.accumulationComputed) {
                updateAccumulator(pos);
                return true;
            }
            return false;
        }

        // Convert input features
        void transform(Position const &pos, OutputType *output, bool refresh) const {
            if (refresh
             || !updateAccumulatorIfPossible(pos)) {
                refreshAccumulator(pos);
            }
            auto const &accumulation{ pos.state()->accumulator.accumulation };

#if defined(AVX2)
            constexpr IndexType NumChunks{ HalfDimensions / SimdWidth };
            constexpr int kControl{ 0b11011000 };
            __m256i const kZero{ _mm256_setzero_si256() };

#elif defined(SSE2)
            constexpr IndexType NumChunks{ HalfDimensions / SimdWidth };

#ifdef SSE41
            __m128i const kZero{ _mm_setzero_si128() };
#else
            __m128i const k0x80s{ _mm_set1_epi8(-128) };
#endif

#elif defined(MMX)
            constexpr IndexType NumChunks{ HalfDimensions / SimdWidth };
            __m64 const k0x80s{ _mm_set1_pi8(-128) };

#elif defined(NEON)
            constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
            int8x8_t const kZero{ 0 };
#endif

            Color const perspectives[2]{ pos.activeSide(), ~pos.activeSide() };
            for (IndexType p = 0; p < 2; ++p) {
                IndexType const offset{ HalfDimensions * p };

#if defined(AVX2)
                auto out{ reinterpret_cast<__m256i*>(&output[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m256i sum0{ _mm256_loadA_si256(&reinterpret_cast<__m256i const*>(accumulation[perspectives[p]][0])[j * 2 + 0]) };
                    __m256i sum1{ _mm256_loadA_si256(&reinterpret_cast<__m256i const*>(accumulation[perspectives[p]][0])[j * 2 + 1]) };
                    _mm256_storeA_si256(&out[j], _mm256_permute4x64_epi64(_mm256_max_epi8(_mm256_packs_epi16(sum0, sum1), kZero), kControl));
                }

#elif defined(SSE2)
                auto out{ reinterpret_cast<__m128i*>(&output[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m128i sum0{ _mm_load_si128(&reinterpret_cast<__m128i const*>(accumulation[perspectives[p]][0])[j * 2 + 0]) };
                    __m128i sum1{ _mm_load_si128(&reinterpret_cast<__m128i const*>(accumulation[perspectives[p]][0])[j * 2 + 1]) };
                    __m128i const packedbytes{ _mm_packs_epi16(sum0, sum1) };

                    _mm_store_si128(&out[j],

#ifdef SSE41
                        _mm_max_epi8(packedbytes, kZero)
#else
                        _mm_subs_epi8(_mm_adds_epi8(packedbytes, k0x80s), k0x80s)
#endif

                    );
                }

#elif defined(MMX)
                auto out{ reinterpret_cast<__m64*>(&output[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m64 sum0{ *(&reinterpret_cast<__m64 const*>(accumulation[perspectives[p]][0])[j * 2 + 0]) };
                    __m64 sum1{ *(&reinterpret_cast<__m64 const*>(accumulation[perspectives[p]][0])[j * 2 + 1]) };
                    __m64 const packedbytes{ _mm_packs_pi16(sum0, sum1) };
                    out[j] = _mm_subs_pi8(_mm_adds_pi8(packedbytes, k0x80s), k0x80s);
                }

#elif defined(NEON)
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
#if defined(MMX)
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
                std::memcpy(accumulator.accumulation[perspective][i], _biases, HalfDimensions * sizeof (BiasType));
                for (auto const index : activeIndices[perspective]) {
                    IndexType const offset{ HalfDimensions * index };

#if defined(AVX512)
                    auto accumulation{ reinterpret_cast<__m512i*>(&accumulator.accumulation[perspective][i][0]) };
                    auto column{ reinterpret_cast<__m512i const*>(&_weights[offset]) };
                    constexpr IndexType NumChunks{ HalfDimensions / SimdWidth };
                    for (IndexType j = 0; j < NumChunks; ++j) {
                        _mm512_storeA_si512(&accumulation[j], _mm512_add_epi16(_mm512_loadA_si512(&accumulation[j]), column[j]));
                    }
#elif defined(AVX2)
                    auto accumulation{ reinterpret_cast<__m256i*>(&accumulator.accumulation[perspective][i][0]) };
                    auto column{ reinterpret_cast<__m256i const*>(&_weights[offset]) };
                    constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
                    for (IndexType j = 0; j < NumChunks; ++j) {
                        _mm256_storeA_si256(&accumulation[j], _mm256_add_epi16(_mm256_loadA_si256(&accumulation[j]), column[j]));
                    }
#elif defined(SSE2)
                    auto accumulation{ reinterpret_cast<__m128i*>(&accumulator.accumulation[perspective][i][0]) };
                    auto column{ reinterpret_cast<__m128i const*>(&_weights[offset]) };
                    constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
                    for (IndexType j = 0; j < NumChunks; ++j) {
                        accumulation[j] = _mm_add_epi16(accumulation[j], column[j]);
                    }
#elif defined(MMX)
                    auto accumulation{ reinterpret_cast<__m64*>(&accumulator.accumulation[perspective][i][0]) };
                    auto column{ reinterpret_cast<__m64 const*>(&_weights[offset]) };
                    constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
                    for (IndexType j = 0; j < NumChunks; ++j) {
                        accumulation[j] = _mm_add_pi16(accumulation[j], column[j]);
                    }
#elif defined(NEON)
                    auto accumulation{ reinterpret_cast<int16x8_t*>(&accumulator.accumulation[perspective][i][0]) };
                    auto column{ reinterpret_cast<int16x8_t const*>(&_weights[offset]) };
                    constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
                    for (IndexType j = 0; j < NumChunks; ++j) {
                        accumulation[j] = vaddq_s16(accumulation[j], column[j]);
                    }
#else
                    for (IndexType j = 0; j < HalfDimensions; ++j) {
                        accumulator.accumulation[perspective][i][j] += _weights[offset + j];
                    }
#endif

                }
            }
#if defined(MMX)
            _mm_empty();
#endif

            accumulator.accumulationComputed = true;
            accumulator.scoreComputed = false;
        }

        // Calculate cumulative value using difference calculation
        void updateAccumulator(Position const &pos) const {
            auto const prevAccumulator{ pos.state()->prevState->accumulator };
            auto &accumulator{ pos.state()->accumulator };
            IndexType i{ 0 };
            Features::IndexList removedIndices[2], addedIndices[2];
            bool reset[2];
            RawFeatures::appendChangedIndices(pos, RefreshTriggers[i], removedIndices, addedIndices, reset);
            for (Color perspective : { WHITE, BLACK }) {

#if defined(AVX2)
                constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
                auto accumulation{ reinterpret_cast<__m256i*>(&accumulator.accumulation[perspective][i][0]) };

#elif defined(SSE2)
                constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
                auto accumulation{ reinterpret_cast<__m128i*>(&accumulator.accumulation[perspective][i][0]) };

#elif defined(MMX)
                constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
                auto accumulation{ reinterpret_cast<__m64*>(&accumulator.accumulation[perspective][i][0]) };

#elif defined(NEON)
                constexpr IndexType NumChunks{ HalfDimensions / (SimdWidth / 2) };
                auto accumulation{ reinterpret_cast<int16x8_t*>(&accumulator.accumulation[perspective][i][0]) };
#endif

                if (reset[perspective]) {
                    std::memcpy(accumulator.accumulation[perspective][i], _biases, HalfDimensions * sizeof (BiasType));
                }
                else {
                    std::memcpy(accumulator.accumulation[perspective][i], prevAccumulator.accumulation[perspective][i], HalfDimensions * sizeof (BiasType));
                    // Difference calculation for the deactivated features
                    for (auto const index : removedIndices[perspective]) {
                        IndexType const offset{ HalfDimensions * index };

#if defined(AVX2)
                        auto column{ reinterpret_cast<__m256i const*>(&_weights[offset]) };
                        for (IndexType j = 0; j < NumChunks; ++j) {
                            accumulation[j] = _mm256_sub_epi16(accumulation[j], column[j]);
                        }

#elif defined(SSE2)
                        auto column{ reinterpret_cast<__m128i const*>(&_weights[offset]) };
                        for (IndexType j = 0; j < NumChunks; ++j) {
                            accumulation[j] = _mm_sub_epi16(accumulation[j], column[j]);
                        }

#elif defined(MMX)
                        auto column{ reinterpret_cast<__m64 const*>(&_weights[offset]) };
                        for (IndexType j = 0; j < NumChunks; ++j) {
                            accumulation[j] = _mm_sub_pi16(accumulation[j], column[j]);
                        }

#elif defined(NEON)
                        auto column{ reinterpret_cast<int16x8_t const*>(&_weights[offset]) };
                        for (IndexType j = 0; j < NumChunks; ++j) {
                            accumulation[j] = vsubq_s16(accumulation[j], column[j]);
                        }

#else
                        for (IndexType j = 0; j < HalfDimensions; ++j) {
                            accumulator.accumulation[perspective][i][j] -= _weights[offset + j];
                        }
#endif

                    }
                }

                {
                    // Difference calculation for the activated features
                    for (auto const index : addedIndices[perspective]) {
                        IndexType const offset{ HalfDimensions * index };

#if defined(AVX2)
                        auto column{ reinterpret_cast<__m256i const*>(&_weights[offset]) };
                        for (IndexType j = 0; j < NumChunks; ++j) {
                            accumulation[j] = _mm256_add_epi16(accumulation[j], column[j]);
                        }

#elif defined(SSE2)
                        auto column{ reinterpret_cast<__m128i const*>(&_weights[offset]) };
                        for (IndexType j = 0; j < NumChunks; ++j) {
                            accumulation[j] = _mm_add_epi16(accumulation[j], column[j]);
                        }

#elif defined(MMX)
                        auto column{ reinterpret_cast<__m64 const*>(&_weights[offset]) };
                        for (IndexType j = 0; j < NumChunks; ++j) {
                            accumulation[j] = _mm_add_pi16(accumulation[j], column[j]);
                        }

#elif defined(NEON)
                        auto column{ reinterpret_cast<int16x8_t const*>(&_weights[offset]) };
                        for (IndexType j = 0; j < NumChunks; ++j) {
                            accumulation[j] = vaddq_s16(accumulation[j], column[j]);
                        }

#else
                        for (IndexType j = 0; j < HalfDimensions; ++j) {
                            accumulator.accumulation[perspective][i][j] += _weights[offset + j];
                        }
#endif

                    }
                }
            }
#if defined(MMX)
            _mm_empty();
#endif

            accumulator.accumulationComputed = true;
            accumulator.scoreComputed = false;
        }
    };

}  // namespace Evaluator::NNUE
