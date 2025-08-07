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

#include <cassert>
#include <initializer_list>

#include "bitboard.h"
#include "position.h"

#if defined(USE_AVX512ICL)
    #include <algorithm>
    #include <array>
    #include <immintrin.h>
#endif

namespace DON {

namespace {

#if defined(USE_AVX512ICL)
inline Move* write_moves(Move* moves, std::uint32_t mask, __m512i vector) noexcept {
    _mm512_storeu_si512(reinterpret_cast<__m512i*>(moves),
                        _mm512_maskz_compress_epi16(mask, vector));
    return moves + popcount(mask);
}
#endif

// Splat pawn moves for a given direction
inline Move* splat_pawn_moves(Move* moves, Bitboard b, Direction d) noexcept {
    assert(d == NORTH || d == SOUTH || d == NORTH_2 || d == SOUTH_2  //
           || d == NORTH_EAST || d == SOUTH_EAST                     //
           || d == NORTH_WEST || d == SOUTH_WEST);

#if defined(USE_AVX512ICL)
    alignas(64) static constexpr auto SolateTable = [] {
        std::array<Move, 64> table{};
        for (std::int8_t i = 0; i < 64; ++i)
        {
            Square s{std::clamp<int8_t>(i - d, 0, 63)};
            table[i] = {Move(s, Square{i})};
        }
        return table;
    }();

    const auto* table = reinterpret_cast<const __m512i*>(SolateTable.data());

    moves = write_moves(moves, static_cast<uint32_t>(b >> 0), _mm512_load_si512(table + 0));
    moves = write_moves(moves, static_cast<uint32_t>(b >> 32), _mm512_load_si512(table + 1));
#else
    while (b)
    {
        Square s = pop_lsb(b);
        *moves++ = Move(s - d, s);
    }
#endif
    return moves;
}

// Splat promotion moves for a given direction
inline Move* splat_promotion_moves(Move* moves, Bitboard b, Direction d) noexcept {
    assert(d == NORTH || d == SOUTH               //
           || d == NORTH_EAST || d == SOUTH_EAST  //
           || d == NORTH_WEST || d == SOUTH_WEST);

#if defined(USE_AVX512ICL)
    while (b)
    {
        Square s = pop_lsb(b);
        for (PieceType promo : {QUEEN, KNIGHT, ROOK, BISHOP})
            *moves++ = Move(s - d, s, promo);
    }
#else
    while (b)
    {
        Square s = pop_lsb(b);
        for (PieceType promo : {QUEEN, KNIGHT, ROOK, BISHOP})
            *moves++ = Move(s - d, s, promo);
    }
#endif
    return moves;
}

// Splat moves for a given square and bitboard
inline Move* splat_moves(Move* moves, Square s, Bitboard b) noexcept {

#if defined(USE_AVX512ICL)
    alignas(64) static constexpr auto SolateTable = [] {
        std::array<Move, 64> table{};
        for (std::int8_t i = 0; i < 64; ++i)
            table[i] = {Move(SQUARE_ZERO, Square{i})};
        return table;
    }();

    __m512i sVec = _mm512_set1_epi16(Move(s, SQUARE_ZERO).raw());

    const auto* table = reinterpret_cast<const __m512i*>(SolateTable.data());

    moves = write_moves(moves, static_cast<uint32_t>(b >> 0),
                        _mm512_or_si512(_mm512_load_si512(table + 0), sVec));
    moves = write_moves(moves, static_cast<uint32_t>(b >> 32),
                        _mm512_or_si512(_mm512_load_si512(table + 1), sVec));
#else
    while (b)
        *moves++ = Move(s, pop_lsb(b));
#endif
    return moves;
}

template<GenType GT>
Move* generate_pawns_moves(const Position& pos, Move* moves, Bitboard target) noexcept {
    assert(!pos.checkers() || !more_than_one(pos.checkers()));

    constexpr bool Evasion = GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET;
    constexpr bool Capture = GT == ENC_CAPTURE || GT == EVA_CAPTURE;
    constexpr bool Quiet   = GT == ENC_QUIET || GT == EVA_QUIET;

    Color ac = pos.active_color();

    Direction Push1 = pawn_spush(ac);
    Direction Push2 = pawn_dpush(ac);
    Direction CaptL = ac == WHITE ? NORTH_WEST : SOUTH_EAST;
    Direction CaptR = ac == WHITE ? NORTH_EAST : SOUTH_WEST;

    Bitboard acPawns   = pos.pieces(ac, PAWN);
    Bitboard on7Pawns  = acPawns & relative_rank(ac, RANK_7);
    Bitboard non7Pawns = acPawns & ~on7Pawns;

    Bitboard empties = ~pos.pieces();
    Bitboard enemies = pos.pieces(~ac);

    if constexpr (Evasion)
        enemies &= target;

    // Single and double pawn pushes, no promotions
    if constexpr (!Capture)
    {
        Bitboard b1 = shift(Push1, non7Pawns) & empties;
        Bitboard b2 = shift(Push1, b1 & relative_rank(ac, RANK_3)) & empties;

        // Consider only blocking squares
        if constexpr (Evasion)
        {
            b1 &= target;
            b2 &= target;
        }

        moves = splat_pawn_moves(moves, b1, Push1);
        moves = splat_pawn_moves(moves, b2, Push2);
    }

    // Promotions and under-promotions & Standard and en-passant captures
    if constexpr (!Quiet)
    {
        Bitboard b;

        if (on7Pawns)
        {
            b = shift(Push1, on7Pawns) & empties;
            // Consider only blocking and capture squares
            if constexpr (Evasion)
                b &= between_bb(pos.king_square(ac), lsb(pos.checkers()));
            moves = splat_promotion_moves(moves, b, Push1);

            b     = shift(CaptL, on7Pawns) & enemies;
            moves = splat_promotion_moves(moves, b, CaptL);

            b     = shift(CaptR, on7Pawns) & enemies;
            moves = splat_promotion_moves(moves, b, CaptR);
        }

        b     = shift(CaptL, non7Pawns) & enemies;
        moves = splat_pawn_moves(moves, b, CaptL);

        b     = shift(CaptR, non7Pawns) & enemies;
        moves = splat_pawn_moves(moves, b, CaptR);

        if (is_ok(pos.ep_square()))
        {
            assert(relative_rank(ac, pos.ep_square()) == RANK_6);
            assert(pos.pieces(~ac, PAWN) & (pos.ep_square() - Push1));
            assert(pos.rule50_count() == 0);
            assert(non7Pawns & relative_rank(ac, RANK_5));

            // An en-passant capture cannot resolve a discovered check
            assert(!(Evasion && (target & (pos.ep_square() + Push1))));

            b = non7Pawns & attacks_bb<PAWN>(pos.ep_square(), ~ac);
            if (more_than_one(b))
            {
                Bitboard pin = b & pos.blockers(ac);
                assert(!more_than_one(pin));
                if (pin && !aligned(pos.king_square(ac), pos.ep_square(), lsb(pin)))
                    b ^= pin;
            }
            assert(b);

            while (b)
                *moves++ = Move(EN_PASSANT, pop_lsb(b), pos.ep_square());
        }
    }
    return moves;
}

template<PieceType PT>
Move* generate_piece_moves(const Position& pos, Move* moves, Bitboard target) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_piece_moves()");
    assert(!pos.checkers() || !more_than_one(pos.checkers()));

    Color ac = pos.active_color();

    Square ksq = pos.king_square(ac);

    Bitboard occupied = pos.pieces();
    Bitboard blockers = pos.blockers(ac);

    Bitboard pc = pos.pieces<PT>(ac, ksq, blockers);
    while (pc)
    {
        Square s = pop_lsb(pc);

        Bitboard b = attacks_bb<PT>(s, occupied) & target;

        if (PT != KNIGHT && (blockers & s))
            b &= line_bb(ksq, s);

        moves = splat_moves(moves, s, b);
    }
    return moves;
}

// clang-format off
template<GenType GT, bool Any>
Move* generate_king_moves(const Position& pos, Move* moves, Bitboard target) noexcept {
    assert(popcount(pos.checkers()) <= 2);

    constexpr bool Castle = GT == ENCOUNTER || GT == ENC_QUIET;

    Color ac = pos.active_color();

    Square ksq = pos.king_square(ac);

    Bitboard b = attacks_bb<KING>(ksq) & target;

    if (b)
    {
        b &= ~(pos.attacks<PAWN>(~ac) | pos.attacks<KING>(~ac));

        Bitboard occupied = pos.pieces() ^ ksq;

        while (b)
        {
            Square s = pop_lsb(b);
            if (!((pos.pieces(~ac, KNIGHT       ) & attacks_bb<KNIGHT>(s))
              || ((pos.pieces(~ac, QUEEN, BISHOP) & attacks_bb<BISHOP>(s))
               && (pos.pieces(~ac, QUEEN, BISHOP) & attacks_bb<BISHOP>(s, occupied)))
              || ((pos.pieces(~ac, QUEEN, ROOK  ) & attacks_bb<ROOK  >(s))
               && (pos.pieces(~ac, QUEEN, ROOK  ) & attacks_bb<ROOK  >(s, occupied)))))
            {
                *moves++ = Move(ksq, s);
                if constexpr (Any) return moves;
            }
        }
    }

    if constexpr (Castle)
    {
        assert(!pos.checkers());
        if (pos.can_castle(ac & ANY_CASTLING))
            for (CastlingRights cr : {ac & KING_SIDE, ac & QUEEN_SIDE})
                if (pos.can_castle(cr) && !pos.castling_impeded(cr))
                {
                    assert(is_ok(pos.castling_rook_square(cr))
                           && (pos.pieces(ac, ROOK) & pos.castling_rook_square(cr)));
                    *moves++ = Move(CASTLING, ksq, pos.castling_rook_square(cr));
                }
    }
   return moves;
}
// clang-format on

template<GenType GT, bool Any>
Move* generate_moves(const Position& pos, Move* moves) noexcept {
    static_assert(GT == ENCOUNTER || GT == ENC_CAPTURE || GT == ENC_QUIET  //
                    || GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET,
                  "Unsupported generate type in generate_moves()");

    constexpr bool Evasion = GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET;

    Color ac = pos.active_color();
    // clang-format off
    Bitboard target;
    // Skip generating non-king moves when in double check
    if (!Evasion || !more_than_one(pos.checkers()))
    {
        switch (GT)
        {
        case ENCOUNTER :   target = ~pos.pieces(ac);                                         break;
        case ENC_CAPTURE : target =  pos.pieces(~ac);                                        break;
        case ENC_QUIET :   target = ~pos.pieces();                                           break;
        case EVASION :     target = between_bb(pos.king_square(ac), lsb(pos.checkers()));    break;
        case EVA_CAPTURE : target = pos.checkers();                                          break;
        case EVA_QUIET :   target = between_ex_bb(pos.king_square(ac), lsb(pos.checkers())); break;
        }

        const auto* lmoves = moves;
        moves = generate_pawns_moves<GT>    (pos, moves, target);
        if (Any && ((moves > lmoves + 0 && pos.legal(lmoves[0]))
                 || (moves > lmoves + 1 && pos.legal(lmoves[1]))
                 || (moves > lmoves + 2 && pos.legal(lmoves[2])))) return moves;
        lmoves = moves;
        moves = generate_piece_moves<KNIGHT>(pos, moves, target);
        if (Any && moves > lmoves) return moves;
        lmoves = moves;
        moves = generate_piece_moves<BISHOP>(pos, moves, target);
        if (Any && moves > lmoves) return moves;
        lmoves = moves;
        moves = generate_piece_moves<ROOK>  (pos, moves, target);
        if (Any && moves > lmoves) return moves;
        lmoves = moves;
        moves = generate_piece_moves<QUEEN> (pos, moves, target);
        if (Any && moves > lmoves) return moves;
    }

    if constexpr (Evasion)
    {
        switch (GT)
        {
        case EVASION :     target = ~pos.pieces(ac);  break;
        case EVA_CAPTURE : target =  pos.pieces(~ac); break;
        case EVA_QUIET :   target = ~pos.pieces();    break;
        }
    }
    // clang-format on

    moves = generate_king_moves<GT, Any>(pos, moves, target);

    return moves;
}

}  // namespace


// <ENCOUNTER  > Generates all pseudo-legal captures and non-captures moves
// <ENC_CAPTURE> Generates all pseudo-legal captures and promotions moves
// <ENC_QUIET  > Generates all pseudo-legal non-captures and castling moves
// <EVASION    > Generates all pseudo-legal check evasions moves
// <EVA_CAPTURE> Generates all pseudo-legal check evasions captures and promotions moves
// <EVA_QUIET  > Generates all pseudo-legal check evasions non-captures moves
template<GenType GT, bool Any>
Move* generate(const Position& pos, Move* moves) noexcept {
    static_assert(GT == ENCOUNTER || GT == ENC_CAPTURE || GT == ENC_QUIET  //
                    || GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET,
                  "Unsupported generate type in generate()");

    assert((GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET) == bool(pos.checkers()));

    moves = generate_moves<GT, Any>(pos, moves);

    return moves;
}

// Explicit template instantiations
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

namespace {

template<bool Any>
Move* generate_legal(const Position& pos, Move* moves) noexcept {

    auto* cur = moves;

    moves = (pos.checkers()  //
               ? generate<EVASION, Any>(pos, moves)
               : generate<ENCOUNTER, Any>(pos, moves));
    // Filter legal moves
    while (cur != moves)
        if (((type_of(pos.piece_on(cur->org_sq())) == PAWN
              && (pos.blockers(pos.active_color()) & cur->org_sq()))
             || cur->type_of() == CASTLING)
            && !pos.legal(*cur))
            *cur = *(--moves);
        else
            ++cur;

    return moves;
}

// Explicit template instantiations
template Move* generate_legal<false>(const Position& pos, Move* moves) noexcept;
template Move* generate_legal<true>(const Position& pos, Move* moves) noexcept;

}  // namespace

// <LEGAL> Generates all legal moves
template<>
Move* generate<LEGAL, false>(const Position& pos, Move* moves) noexcept {
    return generate_legal<false>(pos, moves);
}
template<>
Move* generate<LEGAL, true>(const Position& pos, Move* moves) noexcept {
    return generate_legal<true>(pos, moves);
}

}  // namespace DON
