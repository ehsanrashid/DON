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

namespace DON {

namespace {

void generate_promotion_moves(ExtMoves& extMoves, Square dst, Direction dir) noexcept {

    for (PieceType promo : {QUEEN, ROOK, BISHOP, KNIGHT})
        extMoves += Move(dst - dir, dst, promo);
}

template<GenType GT>
void generate_pawns_moves(ExtMoves& extMoves, const Position& pos, Bitboard target) noexcept {
    assert(!pos.checkers() || !more_than_one(pos.checkers()));

    Color ac = pos.active_color();

    Direction Up_1 = pawn_spush(ac);
    Direction Up_2 = pawn_dpush(ac);
    Direction Up_R = Direction((int(NORTH_EAST) ^ -int(ac)) + int(ac));
    Direction Up_L = Direction((int(NORTH_WEST) ^ -int(ac)) + int(ac));

    Bitboard acPawns   = pos.pieces(ac, PAWN);
    Bitboard on7Pawns  = acPawns & relative_rank(ac, RANK_7);
    Bitboard non7Pawns = acPawns & ~on7Pawns;

    Bitboard empties = ~pos.pieces();
    Bitboard enemies = pos.pieces(~ac);

    Bitboard b = 0;
    if constexpr (GT != CAPTURES)
        if (non7Pawns)
            b = shift(Up_1, non7Pawns) & empties;

    if constexpr (GT == EVASIONS)
    {
        // Consider only blocking squares
        empties &= target;
        enemies &= target;
    }

    // Single and double pawn pushes, no promotions
    if constexpr (GT != CAPTURES)
    {
        Bitboard b1 = b;
        if constexpr (GT == EVASIONS)
            b1 &= empties;
        Bitboard b2 = shift(Up_1, b & relative_rank(ac, RANK_3)) & empties;

        while (b1)
        {
            Square dst = pop_lsb(b1);
            extMoves += Move(dst - Up_1, dst);
        }

        while (b2)
        {
            Square dst = pop_lsb(b2);
            extMoves += Move(dst - Up_2, dst);
        }
    }

    // Promotions and under-promotions & Standard and en-passant captures
    if constexpr (GT != QUIETS)
    {
        if (on7Pawns)
        {
            b = shift(Up_1, on7Pawns) & empties;
            while (b)
                generate_promotion_moves(extMoves, pop_lsb(b), Up_1);

            b = shift(Up_R, on7Pawns) & enemies;
            while (b)
                generate_promotion_moves(extMoves, pop_lsb(b), Up_R);

            b = shift(Up_L, on7Pawns) & enemies;
            while (b)
                generate_promotion_moves(extMoves, pop_lsb(b), Up_L);
        }

        b = shift(Up_R, non7Pawns) & enemies;
        while (b)
        {
            Square dst = pop_lsb(b);
            extMoves += Move(dst - Up_R, dst);
        }

        b = shift(Up_L, non7Pawns) & enemies;
        while (b)
        {
            Square dst = pop_lsb(b);
            extMoves += Move(dst - Up_L, dst);
        }

        if (ep_is_ok(pos.ep_square()))
        {
            assert(relative_rank(ac, pos.ep_square()) == RANK_6);
            assert(pos.pieces(~ac, PAWN) & (pos.ep_square() - Up_1));
            assert(pos.rule50_count() == 0);
            assert(non7Pawns & relative_rank(ac, RANK_5));

            // An en-passant capture cannot resolve a discovered check
            if (GT == EVASIONS && (target & (pos.ep_square() + Up_1)))
                return;

            b = non7Pawns & pawn_attacks_bb(~ac, pos.ep_square());
            /*
            if (Bitboard pin; more_than_one(b) && (pin = pos.blockers(ac) & b))
            {
                assert(!more_than_one(pin));
                if (!aligned(lsb(pin), pos.ep_square(), pos.king_square(ac)))
                    b ^= pin;
            }
            */
            assert(b);

            while (b)
                extMoves += Move(EN_PASSANT, pop_lsb(b), pos.ep_square());
        }
    }
}

template<PieceType PT>
void generate_piece_moves(ExtMoves& extMoves, const Position& pos, Bitboard target) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_piece_moves()");

    assert(!pos.checkers() || !more_than_one(pos.checkers()));

    Color ac = pos.active_color();

    Bitboard pc = pos.pieces<PT>(ac, pos.king_square(ac));
    while (pc)
    {
        Square org = pop_lsb(pc);

        Bitboard b = attacks_bb<PT>(org, pos.pieces()) & target;

        if (PT != KNIGHT && (pos.blockers(ac) & org))
            b &= line_bb(pos.king_square(ac), org);

        while (b)
            extMoves += Move(org, pop_lsb(b));
    }
}

template<GenType GT>
void generate_king_moves(ExtMoves& extMoves, const Position& pos, Bitboard target) noexcept {
    assert(popcount(pos.checkers()) <= 2);

    Color ac = pos.active_color();

    Square ksq = pos.king_square(ac);

    Bitboard b = attacks_bb<KING>(ksq) & target;

    if constexpr (GT == EVASIONS)
    {
        Bitboard occupied = pos.pieces() ^ ksq;

        Bitboard sliders = pos.checkers() & pos.pieces(QUEEN, ROOK, BISHOP);
        while (sliders && b)
        {
            Square slider = pop_lsb(sliders);

            if ((pos.pieces(QUEEN) & slider) && distance(ksq, slider) <= 2)
                b &= ~(attacks_bb<BISHOP>(slider) | attacks_bb<ROOK>(slider, occupied));
            else if ((pos.pieces(ROOK) & slider) && distance(ksq, slider) <= 1)
                b &= ~attacks_bb<ROOK>(slider);
            else
                b &= ~line_bb(ksq, slider) | pos.checkers();
        }
        Bitboard knight = pos.checkers() & pos.pieces(KNIGHT);
        if (knight)
            b &= ~attacks_bb<KNIGHT>(lsb(knight));
    }

    while (b)
        extMoves += Move(ksq, pop_lsb(b));

    if constexpr (GT == QUIETS || GT == ENCOUNTERS)
    {
        assert(!pos.checkers());
        if (pos.can_castle(ac & ANY_CASTLING))
            for (CastlingRights cr : {ac & KING_SIDE, ac & QUEEN_SIDE})
                if (pos.can_castle(cr) && !pos.castling_impeded(cr))
                {
                    assert(pos.castling_rook_square(cr) != SQ_NONE
                           && pos.pieces(ac, ROOK) & pos.castling_rook_square(cr));
                    extMoves += Move(CASTLING, ksq, pos.castling_rook_square(cr));
                }
    }
}

template<GenType GT>
void generate_moves(ExtMoves& extMoves, const Position& pos) noexcept {
    static_assert(GT == CAPTURES || GT == QUIETS || GT == ENCOUNTERS || GT == EVASIONS,
                  "Unsupported generate type in generate_moves()");

    Color ac = pos.active_color();

    Bitboard target;
    // Skip generating non-king moves when in double check
    if (GT != EVASIONS || !more_than_one(pos.checkers()))
    {
        // clang-format off
        switch (GT)
        {
        case CAPTURES :   target = pos.pieces(~ac);                                      break;
        case QUIETS :     target = ~pos.pieces();                                        break;
        case ENCOUNTERS : target = ~pos.pieces(ac);                                      break;
        case EVASIONS :   target = between_bb(pos.king_square(ac), lsb(pos.checkers())); break;
        default :         target = 0;
        }

        generate_pawns_moves<GT>    (extMoves, pos, target);
        generate_piece_moves<KNIGHT>(extMoves, pos, target);
        generate_piece_moves<BISHOP>(extMoves, pos, target);
        generate_piece_moves<ROOK>  (extMoves, pos, target);
        generate_piece_moves<QUEEN> (extMoves, pos, target);
        // clang-format on
    }

    if constexpr (GT == EVASIONS)
        target = ~pos.pieces(ac);

    generate_king_moves<GT>(extMoves, pos, target);
}

}  // namespace

// <CAPTURES>   Generates all pseudo-legal captures plus queen promotions moves
// <QUIETS>     Generates all pseudo-legal non-captures, castling and underpromotions moves
// <ENCOUNTERS> Generates all pseudo-legal captures and non-captures moves
// <EVASIONS>   Generates all pseudo-legal check evasions moves
template<GenType GT>
ExtMoves::NormalItr generate(ExtMoves& extMoves, const Position& pos) noexcept {
    static_assert(GT == CAPTURES || GT == QUIETS || GT == ENCOUNTERS || GT == EVASIONS,
                  "Unsupported generate type in generate()");

    assert((GT == EVASIONS) == bool(pos.checkers()));

    generate_moves<GT>(extMoves, pos);
    return extMoves.end();
}

// Explicit template instantiations
// clang-format off
template ExtMoves::NormalItr generate<CAPTURES>  (ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::NormalItr generate<QUIETS>    (ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::NormalItr generate<ENCOUNTERS>(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::NormalItr generate<EVASIONS>  (ExtMoves& extMoves, const Position& pos) noexcept;
// clang-format on

// Filter illegal moves
ExtMoves::NormalItr filter_illegal(ExtMoves& extMoves, const Position& pos) noexcept {

    Color ac = pos.active_color();

    Square ksq = pos.king_square(ac);

    Bitboard pinned = pos.blockers(ac) & pos.pieces(ac);

    return extMoves.remove_if([=, &pos](Move m) noexcept -> bool {
        return ((pinned & m.org_sq()) || m.org_sq() == ksq || m.type_of() == EN_PASSANT)
            && !pos.legal(m);
    });
}

// <LEGAL> Generates all legal moves
template<>
ExtMoves::NormalItr generate<LEGAL>(ExtMoves& extMoves, const Position& pos) noexcept {

    pos.checkers()  //
      ? generate<EVASIONS>(extMoves, pos)
      : generate<ENCOUNTERS>(extMoves, pos);
    return filter_illegal(extMoves, pos);
}

}  // namespace DON
