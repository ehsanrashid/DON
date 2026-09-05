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

// Definition of input features HalfKAHm of NNUE evaluation function

#include "half_ka_hm.h"

#include <array>

#if defined(USE_AVX512ICL)
    #include <immintrin.h>
#endif

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../types.h"

namespace DON::NNUE::Features {

namespace {

// Index of a feature for king position and piece on square
ALWAYS_INLINE constexpr u16
make_index(const Color perspective, const Square kingSq, const Square s, const Piece pc) noexcept {
    const u8 relOrientation = relative_sq(perspective, HalfKAHm::orientation(kingSq));
    return (static_cast<u8>(s) ^ relOrientation)             //
         + HalfKAHm::PIECE_SQUARE_INDICES[perspective][+pc]  //
         + HalfKAHm::KING_BUCKETS[relative_sq(perspective, kingSq)];
}

}  // namespace

// Append lists of indices for recently changed features from the piece map
void HalfKAHm::append_map_changed_indices(const Color     perspective,
                                          const Square    kingSq,
                                          const PieceMap& oldPieceMap,
                                          const PieceMap& newPieceMap,
                                          Bitboard        removedBB,
                                          Bitboard        addedBB,
                                          IndexVector&    removed,
                                          IndexVector&    added) noexcept {
#if defined(USE_AVX512ICL)
    const __m512i oldPieceVec = _mm512_loadu_si512(oldPieceMap.data());
    const __m512i newPieceVec = _mm512_loadu_si512(newPieceMap.data());

    // PIECE_SQUARE_INDICES and KING_BUCKETS are multiples of 64,
    // while s and orient use only the low six bits.
    // Therefore no carry crosses bit 6, and
    // (s ^ orient) + psi[pc] + bucket == s ^ (psi[pc] + bucket + orient),
    // allowing the orientation to be folded into the per-piece lookup offset.
    const u16 flip   = 56 * perspective;
    const u16 orient = static_cast<u16>(orientation(kingSq)) ^ flip;

    // clang-format off
    const __m512i psi       = _mm512_castsi256_si512(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(PIECE_SQUARE_INDICES[perspective].data())));
    const __m512i psiOffset = _mm512_add_epi16(psi, _mm512_set1_epi16(static_cast<u16>(KING_BUCKETS[static_cast<u8>(kingSq) ^ flip] + orient)));

    __m512i removedSquares = _mm512_maskz_compress_epi8(removedBB, ALL_SQUARES);
    __m512i removedPieces  = _mm512_maskz_compress_epi8(removedBB, oldPieceVec);
    __m512i addedSquares   = _mm512_maskz_compress_epi8(addedBB, ALL_SQUARES);
    __m512i addedPieces    = _mm512_maskz_compress_epi8(addedBB, newPieceVec);

    removedSquares = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(removedSquares));
    removedPieces  = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(removedPieces));
    addedSquares   = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(addedSquares));
    addedPieces    = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(addedPieces));

    const __m512i removedIndices = _mm512_xor_si512(removedSquares, _mm512_permutexvar_epi16(removedPieces, psiOffset));
    const __m512i addedIndices   = _mm512_xor_si512(addedSquares, _mm512_permutexvar_epi16(addedPieces, psiOffset));

    auto* removedSpace = removed.make_space(popcount(removedBB));
    auto* addedSpace   = added.make_space(popcount(addedBB));

    _mm512_storeu_si512(removedSpace, removedIndices);
    _mm512_storeu_si512(addedSpace, addedIndices);
    // clang-format on
#else
    while (removedBB != 0)
    {
        const Square s = pop_lsq(removedBB);

        removed.push_back(make_index(perspective, kingSq, s, oldPieceMap[s]));
    }

    while (addedBB != 0)
    {
        const Square s = pop_lsq(addedBB);

        added.push_back(make_index(perspective, kingSq, s, newPieceMap[s]));
    }
#endif
}

// Append lists of indices for recently changed features
void HalfKAHm::append_changed_indices(const Color      perspective,
                                      const Square     kingSq,
                                      const DirtyType& dP,
                                      IndexVector&     removed,
                                      IndexVector&     added) noexcept {
    // clang-format off
    removed.push_back   (make_index(perspective, kingSq, dP.orgSq, dP.movedPc));
    added.  push_back_if(make_index(perspective, kingSq, dP.dstSq, dP.movedPc)      , is_ok(dP.dstSq));
    removed.push_back_if(make_index(perspective, kingSq, dP.removedSq, dP.removedPc), is_ok(dP.removedSq));
    added.  push_back_if(make_index(perspective, kingSq, dP.addedSq, dP.addedPc)    , is_ok(dP.addedSq));
    // clang-format on
}

// Determine if a full refresh is required based on the dirty piece
bool HalfKAHm::refresh_required(const Color perspective, const DirtyType& dP) noexcept {
    return dP.movedPc == make_piece(perspective, KING);
}

}  // namespace DON::NNUE::Features
