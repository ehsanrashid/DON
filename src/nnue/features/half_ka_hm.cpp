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

// Definition of input features HalfKA_hm of NNUE evaluation function

#include "half_ka_hm.h"

#include <array>

#include "../../bitboard.h"
#include "../../misc.h"
#include "../../types.h"
#include "../common.h"

namespace DON::NNUE::Features {

namespace {

// Unique number for each piece type on each square
constexpr u16 PS_NONE     = 0;
constexpr u16 PS_W_PAWN   = 0 * SQUARE_NB;
constexpr u16 PS_B_PAWN   = 1 * SQUARE_NB;
constexpr u16 PS_W_KNIGHT = 2 * SQUARE_NB;
constexpr u16 PS_B_KNIGHT = 3 * SQUARE_NB;
constexpr u16 PS_W_BISHOP = 4 * SQUARE_NB;
constexpr u16 PS_B_BISHOP = 5 * SQUARE_NB;
constexpr u16 PS_W_ROOK   = 6 * SQUARE_NB;
constexpr u16 PS_B_ROOK   = 7 * SQUARE_NB;
constexpr u16 PS_W_QUEEN  = 8 * SQUARE_NB;
constexpr u16 PS_B_QUEEN  = 9 * SQUARE_NB;
constexpr u16 PS_KING     = 10 * SQUARE_NB;

alignas(CACHE_LINE_SIZE) constexpr Array<u16, COLOR_NB, PIECE_NB> PIECE_SQUARE_INDICES{{
  // Convention: W - us, B - them
  // Viewed from other side, W and B are reversed
  {PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE,   //
   PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE},  //
  {PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE,   //
   PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE}   //
}};

#define B(v) (v * HalfKA_hm::PS_NB)
alignas(CACHE_LINE_SIZE) constexpr Array<IndexType, SQUARE_NB> KING_BUCKETS{
  B(28), B(29), B(30), B(31), B(31), B(30), B(29), B(28),  //
  B(24), B(25), B(26), B(27), B(27), B(26), B(25), B(24),  //
  B(20), B(21), B(22), B(23), B(23), B(22), B(21), B(20),  //
  B(16), B(17), B(18), B(19), B(19), B(18), B(17), B(16),  //
  B(12), B(13), B(14), B(15), B(15), B(14), B(13), B(12),  //
  B(8),  B(9),  B(10), B(11), B(11), B(10), B(9),  B(8),   //
  B(4),  B(5),  B(6),  B(7),  B(7),  B(6),  B(5),  B(4),   //
  B(0),  B(1),  B(2),  B(3),  B(3),  B(2),  B(1),  B(0)    //
};
#undef B

// Mirror square to have king always on e..h files
// (file_of(s) >> 2) is 0 for 0...3, 1 for 4...7
constexpr Square orientation(Square s) noexcept { return Square(((file_of(s) >> 2) ^ 1) * FILE_H); }

static_assert(orientation(SQ_A1) == SQ_H1);
static_assert(orientation(SQ_D1) == SQ_H1);
static_assert(orientation(SQ_E1) == SQ_A1);
static_assert(orientation(SQ_H1) == SQ_A1);
static_assert(orientation(SQ_A8) == SQ_H1);
static_assert(orientation(SQ_H8) == SQ_A1);

// Index of a feature for king position and piece on square
ALWAYS_INLINE constexpr IndexType
make_index(Color perspective, Square kingSq, Square s, Piece pc) noexcept {
    u8 relOrientation = relative_sq(perspective, orientation(kingSq));
    return (u8(s) ^ relOrientation)                //
         + PIECE_SQUARE_INDICES[perspective][+pc]  //
         + KING_BUCKETS[relative_sq(perspective, kingSq)];
}

}  // namespace

#if defined(USE_AVX512ICL)
// Append lists of indices for recently changed features from the piece map
void HalfKA_hm::append_map_changed_indices(Color           perspective,
                                           Square          kingSq,
                                           const PieceMap& oldPieceMap,
                                           const PieceMap& newPieceMap,
                                           Bitboard        removedBB,
                                           Bitboard        addedBB,
                                           IndexList&      removed,
                                           IndexList&      added) noexcept {
    auto* removedWrite = removed.make_space(popcount(removedBB));
    auto* addedWrite   = added.make_space(popcount(addedBB));

    const __m512i oldPieceVec = _mm512_loadu_si512(oldPieceMap.data());
    const __m512i newPieceVec = _mm512_loadu_si512(newPieceMap.data());

    // PieceSquareIndex and KingBuckets are multiples of 64, while s and orient
    // use only the low six bits. Therefore no carry crosses bit 6, and
    // (s ^ orient) + psi[pc] + bucket == s ^ (psi[pc] + bucket + orient),
    // allowing the orientation to be folded into the per-piece lookup offset.
    const u16 flip   = 56 * perspective;
    const u16 orient = u16(orientation(kingSq)) ^ flip;

    // clang-format off
    const __m512i psi       = _mm512_castsi256_si512(_mm256_loadu_si256((const __m256i*) PIECE_SQUARE_INDICES[perspective].data()));
    const __m512i psiOffset = _mm512_add_epi16(psi, _mm512_set1_epi16(u16(KING_BUCKETS[u8(kingSq) ^ flip] + orient)));

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

    _mm512_storeu_si512(removedWrite     , _mm512_cvtepu16_epi32(_mm512_castsi512_si256(removedIndices)));
    _mm512_storeu_si512(removedWrite + 16, _mm512_cvtepu16_epi32(_mm512_extracti64x4_epi64(removedIndices, 1)));

    _mm512_storeu_si512(addedWrite     , _mm512_cvtepu16_epi32(_mm512_castsi512_si256(addedIndices)));
    _mm512_storeu_si512(addedWrite + 16, _mm512_cvtepu16_epi32(_mm512_extracti64x4_epi64(addedIndices, 1)));
    // clang-format on
}
#else
// Append lists of indices for recently changed features from the piece map
void HalfKA_hm::append_map_changed_indices(Color           perspective,
                                           Square          kingSq,
                                           const PieceMap& oldPieceMap,
                                           const PieceMap& newPieceMap,
                                           Bitboard        removedBB,
                                           Bitboard        addedBB,
                                           IndexList&      removed,
                                           IndexList&      added) noexcept {
    while (removedBB != 0)
    {
        Square s = pop_lsq(removedBB);

        removed.push_back(make_index(perspective, kingSq, s, oldPieceMap[s]));
    }

    while (addedBB != 0)
    {
        Square s = pop_lsq(addedBB);

        added.push_back(make_index(perspective, kingSq, s, newPieceMap[s]));
    }
}
#endif

// Append lists of indices for recently changed features
void HalfKA_hm::append_changed_indices(Color            perspective,
                                       Square           kingSq,
                                       const DirtyType& dp,
                                       IndexList&       removed,
                                       IndexList&       added) noexcept {
    removed.push_back(make_index(perspective, kingSq, dp.orgSq, dp.movedPc));

    if (dp.dstSq != SQ_NONE)
        added.push_back(make_index(perspective, kingSq, dp.dstSq, dp.movedPc));

    if (dp.removedSq != SQ_NONE)
        removed.push_back(make_index(perspective, kingSq, dp.removedSq, dp.removedPc));

    if (dp.addedSq != SQ_NONE)
        added.push_back(make_index(perspective, kingSq, dp.addedSq, dp.addedPc));
}

// Determine if a full refresh is required based on the dirty piece
bool HalfKA_hm::refresh_required(Color perspective, const DirtyType& dp) noexcept {
    return dp.movedPc == make_piece(perspective, KING);
}

}  // namespace DON::NNUE::Features
