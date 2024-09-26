/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <utility>

#include "../position.h"
#include "../types.h"
#include "nnue_accumulator.h"
#include "nnue_architecture.h"
#include "nnue_common.h"

namespace DON::Eval::NNUE {

// A class that converts the input features of the NNUE evaluation function

using BiasType       = std::int16_t;
using WeightType     = std::int16_t;
using PSQTWeightType = std::int32_t;

// If vector instructions are enabled, update and refresh the
// accumulator tile by tile such that each tile fits in the CPU's
// vector registers.
#define VECTOR

static_assert(PSQTBuckets % 8 == 0,
              "Per feature PSQT values cannot be processed at granularity lower than 8 at a time.");

#if defined(USE_AVX512)
using vec_t      = __m512i;
using psqt_vec_t = __m256i;
    #define vec_load(a) _mm512_load_si512(a)
    #define vec_store(a, b) _mm512_store_si512(a, b)
    #define vec_add_16(a, b) _mm512_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm512_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm512_mulhi_epi16(a, b)
    #define vec_zero() _mm512_setzero_epi32()
    #define vec_set_16(a) _mm512_set1_epi16(a)
    #define vec_max_16(a, b) _mm512_max_epi16(a, b)
    #define vec_min_16(a, b) _mm512_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm512_slli_epi16(a, b)
    // Inverse permuted at load time
    #define vec_packus_16(a, b) _mm512_packus_epi16(a, b)
    #define vec_load_psqt(a) _mm256_load_si256(a)
    #define vec_store_psqt(a, b) _mm256_store_si256(a, b)
    #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
    #define vec_zero_psqt() _mm256_setzero_si256()
    #define SIMDRegisterCount 16
    #define MaxChunkSize 64

#elif defined(USE_AVX2)
using vec_t      = __m256i;
using psqt_vec_t = __m256i;
    #define vec_load(a) _mm256_load_si256(a)
    #define vec_store(a, b) _mm256_store_si256(a, b)
    #define vec_add_16(a, b) _mm256_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm256_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm256_mulhi_epi16(a, b)
    #define vec_zero() _mm256_setzero_si256()
    #define vec_set_16(a) _mm256_set1_epi16(a)
    #define vec_max_16(a, b) _mm256_max_epi16(a, b)
    #define vec_min_16(a, b) _mm256_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm256_slli_epi16(a, b)
    // Inverse permuted at load time
    #define vec_packus_16(a, b) _mm256_packus_epi16(a, b)
    #define vec_load_psqt(a) _mm256_load_si256(a)
    #define vec_store_psqt(a, b) _mm256_store_si256(a, b)
    #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
    #define vec_zero_psqt() _mm256_setzero_si256()
    #define SIMDRegisterCount 16
    #define MaxChunkSize 32

#elif defined(USE_SSE2)
using vec_t      = __m128i;
using psqt_vec_t = __m128i;
    #define vec_load(a) (*(a))
    #define vec_store(a, b) *(a) = (b)
    #define vec_add_16(a, b) _mm_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm_sub_epi16(a, b)
    #define vec_mulhi_16(a, b) _mm_mulhi_epi16(a, b)
    #define vec_zero() _mm_setzero_si128()
    #define vec_set_16(a) _mm_set1_epi16(a)
    #define vec_max_16(a, b) _mm_max_epi16(a, b)
    #define vec_min_16(a, b) _mm_min_epi16(a, b)
    #define vec_slli_16(a, b) _mm_slli_epi16(a, b)
    #define vec_packus_16(a, b) _mm_packus_epi16(a, b)
    #define vec_load_psqt(a) (*(a))
    #define vec_store_psqt(a, b) *(a) = (b)
    #define vec_add_psqt_32(a, b) _mm_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm_sub_epi32(a, b)
    #define vec_zero_psqt() _mm_setzero_si128()
    #if defined(IS_64BIT)
        #define SIMDRegisterCount 16
    #else
        #define SIMDRegisterCount 8
    #endif
    #define MaxChunkSize 16

#elif defined(USE_NEON)
using vec_t      = int16x8_t;
using psqt_vec_t = int32x4_t;
    #define vec_load(a) (*(a))
    #define vec_store(a, b) *(a) = (b)
    #define vec_add_16(a, b) vaddq_s16(a, b)
    #define vec_sub_16(a, b) vsubq_s16(a, b)
    #define vec_mulhi_16(a, b) vqdmulhq_s16(a, b)
    #define vec_zero() \
        vec_t { 0 }
    #define vec_set_16(a) vdupq_n_s16(a)
    #define vec_max_16(a, b) vmaxq_s16(a, b)
    #define vec_min_16(a, b) vminq_s16(a, b)
    #define vec_slli_16(a, b) vshlq_s16(a, vec_set_16(b))
    #define vec_packus_16(a, b) reinterpret_cast<vec_t>(vcombine_u8(vqmovun_s16(a), vqmovun_s16(b)))
    #define vec_load_psqt(a) (*(a))
    #define vec_store_psqt(a, b) *(a) = (b)
    #define vec_add_psqt_32(a, b) vaddq_s32(a, b)
    #define vec_sub_psqt_32(a, b) vsubq_s32(a, b)
    #define vec_zero_psqt() \
        psqt_vec_t { 0 }
    #define SIMDRegisterCount 16
    #define MaxChunkSize 16

#else
    #undef VECTOR

#endif


#if defined(VECTOR)

    // Compute optimal SIMD register count for feature transformer accumulation.

    // Use __m* types as template arguments, which causes GCC to emit warnings
    // about losing some attribute information. This is irrelevant to us as
    // only take their size, so the following pragma are harmless.
    #if defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wignored-attributes"
    #endif

namespace {
template<typename SIMDRegisterType, typename LaneType, int LaneCount, int MaxRegister>
constexpr int best_register_count() noexcept {
    #define RegisterSize sizeof(SIMDRegisterType)
    #define LaneSize sizeof(LaneType)

    static_assert(RegisterSize >= LaneSize);
    static_assert(MaxRegister <= SIMDRegisterCount);
    static_assert(MaxRegister > 0);
    static_assert(SIMDRegisterCount > 0);
    static_assert(RegisterSize % LaneSize == 0);
    static_assert((LaneCount * LaneSize) % RegisterSize == 0);

    int ideal = (LaneCount * LaneSize) / RegisterSize;
    if (ideal <= MaxRegister)
        return ideal;

    // Look for the largest divisor of the ideal register count that is smaller than MaxRegister
    for (int divisor = MaxRegister; divisor > 1; --divisor)
        if (ideal % divisor == 0)
            return divisor;

    return 1;
}
}  // namespace

    #if defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif
#endif

// Input feature converter
template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> State::*accPtr>
class FeatureTransformer final {

    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

   private:
#if defined(VECTOR)
    static constexpr int REG_COUNT =
      best_register_count<vec_t, WeightType, TransformedFeatureDimensions, SIMDRegisterCount>();
    static constexpr int PSQT_REG_COUNT =
      best_register_count<psqt_vec_t, PSQTWeightType, PSQTBuckets, SIMDRegisterCount>();

    static constexpr IndexType TILE_HEIGHT      = REG_COUNT * sizeof(vec_t) / 2;
    static constexpr IndexType PSQT_TILE_HEIGHT = PSQT_REG_COUNT * sizeof(psqt_vec_t) / 4;
    static_assert(HalfDimensions % TILE_HEIGHT == 0, "TILE_HEIGHT must divide HalfDimensions");
    static_assert(PSQTBuckets % PSQT_TILE_HEIGHT == 0, "PSQT_TILE_HEIGHT must divide PSQTBuckets");
#endif

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = FeatureSet::Dimensions;
    static constexpr IndexType OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr std::size_t BUFFER_SIZE = OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() noexcept {
        return FeatureSet::HASH_VALUE ^ (2 * OutputDimensions);
    }

    static constexpr void order_packs([[maybe_unused]] std::uint64_t* v) {
#if defined(USE_AVX512)  // _mm512_packs_epi16 ordering
        std::uint64_t tmp0 = v[2], tmp1 = v[3];
        v[2] = v[8], v[3] = v[9];
        v[8] = v[4], v[9] = v[5];
        v[4] = tmp0, v[5] = tmp1;
        tmp0 = v[6], tmp1 = v[7];
        v[6] = v[10], v[7] = v[11];
        v[10] = v[12], v[11] = v[13];
        v[12] = tmp0, v[13] = tmp1;
#elif defined(USE_AVX2)  // _mm256_packs_epi16 ordering
        std::swap(v[2], v[4]);
        std::swap(v[3], v[5]);
#endif
    }

    static constexpr void inverse_order_packs([[maybe_unused]] std::uint64_t* v) {
#if defined(USE_AVX512)  // Inverse _mm512_packs_epi16 ordering
        std::uint64_t tmp0 = v[2], tmp1 = v[3];
        v[2] = v[4], v[3] = v[5];
        v[4] = v[8], v[5] = v[9];
        v[8] = tmp0, v[9] = tmp1;
        tmp0 = v[6], tmp1 = v[7];
        v[6] = v[12], v[7] = v[13];
        v[12] = v[10], v[13] = v[11];
        v[10] = tmp0, v[11] = tmp1;
#elif defined(USE_AVX2)  // Inverse _mm256_packs_epi16 ordering
        std::swap(v[2], v[4]);
        std::swap(v[3], v[5]);
#endif
    }

    void permute_weights([[maybe_unused]] void (*order_fn)(uint64_t*)) const {
#if defined(USE_AVX2)
        constexpr IndexType DI =
    #if defined(USE_AVX512)
          16;
    #else
          8;
    #endif

        std::uint64_t* b = reinterpret_cast<std::uint64_t*>(const_cast<BiasType*>(&biases[0]));
        for (IndexType i = 0; i < HalfDimensions * sizeof(BiasType) / sizeof(std::uint64_t);
             i += DI)
            order_fn(&b[i]);

        for (IndexType j = 0; j < InputDimensions; ++j)
        {
            std::uint64_t* w = reinterpret_cast<std::uint64_t*>(
              const_cast<WeightType*>(&weights[j * HalfDimensions]));
            for (IndexType i = 0; i < HalfDimensions * sizeof(WeightType) / sizeof(std::uint64_t);
                 i += DI)
                order_fn(&w[i]);
        }
#endif
    }

    inline void scale_weights(bool read) const {
        for (IndexType j = 0; j < InputDimensions; ++j)
        {
            WeightType* w = const_cast<WeightType*>(&weights[j * HalfDimensions]);
            for (IndexType i = 0; i < HalfDimensions; ++i)
                w[i] = read ? w[i] * 2 : w[i] / 2;
        }

        BiasType* b = const_cast<BiasType*>(biases);
        for (IndexType i = 0; i < HalfDimensions; ++i)
            b[i] = read ? b[i] * 2 : b[i] / 2;
    }

    // Read network parameters
    bool read_parameters(std::istream& istream) noexcept {

        read_leb_128<BiasType>(istream, biases, HalfDimensions);
        read_leb_128<WeightType>(istream, weights, HalfDimensions * InputDimensions);
        read_leb_128<PSQTWeightType>(istream, psqtWeights, PSQTBuckets * InputDimensions);

        permute_weights(inverse_order_packs);
        scale_weights(true);
        return !istream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& ostream) const noexcept {

        permute_weights(order_packs);
        scale_weights(false);

        write_leb_128<BiasType>(ostream, biases, HalfDimensions);
        write_leb_128<WeightType>(ostream, weights, HalfDimensions * InputDimensions);
        write_leb_128<PSQTWeightType>(ostream, psqtWeights, PSQTBuckets * InputDimensions);

        permute_weights(inverse_order_packs);
        scale_weights(true);
        return !ostream.fail();
    }

    // Convert input features
    std::int32_t transform(const Position&                           pos,
                           AccumulatorCaches::Cache<HalfDimensions>* cache,
                           OutputType*                               output,
                           int                                       bucket) const noexcept {
        update_accumulator<WHITE>(pos, cache);
        update_accumulator<BLACK>(pos, cache);

        const Color perspectives[COLOR_NB] = {pos.active_color(), ~pos.active_color()};
        const auto& psqtAccumulation       = (pos.state()->*accPtr).psqtAccumulation;

        std::int32_t psqt = (psqtAccumulation[perspectives[WHITE]][bucket]
                             - psqtAccumulation[perspectives[BLACK]][bucket])
                          / 2;

        const auto& accumulation = (pos.state()->*accPtr).accumulation;

        for (IndexType p = 0; p < 2; ++p)
        {
            IndexType offset = (HalfDimensions / 2) * p;

#if defined(VECTOR)
            constexpr IndexType OutputChunkSize = MaxChunkSize;
            static_assert((HalfDimensions / 2) % OutputChunkSize == 0);
            constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

            vec_t Zero = vec_zero();
            vec_t One  = vec_set_16(127 * 2);

            auto in0 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][0]));
            auto in1 =
              reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
            auto out = reinterpret_cast<vec_t*>(output + offset);

            // Per the NNUE architecture, here we want to multiply pairs of
            // clipped elements and divide the product by 128. To do this,
            // we can naively perform min/max operation to clip each of the
            // four int16 vectors, mullo pairs together, then pack them into
            // one int8 vector. However, there exists a faster way.

            // The idea here is to use the implicit clipping from packus to
            // save us two vec_max_16 instructions. This clipping works due
            // to the fact that any int16 integer below zero will be zeroed
            // on packus.

            // Consider the case where the second element is negative.
            // If we do standard clipping, that element will be zero, which
            // means our pairwise product is zero. If we perform packus and
            // remove the lower-side clip for the second element, then our
            // product before packus will be negative, and is zeroed on pack.
            // The two operation produce equivalent results, but the second
            // one (using packus) saves one max operation per pair.

            // But here we run into a problem: mullo does not preserve the
            // sign of the multiplication. We can get around this by doing
            // mulhi, which keeps the sign. But that requires an additional
            // tweak.

            // mulhi cuts off the last 16 bits of the resulting product,
            // which is the same as performing a rightward shift of 16 bits.
            // We can use this to our advantage. Recall that we want to
            // divide the final product by 128, which is equivalent to a
            // 7-bit right shift. Intuitively, if we shift the clipped
            // value left by 9, and perform mulhi, which shifts the product
            // right by 16 bits, then we will net a right shift of 7 bits.
            // However, this won't work as intended. Since we clip the
            // values to have a maximum value of 127, shifting it by 9 bits
            // might occupy the signed bit, resulting in some positive
            // values being interpreted as negative after the shift.

            // There is a way, however, to get around this limitation. When
            // loading the network, scale accumulator weights and biases by
            // 2. To get the same pairwise multiplication result as before,
            // we need to divide the product by 128 * 2 * 2 = 512, which
            // amounts to a right shift of 9 bits. So now we only have to
            // shift left by 7 bits, perform mulhi (shifts right by 16 bits)
            // and net a 9 bit right shift. Since we scaled everything by
            // two, the values are clipped at 127 * 2 = 254, which occupies
            // 8 bits. Shifting it by 7 bits left will no longer occupy the
            // signed bit, so we are safe.

            // Note that on NEON processors, we shift left by 6 instead
            // because the instruction "vqdmulhq_s16" also doubles the
            // return value after the multiplication, adding an extra shift
            // to the left by 1, so we compensate by shifting less before
            // the multiplication.

            constexpr int shift =
    #if defined(USE_SSE2)
              7;
    #else
              6;
    #endif

            for (IndexType j = 0; j < NumOutputChunks; ++j)
            {
                vec_t sum0a = vec_slli_16(vec_max_16(vec_min_16(in0[j * 2 + 0], One), Zero), shift);
                vec_t sum0b = vec_slli_16(vec_max_16(vec_min_16(in0[j * 2 + 1], One), Zero), shift);
                vec_t sum1a = vec_min_16(in1[j * 2 + 0], One);
                vec_t sum1b = vec_min_16(in1[j * 2 + 1], One);

                vec_t pa = vec_mulhi_16(sum0a, sum1a);
                vec_t pb = vec_mulhi_16(sum0b, sum1b);

                out[j] = vec_packus_16(pa, pb);
            }
#else
            for (IndexType j = 0; j < HalfDimensions / 2; ++j)
            {
                BiasType sum0 = accumulation[static_cast<int>(perspectives[p])][j + 0];
                BiasType sum1 =
                  accumulation[static_cast<int>(perspectives[p])][j + HalfDimensions / 2];
                sum0 = std::clamp<BiasType>(sum0, 0, 127 * 2);
                sum1 = std::clamp<BiasType>(sum1, 0, 127 * 2);

                output[offset + j] = static_cast<OutputType>(unsigned(sum0 * sum1) / 512);
            }
#endif
        }

        return psqt;
    }

    void hint_common_access(const Position&                           pos,
                            AccumulatorCaches::Cache<HalfDimensions>* cache) const noexcept {
        hint_common_access_for_perspective<WHITE>(pos, cache);
        hint_common_access_for_perspective<BLACK>(pos, cache);
    }

   private:
    template<Color Perspective>
    static State* find_computed_accumulator(const Position& pos) noexcept {
        // Look for a usable accumulator of an earlier position. Keep track
        // of the estimated gain in terms of features to be added/subtracted.
        State* st   = pos.state();
        int    gain = FeatureSet::refresh_cost(pos);
        while (st->preState && !(st->*accPtr).computed[Perspective])
        {
            // This governs when a full feature refresh is needed and how many
            // updates are better than just one full refresh.
            if (FeatureSet::requires_refresh(st, Perspective)
                || (gain -= FeatureSet::update_cost(st) + 1) < 0)
                break;
            st = st->preState;
        }
        return st;
    }

    // It computes the accumulator of the next position, or updates the
    // current position's accumulator if CurrentOnly is true.
    template<Color Perspective, bool CurrentOnly>
    void update_accumulator_incremental(const Position& pos, State* computedState) const noexcept {
        assert((computedState->*accPtr).computed[Perspective]);
        assert(computedState->nxtState != nullptr);

#if defined(VECTOR)
        // Gcc-10.2 unnecessarily spills AVX2 registers if this array
        // is defined in the VECTOR code below, once in each branch.
        vec_t      acc[REG_COUNT];
        psqt_vec_t psqt[PSQT_REG_COUNT];
#endif

        Square ksq = pos.king_square(Perspective);

        // The size must be enough to contain the largest possible update.
        // That might depend on the feature set and generally relies on the
        // feature set's update cost calculation to be correct and never allow
        // updates with more added/removed features than MaxActiveDimensions.
        FeatureSet::IndexList removed, added;

        if constexpr (CurrentOnly)
            for (State* st = pos.state(); st != computedState; st = st->preState)
                FeatureSet::append_changed_indices<Perspective>(ksq, st->dirtyPiece, removed,
                                                                added);
        else
            FeatureSet::append_changed_indices<Perspective>(
              ksq, computedState->nxtState->dirtyPiece, removed, added);

        State* nxtState = CurrentOnly ? pos.state() : computedState->nxtState;
        assert(!(nxtState->*accPtr).computed[Perspective]);

#if defined(VECTOR)
        if ((removed.size() == 1 || removed.size() == 2) && added.size() == 1)
        {
            auto accIn = reinterpret_cast<const vec_t*>(
              &(computedState->*accPtr).accumulation[Perspective][0]);
            auto accOut =
              reinterpret_cast<vec_t*>(&(nxtState->*accPtr).accumulation[Perspective][0]);

            IndexType offsetR0 = HalfDimensions * removed[0];
            auto      columnR0 = reinterpret_cast<const vec_t*>(&weights[offsetR0]);
            IndexType offsetA  = HalfDimensions * added[0];
            auto      columnA  = reinterpret_cast<const vec_t*>(&weights[offsetA]);

            if (removed.size() == 1)
            {
                for (IndexType i = 0; i < HalfDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                    accOut[i] = vec_add_16(vec_sub_16(accIn[i], columnR0[i]), columnA[i]);
            }
            else
            {
                IndexType offsetR1 = HalfDimensions * removed[1];
                auto      columnR1 = reinterpret_cast<const vec_t*>(&weights[offsetR1]);

                for (IndexType i = 0; i < HalfDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                    accOut[i] = vec_sub_16(vec_add_16(accIn[i], columnA[i]),
                                           vec_add_16(columnR0[i], columnR1[i]));
            }

            auto accPsqtIn = reinterpret_cast<const psqt_vec_t*>(
              &(computedState->*accPtr).psqtAccumulation[Perspective][0]);
            auto accPsqtOut =
              reinterpret_cast<psqt_vec_t*>(&(nxtState->*accPtr).psqtAccumulation[Perspective][0]);

            const IndexType offsetPsqtR0 = PSQTBuckets * removed[0];
            auto columnPsqtR0 = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtR0]);
            const IndexType offsetPsqtA = PSQTBuckets * added[0];
            auto columnPsqtA = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtA]);

            if (removed.size() == 1)
            {
                for (std::size_t i = 0;
                     i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t); ++i)
                    accPsqtOut[i] = vec_add_psqt_32(vec_sub_psqt_32(accPsqtIn[i], columnPsqtR0[i]),
                                                    columnPsqtA[i]);
            }
            else
            {
                IndexType offsetPsqtR1 = PSQTBuckets * removed[1];
                auto columnPsqtR1 = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtR1]);

                for (std::size_t i = 0;
                     i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t); ++i)
                    accPsqtOut[i] =
                      vec_sub_psqt_32(vec_add_psqt_32(accPsqtIn[i], columnPsqtA[i]),
                                      vec_add_psqt_32(columnPsqtR0[i], columnPsqtR1[i]));
            }
        }
        else
        {
            for (IndexType i = 0; i < HalfDimensions / TILE_HEIGHT; ++i)
            {
                // Load accumulator
                auto accTileIn = reinterpret_cast<const vec_t*>(
                  &(computedState->*accPtr).accumulation[Perspective][i * TILE_HEIGHT]);
                for (IndexType j = 0; j < REG_COUNT; ++j)
                    acc[j] = vec_load(&accTileIn[j]);

                // Difference calculation for the deactivated features
                for (const auto index : removed)
                {
                    IndexType offset = HalfDimensions * index + i * TILE_HEIGHT;
                    auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);
                    for (IndexType j = 0; j < REG_COUNT; ++j)
                        acc[j] = vec_sub_16(acc[j], column[j]);
                }

                // Difference calculation for the activated features
                for (const auto index : added)
                {
                    IndexType offset = HalfDimensions * index + i * TILE_HEIGHT;
                    auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);
                    for (IndexType j = 0; j < REG_COUNT; ++j)
                        acc[j] = vec_add_16(acc[j], column[j]);
                }

                // Store accumulator
                auto accTileOut = reinterpret_cast<vec_t*>(
                  &(nxtState->*accPtr).accumulation[Perspective][i * TILE_HEIGHT]);
                for (IndexType j = 0; j < REG_COUNT; ++j)
                    vec_store(&accTileOut[j], acc[j]);
            }

            for (IndexType i = 0; i < PSQTBuckets / PSQT_TILE_HEIGHT; ++i)
            {
                // Load accumulator
                auto accTilePsqtIn = reinterpret_cast<const psqt_vec_t*>(
                  &(computedState->*accPtr).psqtAccumulation[Perspective][i * PSQT_TILE_HEIGHT]);
                for (std::size_t j = 0; j < PSQT_REG_COUNT; ++j)
                    psqt[j] = vec_load_psqt(&accTilePsqtIn[j]);

                // Difference calculation for the deactivated features
                for (const auto index : removed)
                {
                    IndexType offset = PSQTBuckets * index + i * PSQT_TILE_HEIGHT;
                    auto columnPsqt  = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
                    for (std::size_t j = 0; j < PSQT_REG_COUNT; ++j)
                        psqt[j] = vec_sub_psqt_32(psqt[j], columnPsqt[j]);
                }

                // Difference calculation for the activated features
                for (const auto index : added)
                {
                    IndexType offset = PSQTBuckets * index + i * PSQT_TILE_HEIGHT;
                    auto columnPsqt  = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
                    for (std::size_t j = 0; j < PSQT_REG_COUNT; ++j)
                        psqt[j] = vec_add_psqt_32(psqt[j], columnPsqt[j]);
                }

                // Store accumulator
                auto accTilePsqtOut = reinterpret_cast<psqt_vec_t*>(
                  &(nxtState->*accPtr).psqtAccumulation[Perspective][i * PSQT_TILE_HEIGHT]);
                for (std::size_t j = 0; j < PSQT_REG_COUNT; ++j)
                    vec_store_psqt(&accTilePsqtOut[j], psqt[j]);
            }
        }
#else
        std::memcpy((nxtState->*accPtr).accumulation[Perspective],
                    (computed->*accPtr).accumulation[Perspective],
                    HalfDimensions * sizeof(BiasType));
        std::memcpy((nxtState->*accPtr).psqtAccumulation[Perspective],
                    (computed->*accPtr).psqtAccumulation[Perspective],
                    PSQTBuckets * sizeof(PSQTWeightType));

        // Difference calculation for the deactivated features
        for (const auto index : removed)
        {
            const IndexType offset = HalfDimensions * index;
            for (IndexType i = 0; i < HalfDimensions; ++i)
                (nxtState->*accPtr).accumulation[Perspective][i] -= weights[offset + i];

            for (std::size_t i = 0; i < PSQTBuckets; ++i)
                (nxtState->*accPtr).psqtAccumulation[Perspective][i] -=
                  psqtWeights[index * PSQTBuckets + i];
        }

        // Difference calculation for the activated features
        for (const auto index : added)
        {
            const IndexType offset = HalfDimensions * index;
            for (IndexType i = 0; i < HalfDimensions; ++i)
                (nxtState->*accPtr).accumulation[Perspective][i] += weights[offset + i];

            for (std::size_t i = 0; i < PSQTBuckets; ++i)
                (nxtState->*accPtr).psqtAccumulation[Perspective][i] +=
                  psqtWeights[index * PSQTBuckets + i];
        }
#endif

        (nxtState->*accPtr).computed[Perspective] = true;

        if (!CurrentOnly && nxtState != pos.state())
            update_accumulator_incremental<Perspective, false>(pos, nxtState);
    }

    template<Color Perspective>
    void update_accumulator_refresh_cache(
      const Position& pos, AccumulatorCaches::Cache<HalfDimensions>* cache) const noexcept {
        assert(cache != nullptr);

        Square ksq = pos.king_square(Perspective);

        auto& entry = (*cache)[ksq][Perspective];

        FeatureSet::IndexList removed, added;

        for (Color c : {WHITE, BLACK})
        {
            for (PieceType pt = PAWN; pt <= KING; ++pt)
            {
                Piece    piece    = make_piece(c, pt);
                Bitboard oldBB    = entry.colorBB[c] & entry.typeBB[pt];
                Bitboard newBB    = pos.pieces(c, pt);
                Bitboard toRemove = oldBB & ~newBB;
                Bitboard toAdd    = newBB & ~oldBB;

                while (toRemove)
                {
                    Square sq = pop_lsb(toRemove);
                    removed.push_back(FeatureSet::make_index<Perspective>(sq, piece, ksq));
                }
                while (toAdd)
                {
                    Square sq = pop_lsb(toAdd);
                    added.push_back(FeatureSet::make_index<Perspective>(sq, piece, ksq));
                }
            }
        }

        auto& accumulator                 = pos.state()->*accPtr;
        accumulator.computed[Perspective] = true;

#if defined(VECTOR)
        vec_t      acc[REG_COUNT];
        psqt_vec_t psqt[PSQT_REG_COUNT];

        for (IndexType j = 0; j < HalfDimensions / TILE_HEIGHT; ++j)
        {
            auto accTile =
              reinterpret_cast<vec_t*>(&accumulator.accumulation[Perspective][j * TILE_HEIGHT]);
            auto entryTile = reinterpret_cast<vec_t*>(&entry.accumulation[j * TILE_HEIGHT]);

            for (IndexType k = 0; k < REG_COUNT; ++k)
                acc[k] = entryTile[k];

            int i = 0;
            for (; i < int(std::min(removed.size(), added.size())); ++i)
            {
                IndexType indexR  = removed[i];
                IndexType offsetR = HalfDimensions * indexR + j * TILE_HEIGHT;
                auto      columnR = reinterpret_cast<const vec_t*>(&weights[offsetR]);
                IndexType indexA  = added[i];
                IndexType offsetA = HalfDimensions * indexA + j * TILE_HEIGHT;
                auto      columnA = reinterpret_cast<const vec_t*>(&weights[offsetA]);

                for (unsigned k = 0; k < REG_COUNT; ++k)
                    acc[k] = vec_add_16(acc[k], vec_sub_16(columnA[k], columnR[k]));
            }
            for (; i < int(removed.size()); ++i)
            {
                IndexType index  = removed[i];
                IndexType offset = HalfDimensions * index + j * TILE_HEIGHT;
                auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);

                for (unsigned k = 0; k < REG_COUNT; ++k)
                    acc[k] = vec_sub_16(acc[k], column[k]);
            }
            for (; i < int(added.size()); ++i)
            {
                IndexType index  = added[i];
                IndexType offset = HalfDimensions * index + j * TILE_HEIGHT;
                auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);

                for (unsigned k = 0; k < REG_COUNT; ++k)
                    acc[k] = vec_add_16(acc[k], column[k]);
            }

            for (IndexType k = 0; k < REG_COUNT; ++k)
            {
                vec_store(&entryTile[k], acc[k]);
                vec_store(&accTile[k], acc[k]);
            }
        }

        for (IndexType j = 0; j < PSQTBuckets / PSQT_TILE_HEIGHT; ++j)
        {
            auto accTilePsqt = reinterpret_cast<psqt_vec_t*>(
              &accumulator.psqtAccumulation[Perspective][j * PSQT_TILE_HEIGHT]);
            auto entryTilePsqt =
              reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[j * PSQT_TILE_HEIGHT]);

            for (std::size_t k = 0; k < PSQT_REG_COUNT; ++k)
                psqt[k] = entryTilePsqt[k];

            for (int i = 0; i < int(removed.size()); ++i)
            {
                IndexType index      = removed[i];
                IndexType offset     = PSQTBuckets * index + j * PSQT_TILE_HEIGHT;
                auto      columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);

                for (std::size_t k = 0; k < PSQT_REG_COUNT; ++k)
                    psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
            }
            for (int i = 0; i < int(added.size()); ++i)
            {
                IndexType index      = added[i];
                IndexType offset     = PSQTBuckets * index + j * PSQT_TILE_HEIGHT;
                auto      columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);

                for (std::size_t k = 0; k < PSQT_REG_COUNT; ++k)
                    psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (std::size_t k = 0; k < PSQT_REG_COUNT; ++k)
            {
                vec_store_psqt(&entryTilePsqt[k], psqt[k]);
                vec_store_psqt(&accTilePsqt[k], psqt[k]);
            }
        }

#else

        for (auto index : removed)
        {
            IndexType offset;
            offset = index * HalfDimensions;
            for (IndexType j = 0; j < HalfDimensions; ++j)
                entry.accumulation[j] -= weights[offset + j];
            offset = index * PSQTBuckets;
            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                entry.psqtAccumulation[k] -= psqtWeights[offset + k];
        }
        for (auto index : added)
        {
            IndexType offset;
            offset = index * HalfDimensions;
            for (IndexType j = 0; j < HalfDimensions; ++j)
                entry.accumulation[j] += weights[offset + j];
            offset = index * PSQTBuckets;
            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                entry.psqtAccumulation[k] += psqtWeights[offset + k];
        }

        // The accumulator of the refresh entry has been updated.
        // Now copy its content to the actual accumulator were refreshing.
        std::memcpy(accumulator.accumulation[Perspective], entry.accumulation,
                    sizeof(BiasType) * HalfDimensions);
        std::memcpy(accumulator.psqtAccumulation[Perspective], entry.psqtAccumulation,
                    sizeof(int32_t) * PSQTBuckets);
#endif

        for (Color c : {WHITE, BLACK})
            entry.colorBB[c] = pos.pieces(c);

        for (PieceType pt = PAWN; pt <= KING; ++pt)
            entry.typeBB[pt] = pos.pieces(pt);
    }

    template<Color Perspective>
    void hint_common_access_for_perspective(
      const Position& pos, AccumulatorCaches::Cache<HalfDimensions>* cache) const noexcept {

        // Works like update_accumulator, but performs less work.
        // Updates ONLY the accumulator for pos.

        // Look for a usable accumulator of an earlier position. Keep track
        // of the estimated gain in terms of features to be added/subtracted.
        // Fast early exit.
        if ((pos.state()->*accPtr).computed[Perspective])
            return;

        State* oldest = find_computed_accumulator<Perspective>(pos);

        if ((oldest->*accPtr).computed[Perspective] && oldest != pos.state())
            update_accumulator_incremental<Perspective, true>(pos, oldest);
        else
            update_accumulator_refresh_cache<Perspective>(pos, cache);
    }

    template<Color Perspective>
    void update_accumulator(const Position&                           pos,
                            AccumulatorCaches::Cache<HalfDimensions>* cache) const noexcept {

        State* oldest = find_computed_accumulator<Perspective>(pos);

        if ((oldest->*accPtr).computed[Perspective] && oldest != pos.state())
            // Start from the oldest computed accumulator, update all the
            // accumulators up to the current position.
            update_accumulator_incremental<Perspective, false>(pos, oldest);
        else
            update_accumulator_refresh_cache<Perspective>(pos, cache);
    }

    template<IndexType Size>
    friend struct AccumulatorCaches::Cache;

    alignas(CACHE_LINE_SIZE) BiasType biases[HalfDimensions];
    alignas(CACHE_LINE_SIZE) WeightType weights[HalfDimensions * InputDimensions];
    alignas(CACHE_LINE_SIZE) PSQTWeightType psqtWeights[InputDimensions * PSQTBuckets];
};

}  // namespace DON::Eval::NNUE

#endif  // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
