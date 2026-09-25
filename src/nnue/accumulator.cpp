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
#include <cstddef>  // for offsetof()
#include <cstring>  // for memset()
#include <utility>  // for move()

#include "../bitboard.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "architecture.h"
#include "feature_transformer.h"
#include "ntypes.h"
#include "simd.h"

namespace DON::NNUE {

void AccumulatorCache::Entry::init(const Array<Bias, L1>& biases) noexcept {
    // Initialize accumulation with given biases
    accumulation          = biases;
    constexpr auto offset = offsetof(Entry, psqtAccumulation);
    static_assert(offset <= sizeof(Entry), "offset exceeds object size");
    std::memset(reinterpret_cast<u8*>(this) + offset, 0, sizeof(*this) - offset);
}

Array<AccumulatorCache::Entry, COLOR_NB>&  //
AccumulatorCache::operator[](const Square s) noexcept {
    return entries[s];
}

const Array<AccumulatorCache::Entry, COLOR_NB>&  //
AccumulatorCache::operator[](const Square s) const noexcept {
    return entries[s];
}

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

usize AccumulatorStack::size() const noexcept { return size_; }

Accumulator& AccumulatorStack::top() noexcept { return accumulators[size() - 1]; }

const Accumulator& AccumulatorStack::top() const noexcept { return accumulators[size() - 1]; }

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
        if (size() >= 2 && accumulators[size() - 2].computed[perspective]
            && pos.count() >= PC_COUNT_HYBRID_MIN)
        {
            const auto& dP = top().dirties.dirtyPiece;

            if (dP.movedPc == make_piece(perspective, KING)
                && ((u8(dP.orgSq) & u8{4}) == (u8(dP.dstSq) & u8{4}))
                // excludes castling
                && !is_ok(dP.addedSq))
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

usize AccumulatorStack::find_last_usable_index(const Color perspective) const noexcept {

    for (usize idx = size(); idx-- > 0;)
    {
        if (accumulators[idx].computed[perspective])
            return idx;

        // Threat feature set refreshes require a king move across the center, i.e.,
        // a subset of halfka refreshes
        if (PSQFeature::refresh_required(perspective, accumulators[idx].dirties.dirtyPiece))
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

    for (usize idx = std::max(size(), usize{1}) - 1; idx-- > end;)
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

constexpr auto Dimensions = FeatureTransformer::OutputDimensions;

namespace {
using Tiling = SIMD::Tiling
#if defined(VECTOR)
  <Dimensions, Dimensions, PSQT_BUCKETS>
#endif
  ;

enum class Op : u8 {
    Add,
    Sub
};

#if defined(USE_RVV)
using Tile     = vint16m8_t;
using PsqtTile = vint32m1_t;

ALWAYS_INLINE Tile load_tile(const i16* data, Index j) noexcept {
    usize vl = __riscv_vsetvl_e16m8(Dimensions - j);
    return __riscv_vle16_v_i16m8(data + j, vl);
}

ALWAYS_INLINE void store_tile(i16* dest, Index j, Tile acc) noexcept {
    usize vl = __riscv_vsetvl_e16m8(Dimensions - j);
    __riscv_vse16_v_i16m8(dest + j, acc, vl);
}

ALWAYS_INLINE PsqtTile load_psqt(const i32* data, Index j) noexcept {
    usize vl = __riscv_vsetvl_e32m1(PSQT_BUCKETS - j);
    return __riscv_vle32_v_i32m1(data + j, vl);
}

ALWAYS_INLINE void store_psqt(i32* dest, Index j, PsqtTile psqt) noexcept {
    usize vl = __riscv_vsetvl_e32m1(PSQT_BUCKETS - j);
    __riscv_vse32_v_i32m1(dest + j, psqt, vl);
}

ALWAYS_INLINE void increment_index(Index& j) noexcept { j += __riscv_vsetvl_e16m8(Dimensions - j); }

ALWAYS_INLINE void increment_psqt_index(Index& j) noexcept {
    j += __riscv_vsetvl_e32m1(PSQT_BUCKETS - j);
}

template<Op op>
ALWAYS_INLINE Tile apply(const i16* data, Index j, Tile acc) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    usize      vl      = __riscv_vsetvl_e16m8(Dimensions - j);
    vint16m8_t dataVec = __riscv_vle16_v_i16m8(data + j, vl);
    if constexpr (op == Op::Add)
        acc = __riscv_vadd_vv_i16m8(acc, dataVec, vl);
    else
        acc = __riscv_vsub_vv_i16m8(acc, dataVec, vl);
    return acc;
}

template<Op op>
ALWAYS_INLINE PsqtTile apply(const i32* data, Index j, PsqtTile acc) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    usize      vl      = __riscv_vsetvl_e32m1(PSQT_BUCKETS - j);
    vint32m1_t dataVec = __riscv_vle32_v_i32m1(data + j, vl);
    if constexpr (op == Op::Add)
        acc = __riscv_vadd_vv_i32m1(acc, dataVec, vl);
    else
        acc = __riscv_vsub_vv_i32m1(acc, dataVec, vl);
    return acc;
}

template<Op op>
ALWAYS_INLINE Tile apply_threat_features(const ThreatFeature::IndexList& in,
                                         const FeatureTransformer&       ft,
                                         Index                           j,
                                         Tile                            acc) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    usize vl = __riscv_vsetvl_e16m8(Dimensions - j);
    for (Index i = 0; i < in.size(); ++i)
    {
        const i8* column =
          reinterpret_cast<const i8*>(&ft.threatAndPpWeights[in[i] * Dimensions + j]);
        vint8m4_t weightVec = __riscv_vle8_v_i8m4(column, vl);
        if constexpr (op == Op::Add)
            acc = __riscv_vwadd_wv_i16m8(acc, weightVec, vl);
        else
            acc = __riscv_vwsub_wv_i16m8(acc, weightVec, vl);
    }
    return acc;
}

#else

struct Tile final {
   public:
    auto& operator[](int i) { return inner[i]; }

   private:
    SIMD::vec_t inner[Tiling::RegCount];
};

struct PsqtTile final {
   public:
    auto& operator[](int i) { return inner[i]; }

   private:
    SIMD::psqt_vec_t inner[Tiling::PSQTRegCount];
};

ALWAYS_INLINE Tile load_tile(const i16* data, Index j) noexcept {
    Tile  acc;
    auto* column = reinterpret_cast<const SIMD::vec_t*>(&data[j]);
    for (Index k = 0; k < Tiling::RegCount; ++k)
        acc[k] = column[k];
    return acc;
}

ALWAYS_INLINE void store_tile(i16* dest, Index j, Tile acc) noexcept {
    auto* column = reinterpret_cast<SIMD::vec_t*>(&dest[j]);
    for (Index k = 0; k < Tiling::RegCount; ++k)
        column[k] = acc[k];
}

ALWAYS_INLINE PsqtTile load_psqt(const i32* data, Index j) noexcept {
    PsqtTile psqt;
    auto*    column = reinterpret_cast<const SIMD::psqt_vec_t*>(&data[j]);
    for (Index k = 0; k < Tiling::PSQTRegCount; ++k)
        psqt[k] = column[k];
    return psqt;
}

ALWAYS_INLINE void store_psqt(i32* dest, Index j, PsqtTile psqt) noexcept {
    auto* column = reinterpret_cast<SIMD::psqt_vec_t*>(&dest[j]);
    for (Index k = 0; k < Tiling::PSQTRegCount; ++k)
        column[k] = psqt[k];
}

ALWAYS_INLINE void increment_index(Index& j) noexcept { j += Tiling::TileHeight; }

ALWAYS_INLINE void increment_psqt_index(Index& j) noexcept { j += Tiling::PSQTTileHeight; }

template<Op op>
ALWAYS_INLINE Tile apply(const i16* data, Index j, Tile acc) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    const auto* column = reinterpret_cast<const SIMD::vec_t*>(data + j);
    for (Index k = 0; k < Tiling::RegCount; ++k)
        if constexpr (op == Op::Add)
            acc[k] = vec_add_16(acc[k], column[k]);
        else
            acc[k] = vec_sub_16(acc[k], column[k]);
    return acc;
}

template<Op op>
ALWAYS_INLINE PsqtTile apply(const i32* data, Index j, PsqtTile acc) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    const auto* column = reinterpret_cast<const SIMD::psqt_vec_t*>(data + j);
    for (Index k = 0; k < Tiling::PSQTRegCount; ++k)
        if constexpr (op == Op::Add)
            acc[k] = vec_add_psqt_32(acc[k], column[k]);
        else
            acc[k] = vec_sub_psqt_32(acc[k], column[k]);
    return acc;
}

template<Op op>
ALWAYS_INLINE Tile apply_threat_features(const ThreatFeature::IndexList& in,
                                         const FeatureTransformer&       ft,
                                         const Index                     j,
                                         Tile                            acc) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    // clang-format off
    for (Index i = 0; i < in.size(); ++i)
    {
        auto* column = reinterpret_cast<const SIMD::vec_i8_t*>(&ft.threatAndPpWeights[in[i] * Dimensions + j]);

    #if defined(USE_NEON)
        for (Index k = 0; k + u16{1} < Tiling::RegCount; k += 2)
        {
            if constexpr (op == Op::Add)
            {
                acc[k + 0] = vaddw_s8(acc[k + 0], vget_low_s8(column[k / 2]));
                acc[k + 1] = vaddw_high_s8(acc[k + 1], column[k / 2]);
            }
            else
            {
                acc[k + 0] = vsubw_s8(acc[k + 0], vget_low_s8(column[k / 2]));
                acc[k + 1] = vsubw_high_s8(acc[k + 1], column[k / 2]);
            }
        }

    #elif defined(USE_LSX) && !defined(USE_LASX)
        for (Index k = 0; k + u16{1} < Tiling::RegCount; k += 2)
        {
            const __m128i weight = __lsx_vld(reinterpret_cast<const void*>(&column[k]), 0);

            if constexpr (op == Op::Add)
            {
                acc[k + 0] = vec_add_16(acc[k + 0], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1] = vec_add_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
            else
            {
                acc[k + 0] = vec_sub_16(acc[k + 0], __lsx_vsllwil_h_b(weight, 0));
                acc[k + 1] = vec_sub_16(acc[k + 1], __lsx_vexth_h_b(weight));
            }
        }

    #else
        for (Index k = 0; k < Tiling::RegCount; ++k)
        {
            if constexpr (op == Op::Add)
                acc[k] = vec_add_16(acc[k], vec_convert_8_16(column[k]));
            else
                acc[k] = vec_sub_16(acc[k], vec_convert_8_16(column[k]));
        }
    #endif
    }
    // clang-format on
    return acc;
}

#endif

template<Op op>
ALWAYS_INLINE Tile apply_psq_features(const PSQFeature::IndexList& in,
                                      const FeatureTransformer&    ft,
                                      const Index                  j,
                                      Tile                         acc) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    for (Index i = 0; i < in.size(); ++i)
        acc = apply<op>(&ft.weights[in[i] * Dimensions], j, acc);
    return acc;
}

template<Op op, typename IdxType, usize Size>
ALWAYS_INLINE PsqtTile apply_psqt(const FixedVector<IdxType, Size, IdxType>& in,
                                  const PSQTWeight*                          weights,
                                  const Index                                j,
                                  PsqtTile                                   acc) noexcept {
    static_assert(op == Op::Add || op == Op::Sub);
    for (Index i = 0; i < in.size(); ++i)
        acc = apply<op>(&weights[in[i] * PSQT_BUCKETS], j, acc);
    return acc;
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
    PSQFeature::IndexList oldRemove, oldAdd, newRemove, newAdd;

    Bitboard oldChangedBB = changed_bb(oldEntry.pieceMap, prePieceMap);
    Bitboard oldRemovedBB = oldChangedBB & oldEntry.piecesBB;
    Bitboard oldAddedBB   = oldChangedBB & prePiecesBB;

    Bitboard newChangedBB = changed_bb(newEntry.pieceMap, curPieceMap);
    Bitboard newRemovedBB = newChangedBB & newEntry.piecesBB;
    Bitboard newAddedBB   = newChangedBB & curPiecesBB;

    PSQFeature::append_map_changed_indices(perspective, oldKingSq, oldEntry.pieceMap, prePieceMap,
                                           oldRemovedBB, oldAddedBB, oldRemove, oldAdd);
    PSQFeature::append_map_changed_indices(perspective, newKingSq, newEntry.pieceMap, curPieceMap,
                                           newRemovedBB, newAddedBB, newRemove, newAdd);

    ThreatFeature::IndexList thrRemoved, thrAdded;  // also contain pp indices

    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = Dimensions;
    ThreatFeature::append_changed_indices(perspective, newKingSq, target.dirties.dirtyThreats,
                                          thrRemoved, thrAdded, pfBase, pfStride);
    PairFeature::append_changed_indices(perspective, newKingSq, target.dirties.dirtyPawnPairs,
                                        thrRemoved, thrAdded, pfBase, pfStride);

    const auto& sourceAcc = source.accumulation[perspective];
    auto&       targetAcc = target.accumulation[perspective];

    const auto& sourcePsqtAcc = source.psqtAccumulation[perspective];
    auto&       targetPsqtAcc = target.psqtAccumulation[perspective];

    Tile     acc;
    PsqtTile psqt;

    for (Index j = 0; j < Dimensions; increment_index(j))
    {
        acc = load_tile(newEntry.accumulation.data(), j);

        acc = apply_psq_features<Op::Sub>(newRemove, featureTransformer, j, acc);
        acc = apply_psq_features<Op::Add>(newAdd, featureTransformer, j, acc);

        store_tile(newEntry.accumulation.data(), j, acc);

        // adding the old accumulator adds (most of) the threats and pp weights that needed
        acc = apply<Op::Add>(sourceAcc.data(), j, acc);
        // But have added a whole bunch of psq weights for the wrong king bucket which
        // need to remove first remove the cached psq accumulation for the old king position...
        acc = apply<Op::Sub>(oldEntry.accumulation.data(), j, acc);

        // ... then adjust
        acc = apply_psq_features<Op::Add>(oldRemove, featureTransformer, j, acc);
        acc = apply_psq_features<Op::Sub>(oldAdd, featureTransformer, j, acc);

        acc = apply_threat_features<Op::Sub>(thrRemoved, featureTransformer, j, acc);
        acc = apply_threat_features<Op::Add>(thrAdded, featureTransformer, j, acc);

        store_tile(targetAcc.data(), j, acc);
    }

    for (Index j = 0; j < PSQT_BUCKETS; increment_psqt_index(j))
    {
        psqt = load_psqt(newEntry.psqtAccumulation.data(), j);

        psqt = apply_psqt<Op::Sub>(newRemove, featureTransformer.psqtWeights.data(), j, psqt);
        psqt = apply_psqt<Op::Add>(newAdd, featureTransformer.psqtWeights.data(), j, psqt);

        store_psqt(newEntry.psqtAccumulation.data(), j, psqt);

        psqt = apply<Op::Add>(sourcePsqtAcc.data(), j, psqt);
        psqt = apply<Op::Sub>(oldEntry.psqtAccumulation.data(), j, psqt);
        // clang-format off
        psqt = apply_psqt<Op::Add>(oldRemove, featureTransformer.psqtWeights.data(), j, psqt);
        psqt = apply_psqt<Op::Sub>(oldAdd, featureTransformer.psqtWeights.data(), j, psqt);

        psqt = apply_psqt<Op::Sub>(thrRemoved, featureTransformer.threatAndPpPsqtWeights.data(), j, psqt);
        psqt = apply_psqt<Op::Add>(thrAdded, featureTransformer.threatAndPpPsqtWeights.data(), j, psqt);
        // clang-format on
        store_psqt(targetPsqtAcc.data(), j, psqt);
    }

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
    const Square kingSq = pos.square<KING>(perspective);

    auto& entry = accCache[kingSq][perspective];

    ThreatFeature::IndexList active;
    ThreatFeature::append_active_indices(perspective, pos, active);
    PairFeature::append_active_indices(perspective, pos, active);

    const auto& pieceMap = pos.piece_map();
    const auto  piecesBB = pos.pieces_bb();

    const Bitboard changedBB = changed_bb(entry.pieceMap, pieceMap);

    const Bitboard removedBB = changedBB & entry.piecesBB;
    const Bitboard addedBB   = changedBB & piecesBB;

    PSQFeature::IndexList removed, added;
    PSQFeature::append_map_changed_indices(perspective, kingSq, entry.pieceMap, pieceMap, removedBB,
                                           addedBB, removed, added);

    entry.pieceMap = pieceMap;
    entry.piecesBB = piecesBB;

    target.computed[perspective] = true;

    Tile     acc;
    PsqtTile psqt;

    for (Index j = 0; j < Dimensions; increment_index(j))
    {
        acc = load_tile(entry.accumulation.data(), j);

        acc = apply_psq_features<Op::Sub>(removed, featureTransformer, j, acc);
        acc = apply_psq_features<Op::Add>(added, featureTransformer, j, acc);

        store_tile(entry.accumulation.data(), j, acc);

        acc = apply_threat_features<Op::Add>(active, featureTransformer, j, acc);

        store_tile(target.accumulation[perspective].data(), j, acc);
    }

    for (Index j = 0; j < PSQT_BUCKETS; increment_psqt_index(j))
    {
        psqt = load_psqt(entry.psqtAccumulation.data(), j);
        // clang-format off
        psqt = apply_psqt<Op::Sub>(removed, featureTransformer.psqtWeights.data(), j, psqt);
        psqt = apply_psqt<Op::Add>(added, featureTransformer.psqtWeights.data(), j, psqt);

        store_psqt(entry.psqtAccumulation.data(), j, psqt);

        psqt = apply_psqt<Op::Add>(active, featureTransformer.threatAndPpPsqtWeights.data(), j, psqt);
        // clang-format on
        store_psqt(target.psqtAccumulation[perspective].data(), j, psqt);
    }
}

void apply_combined(Color                           perspective,
                    const FeatureTransformer&       featureTransformer,
                    const Accumulator&              source,
                    Accumulator&                    target,
                    const PSQFeature::IndexList&    psqRemoved,
                    const PSQFeature::IndexList&    psqAdded,
                    const ThreatFeature::IndexList& thrRemoved,
                    const ThreatFeature::IndexList& thrAdded) noexcept {
    const auto& sourceAcc = source.accumulation[perspective];
    auto&       targetAcc = target.accumulation[perspective];

    const auto& sourcePsqtAcc = source.psqtAccumulation[perspective];
    auto&       targetPsqtAcc = target.psqtAccumulation[perspective];

    Tile     acc;
    PsqtTile psqt;

    for (Index j = 0; j < Dimensions; increment_index(j))
    {
        acc = load_tile(sourceAcc.data(), j);

        acc = apply_psq_features<Op::Sub>(psqRemoved, featureTransformer, j, acc);
        acc = apply_psq_features<Op::Add>(psqAdded, featureTransformer, j, acc);

        acc = apply_threat_features<Op::Sub>(thrRemoved, featureTransformer, j, acc);
        acc = apply_threat_features<Op::Add>(thrAdded, featureTransformer, j, acc);

        store_tile(targetAcc.data(), j, acc);
    }

    for (Index j = 0; j < PSQT_BUCKETS; increment_psqt_index(j))
    {
        psqt = load_psqt(sourcePsqtAcc.data(), j);
        // clang-format off
        psqt = apply_psqt<Op::Sub>(psqRemoved, featureTransformer.psqtWeights.data(), j, psqt);
        psqt = apply_psqt<Op::Add>(psqAdded, featureTransformer.psqtWeights.data(), j, psqt);

        psqt = apply_psqt<Op::Sub>(thrRemoved, featureTransformer.threatAndPpPsqtWeights.data(), j, psqt);
        psqt = apply_psqt<Op::Add>(thrAdded, featureTransformer.threatAndPpPsqtWeights.data(), j, psqt);
        // clang-format on
        store_psqt(targetPsqtAcc.data(), j, psqt);
    }
}

void apply_combined_both(const FeatureTransformer&                        featureTransformer,
                         const Accumulator&                               source,
                         Accumulator&                                     target,
                         const Array<PSQFeature::IndexList, COLOR_NB>&    psqRemoved,
                         const Array<PSQFeature::IndexList, COLOR_NB>&    psqAdded,
                         const Array<ThreatFeature::IndexList, COLOR_NB>& thrRemoved,
                         const Array<ThreatFeature::IndexList, COLOR_NB>& thrAdded) noexcept {
    apply_combined(WHITE, featureTransformer, source, target,  //
                   psqRemoved[WHITE], psqAdded[WHITE], thrRemoved[WHITE], thrAdded[WHITE]);
    apply_combined(BLACK, featureTransformer, source, target,  //
                   psqRemoved[BLACK], psqAdded[BLACK], thrRemoved[BLACK], thrAdded[BLACK]);
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
    PSQFeature::IndexList    psqRemoved, psqAdded;
    ThreatFeature::IndexList thrRemoved, thrAdded;

    const auto& dirties = Forward ? target.dirties : source.dirties;

    const auto& dP   = dirties.dirtyPiece;
    const auto& dTs  = dirties.dirtyThreats;
    const auto& dPps = dirties.dirtyPawnPairs;

    // Used solely for prefetching
    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = Dimensions;

    ThreatFeature::append_changed_indices(perspective, kingSq, dTs, Forward ? thrRemoved : thrAdded,
                                          Forward ? thrAdded : thrRemoved, pfBase, pfStride);
    PairFeature::append_changed_indices(perspective, kingSq, dPps, Forward ? thrRemoved : thrAdded,
                                        Forward ? thrAdded : thrRemoved, pfBase, pfStride);
    PSQFeature::append_changed_indices(perspective, kingSq, dP,  //
                                       Forward ? psqRemoved : psqAdded,
                                       Forward ? psqAdded : psqRemoved);

    apply_combined(perspective, featureTransformer, source, target, psqRemoved, psqAdded,
                   thrRemoved, thrAdded);

    target.computed[perspective] = true;
}

void update_incremental_both(const FeatureTransformer& featureTransformer,
                             const Square              wKingSq,
                             const Square              bKingSq,
                             const Accumulator&        source,
                             Accumulator&              target) {
    assert(source.computed[WHITE]);
    assert(source.computed[BLACK]);
    assert(!target.computed[WHITE]);
    assert(!target.computed[BLACK]);

    Array<PSQFeature::IndexList, COLOR_NB>    psqRemoved, psqAdded;
    Array<ThreatFeature::IndexList, COLOR_NB> thrRemoved, thrAdded;

    const auto* pfBase   = featureTransformer.threatAndPpWeights.data();
    const usize pfStride = Dimensions;

    ThreatFeature::append_changed_indices_both(wKingSq, bKingSq, target.dirties.dirtyThreats,
                                               thrRemoved, thrAdded, pfBase, pfStride);
    PairFeature::append_changed_indices_both(wKingSq, bKingSq, target.dirties.dirtyPawnPairs,
                                             thrRemoved, thrAdded, pfBase, pfStride);
    PSQFeature::append_changed_indices_both(wKingSq, bKingSq, target.dirties.dirtyPiece, psqRemoved,
                                            psqAdded);

    apply_combined_both(featureTransformer, source, target,  //
                        psqRemoved, psqAdded, thrRemoved, thrAdded);

    target.computed[WHITE] = true;
    target.computed[BLACK] = true;
}

}  // namespace

}  // namespace DON::NNUE
