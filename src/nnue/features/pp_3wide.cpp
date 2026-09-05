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

#include "pp_3wide.h"

#include <algorithm>
#include <cassert>

#include "../../attacks.h"
#include "../../bitboard.h"
#include "../../misc.h"
#include "../../position.h"
#include "../../types.h"
#include "../ntypes.h"
#include "full_threats.h"

namespace DON::NNUE::Features {

namespace {

ALWAYS_INLINE constexpr u16 make_pawn_id(const Color c, const Square s) noexcept {
    assert(SQ_A2 <= s && s <= SQ_H7);

    return 48 * int(c) + s - SQ_A2;
}

ALWAYS_INLINE constexpr u16 make_index(Color  perspective,
                                       Square kingSq,
                                       Color  color,
                                       Square orgSq,
                                       Square dstSq,
                                       Color  pairedColor) noexcept {
    u8 relOrientation = relative_sq(perspective, FullThreats::orientation(kingSq));

    u8 org = static_cast<u8>(orgSq) ^ relOrientation;
    u8 dst = static_cast<u8>(dstSq) ^ relOrientation;

    Color color_oriented       = Color(color ^ perspective);
    Color pairedColor_oriented = Color(pairedColor ^ perspective);

    assert(SQ_A2 <= org && org <= SQ_H7);
    assert(SQ_A2 <= dst && dst <= SQ_H7);

    const u16 id1 = make_pawn_id(color_oriented, Square{org});
    const u16 id2 = make_pawn_id(pairedColor_oriented, Square{dst});
    const u16 idH = std::max(id1, id2);
    const u16 idL = std::min(id1, id2);

    return PP3Wide::IndexBase + idH * (idH - 1) / 2 + idL;
}

#if defined(USE_AVX512ICL)
ALWAYS_INLINE __m256i pp_idx_epi16(const __m256i a, const __m256i b) noexcept {
    const __m256i hi   = _mm256_max_epu16(a, b);
    const __m256i lo   = _mm256_min_epu16(a, b);
    const __m256i prod = _mm256_mullo_epi16(hi, _mm256_sub_epi16(hi, _mm256_set1_epi16(1)));
    return _mm256_add_epi16(_mm256_add_epi16(_mm256_srli_epi16(prod, 1), lo),
                            _mm256_set1_epi16(i16(PP3Wide::IndexBase)));
}
#endif

}  // namespace

void PP3Wide::append_active_indices(const Color     perspective,
                                    const Position& pos,
                                    IndexVector&    active) noexcept {
    const Square   kingSq   = pos.square<KING>(perspective);
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

void PP3Wide::append_changed_indices(const Color                                    perspective,
                                     const Square                                   kingSq,
                                     const DirtyType&                               dPps,
                                     IndexVector&                                   removed,
                                     IndexVector&                                   added,
                                     [[maybe_unused]] const ThreatWeightType* const pfBase,
                                     [[maybe_unused]] const IndexType pfStride) noexcept {
    const Bitboard wBefore = dPps.before[WHITE];
    const Bitboard bBefore = dPps.before[BLACK];
    const Bitboard wAfter  = dPps.after[WHITE];
    const Bitboard bAfter  = dPps.after[BLACK];

    if (wBefore == wAfter && bBefore == bAfter)
        return;

#if defined(USE_AVX512ICL)
    const u8      relOrientation = relative_sq(perspective, FullThreats::orientation(kingSq));
    const __m512i iota           = ALL_SQUARES;
    const __m512i adjusted       = _mm512_sub_epi8(
      _mm512_xor_si512(iota, _mm512_set1_epi8(relOrientation)), _mm512_set1_epi8(8));

    const auto generate = [&](const Bitboard wUpdatedBB, const Bitboard bUpdatedBB,
                              const Bitboard wPawnsBB, const Bitboard bPawnsBB,
                              IndexVector& vec) noexcept {
        const Bitboard friendBB = perspective == WHITE ? wPawnsBB : bPawnsBB;
        const Bitboard enemyBB  = perspective == WHITE ? bPawnsBB : wPawnsBB;
        const __m512i  ids      = _mm512_mask_blend_epi8(
          friendBB, _mm512_add_epi8(adjusted, _mm512_set1_epi8(48)), adjusted);

        const Bitboard unchangedBB = (wPawnsBB | bPawnsBB) & ~(wUpdatedBB | bUpdatedBB);
        for (Bitboard uBB = wUpdatedBB | bUpdatedBB; uBB != 0;)
        {
            const Square   s        = pop_lsq(uBB);
            const Bitboard partners = Attacks::pawn_pair_bb(s) & (unchangedBB | uBB);
            const int      n        = popcount(partners);
            if (n == 0)
                continue;

            const u16     cOffset = (enemyBB & s) ? 48 : 0;
            const u16     aId     = u16(((u8(s) ^ relOrientation) - 8) + cOffset);
            const __m256i pids    = _mm256_cvtepu8_epi16(
              _mm512_castsi512_si128(_mm512_maskz_compress_epi8(partners, ids)));
            const __m256i feats = pp_idx_epi16(_mm256_set1_epi16(aId), pids);

            u16* w = vec.make_space(n);
            _mm256_storeu_epi16(w, feats);
        }
    };
#else
    const auto generate = [&](const Bitboard wUpdatedBB, const Bitboard bUpdatedBB,
                              const Bitboard wPawnsBB, const Bitboard bPawnsBB, IndexVector& vec) {
        const auto push = [&](const u16 index) noexcept {
            if (pfBase)
                prefetch<PrefetchAccess::READ, PrefetchLoc::LOW>(
                  reinterpret_cast<const void*>(reinterpret_cast<uptr>(pfBase) + index * pfStride));
            vec.push_back(index);
        };

        const Bitboard unchangedBB = (wPawnsBB | bPawnsBB) & ~(wUpdatedBB | bUpdatedBB);
        for (Bitboard uBB = wUpdatedBB | bUpdatedBB; uBB != 0;)
        {
            const Square   s    = pop_lsq(uBB);
            const Bitboard mask = Attacks::pawn_pair_bb(s) & (unchangedBB | uBB);
            const Color    sC   = (bPawnsBB & s) ? BLACK : WHITE;
            for (Bitboard pb = bPawnsBB & mask; pb != 0;)
                push(make_index(perspective, kingSq, sC, s, pop_lsq(pb), BLACK));
            for (Bitboard pw = wPawnsBB & mask; pw != 0;)
                push(make_index(perspective, kingSq, sC, s, pop_lsq(pw), WHITE));
        }
    };
#endif

    generate(wBefore & ~wAfter, bBefore & ~bAfter, wBefore, bBefore, removed);
    generate(wAfter & ~wBefore, bAfter & ~bBefore, wAfter, bAfter, added);
}

}  // namespace DON::NNUE::Features
