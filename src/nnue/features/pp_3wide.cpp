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

    const auto id1 = make_pawn_id(rColor, Square{org});
    const auto id2 = make_pawn_id(rPairedColor, Square{dst});
    const auto idH = std::max(id1, id2);
    const auto idL = std::min(id1, id2);

    return BaseIndex + idL + idH * (idH - 1) / 2;
}

#if defined(USE_AVX512ICL)
ALWAYS_INLINE __m256i pp_idx_epi16(const __m256i a, const __m256i b) noexcept {
    const __m256i hi   = _mm256_max_epu16(a, b);
    const __m256i lo   = _mm256_min_epu16(a, b);
    const __m256i prod = _mm256_mullo_epi16(hi, _mm256_sub_epi16(hi, _mm256_set1_epi16(1)));
    return _mm256_add_epi16(_mm256_add_epi16(_mm256_srli_epi16(prod, 1), lo),
                            _mm256_set1_epi16(BaseIndex));
}
#endif
}  // namespace

void append_active_indices(const Color     perspective,
                           const Position& pos,
                           IndexList&      active) noexcept {
    const Square   kingSq   = pos.square(perspective, KING);
    const Bitboard wPawnsBB = pos.pieces_bb(WHITE, PAWN);
    const Bitboard bPawnsBB = pos.pieces_bb(BLACK, PAWN);

    Bitboard bb;

    bb = wPawnsBB;
    while (bb != 0)
    {
        const Square   orgSq = pop_lsq(bb);
        const Bitboard band  = Attacks::pawn_pair_bb(orgSq);
        for (Bitboard ww = band & bb; ww != 0;)
            active.push_back(make_index(perspective, kingSq, WHITE, orgSq, pop_lsq(ww), WHITE));
        for (Bitboard wb = band & bPawnsBB; wb != 0;)
            active.push_back(make_index(perspective, kingSq, WHITE, orgSq, pop_lsq(wb), BLACK));
    }

    bb = bPawnsBB;
    while (bb != 0)
    {
        const Square   orgSq = pop_lsq(bb);
        const Bitboard band  = Attacks::pawn_pair_bb(orgSq);
        for (Bitboard bbk = band & bb; bbk != 0;)
            active.push_back(make_index(perspective, kingSq, BLACK, orgSq, pop_lsq(bbk), BLACK));
    }
}

void append_changed_indices(const Color               perspective,
                            const Square              kingSq,
                            const DirtyType&          dPps,
                            IndexList&                removed,
                            IndexList&                added,
                            const ThreatWeight* const pfBase,
                            const usize               pfStride) noexcept {
    const auto& beforeBB = dPps.beforeBB;
    const auto& afterBB  = dPps.afterBB;

    if (beforeBB[WHITE] == afterBB[WHITE] && beforeBB[BLACK] == afterBB[BLACK])
        return;

#if defined(USE_AVX512ICL)
    const u8      relOrientation = relative_sq(perspective, orientation(kingSq));
    const __m512i iota           = ALL_SQUARES;
    const __m512i adjusted       = _mm512_sub_epi8(
      _mm512_xor_si512(iota, _mm512_set1_epi8(relOrientation)), _mm512_set1_epi8(8));

    const auto generate = [&](const Bitboard wUpdatedBB, const Bitboard bUpdatedBB,  //
                              const Bitboard wPawnsBB, const Bitboard bPawnsBB,      //
                              IndexList& out) noexcept {
        const Bitboard friendBB = perspective == WHITE ? wPawnsBB : bPawnsBB;
        const Bitboard enemyBB  = perspective == WHITE ? bPawnsBB : wPawnsBB;
        const __m512i  ids      = _mm512_mask_blend_epi8(
          friendBB, _mm512_add_epi8(adjusted, _mm512_set1_epi8(48)), adjusted);

        const Bitboard unchangedBB = (wPawnsBB | bPawnsBB) & ~(wUpdatedBB | bUpdatedBB);
        for (Bitboard updatedBB = wUpdatedBB | bUpdatedBB; updatedBB != 0;)
        {
            const Square   s        = pop_lsq(updatedBB);
            const Bitboard partners = Attacks::pawn_pair_bb(s) & (unchangedBB | updatedBB);
            const int      n        = popcount(partners);
            if (n == 0)
                continue;

            const u16     cOffset = (enemyBB & s) ? 48 : 0;
            const u16     aId     = u16(((u8(s) ^ relOrientation) - 8) + cOffset);
            const __m256i pids    = _mm256_cvtepu8_epi16(
              _mm512_castsi512_si128(_mm512_maskz_compress_epi8(partners, ids)));
            const __m256i feats = pp_idx_epi16(_mm256_set1_epi16(aId), pids);

            u16* w = out.make_space(n);
            _mm256_storeu_epi16(w, feats);
        }
    };
    (void) pfBase;
    (void) pfStride;
#else
    const auto generate = [&](const Bitboard wUpdatedBB, const Bitboard bUpdatedBB,  //
                              const Bitboard wPawnsBB, const Bitboard bPawnsBB,      //
                              IndexList& out) noexcept {
        const auto push = [&](Color color, Square orgSq, Square dstSq, Color pairedColor) noexcept {
            const Index index = make_index(perspective, kingSq, color, orgSq, dstSq, pairedColor);

            if (pfBase != nullptr)
                prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(
                  reinterpret_cast<const void*>(reinterpret_cast<uptr>(pfBase) + index * pfStride));

            out.push_back(index);
        };

        const Bitboard unchangedBB = (wPawnsBB | bPawnsBB) & ~(wUpdatedBB | bUpdatedBB);
        for (Bitboard updatedBB = wUpdatedBB | bUpdatedBB; updatedBB != 0;)
        {
            const Square   orgSq  = pop_lsq(updatedBB);
            const Bitboard maskBB = Attacks::pawn_pair_bb(orgSq) & (unchangedBB | updatedBB);
            const Color    color  = (bPawnsBB & orgSq) ? BLACK : WHITE;
            for (Bitboard bPairedBB = bPawnsBB & maskBB; bPairedBB != 0;)
                push(color, orgSq, pop_lsq(bPairedBB), BLACK);
            for (Bitboard wPairedBB = wPawnsBB & maskBB; wPairedBB != 0;)
                push(color, orgSq, pop_lsq(wPairedBB), WHITE);
        }
    };
#endif

    generate(beforeBB[WHITE] & ~afterBB[WHITE], beforeBB[BLACK] & ~afterBB[BLACK], beforeBB[WHITE],
             beforeBB[BLACK], removed);
    generate(afterBB[WHITE] & ~beforeBB[WHITE], afterBB[BLACK] & ~beforeBB[BLACK], afterBB[WHITE],
             afterBB[BLACK], added);
}

void append_changed_indices_both(const Square                wKingSq,
                                 const Square                bKingSq,
                                 const DirtyType&            dPps,
                                 Array<IndexList, COLOR_NB>& removed,
                                 Array<IndexList, COLOR_NB>& added,
                                 const ThreatWeight* const   pfBase,
                                 const usize                 pfStride) noexcept {
#if defined(USE_AVX512ICL)
    append_changed_indices(WHITE, wKingSq, dPps, removed[WHITE], added[WHITE], pfBase, pfStride);
    append_changed_indices(BLACK, bKingSq, dPps, removed[BLACK], added[BLACK], pfBase, pfStride);
#else
    const auto& beforeBB = dPps.beforeBB;
    const auto& afterBB  = dPps.afterBB;

    if (beforeBB[WHITE] == afterBB[WHITE] && beforeBB[BLACK] == afterBB[BLACK])
        return;

    const auto generate = [&](const Bitboard wUpdatedBB, const Bitboard bUpdatedBB,  //
                              const Bitboard wPawnsBB, const Bitboard bPawnsBB,      //
                              IndexList& wOut, IndexList& bOut) noexcept {
        const auto push = [&](Color color, Square orgSq, Square dstSq, Color pairedColor) noexcept {
            const Index wIndex = make_index(WHITE, wKingSq, color, orgSq, dstSq, pairedColor);
            const Index bIndex = make_index(BLACK, bKingSq, color, orgSq, dstSq, pairedColor);

            if (pfBase != nullptr)
            {
                prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(reinterpret_cast<const void*>(
                  reinterpret_cast<uptr>(pfBase) + wIndex * pfStride));
                prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(reinterpret_cast<const void*>(
                  reinterpret_cast<uptr>(pfBase) + bIndex * pfStride));
            }

            wOut.push_back(wIndex);
            bOut.push_back(bIndex);
        };

        const Bitboard unchangedBB = (wPawnsBB | bPawnsBB) & ~(wUpdatedBB | bUpdatedBB);
        for (Bitboard updatedBB = wUpdatedBB | bUpdatedBB; updatedBB != 0;)
        {
            const Square   orgSq  = pop_lsq(updatedBB);
            const Bitboard maskBB = Attacks::pawn_pair_bb(orgSq) & (unchangedBB | updatedBB);
            const Color    color  = (bPawnsBB & orgSq) ? BLACK : WHITE;

            for (Bitboard bPairedBB = bPawnsBB & maskBB; bPairedBB != 0;)
                push(color, orgSq, pop_lsq(bPairedBB), BLACK);
            for (Bitboard wPairedBB = wPawnsBB & maskBB; wPairedBB != 0;)
                push(color, orgSq, pop_lsq(wPairedBB), WHITE);
        }
    };

    generate(beforeBB[WHITE] & ~afterBB[WHITE], beforeBB[BLACK] & ~afterBB[BLACK],  //
             beforeBB[WHITE], beforeBB[BLACK], removed[WHITE], removed[BLACK]);
    generate(afterBB[WHITE] & ~beforeBB[WHITE], afterBB[BLACK] & ~beforeBB[BLACK],  //
             afterBB[WHITE], afterBB[BLACK], added[WHITE], added[BLACK]);
#endif
}
}  // namespace DON::NNUE::Features::PP3Wide
