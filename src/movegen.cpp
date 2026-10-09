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

#include "movegen.h"

#include <initializer_list>

#if defined(USE_AVX512ICL)
    #include <immintrin.h>
#endif

#include "attacks.h"
#include "bitboard.h"
#include "position.h"

namespace DON {

namespace {
namespace {
// Splat pawn moves
template<Color AC>
void splat_pawn(const Direction dir, Bitboard dstBB, Move*& moves) noexcept {
    assert((dir == Direction::NORTH || dir == Direction::SOUTH               //
            || dir == Direction::NORTH_2 || dir == Direction::SOUTH_2        //
            || dir == Direction::NORTH_EAST || dir == Direction::SOUTH_EAST  //
            || dir == Direction::NORTH_WEST || dir == Direction::SOUTH_WEST)
           && "Unsupported direction in splat_pawn()");

#if defined(USE_AVX512ICL)
    // clang-format off
    const u8 count = popcount(dstBB);
    assert(count <= 8);  // <= 8 pawns per side

    const __m128i dstSquares = _mm_cvtepi8_epi16(_mm512_castsi512_si128(_mm512_maskz_compress_epi8(static_cast<__mmask64>(dstBB), ALL_SQUARES)));
    const __m128i orgSquares = _mm_sub_epi16(dstSquares, _mm_set1_epi16(+dir));

    __m128i      packedMoves = _mm_or_si128(_mm_slli_epi16(orgSquares, Move::OrgSqShift),
                                            _mm_slli_epi16(dstSquares, Move::DstSqShift));

    if constexpr (AC == BLACK)
    {
        // Reverse the first 1..8 packed 16-bit moves.
        alignas(16) constexpr Array<u8, 9, 16> ReverseShuffleBytes{{
          {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
          {0, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
          {2, 3, 0, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
          {4, 5, 2, 3, 0, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
          {6, 7, 4, 5, 2, 3, 0, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
          {8, 9, 6, 7, 4, 5, 2, 3, 0, 1, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80},
          {10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1, 0x80, 0x80, 0x80, 0x80},
          {12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1, 0x80, 0x80},
          {14, 15, 12, 13, 10, 11, 8, 9, 6, 7, 4, 5, 2, 3, 0, 1}  //
        }};

        const __m128i shuffle = _mm_load_si128(reinterpret_cast<const __m128i*>(ReverseShuffleBytes[count].data()));
        packedMoves = _mm_shuffle_epi8(packedMoves, shuffle);
    }
    // clang-format on
    _mm_storeu_si128(reinterpret_cast<__m128i*>(moves), packedMoves);
    moves += count;

#else
    while (dstBB != 0)
    {
        const Square dstSq = AC == WHITE ? pop_lsq(dstBB) : pop_msq(dstBB);
        const Square orgSq = dstSq - dir;

        *moves++ = Move::normal(orgSq, dstSq);
    }
#endif
}

// Splat promotion moves
template<Color AC, GenType GT, bool Enemy>
void splat_promotion(const Bitboard  knightChecksBB,
                     const Direction dir,
                     Bitboard        dstBB,
                     Move*&          moves) noexcept {
    assert((dir == Direction::NORTH || dir == Direction::SOUTH               //
            || dir == Direction::NORTH_EAST || dir == Direction::SOUTH_EAST  //
            || dir == Direction::NORTH_WEST || dir == Direction::SOUTH_WEST)
           && "Unsupported direction in splat_promotion()");

    constexpr bool Capture = GT == GenType::ENC_CAPTURE || GT == GenType::EVA_CAPTURE;
    constexpr bool Quiet   = GT == GenType::ENC_QUIET || GT == GenType::EVA_QUIET;

    while (dstBB != 0)
    {
        const Square dstSq = AC == WHITE ? pop_lsq(dstBB) : pop_msq(dstBB);
        const Square orgSq = dstSq - dir;

        if constexpr (Capture)
        {
            *moves++ = Move::promotion(orgSq, dstSq, QUEEN);

            if ((knightChecksBB & dstSq) != 0)
                *moves++ = Move::promotion(orgSq, dstSq, KNIGHT);
        }

        if constexpr ((Capture && Enemy) || (Quiet && !Enemy))
        {
            *moves++ = Move::promotion(orgSq, dstSq, ROOK);

            *moves++ = Move::promotion(orgSq, dstSq, BISHOP);

            if ((knightChecksBB & dstSq) == 0)
                *moves++ = Move::promotion(orgSq, dstSq, KNIGHT);
        }
    }
}

// Splat moves
template<Color AC, bool Any>
void splat(const Square orgSq, Bitboard dstBB, Move*& moves) noexcept {

#if defined(USE_AVX512ICL)
    // clang-format off
    const u8 count = popcount(dstBB);
    assert(count <= 32);  // At most 32 destinations are supported (Q can attack up to 27 squares)

    const __m512i orgVec     = _mm512_set1_epi16(Move(orgSq, SQ_ZERO).raw());
    const __m512i dstSquares = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(_mm512_maskz_compress_epi8(dstBB, ALL_SQUARES)));

    __m512i      packedMoves = _mm512_or_si512(orgVec, _mm512_slli_epi16(dstSquares, Move::DstSqShift));

    if constexpr (AC == BLACK)
    {
        alignas(CACHE_LINE_SIZE) constexpr Array<u16, 32> ReverseIndices{
          31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
          15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0
        };

        // Reverse the first 'count' 16-bit moves.
        //
        // ReverseIndices - (32 - count)
        // gives: count-1, count-2, ..., 0 for the first 'count' lanes.
        // Only the first 'count' lanes are stored.
        const __m512i reverseIndices = _mm512_load_si512(ReverseIndices.data());
        const __m512i offset         = _mm512_set1_epi16(32 - count);
        const __m512i indices        = _mm512_sub_epi16(reverseIndices, offset);

        packedMoves = _mm512_permutexvar_epi16(indices, packedMoves);
    }
    // clang-format on
    _mm512_storeu_si512(moves, packedMoves);
    moves += count;

#else
    while (dstBB != 0)
    {
        const Square dstSq = AC == WHITE ? pop_lsq(dstBB) : pop_msq(dstBB);

        *moves++ = Move::normal(orgSq, dstSq);

        if constexpr (Any)
        {
            break;
        }
    }
#endif
}

}  // namespace

template<Color AC, GenType GT>
void generate_pawn(const Position& pos, const Bitboard targetBB, Move*& moves) noexcept {
    static_assert(GT == GenType::ENC_CAPTURE || GT == GenType::ENC_QUIET  //
                    || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET,
                  "Unsupported generate type in generate_pawn()");
    assert(pos.checkers_bb() == 0 || !more_than_one(pos.checkers_bb()));
    // clang-format off
    constexpr bool Capture = GT == GenType::ENC_CAPTURE || GT == GenType::EVA_CAPTURE;
    constexpr bool Quiet   = GT == GenType::ENC_QUIET || GT == GenType::EVA_QUIET;
    constexpr bool Evasion = GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET;

    constexpr Direction Push1Dir = pawn_spush(AC);
    constexpr Direction Push2Dir = pawn_dpush(AC);
    constexpr Direction LCapDir  = AC == WHITE ? Direction::NORTH_WEST : Direction::SOUTH_EAST;
    constexpr Direction RCapDir  = AC == WHITE ? Direction::NORTH_EAST : Direction::SOUTH_WEST;

    constexpr Bitboard Rank3BB = rank_bb(relative_rank(AC, RANK_3));
    constexpr Bitboard Rank5BB = rank_bb(relative_rank(AC, RANK_5));
    constexpr Bitboard Rank7BB = rank_bb(relative_rank(AC, RANK_7));
    // clang-format on

    const Square   kingSq     = pos.square(AC, KING);
    const Bitboard blockersBB = pos.blockers_bb(AC);

    const Bitboard PushableBB    = ~blockersBB | file_bb(kingSq);
    const Bitboard LCapturableBB = ~blockersBB | Attacks::anti_diag_bb(kingSq);
    const Bitboard RCapturableBB = ~blockersBB | Attacks::diag_bb(kingSq);

    const Bitboard pawnsBB      = pos.pieces_bb(AC, PAWN);
    const Bitboard yesR7PawnsBB = pawnsBB & Rank7BB;
    const Bitboard notR7PawnsBB = pawnsBB & ~Rank7BB;

    const Bitboard emptyBB = ~pos.pieces_bb();
    Bitboard       enemyBB = pos.pieces_bb(~AC);

    if constexpr (Evasion)
    {
        enemyBB &= targetBB;
    }

    // Promotions and under-promotions
    if (yesR7PawnsBB != 0)
    {
        const Bitboard knightChecksBB = pos.checks_bb(KNIGHT);

        const Bitboard lCapBB  = shift_bb(LCapDir, yesR7PawnsBB & LCapturableBB) & enemyBB;
        const Bitboard rCapBB  = shift_bb(RCapDir, yesR7PawnsBB & RCapturableBB) & enemyBB;
        Bitboard       push1BB = shift_bb(Push1Dir, yesR7PawnsBB & PushableBB) & emptyBB;

        // Consider only blocking and capture squares
        if constexpr (Evasion)
        {
            push1BB &= Attacks::between_bb(kingSq, lsq(pos.checkers_bb()));
        }

        splat_promotion<AC, GT, true>(knightChecksBB, LCapDir, lCapBB, moves);
        splat_promotion<AC, GT, true>(knightChecksBB, RCapDir, rCapBB, moves);
        splat_promotion<AC, GT, false>(knightChecksBB, Push1Dir, push1BB, moves);
    }

    // Standard and en-passant captures
    if constexpr (Capture)
    {
        const Bitboard lCapBB = shift_bb(LCapDir, notR7PawnsBB & LCapturableBB) & enemyBB;
        const Bitboard rCapBB = shift_bb(RCapDir, notR7PawnsBB & RCapturableBB) & enemyBB;

        splat_pawn<AC>(LCapDir, lCapBB, moves);
        splat_pawn<AC>(RCapDir, rCapBB, moves);

        const Square enPassantSq = pos.en_passant_sq();

        if (is_ok(enPassantSq))
        {
            assert(relative_rank(AC, enPassantSq) == RANK_6);
            assert((pos.pieces_bb(~AC, PAWN) & (enPassantSq - Push1Dir)) != 0);
            assert(pos.rule50_count() == 0);
            assert((notR7PawnsBB & Rank5BB) != 0);

            // An en-passant capture cannot resolve a discovered check
            assert(!Evasion || (targetBB & (enPassantSq + Push1Dir)) == 0);

            const Bitboard epBB = square_bb(enPassantSq);

            Bitboard epPawnsBB = notR7PawnsBB & Rank5BB                       //
                               & ((shift_bb(-LCapDir, epBB) & LCapturableBB)  //
                                  | (shift_bb(-RCapDir, epBB) & RCapturableBB));
            assert(epPawnsBB != 0);

            while (epPawnsBB != 0)
            {
                const Square orgSq = AC == WHITE ? pop_lsq(epPawnsBB) : pop_msq(epPawnsBB);

                *moves++ = Move::enpassant(orgSq, enPassantSq);
            }
        }
    }

    // Single and double pawn pushes, no promotions
    if constexpr (Quiet)
    {
        Bitboard push1BB = shift_bb(Push1Dir, notR7PawnsBB & PushableBB) & emptyBB;
        Bitboard push2BB = shift_bb(Push1Dir, push1BB & Rank3BB) & emptyBB;

        // Consider only blocking squares
        if constexpr (Evasion)
        {
            push1BB &= targetBB;
            push2BB &= targetBB;
        }

        splat_pawn<AC>(Push1Dir, push1BB, moves);
        splat_pawn<AC>(Push2Dir, push2BB, moves);
    }
}

template<PieceType PT, bool Any>
void generate_piece(const Position& pos,
                    const Color     ac,
                    const Bitboard  targetBB,
                    Move*&          moves) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_piece()");
    assert(pos.checkers_bb() == 0 || !more_than_one(pos.checkers_bb()));

    const Square   kingSq      = pos.square(ac, KING);
    const Bitboard piecesBB    = pos.pieces_bb(ac, PT);
    const Bitboard occupancyBB = pos.pieces_bb();
    const Bitboard blockersBB  = pos.blockers_bb(ac);

    Bitboard bb;

    bb = piecesBB & ~blockersBB;
    while (bb != 0)
    {
        const Square   orgSq = ac == WHITE ? pop_lsq(bb) : pop_msq(bb);
        const Bitboard dstBB = Attacks::attacks_bb(PT, orgSq, occupancyBB) & targetBB;

        const Move* const pMoves = moves;

        ac == WHITE ? splat<WHITE, Any>(orgSq, dstBB, moves)
                    : splat<BLACK, Any>(orgSq, dstBB, moves);

        if constexpr (Any)
        {
            if (pMoves != moves)
                return;
        }
    }

    // Pinned knights can't move; only pinned sliders can move
    if constexpr (PT == KNIGHT)
    {
        return;
    }

    bb = piecesBB & blockersBB;
    while (bb != 0)
    {
        const Square   orgSq = ac == WHITE ? pop_lsq(bb) : pop_msq(bb);
        const Bitboard dstBB = Attacks::attacks_bb(PT, orgSq, occupancyBB)  //
                             & Attacks::line_bb(kingSq, orgSq) & targetBB;

        const Move* const pMoves = moves;

        ac == WHITE ? splat<WHITE, Any>(orgSq, dstBB, moves)
                    : splat<BLACK, Any>(orgSq, dstBB, moves);

        if constexpr (Any)
        {
            if (pMoves != moves)
                return;
        }
    }
}

template<GenType GT, bool Any>
void generate_king(const Position& pos,
                   const Color     ac,
                   const Bitboard  targetBB,
                   Move*&          moves) noexcept {
    static_assert(GT == GenType::ENC_CAPTURE || GT == GenType::ENC_QUIET  //
                    || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET,
                  "Unsupported generate type in generate_king()");
    assert(popcount(pos.checkers_bb()) <= 2);

    constexpr bool Castle = GT == GenType::ENC_QUIET;

    const Square kingSq = pos.square(ac, KING);

    if constexpr (Castle)
    {
        assert(pos.checkers_bb() == 0);

        if (pos.has_castling_rights() && pos.has_castling_rights(ac, CastlingSide::ANY))
            for (const CastlingSide cs : {CastlingSide::KING, CastlingSide::QUEEN})
            {
                if (pos.castling_possible(ac, cs))
                {
                    assert(is_ok(pos.castling_rook_sq(ac, cs))
                           && (pos.pieces_bb(ac, ROOK) & pos.castling_rook_sq(ac, cs)) != 0);

                    *moves++ = Move::castling(kingSq, pos.castling_rook_sq(ac, cs));

                    if constexpr (Any)
                    {
                        return;
                    }
                }
            }
    }

    const Bitboard dstBB = Attacks::pseudo_attacks_bb(KING, kingSq)  //
                         & ~pos.acc_attacks_bb(KING) & targetBB;

    ac == WHITE ? splat<WHITE, Any>(kingSq, dstBB, moves)  //
                : splat<BLACK, Any>(kingSq, dstBB, moves);
}

}  // namespace

// <ENC_CAPTURE> Generates all legal captures and promotions moves
// <ENC_QUIET  > Generates all legal non-captures and castling moves
// <EVA_CAPTURE> Generates all legal check evasions captures and promotions moves
// <EVA_QUIET  > Generates all legal check evasions non-captures moves
template<GenType GT, bool Any>
void generate(const Position& pos, Move*& moves) noexcept {
    static_assert(GT == GenType::ENC_CAPTURE || GT == GenType::ENC_QUIET  //
                    || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET,
                  "Unsupported generate type in generate()");
    assert((GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET) == (pos.checkers_bb() != 0));

    // clang-format off
    constexpr bool Evasion = GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET;

    const Color ac = pos.active_color();

    Bitboard targetBB;
    // Skip generating non-king moves when in double check
    if (!Evasion || !more_than_one(pos.checkers_bb()))
    {
        switch (GT)
        {
        case GenType::ENC_CAPTURE : targetBB =  pos.pieces_bb(~ac);                                                  break;
        case GenType::ENC_QUIET   : targetBB = ~pos.pieces_bb();                                                     break;
        case GenType::EVA_CAPTURE : targetBB = pos.checkers_bb();                                                    break;
        case GenType::EVA_QUIET   : targetBB = Attacks::between_ex_bb(pos.square(ac, KING), lsq(pos.checkers_bb())); break;
        }

        const Move* const pMoves = moves;

        ac == WHITE ? generate_pawn<WHITE, GT>(pos, targetBB, moves)
                    : generate_pawn<BLACK, GT>(pos, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return;
        }

        generate_piece<KNIGHT, Any>(pos, ac, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return;
        }

        generate_piece<BISHOP, Any>(pos, ac, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return;
        }

        generate_piece<ROOK  , Any>(pos, ac, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return;
        }

        generate_piece<QUEEN , Any>(pos, ac, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return;
        }
    }

    if constexpr (Evasion)
    {
        switch (GT)
        {
        case GenType::EVA_CAPTURE : targetBB =  pos.pieces_bb(~ac); break;
        case GenType::EVA_QUIET   : targetBB = ~pos.pieces_bb();    break;
        }
    }
    // clang-format on

    generate_king<GT, Any>(pos, ac, targetBB, moves);
}

// Explicit template instantiations:
// clang-format off
template void generate<GenType::ENC_CAPTURE, false>(const Position& pos, Move*& moves) noexcept;
template void generate<GenType::ENC_QUIET  , false>(const Position& pos, Move*& moves) noexcept;
template void generate<GenType::EVA_CAPTURE, false>(const Position& pos, Move*& moves) noexcept;
template void generate<GenType::EVA_QUIET  , false>(const Position& pos, Move*& moves) noexcept;

// <LEGAL> Generates all legal moves
template<>
void generate<GenType::LEGAL, false>(const Position& pos, Move*& moves) noexcept {
    if (pos.checkers_bb() == 0)
    {
        generate<GenType::ENC_CAPTURE, false>(pos, moves);
        generate<GenType::ENC_QUIET  , false>(pos, moves);
    }
    else
    {
        generate<GenType::EVA_CAPTURE, false>(pos, moves);
        generate<GenType::EVA_QUIET  , false>(pos, moves);
    }
}
template<>
void generate<GenType::LEGAL, true >(const Position& pos, Move*& moves) noexcept {
    if (pos.checkers_bb() == 0)
    {
        generate<GenType::ENC_CAPTURE, true>(pos, moves);
        generate<GenType::ENC_QUIET, true>(pos, moves);
    }
    else
    {
        generate<GenType::EVA_CAPTURE, true>(pos, moves);
        generate<GenType::EVA_QUIET  , true>(pos, moves);
    }
}
// clang-format on

}  // namespace DON
