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
Move* splat_pawn(const Direction d, Bitboard dstBB, Move* RESTRICT moves) noexcept {
    assert((d == Direction::NORTH || d == Direction::SOUTH               //
            || d == Direction::NORTH_2 || d == Direction::SOUTH_2        //
            || d == Direction::NORTH_EAST || d == Direction::SOUTH_EAST  //
            || d == Direction::NORTH_WEST || d == Direction::SOUTH_WEST)
           && "Unsupported direction in splat_pawn()");

#if defined(USE_AVX512ICL)
    // clang-format off
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

    const u8 count = popcount(dstBB);
    assert(count <= 8);  // <= 8 pawns per side

    const __m128i dstSquares = _mm_cvtepi8_epi16(_mm512_castsi512_si128(_mm512_maskz_compress_epi8(static_cast<__mmask64>(dstBB), ALL_SQUARES)));
    const __m128i orgSquares = _mm_sub_epi16(dstSquares, _mm_set1_epi16(+d));

    __m128i      packedMoves = _mm_or_si128(_mm_slli_epi16(orgSquares, Move::OrgSqShift),
                                            _mm_slli_epi16(dstSquares, Move::DstSqShift));

    if constexpr (AC == BLACK)
    {
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
        const Square orgSq = dstSq - d;

        *moves++ = Move::normal(orgSq, dstSq);
    }
#endif

    return moves;
}

// Splat promotion moves
template<GenType GT, bool Enemy>
Move* splat_promotion(const Color     ac,
                      const Bitboard  knightChecksBB,
                      const Direction d,
                      Bitboard        dstBB,
                      Move* RESTRICT  moves) noexcept {
    assert((d == Direction::NORTH || d == Direction::SOUTH               //
            || d == Direction::NORTH_EAST || d == Direction::SOUTH_EAST  //
            || d == Direction::NORTH_WEST || d == Direction::SOUTH_WEST)
           && "Unsupported direction in splat_promotion()");

    constexpr bool All     = GT == GenType::ENCOUNTER || GT == GenType::EVASION;
    constexpr bool Capture = GT == GenType::ENC_CAPTURE || GT == GenType::EVA_CAPTURE;
    constexpr bool Quiet   = GT == GenType::ENC_QUIET || GT == GenType::EVA_QUIET;

    while (dstBB != 0)
    {
        const Square dstSq = ac == WHITE ? pop_lsq(dstBB) : pop_msq(dstBB);
        const Square orgSq = dstSq - d;

        if constexpr (All || Capture)
        {
            *moves++ = Move::promotion(orgSq, dstSq, QUEEN);

            if ((knightChecksBB & dstSq) != 0)
                *moves++ = Move::promotion(orgSq, dstSq, KNIGHT);
        }

        if constexpr (All || (Capture && Enemy) || (Quiet && !Enemy))
        {
            *moves++ = Move::promotion(orgSq, dstSq, ROOK);

            *moves++ = Move::promotion(orgSq, dstSq, BISHOP);

            if ((knightChecksBB & dstSq) == 0)
                *moves++ = Move::promotion(orgSq, dstSq, KNIGHT);
        }
    }

    return moves;
}

// Splat moves
template<bool Any>
Move* splat(const Color ac, const Square orgSq, Bitboard dstBB, Move* RESTRICT moves) noexcept {

#if defined(USE_AVX512ICL)
    // clang-format off
    alignas(CACHE_LINE_SIZE) constexpr Array<u16, 32> ReverseIndices{
      31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
      15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0
    };

    const u8 count = popcount(dstBB);
    assert(count <= 32);  // Q can attack up to 27 squares

    const __m512i orgVec     = _mm512_set1_epi16(Move(orgSq, SQ_ZERO).raw());
    const __m512i dstSquares = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(_mm512_maskz_compress_epi8(dstBB, ALL_SQUARES)));

    __m512i      packedMoves = _mm512_or_si512(orgVec, _mm512_slli_epi16(dstSquares, Move::DstSqShift));

    if (ac == BLACK)
    {
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
        const Square dstSq = ac == WHITE ? pop_lsq(dstBB) : pop_msq(dstBB);

        *moves++ = Move::normal(orgSq, dstSq);

        if constexpr (Any)
        {
            break;
        }
    }
#endif

    return moves;
}

}  // namespace

template<Color AC, GenType GT>
Move* generate_pawn(const Position& pos, const Bitboard targetBB, Move* RESTRICT moves) noexcept {
    static_assert(
      GT == GenType::ENCOUNTER || GT == GenType::ENC_CAPTURE || GT == GenType::ENC_QUIET  //
        || GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET,
      "Unsupported generate type in generate()");
    assert(pos.checkers_bb() == 0 || !more_than_one(pos.checkers_bb()));
    // clang-format off
    constexpr bool Evasion = GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET;
    constexpr bool Capture = GT == GenType::ENC_CAPTURE || GT == GenType::EVA_CAPTURE;
    constexpr bool Quiet   = GT == GenType::ENC_QUIET || GT == GenType::EVA_QUIET;

    constexpr Direction Push1 = pawn_spush(AC);
    constexpr Direction Push2 = pawn_dpush(AC);
    constexpr Direction LCap  = AC == WHITE ? Direction::NORTH_WEST : Direction::SOUTH_EAST;
    constexpr Direction RCap  = AC == WHITE ? Direction::NORTH_EAST : Direction::SOUTH_WEST;

    constexpr Bitboard Rank3BB = rank_bb(relative_rank(AC, RANK_3));
    constexpr Bitboard Rank7BB = rank_bb(relative_rank(AC, RANK_7));
    // clang-format on

    const Square   kingSq     = pos.square(AC, KING);
    const Bitboard blockersBB = pos.blockers_bb(AC);

    const Bitboard PushableBB = ~blockersBB | file_bb(kingSq);
    const Bitboard LCaptureBB = ~blockersBB | Attacks::anti_diag_bb(kingSq);
    const Bitboard RCaptureBB = ~blockersBB | Attacks::diag_bb(kingSq);

    const Bitboard pawnsBB      = pos.pieces_bb(AC, PAWN);
    const Bitboard yesR7PawnsBB = pawnsBB & Rank7BB;
    const Bitboard notR7PawnsBB = pawnsBB & ~Rank7BB;

    const Bitboard emptyBB = ~pos.pieces_bb();

    Bitboard enemyBB = pos.pieces_bb(~AC);

    if constexpr (Evasion)
    {
        enemyBB &= targetBB;
    }

    // Promotions and under-promotions
    if (yesR7PawnsBB != 0)
    {
        const Bitboard knightChecksBB = pos.checks_bb(KNIGHT);

        const Bitboard lCapBB = shift_bb(LCap, yesR7PawnsBB & LCaptureBB) & enemyBB;

        moves = splat_promotion<GT, true>(AC, knightChecksBB, LCap, lCapBB, moves);

        const Bitboard rCapBB = shift_bb(RCap, yesR7PawnsBB & RCaptureBB) & enemyBB;

        moves = splat_promotion<GT, true>(AC, knightChecksBB, RCap, rCapBB, moves);

        Bitboard push1BB = shift_bb(Push1, yesR7PawnsBB & PushableBB) & emptyBB;
        // Consider only blocking and capture squares
        if constexpr (Evasion)
        {
            push1BB &= Attacks::between_bb(kingSq, lsq(pos.checkers_bb()));
        }

        moves = splat_promotion<GT, false>(AC, knightChecksBB, Push1, push1BB, moves);
    }

    // Single and double pawn pushes, no promotions
    if constexpr (!Capture)
    {
        Bitboard push1BB = shift_bb(Push1, notR7PawnsBB & PushableBB) & emptyBB;
        Bitboard push2BB = shift_bb(Push1, push1BB & Rank3BB) & emptyBB;

        // Consider only blocking squares
        if constexpr (Evasion)
        {
            push1BB &= targetBB;
            push2BB &= targetBB;
        }

        moves = splat_pawn<AC>(Push1, push1BB, moves);
        moves = splat_pawn<AC>(Push2, push2BB, moves);
    }

    // Standard and en-passant captures
    if constexpr (!Quiet)
    {
        const Bitboard lCapBB = shift_bb(LCap, notR7PawnsBB & LCaptureBB) & enemyBB;

        moves = splat_pawn<AC>(LCap, lCapBB, moves);

        const Bitboard rCapBB = shift_bb(RCap, notR7PawnsBB & RCaptureBB) & enemyBB;

        moves = splat_pawn<AC>(RCap, rCapBB, moves);

        const Square enPassantSq = pos.en_passant_sq();

        if (is_ok(enPassantSq))
        {
            assert(relative_rank(AC, enPassantSq) == RANK_6);
            assert((pos.pieces_bb(~AC, PAWN) & (enPassantSq - Push1)) != 0);
            assert(pos.rule50_count() == 0);
            assert((notR7PawnsBB & relative_rank(AC, RANK_5)) != 0);

            // An en-passant capture cannot resolve a discovered check
            assert(!Evasion || (targetBB & (enPassantSq + Push1)) == 0);

            const Bitboard epBB = square_bb(enPassantSq);

            Bitboard epPawnsBB = notR7PawnsBB
                               & ((shift_bb(-RCap, epBB) & LCaptureBB)  //
                                  | (shift_bb(-LCap, epBB) & RCaptureBB));
            assert(epPawnsBB != 0);

            while (epPawnsBB != 0)
            {
                const Square orgSq = AC == WHITE ? pop_lsq(epPawnsBB) : pop_msq(epPawnsBB);

                *moves++ = Move::enpassant(orgSq, enPassantSq);
            }
        }
    }

    return moves;
}

template<PieceType PT, bool Any>
Move* generate_piece(const Position& pos,
                     const Color     ac,
                     const Bitboard  targetBB,
                     Move* RESTRICT  moves) noexcept {
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

        const Move* const RESTRICT pMoves = moves;

        moves = splat<Any>(ac, orgSq, dstBB, moves);

        if constexpr (Any)
        {
            if (pMoves != moves)
                break;
        }
    }

    // Pinned knights can't move; only pinned sliders can move
    if constexpr (PT == KNIGHT)
    {
        return moves;
    }

    bb = piecesBB & blockersBB;
    while (bb != 0)
    {
        const Square   orgSq = ac == WHITE ? pop_lsq(bb) : pop_msq(bb);
        const Bitboard dstBB = Attacks::attacks_bb(PT, orgSq, occupancyBB)  //
                             & Attacks::line_bb(kingSq, orgSq) & targetBB;

        const Move* const RESTRICT pMoves = moves;

        moves = splat<Any>(ac, orgSq, dstBB, moves);

        if constexpr (Any)
        {
            if (pMoves != moves)
                break;
        }
    }

    return moves;
}

template<GenType GT, bool Any>
Move* generate_king(const Position& pos,
                    const Color     ac,
                    const Bitboard  targetBB,
                    Move* RESTRICT  moves) noexcept {
    static_assert(
      GT == GenType::ENCOUNTER || GT == GenType::ENC_CAPTURE || GT == GenType::ENC_QUIET  //
        || GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET,
      "Unsupported generate type in generate_king()");
    assert(popcount(pos.checkers_bb()) <= 2);

    constexpr bool Castle = GT == GenType::ENCOUNTER || GT == GenType::ENC_QUIET;

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
                        return moves;
                    }
                }
            }
    }

    const Bitboard dstBB = Attacks::pseudo_attacks_bb(KING, kingSq)  //
                         & ~pos.acc_attacks_bb(KING) & targetBB;

    moves = splat<Any>(ac, kingSq, dstBB, moves);

    return moves;
}

}  // namespace

// <ENCOUNTER  > Generates all legal captures and non-captures moves
// <ENC_CAPTURE> Generates all legal captures and promotions moves
// <ENC_QUIET  > Generates all legal non-captures and castling moves
// <EVASION    > Generates all legal check evasions moves
// <EVA_CAPTURE> Generates all legal check evasions captures and promotions moves
// <EVA_QUIET  > Generates all legal check evasions non-captures moves
template<GenType GT, bool Any>
Move* generate(const Position& pos, Move* RESTRICT moves) noexcept {
    static_assert(
      GT == GenType::ENCOUNTER || GT == GenType::ENC_CAPTURE || GT == GenType::ENC_QUIET  //
        || GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET,
      "Unsupported generate type in generate()");
    assert((GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET)
           == (pos.checkers_bb() != 0));

    constexpr bool Evasion =
      GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET;

    const Color ac = pos.active_color();

    // clang-format off
    Bitboard targetBB;
    // Skip generating non-king moves when in double check
    if (!Evasion || !more_than_one(pos.checkers_bb()))
    {
        switch (GT)
        {
        case GenType::ENCOUNTER   : targetBB = ~pos.pieces_bb(ac);                                                   break;
        case GenType::ENC_CAPTURE : targetBB =  pos.pieces_bb(~ac);                                                  break;
        case GenType::ENC_QUIET   : targetBB = ~pos.pieces_bb();                                                     break;
        case GenType::EVASION     : targetBB = Attacks::between_bb(pos.square(ac, KING), lsq(pos.checkers_bb()));    break;
        case GenType::EVA_CAPTURE : targetBB = pos.checkers_bb();                                                    break;
        case GenType::EVA_QUIET   : targetBB = Attacks::between_ex_bb(pos.square(ac, KING), lsq(pos.checkers_bb())); break;
        }

        const Move* const RESTRICT pMoves = moves;

        moves = ac == WHITE ? generate_pawn<WHITE, GT>(pos, targetBB, moves)
                            : generate_pawn<BLACK, GT>(pos, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return moves;
        }
        moves = generate_piece<KNIGHT, Any>(pos, ac, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return moves;
        }
        moves = generate_piece<BISHOP, Any>(pos, ac, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return moves;
        }
        moves = generate_piece<ROOK  , Any>(pos, ac, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return moves;
        }
        moves = generate_piece<QUEEN , Any>(pos, ac, targetBB, moves);
        if constexpr (Any)
        {
            if (pMoves != moves) return moves;
        }
    }

    if constexpr (Evasion)
    {
        switch (GT)
        {
        case GenType::EVASION     : targetBB = ~pos.pieces_bb(ac);  break;
        case GenType::EVA_CAPTURE : targetBB =  pos.pieces_bb(~ac); break;
        case GenType::EVA_QUIET   : targetBB = ~pos.pieces_bb();    break;
        }
    }
    // clang-format on

    moves = generate_king<GT, Any>(pos, ac, targetBB, moves);

    return moves;
}

// Explicit template instantiations:
// clang-format off
template Move* generate<GenType::ENCOUNTER, false>(const Position& pos, Move* RESTRICT moves) noexcept;
template Move* generate<GenType::ENCOUNTER, true >(const Position& pos, Move* RESTRICT moves) noexcept;
template Move* generate<GenType::ENC_CAPTURE, false>(const Position& pos, Move* RESTRICT moves) noexcept;
template Move* generate<GenType::ENC_QUIET  , false>(const Position& pos, Move* RESTRICT moves) noexcept;

template Move* generate<GenType::EVASION, false>(const Position& pos, Move* RESTRICT moves) noexcept;
template Move* generate<GenType::EVASION, true >(const Position& pos, Move* RESTRICT moves) noexcept;
template Move* generate<GenType::EVA_CAPTURE, false>(const Position& pos, Move* RESTRICT moves) noexcept;
template Move* generate<GenType::EVA_QUIET  , false>(const Position& pos, Move* RESTRICT moves) noexcept;
// clang-format on

// <LEGAL> Generates all legal moves
template<>
Move* generate<GenType::LEGAL, false>(const Position& pos, Move* RESTRICT moves) noexcept {
    return pos.checkers_bb() != 0 ? generate<GenType::EVASION, false>(pos, moves)
                                  : generate<GenType::ENCOUNTER, false>(pos, moves);
}
template<>
Move* generate<GenType::LEGAL, true>(const Position& pos, Move* RESTRICT moves) noexcept {
    return pos.checkers_bb() != 0 ? generate<GenType::EVASION, true>(pos, moves)
                                  : generate<GenType::ENCOUNTER, true>(pos, moves);
}

}  // namespace DON
