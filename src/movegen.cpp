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

template<GenType GT, Direction D, bool Enemy>
void make_promotions(ExtMoves& extMoves, [[maybe_unused]] Square dst) noexcept {

    if constexpr (GT == CAPTURES || GT == EVASIONS || GT == NON_EVASIONS)
        extMoves += Move::make<PROMOTION>(dst - D, dst, QUEEN);

    if constexpr ((GT == CAPTURES && Enemy) || (GT == QUIETS && !Enemy)  //
                  || GT == EVASIONS || GT == NON_EVASIONS)
        for (PieceType promo : {ROOK, BISHOP, KNIGHT})
            extMoves += Move::make<PROMOTION>(dst - D, dst, promo);
}

template<Color Stm, GenType GT>
void generate_pawn_moves(ExtMoves& extMoves, const Position& pos, Bitboard target) noexcept {

    constexpr Direction Up_1 = pawn_spush(Stm);
    constexpr Direction Up_2 = pawn_dpush(Stm);
    constexpr Direction Up_R = (Stm == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction Up_L = (Stm == WHITE ? NORTH_WEST : SOUTH_EAST);

    Bitboard empties = ~pos.pieces();
    Bitboard enemies = GT == EVASIONS ? pos.checkers() : pos.pieces(~Stm);

    Bitboard allPawns  = pos.pieces(Stm, PAWN);
    Bitboard on7Pawns  = allPawns & (Stm == WHITE ? Rank7BB : Rank2BB);
    Bitboard non7Pawns = allPawns & ~on7Pawns;

    // Single and double pawn pushes, no promotions
    if constexpr (GT != CAPTURES)
    {
        Bitboard b1 = shift<Up_1>(non7Pawns) & empties;
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

            Bitboard dcPawns = pos.blockers(~Stm) & ~file_bb(ksq);
            b1 &= pawn_attacks_bb(~Stm, ksq) | shift<Up_1>(dcPawns);
            b2 &= pawn_attacks_bb(~Stm, ksq) | shift<Up_2>(dcPawns);
        }

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

    // Promotions and under-promotions
    if (on7Pawns)
    {
        Bitboard b1 = shift<Up_R>(on7Pawns) & enemies;
        Bitboard b2 = shift<Up_L>(on7Pawns) & enemies;
        Bitboard b3 = shift<Up_1>(on7Pawns) & empties;

        if constexpr (GT == EVASIONS)
            b3 &= target;

        while (b1)
            make_promotions<GT, Up_R, true>(extMoves, pop_lsb(b1));

        while (b2)
            make_promotions<GT, Up_L, true>(extMoves, pop_lsb(b2));

        while (b3)
            make_promotions<GT, Up_1, false>(extMoves, pop_lsb(b3));
    }

    // Standard and en-passant captures
    if constexpr (GT == CAPTURES || GT == EVASIONS || GT == NON_EVASIONS)
    {
        Bitboard b1 = shift<Up_R>(non7Pawns) & enemies;
        Bitboard b2 = shift<Up_L>(non7Pawns) & enemies;

        while (b1)
        {
            Square dst = pop_lsb(b1);
            extMoves += Move(dst - Up_R, dst);
        }

        while (b2)
        {
            Square dst = pop_lsb(b2);
            extMoves += Move(dst - Up_L, dst);
        }

        Bitboard on5Pawns = non7Pawns & (Stm == WHITE ? Rank5BB : Rank4BB);
        if (on5Pawns && is_ok_ep(pos.ep_square()))
        {
            assert(relative_rank(Stm, pos.ep_square()) == RANK_6);
            assert(pos.rule50_count() == 0);

            // An en-passant capture cannot resolve a discovered check
            if constexpr (GT == EVASIONS)
                if (target & (pos.ep_square() + Up_1))
                    return;

            b1 = on5Pawns & pawn_attacks_bb(~Stm, pos.ep_square());
            if (Bitboard b; more_than_one(b1) && (b = pos.blockers(Stm) & b1))
            {
                assert(!more_than_one(b));
                Square psq = lsb(b);
                if (!aligned(psq, pos.king_square(Stm), pos.ep_square()))
                    b1 ^= psq;
            }
            assert(b1);

            while (b1)
                extMoves += Move::make<EN_PASSANT>(pop_lsb(b1), pos.ep_square());
        }
    }
}

template<Color Stm, PieceType PT, bool Checks>
void generate_moves(ExtMoves& extMoves, const Position& pos, Bitboard target) noexcept {
    static_assert(PT == KNIGHT || PT == BISHOP || PT == ROOK || PT == QUEEN,
                  "Unsupported piece type in generate_moves()");

    Square ksq = pos.king_square(Stm);

    Bitboard occupied = pos.pieces();
    Bitboard pc       = pos.pieces<PT>(Stm, ksq);
    while (pc)
    {
        Square org = pop_lsb(pc);

        Bitboard b = attacks_bb<PT>(org, occupied) & target;
        if constexpr (PT != KNIGHT)
            if (b && (pos.blockers(Stm) & org))
                b &= line_bb(ksq, org);
        // To check, you either move freely a blocker or make a direct check.
        if constexpr (Checks)
            if (b && (PT == QUEEN || !(pos.blockers(~Stm) & org)))
                b &= pos.checks(PT);

        while (b)
            extMoves += Move(org, pop_lsb(b));
    }
}

template<Color Stm, GenType GT>
void generate_all(ExtMoves& extMoves, const Position& pos) noexcept {
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

        generate_pawn_moves<Stm, GT>(extMoves, pos, target);
        generate_moves<Stm, KNIGHT, Checks>(extMoves, pos, target);
        generate_moves<Stm, BISHOP, Checks>(extMoves, pos, target);
        generate_moves<Stm, ROOK, Checks>(extMoves, pos, target);
        generate_moves<Stm, QUEEN, Checks>(extMoves, pos, target);
    }

    if (!Checks || pos.blockers(~Stm) & ksq)
    {
        if constexpr (GT == EVASIONS)
        {
            target = ~pos.pieces(Stm);

            Bitboard sliders = pos.checkers() & ~pos.pieces(PAWN, KNIGHT);
            while (sliders)
            {
                Square slider = pop_lsb(sliders);
                target &= ~line_bb(ksq, slider) | slider;
            }
        }

        Bitboard b = attacks_bb<KING>(ksq) & target;
        // Stop king from stepping in the way to check
        if (Checks)
            b &= ~attacks_bb<QUEEN>(pos.king_square(~Stm));

        while (b)
            extMoves += Move(ksq, pop_lsb(b));

        if constexpr (GT == QUIETS || GT == NON_EVASIONS)
            if (pos.can_castle(Stm & ANY_CASTLING))
                for (CastlingRights cr : {Stm & KING_SIDE, Stm & QUEEN_SIDE})
                    if (pos.can_castle(cr) && !pos.castling_impeded(cr))
                    {
                        assert(!pos.checkers());
                        assert(is_ok(pos.castling_rook_square(cr)));
                        assert(pos.pieces(Stm, ROOK) & pos.castling_rook_square(cr));
                        extMoves += Move::make<CASTLING>(ksq, pos.castling_rook_square(cr));
                    }
    }
}

}  // namespace

// <CAPTURES>     Generates all pseudo-legal captures plus queen promotions moves
// <QUIETS>       Generates all pseudo-legal non-captures, castling and underpromotions moves
// <QUIET_CHECKS> Generates all pseudo-legal non-captures giving check moves,
//                except castling and promotions moves
// <EVASIONS>     Generates all pseudo-legal check evasions moves
// <NON_EVASIONS> Generates all pseudo-legal captures and non-captures moves
template<GenType GT>
ExtMoves::NormalItr generate(ExtMoves& extMoves, const Position& pos) noexcept {
    static_assert(GT == CAPTURES || GT == QUIETS || GT == QUIET_CHECKS || GT == EVASIONS
                    || GT == NON_EVASIONS,
                  "Unsupported generate type in generate()");

    assert((GT == EVASIONS) == bool(pos.checkers()));

    switch (GT)
    {
    case CAPTURES :
        extMoves.reserve(32);
        break;
    case QUIETS :
        extMoves.reserve(48);
        break;
    case QUIET_CHECKS :
        extMoves.reserve(16);
        break;
    default :
        extMoves.reserve(64 - 48 * (pos.checkers() != 0));
    }
    pos.side_to_move() == WHITE ? generate_all<WHITE, GT>(extMoves, pos)
                                : generate_all<BLACK, GT>(extMoves, pos);
    return extMoves.end();
}

// Explicit template instantiations
// clang-format off
template ExtMoves::NormalItr generate<CAPTURES>(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::NormalItr generate<QUIETS>(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::NormalItr generate<QUIET_CHECKS>(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::NormalItr generate<EVASIONS>(ExtMoves& extMoves, const Position& pos) noexcept;
template ExtMoves::NormalItr generate<NON_EVASIONS>(ExtMoves& extMoves, const Position& pos) noexcept;
// clang-format on

// <LEGAL> Generates all legal moves
template<>
ExtMoves::NormalItr generate<LEGAL>(ExtMoves& extMoves, const Position& pos) noexcept {

    pos.checkers() != 0  //
      ? generate<EVASIONS>(extMoves, pos)
      : generate<NON_EVASIONS>(extMoves, pos);
    return filter_illegal(extMoves, pos);
}

// Filter illegal moves
ExtMoves::NormalItr filter_illegal(ExtMoves& extMoves, const Position& pos) noexcept {
    Color    stm    = pos.side_to_move();
    Bitboard pinned = pos.blockers(stm) & pos.pieces(stm);
    Square   ksq    = pos.king_square(stm);

    return extMoves.remove_if([&](const ExtMove& em) noexcept -> bool {
        return ((pinned & em.org_sq()) || em.org_sq() == ksq || em.type_of() == EN_PASSANT)
            && !pos.legal(em);
    });
}

}  // namespace DON
