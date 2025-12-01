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
Move* splat_pawn_moves(Bitboard b, Move* moves) noexcept {
    static_assert(D == NORTH || D == SOUTH                 //
                    || D == NORTH_2 || D == SOUTH_2        //
                    || D == NORTH_EAST || D == SOUTH_EAST  //
                    || D == NORTH_WEST || D == SOUTH_WEST,
                  "D is invalid");

#if defined(USE_AVX512ICL)
    (void) AC;
    alignas(CACHE_LINE_SIZE) constexpr auto SplatTable = []() constexpr {
        StdArray<Move, SQUARE_NB> table{};
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            Square sq = std::clamp(s - D, SQ_A1, SQ_H8);
            table[s]  = Move(sq, s);
        }
        return table;
    }();

    const auto* table = reinterpret_cast<const __m512i*>(SplatTable.data());

    moves = write_moves(std::uint32_t(b >> 00), _mm512_load_si512(table + 0), moves);
    moves = write_moves(std::uint32_t(b >> 32), _mm512_load_si512(table + 1), moves);
#else
    while (b)
    {
        Square dst;
        if constexpr (AC == WHITE)
            dst = pop_msb(b);
        else
            dst = pop_lsb(b);

        *moves++ = Move(dst - D, dst);
    }
#endif

    return moves;
}

// Splat promotion moves
template<Color AC, GenType GT, Direction D, bool Enemy>
Move* splat_promotion_moves(Bitboard b, Move* moves) noexcept {
    static_assert(D == NORTH || D == SOUTH                 //
                    || D == NORTH_EAST || D == SOUTH_EAST  //
                    || D == NORTH_WEST || D == SOUTH_WEST,
                  "D is invalid");

    constexpr bool All     = GT == ENCOUNTER || GT == EVASION;
    constexpr bool Capture = GT == ENC_CAPTURE || GT == EVA_CAPTURE;
    constexpr bool Quiet   = GT == ENC_QUIET || GT == EVA_QUIET;

    while (b)
    {
        Square dst;
        if constexpr (AC == WHITE)
            dst = pop_msb(b);
        else
            dst = pop_lsb(b);

        if constexpr (All || Capture)
            *moves++ = Move(dst - D, dst, QUEEN);
        if constexpr (All || (Capture && Enemy) || (Quiet && !Enemy))
        {
            *moves++ = Move(dst - D, dst, ROOK);
            *moves++ = Move(dst - D, dst, BISHOP);
            *moves++ = Move(dst - D, dst, KNIGHT);
        }
    }

    return moves;
}

// Splat moves
template<Color AC>
Move* splat_moves(Square org, Bitboard b, Move* moves) noexcept {

#if defined(USE_AVX512ICL)
    (void) AC;
    alignas(CACHE_LINE_SIZE) constexpr auto SplatTable = []() constexpr {
        StdArray<Move, SQUARE_NB> table{};
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
            table[s] = Move(SQUARE_ZERO, s);
        return table;
    }();

    __m512i orgVec = _mm512_set1_epi16(Move(org, SQUARE_ZERO).raw());

    const auto* table = reinterpret_cast<const __m512i*>(SplatTable.data());

    moves = write_moves(std::uint32_t(b >> 00),
                        _mm512_or_si512(_mm512_load_si512(table + 0), orgVec), moves);
    moves = write_moves(std::uint32_t(b >> 32),
                        _mm512_or_si512(_mm512_load_si512(table + 1), orgVec), moves);
#else
    while (b)
    {
        Square dst;
        if constexpr (AC == WHITE)
            dst = pop_msb(b);
        else
            dst = pop_lsb(b);

        *moves++ = Move(org, dst);
    }
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
    constexpr Direction LCap  = AC == WHITE ? NORTH_WEST : SOUTH_EAST;
    constexpr Direction RCap  = AC == WHITE ? NORTH_EAST : SOUTH_WEST;

    Bitboard acPawns   = pos.pieces(AC, PAWN);
    Bitboard on7Pawns  = acPawns & relative_rank(AC, RANK_7);
    Bitboard non7Pawns = acPawns & ~on7Pawns;

    Bitboard empties = ~pos.pieces();
    Bitboard enemies = pos.pieces(~AC);

    if constexpr (Evasion)
        enemies &= target;

    // Promotions and under-promotions
    if (on7Pawns)
    {
        Bitboard b;

        b     = shift_bb<LCap>(on7Pawns) & enemies;
        moves = splat_promotion_moves<AC, GT, LCap, true>(b, moves);

        b     = shift_bb<RCap>(on7Pawns) & enemies;
        moves = splat_promotion_moves<AC, GT, RCap, true>(b, moves);

        b = shift_bb<Push1>(on7Pawns) & empties;
        // Consider only blocking and capture squares
        if constexpr (Evasion)
            b &= between_bb(pos.square<KING>(AC), lsb(pos.checkers()));
        moves = splat_promotion_moves<AC, GT, Push1, false>(b, moves);
    }

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

        moves = splat_pawn_moves<AC, Push1>(b1, moves);
        moves = splat_pawn_moves<AC, Push2>(b2, moves);
    }

    // Standard and en-passant captures
    if constexpr (!Quiet)
    {
        Bitboard b;

        b     = shift_bb<LCap>(non7Pawns) & enemies;
        moves = splat_pawn_moves<AC, LCap>(b, moves);

        b     = shift_bb<RCap>(non7Pawns) & enemies;
        moves = splat_pawn_moves<AC, RCap>(b, moves);

        Square enPassantSq = pos.en_passant_sq();
        if (is_ok(enPassantSq))
        {
            assert(relative_rank(AC, enPassantSq) == RANK_6);
            assert(pos.pieces(~AC, PAWN) & (enPassantSq - Push1));
            assert(pos.rule50_count() == 0);
            assert(non7Pawns & relative_rank(AC, RANK_5));

            // An en-passant capture cannot resolve a discovered check
            assert(!(Evasion && (target & (enPassantSq + Push1))));

            b = non7Pawns & attacks_bb<PAWN>(enPassantSq, ~AC);
            if (more_than_one(b))
            {
                Bitboard pinned = b & pos.blockers(AC);
                assert(!more_than_one(pinned));

                if (pinned && !aligned(pos.square<KING>(AC), enPassantSq, lsb(pinned)))
                    b ^= pinned;
            }
            assert(b);

            while (b)
            {
                Square org;
                if constexpr (AC == WHITE)
                    org = pop_lsb(b);
                else
                    org = pop_msb(b);

                *moves++ = Move(EN_PASSANT, org, enPassantSq);
            }
        }
    }

    return moves;
}

template<Color AC, PieceType PT>
Move* generate_piece_moves(const Position& pos, Move* moves, Bitboard target) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_piece_moves()");
    assert(!pos.checkers() || !more_than_one(pos.checkers()));

    const auto& pL = pos.squares<PT>(AC);
    const auto* pB = pos.base(AC);

    const std::size_t n = pL.count();
    assert(n <= Position::CAPACITY[PT - 1]);

    StdArray<Square, Position::CAPACITY[PT - 1]> sortedPL;
    std::memcpy(sortedPL.data(), pL.data(pB), n * sizeof(Square));

    Square*       beg = sortedPL.data();
    Square* const end = beg + n;

    if (n > 1)
    {
        if constexpr (AC == WHITE)
            std::sort(beg, end, std::greater<>{});
        else
            std::sort(beg, end, std::less<>{});
    }

    for (; beg != end; ++beg)
    {
        Square org = *beg;

        Bitboard b = attacks_bb<PT>(org, pos.pieces()) & target;

        if (pos.blockers(AC) & org)
            b &= line_bb(pos.square<KING>(AC), org);

        moves = splat_moves<AC>(org, b, moves);
    }

    return moves;
}

template<Color AC, GenType GT, bool Any>
Move* generate_king_moves(const Position& pos, Move* moves, Bitboard target) noexcept {
    //assert(popcount(pos.checkers()) <= 2);

    constexpr bool Castle = GT == ENCOUNTER || GT == ENC_QUIET;

    Square kingSq = pos.square<KING>(AC);

    Bitboard b = attacks_bb<KING>(kingSq) & target;

    if (b)
    {
        b &= ~(pos.attacks<KNIGHT>(~AC) | attacks_bb<KING>(pos.square<KING>(~AC)));

        Bitboard occupied = pos.pieces() ^ kingSq;

        while (b)
        {
            Square dst;
            if constexpr (AC == WHITE)
                dst = pop_msb(b);
            else
                dst = pop_lsb(b);

            if (pos.slide_attackers_to(dst, occupied) & pos.pieces(~AC))
                continue;

            *moves++ = Move(kingSq, dst);

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
        case ENCOUNTER :   target = ~pos.pieces(AC);                                          break;
        case ENC_CAPTURE : target =  pos.pieces(~AC);                                         break;
        case ENC_QUIET :   target = ~pos.pieces();                                            break;
        case EVASION :     target = between_bb(pos.square<KING>(AC), lsb(pos.checkers()));    break;
        case EVA_CAPTURE : target = pos.checkers();                                           break;
        case EVA_QUIET :   target = between_ex_bb(pos.square<KING>(AC), lsb(pos.checkers())); break;
        }

        const auto* pMoves = moves;
        moves = generate_pawns_moves<AC, GT    >(pos, moves, target);
        if (Any && ((pMoves + 0 < moves && pos.legal(pMoves[0]))
                 || (pMoves + 1 < moves && pos.legal(pMoves[1]))
                 || (pMoves + 2 < moves && pos.legal(pMoves[2])))) return moves;
        pMoves = moves;
        moves = generate_piece_moves<AC, KNIGHT>(pos, moves, target);
        if (Any && pMoves != moves) return moves;
        moves = generate_piece_moves<AC, BISHOP>(pos, moves, target);
        if (Any && pMoves != moves) return moves;
        moves = generate_piece_moves<AC, ROOK  >(pos, moves, target);
        if (Any && pMoves != moves) return moves;
        moves = generate_piece_moves<AC, QUEEN >(pos, moves, target);
        if (Any && pMoves != moves) return moves;
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

    Move *read = moves, *write = moves;

    moves = pos.checkers()  //
            ? generate<EVASION, Any>(pos, moves)
            : generate<ENCOUNTER, Any>(pos, moves);

    Bitboard pawns    = pos.pieces(PAWN);
    Bitboard blockers = pos.blockers(pos.active_color());

    Bitboard pawnBlockers = pawns & blockers;
    // Filter illegal moves (preserve order)
    while (read != moves)
    {
        Move m = *read++;

        if (((pawnBlockers & m.org_sq()) || m.type_of() == CASTLING) && !pos.legal(m))
            continue;  // Skip illegal

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
