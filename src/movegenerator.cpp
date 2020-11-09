#include "movegenerator.h"

#include <iostream>
#include <sstream>

#include "bitboard.h"
#include "notation.h"

namespace {

    /// Generates piece move
    template<bool Checks>
    void generatePieceMoves(ValMoves &moves, Position const &pos, Bitboard targets) noexcept {

        for (PieceType pt = NIHT; pt <= QUEN; ++pt) {
            Square const *ps{ pos.squares(pos.activeSide()|pt) };
            Square s;
            while ((s = *ps++) != SQ_NONE) {
                if (Checks
                 && pos.isKingBlockersOn(~pos.activeSide(), s)) {
                    continue;
                }
                Bitboard attacks{ attacksBB(pt, s, pos.pieces()) & targets };
                if (Checks) {
                    attacks &= pos.checks(pt);
                }
                while (attacks != 0) { moves += makeMove(s, popLSq(attacks)); }
            }
        }
    }

    /// Generates pawn promotion move
    template<GenType GT>
    void generatePromotionMoves(ValMoves &moves, Position const &pos, Bitboard promotions, Direction dir) noexcept {
        while (promotions != 0) {
            auto const dst{ popLSq(promotions) };
            auto const org{ dst - dir };

            if (GT == CAPTURE
             || GT == NORMAL
             || GT == EVASION) {
                moves += makePromoteMove(org, dst, QUEN);
                if (contains(pos.checks(NIHT), dst)) {
                    moves += makePromoteMove(org, dst, NIHT);
                }
            }
            if (GT == QUIET
             || GT == NORMAL
             || GT == EVASION) {
                moves += makePromoteMove(org, dst, ROOK);
                moves += makePromoteMove(org, dst, BSHP);
                if (!contains(pos.checks(NIHT), dst)) {
                    moves += makePromoteMove(org, dst, NIHT);
                }
            }
        }
    }
    /// Generates pawn normal move
    template<GenType GT, Color Own>
    void generatePawnMoves(ValMoves &moves, Position const &pos, Bitboard targets) noexcept {
        constexpr auto Opp{ ~Own };

        Bitboard const empties{ ~pos.pieces() };
        Bitboard const enemies{  pos.pieces(Opp) & targets };

        Bitboard const pawns{ pos.pieces(Own, PAWN) };

        Bitboard const r7Pawns{ pawns &  rankBB(relativeRank(Own, RANK_7)) }; // Pawns on 7th Rank only
        Bitboard const rxPawns{ pawns & ~rankBB(relativeRank(Own, RANK_7)) }; // Pawns not on 7th Rank

        // Pawn single-push and double-push, no promotions
        if (GT != CAPTURE) {

            Bitboard pushs1{ empties & pawnSglPushBB<Own>(rxPawns) };
            Bitboard pushs2{ empties & pawnSglPushBB<Own>(pushs1 & rankBB(relativeRank(Own, RANK_3))) };

            if (GT == EVASION) {
                // Only blocking squares
                pushs1 &= targets;
                pushs2 &= targets;
            }
            if (GT == QUIET_CHECK) {
                // Only checking squares
                pushs1 &= pos.checks(PAWN);
                pushs2 &= pos.checks(PAWN);
                // Pawns which give discovered check
                // Add pawn pushes which give discovered check.
                // This is possible only if the pawn is not on the same file as the enemy king, because don't generate captures.
                // Note that a possible discovery check promotion has been already generated among captures.
                Bitboard const dscPawns{ rxPawns
                                       & pos.kingBlockers(Opp)
                                       & ~fileBB(pos.square(Opp|KING)) };
                if (dscPawns != 0) {
                    Bitboard const dscPushs1{ empties & pawnSglPushBB<Own>(dscPawns) };
                    Bitboard const dscPushs2{ empties & pawnSglPushBB<Own>(dscPushs1 & rankBB(relativeRank(Own, RANK_3))) };
                    pushs1 |= dscPushs1;
                    pushs2 |= dscPushs2;
                }
            }

            while (pushs1 != 0) { auto const dst{ popLSq(pushs1) }; moves += makeMove(dst - PawnPush[Own], dst); }
            while (pushs2 != 0) { auto const dst{ popLSq(pushs2) }; moves += makeMove(dst - PawnPush[Own]*2, dst); }
        }

        // Promotions (queening and under-promotions)
        if (r7Pawns != 0) {
            Bitboard b;

            b = enemies & pawnLAttackBB<Own>(r7Pawns);
            generatePromotionMoves<GT>(moves, pos, b, PawnLAtt[Own]);

            b = enemies & pawnRAttackBB<Own>(r7Pawns);
            generatePromotionMoves<GT>(moves, pos, b, PawnRAtt[Own]);

            b = empties & pawnSglPushBB<Own>(r7Pawns);
            if (GT == EVASION) {
                b &= targets;
            }
            generatePromotionMoves<GT>(moves, pos, b, PawnPush[Own]);
        }

        // Pawn normal and en-passant captures, no promotions
        if (GT != QUIET
         && GT != QUIET_CHECK) {

            Bitboard attacksL{ enemies & pawnLAttackBB<Own>(rxPawns) };
            Bitboard attacksR{ enemies & pawnRAttackBB<Own>(rxPawns) };
            while (attacksL != 0) { auto const dst{ popLSq(attacksL) }; moves += makeMove(dst - PawnLAtt[Own], dst); }
            while (attacksR != 0) { auto const dst{ popLSq(attacksR) }; moves += makeMove(dst - PawnRAtt[Own], dst); }

            if (pos.epSquare() != SQ_NONE) {
                assert(relativeRank(Own, pos.epSquare()) == RANK_6);
                Bitboard epPawns{ rxPawns & pawnAttacksBB(Opp, pos.epSquare()) };

                // If the checking piece is the double pushed pawn and also is in the target.
                // Otherwise this is a discovery check and are forced to do otherwise.
                if (GT == EVASION
                 && !contains(enemies /*& pos.pieces(PAWN)*/, pos.epSquare() - PawnPush[Own])) {
                    epPawns = 0;
                }
                assert(popCount(epPawns) <= 2);
                while (epPawns != 0) { moves += makeMove<ENPASSANT>(popLSq(epPawns), pos.epSquare()); }
            }
        }
    }

    /// Generates king move
    template<GenType GT>
    void generateKingMoves(ValMoves &moves, Position const &pos, Bitboard targets) noexcept {
        assert(pos.checkers() == 0);

        Bitboard attacks{  attacksBB<KING>(pos.square( pos.activeSide()|KING))
                        &  targets
                        & ~attacksBB<KING>(pos.square(~pos.activeSide()|KING)) };
        while (attacks != 0) { moves += makeMove(pos.square(pos.activeSide()|KING), popLSq(attacks)); }

        if (GT == QUIET
         || GT == NORMAL) {
            if (pos.canCastle(pos.activeSide())) {
                for (CastleSide const cs : { CS_KING, CS_QUEN }) {
                    if (pos.castleRookSq(pos.activeSide(), cs) != SQ_NONE
                     && pos.castleExpeded(pos.activeSide(), cs)
                     && pos.canCastle(pos.activeSide(), cs)) {
                        moves += makeMove<CASTLE>(pos.square(pos.activeSide()|KING), pos.castleRookSq(pos.activeSide(), cs));
                    }
                }
            }
        }
    }


    /// Generates all pseudo-legal moves of color for targets.
    template<GenType GT>
    void generateMoves(ValMoves &moves, Position const &pos, Bitboard targets) noexcept {
        constexpr bool Checks{ GT == QUIET_CHECK };

        pos.activeSide() == WHITE ?
            generatePawnMoves<GT, WHITE>(moves, pos, targets) :
            generatePawnMoves<GT, BLACK>(moves, pos, targets);

        generatePieceMoves<Checks>(moves, pos, targets);
    }
}

template<GenType GT>
void generate(ValMoves &moves, Position const &pos) noexcept {
    static_assert(GT == CAPTURE
                || GT == QUIET
                || GT == NORMAL, "GT incorrect");
    assert(pos.checkers() == 0);

    Bitboard const targets{
        GT == CAPTURE ?  pos.pieces(~pos.activeSide()) :
        GT == QUIET   ? ~pos.pieces() :
        GT == NORMAL  ? ~pos.pieces( pos.activeSide()) : 0 };

    generateMoves<GT>(moves, pos, targets);
    generateKingMoves<GT>(moves, pos, targets);
}

/// Explicit template instantiations
/// --------------------------------
/// generate<NORMAL>     Generates all pseudo-legal captures and non-captures.
template void generate<NORMAL>(ValMoves&, Position const&);
/// generate<CAPTURE>     Generates all pseudo-legal captures and queen promotions.
template void generate<CAPTURE>(ValMoves&, Position const&);
/// generate<QUIET>       Generates all pseudo-legal non-captures and underpromotions.
template void generate<QUIET>(ValMoves&, Position const&);

/// generate<EVASION>     Generates all pseudo-legal check evasions moves.
template<> void generate<EVASION>(ValMoves &moves, Position const &pos) noexcept {
    assert(pos.checkers() != 0
        && popCount(pos.checkers()) <= 2);

    // Double-check, only king move can save the day
    if (!moreThanOne(pos.checkers())) {

        // Generates blocking or captures of the checking piece
        auto const checkSq{ scanLSq(pos.checkers()) };
        Bitboard const targets{ betweenBB(checkSq, pos.square(pos.activeSide()|KING)) | checkSq };

        generateMoves<EVASION>(moves, pos, targets);
    }

    Bitboard checkAttacks{ attacksBB<KING>(pos.square(~pos.activeSide()|KING)) };
    Bitboard checkersEx{ pos.checkers() & ~pos.pieces(PAWN) };
    Bitboard const mocc{ pos.pieces() ^ pos.square(pos.activeSide()|KING) };
    // Squares attacked by slide checkers will remove them from the king evasions
    // so to skip known illegal moves avoiding useless legality check later.
    while (checkersEx != 0) {
        auto const sq{ popLSq(checkersEx) };
        checkAttacks |= attacksBB(pType(pos[sq]), sq, mocc);
    }
    // Generate evasions for king, capture and non-capture moves
    Bitboard attacks{  attacksBB<KING>(pos.square(pos.activeSide()|KING))
                    & ~checkAttacks
                    & ~pos.pieces(pos.activeSide()) };
    while (attacks != 0) { moves += makeMove(pos.square(pos.activeSide()|KING), popLSq(attacks)); }
}

/// generate<QUIET_CHECK> Generates all pseudo-legal non-captures and knight under promotions check giving moves.
template<> void generate<QUIET_CHECK>(ValMoves &moves, Position const &pos) noexcept {
    assert(pos.checkers() == 0);

    Bitboard const targets{ ~pos.pieces() };

    // Pawns is excluded, already generated with direct checks
    Bitboard dscBlockersEx{  pos.pieces(pos.activeSide())
                          &  pos.kingBlockers(~pos.activeSide())
                          & ~pos.pieces(PAWN) };
    assert((dscBlockersEx & pos.pieces(QUEN)) == 0);
    while (dscBlockersEx != 0) {
        auto const sq{ popLSq(dscBlockersEx) };
        auto const pt{ pType(pos[sq]) };

        Bitboard attacks{ attacksBB(pt, sq, pos.pieces()) & targets };
        if (pt == KING) {
            // Stop king from stepping in the way to check
            attacks &= ~attacksBB<QUEN>(pos.square(~pos.activeSide()|KING));
        }

        while (attacks != 0) { moves += makeMove(sq, popLSq(attacks)); }
    }

    generateMoves<QUIET_CHECK>(moves, pos, targets);
}

/// generate<LEGAL>       Generates all legal moves.
template<> void generate<LEGAL>(ValMoves &moves, Position const &pos) noexcept {

    moves.reserve(64 - 48 * (pos.checkers() != 0));

    pos.checkers() == 0 ?
        generate<NORMAL>(moves, pos) :
        generate<EVASION>(moves, pos);

    // Filter illegal moves
    moves.erase(std::remove_if(moves.begin(), moves.end(),
                                [&](ValMove const &vm) {
                                    return (pType(pos.movedPiece(vm)) == KING
                                         && mType(vm) == SIMPLE
                                         && (pos.attackersTo(dstSq(vm), pos.pieces() ^ pos.square(pos.activeSide()|KING)) & pos.pieces(~pos.activeSide())) != 0)
                                        || ((mType(vm) == CASTLE
                                          || mType(vm) == ENPASSANT
                                            // Pinned pieces
                                          || contains(pos.pieces(pos.activeSide()) & pos.kingBlockers(pos.activeSide()), orgSq(vm)))
                                         && !pos.legal(vm));
                                }), moves.end());
}

void Perft::operator+=(Perft const &perft) noexcept {
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
void Perft::operator-=(Perft const &perft) noexcept {
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

void Perft::classify(Position &pos, Move m) noexcept {

    if (mType(m) == ENPASSANT
     || contains(pos.pieces(~pos.activeSide()), dstSq(m))) {
        ++capture;
        if (mType(m) == ENPASSANT) {
            ++enpassant;
        }
    }
    if (pos.giveCheck(m)) {
        ++anyCheck;
        // Discovered Check but not Direct Check
        if (!contains(pos.checks(mType(m) != PROMOTE ? pType(pos.movedPiece(m)) : promoteType(m)), dstSq(m))) {
            if (mType(m) == ENPASSANT) {
                Bitboard const mocc{ (pos.pieces() ^ orgSq(m) ^ makeSquare(sFile(dstSq(m)), sRank(orgSq(m)))) | dstSq(m) };
                if ((pos.pieces(pos.activeSide(), BSHP, QUEN)
                   & attacksBB<BSHP>(pos.square(~pos.activeSide()|KING), mocc)) != 0
                 || (pos.pieces(pos.activeSide(), ROOK, QUEN)
                   & attacksBB<ROOK>(pos.square(~pos.activeSide()|KING), mocc)) != 0) {
                    ++dscCheck;
                }
            } else
            if (pos.isKingBlockersOn(~pos.activeSide(), orgSq(m))
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
        if (MoveList<LEGAL>(pos).size() == 0) {
            ++checkmate;
        }
        pos.undoMove(m);
    }
    //else {
    //    StateInfo si;
    //    pos.doMove(m, si, false);
    //    if (MoveList<LEGAL>(pos).size() == 0) {
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
/// All the leaf nodes up to the given depth are generated, and the accumulate is returned.
template<bool RootNode>
Perft perft(Position &pos, Depth depth, bool detail) noexcept {
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
        std::cout << oss.str() << '\n';
    }
    for (auto const &vm : MoveList<LEGAL>(pos)) {
        Perft leaf;

        if (RootNode
         && depth <= 1) {
            ++leaf.any;
            if (detail) {
                leaf.classify(pos, vm);
            }
        } else {
            StateInfo si;
            ASSERT_ALIGNED(&si, Evaluator::NNUE::CacheLineSize);
            pos.doMove(vm, si);

            if (depth <= 2) {
                for (auto &ivm : MoveList<LEGAL>(pos)) {
                    ++leaf.any;
                    if (detail) {
                        leaf.classify(pos, ivm);
                    }
                }
            } else {
                leaf = perft<false>(pos, depth - 1, detail);
            }

            pos.undoMove(vm);
        }
        sumLeaf += leaf;

        if (RootNode) {
            ++sumLeaf.num;

            std::ostringstream oss;
            oss << std::right << std::setfill('0') << std::setw( 2) << sumLeaf.num << " "
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
            std::cout << oss.str() << '\n';
        }
    }
    if (RootNode) {
        std::ostringstream oss;
        oss << '\n'
            << "Total:  " << std::right << std::setfill('.')
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
        std::cout << oss.str() << '\n';
    }
    return sumLeaf;
}
/// Explicit template instantiations
/// --------------------------------
template Perft perft<true >(Position&, Depth, bool);
template Perft perft<false>(Position&, Depth, bool);
