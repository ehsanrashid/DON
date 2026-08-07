/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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

#include "accumulator.h"

#include <algorithm>
#include <type_traits>
#include <utility>

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "feature_transformer.h"  // IWYU pragma: keep
#include "features/full_threats.h"
#include "features/half_ka_hm.h"
#include "simd.h"

namespace DON::NNUE {

using namespace SIMD;

namespace {

template<typename VectorWrapper,
         IndexType Width,
         UpdateOperation... ops,
         typename ElementType,
         typename... Ts,
         std::enable_if_t<is_all_same_v<ElementType, Ts...>, bool> = true>
void fused_row_reduce(const ElementType* in, ElementType* out, const Ts* const... rows) noexcept {
    constexpr IndexType Size = Width * sizeof(ElementType) / sizeof(typename VectorWrapper::type);

    auto* vecIn  = reinterpret_cast<const typename VectorWrapper::type*>(in);
    auto* vecOut = reinterpret_cast<typename VectorWrapper::type*>(out);

    for (IndexType i = 0; i < Size; ++i)
        vecOut[i] = fused<VectorWrapper, ops...>(
          vecIn[i], reinterpret_cast<const typename VectorWrapper::type*>(rows)[i]...);
}

template<typename FeatureSet>
struct AccumulatorUpdateContext final {

    static constexpr auto Dimensions = FeatureTransformer::OutputDimensions;

    AccumulatorUpdateContext(Color                               perspective,
                             const FeatureTransformer&           featureTrans,
                             const AccumulatorState<FeatureSet>& computedState,
                             AccumulatorState<FeatureSet>&       targetState) noexcept :
        featureTransformer{featureTrans},
        computedAcc(computedState.accumulation[perspective]),
        computedPsqtAcc(computedState.psqtAccumulation[perspective]),
        targetAcc(targetState.accumulation[perspective]),
        targetPsqtAcc(targetState.psqtAccumulation[perspective]) {}

    template<UpdateOperation... ops,
             typename... Ts,
             std::enable_if_t<is_all_same_v<IndexType, Ts...>, bool> = true>
    void apply(const Ts... indices) noexcept {

        auto to_weight_vector = [&](IndexType index) noexcept {
            return &featureTransformer.weights[index * Dimensions];
        };

        auto to_psqt_weight_vector = [&](IndexType index) noexcept {
            return &featureTransformer.psqtWeights[index * PSQTBuckets];
        };

        fused_row_reduce<Vec16Wrapper, Dimensions, ops...>(  //
          computedAcc.data(), targetAcc.data(), to_weight_vector(indices)...);
        fused_row_reduce<Vec32Wrapper, PSQTBuckets, ops...>(  //
          computedPsqtAcc.data(), targetPsqtAcc.data(), to_psqt_weight_vector(indices)...);
    }

    void apply(const typename FeatureSet::IndexList& removed,
               const typename FeatureSet::IndexList& added) noexcept {

#if defined(VECTOR)
        using Tiling = Tiling<Dimensions, PSQTBuckets>;

        vec_t      acc[Tiling::RegCount];
        psqt_vec_t psqt[Tiling::PSQTRegCount];

        const auto* threatWeights = featureTransformer.threatWeights.data();

        const usize removedSize = removed.size();
        const usize addedSize   = added.size();

        // clang-format off
        for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
        {
            auto* computedTile = reinterpret_cast<const vec_t*>(&computedAcc[j * Tiling::TileHeight]);
            auto* targetTile   = reinterpret_cast<vec_t*>(&targetAcc[j * Tiling::TileHeight]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = computedTile[k];

    #if defined(USE_AVX512ICL)
            // AVX-512 ICL: 2-way unroll to break dependency chains
            IndexType i;

            i = 0;
            for (; i + 2 <= removedSize; i += 2)
            {
                usize offset0 = removed[i + 0] * Dimensions;
                usize offset1 = removed[i + 1] * Dimensions;
                const auto* column0 = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset0]);
                const auto* column1 = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset1]);

                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                {
                    acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column0[k]));
                    acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column1[k]));
                }
            }
            while (i < removedSize)
            {
                usize offset = removed[i] * Dimensions;
                const auto* column = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset]);

                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                    acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));

                ++i;
            }

            i = 0;
            for (; i + 2 <= addedSize; i += 2)
            {
                usize offset0 = added[i + 0] * Dimensions;
                usize offset1 = added[i + 1] * Dimensions;
                const auto* column0 = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset0]);
                const auto* column1 = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset1]);

                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                {
                    acc[k] = vec_add_16(acc[k], vec_convert_8_16(column0[k]));
                    acc[k] = vec_add_16(acc[k], vec_convert_8_16(column1[k]));
                }
            }
            while (i < addedSize)
            {
                usize offset = added[i] * Dimensions;
                const auto* column = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset]);

                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                    acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));

                ++i;
            }
    #else
            for (IndexType i = 0; i < removedSize; ++i)
            {
                usize offset = removed[i] * Dimensions;
                const auto* column = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset]);

        #if defined(USE_NEON)
                for (IndexType k = 0; k < Tiling::RegCount; k += 2)
                {
                    acc[k + 0] = vsubw_s8(acc[k + 0], vget_low_s8(column[k / 2]));
                    acc[k + 1] = vsubw_high_s8(acc[k + 1], column[k / 2]);
                }
        #else
                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                    acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));
        #endif
            }

            for (IndexType i = 0; i < addedSize; ++i)
            {
                usize offset = added[i] * Dimensions;
                const auto* column = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset]);

        #if defined(USE_NEON)
                for (IndexType k = 0; k < Tiling::RegCount; k += 2)
                {
                    acc[k + 0] = vaddw_s8(acc[k + 0], vget_low_s8(column[k / 2]));
                    acc[k + 1] = vaddw_high_s8(acc[k + 1], column[k / 2]);
                }
        #else
                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                    acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
        #endif
            }
    #endif
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                vec_store(&targetTile[k], acc[k]);

            threatWeights += Tiling::TileHeight;
        }

        const auto* threatPsqtWeights = featureTransformer.threatPsqtWeights.data();

        for (IndexType j = 0; j < PSQTBuckets / Tiling::PSQTTileHeight; ++j)
        {
            auto* computedPsqtTile = reinterpret_cast<const psqt_vec_t*>(&computedPsqtAcc[j * Tiling::PSQTTileHeight]);
            auto* targetPsqtTile   = reinterpret_cast<psqt_vec_t*>(&targetPsqtAcc[j * Tiling::PSQTTileHeight]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = computedPsqtTile[k];

            for (IndexType i = 0; i < removedSize; ++i)
            {
                usize offset     = removed[i] * PSQTBuckets;
                const auto* columnPsqt = reinterpret_cast<const psqt_vec_t*>(&threatPsqtWeights[offset]);

                for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                    psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (IndexType i = 0; i < addedSize; ++i)
            {
                usize offset     = added[i] * PSQTBuckets;
                const auto* columnPsqt = reinterpret_cast<const psqt_vec_t*>(&threatPsqtWeights[offset]);

                for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                    psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                vec_store_psqt(&targetPsqtTile[k], psqt[k]);

            threatPsqtWeights += Tiling::PSQTTileHeight;
        }
        // clang-format on
#else

        targetAcc     = computedAcc;
        targetPsqtAcc = computedPsqtAcc;

        for (const auto index : removed)
        {
            for (IndexType i = 0; i < Dimensions; ++i)
                targetAcc[i] -= featureTransformer.threatWeights[index * Dimensions + i];

            for (IndexType i = 0; i < PSQTBuckets; ++i)
                targetPsqtAcc[i] -= featureTransformer.threatPsqtWeights[index * PSQTBuckets + i];
        }

        for (const auto index : added)
        {
            for (IndexType i = 0; i < Dimensions; ++i)
                targetAcc[i] += featureTransformer.threatWeights[index * Dimensions + i];

            for (IndexType i = 0; i < PSQTBuckets; ++i)
                targetPsqtAcc[i] += featureTransformer.threatPsqtWeights[index * PSQTBuckets + i];
        }

#endif
    }

    const FeatureTransformer&                 featureTransformer;
    const Array<BiasType, Dimensions>&        computedAcc;
    const Array<PSQTWeightType, PSQTBuckets>& computedPsqtAcc;
    Array<BiasType, Dimensions>&              targetAcc;
    Array<PSQTWeightType, PSQTBuckets>&       targetPsqtAcc;
};

template<typename FeatureSet>
auto make_accumulator_update_context(Color                               perspective,
                                     const FeatureTransformer&           featureTransformer,
                                     const AccumulatorState<FeatureSet>& computedState,
                                     AccumulatorState<FeatureSet>&       targetState) noexcept {
    return AccumulatorUpdateContext<FeatureSet>{perspective, featureTransformer, computedState,
                                                targetState};
}

void update_accumulator_dbl_incr(Color                                  perspective,
                                 const FeatureTransformer&              featureTransformer,
                                 Square                                 kingSq,
                                 const AccumulatorState<PSQFeatureSet>& computedState,
                                 const AccumulatorState<PSQFeatureSet>& middleState,
                                 AccumulatorState<PSQFeatureSet>&       targetState) noexcept {

    assert(computedState.computed[perspective]);
    assert(!middleState.computed[perspective]);
    assert(!targetState.computed[perspective]);

    PSQFeatureSet::IndexList removed, added;
    PSQFeatureSet::append_changed_indices(perspective, kingSq, middleState.dirty, removed, added);
    PSQFeatureSet::append_changed_indices(perspective, kingSq, targetState.dirty, removed, added);

    [[maybe_unused]] const usize removedSize = removed.size();
    [[maybe_unused]] const usize addedSize   = added.size();

    // Can't capture a piece that was just involved in castling since the rook ends up in a square that the king passed
    assert(removedSize == 2 || removedSize == 3);
    assert(addedSize == 1);

    // Workaround compiler warning for uninitialized variables, replicated on
    // profile builds on windows with gcc 14.2.0.
    // Also helps with optimizations on some compilers.
    ASSUME(removedSize == 2 || removedSize == 3);
    ASSUME(addedSize == 1);

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computedState, targetState);

    if (removedSize == 2)
        updateContext
          .template apply<UpdateOperation::Add, UpdateOperation::Sub, UpdateOperation::Sub>(
            added[0], removed[0], removed[1]);
    else
        updateContext.template apply<UpdateOperation::Add, UpdateOperation::Sub,
                                     UpdateOperation::Sub, UpdateOperation::Sub>(
          added[0], removed[0], removed[1], removed[2]);

    targetState.computed[perspective] = true;
}

void update_accumulator_dbl_incr(Color                                     perspective,
                                 const FeatureTransformer&                 featureTransformer,
                                 Square                                    kingSq,
                                 const AccumulatorState<ThreatFeatureSet>& computedState,
                                 const AccumulatorState<ThreatFeatureSet>& middleState,
                                 AccumulatorState<ThreatFeatureSet>&       targetState,
                                 const DirtyPiece&                         dp2) noexcept {

    assert(computedState.computed[perspective]);
    assert(!middleState.computed[perspective]);
    assert(!targetState.computed[perspective]);

    ThreatFeatureSet::FusedData fusedData{dp2.removedSq};

    const auto* pfBase   = featureTransformer.threatWeights.data();
    usize       pfStride = FeatureTransformer::OutputDimensions;

    ThreatFeatureSet::IndexList removed, added;
    ThreatFeatureSet::append_changed_indices(perspective, kingSq, middleState.dirty, removed, added,
                                             &fusedData, true, pfBase, pfStride);
    ThreatFeatureSet::append_changed_indices(perspective, kingSq, targetState.dirty, removed, added,
                                             &fusedData, false, pfBase, pfStride);

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computedState, targetState);

    updateContext.apply(removed, added);

    targetState.computed[perspective] = true;
}

// Computes the accumulator of the next position, on given computedState
template<bool Forward, typename FeatureSet>
void update_accumulator_incr(Color                               perspective,
                             const FeatureTransformer&           featureTransformer,
                             Square                              kingSq,
                             const AccumulatorState<FeatureSet>& computedState,
                             AccumulatorState<FeatureSet>&       targetState) noexcept {

    assert(computedState.computed[perspective]);
    assert(!targetState.computed[perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    // In this case, the maximum size of both feature addition and removal is 2,
    // since incrementally updating one move at a time.
    typename FeatureSet::IndexList removed{}, added{};

    if constexpr (std::is_same_v<FeatureSet, ThreatFeatureSet>)
    {
        const auto* pfBase   = featureTransformer.threatWeights.data();
        usize       pfStride = FeatureTransformer::OutputDimensions;

        if constexpr (Forward)
            FeatureSet::append_changed_indices(perspective, kingSq, targetState.dirty, removed,
                                               added, nullptr, false, pfBase, pfStride);
        else
            FeatureSet::append_changed_indices(perspective, kingSq, computedState.dirty, added,
                                               removed, nullptr, false, pfBase, pfStride);
    }
    else
    {
        if constexpr (Forward)
            FeatureSet::append_changed_indices(perspective, kingSq, targetState.dirty, removed,
                                               added);
        else
            FeatureSet::append_changed_indices(perspective, kingSq, computedState.dirty, added,
                                               removed);
    }

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computedState, targetState);

    if constexpr (std::is_same_v<FeatureSet, ThreatFeatureSet>)
    {
        updateContext.apply(removed, added);
    }
    else
    {
        [[maybe_unused]] const usize removedSize = removed.size();
        [[maybe_unused]] const usize addedSize   = added.size();

        assert(removedSize == 1 || removedSize == 2);
        assert(addedSize == 1 || addedSize == 2);
        assert((Forward && addedSize <= removedSize) || (!Forward && removedSize <= addedSize));

        // Workaround compiler warning for uninitialized variables,
        // replicated on profile builds on windows with gcc 14.2.0.
        // Also helps with optimizations on some compilers.
        ASSUME(removedSize == 1 || removedSize == 2);
        ASSUME(addedSize == 1 || addedSize == 2);

        if (!(removedSize == 1 || removedSize == 2) || !(addedSize == 1 || addedSize == 2))
            UNREACHABLE();

        if ((Forward && removedSize == 1) || (!Forward && addedSize == 1))
        {
            assert(addedSize == 1 && removedSize == 1);
            updateContext.template apply<UpdateOperation::Add, UpdateOperation::Sub>(  //
              added[0], removed[0]);
        }
        else if (Forward && addedSize == 1)
        {
            assert(removedSize == 2);
            updateContext
              .template apply<UpdateOperation::Add, UpdateOperation::Sub, UpdateOperation::Sub>(
                added[0], removed[0], removed[1]);
        }
        else if (!Forward && removedSize == 1)
        {
            assert(addedSize == 2);
            updateContext
              .template apply<UpdateOperation::Add, UpdateOperation::Add, UpdateOperation::Sub>(
                added[0], added[1], removed[0]);
        }
        else
        {
            assert(addedSize == 2 && removedSize == 2);
            updateContext.template apply<UpdateOperation::Add, UpdateOperation::Add,
                                         UpdateOperation::Sub, UpdateOperation::Sub>(
              added[0], added[1], removed[0], removed[1]);
        }
    }

    targetState.computed[perspective] = true;
}

Bitboard changed_bb(const PieceMap& oldPieceMap, const PieceMap& newPieceMap) noexcept {
#if defined(USE_AVX512) || defined(USE_AVX2)
    Bitboard samedBB = 0;

    for (usize s : {usize(0), SQUARE_NB / 2})
    {
        __m256i oldV = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&oldPieceMap[s]));
        __m256i newV = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&newPieceMap[s]));
        __m256i cmp  = _mm256_cmpeq_epi8(oldV, newV);
        u32     mask = _mm256_movemask_epi8(cmp);
        samedBB |= Bitboard{mask} << s;
    }

    return ~samedBB;
#elif defined(USE_NEON)
    uint8x16x4_t oldV = vld4q_u8(reinterpret_cast<const u8*>(oldPieceMap.data()));
    uint8x16x4_t newV = vld4q_u8(reinterpret_cast<const u8*>(newPieceMap.data()));

    auto cmp = [&oldV, &newV](usize i) noexcept { return vceqq_u8(oldV.val[i], newV.val[i]); };

    uint8x16_t cmp_01 = vsriq_n_u8(cmp(1), cmp(0), 1);
    uint8x16_t cmp_23 = vsriq_n_u8(cmp(3), cmp(2), 1);
    uint8x16_t merged = vsriq_n_u8(cmp_23, cmp_01, 2);
    merged            = vsriq_n_u8(merged, merged, 4);
    uint8x8_t samedBB = vshrn_n_u16(vreinterpretq_u16_u8(merged), 4);

    return ~vget_lane_u64(vreinterpret_u64_u8(samedBB), 0);
#else
    Bitboard changedBB = 0;

    for (usize s = 0; s < SQUARE_NB; ++s)
        changedBB |= Bitboard{oldPieceMap[s] != newPieceMap[s]} << s;

    return changedBB;
#endif
}

void update_accumulator_refresh_cache(Color                            perspective,
                                      const FeatureTransformer&        featureTransformer,
                                      const Position&                  pos,
                                      AccumulatorState<PSQFeatureSet>& accState,
                                      AccumulatorCache&                accCache) noexcept {
    constexpr auto Dimensions = FeatureTransformer::OutputDimensions;

    Square kingSq = pos.square<KING>(perspective);

    auto& entry = accCache[kingSq][perspective];

    PSQFeatureSet::IndexList removed, added;

    auto& pieceMap = pos.piece_map();
    auto  piecesBB = pos.pieces_bb();

    Bitboard changedBB = changed_bb(entry.pieceMap, pieceMap);

    Bitboard removedBB = changedBB & entry.piecesBB;
    Bitboard addedBB   = changedBB & piecesBB;

    PSQFeatureSet::append_map_changed_indices(perspective, kingSq, entry.pieceMap, pieceMap,
                                              removedBB, addedBB, removed, added);

    entry.pieceMap = pieceMap;
    entry.piecesBB = piecesBB;

    accState.computed[perspective] = true;

#if defined(VECTOR)
    using Tiling = Tiling<Dimensions, PSQTBuckets>;

    vec_t      acc[Tiling::RegCount];
    psqt_vec_t psqt[Tiling::PSQTRegCount];

    const auto* weights = featureTransformer.weights.data();

    const usize removedSize = removed.size();
    const usize addedSize   = added.size();

    // clang-format off
    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile   = reinterpret_cast<vec_t*>(&accState.accumulation[perspective][j * Tiling::TileHeight]);
        auto* entryTile = reinterpret_cast<vec_t*>(&entry.accumulation[j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            acc[k] = entryTile[k];

        usize i = 0;
        while (i < std::min(removedSize, addedSize))
        {
            usize offsetR = removed[i] * Dimensions;
            const auto* columnR = reinterpret_cast<const vec_t*>(&weights[offsetR]);
            usize offsetA = added[i] * Dimensions;
            const auto* columnA = reinterpret_cast<const vec_t*>(&weights[offsetA]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = fused<Vec16Wrapper, UpdateOperation::Add, UpdateOperation::Sub>(acc[k], columnA[k], columnR[k]);

            ++i;
        }
        while (i < removedSize)
        {
            usize offset = removed[i] * Dimensions;
            const auto* column = reinterpret_cast<const vec_t*>(&weights[offset]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);

            ++i;
        }
        while (i < addedSize)
        {
            usize offset = added[i] * Dimensions;
            const auto* column = reinterpret_cast<const vec_t*>(&weights[offset]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);

            ++i;
        }

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
        {
            vec_store(&entryTile[k], acc[k]);
            vec_store(&accTile[k], acc[k]);
        }

        weights += Tiling::TileHeight;
    }

    const auto* psqtWeights = featureTransformer.psqtWeights.data();

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PSQTTileHeight; ++j)
    {
        auto* accPsqtTile   = reinterpret_cast<psqt_vec_t*>(&accState.psqtAccumulation[perspective][j * Tiling::PSQTTileHeight]);
        auto* entryPsqtTile = reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[j * Tiling::PSQTTileHeight]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            psqt[k] = entryPsqtTile[k];

        for (usize i = 0; i < removedSize; ++i)
        {
            usize offset = removed[i] * PSQTBuckets;
            const auto* column = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], column[k]);
        }

        for (usize i = 0; i < addedSize; ++i)
        {
            usize offset = added[i] * PSQTBuckets;
            const auto* column = reinterpret_cast<const psqt_vec_t*>(&psqtWeights[offset]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
        {
            vec_store_psqt(&accPsqtTile  [k], psqt[k]);
            vec_store_psqt(&entryPsqtTile[k], psqt[k]);
        }

        psqtWeights += Tiling::PSQTTileHeight;
    }
    // clang-format on
#else

    for (auto index : removed)
    {
        for (IndexType i = 0; i < Dimensions; ++i)
            entry.accumulation[i] -= featureTransformer.weights[index * Dimensions + i];
        for (IndexType i = 0; i < PSQTBuckets; ++i)
            entry.psqtAccumulation[i] -= featureTransformer.psqtWeights[index * PSQTBuckets + i];
    }

    for (auto index : added)
    {
        for (IndexType i = 0; i < Dimensions; ++i)
            entry.accumulation[i] += featureTransformer.weights[index * Dimensions + i];
        for (IndexType i = 0; i < PSQTBuckets; ++i)
            entry.psqtAccumulation[i] += featureTransformer.psqtWeights[index * PSQTBuckets + i];
    }

    // The accumulator of the refresh entry has been updated.
    // Now copy its content to the actual accumulator were refreshing.
    accState.accumulation[perspective]     = entry.accumulation;
    accState.psqtAccumulation[perspective] = entry.psqtAccumulation;
#endif
}

void update_threats_accumulator_full(Color                               perspective,
                                     const FeatureTransformer&           featureTransformer,
                                     const Position&                     pos,
                                     AccumulatorState<ThreatFeatureSet>& accState) noexcept {
    constexpr auto Dimensions = FeatureTransformer::OutputDimensions;

    ThreatFeatureSet::IndexList active;
    ThreatFeatureSet::append_active_indices(perspective, pos, active);

    accState.computed[perspective] = true;

#if defined(VECTOR)
    using Tiling = Tiling<Dimensions, PSQTBuckets>;

    vec_t      acc[Tiling::RegCount];
    psqt_vec_t psqt[Tiling::PSQTRegCount];

    const auto* threatWeights = featureTransformer.threatWeights.data();

    // clang-format off
    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile = reinterpret_cast<vec_t*>(&accState.accumulation[perspective][j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            acc[k] = vec_zero();

        for (IndexType i = 0; i < active.size(); ++i)
        {
            usize offset = active[i] * Dimensions;
            const auto* column = reinterpret_cast<const vec_i8_t*>(&threatWeights[offset]);

    #if defined(USE_NEON)
            for (IndexType k = 0; k < Tiling::RegCount; k += 2)
            {
                acc[k + 0] = vaddw_s8(acc[k + 0], vget_low_s8(column[k / 2]));
                acc[k + 1] = vaddw_high_s8(acc[k + 1], column[k / 2]);
            }
    #else
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
    #endif
        }

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            vec_store(&accTile[k], acc[k]);

        threatWeights += Tiling::TileHeight;
    }

    const auto* threatPsqtWeights = featureTransformer.threatPsqtWeights.data();

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PSQTTileHeight; ++j)
    {
        auto* accPsqtTile = reinterpret_cast<psqt_vec_t*>(&accState.psqtAccumulation[perspective][j * Tiling::PSQTTileHeight]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            psqt[k] = vec_zero_psqt();

        for (IndexType i = 0; i < active.size(); ++i)
        {
            usize offset = active[i] * PSQTBuckets;
            const auto* column = reinterpret_cast<const psqt_vec_t*>(&threatPsqtWeights[offset]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            vec_store_psqt(&accPsqtTile[k], psqt[k]);

        threatPsqtWeights += Tiling::PSQTTileHeight;
    }
    // clang-format on
#else

    accState.accumulation[perspective].fill(0);
    accState.psqtAccumulation[perspective].fill(0);

    for (const auto index : active)
    {
        for (IndexType i = 0; i < Dimensions; ++i)
            accState.accumulation[perspective][i] +=
              featureTransformer.threatWeights[index * Dimensions + i];

        for (IndexType i = 0; i < PSQTBuckets; ++i)
            accState.psqtAccumulation[perspective][i] +=
              featureTransformer.threatPsqtWeights[index * PSQTBuckets + i];
    }
#endif
}

}  // namespace

template<typename T>
const Array<AccumulatorState<T>, AccumulatorStack::SIZE>&
AccumulatorStack::accumulators() const noexcept {
    static_assert(std::is_same_v<T, PSQFeatureSet> || std::is_same_v<T, ThreatFeatureSet>,
                  "Invalid Feature Set Type");

    if constexpr (std::is_same_v<T, PSQFeatureSet>)
        return psqAccumulators;

    if constexpr (std::is_same_v<T, ThreatFeatureSet>)
        return threatAccumulators;
}

template<typename T>
Array<AccumulatorState<T>, AccumulatorStack::SIZE>& AccumulatorStack::mut_accumulators() noexcept {
    static_assert(std::is_same_v<T, PSQFeatureSet> || std::is_same_v<T, ThreatFeatureSet>,
                  "Invalid Feature Set Type");

    if constexpr (std::is_same_v<T, PSQFeatureSet>)
        return psqAccumulators;

    if constexpr (std::is_same_v<T, ThreatFeatureSet>)
        return threatAccumulators;
}

template<typename T>
const AccumulatorState<T>& AccumulatorStack::state() const noexcept {
    return accumulators<T>()[size - 1];
}

// Explicit template instantiations:
template const AccumulatorState<PSQFeatureSet>&    AccumulatorStack::state() const noexcept;
template const AccumulatorState<ThreatFeatureSet>& AccumulatorStack::state() const noexcept;

template<typename T>
AccumulatorState<T>& AccumulatorStack::mut_state() noexcept {
    return mut_accumulators<T>()[size - 1];
}

// Explicit template instantiations:
template AccumulatorState<PSQFeatureSet>&    AccumulatorStack::mut_state() noexcept;
template AccumulatorState<ThreatFeatureSet>& AccumulatorStack::mut_state() noexcept;

void AccumulatorStack::reset() noexcept {
    psqAccumulators[0].reset({});
    threatAccumulators[0].reset({});
    size = 1;
}

void AccumulatorStack::push(DirtyBoard&& db) noexcept {
    assert(size < SIZE);

    psqAccumulators[size].reset(std::move(db.dirtyPiece));
    threatAccumulators[size].reset(std::move(db.dirtyThreats));
    ++size;
}

void AccumulatorStack::pop() noexcept {
    assert(size > 1);

    --size;
}

void AccumulatorStack::evaluate(const Position&           pos,
                                const FeatureTransformer& featureTransformer,
                                AccumulatorCache&         accCache) noexcept {

    evaluate<PSQFeatureSet>(WHITE, pos, featureTransformer, accCache);
    evaluate<PSQFeatureSet>(BLACK, pos, featureTransformer, accCache);

    evaluate<ThreatFeatureSet>(WHITE, pos, featureTransformer, accCache);
    evaluate<ThreatFeatureSet>(BLACK, pos, featureTransformer, accCache);
}

template<typename FeatureSet>
void AccumulatorStack::evaluate(Color                     perspective,
                                const Position&           pos,
                                const FeatureTransformer& featureTransformer,
                                AccumulatorCache&         accCache) noexcept {

    auto lastAccIdx = last_usable_accumulator_index<FeatureSet>(perspective);

    if (accumulators<FeatureSet>()[lastAccIdx].computed[perspective])
    {
        update_forward_incr<FeatureSet>(perspective, pos, featureTransformer, lastAccIdx);
    }
    else
    {
        if constexpr (std::is_same_v<FeatureSet, PSQFeatureSet>)
            update_accumulator_refresh_cache(perspective, featureTransformer, pos,
                                             mut_state<PSQFeatureSet>(), accCache);
        else
            update_threats_accumulator_full(perspective, featureTransformer, pos,
                                            mut_state<ThreatFeatureSet>());

        update_backward_incr<FeatureSet>(perspective, pos, featureTransformer, lastAccIdx);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
template<typename FeatureSet>
usize AccumulatorStack::last_usable_accumulator_index(Color perspective) const noexcept {

    for (usize idx = size; idx-- > 0;)
    {
        if (accumulators<FeatureSet>()[idx].computed[perspective])
            return idx;

        if (FeatureSet::refresh_required(perspective, accumulators<FeatureSet>()[idx].dirty))
            return idx;
    }

    return 0;
}

template<typename FeatureSet>
void AccumulatorStack::update_forward_incr(Color                     perspective,
                                           const Position&           pos,
                                           const FeatureTransformer& featureTransformer,
                                           usize                     beg) noexcept {

    assert(beg < size && size <= SIZE);
    assert(accumulators<FeatureSet>()[beg].computed[perspective]);

    Square kingSq = pos.square<KING>(perspective);

    for (usize idx = beg; ++idx < size;)
    {
        if (idx + 1 < size)
        {
            auto& dp1 = mut_accumulators<PSQFeatureSet>()[idx].dirty;
            auto& dp2 = mut_accumulators<PSQFeatureSet>()[idx + 1].dirty;

            auto& accumulators = mut_accumulators<FeatureSet>();

            if constexpr (std::is_same_v<FeatureSet, PSQFeatureSet>)
            {
                if (dp1.dstSq != SQ_NONE && dp1.dstSq == dp2.removedSq)
                {
                    Square capturedSq = dp1.dstSq;
                    dp1.dstSq = dp2.removedSq = SQ_NONE;
                    update_accumulator_dbl_incr(perspective, featureTransformer, kingSq,
                                                accumulators[idx - 1], accumulators[idx],
                                                accumulators[idx + 1]);
                    dp1.dstSq = dp2.removedSq = capturedSq;

                    ++idx;
                    continue;
                }
            }
            if constexpr (std::is_same_v<FeatureSet, ThreatFeatureSet>)
            {
                if (dp2.removedSq != SQ_NONE
                    && (accumulators[idx].dirty.threateningBB & dp2.removedSq) != 0)
                {
                    update_accumulator_dbl_incr(perspective, featureTransformer, kingSq,
                                                accumulators[idx - 1], accumulators[idx],
                                                accumulators[idx + 1], dp2);
                    ++idx;
                    continue;
                }
            }
        }

        update_accumulator_incr<true>(perspective, featureTransformer, kingSq,
                                      accumulators<FeatureSet>()[idx - 1],
                                      mut_accumulators<FeatureSet>()[idx]);
    }

    assert(state<FeatureSet>().computed[perspective]);
}

template<typename FeatureSet>
void AccumulatorStack::update_backward_incr(Color                     perspective,
                                            const Position&           pos,
                                            const FeatureTransformer& featureTransformer,
                                            usize                     end) noexcept {

    assert(end < size && size <= SIZE);
    assert(state<FeatureSet>().computed[perspective]);

    Square kingSq = pos.square<KING>(perspective);

    for (usize idx = std::max(size, usize{1}) - 1; idx-- > end;)
        update_accumulator_incr<false>(perspective, featureTransformer, kingSq,
                                       accumulators<FeatureSet>()[idx + 1],
                                       mut_accumulators<FeatureSet>()[idx]);

    assert(accumulators<FeatureSet>()[end].computed[perspective]);
}

}  // namespace DON::NNUE
