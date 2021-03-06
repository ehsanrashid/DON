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
        using OutputType = int32_t;
        static_assert(std::is_same<InputType, uint8_t>::value, "");

        // Number of input/output dimensions
        static constexpr IndexType InputDimensions{ PreviousLayer::OutputDimensions };
        static constexpr IndexType OutputDimensions{ OutputDimensionsT };
        static constexpr IndexType PaddedInputDimensions{ ceilToMultiple<IndexType>(InputDimensions, MaxSimdWidth) };

    #if defined(USE_AVX512)
        static constexpr IndexType OutputSimdWidth{ SimdWidth / 2 };
    #elif defined(USE_SSSE3)
        static constexpr IndexType OutputSimdWidth{ SimdWidth / 4 };
    #endif

        // Size of forward propagation buffer used in this layer
        static constexpr size_t SelfBufferSize{ ceilToMultiple(OutputDimensions * sizeof(OutputType), CacheLineSize) };

        // Size of the forward propagation buffer used from the input layer to this layer
        static constexpr size_t BufferSize{ PreviousLayer::BufferSize + SelfBufferSize };

        // Hash value embedded in the evaluation file
        static constexpr uint32_t getHashValue() {
            uint32_t hashValue{ 0xCC03DAE4u };
            hashValue += OutputDimensions;
            hashValue ^= PreviousLayer::getHashValue() >> 1;
            hashValue ^= PreviousLayer::getHashValue() << 31;
            return hashValue;
        }

        // Read network parameters
        bool readParameters(std::istream &istream) {
            if (!previousLayer_.readParameters(istream)) {
                return false;
            }
            for (size_t i = 0; i < OutputDimensions; ++i) {
                biases_[i] = readLittleEndian<BiasType>(istream);
            }
            for (size_t i = 0; i < OutputDimensions * PaddedInputDimensions; ++i) {

        #if defined(USE_SSSE3)
                weights_[(i / 4) % (PaddedInputDimensions / 4) * OutputDimensions * 4 +
                          i / PaddedInputDimensions * 4 +
                          i % 4] = readLittleEndian<WeightType>(istream);
        #else
                weights_[i] = readLittleEndian<WeightType>(istream);
        #endif
            }

        #if defined(USE_SSSE3)
            // Determine if eights of weight and input products can be summed using 16bits
            // without saturation. We assume worst case combinations of 0 and 127 for all inputs.
            if (OutputDimensions > 1
                && !istream.fail()) {

                saturation.count = 0;

            #if !defined(USE_VNNI)
                for (IndexType i = 0; i < PaddedInputDimensions; i += 16) {
                    for (IndexType j = 0; j < OutputDimensions; ++j) {
                        for (int x = 0; x < 2; ++x) {
                            WeightType *w{ &weights_[i * OutputDimensions + j * 4 + x * 2] };
                            int sum[2] = { 0, 0 };
                            for (int k = 0; k < 8; ++k) {
                                IndexType idx{ k / 2 * OutputDimensions * 4 + k % 2 };
                                sum[w[idx] < 0] += w[idx];
                            }
                            for (int sign : { -1, 1 }) {
                                while (sign * sum[sign == -1] > 258) {
                                    int maxK{ 0 }, maxW{ 0 };
                                    for (int k = 0; k < 8; ++k) {
                                        IndexType idx = k / 2 * OutputDimensions * 4 + k % 2;
                                        if (maxW < sign * w[idx]) {
                                            maxK = k, maxW = sign * w[idx];
                                        }
                                    }

                                    IndexType idx{ maxK / 2 * OutputDimensions * 4 + maxK % 2 };
                                    sum[sign == -1] -= w[idx];
                                    saturation.add(j, i + maxK / 2 * 4 + maxK % 2 + x * 2, w[idx]);
                                    w[idx] = 0;
                                }
                            }
                        }
                    }
                }
                // Non functional optimization for faster more linear access
                std::sort(saturation.ids, saturation.ids + saturation.count,
                    [](const Entry &e1, const Entry &e2) {
                        return e1.in != e2.in ?
                                e1.in < e2.in :
                                e1.out < e2.out;
                    });
            #endif
            }
        #endif

            return !istream.fail();
        }

        // Forward propagation
        OutputType const* propagate(TransformedFeatureType const *transformedFeatures, char *buffer) const {

            auto const input{ previousLayer_.propagate(transformedFeatures, buffer + SelfBufferSize) };

    #if defined(USE_AVX512)

            [[maybe_unused]] __m512i const One512{ _mm512_set1_epi16(1) };

            [[maybe_unused]] auto m512_hadd = [](__m512i sum, int bias) -> int {
                return _mm512_reduce_add_epi32(sum) + bias;
            };

            [[maybe_unused]] auto m512_add_dpbusd_epi32 = [=](__m512i &acc, __m512i a, __m512i b) {
        #if defined(USE_VNNI)
                acc = _mm512_dpbusd_epi32(acc, a, b);
        #else
                __m512i product0{ _mm512_maddubs_epi16(a, b) };
                product0 = _mm512_madd_epi16(product0, One512);
                acc = _mm512_add_epi32(acc, product0);
        #endif
            };

            [[maybe_unused]] auto m512_add_dpbusd_epi32x4 = [=](__m512i &acc, __m512i a0, __m512i b0,
                                                                              __m512i a1, __m512i b1,
                                                                              __m512i a2, __m512i b2,
                                                                              __m512i a3, __m512i b3)
            {
        #if defined(USE_VNNI)
                acc = _mm512_dpbusd_epi32(acc, a0, b0);
                acc = _mm512_dpbusd_epi32(acc, a1, b1);
                acc = _mm512_dpbusd_epi32(acc, a2, b2);
                acc = _mm512_dpbusd_epi32(acc, a3, b3);
        #else
                __m512i product0{ _mm512_maddubs_epi16(a0, b0) };
                __m512i product1{ _mm512_maddubs_epi16(a1, b1) };
                __m512i product2{ _mm512_maddubs_epi16(a2, b2) };
                __m512i product3{ _mm512_maddubs_epi16(a3, b3) };
                product0 = _mm512_add_epi16(product0, product1);
                product2 = _mm512_add_epi16(product2, product3);
                product0 = _mm512_add_epi16(product0, product2);
                product0 = _mm512_madd_epi16(product0, One512);
                acc = _mm512_add_epi32(acc, product0);
        #endif
            };

    #endif
    #if defined(USE_AVX2)

            [[maybe_unused]] __m256i const One256{ _mm256_set1_epi16(1) };

            [[maybe_unused]] auto m256_hadd = [](__m256i sum, int bias) -> int {
                __m128i sum128{ _mm_add_epi32(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1)) };
                sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_BADC));
                sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_CDAB));
                return _mm_cvtsi128_si32(sum128) + bias;
            };

            [[maybe_unused]] auto m256_add_dpbusd_epi32 = [=](__m256i &acc, __m256i a, __m256i b) {
        #if defined(USE_VNNI)
                acc = _mm256_dpbusd_epi32(acc, a, b);
        #else
                __m256i product0{ _mm256_maddubs_epi16(a, b) };
                product0 = _mm256_madd_epi16(product0, One256);
                acc = _mm256_add_epi32(acc, product0);
        #endif
            };

            [[maybe_unused]] auto m256_add_dpbusd_epi32x4 = [=](__m256i &acc, __m256i a0, __m256i b0,
                                                                              __m256i a1, __m256i b1,
                                                                              __m256i a2, __m256i b2,
                                                                              __m256i a3, __m256i b3)
            {
        #if defined(USE_VNNI)
                acc = _mm256_dpbusd_epi32(acc, a0, b0);
                acc = _mm256_dpbusd_epi32(acc, a1, b1);
                acc = _mm256_dpbusd_epi32(acc, a2, b2);
                acc = _mm256_dpbusd_epi32(acc, a3, b3);
        #else
                __m256i product0{ _mm256_maddubs_epi16(a0, b0) };
                __m256i product1{ _mm256_maddubs_epi16(a1, b1) };
                __m256i product2{ _mm256_maddubs_epi16(a2, b2) };
                __m256i product3{ _mm256_maddubs_epi16(a3, b3) };
                product0 = _mm256_add_epi16(product0, product1);
                product2 = _mm256_add_epi16(product2, product3);
                product0 = _mm256_add_epi16(product0, product2);
                product0 = _mm256_madd_epi16(product0, One256);
                acc = _mm256_add_epi32(acc, product0);
        #endif
            };

    #endif
    #if defined(USE_SSSE3)

            [[maybe_unused]] __m128i const One128{ _mm_set1_epi16(1) };

            [[maybe_unused]] auto m128_hadd = [](__m128i sum, int bias) -> int {
                sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0x4E)); //_MM_PERM_BADC
                sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0xB1)); //_MM_PERM_CDAB
                return _mm_cvtsi128_si32(sum) + bias;
            };

            [[maybe_unused]] auto m128_add_dpbusd_epi32 = [=](__m128i &acc, __m128i a, __m128i b) {
                __m128i product0{ _mm_maddubs_epi16(a, b) };
                product0 = _mm_madd_epi16(product0, One128);
                acc = _mm_add_epi32(acc, product0);
            };

            [[maybe_unused]] auto m128_add_dpbusd_epi32x4 = [=](__m128i &acc, __m128i a0, __m128i b0,
                                                                              __m128i a1, __m128i b1,
                                                                              __m128i a2, __m128i b2,
                                                                              __m128i a3, __m128i b3)
            {
                __m128i product0{ _mm_maddubs_epi16(a0, b0) };
                __m128i product1{ _mm_maddubs_epi16(a1, b1) };
                __m128i product2{ _mm_maddubs_epi16(a2, b2) };
                __m128i product3{ _mm_maddubs_epi16(a3, b3) };
                product0 = _mm_add_epi16(product0, product1);
                product2 = _mm_add_epi16(product2, product3);
                product0 = _mm_add_epi16(product0, product2);
                product0 = _mm_madd_epi16(product0, One128);
                acc = _mm_add_epi32(acc, product0);
            };

    #endif

    #if defined(USE_AVX512)
        using vec_t                 = __m512i;
        #define vec_setzero         _mm512_setzero_si512
        #define vec_set_32          _mm512_set1_epi32
        auto &vec_add_dpbusd_32     = m512_add_dpbusd_epi32;
        auto &vec_add_dpbusd_32x4   = m512_add_dpbusd_epi32x4;
        auto &vec_hadd              = m512_hadd;
    #elif defined(USE_AVX2)
        using vec_t                 = __m256i;
        #define vec_setzero         _mm256_setzero_si256
        #define vec_set_32          _mm256_set1_epi32
        auto &vec_add_dpbusd_32     = m256_add_dpbusd_epi32;
        auto &vec_add_dpbusd_32x4   = m256_add_dpbusd_epi32x4;
        auto &vec_hadd              = m256_hadd;
    #elif defined(USE_SSSE3)
        using vec_t                 = __m128i;
        #define vec_setzero         _mm_setzero_si128
        #define vec_set_32          _mm_set1_epi32
        auto &vec_add_dpbusd_32     = m128_add_dpbusd_epi32;
        auto &vec_add_dpbusd_32x4   = m128_add_dpbusd_epi32x4;
        auto &vec_hadd              = m128_hadd;
    #endif

    #if defined(USE_SSSE3)

        auto const output{ reinterpret_cast<OutputType*>(buffer) };
        auto const inputVector{ reinterpret_cast<vec_t const*>(input) };

        static_assert(OutputDimensions % OutputSimdWidth == 0
                   || OutputDimensions == 1);

        // OutputDimensions is either 1 or a multiple of SimdWidth
        // because then it is also an input dimension.
        if constexpr (OutputDimensions % OutputSimdWidth == 0) {
            constexpr IndexType NumChunks{ PaddedInputDimensions / 4 };

            auto const input32{ reinterpret_cast<int32_t const*>(input) };
            vec_t *outptr{ reinterpret_cast<vec_t*>(output) };
            std::memcpy(output, biases_, OutputDimensions * sizeof(OutputType));

            for (IndexType i = 0; i < NumChunks - 3; i += 4) {
                vec_t const in0{ vec_set_32(input32[i + 0]) };
                vec_t const in1{ vec_set_32(input32[i + 1]) };
                vec_t const in2{ vec_set_32(input32[i + 2]) };
                vec_t const in3{ vec_set_32(input32[i + 3]) };
                auto const col0{ reinterpret_cast<vec_t const*>(&weights_[(i + 0) * OutputDimensions * 4]) };
                auto const col1{ reinterpret_cast<vec_t const*>(&weights_[(i + 1) * OutputDimensions * 4]) };
                auto const col2{ reinterpret_cast<vec_t const*>(&weights_[(i + 2) * OutputDimensions * 4]) };
                auto const col3{ reinterpret_cast<vec_t const*>(&weights_[(i + 3) * OutputDimensions * 4]) };
                for (int j = 0; j * OutputSimdWidth < OutputDimensions; ++j) {
                    vec_add_dpbusd_32x4(outptr[j], in0, col0[j], in1, col1[j], in2, col2[j], in3, col3[j]);
                }
            }
            for (int i = 0; i < saturation.count; ++i) {
                output[saturation.ids[i].out] += input[saturation.ids[i].in] * saturation.ids[i].w;
            }
        } else
        if constexpr (OutputDimensions == 1) {

        #if defined(USE_AVX512)
            if constexpr (PaddedInputDimensions % (SimdWidth * 2) != 0) {
                constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
                auto const inputVector256{ reinterpret_cast<__m256i const*>(input) };

                __m256i sum0{ _mm256_setzero_si256() };
                auto const row0{ reinterpret_cast<__m256i const*>(&weights_[0]) };

                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m256i const in{ inputVector256[j] };
                    m256_add_dpbusd_epi32(sum0, in, row0[j]);
                }
                output[0] = m256_hadd(sum0, biases_[0]);
            } else
        #endif
            {
        #if defined(USE_AVX512)
                constexpr IndexType NumChunks{ PaddedInputDimensions / (SimdWidth * 2) };
        #else
                constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
        #endif
                vec_t sum0{ vec_setzero() };
                auto const row0{ reinterpret_cast<vec_t const*>(&weights_[0]) };

                for (IndexType j = 0; j < NumChunks; ++j) {
                    vec_t const in{ inputVector[j] };
                    vec_add_dpbusd_32(sum0, in, row0[j]);
                }
                output[0] = vec_hadd(sum0, biases_[0]);
            }
        }
    #else

        // Use old implementation for the other architectures.
        auto output{ reinterpret_cast<OutputType*>(buffer) };

        #if defined(USE_SSE2)
        constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
        __m128i const Zero{ _mm_setzero_si128() };
        auto const inputVector{ reinterpret_cast<__m128i const*>(input) };
        #elif defined(USE_MMX)
        constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
        __m64 const Zero{ _mm_setzero_si64() };
        auto const inputVector{ reinterpret_cast<__m64 const*>(input) };
        #elif defined(USE_NEON)
        constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
        auto const inputVector{ reinterpret_cast<int8x8_t const*>(input) };
        #endif

        for (IndexType i = 0; i < OutputDimensions; ++i) {
            IndexType const offset{ i * PaddedInputDimensions };

        #if defined(USE_SSE2)
            __m128i sum_lo{ _mm_cvtsi32_si128(biases_[i]) };
            __m128i sum_hi{ Zero };
            auto const row{ reinterpret_cast<__m128i const*>(&weights_[offset]) };
            for (IndexType j = 0; j < NumChunks; ++j) {
                __m128i row_j = _mm_load_si128(&row[j]);
                __m128i input_j = _mm_load_si128(&inputVector[j]);
                __m128i extended_row_lo = _mm_srai_epi16(_mm_unpacklo_epi8(row_j, row_j), 8);
                __m128i extended_row_hi = _mm_srai_epi16(_mm_unpackhi_epi8(row_j, row_j), 8);
                __m128i extended_input_lo = _mm_unpacklo_epi8(input_j, Zero);
                __m128i extended_input_hi = _mm_unpackhi_epi8(input_j, Zero);
                __m128i product_lo = _mm_madd_epi16(extended_row_lo, extended_input_lo);
                __m128i product_hi = _mm_madd_epi16(extended_row_hi, extended_input_hi);
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
            __m64 sum_lo{ _mm_cvtsi32_si64(biases_[i]) };
            __m64 sum_hi{ Zero };
            auto const row{ reinterpret_cast<__m64 const*>(&weights_[offset]) };
            for (IndexType j = 0; j < NumChunks; ++j) {
                __m64 row_j = row[j];
                __m64 input_j = inputVector[j];
                __m64 extended_row_lo = _mm_srai_pi16(_mm_unpacklo_pi8(row_j, row_j), 8);
                __m64 extended_row_hi = _mm_srai_pi16(_mm_unpackhi_pi8(row_j, row_j), 8);
                __m64 extended_input_lo = _mm_unpacklo_pi8(input_j, Zero);
                __m64 extended_input_hi = _mm_unpackhi_pi8(input_j, Zero);
                __m64 product_lo = _mm_madd_pi16(extended_row_lo, extended_input_lo);
                __m64 product_hi = _mm_madd_pi16(extended_row_hi, extended_input_hi);
                sum_lo = _mm_add_pi32(sum_lo, product_lo);
                sum_hi = _mm_add_pi32(sum_hi, product_hi);
            }
            __m64 sum{ _mm_add_pi32(sum_lo, sum_hi) };
            sum = _mm_add_pi32(sum, _mm_unpackhi_pi32(sum, sum));
            output[i] = _mm_cvtsi64_si32(sum);

        #elif defined(USE_NEON)
            int32x4_t sum{ biases_[i] };
            auto const row{ reinterpret_cast<int8x8_t const*>(&weights_[offset]) };
            for (IndexType j = 0; j < NumChunks; ++j) {
                int16x8_t product{ vmull_s8(inputVector[j * 2], row[j * 2]) };
                product = vmlal_s8(product, inputVector[j * 2 + 1], row[j * 2 + 1]);
                sum = vpadalq_s16(sum, product);
            }
            output[i] = sum[0] + sum[1] + sum[2] + sum[3];

        #else
            OutputType sum{ biases_[i] };
            for (IndexType j = 0; j < InputDimensions; ++j) {
                sum += weights_[offset + j] * input[j];
            }
            output[i] = sum;
        #endif

        }
        #if defined(USE_MMX)
        _mm_empty();
        #endif

    #endif
            return output;
        }

    private:

        using BiasType = OutputType;
        using WeightType = int8_t;

        PreviousLayer previousLayer_;

        alignas(CacheLineSize) BiasType biases_[OutputDimensions];
        alignas(CacheLineSize) WeightType weights_[OutputDimensions * PaddedInputDimensions];

    #if defined(USE_SSSE3)

        struct Entry {
            uint16_t out;
            uint16_t in;
            int8_t w;
        };

        struct Saturation {

            Saturation() :
                count{ 0 } {
            }

            void add(uint16_t out, uint16_t in, int8_t w) {
                ids[count].out = out;
                ids[count].in = in;
                ids[count].w = w;
                ++count;
            }

            int count;
            Entry ids[PaddedInputDimensions * OutputDimensions * 3 / 4];
        };

        Saturation saturation;
    #endif

    };

}
