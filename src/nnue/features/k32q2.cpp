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

// Definition of input features K32Q2 of NNUE evaluation function

#include "../../nnue/features/k32q2.h"

#include <array>
#include <initializer_list>

#if defined(USE_AVX512ICL)
    #include <immintrin.h>
#endif

#include "../../bitboard.h"
#include "../../types.h"
#include "../../nnue/types.h"
#include "../../utility/attributes.h"
#include "../../utility/types.h"

namespace DON::NNUE::Features {

namespace {

// Unique number for each piece type on each square
constexpr Index PS_NONE     = 0;
constexpr Index PS_W_PAWN   = 0 * SQUARE_NB;
constexpr Index PS_B_PAWN   = 1 * SQUARE_NB;
constexpr Index PS_W_KNIGHT = 2 * SQUARE_NB;
constexpr Index PS_B_KNIGHT = 3 * SQUARE_NB;
constexpr Index PS_W_BISHOP = 4 * SQUARE_NB;
constexpr Index PS_B_BISHOP = 5 * SQUARE_NB;
constexpr Index PS_W_ROOK   = 6 * SQUARE_NB;
constexpr Index PS_B_ROOK   = 7 * SQUARE_NB;
constexpr Index PS_W_QUEEN  = 8 * SQUARE_NB;
constexpr Index PS_B_QUEEN  = 9 * SQUARE_NB;
constexpr Index PS_KING     = 10 * SQUARE_NB;
constexpr Index PS_NB       = 11 * SQUARE_NB;

constexpr Array<Index, COLOR_NB, PIECE_NB> PIECE_SQUARE_INDICES{{
  // Convention: W - us, B - them
  // Viewed from other side, W and B are reversed
  {PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE,   //
   PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE},  //
  {PS_NONE, PS_B_PAWN, PS_B_KNIGHT, PS_B_BISHOP, PS_B_ROOK, PS_B_QUEEN, PS_KING, PS_NONE,   //
   PS_NONE, PS_W_PAWN, PS_W_KNIGHT, PS_W_BISHOP, PS_W_ROOK, PS_W_QUEEN, PS_KING, PS_NONE}   //
}};

#define B(v) (2 * v * PS_NB)
// clang-format off
    static constexpr Array<Index, SQUARE_NB> KING_BUCKETS{
      B(28), B(29), B(30), B(31), B(31), B(30), B(29), B(28),  //
      B(24), B(25), B(26), B(27), B(27), B(26), B(25), B(24),  //
      B(20), B(21), B(22), B(23), B(23), B(22), B(21), B(20),  //
      B(16), B(17), B(18), B(19), B(19), B(18), B(17), B(16),  //
      B(12), B(13), B(14), B(15), B(15), B(14), B(13), B(12),  //
      B( 8), B(9 ), B(10), B(11), B(11), B(10), B( 9), B( 8),  //
      B( 4), B( 5), B( 6), B( 7), B( 7), B( 6), B( 5), B( 4),  //
      B( 0), B( 1), B( 2), B( 3), B( 3), B( 2), B( 1), B( 0)   //
    };
// clang-format on
#undef B

alignas(CACHE_LINE_SIZE) constexpr auto OFFSETS = []() constexpr noexcept {
    Array<u32, 2 * COLOR_NB * SQUARE_NB, PIECE_NB> offsets{};

    for (const bool q : {false, true})
    {
        const u16 queenOffset = q ? PS_NB : 0;
        for (const Color c : {WHITE, BLACK})
        {
            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                const u16 flip   = 56 * c;
                const u16 orient = relative_sq(c, K32Q2::orientation(s));
                for (usize pieceIndex = 0; pieceIndex < PIECE_NB; ++pieceIndex)
                    offsets[(int(q) * COLOR_NB + c) * SQUARE_NB + s][pieceIndex] =
                      PIECE_SQUARE_INDICES[c][pieceIndex] + KING_BUCKETS[u8(s) ^ flip] + queenOffset
                      + orient;
            }
        }
    }

    return offsets;
}();

// Index of a feature for king position and piece on square
ALWAYS_INLINE constexpr Index make_index(const Color  perspective,
                                         const Square kingSq,
                                         bool         opponentHasQueen,
                                         const Square s,
                                         const Piece  pc) noexcept {
    return Index(s)
         ^ OFFSETS[(int(opponentHasQueen) * COLOR_NB + perspective) * SQUARE_NB + kingSq][+pc];
}

}  // namespace

void K32Q2::append_map_changed_indices(const Color     perspective,
                                       const Square    kingSq,
                                       bool            opponentHasQueen,
                                       const PieceMap& oldPieceMap,
                                       const PieceMap& newPieceMap,
                                       Bitboard        removedBB,
                                       Bitboard        addedBB,
                                       IndexList&      removed,
                                       IndexList&      added) noexcept {
#if defined(USE_AVX512ICL)
    const __m512i oldPieceVec = _mm512_loadu_si512(oldPieceMap.data());
    const __m512i newPieceVec = _mm512_loadu_si512(newPieceMap.data());

    // PIECE_SQUARE_INDICES and KING_BUCKETS are multiples of 64,
    // while s and orient use only the low six bits.
    // Therefore no carry crosses bit 6, and
    // (s ^ orient) + psi[pc] + bucket == s ^ (psi[pc] + bucket + orient),
    // allowing the orientation to be folded into the per-piece lookup offset.
    const u16 flip        = 56 * perspective;
    const u16 orient      = static_cast<u16>(orientation(kingSq)) ^ flip;
    const u16 queenOffset = opponentHasQueen ? PS_NB : 0;

    // clang-format off
    const __m512i psi       = _mm512_castsi256_si512(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(PIECE_SQUARE_INDICES[perspective].data())));
    const __m512i psiOffset = _mm512_add_epi16(psi, _mm512_set1_epi16(u16(KING_BUCKETS[u8(kingSq) ^ flip] + queenOffset + orient)));

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

        removed.push_back(make_index(perspective, kingSq, opponentHasQueen, s, oldPieceMap[s]));
    }
    while (addedBB != 0)
    {
        const Square s = pop_lsq(addedBB);

        added.push_back(make_index(perspective, kingSq, opponentHasQueen, s, newPieceMap[s]));
    }
#endif
}

void K32Q2::append_changed_indices(const Color      perspective,
                                   const Square     kingSq,
                                   bool             opponentHasQueen,
                                   const DirtyType& dP,
                                   IndexList&       removed,
                                   IndexList&       added) noexcept {
    // clang-format off
    removed.push_back   (make_index(perspective, kingSq, opponentHasQueen, dP.orgSq, dP.movedPc));
    added.  push_back_if(make_index(perspective, kingSq, opponentHasQueen, dP.dstSq, dP.movedPc)      , is_ok(dP.dstSq));
    removed.push_back_if(make_index(perspective, kingSq, opponentHasQueen, dP.removedSq, dP.removedPc), is_ok(dP.removedSq));
    added.  push_back_if(make_index(perspective, kingSq, opponentHasQueen, dP.addedSq, dP.addedPc)    , is_ok(dP.addedSq));
    // clang-format on
}

void K32Q2::append_changed_indices_both(const Square                wKingSq,
                                        const Square                bKingSq,
                                        bool                        bHasQueen,
                                        bool                        wHasQueen,
                                        const DirtyType&            dP,
                                        Array<IndexList, COLOR_NB>& removed,
                                        Array<IndexList, COLOR_NB>& added) noexcept {
    append_changed_indices(WHITE, wKingSq, bHasQueen, dP, removed[WHITE], added[WHITE]);
    append_changed_indices(BLACK, bKingSq, wHasQueen, dP, removed[BLACK], added[BLACK]);
}

bool K32Q2::refresh_required(const Color perspective, const DirtyType& dP) noexcept {
    if (dP.movedPc == make_piece(perspective, KING))
        return true;

    const Color opponent = ~perspective;
    if ((is_ok(dP.removedSq) && dP.removedPc == make_piece(opponent, QUEEN))
        || (is_ok(dP.addedPc) && dP.addedPc == make_piece(opponent, QUEEN)))
        return true;

    return false;
}

}  // namespace DON::NNUE::Features
