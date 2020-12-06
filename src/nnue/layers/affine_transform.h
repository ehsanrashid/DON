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
            if (!_previousLayer.readParameters(istream)) {
                return false;
            }
            for (size_t i = 0; i < OutputDimensions; ++i) {
                biases_[i] = readLittleEndian<BiasType>(istream);
            }
            for (size_t i = 0; i < OutputDimensions * PaddedInputDimensions; ++i) {
                weights_[i] = readLittleEndian<WeightType>(istream);
            }
            return !istream.fail();
        }

        // Forward propagation
        OutputType const *propagate(TransformedFeatureType const *transformedFeatures, char *buffer) const {

            auto const input{ _previousLayer.propagate(transformedFeatures, buffer + SelfBufferSize) };

    #if defined(USE_AVX512)

            [[maybe_unused]] __m512i const One512{ _mm512_set1_epi16(1) };

            [[maybe_unused]] auto m512_hadd = [](__m512i sum, int bias) -> int {
                return _mm512_reduce_add_epi32(sum) + bias;
            };

            // This function takes
            //   sum0 = [xmm0a, xmm0b, xmm0c, xmm0d]
            //   sum1 = [xmm1a, xmm1b, xmm1c, xmm1d]
            //   sum2 = [xmm2a, xmm2b, xmm2c, xmm2d]
            //   sum3 = [xmm3a, xmm3b, xmm3c, xmm3d]
            // and returns
            //   ret = [
            //     reduce_add_epi32(xmm0a), reduce_add_epi32(xmm1a), reduce_add_epi32(xmm2a), reduce_add_epi32(xmm3a),
            //     reduce_add_epi32(xmm0b), reduce_add_epi32(xmm1b), reduce_add_epi32(xmm2b), reduce_add_epi32(xmm3b),
            //     reduce_add_epi32(xmm0c), reduce_add_epi32(xmm1c), reduce_add_epi32(xmm2c), reduce_add_epi32(xmm3c),
            //     reduce_add_epi32(xmm0d), reduce_add_epi32(xmm1d), reduce_add_epi32(xmm2d), reduce_add_epi32(xmm3d)
            //   ]
            [[maybe_unused]] auto m512_hadd128x16_interleave = [](__m512i sum0, __m512i sum1, __m512i sum2, __m512i sum3) -> __m512i {

                __m512i const sum01a{ _mm512_unpacklo_epi32(sum0, sum1) };
                __m512i const sum01b{ _mm512_unpackhi_epi32(sum0, sum1) };

                __m512i const sum23a{ _mm512_unpacklo_epi32(sum2, sum3) };
                __m512i const sum23b{ _mm512_unpackhi_epi32(sum2, sum3) };

                __m512i const sum01{ _mm512_add_epi32(sum01a, sum01b) };
                __m512i const sum23{ _mm512_add_epi32(sum23a, sum23b) };

                __m512i const sum0123a{ _mm512_unpacklo_epi64(sum01, sum23) };
                __m512i const sum0123b{ _mm512_unpackhi_epi64(sum01, sum23) };

                return _mm512_add_epi32(sum0123a, sum0123b);
            };

            [[maybe_unused]] auto m512_haddx4 = [m512_hadd128x16_interleave](__m512i sum0, __m512i sum1, __m512i sum2, __m512i sum3, __m128i bias) -> __m128i {

                __m512i const sum{ m512_hadd128x16_interleave(sum0, sum1, sum2, sum3) };

                __m256i sum256lo{ _mm512_castsi512_si256(sum) };
                __m256i sum256hi{ _mm512_extracti64x4_epi64(sum, 1) };

                sum256lo = _mm256_add_epi32(sum256lo, sum256hi);

                __m128i const sum128lo{ _mm256_castsi256_si128(sum256lo) };
                __m128i const sum128hi{ _mm256_extracti128_si256(sum256lo, 1) };

                return _mm_add_epi32(_mm_add_epi32(sum128lo, sum128hi), bias);
            };

            [[maybe_unused]] auto m512_haddx8 = [m512_hadd128x16_interleave](__m512i sum0, __m512i sum1, __m512i sum2, __m512i sum3, __m512i sum4, __m512i sum5, __m512i sum6, __m512i sum7, __m256i bias) -> __m256i {

                __m512i const suma{ m512_hadd128x16_interleave(sum0, sum1, sum2, sum3) };
                __m512i const sumb{ m512_hadd128x16_interleave(sum4, sum5, sum6, sum7) };

                __m512i const indices0{ _mm512_setr_epi64(0, 1, 8, 9, 4, 5, 12, 13) };
                __m512i const indices1{ _mm512_setr_epi64(2, 3, 10, 11, 6, 7, 14, 15) };
                __m512i const x{ _mm512_add_epi32(_mm512_permutex2var_epi64(suma, indices0, sumb), _mm512_permutex2var_epi64(suma, indices1, sumb)) };

                __m256i const sum256lo{ _mm512_castsi512_si256(x) };
                __m256i const sum256hi{ _mm512_extracti64x4_epi64(x, 1) };

                return _mm256_add_epi32(_mm256_add_epi32(sum256lo, sum256hi), bias);
            };

            [[maybe_unused]] auto m512_hadd256x8 = [m512_hadd128x16_interleave](__m512i sum0, __m512i sum1, __m512i sum2, __m512i sum3, __m256i bias) -> __m256i {

                __m512i sum{ m512_hadd128x16_interleave(sum0, sum1, sum2, sum3) };

                __m512i const indices{ _mm512_setr_epi32(0, 4, 8, 12, 2, 6, 10, 14, 1, 5, 9, 13, 3, 7, 11, 15) };
                sum = _mm512_permutexvar_epi32(indices, sum);

                __m256i const sum256lo{ _mm512_castsi512_si256(sum) };
                __m256i const sum256hi{ _mm512_extracti64x4_epi64(sum, 1) };

                return _mm256_add_epi32(_mm256_hadd_epi32(sum256lo, sum256hi), bias);
            };

            [[maybe_unused]] auto m512_hadd256x16 = [m512_hadd128x16_interleave](__m512i sum0, __m512i sum1, __m512i sum2, __m512i sum3, __m512i sum4, __m512i sum5, __m512i sum6, __m512i sum7, __m512i bias) -> __m512i {

                __m512i const suma{ m512_hadd128x16_interleave(sum0, sum1, sum2, sum3) };
                __m512i const sumb{ m512_hadd128x16_interleave(sum4, sum5, sum6, sum7) };

                __m512i const indices0{ _mm512_setr_epi64(0, 1, 8, 9, 4, 5, 12, 13) };
                __m512i const indices1{ _mm512_setr_epi64(2, 3, 10, 11, 6, 7, 14, 15) };
                __m512i const x{ _mm512_add_epi32(_mm512_permutex2var_epi64(suma, indices0, sumb), _mm512_permutex2var_epi64(suma, indices1, sumb)) };

                __m512i indices{ _mm512_setr_epi32(0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15) };
                return _mm512_add_epi32(_mm512_permutexvar_epi32(indices, x), bias);
            };

        #if defined(USE_VNNI)
            [[maybe_unused]] auto m512_add_dpbusd_epi32 = [=](__m512i &acc, __m512i a, __m512i b) {
                acc = _mm512_dpbusd_epi32(acc, a, b);
            };
        #else
            [[maybe_unused]] auto m512_dpbusd_epi32 = [=](__m512i a, __m512i b) -> __m512i {
                __m512i product0{ _mm512_maddubs_epi16(a, b) };
                return _mm512_madd_epi16(product0, One512);
            };
        #endif

    #endif
    #if defined(USE_AVX2)

            [[maybe_unused]] __m256i const One256{ _mm256_set1_epi16(1) };

            [[maybe_unused]] auto m256_hadd = [](__m256i sum, int bias) -> int {
                __m128i sum128{ _mm_add_epi32(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1)) };
                sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_BADC));
                sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_PERM_CDAB));
                return _mm_cvtsi128_si32(sum128) + bias;
            };

            [[maybe_unused]] auto m256_haddx4 = [](__m256i sum0, __m256i sum1, __m256i sum2, __m256i sum3, __m128i bias) -> __m128i {
                sum0 = _mm256_hadd_epi32(sum0, sum1);
                sum2 = _mm256_hadd_epi32(sum2, sum3);

                sum0 = _mm256_hadd_epi32(sum0, sum2);

                __m128i const sum128lo{ _mm256_castsi256_si128(sum0) };
                __m128i const sum128hi{ _mm256_extracti128_si256(sum0, 1) };

                return _mm_add_epi32(_mm_add_epi32(sum128lo, sum128hi), bias);
            };

        #if defined(USE_VNNI)
            [[maybe_unused]] auto m256_add_dpbusd_epi32 = [=](__m256i &acc, __m256i a, __m256i b) {
                acc = _mm256_dpbusd_epi32(acc, a, b);
            };
        #else
            [[maybe_unused]] auto m256_dpbusd_epi32 = [=](__m256i a, __m256i b) -> __m256i {
                __m256i product0{ _mm256_maddubs_epi16(a, b) };
                return _mm256_madd_epi16(product0, One256);
            };
        #endif

    #endif
    #if defined(USE_SSSE3)

            [[maybe_unused]] __m128i const One128{ _mm_set1_epi16(1) };

            [[maybe_unused]] auto m128_hadd = [](__m128i sum, int bias) -> int {
                sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0x4E)); //_MM_PERM_BADC
                sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0xB1)); //_MM_PERM_CDAB
                return _mm_cvtsi128_si32(sum) + bias;
            };

            [[maybe_unused]] auto m128_haddx4 = [](__m128i sum0, __m128i sum1, __m128i sum2, __m128i sum3, __m128i bias) -> __m128i {
                sum0 = _mm_hadd_epi32(sum0, sum1);
                sum2 = _mm_hadd_epi32(sum2, sum3);

                sum0 = _mm_hadd_epi32(sum0, sum2);

                return _mm_add_epi32(sum0, bias);
            };

            [[maybe_unused]] auto m128_dpbusd_epi32 = [=](__m128i a, __m128i b) -> __m128i {
                __m128i product0{ _mm_maddubs_epi16(a, b) };
                return _mm_madd_epi16(product0, One128);
            };

    #endif

    #if defined(USE_AVX512)

            constexpr IndexType NumChunks512{ PaddedInputDimensions / (SimdWidth * 2) };
            constexpr IndexType NumChunks256{ PaddedInputDimensions / SimdWidth };

            auto const output{ reinterpret_cast<OutputType*>(buffer) };

            // Since to saturate a zmm register it takes 64 bytes we
            // cannot use AVX512 for the smaller affine transforms.
            // Instead we fallback to a AVX2 implementation if the
            // InputDimensions isn't a multiple of 64.
            // Note that this means that for example for
            // InputDimensions of 96 we fallback to AVX2 even though
            // the first 64 elements could be processed with AVX512.
            // This is caused by mixing the __m256 and __m512 variables
            // required to better handle that case and it would
            // require handling more cases statically not to lose performance.
            // This should be revisited if such input dimensions are to be considered.
            [[maybe_unused]] auto const input_vector512{ reinterpret_cast<__m512i const*>(input) };
            [[maybe_unused]] auto const input_vector256{ reinterpret_cast<__m256i const*>(input) };

            // OutputDimensions is either 1 or a multiple of SimdWidth
            // because then it is also an input dimension.
            if constexpr (OutputDimensions % 16 == 0 && NumChunks256 == 1) {
                for (IndexType i = 0; i < OutputDimensions; i += 16) {
                    IndexType const offset01a = (i + 0) * PaddedInputDimensions;
                    IndexType const offset23a = (i + 2) * PaddedInputDimensions;
                    IndexType const offset45a = (i + 4) * PaddedInputDimensions;
                    IndexType const offset67a = (i + 6) * PaddedInputDimensions;
                    IndexType const offset01b = (i + 8) * PaddedInputDimensions;
                    IndexType const offset23b = (i + 10) * PaddedInputDimensions;
                    IndexType const offset45b = (i + 12) * PaddedInputDimensions;
                    IndexType const offset67b = (i + 14) * PaddedInputDimensions;

                    __m512i const bias{ *reinterpret_cast<__m512i const*>(&biases_[i]) };
                    __m512i *outptr{ reinterpret_cast<__m512i*>(&output[i]) };

                    auto const row01a{ *reinterpret_cast<__m512i const*>(&weights_[offset01a]) };
                    auto const row23a{ *reinterpret_cast<__m512i const*>(&weights_[offset23a]) };
                    auto const row45a{ *reinterpret_cast<__m512i const*>(&weights_[offset45a]) };
                    auto const row67a{ *reinterpret_cast<__m512i const*>(&weights_[offset67a]) };
                    auto const row01b{ *reinterpret_cast<__m512i const*>(&weights_[offset01b]) };
                    auto const row23b{ *reinterpret_cast<__m512i const*>(&weights_[offset23b]) };
                    auto const row45b{ *reinterpret_cast<__m512i const*>(&weights_[offset45b]) };
                    auto const row67b{ *reinterpret_cast<__m512i const*>(&weights_[offset67b]) };

                    __m256i const in256{ input_vector256[0] };
                    __m512i const in{ _mm512_inserti64x4(_mm512_castsi256_si512(in256), in256, 1) };

                #if defined(USE_VNNI)
                    __m512i sum01a{ _mm512_setzero_si512() };
                    __m512i sum23a{ _mm512_setzero_si512() };
                    __m512i sum45a{ _mm512_setzero_si512() };
                    __m512i sum67a{ _mm512_setzero_si512() };
                    __m512i sum01b{ _mm512_setzero_si512() };
                    __m512i sum23b{ _mm512_setzero_si512() };
                    __m512i sum45b{ _mm512_setzero_si512() };
                    __m512i sum67b{ _mm512_setzero_si512() };

                    m512_add_dpbusd_epi32(sum01a, in, row01a);
                    m512_add_dpbusd_epi32(sum23a, in, row23a);
                    m512_add_dpbusd_epi32(sum45a, in, row45a);
                    m512_add_dpbusd_epi32(sum67a, in, row67a);
                    m512_add_dpbusd_epi32(sum01b, in, row01b);
                    m512_add_dpbusd_epi32(sum23b, in, row23b);
                    m512_add_dpbusd_epi32(sum45b, in, row45b);
                    m512_add_dpbusd_epi32(sum67b, in, row67b);

                #else
                    __m512i sum01a{ m512_dpbusd_epi32(in, row01a) };
                    __m512i sum23a{ m512_dpbusd_epi32(in, row23a) };
                    __m512i sum45a{ m512_dpbusd_epi32(in, row45a) };
                    __m512i sum67a{ m512_dpbusd_epi32(in, row67a) };
                    __m512i sum01b{ m512_dpbusd_epi32(in, row01b) };
                    __m512i sum23b{ m512_dpbusd_epi32(in, row23b) };
                    __m512i sum45b{ m512_dpbusd_epi32(in, row45b) };
                    __m512i sum67b{ m512_dpbusd_epi32(in, row67b) };

                #endif

                    *outptr = m512_hadd256x16(sum01a, sum23a, sum45a, sum67a, sum01b, sum23b, sum45b, sum67b, bias);
                }
            } else
            if constexpr (OutputDimensions % 4 == 0) {
                for (IndexType i = 0; i < OutputDimensions; i += 4) {
                    IndexType const offset0{ (i + 0) * PaddedInputDimensions };
                    IndexType const offset1{ (i + 1) * PaddedInputDimensions };
                    IndexType const offset2{ (i + 2) * PaddedInputDimensions };
                    IndexType const offset3{ (i + 3) * PaddedInputDimensions };

                    __m128i const bias{ *reinterpret_cast<__m128i const*>(&biases_[i]) };
                    __m128i *outptr{ reinterpret_cast<__m128i*>(&output[i]) };

                    if constexpr (PaddedInputDimensions % (SimdWidth * 2) == 0) {

                        auto const row0{ reinterpret_cast<__m512i const*>(&weights_[offset0]) };
                        auto const row1{ reinterpret_cast<__m512i const*>(&weights_[offset1]) };
                        auto const row2{ reinterpret_cast<__m512i const*>(&weights_[offset2]) };
                        auto const row3{ reinterpret_cast<__m512i const*>(&weights_[offset3]) };

                    #if defined(USE_VNNI)
                        __m512i sum0{ _mm512_setzero_si512() };
                        __m512i sum1{ _mm512_setzero_si512() };
                        __m512i sum2{ _mm512_setzero_si512() };
                        __m512i sum3{ _mm512_setzero_si512() };
                        const IndexType Start = 0;
                    #else
                        __m512i sum0{ m512_dpbusd_epi32(input_vector512[0], row0[0]) };
                        __m512i sum1{ m512_dpbusd_epi32(input_vector512[0], row1[0]) };
                        __m512i sum2{ m512_dpbusd_epi32(input_vector512[0], row2[0]) };
                        __m512i sum3{ m512_dpbusd_epi32(input_vector512[0], row3[0]) };
                        const IndexType Start = 1;
                    #endif

                        for (IndexType j = Start; j < NumChunks512; ++j) {
                            __m512i const in{ input_vector512[j] };

                        #if defined(USE_VNNI)
                            m512_add_dpbusd_epi32(sum0, in, row0[j]);
                            m512_add_dpbusd_epi32(sum1, in, row1[j]);
                            m512_add_dpbusd_epi32(sum2, in, row2[j]);
                            m512_add_dpbusd_epi32(sum3, in, row3[j]);
                        #else
                            sum0 = _mm512_add_epi32(sum0, m512_dpbusd_epi32(in, row0[j]));
                            sum1 = _mm512_add_epi32(sum1, m512_dpbusd_epi32(in, row1[j]));
                            sum2 = _mm512_add_epi32(sum2, m512_dpbusd_epi32(in, row2[j]));
                            sum3 = _mm512_add_epi32(sum3, m512_dpbusd_epi32(in, row3[j]));
                        #endif
                        }

                        *outptr = m512_haddx4(sum0, sum1, sum2, sum3, bias);
                    } else {

                        auto const row0{ reinterpret_cast<__m256i const*>(&weights_[offset0]) };
                        auto const row1{ reinterpret_cast<__m256i const*>(&weights_[offset1]) };
                        auto const row2{ reinterpret_cast<__m256i const*>(&weights_[offset2]) };
                        auto const row3{ reinterpret_cast<__m256i const*>(&weights_[offset3]) };

                    #if defined(USE_VNNI)
                        __m256i sum0{ _mm256_setzero_si256() };
                        __m256i sum1{ _mm256_setzero_si256() };
                        __m256i sum2{ _mm256_setzero_si256() };
                        __m256i sum3{ _mm256_setzero_si256() };
                        const IndexType Start = 0;
                    #else
                        __m256i sum0{ m256_dpbusd_epi32(input_vector256[0], row0[0]) };
                        __m256i sum1{ m256_dpbusd_epi32(input_vector256[0], row1[0]) };
                        __m256i sum2{ m256_dpbusd_epi32(input_vector256[0], row2[0]) };
                        __m256i sum3{ m256_dpbusd_epi32(input_vector256[0], row3[0]) };
                        const IndexType Start = 1;
                    #endif

                        for (IndexType j = Start; j < NumChunks256; ++j) {
                            __m256i const in{ input_vector256[j] };

                        #if defined(USE_VNNI)
                            m256_add_dpbusd_epi32(sum0, in, row0[j]);
                            m256_add_dpbusd_epi32(sum1, in, row1[j]);
                            m256_add_dpbusd_epi32(sum2, in, row2[j]);
                            m256_add_dpbusd_epi32(sum3, in, row3[j]);
                        #else
                            sum0 = _mm256_add_epi32(sum0, m256_dpbusd_epi32(in, row0[j]));
                            sum1 = _mm256_add_epi32(sum1, m256_dpbusd_epi32(in, row1[j]));
                            sum2 = _mm256_add_epi32(sum2, m256_dpbusd_epi32(in, row2[j]));
                            sum3 = _mm256_add_epi32(sum3, m256_dpbusd_epi32(in, row3[j]));
                        #endif
                        }

                        *outptr = m256_haddx4(sum0, sum1, sum2, sum3, bias);
                    }
                }
            } else if constexpr (OutputDimensions == 1) {
                if constexpr (PaddedInputDimensions % (SimdWidth * 2) == 0) {
                    __m512i sum0{ _mm512_setzero_si512() };

                    auto const row0{ reinterpret_cast<__m512i const*>(&weights_[0]) };

                #if defined(USE_VNNI)
                    __m512i sum0{ _mm512_setzero_si512() };
                    const IndexType Start = 0;
                #else
                    __m512i sum0{ m512_dpbusd_epi32(input_vector512[0], row0[0]) };
                    const IndexType Start = 1;
                #endif

                    for (IndexType j = Start; j < NumChunks512; ++j) {
                        __m512i const in{ input_vector512[j] };

                    #if defined(USE_VNNI)
                        m512_add_dpbusd_epi32(sum0, in, row0[j]);
                    #else
                        sum0 = _mm512_add_epi32(sum0, m512_dpbusd_epi32(in, row0[j]));
                    #endif
                    }

                    output[0] = m512_hadd(sum0, biases_[0]);
                } else {

                    auto const row0{ reinterpret_cast<__m256i const*>(&weights_[0]) };

                #if defined(USE_VNNI)
                    __m256i sum0{ _mm256_setzero_si256() };
                    const IndexType Start = 0;
                #else
                    __m256i sum0{ m256_dpbusd_epi32(input_vector256[0], row0[0]) };
                    const IndexType Start = 1;
                #endif

                    for (IndexType j = Start; j < NumChunks256; ++j) {
                        __m256i const in{ input_vector256[j] };

                    #if defined(USE_VNNI)
                        m256_add_dpbusd_epi32(sum0, in, row0[j]);
                    #else
                        sum0 = _mm256_add_epi32(sum0, m256_dpbusd_epi32(in, row0[j]));
                    #endif
                    }

                    output[0] = m256_hadd(sum0, biases_[0]);
                }
            } else {
                // This case can never happen because OutputDimensions
                // is always 1 or a multiple of SimdWidth.
                assert(false);
            }

    #elif defined(USE_AVX2)

            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };

            auto const output{ reinterpret_cast<OutputType*>(buffer) };
            auto const input_vector{ reinterpret_cast<__m256i const*>(input) };

            // OutputDimensions is either 1 or a multiple of SimdWidth
            // because then it is also an input dimension.
            if constexpr (OutputDimensions % 4 == 0) {
                for (IndexType i = 0; i < OutputDimensions; i += 4) {
                    IndexType const offset0{ (i + 0) * PaddedInputDimensions };
                    IndexType const offset1{ (i + 1) * PaddedInputDimensions };
                    IndexType const offset2{ (i + 2) * PaddedInputDimensions };
                    IndexType const offset3{ (i + 3) * PaddedInputDimensions };

                    __m128i const bias{ *reinterpret_cast<__m128i const*>(&biases_[i]) };
                    __m128i *outptr{ reinterpret_cast<__m128i*>(&output[i]) };

                    auto const row0{ reinterpret_cast<__m256i const*>(&weights_[offset0]) };
                    auto const row1{ reinterpret_cast<__m256i const*>(&weights_[offset1]) };
                    auto const row2{ reinterpret_cast<__m256i const*>(&weights_[offset2]) };
                    auto const row3{ reinterpret_cast<__m256i const*>(&weights_[offset3]) };

                #if defined(USE_VNNI)
                    __m256i sum0{ _mm256_setzero_si256() };
                    __m256i sum1{ _mm256_setzero_si256() };
                    __m256i sum2{ _mm256_setzero_si256() };
                    __m256i sum3{ _mm256_setzero_si256() };
                    const IndexType Start = 0;
                #else
                    __m256i sum0{ m256_dpbusd_epi32(input_vector[0], row0[0]) };
                    __m256i sum1{ m256_dpbusd_epi32(input_vector[0], row1[0]) };
                    __m256i sum2{ m256_dpbusd_epi32(input_vector[0], row2[0]) };
                    __m256i sum3{ m256_dpbusd_epi32(input_vector[0], row3[0]) };
                    const IndexType Start = 1;
                #endif

                    for (IndexType j = Start; j < NumChunks; ++j) {
                        __m256i const in{ input_vector[j] };

                    #if defined(USE_VNNI)
                        m256_add_dpbusd_epi32(sum0, in, row0[j]);
                        m256_add_dpbusd_epi32(sum1, in, row1[j]);
                        m256_add_dpbusd_epi32(sum2, in, row2[j]);
                        m256_add_dpbusd_epi32(sum3, in, row3[j]);
                    #else
                        sum0 = _mm256_add_epi32(sum0, m256_dpbusd_epi32(in, row0[j]));
                        sum1 = _mm256_add_epi32(sum1, m256_dpbusd_epi32(in, row1[j]));
                        sum2 = _mm256_add_epi32(sum2, m256_dpbusd_epi32(in, row2[j]));
                        sum3 = _mm256_add_epi32(sum3, m256_dpbusd_epi32(in, row3[j]));
                    #endif
                    }

                    *outptr = m256_haddx4(sum0, sum1, sum2, sum3, bias);
                }
            } else if constexpr (OutputDimensions == 1) {

                auto const row0{ reinterpret_cast<__m256i const*>(&weights_[0]) };

            #if defined(USE_VNNI)
                __m256i sum0{ _mm256_setzero_si256() };
                const IndexType Start = 0;
            #else
                __m256i sum0{ m256_dpbusd_epi32(input_vector[0], row0[0]) };
                const IndexType Start = 1;
            #endif

                for (IndexType j = Start; j < NumChunks; ++j) {
                    __m256i const in{ input_vector[j] };

                #if defined(USE_VNNI)
                    m256_add_dpbusd_epi32(sum0, in, row0[j]);
                #else
                    sum0 = _mm256_add_epi32(sum0, m256_dpbusd_epi32(in, row0[j]));
                #endif
                }

                output[0] = m256_hadd(sum0, biases_[0]);
            } else {
                // This case can never happen because OutputDimensions
                // is always 1 or a multiple of SimdWidth.
                assert(false);
            }

    #elif defined(USE_SSSE3)

            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };

            auto output{ reinterpret_cast<OutputType*>(buffer) };
            auto const input_vector{ reinterpret_cast<__m128i const*>(input) };

            // OutputDimensions is either 1 or a multiple of SimdWidth
            // because then it is also an input dimension.
            if constexpr (OutputDimensions % 4 == 0) {
                for (IndexType i = 0; i < OutputDimensions; i += 4) {
                    IndexType const offset0{ (i + 0) * PaddedInputDimensions };
                    IndexType const offset1{ (i + 1) * PaddedInputDimensions };
                    IndexType const offset2{ (i + 2) * PaddedInputDimensions };
                    IndexType const offset3{ (i + 3) * PaddedInputDimensions };

                    __m128i const bias{ *reinterpret_cast<__m128i const*>(&biases_[i]) };
                    __m128i *outptr{ reinterpret_cast<__m128i*>(&output[i]) };

                    auto const row0{ reinterpret_cast<__m128i const*>(&weights_[offset0]) };
                    auto const row1{ reinterpret_cast<__m128i const*>(&weights_[offset1]) };
                    auto const row2{ reinterpret_cast<__m128i const*>(&weights_[offset2]) };
                    auto const row3{ reinterpret_cast<__m128i const*>(&weights_[offset3]) };

                    __m128i sum0{ _mm_setzero_si128() };
                    __m128i sum1{ _mm_setzero_si128() };
                    __m128i sum2{ _mm_setzero_si128() };
                    __m128i sum3{ _mm_setzero_si128() };
                    for (int j = 1; j < (int)NumChunks; ++j) {
                        __m128i const in{ input_vector[j] };

                        sum0 = _mm_add_epi32(sum0, m128_dpbusd_epi32(in, row0[j]));
                        sum1 = _mm_add_epi32(sum1, m128_dpbusd_epi32(in, row1[j]));
                        sum2 = _mm_add_epi32(sum2, m128_dpbusd_epi32(in, row2[j]));
                        sum3 = _mm_add_epi32(sum3, m128_dpbusd_epi32(in, row3[j]));
                    }

                    *outptr = m128_haddx4(sum0, sum1, sum2, sum3, bias);
                }
            } else if constexpr (OutputDimensions == 1) {

                auto const row0{ reinterpret_cast<__m128i const*>(&weights_[0]) };

                __m128i sum0{ _mm_setzero_si128() };

                for (int j = 1; j < (int)NumChunks; ++j) {
                    sum0 = _mm_add_epi32(sum0, m128_dpbusd_epi32(input_vector[j], row0[j]));
                }

                output[0] = m128_hadd(sum0, biases_[0]);
            } else {
                // This case can never happen because OutputDimensions
                // is always 1 or a multiple of SimdWidth.
                assert(false);
            }

    #else

            // Use old implementation for the other architectures.

            auto output{ reinterpret_cast<OutputType*>(buffer) };

        #if defined(USE_SSE2)
            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
            #if !defined(USE_SSSE3)
            __m128i const Zero{ _mm_setzero_si128() };
            #endif
            auto const input_vector{ reinterpret_cast<__m128i const*>(input) };

        #elif defined(USE_MMX)
            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
            __m64 const Zero = _mm_setzero_si64();
            auto const input_vector{ reinterpret_cast<__m64 const*>(input) };

        #elif defined(USE_NEON)
            constexpr IndexType NumChunks{ PaddedInputDimensions / SimdWidth };
            auto const input_vector{ reinterpret_cast<int8x8_t const*>(input) };

        #endif

            for (IndexType i = 0; i < OutputDimensions; ++i) {
                IndexType const offset = i * PaddedInputDimensions;

        #if defined(USE_SSE2)
                __m128i sum_lo = _mm_cvtsi32_si128(biases_[i]);
                __m128i sum_hi = Zero;
                auto const row{ reinterpret_cast<__m128i const*>(&weights_[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m128i row_j = _mm_load_si128(&row[j]);
                    __m128i input_j = _mm_load_si128(&input_vector[j]);
                    __m128i row_signs = _mm_cmpgt_epi8(Zero, row_j);
                    __m128i extended_row_lo = _mm_unpacklo_epi8(row_j, row_signs);
                    __m128i extended_row_hi = _mm_unpackhi_epi8(row_j, row_signs);
                    __m128i extended_input_lo = _mm_unpacklo_epi8(input_j, Zero);
                    __m128i extended_input_hi = _mm_unpackhi_epi8(input_j, Zero);
                    __m128i product_lo = _mm_madd_epi16(extended_row_lo, extended_input_lo);
                    __m128i product_hi = _mm_madd_epi16(extended_row_hi, extended_input_hi);
                    sum_lo = _mm_add_epi32(sum_lo, product_lo);
                    sum_hi = _mm_add_epi32(sum_hi, product_hi);
                }
                __m128i sum = _mm_add_epi32(sum_lo, sum_hi);
                __m128i sum_high_64 = _mm_shuffle_epi32(sum, _MM_SHUFFLE(1, 0, 3, 2));
                sum = _mm_add_epi32(sum, sum_high_64);
                __m128i sum_second_32 = _mm_shufflelo_epi16(sum, _MM_SHUFFLE(1, 0, 3, 2));
                sum = _mm_add_epi32(sum, sum_second_32);
                output[i] = _mm_cvtsi128_si32(sum);

        #elif defined(USE_MMX)
                __m64 sum_lo = _mm_cvtsi32_si64(biases_[i]);
                __m64 sum_hi = Zero;
                auto const row{ reinterpret_cast<__m64 const*>(&weights_[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    __m64 row_j = row[j];
                    __m64 input_j = input_vector[j];
                    __m64 row_signs = _mm_cmpgt_pi8(Zero, row_j);
                    __m64 extended_row_lo = _mm_unpacklo_pi8(row_j, row_signs);
                    __m64 extended_row_hi = _mm_unpackhi_pi8(row_j, row_signs);
                    __m64 extended_input_lo = _mm_unpacklo_pi8(input_j, Zero);
                    __m64 extended_input_hi = _mm_unpackhi_pi8(input_j, Zero);
                    __m64 product_lo = _mm_madd_pi16(extended_row_lo, extended_input_lo);
                    __m64 product_hi = _mm_madd_pi16(extended_row_hi, extended_input_hi);
                    sum_lo = _mm_add_pi32(sum_lo, product_lo);
                    sum_hi = _mm_add_pi32(sum_hi, product_hi);
                }
                __m64 sum = _mm_add_pi32(sum_lo, sum_hi);
                sum = _mm_add_pi32(sum, _mm_unpackhi_pi32(sum, sum));
                output[i] = _mm_cvtsi64_si32(sum);

        #elif defined(USE_NEON)
                int32x4_t sum = { biases_[i] };
                auto const row{ reinterpret_cast<int8x8_t const*>(&weights_[offset]) };
                for (IndexType j = 0; j < NumChunks; ++j) {
                    int16x8_t product = vmull_s8(input_vector[j * 2], row[j * 2]);
                    product = vmlal_s8(product, input_vector[j * 2 + 1], row[j * 2 + 1]);
                    sum = vpadalq_s16(sum, product);
                }
                output[i] = sum[0] + sum[1] + sum[2] + sum[3];

        #else
                OutputType sum = biases_[i];
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

        PreviousLayer _previousLayer;

        alignas(CacheLineSize) BiasType biases_[OutputDimensions];
        alignas(CacheLineSize) WeightType weights_[OutputDimensions * PaddedInputDimensions];
    };

}
