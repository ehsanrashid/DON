// Definition of layer AffineTransform of NNUE evaluation function
#pragma once

#include <iostream>

#include "../nnue_common.h"

namespace Evaluator::NNUE::Layers {

    // Affine transformation layer
    template<typename PreviousLayer, IndexType OutputDimensionsT>
    class AffineTransform {

    public:
        // Input/output type
        using InputType = typename PreviousLayer::OutputType;
        using OutputType = i32;
        static_assert (std::is_same<InputType, u08>::value, "");

        // Number of input/output dimensions
        static constexpr IndexType InputDimensions{ PreviousLayer::OutputDimensions };
        static constexpr IndexType OutputDimensions{ OutputDimensionsT };
        static constexpr IndexType PaddedInputDimensions{ ceilToMultiple<IndexType>(InputDimensions, MaxSimdWidth) };

    private:
        using BiasType = OutputType;
        using WeightType = i08;

        PreviousLayer _previousLayer;

        alignas(CacheLineSize) BiasType _biases[OutputDimensions];
        alignas(CacheLineSize) WeightType _weights[OutputDimensions * PaddedInputDimensions];

    public:
        // Size of forward propagation buffer used in this layer
        static constexpr size_t SelfBufferSize{ ceilToMultiple(OutputDimensions * sizeof (OutputType), CacheLineSize) };

        // Size of the forward propagation buffer used from the input layer to this layer
        static constexpr size_t BufferSize{ PreviousLayer::BufferSize + SelfBufferSize };

        // Hash value embedded in the evaluation file
        static constexpr u32 getHashValue() {
            u32 hashValue{ 0xCC03DAE4u };
            hashValue += OutputDimensions;
            hashValue ^= PreviousLayer::getHashValue() >> 1;
            hashValue ^= PreviousLayer::getHashValue() << 31;
            return hashValue;
        }

        // Read network parameters
        bool readParameters(std::istream &is) {
            if (!_previousLayer.readParameters(is)) {
                return false;
            }
            for (size_t i = 0; i < OutputDimensions; ++i) {
                _biases[i] = readLittleEndian<BiasType>(is);
            }
            for (size_t i = 0; i < OutputDimensions * PaddedInputDimensions; ++i) {
                _weights[i] = readLittleEndian<WeightType>(is);
            }
            return !is.fail();
        }

        // Forward propagation
        OutputType const* propagate(TransformedFeatureType const *transformedFeatures, char *buffer) const {
        
            auto const input{ _previousLayer.propagate(transformedFeatures, buffer + SelfBufferSize) };
            auto const output{ reinterpret_cast<OutputType*>(buffer) };

#if defined(USE_AVX512)
            constexpr IndexType NumChunks{ PaddedInputDimensions / (SimdWidth * 2) };
            auto const inputVector{ reinterpret_cast<__m512i const*>(input) };
    #if !defined(USE_VNNI)
            __m512i const kOnes{ _mm512_set1_epi16(1) };
    #endif
#elif defined(USE_AVX2)
            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
            __m256i const kOnes{ _mm256_set1_epi16(1) };
            auto const inputVector{ reinterpret_cast<__m256i const*>(input) };
#elif defined(USE_SSE2)
            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
    #if !defined(USE_SSSE3)
            __m128i const kZeros{ _mm_setzero_si128() };
    #else
            __m128i const kOnes{ _mm_set1_epi16(1) };
    #endif
            auto const inputVector{ reinterpret_cast<__m128i const*>(input) };
#elif defined(USE_MMX)
            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
            __m64 const kZeros{ _mm_setzero_si64() };
            auto const inputVector{ reinterpret_cast<__m64 const*>(input) };
#elif defined(USE_NEON)
            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
            auto const inputVector{ reinterpret_cast<int8x8_t const*>(input) };
#endif

            for (IndexType i = 0; i < OutputDimensions; ++i) {
                IndexType const offset{ i * PaddedInputDimensions };

#if defined(USE_AVX512)
                __m512i sum{ _mm512_setzero_si512() };
                auto const row{ reinterpret_cast<__m512i const*>(&_weights[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
    #if defined(USE_VNNI)
                    sum = _mm512_dpbusd_epi32(sum, _mm512_loadA_si512(&inputVector[j]), _mm512_load_si512(&row[j]));
    #else
                    __m512i product{ _mm512_maddubs_epi16(_mm512_loadA_si512(&inputVector[j]), _mm512_load_si512(&row[j])) };
                    product = _mm512_madd_epi16(product, kOnes);
                    sum = _mm512_add_epi32(sum, product);
    #endif
                }

                // Note: Changing MaxSimdWidth from 32 to 64 breaks loading existing networks.
                // As a result PaddedInputDimensions may not be an even multiple of 64(512bit)
                // and we have to do one more 256bit chunk.
                if (PaddedInputDimensions != NumChunks * SimdWidth * 2) {
                    auto const iv256{ reinterpret_cast<__m256i const*>(&inputVector[NumChunks]) };
                    auto const row256{ reinterpret_cast<__m256i const*>(&row[NumChunks]) };
    #if defined(USE_VNNI)
                    __m256i product256{ _mm256_dpbusd_epi32(_mm512_castsi512_si256(sum), _mm256_loadA_si256(&iv256[0]), _mm256_load_si256(&row256[0])) };
                    sum = _mm512_inserti32x8(sum, product256, 0);
    #else
                    __m256i product256{ _mm256_maddubs_epi16(_mm256_loadA_si256(&iv256[0]), _mm256_load_si256(&row256[0])) };
                    sum = _mm512_add_epi32(sum, _mm512_cvtepi16_epi32(product256));
    #endif
                }
                output[i] = _mm512_reduce_add_epi32(sum) + _biases[i];

#elif defined(USE_AVX2)
                __m256i sum{ _mm256_setzero_si256() };
                auto const row{ reinterpret_cast<__m256i const*>(&_weights[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m256i product{ _mm256_maddubs_epi16(_mm256_loadA_si256(&inputVector[j]), _mm256_load_si256(&row[j])) };
                    product = _mm256_madd_epi16(product, kOnes);
                    sum = _mm256_add_epi32(sum, product);
                }
                __m128i sum128{ _mm_add_epi32(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1)) };
                sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_BADC));
                sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_CDAB));
                output[i] = _mm_cvtsi128_si32(sum128) + _biases[i];

#elif defined(USE_SSSE3)
                __m128i sum{ _mm_setzero_si128() };
                auto const row{ reinterpret_cast<__m128i const*>(&_weights[offset]) };
                for (int j = 0; j < (int) NumChunks - 1; j += 2) {
                    __m128i product0{ _mm_maddubs_epi16(_mm_load_si128(&inputVector[j]), _mm_load_si128(&row[j])) };
                    product0 = _mm_madd_epi16(product0, kOnes);
                    sum = _mm_add_epi32(sum, product0);
                    __m128i product1{ _mm_maddubs_epi16(_mm_load_si128(&inputVector[j + 1]), _mm_load_si128(&row[j + 1])) };
                    product1 = _mm_madd_epi16(product1, kOnes);
                    sum = _mm_add_epi32(sum, product1);
                }
                if (NumChunks & 0x1) {
                    __m128i product{ _mm_maddubs_epi16(_mm_load_si128(&inputVector[NumChunks - 1]), _mm_load_si128(&row[NumChunks - 1])) };
                    product = _mm_madd_epi16(product, kOnes);
                    sum = _mm_add_epi32(sum, product);
                }
                sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0x4E)); //_MM_PERM_BADC
                sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0xB1)); //_MM_PERM_CDAB
                output[i] = _mm_cvtsi128_si32(sum) + _biases[i];

#elif defined(USE_SSE2)
                __m128i sum_lo{ _mm_cvtsi32_si128(_biases[i]) };
                __m128i sum_hi{ kZeros };
                auto const row{ reinterpret_cast<__m128i const*>(&_weights[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m128i row_j{ _mm_load_si128(&row[j]) };
                    __m128i input_j{ _mm_load_si128(&inputVector[j]) };
                    __m128i row_signs{ _mm_cmpgt_epi8(kZeros, row_j) };
                    __m128i extended_row_lo{ _mm_unpacklo_epi8(row_j, row_signs) };
                    __m128i extended_row_hi{ _mm_unpackhi_epi8(row_j, row_signs) };
                    __m128i extended_input_lo{ _mm_unpacklo_epi8(input_j, kZeros) };
                    __m128i extended_input_hi{ _mm_unpackhi_epi8(input_j, kZeros) };
                    __m128i product_lo{ _mm_madd_epi16(extended_row_lo, extended_input_lo) };
                    __m128i product_hi{ _mm_madd_epi16(extended_row_hi, extended_input_hi) };
                    sum_lo = _mm_add_epi32(sum_lo, product_lo);
                    sum_hi = _mm_add_epi32(sum_hi, product_hi);
                }
                __m128i sum{ _mm_add_epi32(sum_lo, sum_hi) };
                __m128i sum_high_64{ _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2)) };
                sum = _mm_add_epi32(sum, sum_high_64);
                __m128i sum_second_32{ _mm_shufflelo_epi16(sum, _MM_SHUFFLE(1, 0, 3, 2)) };
                sum = _mm_add_epi32(sum, sum_second_32);
                output[i] = _mm_cvtsi128_si32(sum);

#elif defined(USE_MMX)
                __m64 sum_lo{ _mm_cvtsi32_si64(_biases[i]) };
                __m64 sum_hi{ kZeros };
                auto const row{ reinterpret_cast<__m64 const*>(&_weights[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m64 row_j{ row[j] };
                    __m64 input_j{ inputVector[j] };
                    __m64 row_signs{ _mm_cmpgt_pi8(kZeros, row_j) };
                    __m64 extended_row_lo{ _mm_unpacklo_pi8(row_j, row_signs) };
                    __m64 extended_row_hi{ _mm_unpackhi_pi8(row_j, row_signs) };
                    __m64 extended_input_lo{ _mm_unpacklo_pi8(input_j, kZeros) };
                    __m64 extended_input_hi{ _mm_unpackhi_pi8(input_j, kZeros) };
                    __m64 product_lo{ _mm_madd_pi16(extended_row_lo, extended_input_lo) };
                    __m64 product_hi{ _mm_madd_pi16(extended_row_hi, extended_input_hi) };
                    sum_lo = _mm_add_pi32(sum_lo, product_lo);
                    sum_hi = _mm_add_pi32(sum_hi, product_hi);
                }
                __m64 sum{ _mm_add_pi32(sum_lo, sum_hi) };
                sum = _mm_add_pi32(sum, _mm_unpackhi_pi32(sum, sum));
                output[i] = _mm_cvtsi64_si32(sum);

#elif defined(USE_NEON)
                int32x4_t sum{ _biases[i] };
                auto const row{ reinterpret_cast<int8x8_t const*>(&_weights[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    int16x8_t product = vmull_s8(inputVector[j * 2], row[j * 2]);
                    product = vmlal_s8(product, inputVector[j * 2 + 1], row[j * 2 + 1]);
                    sum = vpadalq_s16(sum, product);
                }
                output[i] = sum[0] + sum[1] + sum[2] + sum[3];
#else
                OutputType sum{ _biases[i] };
                for (IndexType j = 0; j < InputDimensions; ++j) {
                    sum += _weights[offset + j] * input[j];
                }
                output[i] = sum;
#endif

            }
#if defined(USE_MMX)
            _mm_empty();
#endif
            return output;
        }
    };

}  // namespace Evaluator::NNUE::Layers
