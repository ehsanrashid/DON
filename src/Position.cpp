#include "Position.h"

#include <cassert>
#include <cstring> // For std::memset and std::memcpy
#include <algorithm>

#include "Cuckoo.h"
#include "Helper.h"
#include "MoveGenerator.h"
#include "Notation.h"
#include "Polyglot.h"
#include "PSQTable.h"
#include "SyzygyTB.h"
#include "Thread.h"
#include "Transposition.h"
#include "Zobrist.h"
#include "UCI.h"

Array<Score, PIECES, SQUARES> PSQ;

namespace {

    /// Computes the non-pawn middle game material value for the given side.
    /// Material values are updated incrementally during the search.
    template<Color Own>
    Value computeNPM(Position const &pos) {

        auto npm{ VALUE_ZERO };

        for (PieceType pt = NIHT; pt <= QUEN; ++pt) {
            npm += PieceValues[MG][pt] * pos.count(Own|pt);
        }
        return npm;
    }
    /// Explicit template instantiations
    /// --------------------------------
    template Value computeNPM<WHITE>(Position const&);
    template Value computeNPM<BLACK>(Position const&);

}


void StateInfo::clear() {
    matlKey = 0;
    pawnKey = 0;
    castleRights = CR_NONE;
    epSquare = SQ_NONE;
    clockPly = 0;
    nullPly = 0;

    posiKey = 0;
    checkers = 0;
    captured = NONE;
    promoted = false;
    repetition = 0;
    kingBlockers.fill(0);
    kingCheckers.fill(0);
    checks.fill(0);

    ptr = nullptr;
}

//// initialize() static function
//void Position::initialize()
//{}

Key Position::pgKey() const {
    return PolyZob.computePosiKey(*this);
}
/// Position::movePosiKey() computes the new hash key after the given moven, needed for speculative prefetch.
/// It doesn't recognize special moves like castling, en-passant and promotions.
Key Position::movePosiKey(Move m) const {
    assert(isOk(m));
    //assert(pseudoLegal(m)
    //    && legal(m));
    /*
    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    auto mp = board[org];
    auto cp = mType(m) != ENPASSANT ?
                board[dst] : ~active|PAWN;

    auto pKey{ posiKey()
             ^ RandZob.colorKey
             ^ (epSquare() != SQ_NONE ? RandZob.enpassantKey[sFile(epSquare())] : 0) };

    if (mType(m) == CASTLE) {
        // ROOK
        pKey ^= RandZob.pieceSquareKey[cp][dst]
              ^ RandZob.pieceSquareKey[cp][rookCastleSq(org, dst)];
    }
    else {
        if (cp != NO_PIECE) {
            pKey ^= RandZob.pieceSquareKey[cp][mType(m) != ENPASSANT ? dst : dst - PawnPush[active]];
        }
        else
        if (pType(mp) == PAWN
         && dst == org + PawnPush[active] * 2) {
            auto epSq{ org + PawnPush[active] };
            if (canEnpassant(~active, epSq, false)) {
                pKey ^= RandZob.enpassantKey[sFile(epSq)];
            }
        }
    }
    return pKey
         ^ RandZob.pieceSquareKey[mp][org]
         ^ RandZob.pieceSquareKey[mType(m) != PROMOTE ? mp : active|promoteType(m)][mType(m) != CASTLE ? dst : kingCastleSq(org, dst)]
         ^ RandZob.castleRightKey[castleRights() & (sqCastleRight[org]|sqCastleRight[dst])];
    */

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    auto pKey{ posiKey()
             ^ RandZob.colorKey
             ^ RandZob.pieceSquareKey[board[org]][org]
             ^ RandZob.pieceSquareKey[board[org]][dst] };
    if (board[dst] != NO_PIECE) {
        pKey ^= RandZob.pieceSquareKey[board[dst]][dst];
    }
    if (epSquare() != SQ_NONE) {
        pKey ^= RandZob.enpassantKey[sFile(epSquare())];
    }
    return pKey;
}

/// Position::draw() checks whether position is drawn by: Clock Ply Rule, Repetition.
/// It does not detect Insufficient materials and Stalemate.
bool Position::draw(i16 pp) const {
    return  // Draw by Clock Ply Rule?
            // Not in check or in check have legal moves
           (clockPly() >= 2 * i16(Options["Draw MoveCount"])
         && (checkers() == 0
          || MoveList<GenType::LEGAL>(*this).size() != 0))
            // Draw by Repetition?
            // Return a draw score if a position repeats once earlier but strictly
            // after the root, or repeats twice before or at the root.
        || (repetition() != 0
         && repetition() < pp);
}

/// Position::repeated() tests whether there has been at least one repetition of positions since the last capture or pawn move.
bool Position::repeated() const {
    auto end{ std::min(clockPly(), nullPly()) };
    auto const *csi{ _stateInfo };
    while (end-- >= 4) {
        if (csi->repetition != 0) {
            return true;
        }
        csi = csi->ptr;
    }
    return false;
}

/// Position::cycled() tests if the position has a move which draws by repetition,
/// or an earlier position has a move that directly reaches the current position.
bool Position::cycled(i16 pp) const {
    auto end{ std::min(clockPly(), nullPly()) };
    if (end < 3) {
        return false;
    }

    Key pKey{ posiKey() };

    auto const *psi = _stateInfo->ptr;
    for (i16 i = 3; i <= end; i += 2) {
        psi = psi->ptr->ptr;

        Key moveKey{ pKey
                   ^ psi->posiKey };

        Cuckoo cuckoo;
        if (Cuckoos::lookup(moveKey, cuckoo)) {
            assert(!cuckoo.empty());

            // Legality of a reverting move: clear path
            if ((pieces() & betweenBB(cuckoo.sq1, cuckoo.sq2)) == 0) {

                if (i < pp) {
                    return true;
                }
                assert(cuckoo.piece == board[cuckoo.sq1]
                    || cuckoo.piece == board[cuckoo.sq2]);
                // For nodes before or at the root, check that the move is a repetition one
                // rather than a move to the current position
                // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in the same location.
                if (pColor(cuckoo.piece) != active) {
                    continue;
                }
                // For repetitions before or at the root, require one more
                if (psi->repetition != 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

/// Position::sliderBlockersAt() returns a bitboard of all the pieces that are blocking attacks on the square.
/// King-attack piece can be either pinner or hidden piece.
Bitboard Position::sliderBlockersAt(Square s, Bitboard attackers, Bitboard &pinners, Bitboard &hidders) const {
    Bitboard blockers{ 0 };

    Bitboard defenders{ pieces(pColor(board[s])) };
    // Snipers are X-ray slider attackers at 's'
    // No need to remove direct attackers at 's' as in check no evaluation
    Bitboard snipers{ attackers
                    & ((pieces(BSHP, QUEN) & PieceAttackBB[BSHP][s])
                     | (pieces(ROOK, QUEN) & PieceAttackBB[ROOK][s])) };
    Bitboard mocc{ pieces() ^ snipers };
    while (snipers != 0) {
        auto sniperSq{ popLSq(snipers) };
        Bitboard b{ betweenBB(s, sniperSq) & mocc };
        if (b != 0
         && !moreThanOne(b)) {
            blockers |= b;
            if ((b & defenders) != 0) {
                pinners |= sniperSq;
            }
            else {
                hidders |= sniperSq;
            }
        }
    }
    return blockers;
}

/// Position::pseudoLegal() tests whether a random move is pseudo-legal.
/// It is used to validate moves from TT that can be corrupted
/// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudoLegal(Move m) const
{
    assert(isOk(m));

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if (!contains(pieces(active), org)) {
        return false;
    }

    auto chkrs{ checkers() };

    if (mType(m) == CASTLE) {

        auto cs{ dst > org ? CS_KING : CS_QUEN };
        return board[org] == (active|KING) //&& contains(pieces(active, KING), org)
            && board[dst] == (active|ROOK) //&& contains(pieces(active, ROOK), dst)
            && chkrs == 0
            && castleRookSq(active, cs) == dst
            && castleExpeded(active, cs)
            //&& relativeRank(active, org) == RANK_1
            //&& relativeRank(active, dst) == RANK_1
            && canCastle(active, cs);
    }

    // The captured square cannot be occupied by a friendly piece
    if (contains(pieces(active), dst)) {
        return false;
    }

    // Handle the special case of a piece move
    if (pType(board[org]) == PAWN) {
        auto orgR{ relativeRank(active, org) };
        auto dstR{ relativeRank(active, dst) };
        auto Push{ PawnPush[active] };

        if (// Single push
            (((mType(m) != NORMAL
            || RANK_2 > orgR || orgR > RANK_6
            || RANK_3 > dstR || dstR > RANK_7)
           && (mType(m) != PROMOTE
            || orgR != RANK_7
            || dstR != RANK_8))
          || dst != org + Push
          || !empty(dst))
            // Normal capture
         && (((mType(m) != NORMAL
            || RANK_2 > orgR || orgR > RANK_6
            || RANK_3 > dstR || dstR > RANK_7)
           && (mType(m) != PROMOTE
            || orgR != RANK_7
            || dstR != RANK_8))
          || !contains(PawnAttackBB[active][org], dst)
          || empty(dst))
            // Double push
         && (mType(m) != NORMAL
          || orgR != RANK_2
          || dstR != RANK_4
          || dst != org + Push * 2
          || !empty(dst)
          || !empty(dst - Push))
            // Enpassant capture
         && (mType(m) != ENPASSANT
          || orgR != RANK_5
          || dstR != RANK_6
          || dst != epSquare()
          || !contains(PawnAttackBB[active][org], dst)
          || !empty(dst)
          || empty(dst - Push)
          || clockPly() != 0)) {
            return false;
        }
    }
    else {
        if (mType(m) != NORMAL
         || !contains(attacksFrom(org), dst)) {
            return false;
        }
    }

    // Evasions generator already takes care to avoid some kind of illegal moves and legal() relies on this.
    // So have to take care that the same kind of moves are filtered out here.
    if (chkrs != 0) {
        auto fkSq = square(active|KING);
        // In case of king moves under check, remove king so to catch
        // as invalid moves like B1A1 when opposite queen is on C1.
        if (org == fkSq) {
            return (attackersTo(dst, pieces() ^ fkSq) & pieces(~active)) == 0;
        }
        // Double check? In this case a king move is required
        if (moreThanOne(chkrs)) {
            return false;
        }
        return mType(m) != ENPASSANT ?
                // Move must be a capture of the checking piece or a blocking evasion of the checking piece
                contains(chkrs | betweenBB(scanLSq(chkrs), fkSq), dst) :
                // Move must be a capture of the checking enpassant pawn or a blocking evasion of the checking piece
                (contains(chkrs & pieces(PAWN), dst - PawnPush[active])
              || contains(betweenBB(scanLSq(chkrs), fkSq), dst));
    }
    return true;
}
/// Position::legal() tests whether a pseudo-legal move is legal.
bool Position::legal(Move m) const {
    assert(isOk(m));
    //assert(pseudoLegal(m));

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    assert(contains(pieces(active), org));

    // Castling moves check for clear path for king
    if (mType(m) == CASTLE) {
        assert(board[org] == (active|KING) //&& contains(pieces(active, KING), org)
            && board[dst] == (active|ROOK) //&& contains(pieces(active, ROOK), dst)
            && castleRookSq(active, dst > org ? CS_KING : CS_QUEN) == dst
            && castleExpeded(active, dst > org ? CS_KING : CS_QUEN)
            //&& relativeRank(active, org) == RANK_1
            //&& relativeRank(active, dst) == RANK_1
            && canCastle(active, dst > org ? CS_KING : CS_QUEN)
            && checkers() == 0);

        // Check king's path for attackers
        Bitboard mocc{ pieces() ^ dst };
        Bitboard enemies{ pieces(~active) };
        Bitboard kingPath{ castleKingPath(active, dst > org ? CS_KING : CS_QUEN) };
        while (kingPath != 0) {
            if ((enemies & attackersTo(popLSq(kingPath), mocc)) != 0) {
                return false;
            }
        }
        //// In case of Chess960, verify that when moving the castling rook we do not discover some hidden checker.
        //// For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        //return !Options["UCI_Chess960"]
        //    || (enemies
        //      & pieces(ROOK, QUEN)
        //      & rankBB(org)
        //      & attacksBB<ROOK>(kingCastleSq(org, dst), pieces() ^ dst)) == 0;
        return true;
    }

    auto fkSq{ square(active|KING) };

    // Enpassant captures are a tricky special case. Because they are rather uncommon,
    // do it simply by testing whether the king is attacked after the move is made.
    if (mType(m) == ENPASSANT) {
        assert(board[org] == (active|PAWN) //&& contains(pieces(active, PAWN), org)
            && relativeRank(active, org) == RANK_5
            && relativeRank(active, dst) == RANK_6
            && clockPly() == 0
            && dst == epSquare()
            && empty(dst)
            && board[dst - PawnPush[active]] == (~active|PAWN));

        Bitboard mocc{ (pieces() ^ org ^ (dst - PawnPush[active])) | dst };
        return (pieces(~active, BSHP, QUEN) & attacksBB<BSHP>(fkSq, mocc)) == 0
            && (pieces(~active, ROOK, QUEN) & attacksBB<ROOK>(fkSq, mocc)) == 0;
    }

    return
        org == fkSq ?
            // KING NORMAL moves
            // Only king moves to non attacked squares, sliding check x-rays the king
            // In case of king moves under check have to remove king so to catch
            // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
            // check whether the destination square is attacked by the opponent.
            (attackersTo(dst, pieces() ^ fkSq) & pieces(~active)) == 0 :
            // OTHER NORMAL + PROMOTE moves
            // A non-king move is legal if and only if
            // - not pinned
            // - moving along the ray from the king
            !contains(kingBlockers(active), org)
         || aligned(fkSq, org, dst);
}

/// Position::giveCheck() tests whether a pseudo-legal move gives a check.
bool Position::giveCheck(Move m) const {
    assert(isOk(m));

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    assert(contains(pieces(active), org));

    auto ekSq{ square(~active|KING) };

    if (// Direct check ?
        contains(checks(mType(m) != PROMOTE ? pType(board[org]) : promoteType(m)), dst)
        // Discovered check ?
     || (contains(kingBlockers(~active), org)
      && !aligned(ekSq, org, dst))) {
        return true;
    }

    switch (mType(m)) {
    case NORMAL: {
        return false;
    }
    case ENPASSANT: {
        // Enpassant capture with check?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        Bitboard mocc{ (pieces() ^ org ^ makeSquare(sFile(dst), sRank(org))) | dst };
        return (pieces(active, BSHP, QUEN)
              & attacksBB<BSHP>(ekSq, mocc)) != 0
            || (pieces(active, ROOK, QUEN)
              & attacksBB<ROOK>(ekSq, mocc)) != 0;
    }
    case CASTLE: {
        // Castling with check?
        auto kingDst{ kingCastleSq(org, dst) };
        auto rookDst{ rookCastleSq(org, dst) };
        Bitboard mocc{ (pieces() ^ org ^ dst) | kingDst | rookDst };
        return contains(attacksBB<ROOK>(rookDst, mocc), ekSq);
    }
    // case PROMOTE:
    default: {
        // Promotion with check?
        auto ppt{ promoteType(m) };
        Bitboard mocc{ (pieces() ^ org) | dst };
        return
         //   ppt > NIHT
         //&& contains(attacksBB(ppt, dst, mocc), ekSq)
            ((ppt == BSHP
           || ppt == QUEN)
          && contains(attacksBB<BSHP>(dst, mocc), ekSq))
         || ((ppt == ROOK
           || ppt == QUEN)
          && contains(attacksBB<ROOK>(dst, mocc), ekSq));
    }
    }
}

bool Position::giveDblCheck(Move m) const {
    assert(isOk(m));
    //assert(giveCheck(m));

    if (mType(m) == CASTLE) {
        return false;
    }

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };

    auto ekSq{ square(~active|KING) };

    if (mType(m) == ENPASSANT) {
        Bitboard mocc{ (pieces() ^ org ^ makeSquare(sFile(dst), sRank(org))) | dst };
        auto chkrCount{ popCount((pieces(active, BSHP, QUEN)
                                & attacksBB<BSHP>(ekSq, mocc))
                               | (pieces(active, ROOK, QUEN)
                                & attacksBB<ROOK>(ekSq, mocc))) };
        return chkrCount > 1
            || (chkrCount > 0
             && contains(checks(PAWN), dst));
    }

    return
        // Direct check ?
        contains(checks(mType(m) != PROMOTE ? pType(board[org]) : promoteType(m)), dst)
        // Discovered check ?
     && (contains(kingBlockers(~active), org)
      /*&& !aligned(ekSq, org, dst)*/);
}

/// Position::setCastle() set the castling right.
void Position::setCastle(Color c, Square rookOrg) {
    auto kingOrg{ square(c|KING) };
    assert(isOk(rookOrg)
        && relativeRank(c, kingOrg) == RANK_1
        && relativeRank(c, rookOrg) == RANK_1
        && board[rookOrg] == (c|ROOK)); //&& contains(pieces(c, ROOK), rookOrg)

    auto cs{rookOrg > kingOrg ? CS_KING : CS_QUEN};
    auto kingDst{ kingCastleSq(kingOrg, rookOrg) };
    auto rookDst{ rookCastleSq(kingOrg, rookOrg) };
    auto cr{ makeCastleRight(c, cs) };
    cslRookSq[c][cs] = rookOrg;
    _stateInfo->castleRights |= cr;
    sqCastleRight[kingOrg]   |= cr;
    sqCastleRight[rookOrg]   |= cr;

    cslKingPath[c][cs] = (betweenBB(kingOrg, kingDst) | kingDst) & ~kingOrg;
    cslRookPath[c][cs] = (betweenBB(kingOrg, kingDst) | betweenBB(rookOrg, rookDst) | kingDst | rookDst) & ~kingOrg & ~rookOrg;
}
/// Position::setCheckInfo() sets check info used for fast check detection.
void Position::setCheckInfo() {
    _stateInfo->kingCheckers[WHITE] = 0;
    _stateInfo->kingCheckers[BLACK] = 0;
    _stateInfo->kingBlockers[WHITE] = sliderBlockersAt(square(WHITE|KING), pieces(BLACK), _stateInfo->kingCheckers[WHITE], _stateInfo->kingCheckers[BLACK]);
    _stateInfo->kingBlockers[BLACK] = sliderBlockersAt(square(BLACK|KING), pieces(WHITE), _stateInfo->kingCheckers[BLACK], _stateInfo->kingCheckers[WHITE]);

    auto ekSq{ square(~active|KING) };
    _stateInfo->checks[PAWN] = PawnAttackBB[~active][ekSq];
    _stateInfo->checks[NIHT] = attacksFrom(NIHT, ekSq);
    _stateInfo->checks[BSHP] = attacksFrom(BSHP, ekSq);
    _stateInfo->checks[ROOK] = attacksFrom(ROOK, ekSq);
    _stateInfo->checks[QUEN] = _stateInfo->checks[BSHP]|_stateInfo->checks[ROOK];
    _stateInfo->checks[KING] = 0;
}

/// Position::canEnpassant() Can the enpassant possible.
bool Position::canEnpassant(Color c, Square epSq, bool moveDone) const {
    assert(isOk(epSq)
        && relativeRank(c, epSq) == RANK_6);
    auto cap{ moveDone ? epSq - PawnPush[c] : epSq + PawnPush[c] };
    assert(board[cap] == (~c|PAWN)); //contains(pieces(~c, PAWN), cap));
    // Enpassant attackers
    Bitboard attackers{ pieces(c, PAWN)
                      & PawnAttackBB[~c][epSq] };
    assert(popCount(attackers) <= 2);
    if (attackers == 0) {
       return false;
    }

    auto kSq{ square(c|KING) };
    Bitboard bq{ pieces(~c, BSHP, QUEN) & PieceAttackBB[BSHP][kSq] };
    Bitboard rq{ pieces(~c, ROOK, QUEN) & PieceAttackBB[ROOK][kSq] };
    Bitboard mocc{ (pieces() ^ cap) | epSq };
    while (attackers != 0) {
        Bitboard amocc{ mocc ^ popLSq(attackers) };
        // Check enpassant is legal for the position
        if ((bq == 0 || (bq & attacksBB<BSHP>(kSq, amocc)) == 0)
         && (rq == 0 || (rq & attacksBB<ROOK>(kSq, amocc)) == 0)) {
            return true;
        }
    }
    return false;
}

/// Position::see() (Static Exchange Evaluator [SEE] Greater or Equal):
/// Checks the SEE value of move is greater or equal to the given threshold.
/// An algorithm similar to alpha-beta pruning with a null window is used.
bool Position::see(Move m, Value threshold) const {
    assert(isOk(m));

    // Only deal with normal moves, assume others pass a simple SEE
    if (mType(m) != NORMAL) {
        return threshold <= VALUE_ZERO;
    }

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };

    i32 val;
    val = PieceValues[MG][pType(board[dst])] - threshold;
    if (val < 0) {
        return false;
    }

    val = PieceValues[MG][pType(board[org])] - val;
    if (val <= 0) {
        return true;
    }

    Bitboard mocc{ pieces() ^ org ^ dst };
    Bitboard attackers{ attackersTo(dst, mocc) };

    if (attackers == 0) {
        return true;
    }

    bool res{ true };

    auto mov{ pColor(board[org]) };

    Array<Square, COLORS> kSq
    {
        square(W_KING),
        square(B_KING)
    };
    Array<Bitboard, COLORS> kBlockers
    {
        kingBlockers(WHITE),
        kingBlockers(BLACK)
    };
    Array<Bitboard, COLORS> kCheckers
    {
        kingCheckers(WHITE),
        kingCheckers(BLACK)
    };

    while (attackers != 0) {
        mov = ~mov;
        attackers &= mocc;

        Bitboard movAttackers{ attackers & pieces(mov) };

        // If mov has no more attackers then give up: mov loses
        if (movAttackers == 0) {
            break;
        }
        // Update Pinners and Pinneds
        if (contains(kCheckers[mov], org)) {
            kCheckers[mov] ^= org;
            kBlockers[mov] &= ~betweenBB(kSq[mov], org);
        }
        // Don't allow pinned pieces for defensive capture,
        // as long respective pinners are on their original square.
        if (// Pinners
            (kCheckers[mov]
           & pieces(~mov)
           & mocc) != 0) {
            movAttackers &= ~kBlockers[mov];
        }
        else
        // Only allow king for defensive capture to evade the discovered check,
        // as long any discoverers are on their original square.
        if (contains(kBlockers[mov], org)
         && !aligned(kSq[mov], org, dst)
         && (kCheckers[~mov]
           //& pieces(~mov)
           & mocc
           //& attacksBB<QUEN>(kSq[mov], mocc)
           & LineBB[kSq[mov]][org]) != 0) {
            movAttackers = SquareBB[kSq[mov]];
        }

        // If mov has no more attackers then give up: mov loses
        if (movAttackers == 0) {
            break;
        }

        res = !res;

        // Locate and remove the next least valuable attacker, and add to
        // the bitboard 'attackers' any X-ray attackers behind it.
        Bitboard bb;

        if ((bb = pieces(PAWN) & movAttackers) != 0) {
            if ((val = VALUE_MG_PAWN - val) < res) {
                break;
            }
            mocc ^= (org = scanLSq(bb));
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc));
        }
        else
        if ((bb = pieces(NIHT) & movAttackers) != 0) {
            if ((val = VALUE_MG_NIHT - val) < res) {
                break;
            }
            mocc ^= (org = scanLSq(bb));
        }
        else
        if ((bb = pieces(BSHP) & movAttackers) != 0) {
            if ((val = VALUE_MG_BSHP - val) < res) {
                break;
            }
            mocc ^= (org = scanLSq(bb));
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc));
        }
        else
        if ((bb = pieces(ROOK) & movAttackers) != 0) {
            if ((val = VALUE_MG_ROOK - val) < res) {
                break;
            }
            mocc ^= (org = scanLSq(bb));
            attackers |= (pieces(ROOK, QUEN) & attacksBB<ROOK>(dst, mocc));
        }
        else
        if ((bb = pieces(QUEN) & movAttackers) != 0) {
            if ((val = VALUE_MG_QUEN - val) < res) {
                break;
            }
            mocc ^= (org = scanLSq(bb));
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc))
                       | (pieces(ROOK, QUEN) & attacksBB<ROOK>(dst, mocc));
        }
        else { // KING
            // If we "capture" with the king but opponent still has attackers, reverse the result.
            //return (attackers & pieces(~mov)) != 0 ? !res : res;
            return ((attackers & pieces(~mov)) != 0) != res;
        }
    }

    return res;
}

/// Position::clear() clear the position.
void Position::clear() {
    board.fill(NO_PIECE);
    colors.fill(0);
    types.fill(0);
    for (auto &set : squareSet) {
        set.clear();
    }
    npMaterial.fill(VALUE_ZERO);

    cslRookSq.fill(SQ_NONE);
    cslKingPath.fill(0);
    cslRookPath.fill(0);

    sqCastleRight.fill(CR_NONE);

    psq = SCORE_ZERO;
    ply = 0;
    active = COLORS;
    _thread = nullptr;
}

/// Position::setup() initializes the position object with the given FEN string.
/// This function is not very robust - make sure that input FENs are correct,
/// this is assumed to be the responsibility of the GUI.
Position& Position::setup(std::string const &ff, StateInfo &si, Thread *const th) {
    // A FEN string defines a particular position using only the ASCII character set.
    // A FEN string contains six fields separated by a space.
    // 1) Piece placement (from White's perspective).
    //    Each rank is described, starting with rank 8 and ending with rank 1;
    //    within each rank, the contents of each square are described from file A through file H.
    //    Following the Standard Algebraic Notation (SAN),
    //    each piece is identified by a single letter taken from the standard English names.
    //    White pieces are designated using upper-case letters ("PNBRQK") while
    //    Black pieces are designated using lower-case letters ("pnbrqk").
    //    Blank squares are noted using digits 1 through 8 (the number of blank squares),
    //    and "/" separates ranks.
    // 2) Active color. "w" means white, "b" means black - moves next.
    // 3) Castling availability. If neither side can castle, this is "-".
    //    Otherwise, this has one or more letters:
    //    "K" (White can castle  King side).
    //    "Q" (White can castle Queen side).
    //    "k" (Black can castle  King side).
    //    "q" (Black can castle Queen side).
    //    In Chess 960 file "a-h" is used.
    // 4) Enpassant target square(in algebraic notation).
    //    If there's no enpassant target square, this is "-".
    //    If a pawn has just made a 2-square move, this is the position "behind" the pawn.
    //    This is recorded only if there really is a pawn that might have advanced two squares
    //    and if there is a pawn in position to make an enpassant capture legally!!!.
    // 5) Half move clock. This is the number of half moves since the last pawn advance or capture.
    //    This is used to determine if a draw can be claimed under the fifty-move rule.
    // 6) Full move number. The number of the full move.
    //    It starts at 1, and is incremented after Black's move.

    assert(!whiteSpaces(ff));

    clear();
    si.clear();
    _stateInfo = &si;

    std::istringstream iss{ ff };
    iss >> std::noskipws;

    u08 token;
    // 1. Piece placement on Board
    Square sq{ SQ_A8 };
    while ((iss >> token)
        && !isspace(token)) {

        Piece p;
        if ('1' <= token && token <= '8') {
            sq += (token - '0') * EAST;
        }
        else
        if (token == '/') {
            sq += 2 * SOUTH;
        }
        else
        if ((p = toPiece(token)) != NO_PIECE) {
            placePiece(sq, p);
            ++sq;
        }
        //else {
        //    assert(false);
        //}
    }
    assert(count(W_KING) == 1
        && count(B_KING) == 1);

    // 2. Active color
    iss >> token;
    active = toColor(token);

    // 3. Castling availability
    iss >> token;
    while ((iss >> token)
        && !isspace(token)) {
        Color c = isupper(token) ? WHITE : BLACK;
        Piece rook = (c|ROOK);

        Square rookOrg;
        token = char(tolower(token));

        if (token == 'k') {
            for (rookOrg = relativeSq(c, SQ_H1);
                 rook != board[rookOrg];
                 /*&& rookOrg > square(c|KING)*/
                 --rookOrg) {}
        }
        else
        if (token == 'q') {
            for (rookOrg = relativeSq(c, SQ_A1);
                 rook != board[rookOrg];
                 /*&& rookOrg < square(c|KING)*/
                 ++rookOrg) {}
        }
        else
        if ('a' <= token && token <= 'h') {
            rookOrg = makeSquare(toFile(token), relativeRank(c, RANK_1));
        }
        else {
            assert(token == '-');
            continue;
        }

        setCastle(c, rookOrg);
    }

    // 4. Enpassant square. Ignore if no pawn capture is possible.
    u08 file, rank;
    if ((iss >> file && ('a' <= file && file <= 'h'))
     && (iss >> rank && ('3' == rank || rank == '6'))) {
        auto epSq{ makeSquare(toFile(file), toRank(rank)) };
        if (canEnpassant(active, epSq)) {
            _stateInfo->epSquare = epSq;
        }
    }

    // 5-6. Half move clock and Full move number.
    iss >> std::skipws
        >> _stateInfo->clockPly
        >> ply;

    if (epSquare() != SQ_NONE) {
        _stateInfo->clockPly = 0;
    }
    // Rule 50 draw case.
    assert(100 >= clockPly());
    // Convert from moves starting from 1 to ply starting from 0.
    ply = i16(std::max(2 * (ply - 1), 0) + active);
    assert(0 <= gamePly());

    npMaterial[WHITE] = computeNPM<WHITE>(*this);
    npMaterial[BLACK] = computeNPM<BLACK>(*this);

    _stateInfo->matlKey = RandZob.computeMatlKey(*this);
    _stateInfo->pawnKey = RandZob.computePawnKey(*this);
    _stateInfo->posiKey = RandZob.computePosiKey(*this);
    _stateInfo->checkers = attackersTo(square(active|KING)) & pieces(~active);
    setCheckInfo();
    _thread = th;

    assert(ok());
    return *this;
}
/// Position::setup() initializes the position object with the given endgame code string like "KBPKN".
/// It is mainly an helper to get the material key out of an endgame code.
Position& Position::setup(std::string const &code, Color c, StateInfo &si) {
    assert(code[0] == 'K'
        && code.find('K', 1) != std::string::npos);

    Array<std::string, COLORS> codes
    {
        code.substr(code.find('K', 1)),
        code.substr(0, std::min(code.find('v'), code.find('K', 1)))
    };
    assert(0 < codes[WHITE].length() && codes[WHITE].length() < 8);
    assert(0 < codes[BLACK].length() && codes[BLACK].length() < 8);

    toLower(codes[c]);

    std::ostringstream oss;
    oss << "8/"
        << codes[WHITE] << char('0' + 8 - codes[WHITE].length()) << "/8/8/8/8/"
        << codes[BLACK] << char('0' + 8 - codes[BLACK].length()) << "/8 w - - 0 1";

    return setup(oss.str(), si, nullptr);
}

/// Position::doMove() makes a move, and saves all information necessary to a StateInfo object.
/// The move is assumed to be legal.
void Position::doMove(Move m, StateInfo &si, bool isCheck) {
    assert(isOk(m)
        && pseudoLegal(m)
        && legal(m)
        && &si != _stateInfo);

    _thread->nodes.fetch_add(1, std::memory_order::memory_order_relaxed);
    Key pKey{ posiKey()
            ^ RandZob.colorKey };

    // Copy some fields of old state info to new state info object
    std::memcpy(&si, _stateInfo, offsetof(StateInfo, posiKey));
    si.ptr = _stateInfo;
    _stateInfo = &si;

    ++ply;
    ++_stateInfo->clockPly;
    ++_stateInfo->nullPly;
    _stateInfo->promoted = false;

    auto pasive = ~active;

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    assert(contains(pieces(active), org)
        && (!contains(pieces(active), dst)
         || mType(m) == CASTLE));

    auto mp = board[org];
    assert(mp != NO_PIECE);
    auto cp = mType(m) != ENPASSANT ?
                board[dst] : (pasive|PAWN);

    if (mType(m) == CASTLE) {
        assert(mp == (active|KING)
            && cp == (active|ROOK)
            && castleRookSq(active, dst > org ? CS_KING : CS_QUEN) == dst
            && castleExpeded(active, dst > org ? CS_KING : CS_QUEN)
            && relativeRank(active, org) == RANK_1
            && relativeRank(active, dst) == RANK_1
            && canCastle(active, dst > org ? CS_KING : CS_QUEN)
            && _stateInfo->ptr->checkers == 0); //&& (attackersTo(org) & pieces(pasive))

        auto rookOrg{ dst }; // Castling is encoded as "King captures friendly Rook"
        auto rookDst{ rookCastleSq(org, rookOrg) };
        /* king*/dst = kingCastleSq(org, rookOrg);
        // Remove both pieces first since squares could overlap in chess960
        removePiece(org);
        removePiece(rookOrg);
        board[org] = board[rookOrg] = NO_PIECE; // Not done by removePiece()
        placePiece(dst    , mp);
        placePiece(rookDst, cp);
        pKey ^= RandZob.pieceSquareKey[cp][rookOrg]
              ^ RandZob.pieceSquareKey[cp][rookDst];

        cp = NO_PIECE;
    }

    if (cp != NO_PIECE) {
        assert(pType(cp) < KING);

        auto cap = dst;
        if (pType(cp) == PAWN) {
            if (mType(m) == ENPASSANT) {
                cap -= PawnPush[active];

                assert(mp == (active|PAWN)
                    && relativeRank(active, org) == RANK_5
                    && relativeRank(active, dst) == RANK_6
                    && clockPly() == 1
                    && dst == epSquare()
                    && empty(dst) //&& !contains(pieces(), dst)
                    && cp == (pasive|PAWN)
                    && board[cap] == (pasive|PAWN)); //&& contains(pieces(pasive, PAWN), cap));
            }
            _stateInfo->pawnKey ^= RandZob.pieceSquareKey[cp][cap];
        }
        else {
            npMaterial[pasive] -= PieceValues[MG][pType(cp)];
        }

        removePiece(cap);
        if (mType(m) == ENPASSANT) {
            board[cap] = NO_PIECE; // Not done by removePiece()
        }
        pKey ^= RandZob.pieceSquareKey[cp][cap];
        _stateInfo->matlKey ^= RandZob.pieceSquareKey[cp][count(cp)];
        // Reset clock ply counter
        _stateInfo->clockPly = 0;
    }
    // Set capture piece
    _stateInfo->captured = pType(cp);

    // Move the piece. The tricky Chess960 castling is handled earlier
    if (mType(m) != CASTLE) {
        movePiece(org, dst);
    }
    pKey ^= RandZob.pieceSquareKey[mp][org]
          ^ RandZob.pieceSquareKey[mp][dst];

    // Reset enpassant square
    if (epSquare() != SQ_NONE) {
        assert(1 >= clockPly());
        pKey ^= RandZob.enpassantKey[sFile(epSquare())];
        _stateInfo->epSquare = SQ_NONE;
    }

    // Update castling rights
    CastleRight cr;
    if (castleRights() != CR_NONE
     && (cr = (sqCastleRight[org]|sqCastleRight[dst])) != CR_NONE) {
        pKey ^= RandZob.castleRightKey[castleRights() & cr];
        _stateInfo->castleRights &= ~cr;
    }

    if (pType(mp) == PAWN) {

        // Double push pawn
        // Set enpassant square if the moved pawn can be captured
        if (dst == org + PawnPush[active] * 2
         && canEnpassant(pasive, org + PawnPush[active])) {
            _stateInfo->epSquare = org + PawnPush[active];
            pKey ^= RandZob.enpassantKey[sFile(_stateInfo->epSquare)];
        }
        else
        if (mType(m) == PROMOTE) {
            assert(pType(mp) == PAWN
                && relativeRank(active, org) == RANK_7
                && relativeRank(active, dst) == RANK_8);

            auto pp{ active|promoteType(m) };
            // Replace the pawn with the promoted piece
            removePiece(dst);
            placePiece(dst, pp);
            npMaterial[active] += PieceValues[MG][pType(pp)];
            pKey ^= RandZob.pieceSquareKey[mp][dst]
                  ^ RandZob.pieceSquareKey[pp][dst];
            _stateInfo->pawnKey ^= RandZob.pieceSquareKey[mp][dst];
            _stateInfo->matlKey ^= RandZob.pieceSquareKey[mp][count(mp)]
                                 ^ RandZob.pieceSquareKey[pp][count(pp) - 1];
            _stateInfo->promoted = true;
        }

        // Reset clock ply counter
        _stateInfo->clockPly = 0;
        _stateInfo->pawnKey ^= RandZob.pieceSquareKey[mp][org]
                             ^ RandZob.pieceSquareKey[mp][dst];
    }

    assert((attackersTo(square(active|KING)) & pieces(pasive)) == 0);
    // Calculate checkers
    _stateInfo->checkers = isCheck ? attackersTo(square(pasive|KING)) & pieces(active) : 0;
    assert(!isCheck
        || (checkers() != 0
         && popCount(checkers()) <= 2));

    // Switch sides
    active = pasive;
    // Update the key with the final value
    _stateInfo->posiKey = pKey;

    setCheckInfo();

    // Calculate the repetition info. It is the ply distance from the previous
    // occurrence of the same position, negative in the 3-fold case, or zero
    // if the position was not repeated.
    _stateInfo->repetition = 0;
    auto end = std::min(clockPly(), nullPly());
    if (end >= 4) {
        auto const* psi{ _stateInfo->ptr->ptr };
        for (i16 i = 4; i <= end; i += 2) {
            psi = psi->ptr->ptr;
            if (psi->posiKey == posiKey()) {
                _stateInfo->repetition = i * (1 - 2 * (psi->repetition != 0));
                break;
            }
        }
    }

    assert(ok());
}
/// Position::undoMove() unmakes a move, and restores the position to exactly the same state as before the move was made.
/// The move is assumed to be legal.
void Position::undoMove(Move m) {
    assert(isOk(m)
        && _stateInfo->ptr != nullptr);

    active = ~active;

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    assert(empty(org)
        || mType(m) == CASTLE);
    assert(captured() < KING);

    if (mType(m) == CASTLE) {
        assert(relativeRank(active, org) == RANK_1
            && relativeRank(active, dst) == RANK_1
            && captured() == NONE);

        auto rookOrg{ dst }; // Castling is encoded as "King captures friendly Rook"
        auto rookDst{ rookCastleSq(org, rookOrg) };
        /* king*/dst = kingCastleSq(org, rookOrg);
        // Remove both pieces first since squares could overlap in chess960
        removePiece(dst);
        removePiece(rookDst);
        board[dst] = board[rookDst] = NO_PIECE; // Not done by removePiece()
        placePiece(org    , active|KING);
        placePiece(rookOrg, active|ROOK);
    }
    else {

        auto mp = board[dst];
        assert(mp != NO_PIECE
            && pColor(mp) == active);

        if (mType(m) == PROMOTE) {
            assert(NIHT <= pType(mp) && pType(mp) <= QUEN
                && relativeRank(active, org) == RANK_7
                && relativeRank(active, dst) == RANK_8);

            removePiece(dst);
            placePiece(dst, active|PAWN);
            npMaterial[active] -= PieceValues[MG][pType(mp)];
        }
        // Move the piece
        movePiece(dst, org);

        if (captured() != NONE) {

            auto cap{ dst };

            if (mType(m) == ENPASSANT) {

                cap -= PawnPush[active];

                assert(pType(mp) == PAWN //&& contains(pieces(active, PAWN), org)
                    && relativeRank(active, org) == RANK_5
                    && relativeRank(active, dst) == RANK_6
                    && dst == _stateInfo->ptr->epSquare
                    //&& empty(cap)
                    && captured() == PAWN);
            }
            assert(empty(cap));

            // Restore the captured piece.
            placePiece(cap, ~active|captured());

            if (captured() > PAWN) {
                npMaterial[~active] += PieceValues[MG][captured()];
            }
        }
    }

    // Point state pointer back to the previous state.
    _stateInfo = _stateInfo->ptr;

    --ply;

    assert(ok());
}
/// Position::doNullMove() makes a 'null move'.
/// It flips the side to move without executing any move on the board.
void Position::doNullMove(StateInfo &si) {
    assert(&si != _stateInfo
        && checkers() == 0);

    std::memcpy(&si, _stateInfo, sizeof (StateInfo));
    si.ptr = _stateInfo;
    _stateInfo = &si;

    ++_stateInfo->clockPly;
    _stateInfo->nullPly = 0;
    _stateInfo->captured = NONE;
    _stateInfo->promoted = false;

    // Reset enpassant square
    if (epSquare() != SQ_NONE) {
        _stateInfo->posiKey ^= RandZob.enpassantKey[sFile(epSquare())];
        _stateInfo->epSquare = SQ_NONE;
    }

    active = ~active;
    _stateInfo->posiKey ^= RandZob.colorKey;

    setCheckInfo();

    _stateInfo->repetition = 0;

    assert(ok());
}
/// Position::undoNullMove() unmakes a 'null move'.
void Position::undoNullMove() {
    assert(_stateInfo->ptr != nullptr
        && nullPly() == 0
        && captured() == NONE
        && checkers() == 0);

    active = ~active;
    _stateInfo = _stateInfo->ptr;

    assert(ok());
}

/// Position::flip() flips position mean White and Black sides swaped.
/// This is only useful for debugging especially for finding evaluation symmetry bugs.
void Position::flip() {
    std::istringstream iss{ fen() };
    std::string ff, token;
    // 1. Piece placement
    for (Rank r = RANK_8; r >= RANK_1; --r) {
        std::getline(iss, token, r > RANK_1 ? '/' : ' ');
        toggle(token);
        ff.insert(0, token + (r < RANK_8 ? "/" : " "));
    }
    // 2. Active color
    iss >> token;
    ff += toChar(~toColor(token[0]));
    ff += " ";
    // 3. Castling availability
    iss >> token;
    if (token != "-") {
        toggle(token);
    }
    ff += token;
    ff += " ";
    // 4. Enpassant square
    iss >> token;
    if (token != "-") {
        token.replace(1, 1, { 1, toChar(~toRank(token[1])) });
    }
    ff += token;
    // 5-6. Halfmove clock and Fullmove number
    std::getline(iss, token, '\n');
    ff += token;

    setup(ff, *_stateInfo, _thread);

    assert(ok());
}
/// Position::mirror() mirrors position mean King and Queen sides swaped.
void Position::mirror() {
    std::istringstream iss{ fen() };
    std::string ff, token;
    // 1. Piece placement
    for (Rank r = RANK_8; r >= RANK_1; --r) {
        std::getline(iss, token, r > RANK_1 ? '/' : ' ');
        reverse(token);
        ff += token + (r > RANK_1 ? "/" : " ");
    }
    // 2. Active color
    iss >> token;
    ff += token;
    ff += ' ';
    // 3. Castling availability
    iss >> token;
    if (token != "-") {
        for (auto &ch : token) {
            if (Options["UCI_Chess960"]) {
                assert(isalpha(ch));
                ch = toChar(~toFile(char(tolower(ch))), islower(ch));
            }
            else {
            switch (ch) {
            case 'K': ch = 'Q'; break;
            case 'Q': ch = 'K'; break;
            case 'k': ch = 'q'; break;
            case 'q': ch = 'k'; break;
            default: assert(false); break;
            }
            }
        }
    }
    ff += token;
    ff += ' ';
    // 4. Enpassant square
    iss >> token;
    if (token != "-") {
        token.replace(0, 1, { 1, toChar(~toFile(token[0])) });
    }
    ff += token;
    // 5-6. Halfmove clock and Fullmove number
    std::getline(iss, token, '\n');
    ff += token;

    setup(ff, *_stateInfo, _thread);

    assert(ok());
}

/// Position::fen() returns a FEN representation of the position.
/// In case of Chess960 the Shredder-FEN notation is used.
std::string Position::fen(bool full) const {
    std::ostringstream oss;

    for (Rank r = RANK_8; r >= RANK_1; --r) {
        for (File f = FILE_A; f <= FILE_H; ++f) {
            i16 emptyCount;
            for (emptyCount = 0; f <= FILE_H && empty(makeSquare(f, r)); ++f) {
                ++emptyCount;
            }
            if (emptyCount != 0) {
                oss << emptyCount;
            }
            if (f <= FILE_H) {
                oss << board[makeSquare(f, r)];
            }
        }
        if (r > RANK_1) {
            oss << '/';
        }
    }

    oss << ' ' << active << ' ';

    if (castleRights() != CR_NONE) {
        bool chess960{Options["UCI_Chess960"]};
        if (canCastle(WHITE, CS_KING)) { oss << (chess960 ? toChar(sFile(castleRookSq(WHITE, CS_KING)), false) : 'K'); }
        if (canCastle(WHITE, CS_QUEN)) { oss << (chess960 ? toChar(sFile(castleRookSq(WHITE, CS_QUEN)), false) : 'Q'); }
        if (canCastle(BLACK, CS_KING)) { oss << (chess960 ? toChar(sFile(castleRookSq(BLACK, CS_KING)),  true) : 'k'); }
        if (canCastle(BLACK, CS_QUEN)) { oss << (chess960 ? toChar(sFile(castleRookSq(BLACK, CS_QUEN)),  true) : 'q'); }
    }
    else {
        oss << '-';
    }

    oss << ' ' << (epSquare() != SQ_NONE ? ::toString(epSquare()) : "-");

    if (full) {
        oss << ' ' << clockPly() << ' ' << moveCount();
    }

    return oss.str();
}
/// Position::toString() returns an ASCII representation of the position.
std::string Position::toString() const {
    std::ostringstream oss;
    oss << " +---+---+---+---+---+---+---+---+\n";
    for (Rank r = RANK_8; r >= RANK_1; --r) {
        oss << r << "| ";
        for (File f = FILE_A; f <= FILE_H; ++f) {
            oss << board[makeSquare(f, r)] << " | ";
        }
        oss << "\n +---+---+---+---+---+---+---+---+\n";
    }
    for (File f = FILE_A; f <= FILE_H; ++f) {
        oss << "   " << toChar(f, false);
    }
    oss << "\nFEN: " << fen()
        << "\nKey: " << std::uppercase << std::hex << std::setfill('0')
                     << std::setw(16) << posiKey()
                     << std::nouppercase << std::dec << std::setfill(' ');
    oss << "\nCheckers: ";
    for (Bitboard b = checkers(); b != 0; ) {
        oss << popLSq(b) << ' ';
    }
    if (Book.enabled) {
        oss << '\n' << Book.show(*this);
    }
    if (SyzygyTB::MaxPieceLimit >= count()
     && castleRights() == CR_NONE) {
        SyzygyTB::ProbeState wdlState;
        auto wdlScore = SyzygyTB::probeWDL(*const_cast<Position*>(this), wdlState);
        SyzygyTB::ProbeState dtzState;
        auto dtzScore = SyzygyTB::probeDTZ(*const_cast<Position*>(this), dtzState);
        oss << "\nTablebases WDL: " << std::setw(4) << wdlScore << " (" << wdlState << ")"
            << "\nTablebases DTZ: " << std::setw(4) << dtzScore << " (" << dtzState << ")";
    }
    oss << '\n';

    return oss.str();
}

std::ostream& operator<<(std::ostream &os, Position const &pos) {
    os << pos.toString();
    return os;
}

#if !defined(NDEBUG)

/// Position::ok() performs some consistency checks for the position,
/// and raises an assert if something wrong is detected.
bool Position::ok() const {
    constexpr bool Fast = true;

    // BASIC
    if (!isOk(active)
     || (count() > 32
      || count() != popCount(pieces()))) {
        assert(false && "Position OK: BASIC");
        return false;
    }
    for (Color c : { WHITE, BLACK }) {
        if (count(c) > 16
         || count(c) != popCount(pieces(c))
         || std::count(board.begin(), board.end(), (c|KING)) != 1
         || count(c|KING) != 1
         || !isOk(square(c|KING))
         || board[square(c|KING)] != (c|KING)
         || (        (count(c|PAWN)
           + std::max(count(c|NIHT) - 2, 0)
           + std::max(count(c|BSHP) - 2, 0)
           + std::max(count(c|ROOK) - 2, 0)
           + std::max(count(c|QUEN) - 1, 0)) > 8)) {
            assert(false && "Position OK: BASIC");
            return false;
        }
    }
    // BITBOARD
    if ((pieces(WHITE) & pieces(BLACK)) != 0
     || (pieces(WHITE) | pieces(BLACK)) != pieces()
     || (pieces(WHITE) ^ pieces(BLACK)) != pieces()
     || (pieces(PAWN)|pieces(NIHT)|pieces(BSHP)|pieces(ROOK)|pieces(QUEN)|pieces(KING))
     != (pieces(PAWN)^pieces(NIHT)^pieces(BSHP)^pieces(ROOK)^pieces(QUEN)^pieces(KING))
     || (pieces(PAWN) & (RankBB[RANK_1]|RankBB[RANK_8])) != 0
     || popCount(attackersTo(square(~active|KING)) & pieces( active)) != 0
     || popCount(attackersTo(square( active|KING)) & pieces(~active)) > 2) {
        assert(false && "Position OK: BITBOARD");
        return false;
    }
    for (PieceType pt1 = PAWN; pt1 <= KING; ++pt1) {
        for (PieceType pt2 = PAWN; pt2 <= KING; ++pt2) {
            if (pt1 != pt2
             && (pieces(pt1) & pieces(pt2)) != 0) {
                assert(false && "Position OK: BITBOARD");
                return false;
            }
        }
    }
    for (Color c : { WHITE, BLACK }) {
        if (popCount(pieces(c, KING)) != 1
         || (        (popCount(pieces(c, PAWN))
           + std::max(popCount(pieces(c, NIHT)) - 2, 0)
           + std::max(popCount(pieces(c, BSHP)) - 2, 0)
           + std::max(popCount(pieces(c, ROOK)) - 2, 0)
           + std::max(popCount(pieces(c, QUEN)) - 1, 0)) > 8)
         || (        (popCount(pieces(c, PAWN))
           + std::max(popCount(pieces(c, BSHP) & ColorBB[WHITE]) - 1, 0)
           + std::max(popCount(pieces(c, BSHP) & ColorBB[BLACK]) - 1, 0)) > 8)) {
            assert(false && "Position OK: BITBOARD");
            return false;
        }
    }

    // Non-Pawn material & PSQ
    if (nonPawnMaterial(WHITE) != computeNPM<WHITE>(*this)
     || nonPawnMaterial(BLACK) != computeNPM<BLACK>(*this)
     || psqScore() != PSQT::computePSQ(*this)) {
        assert(false && "Position OK: PSQ");
        return false;
    }

    if (Fast) {
        return true;
    }

    // SQUARE_LIST
    for (Piece p : Pieces) {
        if (count(p) != popCount(pieces(pColor(p), pType(p)))) {
            assert(false && "Position OK: SQUARE_LIST");
            return false;
        }
        for (Square s : squares(p)) {
            if (!isOk(s)
             || board[s] != p) {
                assert(false && "Position OK: SQUARE_LIST");
                return false;
            }
        }
    }

    // CASTLING
    for (Color c : { WHITE, BLACK }) {
        for (CastleSide cs : { CS_KING, CS_QUEN }) {
            auto cr{ makeCastleRight(c, cs) };
            if (canCastle(c, cs)
             && (board[castleRookSq(c, cs)] != (c|ROOK)
              ||  sqCastleRight[castleRookSq(c, cs)] != cr
              || (sqCastleRight[square(c|KING)] & cr) != cr)) {
                assert(false && "Position OK: CASTLING");
                return false;
            }
        }
    }
    // STATE_INFO
    if (matlKey() != RandZob.computeMatlKey(*this)
     || pawnKey() != RandZob.computePawnKey(*this)
     || posiKey() != RandZob.computePosiKey(*this)
     || checkers() != (attackersTo(square(active|KING)) & pieces(~active))
     || popCount(checkers()) > 2
     || clockPly() > 2 * i16(Options["Draw MoveCount"])
     || (captured() != NONE
      && clockPly() != 0)
     || (epSquare() != SQ_NONE
      && (clockPly() != 0
       || relativeRank(active, epSquare()) != RANK_6
       || !canEnpassant(active, epSquare())))) {
        assert(false && "Position OK: STATE_INFO");
        return false;
    }

    return true;
}

/// isOk() Check the validity of FEN string
bool isOk(std::string const &fen) {
    Position pos;
    StateInfo si;
    return !whiteSpaces(fen)
        && pos.setup(fen, si, nullptr).ok();
}

#endif
