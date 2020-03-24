#include "MoveGenerator.h"

#include <iostream>
#include <sstream>

#include "BitBoard.h"
#include "Notation.h"

namespace {

    /// Generates piece normal move
    template<bool Checks>
    void generatePieceMoves(ValMoves &moves, Position const &pos, Bitboard targets) {
        auto activeSide{ pos.activeSide() };

        for (PieceType pt = NIHT; pt <= QUEN; ++pt) {
            for (Square s : pos.squares(activeSide|pt)) {

                if (Checks
                 && contains(pos.kingBlockers(~activeSide), s)) {
                    continue;
                }

                Bitboard attacks{ pos.attacksFrom(pt, s)
                                & targets };

                if (Checks) {
                    attacks &= pos.checks(pt);
                }

                while (attacks != 0) { moves += makeMove<NORMAL>(s, popLSq(attacks)); }
            }
        }
    }

    /// Generates pawn promotion move
    template<GenType GT>
    void generatePromotionMoves(ValMoves &moves, Position const &pos, Bitboard promotion, Direction dir) {
        while (promotion != 0) {
            auto dst{ popLSq(promotion) };
            auto org{ dst - dir };

            if (GT == GenType::CAPTURE
             || GT == GenType::NATURAL
             || GT == GenType::EVASION) {
                moves += makePromoteMove(org, dst, QUEN);
            }
            if (GT == GenType::QUIET
             || GT == GenType::NATURAL
             || GT == GenType::EVASION) {
                moves += makePromoteMove(org, dst, ROOK);
                moves += makePromoteMove(org, dst, BSHP);
                moves += makePromoteMove(org, dst, NIHT);
            }

            if (GT == GenType::QUIET_CHECK) {
                auto ekSq{ pos.square(~pos.activeSide()|KING) };

                if (contains(PieceAttackBB[NIHT][dst], ekSq)) {
                    moves += makePromoteMove(org, dst, NIHT);
                }
                /*
                Bitboard mocc{ pos.pieces() ^ org };

                Bitboard rookAttacks{ attacksBB<ROOK>(dst, mocc) };
                if (contains(rookAttacks, ekSq)) {
                    moves += makePromoteMove(org, dst, ROOK);
                }

                Bitboard bshpAttacks{ attacksBB<BSHP>(dst, mocc) };
                if (contains(bshpAttacks, ekSq)) {
                    moves += makePromoteMove(org, dst, BSHP);
                }
                */
            }
        }
    }
    /// Generates pawn normal move
    template<GenType GT, Color Own>
    void generatePawnMoves(ValMoves &moves, Position const &pos, Bitboard targets) {
        constexpr auto Opp{ ~Own };
        constexpr auto Push1{ PawnPush[Own] };
        constexpr auto Push2{ Push1 + Push1 };
        constexpr auto LAtt{ PawnLAtt[Own] };
        constexpr auto RAtt{ PawnRAtt[Own] };

        constexpr Bitboard Rank3{ RankBB[relativeRank(Own, RANK_3)] };
        constexpr Bitboard Rank7{ RankBB[relativeRank(Own, RANK_7)] };

        Bitboard empties{ ~pos.pieces() };
        Bitboard enemies{ pos.pieces(Opp) & targets };

        Bitboard pawns{ pos.pieces(Own, PAWN) };

        Bitboard r7Pawns{ pawns &  Rank7 }; // Pawns on 7th Rank only
        Bitboard rxPawns{ pawns & ~Rank7 }; // Pawns not on 7th Rank

        // Pawn single-push and double-push, no promotions
        if (GT != GenType::CAPTURE) {

            Bitboard pushs1{ empties & pawnSglPushBB<Own>(rxPawns) };
            Bitboard pushs2{ empties & pawnSglPushBB<Own>(pushs1 & Rank3) };

            if (GT == GenType::EVASION) {
                // Only blocking squares
                pushs1 &= targets;
                pushs2 &= targets;
            }
            if (GT == GenType::QUIET_CHECK) {
                // Only checking squares
                pushs1 &= pos.checks(PAWN);
                pushs2 &= pos.checks(PAWN);
                // Pawns which give discovered check
                // Add pawn pushes which give discovered check.
                // This is possible only if the pawn is not on the same file as the enemy king, because don't generate captures.
                // Note that a possible discovery check promotion has been already generated among captures.
                Bitboard dscPawns{ rxPawns
                                 & pos.kingBlockers(Opp)
                                 & ~fileBB(pos.square(Opp|KING)) };
                if (dscPawns != 0) {
                    Bitboard dscPushs1{ empties & pawnSglPushBB<Own>(dscPawns) };
                    Bitboard dscPushs2{ empties & pawnSglPushBB<Own>(dscPushs1 & Rank3) };
                    pushs1 |= dscPushs1;
                    pushs2 |= dscPushs2;
                }
            }

            while (pushs1 != 0) { auto dst{ popLSq(pushs1) }; moves += makeMove<NORMAL>(dst - Push1, dst); }
            while (pushs2 != 0) { auto dst{ popLSq(pushs2) }; moves += makeMove<NORMAL>(dst - Push2, dst); }
        }

        // Promotions (queening and under-promotions)
        if (r7Pawns != 0) {
            Bitboard b;

            b = enemies & pawnLAttackBB<Own>(r7Pawns);
            generatePromotionMoves<GT>(moves, pos, b, LAtt);

            b = enemies & pawnRAttackBB<Own>(r7Pawns);
            generatePromotionMoves<GT>(moves, pos, b, RAtt);

            b = empties & pawnSglPushBB<Own>(r7Pawns);
            if (GT == GenType::EVASION) {
                b &= targets;
            }
            generatePromotionMoves<GT>(moves, pos, b, Push1);
        }

        // Pawn normal and en-passant captures, no promotions
        if (GT == GenType::CAPTURE
         || GT == GenType::EVASION
         || GT == GenType::NATURAL) {

            Bitboard attacksL{ enemies & pawnLAttackBB<Own>(rxPawns) };
            Bitboard attacksR{ enemies & pawnRAttackBB<Own>(rxPawns) };
            while (attacksL != 0) { auto dst{ popLSq(attacksL) }; moves += makeMove<NORMAL>(dst - LAtt, dst); }
            while (attacksR != 0) { auto dst{ popLSq(attacksR) }; moves += makeMove<NORMAL>(dst - RAtt, dst); }

            auto epSq = pos.epSquare();
            if (epSq != SQ_NONE) {
                assert(relativeRank(Own, epSq) == RANK_6);
                Bitboard epPawns{ rxPawns
                                & PawnAttackBB[Opp][epSq] };

                // If the checking piece is the double pushed pawn and also is in the target.
                // Otherwise this is a discovery check and are forced to do otherwise.
                if (GT == GenType::EVASION
                 && !contains(enemies /*& pos.pieces(PAWN)*/, epSq - Push1)) {
                    epPawns = 0;
                }
                assert(popCount(epPawns) <= 2);
                while (epPawns != 0) { moves += makeMove<ENPASSANT>(popLSq(epPawns), epSq); }
            }
        }
    }

    /// Generates king normal move
    template<GenType GT>
    void generateKingMoves(ValMoves &moves, Position const &pos, Bitboard targets) {
        assert(pos.checkers() == 0);

        auto activeSide{ pos.activeSide() };
        auto fkSq{ pos.square( activeSide|KING) };
        auto ekSq{ pos.square(~activeSide|KING) };
        Bitboard attacks{  PieceAttackBB[KING][fkSq]
                        &  targets
                        & ~PieceAttackBB[KING][ekSq] };
        while (attacks != 0) { moves += makeMove<NORMAL>(fkSq, popLSq(attacks)); }

        if (GT == GenType::NATURAL
         || GT == GenType::QUIET) {
            if (pos.canCastle(activeSide)) {
                for (CastleSide cs : { CS_KING, CS_QUEN }) {
                    if (pos.castleExpeded(activeSide, cs)
                     && pos.canCastle(activeSide, cs)) {
                        moves += makeMove<CASTLE>(fkSq, pos.castleRookSq(activeSide, cs));
                    }
                }
            }
        }
    }


    /// Generates all pseudo-legal moves of color for targets.
    template<GenType GT>
    void generateMoves(ValMoves &moves, Position const &pos, Bitboard targets) {

        constexpr bool Checks{ GT == GenType::QUIET_CHECK };

        pos.activeSide() == WHITE ?
            generatePawnMoves<GT, WHITE>(moves, pos, targets) :
            generatePawnMoves<GT, BLACK>(moves, pos, targets);

        generatePieceMoves<Checks>(moves, pos, targets);
    }
}

template<GenType GT>
void generate(ValMoves &moves, Position const &pos) {
    static_assert (GT == GenType::NATURAL
                || GT == GenType::CAPTURE
                || GT == GenType::QUIET, "GT incorrect");
    assert(pos.checkers() == 0);

    Bitboard targets =
        GT == GenType::NATURAL ? ~pos.pieces( pos.activeSide()) :
        GT == GenType::CAPTURE ?  pos.pieces(~pos.activeSide()) :
        GT == GenType::QUIET   ? ~pos.pieces() : 0;

    generateMoves<GT>(moves, pos, targets);
    generateKingMoves<GT>(moves, pos, targets);
}

/// Explicit template instantiations
/// --------------------------------
/// generate<NATURAL>     Generates all pseudo-legal captures and non-captures.
template void generate<GenType::NATURAL>(ValMoves&, Position const&);
/// generate<CAPTURE>     Generates all pseudo-legal captures and queen promotions.
template void generate<GenType::CAPTURE>(ValMoves&, Position const&);
/// generate<QUIET>       Generates all pseudo-legal non-captures and underpromotions.
template void generate<GenType::QUIET>(ValMoves&, Position const&);

/// generate<EVASION>     Generates all pseudo-legal check evasions moves.
template<> void generate<GenType::EVASION>(ValMoves &moves, Position const &pos) {
    Bitboard checkers{ pos.checkers() };
    assert(checkers != 0
        && popCount(checkers) <= 2);

    auto activeSide{ pos.activeSide() };
    auto fkSq{ pos.square(activeSide|KING) };
    // Double-check, only king move can save the day
    if (!moreThanOne(checkers)) {

        // Generates blocking or captures of the checking piece
        auto checkSq{ scanLSq(checkers) };
        Bitboard targets{ betweenBB(checkSq, fkSq) | checkSq };

        generateMoves<GenType::EVASION>(moves, pos, targets);
    }

    Bitboard checkAttacks{ PieceAttackBB[KING][pos.square(~activeSide|KING)] };
    Bitboard checkersEx{  checkers
                       & ~pos.pieces(PAWN) };
    Bitboard mocc{ pos.pieces() ^ fkSq };
    // Squares attacked by slide checkers will remove them from the king evasions
    // so to skip known illegal moves avoiding useless legality check later.
    while (checkersEx != 0) {
        checkAttacks |= pos.attacksFrom(popLSq(checkersEx), mocc);
    }
    // Generate evasions for king, capture and non-capture moves
    Bitboard attacks{  PieceAttackBB[KING][fkSq]
                    & ~checkAttacks
                    & ~pos.pieces(activeSide) };
    while (attacks != 0) { moves += makeMove<NORMAL>(fkSq, popLSq(attacks)); }
}

/// generate<QUIET_CHECK> Generates all pseudo-legal non-captures and knight under promotions check giving moves.
template<> void generate<GenType::QUIET_CHECK>(ValMoves &moves, Position const &pos) {
    assert(pos.checkers() == 0);

    auto activeSide{ pos.activeSide() };
    Bitboard targets{ ~pos.pieces() };

    auto fkSq{ pos.square(activeSide|KING) };
    // Pawns is excluded, already generated with direct checks
    Bitboard dscBlockersEx{  pos.pieces(activeSide)
                          &  pos.kingBlockers(~activeSide)
                          & ~pos.pieces(PAWN) };
    assert((dscBlockersEx & pos.pieces(QUEN)) == 0);
    while (dscBlockersEx != 0) {
        auto org{ popLSq(dscBlockersEx) };

        Bitboard attacks{ pos.attacksFrom(org)
                        & targets };
        if (org == fkSq) {
            // Stop king from stepping in the way to check
            attacks &= ~PieceAttackBB[QUEN][pos.square(~activeSide|KING)];
        }

        while (attacks != 0) { moves += makeMove<NORMAL>(org, popLSq(attacks)); }
    }

    generateMoves<GenType::QUIET_CHECK>(moves, pos, targets);
}

/// generate<LEGAL>       Generates all legal moves.
template<> void generate<GenType::LEGAL>(ValMoves &moves, Position const &pos) {

    moves.reserve(64 - 48 * (pos.checkers() != 0));

    pos.checkers() == 0 ?
        generate<GenType::NATURAL>(moves, pos) :
        generate<GenType::EVASION>(moves, pos);

    auto activeSide{ pos.activeSide() };
    auto fkSq{ pos.square(activeSide|KING) };
    Bitboard mocc{ pos.pieces() ^ fkSq };
    Bitboard enemies{ pos.pieces(~activeSide) };
    Bitboard pinneds{ pos.pieces(activeSide)
                    & pos.kingBlockers(activeSide) };

    // Filter illegal moves
    moves.erase(
        std::remove_if(
            moves.begin(), moves.end(),
            [&](ValMove const &vm) {
                return (mType(vm) == NORMAL
                     && orgSq(vm) == fkSq
                     && (pos.attackersTo(dstSq(vm), mocc) & enemies) != 0)
                    || ((contains(pinneds, orgSq(vm))
                      || mType(vm) == CASTLE
                      || mType(vm) == ENPASSANT)
                     && !pos.legal(vm));
            }),
        moves.end());
}

void Perft::operator+=(Perft const &perft) {
    any       += perft.any;
    capture   += perft.capture;
    enpassant += perft.enpassant;
    anyCheck  += perft.anyCheck;
    dscCheck  += perft.dscCheck;
    dblCheck  += perft.dblCheck;
    castle    += perft.castle;
    promotion += perft.promotion;
    checkmate += perft.checkmate;
    //stalemate += perft.stalemate;
}
void Perft::operator-=(Perft const &perft) {
    any       -= perft.any;
    capture   -= perft.capture;
    enpassant -= perft.enpassant;
    anyCheck  -= perft.anyCheck;
    dscCheck  -= perft.dscCheck;
    dblCheck  -= perft.dblCheck;
    castle    -= perft.castle;
    promotion -= perft.promotion;
    checkmate -= perft.checkmate;
    //stalemate -= perft.stalemate;
}

void Perft::classify(Position &pos, Move m) {

    auto activeSide{ pos.activeSide() };

    if (mType(m) == ENPASSANT
     || contains(pos.pieces(~activeSide), dstSq(m))) {
        ++capture;
        if (mType(m) == ENPASSANT) {
            ++enpassant;
        }
    }
    if (pos.giveCheck(m)) {
        ++anyCheck;
        // Discovered Check but not Direct Check
        if (!contains(pos.checks(mType(m) != PROMOTE ? pType(pos[orgSq(m)]) : promoteType(m)), dstSq(m))) {
            auto ekSq{ pos.square(~activeSide|KING) };
            if (mType(m) == ENPASSANT) {
                Bitboard mocc{ (pos.pieces() ^ orgSq(m) ^ makeSquare(sFile(dstSq(m)), sRank(orgSq(m)))) | dstSq(m) };
                if ((pos.pieces(activeSide, BSHP, QUEN)
                   & attacksBB<BSHP>(ekSq, mocc)) != 0
                 || (pos.pieces(activeSide, ROOK, QUEN)
                   & attacksBB<ROOK>(ekSq, mocc)) != 0) {
                    ++dscCheck;
                }
            }
            else
            if (contains(pos.kingBlockers(~activeSide), orgSq(m))
             /*&& !aligned(orgSq(m), dstSq(m), ekSq)*/) {
                ++dscCheck;
            }
        }
        //if (pos.giveDblCheck(m)) {
        //    ++dblCheck;
        //}

        StateInfo si;
        pos.doMove(m, si, true);
        assert(pos.checkers() != 0
            && popCount(pos.checkers()) <= 2);
        if (moreThanOne(pos.checkers())) {
            ++dblCheck;
        }
        if (MoveList<GenType::LEGAL>(pos).size() == 0) {
            ++checkmate;
        }
        pos.undoMove(m);
    }
    //else {
    //    StateInfo si;
    //    pos.doMove(m, si, false);
    //    if (MoveList<GenType::LEGAL>(pos).size() == 0) {
    //        ++stalemate;
    //    }
    //    pos.undoMove(m);
    //}
    if (mType(m) == CASTLE) {
        ++castle;
    }
    if (mType(m) == PROMOTE) {
        ++promotion;
    }
}

/// perft() is utility to verify move generation.
/// All the leaf nodes up to the given depth are generated, and the sum is returned.
template<bool RootNode>
Perft perft(Position &pos, Depth depth, bool detail) {
    Perft sumLeaf;
    if (RootNode) {
        std::ostringstream oss;
        oss << std::left
            << std::setw( 3) << "N"
            << std::setw(10) << "Move"
            << std::setw(19) << "Any";
        if (detail) {
            oss << std::setw(17) << "Capture"
                << std::setw(15) << "Enpassant"
                << std::setw(17) << "AnyCheck"
                << std::setw(15) << "DscCheck"
                << std::setw(15) << "DblCheck"
                << std::setw(15) << "Castle"
                << std::setw(15) << "Promote"
                << std::setw(15) << "Checkmate"
                //<< std::setw(15) << "Stalemate"
                ;
        }
        std::cout << oss.str() << std::endl;
    }
    for (auto const &vm : MoveList<GenType::LEGAL>(pos)) {
        Perft leaf;
        if (RootNode
         && depth <= DEPTH_ONE) {
            ++leaf.any;
            if (detail) {
                leaf.classify(pos, vm);
            }
        }
        else {
            StateInfo si;
            pos.doMove(vm, si);

            if (depth <= 2 * DEPTH_ONE) {
                for (auto &ivm : MoveList<GenType::LEGAL>(pos)) {
                    ++leaf.any;
                    if (detail) {
                        leaf.classify(pos, ivm);
                    }
                }
            }
            else {
                leaf = perft<false>(pos, depth - 1, detail);
            }

            pos.undoMove(vm);
        }
        sumLeaf += leaf;

        if (RootNode) {
            ++sumLeaf.moves;

            std::ostringstream oss;
            oss << std::right << std::setfill('0') << std::setw( 2) << sumLeaf.moves << " "
                << std::left  << std::setfill(' ') << std::setw( 7) << //moveToCAN(vm)
                                                                       moveToSAN(vm, pos)
                << std::right << std::setfill('.') << std::setw(16) << leaf.any;
            if (detail) {
                oss << "   " << std::setw(14) << leaf.capture
                    << "   " << std::setw(12) << leaf.enpassant
                    << "   " << std::setw(14) << leaf.anyCheck
                    << "   " << std::setw(12) << leaf.dscCheck
                    << "   " << std::setw(12) << leaf.dblCheck
                    << "   " << std::setw(12) << leaf.castle
                    << "   " << std::setw(12) << leaf.promotion
                    << "   " << std::setw(12) << leaf.checkmate
                    //<< "   " << std::setw(12) << leaf.stalemate
                    ;
            }
            std::cout << oss.str() << std::endl;
        }
    }
    if (RootNode) {
        std::ostringstream oss;
        oss << "\nTotal:  " << std::right << std::setfill('.')
            << std::setw(18) << sumLeaf.any;
        if (detail) {
            oss << " " << std::setw(16) << sumLeaf.capture
                << " " << std::setw(14) << sumLeaf.enpassant
                << " " << std::setw(16) << sumLeaf.anyCheck
                << " " << std::setw(14) << sumLeaf.dscCheck
                << " " << std::setw(14) << sumLeaf.dblCheck
                << " " << std::setw(14) << sumLeaf.castle
                << " " << std::setw(14) << sumLeaf.promotion
                << " " << std::setw(14) << sumLeaf.checkmate
                //<< " " << std::setw(14) << sumLeaf.stalemate
                ;
        }
        std::cout << oss.str() << std::endl;
    }
    return sumLeaf;
}
/// Explicit template instantiations
/// --------------------------------
template Perft perft<true >(Position&, Depth, bool);
template Perft perft<false>(Position&, Depth, bool);
