// Definition of layer ClippedReLU of NNUE evaluation function
#pragma once

#include "../nnue_common.h"

namespace Evaluator::NNUE::Layers {

    // Clipped ReLU
    template<typename PreviousLayer>
    class ClippedReLU {

    private:
        PreviousLayer _previousLayer;

    public:
        // Input/output type
        using InputType = typename PreviousLayer::OutputType;
        using OutputType = u08;
        static_assert (std::is_same<InputType, i32>::value, "");

        // Number of input/output dimensions
        static constexpr IndexType InputDimensions{ PreviousLayer::OutputDimensions };
        static constexpr IndexType OutputDimensions{ InputDimensions };

        // Size of forward propagation buffer used in this layer
        static constexpr size_t SelfBufferSize{ ceilToMultiple(OutputDimensions * sizeof (OutputType), CacheLineSize) };

        // Size of the forward propagation buffer used from the input layer to this layer
        static constexpr size_t BufferSize{ PreviousLayer::BufferSize + SelfBufferSize };

        // Hash value embedded in the evaluation file
        static constexpr u32 getHashValue() {
            u32 hashValue{ 0x538D24C7u };
            hashValue += PreviousLayer::getHashValue();
            return hashValue;
        }

        // Read network parameters
        bool readParameters(std::istream &istream) {
            return _previousLayer.readParameters(istream);
        }

        // Forward propagation
        OutputType const* propagate(TransformedFeatureType const *transformedFeatures, char *buffer) const {

            auto const input{ _previousLayer.propagate(transformedFeatures, buffer + SelfBufferSize) };
            auto const output{ reinterpret_cast<OutputType*>(buffer) };

#if defined(USE_AVX2)
            constexpr IndexType NumChunks{ InputDimensions / SimdWidth };
            __m256i const kZero{ _mm256_setzero_si256() };
            __m256i const kOffsets{ _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0) };
            auto const in{ reinterpret_cast<__m256i const*>(input) };
            auto const out{ reinterpret_cast<__m256i*>(output) };
            for (IndexType i = 0; i < NumChunks; ++i) {
                __m256i const words0{ _mm256_srai_epi16(_mm256_packs_epi32(_mm256_loadA_si256(&in[i * 4 + 0]), _mm256_loadA_si256(&in[i * 4 + 1])), WeightScaleBits) };
                __m256i const words1{ _mm256_srai_epi16(_mm256_packs_epi32(_mm256_loadA_si256(&in[i * 4 + 2]), _mm256_loadA_si256(&in[i * 4 + 3])), WeightScaleBits) };
                _mm256_storeA_si256(&out[i], _mm256_permutevar8x32_epi32(_mm256_max_epi8(_mm256_packs_epi16(words0, words1), kZero), kOffsets));
            }
            constexpr IndexType Start{ NumChunks * SimdWidth };

#elif defined(USE_SSE2)
            constexpr IndexType NumChunks{ InputDimensions / SimdWidth };

    #if defined(USE_SSE41)
            __m128i const kZero{ _mm_setzero_si128() };
    #else
            __m128i const k0x80s{ _mm_set1_epi8(-128) };
    #endif

            auto const in{ reinterpret_cast<__m128i const*>(input) };
            auto const out{ reinterpret_cast<__m128i*>(output) };
            for (IndexType i = 0; i < NumChunks; ++i) {
                __m128i const words0{ _mm_srai_epi16(_mm_packs_epi32(_mm_load_si128(&in[i * 4 + 0]), _mm_load_si128(&in[i * 4 + 1])), WeightScaleBits) };
                __m128i const words1{ _mm_srai_epi16(_mm_packs_epi32(_mm_load_si128(&in[i * 4 + 2]), _mm_load_si128(&in[i * 4 + 3])), WeightScaleBits) };
                __m128i const packedbytes{ _mm_packs_epi16(words0, words1) };
                _mm_store_si128(&out[i],
    #if defined(USE_SSE41)
                    _mm_max_epi8(packedbytes, kZero)
    #else
                    _mm_subs_epi8(_mm_adds_epi8(packedbytes, k0x80s), k0x80s)
    #endif
                );
            }
            constexpr IndexType Start{ NumChunks * SimdWidth };

#elif defined(USE_MMX)
            constexpr IndexType NumChunks{ InputDimensions / SimdWidth };
            __m64 const k0x80s{ _mm_set1_pi8(-128) };
            auto const in{ reinterpret_cast<__m64 const*>(input) };
            auto const out{ reinterpret_cast<__m64*>(output) };
            for (IndexType i = 0; i < NumChunks; ++i) {
                __m64 const words0{ _mm_srai_pi16(_mm_packs_pi32(in[i * 4 + 0], in[i * 4 + 1]), WeightScaleBits) };
                __m64 const words1{ _mm_srai_pi16(_mm_packs_pi32(in[i * 4 + 2], in[i * 4 + 3]), WeightScaleBits) };
                __m64 const packedbytes{ _mm_packs_pi16(words0, words1) };
                out[i] = _mm_subs_pi8(_mm_adds_pi8(packedbytes, k0x80s), k0x80s);
            }
            _mm_empty();
            constexpr IndexType Start{ NumChunks * SimdWidth };

#elif defined(USE_NEON)
            constexpr IndexType NumChunks{ InputDimensions / (SimdWidth / 2) };
            int8x8_t const kZero{ 0 };
            auto const in{ reinterpret_cast<int32x4_t const*>(input) };
            auto const out{ reinterpret_cast<int8x8_t*>(output) };
            for (IndexType i = 0; i < NumChunks; ++i) {
                int16x8_t shifted;
                auto const pack{ reinterpret_cast<int16x4_t*>(&shifted) };
                pack[0] = vqshrn_n_s32(in[i * 2 + 0], WeightScaleBits);
                pack[1] = vqshrn_n_s32(in[i * 2 + 1], WeightScaleBits);
                out[i] = vmax_s8(vqmovn_s16(shifted), kZero);
            }
            constexpr IndexType Start{ NumChunks * (SimdWidth / 2) };
#else
            constexpr IndexType Start{ 0 };
#endif

            for (IndexType i = Start; i < InputDimensions; ++i) {
                output[i] = static_cast<OutputType>(std::max(0, std::min(127, input[i] >> WeightScaleBits)));
            }
            return output;
        }
    };

}  // namespace Evaluator::NNUE::Layers
