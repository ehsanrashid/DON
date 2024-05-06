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

template<GenType GT, Direction D, bool Enemy>
ExtMove* make_promotions(ExtMove* moves, [[maybe_unused]] Square dst) noexcept {

    if constexpr (GT == CAPTURES || GT == EVASIONS || GT == NON_EVASIONS)
        *moves++ = Move::make<PROMOTION>(dst - D, dst, QUEEN);

    if constexpr ((GT == CAPTURES && Enemy) || (GT == QUIETS && !Enemy)  //
                  || GT == EVASIONS || GT == NON_EVASIONS)
        for (PieceType promo : {ROOK, BISHOP, KNIGHT})
            *moves++ = Move::make<PROMOTION>(dst - D, dst, promo);

    return moves;
}

template<Color Stm, GenType GT>
ExtMove* generate_pawn_moves(const Position& pos, ExtMove* moves, Bitboard target) noexcept {

    constexpr Direction Up_1 = pawn_spush(Stm);
    constexpr Direction Up_2 = pawn_dpush(Stm);
    constexpr Direction Up_R = (Stm == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction Up_L = (Stm == WHITE ? NORTH_WEST : SOUTH_EAST);

    Bitboard empties = ~pos.pieces();
    Bitboard enemies = GT == EVASIONS ? pos.checkers() : pos.pieces(~Stm);

    Bitboard on7Pawns    = pos.pieces(Stm, PAWN) & (Stm == WHITE ? Rank7BB : Rank2BB);
    Bitboard notOn7Pawns = pos.pieces(Stm, PAWN) & ~on7Pawns;

    // Single and double pawn pushes, no promotions
    if constexpr (GT != CAPTURES)
    {
        Bitboard b1 = shift<Up_1>(notOn7Pawns) & empties;
        Bitboard b2 = shift<Up_1>(b1 & (Stm == WHITE ? Rank3BB : Rank6BB)) & empties;

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
            Square ksq = pos.king_square(~Stm);

            Bitboard dcCandidatePawns = pos.blockers(~Stm) & ~file_bb(ksq);
            b1 &= pawn_attacks_bb(~Stm, ksq) | shift<Up_1>(dcCandidatePawns);
            b2 &= pawn_attacks_bb(~Stm, ksq) | shift<Up_2>(dcCandidatePawns);
        }

        while (b1)
        {
            Square dst = pop_lsb(b1);
            *moves++   = Move(dst - Up_1, dst);
        }

        while (b2)
        {
            Square dst = pop_lsb(b2);
            *moves++   = Move(dst - Up_2, dst);
        }
    }

    // Promotions and under-promotions
    if (on7Pawns)
    {
        Bitboard b1 = shift<Up_R>(on7Pawns) & enemies;
        Bitboard b2 = shift<Up_L>(on7Pawns) & enemies;
        Bitboard b3 = shift<Up_1>(on7Pawns) & empties;

        if constexpr (GT == EVASIONS)
            b3 &= target;

        while (b1)
            moves = make_promotions<GT, Up_R, true>(moves, pop_lsb(b1));

        while (b2)
            moves = make_promotions<GT, Up_L, true>(moves, pop_lsb(b2));

        while (b3)
            moves = make_promotions<GT, Up_1, false>(moves, pop_lsb(b3));
    }

    // Standard and en-passant captures
    if constexpr (GT == CAPTURES || GT == EVASIONS || GT == NON_EVASIONS)
    {
        Bitboard b1 = shift<Up_R>(notOn7Pawns) & enemies;
        Bitboard b2 = shift<Up_L>(notOn7Pawns) & enemies;

        while (b1)
        {
            Square dst = pop_lsb(b1);
            *moves++   = Move(dst - Up_R, dst);
        }

        while (b2)
        {
            Square dst = pop_lsb(b2);
            *moves++   = Move(dst - Up_L, dst);
        }

        Bitboard on5Pawns = notOn7Pawns & (Stm == WHITE ? Rank5BB : Rank4BB);
        if (on5Pawns && is_ok_ep(pos.ep_square()))
        {
            assert(relative_rank(Stm, pos.ep_square()) == RANK_6);
            assert(pos.rule50_count() == 0);

            // An en-passant capture cannot resolve a discovered check
            if constexpr (GT == EVASIONS)
                if (target & (pos.ep_square() + Up_1))
                    return moves;

            b1 = on5Pawns & pawn_attacks_bb(~Stm, pos.ep_square());
            assert(b1);

            while (b1)
                *moves++ = Move::make<EN_PASSANT>(pop_lsb(b1), pos.ep_square());
        }
    }

    return moves;
}

template<Color Stm, PieceType PT, bool Checks>
ExtMove* generate_moves(const Position& pos, ExtMove* moves, Bitboard target) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_moves()");

    Square ksq = pos.king_square(Stm);

    Bitboard pc = pos.pieces(Stm, PT);
    // clang-format off
    switch (PT)
    {
    case KNIGHT : pc &= ~pos.blockers(Stm); break;
    case BISHOP : pc &= ~pos.blockers(Stm) | attacks_bb<BISHOP>(ksq); break;
    case ROOK :   pc &= ~pos.blockers(Stm) | attacks_bb<ROOK>(ksq); break;
    default :;
    }
    // clang-format on
    while (pc)
    {
        Square org = pop_lsb(pc);

        Bitboard b = attacks_bb<PT>(org, pos.pieces()) & target;
        if constexpr (PT != KNIGHT)
            if (b && (pos.blockers(Stm) & org))
                b &= line_bb(ksq, org);
        // To check, you either move freely a blocker or make a direct check.
        if constexpr (Checks)
            if (b && (PT == QUEEN || !(pos.blockers(~Stm) & org)))
                b &= pos.checks(PT);

        while (b)
            *moves++ = Move(org, pop_lsb(b));
    }

    return moves;
}

template<Color Stm, GenType GT>
ExtMove* generate_all(const Position& pos, ExtMove* moves) noexcept {
    static_assert(GT == CAPTURES || GT == QUIETS || GT == QUIET_CHECKS || GT == EVASIONS
                    || GT == NON_EVASIONS,
                  "Unsupported generate type in generate_all()");

    constexpr bool Checks = GT == QUIET_CHECKS;  // Reduce template instantiations

    Square ksq = pos.king_square(Stm);

    Bitboard target;
    // Skip generating non-king moves when in double check
    if (GT != EVASIONS || !more_than_one(pos.checkers()))
    {
        target = GT == CAPTURES     ? pos.pieces(~Stm)
               : GT == EVASIONS     ? between_bb(ksq, lsb(pos.checkers()))
               : GT == NON_EVASIONS ? ~pos.pieces(Stm)
                                    : ~pos.pieces();  // QUIETS || QUIET_CHECKS

        moves = generate_pawn_moves<Stm, GT>(pos, moves, target);
        moves = generate_moves<Stm, KNIGHT, Checks>(pos, moves, target);
        moves = generate_moves<Stm, BISHOP, Checks>(pos, moves, target);
        moves = generate_moves<Stm, ROOK, Checks>(pos, moves, target);
        moves = generate_moves<Stm, QUEEN, Checks>(pos, moves, target);
    }

    if (!Checks || pos.blockers(~Stm) & ksq)
    {
        if constexpr (GT == EVASIONS)
        {
            target = ~pos.pieces(Stm);

            Bitboard exCheckers = pos.checkers() & ~pos.pieces(PAWN, KNIGHT);
            while (exCheckers)
            {
                Square exChecker = pop_lsb(exCheckers);
                target &= ~line_bb(ksq, exChecker) | exChecker;
            }
        }

        Bitboard b = attacks_bb<KING>(ksq) & target;  // & ~pos.attacks_by(~Stm);
        if (Checks)
            b &= ~attacks_bb<QUEEN>(pos.king_square(~Stm));

        while (b)
            *moves++ = Move(ksq, pop_lsb(b));

        if constexpr (GT == QUIETS || GT == NON_EVASIONS)
            if (pos.can_castle(Stm & ANY_CASTLING))
                for (CastlingRights cr : {Stm & KING_SIDE, Stm & QUEEN_SIDE})
                    if (pos.can_castle(cr) && !pos.castling_impeded(cr))
                    {
                        assert(!pos.checkers());
                        assert(is_ok(pos.castling_rook_square(cr)));
                        assert(pos.pieces(Stm, ROOK) & pos.castling_rook_square(cr));
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

    return pos.side_to_move() == WHITE ? generate_all<WHITE, GT>(pos, moves)
                                       : generate_all<BLACK, GT>(pos, moves);
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
    {
        if (((pinned & cur->org_sq()) || cur->org_sq() == ksq || cur->type_of() == EN_PASSANT)
            && !pos.legal(*cur))
        {
            *cur = *(--moves);
            continue;
        }
        ++cur;
    }
    return moves;
}

}  // namespace DON
