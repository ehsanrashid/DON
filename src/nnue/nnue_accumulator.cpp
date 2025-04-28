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
#include <type_traits>

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

template<typename VectorWrapper,
         IndexType Width,
         UpdateOperation... ops,
         typename ElementType,
         typename... Ts,
         std::enable_if_t<is_all_same_v<ElementType, Ts...>, bool> = true>
void fused_row_reduce(const ElementType* in, ElementType* out, const Ts* const... rows) {
    constexpr IndexType Size = Width * sizeof(ElementType) / sizeof(typename VectorWrapper::type);

    auto* vecIn  = reinterpret_cast<const typename VectorWrapper::type*>(in);
    auto* vecOut = reinterpret_cast<typename VectorWrapper::type*>(out);

    for (IndexType i = 0; i < Size; ++i)
        vecOut[i] = fused<VectorWrapper, ops...>(
          vecIn[i], reinterpret_cast<const typename VectorWrapper::type*>(rows)[i]...);
}

template<Color Perspective, IndexType Dimensions>
struct AccumulatorUpdateContext {


    AccumulatorUpdateContext(const FeatureTransformer<Dimensions>& ft,
                             const AccumulatorState&               fSt,
                             AccumulatorState&                     tSt) noexcept :
        featureTransformer{ft},
        fState{fSt},
        tState{tSt} {}

    template<UpdateOperation... ops,
             typename... Ts,
             std::enable_if_t<is_all_same_v<IndexType, Ts...>, bool> = true>
    void apply(const Ts... indices) noexcept {
        auto to_weight_vector = [&](const IndexType index) {
            return &featureTransformer.weights[index * Dimensions];
        };

        auto to_psqt_weight_vector = [&](const IndexType index) {
            return &featureTransformer.psqtWeights[index * PSQTBuckets];
        };

        fused_row_reduce<Vec16Wrapper, Dimensions, ops...>(
          (fState.acc<Dimensions>()).accumulation[Perspective],
          (tState.acc<Dimensions>()).accumulation[Perspective], to_weight_vector(indices)...);

        fused_row_reduce<Vec32Wrapper, PSQTBuckets, ops...>(
          (fState.acc<Dimensions>()).psqtAccumulation[Perspective],
          (tState.acc<Dimensions>()).psqtAccumulation[Perspective],
          to_psqt_weight_vector(indices)...);
    }

    const FeatureTransformer<Dimensions>& featureTransformer;
    const AccumulatorState&               fState;
    AccumulatorState&                     tState;
};

template<Color Perspective, IndexType Dimensions>
auto make_accumulator_update_context(const FeatureTransformer<Dimensions>& featureTransformer,
                                     const AccumulatorState&               fState,
                                     AccumulatorState&                     tState) noexcept {
    return AccumulatorUpdateContext<Perspective, Dimensions>{featureTransformer, fState, tState};
}

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

    assert(added.size() == 1 || added.size() == 2);
    assert(removed.size() == 1 || removed.size() == 2);
    assert((Forward && added.size() <= removed.size())
           || (!Forward && removed.size() <= added.size()));

    // Workaround compiler warning for uninitialized variables,
    // replicated on profile builds on windows with gcc 14.2.0.
    assume(added.size() == 1 || added.size() == 2);
    assume(removed.size() == 1 || removed.size() == 2);

    auto updateContext =
      make_accumulator_update_context<Perspective>(featureTransformer, computedState, targetState);

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

    // clang-format off
    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        auto* accTile   = reinterpret_cast<vec_t*>(&accumulator.accumulation[Perspective][j * Tiling::TileHeight]);
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
        auto* psqtAccTile   = reinterpret_cast<psqt_vec_t*>(&accumulator.psqtAccumulation[Perspective][j * Tiling::PSQTTileHeight]);
        auto* psqtEntryTile = reinterpret_cast<psqt_vec_t*>(&entry.psqtAccumulation[j * Tiling::PSQTTileHeight]);

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
            vec_store_psqt(&psqtAccTile  [k], psqt[k]);
            vec_store_psqt(&psqtEntryTile[k], psqt[k]);
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
    return accStates[size - 1];
}
AccumulatorState& AccumulatorStack::latest_state() noexcept { return accStates[size - 1]; }

void AccumulatorStack::reset() noexcept {
    accStates[0].reset({});
    size = 1;
}

void AccumulatorStack::push(const DirtyPiece& dp) noexcept {
    assert(size < accStates.size() - 1);
    accStates[size].reset(dp);
    ++size;
}

void AccumulatorStack::pop() noexcept {
    assert(size > 1);
    --size;
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

    for (std::size_t idx = size - 1; idx > 0; --idx)
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

    for (std::size_t idx = begin + 1; idx < size; ++idx)
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
    assert(end < size);
    assert((clatest_state().acc<Dimensions>()).computed[Perspective]);

    Square ksq = pos.king_square(Perspective);

    for (std::size_t idx = size - 2; idx >= end; --idx)
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
