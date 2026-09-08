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
#include <cassert>
#include <utility>

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "architecture.h"
#include "feature_transformer.h"
#include "ntypes.h"
#include "simd.h"

namespace DON::NNUE {

namespace {

void update_hybrid(Color                     perspective,
                   const Position&           pos,
                   const FeatureTransformer& featureTransformer,
                   const Accumulator&        source,
                   Accumulator&              target,
                   AccumulatorCache&         accCache) noexcept;

void update_refresh_cache(Color                     perspective,
                          const Position&           pos,
                          const FeatureTransformer& featureTransformer,
                          Accumulator&              target,
                          AccumulatorCache&         accCache) noexcept;

template<bool Forward>
void update_incremental(Color                     perspective,
                        Square                    kingSq,
                        const FeatureTransformer& featureTransformer,
                        const Accumulator&        source,
                        Accumulator&              target) noexcept;

void update_incremental_both(const FeatureTransformer& featureTransformer,
                             Square                    wKingSq,
                             Square                    bKingSq,
                             const Accumulator&        source,
                             Accumulator&              target);

}  // namespace

void AccumulatorStack::reset() noexcept {
    accumulators[0].set({});
    size_ = 1;
}

void AccumulatorStack::push(Dirties&& dirties) noexcept {
    assert(size() < Size);

    accumulators[size_++].set(std::move(dirties));
}

void AccumulatorStack::pop() noexcept {
    assert(size() > 1);

    --size_;
}

void AccumulatorStack::evaluate(const Position&           pos,
                                const FeatureTransformer& featureTransformer,
                                AccumulatorCache&         accCache) noexcept {
    const auto wlastUsableIdx = find_last_usable_index(WHITE);
    const auto blastUsableIdx = find_last_usable_index(BLACK);

    if (accumulators[wlastUsableIdx].computed[WHITE]
        && accumulators[blastUsableIdx].computed[BLACK])
        update_incremental_forward_both(pos, featureTransformer, wlastUsableIdx, blastUsableIdx);
    else
    {
        evaluate(WHITE, pos, featureTransformer, accCache, wlastUsableIdx);
        evaluate(BLACK, pos, featureTransformer, accCache, blastUsableIdx);
    }
}

void AccumulatorStack::evaluate(const Color               perspective,
                                const Position&           pos,
                                const FeatureTransformer& featureTransformer,
                                AccumulatorCache&         accCache,
                                const usize               lastUsableIdx) noexcept {
    constexpr u8 PC_COUNT_HYBRID_MIN = 15;

    if (accumulators[lastUsableIdx].computed[perspective])
        update_incremental_forward(perspective, pos, featureTransformer, lastUsableIdx);
    else
    {
        const auto& dirtyPiece = top().dirties.dirtyPiece;

        if (dirtyPiece.movedPc == make_piece(perspective, KING)
            && accumulators[size() - 2].computed[perspective] && pos.count() >= PC_COUNT_HYBRID_MIN
            && ((u8(dirtyPiece.orgSq) & 4) == (u8(dirtyPiece.dstSq) & 4))
            // excludes castling
            && !is_ok(dirtyPiece.addedSq))
        {
            update_hybrid(perspective, pos, featureTransformer, accumulators[size() - 2], top(),
                          accCache);
            return;
        }

        update_refresh_cache(perspective, pos, featureTransformer, top(), accCache);
        update_incremental_backward(perspective, pos, featureTransformer, lastUsableIdx);
    }
}

// Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
// state just before a change that requires full refresh.
usize AccumulatorStack::find_last_usable_index(const Color perspective) const noexcept {

    for (usize idx = size(); idx-- > 0;)
    {
        if (accumulators[idx].computed[perspective])
            return idx;

        // Threat feature set refreshes require a king move across the center, i.e.,
        // a subset of halfka refreshes
        if (PSQFeatureSet::refresh_required(perspective, accumulators[idx].dirties.dirtyPiece))
            return idx;
    }

    return 0;
}

void AccumulatorStack::update_incremental_forward(const Color               perspective,
                                                  const Position&           pos,
                                                  const FeatureTransformer& featureTransformer,
                                                  const usize               beg) noexcept {
    assert(beg < size() && size() <= Size);
    assert(accumulators[beg].computed[perspective]);

    const Square kingSq = pos.square<KING>(perspective);

    for (usize idx = beg; ++idx < size();)
        update_incremental<true>(perspective, kingSq, featureTransformer, accumulators[idx - 1],
                                 accumulators[idx]);

    assert(top().computed[perspective]);
}

void AccumulatorStack::update_incremental_backward(const Color               perspective,
                                                   const Position&           pos,
                                                   const FeatureTransformer& featureTransformer,
                                                   const usize               end) noexcept {
    assert(end < size() && size() <= Size);
    assert(top().computed[perspective]);

    const Square kingSq = pos.square<KING>(perspective);

    for (usize idx = std::max<usize>(size(), 1) - 1; idx-- > end;)
        update_incremental<false>(perspective, kingSq, featureTransformer, accumulators[idx + 1],
                                  accumulators[idx]);

    assert(accumulators[end].computed[perspective]);
}

void AccumulatorStack::update_incremental_forward_both(const Position&           pos,
                                                       const FeatureTransformer& featureTransformer,
                                                       const usize               wBeg,
                                                       const usize               bBeg) noexcept {
    assert(wBeg < size());
    assert(bBeg < size());
    assert(accumulators[wBeg].computed[WHITE]);
    assert(accumulators[bBeg].computed[BLACK]);

    const Square wKingSq = pos.square<KING>(WHITE);
    const Square bKingSq = pos.square<KING>(BLACK);
    const usize  maxBeg  = std::max(wBeg, bBeg);

    // Catch up the lagging perspective, then traverse the common suffix once.
    for (usize idx = wBeg + 1; idx <= maxBeg; ++idx)
        update_incremental<true>(WHITE, wKingSq, featureTransformer, accumulators[idx - 1],
                                 accumulators[idx]);
    for (usize idx = bBeg + 1; idx <= maxBeg; ++idx)
        update_incremental<true>(BLACK, bKingSq, featureTransformer, accumulators[idx - 1],
                                 accumulators[idx]);

    for (usize idx = maxBeg + 1; idx < size(); ++idx)
        update_incremental_both(featureTransformer, wKingSq, bKingSq, accumulators[idx - 1],
                                accumulators[idx]);

    assert(top().computed[WHITE]);
    assert(top().computed[BLACK]);
}

namespace {

Bitboard changed_bb(const PieceMap& oldPieceMap, const PieceMap& newPieceMap) noexcept {
#if defined(USE_SSE2)
    #if defined(USE_AVX2)
    Bitboard sameBB = 0;

    for (const usize s : {0, 32})
    {
        const __m256i oldV  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&oldPieceMap[s]));
        const __m256i newV  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&newPieceMap[s]));
        const __m256i equal = _mm256_cmpeq_epi8(oldV, newV);
        const u32     mask  = _mm256_movemask_epi8(equal);

        sameBB |= Bitboard{mask} << s;
    }

    return ~sameBB;

    #else
    Bitboard sameBB = 0;

    for (const usize s : {0, 16, 32, 48})
    {
        const __m128i oldV  = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&oldPieceMap[s]));
        const __m128i newV  = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&newPieceMap[s]));
        const __m128i equal = _mm_cmpeq_epi8(oldV, newV);
        const u16     mask  = _mm_movemask_epi8(equal);

        sameBB |= Bitboard{mask} << s;
    }

    return ~sameBB;

    #endif

#elif defined(USE_LSX)
    #if defined(USE_LASX)
    Bitboard changedBB = 0;

    for (const usize s : {0, 32})
    {
        const __m256i oldV     = __lasx_xvld(reinterpret_cast<const void*>(&oldPieceMap[s]), 0);
        const __m256i newV     = __lasx_xvld(reinterpret_cast<const void*>(&newPieceMap[s]), 0);
        const __m256i diff     = __lasx_xvxor_v(oldV, newV);
        const __m256i simdMask = __lasx_xvmsknz_b(diff);
        const u32     loMask   = __lasx_xvpickve2gr_d(simdMask, 0);
        const u32     hiMask   = __lasx_xvpickve2gr_d(simdMask, 2);

        changedBB |= (Bitboard{loMask} | (Bitboard{hiMask} << 16)) << s;
    }

    return changedBB;

    #else
    Bitboard changedBB = 0;

    for (const usize s : {0, 16, 32, 48})
    {
        const __m128i oldV     = __lsx_vld(reinterpret_cast<const void*>(&oldPieceMap[s]), 0);
        const __m128i newV     = __lsx_vld(reinterpret_cast<const void*>(&newPieceMap[s]), 0);
        const __m128i diff     = __lsx_vxor_v(oldV, newV);
        const __m128i simdMask = __lsx_vmsknz_b(diff);
        const u16     mask     = __lsx_vpickve2gr_d(simdMask, 0);

        changedBB |= Bitboard{mask} << s;
    }

    return changedBB;

    #endif

#elif defined(USE_NEON)
    const uint8x16x4_t oldV = vld4q_u8(reinterpret_cast<const u8*>(oldPieceMap.data()));
    const uint8x16x4_t newV = vld4q_u8(reinterpret_cast<const u8*>(newPieceMap.data()));

    const auto equal = [&oldV, &newV](const usize i) noexcept {
        return vceqq_u8(oldV.val[i], newV.val[i]);
    };

    const uint8x16_t equal01 = vsriq_n_u8(equal(1), equal(0), 1);
    const uint8x16_t equal23 = vsriq_n_u8(equal(3), equal(2), 1);
    uint8x16_t       merged  = vsriq_n_u8(equal23, equal01, 2);
    merged                   = vsriq_n_u8(merged, merged, 4);

    const uint8x8_t packed = vshrn_n_u16(vreinterpretq_u16_u8(merged), 4);
    const Bitboard  sameBB = vget_lane_u64(vreinterpret_u64_u8(packed), 0);

    return ~sameBB;

#elif defined(USE_RVV)

    #define RVV_MASK(mx, bx) \
        __riscv_vmv_x_s_u64m1_u64(__riscv_vreinterpret_v_u8m1_u64m1( \
          __riscv_vreinterpret_v_b##bx##_u8m1(__riscv_vmsne_vv_i8m##mx##_b##bx( \
            __riscv_vle8_v_i8m##mx(reinterpret_cast<const i8*>(oldPieceMap.data()), 64), \
            __riscv_vle8_v_i8m##mx(reinterpret_cast<const i8*>(newPieceMap.data()), 64), 64))))

    const usize maxVl = __riscv_vsetvlmax_e8m1();
    if (maxVl >= 64)
        return RVV_MASK(1, 8);
    else if (maxVl == 32)
        return RVV_MASK(2, 4);
    else
        return RVV_MASK(4, 2);

    #undef RVV_MASK

#else
    Bitboard changedBB = 0;

    for (usize s = 0; s < SQUARE_NB; ++s)
        changedBB |= Bitboard{oldPieceMap[s] != newPieceMap[s]} << s;

    return changedBB;

#endif
}

// Updates accumulator for a king move, and also updates the accumulator cache
// for the new king position.
void update_hybrid(const Color               perspective,
                   const Position&           pos,
                   const FeatureTransformer& featureTransformer,
                   const Accumulator&        source,
                   Accumulator&              target,
                   AccumulatorCache&         accCache) noexcept {
    const auto& dirtyPiece = target.dirties.dirtyPiece;

    assert(dirtyPiece.movedPc == make_piece(perspective, KING));
    assert(is_ok(dirtyPiece.dstSq));
    assert((u8(dirtyPiece.orgSq) & 4) == (u8(dirtyPiece.dstSq) & 4));
    assert(source.computed[perspective]);
    assert(!target.computed[perspective]);

    const Square oldKingSq = dirtyPiece.orgSq;
    const Square newKingSq = dirtyPiece.dstSq;
    assert(oldKingSq != newKingSq);

    const auto& curPieceMap = pos.piece_map();
    auto        prePieceMap = curPieceMap;  // copies 64 bytes!

    const Bitboard curPiecesBB = pos.pieces_bb();
    Bitboard       prePiecesBB = curPiecesBB;

    assert(prePieceMap[newKingSq] == dirtyPiece.movedPc);

    if (is_ok(dirtyPiece.removedSq))
    {
        assert(dirtyPiece.removedSq == newKingSq);
        prePieceMap[newKingSq] = dirtyPiece.removedPc;
    }
    else
    {
        prePieceMap[newKingSq] = Piece::NO_PIECE;
        prePiecesBB &= ~square_bb(newKingSq);
    }

    assert(prePieceMap[oldKingSq] == Piece::NO_PIECE);
    prePieceMap[oldKingSq] = make_piece(perspective, KING);
    prePiecesBB |= square_bb(oldKingSq);

    const auto& oldEntry = accCache[oldKingSq][perspective];
    auto&       newEntry = accCache[newKingSq][perspective];

    // "Remove" means we need to remove them from the cache entry,
    // "Add" means add them to the entry to get the accumulator we want
    PSQFeatureSet::IndexVector oldRemove, oldAdd, newRemove, newAdd;

    Bitboard oldChangedBB = changed_bb(oldEntry.pieceMap, prePieceMap);
    Bitboard oldRemovedBB = oldChangedBB & oldEntry.piecesBB;
    Bitboard oldAddedBB   = oldChangedBB & prePiecesBB;

    Bitboard newChangedBB = changed_bb(newEntry.pieceMap, curPieceMap);
    Bitboard newRemovedBB = newChangedBB & newEntry.piecesBB;
    Bitboard newAddedBB   = newChangedBB & curPiecesBB;

    PSQFeatureSet::append_map_changed_indices(perspective, oldKingSq, oldEntry.pieceMap,
                                              prePieceMap, oldRemovedBB, oldAddedBB, oldRemove,
                                              oldAdd);
    PSQFeatureSet::append_map_changed_indices(perspective, newKingSq, newEntry.pieceMap,
                                              curPieceMap, newRemovedBB, newAddedBB, newRemove,
                                              newAdd);

    ThreatFeatureSet::IndexVector thrRemoved, thrAdded;  // also contain pp indices

    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = FeatureTransformer::OutputDimensions;
    ThreatFeatureSet::append_changed_indices(perspective, newKingSq, target.dirties.dirtyThreats,
                                             thrRemoved, thrAdded, pfBase, pfStride);
    PairFeatureSet::append_changed_indices(perspective, newKingSq, target.dirties.dirtyPawnPairs,
                                           thrRemoved, thrAdded, pfBase, pfStride);

    constexpr auto Dimensions = FeatureTransformer::OutputDimensions;

    const auto& sourceAcc = source.accumulation[perspective];
    auto&       targetAcc = target.accumulation[perspective];

    const auto& sourcePsqtAcc = source.psqtAccumulation[perspective];
    auto&       targetPsqtAcc = target.psqtAccumulation[perspective];

#if defined(VECTOR)
    using Tiling [[maybe_unused]] = SIMD::Tiling<Dimensions, Dimensions, PSQT_BUCKETS>;

    SIMD::vec_t      acc[Tiling::RegCount];
    SIMD::psqt_vec_t psqt[Tiling::PSQTRegCount];

    const auto* weights            = &featureTransformer.weights[0];
    const auto* threatAndPpWeights = &featureTransformer.threatAndPpWeights[0];
    // clang-format off
    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        const IndexType tileOff  = j * Tiling::TileHeight;

        const auto* sourceTile   = reinterpret_cast<const SIMD::vec_t*>(&sourceAcc[tileOff]);
        const auto* oldEntryTile = reinterpret_cast<const SIMD::vec_t*>(&oldEntry.accumulation[tileOff]);

        auto* newEntryTile = reinterpret_cast<SIMD::vec_t*>(&newEntry.accumulation[tileOff]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            acc[k] = newEntryTile[k];

        for (IndexType i = 0; i < newRemove.size(); ++i)
        {
            auto* column = reinterpret_cast<const SIMD::vec_t*>(&weights[newRemove[i] * Dimensions + tileOff]);
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);
        }
        for (IndexType i = 0; i < newAdd.size(); ++i)
        {
            auto* column = reinterpret_cast<const SIMD::vec_t*>(&weights[newAdd[i] * Dimensions + tileOff]);
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
        {
            vec_store(&newEntryTile[k], acc[k]);
            // adding the old accumulator adds (most of) the threats and pp weights that needed
            acc[k] = vec_add_16(acc[k], sourceTile[k]);
            // But have added a whole bunch of psq weights for the wrong king bucket which
            // need to remove first remove the cached psq accumulation for the old king position...
            acc[k] = vec_sub_16(acc[k], oldEntryTile[k]);
        }

        // ... then adjust
        for (IndexType i = 0; i < oldRemove.size(); ++i)
        {
            auto* column = reinterpret_cast<const SIMD::vec_t*>(&weights[oldRemove[i] * Dimensions + tileOff]);
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);
        }
        for (IndexType i = 0; i < oldAdd.size(); ++i)
        {
            auto* column = reinterpret_cast<const SIMD::vec_t*>(&weights[oldAdd[i] * Dimensions + tileOff]);
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);
        }

        for (IndexType i = 0; i < thrRemoved.size(); ++i)
        {
            auto* column = reinterpret_cast<const SIMD::vec_i8_t*>(&threatAndPpWeights[thrRemoved[i] * Dimensions + tileOff]);

    #if defined(USE_NEON)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                acc[k + 0] = vsubw_s8(acc[k], vget_low_s8(column[k / 2]));
                acc[k + 1] = vsubw_high_s8(acc[k + 1], column[k / 2]);
            }
    #elif defined(USE_LSX) && !defined(USE_LASX)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);
                acc[k + 0]           = vec_sub_16(acc[k + 0], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1]           = vec_sub_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
    #else
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));
    #endif
        }

        for (IndexType i = 0; i < thrAdded.size(); ++i)
        {
            auto* column = reinterpret_cast<const SIMD::vec_i8_t*>(&threatAndPpWeights[thrAdded[i] * Dimensions + tileOff]);

    #if defined(USE_NEON)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                acc[k + 0] = vaddw_s8(acc[k], vget_low_s8(column[k / 2]));
                acc[k + 1] = vaddw_high_s8(acc[k + 1], column[k / 2]);
            }
    #elif defined(USE_LSX) && !defined(USE_LASX)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);
                acc[k + 0]           = vec_add_16(acc[k + 0], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1]           = vec_add_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
    #else
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
    #endif
        }

        auto* targetTile = reinterpret_cast<SIMD::vec_t*>(&targetAcc[tileOff]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            vec_store(&targetTile[k], acc[k]);
    }

    for (IndexType j = 0; j < PSQT_BUCKETS / Tiling::PSQTTileHeight; ++j)
    {
        const IndexType psqtTileOff  = j * Tiling::PSQTTileHeight;

        const auto* sourcePsqtTile   = reinterpret_cast<const SIMD::psqt_vec_t*>(&sourcePsqtAcc[psqtTileOff]);
        const auto* oldEntryTilePsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&oldEntry.psqtAccumulation[psqtTileOff]);

        auto* newEntryTilePsqt = reinterpret_cast<SIMD::psqt_vec_t*>(&newEntry.psqtAccumulation[psqtTileOff]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            psqt[k] = newEntryTilePsqt[k];

        for (IndexType i = 0; i < newRemove.size(); ++i)
        {
            auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&featureTransformer.psqtWeights[newRemove[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }
        for (IndexType i = 0; i < newAdd.size(); ++i)
        {
            auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&featureTransformer.psqtWeights[newAdd[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
        {
            vec_store_psqt(&newEntryTilePsqt[k], psqt[k]);
            psqt[k] = vec_add_psqt_32(psqt[k], sourcePsqtTile[k]);
            psqt[k] = vec_sub_psqt_32(psqt[k], oldEntryTilePsqt[k]);
        }

        for (IndexType i = 0; i < oldRemove.size(); ++i)
        {
            auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&featureTransformer.psqtWeights[oldRemove[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }
        for (IndexType i = 0; i < oldAdd.size(); ++i)
        {
            auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&featureTransformer.psqtWeights[oldAdd[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType i = 0; i < thrRemoved.size(); ++i)
        {
            auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&featureTransformer.threatAndPpPsqtWeights[thrRemoved[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType i = 0; i < thrAdded.size(); ++i)
        {
            auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&featureTransformer.threatAndPpPsqtWeights[thrAdded[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        auto* targetPsqtTile = reinterpret_cast<SIMD::psqt_vec_t*>(&targetPsqtAcc[psqtTileOff]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            vec_store_psqt(&targetPsqtTile[k], psqt[k]);
    }
    // clang-format on
#else
    for (const auto index : newRemove)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            newEntry.accumulation[j] -= featureTransformer.weights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            newEntry.psqtAccumulation[k] -= featureTransformer.psqtWeights[psqtOffset + k];
    }
    for (const auto index : newAdd)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            newEntry.accumulation[j] += featureTransformer.weights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            newEntry.psqtAccumulation[k] += featureTransformer.psqtWeights[psqtOffset + k];
    }

    targetAcc     = newEntry.accumulation;
    targetPsqtAcc = newEntry.psqtAccumulation;

    for (IndexType j = 0; j < Dimensions; ++j)
    {
        targetAcc[j] += sourceAcc[j];
        targetAcc[j] -= oldEntry.accumulation[j];
    }
    for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
    {
        targetPsqtAcc[k] += sourcePsqtAcc[k];
        targetPsqtAcc[k] -= oldEntry.psqtAccumulation[k];
    }

    for (const auto index : oldRemove)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            targetAcc[j] += featureTransformer.weights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            targetPsqtAcc[k] += featureTransformer.psqtWeights[psqtOffset + k];
    }
    for (const auto index : oldAdd)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            targetAcc[j] -= featureTransformer.weights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            targetPsqtAcc[k] -= featureTransformer.psqtWeights[psqtOffset + k];
    }

    for (const auto index : thrRemoved)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            targetAcc[j] -= featureTransformer.threatAndPpWeights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            targetPsqtAcc[k] -= featureTransformer.threatAndPpPsqtWeights[psqtOffset + k];
    }
    for (const auto index : thrAdded)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            targetAcc[j] += featureTransformer.threatAndPpWeights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            targetPsqtAcc[k] += featureTransformer.threatAndPpPsqtWeights[psqtOffset + k];
    }

#endif

    newEntry.pieceMap = curPieceMap;
    newEntry.piecesBB = curPiecesBB;

    target.computed[perspective] = true;
}

// HalfKA data comes from the Finny table entry, while the threats are built
// from the active threat features
void update_refresh_cache(const Color               perspective,
                          const Position&           pos,
                          const FeatureTransformer& featureTransformer,
                          Accumulator&              target,
                          AccumulatorCache&         accCache) noexcept {
    constexpr auto Dimensions = FeatureTransformer::OutputDimensions;

    const Square kingSq = pos.square<KING>(perspective);

    auto& entry = accCache[kingSq][perspective];

    ThreatFeatureSet::IndexVector active;
    ThreatFeatureSet::append_active_indices(perspective, pos, active);
    PairFeatureSet::append_active_indices(perspective, pos, active);

    const auto& pieceMap = pos.piece_map();
    const auto  piecesBB = pos.pieces_bb();

    const Bitboard changedBB = changed_bb(entry.pieceMap, pieceMap);

    const Bitboard removedBB = changedBB & entry.piecesBB;
    const Bitboard addedBB   = changedBB & piecesBB;

    PSQFeatureSet::IndexVector removed, added;
    PSQFeatureSet::append_map_changed_indices(perspective, kingSq, entry.pieceMap, pieceMap,
                                              removedBB, addedBB, removed, added);

    entry.pieceMap = pieceMap;
    entry.piecesBB = piecesBB;

    target.computed[perspective] = true;

#if defined(VECTOR)
    using Tiling [[maybe_unused]] = SIMD::Tiling<Dimensions, Dimensions, PSQT_BUCKETS>;

    SIMD::vec_t      acc[Tiling::RegCount];
    SIMD::psqt_vec_t psqt[Tiling::PSQTRegCount];

    const auto* weights            = featureTransformer.weights.data();
    const auto* threatAndPpWeights = featureTransformer.threatAndPpWeights.data();
    // clang-format off
    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        const IndexType tileOff = j * Tiling::TileHeight;

        auto* entryTile = reinterpret_cast<SIMD::vec_t*>(&entry.accumulation[tileOff]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            acc[k] = entryTile[k];

        for (IndexType i = 0; i < removed.size(); ++i)
        {
            const auto* column = reinterpret_cast<const SIMD::vec_t*>(&weights[removed[i] * Dimensions + tileOff]);
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_sub_16(acc[k], column[k]);
        }
        for (IndexType i = 0; i < added.size(); ++i)
        {
            const auto* column = reinterpret_cast<const SIMD::vec_t*>(&weights[added[i] * Dimensions + tileOff]);
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], column[k]);
        }

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            vec_store(&entryTile[k], acc[k]);

        for (IndexType i = 0; i < active.size(); ++i)
        {
            const auto* column = reinterpret_cast<const SIMD::vec_i8_t*>(&threatAndPpWeights[active[i] * Dimensions + tileOff]);

    #if defined(USE_NEON)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                acc[k + 0] = vaddw_s8(     acc[k + 0], vget_low_s8(column[k / 2]));
                acc[k + 1] = vaddw_high_s8(acc[k + 1],             column[k / 2]);
            }
    #elif defined(USE_LSX) && !defined(USE_LASX)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);
                acc[k + 0]           = vec_add_16(acc[k + 0], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1]           = vec_add_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
    #else
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
    #endif
        }

        auto* accTile = reinterpret_cast<SIMD::vec_t*>(&target.accumulation[perspective][tileOff]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            vec_store(&accTile[k], acc[k]);
    }

    const auto* psqtWeights       = featureTransformer.psqtWeights.data();
    const auto* threatAndPpPsqtWeights = featureTransformer.threatAndPpPsqtWeights.data();

    for (IndexType j = 0; j < PSQT_BUCKETS / Tiling::PSQTTileHeight; ++j)
    {
        const IndexType psqtTileOff = j * Tiling::PSQTTileHeight;

        auto* entryPsqtTile = reinterpret_cast<SIMD::psqt_vec_t*>(&entry.psqtAccumulation[psqtTileOff]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            psqt[k] = entryPsqtTile[k];

        for (IndexType i = 0; i < removed.size(); ++i)
        {
            const auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&psqtWeights[removed[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }
        for (IndexType i = 0; i < added.size(); ++i)
        {
            const auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&psqtWeights[added[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            vec_store_psqt(&entryPsqtTile[k], psqt[k]);

        for (IndexType i = 0; i < active.size(); ++i)
        {
            const auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&threatAndPpPsqtWeights[active[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        auto* accPsqtTile = reinterpret_cast<SIMD::psqt_vec_t*>(&target.psqtAccumulation[perspective][psqtTileOff]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            vec_store_psqt(&accPsqtTile[k], psqt[k]);
    }
    // clang-format on
#elif defined(USE_RVV)
    const auto* weights                = &featureTransformer.weights[0];
    const auto* threatAndPpWeights     = &featureTransformer.threatAndPpWeights[0];
    const auto* psqtWeights            = &featureTransformer.psqtWeights[0];
    const auto* threatAndPpPsqtWeights = &featureTransformer.threatAndPpPsqtWeights[0];
    // clang-format off
    for (usize tileOff = 0; tileOff < Dimensions;)
    {
        const usize vl = __riscv_vsetvl_e16m8(Dimensions - tileOff);

        vint16m8_t accum = __riscv_vle16_v_i16m8(&entry.accumulation[tileOff], vl);
        for (const auto i : removed)
            accum = __riscv_vsub_vv_i16m8(
              accum, __riscv_vle16_v_i16m8(&weights[i * Dimensions + tileOff], vl), vl);
        for (const auto i : added)
            accum = __riscv_vadd_vv_i16m8(
              accum, __riscv_vle16_v_i16m8(&weights[i * Dimensions + tileOff], vl), vl);

        __riscv_vse16_v_i16m8(&entry.accumulation[tileOff], accum, vl);

        for (const auto i : active)
            accum = __riscv_vwadd_wv_i16m8(
              accum, __riscv_vle8_v_i8m4(&threatAndPpWeights[i * Dimensions + tileOff], vl), vl);

        __riscv_vse16_v_i16m8(&target.accumulation[perspective][tileOff], accum, vl);

        tileOff += vl;
    }

    for (usize tileOff = 0; tileOff < PSQT_BUCKETS;)
    {
        const usize vl = __riscv_vsetvl_e32m1(PSQT_BUCKETS - tileOff);

        vint32m1_t accum = __riscv_vle32_v_i32m1(&entry.psqtAccumulation[tileOff], vl);
        for (const auto i : removed)
            accum = __riscv_vsub_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&psqtWeights[i * PSQT_BUCKETS + tileOff], vl), vl);
        for (const auto i : added)
            accum = __riscv_vadd_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&psqtWeights[i * PSQT_BUCKETS + tileOff], vl), vl);

        __riscv_vse32_v_i32m1(&entry.psqtAccumulation[tileOff], accum, vl);

        for (const auto i : active)
            accum = __riscv_vadd_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&threatAndPpPsqtWeights[i * PSQT_BUCKETS + tileOff], vl), vl);

        __riscv_vse32_v_i32m1(&target.psqtAccumulation[perspective][tileOff], accum, vl);

        tileOff += vl;
    }
    // clang-format on
#else

    for (const auto index : removed)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            entry.accumulation[j] -= featureTransformer.weights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            entry.psqtAccumulation[k] -= featureTransformer.psqtWeights[psqtOffset + k];
    }
    for (const auto index : added)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            entry.accumulation[j] += featureTransformer.weights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            entry.psqtAccumulation[k] += featureTransformer.psqtWeights[psqtOffset + k];
    }

    // The accumulator of the refresh entry has been updated.
    // Now copy its content to the actual accumulator were refreshing.
    target.accumulation[perspective]     = entry.accumulation;
    target.psqtAccumulation[perspective] = entry.psqtAccumulation;

    for (const auto index : active)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            target.accumulation[perspective][j] +=
              featureTransformer.threatAndPpWeights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            target.psqtAccumulation[perspective][k] +=
              featureTransformer.threatAndPpPsqtWeights[psqtOffset + k];
    }

#endif
}

void apply_combined(Color                                perspective,
                    const FeatureTransformer&            featureTransformer,
                    const Accumulator&                   source,
                    Accumulator&                         target,
                    const PSQFeatureSet::IndexVector&    psqAdded,
                    const PSQFeatureSet::IndexVector&    psqRemoved,
                    const ThreatFeatureSet::IndexVector& thrAdded,
                    const ThreatFeatureSet::IndexVector& thrRemoved) noexcept {
    constexpr auto Dimensions = FeatureTransformer::OutputDimensions;

    const auto& sourceAcc = source.accumulation[perspective];
    auto&       targetAcc = target.accumulation[perspective];

    const auto& sourcePsqtAcc = source.psqtAccumulation[perspective];
    auto&       targetPsqtAcc = target.psqtAccumulation[perspective];

#if defined(VECTOR)
    using Tiling [[maybe_unused]] = SIMD::Tiling<Dimensions, Dimensions, PSQT_BUCKETS>;

    SIMD::vec_t      acc[Tiling::RegCount];
    SIMD::psqt_vec_t psqt[Tiling::PSQTRegCount];

    const auto* weights            = featureTransformer.weights.data();
    const auto* threatAndPpWeights = featureTransformer.threatAndPpWeights.data();
    // clang-format off
    for (IndexType j = 0; j < Dimensions / Tiling::TileHeight; ++j)
    {
        const IndexType tileOff = j * Tiling::TileHeight;

        const auto* sourceTile = reinterpret_cast<const SIMD::vec_t*>(&sourceAcc[tileOff]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            acc[k] = sourceTile[k];

        for (IndexType i = 0; i < psqRemoved.size(); ++i)
        {
            const auto* row = reinterpret_cast<const SIMD::vec_t*>(&weights[psqRemoved[i] * Dimensions + tileOff]);
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_sub_16(acc[k], row[k]);
        }

        for (IndexType i = 0; i < psqAdded.size(); ++i)
        {
            const auto* row = reinterpret_cast<const SIMD::vec_t*>(&weights[psqAdded[i] * Dimensions + tileOff]);
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], row[k]);
        }

        for (IndexType i = 0; i < thrRemoved.size(); ++i)
        {
            const auto* column = reinterpret_cast<const SIMD::vec_i8_t*>(&threatAndPpWeights[thrRemoved[i] * Dimensions + tileOff]);

    #if defined(USE_NEON)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                acc[k + 0] = vsubw_s8(     acc[k + 0], vget_low_s8(column[k / 2]));
                acc[k + 1] = vsubw_high_s8(acc[k + 1],             column[k / 2]);
            }
    #elif defined(USE_LSX) && !defined(USE_LASX)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);
                acc[k + 0]           = vec_sub_16(acc[k + 0], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1]           = vec_sub_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
    #else
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));
    #endif
        }

        for (IndexType i = 0; i < thrAdded.size(); ++i)
        {
            const auto* column = reinterpret_cast<const SIMD::vec_i8_t*>(&threatAndPpWeights[thrAdded[i] * Dimensions + tileOff]);

    #if defined(USE_NEON)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                acc[k + 0] = vaddw_s8     (acc[k + 0], vget_low_s8(column[k / 2]));
                acc[k + 1] = vaddw_high_s8(acc[k + 1],             column[k / 2]);
            }
    #elif defined(USE_LSX) && !defined(USE_LASX)
            for (IndexType k = 0; k + 1 < Tiling::RegCount; k += 2)
            {
                const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);
                acc[k + 0]           = vec_add_16(acc[k + 0], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1]           = vec_add_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
    #else
            for (IndexType k = 0; k < Tiling::RegCount; ++k)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
    #endif
        }

        auto* targetTile = reinterpret_cast<SIMD::vec_t*>(&targetAcc[tileOff]);

        for (IndexType k = 0; k < Tiling::RegCount; ++k)
            vec_store(&targetTile[k], acc[k]);
    }

    const auto* psqtWeights            = featureTransformer.psqtWeights.data();
    const auto* threatAndPpPsqtWeights = featureTransformer.threatAndPpPsqtWeights.data();

    for (IndexType j = 0; j < PSQT_BUCKETS / Tiling::PSQTTileHeight; ++j)
    {
        const IndexType psqtTileOff = j * Tiling::PSQTTileHeight;

        const auto* sourcePsqtTile = reinterpret_cast<const SIMD::psqt_vec_t*>(&sourcePsqtAcc[psqtTileOff]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            psqt[k] = sourcePsqtTile[k];

        for (IndexType i = 0; i < psqRemoved.size(); ++i)
        {
            const auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&psqtWeights[psqRemoved[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType i = 0; i < psqAdded.size(); ++i)
        {
            const auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&psqtWeights[psqAdded[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType i = 0; i < thrRemoved.size(); ++i)
        {
            const auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&threatAndPpPsqtWeights[thrRemoved[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_sub_psqt_32(psqt[k], columnPsqt[k]);
        }

        for (IndexType i = 0; i < thrAdded.size(); ++i)
        {
            const auto* columnPsqt = reinterpret_cast<const SIMD::psqt_vec_t*>(&threatAndPpPsqtWeights[thrAdded[i] * PSQT_BUCKETS + psqtTileOff]);
            for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
                psqt[k] = vec_add_psqt_32(psqt[k], columnPsqt[k]);
        }

        auto* targetPsqtTile = reinterpret_cast<SIMD::psqt_vec_t*>(&targetPsqtAcc[psqtTileOff]);

        for (IndexType k = 0; k < Tiling::PSQTRegCount; ++k)
            vec_store_psqt(&targetPsqtTile[k], psqt[k]);
    }
    // clang-format on
#elif defined(USE_RVV)
    const auto* psqWeights             = &featureTransformer.weights[0];
    const auto* threatAndPpWeights     = &featureTransformer.threatAndPpWeights[0];
    const auto* psqtWeights            = &featureTransformer.psqtWeights[0];
    const auto* threatAndPpPsqtWeights = &featureTransformer.threatAndPpPsqtWeights[0];
    // clang-format off
    for (usize tileOff = 0; tileOff < Dimensions;)
    {
        const usize vl = __riscv_vsetvl_e16m8(Dimensions - tileOff);

        vint16m8_t accum = __riscv_vle16_v_i16m8(&sourceAcc[tileOff], vl);
        for (const auto i : psqRemoved)
            accum = __riscv_vsub_vv_i16m8(
              accum, __riscv_vle16_v_i16m8(&psqWeights[i * Dimensions + tileOff], vl), vl);
        for (const auto i : psqAdded)
            accum = __riscv_vadd_vv_i16m8(
              accum, __riscv_vle16_v_i16m8(&psqWeights[i * Dimensions + tileOff], vl), vl);
        for (const auto i : thrRemoved)
            accum = __riscv_vwsub_wv_i16m8(
              accum, __riscv_vle8_v_i8m4(&threatAndPpWeights[i * Dimensions + tileOff], vl), vl);
        for (const auto i : thrAdded)
            accum = __riscv_vwadd_wv_i16m8(
              accum, __riscv_vle8_v_i8m4(&threatAndPpWeights[i * Dimensions + tileOff], vl), vl);

        __riscv_vse16_v_i16m8(&targetAcc[tileOff], accum, vl);
        tileOff += vl;
    }

    for (usize tileOff = 0; tileOff < PSQT_BUCKETS;)
    {
        const usize vl = __riscv_vsetvl_e32m1(PSQT_BUCKETS - tileOff);

        vint32m1_t accum = __riscv_vle32_v_i32m1(&sourcePsqtAcc[tileOff], vl);
        for (const auto i : psqRemoved)
            accum = __riscv_vsub_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&psqtWeights[i * PSQT_BUCKETS + tileOff], vl), vl);
        for (const auto i : psqAdded)
            accum = __riscv_vadd_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&psqtWeights[i * PSQT_BUCKETS + tileOff], vl), vl);
        for (const auto i : thrRemoved)
            accum = __riscv_vsub_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&threatAndPpPsqtWeights[i * PSQT_BUCKETS + tileOff], vl), vl);
        for (const auto i : thrAdded)
            accum = __riscv_vadd_vv_i32m1(
              accum, __riscv_vle32_v_i32m1(&threatAndPpPsqtWeights[i * PSQT_BUCKETS + tileOff], vl), vl);

        __riscv_vse32_v_i32m1(&targetPsqtAcc[tileOff], accum, vl);
        tileOff += vl;
    }
    // clang-format on
#else

    targetAcc     = sourceAcc;
    targetPsqtAcc = sourcePsqtAcc;

    for (const auto index : psqRemoved)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            targetAcc[j] -= featureTransformer.weights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            targetPsqtAcc[k] -= featureTransformer.psqtWeights[psqtOffset + k];
    }

    for (const auto index : psqAdded)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            targetAcc[j] += featureTransformer.weights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            targetPsqtAcc[k] += featureTransformer.psqtWeights[psqtOffset + k];
    }

    for (const auto index : thrRemoved)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            targetAcc[j] -= featureTransformer.threatAndPpWeights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            targetPsqtAcc[k] -= featureTransformer.threatAndPpPsqtWeights[psqtOffset + k];
    }

    for (const auto index : thrAdded)
    {
        const auto accOffset = index * Dimensions;
        for (IndexType j = 0; j < Dimensions; ++j)
            targetAcc[j] += featureTransformer.threatAndPpWeights[accOffset + j];
        const auto psqtOffset = index * PSQT_BUCKETS;
        for (IndexType k = 0; k < PSQT_BUCKETS; ++k)
            targetPsqtAcc[k] += featureTransformer.threatAndPpPsqtWeights[psqtOffset + k];
    }

#endif
}

template<bool Forward>
void update_incremental(const Color               perspective,
                        const Square              kingSq,
                        const FeatureTransformer& featureTransformer,
                        const Accumulator&        source,
                        Accumulator&              target) noexcept {

    assert(source.computed[perspective]);
    assert(!target.computed[perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    PSQFeatureSet::IndexVector    psqRemoved, psqAdded;
    ThreatFeatureSet::IndexVector thrRemoved, thrAdded;

    const auto& dirties = Forward ? target.dirties : source.dirties;

    const auto& dP   = dirties.dirtyPiece;
    const auto& dTs  = dirties.dirtyThreats;
    const auto& dPps = dirties.dirtyPawnPairs;

    // Used solely for prefetching
    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = FeatureTransformer::OutputDimensions;

    ThreatFeatureSet::append_changed_indices(perspective, kingSq, dTs,
                                             Forward ? thrRemoved : thrAdded,
                                             Forward ? thrAdded : thrRemoved, pfBase, pfStride);
    PairFeatureSet::append_changed_indices(perspective, kingSq, dPps,
                                           Forward ? thrRemoved : thrAdded,
                                           Forward ? thrAdded : thrRemoved, pfBase, pfStride);
    PSQFeatureSet::append_changed_indices(perspective, kingSq, dP,  //
                                          Forward ? psqRemoved : psqAdded,
                                          Forward ? psqAdded : psqRemoved);

    apply_combined(perspective, featureTransformer, source, target, psqAdded, psqRemoved, thrAdded,
                   thrRemoved);

    target.computed[perspective] = true;
}

void update_incremental_both(const FeatureTransformer& featureTransformer,
                             Square                    wKingSq,
                             Square                    bKingSq,
                             const Accumulator&        source,
                             Accumulator&              target) {
    assert(source.computed[WHITE]);
    assert(source.computed[BLACK]);
    assert(!target.computed[WHITE]);
    assert(!target.computed[BLACK]);

    PSQFeatureSet::IndexVector    psqRemoved[COLOR_NB], psqAdded[COLOR_NB];
    ThreatFeatureSet::IndexVector thrRemoved[COLOR_NB], thrAdded[COLOR_NB];

    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = FeatureTransformer::OutputDimensions;

    ThreatFeatureSet::append_changed_indices_both(
      wKingSq, bKingSq, target.dirties.dirtyThreats, thrRemoved[WHITE], thrAdded[WHITE],
      thrRemoved[BLACK], thrAdded[BLACK], pfBase, pfStride);
    PairFeatureSet::append_changed_indices_both(
      wKingSq, bKingSq, target.dirties.dirtyPawnPairs, thrRemoved[WHITE], thrAdded[WHITE],
      thrRemoved[BLACK], thrAdded[BLACK], pfBase, pfStride);

    PSQFeatureSet::append_changed_indices(WHITE, wKingSq, target.dirties.dirtyPiece,
                                          psqRemoved[WHITE], psqAdded[WHITE]);
    PSQFeatureSet::append_changed_indices(BLACK, bKingSq, target.dirties.dirtyPiece,
                                          psqRemoved[BLACK], psqAdded[BLACK]);

    apply_combined(WHITE, featureTransformer, source, target, psqAdded[WHITE], psqRemoved[WHITE],
                   thrAdded[WHITE], thrRemoved[WHITE]);
    apply_combined(BLACK, featureTransformer, source, target, psqAdded[BLACK], psqRemoved[BLACK],
                   thrAdded[BLACK], thrRemoved[BLACK]);

    target.computed[WHITE] = true;
    target.computed[BLACK] = true;
}

}  // namespace

}  // namespace DON::NNUE
