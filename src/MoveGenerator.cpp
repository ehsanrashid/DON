#include "MoveGenerator.h"

#include <iostream>

#include "BitBoard.h"
#include "Notation.h"
#include "Util.h"

using namespace std;
using namespace BitBoard;

namespace {

    /// Generates piece normal move
    template<GenType GT>
    void generatePieceMoves(ValMoves &moves, const Position &pos, Bitboard targets)
    {
        for (auto pt : { NIHT, BSHP, ROOK, QUEN })
        {
            for (auto s : pos.squares[pos.active|pt])
            {
                if (   GenType::CHECK == GT
                    || GenType::QUIET_CHECK == GT)
                {
                    if (   contains(pos.si->kingBlockers[~pos.active], s)
                        || 0 == (PieceAttacks[pt][s] & targets & pos.si->checks[pt]))
                    {
                        continue;
                    }
                }

                Bitboard attacks = pos.attacksFrom(pt, s) & targets;

                if (   GenType::CHECK == GT
                    || GenType::QUIET_CHECK == GT)
                {
                    attacks &= pos.si->checks[pt];
                }

                while (0 != attacks) { moves += makeMove<NORMAL>(s, popLSq(attacks)); }
            }
        }
    }

    /// Generates pawn promotion move
    template<GenType GT>
    void generatePromotionMoves(ValMoves &moves, const Position &pos, Bitboard promotion, Delta del)
    {
        auto ekSq = pos.square(~pos.active|KING);
        while (0 != promotion)
        {
            auto dst = popLSq(promotion);
            auto org = dst - del;

            switch (GT)
            {
            case GenType::NATURAL:
            case GenType::EVASION:
                moves += makePromoteMove(org, dst, QUEN);
                /* fall through */
            case GenType::QUIET:
                moves += makePromoteMove(org, dst, ROOK);
                moves += makePromoteMove(org, dst, BSHP);
                moves += makePromoteMove(org, dst, NIHT);
                break;
            case GenType::CAPTURE:
                moves += makePromoteMove(org, dst, QUEN);
                break;
            case GenType::CHECK:
            {
                Bitboard mocc = pos.pieces() ^ org;
                Bitboard rookAttacks = attacksBB<ROOK>(dst, mocc);
                Bitboard bshpAttacks = attacksBB<BSHP>(dst, mocc);
                if (contains(rookAttacks | bshpAttacks, ekSq))
                {
                    moves += makePromoteMove(org, dst, QUEN);
                }
                if (contains(rookAttacks, ekSq))
                {
                    moves += makePromoteMove(org, dst, ROOK);
                }
                if (contains(bshpAttacks, ekSq))
                {
                    moves += makePromoteMove(org, dst, BSHP);
                }
            }
                /* fall through */
            case GenType::QUIET_CHECK:
                if (contains(PieceAttacks[NIHT][dst], ekSq))
                {
                    moves += makePromoteMove(org, dst, NIHT);
                }
                break;
            default: assert(false); break;
            }
        }
    }
    /// Generates pawn normal move
    template<GenType GT>
    void generatePawnMoves(ValMoves &moves, const Position &pos, Bitboard targets)
    {
        const auto Push = pawnPush(pos.active);

        Bitboard empties = ~pos.pieces();
        Bitboard enemies =  pos.pieces(~pos.active) & targets;

        Bitboard pawns = pos.pieces(pos.active, PAWN);
        Bitboard Rank7 = rankBB(relRank(pos.active, R_7));
        // Pawns not on 7th Rank
        Bitboard rxPawns = pawns & ~Rank7;
        // Pawns only on 7th Rank
        Bitboard r7Pawns = pawns &  Rank7;
        switch (GT)
        {
        case GenType::NATURAL:
        case GenType::EVASION:
        case GenType::CAPTURE:
        case GenType::CHECK:
        {
            // Pawn normal and en-passant captures, no promotions
            Bitboard l_attacks = enemies & pawnLAttacks(pos.active, rxPawns);
            Bitboard r_attacks = enemies & pawnRAttacks(pos.active, rxPawns);
            if (GenType::CHECK == GT)
            {
                l_attacks &= pos.si->checks[PAWN];
                r_attacks &= pos.si->checks[PAWN];
                // Pawns which give discovered check
                // Add pawn captures which give discovered check.
                Bitboard dsc_pawns = rxPawns
                                   & pos.si->kingBlockers[~pos.active];
                if (0 != dsc_pawns)
                {
                    l_attacks |= enemies & pawnLAttacks(pos.active, dsc_pawns);
                    r_attacks |= enemies & pawnRAttacks(pos.active, dsc_pawns);
                }
            }
            while (0 != l_attacks) { auto dst = popLSq(l_attacks); moves += makeMove<NORMAL>(dst - pawnLAtt(pos.active), dst); }
            while (0 != r_attacks) { auto dst = popLSq(r_attacks); moves += makeMove<NORMAL>(dst - pawnRAtt(pos.active), dst); }

            if (SQ_NO != pos.si->enpassantSq)
            {
                assert(R_6 == relRank(pos.active, pos.si->enpassantSq));
                Bitboard ep_captures = rxPawns
                                     & PawnAttacks[~pos.active][pos.si->enpassantSq];
                switch (GT)
                {
                case GenType::EVASION:
                    // If the checking piece is the double pushed pawn and also is in the target.
                    // Otherwise this is a discovery check and are forced to do otherwise.
                    if (!contains(enemies /*& pos.pieces(PAWN)*/, pos.si->enpassantSq - Push))
                    {
                        ep_captures = 0;
                    }
                    break;
                case GenType::CHECK:
                    if (//!contains(PawnAttacks[pos.active][pos.si->enpassantSq], pos.square(~pos.active|KING))
                        !contains(pos.si->checks[PAWN], pos.si->enpassantSq))
                    {
                        // En-passant pawns which give discovered check
                        ep_captures &= pos.si->kingBlockers[~pos.active];
                    }
                    break;
                default: break;
                }
                assert(2 >= popCount(ep_captures));
                while (0 != ep_captures) { moves += makeMove<ENPASSANT>(popLSq(ep_captures), pos.si->enpassantSq); }
            }
        }
            /* fall through */
        case GenType::QUIET:
        case GenType::QUIET_CHECK:
        {
            // Promotions (queening and under-promotions)
            if (0 != r7Pawns)
            {
                Bitboard b;

                b = enemies & pawnLAttacks(pos.active, r7Pawns);
                generatePromotionMoves<GT>(moves, pos, b, pawnLAtt(pos.active));
                b = enemies & pawnRAttacks(pos.active, r7Pawns);
                generatePromotionMoves<GT>(moves, pos, b, pawnRAtt(pos.active));
                b = empties & pawnSglPushes(pos.active, r7Pawns);
                if (GenType::EVASION == GT)
                {
                    b &= targets;
                }
                generatePromotionMoves<GT>(moves, pos, b, Push);
            }

            if (GenType::CAPTURE == GT)
            {
                break;
            }

            // Pawn single-push and double-push, no promotions
            Bitboard pushs_1 = empties & pawnSglPushes(pos.active, rxPawns);
            Bitboard pushs_2 = empties & pawnSglPushes(pos.active, pushs_1 & rankBB(relRank(pos.active, R_3)));
            switch (GT)
            {
            case GenType::EVASION:
                pushs_1 &= targets;
                pushs_2 &= targets;
                break;
            case GenType::CHECK:
            case GenType::QUIET_CHECK:
            {
                pushs_1 &= pos.si->checks[PAWN];
                pushs_2 &= pos.si->checks[PAWN];
                // Pawns which give discovered check
                // Add pawn pushes which give discovered check.
                // This is possible only if the pawn is not on the same file as the enemy king, because don't generate captures.
                // Note that a possible discovery check promotion has been already generated among captures.
                Bitboard dsc_pawns = rxPawns
                                   & pos.si->kingBlockers[~pos.active]
                                   & ~fileBB(pos.square(~pos.active|KING));
                if (0 != dsc_pawns)
                {
                    Bitboard dc_pushs_1 = empties & pawnSglPushes(pos.active, dsc_pawns);
                    Bitboard dc_pushs_2 = empties & pawnSglPushes(pos.active, dc_pushs_1 & rankBB(relRank(pos.active, R_3)));
                    pushs_1 |= dc_pushs_1;
                    pushs_2 |= dc_pushs_2;
                }
            }
                break;
            default: break;
            }
            while (0 != pushs_1) { auto dst = popLSq(pushs_1); moves += makeMove<NORMAL>(dst - 1 * Push, dst); }
            while (0 != pushs_2) { auto dst = popLSq(pushs_2); moves += makeMove<NORMAL>(dst - 2 * Push, dst); }
        }
            break;
        default: assert(false); break;
        }
    }

    /// Generates king normal move
    template<GenType GT>
    void generateKingMoves(ValMoves &moves, const Position &pos, Bitboard targets)
    {
        auto fkSq = pos.square(pos.active|KING);
        Bitboard attacks = PieceAttacks[KING][fkSq] & targets;
        while (0 != attacks) { moves += makeMove<NORMAL>(fkSq, popLSq(attacks)); }

        if (   GenType::NATURAL == GT
            || GenType::QUIET == GT)
        {
            if (CR_NONE != pos.castleRight(pos.active))
            {
                for (auto cs : { CS_KING, CS_QUEN })
                {
                    if (   pos.canCastle(makeCastleRight(pos.active, cs))
                        && pos.castleExpeded(pos.active, cs))
                    {
                        moves += makeMove<CASTLE>(fkSq, pos.castleRookSq[pos.active][cs]);
                    }
                }
            }
        }
    }


    /// Generates all pseudo-legal moves of color for targets.
    template<GenType GT>
    void generateMoves(ValMoves &moves, const Position &pos, Bitboard targets)
    {
        generatePawnMoves<GT>(moves, pos, targets);
        generatePieceMoves<GT>(moves, pos, targets);
    }
}

template<GenType GT>
void generate(ValMoves &moves, const Position &pos)
{
    assert(0 == pos.si->checkers);
    static_assert (GenType::NATURAL == GT
                || GenType::CAPTURE == GT
                || GenType::QUIET == GT, "GT incorrect");
    moves.clear();
    Bitboard targets;
    switch (GT)
    {
    case GenType::NATURAL: targets = ~pos.pieces(pos.active); break;
    case GenType::CAPTURE: targets =  pos.pieces(~pos.active); break;
    case GenType::QUIET:   targets = ~pos.pieces(); break;
    default: assert(false);targets = 0;
    }
    generateMoves<GT>(moves, pos, targets);
    generateKingMoves<GT>(moves, pos, targets);
}

/// Explicit template instantiations
/// --------------------------------
/// generate<NATURAL>     Generates all pseudo-legal captures and non-captures.
template void generate<GenType::NATURAL>(ValMoves&, const Position&);
/// generate<CAPTURE>     Generates all pseudo-legal captures and queen promotions.
template void generate<GenType::CAPTURE>(ValMoves&, const Position&);
/// generate<QUIET>       Generates all pseudo-legal non-captures and underpromotions.
template void generate<GenType::QUIET  >(ValMoves&, const Position&);

/// generate<EVASION>     Generates all pseudo-legal check evasions moves.
template<> void generate<GenType::EVASION    >(ValMoves &moves, const Position &pos)
{
    assert(0 != pos.si->checkers
        && 2 >= popCount(pos.si->checkers));

    moves.clear();
    auto fkSq = pos.square(pos.active|KING);
    Bitboard checks = PieceAttacks[KING][pos.square(~pos.active|KING)];
    Bitboard ex_checkers = pos.si->checkers & ~pos.pieces(NIHT, PAWN);
    // Squares attacked by slide checkers will remove them from the king evasions
    // so to skip known illegal moves avoiding useless legality check later.
    while (0 != ex_checkers)
    {
        auto check_sq = popLSq(ex_checkers);
        checks |= lines(check_sq, fkSq) ^ check_sq;
    }
    // Generate evasions for king, capture and non-capture moves
    Bitboard attacks = PieceAttacks[KING][fkSq]
                     & ~checks
                     & ~pos.pieces(pos.active);
    while (0 != attacks) { moves += makeMove<NORMAL>(fkSq, popLSq(attacks)); }

    // Double-check, only king move can save the day
    if (moreThanOne(pos.si->checkers))
    {
        return;
    }

    // Generates blocking or captures of the checking piece
    auto check_sq = scanLSq(pos.si->checkers);
    Bitboard targets = betweens(check_sq, fkSq) | check_sq;

    generateMoves<GenType::EVASION>(moves, pos, targets);
}
/// generate<CHECK>       Generates all pseudo-legal check giving moves.
template<> void generate<GenType::CHECK      >(ValMoves &moves, const Position &pos)
{
    assert(0 == pos.si->checkers);
    moves.clear();
    Bitboard targets = ~pos.pieces(pos.active);
    // Pawns is excluded, will be generated together with direct checks
    Bitboard ex_dsc_blockers =  pos.si->kingBlockers[~pos.active]
                             &  pos.pieces(pos.active)
                             & ~pos.pieces(PAWN);
    assert(0 == (ex_dsc_blockers & pos.pieces(QUEN)));
    while (0 != ex_dsc_blockers)
    {
        auto org = popLSq(ex_dsc_blockers);
        Bitboard attacks = pos.attacksFrom(org) & targets;
        if (KING == pType(pos[org]))
        {
            attacks &= ~PieceAttacks[QUEN][pos.square(~pos.active|KING)];
        }
        while (0 != attacks) { moves += makeMove<NORMAL>(org, popLSq(attacks)); }
    }

    generateMoves<GenType::CHECK>(moves, pos, targets);
}
/// generate<QUIET_CHECK> Generates all pseudo-legal non-captures and knight under promotions check giving moves.
template<> void generate<GenType::QUIET_CHECK>(ValMoves &moves, const Position &pos)
{
    assert(0 == pos.si->checkers);
    moves.clear();
    Bitboard targets = ~pos.pieces();
    // Pawns is excluded, will be generated together with direct checks
    Bitboard ex_dsc_blockers =  pos.si->kingBlockers[~pos.active]
                             &  pos.pieces(pos.active)
                             & ~pos.pieces(PAWN);
    assert(0 == (ex_dsc_blockers & pos.pieces(QUEN)));
    while (0 != ex_dsc_blockers)
    {
        auto org = popLSq(ex_dsc_blockers);
        Bitboard attacks = pos.attacksFrom(org) & targets;
        if (KING == pType(pos[org]))
        {
            attacks &= ~PieceAttacks[QUEN][pos.square(~pos.active|KING)];
        }
        while (0 != attacks) { moves += makeMove<NORMAL>(org, popLSq(attacks)); }
    }

    generateMoves<GenType::QUIET_CHECK>(moves, pos, targets);
}

/// generate<LEGAL>       Generates all legal moves.
template<> void generate<GenType::LEGAL      >(ValMoves &moves, const Position &pos)
{
    0 == pos.si->checkers ?
        generate<GenType::NATURAL>(moves, pos) :
        generate<GenType::EVASION>(moves, pos);
    filterIllegal(moves, pos);
}

/// Filter illegal moves
void filterIllegal(ValMoves &moves, const Position &pos)
{
    moves.erase(std::remove_if(moves.begin(), moves.end(),
                               [&pos](const ValMove &vm) { return !pos.fullLegal(vm); }),
                 moves.end());
}

void Perft::classify(Position &pos, Move m)
{
    if (   ENPASSANT == mType(m)
        || contains(pos.pieces(~pos.active), dstSq(m)))
    {
        ++capture;
        if (ENPASSANT == mType(m))
        {
            ++enpassant;
        }
    }
    if (pos.giveCheck(m))
    {
        ++anyCheck;
        if (!contains(pos.si->checks[PROMOTE != mType(m) ? pType(pos[orgSq(m)]) : promoteType(m)], dstSq(m)))
        {
            auto ekSq = pos.square(~pos.active|KING);
            if (   contains(pos.si->kingBlockers[~pos.active], orgSq(m))
                && !squaresAligned(orgSq(m), dstSq(m), ekSq))
            {
                ++dscCheck;
            }
            else
            if (ENPASSANT == mType(m))
            {
                auto epSq = makeSquare(sFile(dstSq(m)), sRank(orgSq(m)));
                Bitboard mocc = (pos.pieces() ^ orgSq(m) ^ epSq) | dstSq(m);
                if (   0 != (pos.pieces(pos.active, BSHP, QUEN) & attacksBB<BSHP>(ekSq, mocc))
                    || 0 != (pos.pieces(pos.active, ROOK, QUEN) & attacksBB<ROOK>(ekSq, mocc)))
                {
                    ++dscCheck;
                }
            }
        }
        StateInfo si;
        pos.doMove(m, si, true);
        assert(0 != pos.si->checkers
            && 2 >= popCount(pos.si->checkers));
        if (moreThanOne(pos.si->checkers))
        {
            ++dblCheck;
        }
        if (0 == MoveList<GenType::LEGAL>(pos).size())
        {
            ++checkmate;
        }
        pos.undoMove(m);
    }
    //else
    //{
    //    StateInfo si;
    //    pos.doMove(m, si, false);
    //    if (0 == MoveList<GenType::LEGAL>(pos).size())
    //    {
    //        ++stalemate;
    //    }
    //    pos.undoMove(m);
    //}
    if (CASTLE == mType(m))
    {
        ++castle;
    }
    if (PROMOTE == mType(m))
    {
        ++promotion;
    }
}

/// perft() is utility to verify move generation.
/// All the leaf nodes up to the given depth are generated, and the sum is returned.
template<bool RootNode>
Perft perft(Position &pos, Depth depth, bool detail)
{
    Perft sumLeaf;
    if (RootNode)
    {
        sync_cout << left << setfill(' ')
                  << setw( 3) << "N"
                  << setw(10) << "Move"
                  << setw(19) << "Any";
        if (detail)
        {
            cout << setw(17) << "Capture"
                 << setw(15) << "Enpassant"
                 << setw(17) << "AnyCheck"
                 << setw(15) << "DscCheck"
                 << setw(15) << "DblCheck"
                 << setw(15) << "Castle"
                 << setw(15) << "Promote"
                 << setw(15) << "Checkmate"
                 //<< setw(15) << "Stalemate"
                 ;
        }
        cout << sync_endl;
    }
    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
    {
        Perft leaf;
        if (   RootNode
            && 1 >= depth)
        {
            ++leaf.any;
            if (detail) leaf.classify(pos, vm);
        }
        else
        {
            StateInfo si;
            pos.doMove(vm, si);

            if (2 >= depth)
            {
                for (const auto &ivm : MoveList<GenType::LEGAL>(pos))
                {
                    ++leaf.any;
                    if (detail) leaf.classify(pos, ivm);
                }
            }
            else
            {
                leaf = perft<false>(pos, depth - 1, detail);
            }

            pos.undoMove(vm);
        }
        sumLeaf += leaf;

        if (RootNode)
        {
            ++sumLeaf.moves;

            sync_cout << right << setfill('0') << setw( 2) << sumLeaf.moves
                      << " "
                      << left  << setfill(' ') << setw( 7)
                                                           <<
                                                              //canMove(vm)
                                                              sanMove(vm, pos)
                      << right << setfill('.') << setw(16) << leaf.any;
            if (detail)
            {
                cout << "   " << setw(14) << leaf.capture
                     << "   " << setw(12) << leaf.enpassant
                     << "   " << setw(14) << leaf.anyCheck
                     << "   " << setw(12) << leaf.dscCheck
                     << "   " << setw(12) << leaf.dblCheck
                     << "   " << setw(12) << leaf.castle
                     << "   " << setw(12) << leaf.promotion
                     << "   " << setw(12) << leaf.checkmate
                     //<< "   " << setw(12) << leaf.stalemate
                     ;
            }
            cout << setfill(' ')
                 << left << sync_endl;
        }
    }
    if (RootNode)
    {
        sync_cout << endl
                  << "Total:  " << right << setfill('.') << setw(18) << sumLeaf.any;
        if (detail)
        {
            cout << " " << setw(16) << sumLeaf.capture
                 << " " << setw(14) << sumLeaf.enpassant
                 << " " << setw(16) << sumLeaf.anyCheck
                 << " " << setw(14) << sumLeaf.dscCheck
                 << " " << setw(14) << sumLeaf.dblCheck
                 << " " << setw(14) << sumLeaf.castle
                 << " " << setw(14) << sumLeaf.promotion
                 << " " << setw(14) << sumLeaf.checkmate
                 //<< " " << setw(14) << sumLeaf.stalemate
                 ;
        }
        cout << setfill(' ')
             << left
             << endl
             << sync_endl;
    }
    return sumLeaf;
}
/// Explicit template instantiations
/// --------------------------------
template Perft perft<true >(Position&, Depth, bool);
template Perft perft<false>(Position&, Depth, bool);
