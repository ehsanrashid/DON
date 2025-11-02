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

#include <initializer_list>
#if defined(USE_AVX512ICL)
    #include <algorithm>
    #include <array>
    #include <immintrin.h>
#endif

#include "bitboard.h"
#include "position.h"

namespace DON {

namespace {

#if defined(USE_AVX512ICL)
Move* write_moves(Move* moves, std::uint32_t mask, __m512i vector) noexcept {
    // Avoid _mm512_mask_compressstoreu_epi16() as it's 256 uOps on Zen4
    _mm512_storeu_si512(reinterpret_cast<__m512i*>(moves),
                        _mm512_maskz_compress_epi16(mask, vector));
    return moves + popcount(mask);
}
#endif

// Splat pawn moves
template<Direction D>
Move* splat_pawn_moves(Move* moves, Bitboard b) noexcept {
    static_assert(D == NORTH || D == SOUTH || D == NORTH_2 || D == SOUTH_2  //
                    || D == NORTH_EAST || D == SOUTH_EAST || D == NORTH_WEST || D == SOUTH_WEST,
                  "D is invalid");

#if defined(USE_AVX512ICL)
    alignas(CACHE_LINE_SIZE) constexpr auto SplatTable = []() constexpr {
        std::array<Move, 64> table{};
        for (std::int8_t i = 0; i < 64; ++i)
        {
            Square s{std::clamp<std::int8_t>(i - D, 0, 63)};
            table[i] = {Move(s, Square{i})};
        }
        return table;
    }();

    const auto* table = reinterpret_cast<const __m512i*>(SplatTable.data());

    moves = write_moves(moves, std::uint32_t(b >> 00), _mm512_load_si512(table + 0));
    moves = write_moves(moves, std::uint32_t(b >> 32), _mm512_load_si512(table + 1));
#else
    while (b)
    {
        Square s = pop_lsb(b);
        *moves++ = Move(s - D, s);
    }
#endif
    return moves;
}

// Splat promotion moves
template<Direction D>
Move* splat_promotion_moves(Move* moves, Bitboard b) noexcept {
    static_assert(D == NORTH || D == SOUTH || D == NORTH_2 || D == SOUTH_2  //
                    || D == NORTH_EAST || D == SOUTH_EAST || D == NORTH_WEST || D == SOUTH_WEST,
                  "D is invalid");

    while (b)
    {
        Square s = pop_lsb(b);
        for (PieceType promo : {QUEEN, KNIGHT, ROOK, BISHOP})
            *moves++ = Move(s - D, s, promo);
    }
    return moves;
}

// Splat moves for a given square and bitboard
Move* splat_moves(Move* moves, Square s, Bitboard b) noexcept {

#if defined(USE_AVX512ICL)
    alignas(CACHE_LINE_SIZE) constexpr auto SplatTable = []() constexpr {
        std::array<Move, 64> table{};
        for (std::int8_t i = 0; i < 64; ++i)
            table[i] = {Move(SQUARE_ZERO, Square{i})};
        return table;
    }();

    __m512i sVec = _mm512_set1_epi16(Move(s, SQUARE_ZERO).raw());

    const auto* table = reinterpret_cast<const __m512i*>(SplatTable.data());

    moves = write_moves(moves, std::uint32_t(b >> 00),
                        _mm512_or_si512(_mm512_load_si512(table + 0), sVec));
    moves = write_moves(moves, std::uint32_t(b >> 32),
                        _mm512_or_si512(_mm512_load_si512(table + 1), sVec));
#else
    while (b)
        *moves++ = Move(s, pop_lsb(b));
#endif
    return moves;
}

template<Color AC, GenType GT>
Move* generate_pawns_moves(const Position& pos, Move* moves, Bitboard target) noexcept {
    assert(!pos.checkers() || !more_than_one(pos.checkers()));

    constexpr bool Evasion = GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET;
    constexpr bool Capture = GT == ENC_CAPTURE || GT == EVA_CAPTURE;
    constexpr bool Quiet   = GT == ENC_QUIET || GT == EVA_QUIET;

    constexpr Direction Push1 = pawn_spush(AC);
    constexpr Direction Push2 = pawn_dpush(AC);
    constexpr Direction CaptL = AC == WHITE ? NORTH_WEST : SOUTH_EAST;
    constexpr Direction CaptR = AC == WHITE ? NORTH_EAST : SOUTH_WEST;

    Bitboard acPawns   = pos.pieces(AC, PAWN);
    Bitboard on7Pawns  = acPawns & relative_rank(AC, RANK_7);
    Bitboard non7Pawns = acPawns & ~on7Pawns;

    Bitboard empties = ~pos.pieces();
    Bitboard enemies = pos.pieces(~AC);

    if constexpr (Evasion)
        enemies &= target;

    // Single and double pawn pushes, no promotions
    if constexpr (!Capture)
    {
        Bitboard b1 = shift_bb<Push1>(non7Pawns) & empties;
        Bitboard b2 = shift_bb<Push1>(b1 & relative_rank(AC, RANK_3)) & empties;

        // Consider only blocking squares
        if constexpr (Evasion)
        {
            b1 &= target;
            b2 &= target;
        }

        moves = splat_pawn_moves<Push1>(moves, b1);
        moves = splat_pawn_moves<Push2>(moves, b2);
    }

    // Promotions and under-promotions & Standard and en-passant captures
    if constexpr (!Quiet)
    {
        Bitboard b;

        if (on7Pawns)
        {
            b = shift_bb<Push1>(on7Pawns) & empties;
            // Consider only blocking and capture squares
            if constexpr (Evasion)
                b &= between_bb(pos.king_sq(AC), lsb(pos.checkers()));
            moves = splat_promotion_moves<Push1>(moves, b);

            b     = shift_bb<CaptL>(on7Pawns) & enemies;
            moves = splat_promotion_moves<CaptL>(moves, b);

            b     = shift_bb<CaptR>(on7Pawns) & enemies;
            moves = splat_promotion_moves<CaptR>(moves, b);
        }

        b     = shift_bb<CaptL>(non7Pawns) & enemies;
        moves = splat_pawn_moves<CaptL>(moves, b);

        b     = shift_bb<CaptR>(non7Pawns) & enemies;
        moves = splat_pawn_moves<CaptR>(moves, b);

        if (is_ok(pos.ep_sq()))
        {
            assert(relative_rank(AC, pos.ep_sq()) == RANK_6);
            assert(pos.pieces(~AC, PAWN) & (pos.ep_sq() - Push1));
            assert(pos.rule50_count() == 0);
            assert(non7Pawns & relative_rank(AC, RANK_5));

            // An en-passant capture cannot resolve a discovered check
            assert(!(Evasion && (target & (pos.ep_sq() + Push1))));

            b = non7Pawns & attacks_bb<PAWN>(pos.ep_sq(), ~AC);
            if (more_than_one(b))
            {
                Bitboard pin = b & pos.blockers(AC);
                assert(!more_than_one(pin));
                if (pin && !aligned(pos.king_sq(AC), pos.ep_sq(), lsb(pin)))
                    b ^= pin;
            }
            assert(b);

            while (b)
                *moves++ = Move(EN_PASSANT, pop_lsb(b), pos.ep_sq());
        }
    }
    return moves;
}

template<Color AC, PieceType PT>
Move* generate_piece_moves(const Position& pos, Move* moves, Bitboard target) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_piece_moves()");
    assert(!pos.checkers() || !more_than_one(pos.checkers()));

    Bitboard pc = pos.pieces<PT>(AC);
    while (pc)
    {
        Square s = pop_lsb(pc);

        Bitboard b = attacks_bb<PT>(s, pos.pieces()) & target;
        if (PT != KNIGHT && (pos.blockers(AC) & s))
            b &= line_bb(pos.king_sq(AC), s);

        moves = splat_moves(moves, s, b);
    }
    return moves;
}

template<Color AC, GenType GT, bool Any>
Move* generate_king_moves(const Position& pos, Move* moves, Bitboard target) noexcept {
    assert(popcount(pos.checkers()) <= 2);

    constexpr bool Castle = GT == ENCOUNTER || GT == ENC_QUIET;

    Square kingSq = pos.king_sq(AC);

    Bitboard b = attacks_bb<KING>(kingSq) & target;

    if (b)
    {
        b &= ~(pos.attacks<KNIGHT>(~AC) | attacks_bb<KING>(pos.king_sq(~AC)));

        Bitboard occupied = pos.pieces() ^ kingSq;

        while (b)
            if (Square s = pop_lsb(b); !(pos.slide_attackers_to(s, occupied) & pos.pieces(~AC)))
            {
                *moves++ = Move(kingSq, s);
                if constexpr (Any)
                    return moves;
            }
    }

    if constexpr (Castle)
    {
        assert(!pos.checkers());
        if (pos.can_castle(AC & ANY_CASTLING))
            for (CastlingRights cr : {AC & KING_SIDE, AC & QUEEN_SIDE})
                if (pos.can_castle(cr) && !pos.castling_impeded(cr))
                {
                    assert(is_ok(pos.castling_rook_sq(cr))
                           && (pos.pieces(AC, ROOK) & pos.castling_rook_sq(cr)));
                    *moves++ = Move(CASTLING, kingSq, pos.castling_rook_sq(cr));
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
    Bitboard target;
    // Skip generating non-king moves when in double check
    if (!Evasion || !more_than_one(pos.checkers()))
    {
        switch (GT)
        {
        case ENCOUNTER :   target = ~pos.pieces(AC);                                     break;
        case ENC_CAPTURE : target =  pos.pieces(~AC);                                    break;
        case ENC_QUIET :   target = ~pos.pieces();                                       break;
        case EVASION :     target = between_bb(pos.king_sq(AC), lsb(pos.checkers()));    break;
        case EVA_CAPTURE : target = pos.checkers();                                      break;
        case EVA_QUIET :   target = between_ex_bb(pos.king_sq(AC), lsb(pos.checkers())); break;
        }

        const auto* pmoves = moves;
        moves = generate_pawns_moves<AC, GT    >(pos, moves, target);
        if (Any && ((pmoves + 0 < moves && pos.legal(pmoves[0]))
                 || (pmoves + 1 < moves && pos.legal(pmoves[1]))
                 || (pmoves + 2 < moves && pos.legal(pmoves[2])))) return moves;
        pmoves = moves;
        moves = generate_piece_moves<AC, KNIGHT>(pos, moves, target);
        if (Any && pmoves != moves) return moves;
        moves = generate_piece_moves<AC, BISHOP>(pos, moves, target);
        if (Any && pmoves != moves) return moves;
        moves = generate_piece_moves<AC, ROOK  >(pos, moves, target);
        if (Any && pmoves != moves) return moves;
        moves = generate_piece_moves<AC, QUEEN >(pos, moves, target);
        if (Any && pmoves != moves) return moves;
    }

    if constexpr (Evasion)
    {
        switch (GT)
        {
        case EVASION :     target = ~pos.pieces(AC);  break;
        case EVA_CAPTURE : target =  pos.pieces(~AC); break;
        case EVA_QUIET :   target = ~pos.pieces();    break;
        }
    }
    // clang-format on

    moves = generate_king_moves<AC, GT, Any>(pos, moves, target);

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

namespace {

template<bool Any>
Move* generate_legal(const Position& pos, Move* moves) noexcept {

    Move *read, *write;
    read = write = moves;

    moves = pos.checkers()  //
            ? generate<EVASION, Any>(pos, moves)
            : generate<ENCOUNTER, Any>(pos, moves);

    Bitboard blockers = pos.blockers(pos.active_color());
    // Filter illegal moves (preserve order)
    while (read != moves)
    {
        Move m = *read++;
        if (((type_of(pos.piece_on(m.org_sq())) == PAWN && (blockers & m.org_sq()))
             || m.type_of() == CASTLING)
            && !pos.legal(m))
            continue;  // skip illegal
        *write++ = m;
    }

    return write;
}

// Explicit template instantiations:
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
