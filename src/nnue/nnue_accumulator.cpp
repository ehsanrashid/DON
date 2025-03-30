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

#include <cassert>
#include <initializer_list>
#include <memory>

#include "../bitboard.h"
#include "../position.h"
#include "../types.h"
#include "network.h"
#include "nnue_feature_transformer.h"

namespace DON::NNUE {

#if defined(__GNUC__) && !defined(__clang__)
    #define assume(cond) \
        do \
        { \
            if (!(cond)) \
                __builtin_unreachable(); \
        } while (0)
#else
    // do nothing for other compilers
    #define assume(cond)
#endif

namespace {

// Computes the accumulator of the next position, on given computedState
template<Color Perspective, bool Forward, IndexType TransformedFeatureDimensions>
void update_accumulator_incremental(
  const FeatureTransformer<TransformedFeatureDimensions>& featureTransformer,
  Square                                                  ksq,
  const AccumulatorState&                                 computedState,
  AccumulatorState&                                       targetState) noexcept {
    assert((computedState.acc<TransformedFeatureDimensions>()).computed[Perspective]);
    assert(!(targetState.acc<TransformedFeatureDimensions>()).computed[Perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    // In this case, the maximum size of both feature addition and removal is 2,
    // since incrementally updating one move at a time.
    FeatureSet::IndexList removed, added;
    if constexpr (Forward)
        FeatureSet::append_changed_indices<Perspective>(ksq, targetState.dirtyPiece, removed,
                                                        added);
    else
        FeatureSet::append_changed_indices<Perspective>(ksq, computedState.dirtyPiece, added,
                                                        removed);

    if (removed.size() == 0 && added.size() == 0)
    {
        std::memcpy((targetState.acc<TransformedFeatureDimensions>()).accumulation[Perspective],
                    (computedState.acc<TransformedFeatureDimensions>()).accumulation[Perspective],
                    TransformedFeatureDimensions * sizeof(BiasType));
        std::memcpy(
          (targetState.acc<TransformedFeatureDimensions>()).psqtAccumulation[Perspective],
          (computedState.acc<TransformedFeatureDimensions>()).psqtAccumulation[Perspective],
          PSQTBuckets * sizeof(PSQTWeightType));
    }
    else
    {
        // clang-format off
            assert(added.size() == 1 || added.size() == 2);
            assert(removed.size() == 1 || removed.size() == 2);
            assert(( Forward && added.size() <= removed.size())
                || (!Forward && removed.size() <= added.size()));

        // Workaround compiler warning for uninitialized variables,
        // replicated on profile builds on windows with gcc 14.2.0.
        assume(added.size() == 1 || added.size() == 2);
        assume(removed.size() == 1 || removed.size() == 2);

#if defined(VECTOR)
            
            auto* accIn  = reinterpret_cast<const vec_t*>(&(computedState.acc<TransformedFeatureDimensions>()).accumulation[Perspective][0]);
            auto* accOut = reinterpret_cast<      vec_t*>(&(targetState.acc<TransformedFeatureDimensions>()).accumulation[Perspective][0]);

            auto  offsetA0 = TransformedFeatureDimensions * added[0];
            auto* columnA0 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA0]);
            auto  offsetR0 = TransformedFeatureDimensions * removed[0];
            auto* columnR0 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR0]);

            if ((Forward && removed.size() == 1)
                || (!Forward && added.size() == 1))  // added.size() == removed.size() == 1
            {
                for (IndexType i = 0; i < TransformedFeatureDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                    accOut[i] = vec_add_16(vec_sub_16(accIn[i], columnR0[i]), columnA0[i]);
            }
            else if (Forward && added.size() == 1)  // removed.size() == 2
            {
                auto  offsetR1 = TransformedFeatureDimensions * removed[1];
                auto* columnR1 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR1]);

                for (IndexType i = 0; i < TransformedFeatureDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                    accOut[i] = vec_sub_16(vec_add_16(accIn   [i], columnA0[i]),
                                           vec_add_16(columnR0[i], columnR1[i]));
            }
            else if (!Forward && removed.size() == 1)  // added.size() == 2
            {
                auto  offsetA1 = TransformedFeatureDimensions * added[1];
                auto* columnA1 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA1]);

                for (IndexType i = 0; i < TransformedFeatureDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                    accOut[i] = vec_add_16(vec_add_16(accIn   [i], columnA0[i]),
                                           vec_sub_16(columnA1[i], columnR0[i]));
            }
            else // added.size() == removed.size() == 2
            {
                auto  offsetA1 = TransformedFeatureDimensions * added[1];
                auto* columnA1 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA1]);
                auto  offsetR1 = TransformedFeatureDimensions * removed[1];
                auto* columnR1 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR1]);

                for (IndexType i = 0; i < TransformedFeatureDimensions * sizeof(WeightType) / sizeof(vec_t); ++i)
                    accOut[i] = vec_add_16(accIn[i], vec_sub_16(vec_add_16(columnA0[i], columnA1[i]),
                                                                vec_add_16(columnR0[i], columnR1[i])));
            }

            auto* psqtAccIn  = reinterpret_cast<const psqt_vec_t*>(&(computedState.acc<TransformedFeatureDimensions>()).psqtAccumulation[Perspective][0]);
            auto* psqtAccOut = reinterpret_cast<      psqt_vec_t*>(&(targetState.acc<TransformedFeatureDimensions>()).psqtAccumulation[Perspective][0]);

            auto  psqtOffsetA0 = PSQTBuckets * added[0];
            auto* psqtColumnA0 = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[psqtOffsetA0]);
            auto  psqtOffsetR0 = PSQTBuckets * removed[0];
            auto* psqtColumnR0 = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[psqtOffsetR0]);

            if ((Forward && removed.size() == 1)
                || (!Forward && added.size() == 1))  // added.size() == removed.size() == 1
            {
                for (IndexType i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t); ++i)
                    psqtAccOut[i] = vec_add_psqt_32(vec_sub_psqt_32(psqtAccIn[i], psqtColumnR0[i]), psqtColumnA0[i]);
            }
            else if (Forward && added.size() == 1)  // removed.size() == 2
            {
                auto  psqtOffsetR1 = PSQTBuckets * removed[1];
                auto* psqtColumnR1 = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[psqtOffsetR1]);

                for (IndexType i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t); ++i)
                    psqtAccOut[i] = vec_sub_psqt_32(vec_add_psqt_32(psqtAccIn   [i], psqtColumnA0[i]),
                                                    vec_add_psqt_32(psqtColumnR0[i], psqtColumnR1[i]));
            }
            else if (!Forward && removed.size() == 1)  // added.size() == 2
            {
                auto  psqtOffsetA1 = PSQTBuckets * added[1];
                auto* psqtColumnA1 = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[psqtOffsetA1]);

                for (IndexType i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t); ++i)
                    psqtAccOut[i] = vec_add_psqt_32(vec_add_psqt_32(psqtAccIn   [i], psqtColumnA0[i]),
                                                    vec_sub_psqt_32(psqtColumnA1[i], psqtColumnR0[i]));
            }
            else
            {
                auto  psqtOffsetA1 = PSQTBuckets * added[1];
                auto* psqtColumnA1 = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[psqtOffsetA1]);
                auto  psqtOffsetR1 = PSQTBuckets * removed[1];
                auto* psqtColumnR1 = reinterpret_cast<const psqt_vec_t*>(&featureTransformer.psqtWeights[psqtOffsetR1]);

                for (IndexType i = 0; i < PSQTBuckets * sizeof(PSQTWeightType) / sizeof(psqt_vec_t); ++i)
                    psqtAccOut[i] = vec_add_psqt_32(psqtAccIn[i],
                                        vec_sub_psqt_32(vec_add_psqt_32(psqtColumnA0[i], psqtColumnA1[i]),
                                                        vec_add_psqt_32(psqtColumnR0[i], psqtColumnR1[i])));
            }

#else
            std::memcpy((targetState.acc<TransformedFeatureDimensions>()).accumulation[Perspective],
                        (computedState.acc<TransformedFeatureDimensions>()).accumulation[Perspective],
                        TransformedFeatureDimensions * sizeof(BiasType));
            std::memcpy((targetState.acc<TransformedFeatureDimensions>()).psqtAccumulation[Perspective],
                        (computedState.acc<TransformedFeatureDimensions>()).psqtAccumulation[Perspective],
                        PSQTBuckets * sizeof(PSQTWeightType));

            // Difference calculation for the deactivated features
            for (auto index : removed)
            {
                for (IndexType i = 0; i < TransformedFeatureDimensions; ++i)
                    (targetState.acc<TransformedFeatureDimensions>()).accumulation[Perspective][i] -= featureTransformer.weights[index * TransformedFeatureDimensions + i];

                for (IndexType i = 0; i < PSQTBuckets; ++i)
                    (targetState.acc<TransformedFeatureDimensions>()).psqtAccumulation[Perspective][i] -= featureTransformer.psqtWeights[index * PSQTBuckets + i];
            }

            // Difference calculation for the activated features
            for (auto index : added)
            {
                for (IndexType i = 0; i < TransformedFeatureDimensions; ++i)
                    (targetState.acc<TransformedFeatureDimensions>()).accumulation[Perspective][i] += featureTransformer.weights[index * TransformedFeatureDimensions + i];

                for (IndexType i = 0; i < PSQTBuckets; ++i)
                    (targetState.acc<TransformedFeatureDimensions>()).psqtAccumulation[Perspective][i] += featureTransformer.psqtWeights[index * PSQTBuckets + i];
            }
#endif
        // clang-format on
    }

    (targetState.acc<TransformedFeatureDimensions>()).computed[Perspective] = true;
}

template<Color Perspective, IndexType Dimensions>
void update_accumulator_refresh_cache(const FeatureTransformer<Dimensions>& featureTransformer,
                                      const Position&                       pos,
                                      AccumulatorState&                     accState,
                                      Cache<Dimensions>&                    cache) noexcept {
#if defined(VECTOR)
    using Tiling = SIMDTiling<Dimensions>;
#endif

    Square ksq = pos.king_square(Perspective);

    auto& entry = cache[ksq][Perspective];

    FeatureSet::IndexList removed, added;

    for (Color c : {WHITE, BLACK})
    {
        for (PieceType pt = PAWN; pt <= KING; ++pt)
        {
            Piece    piece    = make_piece(c, pt);
            Bitboard oldBB    = entry.colorBB[c] & entry.typeBB[pt];
            Bitboard newBB    = pos.pieces(c, pt);
            Bitboard removeBB = oldBB & ~newBB;
            Bitboard addBB    = newBB & ~oldBB;

            while (removeBB)
            {
                Square s = pop_lsb(removeBB);
                removed.push_back(FeatureSet::make_index<Perspective>(s, piece, ksq));
            }
            while (addBB)
            {
                Square s = pop_lsb(addBB);
                added.push_back(FeatureSet::make_index<Perspective>(s, piece, ksq));
            }
        }
    }

    auto& accumulator = accState.acc<Dimensions>();

    accumulator.computed[Perspective] = true;

#if defined(VECTOR)

    vec_t      acc[Tiling::RegCount];
    psqt_vec_t psqt[Tiling::PSQTRegCount];

    bool last3Combine =
      std::abs(int(removed.size()) - int(added.size())) == 1 && removed.size() + added.size() > 2;

    // clang-format off
        for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
        {
            auto* accTile = reinterpret_cast<vec_t*>(
              &accumulator.accumulation[Perspective][j * Tiling::TileHeight]);
            auto* entryTile =
              reinterpret_cast<vec_t*>(&entry.accumulation[j * Tiling::TileHeight]);

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = entryTile[k];

            std::size_t i = 0;
            for (; i < std::min(removed.size(), added.size()) - last3Combine; ++i)
            {
                auto  offsetR = removed[i] * Dimensions + j * Tiling::TileHeight;
                auto* columnR = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR]);
                auto  offsetA = added[i] * Dimensions + j * Tiling::TileHeight;
                auto* columnA = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA]);

                for (IndexType k = 0; k < Tiling::RegCount; ++k)
                    acc[k] = vec_add_16(acc[k], vec_sub_16(columnA[k], columnR[k]));
            }

            if (last3Combine)
            {
                auto  offsetR = removed[i] * Dimensions + j * Tiling::TileHeight;
                auto* columnR = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR]);
                auto  offsetA = added[i] * Dimensions + j * Tiling::TileHeight;
                auto* columnA = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA]);

                if (removed.size() > added.size())
                {
                    auto  offsetR2 = removed[i + 1] * Dimensions + j * Tiling::TileHeight;
                    auto* columnR2 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetR2]);

                    for (IndexType k = 0; k < Tiling::RegCount; ++k)
                        acc[k] = vec_sub_16(vec_add_16(acc    [k], columnA[k]),
                                            vec_add_16(columnR[k], columnR2[k]));
                }
                else
                {
                    auto  offsetA2 = added[i + 1] * Dimensions + j * Tiling::TileHeight;
                    auto* columnA2 = reinterpret_cast<const vec_t*>(&featureTransformer.weights[offsetA2]);

                    for (IndexType k = 0; k < Tiling::RegCount; ++k)
                        acc[k] = vec_add_16(vec_sub_16(acc    [k], columnR[k]),
                                            vec_add_16(columnA[k], columnA2[k]));
                }
            }
            else
            {
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
            }

            for (IndexType k = 0; k < Tiling::RegCount; ++k)
            {
                vec_store(&entryTile[k], acc[k]);
                vec_store(&accTile[k], acc[k]);
            }
        }

        for (IndexType j = 0; j < PSQTBuckets / Tiling::PSQTTileHeight; ++j)
        {
            auto* psqtAccTile = reinterpret_cast<psqt_vec_t*>(
              &accumulator.psqtAccumulation[Perspective][j * Tiling::PSQTTileHeight]);
            auto* psqtEntryTile =
              reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[j * Tiling::PSQTTileHeight]);

            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = psqtEntryTile[k];

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
                vec_store_psqt(&psqtEntryTile[k], psqt[k]);
                vec_store_psqt(&psqtAccTile  [k], psqt[k]);
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
    std::memcpy(accumulator.accumulation[Perspective], entry.accumulation,
                Dimensions * sizeof(BiasType));
    std::memcpy(accumulator.psqtAccumulation[Perspective], entry.psqtAccumulation,
                PSQTBuckets * sizeof(PSQTWeightType));
#endif

    for (Color c : {WHITE, BLACK})
        entry.colorBB[c] = pos.pieces(c);

    for (PieceType pt = PAWN; pt <= KING; ++pt)
        entry.typeBB[pt] = pos.pieces(pt);
}

}  // namespace

#undef assume

void AccumulatorState::reset(const DirtyPiece& dp) noexcept {
    dirtyPiece = dp;
    big.computed.fill(false);
    small.computed.fill(false);
}

void AccumulatorCaches::init(const Networks& networks) noexcept {

    big.init(networks.big);
    small.init(networks.small);
}


const AccumulatorState& AccumulatorStack::clatest_state() const noexcept {
    return accStates[index - 1];
}
AccumulatorState& AccumulatorStack::latest_state() noexcept {  //
    return accStates[index - 1];
}

void AccumulatorStack::reset(const Position&    pos,
                             const Networks&    networks,
                             AccumulatorCaches& caches) noexcept {
    index = 1;

    update_accumulator_refresh_cache<WHITE, BigTransformedFeatureDimensions>(
      *networks.big.featureTransformer, pos, accStates[0], caches.big);
    update_accumulator_refresh_cache<BLACK, BigTransformedFeatureDimensions>(
      *networks.big.featureTransformer, pos, accStates[0], caches.big);

    update_accumulator_refresh_cache<WHITE, SmallTransformedFeatureDimensions>(
      *networks.small.featureTransformer, pos, accStates[0], caches.small);
    update_accumulator_refresh_cache<BLACK, SmallTransformedFeatureDimensions>(
      *networks.small.featureTransformer, pos, accStates[0], caches.small);
}

void AccumulatorStack::push(const DirtyPiece& dp) noexcept {
    assert(index < accStates.size() - 1);
    accStates[index++].reset(dp);
}

void AccumulatorStack::pop() noexcept {
    assert(index > 1);
    --index;
}

template<IndexType Dimensions>
void AccumulatorStack::evaluate(const Position&                       pos,
                                const FeatureTransformer<Dimensions>& featureTransformer,
                                Cache<Dimensions>&                    cache) noexcept {

    evaluate_side<WHITE>(pos, featureTransformer, cache);
    evaluate_side<BLACK>(pos, featureTransformer, cache);
}

template<Color Perspective, IndexType Dimensions>
void AccumulatorStack::evaluate_side(const Position&                       pos,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     Cache<Dimensions>&                    cache) noexcept {

    auto lastUsableAcc = find_last_usable_accumulator<Perspective, Dimensions>();

    if ((accStates[lastUsableAcc].template acc<Dimensions>()).computed[Perspective])
        forward_update_incremental<Perspective>(pos, featureTransformer, lastUsableAcc);
    else
    {
        update_accumulator_refresh_cache<Perspective>(featureTransformer, pos, latest_state(),
                                                      cache);
        backward_update_incremental<Perspective>(pos, featureTransformer, lastUsableAcc);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
template<Color Perspective, IndexType Dimensions>
std::size_t AccumulatorStack::find_last_usable_accumulator() const noexcept {

    for (std::size_t idx = index - 1; idx > 0; --idx)
    {
        if ((accStates[idx].template acc<Dimensions>()).computed[Perspective])
            return idx;

        if (FeatureSet::requires_refresh(accStates[idx].dirtyPiece, Perspective))
            return idx;
    }

    return 0;
}

template<Color Perspective, IndexType Dimensions>
void AccumulatorStack::forward_update_incremental(
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  const std::size_t                     begin) noexcept {

    assert(begin < accStates.size());
    assert((accStates[begin].acc<Dimensions>()).computed[Perspective]);

    Square ksq = pos.king_square(Perspective);

    for (std::size_t idx = begin + 1; idx < index; ++idx)
        update_accumulator_incremental<Perspective, true>(featureTransformer, ksq,
                                                          accStates[idx - 1], accStates[idx]);

    assert((clatest_state().acc<Dimensions>()).computed[Perspective]);
}

template<Color Perspective, IndexType Dimensions>
void AccumulatorStack::backward_update_incremental(
  const Position&                       pos,
  const FeatureTransformer<Dimensions>& featureTransformer,
  const std::size_t                     end) noexcept {

    assert(end < accStates.size());
    assert(end < index);
    assert((clatest_state().acc<Dimensions>()).computed[Perspective]);

    Square ksq = pos.king_square(Perspective);

    for (std::size_t idx = index - 2; idx >= end; --idx)
        update_accumulator_incremental<Perspective, false>(featureTransformer, ksq,
                                                           accStates[idx + 1], accStates[idx]);

    assert((accStates[end].acc<Dimensions>()).computed[Perspective]);
}

// Explicit template instantiations
template  //
  void
  AccumulatorStack::evaluate<BigTransformedFeatureDimensions>(
    const Position&                                            pos,
    const FeatureTransformer<BigTransformedFeatureDimensions>& featureTransformer,
    Cache<BigTransformedFeatureDimensions>&                    cache) noexcept;
template  //
  void
  AccumulatorStack::evaluate<SmallTransformedFeatureDimensions>(
    const Position&                                              pos,
    const FeatureTransformer<SmallTransformedFeatureDimensions>& featureTransformer,
    Cache<SmallTransformedFeatureDimensions>&                    cache) noexcept;

}  // namespace DON::NNUE
