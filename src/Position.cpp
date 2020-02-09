#include "Position.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "MoveGenerator.h"
#include "Notation.h"
#include "Option.h"
#include "Polyglot.h"
#include "PSQTable.h"
#include "TBsyzygy.h"
#include "Thread.h"
#include "Transposition.h"
#include "Zobrist.h"

using namespace std;
using namespace BitBoard;
using namespace TBSyzygy;

namespace {

    /// Computes the non-pawn middle game material value for the given side.
    /// Material values are updated incrementally during the search.
    template<Color Own>
    Value computeNPM(const Position &pos)
    {
        auto npm = VALUE_ZERO;
        for (auto pt : { NIHT, BSHP, ROOK, QUEN })
        {
            npm += PieceValues[MG][pt] * pos.count(Own|pt);
        }
        return npm;
    }
    /// Explicit template instantiations
    /// --------------------------------
    template Value computeNPM<WHITE>(const Position&);
    template Value computeNPM<BLACK>(const Position&);

    // Marcel van Kervink's cuckoo algorithm for fast detection of "upcoming repetition".
    // Description of the algorithm in the following paper:
    // https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

    struct Cuckoo
    {
        Key  key;   // Zobrist key
        Move move;  // Valid reversible move

        Cuckoo() = default;
        Cuckoo(Key k, Move m)
            : key(k)
            , move(m)
        {}

        bool empty() const
        {
            return 0 == key
                || MOVE_NONE == move;
        }
    };

    // Cuckoo table
    array<Cuckoo, 0x2000> Cuckoos;

    // Hash functions for indexing the cuckoo tables

    inline u16 H1(Key key) { return u16((key >> 0x00) & (Cuckoos.size() - 1)); }
    inline u16 H2(Key key) { return u16((key >> 0x10) & (Cuckoos.size() - 1)); }

}

void Position::initialize()
{
    // Prepare the Cuckoo tables
    Cuckoos.fill({0, MOVE_NONE});
    u16 count = 0;
    for (auto c : { WHITE, BLACK })
    {
        for (auto pt : { NIHT, BSHP, ROOK, QUEN, KING })
        {
            for (auto org : SQ)
            {
                for (auto dst = Square(org + 1); dst <= SQ_H8; ++dst)
                {
                    if (contains(PieceAttacks[pt][org], dst))
                    {
                        Cuckoo cuckoo( RandZob.pieceSquareKey[c][pt][org]
                                     ^ RandZob.pieceSquareKey[c][pt][dst]
                                     ^ RandZob.colorKey,
                                       makeMove<NORMAL>(org, dst));

                        u16 i = H1(cuckoo.key);
                        while (true)
                        {
                            std::swap(Cuckoos[i], cuckoo);
                            // Arrived at empty slot ?
                            if (cuckoo.empty())
                            {
                                break;
                            }
                            // Push victim to alternative slot
                            i = i == H1(cuckoo.key) ?
                                    H2(cuckoo.key) :
                                    H1(cuckoo.key);
                        }
                        ++count;
                    }
                }
            }
        }
    }
    assert(3668 == count);
}


Key Position::pgKey() const
{
    return PolyZob.computePosiKey(*this);
}
/// Position::move_posi_key() computes the new hash key after the given moven.
/// Needed for speculative prefetch.
Key Position::movePosiKey(Move m) const
{
    assert(isOk(m)
        && pseudoLegal(m)
        && legal(m));

    auto org = orgSq(m);
    auto dst = dstSq(m);
    auto key = si->posiKey;
    if (CASTLE == mType(m))
    {
        key ^= RandZob.pieceSquareKey[active][ROOK][dst]
             ^ RandZob.pieceSquareKey[active][ROOK][relSq(active, dst > org ? SQ_F1 : SQ_D1)];
    }
    else
    {
        auto cpt = ENPASSANT != mType(m) ? pType(piece[dst]) : PAWN;
        if (NONE != cpt)
        {
            key ^= RandZob.pieceSquareKey[~active][cpt][ENPASSANT != mType(m) ? dst : dst - pawnPush(active)];
        }
        else
        if (   PAWN == pType(piece[org])
            && dst == org + 2 * pawnPush(active))
        {
            auto epSq = org + pawnPush(active);
            if (canEnpassant(~active, epSq, false))
            {
                key ^= RandZob.enpassantKey[sFile(epSq)];
            }
        }
    }
    if (SQ_NO != si->enpassantSq)
    {
        key ^= RandZob.enpassantKey[sFile(si->enpassantSq)];
    }
    return key
         ^ RandZob.colorKey
         ^ RandZob.pieceSquareKey[active][pType(piece[org])][org]
         ^ RandZob.pieceSquareKey[active][PROMOTE != mType(m) ? pType(piece[org]) : promoteType(m)][CASTLE != mType(m) ? dst : relSq(active, dst > org ? SQ_G1 : SQ_C1)]
         ^ RandZob.castleRightKey[si->castleRights & (castleRights[org] | castleRights[dst])];
}

/// Position::draw() checks whether position is drawn by: Clock Ply Rule, Repetition.
/// It does not detect Insufficient materials and Stalemate.
bool Position::draw(i16 pp) const
{
    return
            // Draw by Clock Ply Rule?
            // Not in check or in check have legal moves
           (   si->clockPly >= 2 * i32(Options["Draw MoveCount"])
            && (   0 == si->checkers
                || 0 != MoveList<GenType::LEGAL>(*this).size()))
            // Draw by Repetition?
            // Return a draw score if a position repeats once earlier but strictly
            // after the root, or repeats twice before or at the root.
        || (   0 != si->repetition
            && pp > si->repetition);
}

/// Position::repeated() tests whether there has been at least one repetition of positions since the last capture or pawn move.
bool Position::repeated() const
{
    const auto *csi = si;
    auto end = std::min(csi->clockPly, csi->nullPly);
    while (end-- >= 4)
    {
        if (0 != csi->repetition)
        {
            return true;
        }
        csi = csi->ptr;
    }
    return false;
}

/// Position::cycled() tests if the position has a move which draws by repetition,
/// or an earlier position has a move that directly reaches the current position.
bool Position::cycled(i16 pp) const
{
    auto end = std::min(si->clockPly, si->nullPly);
    if (end < 3)
    {
        return false;
    }

    Key posiKey = si->posiKey;
    const auto *psi = si->ptr;

    for (u08 p = 3; p <= end; p += 2)
    {
        psi = psi->ptr->ptr;

        u16 j;
        Key moveKey = posiKey ^ psi->posiKey;
        if (   (j = H1(moveKey), moveKey == Cuckoos[j].key)
            || (j = H2(moveKey), moveKey == Cuckoos[j].key))
        {
            auto move = Cuckoos[j].move;
            auto org = orgSq(move);
            auto dst = dstSq(move);
            if (0 == (betweens(org, dst) & pieces()))
            {
                if (p < pp)
                {
                    return true;
                }
                // For nodes before or at the root, check that the move is a repetition one
                // rather than a move to the current position
                // In the cuckoo table, both moves Rc1c5 and Rc5c1 are stored in the same location.
                // Select the legal one by swaping if necessary.
                //if (empty(org))
                //{
                //    std::swap(org, dst);
                //}
                //assert(!empty(org));
                if (pColor(piece[empty(org) ? dst : org]) != active)
                {
                    continue;
                }
                // For repetitions before or at the root, require one more
                if (0 != psi->repetition)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

/// Position::sliderBlockersAt() returns a bitboard of all the pieces that are blocking attacks on the square.
/// King-attack piece can be either pinner or hidden piece.
Bitboard Position::sliderBlockersAt(Square s, Bitboard attackers, Bitboard &pinners, Bitboard &hidders) const
{
    Bitboard blockers = 0;

    Bitboard defenders = pieces(pColor(piece[s]));
    // Snipers are X-ray slider attackers at 's'
    // No need to remove direct attackers at 's' as in check no evaluation
    Bitboard snipers = attackers
                     & (  (pieces(BSHP, QUEN) & PieceAttacks[BSHP][s])
                        | (pieces(ROOK, QUEN) & PieceAttacks[ROOK][s]));
    Bitboard mocc = pieces() ^ snipers;
    while (0 != snipers)
    {
        auto sniperSq = popLSq(snipers);
        Bitboard b = betweens(s, sniperSq) & mocc;
        if (   0 != b
            && !moreThanOne(b))
        {
            blockers |= b;
            if (0 != (b & defenders))
            {
                pinners |= sniperSq;
            }
            else
            {
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

    auto org = orgSq(m);
    auto dst = dstSq(m);
    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if (!contains(pieces(active), org))
    {
        return false;
    }

    if (CASTLE == mType(m))
    {
        auto cs = dst > org ? CS_KING : CS_QUEN;

        return (active|KING) == piece[org] //&& contains(pieces(active, KING), org)
            && (active|ROOK) == piece[dst] //&& contains(pieces(active, ROOK), dst)
            && castleRookSq[active][cs] == dst
            && castleExpeded(active, cs)
            //&& R_1 == relRank(active, org)
            //&& R_1 == relRank(active, dst)
            && canCastle(makeCastleRight(active, cs))
            && 0 == si->checkers;
    }

    // The captured square cannot be occupied by a friendly piece
    if (contains(pieces(active), dst))
    {
        return false;
    }
    auto mpt = pType(piece[org]);
    // Handle the special case of a piece move
    if (PAWN == mpt)
    {
        auto orgR = relRank(active, org);
        auto dstR = relRank(active, dst);

        if (    // Single push
               (   (   (   NORMAL != mType(m)
                        || R_2 > orgR || orgR > R_6
                        || R_3 > dstR || dstR > R_7)
                    && (   PROMOTE != mType(m)
                        || R_7 != orgR
                        || R_8 != dstR))
                || dst != org + 1 * pawnPush(active)
                || !empty(dst))
                // Normal capture
            && (   (   (   NORMAL != mType(m)
                        || R_2 > orgR || orgR > R_6
                        || R_3 > dstR || dstR > R_7)
                    && (   PROMOTE != mType(m)
                        || R_7 != orgR
                        || R_8 != dstR))
                || !contains(PawnAttacks[active][org], dst)
                || empty(dst))
                // Double push
            && (   NORMAL != mType(m)
                || R_2 != orgR
                || R_4 != dstR
                || dst != org + 2 * pawnPush(active)
                || !empty(dst)
                || !empty(dst - 1 * pawnPush(active)))
                // Enpassant capture
            && (   ENPASSANT != mType(m)
                || R_5 != orgR
                || R_6 != dstR
                || dst != si->enpassantSq
                || !contains(PawnAttacks[active][org], dst)
                || !empty(dst)
                || empty(dst - 1 * pawnPush(active))
                || 0 != si->clockPly))
        {
            return false;
        }
    }
    else
    if (   NORMAL != mType(m)
        || !contains(attacksFrom(mpt, org), dst))
    {
        return false;
    }

    // Evasions generator already takes care to avoid some kind of illegal moves and legal() relies on this.
    // So have to take care that the same kind of moves are filtered out here.
    if (0 != si->checkers)
    {
        // In case of king moves under check, remove king so to catch
        // as invalid moves like B1A1 when opposite queen is on C1.
        if (KING == mpt)
        {
            return 0 == (attackersTo(dst, pieces() ^ org) & pieces(~active));
        }
        // Double check? In this case a king move is required
        if (moreThanOne(si->checkers))
        {
            return false;
        }
        return ENPASSANT != mType(m) ?
                // Move must be a capture of the checking piece or a blocking evasion of the checking piece
                contains(si->checkers | betweens(scanLSq(si->checkers), square(active|KING)), dst) :
                // Move must be a capture of the checking Enpassant pawn or a blocking evasion of the checking piece
                   (   0 != (si->checkers & pieces(~active, PAWN))
                    && contains(si->checkers, dst - pawnPush(active)))
                || contains(betweens(scanLSq(si->checkers), square(active|KING)), dst);
    }
    return true;
}
/// Position::legal() tests whether a pseudo-legal move is legal.
bool Position::legal(Move m) const
{
    assert(isOk(m));

    auto org = orgSq(m);
    auto dst = dstSq(m);
    assert(contains(pieces(active), org));

    switch (mType(m))
    {
    case NORMAL:
        // Only king moves to non attacked squares, sliding check x-rays the king
        // In case of king moves under check have to remove king so to catch
        // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
        // check whether the destination square is attacked by the opponent.
        if (KING == pType(piece[org]))
        {
            return 0 == (attackersTo(dst, pieces() ^ org) & pieces(~active));
        }
        /* fall through */
    case PROMOTE:
        assert(NORMAL == mType(m)
            || (   (active|PAWN) == piece[org] //&& contains(pieces(active, PAWN), org)
                && R_7 == relRank(active, org)
                && R_8 == relRank(active, dst)));

        // A non-king move is legal if and only if
        // - not pinned
        // - moving along the ray from the king
        return !contains(si->kingBlockers[active], org)
            || squaresAligned(org, dst, square(active|KING));
        break;
    case CASTLE:
    {
        assert((active|KING) == piece[org] //&& contains(pieces(active, KING), org)
            && (active|ROOK) == piece[dst] //&& contains(pieces(active, ROOK), dst)
            && castleRookSq[active][dst > org ? CS_KING : CS_QUEN] == dst
            && castleExpeded(active, dst > org ? CS_KING : CS_QUEN)
            //&& R_1 == relRank(active, org)
            //&& R_1 == relRank(active, dst)
            && canCastle(makeCastleRight(active, dst > org ? CS_KING : CS_QUEN))
            && 0 == si->checkers);
        // Castle is always encoded as "King captures friendly Rook".
        Bitboard b = castleKingPath[active][dst > org ? CS_KING : CS_QUEN];
        // Check king's path for attackers.
        while (0 != b)
        {
            if (0 != (attackersTo(popLSq(b)) & pieces(~active)))
            {
                return false;
            }
        }
        // In case of Chess960, verify that when moving the castling rook we do not discover some hidden checker.
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        return !bool(Options["UCI_Chess960"])
            || 0 == (  pieces(~active, ROOK, QUEN)
                     & rankBB(relRank(active, R_1))
                     & attacksBB<ROOK>(relSq(active, dst > org ? SQ_G1 : SQ_C1), pieces() ^ dst));
    }
    case ENPASSANT:
    {
        // Enpassant captures are a tricky special case. Because they are rather uncommon,
        // do it simply by testing whether the king is attacked after the move is made.
        assert((active|PAWN) == piece[org] //&& contains(pieces(active, PAWN), org)
            && R_5 == relRank(active, org)
            && R_6 == relRank(active, dst)
            && 0 == si->clockPly
            && dst == si->enpassantSq
            && empty(dst) //&& !contains(pieces(), dst)
            && (~active|PAWN) == piece[dst - pawnPush(active)]); //&& contains(pieces(~active, PAWN), dst - pawnPush(active))
        Bitboard mocc = (pieces() ^ org ^ (dst - pawnPush(active))) | dst;
        // If any attacker then in check and not legal move.
        return 0 == (pieces(~active, BSHP, QUEN) & attacksBB<BSHP>(square(active|KING), mocc))
            && 0 == (pieces(~active, ROOK, QUEN) & attacksBB<ROOK>(square(active|KING), mocc));
    }
    default: assert(false); return false;
    }
}

bool Position::fullLegal(Move m) const
{
    return (   ENPASSANT != mType(m)
            && !contains(si->kingBlockers[active] | square(active|KING), orgSq(m)))
        || legal(m);
}
/// Position::giveCheck() tests whether a pseudo-legal move gives a check.
bool Position::giveCheck(Move m) const
{
    assert(isOk(m));

    auto org = orgSq(m);
    auto dst = dstSq(m);
    assert(contains(pieces(active), org));

    if (    // Direct check ?
           contains(si->checks[PROMOTE != mType(m) ? pType(piece[org]) : promoteType(m)], dst)
            // Discovered check ?
        || (   contains(si->kingBlockers[~active], org)
            && !squaresAligned(org, dst, square(~active|KING))))
    {
        return true;
    }

    switch (mType(m))
    {
    case NORMAL:
        return false;
    case CASTLE:
    {
        // Castling with check?
        auto kingDst = relSq(active, dst > org ? SQ_G1 : SQ_C1);
        auto rookDst = relSq(active, dst > org ? SQ_F1 : SQ_D1);
        return contains(attacksBB<ROOK>(rookDst, (pieces() ^ org ^ dst) | kingDst | rookDst), square(~active|KING));
    }
    case ENPASSANT:
    {
        // Enpassant capture with check?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        auto epSq = makeSquare(sFile(dst), sRank(org));
        Bitboard mocc = (pieces() ^ org ^ epSq) | dst;
        return 0 != (pieces(active, BSHP, QUEN) & attacksBB<BSHP>(square(~active|KING), mocc))
            || 0 != (pieces(active, ROOK, QUEN) & attacksBB<ROOK>(square(~active|KING), mocc));
    }
    case PROMOTE:
    {
        // Promotion with check?
        return contains(attacksFrom(promoteType(m), dst, pieces() ^ org), square(~active|KING));
    }
    default: assert(false); return false;
    }
}

/// Position::setCastle() set the castling right.
void Position::setCastle(Color c, Square rookOrg)
{
    assert(isOk(rookOrg)
        && R_1 == relRank(c, rookOrg)
        && (c|ROOK) == piece[rookOrg]); //&& contains(pieces(c, ROOK), rookOrg)

    auto kingOrg = square(c|KING);
    assert(R_1 == relRank(c, kingOrg));
    auto cs = rookOrg > kingOrg ? CS_KING : CS_QUEN;
    castleRookSq[c][cs] = rookOrg;

    auto kingDst = relSq(c, rookOrg > kingOrg ? SQ_G1 : SQ_C1);
    auto rookDst = relSq(c, rookOrg > kingOrg ? SQ_F1 : SQ_D1);
    auto cr = makeCastleRight(c, cs);
    si->castleRights      |= cr;
    castleRights[kingOrg] |= cr;
    castleRights[rookOrg] |= cr;

    castleKingPath[c][cs] = (betweens(kingOrg, kingDst) | kingDst)
                          & ~(squareBB(kingOrg));
    castleRookPath[c][cs] = (betweens(kingOrg, kingDst) | betweens(rookOrg, rookDst) | kingDst | rookDst)
                          & ~(squareBB(kingOrg) | squareBB(rookOrg));
}
/// Position::setCheckInfo() sets check info used for fast check detection.
void Position::setCheckInfo()
{
    si->kingCheckers[WHITE] = 0;
    si->kingCheckers[BLACK] = 0;
    si->kingBlockers[WHITE] = sliderBlockersAt(square(WHITE|KING), pieces(BLACK), si->kingCheckers[WHITE], si->kingCheckers[BLACK]);
    si->kingBlockers[BLACK] = sliderBlockersAt(square(BLACK|KING), pieces(WHITE), si->kingCheckers[BLACK], si->kingCheckers[WHITE]);

    si->checks[PAWN] = PawnAttacks[~active][square(~active|KING)];
    si->checks[NIHT] = PieceAttacks[NIHT][square(~active|KING)];
    si->checks[BSHP] = attacksBB<BSHP>(square(~active|KING), pieces());
    si->checks[ROOK] = attacksBB<ROOK>(square(~active|KING), pieces());
    si->checks[QUEN] = si->checks[BSHP]
                     | si->checks[ROOK];
    si->checks[KING] = 0;
}

/// Position::canEnpassant() Can the enpassant possible.
bool Position::canEnpassant(Color c, Square epSq, bool moveDone) const
{
    assert(isOk(epSq)
        && R_6 == relRank(c, epSq));
    auto cap = moveDone ?
                epSq - pawnPush(c) :
                epSq + pawnPush(c);
    assert((~c|PAWN) == piece[cap]); //contains(pieces(~c, PAWN), cap));
    // Enpassant attackers
    Bitboard attackers = pieces(c, PAWN) & PawnAttacks[~c][epSq];
    if (0 == attackers)
    {
        return false;
    }
    assert(2 >= popCount(attackers));
    Bitboard mocc = (pieces() ^ cap) | epSq;
    auto k_sq = square(c|KING);
    Bitboard bq = pieces(~c, BSHP, QUEN) & PieceAttacks[BSHP][k_sq];
    Bitboard rq = pieces(~c, ROOK, QUEN) & PieceAttacks[ROOK][k_sq];
    //if (   0 == bq
    //    && 0 == rq)
    //{
    //    return true;
    //}
    while (0 != attackers)
    {
        auto org = popLSq(attackers);
        assert(contains(mocc, org));
        // Check enpassant is legal for the position
        if (   0 == (bq & attacksBB<BSHP>(k_sq, mocc ^ org))
            && 0 == (rq & attacksBB<ROOK>(k_sq, mocc ^ org)))
        {
            return true;
        }
    }
    return false;
}

/// Position::see() (Static Exchange Evaluator [SEE] Greater or Equal):
/// Checks the SEE value of move is greater or equal to the given threshold.
/// An algorithm similar to alpha-beta pruning with a null window is used.
bool Position::see(Move m, Value threshold) const
{
    assert(isOk(m));

    // Only deal with normal moves, assume others pass a simple SEE
    if (NORMAL != mType(m))
    {
        return VALUE_ZERO >= threshold;
    }

    auto org = orgSq(m);
    auto dst = dstSq(m);

    i32 swap;
    swap = PieceValues[MG][pType(piece[dst])] - threshold;
    if (0 > swap)
    {
        return false;
    }

    swap = PieceValues[MG][pType(piece[org])] - swap;
    if (0 >= swap)
    {
        return true;
    }

    bool res = true;

    Bitboard mocc = pieces() ^ org ^ dst;
    auto mov = pColor(piece[org]);

    Bitboard attackers = attackersTo(dst, mocc);
    while (0 != attackers)
    {
        mov = ~mov;
        attackers &= mocc;

        Bitboard movAttackers = attackers & pieces(mov);

        // If mov has no more attackers then give up: mov loses
        if (0 == movAttackers)
        {
            break;
        }

        // Only allow king for defensive capture to evade the discovered check,
        // as long any discoverers are on their original square.
        if (   contains(si->kingBlockers[mov] & pieces(~mov), org)
            && (  si->kingCheckers[~mov]
                & pieces(~mov)
                & mocc
                & attacksBB<QUEN>(square(mov|KING), mocc)) != 0)
        {
            movAttackers &= pieces(KING);
        }
        // Don't allow pinned pieces for defensive capture,
        // as long respective pinners are on their original square.
        else
        {
            Bitboard movPinnedAttackers = si->kingBlockers[mov] & movAttackers;
            while (0 != movPinnedAttackers)
            {
                auto sq = popLSq(movPinnedAttackers);
                if ((  si->kingCheckers[mov]
                     & pieces(~mov)
                     & mocc
                     & attacksBB<QUEN>(square(mov|KING), mocc ^ sq)) != 0)
                {
                    movAttackers ^= sq;
                }
            }
        }

        // If mov has no more attackers then give up: mov loses
        if (0 == movAttackers)
        {
            break;
        }

        res = !res;

        // Locate and remove the next least valuable attacker, and add to
        // the bitboard 'attackers' any X-ray attackers behind it.

        Bitboard bb;

        if (0 != (bb = pieces(PAWN) & movAttackers))
        {
            swap = VALUE_MG_PAWN - swap;
            if (swap < res)
            {
                break;
            }

            org = scanLSq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(NIHT) & movAttackers))
        {
            swap = VALUE_MG_NIHT - swap;
            if (swap < res)
            {
                break;
            }

            org = scanLSq(bb);
            mocc ^= org;
        }
        else
        if (0 != (bb = pieces(BSHP) & movAttackers))
        {
            swap = VALUE_MG_BSHP - swap;
            if (swap < res)
            {
                break;
            }

            org = scanLSq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(ROOK) & movAttackers))
        {
            swap = VALUE_MG_ROOK - swap;
            if (swap < res)
            {
                break;
            }

            org = scanLSq(bb);
            mocc ^= org;
            attackers |= (pieces(ROOK, QUEN) & attacksBB<ROOK>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(QUEN) & movAttackers))
        {
            swap = VALUE_MG_QUEN - swap;
            if (swap < res)
            {
                break;
            }

            org = scanLSq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacksBB<BSHP>(dst, mocc))
                       | (pieces(ROOK, QUEN) & attacksBB<ROOK>(dst, mocc));
        }
        else // KING
        {
            // If we "capture" with the king but opponent still has attackers, reverse the result.
            return res != (0 != (attackers & pieces(~mov)));
        }
    }

    return res;
}

/// Position::clear() clear the position.
void Position::clear()
{
    piece.fill(NO_PIECE);
    colors.fill(0);
    types.fill(0);

    castleRights.fill(CR_NONE);

    for (auto &sqs : squares) { sqs.clear(); }
    for (auto &crs : castleRookSq) { crs.fill(SQ_NO); }
    for (auto &ckp : castleKingPath) { ckp.fill(0); }
    for (auto &crp : castleRookPath) { crp.fill(0); }

    psq = SCORE_ZERO;
    ply = 0;
    active = CLR_NO;
    thread = nullptr;
}

void Position::placePiece(Square s, Piece p)
{
    assert(isOk(p)
        && std::count(squares[p].begin(), squares[p].end(), s) == 0);
    colors[pColor(p)] |= s;
    types[pType(p)] |= s;
    types[NONE] |= s;
    squares[p].push_back(s);
    psq += PSQ[p][s];
    piece[s] = p;
}
void Position::removePiece(Square s)
{
    auto p = piece[s];
    assert(isOk(p)
        && std::count(squares[p].begin(), squares[p].end(), s) == 1);
    colors[pColor(p)] ^= s;
    types[pType(p)] ^= s;
    types[NONE] ^= s;
    squares[p].remove(s);
    psq -= PSQ[p][s];
    //piece[s] = NO_PIECE; // Not needed, overwritten by the capturing one
}
void Position::movePiece(Square s1, Square s2)
{
    auto p = piece[s1];
    assert(isOk(p)
        && std::count(squares[p].begin(), squares[p].end(), s1) == 1
        && std::count(squares[p].begin(), squares[p].end(), s2) == 0);
    Bitboard bb = s1 | s2;
    colors[pColor(p)] ^= bb;
    types[pType(p)] ^= bb;
    types[NONE] ^= bb;
    std::replace(squares[p].begin(), squares[p].end(), s1, s2);
    psq += PSQ[p][s2] - PSQ[p][s1];
    piece[s2] = p;
    piece[s1] = NO_PIECE;
}


/// Position::setup() initializes the position object with the given FEN string.
/// This function is not very robust - make sure that input FENs are correct,
/// this is assumed to be the responsibility of the GUI.
Position& Position::setup(const string &ff, StateInfo &nsi, Thread *const th)
{
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

    assert(!ff.empty());

    clear();
    std::memset(&nsi, 0, sizeof (StateInfo));
    nsi.capture = NONE;
    //nsi.promote = NONE;
    si = &nsi;

    istringstream iss{ ff };
    iss >> noskipws;

    u08 token;
    // 1. Piece placement on Board
    size_t idx;
    Square sq = SQ_A8;
    while (   (iss >> token)
           && !isspace(token))
    {
        if (isdigit(token)
        && ('1' <= token && token <= '8'))
        {
            sq += Delta(token - '0');
        }
        else
        if (token == '/')
        {
            sq += 2 * DEL_S;
        }
        else
        if ((idx = PieceChar.find(token)) != string::npos)
        {
            placePiece(sq, Piece(idx));
            ++sq;
        }
        else
        {
            assert(false);
        }
    }
    assert(1 == count(WHITE|KING)
        && 1 == count(BLACK|KING));

    // 2. Active color
    iss >> token;
    active = Color(ColorChar.find(token));

    // 3. Castling availability
    iss >> token;
    while (   (iss >> token)
           && !isspace(token))
    {
        if (token == '-')
        {
            continue;
        }

        Color c = isupper(token) ? WHITE : BLACK;
        assert(R_1 == relRank(c, square(c|KING)));
        Piece rook = (c|ROOK);
        auto rookOrg = SQ_NO;
        token = char(tolower(token));
        switch (token)
        {
        case 'k':
            rookOrg = relSq(c, SQ_H1);
            while (rook != piece[rookOrg]
              /*&& rookOrg > square(c|KING)*/)
            {
                --rookOrg;
            }
            break;
        case 'q':
            rookOrg = relSq(c, SQ_A1);
            while (rook != piece[rookOrg]
              /*&& rookOrg < square(c|KING)*/)
            {
                ++rookOrg;
            }
            break;
        // Chess960
        case 'a': case 'b': case 'c': case 'd':
        case 'e': case 'f': case 'g': case 'h':
            rookOrg = makeSquare(toFile(token), relRank(c, R_1));
            break;
        default:
            assert(false);
            break;
        }
        setCastle(c, rookOrg);
    }

    // 4. Enpassant square. Ignore if no pawn capture is possible.
    u08 file, rank;
    if (   (iss >> file && ('a' <= file && file <= 'h'))
        && (iss >> rank && ('3' == rank || rank == '6')))
    {
        auto epSq = makeSquare(toFile(file), toRank(rank));
        if (canEnpassant(active, epSq))
        {
            si->enpassantSq = epSq;
        }
    }
    else
    {
        si->enpassantSq = SQ_NO;
    }

    // 5-6. Half move clock and Full move number.
    iss >> skipws
        >> si->clockPly
        >> ply;

    if (SQ_NO != si->enpassantSq)
    {
        si->clockPly = 0;
    }
    // Rule 50 draw case.
    assert(100 >= si->clockPly);
    // Convert from moves starting from 1 to ply starting from 0.
    ply = i16(std::max(2 * (ply - 1), 0) + active);

    thread = th;
    si->npm[WHITE] = computeNPM<WHITE>(*this);
    si->npm[BLACK] = computeNPM<BLACK>(*this);
    si->matlKey = RandZob.computeMatlKey(*this);
    si->pawnKey = RandZob.computePawnKey(*this);
    si->posiKey = RandZob.computePosiKey(*this);
    si->checkers = attackersTo(square(active|KING)) & pieces(~active);
    setCheckInfo();

    assert(ok());
    return *this;
}
/// Position::setup() initializes the position object with the given endgame code string like "KBPKN".
/// It is mainly an helper to get the material key out of an endgame code.
Position& Position::setup(const string &code, Color c, StateInfo &nsi)
{
    assert(0 < code.size() && code.size() <= 8
        && code[0] == 'K'
        && code.find('K', 1) != string::npos);

    array<string, CLR_NO> sides
    {
        code.substr(   code.find('K', 1)), // Weak
        code.substr(0, code.find('K', 1))  // Strong
    };
    assert(8 >= sides[WHITE].size()
        && 8 >= sides[BLACK].size());

    toLower(sides[c]);
    string fen = "8/" + sides[WHITE] + char('0' + 8 - sides[WHITE].size()) + "/8/8/8/8/"
                      + sides[BLACK] + char('0' + 8 - sides[BLACK].size()) + "/8 w - -";

    return setup(fen, nsi, nullptr);
}

/// Position::doMove() makes a move, and saves all information necessary to a StateInfo object.
/// The move is assumed to be legal.
void Position::doMove(Move m, StateInfo &nsi, bool giveCheck)
{
    assert(isOk(m)
        && &nsi != si);

    thread->nodes.fetch_add(1, std::memory_order::memory_order_relaxed);
    Key posiKey = si->posiKey ^ RandZob.colorKey;

    // Copy some fields of old state info to new state info object
    std::memcpy(&nsi, si, offsetof(StateInfo, posiKey));
    nsi.ptr = si;
    si = &nsi;

    ++ply;
    ++si->clockPly;
    ++si->nullPly;

    auto org = orgSq(m);
    auto dst = dstSq(m);
    assert(contains(pieces(active), org)
        && (!contains(pieces(active), dst)
         || CASTLE == mType(m)));

    auto mpt = pType(piece[org]);
    assert(NONE != mpt);
    auto pasive = ~active;

    if (CASTLE == mType(m))
    {
        assert((active|KING) == piece[org] //&& contains(pieces(active, KING), org)
            && (active|ROOK) == piece[dst] //&& contains(pieces(active, ROOK), dst)
            && castleRookSq[active][dst > org ? CS_KING : CS_QUEN] == dst
            && castleExpeded(active, dst > org ? CS_KING : CS_QUEN)
            //&& R_1 == relRank(active, org)
            //&& R_1 == relRank(active, dst)
            && canCastle(makeCastleRight(active, dst > org ? CS_KING : CS_QUEN))
            && 0 == si->ptr->checkers); //&& (attackersTo(org) & pieces(pasive))

        si->capture = NONE;
        auto rookOrg = dst; // Castling is encoded as "King captures friendly Rook"
        auto rookDst = relSq(active, rookOrg > org ? SQ_F1 : SQ_D1);
        /* king*/dst = relSq(active, rookOrg > org ? SQ_G1 : SQ_C1);
        // Remove both pieces first since squares could overlap in chess960
        removePiece(org);
        removePiece(rookOrg);
        piece[org] = piece[rookOrg] = NO_PIECE; // Not done by removePiece()
        placePiece(dst    , active|KING);
        placePiece(rookDst, active|ROOK);
        posiKey ^= RandZob.pieceSquareKey[active][ROOK][rookOrg]
                 ^ RandZob.pieceSquareKey[active][ROOK][rookDst];
    }
    else
    {
        auto capture = ENPASSANT != mType(m) ? pType(piece[dst]) : PAWN;
        if (NONE != capture)
        {
            assert(KING != capture);

            auto cap = dst;
            if (PAWN == capture)
            {
                if (ENPASSANT == mType(m))
                {
                    cap -= pawnPush(active);

                    assert(PAWN == mpt
                        && R_5 == relRank(active, org)
                        && R_6 == relRank(active, dst)
                        && 1 == si->clockPly
                        && dst == si->enpassantSq
                        && empty(dst) //&& !contains(pieces(), dst)
                        && (pasive|PAWN) == piece[cap]); //&& contains(pieces(pasive, PAWN), cap));
                }

                si->pawnKey ^= RandZob.pieceSquareKey[pasive][PAWN][cap];
            }
            else
            {
                si->npm[pasive] -= PieceValues[MG][capture];
            }

            // Reset clock ply counter
            si->clockPly = 0;
            removePiece(cap);
            if (ENPASSANT == mType(m))
            {
                piece[cap] = NO_PIECE; // Not done by removePiece()
            }
            posiKey ^= RandZob.pieceSquareKey[pasive][capture][cap];
            si->matlKey ^= RandZob.pieceSquareKey[pasive][capture][count(pasive|capture)];
            prefetch(thread->matlTable[si->matlKey]);
        }
        // Set capture piece
        si->capture = capture;
        // Move the piece
        movePiece(org, dst);
    }
    posiKey ^= RandZob.pieceSquareKey[active][mpt][org]
             ^ RandZob.pieceSquareKey[active][mpt][dst];

    // Reset enpassant square
    if (SQ_NO != si->enpassantSq)
    {
        assert(1 >= si->clockPly);
        posiKey ^= RandZob.enpassantKey[sFile(si->enpassantSq)];
        si->enpassantSq = SQ_NO;
    }

    // Update castling rights
    CastleRight cr;
    if (   CR_NONE != si->castleRights
        && CR_NONE != (cr = (castleRights[org]|castleRights[dst])))
    {
        posiKey ^= RandZob.castleRightKey[si->castleRights & cr];
        si->castleRights &= ~cr;
    }

    //auto promote = NONE;
    if (PAWN == mpt)
    {
        if (PROMOTE == mType(m))
        {
            assert(PAWN == mpt
                && R_7 == relRank(active, org)
                && R_8 == relRank(active, dst));

            auto promote = promoteType(m);
            // Replace the pawn with the promoted piece
            removePiece(dst);
            placePiece(dst, active|promote);
            si->npm[active] += PieceValues[MG][promote];
            posiKey ^= RandZob.pieceSquareKey[active][PAWN][dst]
                     ^ RandZob.pieceSquareKey[active][promote][dst];
            si->pawnKey ^= RandZob.pieceSquareKey[active][PAWN][dst];
            si->matlKey ^= RandZob.pieceSquareKey[active][PAWN][count(active|PAWN)]
                         ^ RandZob.pieceSquareKey[active][promote][count(active|promote) - 1];
            prefetch(thread->matlTable[si->matlKey]);
        }
        else
        // Double push pawn
        if (dst == org + 2 * pawnPush(active))
        {
            auto epSq = org + 1 * pawnPush(active);
            // Set enpassant square if the moved pawn can be captured
            if (canEnpassant(pasive, epSq))
            {
                si->enpassantSq = epSq;
                posiKey ^= RandZob.enpassantKey[sFile(si->enpassantSq)];
            }
        }

        // Reset clock ply counter
        si->clockPly = 0;
        si->pawnKey ^= RandZob.pieceSquareKey[active][PAWN][org]
                     ^ RandZob.pieceSquareKey[active][PAWN][dst];
        //prefetch(thread->pawnTable[si->pawnKey]);
    }
    //si->promote = promote;

    assert(0 == (attackersTo(square(active|KING)) & pieces(pasive)));

    // Calculate checkers
    si->checkers = giveCheck ? attackersTo(square(pasive|KING)) & pieces(active) : 0;
    assert(!giveCheck
        || (0 != si->checkers
         && 2 >= popCount(si->checkers)));

    // Switch sides
    active = pasive;
    // Update the key with the final value
    si->posiKey = posiKey;

    // Calculate the repetition info. It is the ply distance from the previous
    // occurrence of the same position, negative in the 3-fold case, or zero
    // if the position was not repeated.
    si->repetition = 0;
    auto end = std::min(si->clockPly, si->nullPly);
    if (end >= 4)
    {
        const auto* psi = si->ptr->ptr;
        for (i16 i = 4; i <= end; i += 2)
        {
            psi = psi->ptr->ptr;
            if (psi->posiKey == si->posiKey)
            {
                si->repetition = 0 != psi->repetition ? -i : i;
                break;
            }
        }
    }

    setCheckInfo();

    assert(ok());
}
/// Position::undoMove() unmakes a move, and restores the position to exactly the same state as before the move was made.
/// The move is assumed to be legal.
void Position::undoMove(Move m)
{
    assert(isOk(m)
        && nullptr != si->ptr
        && KING != si->capture);

    auto org = orgSq(m);
    auto dst = dstSq(m);
    assert(empty(org)
        || CASTLE == mType(m));

    active = ~active;

    if (CASTLE == mType(m))
    {
        assert(R_1 == relRank(active, org)
            && R_1 == relRank(active, dst)
            && NONE == si->capture);
            //&& NONE == si->promote);

        auto rookOrg = dst; // Castling is encoded as "King captures friendly Rook"
        auto rookDst = relSq(active, rookOrg > org ? SQ_F1 : SQ_D1);
        /* king*/dst = relSq(active, rookOrg > org ? SQ_G1 : SQ_C1);
        // Remove both pieces first since squares could overlap in chess960
        removePiece(dst);
        removePiece(rookDst);
        piece[dst] = piece[rookDst] = NO_PIECE; // Not done by removePiece()
        placePiece(org    , active|KING);
        placePiece(rookOrg, active|ROOK);
    }
    else
    {
        if (PROMOTE == mType(m))
        {
            assert(R_7 == relRank(active, org)
                && R_8 == relRank(active, dst));
                //&& promoteType(m) == si->promote);

            removePiece(dst);
            placePiece(dst, active|PAWN);
        }
        // Move the piece
        movePiece(dst, org);

        if (NONE != si->capture)
        {
            auto cap = dst;

            if (ENPASSANT == mType(m))
            {
                cap -= pawnPush(active);

                assert((active|PAWN) == piece[org] //&& contains(pieces(active, PAWN), org)
                    && R_5 == relRank(active, org)
                    && R_6 == relRank(active, dst)
                    && dst == si->ptr->enpassantSq
                    && PAWN == si->capture);
            }
            // Restore the captured piece.
            assert(empty(cap));
            placePiece(cap, ~active|si->capture);
        }
    }

    // Point state pointer back to the previous state.
    si = si->ptr;
    --ply;

    assert(ok());
}
/// Position::doNullMove() makes a 'null move'.
/// It flips the side to move without executing any move on the board.
void Position::doNullMove(StateInfo &nsi)
{
    assert(&nsi != si
        && 0 == si->checkers);

    std::memcpy(&nsi, si, sizeof (StateInfo));
    nsi.ptr = si;
    si = &nsi;

    ++si->clockPly;
    si->nullPly = 0;
    si->capture = NONE;
    //si->promote = NONE;
    // Reset enpassant square.
    if (SQ_NO != si->enpassantSq)
    {
        si->posiKey ^= RandZob.enpassantKey[sFile(si->enpassantSq)];
        si->enpassantSq = SQ_NO;
    }

    active = ~active;
    si->posiKey ^= RandZob.colorKey;

    prefetch(TT.cluster(si->posiKey)->entries);

    si->repetition = 0;
    setCheckInfo();

    assert(ok());
}
/// Position::undoNullMove() unmakes a 'null move'.
void Position::undoNullMove()
{
    assert(nullptr != si->ptr
        && 0 == si->nullPly
        && NONE == si->capture
        //&& NONE == si->promote
        && 0 == si->checkers);

    active = ~active;
    si = si->ptr;

    assert(ok());
}

/// Position::flip() flips position mean White and Black sides swaped.
/// This is only useful for debugging especially for finding evaluation symmetry bugs.
void Position::flip()
{
    istringstream iss{ fen() };
    string ff, token;
    // 1. Piece placement
    for (auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
    {
        std::getline(iss, token, r > R_1 ? '/' : ' ');
        toggleCase(token);
        token += r < R_8 ? "/" : " ";
        ff = token + ff;
    }
    // 2. Active color
    iss >> token;
    ff += (token == "w" ? "b" : "w");
    ff += " ";
    // 3. Castling availability
    iss >> token;
    if (token != "-")
    {
        toggleCase(token);
    }
    ff += token;
    ff += " ";
    // 4. Enpassant square
    iss >> token;
    if (token != "-")
    {
        token.replace(1, 1, {1, toChar(~toRank(token[1]))});
    }
    ff += token;
    // 5-6. Halfmove clock and Fullmove number
    std::getline(iss, token, '\n');
    ff += token;

    setup(ff, *si, thread);

    assert(ok());
}
/// Position::mirror() mirrors position mean King and Queen sides swaped.
void Position::mirror()
{
    istringstream iss{ fen() };
    string ff, token;
    // 1. Piece placement
    for (auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
    {
        std::getline(iss, token, r > R_1 ? '/' : ' ');
        std::reverse(token.begin(), token.end());
        token += r > R_1 ? '/' : ' ';
        ff = ff + token;
    }
    // 2. Active color
    iss >> token;
    ff += token;
    ff += ' ';
    // 3. Castling availability
    iss >> token;
    if (token != "-")
    {
        for (auto &ch : token)
        {
            if (bool(Options["UCI_Chess960"]))
            {
                assert(isalpha(ch));
                ch = toChar(~toFile(char(tolower(ch))), islower(ch));
            }
            else
            {
                switch (ch)
                {
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
    if (token != "-")
    {
        token.replace(0, 1, {1, toChar(~toFile(token[0]))});
    }
    ff += token;
    // 5-6. Halfmove clock and Fullmove number
    std::getline(iss, token, '\n');
    ff += token;

    setup(ff, *si, thread);

    assert(ok());
}

/// Position::fen() returns a FEN representation of the position.
/// In case of Chess960 the Shredder-FEN notation is used.
string Position::fen(bool full) const
{
    ostringstream oss;

    for (auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
    {
        for (auto f = F_A; f <= F_H; ++f)
        {
            i16 emptyCount;
            for (emptyCount = 0; f <= F_H && empty(makeSquare(f, r)); ++f)
            {
                ++emptyCount;
            }
            if (0 != emptyCount)
            {
                oss << emptyCount;
            }
            if (f <= F_H)
            {
                oss << piece[makeSquare(f, r)];
            }
        }
        if (r > R_1)
        {
            oss << '/';
        }
    }

    oss << ' ' << active << ' ';

    if (canCastle(CR_ANY))
    {
        if (canCastle(CR_WKING)) oss << (bool(Options["UCI_Chess960"]) ? toChar(sFile(castleRookSq[WHITE][CS_KING]), false) : 'K');
        if (canCastle(CR_WQUEN)) oss << (bool(Options["UCI_Chess960"]) ? toChar(sFile(castleRookSq[WHITE][CS_QUEN]), false) : 'Q');
        if (canCastle(CR_BKING)) oss << (bool(Options["UCI_Chess960"]) ? toChar(sFile(castleRookSq[BLACK][CS_KING]),  true) : 'k');
        if (canCastle(CR_BQUEN)) oss << (bool(Options["UCI_Chess960"]) ? toChar(sFile(castleRookSq[BLACK][CS_QUEN]),  true) : 'q');
    }
    else
    {
        oss << '-';
    }

    oss << ' ' << (SQ_NO != si->enpassantSq ? toString(si->enpassantSq) : "-");

    if (full)
    {
        oss << ' ' << si->clockPly << ' ' << moveCount();
    }

    return oss.str();
}
/// Position::operator string() returns an ASCII representation of the position.
Position::operator std::string() const
{
    ostringstream oss;

    oss << " +---+---+---+---+---+---+---+---+\n";
    for (auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
    {
        oss << toChar(r) << "| ";
        for (auto f : { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H })
        {
            oss << piece[makeSquare(f, r)] << " | ";
        }
        oss << "\n +---+---+---+---+---+---+---+---+\n";
    }
    for (auto f : { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H })
    {
        oss << "   " << toChar(f, false);
    }

    oss << "\nFEN: " << fen()
        << "\nKey: "
        << setfill('0')
        << hex
        << uppercase
        << setw(16) << si->posiKey
        << nouppercase
        << dec
        << setfill(' ');
    oss << "\nCheckers: ";
    for (Bitboard b = si->checkers; 0 != b; )
    {
        oss << popLSq(b) << ' ';
    }
    if (Book.enabled)
    {
        oss << '\n' << Book.show(*this);
    }
    if (   MaxLimitPiece >= count()
        && !canCastle(CR_ANY))
    {
        ProbeState wdlState; auto wdl = probeWDL(*const_cast<Position*>(this), wdlState);
        ProbeState dtzState; auto dtz = probeDTZ(*const_cast<Position*>(this), dtzState);
        oss << "\nTablebases WDL: " << setw(4) << wdl << " (" << wdlState << ")"
            << "\nTablebases DTZ: " << setw(4) << dtz << " (" << dtzState << ")";
    }
    oss << '\n';

    return oss.str();
}

#if !defined(NDEBUG)
/// Position::ok() performs some consistency checks for the position,
/// and raises an assert if something wrong is detected.
bool Position::ok() const
{
    constexpr bool Fast = true;

    // BASIC
    if (   !isOk(active)
        || (   count() > 32
            || count() != popCount(pieces())))
    {
        assert(false && "Position OK: BASIC");
        return false;
    }
    for (auto c : { WHITE, BLACK })
    {
        if (   count(c) > 16
            || count(c) != popCount(pieces(c))
            || 1 != std::count(piece.begin(), piece.end(), (c|KING))
            || 1 != count(c|KING)
            || !isOk(square(c|KING))
            || piece[square(c|KING)] != (c|KING)
            || (          (count(c|PAWN)
                + std::max(count(c|NIHT)-2, 0)
                + std::max(count(c|BSHP)-2, 0)
                + std::max(count(c|ROOK)-2, 0)
                + std::max(count(c|QUEN)-1, 0)) > 8))
        {
            assert(false && "Position OK: BASIC");
            return false;
        }
    }
    // BITBOARD
    if (   (pieces(WHITE) & pieces(BLACK)) != 0
        || (pieces(WHITE) | pieces(BLACK)) != pieces()
        || (pieces(WHITE) ^ pieces(BLACK)) != pieces()
        || (pieces(PAWN)|pieces(NIHT)|pieces(BSHP)|pieces(ROOK)|pieces(QUEN)|pieces(KING))
        != (pieces(PAWN)^pieces(NIHT)^pieces(BSHP)^pieces(ROOK)^pieces(QUEN)^pieces(KING))
        || 0 != (pieces(PAWN) & (R1BB|R8BB))
        || 0 != popCount(attackersTo(square(~active|KING)) & pieces( active))
        || 2 <  popCount(attackersTo(square( active|KING)) & pieces(~active)))
    {
        assert(false && "Position OK: BITBOARD");
        return false;
    }
    for (auto pt1 : { PAWN, NIHT, BSHP, ROOK, QUEN, KING })
    {
        for (auto pt2 : { PAWN, NIHT, BSHP, ROOK, QUEN, KING })
        {
            if (   pt1 != pt2
                && 0 != (pieces(pt1) & pieces(pt2)))
            {
                assert(false && "Position OK: BITBOARD");
                return false;
            }
        }
    }
    for (auto c : { WHITE, BLACK })
    {
        if (   1 != popCount(pieces(c, KING))
            || (          (popCount(pieces(c, PAWN))
                + std::max(popCount(pieces(c, NIHT))-2, 0)
                + std::max(popCount(pieces(c, BSHP))-2, 0)
                + std::max(popCount(pieces(c, ROOK))-2, 0)
                + std::max(popCount(pieces(c, QUEN))-1, 0)) > 8)
            || (          (popCount(pieces(c, PAWN))
                + std::max(popCount(pieces(c, BSHP) & Colors[WHITE])-1, 0)
                + std::max(popCount(pieces(c, BSHP) & Colors[BLACK])-1, 0)) > 8))
        {
            assert(false && "Position OK: BITBOARD");
            return false;
        }
    }

    // PSQ
    if (psq != computePSQ(*this))
    {
        assert(false && "Position OK: PSQ");
        return false;
    }

    if (Fast)
    {
        return true;
    }

    // SQUARE_LIST
    for (auto p : { W_PAWN, W_NIHT, W_BSHP, W_ROOK, W_QUEN, W_KING,
                    B_PAWN, B_NIHT, B_BSHP, B_ROOK, B_QUEN, B_KING })
    {
        if (count(p) != popCount(pieces(pColor(p), pType(p))))
        {
            assert(false && "Position OK: SQUARE_LIST");
            return false;
        }
        for (auto s : squares[p])
        {
            if (   !isOk(s)
                || piece[s] != p)
            {
                assert(false && "Position OK: SQUARE_LIST");
                return false;
            }
        }
    }

    // CASTLING
    for (auto c : { WHITE, BLACK })
    {
        for (auto cs : { CS_KING, CS_QUEN })
        {
            auto cr = makeCastleRight(c, cs);
            if (   canCastle(cr)
                && (   piece[castleRookSq[c][cs]] != (c|ROOK)
                    || castleRights[castleRookSq[c][cs]] != cr
                    || (castleRights[square(c|KING)] & cr) != cr))
            {
                assert(false && "Position OK: CASTLING");
                return false;
            }
        }
    }
    // STATE_INFO
    if (   si->npm[WHITE] != computeNPM<WHITE>(*this)
        || si->npm[BLACK] != computeNPM<BLACK>(*this)
        || si->matlKey != RandZob.computeMatlKey(*this)
        || si->pawnKey != RandZob.computePawnKey(*this)
        || si->posiKey != RandZob.computePosiKey(*this)
        || si->checkers != (attackersTo(square(active|KING)) & pieces(~active))
        || 2 < popCount(si->checkers)
        || si->clockPly > 2 * i32(Options["Draw MoveCount"])
        || (   NONE != si->capture
            && 0 != si->clockPly)
        || (   SQ_NO != si->enpassantSq
            && (   0 != si->clockPly
                || R_6 != relRank(active, si->enpassantSq)
                || !canEnpassant(active, si->enpassantSq))))
    {
        assert(false && "Position OK: STATE_INFO");
        return false;
    }

    return true;
}
#endif
