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

#include "../../nnue/features/pp_3wide.h"

#include <algorithm>
#include <cassert>

#include "../../attacks.h"
#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../../nnue/types.h"
#include "../../utility/attributes.h"
#include "../../utility/types.h"

namespace DON::NNUE::Features::PP3Wide {
namespace {
ALWAYS_INLINE constexpr Index make_pawn_id(const Color c, const Square s) noexcept {
    assert(SQ_A2 <= s && s <= SQ_H7);

    return 48 * int(c) + s - SQ_A2;
}

ALWAYS_INLINE constexpr Index pp_index(const Index id1, const Index id2) noexcept {
    const Index idL = std::min(id1, id2);
    const Index idH = std::max(id1, id2);

    return BaseIndex + idL + idH * (idH - 1) / 2;
}

#if defined(USE_AVX512ICL)
ALWAYS_INLINE __m256i pp_index_epi16(const __m256i id1, const __m256i id2) noexcept {
    const __m256i idL  = _mm256_min_epu16(id1, id2);
    const __m256i idH  = _mm256_max_epu16(id1, id2);
    const __m256i prod = _mm256_mullo_epi16(idH, _mm256_sub_epi16(idH, _mm256_set1_epi16(1)));
    return _mm256_add_epi16(_mm256_set1_epi16(BaseIndex),
                            _mm256_add_epi16(idL, _mm256_srli_epi16(prod, 1)));
}
#endif

ALWAYS_INLINE constexpr Index make_index(const Color  perspective,
                                         const Square kingSq,
                                         const Color  color,
                                         const Square orgSq,
                                         const Square dstSq,
                                         const Color  pairedColor) noexcept {
    const u8 orient = u8(orientation(kingSq)) ^ (56 * perspective);

    const u8 org = u8(orgSq) ^ orient;
    const u8 dst = u8(dstSq) ^ orient;

    assert(SQ_A2 <= org && org <= SQ_H7);
    assert(SQ_A2 <= dst && dst <= SQ_H7);

    const Color rColor       = Color(color ^ perspective);
    const Color rPairedColor = Color(pairedColor ^ perspective);

    const Index id1 = make_pawn_id(rColor, Square{org});
    const Index id2 = make_pawn_id(rPairedColor, Square{dst});

    return pp_index(id1, id2);
}

template<usize N>
ALWAYS_INLINE void append_active_indices_(Color perspective,
                                          const Square (&kingSq)[N],
                                          const Bitboard updatedBB,
                                          const Bitboard wPawnsBB,
                                          const Bitboard bPawnsBB,
                                          IndexList* const (&active)[N],
                                          [[maybe_unused]] const ThreatWeight* const pfBase,
                                          [[maybe_unused]] const usize               pfStride)  //
  noexcept {
    static_assert(N == 1 || N == 2);

    if (updatedBB == 0)
        return;

    Array<u8, N> orientations;
    for (usize i = 0; i < N; ++i)
        orientations[i] = u8(orientation(kingSq[i])) ^ u8(56 * (perspective ^ i));

#if defined(USE_AVX512ICL)
    __m512i ids[N];
    for (usize i = 0; i < N; ++i)
    {
        const Bitboard enemyBB  = (perspective ^ i) == WHITE ? bPawnsBB : wPawnsBB;
        const __m512i  adjusted = _mm512_sub_epi8(
          _mm512_xor_si512(ALL_SQUARES, _mm512_set1_epi8(orientations[i])), _mm512_set1_epi8(8));
        ids[i] = _mm512_mask_add_epi8(adjusted, enemyBB, adjusted, _mm512_set1_epi8(48));
    }
#endif

    // Enumerate changed pawns and their partners once for all requested perspectives.
    const Bitboard unchangedBB = (wPawnsBB | bPawnsBB) & ~updatedBB;
    for (Bitboard uBB = updatedBB; uBB != 0;)
    {
        const Square   s          = pop_lsq(uBB);
        const Bitboard partnersBB = Attacks::pawn_pair_bb(s) & (unchangedBB | uBB);
        if (partnersBB == 0)
            continue;

#if defined(USE_AVX512ICL)
        const int n = popcount(partnersBB);
        assert(n <= 16);

        for (usize i = 0; i < N; ++i)
        {
            const Bitboard enemyBB = (perspective ^ i) == WHITE ? bPawnsBB : wPawnsBB;
            const u16      sId  = u16(((u8(s) ^ orientations[i]) - 8) + ((enemyBB & s) ? 48 : 0));
            const __m256i  pIds = _mm256_cvtepu8_epi16(
              _mm512_castsi512_si128(_mm512_maskz_compress_epi8(partnersBB, ids[i])));
            const __m256i feats = pp_index_epi16(_mm256_set1_epi16(sId), pIds);
            // Position::set material limits bound refresh lists to 232 entries:
            // 80 + 8 * (16 - P) + 2 * P + P * (P - 1) / 2, for P <= 16 pawns.
            // Updates have at most 96 threats + 120 pairs. Both fit in 256 entries
            // including the 15 extra lanes written by an unmasked store.
            u16* const w = active[i]->make_space(n);
            _mm256_storeu_epi16(w, feats);
        }
#else
        const Color sC = (bPawnsBB & s) ? BLACK : WHITE;

        Array<Index, N> sId;
        for (usize i = 0; i < N; ++i)
            sId[i] = make_pawn_id(Color(sC ^ perspective ^ i), Square(u8(s) ^ orientations[i]));

        const auto push_back = [&](const Square dst, const Color pairedColor) noexcept {
            for (usize i = 0; i < N; ++i)
            {
                const Index dId   = make_pawn_id(Color(pairedColor ^ perspective ^ i),
                                                 Square(u8(dst) ^ orientations[i]));
                const Index index = pp_index(sId[i], dId);

                if (pfBase != nullptr)
                    prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(reinterpret_cast<const void*>(
                      reinterpret_cast<uptr>(pfBase) + index * pfStride));

                active[i]->push_back(index);
            }
        };

        for (Bitboard pb = bPawnsBB & partnersBB; pb != 0;)
            push_back(pop_lsq(pb), BLACK);
        for (Bitboard pw = wPawnsBB & partnersBB; pw != 0;)
            push_back(pop_lsq(pw), WHITE);
#endif
    }
}

template<usize N>
ALWAYS_INLINE void append_changed_indices_(Color perspective,
                                           const Square (&ksq)[N],
                                           const DirtyType& dPps,
                                           IndexList* const (&removed)[N],
                                           IndexList* const (&added)[N],
                                           const ThreatWeight* const pfBase,
                                           const usize               pfStride) noexcept {
    const auto& beforeBB = dPps.beforeBB;
    const auto& afterBB  = dPps.afterBB;

    if (beforeBB[WHITE] == afterBB[WHITE] && beforeBB[BLACK] == afterBB[BLACK])
        return;

    append_active_indices_<N>(
      perspective, ksq, (beforeBB[WHITE] & ~afterBB[WHITE]) | (beforeBB[BLACK] & ~afterBB[BLACK]),
      beforeBB[WHITE], beforeBB[BLACK], removed, pfBase, pfStride);
    append_active_indices_<N>(
      perspective, ksq, (afterBB[WHITE] & ~beforeBB[WHITE]) | (afterBB[BLACK] & ~beforeBB[BLACK]),
      afterBB[WHITE], afterBB[BLACK], added, pfBase, pfStride);
}

}  // namespace

void append_active_indices(const Color     perspective,
                           const Position& pos,
                           IndexList&      active) noexcept {
    const Square   kingSq   = pos.square(perspective, KING);
    const Bitboard wPawnsBB = pos.pieces_bb(WHITE, PAWN);
    const Bitboard bPawnsBB = pos.pieces_bb(BLACK, PAWN);
    append_active_indices_<1>(perspective, {kingSq}, wPawnsBB | bPawnsBB, wPawnsBB, bPawnsBB,
                              {&active}, nullptr, 0);
}

void append_changed_indices(const Color               perspective,
                            const Square              kingSq,
                            const DirtyType&          dPps,
                            IndexList&                removed,
                            IndexList&                added,
                            const ThreatWeight* const pfBase,
                            const usize               pfStride) noexcept {
    append_changed_indices_<1>(perspective, {kingSq}, dPps, {&removed}, {&added}, pfBase, pfStride);
}

void append_changed_indices_both(const Square                wKingSq,
                                 const Square                bKingSq,
                                 const DirtyType&            dPps,
                                 Array<IndexList, COLOR_NB>& removed,
                                 Array<IndexList, COLOR_NB>& added,
                                 const ThreatWeight* const   pfBase,
                                 const usize                 pfStride) noexcept {
    append_changed_indices_<2>(WHITE, {wKingSq, bKingSq}, dPps,  //
                               {&removed[WHITE], &removed[BLACK]}, {&added[WHITE], &added[BLACK]},
                               pfBase, pfStride);
}
}  // namespace DON::NNUE::Features::PP3Wide
