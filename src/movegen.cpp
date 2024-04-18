/*
  DON, a UCI chess playing engine derived from Glaurung 2.1

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

template<GenType GT, Direction D, bool Enemy>
ExtMove* make_promotions(ExtMove* moves, [[maybe_unused]] Square dst) noexcept {

    if constexpr (GT == CAPTURES || GT == EVASIONS || GT == NON_EVASIONS)
        *moves++ = Move::make<PROMOTION>(dst - D, dst, QUEEN);

    if constexpr ((GT == CAPTURES && Enemy) || (GT == QUIETS && !Enemy) || GT == EVASIONS
                  || GT == NON_EVASIONS)
    {
        *moves++ = Move::make<PROMOTION>(dst - D, dst, ROOK);
        *moves++ = Move::make<PROMOTION>(dst - D, dst, BISHOP);
        *moves++ = Move::make<PROMOTION>(dst - D, dst, KNIGHT);
    }

    return moves;
}

template<Color Us, GenType GT>
ExtMove* generate_pawn_moves(const Position& pos, ExtMove* moves, Bitboard target) noexcept {

    constexpr Direction Up      = pawn_push(Us);
    constexpr Direction UpRight = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft  = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);

    Bitboard empties = ~pos.pieces();
    Bitboard enemies = GT == EVASIONS ? pos.checkers() : pos.pieces(~Us);

    Bitboard on7Pawns    = pos.pieces(Us, PAWN) & (Us == WHITE ? Rank7BB : Rank2BB);
    Bitboard notOn7Pawns = pos.pieces(Us, PAWN) & ~on7Pawns;

    // Single and double pawn pushes, no promotions
    if constexpr (GT != CAPTURES)
    {
        Bitboard b1 = shift<Up>(notOn7Pawns) & empties;
        Bitboard b2 = shift<Up>(b1 & (Us == WHITE ? Rank3BB : Rank6BB)) & empties;

        if constexpr (GT == EVASIONS)  // Consider only blocking squares
        {
            b1 &= target;
            b2 &= target;
        }

        if constexpr (GT == QUIET_CHECKS)
        {
            // To make a quiet check, you either make a direct check by pushing a pawn
            // or push a blocker pawn that is not on the same file as the enemy king.
            // Discovered check promotion has been already generated amongst the captures.
            Square ksq = pos.king_square(~Us);

            Bitboard dcCandidatePawns = pos.blockers(~Us) & ~file_bb(ksq);
            b1 &= pawn_attacks_bb(~Us, ksq) | shift<Up>(dcCandidatePawns);
            b2 &= pawn_attacks_bb(~Us, ksq) | shift<2 * Up>(dcCandidatePawns);
        }

        while (b1)
        {
            Square dst = pop_lsb(b1);
            *moves++   = Move(dst - Up, dst);
        }

        while (b2)
        {
            Square dst = pop_lsb(b2);
            *moves++   = Move(dst - 2 * Up, dst);
        }
    }

    // Promotions and under-promotions
    if (on7Pawns)
    {
        Bitboard b1 = shift<UpRight>(on7Pawns) & enemies;
        Bitboard b2 = shift<UpLeft>(on7Pawns) & enemies;
        Bitboard b3 = shift<Up>(on7Pawns) & empties;

        if constexpr (GT == EVASIONS)
            b3 &= target;

        while (b1)
            moves = make_promotions<GT, UpRight, true>(moves, pop_lsb(b1));

        while (b2)
            moves = make_promotions<GT, UpLeft, true>(moves, pop_lsb(b2));

        while (b3)
            moves = make_promotions<GT, Up, false>(moves, pop_lsb(b3));
    }

    // Standard and en passant captures
    if constexpr (GT == CAPTURES || GT == EVASIONS || GT == NON_EVASIONS)
    {
        Bitboard b1 = shift<UpRight>(notOn7Pawns) & enemies;
        Bitboard b2 = shift<UpLeft>(notOn7Pawns) & enemies;

        while (b1)
        {
            Square dst = pop_lsb(b1);
            *moves++   = Move(dst - UpRight, dst);
        }

        while (b2)
        {
            Square dst = pop_lsb(b2);
            *moves++   = Move(dst - UpLeft, dst);
        }

        Bitboard on5Pawns = notOn7Pawns & (Us == WHITE ? Rank5BB : Rank4BB);
        if (on5Pawns && is_ok(pos.ep_square()))
        {
            assert(rank_of(pos.ep_square()) == relative_rank(Us, RANK_6));
            assert(pos.rule50_count() == 0);

            // An en passant capture cannot resolve a discovered check
            if constexpr (GT == EVASIONS)
                if (target & (pos.ep_square() + Up))
                    return moves;

            b1 = on5Pawns & pawn_attacks_bb(~Us, pos.ep_square());
            assert(b1);

            while (b1)
                *moves++ = Move::make<EN_PASSANT>(pop_lsb(b1), pos.ep_square());
        }
    }

    return moves;
}

template<Color Us, PieceType PT, bool Checks>
ExtMove* generate_moves(const Position& pos, ExtMove* moves, Bitboard target) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_moves()");

    Bitboard bb = pos.pieces(Us, PT);

    while (bb)
    {
        Square org = pop_lsb(bb);

        Bitboard b = attacks_bb<PT>(org, pos.pieces()) & target;
        if (pos.blockers(Us) & org)
        {
            if constexpr (PT == KNIGHT)
                continue;
            b &= line_bb(org, pos.king_square(Us));
        }
        // To check, you either move freely a blocker or make a direct check.
        if (Checks && (PT == QUEEN || !(pos.blockers(~Us) & org)))
            b &= pos.check_squares(PT);

        while (b)
            *moves++ = Move(org, pop_lsb(b));
    }

    return moves;
}

template<Color Us, GenType GT>
ExtMove* generate_all(const Position& pos, ExtMove* moves) noexcept {
    static_assert(GT == CAPTURES || GT == QUIETS || GT == QUIET_CHECKS || GT == EVASIONS
                    || GT == NON_EVASIONS,
                  "Unsupported generate type in generate_all()");

    constexpr bool Checks = GT == QUIET_CHECKS;  // Reduce template instantiations

    Square ksq = pos.king_square(Us);

    Bitboard target;
    // Skip generating non-king moves when in double check
    if (GT != EVASIONS || !more_than_one(pos.checkers()))
    {
        target = GT == CAPTURES     ? pos.pieces(~Us)
               : GT == EVASIONS     ? between_bb(ksq, lsb(pos.checkers()))
               : GT == NON_EVASIONS ? ~pos.pieces(Us)
                                    : ~pos.pieces();  // QUIETS || QUIET_CHECKS

        moves = generate_pawn_moves<Us, GT>(pos, moves, target);
        moves = generate_moves<Us, KNIGHT, Checks>(pos, moves, target);
        moves = generate_moves<Us, BISHOP, Checks>(pos, moves, target);
        moves = generate_moves<Us, ROOK, Checks>(pos, moves, target);
        moves = generate_moves<Us, QUEEN, Checks>(pos, moves, target);
    }

    if (!Checks || pos.blockers(~Us) & ksq)
    {
        if constexpr (GT == EVASIONS)
        {
            target = ~pos.pieces(Us);

            Bitboard checkers = pos.checkers();
            assert(checkers);
            while (checkers)
            {
                Square checker = pop_lsb(checkers);
                if (pos.pieces(PAWN, KNIGHT) & checker)
                    continue;
                target &= ~line_bb(checker, ksq) | checker;
            }
        }

        Bitboard threatened = pos.attacks_by<PAWN>(~Us) | pos.attacks_by<KNIGHT>(~Us)
                            | pos.attacks_by<BISHOP>(~Us) | pos.attacks_by<ROOK>(~Us)
                            | pos.attacks_by<QUEEN>(~Us) | attacks_bb<KING>(pos.king_square(~Us));

        Bitboard b = attacks_bb<KING>(ksq) & target & ~threatened;
        if (Checks)
            b &= ~attacks_bb<QUEEN>(pos.king_square(~Us));

        while (b)
            *moves++ = Move(ksq, pop_lsb(b));

        if constexpr (GT == QUIETS || GT == NON_EVASIONS)
            if (pos.can_castle(Us & ANY_CASTLING))
                for (CastlingRights cr : {Us & KING_SIDE, Us & QUEEN_SIDE})
                    if (pos.can_castle(cr) && !pos.castling_impeded(cr))
                    {
                        assert(!pos.checkers());
                        assert(is_ok(pos.castling_rook_square(cr)));
                        assert(pos.piece_on(pos.castling_rook_square(cr)) == make_piece(Us, ROOK));
                        *moves++ = Move::make<CASTLING>(ksq, pos.castling_rook_square(cr));
                    }
    }

    return moves;
}

}  // namespace


// <CAPTURES>     Generates all pseudo-legal captures plus queen promotions moves
// <QUIETS>       Generates all pseudo-legal non-captures, castling and underpromotions moves
// <QUIET_CHECKS> Generates all pseudo-legal non-captures giving check moves,
//                except castling and promotions moves
// <EVASIONS>     Generates all pseudo-legal check evasions moves
// <NON_EVASIONS> Generates all pseudo-legal captures and non-captures moves
//
// Returns a pointer to the end of the move list.
template<GenType GT>
ExtMove* generate(const Position& pos, ExtMove* moves) noexcept {
    static_assert(GT == CAPTURES || GT == QUIETS || GT == QUIET_CHECKS || GT == EVASIONS
                    || GT == NON_EVASIONS,
                  "Unsupported generate type in generate()");

    assert((GT == EVASIONS) == bool(pos.checkers()));

    switch (pos.side_to_move())
    {
    case WHITE :
        return generate_all<WHITE, GT>(pos, moves);
    case BLACK :
        return generate_all<BLACK, GT>(pos, moves);
    default :
        return moves;
    }
}

// Explicit template instantiations
template ExtMove* generate<CAPTURES>(const Position& pos, ExtMove* moves) noexcept;
template ExtMove* generate<QUIETS>(const Position& pos, ExtMove* moves) noexcept;
template ExtMove* generate<QUIET_CHECKS>(const Position& pos, ExtMove* moves) noexcept;
template ExtMove* generate<EVASIONS>(const Position& pos, ExtMove* moves) noexcept;
template ExtMove* generate<NON_EVASIONS>(const Position& pos, ExtMove* moves) noexcept;

// <LEGAL> Generates all legal moves
template<>
ExtMove* generate<LEGAL>(const Position& pos, ExtMove* moves) noexcept {
    Color    stm    = pos.side_to_move();
    Bitboard pinned = pos.blockers(stm) & pos.pieces(stm);
    Square   ksq    = pos.king_square(stm);

    ExtMove* cur = moves;
    moves        = pos.checkers()  //
                   ? generate<EVASIONS>(pos, moves)
                   : generate<NON_EVASIONS>(pos, moves);
    while (cur != moves)
        if (((pinned & cur->org_sq()) || cur->org_sq() == ksq || cur->type_of() == EN_PASSANT)
            && !pos.legal(*cur))
            *cur = *(--moves);
        else
            ++cur;

    return moves;
}

}  // namespace DON
