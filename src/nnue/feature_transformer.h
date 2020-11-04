// A class that converts the input features of the NNUE evaluation function
#pragma once

#include "nnue_common.h"
#include "architecture.h"
#include "features/index_list.h"

namespace Evaluator::NNUE {

    // If vector instructions are enabled, we update and refresh the
    // accumulator tile by tile such that each tile fits in the CPU's vector registers.
    #define VECTOR

    #if defined(USE_AVX512)
        using vec_t = __m512i;
        #define vec_load(a)     _mm512_loadA_si512(a)
        #define vec_store(a,b)  _mm512_storeA_si512(a,b)
        #define vec_add_16(a,b) _mm512_add_epi16(a,b)
        #define vec_sub_16(a,b) _mm512_sub_epi16(a,b)
        static constexpr IndexType NumRegs = 8; // only 8 are needed

    #elif defined(USE_AVX2)
        using vec_t = __m256i;
        #define vec_load(a)     _mm256_loadA_si256(a)
        #define vec_store(a,b)  _mm256_storeA_si256(a,b)
        #define vec_add_16(a,b) _mm256_add_epi16(a,b)
        #define vec_sub_16(a,b) _mm256_sub_epi16(a,b)
        static constexpr IndexType NumRegs = 16;

    #elif defined(USE_SSE2)
        using vec_t = __m128i;
        #define vec_load(a)     (*(a))
        #define vec_store(a,b)  *(a)=(b)
        #define vec_add_16(a,b) _mm_add_epi16(a,b)
        #define vec_sub_16(a,b) _mm_sub_epi16(a,b)

        #if defined(IS_64BIT)
        static constexpr IndexType NumRegs = 16;
        #else
        static constexpr IndexType NumRegs = 8;
        #endif

    #elif defined(USE_MMX)
        using vec_t = __m64;
        #define vec_load(a)     (*(a))
        #define vec_store(a,b)  *(a)=(b)
        #define vec_add_16(a,b) _mm_add_pi16(a,b)
        #define vec_sub_16(a,b) _mm_sub_pi16(a,b)
        static constexpr IndexType NumRegs = 8;

    #elif defined(USE_NEON)
        using vec_t = int16x8_t;
        #define vec_load(a)     (*(a))
        #define vec_store(a,b)  *(a)=(b)
        #define vec_add_16(a,b) vaddq_s16(a,b)
        #define vec_sub_16(a,b) vsubq_s16(a,b)
        static constexpr IndexType NumRegs = 16;

    #else

        #undef VECTOR

    #endif

    // Input feature converter
    class FeatureTransformer {

    private:
        // Number of output dimensions for one side
        static constexpr IndexType HalfDimensions{ TransformedFeatureDimensions };

    #if defined(VECTOR)
        static constexpr IndexType TileHeight = NumRegs * sizeof(vec_t) / 2;
        static_assert (HalfDimensions % TileHeight == 0, "TileHeight must divide HalfDimensions");
    #endif

    public:
        // Output type
        using OutputType = TransformedFeatureType;

        // Number of input/output dimensions
        static constexpr IndexType InputDimensions{ RawFeatures::Dimensions };
        static constexpr IndexType OutputDimensions{ HalfDimensions * 2 };

        // Size of forward propagation buffer
        static constexpr size_t BufferSize{ OutputDimensions * sizeof(OutputType) };

        // Hash value embedded in the evaluation file
        static constexpr uint32_t getHashValue() {
            return RawFeatures::HashValue ^ OutputDimensions;
        }

        // Read network parameters
        bool readParameters(std::istream &istream) {
            for (size_t i = 0; i < HalfDimensions; ++i) {
                biases_[i] = readLittleEndian<BiasType>(istream);
            }
            for (size_t i = 0; i < HalfDimensions * InputDimensions; ++i) {
                weights_[i] = readLittleEndian<WeightType>(istream);
            }
            return !istream.fail();
        }

        // Convert input features
        void transform(Position const &pos, OutputType *output) const {

            updateAccumulator(pos, WHITE);
            updateAccumulator(pos, BLACK);

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

        // Calculate cumulative value using difference calculation
        void updateAccumulator(Position const &pos, const Color c) const {

        #if defined(VECTOR)
            // Gcc-10.2 unnecessarily spills AVX2 registers if this array
            // is defined in the VECTOR code below, once in each branch
            vec_t acc[NumRegs];
        #endif
            constexpr int MaxSteps = 6;
            StateInfo *stack[MaxSteps];
            int step = 0;
            int gain = popCount(pos.pieces()) - 2;

            // Look for a usable accumulator of an earlier position at most MaxSteps
            // back. We keep track of the estimated gain in terms of features to be
            // added/subtracted and accumulators to be saved.
            StateInfo *si = pos.state();
            while (si->accumulator.state[c] == EMPTY
                && step < MaxSteps) {

                auto &mi = si->moveInfo;
                if (mi.piece[0] == (c|KING)
                 || (gain -= mi.pieceCount + 2) <= 0) {
                    break;
                }
                stack[step++] = si;
                si = si->prevState;
            }

            if (si->accumulator.state[c] == COMPUTED) {
                // Update incrementally, including previous accumulators

                // First gather all features to be updated and mark the accumulators
                // as computed
                Features::IndexList added[MaxSteps], removed[MaxSteps];
                for (int i = 0; i < step; ++i) {
                    auto &mi = stack[i]->moveInfo;
                    Features::HalfKP<Features::Side::FRIEND>::appendChangedIndices(pos, mi, c, &removed[i], &added[i]);
                    stack[i]->accumulator.state[c] = COMPUTED;
                }

            #if defined(VECTOR)

                for (IndexType j = 0; j < HalfDimensions / TileHeight; ++j) {
                    auto accTile = reinterpret_cast<vec_t*>(&si->accumulator.accumulation[c][0][j * TileHeight]);
                    for (IndexType k = 0; k < NumRegs; ++k) {
                        acc[k] = vec_load(&accTile[k]);
                    }
                    for (int i = step - 1; i >= 0; --i) {
                        // Difference calculation for the deactivated features
                        for (const auto index : removed[i]) {
                            const IndexType offset = HalfDimensions * index + j * TileHeight;
                            auto column = reinterpret_cast<const vec_t*>(&weights_[offset]);
                            for (IndexType k = 0; k < NumRegs; ++k) {
                                acc[k] = vec_sub_16(acc[k], column[k]);
                            }
                        }

                        // Difference calculation for the activated features
                        for (const auto index : added[i]) {
                            const IndexType offset = HalfDimensions * index + j * TileHeight;
                            auto column = reinterpret_cast<const vec_t*>(&weights_[offset]);
                            for (IndexType k = 0; k < NumRegs; ++k) {
                                acc[k] = vec_add_16(acc[k], column[k]);
                            }
                        }

                        accTile = reinterpret_cast<vec_t*>(&stack[i]->accumulator.accumulation[c][0][j * TileHeight]);
                        for (IndexType k = 0; k < NumRegs; ++k) {
                            vec_store(&accTile[k], acc[k]);
                        }
                    }
                }

            #else

                for (int i = step - 1; i >= 0; --i) {
                    std::memcpy(stack[i]->accumulator.accumulation[c][0], si->accumulator.accumulation[c][0], HalfDimensions * sizeof(BiasType));
                    si = stack[i];

                    // Difference calculation for the deactivated features
                    for (const auto index : removed[i]) {
                        const IndexType offset = HalfDimensions * index;

                        for (IndexType j = 0; j < HalfDimensions; ++j) {
                            si->accumulator.accumulation[c][0][j] -= weights_[offset + j];
                        }
                    }

                    // Difference calculation for the activated features
                    for (const auto index : added[i]) {
                        const IndexType offset = HalfDimensions * index;

                        for (IndexType j = 0; j < HalfDimensions; ++j) {
                            si->accumulator.accumulation[c][0][j] += weights_[offset + j];
                        }
                    }
                }

            #endif
            } else {
                // Refresh the accumulator
                auto& accumulator{ pos.state()->accumulator };
                accumulator.state[c] = COMPUTED;
                Features::IndexList active;
                Features::HalfKP<Features::Side::FRIEND>::appendActiveIndices(pos, c, &active);

            #if defined(VECTOR)
                for (IndexType j = 0; j < HalfDimensions / TileHeight; ++j) {
                    auto biasesTile = reinterpret_cast<const vec_t*>(&biases_[j * TileHeight]);
                    for (IndexType k = 0; k < NumRegs; ++k) {
                        acc[k] = biasesTile[k];
                    }

                    for (const auto index : active) {
                        const IndexType offset = HalfDimensions * index + j * TileHeight;
                        auto column = reinterpret_cast<const vec_t*>(&weights_[offset]);

                        for (unsigned k = 0; k < NumRegs; ++k) {
                            acc[k] = vec_add_16(acc[k], column[k]);
                        }
                    }

                    auto accTile = reinterpret_cast<vec_t*>(&accumulator.accumulation[c][0][j * TileHeight]);
                    for (unsigned k = 0; k < NumRegs; ++k) {
                        vec_store(&accTile[k], acc[k]);
                    }
                }

            #else

                std::memcpy(accumulator.accumulation[c][0], biases_, HalfDimensions * sizeof(BiasType));

                for (const auto index : active) {
                    const IndexType offset{ HalfDimensions * index };

                    for (IndexType j = 0; j < HalfDimensions; ++j) {
                        accumulator.accumulation[c][0][j] += weights_[offset + j];
                    }
                }

            #endif
            }

        #if defined(USE_MMX)
            _mm_empty();
        #endif
        }

        using BiasType = int16_t;
        using WeightType = int16_t;

        alignas(CacheLineSize) BiasType biases_[HalfDimensions];
        alignas(CacheLineSize) WeightType weights_[HalfDimensions * InputDimensions];

    };

}
