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

#include "bitboard.h"
#include "position.h"

namespace DON {

namespace {

void generate_promotion_moves(ExtMoves& extMoves, Square s, Direction d) noexcept {
    assert(d == NORTH || d == SOUTH               //
           || d == NORTH_EAST || d == SOUTH_EAST  //
           || d == NORTH_WEST || d == SOUTH_WEST);

    for (PieceType promo : {QUEEN, KNIGHT, ROOK, BISHOP})
        extMoves.emplace_back(s - d, s, promo);
}

template<GenType GT>
void generate_pawns_moves(ExtMoves& extMoves, const Position& pos, Bitboard target) noexcept {
    assert(!pos.checkers() || !more_than_one(pos.checkers()));

    constexpr bool Evasion = GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET;
    constexpr bool Capture = GT == ENC_CAPTURE || GT == EVA_CAPTURE;
    constexpr bool Quiet   = GT == ENC_QUIET || GT == EVA_QUIET;

    Color ac = pos.active_color();

    Direction Push1 = pawn_spush(ac);
    Direction Push2 = pawn_dpush(ac);
    Direction PushL = ac == WHITE ? NORTH_WEST : SOUTH_EAST;
    Direction PushR = ac == WHITE ? NORTH_EAST : SOUTH_WEST;

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

        while (b1)
        {
            Square s = pop_lsb(b1);
            extMoves.emplace_back(s - Push1, s);
        }

        while (b2)
        {
            Square s = pop_lsb(b2);
            extMoves.emplace_back(s - Push2, s);
        }
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

            while (b)
                generate_promotion_moves(extMoves, pop_lsb(b), Push1);

            b = shift(PushL, on7Pawns) & enemies;

            while (b)
                generate_promotion_moves(extMoves, pop_lsb(b), PushL);

            b = shift(PushR, on7Pawns) & enemies;

            while (b)
                generate_promotion_moves(extMoves, pop_lsb(b), PushR);
        }

        b = shift(PushL, non7Pawns) & enemies;

        while (b)
        {
            Square s = pop_lsb(b);
            extMoves.emplace_back(s - PushL, s);
        }

        b = shift(PushR, non7Pawns) & enemies;

        while (b)
        {
            Square s = pop_lsb(b);
            extMoves.emplace_back(s - PushR, s);
        }

        if (is_ok(pos.ep_square()))
        {
            assert(relative_rank(ac, pos.ep_square()) == RANK_6);
            assert(pos.pieces(~ac, PAWN) & (pos.ep_square() - Push1));
            assert(pos.rule50_count() == 0);
            assert(non7Pawns & relative_rank(ac, RANK_5));

            // An en-passant capture cannot resolve a discovered check
            //if (Evasion && (target & (pos.ep_square() + Push1)))
            //    return;
            assert(!(Evasion && (target & (pos.ep_square() + Push1))));

            b = non7Pawns & pawn_attacks_bb(~ac, pos.ep_square());
            if (more_than_one(b))
            {
                Bitboard pin = b & pos.blockers(ac);
                assert(!more_than_one(pin));
                if (pin && !aligned(pos.king_square(ac), pos.ep_square(), lsb(pin)))
                    b ^= pin;
            }
            assert(b);

            while (b)
                extMoves.emplace_back(EN_PASSANT, pop_lsb(b), pos.ep_square());
        }
    }
}

template<PieceType PT>
void generate_piece_moves(ExtMoves& extMoves, const Position& pos, Bitboard target) noexcept {
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

        while (b)
            extMoves.emplace_back(s, pop_lsb(b));
    }
}

// clang-format off
template<GenType GT, bool Any>
void generate_king_moves(ExtMoves& extMoves, const Position& pos, Bitboard target) noexcept {
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
                extMoves.emplace_back(ksq, s);
                if constexpr (Any) return;
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
                    extMoves.emplace_back(CASTLING, ksq, pos.castling_rook_square(cr));
                }
    }
}
// clang-format on

template<GenType GT, bool Any>
void generate_moves(ExtMoves& extMoves, const Position& pos) noexcept {
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
        case ENC_CAPTURE : target = pos.pieces(~ac);                                         break;
        case ENC_QUIET :   target = ~pos.pieces();                                           break;
        case EVASION :     target = between_bb(pos.king_square(ac), lsb(pos.checkers()));    break;
        case EVA_CAPTURE : target = pos.checkers();                                          break;
        case EVA_QUIET :   target = between_ex_bb(pos.king_square(ac), lsb(pos.checkers())); break;
        }

        generate_pawns_moves<GT>    (extMoves, pos, target);
        if (Any && ((extMoves.size() > 0 && pos.legal(extMoves[0]))
                 || (extMoves.size() > 1 && pos.legal(extMoves[1]))
                 || (extMoves.size() > 2 && pos.legal(extMoves[2])))) return;
        [[maybe_unused]] auto extEnd = extMoves.end();
        generate_piece_moves<KNIGHT>(extMoves, pos, target);
        if (Any && extEnd != extMoves.end()) return;
        generate_piece_moves<BISHOP>(extMoves, pos, target);
        if (Any && extEnd != extMoves.end()) return;
        generate_piece_moves<ROOK>  (extMoves, pos, target);
        if (Any && extEnd != extMoves.end()) return;
        generate_piece_moves<QUEEN> (extMoves, pos, target);
        if (Any && extEnd != extMoves.end()) return;
    }

    if constexpr (Evasion)
    {
        switch (GT)
        {
        case EVASION :     target = ~pos.pieces(ac); break;
        case EVA_CAPTURE : target = pos.pieces(~ac); break;
        case EVA_QUIET :   target = ~pos.pieces();   break;
        }
    }
    // clang-format on

    generate_king_moves<GT, Any>(extMoves, pos, target);
}

template<bool Any>
ExtMoves::Itr generate_legal(ExtMoves& extMoves, const Position& pos) noexcept {
    pos.checkers()  //
      ? generate<EVASION, Any>(extMoves, pos)
      : generate<ENCOUNTER, Any>(extMoves, pos);
    // Filter legal moves
    return extMoves.remove_if([&pos](const Move& m) noexcept {
        assert(pos.pseudo_legal(m));
        return ((type_of(pos.piece_on(m.org_sq())) == PAWN
                 && (pos.blockers(pos.active_color()) & m.org_sq()))
                || m.type_of() == CASTLING)
            && !pos.legal(m);
    });
}

// Explicit template instantiations
template ExtMoves::Itr generate_legal<false>(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::Itr generate_legal<true>(ExtMoves& extMoves, const Position& pos) noexcept;

}  // namespace

// clang-format off

// <ENCOUNTER  > Generates all pseudo-legal captures and non-captures moves
// <ENC_CAPTURE> Generates all pseudo-legal captures and promotions moves
// <ENC_QUIET  > Generates all pseudo-legal non-captures and castling moves
// <EVASION    > Generates all pseudo-legal check evasions moves
// <EVA_CAPTURE> Generates all pseudo-legal check evasions captures and promotions moves
// <EVA_QUIET  > Generates all pseudo-legal check evasions non-captures moves
template<GenType GT, bool Any>
ExtMoves::Itr generate(ExtMoves& extMoves, const Position& pos) noexcept {
    static_assert(GT == ENCOUNTER || GT == ENC_CAPTURE || GT == ENC_QUIET  //
                    || GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET,
                  "Unsupported generate type in generate()");

    assert((GT == EVASION || GT == EVA_CAPTURE || GT == EVA_QUIET) == bool(pos.checkers()));

    if constexpr (!Any)
        extMoves.reserve(24 + 12 * (GT == ENCOUNTER) +  4 * (GT == ENC_CAPTURE) +  8 * (GT == ENC_QUIET)  //
                            -  8 * (GT == EVASION)   - 16 * (GT == EVA_CAPTURE) - 12 * (GT == EVA_QUIET));
    generate_moves<GT, Any>(extMoves, pos);
    return extMoves.end();
}

// Explicit template instantiations
template ExtMoves::Itr generate<ENCOUNTER  , false>(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::Itr generate<ENCOUNTER  , true >(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::Itr generate<ENC_CAPTURE, false>(ExtMoves& extMoves, const Position& pos) noexcept;
//template ExtMoves::Itr generate<ENC_CAPTURE, true >(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::Itr generate<ENC_QUIET  , false>(ExtMoves& extMoves, const Position& pos) noexcept;
//template ExtMoves::Itr generate<ENC_QUIET  , true >(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::Itr generate<EVASION    , false>(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::Itr generate<EVASION    , true >(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::Itr generate<EVA_CAPTURE, false>(ExtMoves& extMoves, const Position& pos) noexcept;
//template ExtMoves::Itr generate<EVA_CAPTURE, true >(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::Itr generate<EVA_QUIET  , false>(ExtMoves& extMoves, const Position& pos) noexcept;
//template ExtMoves::Itr generate<EVA_QUIET  , true >(ExtMoves& extMoves, const Position& pos) noexcept;

// clang-format on

// <LEGAL> Generates all legal moves
template<>
ExtMoves::Itr generate<LEGAL, false>(ExtMoves& extMoves, const Position& pos) noexcept {
    return generate_legal<false>(extMoves, pos);
}

template<>
ExtMoves::Itr generate<LEGAL, true>(ExtMoves& extMoves, const Position& pos) noexcept {
    return generate_legal<true>(extMoves, pos);
}

}  // namespace DON
