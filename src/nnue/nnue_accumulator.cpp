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

#include "nnue_accumulator.h"

#include <type_traits>
#include <utility>

#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "features/half_ka_v2_hm.h"
#include "nnue_feature_transformer.h"  // IWYU pragma: keep
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

template<typename FeatureSet, IndexType Dimensions>
struct AccumulatorUpdateContext final {

    AccumulatorUpdateContext(Color                                 perspective,
                             const FeatureTransformer<Dimensions>& featureTrans,
                             const AccumulatorState<FeatureSet>&   computedState,
                             AccumulatorState<FeatureSet>&         targetState) noexcept :
        featureTransformer{featureTrans},
        computedAcc((computedState.template acc<Dimensions>()).accumulation[perspective]),
        computedPsqtAcc((computedState.template acc<Dimensions>()).psqtAccumulation[perspective]),
        targetAcc((targetState.template acc<Dimensions>()).accumulation[perspective]),
        targetPsqtAcc((targetState.template acc<Dimensions>()).psqtAccumulation[perspective]) {}

    template<UpdateOperation... ops,
             typename... Ts,
             std::enable_if_t<is_all_same_v<IndexType, Ts...>, bool> = true>
    void apply(const Ts... indices) noexcept {

        auto to_weight_vector = [&](IndexType index) {
            return &featureTransformer.weights[index * Dimensions];
        };

        auto to_psqt_weight_vector = [&](IndexType index) {
            return &featureTransformer.psqtWeights[index * PSQTBuckets];
        };

        fused_row_reduce<Vec16Wrapper, Dimensions, ops...>(  //
          computedAcc.data(), targetAcc.data(), to_weight_vector(indices)...);
        fused_row_reduce<Vec32Wrapper, PSQTBuckets, ops...>(  //
          computedPsqtAcc.data(), targetPsqtAcc.data(), to_psqt_weight_vector(indices)...);
    }

    void apply(typename FeatureSet::IndexList& added,
               typename FeatureSet::IndexList& removed) noexcept {

#if defined(VECTOR)
        using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, PSQTBuckets>;

        vec_t      acc[Tiling::RegCount];
        psqt_vec_t psqt[Tiling::PSQTRegCount];

        // clang-format off
        for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
        {
            auto* computedTile = reinterpret_cast<const vec_t*>(&computedAcc[j * Tiling::TileHeight]);
            auto* targetTile   = reinterpret_cast<vec_t*>(&targetAcc[j * Tiling::TileHeight]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = computedTile[k];

            for (IndexType i = 0; i < removed.size(); ++i)
            {
                auto  offset = removed[i] * Dimensions + j * Tiling::TileHeight;
                auto* column = reinterpret_cast<const vec_i8_t*>(&featureTransformer.threatWeights[offset]);

    #if defined(USE_NEON)
                for (IndexType k = 0; k < Tiling::RegCount; k += 2)
                {
                    acc[k + 0] = vec_sub_16(acc[k + 0], vmovl_s8(vget_low_s8(column[k / 2])));
                    acc[k + 1] = vec_sub_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
                }
    #else
                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                    acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));
    #endif
            }

            for (IndexType i = 0; i < added.size(); ++i)
            {
                auto  offset = added[i] * Dimensions + j * Tiling::TileHeight;
                auto* column = reinterpret_cast<const vec_i8_t*>(&featureTransformer.threatWeights[offset]);

    #if defined(USE_NEON)
                for (IndexType k = 0; k < Tiling::RegCount; k += 2)
                {
                    acc[k + 0] = vec_add_16(acc[k + 0], vmovl_s8(vget_low_s8(column[k / 2])));
                    acc[k + 1] = vec_add_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
                }
    #else
                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                    acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
    #endif
            }

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                vec_store(&targetTile[k], acc[k]);
        }

        for (IndexType j = 0; j < PSQTBuckets / Tiling::PSQTTileHeight; ++j)
        {
            auto* computedPsqtTile = reinterpret_cast<const psqt_vec_t*>(&computedPsqtAcc[j * Tiling::PSQTTileHeight]);
            auto* targetPsqtTile   = reinterpret_cast<psqt_vec_t*>(&targetPsqtAcc[j * Tiling::PSQTTileHeight]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = computedPsqtTile[k];

            for (IndexType i = 0; i < removed.size(); ++i)
            {
                auto  offset     = removed[i] * PSQTBuckets + j * Tiling::PSQTTileHeight;
                auto* columnPsqt = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.threatPsqtWeights[offset]);

                for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                    psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (IndexType i = 0; i < added.size(); ++i)
            {
                auto  offset     = added[i] * PSQTBuckets + j * Tiling::PSQTTileHeight;
                auto* columnPsqt = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.threatPsqtWeights[offset]);

                for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                    psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
            }

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                vec_store_psqt(&targetPsqtTile[k], psqt[k]);
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

    const FeatureTransformer<Dimensions>&        featureTransformer;
    const StdArray<BiasType, Dimensions>&        computedAcc;
    const StdArray<PSQTWeightType, PSQTBuckets>& computedPsqtAcc;
    StdArray<BiasType, Dimensions>&              targetAcc;
    StdArray<PSQTWeightType, PSQTBuckets>&       targetPsqtAcc;
};

template<typename FeatureSet, IndexType Dimensions>
auto make_accumulator_update_context(Color                                 perspective,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     const AccumulatorState<FeatureSet>&   computedState,
                                     AccumulatorState<FeatureSet>&         targetState) noexcept {
    return AccumulatorUpdateContext<FeatureSet, Dimensions>{perspective, featureTransformer,
                                                            computedState, targetState};
}

template<IndexType TransformedFeatureDimensions>
void update_accumulator_incremental_double(
  Color                                                   perspective,
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  Square                                                  kingSq,
  const AccumulatorState<PSQFeatureSet>&                  computedState,
  AccumulatorState<PSQFeatureSet>&                        middleState,
  AccumulatorState<PSQFeatureSet>&                        targetState) noexcept {

    assert(computedState.acc<TransformedFeatureDimensions>().computed[perspective]);
    assert(!middleState.acc<TransformedFeatureDimensions>().computed[perspective]);
    assert(!targetState.acc<TransformedFeatureDimensions>().computed[perspective]);

    PSQFeatureSet::IndexList removed, added;
    PSQFeatureSet::append_changed_indices(perspective, kingSq, middleState.dirtyType, removed,
                                          added);
    // Can't capture a piece that was just involved in castling since the rook ends up
    // in a square that the king passed
    assert(added.size() < 2);
    PSQFeatureSet::append_changed_indices(perspective, kingSq, targetState.dirtyType, removed,
                                          added);

    assert(added.size() == 1);
    assert(removed.size() == 2 || removed.size() == 3);

    // Workaround compiler warning for uninitialized variables, replicated on
    // profile builds on windows with gcc 14.2.0.
    // TODO remove once unneeded
    ASSUME(added.size() == 1);
    ASSUME(removed.size() == 2 || removed.size() == 3);

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computedState, targetState);

    if (removed.size() == 2)
        updateContext.template apply<Add, Sub, Sub>(added[0], removed[0], removed[1]);
    else
        updateContext.template apply<Add, Sub, Sub, Sub>(added[0], removed[0], removed[1],
                                                         removed[2]);

    targetState.acc<TransformedFeatureDimensions>().computed[perspective] = true;
}

template<IndexType TransformedFeatureDimensions>
void update_accumulator_incremental_double(
  Color                                                   perspective,
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  Square                                                  kingSq,
  const AccumulatorState<ThreatFeatureSet>&               computedState,
  AccumulatorState<ThreatFeatureSet>&                     middleState,
  AccumulatorState<ThreatFeatureSet>&                     targetState,
  const DirtyPiece&                                       dp2) noexcept {

    assert(computedState.acc<TransformedFeatureDimensions>().computed[perspective]);
    assert(!middleState.acc<TransformedFeatureDimensions>().computed[perspective]);
    assert(!targetState.acc<TransformedFeatureDimensions>().computed[perspective]);

    ThreatFeatureSet::FusedData fusedData;

    fusedData.dp2removedSq = dp2.removeSq;

    ThreatFeatureSet::IndexList removed, added;
    ThreatFeatureSet::append_changed_indices(perspective, kingSq, middleState.dirtyType, removed,
                                             added, &fusedData, true);
    ThreatFeatureSet::append_changed_indices(perspective, kingSq, targetState.dirtyType, removed,
                                             added, &fusedData, false);

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computedState, targetState);

    updateContext.apply(added, removed);

    targetState.acc<TransformedFeatureDimensions>().computed[perspective] = true;
}

// Computes the accumulator of the next position, on given computedState
template<bool Forward, typename FeatureSet, IndexType TransformedFeatureDimensions>
void update_accumulator_incremental(
  Color                                                   perspective,
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  Square                                                  kingSq,
  const AccumulatorState<FeatureSet>&                     computedState,
  AccumulatorState<FeatureSet>&                           targetState) noexcept {

    assert((computedState.template acc<TransformedFeatureDimensions>()).computed[perspective]);
    assert(!(targetState.template acc<TransformedFeatureDimensions>()).computed[perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    // In this case, the maximum size of both feature addition and removal is 2,
    // since incrementally updating one move at a time.
    typename FeatureSet::IndexList removed, added;
    if constexpr (Forward)
        FeatureSet::append_changed_indices(perspective, kingSq, targetState.dirtyType, removed,
                                           added);
    else
        FeatureSet::append_changed_indices(perspective, kingSq, computedState.dirtyType, added,
                                           removed);

    auto updateContext =
      make_accumulator_update_context(perspective, featureTransformer, computedState, targetState);

    if constexpr (std::is_same_v<FeatureSet, ThreatFeatureSet>)
    {
        updateContext.apply(added, removed);
    }
    else
    {
        assert(added.size() == 1 || added.size() == 2);
        assert(removed.size() == 1 || removed.size() == 2);
        assert((Forward && added.size() <= removed.size())
               || (!Forward && removed.size() <= added.size()));

        // Workaround compiler warning for uninitialized variables, replicated on
        // profile builds on windows with gcc 14.2.0.
        // TODO remove once unneeded
        ASSUME(added.size() == 1 || added.size() == 2);
        ASSUME(removed.size() == 1 || removed.size() == 2);

        if ((Forward && removed.size() == 1) || (!Forward && added.size() == 1))
        {
            assert(added.size() == 1 && removed.size() == 1);
            updateContext.template apply<Add, Sub>(added[0], removed[0]);
        }
        else if (Forward && added.size() == 1)
        {
            assert(removed.size() == 2);
            updateContext.template apply<Add, Sub, Sub>(added[0], removed[0], removed[1]);
        }
        else if (!Forward && removed.size() == 1)
        {
            assert(added.size() == 2);
            updateContext.template apply<Add, Add, Sub>(added[0], added[1], removed[0]);
        }
        else
        {
            assert(added.size() == 2 && removed.size() == 2);
            updateContext.template apply<Add, Add, Sub, Sub>(added[0], added[1], removed[0],
                                                             removed[1]);
        }
    }

    (targetState.template acc<TransformedFeatureDimensions>()).computed[perspective] = true;
}

Bitboard changed_bb(const StdArray<Piece, SQUARE_NB>& oldPieces,
                    const StdArray<Piece, SQUARE_NB>& newPieces) noexcept {
#if defined(USE_AVX512) || defined(USE_AVX2)
    Bitboard samedBB = 0;
    for (std::size_t s = 0; s < SQUARE_NB; s += 32)
    {
        __m256i oldV     = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&oldPieces[s]));
        __m256i newV     = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&newPieces[s]));
        __m256i cmpEqual = _mm256_cmpeq_epi8(oldV, newV);

        std::uint32_t equalMask = _mm256_movemask_epi8(cmpEqual);
        samedBB |= Bitboard(equalMask) << s;
    }
    return ~samedBB;
#else
    Bitboard changedBB = 0;
    for (std::size_t s = 0; s < SQUARE_NB; ++s)
        changedBB |= Bitboard(oldPieces[s] != newPieces[s]) << s;
    return changedBB;
#endif
}

template<IndexType Dimensions>
void update_accumulator_refresh_cache(Color                                 perspective,
                                      const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState<PSQFeatureSet>&      accState,
                                      AccumulatorCaches::Cache<Dimensions>& cache) noexcept {

    Square kingSq = pos.square<KING>(perspective);

    auto& entry = cache[kingSq][perspective];

    PSQFeatureSet::IndexList removed, added;

    const auto& pieceMap = pos.piece_map();
    const auto  piecesBB = pos.pieces_bb();

    Bitboard changedBB = changed_bb(entry.pieceMap, pieceMap);

    Bitboard removedBB = changedBB & entry.piecesBB;
    PSQFeatureSet::append_active_indices(perspective, kingSq, entry.pieceMap, removedBB, removed);

    Bitboard addedBB = changedBB & piecesBB;
    PSQFeatureSet::append_active_indices(perspective, kingSq, pieceMap, addedBB, added);

    entry.pieceMap = pieceMap;
    entry.piecesBB = piecesBB;

    auto& accumulator = accState.acc<Dimensions>();

    accumulator.computed[perspective] = true;

#if defined(VECTOR)
    using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, PSQTBuckets>;

    vec_t      acc[Tiling::RegCount];
    psqt_vec_t psqt[Tiling::PSQTRegCount];

    // clang-format off
    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile   = reinterpret_cast<vec_t*>(&accumulator.accumulation[perspective][j * Tiling::TileHeight]);
        auto* entryTile = reinterpret_cast<vec_t*>(&entry.accumulation[j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            acc[k] = entryTile[k];

        std::size_t i = 0;
        for (; i < std::min(removed.size(), added.size()); ++i)
        {
            auto  offsetR = removed[i] * Dimensions + j * Tiling::TileHeight;
            auto* columnR = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR]);
            auto  offsetA = added[i] * Dimensions + j * Tiling::TileHeight;
            auto* columnA = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = fused<Vec16Wrapper, Add, Sub>(acc[k], columnA[k], columnR[k]);
        }

        for (; i < removed.size(); ++i)
        {
            auto  offset = removed[i] * Dimensions + j * Tiling::TileHeight;
            auto* column = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offset]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);
        }

        for (; i < added.size(); ++i)
        {
            auto  offset = added[i] * Dimensions + j * Tiling::TileHeight;
            auto* column = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offset]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
        {
            vec_store(&entryTile[k], acc[k]);
            vec_store(&accTile[k], acc[k]);
        }
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PSQTTileHeight; ++j)
    {
        auto* accPsqtTile   = reinterpret_cast<psqt_vec_t*>(&accumulator.psqtAccumulation[perspective][j * Tiling::PSQTTileHeight]);
        auto* entryPsqtTile = reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[j * Tiling::PSQTTileHeight]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            psqt[k] = entryPsqtTile[k];

        for (std::size_t i = 0; i < removed.size(); ++i)
        {
            auto  offset = removed[i] * PSQTBuckets + j * Tiling::PSQTTileHeight;
            auto* column = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offset]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], column[k]);
        }

        for (std::size_t i = 0; i < added.size(); ++i)
        {
            auto  offset = added[i] * PSQTBuckets + j * Tiling::PSQTTileHeight;
            auto* column = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[offset]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
        {
            vec_store_psqt(&accPsqtTile  [k], psqt[k]);
            vec_store_psqt(&entryPsqtTile[k], psqt[k]);
        }
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
    accumulator.accumulation[perspective]     = entry.accumulation;
    accumulator.psqtAccumulation[perspective] = entry.psqtAccumulation;
#endif
}

template<IndexType Dimensions>
void update_threats_accumulator_full(Color                                 perspective,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     const Position&                       pos,
                                     AccumulatorState<ThreatFeatureSet>&   accState) noexcept {

    ThreatFeatureSet::IndexList active;
    ThreatFeatureSet::append_active_indices(perspective, pos, active);

    auto& accumulator = accState.acc<Dimensions>();

    accumulator.computed[perspective] = true;

#if defined(VECTOR)
    using Tiling [[maybe_unused]] = SIMDTiling<Dimensions, PSQTBuckets>;

    vec_t      acc[Tiling::RegCount];
    psqt_vec_t psqt[Tiling::PSQTRegCount];

    // clang-format off
    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile = reinterpret_cast<vec_t*>(&accumulator.accumulation[perspective][j * Tiling::TileHeight]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            acc[k] = vec_zero();

        IndexType i = 0;

        for (; i < active.size(); ++i)
        {
            auto  offset = active[i] * Dimensions + j * Tiling::TileHeight;
            auto* column = reinterpret_cast<const vec_i8_t*>(&featureTransformer.threatWeights[offset]);

    #if defined(USE_NEON)
            for (IndexType k = 0; k < Tiling::RegCount; k += 2)
            {
                acc[k + 0] = vec_add_16(acc[k + 0], vmovl_s8(vget_low_s8(column[k / 2])));
                acc[k + 1] = vec_add_16(acc[k + 1], vmovl_high_s8(column[k / 2]));
            }
    #else
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
    #endif
        }

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            vec_store(&accTile[k], acc[k]);
    }

    for (IndexType j = 0; j < PSQTBuckets / Tiling::PSQTTileHeight; ++j)
    {
        auto* accPsqtTile = reinterpret_cast<psqt_vec_t*>(&accumulator.psqtAccumulation[perspective][j * Tiling::PSQTTileHeight]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            psqt[k] = vec_zero_psqt();

        for (IndexType i = 0; i < active.size(); ++i)
        {
            auto  offset = active[i] * PSQTBuckets + j * Tiling::PSQTTileHeight;
            auto* column = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.threatPsqtWeights[offset]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            vec_store_psqt(&accPsqtTile[k], psqt[k]);
    }
    // clang-format on
#else

    accumulator.accumulation[perspective].fill(0);
    accumulator.psqtAccumulation[perspective].fill(0);

    for (const auto index : active)
    {
        for (IndexType i = 0; i < Dimensions; ++i)
            accumulator.accumulation[perspective][i] +=
              featureTransformer.threatWeights[index * Dimensions + i];

        for (IndexType i = 0; i < PSQTBuckets; ++i)
            accumulator.psqtAccumulation[perspective][i] +=
              featureTransformer.threatPsqtWeights[index * PSQTBuckets + i];
    }
#endif
}

}  // namespace

template<typename T>
const StdArray<AccumulatorState<T>, AccumulatorStack::MAX_SIZE>&
AccumulatorStack::accumulators() const noexcept {
    static_assert(std::is_same_v<T, PSQFeatureSet> || std::is_same_v<T, ThreatFeatureSet>,
                  "Invalid Feature Set Type");

    if constexpr (std::is_same_v<T, PSQFeatureSet>)
        return psqAccumulators;

    if constexpr (std::is_same_v<T, ThreatFeatureSet>)
        return threatAccumulators;
}

template<typename T>
StdArray<AccumulatorState<T>, AccumulatorStack::MAX_SIZE>&
AccumulatorStack::mut_accumulators() noexcept {
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
    assert(size < MAX_SIZE);
    psqAccumulators[size].reset(std::move(db.dp));
    threatAccumulators[size].reset(std::move(db.dts));
    ++size;
}

void AccumulatorStack::pop() noexcept {
    assert(size > 1);
    --size;
}

template<IndexType Dimensions>
void AccumulatorStack::evaluate(const Position&                       pos,
                                const FeatureTransformer<Dimensions>& featureTransformer,
                                AccumulatorCaches::Cache<Dimensions>& cache) noexcept {

    constexpr bool UseThreats = Dimensions == BigTransformedFeatureDimensions;

    evaluate<PSQFeatureSet>(WHITE, pos, featureTransformer, cache);
    if (UseThreats)
        evaluate<ThreatFeatureSet>(WHITE, pos, featureTransformer, cache);
    evaluate<PSQFeatureSet>(BLACK, pos, featureTransformer, cache);
    if (UseThreats)
        evaluate<ThreatFeatureSet>(BLACK, pos, featureTransformer, cache);
}

template<typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::evaluate(Color                                 perspective,
                                const Position&                       pos,
                                const FeatureTransformer<Dimensions>& featureTransformer,
                                AccumulatorCaches::Cache<Dimensions>& cache) noexcept {

    std::size_t lastAccIdx = last_usable_accumulator_index<FeatureSet, Dimensions>(perspective);

    if ((accumulators<FeatureSet>()[lastAccIdx].template acc<Dimensions>()).computed[perspective])
    {
        forward_update_incremental<FeatureSet>(perspective, pos, featureTransformer, lastAccIdx);
    }
    else
    {
        if constexpr (std::is_same_v<FeatureSet, PSQFeatureSet>)
            update_accumulator_refresh_cache(perspective, featureTransformer, pos,
                                             mut_state<PSQFeatureSet>(), cache);
        else
            update_threats_accumulator_full(perspective, featureTransformer, pos,
                                            mut_state<ThreatFeatureSet>());

        backward_update_incremental<FeatureSet>(perspective, pos, featureTransformer, lastAccIdx);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
template<typename FeatureSet, IndexType Dimensions>
std::size_t AccumulatorStack::last_usable_accumulator_index(Color perspective) const noexcept {

    for (std::size_t idx = size - 1; idx > 0; --idx)
    {
        if ((accumulators<FeatureSet>()[idx].template acc<Dimensions>()).computed[perspective])
            return idx;

        if (FeatureSet::requires_refresh(perspective, accumulators<FeatureSet>()[idx].dirtyType))
            return idx;
    }

    return 0;
}

template<typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::forward_update_incremental(
  Color                                 perspective,
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  std::size_t                           begin) noexcept {

    assert(begin < size && size <= MAX_SIZE);
    assert((accumulators<FeatureSet>()[begin].template acc<Dimensions>()).computed[perspective]);

    Square kingSq = pos.square<KING>(perspective);

    for (std::size_t idx = begin; ++idx < size;)
    {
        if (idx + 1 < size)
        {
            auto& dp1 = mut_accumulators<PSQFeatureSet>()[idx].dirtyType;
            auto& dp2 = mut_accumulators<PSQFeatureSet>()[idx + 1].dirtyType;

            auto& accumulators = mut_accumulators<FeatureSet>();

            if constexpr (std::is_same_v<FeatureSet, PSQFeatureSet>)
            {
                if (is_ok(dp1.dstSq) && dp1.dstSq == dp2.removeSq)
                {
                    Square capturedSq = dp1.dstSq;
                    dp1.dstSq = dp2.removeSq = SQ_NONE;
                    update_accumulator_incremental_double(perspective, featureTransformer, kingSq,
                                                          accumulators[idx - 1], accumulators[idx],
                                                          accumulators[idx + 1]);
                    dp1.dstSq = dp2.removeSq = capturedSq;

                    ++idx;
                    continue;
                }
            }
            if constexpr (std::is_same_v<FeatureSet, ThreatFeatureSet>)
            {
                if (is_ok(dp2.removeSq)
                    && (accumulators[idx].dirtyType.threateningBB & dp2.removeSq))
                {
                    update_accumulator_incremental_double(perspective, featureTransformer, kingSq,
                                                          accumulators[idx - 1], accumulators[idx],
                                                          accumulators[idx + 1], dp2);
                    ++idx;
                    continue;
                }
            }
        }

        update_accumulator_incremental<true>(perspective, featureTransformer, kingSq,
                                             accumulators<FeatureSet>()[idx - 1],
                                             mut_accumulators<FeatureSet>()[idx]);
    }

    assert((state<FeatureSet>().template acc<Dimensions>()).computed[perspective]);
}

template<typename FeatureSet, IndexType Dimensions>
void AccumulatorStack::backward_update_incremental(
  Color                                 perspective,
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  std::size_t                           end) noexcept {

    assert(end < size && size <= MAX_SIZE);
    assert((state<FeatureSet>().template acc<Dimensions>()).computed[perspective]);

    Square kingSq = pos.square<KING>(perspective);

    for (std::size_t idx = size ? size - 1 : 0; idx-- > end;)
        update_accumulator_incremental<false>(perspective, featureTransformer, kingSq,
                                              accumulators<FeatureSet>()[idx + 1],
                                              mut_accumulators<FeatureSet>()[idx]);

    assert((accumulators<FeatureSet>()[end].template acc<Dimensions>()).computed[perspective]);
}

// Explicit template instantiations:
template void AccumulatorStack::evaluate<BigTransformedFeatureDimensions>(
  const Position&                                            pos,
  const FeatureTransformer<BigTransformedFeatureDimensions>& featureTransformer,
  AccumulatorCaches::Cache<BigTransformedFeatureDimensions>& cache) noexcept;
template void AccumulatorStack::evaluate<SmallTransformedFeatureDimensions>(
  const Position&                                              pos,
  const FeatureTransformer<SmallTransformedFeatureDimensions>& featureTransformer,
  AccumulatorCaches::Cache<SmallTransformedFeatureDimensions>& cache) noexcept;

}  // namespace DON::NNUE
