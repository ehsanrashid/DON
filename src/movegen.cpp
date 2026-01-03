/*
  DON, a UCI chess playing engine derived from Stockfish

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

#include <algorithm>
#include <cstring>
#include <functional>
#include <initializer_list>

#if defined(USE_AVX512ICL)
    #include <immintrin.h>
#endif

#include "bitboard.h"
#include "position.h"

namespace DON {

namespace {

#if defined(USE_AVX512ICL)
Move* write_moves(std::uint32_t mask, __m512i vector, Move* moves) noexcept {
    // Avoid _mm512_mask_compressstoreu_epi16() as it's 256 uOps on Zen4
    _mm512_storeu_si512(reinterpret_cast<__m512i*>(moves),
                        _mm512_maskz_compress_epi16(mask, vector));
    return moves + popcount(mask);
}
#endif

// Splat pawn moves
template<Color AC, Direction D>
Move* splat_pawn_moves(Bitboard dstBB, Move* moves) noexcept {
    static_assert(D == NORTH || D == SOUTH                 //
                    || D == NORTH_2 || D == SOUTH_2        //
                    || D == NORTH_EAST || D == SOUTH_EAST  //
                    || D == NORTH_WEST || D == SOUTH_WEST,
                  "D is invalid");

#if defined(USE_AVX512ICL)
    (void) AC;
    alignas(CACHE_LINE_SIZE) constexpr auto SplatTable = []() constexpr noexcept {
        StdArray<Move, SQUARE_NB> table{};
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            Square sq = std::clamp(s - D, SQ_A1, SQ_H8);
            table[s]  = Move{sq, s};
        }
        return table;
    }();

    const auto* table = reinterpret_cast<const __m512i*>(SplatTable.data());

    moves = write_moves(std::uint32_t(dstBB >> 00), _mm512_load_si512(table + 0), moves);
    moves = write_moves(std::uint32_t(dstBB >> 32), _mm512_load_si512(table + 1), moves);
#else
    while (dstBB != 0)
    {
        Square dstSq;
        if constexpr (AC == WHITE)
            dstSq = pop_msq(dstBB);
        else
            dstSq = pop_lsq(dstBB);

        *moves++ = Move{dstSq - D, dstSq};
    }
#endif

    return moves;
}

// Splat promotion moves
template<Color AC, GenType GT, Direction D, bool Enemy>
Move* splat_promotion_moves(Bitboard dstBB, Bitboard knightChecksBB, Move* moves) noexcept {
    static_assert(D == NORTH || D == SOUTH                 //
                    || D == NORTH_EAST || D == SOUTH_EAST  //
                    || D == NORTH_WEST || D == SOUTH_WEST,
                  "D is invalid");

    constexpr bool All     = GT == ENCOUNTER || GT == EVASION;
    constexpr bool Capture = GT == ENC_CAPTURE || GT == EVA_CAPTURE;
    constexpr bool Quiet   = GT == ENC_QUIET || GT == EVA_QUIET;

    while (dstBB != 0)
    {
        Square dstSq;
        if constexpr (AC == WHITE)
            dstSq = pop_msq(dstBB);
        else
            dstSq = pop_lsq(dstBB);

        if constexpr (All || Capture)
        {
            *moves++ = Move::make<MT::PROMOTION>(dstSq - D, dstSq, QUEEN);

            if ((knightChecksBB & dstSq) != 0)
                *moves++ = Move::make<MT::PROMOTION>(dstSq - D, dstSq, KNIGHT);
        }

        if constexpr (All || (Capture && Enemy) || (Quiet && !Enemy))
        {
            *moves++ = Move::make<MT::PROMOTION>(dstSq - D, dstSq, ROOK);

            *moves++ = Move::make<MT::PROMOTION>(dstSq - D, dstSq, BISHOP);

            if ((knightChecksBB & dstSq) == 0)
                *moves++ = Move::make<MT::PROMOTION>(dstSq - D, dstSq, KNIGHT);
        }
    }

    return moves;
}

// Splat moves
template<Color AC>
Move* splat_moves(Square orgSq, Bitboard dstBB, Move* moves) noexcept {

#if defined(USE_AVX512ICL)
    (void) AC;
    alignas(CACHE_LINE_SIZE) constexpr auto SplatTable = []() constexpr noexcept {
        StdArray<Move, SQUARE_NB> table{};
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
            table[s] = Move{SQUARE_ZERO, s};
        return table;
    }();

    __m512i orgVec = _mm512_set1_epi16(Move{orgSq, SQUARE_ZERO}.raw());

    const auto* table = reinterpret_cast<const __m512i*>(SplatTable.data());

    moves = write_moves(std::uint32_t(dstBB >> 00),
                        _mm512_or_si512(_mm512_load_si512(table + 0), orgVec), moves);
    moves = write_moves(std::uint32_t(dstBB >> 32),
                        _mm512_or_si512(_mm512_load_si512(table + 1), orgVec), moves);
#else
    while (dstBB != 0)
    {
        Square dstSq;
        if constexpr (AC == WHITE)
            dstSq = pop_msq(dstBB);
        else
            dstSq = pop_lsq(dstBB);

        *moves++ = Move{orgSq, dstSq};
    }
#endif

    return moves;
}

template<Color AC, GenType GT>
Move* generate_pawns_moves(const Position& pos, Move* moves, Bitboard targetBB) noexcept {
    assert(pos.checkers_bb() == 0 || !more_than_one(pos.checkers_bb()));

    constexpr bool Evasion = GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET;
    constexpr bool Capture = GT == ENC_CAPTURE || GT == EVA_CAPTURE;
    constexpr bool Quiet   = GT == ENC_QUIET || GT == EVA_QUIET;

    constexpr Direction Push1 = pawn_spush(AC);
    constexpr Direction Push2 = pawn_dpush(AC);
    constexpr Direction LCap  = AC == WHITE ? NORTH_WEST : SOUTH_EAST;
    constexpr Direction RCap  = AC == WHITE ? NORTH_EAST : SOUTH_WEST;

    Bitboard acPawnsBB    = pos.pieces_bb(AC, PAWN);
    Bitboard yesR7PawnsBB = acPawnsBB & relative_rank(AC, RANK_7);
    Bitboard notR7PawnsBB = acPawnsBB & ~yesR7PawnsBB;

    Bitboard emptyBB = ~pos.pieces_bb();
    Bitboard enemyBB = pos.pieces_bb(~AC);

    if constexpr (Evasion)
        enemyBB &= targetBB;

    Move *rMoves = moves, *wMoves = moves;

    // Promotions and under-promotions
    if (yesR7PawnsBB != 0)
    {
        Bitboard knightChecksBB = pos.checks_bb(KNIGHT);

        Bitboard dstBB;

        dstBB = shift_bb<LCap>(yesR7PawnsBB) & enemyBB;
        moves = splat_promotion_moves<AC, GT, LCap, true>(dstBB, knightChecksBB, moves);

        dstBB = shift_bb<RCap>(yesR7PawnsBB) & enemyBB;
        moves = splat_promotion_moves<AC, GT, RCap, true>(dstBB, knightChecksBB, moves);

        dstBB = shift_bb<Push1>(yesR7PawnsBB) & emptyBB;
        // Consider only blocking and capture squares
        if constexpr (Evasion)
            dstBB &= between_bb(pos.square<KING>(AC), lsq(pos.checkers_bb()));
        moves = splat_promotion_moves<AC, GT, Push1, false>(dstBB, knightChecksBB, moves);
    }

    // Single and double pawn pushes, no promotions
    if constexpr (!Capture)
    {
        Bitboard dstBB1 = shift_bb<Push1>(notR7PawnsBB) & emptyBB;
        Bitboard dstBB2 = shift_bb<Push1>(dstBB1 & relative_rank(AC, RANK_3)) & emptyBB;

        // Consider only blocking squares
        if constexpr (Evasion)
        {
            dstBB1 &= targetBB;
            dstBB2 &= targetBB;
        }

        moves = splat_pawn_moves<AC, Push1>(dstBB1, moves);
        moves = splat_pawn_moves<AC, Push2>(dstBB2, moves);
    }

    // Standard and en-passant captures
    if constexpr (!Quiet)
    {
        Bitboard dstBB;

        dstBB = shift_bb<LCap>(notR7PawnsBB) & enemyBB;
        moves = splat_pawn_moves<AC, LCap>(dstBB, moves);

        dstBB = shift_bb<RCap>(notR7PawnsBB) & enemyBB;
        moves = splat_pawn_moves<AC, RCap>(dstBB, moves);

        Square enPassantSq = pos.en_passant_sq();
        if (is_ok(enPassantSq))
        {
            assert(relative_rank(AC, enPassantSq) == RANK_6);
            assert(pos.pieces_bb(~AC, PAWN) & (enPassantSq - Push1));
            assert(pos.rule50_count() == 0);
            assert(notR7PawnsBB & relative_rank(AC, RANK_5));

            // An en-passant capture cannot resolve a discovered check
            assert(!(Evasion && (targetBB & (enPassantSq + Push1)) != 0));

            Bitboard orgBB = notR7PawnsBB & attacks_bb<PAWN>(enPassantSq, ~AC);
            assert(orgBB != 0);

            while (orgBB != 0)
            {
                Square orgSq;
                if constexpr (AC == WHITE)
                    orgSq = pop_lsq(orgBB);
                else
                    orgSq = pop_msq(orgBB);

                *moves++ = Move::make<MT::EN_PASSANT>(orgSq, enPassantSq);
            }
        }
    }

    Square   kingSq     = pos.square<KING>(AC);
    Bitboard blockersBB = pos.blockers_bb(AC);

    // Filter illegal moves (preserve order)
    while (rMoves != moves)
    {
        Move m = *rMoves++;

        Square orgSq = m.org_sq();

        if ((blockersBB & orgSq) == 0 || aligned(kingSq, orgSq, m.dst_sq()))
            *wMoves++ = m;
    }

    return wMoves;
}

template<Color AC, PieceType PT>
Move* generate_piece_moves(const Position& pos, Move* moves, Bitboard targetBB) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_piece_moves()");
    assert(pos.checkers_bb() == 0 || !more_than_one(pos.checkers_bb()));

    const auto cnt = pos.count(AC, PT);

    if (cnt == 0)
        return moves;

    StdArray<Square, Position::CAPACITIES[PT - 1]> sqs;
    //std::memset(sqs.data(), SQ_NONE, sizeof(sqs));

    std::memcpy(sqs.data(), pos.squares(AC, PT).data(pos.base(AC)), cnt * sizeof(Square));

    Square*       begSq = sqs.data();
    Square* const endSq = begSq + cnt;

    if (cnt > 1)
    {
        if constexpr (AC == WHITE)
            std::sort(begSq, endSq, std::greater<>{});
        else
            std::sort(begSq, endSq, std::less<>{});
    }

    Square   kingSq      = pos.square<KING>(AC);
    Bitboard occupancyBB = pos.pieces_bb();
    Bitboard blockersBB  = pos.blockers_bb(AC);

    for (; begSq != endSq; ++begSq)
    {
        Square orgSq = *begSq;

        Bitboard dstBB = attacks_bb<PT>(orgSq, occupancyBB)
                       & ((blockersBB & orgSq) == 0 ? FULL_BB : line_bb(kingSq, orgSq))  //
                       & targetBB;

        moves = splat_moves<AC>(orgSq, dstBB, moves);
    }

    return moves;
}

template<Color AC, GenType GT, bool Any>
Move* generate_king_moves(const Position& pos, Move* moves, Bitboard targetBB) noexcept {
    //assert(popcount(pos.checkers_bb()) <= 2);

    constexpr bool Castle = GT == ENCOUNTER || GT == ENC_QUIET;

    Square kingSq = pos.square<KING>(AC);

    Bitboard dstBB = attacks_bb<KING>(kingSq) & ~pos.acc_attacks_bb<KING>() & targetBB;

    while (dstBB != 0)
    {
        Square dstSq;
        if constexpr (AC == WHITE)
            dstSq = pop_msq(dstBB);
        else
            dstSq = pop_lsq(dstBB);

        *moves++ = Move{kingSq, dstSq};

        if constexpr (Any)
            return moves;
    }

    if constexpr (Castle)
    {
        assert(pos.checkers_bb() == 0);

        if (pos.has_castling_rights() && pos.has_castling_rights(AC, CastlingSide::ANY))
            for (CastlingSide cs : {CastlingSide::KING, CastlingSide::QUEEN})
                if (pos.castling_possible(AC, cs))
                {
                    assert(is_ok(pos.castling_rook_sq(AC, cs))
                           && (pos.pieces_bb(AC, ROOK) & pos.castling_rook_sq(AC, cs)) != 0);

                    *moves++ = Move::make<MT::CASTLING>(kingSq, pos.castling_rook_sq(AC, cs));

                    if constexpr (Any)
                        return moves;
                }
    }

    return moves;
}

template<Color AC, GenType GT, bool Any>
Move* generate_moves(const Position& pos, Move* moves) noexcept {
    static_assert(GT == ENCOUNTER || GT == ENC_CAPTURE || GT == ENC_QUIET  //
                    || GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET,
                  "Unsupported generate type in generate_moves()");

    constexpr bool Evasion = GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET;

    // clang-format off
    Bitboard targetBB;
    // Skip generating non-king moves when in double check
    if (!Evasion || !more_than_one(pos.checkers_bb()))
    {
        switch (GT)
        {
        case ENCOUNTER :   targetBB = ~pos.pieces_bb(AC);                                          break;
        case ENC_CAPTURE : targetBB =  pos.pieces_bb(~AC);                                         break;
        case ENC_QUIET :   targetBB = ~pos.pieces_bb();                                            break;
        case EVASION :     targetBB = between_bb(pos.square<KING>(AC), lsq(pos.checkers_bb()));    break;
        case EVA_CAPTURE : targetBB = pos.checkers_bb();                                           break;
        case EVA_QUIET :   targetBB = between_ex_bb(pos.square<KING>(AC), lsq(pos.checkers_bb())); break;
        }

        const auto* pMoves = moves;
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
        case EVASION :     targetBB = ~pos.pieces_bb(AC);  break;
        case EVA_CAPTURE : targetBB =  pos.pieces_bb(~AC); break;
        case EVA_QUIET :   targetBB = ~pos.pieces_bb();    break;
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
Move* generate(const Position& pos, Move* moves) noexcept {
    static_assert(GT == ENCOUNTER || GT == ENC_CAPTURE || GT == ENC_QUIET  //
                    || GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET,
                  "Unsupported generate type in generate()");

    assert((GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET) == (pos.checkers_bb() != 0));

    return pos.active_color() == WHITE ? generate_moves<WHITE, GT, Any>(pos, moves)
                                       : generate_moves<BLACK, GT, Any>(pos, moves);
}

// Explicit template instantiations:
template Move* generate<ENCOUNTER, false>(const Position& pos, Move* moves) noexcept;
template Move* generate<ENCOUNTER, true>(const Position& pos, Move* moves) noexcept;
template Move* generate<ENC_CAPTURE, false>(const Position& pos, Move* moves) noexcept;
//template Move* generate<ENC_CAPTURE, true>(const Position& pos, Move* moves) noexcept;
template Move* generate<ENC_QUIET, false>(const Position& pos, Move* moves) noexcept;
//template Move* generate<ENC_QUIET, true>(const Position& pos, Move* moves) noexcept;
template Move* generate<EVASION, false>(const Position& pos, Move* moves) noexcept;
template Move* generate<EVASION, true>(const Position& pos, Move* moves) noexcept;
template Move* generate<EVA_CAPTURE, false>(const Position& pos, Move* moves) noexcept;
//template Move* generate<EVA_CAPTURE, true >(const Position& pos, Move* moves) noexcept;
template Move* generate<EVA_QUIET, false>(const Position& pos, Move* moves) noexcept;
//template Move* generate<EVA_QUIET, true>(const Position& pos, Move* moves) noexcept;

// <LEGAL> Generates all legal moves
template<>
Move* generate<LEGAL, false>(const Position& pos, Move* moves) noexcept {
    return pos.checkers_bb() != 0 ? generate<EVASION, false>(pos, moves)
                                  : generate<ENCOUNTER, false>(pos, moves);
}
template<>
Move* generate<LEGAL, true>(const Position& pos, Move* moves) noexcept {
    return pos.checkers_bb() != 0 ? generate<EVASION, true>(pos, moves)
                                  : generate<ENCOUNTER, true>(pos, moves);
}

}  // namespace DON
