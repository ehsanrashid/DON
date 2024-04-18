/*
  DON, a UCI chess playing engine derived from Glaurung 2.1

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

// If vector instructions are enabled, we update and refresh the
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
    #define vec_mul_16(a, b) _mm512_mullo_epi16(a, b)
    #define vec_zero() _mm512_setzero_epi32()
    #define vec_set_16(a) _mm512_set1_epi16(a)
    #define vec_max_16(a, b) _mm512_max_epi16(a, b)
    #define vec_min_16(a, b) _mm512_min_epi16(a, b)
    // Inverse permuted at load time
    #define vec_msb_pack_16(a, b) \
        _mm512_packs_epi16(_mm512_srli_epi16(a, 7), _mm512_srli_epi16(b, 7))
    #define vec_load_psqt(a) _mm256_load_si256(a)
    #define vec_store_psqt(a, b) _mm256_store_si256(a, b)
    #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
    #define vec_zero_psqt() _mm256_setzero_si256()
    #define NumRegistersSIMD 16
    #define MaxChunkSize 64

#elif defined(USE_AVX2)
using vec_t      = __m256i;
using psqt_vec_t = __m256i;
    #define vec_load(a) _mm256_load_si256(a)
    #define vec_store(a, b) _mm256_store_si256(a, b)
    #define vec_add_16(a, b) _mm256_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm256_sub_epi16(a, b)
    #define vec_mul_16(a, b) _mm256_mullo_epi16(a, b)
    #define vec_zero() _mm256_setzero_si256()
    #define vec_set_16(a) _mm256_set1_epi16(a)
    #define vec_max_16(a, b) _mm256_max_epi16(a, b)
    #define vec_min_16(a, b) _mm256_min_epi16(a, b)
    // Inverse permuted at load time
    #define vec_msb_pack_16(a, b) \
        _mm256_packs_epi16(_mm256_srli_epi16(a, 7), _mm256_srli_epi16(b, 7))
    #define vec_load_psqt(a) _mm256_load_si256(a)
    #define vec_store_psqt(a, b) _mm256_store_si256(a, b)
    #define vec_add_psqt_32(a, b) _mm256_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm256_sub_epi32(a, b)
    #define vec_zero_psqt() _mm256_setzero_si256()
    #define NumRegistersSIMD 16
    #define MaxChunkSize 32

#elif defined(USE_SSE2)
using vec_t      = __m128i;
using psqt_vec_t = __m128i;
    #define vec_load(a) (*(a))
    #define vec_store(a, b) *(a) = (b)
    #define vec_add_16(a, b) _mm_add_epi16(a, b)
    #define vec_sub_16(a, b) _mm_sub_epi16(a, b)
    #define vec_mul_16(a, b) _mm_mullo_epi16(a, b)
    #define vec_zero() _mm_setzero_si128()
    #define vec_set_16(a) _mm_set1_epi16(a)
    #define vec_max_16(a, b) _mm_max_epi16(a, b)
    #define vec_min_16(a, b) _mm_min_epi16(a, b)
    #define vec_msb_pack_16(a, b) _mm_packs_epi16(_mm_srli_epi16(a, 7), _mm_srli_epi16(b, 7))
    #define vec_load_psqt(a) (*(a))
    #define vec_store_psqt(a, b) *(a) = (b)
    #define vec_add_psqt_32(a, b) _mm_add_epi32(a, b)
    #define vec_sub_psqt_32(a, b) _mm_sub_epi32(a, b)
    #define vec_zero_psqt() _mm_setzero_si128()
    #define NumRegistersSIMD (Is64Bit ? 16 : 8)
    #define MaxChunkSize 16

#elif defined(USE_NEON)
using vec_t      = int16x8_t;
using psqt_vec_t = int32x4_t;
    #define vec_load(a) (*(a))
    #define vec_store(a, b) *(a) = (b)
    #define vec_add_16(a, b) vaddq_s16(a, b)
    #define vec_sub_16(a, b) vsubq_s16(a, b)
    #define vec_mul_16(a, b) vmulq_s16(a, b)
    #define vec_zero() \
        vec_t { 0 }
    #define vec_set_16(a) vdupq_n_s16(a)
    #define vec_max_16(a, b) vmaxq_s16(a, b)
    #define vec_min_16(a, b) vminq_s16(a, b)
inline vec_t vec_msb_pack_16(vec_t a, vec_t b) noexcept {
    const int8x8_t  shifta    = vshrn_n_s16(a, 7);
    const int8x8_t  shiftb    = vshrn_n_s16(b, 7);
    const int8x16_t compacted = vcombine_s8(shifta, shiftb);
    return *reinterpret_cast<const vec_t*>(&compacted);
}
    #define vec_load_psqt(a) (*(a))
    #define vec_store_psqt(a, b) *(a) = (b)
    #define vec_add_psqt_32(a, b) vaddq_s32(a, b)
    #define vec_sub_psqt_32(a, b) vsubq_s32(a, b)
    #define vec_zero_psqt() \
        psqt_vec_t { 0 }
    #define NumRegistersSIMD 16
    #define MaxChunkSize 16

#else
    #undef VECTOR

#endif


#if defined(VECTOR)

    // Compute optimal SIMD register count for feature transformer accumulation.

    // We use __m* types as template arguments, which causes GCC to emit warnings
    // about losing some attribute information. This is irrelevant to us as we
    // only take their size, so the following pragma are harmless.
    #if defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wignored-attributes"
    #endif

namespace {
template<typename SIMDRegisterType, typename LaneType, int NumLanes, int MaxRegisters>
constexpr int BestRegisterCount() noexcept {
    #define RegisterSize sizeof(SIMDRegisterType)
    #define LaneSize sizeof(LaneType)

    static_assert(RegisterSize >= LaneSize);
    static_assert(MaxRegisters <= NumRegistersSIMD);
    static_assert(MaxRegisters > 0);
    static_assert(NumRegistersSIMD > 0);
    static_assert(RegisterSize % LaneSize == 0);
    static_assert((NumLanes * LaneSize) % RegisterSize == 0);

    const int ideal = (NumLanes * LaneSize) / RegisterSize;
    if (ideal <= MaxRegisters)
        return ideal;

    // Look for the largest divisor of the ideal register count that is smaller than MaxRegisters
    for (int divisor = MaxRegisters; divisor > 1; --divisor)
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

   private:
    // Number of output dimensions for one side
    static constexpr IndexType HalfDimensions = TransformedFeatureDimensions;

#if defined(VECTOR)
    static constexpr int NumRegs =
      BestRegisterCount<vec_t, WeightType, TransformedFeatureDimensions, NumRegistersSIMD>();
    static constexpr int NumPsqtRegs =
      BestRegisterCount<psqt_vec_t, PSQTWeightType, PSQTBuckets, NumRegistersSIMD>();

    static constexpr IndexType TileHeight     = NumRegs * sizeof(vec_t) / 2;
    static constexpr IndexType PsqtTileHeight = NumPsqtRegs * sizeof(psqt_vec_t) / 4;
    static_assert(HalfDimensions % TileHeight == 0, "TileHeight must divide HalfDimensions");
    static_assert(PSQTBuckets % PsqtTileHeight == 0, "PsqtTileHeight must divide PSQTBuckets");
#endif

   public:
    // Output type
    using OutputType = TransformedFeatureType;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = FeatureSet::Dimensions;
    static constexpr IndexType OutputDimensions = HalfDimensions;

    // Size of forward propagation buffer
    static constexpr std::size_t BufferSize = OutputDimensions * sizeof(OutputType);

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value() noexcept {
        return FeatureSet::HashValue ^ (2 * OutputDimensions);
    }

    static constexpr void order_packs([[maybe_unused]] std::uint64_t* v) {
#if defined(USE_AVX512)  // _mm512_packs_epi16 ordering
        std::uint64_t tmp[2]{v[2], v[3]};
        v[2] = v[8], v[3] = v[9];
        v[8] = v[4], v[9] = v[5];
        v[4] = tmp[0], v[5] = tmp[1];
        tmp[0] = v[6], tmp[1] = v[7];
        v[6] = v[10], v[7] = v[11];
        v[10] = v[12], v[11] = v[13];
        v[12] = tmp[0], v[13] = tmp[1];
#elif defined(USE_AVX2)  // _mm256_packs_epi16 ordering
        std::swap(v[2], v[4]);
        std::swap(v[3], v[5]);
#endif
    }

    static constexpr void inverse_order_packs([[maybe_unused]] std::uint64_t* v) {
#if defined(USE_AVX512)  // Inverse _mm512_packs_epi16 ordering
        std::uint64_t tmp[2]{v[2], v[3]};
        v[2] = v[4], v[3] = v[5];
        v[4] = v[8], v[5] = v[9];
        v[8] = tmp[0], v[9] = tmp[1];
        tmp[0] = v[6], tmp[1] = v[7];
        v[6] = v[12], v[7] = v[13];
        v[12] = v[10], v[13] = v[11];
        v[10] = tmp[0], v[11] = tmp[1];
#elif defined(USE_AVX2)  // Inverse _mm256_packs_epi16 ordering
        std::swap(v[2], v[4]);
        std::swap(v[3], v[5]);
#endif
    }

    void permute_weights([[maybe_unused]] void (*order_fn)(uint64_t*)) const {
#if defined(USE_AVX2)
    #if defined(USE_AVX512)
        constexpr IndexType DI = 16;
    #else
        constexpr IndexType DI = 8;
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

    // Read network parameters
    bool read_parameters(std::istream& istream) noexcept {

        read_leb_128<BiasType>(istream, biases, HalfDimensions);
        read_leb_128<WeightType>(istream, weights, HalfDimensions * InputDimensions);
        read_leb_128<PSQTWeightType>(istream, psqtWeights, PSQTBuckets * InputDimensions);

        permute_weights(inverse_order_packs);
        return !istream.fail();
    }

    // Write network parameters
    bool write_parameters(std::ostream& ostream) const noexcept {

        permute_weights(order_packs);

        write_leb_128<BiasType>(ostream, biases, HalfDimensions);
        write_leb_128<WeightType>(ostream, weights, HalfDimensions * InputDimensions);
        write_leb_128<PSQTWeightType>(ostream, psqtWeights, PSQTBuckets * InputDimensions);

        permute_weights(inverse_order_packs);
        return !ostream.fail();
    }

    // Convert input features
    std::int32_t
    transform(const Position& pos, OutputType* output, int bucket, bool psqtOnly) const noexcept {
        update_accumulator<WHITE>(pos, psqtOnly);
        update_accumulator<BLACK>(pos, psqtOnly);

        const Color perspectives[COLOR_NB] = {pos.side_to_move(), ~pos.side_to_move()};
        const auto& psqtAccumulation       = (pos.state()->*accPtr).psqtAccumulation;

        const auto psqt = (psqtAccumulation[perspectives[WHITE]][bucket]
                           - psqtAccumulation[perspectives[BLACK]][bucket])
                        / 2;

        if (psqtOnly)
            return psqt;

        const auto& accumulation = (pos.state()->*accPtr).accumulation;

        for (IndexType p = 0; p < 2; ++p)
        {
            const IndexType offset = (HalfDimensions / 2) * p;

#if defined(VECTOR)

            constexpr IndexType OutputChunkSize = MaxChunkSize;
            static_assert((HalfDimensions / 2) % OutputChunkSize == 0);
            constexpr IndexType NumOutputChunks = HalfDimensions / 2 / OutputChunkSize;

            const vec_t Zero = vec_zero();
            const vec_t One  = vec_set_16(127);

            const vec_t* in0 = reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][0]));
            const vec_t* in1 =
              reinterpret_cast<const vec_t*>(&(accumulation[perspectives[p]][HalfDimensions / 2]));
            vec_t* out = reinterpret_cast<vec_t*>(output + offset);

            for (IndexType j = 0; j < NumOutputChunks; ++j)
            {
                const vec_t sum0a = vec_max_16(vec_min_16(in0[j * 2 + 0], One), Zero);
                const vec_t sum0b = vec_max_16(vec_min_16(in0[j * 2 + 1], One), Zero);
                const vec_t sum1a = vec_max_16(vec_min_16(in1[j * 2 + 0], One), Zero);
                const vec_t sum1b = vec_max_16(vec_min_16(in1[j * 2 + 1], One), Zero);

                const vec_t pa = vec_mul_16(sum0a, sum1a);
                const vec_t pb = vec_mul_16(sum0b, sum1b);

                out[j] = vec_msb_pack_16(pa, pb);
            }

#else

            for (IndexType j = 0; j < HalfDimensions / 2; ++j)
            {
                BiasType sum0 = accumulation[perspectives[p]][j + 0];
                BiasType sum1 = accumulation[perspectives[p]][j + HalfDimensions / 2];
                sum0          = std::clamp<BiasType>(sum0, 0, 127);
                sum1          = std::clamp<BiasType>(sum1, 0, 127);

                output[offset + j] = static_cast<OutputType>(unsigned(sum0 * sum1) / 128);
            }

#endif
        }

        return psqt;
    }

    void hint_common_access(const Position& pos, bool psqtOnly) const noexcept {
        hint_common_access_for_perspective<WHITE>(pos, psqtOnly);
        hint_common_access_for_perspective<BLACK>(pos, psqtOnly);
    }

   private:
    template<Color Perspective>
    [[nodiscard]] static std::pair<StateInfo*, StateInfo*>
    try_find_computed_accumulator(const Position& pos, bool psqtOnly) noexcept {
        // Look for a usable accumulator of an earlier position. We keep track
        // of the estimated gain in terms of features to be added/subtracted.
        StateInfo* st        = pos.state();
        StateInfo* nextState = nullptr;
        int        gain      = FeatureSet::refresh_cost(pos);
        while (st->previous
               && (!(st->*accPtr).computedPSQT[Perspective]
                   || (!psqtOnly && !(st->*accPtr).computed[Perspective])))
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

    // NOTE: The parameter toUpdateStates is an array of position states, ending with nullptr.
    //       All states must be sequential, that is toUpdateStates[i] must either be reachable
    //       by repeatedly applying ->previous from toUpdateStates[i+1] or
    //       toUpdateStates[i] == nullptr.
    //       computedState must be reachable by repeatedly applying ->previous on
    //       toUpdateStates[0], if not nullptr.
    template<Color Perspective, std::size_t N>
    void update_accumulator_incremental(const Position& pos,
                                        StateInfo*      computedState,
                                        StateInfo*      toUpdateStates[N],
                                        bool            psqtOnly) const noexcept {
        static_assert(N > 0);
        assert(!toUpdateStates[N - 1]);

#if defined(VECTOR)
        // Gcc-10.2 unnecessarily spills AVX2 registers if this array
        // is defined in the VECTOR code below, once in each branch
        vec_t      acc[NumRegs];
        psqt_vec_t psqt[NumPsqtRegs];
#endif

        if (!toUpdateStates[0])
            return;

        // Update incrementally going back through toUpdateStates.

        // Gather all features to be updated.
        Square ksq = pos.king_square(Perspective);

        // The size must be enough to contain the largest possible update.
        // That might depend on the feature set and generally relies on the
        // feature set's update cost calculation to be correct and never allow
        // updates with more added/removed features than MaxActiveDimensions.
        FeatureSet::IndexList removed[N - 1], added[N - 1];

        {
            // Last potential state to update. Skip last element because it must be nullptr.
            int i = N - 2;
            while (!toUpdateStates[i])
                --i;

            for (StateInfo* st = toUpdateStates[i]; i >= 0; --i)
            {
                (toUpdateStates[i]->*accPtr).computed[Perspective]     = !psqtOnly;
                (toUpdateStates[i]->*accPtr).computedPSQT[Perspective] = true;

                const StateInfo* endState = i == 0 ? computedState : toUpdateStates[i - 1];
                for (; st != endState; st = st->previous)
                    FeatureSet::append_changed_indices<Perspective>(ksq, st->dirtyPiece, removed[i],
                                                                    added[i]);
            }
        }

        StateInfo* st = computedState;

        // Now update the accumulators listed in toUpdateStates[], where the last element is a sentinel.
#if defined(VECTOR)

        if (!toUpdateStates[1] && (removed[0].size() == 1 || removed[0].size() == 2)
            && added[0].size() == 1)
        {
            assert(toUpdateStates[0]);

            if (!psqtOnly)
            {
                auto accIn =
                  reinterpret_cast<const vec_t*>(&(st->*accPtr).accumulation[Perspective][0]);
                auto accOut = reinterpret_cast<vec_t*>(
                  &(toUpdateStates[0]->*accPtr).accumulation[Perspective][0]);

                const IndexType offsetR0 = HalfDimensions * removed[0][0];
                auto            columnR0 = reinterpret_cast<const vec_t*>(&weights[offsetR0]);
                const IndexType offsetA  = HalfDimensions * added[0][0];
                auto            columnA  = reinterpret_cast<const vec_t*>(&weights[offsetA]);

                if (removed[0].size() == 1)
                {
                    for (IndexType k = 0; k < HalfDimensions * sizeof(std::int16_t) / sizeof(vec_t);
                         ++k)
                        accOut[k] = vec_add_16(vec_sub_16(accIn[k], columnR0[k]), columnA[k]);
                }
                else
                {
                    const IndexType offsetR1 = HalfDimensions * removed[0][1];
                    auto            columnR1 = reinterpret_cast<const vec_t*>(&weights[offsetR1]);

                    for (IndexType k = 0; k < HalfDimensions * sizeof(std::int16_t) / sizeof(vec_t);
                         ++k)
                        accOut[k] = vec_sub_16(vec_add_16(accIn[k], columnA[k]),
                                               vec_add_16(columnR0[k], columnR1[k]));
                }
            }

            auto accPsqtIn =
              reinterpret_cast<const psqt_vec_t*>(&(st->*accPtr).psqtAccumulation[Perspective][0]);
            auto accPsqtOut = reinterpret_cast<psqt_vec_t*>(
              &(toUpdateStates[0]->*accPtr).psqtAccumulation[Perspective][0]);

            const IndexType offsetPsqtR0 = PSQTBuckets * removed[0][0];
            auto columnPsqtR0 = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtR0]);
            const IndexType offsetPsqtA = PSQTBuckets * added[0][0];
            auto columnPsqtA = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offsetPsqtA]);

            if (removed[0].size() == 1)
            {
                for (std::size_t k = 0; k < PSQTBuckets * sizeof(std::int32_t) / sizeof(psqt_vec_t);
                     ++k)
                    accPsqtOut[k] = vec_add_psqt_32(vec_sub_psqt_32(accPsqtIn[k], columnPsqtR0[k]),
                                                    columnPsqtA[k]);
            }
            else
            {
                const IndexType offsetPsqtR1 = PSQTBuckets * removed[0][1];
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
            if (!psqtOnly)
                for (IndexType j = 0; j < HalfDimensions / TileHeight; ++j)
                {
                    // Load accumulator
                    auto accTileIn = reinterpret_cast<const vec_t*>(
                      &(st->*accPtr).accumulation[Perspective][j * TileHeight]);
                    for (IndexType k = 0; k < NumRegs; ++k)
                        acc[k] = vec_load(&accTileIn[k]);

                    for (IndexType i = 0; toUpdateStates[i]; ++i)
                    {
                        // Difference calculation for the deactivated features
                        for (const auto index : removed[i])
                        {
                            const IndexType offset = HalfDimensions * index + j * TileHeight;
                            auto column = reinterpret_cast<const vec_t*>(&weights[offset]);
                            for (IndexType k = 0; k < NumRegs; ++k)
                                acc[k] = vec_sub_16(acc[k], column[k]);
                        }

                        // Difference calculation for the activated features
                        for (const auto index : added[i])
                        {
                            const IndexType offset = HalfDimensions * index + j * TileHeight;
                            auto column = reinterpret_cast<const vec_t*>(&weights[offset]);
                            for (IndexType k = 0; k < NumRegs; ++k)
                                acc[k] = vec_add_16(acc[k], column[k]);
                        }

                        // Store accumulator
                        auto accTileOut = reinterpret_cast<vec_t*>(
                          &(toUpdateStates[i]->*accPtr).accumulation[Perspective][j * TileHeight]);
                        for (IndexType k = 0; k < NumRegs; ++k)
                            vec_store(&accTileOut[k], acc[k]);
                    }
                }

            for (IndexType j = 0; j < PSQTBuckets / PsqtTileHeight; ++j)
            {
                // Load accumulator
                auto accTilePsqtIn = reinterpret_cast<const psqt_vec_t*>(
                  &(st->*accPtr).psqtAccumulation[Perspective][j * PsqtTileHeight]);
                for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                    psqt[k] = vec_load_psqt(&accTilePsqtIn[k]);

                for (IndexType i = 0; toUpdateStates[i]; ++i)
                {
                    // Difference calculation for the deactivated features
                    for (const auto index : removed[i])
                    {
                        const IndexType offset = PSQTBuckets * index + j * PsqtTileHeight;
                        auto columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
                        for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                            psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
                    }

                    // Difference calculation for the activated features
                    for (const auto index : added[i])
                    {
                        const IndexType offset = PSQTBuckets * index + j * PsqtTileHeight;
                        auto columnPsqt = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);
                        for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                            psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
                    }

                    // Store accumulator
                    auto accTilePsqtOut = reinterpret_cast<psqt_vec_t*>(
                      &(toUpdateStates[i]->*accPtr)
                         .psqtAccumulation[Perspective][j * PsqtTileHeight]);
                    for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                        vec_store_psqt(&accTilePsqtOut[k], psqt[k]);
                }
            }
        }
#else
        for (IndexType i = 0; toUpdateStates[i]; ++i)
        {
            if (!psqtOnly)
                std::memcpy((toUpdateStates[i]->*accPtr).accumulation[Perspective],
                            (st->*accPtr).accumulation[Perspective],
                            HalfDimensions * sizeof(BiasType));

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                (toUpdateStates[i]->*accPtr).psqtAccumulation[Perspective][k] =
                  (st->*accPtr).psqtAccumulation[Perspective][k];

            st = toUpdateStates[i];

            // Difference calculation for the deactivated features
            for (const auto index : removed[i])
            {
                if (!psqtOnly)
                {
                    const IndexType offset = HalfDimensions * index;
                    for (IndexType j = 0; j < HalfDimensions; ++j)
                        (st->*accPtr).accumulation[Perspective][j] -= weights[offset + j];
                }

                for (std::size_t k = 0; k < PSQTBuckets; ++k)
                    (st->*accPtr).psqtAccumulation[Perspective][k] -=
                      psqtWeights[index * PSQTBuckets + k];
            }

            // Difference calculation for the activated features
            for (const auto index : added[i])
            {
                if (!psqtOnly)
                {
                    const IndexType offset = HalfDimensions * index;
                    for (IndexType j = 0; j < HalfDimensions; ++j)
                        (st->*accPtr).accumulation[Perspective][j] += weights[offset + j];
                }

                for (std::size_t k = 0; k < PSQTBuckets; ++k)
                    (st->*accPtr).psqtAccumulation[Perspective][k] +=
                      psqtWeights[index * PSQTBuckets + k];
            }
        }
#endif
    }

    template<Color Perspective>
    void update_accumulator_refresh(const Position& pos, bool psqtOnly) const noexcept {
#if defined(VECTOR)
        // Gcc-10.2 unnecessarily spills AVX2 registers if this array
        // is defined in the VECTOR code below, once in each branch
        vec_t      acc[NumRegs];
        psqt_vec_t psqt[NumPsqtRegs];
#endif

        // Refresh the accumulator
        // Could be extracted to a separate function because it's done in 2 places,
        // but it's unclear if compilers would correctly handle register allocation.
        auto& accumulator                     = pos.state()->*accPtr;
        accumulator.computed[Perspective]     = !psqtOnly;
        accumulator.computedPSQT[Perspective] = true;
        FeatureSet::IndexList active;
        FeatureSet::append_active_indices<Perspective>(pos, active);

#if defined(VECTOR)
        if (!psqtOnly)
            for (IndexType j = 0; j < HalfDimensions / TileHeight; ++j)
            {
                auto biasesTile = reinterpret_cast<const vec_t*>(&biases[j * TileHeight]);
                for (IndexType k = 0; k < NumRegs; ++k)
                    acc[k] = biasesTile[k];

                int i = 0;
                for (; i < int(active.size()) - 1; i += 2)
                {
                    IndexType       index0  = active[i];
                    IndexType       index1  = active[i + 1];
                    const IndexType offset0 = HalfDimensions * index0 + j * TileHeight;
                    const IndexType offset1 = HalfDimensions * index1 + j * TileHeight;
                    auto            column0 = reinterpret_cast<const vec_t*>(&weights[offset0]);
                    auto            column1 = reinterpret_cast<const vec_t*>(&weights[offset1]);

                    for (unsigned k = 0; k < NumRegs; ++k)
                        acc[k] = vec_add_16(acc[k], vec_add_16(column0[k], column1[k]));
                }
                for (; i < int(active.size()); ++i)
                {
                    IndexType       index  = active[i];
                    const IndexType offset = HalfDimensions * index + j * TileHeight;
                    auto            column = reinterpret_cast<const vec_t*>(&weights[offset]);

                    for (unsigned k = 0; k < NumRegs; ++k)
                        acc[k] = vec_add_16(acc[k], column[k]);
                }

                auto accTile =
                  reinterpret_cast<vec_t*>(&accumulator.accumulation[Perspective][j * TileHeight]);
                for (unsigned k = 0; k < NumRegs; k++)
                    vec_store(&accTile[k], acc[k]);
            }

        for (IndexType j = 0; j < PSQTBuckets / PsqtTileHeight; ++j)
        {
            for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                psqt[k] = vec_zero_psqt();

            int i = 0;
            for (; i < int(active.size()) - 1; i += 2)
            {
                IndexType       index0  = active[i];
                IndexType       index1  = active[i + 1];
                const IndexType offset0 = PSQTBuckets * index0 + j * PsqtTileHeight;
                const IndexType offset1 = PSQTBuckets * index1 + j * PsqtTileHeight;
                auto columnPsqt0 = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset0]);
                auto columnPsqt1 = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset1]);

                for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                    psqt[k] =
                      vec_add_psqt_32(psqt[k], vec_add_psqt_32(columnPsqt0[k], columnPsqt1[k]));
            }
            for (; i < int(active.size()); ++i)
            {
                IndexType       index  = active[i];
                const IndexType offset = PSQTBuckets * index + j * PsqtTileHeight;
                auto columnPsqt        = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);

                for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                    psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
            }

            auto accTilePsqt = reinterpret_cast<psqt_vec_t*>(
              &accumulator.psqtAccumulation[Perspective][j * PsqtTileHeight]);
            for (std::size_t k = 0; k < NumPsqtRegs; ++k)
                vec_store_psqt(&accTilePsqt[k], psqt[k]);
        }

#else
        if (!psqtOnly)
            std::memcpy(accumulator.accumulation[Perspective], biases,
                        HalfDimensions * sizeof(BiasType));

        for (std::size_t k = 0; k < PSQTBuckets; ++k)
            accumulator.psqtAccumulation[Perspective][k] = 0;

        for (const auto index : active)
        {
            if (!psqtOnly)
            {
                const IndexType offset = HalfDimensions * index;
                for (IndexType j = 0; j < HalfDimensions; ++j)
                    accumulator.accumulation[Perspective][j] += weights[offset + j];
            }

            for (std::size_t k = 0; k < PSQTBuckets; ++k)
                accumulator.psqtAccumulation[Perspective][k] +=
                  psqtWeights[index * PSQTBuckets + k];
        }
#endif
    }

    template<Color Perspective>
    void hint_common_access_for_perspective(const Position& pos, bool psqtOnly) const noexcept {
        // Works like update_accumulator, but performs less work.
        // Updates ONLY the accumulator for pos.

        // Look for a usable accumulator of an earlier position. We keep track
        // of the estimated gain in terms of features to be added/subtracted.
        // Fast early exit.
        if ((pos.state()->*accPtr).computed[Perspective]
            || (psqtOnly && (pos.state()->*accPtr).computedPSQT[Perspective]))
            return;

        auto [oldestState, _] = try_find_computed_accumulator<Perspective>(pos, psqtOnly);

        if ((oldestState->*accPtr).computed[Perspective]
            || (psqtOnly && (oldestState->*accPtr).computedPSQT[Perspective]))
        {
            // Only update current position accumulator to minimize work.
            StateInfo* toUpdateStates[2]{pos.state(), nullptr};
            update_accumulator_incremental<Perspective, 2>(pos, oldestState, toUpdateStates,
                                                           psqtOnly);
        }
        else
            update_accumulator_refresh<Perspective>(pos, psqtOnly);
    }

    template<Color Perspective>
    void update_accumulator(const Position& pos, bool psqtOnly) const noexcept {

        auto [oldestState, nextState] = try_find_computed_accumulator<Perspective>(pos, psqtOnly);

        if ((oldestState->*accPtr).computed[Perspective]
            || (psqtOnly && (oldestState->*accPtr).computedPSQT[Perspective]))
        {
            if (!nextState)
                return;

            // Now update the accumulators listed in toUpdateStates[], where the last element is a sentinel.
            // Currently, we update 2 accumulators.
            //     1. for the current position
            //     2. the next accumulator after the computed one
            // The heuristic may change in the future.
            StateInfo* toUpdateStates[3]{nextState,
                                         nextState == pos.state() ? nullptr : pos.state(), nullptr};

            update_accumulator_incremental<Perspective, 3>(pos, oldestState, toUpdateStates,
                                                           psqtOnly);
        }
        else
            update_accumulator_refresh<Perspective>(pos, psqtOnly);
    }

    alignas(CacheLineSize) BiasType biases[HalfDimensions];
    alignas(CacheLineSize) WeightType weights[HalfDimensions * InputDimensions];
    alignas(CacheLineSize) PSQTWeightType psqtWeights[InputDimensions * PSQTBuckets];
};

}  // namespace DON::Eval::NNUE

#endif  // #ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
