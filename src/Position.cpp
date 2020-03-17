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
    captured = NONE;
    checkers = 0;
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
/// Position::movePosiKey() computes the new hash key after the given moven.
/// Needed for speculative prefetch.
Key Position::movePosiKey(Move m) const {
    assert(isOk(m)
        && pseudoLegal(m)
        && legal(m));

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    auto pKey{ posiKey() ^ RandZob.colorKey };
    if (CASTLE == mType(m)) {
        pKey ^= RandZob.pieceSquareKey[active|ROOK][dst]
              ^ RandZob.pieceSquareKey[active|ROOK][rookCastleSq(org, dst)];
    }
    else {
        auto cp{ ENPASSANT != mType(m) ? board[dst] : ~active|PAWN };
        if (NO_PIECE != cp) {
            pKey ^= RandZob.pieceSquareKey[cp][ENPASSANT != mType(m) ? dst : dst - PawnPush[active]];
        }
        else
        if (PAWN == pType(board[org])
         && dst == org + 2 * PawnPush[active]) {
            auto epSq{ org + PawnPush[active] };
            if (canEnpassant(~active, epSq, false)) {
                pKey ^= RandZob.enpassantKey[sFile(epSq)];
            }
        }
    }
    if (SQ_NONE != epSquare()) {
        pKey ^= RandZob.enpassantKey[sFile(epSquare())];
    }
    return pKey
         ^ RandZob.pieceSquareKey[board[org]][org]
         ^ RandZob.pieceSquareKey[PROMOTE != mType(m) ? board[org] : active|promoteType(m)][CASTLE != mType(m) ? dst : kingCastleSq(org, dst)]
         ^ RandZob.castleRightKey[castleRights() & (sqCastleRight[org]|sqCastleRight[dst])];
}

/// Position::draw() checks whether position is drawn by: Clock Ply Rule, Repetition.
/// It does not detect Insufficient materials and Stalemate.
bool Position::draw(i16 pp) const {
    return  // Draw by Clock Ply Rule?
            // Not in check or in check have legal moves
           (clockPly() >= 2 * i16(Options["Draw MoveCount"])
         && (0 == checkers()
          || 0 != MoveList<GenType::LEGAL>(*this).size()))
            // Draw by Repetition?
            // Return a draw score if a position repeats once earlier but strictly
            // after the root, or repeats twice before or at the root.
        || (0 != repetition()
         && pp > repetition());
}

/// Position::repeated() tests whether there has been at least one repetition of positions since the last capture or pawn move.
bool Position::repeated() const {
    auto end{ std::min(clockPly(), nullPly()) };
    auto const *csi{ _stateInfo };
    while (end-- >= 4) {
        if (0 != csi->repetition) {
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
            if (0 == (betweenBB(cuckoo.sq1, cuckoo.sq2) & pieces())) {

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
                if (0 != psi->repetition) {
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
    Bitboard mocc{pieces() ^ snipers};
    while (0 != snipers) {
        auto sniperSq{ popLSq(snipers) };
        Bitboard b{ betweenBB(s, sniperSq) & mocc };
        if (0 != b
         && !moreThanOne(b)) {
            blockers |= b;
            if (0 != (b & defenders)) {
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

    if (CASTLE == mType(m)) {

        auto cs{ dst > org ? CS_KING : CS_QUEN };
        return (active|KING) == board[org] //&& contains(pieces(active, KING), org)
            && (active|ROOK) == board[dst] //&& contains(pieces(active, ROOK), dst)
            && 0 == chkrs
            && castleRookSq(active, cs) == dst
            && castleExpeded(active, cs)
            //&& RANK_1 == relativeRank(active, org)
            //&& RANK_1 == relativeRank(active, dst)
            && canCastle(active, cs);
    }

    // The captured square cannot be occupied by a friendly piece
    if (contains(pieces(active), dst)) {
        return false;
    }

    // Handle the special case of a piece move
    if (PAWN == pType(board[org])) {
        auto orgR{ relativeRank(active, org) };
        auto dstR{ relativeRank(active, dst) };

        if (// Single push
            (((NORMAL != mType(m)
            || RANK_2 > orgR || orgR > RANK_6
            || RANK_3 > dstR || dstR > RANK_7)
           && (PROMOTE != mType(m)
            || RANK_7 != orgR
            || RANK_8 != dstR))
          || dst != org + 1 * PawnPush[active]
          || !empty(dst))
            // Normal capture
         && (((NORMAL != mType(m)
            || RANK_2 > orgR || orgR > RANK_6
            || RANK_3 > dstR || dstR > RANK_7)
           && (PROMOTE != mType(m)
            || RANK_7 != orgR
            || RANK_8 != dstR))
          || !contains(pawnAttacksFrom(active, org), dst)
          || empty(dst))
            // Double push
         && (NORMAL != mType(m)
          || RANK_2 != orgR
          || RANK_4 != dstR
          || dst != org + 2 * PawnPush[active]
          || !empty(dst)
          || !empty(dst - 1 * PawnPush[active]))
            // Enpassant capture
         && (ENPASSANT != mType(m)
          || RANK_5 != orgR
          || RANK_6 != dstR
          || dst != epSquare()
          || !contains(pawnAttacksFrom(active, org), dst)
          || !empty(dst)
          || empty(dst - 1 * PawnPush[active])
          || 0 != clockPly())) {
            return false;
        }
    }
    else {
        if (NORMAL != mType(m)
         || !contains(pieceAttacksFrom(pType(board[org]), org), dst)) {
            return false;
        }
    }

    // Evasions generator already takes care to avoid some kind of illegal moves and legal() relies on this.
    // So have to take care that the same kind of moves are filtered out here.
    if (0 != chkrs) {
        auto fkSq = square(active|KING);
        // In case of king moves under check, remove king so to catch
        // as invalid moves like B1A1 when opposite queen is on C1.
        if (fkSq == org) {
            return 0 == (attackersTo(dst, pieces() ^ fkSq) & pieces(~active));
        }
        // Double check? In this case a king move is required
        if (moreThanOne(chkrs)) {
            return false;
        }
        return ENPASSANT != mType(m) ?
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
    auto fkSq = square(active|KING);
    assert(contains(pieces(active), org));

    switch (mType(m)) {
    case NORMAL:
        // Only king moves to non attacked squares, sliding check x-rays the king
        // In case of king moves under check have to remove king so to catch
        // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
        // check whether the destination square is attacked by the opponent.
        if (fkSq == org) {
            return 0 == (attackersTo(dst, pieces() ^ fkSq) & pieces(~active));
        }
        /* fall through */
    case PROMOTE:
        assert(NORMAL == mType(m)
            || ((active|PAWN) == board[org] //&& contains(pieces(active, PAWN), org)
             && RANK_7 == relativeRank(active, org)
             && RANK_8 == relativeRank(active, dst)));

        // A non-king move is legal if and only if
        // - not pinned
        // - moving along the ray from the king
        return !contains(kingBlockers(active), org)
            || aligned(org, dst, fkSq);
        break;
    case CASTLE: {
        assert((active|KING) == board[org] //&& contains(pieces(active, KING), org)
            && (active|ROOK) == board[dst] //&& contains(pieces(active, ROOK), dst)
            && castleRookSq(active, dst > org ? CS_KING : CS_QUEN) == dst
            && castleExpeded(active, dst > org ? CS_KING : CS_QUEN)
            //&& RANK_1 == relativeRank(active, org)
            //&& RANK_1 == relativeRank(active, dst)
            && canCastle(active, dst > org ? CS_KING : CS_QUEN)
            && 0 == checkers());
        // Castle is always encoded as "King captures friendly Rook".
        Bitboard b{ castleKingPath(active, dst > org ? CS_KING : CS_QUEN) };
        // Check king's path for attackers.
        while (0 != b) {
            if (0 != (attackersTo(popLSq(b)) & pieces(~active))) {
                return false;
            }
        }
        // In case of Chess960, verify that when moving the castling rook we do not discover some hidden checker.
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        return !Options["UCI_Chess960"]
            || 0 == (pieces(~active, ROOK, QUEN)
                   & RankBB[relativeRank(active, RANK_1)]
                   & attacksBB<ROOK>(kingCastleSq(org, dst), pieces() ^ dst));
    }
    case ENPASSANT: {
        // Enpassant captures are a tricky special case. Because they are rather uncommon,
        // do it simply by testing whether the king is attacked after the move is made.
        assert((active|PAWN) == board[org] //&& contains(pieces(active, PAWN), org)
            && RANK_5 == relativeRank(active, org)
            && RANK_6 == relativeRank(active, dst)
            && 0 == clockPly()
            && dst == epSquare()
            && empty(dst) //&& !contains(pieces(), dst)
            && (~active|PAWN) == board[dst - PawnPush[active]]); //&& contains(pieces(~active, PAWN), dst - PawnPush[active])
        Bitboard mocc{ (pieces() ^ org ^ (dst - PawnPush[active])) | dst };
        // If any attacker then in check and not legal move.
        return 0 == (pieces(~active, BSHP, QUEN) & attacksBB<BSHP>(fkSq, mocc))
            && 0 == (pieces(~active, ROOK, QUEN) & attacksBB<ROOK>(fkSq, mocc));
    }
    default: assert(false); return false;
    }
}

/// Position::giveCheck() tests whether a pseudo-legal move gives a check.
bool Position::giveCheck(Move m) const {
    assert(isOk(m));

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    assert(contains(pieces(active), org));

    if (// Direct check ?
        contains(checks(PROMOTE != mType(m) ? pType(board[org]) : promoteType(m)), dst)
        // Discovered check ?
     || (contains(kingBlockers(~active), org)
      && !aligned(org, dst, square(~active|KING)))) {
        return true;
    }

    switch (mType(m))
    {
    case NORMAL:
        return false;
    case CASTLE: {
        // Castling with check?
        auto kingDst{ kingCastleSq(org, dst) };
        auto rookDst{ rookCastleSq(org, dst) };
        Bitboard mocc{ (pieces() ^ org ^ dst) | kingDst | rookDst };
        return contains(attacksBB<ROOK>(rookDst, mocc), square(~active|KING));
    }
    case ENPASSANT: {
        // Enpassant capture with check?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        auto epSq{ makeSquare(sFile(dst), sRank(org)) };
        Bitboard mocc{ (pieces() ^ org ^ epSq) | dst };
        return 0 != (pieces(active, BSHP, QUEN) & attacksBB<BSHP>(square(~active|KING), mocc))
            || 0 != (pieces(active, ROOK, QUEN) & attacksBB<ROOK>(square(~active|KING), mocc));
    }
    case PROMOTE: {
        // Promotion with check?
        Bitboard mocc{ pieces() ^ org };
        return contains(attacksBB(promoteType(m), dst, mocc), square(~active|KING));
    }
    default: assert(false); return false;
    }
}

/// Position::setCastle() set the castling right.
void Position::setCastle(Color c, Square rookOrg) {
    auto kingOrg{ square(c|KING) };
    assert(isOk(rookOrg)
        && RANK_1 == relativeRank(c, kingOrg)
        && RANK_1 == relativeRank(c, rookOrg)
        && (c|ROOK) == board[rookOrg]); //&& contains(pieces(c, ROOK), rookOrg)

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
    _stateInfo->checks[PAWN] = pawnAttacksFrom(~active, ekSq);
    _stateInfo->checks[NIHT] = pieceAttacksFrom(NIHT, ekSq);
    _stateInfo->checks[BSHP] = pieceAttacksFrom(BSHP, ekSq);
    _stateInfo->checks[ROOK] = pieceAttacksFrom(ROOK, ekSq);
    _stateInfo->checks[QUEN] = _stateInfo->checks[BSHP]|_stateInfo->checks[ROOK];
    _stateInfo->checks[KING] = 0;
}

/// Position::canEnpassant() Can the enpassant possible.
bool Position::canEnpassant(Color c, Square epSq, bool moveDone) const {
    assert(isOk(epSq)
        && RANK_6 == relativeRank(c, epSq));
    auto cap{ moveDone ? epSq - PawnPush[c] : epSq + PawnPush[c] };
    assert((~c|PAWN) == board[cap]); //contains(pieces(~c, PAWN), cap));
    // Enpassant attackers
    Bitboard attackers{ pieces(c, PAWN)
                      & pawnAttacksFrom(~c, epSq) };
    assert(2 >= popCount(attackers));
    if (0 == attackers) {
       return false;
    }

    auto kSq{ square(c|KING) };
    Bitboard bq{ pieces(~c, BSHP, QUEN) & PieceAttackBB[BSHP][kSq] };
    Bitboard rq{ pieces(~c, ROOK, QUEN) & PieceAttackBB[ROOK][kSq] };
    Bitboard mocc{ (pieces() ^ cap) | epSq };
    while (0 != attackers) {
        auto org{ popLSq(attackers) };
        assert(contains(mocc, org));
        // Check enpassant is legal for the position
        if ((0 == bq || 0 == (bq & attacksBB<BSHP>(kSq, mocc ^ org)))
         && (0 == rq || 0 == (rq & attacksBB<ROOK>(kSq, mocc ^ org)))) {
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
    if (NORMAL != mType(m)) {
        return VALUE_ZERO >= threshold;
    }

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };

    i32 swap;
    swap = PieceValues[MG][pType(board[dst])] - threshold;
    if (0 > swap) {
        return false;
    }

    swap = PieceValues[MG][pType(board[org])] - swap;
    if (0 >= swap) {
        return true;
    }

    bool res{ true };

    Bitboard mocc{ pieces() ^ org ^ dst };
    auto mov{ pColor(board[org]) };

    Bitboard attackers{ attackersTo(dst, mocc) };
    while (0 != attackers) {
        mov = ~mov;
        attackers &= mocc;

        Bitboard movAttackers{ attackers & pieces(mov) };

        // If mov has no more attackers then give up: mov loses
        if (0 == movAttackers) {
            break;
        }

        // Only allow king for defensive capture to evade the discovered check,
        // as long any discoverers are on their original square.
        if (contains(kingBlockers(mov) & pieces(~mov), org)
         && 0 != (kingCheckers(~mov)
                & pieces(~mov)
                & mocc
                & attacksBB<QUEN>(square(mov|KING), mocc))) {
            movAttackers &= pieces(KING);
        }
        // Don't allow pinned pieces for defensive capture,
        // as long respective pinners are on their original square.
        else {
            // Pinned Attackers
            Bitboard pinned{ kingBlockers(mov) & movAttackers };
            while (0 != pinned) {
                auto sq{ popLSq(pinned) };
                if (0 != (kingCheckers(mov)
                        & pieces(~mov)
                        & mocc
                        & attacksBB<QUEN>(square(mov|KING), mocc ^ sq))) {
                    movAttackers ^= sq;
                }
            }
        }

        // If mov has no more attackers then give up: mov loses
        if (0 == movAttackers) {
            break;
        }

        res = !res;

        // Locate and remove the next least valuable attacker, and add to
        // the bitboard 'attackers' any X-ray attackers behind it.
        Bitboard bb;

        if (0 != (bb = pieces(PAWN) & movAttackers)) {
            if ((swap = VALUE_MG_PAWN - swap) < 1*res) {
                break;
            }
            org = scanLSq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(NIHT) & movAttackers)) {
            if ((swap = VALUE_MG_NIHT - swap) < 1*res) {
                break;
            }
            org = scanLSq(bb);
            mocc ^= org;
        }
        else
        if (0 != (bb = pieces(BSHP) & movAttackers)) {
            if ((swap = VALUE_MG_BSHP - swap) < 1*res) {
                break;
            }
            org = scanLSq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(ROOK) & movAttackers)) {
            if ((swap = VALUE_MG_ROOK - swap) < 1*res) {
                break;
            }
            org = scanLSq(bb);
            mocc ^= org;
            attackers |= (pieces(ROOK, QUEN) & attacksBB<ROOK>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(QUEN) & movAttackers)) {
            if ((swap = VALUE_MG_QUEN - swap) < 1*res) {
                break;
            }
            org = scanLSq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc))
                       | (pieces(ROOK, QUEN) & attacksBB<ROOK>(dst, mocc));
        }
        else { // KING
            // If we "capture" with the king but opponent still has attackers, reverse the result.
            //return res != (0 != (attackers & pieces(~mov)));
            return 0 != (attackers & pieces(~mov)) ? !res : res;
        }
    }

    return res;
}

/// Position::clear() clear the position.
void Position::clear() {
    board.fill(NO_PIECE);
    colors.fill(0);
    types.fill(0);
    for (auto &list : pieceList) {
        list.clear();
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

void Position::placePiece(Square s, Piece p) {
    assert(isOk(p)
        && std::count(pieceList[p].begin(), pieceList[p].end(), s) == 0);
    colors[pColor(p)] |= s;
    types[pType(p)] |= s;
    types[NONE] |= s;
    pieceList[p].emplace_back(s);
    psq += PSQ[p][s];
    board[s] = p;
}
void Position::removePiece(Square s) {
    auto p{ board[s] };
    assert(isOk(p)
        && 1 == std::count(pieceList[p].begin(), pieceList[p].end(), s));
    colors[pColor(p)] ^= s;
    types[pType(p)] ^= s;
    types[NONE] ^= s;
    pieceList[p].remove(s);
    psq -= PSQ[p][s];
    //board[s] = NO_PIECE; // Not needed, overwritten by the capturing one
}
void Position::movePiece(Square s1, Square s2) {
    auto p{ board[s1] };
    assert(isOk(p)
        && 1 == std::count(pieceList[p].begin(), pieceList[p].end(), s1)
        && 0 == std::count(pieceList[p].begin(), pieceList[p].end(), s2));
    Bitboard bb{ s1 | s2 };
    colors[pColor(p)] ^= bb;
    types[pType(p)] ^= bb;
    types[NONE] ^= bb;
    std::replace(pieceList[p].begin(), pieceList[p].end(), s1, s2);
    psq += PSQ[p][s2]
         - PSQ[p][s1];
    board[s2] = p;
    board[s1] = NO_PIECE;
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
    assert(1 == count(W_KING)
        && 1 == count(B_KING));

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

        if ('k' == token) {
            for (rookOrg = relativeSq(c, SQ_H1);
                 rook != board[rookOrg];
                 /*&& rookOrg > square(c|KING)*/
                 --rookOrg) {}
        }
        else
        if ('q' == token) {
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
            assert('-' == token);
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

    if (SQ_NONE != epSquare()) {
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

    auto pasive = ~active;

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    assert(contains(pieces(active), org)
        && (!contains(pieces(active), dst)
         || CASTLE == mType(m)));

    auto mp = board[org];
    assert(NO_PIECE != mp);
    auto cp = ENPASSANT != mType(m) ?
                board[dst] : pasive|PAWN;

    if (CASTLE == mType(m)) {
        assert((active|KING) == mp
            && (active|ROOK) == cp
            && castleRookSq(active, dst > org ? CS_KING : CS_QUEN) == dst
            && castleExpeded(active, dst > org ? CS_KING : CS_QUEN)
            && RANK_1 == relativeRank(active, org)
            && RANK_1 == relativeRank(active, dst)
            && canCastle(active, dst > org ? CS_KING : CS_QUEN)
            && 0 == _stateInfo->ptr->checkers); //&& (attackersTo(org) & pieces(pasive))

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

    if (NO_PIECE != cp) {
        assert(KING != pType(cp));

        auto cap = dst;
        if (PAWN == pType(cp)) {
            if (ENPASSANT == mType(m)) {
                cap -= PawnPush[active];

                assert(PAWN == pType(mp)
                    && RANK_5 == relativeRank(active, org)
                    && RANK_6 == relativeRank(active, dst)
                    && 1 == clockPly()
                    && dst == epSquare()
                    && empty(dst) //&& !contains(pieces(), dst)
                    && cp == board[cap]); //&& contains(pieces(pasive, PAWN), cap));
            }
            _stateInfo->pawnKey ^= RandZob.pieceSquareKey[cp][cap];
        }
        else {
            npMaterial[pasive] -= PieceValues[MG][pType(cp)];
        }

        removePiece(cap);
        if (ENPASSANT == mType(m)) {
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
    if (CASTLE != mType(m)) {
        movePiece(org, dst);
    }
    pKey ^= RandZob.pieceSquareKey[mp][org]
          ^ RandZob.pieceSquareKey[mp][dst];

    // Reset enpassant square
    if (SQ_NONE != epSquare()) {
        assert(1 >= clockPly());
        pKey ^= RandZob.enpassantKey[sFile(epSquare())];
        _stateInfo->epSquare = SQ_NONE;
    }

    // Update castling rights
    CastleRight cr;
    if (CR_NONE != castleRights()
     && CR_NONE != (cr = (sqCastleRight[org]|sqCastleRight[dst]))) {
        pKey ^= RandZob.castleRightKey[castleRights() & cr];
        _stateInfo->castleRights &= ~cr;
    }

    if (PAWN == pType(mp)) {
        // Double push pawn
        if (relativeRank(active, RANK_2) == sRank(org)
         && dst == org + 2 * PawnPush[active]) {
            auto epSq{ org + 1 * PawnPush[active] };
            // Set enpassant square if the moved pawn can be captured
            if (canEnpassant(pasive, epSq)) {
                _stateInfo->epSquare = epSq;
                pKey ^= RandZob.enpassantKey[sFile(epSq)];
            }
        }
        else
        if (PROMOTE == mType(m)) {
            assert(PAWN == pType(mp)
                && RANK_7 == relativeRank(active, org)
                && RANK_8 == relativeRank(active, dst));

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
        }

        // Reset clock ply counter
        _stateInfo->clockPly = 0;
        _stateInfo->pawnKey ^= RandZob.pieceSquareKey[mp][org]
                             ^ RandZob.pieceSquareKey[mp][dst];
    }

    if (PAWN == pType(mp)
     || PAWN == pType(cp)
     || PROMOTE == mType(m)) {
        prefetch(_thread->pawnHash[pawnKey()]);
    }
    if (NO_PIECE != cp
     || PROMOTE == mType(m)) {
        prefetch(_thread->matlHash[matlKey()]);
    }

    // Update the key with the final value
    _stateInfo->posiKey = pKey;
    //prefetch(TT.cluster(posiKey())->entryTable);

    assert(0 == (attackersTo(square(active|KING)) & pieces(pasive)));
    // Calculate checkers
    _stateInfo->checkers = isCheck ? attackersTo(square(pasive|KING)) & pieces(active) : 0;
    assert(!isCheck
        || (0 != checkers()
         && 2 >= popCount(checkers())));

    // Switch sides
    active = pasive;

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
                _stateInfo->repetition = 0 != psi->repetition ? -i : i;
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
        && nullptr != _stateInfo->ptr);

    active = ~active;

    auto org{ orgSq(m) };
    auto dst{ dstSq(m) };
    assert(empty(org)
        || CASTLE == mType(m));
    assert(KING != captured());

    if (CASTLE == mType(m)) {
        assert(RANK_1 == relativeRank(active, org)
            && RANK_1 == relativeRank(active, dst)
            && NONE == captured());

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
        assert(NO_PIECE != mp
            && active == pColor(mp));

        if (PROMOTE == mType(m)) {
            assert(NIHT <= pType(mp) && pType(mp) <= QUEN
                && RANK_7 == relativeRank(active, org)
                && RANK_8 == relativeRank(active, dst));

            mp = active|PAWN;
            removePiece(dst);
            placePiece(dst, mp);
            npMaterial[active] -= PieceValues[MG][promoteType(m)];
        }
        // Move the piece
        movePiece(dst, org);

        if (NONE != captured()) {

            auto cap{ dst };

            if (ENPASSANT == mType(m)) {

                cap -= PawnPush[active];

                assert(PAWN == pType(mp) //&& contains(pieces(active, PAWN), org)
                    && RANK_5 == relativeRank(active, org)
                    && RANK_6 == relativeRank(active, dst)
                    && dst == _stateInfo->ptr->epSquare
                    //&& empty(cap)
                    && PAWN == captured());
            }
            assert(empty(cap));

            // Restore the captured piece.
            placePiece(cap, ~active|captured());

            if (PAWN != captured()) {

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
        && 0 == checkers());

    std::memcpy(&si, _stateInfo, sizeof (StateInfo));
    si.ptr = _stateInfo;
    _stateInfo = &si;

    ++_stateInfo->clockPly;
    _stateInfo->nullPly = 0;
    _stateInfo->captured = NONE;

    // Reset enpassant square.
    if (SQ_NONE != epSquare()) {
        _stateInfo->posiKey ^= RandZob.enpassantKey[sFile(epSquare())];
        _stateInfo->epSquare = SQ_NONE;
    }

    _stateInfo->posiKey ^= RandZob.colorKey;
    prefetch(TT.cluster(posiKey())->entryTable);

    active = ~active;

    setCheckInfo();

    _stateInfo->repetition = 0;

    assert(ok());
}
/// Position::undoNullMove() unmakes a 'null move'.
void Position::undoNullMove() {
    assert(nullptr != _stateInfo->ptr
        && 0 == nullPly()
        && NONE == captured()
        && 0 == checkers());

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
            if (0 != emptyCount) {
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

    if (CR_NONE != castleRights()) {
        bool chess960{Options["UCI_Chess960"]};
        if (canCastle(WHITE, CS_KING)) { oss << (chess960 ? toChar(sFile(castleRookSq(WHITE, CS_KING)), false) : 'K'); }
        if (canCastle(WHITE, CS_QUEN)) { oss << (chess960 ? toChar(sFile(castleRookSq(WHITE, CS_QUEN)), false) : 'Q'); }
        if (canCastle(BLACK, CS_KING)) { oss << (chess960 ? toChar(sFile(castleRookSq(BLACK, CS_KING)),  true) : 'k'); }
        if (canCastle(BLACK, CS_QUEN)) { oss << (chess960 ? toChar(sFile(castleRookSq(BLACK, CS_QUEN)),  true) : 'q'); }
    }
    else {
        oss << '-';
    }

    oss << ' ' << (SQ_NONE != epSquare() ? ::toString(epSquare()) : "-");

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
    for (Bitboard b = checkers(); 0 != b; ) {
        oss << popLSq(b) << ' ';
    }
    if (Book.enabled) {
        oss << '\n' << Book.show(*this);
    }
    if (SyzygyTB::MaxPieceLimit >= count()
     && CR_NONE == castleRights()) {
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
         || 1 != std::count(board.begin(), board.end(), (c|KING))
         || 1 != count(c|KING)
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
     || 0 != (pieces(PAWN) & (RankBB[RANK_1]|RankBB[RANK_8]))
     || 0 != popCount(attackersTo(square(~active|KING)) & pieces( active))
     || 2 <  popCount(attackersTo(square( active|KING)) & pieces(~active))) {
        assert(false && "Position OK: BITBOARD");
        return false;
    }
    for (PieceType pt1 = PAWN; pt1 <= KING; ++pt1) {
        for (PieceType pt2 = PAWN; pt2 <= KING; ++pt2) {
            if (pt1 != pt2
             && 0 != (pieces(pt1) & pieces(pt2))) {
                assert(false && "Position OK: BITBOARD");
                return false;
            }
        }
    }
    for (Color c : { WHITE, BLACK }) {
        if (1 != popCount(pieces(c, KING))
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
     || 2 < popCount(checkers())
     || clockPly() > 2 * i16(Options["Draw MoveCount"])
     || (NONE != captured()
      && 0 != clockPly())
     || (SQ_NONE != epSquare()
      && (0 != clockPly()
       || RANK_6 != relativeRank(active, epSquare())
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
