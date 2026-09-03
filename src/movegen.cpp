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

// Splat pawn moves
template<Color AC, Direction D>
Move* splat_pawn_moves(Bitboard dstBB, Move* RESTRICT moves) noexcept {
    static_assert(D == Direction::NORTH || D == Direction::SOUTH                 //
                    || D == Direction::NORTH_2 || D == Direction::SOUTH_2        //
                    || D == Direction::NORTH_EAST || D == Direction::SOUTH_EAST  //
                    || D == Direction::NORTH_WEST || D == Direction::SOUTH_WEST,
                  "D is invalid");

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
    const __m128i orgSquares = _mm_sub_epi16(dstSquares, _mm_set1_epi16(+D));

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
        const Square orgSq = dstSq - D;

        *moves++ = Move{orgSq, dstSq};
    }
#endif

    return moves;
}

// Splat promotion moves
template<Color AC, GenType GT, Direction D, bool Enemy>
Move* splat_promotion_moves(Bitboard       dstBB,
                            const Bitboard knightChecksBB,
                            Move* RESTRICT moves) noexcept {
    static_assert(D == Direction::NORTH || D == Direction::SOUTH                 //
                    || D == Direction::NORTH_EAST || D == Direction::SOUTH_EAST  //
                    || D == Direction::NORTH_WEST || D == Direction::SOUTH_WEST,
                  "D is invalid");

    constexpr bool All     = GT == GenType::ENCOUNTER || GT == GenType::EVASION;
    constexpr bool Capture = GT == GenType::ENC_CAPTURE || GT == GenType::EVA_CAPTURE;
    constexpr bool Quiet   = GT == GenType::ENC_QUIET || GT == GenType::EVA_QUIET;

    while (dstBB != 0)
    {
        const Square dstSq = AC == WHITE ? pop_lsq(dstBB) : pop_msq(dstBB);

        [[maybe_unused]] const Square orgSq = dstSq - D;

        if constexpr (All || Capture)
        {
            *moves++ = Move{orgSq, dstSq, QUEEN};

            if ((knightChecksBB & dstSq) != 0)
                *moves++ = Move{orgSq, dstSq, KNIGHT};
        }

        if constexpr (All || (Capture && Enemy) || (Quiet && !Enemy))
        {
            *moves++ = Move{orgSq, dstSq, ROOK};

            *moves++ = Move{orgSq, dstSq, BISHOP};

            if ((knightChecksBB & dstSq) == 0)
                *moves++ = Move{orgSq, dstSq, KNIGHT};
        }
    }

    return moves;
}

// Splat moves
template<Color AC>
Move* splat_moves(Square orgSq, Bitboard dstBB, Move* RESTRICT moves) noexcept {

#if defined(USE_AVX512ICL)
    // clang-format off
    alignas(CACHE_LINE_SIZE) constexpr Array<u16, 32> ReverseIndices{
      31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
      15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0  //
    };

    const u8 count = popcount(dstBB);
    assert(count <= 32);  // Q can attack up to 27 squares

    const __m512i orgVec     = _mm512_set1_epi16(Move(orgSq, SQUARE_ZERO).raw());
    const __m512i dstSquares = _mm512_cvtepi8_epi16(_mm512_castsi512_si256(_mm512_maskz_compress_epi8(dstBB, ALL_SQUARES)));

    __m512i      packedMoves = _mm512_or_si512(orgVec, _mm512_slli_epi16(dstSquares, Move::DstSqShift));

    if constexpr (AC == BLACK)
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
        const Square dstSq = AC == WHITE ? pop_lsq(dstBB) : pop_msq(dstBB);

        *moves++ = Move{orgSq, dstSq};
    }
#endif

    return moves;
}

template<Color AC, GenType GT>
Move* generate_pawns_moves(const Position& pos,
                           Move* RESTRICT  moves,
                           const Bitboard  targetBB) noexcept {
    assert(pos.checkers_bb() == 0 || !more_than_one(pos.checkers_bb()));

    constexpr bool Evasion =
      GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET;
    constexpr bool Capture = GT == GenType::ENC_CAPTURE || GT == GenType::EVA_CAPTURE;
    constexpr bool Quiet   = GT == GenType::ENC_QUIET || GT == GenType::EVA_QUIET;

    constexpr Direction Push1 = pawn_spush(AC);
    constexpr Direction Push2 = pawn_dpush(AC);
    constexpr Direction LCap  = AC == WHITE ? Direction::NORTH_WEST : Direction::SOUTH_EAST;
    constexpr Direction RCap  = AC == WHITE ? Direction::NORTH_EAST : Direction::SOUTH_WEST;

    const Bitboard pawnsBB      = pos.pieces_bb(AC, PAWN);
    const Bitboard yesR7PawnsBB = pawnsBB & relative_rank(AC, RANK_7);
    const Bitboard notR7PawnsBB = pawnsBB & ~yesR7PawnsBB;

    const Bitboard emptyBB = ~pos.pieces_bb();

    Bitboard enemyBB = pos.pieces_bb(~AC);

    if constexpr (Evasion)
        enemyBB &= targetBB;

    const Move* RESTRICT rMoves = moves;
    Move* RESTRICT       wMoves = moves;

    // Promotions and under-promotions
    if (yesR7PawnsBB != 0)
    {
        const Bitboard knightChecksBB = pos.checks_bb(KNIGHT);

        const Bitboard lCapBB = shift_bb<LCap>(yesR7PawnsBB) & enemyBB;
        moves = splat_promotion_moves<AC, GT, LCap, true>(lCapBB, knightChecksBB, moves);

        const Bitboard rCapBB = shift_bb<RCap>(yesR7PawnsBB) & enemyBB;
        moves = splat_promotion_moves<AC, GT, RCap, true>(rCapBB, knightChecksBB, moves);

        Bitboard push1BB = shift_bb<Push1>(yesR7PawnsBB) & emptyBB;
        // Consider only blocking and capture squares
        if constexpr (Evasion)
            push1BB &= between_bb(pos.square<KING>(AC), lsq(pos.checkers_bb()));
        moves = splat_promotion_moves<AC, GT, Push1, false>(push1BB, knightChecksBB, moves);
    }

    // Single and double pawn pushes, no promotions
    if constexpr (!Capture)
    {
        Bitboard push1BB = shift_bb<Push1>(notR7PawnsBB) & emptyBB;
        Bitboard push2BB = shift_bb<Push1>(push1BB & relative_rank(AC, RANK_3)) & emptyBB;

        // Consider only blocking squares
        if constexpr (Evasion)
        {
            push1BB &= targetBB;
            push2BB &= targetBB;
        }

        moves = splat_pawn_moves<AC, Push1>(push1BB, moves);
        moves = splat_pawn_moves<AC, Push2>(push2BB, moves);
    }

    // Standard and en-passant captures
    if constexpr (!Quiet)
    {
        const Bitboard lCapBB = shift_bb<LCap>(notR7PawnsBB) & enemyBB;
        moves                 = splat_pawn_moves<AC, LCap>(lCapBB, moves);

        const Bitboard rCapBB = shift_bb<RCap>(notR7PawnsBB) & enemyBB;
        moves                 = splat_pawn_moves<AC, RCap>(rCapBB, moves);

        if (pos.en_passant_sq() != SQ_NONE)
        {
            assert(relative_rank(AC, pos.en_passant_sq()) == RANK_6);
            assert((pos.pieces_bb(~AC, PAWN) & (pos.en_passant_sq() - Push1)) != 0);
            assert(pos.rule50_count() == 0);
            assert((notR7PawnsBB & relative_rank(AC, RANK_5)) != 0);

            // An en-passant capture cannot resolve a discovered check
            assert(!Evasion || (targetBB & (pos.en_passant_sq() + Push1)) == 0);

            Bitboard epPawnsBB = notR7PawnsBB & attacks_bb<PAWN>(pos.en_passant_sq(), ~AC);
            assert(epPawnsBB != 0);

            while (epPawnsBB != 0)
            {
                const Square orgSq = AC == WHITE ? pop_lsq(epPawnsBB) : pop_msq(epPawnsBB);

                *moves++ = Move{orgSq, pos.en_passant_sq(), MT::EN_PASSANT};
            }
        }
    }

    const Square   kingSq     = pos.square<KING>(AC);
    const Bitboard blockersBB = pos.blockers_bb(AC);

    // Filter illegal moves (preserve order)
    while (rMoves != moves)
    {
        const Move m = *rMoves++;

        *wMoves = m;

        wMoves += int((blockersBB & m.org_sq()) == 0 || aligned(kingSq, m.org_sq(), m.dst_sq()));
    }

    return wMoves;
}

template<Color AC, PieceType PT>
Move* generate_piece_moves(const Position& pos,
                           Move* RESTRICT  moves,
                           const Bitboard  targetBB) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_piece_moves()");
    assert(pos.checkers_bb() == 0 || !more_than_one(pos.checkers_bb()));

    Bitboard bb = pos.pieces_bb(AC, PT);

    if (bb == 0)
        return moves;

    const Square   kingSq      = pos.square<KING>(AC);
    const Bitboard occupancyBB = pos.pieces_bb();
    const Bitboard blockersBB  = pos.blockers_bb(AC);

    while (bb != 0)
    {
        const Square   orgSq  = AC == WHITE ? pop_lsq(bb) : pop_msq(bb);
        const Bitboard maskBB = (blockersBB & orgSq) == 0 ? FULL_BB : line_bb(kingSq, orgSq);
        const Bitboard dstBB  = attacks_bb<PT>(orgSq, occupancyBB) & maskBB & targetBB;

        moves = splat_moves<AC>(orgSq, dstBB, moves);
    }

    return moves;
}

template<Color AC, GenType GT, bool Any>
Move* generate_king_moves(const Position& pos,
                          Move* RESTRICT  moves,
                          const Bitboard  targetBB) noexcept {
    assert(popcount(pos.checkers_bb()) <= 2);

    constexpr bool Castle = GT == GenType::ENCOUNTER || GT == GenType::ENC_QUIET;

    const Square kingSq = pos.square<KING>(AC);

    Bitboard dstBB = attacks_bb<KING>(kingSq) & ~pos.acc_attacks_bb<KING>() & targetBB;

    while (dstBB != 0)
    {
        const Square dstSq = AC == WHITE ? pop_lsq(dstBB) : pop_msq(dstBB);

        *moves++ = Move{kingSq, dstSq};

        if constexpr (Any)
            return moves;
    }

    if constexpr (Castle)
    {
        assert(pos.checkers_bb() == 0);

        if (pos.has_castling_rights() && pos.has_castling_rights(AC, CastlingSide::ANY))
            for (const CastlingSide cs : {CastlingSide::KING, CastlingSide::QUEEN})
                if (pos.castling_possible(AC, cs))
                {
                    assert(is_ok(pos.castling_rook_sq(AC, cs))
                           && (pos.pieces_bb(AC, ROOK) & pos.castling_rook_sq(AC, cs)) != 0);

                    *moves++ = Move{kingSq, pos.castling_rook_sq(AC, cs), MT::CASTLING};

                    if constexpr (Any)
                        return moves;
                }
    }

    return moves;
}

template<Color AC, GenType GT, bool Any>
Move* generate_moves(const Position& pos, Move* RESTRICT moves) noexcept {
    static_assert(
      GT == GenType::ENCOUNTER || GT == GenType::ENC_CAPTURE || GT == GenType::ENC_QUIET  //
        || GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET,
      "Unsupported generate type in generate_moves()");

    constexpr bool Evasion =
      GT == GenType::EVASION || GT == GenType::EVA_CAPTURE || GT == GenType::EVA_QUIET;

    // clang-format off
    Bitboard targetBB;
    // Skip generating non-king moves when in double check
    if (!Evasion || !more_than_one(pos.checkers_bb()))
    {
        switch (GT)
        {
        case GenType::ENCOUNTER   : targetBB = ~pos.pieces_bb(AC);                                          break;
        case GenType::ENC_CAPTURE : targetBB =  pos.pieces_bb(~AC);                                         break;
        case GenType::ENC_QUIET   : targetBB = ~pos.pieces_bb();                                            break;
        case GenType::EVASION     : targetBB = between_bb(pos.square<KING>(AC), lsq(pos.checkers_bb()));    break;
        case GenType::EVA_CAPTURE : targetBB = pos.checkers_bb();                                           break;
        case GenType::EVA_QUIET   : targetBB = between_ex_bb(pos.square<KING>(AC), lsq(pos.checkers_bb())); break;
        }

        const Move* RESTRICT pMoves = moves;
        moves = generate_pawns_moves<AC, GT    >(pos, moves, targetBB);
        if (Any && pMoves != moves) return moves;
        moves = generate_piece_moves<AC, KNIGHT>(pos, moves, targetBB);
        if (Any && pMoves != moves) return moves;
        moves = generate_piece_moves<AC, BISHOP>(pos, moves, targetBB);
        if (Any && pMoves != moves) return moves;
        moves = generate_piece_moves<AC, ROOK  >(pos, moves, targetBB);
        if (Any && pMoves != moves) return moves;
        moves = generate_piece_moves<AC, QUEEN >(pos, moves, targetBB);
        if (Any && pMoves != moves) return moves;
    }

    if constexpr (Evasion)
    {
        switch (GT)
        {
        case GenType::EVASION     : targetBB = ~pos.pieces_bb(AC);  break;
        case GenType::EVA_CAPTURE : targetBB =  pos.pieces_bb(~AC); break;
        case GenType::EVA_QUIET   : targetBB = ~pos.pieces_bb();    break;
        }
    }
    // clang-format on

    moves = generate_king_moves<AC, GT, Any>(pos, moves, targetBB);

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

    return pos.active_color() == WHITE ? generate_moves<WHITE, GT, Any>(pos, moves)
                                       : generate_moves<BLACK, GT, Any>(pos, moves);
}

// Explicit template instantiations:
template Move* generate<GenType::ENCOUNTER, false>(const Position&, Move* RESTRICT) noexcept;
template Move* generate<GenType::ENCOUNTER, true>(const Position&, Move* RESTRICT) noexcept;
template Move* generate<GenType::ENC_CAPTURE, false>(const Position&, Move* RESTRICT) noexcept;
template Move* generate<GenType::ENC_QUIET, false>(const Position&, Move* RESTRICT) noexcept;

template Move* generate<GenType::EVASION, false>(const Position&, Move* RESTRICT) noexcept;
template Move* generate<GenType::EVASION, true>(const Position&, Move* RESTRICT) noexcept;
template Move* generate<GenType::EVA_CAPTURE, false>(const Position&, Move* RESTRICT) noexcept;
template Move* generate<GenType::EVA_QUIET, false>(const Position&, Move* RESTRICT) noexcept;

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
