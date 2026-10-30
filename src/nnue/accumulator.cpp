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

#include "../nnue/accumulator.h"

#include <algorithm>
#include <cassert>
#include <cstddef>  // for offsetof()
#include <cstring>  // for memset()

#include "../bitboard.h"
#include "../position.h"
#include "../types.h"
#include "../nnue/feature_transformer.h"
#include "../nnue/simd.h"
#include "../nnue/types.h"
#include "../nnue/features/full_threats.h"
#include "../nnue/features/k32h3.h"
#include "../nnue/features/pp_3wide.h"
#include "../utility/attributes.h"
#include "../utility/types.h"

namespace DON::NNUE {

void AccumulatorCache::Entry::init(const Array<Bias, L1>& biases) noexcept {
    // Initialize accumulation with given biases
    accumulation          = biases;
    constexpr auto offset = offsetof(Entry, pieceMap);
    static_assert(offset <= sizeof(Entry), "offset exceeds object size");
    std::memset(reinterpret_cast<u8*>(this) + offset, 0, sizeof(*this) - offset);
}

void AccumulatorStack::reset() noexcept {
    size_ = 0;
    push_and_clear();
}

Dirties& AccumulatorStack::push(const Position& pos) noexcept {
    assert(size() < Size);

    push_and_clear();
    top().beforeBlock = Features::K32H3::block_state(pos);
    return top().dirties;
}

void AccumulatorStack::pop() noexcept {
    assert(size() > 1);

    --size_;
}

usize AccumulatorStack::size() const noexcept { return size_; }

Accumulator& AccumulatorStack::top() noexcept { return accumulators[size() - 1]; }

const Accumulator& AccumulatorStack::top() const noexcept { return accumulators[size() - 1]; }

void AccumulatorStack::push_and_clear() noexcept { accumulators[size_++].clear(); }

namespace {

void update_hybrid(Color                     perspective,
                   const Position&           pos,
                   const FeatureTransformer& featureTransformer,
                   const Accumulator&        srcAcc,
                   Accumulator&              dstAcc,
                   AccumulatorCache&         accCache) noexcept;

void update_refresh_cache(Color                     perspective,
                          const Position&           pos,
                          const FeatureTransformer& featureTransformer,
                          Accumulator&              dstAcc,
                          AccumulatorCache&         accCache) noexcept;

template<bool Forward>
void update_incremental(Color                     perspective,
                        Square                    kingSq,
                        const u8                  psqBlock,
                        const FeatureTransformer& featureTransformer,
                        const Accumulator&        srcAcc,
                        Accumulator&              dstAcc) noexcept;

void update_incremental_both(const FeatureTransformer& featureTransformer,
                             Square                    wKingSq,
                             Square                    bKingSq,
                             u8                        whiteBlock,
                             u8                        blackBlock,
                             const Accumulator&        srcAcc,
                             Accumulator&              dstAcc) noexcept;

}  // namespace

void AccumulatorStack::evaluate(const Position&           pos,
                                const FeatureTransformer& featureTransformer,
                                AccumulatorCache&         accCache) noexcept {
    const auto wlastUsableIdx = find_last_usable_index(WHITE, pos);
    const auto blastUsableIdx = find_last_usable_index(BLACK, pos);

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
    constexpr u8 HybridMinCount = 15;

    if (accumulators[lastUsableIdx].computed[perspective])
        update_incremental_forward(perspective, pos, featureTransformer, lastUsableIdx);
    else
    {
        if (size() >= 2 && accumulators[size() - 2].computed[perspective]
            && pos.count() >= HybridMinCount)
        {
            const auto& dP          = top().dirties.dirtyPiece;
            const auto& beforeBlock = top().beforeBlock;
            const auto  afterBlock  = Features::K32H3::block_state(pos);

            if (dP.movedPc == make_piece(perspective, KING)
                && ((u8(dP.orgSq) & u8{4}) == (u8(dP.dstSq) & u8{4}))
                && Features::K32H3::block(beforeBlock, perspective)
                     == Features::K32H3::block(afterBlock, perspective)
                && !is_ok(dP.addedSq))  // excludes castling
            {
                update_hybrid(perspective, pos, featureTransformer, accumulators[size() - 2], top(),
                              accCache);
                return;
            }
        }

        update_refresh_cache(perspective, pos, featureTransformer, top(), accCache);
        update_incremental_backward(perspective, pos, featureTransformer, lastUsableIdx);
    }
}

usize AccumulatorStack::find_last_usable_index(const Color     perspective,
                                               const Position& pos) const noexcept {
    // Backtrack psq block changes
    auto afterBlock = Features::K32H3::block_state(pos);

    for (usize idx = size(); idx-- > 0;)
    {
        if (accumulators[idx].computed[perspective])
            return idx;

        const auto& dirtyPiece  = accumulators[idx].dirties.dirtyPiece;
        const auto& beforeBlock = accumulators[idx].beforeBlock;

        // Threat feature set refreshes require a king move across the center, i.e.,
        // a subset of halfka refreshes
        if (Features::K32H3::refresh_required(perspective, dirtyPiece, beforeBlock, afterBlock))
            return idx;

        afterBlock = beforeBlock;
    }

    return 0;
}

void AccumulatorStack::update_incremental_forward(const Color               perspective,
                                                  const Position&           pos,
                                                  const FeatureTransformer& featureTransformer,
                                                  const usize               beg) noexcept {
    assert(beg < size() && size() <= Size);
    assert(accumulators[beg].computed[perspective]);

    const Square kingSq   = pos.square(perspective, KING);
    const u8     psqBlock = Features::K32H3::block(pos, perspective);

    for (usize idx = beg; ++idx < size();)
        update_incremental<true>(perspective, kingSq, psqBlock, featureTransformer,
                                 accumulators[idx - 1], accumulators[idx]);

    assert(top().computed[perspective]);
}

void AccumulatorStack::update_incremental_backward(const Color               perspective,
                                                   const Position&           pos,
                                                   const FeatureTransformer& featureTransformer,
                                                   const usize               end) noexcept {
    assert(end < size() && size() <= Size);
    assert(top().computed[perspective]);

    const Square kingSq   = pos.square(perspective, KING);
    const u8     psqBlock = Features::K32H3::block(pos, perspective);

    for (usize idx = std::max(size(), usize{1}) - 1; idx-- > end;)
        update_incremental<false>(perspective, kingSq, psqBlock, featureTransformer,
                                  accumulators[idx + 1], accumulators[idx]);

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

    const Square wKingSq    = pos.square(WHITE, KING);
    const Square bKingSq    = pos.square(BLACK, KING);
    const u8     whiteBlock = Features::K32H3::block(pos, WHITE);
    const u8     blackBlock = Features::K32H3::block(pos, BLACK);

    const usize maxBeg = std::max(wBeg, bBeg);

    // Catch up the lagging perspective, then traverse the common suffix once.
    for (usize idx = wBeg + 1; idx <= maxBeg; ++idx)
        update_incremental<true>(WHITE, wKingSq, whiteBlock, featureTransformer,
                                 accumulators[idx - 1], accumulators[idx]);
    for (usize idx = bBeg + 1; idx <= maxBeg; ++idx)
        update_incremental<true>(BLACK, bKingSq, blackBlock, featureTransformer,
                                 accumulators[idx - 1], accumulators[idx]);

    for (usize idx = maxBeg + 1; idx < size(); ++idx)
        update_incremental_both(featureTransformer, wKingSq, bKingSq, whiteBlock, blackBlock,
                                accumulators[idx - 1], accumulators[idx]);

    assert(top().computed[WHITE]);
    assert(top().computed[BLACK]);
}

namespace {

constexpr usize Dimensions = FeatureTransformer::OutputDimensions;

namespace {
#if defined(VECTOR)
using Tiling = SIMD::Tiling<Dimensions, Dimensions>;
#else
namespace Tiling = SIMD::Tiling;
#endif

enum class Op : u8 {
    Add,
    Sub
};

#if defined(USE_RVV)
using Tile = vint16m8_t;

ALWAYS_INLINE void load_tile(const i16* const src, const Index j, Tile& tile) noexcept {
    usize vl = __riscv_vsetvl_e16m8(Dimensions - j);
    tile     = __riscv_vle16_v_i16m8(src + j, vl);
}

ALWAYS_INLINE void store_tile(i16* const dst, const Index j, const Tile& tile) noexcept {
    usize vl = __riscv_vsetvl_e16m8(Dimensions - j);
    __riscv_vse16_v_i16m8(dst + j, tile, vl);
}

ALWAYS_INLINE void increment_index(Index& j) noexcept { j += __riscv_vsetvl_e16m8(Dimensions - j); }

template<Op op>
ALWAYS_INLINE void apply(const i16* const src, const Index j, Tile& tile) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    usize      vl     = __riscv_vsetvl_e16m8(Dimensions - j);
    vint16m8_t srcVec = __riscv_vle16_v_i16m8(src, vl);
    if constexpr (op == Op::Add)
        tile = __riscv_vadd_vv_i16m8(tile, srcVec, vl);
    else
        tile = __riscv_vsub_vv_i16m8(tile, srcVec, vl);
}

template<Op op>
ALWAYS_INLINE void apply_single(const i8* data, const Index j, Tile& tile) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    usize     vl        = __riscv_vsetvl_e16m8(Dimensions - j);
    vint8m4_t weightVec = __riscv_vle8_v_i8m4(data, vl);
    if constexpr (op == Op::Add)
        tile = __riscv_vwadd_wv_i16m8(tile, weightVec, vl);
    else
        tile = __riscv_vwsub_wv_i16m8(tile, weightVec, vl);
}

#else

struct Tile final {
   public:
    auto&       operator[](const Index i) noexcept { return inner[i]; }
    const auto& operator[](const Index i) const noexcept { return inner[i]; }

   private:
    SIMD::vec_t inner[Tiling::RegCount];
};

ALWAYS_INLINE void load_tile(const i16* const src, const Index j, Tile& tile) noexcept {
    auto* column = reinterpret_cast<const SIMD::vec_t*>(&src[j]);
    for (Index k = 0; k < Tiling::RegCount; ++k)
        tile[k] = column[k];
}

ALWAYS_INLINE void store_tile(i16* const dst, const Index j, const Tile& tile) noexcept {
    auto* column = reinterpret_cast<SIMD::vec_t*>(&dst[j]);
    for (Index k = 0; k < Tiling::RegCount; ++k)
        column[k] = tile[k];
}

ALWAYS_INLINE void increment_index(Index& j) noexcept { j += Tiling::TileHeight; }

template<Op op>
ALWAYS_INLINE void
apply(const i16* const src, [[maybe_unused]] const Index j, Tile& tile) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    const auto* column = reinterpret_cast<const SIMD::vec_t*>(src);
    for (Index k = 0; k < Tiling::RegCount; ++k)
        if constexpr (op == Op::Add)
            tile[k] = vec_add_16(tile[k], column[k]);
        else
            tile[k] = vec_sub_16(tile[k], column[k]);
}

template<Op op>
ALWAYS_INLINE void
apply_single(const i8* src, [[maybe_unused]] const Index j, Tile& tile) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    [[maybe_unused]] const auto* column = reinterpret_cast<const SIMD::vec_i8_t*>(src);

    #if defined(USE_NEON)
    for (Index k = 0; k + 1 < Tiling::RegCount; k += 2)
    {
        if constexpr (op == Op::Add)
        {
            tile[k + 0] = vaddw_s8(tile[k + 0], vget_low_s8(column[k / 2]));
            tile[k + 1] = vaddw_high_s8(tile[k + 1], column[k / 2]);
        }
        else
        {
            tile[k + 0] = vsubw_s8(tile[k + 0], vget_low_s8(column[k / 2]));
            tile[k + 1] = vsubw_high_s8(tile[k + 1], column[k / 2]);
        }
    }
    #elif defined(USE_LSX) && !defined(USE_LASX)
    for (Index k = 0; k + 1 < Tiling::RegCount; k += 2)
    {
        const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);

        if constexpr (op == Op::Add)
        {
            tile[k + 0] = vec_add_16(tile[k + 0], __lsx_vsllwil_h_b(weight, 0));
            tile[k + 1] = vec_add_16(tile[k + 1], __lsx_vexth_h_b(weight));
        }
        else
        {
            tile[k + 0] = vec_sub_16(tile[k + 0], __lsx_vsllwil_h_b(weight, 0));
            tile[k + 1] = vec_sub_16(tile[k + 1], __lsx_vexth_h_b(weight));
        }
    }
    #elif defined(USE_PAIRED_WIDENING_ADD)
    static_assert(Tiling::RegCount % 2 == 0);
    const SIMD::vec_t zero = vec_zero();
        #if defined(__clang__)
            #pragma clang loop unroll(full)
        #elif defined(__GNUC__)
            #pragma GCC unroll(Tiling::RegCount / 2)
        #endif
    for (Index k = 0; k < Tiling::RegCount; k += 2)
    {
        const SIMD::vec_t weight = vec_load(reinterpret_cast<const SIMD::vec_t*>(src) + k / 2);
        const SIMD::vec_t lo     = vec_srai_16(vec_unpacklo_8(zero, weight), 8);
        const SIMD::vec_t hi     = vec_srai_16(vec_unpackhi_8(zero, weight), 8);

        if constexpr (op == Op::Add)
        {
            tile[k + 0] = vec_add_16(tile[k + 0], lo);
            tile[k + 1] = vec_add_16(tile[k + 1], hi);
        }
        else
        {
            tile[k + 0] = vec_sub_16(tile[k + 0], lo);
            tile[k + 1] = vec_sub_16(tile[k + 1], hi);
        }
    }
    #else
    for (Index k = 0; k < Tiling::RegCount; ++k)
    {
        if constexpr (op == Op::Add)
            tile[k] = vec_add_16(tile[k], vec_convert_8_16(column[k]));
        else
            tile[k] = vec_sub_16(tile[k], vec_convert_8_16(column[k]));
    }
    #endif
}

    #if defined(USE_PAIRED_WIDENING_ADD)
template<Op op>
ALWAYS_INLINE void apply_pair(const i8* src0, const i8* src1, Tile& tile) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    static_assert(Tiling::RegCount % 2 == 0);
    const SIMD::vec_t ones = vec_set_8(1);
        #if defined(__clang__)
            #pragma clang loop unroll(full)
        #elif defined(__GNUC__)
            #pragma GCC unroll(Tiling::RegCount / 2)
        #endif
    for (Index k = 0; k + 1 < Tiling::RegCount; k += 2)
    {
        const SIMD::vec_t w0 = vec_load(reinterpret_cast<const SIMD::vec_t*>(src0) + k / 2);
        const SIMD::vec_t w1 = vec_load(reinterpret_cast<const SIMD::vec_t*>(src1) + k / 2);
        const SIMD::vec_t lo = vec_maddubs_16(ones, vec_unpacklo_8(w0, w1));
        const SIMD::vec_t hi = vec_maddubs_16(ones, vec_unpackhi_8(w0, w1));

        if constexpr (op == Op::Add)
        {
            tile[k + 0] = vec_add_16(tile[k + 0], lo);
            tile[k + 1] = vec_add_16(tile[k + 1], hi);
        }
        else
        {
            tile[k + 0] = vec_sub_16(tile[k + 0], lo);
            tile[k + 1] = vec_sub_16(tile[k + 1], hi);
        }
    }
}
    #endif
#endif

///////////////////////////////////////////////////////////////////////////////
// The weight-table base already includes the tile offset.
template<Op op, bool Incremental = false>
ALWAYS_INLINE void
apply_features(const u16* indices, Index size, const i8* weights, Index j, Tile& tile) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    if constexpr (Incremental)
    {
        assert(0 <= size && size <= 2);
#if defined(VECTOR) || defined(USE_RVV)
    #if defined(USE_PAIRED_WIDENING_ADD)
        if (size == 2)
        {
            apply_pair<op>(&weights[indices[0] * Dimensions], &weights[indices[1] * Dimensions],
                           tile);
            return;
        }
    #endif
        if (size > 0)
            apply_single<op>(&weights[indices[0] * Dimensions], j, tile);
        if (size > 1)
            apply_single<op>(&weights[indices[1] * Dimensions], j, tile);
        return;
#endif
    }

    Index i = 0;
#if defined(USE_PAIRED_WIDENING_ADD)
    for (; i + 1 < size; i += 2)
        apply_pair<op>(&weights[indices[i + 0] * Dimensions], &weights[indices[i + 1] * Dimensions],
                       tile);
#endif
    for (; i < size; ++i)
        apply_single<op>(&weights[indices[i] * Dimensions], j, tile);
}

template<Op op>
ALWAYS_INLINE void apply_psq_features(const Features::K32H3::IndexList& psqList,
                                      const Weight*                     psqWeights,
                                      Index                             j,
                                      Tile&                             tile) noexcept {
    apply_features<op>(psqList.begin(), psqList.size(), &psqWeights[j], j, tile);
}

template<Op op>
ALWAYS_INLINE void apply_threat_features(const Features::FullThreats::IndexList& thrList,
                                         const FeatureTransformer&               ft,
                                         Index                                   j,
                                         Tile&                                   tile) noexcept {
    apply_features<op>(thrList.begin(), thrList.size(), &ft.threatAndPpWeights[j], j, tile);
}

template<Op op, bool Incremental = false>
ALWAYS_INLINE void apply_combined_features(const Features::K32H3::IndexList&       psqList,
                                           const Features::FullThreats::IndexList& thrList,
                                           const FeatureTransformer&               ft,
                                           const Weight*                           psqBase,
                                           Index                                   j,
                                           Tile&                                   tile) noexcept {
    if constexpr (Incremental)
        assert(psqList.size() == 1 || psqList.size() == 2);

    const auto* psqWeights = &psqBase[j];
    const auto* thrWeights = &ft.threatAndPpWeights[j];
    Index       psqCount   = psqList.size();
    Index       thrBegin   = 0;
#if defined(USE_PAIRED_WIDENING_ADD)
    // Keep the last odd psq row paired with the first threat/pp row of the same sign.
    if (psqCount % 2 != 0 && thrList.size() != 0)
    {
        --psqCount;
        thrBegin = 1;
    }
#endif

    apply_features<op, Incremental>(psqList.begin(), psqCount, psqWeights, j, tile);
#if defined(USE_PAIRED_WIDENING_ADD)
    if (thrBegin != 0)
        apply_pair<op>(&psqWeights[psqList[psqCount] * Dimensions],
                       &thrWeights[thrList[0] * Dimensions], tile);
#endif
    apply_features<op>(thrList.begin() + thrBegin,
                       thrBegin < thrList.size() ? thrList.size() - thrBegin : 0, thrWeights, j,
                       tile);
}

}  // namespace

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
                   const Accumulator&        srcAcc,
                   Accumulator&              dstAcc,
                   AccumulatorCache&         accCache) noexcept {
    const auto& dirtyPiece = dstAcc.dirties.dirtyPiece;

    assert(dirtyPiece.movedPc == make_piece(perspective, KING));
    assert(is_ok(dirtyPiece.dstSq));
    assert((u8(dirtyPiece.orgSq) & 4) == (u8(dirtyPiece.dstSq) & 4));
    assert(srcAcc.computed[perspective]);
    assert(!dstAcc.computed[perspective]);

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

    const auto psqBlock = Features::K32H3::block(pos, perspective);

    const auto& oldEntry   = accCache[psqBlock][oldKingSq][perspective];
    auto&       newEntry   = accCache[psqBlock][newKingSq][perspective];
    const auto* psqWeights = featureTransformer.psq_weights(psqBlock);

    // "Remove" means we need to remove them from the cache entry,
    // "Add" means add them to the entry to get the accumulator we want
    Features::K32H3::IndexList oldRemove, oldAdd, newRemove, newAdd;

    Bitboard oldChangedBB = changed_bb(oldEntry.pieceMap, prePieceMap);
    Bitboard oldRemovedBB = oldChangedBB & oldEntry.piecesBB;
    Bitboard oldAddedBB   = oldChangedBB & prePiecesBB;

    Bitboard newChangedBB = changed_bb(newEntry.pieceMap, curPieceMap);
    Bitboard newRemovedBB = newChangedBB & newEntry.piecesBB;
    Bitboard newAddedBB   = newChangedBB & curPiecesBB;

    Features::K32H3::append_map_changed_indices(perspective, oldKingSq,          //
                                                oldEntry.pieceMap, prePieceMap,  //
                                                oldRemovedBB, oldAddedBB, oldRemove, oldAdd);
    Features::K32H3::append_map_changed_indices(perspective, newKingSq,          //
                                                newEntry.pieceMap, curPieceMap,  //
                                                newRemovedBB, newAddedBB, newRemove, newAdd);

    Features::FullThreats::IndexList thrRemoved, thrAdded;  // also contain pp indices

    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = Dimensions;
    Features::FullThreats::append_changed_indices(perspective, newKingSq,       //
                                                  dstAcc.dirties.dirtyThreats,  //
                                                  thrRemoved, thrAdded, pfBase, pfStride);
    Features::PP3Wide::append_changed_indices(perspective, newKingSq,         //
                                              dstAcc.dirties.dirtyPawnPairs,  //
                                              thrRemoved, thrAdded, pfBase, pfStride);

    const auto& srcAccm = srcAcc.accumulation[perspective];
    auto&       dstAccm = dstAcc.accumulation[perspective];

    Tile tile;

    for (Index j = 0; j < Dimensions; increment_index(j))
    {
        load_tile(newEntry.accumulation.data(), j, tile);

        apply_psq_features<Op::Sub>(newRemove, psqWeights, j, tile);
        apply_psq_features<Op::Add>(newAdd, psqWeights, j, tile);

        store_tile(newEntry.accumulation.data(), j, tile);

        // adding the old accumulator adds (most of) the threats and pp weights that needed
        apply<Op::Add>(&srcAccm[j], j, tile);
        // But have added a whole bunch of psq weights for the wrong king bucket which
        // need to remove first remove the cached psq accumulation for the old king position...
        apply<Op::Sub>(&oldEntry.accumulation[j], j, tile);

        // ... then adjust
        apply_combined_features<Op::Sub>(  //
          oldAdd, thrRemoved, featureTransformer, psqWeights, j, tile);
        apply_combined_features<Op::Add>(  //
          oldRemove, thrAdded, featureTransformer, psqWeights, j, tile);

        store_tile(dstAccm.data(), j, tile);
    }

    newEntry.pieceMap = curPieceMap;
    newEntry.piecesBB = curPiecesBB;

    dstAcc.computed[perspective] = true;
}

// HalfKA data comes from the Finny table entry, while the threats are built
// from the active threat features
void update_refresh_cache(const Color               perspective,
                          const Position&           pos,
                          const FeatureTransformer& featureTransformer,
                          Accumulator&              dstAcc,
                          AccumulatorCache&         accCache) noexcept {
    const Square kingSq   = pos.square(perspective, KING);
    const u8     psqBlock = Features::K32H3::block(pos, perspective);

    auto&       entry      = accCache[psqBlock][kingSq][perspective];
    const auto* psqWeights = featureTransformer.psq_weights(psqBlock);

    Features::FullThreats::IndexList active;
    Features::FullThreats::append_active_indices(perspective, pos, active);
    Features::PP3Wide::append_active_indices(perspective, pos, active);

    const auto& pieceMap = pos.piece_map();
    const auto  piecesBB = pos.pieces_bb();

    const Bitboard changedBB = changed_bb(entry.pieceMap, pieceMap);

    const Bitboard removedBB = changedBB & entry.piecesBB;
    const Bitboard addedBB   = changedBB & piecesBB;

    Features::K32H3::IndexList removed, added;
    Features::K32H3::append_map_changed_indices(perspective, kingSq, entry.pieceMap, pieceMap,
                                                removedBB, addedBB, removed, added);

    entry.pieceMap = pieceMap;
    entry.piecesBB = piecesBB;

    dstAcc.computed[perspective] = true;

    Tile tile;

    for (Index j = 0; j < Dimensions; increment_index(j))
    {
        load_tile(entry.accumulation.data(), j, tile);

        apply_psq_features<Op::Sub>(removed, psqWeights, j, tile);
        apply_psq_features<Op::Add>(added, psqWeights, j, tile);

        store_tile(entry.accumulation.data(), j, tile);

        apply_threat_features<Op::Add>(active, featureTransformer, j, tile);

        store_tile(dstAcc.accumulation[perspective].data(), j, tile);
    }
}

void apply_combined(Color                                   perspective,
                    const FeatureTransformer&               featureTransformer,
                    const Accumulator&                      srcAcc,
                    Accumulator&                            dstAcc,
                    const Features::K32H3::IndexList&       psqRemoved,
                    const Features::K32H3::IndexList&       psqAdded,
                    const Features::FullThreats::IndexList& thrRemoved,
                    const Features::FullThreats::IndexList& thrAdded,
                    const Weight*                           psqWeights) noexcept {
    const auto& srcAccm = srcAcc.accumulation[perspective];
    auto&       dstAccm = dstAcc.accumulation[perspective];

    Tile tile;

    for (Index j = 0; j < Dimensions; increment_index(j))
    {
        load_tile(srcAccm.data(), j, tile);

        apply_combined_features<Op::Sub, true>(  //
          psqRemoved, thrRemoved, featureTransformer, psqWeights, j, tile);
        apply_combined_features<Op::Add, true>(  //
          psqAdded, thrAdded, featureTransformer, psqWeights, j, tile);

        store_tile(dstAccm.data(), j, tile);
    }
}

void apply_combined_both(const FeatureTransformer&                          featureTransformer,
                         const Accumulator&                                 srcAcc,
                         Accumulator&                                       dstAcc,
                         const Array<Features::K32H3::IndexList, COLOR_NB>& psqRemoved,
                         const Array<Features::K32H3::IndexList, COLOR_NB>& psqAdded,
                         const Array<Features::FullThreats::IndexList, COLOR_NB>& thrRemoved,
                         const Array<Features::FullThreats::IndexList, COLOR_NB>& thrAdded,
                         const Weight*                                            wPsqWeights,
                         const Weight*                                            bPsqWeights)  //
  noexcept {
    apply_combined(WHITE, featureTransformer, srcAcc, dstAcc,  //
                   psqRemoved[WHITE], psqAdded[WHITE], thrRemoved[WHITE], thrAdded[WHITE],
                   wPsqWeights);
    apply_combined(BLACK, featureTransformer, srcAcc, dstAcc,  //
                   psqRemoved[BLACK], psqAdded[BLACK], thrRemoved[BLACK], thrAdded[BLACK],
                   bPsqWeights);
}

template<bool Forward>
void update_incremental(const Color               perspective,
                        const Square              kingSq,
                        const u8                  psqBlock,
                        const FeatureTransformer& featureTransformer,
                        const Accumulator&        srcAcc,
                        Accumulator&              dstAcc) noexcept {
    assert(srcAcc.computed[perspective]);
    assert(!dstAcc.computed[perspective]);

    // The size must be enough to contain the largest possible update.
    // That might depend on the feature set and generally relies on the
    // feature set's update cost calculation to be correct and never allow
    // updates with more added/removed features than MaxActiveDimensions.
    Features::K32H3::IndexList       psqRemoved, psqAdded;
    Features::FullThreats::IndexList thrRemoved, thrAdded;

    const auto& dirties = Forward ? dstAcc.dirties : srcAcc.dirties;

    const auto& dP   = dirties.dirtyPiece;
    const auto& dTs  = dirties.dirtyThreats;
    const auto& dPps = dirties.dirtyPawnPairs;

    // Used solely for prefetching
    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = Dimensions;

    Features::FullThreats::append_changed_indices(perspective, kingSq, dTs,         //
                                                  Forward ? thrRemoved : thrAdded,  //
                                                  Forward ? thrAdded : thrRemoved, pfBase,
                                                  pfStride);
    Features::PP3Wide::append_changed_indices(perspective, kingSq, dPps,        //
                                              Forward ? thrRemoved : thrAdded,  //
                                              Forward ? thrAdded : thrRemoved, pfBase, pfStride);
    Features::K32H3::append_changed_indices(perspective, kingSq, dP,          //
                                            Forward ? psqRemoved : psqAdded,  //
                                            Forward ? psqAdded : psqRemoved);

    apply_combined(perspective, featureTransformer, srcAcc, dstAcc, psqRemoved, psqAdded,
                   thrRemoved, thrAdded, featureTransformer.psq_weights(psqBlock));

    dstAcc.computed[perspective] = true;
}

void update_incremental_both(const FeatureTransformer& featureTransformer,
                             const Square              wKingSq,
                             const Square              bKingSq,
                             const u8                  whiteBlock,
                             const u8                  blackBlock,
                             const Accumulator&        srcAcc,
                             Accumulator&              dstAcc) noexcept {
    assert(srcAcc.computed[WHITE]);
    assert(srcAcc.computed[BLACK]);
    assert(!dstAcc.computed[WHITE]);
    assert(!dstAcc.computed[BLACK]);

    Array<Features::K32H3::IndexList, COLOR_NB>       psqRemoved, psqAdded;
    Array<Features::FullThreats::IndexList, COLOR_NB> thrRemoved, thrAdded;

    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = Dimensions;

    Features::FullThreats::append_changed_indices_both(wKingSq, bKingSq,             //
                                                       dstAcc.dirties.dirtyThreats,  //
                                                       thrRemoved, thrAdded, pfBase, pfStride);
    Features::PP3Wide::append_changed_indices_both(wKingSq, bKingSq,               //
                                                   dstAcc.dirties.dirtyPawnPairs,  //
                                                   thrRemoved, thrAdded, pfBase, pfStride);
    Features::K32H3::append_changed_indices_both(wKingSq, bKingSq,           //
                                                 dstAcc.dirties.dirtyPiece,  //
                                                 psqRemoved, psqAdded);

    apply_combined_both(featureTransformer, srcAcc, dstAcc,  //
                        psqRemoved, psqAdded, thrRemoved, thrAdded,
                        featureTransformer.psq_weights(whiteBlock),
                        featureTransformer.psq_weights(blackBlock));

    dstAcc.computed[WHITE] = true;
    dstAcc.computed[BLACK] = true;
}

}  // namespace

}  // namespace DON::NNUE
