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

// A class that converts the input features of the NNUE evaluation function

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
         Accumulator<TransformedFeatureDimensions> StateInfo::*accPtr>
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

        Color perspectives[COLOR_NB] = {pos.side_to_move(), ~pos.side_to_move()};
        auto& psqtAccumulation       = (pos.state()->*accPtr).psqtAccumulation;

        std::int32_t psqt = (psqtAccumulation[perspectives[WHITE]][bucket]
                             - psqtAccumulation[perspectives[BLACK]][bucket])
                          / 2;

        auto& accumulation = (pos.state()->*accPtr).accumulation;

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

            for (IndexType j = 0; j < NumOutputChunks; ++j)
            {
                // What want to do is multiply inputs in a pairwise manner (after clipping), and then shift right by 9.
                // Instead, shift left by 7, and use mulhi, stripping the bottom 16 bits, effectively shifting right by 16,
                // resulting in a net shift of 9 bits. Use mulhi because it maintains the sign of the multiplication (unlike mullo),
                // allowing us to make use of packus to clip 2 of the inputs, resulting in a save of 2 "vec_max_16" calls.
                // A special case is when use NEON, where shift left by 6 instead, because the instruction "vqdmulhq_s16"
                // also doubles the return value after the multiplication, adding an extra shift to the left by 1, so
                // compensate by shifting less before the multiplication.
                constexpr int Shift =
    #if defined(USE_SSE2)
                  7;
    #else
                  6;
    #endif
                vec_t sum0a = vec_slli_16(vec_max_16(vec_min_16(in0[j * 2 + 0], One), Zero), Shift);
                vec_t sum0b = vec_slli_16(vec_max_16(vec_min_16(in0[j * 2 + 1], One), Zero), Shift);
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
    [[nodiscard]] static std::pair<StateInfo*, StateInfo*>
    try_find_computed_accumulator(const Position& pos) noexcept {
        // Look for a usable accumulator of an earlier position. Keep track
        // of the estimated gain in terms of features to be added/subtracted.
        StateInfo* st        = pos.state();
        StateInfo* nextState = nullptr;
        int        gain      = FeatureSet::refresh_cost(pos);
        while (st->previous && !(st->*accPtr).computed[Perspective])
        {
            // This governs when a full feature refresh is needed and how many
            // updates are better than just one full refresh.
            if (FeatureSet::requires_refresh(st, Perspective)
                || (gain -= FeatureSet::update_cost(st) + 1) < 0)
                break;
            nextState = st;
            st        = st->previous;
        }
        return {st, nextState};
    }

    // NOTE: The parameter toUpdateStates is an array of position states.
    //       All states must be sequential, that is toUpdateStates[i] must
    //       either be reachable by repeatedly applying ->previous from
    //       toUpdateStates[i+1], and computedState must be reachable by
    //       repeatedly applying ->previous on toUpdateStates[0].
    template<Color Perspective, std::size_t N>
    void update_accumulator_incremental(const Position& pos,
                                        StateInfo*      computedState,
                                        StateInfo*      toUpdateStates[N]) const noexcept {
        static_assert(N > 0);
        assert([&]() {
            for (std::size_t i = 0; i < N; ++i)
                if (toUpdateStates[i] == nullptr)
                    return false;
            return true;
        }());

#if defined(VECTOR)
        // Gcc-10.2 unnecessarily spills AVX2 registers if this array
        // is defined in the VECTOR code below, once in each branch.
        vec_t      acc[REG_COUNT];
        psqt_vec_t psqt[PSQT_REG_COUNT];
#endif

        // Update incrementally going back through toUpdateStates.
        // Gather all features to be updated.
        Square ksq = pos.king_square(Perspective);

        // The size must be enough to contain the largest possible update.
        // That might depend on the feature set and generally relies on the
        // feature set's update cost calculation to be correct and never allow
        // updates with more added/removed features than MaxActiveDimentions.
        FeatureSet::IndexList removed[N], added[N];

        for (int i = N - 1; i >= 0; --i)
        {
            (toUpdateStates[i]->*accPtr).computed[Perspective] = true;

            StateInfo* endState = i == 0 ? computedState : toUpdateStates[i - 1];

            for (StateInfo* st = toUpdateStates[i]; st != endState; st = st->previous)
                FeatureSet::append_changed_indices<Perspective>(ksq, st->dirtyPiece, removed[i],
                                                                added[i]);
        }

        StateInfo* st = computedState;

        // Now update the accumulators listed in toUpdateStates[], where the last element is a sentinel.
#if defined(VECTOR)

        if (N == 1 && (removed[0].size() == 1 || removed[0].size() == 2) && added[0].size() == 1)
        {
            assert(toUpdateStates[0] != nullptr);

            auto accIn =
              reinterpret_cast<const vec_t*>(&(st->*accPtr).accumulation[Perspective][0]);
            auto accOut =
              reinterpret_cast<vec_t*>(&(toUpdateStates[0]->*accPtr).accumulation[Perspective][0]);

            IndexType offsetR0 = HalfDimensions * removed[0][0];
            auto      columnR0 = reinterpret_cast<const vec_t*>(&weights[offsetR0]);
            IndexType offsetA  = HalfDimensions * added[0][0];
            auto      columnA  = reinterpret_cast<const vec_t*>(&weights[offsetA]);

            if (removed[0].size() == 1)
            {
                for (IndexType k = 0; k < HalfDimensions * sizeof(std::int16_t) / sizeof(vec_t);
                     ++k)
                    accOut[k] = vec_add_16(vec_sub_16(accIn[k], columnR0[k]), columnA[k]);
            }
            else
            {
                IndexType offsetR1 = HalfDimensions * removed[0][1];
                auto      columnR1 = reinterpret_cast<const vec_t*>(&weights[offsetR1]);

                for (IndexType k = 0; k < HalfDimensions * sizeof(std::int16_t) / sizeof(vec_t);
                     ++k)
                    accOut[k] = vec_sub_16(vec_add_16(accIn[k], columnA[k]),
                                           vec_add_16(columnR0[k], columnR1[k]));
            }

            auto accPsqtIn =
              reinterpret_cast<const psqt_vec_t*>(&(st->*accPtr).psqtAccumulation[Perspective][0]);
            auto accPsqtOut = reinterpret_cast<psqt_vec_t*>(
              &(toUpdateStates[0]->*accPtr).psqtAccumulation[Perspective][0]);

            IndexType offsetPsqtR0 = PSQTBuckets * removed[0][0];
            auto columnPsqtR0     = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtR0]);
            IndexType offsetPsqtA = PSQTBuckets * added[0][0];
            auto      columnPsqtA = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtA]);

            if (removed[0].size() == 1)
            {
                for (std::size_t k = 0; k < PSQTBuckets * sizeof(std::int32_t) / sizeof(psqt_vec_t);
                     ++k)
                    accPsqtOut[k] = vec_add_psqt_32(vec_sub_psqt_32(accPsqtIn[k], columnPsqtR0[k]),
                                                    columnPsqtA[k]);
            }
            else
            {
                IndexType offsetPsqtR1 = PSQTBuckets * removed[0][1];
                auto columnPsqtR1 = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtR1]);

                for (std::size_t k = 0; k < PSQTBuckets * sizeof(std::int32_t) / sizeof(psqt_vec_t);
                     ++k)
                    accPsqtOut[k] =
                      vec_sub_psqt_32(vec_add_psqt_32(accPsqtIn[k], columnPsqtA[k]),
                                      vec_add_psqt_32(columnPsqtR0[k], columnPsqtR1[k]));
            }
        }
        else
        {
            for (IndexType j = 0; j < HalfDimensions / TILE_HEIGHT; ++j)
            {
                // Load accumulator
                auto accTileIn = reinterpret_cast<const vec_t*>(
                  &(st->*accPtr).accumulation[Perspective][j * TILE_HEIGHT]);
                for (IndexType k = 0; k < REG_COUNT; ++k)
                    acc[k] = vec_load(&accTileIn[k]);

                for (IndexType i = 0; i < N; ++i)
                {
                    // Difference calculation for the deactivated features
                    for (auto index : removed[i])
                    {
                        IndexType offset = HalfDimensions * index + j * TILE_HEIGHT;
                        auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);
                        for (IndexType k = 0; k < REG_COUNT; ++k)
                            acc[k] = vec_sub_16(acc[k], column[k]);
                    }

                    // Difference calculation for the activated features
                    for (auto index : added[i])
                    {
                        IndexType offset = HalfDimensions * index + j * TILE_HEIGHT;
                        auto      column = reinterpret_cast<const vec_t*>(&weights[offset]);
                        for (IndexType k = 0; k < REG_COUNT; ++k)
                            acc[k] = vec_add_16(acc[k], column[k]);
                    }

                    // Store accumulator
                    auto accTileOut = reinterpret_cast<vec_t*>(
                      &(toUpdateStates[i]->*accPtr).accumulation[Perspective][j * TILE_HEIGHT]);
                    for (IndexType k = 0; k < REG_COUNT; ++k)
                        vec_store(&accTileOut[k], acc[k]);
                }
            }

            for (IndexType j = 0; j < PSQTBuckets / PSQT_TILE_HEIGHT; ++j)
            {
                // Load accumulator
                auto accTilePsqtIn = reinterpret_cast<const psqt_vec_t*>(
                  &(st->*accPtr).psqtAccumulation[Perspective][j * PSQT_TILE_HEIGHT]);
                for (std::size_t k = 0; k < PSQT_REG_COUNT; ++k)
                    psqt[k] = vec_load_psqt(&accTilePsqtIn[k]);

                for (IndexType i = 0; i < N; ++i)
                {
                    // Difference calculation for the deactivated features
                    for (auto index : removed[i])
                    {
                        IndexType offset = PSQTBuckets * index + j * PSQT_TILE_HEIGHT;
                        auto columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
                        for (std::size_t k = 0; k < PSQT_REG_COUNT; ++k)
                            psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
                    }

                    // Difference calculation for the activated features
                    for (auto index : added[i])
                    {
                        IndexType offset = PSQTBuckets * index + j * PSQT_TILE_HEIGHT;
                        auto columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
                        for (std::size_t k = 0; k < PSQT_REG_COUNT; ++k)
                            psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
                    }

                    // Store accumulator
                    auto accTilePsqtOut = reinterpret_cast<psqt_vec_t*>(
                      &(toUpdateStates[i]->*accPtr)
                         .psqtAccumulation[Perspective][j * PSQT_TILE_HEIGHT]);
                    for (std::size_t k = 0; k < PSQT_REG_COUNT; ++k)
                        vec_store_psqt(&accTilePsqtOut[k], psqt[k]);
                }
            }
        }
#else
        for (IndexType i = 0; i < N; ++i)
        {
            std::memcpy((toUpdateStates[i]->*accPtr).accumulation[Perspective],
                        (st->*accPtr).accumulation[Perspective], HalfDimensions * sizeof(BiasType));

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                (toUpdateStates[i]->*accPtr).psqtAccumulation[Perspective][k] =
                  (st->*accPtr).psqtAccumulation[Perspective][k];

            st = toUpdateStates[i];

            // Difference calculation for the deactivated features
            for (auto index : removed[i])
            {
                IndexType offset = HalfDimensions * index;
                for (IndexType j = 0; j < HalfDimensions; ++j)
                    (st->*accPtr).accumulation[Perspective][j] -= weights[offset + j];

                for (std::size_t k = 0; k < PSQTBuckets; ++k)
                    (st->*accPtr).psqtAccumulation[Perspective][k] -=
                      psqtWeights[index * PSQTBuckets + k];
            }

            // Difference calculation for the activated features
            for (auto index : added[i])
            {
                IndexType offset = HalfDimensions * index;
                for (IndexType j = 0; j < HalfDimensions; ++j)
                    (st->*accPtr).accumulation[Perspective][j] += weights[offset + j];

                for (std::size_t k = 0; k < PSQTBuckets; ++k)
                    (st->*accPtr).psqtAccumulation[Perspective][k] +=
                      psqtWeights[index * PSQTBuckets + k];
            }
        }
#endif
    }

    template<Color Perspective>
    void update_accumulator_refresh_cache(
      const Position& pos, AccumulatorCaches::Cache<HalfDimensions>* cache) const noexcept {
        assert(cache != nullptr);

        Square ksq = pos.king_square(Perspective);

        auto&                 entry = (*cache)[ksq][Perspective];
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
            IndexType offset = index * HalfDimensions;
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

        auto [oldestState, _] = try_find_computed_accumulator<Perspective>(pos);

        if ((oldestState->*accPtr).computed[Perspective])
        {
            // Only update current position accumulator to minimize work
            StateInfo* toUpdateStates[1]{pos.state()};
            update_accumulator_incremental<Perspective, 1>(pos, oldestState, toUpdateStates);
        }
        else
            update_accumulator_refresh_cache<Perspective>(pos, cache);
    }

    template<Color Perspective>
    void update_accumulator(const Position&                           pos,
                            AccumulatorCaches::Cache<HalfDimensions>* cache) const noexcept {

        auto [oldestState, nextState] = try_find_computed_accumulator<Perspective>(pos);

        if ((oldestState->*accPtr).computed[Perspective])
        {
            if (nextState == nullptr)
                return;

            // Now update the accumulators listed in toUpdateStates[],
            // where the last element is a sentinel.
            // Currently, update 2 accumulators.
            //     1. for the current position
            //     2. the next accumulator after the computed one
            // The heuristic may change in the future.
            if (nextState == pos.state())
            {
                StateInfo* toUpdateStates[1]{nextState};
                update_accumulator_incremental<Perspective, 1>(pos, oldestState, toUpdateStates);
            }
            else
            {
                StateInfo* toUpdateStates[2]{nextState, pos.state()};
                update_accumulator_incremental<Perspective, 2>(pos, oldestState, toUpdateStates);
            }
        }
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
