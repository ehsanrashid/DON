#include "Position.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include "MoveGenerator.h"
#include "Notation.h"
#include "Option.h"
#include "Polyglot.h"
#include "TBsyzygy.h"
#include "Thread.h"
#include "Transposition.h"

using namespace std;
using namespace BitBoard;
using namespace TBSyzygy;

namespace {

    /// Computes the non-pawn middle game material value for the given side.
    /// Material values are updated incrementally during the search.
    template<Color Own>
    Value compute_npm (const Position &pos)
    {
        auto npm = VALUE_ZERO;
        for (const auto pt : { NIHT, BSHP, ROOK, QUEN })
        {
            npm += PieceValues[MG][pt] * pos.count(Own|pt);
        }
        return npm;
    }
    /// Explicit template instantiations
    /// --------------------------------
    template Value compute_npm<WHITE>(const Position&);
    template Value compute_npm<BLACK>(const Position&);

    // Marcel van Kervink's cuckoo algorithm for fast detection of "upcoming repetition".
    // Description of the algorithm in the following paper:
    // https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

    struct Cuckoo
    {
        Key key;    // Zobrist key
        Move move;  // Valid reversible move

        Cuckoo(Key k, Move m)
            : key(k)
            , move(m)
        {}
        Cuckoo()
            : Cuckoo(0, MOVE_NONE)
        {}

        bool empty() const
        {
            return key == 0
                || move == MOVE_NONE;
        }
    };

    // Cuckoo table
    array<Cuckoo, 0x2000> Cuckoos;

    // Hash functions for indexing the cuckoo tables

    inline u16 H1(Key key) { return u16((key >> 0x00) & (Cuckoos.size() - 1)); }
    inline u16 H2(Key key) { return u16((key >> 0x10) & (Cuckoos.size() - 1)); }

}

const StateInfo StateInfo::Empty
{
    0,          //matl_key
    0,          //pawn_key
    CR_NONE,    //castle_rights
    SQ_NO,      //enpassant_sq
    0,          //clock_ply
    0,          //null_ply

    0,          //posi_key
    NONE,       //capture
    NONE,       //promote
    0,          //checkers
    0,          //repetition
    {0, 0},
    {0, 0},
    {0, 0, 0, 0, 0, 0},
    nullptr
};

void Position::initialize()
{
    // Prepare the Cuckoo tables
    Cuckoos.fill({0, MOVE_NONE});
    u16 count = 0;
    for (const auto c : { WHITE, BLACK })
    {
        for (const auto pt : { NIHT, BSHP, ROOK, QUEN, KING })
        {
            for (const auto &org : SQ)
            {
                for (auto dst = org + DEL_E; dst <= SQ_H8; ++dst)
                {
                    if (contains(PieceAttacks[pt][org], dst))
                    {
                        Cuckoo cuckoo( RandZob.piece_square[c][pt][org]
                                     ^ RandZob.piece_square[c][pt][dst]
                                     ^ RandZob.color,
                                       make_move<NORMAL>(org, dst));

                        u16 i = H1(cuckoo.key);
                        do
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
                        } while (true);

                        ++count;
                    }
                }
            }
        }
    }
    assert(3668 == count);
}

/// Position::draw() checks whether position is drawn by: Clock Ply Rule, Repetition.
/// It does not detect Insufficient materials and Stalemate.
bool Position::draw(i16 pp) const
{
    return
            // Draw by Clock Ply Rule?
            // Not in check or in check have legal moves
           (   si->clock_ply >= 2*i32(Options["Draw MoveCount"])
            && (   0 == si->checkers
                || 0 != MoveList<GenType::LEGAL>(*this).size()))
            // Draw by Repetition?
            // Return a draw score if a position repeats once earlier but strictly
            // after the root, or repeats twice before or at the root.
        || (   0 != si->repetition
            && si->repetition < pp);
}

/// Position::repeated() tests whether there has been at least one repetition of positions since the last capture or pawn move.
bool Position::repeated() const
{
    const auto *csi = si;
    auto end = std::min(csi->clock_ply, csi->null_ply);
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
    auto end = std::min(si->clock_ply, si->null_ply);
    if (end < 3)
    {
        return false;
    }

    Key posi_key = si->posi_key;
    const auto *psi = si->ptr;

    for (Depth p = 3; p <= end; p += 2)
    {
        psi = psi->ptr->ptr;
        Key key = posi_key ^ psi->posi_key;

        u16 j;
        if (   (j = H1(key), key == Cuckoos[j].key)
            || (j = H2(key), key == Cuckoos[j].key))
        {
            auto org = org_sq(Cuckoos[j].move);
            auto dst = dst_sq(Cuckoos[j].move);
            if (0 == (between_bb(org, dst) & pieces()))
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
                if (color(piece[empty(org) ? dst : org]) != active)
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

/// Position::slider_blockers() returns a bitboard of all the pieces that are blocking attacks on the square.
/// King-attack piece can be either pinner or hidden piece.
Bitboard Position::slider_blockers(Square s, Color c, Bitboard excludes, Bitboard &pinners, Bitboard &hiddens) const
{
    Bitboard blockers = 0;
    // Snipers are attackers to the square 's'
    Bitboard snipers = (  pieces(c)
                        & ~(excludes | attackers_to(s))) // Remove direct attackers to the square 's'
                     & (  (pieces(BSHP, QUEN) & PieceAttacks[BSHP][s])
                        | (pieces(ROOK, QUEN) & PieceAttacks[ROOK][s]));
    // Occupancy are pieces but removed snipers
    Bitboard mocc = pieces() ^ snipers;
    while (0 != snipers)
    {
        auto sniper_sq = pop_lsq(snipers);

        Bitboard b = mocc & between_bb(s, sniper_sq);
        if (   0 != b
            && !more_than_one(b))
        {
            blockers |= b;
            if (0 != (b & pieces(c)))
            {
                hiddens |= sniper_sq;
            }
            else
            {
                pinners |= sniper_sq;
            }
        }
    }

    return blockers;
}

/// Position::pseudo_legal() tests whether a random move is pseudo-legal.
/// It is used to validate moves from TT that can be corrupted
/// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal(Move m) const
{
    assert(_ok(m));

    auto org = org_sq(m);
    auto dst = dst_sq(m);
    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if (!contains(pieces(active), org))
    {
        return false;
    }
    if (CASTLE == mtype(m))
    {
        return (active|KING) == piece[org] //&& contains(pieces(active, KING), org)
            && (active|ROOK) == piece[dst] //&& contains(pieces(active, ROOK), dst)
            && castle_rook_sq[active][dst > org ? CS_KING : CS_QUEN] == dst
            && expeded_castle(active, dst > org ? CS_KING : CS_QUEN)
            //&& R_1 == rel_rank(active, org)
            //&& R_1 == rel_rank(active, dst)
            && si->can_castle(active|(dst > org ? CS_KING : CS_QUEN))
            && 0 == si->checkers;
    }
    // The captured square cannot be occupied by a friendly piece
    if (contains(pieces(active), dst))
    {
        return false;
    }
    // Handle the special case of a piece move
    if (PAWN == ptype(piece[org]))
    {
        if (    // Single push
               (   (   (   NORMAL != mtype(m)
                        || R_6 < rel_rank(active, org)
                        || R_7 < rel_rank(active, dst)
                        || NIHT != promote(m))
                    && (   PROMOTE != mtype(m)
                        || R_7 != rel_rank(active, org)
                        || R_8 != rel_rank(active, dst)))
                || dst != org + pawn_push(active)
                || !empty(dst))
                // Normal capture
            && (   (   (   NORMAL != mtype(m)
                        || R_6 < rel_rank(active, org)
                        || R_7 < rel_rank(active, dst)
                        || NIHT != promote(m))
                    && (   PROMOTE != mtype(m)
                        || R_7 != rel_rank(active, org)
                        || R_8 != rel_rank(active, dst)))
                || !contains(PawnAttacks[active][org], dst)
                || empty(dst))
                // Enpassant capture
            && (   ENPASSANT != mtype(m)
                || R_5 != rel_rank(active, org)
                || R_6 != rel_rank(active, dst)
                || NIHT != promote(m)
                || dst != si->enpassant_sq
                || !contains(PawnAttacks[active][org], dst)
                || !empty(dst)
                || empty(dst - pawn_push(active))
                || 0 != si->clock_ply)
                // Double push
            && (   NORMAL != mtype(m)
                || R_2 != rel_rank(active, org)
                || R_4 != rel_rank(active, dst)
                || NIHT != promote(m)
                || dst != org + 2*pawn_push(active)
                || !empty(dst)
                || !empty(dst - pawn_push(active))))
        {
            return false;
        }
    }
    else
    if (   NORMAL != mtype(m)
        || !contains(attacks_from(org), dst))
    {
        return false;
    }

    // Evasions generator already takes care to avoid some kind of illegal moves and legal() relies on this.
    // So have to take care that the same kind of moves are filtered out here.
    if (0 != si->checkers)
    {
        // In case of king moves under check, remove king so to catch
        // as invalid moves like B1A1 when opposite queen is on C1.
        if (KING == ptype(piece[org]))
        {
            return 0 == (attackers_to(dst, pieces() ^ org) & pieces(~active));
        }
        // Double check? In this case a king move is required
        if (more_than_one(si->checkers))
        {
            return false;
        }
        return ENPASSANT != mtype(m) ?
                // Move must be a capture of the checking piece or a blocking evasion of the checking piece
                contains(si->checkers | between_bb(scan_lsq(si->checkers), square(active|KING)), dst) :
                // Move must be a capture of the checking Enpassant pawn or a blocking evasion of the checking piece
                   (   0 != (si->checkers & pieces(~active, PAWN))
                    && contains(si->checkers, dst - pawn_push(active)))
                || contains(between_bb(scan_lsq(si->checkers), square(active|KING)), dst);
    }
    return true;
}
/// Position::legal() tests whether a pseudo-legal move is legal.
bool Position::legal(Move m) const
{
    assert(_ok(m));

    auto org = org_sq(m);
    auto dst = dst_sq(m);
    assert(contains(pieces(active), org));

    switch (mtype(m))
    {
    case NORMAL:
        // Only king moves to non attacked squares, sliding check x-rays the king
        // In case of king moves under check have to remove king so to catch
        // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
        // check whether the destination square is attacked by the opponent.
        if (KING == ptype(piece[org]))
        {
            return 0 == (attackers_to(dst, pieces() ^ org) & pieces(~active));
        }
        /* fall through */
    case PROMOTE:
        assert((   NORMAL == mtype(m)
                && NIHT == promote(m))
            || (   (active|PAWN) == piece[org] //&& contains(pieces(active, PAWN), org)
                && R_7 == rel_rank(active, org)
                && R_8 == rel_rank(active, dst)));

        // A non-king move is legal if and only if
        // - not pinned
        // - moving along the ray from the king
        return !contains(si->king_blockers[active], org)
            || squares_aligned(org, dst, square(active|KING));
        break;
    case CASTLE:
    {
        assert((active|KING) == piece[org] //&& contains(pieces(active, KING), org)
            && (active|ROOK) == piece[dst] //&& contains(pieces(active, ROOK), dst)
            && castle_rook_sq[active][dst > org ? CS_KING : CS_QUEN] == dst
            && expeded_castle(active, dst > org ? CS_KING : CS_QUEN)
            //&& R_1 == rel_rank(active, org)
            //&& R_1 == rel_rank(active, dst)
            && si->can_castle(active|(dst > org ? CS_KING : CS_QUEN))
            && 0 == si->checkers);
        // Castle is always encoded as "King captures friendly Rook".
        Bitboard b = castle_king_path_bb[active][dst > org ? CS_KING : CS_QUEN];
        // Check king's path for attackers.
        while (0 != b)
        {
            if (0 != (attackers_to(pop_lsq(b)) & pieces(~active)))
            {
                return false;
            }
        }
        // In case of Chess960, verify that when moving the castling rook we do not discover some hidden checker.
        // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
        return !bool(Options["UCI_Chess960"])
            || 0 == (  pieces(~active, ROOK, QUEN)
                     & rank_bb(rel_rank(active, R_1))
                     & attacks_bb<ROOK>(rel_sq(active, dst > org ? SQ_G1 : SQ_C1), pieces() ^ dst));
    }
    case ENPASSANT:
    {
        // Enpassant captures are a tricky special case. Because they are rather uncommon,
        // do it simply by testing whether the king is attacked after the move is made.
        assert((active|PAWN) == piece[org] //&& contains(pieces(active, PAWN), org)
            && R_5 == rel_rank(active, org)
            && R_6 == rel_rank(active, dst)
            && 0 == si->clock_ply
            && dst == si->enpassant_sq
            && empty(dst) //&& !contains(pieces(), dst)
            && (~active|PAWN) == piece[dst - pawn_push(active)]); //&& contains(pieces(~active, PAWN), dst - pawn_push(active))
        Bitboard mocc = (pieces() ^ org ^ (dst - pawn_push(active))) | dst;
        // If any attacker then in check and not legal move.
        return 0 == (pieces(~active, BSHP, QUEN) & attacks_bb<BSHP>(square(active|KING), mocc))
            && 0 == (pieces(~active, ROOK, QUEN) & attacks_bb<ROOK>(square(active|KING), mocc));
    }
    default: assert(false); return false;
    }
}
/// Position::gives_check() tests whether a pseudo-legal move gives a check.
bool Position::gives_check(Move m) const
{
    assert(_ok(m));

    auto org = org_sq(m);
    auto dst = dst_sq(m);
    assert(contains(pieces(active), org));

    if (    // Direct check ?
           contains(si->checks[PROMOTE != mtype(m) ? ptype(piece[org]) : promote(m)], dst)
            // Discovered check ?
        || (   contains(si->king_blockers[~active], org)
            && !squares_aligned(org, dst, square(~active|KING))))
    {
        return true;
    }

    switch (mtype(m))
    {
    case NORMAL:
        return false;
    case CASTLE:
    {
        // Castling with check?
        auto king_dst = rel_sq(active, dst > org ? SQ_G1 : SQ_C1);
        auto rook_dst = rel_sq(active, dst > org ? SQ_F1 : SQ_D1);
        return contains(attacks_bb<ROOK>(rook_dst, (pieces() ^ org ^ dst) | king_dst | rook_dst), square(~active|KING));
    }
    case ENPASSANT:
    {
        // Enpassant capture with check?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        Bitboard mocc = (pieces() ^ org ^ (_file(dst)|_rank(org))) | dst;
        return 0 != (pieces(active, BSHP, QUEN) & attacks_bb<BSHP>(square(~active|KING), mocc))
            || 0 != (pieces(active, ROOK, QUEN) & attacks_bb<ROOK>(square(~active|KING), mocc));
    }
    case PROMOTE:
    {
        // Promotion with check?
        return contains(attacks_from(promote(m), dst, pieces() ^ org), square(~active|KING));
    }
    default: assert(false); return false;
    }
}

/// Position::set_castle() set the castling right.
void Position::set_castle(Color c, Square rook_org)
{
    assert(_ok(rook_org)
        && R_1 == rel_rank(c, rook_org)
        && (c|ROOK) == piece[rook_org]); //&& contains(pieces(c, ROOK), rook_org)

    auto king_org = square(c|KING);
    assert(R_1 == rel_rank(c, king_org));
    auto cs = rook_org > king_org ? CS_KING : CS_QUEN;
    castle_rook_sq[c][cs] = rook_org;

    auto king_dst = rel_sq(c, rook_org > king_org ? SQ_G1 : SQ_C1);
    auto rook_dst = rel_sq(c, rook_org > king_org ? SQ_F1 : SQ_D1);
    auto cr = c|cs;
    si->castle_rights |= cr;
    castle_right[king_org] |= cr;
    castle_right[rook_org] |= cr;

    castle_king_path_bb[c][cs] = (between_bb(king_org, king_dst) | king_dst)
                               & ~(square_bb(king_org));
    castle_rook_path_bb[c][cs] = (between_bb(king_org, king_dst) | between_bb(rook_org, rook_dst) | king_dst | rook_dst)
                               & ~(square_bb(king_org) | square_bb(rook_org));
}
/// Position::can_enpassant() Can the enpassant possible.
bool Position::can_enpassant(Color c, Square ep_sq, bool move_done) const
{
    assert(_ok(ep_sq)
        && R_6 == rel_rank(c, ep_sq));
    auto cap = move_done ?
                ep_sq - pawn_push(c) :
                ep_sq + pawn_push(c);
    assert((~c|PAWN) == piece[cap]); //contains(pieces(~c, PAWN), cap));
    // Enpassant attackers
    Bitboard attackers = pieces(c, PAWN) & PawnAttacks[~c][ep_sq];
    if (0 == attackers)
    {
        return false;
    }
    assert(2 >= pop_count(attackers));
    Bitboard mocc = (pieces() ^ cap) | ep_sq;
    auto k_sq = square(c|KING);
    Bitboard bq = pieces(~c, BSHP, QUEN) & PieceAttacks[BSHP][k_sq];
    Bitboard rq = pieces(~c, ROOK, QUEN) & PieceAttacks[ROOK][k_sq];
    if (   0 == bq
        && 0 == rq)
    {
        return true;
    }
    while (0 != attackers)
    {
        auto org = pop_lsq(attackers);
        assert(contains(mocc, org));
        // Check enpassant is legal for the position
        if (   (0 == bq || 0 == (bq & attacks_bb<BSHP>(k_sq, mocc ^ org)))
            && (0 == rq || 0 == (rq & attacks_bb<ROOK>(k_sq, mocc ^ org))))
        {
            return true;
        }
    }
    return false;
}

/// Position::see_ge() (Static Exchange Evaluator [SEE] Greater or Equal):
/// Checks the SEE value of move is greater or equal to the given threshold.
/// An algorithm similar to alpha-beta pruning with a null window is used.
bool Position::see_ge(Move m, Value threshold) const
{
    assert(_ok(m));

    // Only deal with normal moves, assume others pass a simple SEE
    if (NORMAL != mtype(m))
    {
        return VALUE_ZERO >= threshold;
    }

    auto org = org_sq(m);
    auto dst = dst_sq(m);

    i32 swap;
    swap = PieceValues[MG][ptype(piece[dst])] - threshold;
    if (0 > swap)
    {
        return false;
    }

    swap = PieceValues[MG][ptype(piece[org])] - swap;
    if (0 >= swap)
    {
        return true;
    }

    bool res = true;

    Bitboard mocc = pieces() ^ org ^ dst;
    auto mov = color(piece[org]);

    Bitboard attackers = attackers_to(dst, mocc);
    while (0 != attackers)
    {
        mov = ~mov;
        attackers &= mocc;

        Bitboard mov_attackers = attackers & pieces(mov);

        // If mov has no more attackers then give up: mov loses
        if (0 == mov_attackers)
        {
            break;
        }

        // Only allow king for defensive capture to evade the discovered check,
        // as long any discoverers are on their original square.
        if (   contains(si->king_blockers[mov] & pieces(~mov), org)
            && (  si->king_checkers[~mov]
                & pieces(~mov)
                & mocc
                & attacks_bb<QUEN>(square(mov|KING), mocc)) != 0)
        {
            mov_attackers &= pieces(KING);
        }
        // Don't allow pinned pieces for defensive capture,
        // as long respective pinners are on their original square.
        else
        {
            Bitboard mov_pinneds = mov_attackers & si->king_blockers[mov];
            while (0 != mov_pinneds)
            {
                auto sq = pop_lsq(mov_pinneds);
                if ((  si->king_checkers[mov]
                     & pieces(~mov)
                     & mocc
                     & attacks_bb<QUEN>(square(mov|KING), mocc ^ sq)) != 0)
                {
                    mov_attackers ^= sq;
                }
            }
        }

        // If mov has no more attackers then give up: mov loses
        if (0 == mov_attackers)
        {
            break;
        }

        res = !res;

        // Locate and remove the next least valuable attacker, and add to
        // the bitboard 'attackers' any X-ray attackers behind it.

        Bitboard bb;

        if (0 != (bb = pieces(PAWN) & mov_attackers))
        {
            swap = VALUE_MG_PAWN - swap;
            if (swap < res)
            {
                break;
            }

            org = scan_lsq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacks_bb<BSHP>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(NIHT) & mov_attackers))
        {
            swap = VALUE_MG_NIHT - swap;
            if (swap < res)
            {
                break;
            }

            org = scan_lsq(bb);
            mocc ^= org;
        }
        else
        if (0 != (bb = pieces(BSHP) & mov_attackers))
        {
            swap = VALUE_MG_BSHP - swap;
            if (swap < res)
            {
                break;
            }

            org = scan_lsq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacks_bb<BSHP>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(ROOK) & mov_attackers))
        {
            swap = VALUE_MG_ROOK - swap;
            if (swap < res)
            {
                break;
            }

            org = scan_lsq(bb);
            mocc ^= org;
            attackers |= (pieces(ROOK, QUEN) & attacks_bb<ROOK>(dst, mocc));
        }
        else
        if (0 != (bb = pieces(QUEN) & mov_attackers))
        {
            swap = VALUE_MG_QUEN - swap;
            if (swap < res)
            {
                break;
            }

            org = scan_lsq(bb);
            mocc ^= org;
            attackers |= (pieces(BSHP, QUEN) & attacks_bb<BSHP>(dst, mocc))
                       | (pieces(ROOK, QUEN) & attacks_bb<ROOK>(dst, mocc));
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
    color_bb.fill(0);
    type_bb.fill(0);

    npm.fill(VALUE_ZERO);

    castle_right.fill(CR_NONE);

    for (auto &sq : squares) { sq.clear(); }

    for (auto &crs : castle_rook_sq) { crs.fill(SQ_NO); }
    for (auto &ckp : castle_king_path_bb) { ckp.fill(0); }
    for (auto &crp : castle_rook_path_bb) { crp.fill(0); }

    psq = SCORE_ZERO;
    ply = 0;
    active = CLR_NO;
    thread = nullptr;
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
    std::memcpy(&nsi, &StateInfo::Empty, sizeof (StateInfo));
    si = &nsi;

    istringstream iss{ff};
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
            sq += 2*DEL_S;
        }
        else
        if ((idx = PieceChar.find(token)) != string::npos)
        {
            place_piece(sq, Piece(idx));
            ++sq;
        }
        else
        {
            assert(false);
        }
    }
    assert(1 == count(WHITE|KING)
        && 1 == count(BLACK|KING));
    npm[WHITE] = compute_npm<WHITE>(*this);
    npm[BLACK] = compute_npm<BLACK>(*this);

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
        assert(R_1 == rel_rank(c, square(c|KING)));
        Piece rook = (c|ROOK);
        auto rook_org = SQ_NO;
        token = char(tolower(token));
        switch (token)
        {
        case 'k':
            rook_org = rel_sq(c, SQ_H1);
            while (rook != piece[rook_org]
              /*&& rook_org > square(c|KING)*/)
            {
                --rook_org;
            }
            break;
        case 'q':
            rook_org = rel_sq(c, SQ_A1);
            while (rook != piece[rook_org]
              /*&& rook_org < square(c|KING)*/)
            {
                ++rook_org;
            }
            break;
        // Chess960
        case 'a': case 'b': case 'c': case 'd':
        case 'e': case 'f': case 'g': case 'h':
            rook_org = to_file(token) | _rank(square(c|KING));
            break;
        default:
            assert(false);
            break;
        }
        set_castle(c, rook_org);
    }

    // 4. Enpassant square. Ignore if no pawn capture is possible.
    u08 file, rank;
    if (   (iss >> file && ('a' <= file && file <= 'h'))
        && (iss >> rank && ('3' == rank || rank == '6')))
    {
        if (can_enpassant(active, to_square(file, rank)))
        {
            si->enpassant_sq = to_square(file, rank);
        }
    }

    // 5-6. Half move clock and Full move number.
    iss >> skipws
        >> si->clock_ply
        >> ply;

    if (SQ_NO != si->enpassant_sq)
    {
        si->clock_ply = 0;
    }
    // Rule 50 draw case.
    assert(100 >= si->clock_ply);
    // Convert from moves starting from 1 to ply starting from 0.
    ply = i16(std::max(2 * (ply - 1), 0) + (BLACK == active));

    thread = th;

    si->posi_key = RandZob.compute_posi_key(*this);
    si->matl_key = RandZob.compute_matl_key(*this);
    si->pawn_key = RandZob.compute_pawn_key(*this);
    si->checkers = attackers_to(square(active|KING)) & pieces(~active);
    si->set_check_info(*this);

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

    to_lower (sides[c]);
    string fen = "8/" + sides[WHITE] + char('0' + 8 - sides[WHITE].size()) + "/8/8/8/8/"
                      + sides[BLACK] + char('0' + 8 - sides[BLACK].size()) + "/8 w - -";

    return setup(fen, nsi, nullptr);
}

/// Position::do_move() makes a move, and saves all information necessary to a StateInfo object.
/// The move is assumed to be legal.
void Position::do_move(Move m, StateInfo &nsi, bool is_check)
{
    assert(_ok(m)
        && &nsi != si);

    thread->nodes.fetch_add(1, std::memory_order::memory_order_relaxed);
    auto key = si->posi_key;

    // Copy some fields of old state info to new state info object
    std::memcpy(&nsi, si, offsetof(StateInfo, posi_key));
    nsi.ptr = si;
    si = &nsi;

    ++ply;
    ++si->clock_ply;
    ++si->null_ply;
    si->promote = NONE;

    auto org = org_sq(m);
    auto dst = dst_sq(m);
    assert(contains(pieces(active), org)
        && (!contains(pieces(active), dst)
         || CASTLE == mtype(m)));

    auto mpt = ptype(piece[org]);
    assert(NONE != mpt);
    auto pasive = ~active;

    if (CASTLE == mtype(m))
    {
        assert((active|KING) == piece[org] //&& contains(pieces(active, KING), org)
            && (active|ROOK) == piece[dst] //&& contains(pieces(active, ROOK), dst)
            && castle_rook_sq[active][dst > org ? CS_KING : CS_QUEN] == dst
            && expeded_castle(active, dst > org ? CS_KING : CS_QUEN)
            //&& R_1 == rel_rank(active, org)
            //&& R_1 == rel_rank(active, dst)
            && si->can_castle(active|(dst > org ? CS_KING : CS_QUEN))
            && 0 == si->ptr->checkers); //&& (attackers_to(org) & pieces(pasive))

        si->capture = NONE;
        auto rook_org = dst; // Castling is encoded as "King captures friendly Rook"
        auto rook_dst = rel_sq(active, rook_org > org ? SQ_F1 : SQ_D1);
        /* king */dst = rel_sq(active, rook_org > org ? SQ_G1 : SQ_C1);
        // Remove both pieces first since squares could overlap in chess960
        remove_piece(org);
        remove_piece(rook_org);
        piece[org] = piece[rook_org] = NO_PIECE; // Not done by remove_piece()
        place_piece(dst, active|KING);
        place_piece(rook_dst, active|ROOK);

        key ^= RandZob.piece_square[active][ROOK][rook_org]
             ^ RandZob.piece_square[active][ROOK][rook_dst];
    }
    else
    {
        si->capture = ENPASSANT == mtype(m) ?
                        PAWN :
                        ptype(piece[dst]);

        if (NONE != si->capture)
        {
            assert(KING != si->capture);

            auto cap = dst;
            if (PAWN == si->capture)
            {
                if (ENPASSANT == mtype(m))
                {
                    cap -= pawn_push(active);

                    assert(PAWN == mpt
                        && R_5 == rel_rank(active, org)
                        && R_6 == rel_rank(active, dst)
                        && 1 == si->clock_ply
                        && dst == si->enpassant_sq
                        && empty(dst) //&& !contains(pieces(), dst)
                        && (pasive|PAWN) == piece[cap]); //&& contains(pieces(pasive, PAWN), cap));
                }

                si->pawn_key ^= RandZob.piece_square[pasive][PAWN][cap];
            }
            else
            {
                npm[pasive] -= PieceValues[MG][si->capture];
            }

            // Reset clock ply counter
            si->clock_ply = 0;
            remove_piece(cap);
            if (ENPASSANT == mtype(m))
            {
                piece[cap] = NO_PIECE; // Not done by remove_piece()
            }
            key ^= RandZob.piece_square[pasive][si->capture][cap];
            si->matl_key ^= RandZob.piece_square[pasive][si->capture][count(pasive|si->capture)];
            prefetch(thread->matl_table[si->matl_key]);
        }

        // Move the piece
        move_piece(org, dst);
    }
    key ^= RandZob.piece_square[active][mpt][org]
         ^ RandZob.piece_square[active][mpt][dst];

    // Reset enpassant square
    if (SQ_NO != si->enpassant_sq)
    {
        assert(1 >= si->clock_ply);
        key ^= RandZob.enpassant[_file(si->enpassant_sq)];
        si->enpassant_sq = SQ_NO;
    }

    // Update castling rights
    if (CR_NONE != si->castle_rights)
    {
        auto cr = (castle_right[org] | castle_right[dst]);
        if (CR_NONE != cr)
        {
            key ^= RandZob.castle_right[si->castle_rights & cr];
            si->castle_rights &= ~cr;
        }
    }

    if (PAWN == mpt)
    {
        if (PROMOTE == mtype(m))
        {
            assert(PAWN == mpt
                && R_7 == rel_rank(active, org)
                && R_8 == rel_rank(active, dst)
                /*&& NIHT <= promote(m) && promote(m) <= QUEN*/);

            si->promote = promote(m);
            // Replace the pawn with the promoted piece
            remove_piece(dst);
            place_piece(dst, active|si->promote);
            npm[active] += PieceValues[MG][si->promote];
            key ^= RandZob.piece_square[active][PAWN][dst]
                 ^ RandZob.piece_square[active][si->promote][dst];
            si->pawn_key ^= RandZob.piece_square[active][PAWN][dst];
            si->matl_key ^= RandZob.piece_square[active][PAWN][count(active|PAWN)]
                          ^ RandZob.piece_square[active][si->promote][count(active|si->promote) - 1];
            prefetch(thread->matl_table[si->matl_key]);
        }
        else
        // Double push pawn
        if (dst == org + 2*pawn_push(active))
        {
            // Set enpassant square if the moved pawn can be captured
            if (can_enpassant(pasive, org + pawn_push(active)))
            {
                si->enpassant_sq = org + pawn_push(active);
                key ^= RandZob.enpassant[_file(si->enpassant_sq)];
            }
        }

        // Reset clock ply counter
        si->clock_ply = 0;
        si->pawn_key ^= RandZob.piece_square[active][PAWN][org]
                      ^ RandZob.piece_square[active][PAWN][dst];
        //prefetch(thread->pawn_table[si->pawn_key]);
    }

    assert(0 == (attackers_to(square(active|KING)) & pieces(pasive)));

    // Calculate checkers
    si->checkers = is_check ? attackers_to(square(pasive|KING)) & pieces(active) : 0;
    assert(!is_check
        || (0 != si->checkers
         && 2 >= pop_count(si->checkers)));

    // Switch sides
    active = pasive;
    key ^= RandZob.color;
    // Update the key with the final value
    si->posi_key = key;

    si->set_check_info(*this);

    // Calculate the repetition info. It is the ply distance from the previous
    // occurrence of the same position, negative in the 3-fold case, or zero
    // if the position was not repeated.
    si->repetition = 0;
    auto end = std::min(si->clock_ply, si->null_ply);
    if (end >= 4)
    {
        const auto* psi = si->ptr->ptr;
        for (i16 i = 4; i <= end; i += 2)
        {
            psi = psi->ptr->ptr;
            if (psi->posi_key == si->posi_key)
            {
                si->repetition = 0 != psi->repetition ? -i : i;
                break;
            }
        }
    }

    assert(ok());
}
/// Position::undo_move() unmakes a move, and restores the position to exactly the same state as before the move was made.
/// The move is assumed to be legal.
void Position::undo_move(Move m)
{
    assert(_ok(m)
        && nullptr != si->ptr
        && KING != si->capture);

    auto org = org_sq(m);
    auto dst = dst_sq(m);
    assert(empty(org)
        || CASTLE == mtype(m));

    active = ~active;

    if (CASTLE == mtype(m))
    {
        assert(R_1 == rel_rank(active, org)
            && R_1 == rel_rank(active, dst)
            && NONE == si->capture
            && NONE == si->promote);

        auto rook_org = dst; // Castling is encoded as "King captures friendly Rook"
        auto rook_dst = rel_sq(active, rook_org > org ? SQ_F1 : SQ_D1);
        /* king */dst = rel_sq(active, rook_org > org ? SQ_G1 : SQ_C1);
        // Remove both pieces first since squares could overlap in chess960
        remove_piece(dst);
        remove_piece(rook_dst);
        piece[dst] = piece[rook_dst] = NO_PIECE; // Not done by remove_piece()
        place_piece(org, active|KING);
        place_piece(rook_org, active|ROOK);
    }
    else
    {
        if (PROMOTE == mtype(m))
        {
            assert(R_7 == rel_rank(active, org)
                && R_8 == rel_rank(active, dst)
                && promote(m) == si->promote);

            remove_piece(dst);
            place_piece(dst, active|PAWN);
            npm[active] -= PieceValues[MG][si->promote];
        }
        // Move the piece
        move_piece(dst, org);

        if (NONE != si->capture)
        {
            auto cap = dst;

            if (ENPASSANT == mtype(m))
            {
                cap -= pawn_push(active);

                assert((active|PAWN) == piece[org] //&& contains(pieces(active, PAWN), org)
                    && R_5 == rel_rank(active, org)
                    && R_6 == rel_rank(active, dst)
                    && dst == si->ptr->enpassant_sq
                    && PAWN == si->capture);
            }
            // Restore the captured piece.
            assert(empty(cap));
            place_piece(cap, ~active|si->capture);
            if (PAWN != si->capture)
            {
                npm[~active] += PieceValues[MG][si->capture];
            }
        }
    }

    // Point state pointer back to the previous state.
    si = si->ptr;
    --ply;

    assert(ok());
}
/// Position::do_null_move() makes a 'null move'.
/// It flips the side to move without executing any move on the board.
void Position::do_null_move(StateInfo &nsi)
{
    assert(&nsi != si
        && 0 == si->checkers);

    std::memcpy(&nsi, si, sizeof (StateInfo));
    nsi.ptr = si;
    si = &nsi;

    ++si->clock_ply;
    si->null_ply = 0;
    si->capture = NONE;
    si->promote = NONE;
    // Reset enpassant square.
    if (SQ_NO != si->enpassant_sq)
    {
        si->posi_key ^= RandZob.enpassant[_file(si->enpassant_sq)];
        si->enpassant_sq = SQ_NO;
    }

    active = ~active;
    si->posi_key ^= RandZob.color;
    prefetch(TT.cluster(si->posi_key)->entries);

    si->set_check_info(*this);

    si->repetition = 0;

    assert(ok());
}
/// Position::undo_null_move() unmakes a 'null move'.
void Position::undo_null_move()
{
    assert(nullptr != si->ptr
        && 0 == si->null_ply
        && NONE == si->capture
        && NONE == si->promote
        && 0 == si->checkers);

    active = ~active;
    si = si->ptr;

    assert(ok());
}

/// Position::flip() flips position mean White and Black sides swaped.
/// This is only useful for debugging especially for finding evaluation symmetry bugs.
void Position::flip()
{
    istringstream iss{fen()};
    string ff, token;
    // 1. Piece placement
    for (const auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
    {
        std::getline(iss, token, r > R_1 ? '/' : ' ');
        toggle(token);
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
        toggle(token);
    }
    ff += token;
    ff += " ";
    // 4. Enpassant square
    iss >> token;
    if (token != "-")
    {
        token.replace(1, 1, {1, to_char(~to_rank(token[1]))});
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
    istringstream iss{fen()};
    string ff, token;
    // 1. Piece placement
    for (const auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
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
                ch = to_char(~to_file(char(tolower(ch))), islower(ch));
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
        token.replace(0, 1, {1, to_char(~to_file(token[0]))});
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

    for (const auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
    {
        for (auto f = F_A; f <= F_H; ++f)
        {
            i16 empty_count;
            for (empty_count = 0; f <= F_H && empty(f|r); ++f)
            {
                ++empty_count;
            }
            if (0 != empty_count)
            {
                oss << empty_count;
            }
            if (f <= F_H)
            {
                oss << piece[f|r];
            }
        }
        if (r > R_1)
        {
            oss << '/';
        }
    }

    oss << ' ' << active << ' ';

    if (si->can_castle(CR_ANY))
    {
        if (si->can_castle(CR_WKING)) oss << (bool(Options["UCI_Chess960"]) ? to_char(_file(castle_rook_sq[WHITE][CS_KING]), false) : 'K');
        if (si->can_castle(CR_WQUEN)) oss << (bool(Options["UCI_Chess960"]) ? to_char(_file(castle_rook_sq[WHITE][CS_QUEN]), false) : 'Q');
        if (si->can_castle(CR_BKING)) oss << (bool(Options["UCI_Chess960"]) ? to_char(_file(castle_rook_sq[BLACK][CS_KING]),  true) : 'k');
        if (si->can_castle(CR_BQUEN)) oss << (bool(Options["UCI_Chess960"]) ? to_char(_file(castle_rook_sq[BLACK][CS_QUEN]),  true) : 'q');
    }
    else
    {
        oss << '-';
    }

    oss << ' ' << (SQ_NO != si->enpassant_sq ? to_string(si->enpassant_sq) : "-");

    if (full)
    {
        oss << ' ' << si->clock_ply << ' ' << move_num();
    }

    return oss.str();
}
/// Position::operator string() returns an ASCII representation of the position.
Position::operator std::string() const
{
    ostringstream oss;

    oss << " +---+---+---+---+---+---+---+---+\n";
    for (const auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
    {
        oss << to_char(r) << "| ";
        for (const auto f : { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H })
        {
            oss << piece[f|r] << " | ";
        }
        oss << "\n +---+---+---+---+---+---+---+---+\n";
    }
    for (const auto f : { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H })
    {
        oss << "   " << to_char(f, false);
    }

    oss << "\nFEN: " << fen()
        << "\nKey: "
        << setfill('0')
        << hex
        << uppercase
        << setw(16) << si->posi_key
        << nouppercase
        << dec
        << setfill(' ');
    oss << "\nCheckers: ";
    for (Bitboard b = si->checkers; 0 != b; )
    {
        oss << pop_lsq(b) << ' ';
    }
    if (Book.enabled)
    {
        oss << '\n' << Book.show(*this);
    }
    if (   MaxLimitPiece >= count()
        && !si->can_castle(CR_ANY))
    {
        ProbeState wdl_state; auto wdl = probe_wdl(*const_cast<Position*>(this), wdl_state);
        ProbeState dtz_state; auto dtz = probe_dtz(*const_cast<Position*>(this), dtz_state);
        oss << "\nTablebases WDL: " << setw(4) << wdl << " (" << wdl_state << ")"
            << "\nTablebases DTZ: " << setw(4) << dtz << " (" << dtz_state << ")";
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
    if (   !_ok(active)
        || (   count() > 32
            || count() != pop_count(pieces())))
    {
        assert(false && "Position OK: BASIC");
        return false;
    }
    for (const auto c : { WHITE, BLACK })
    {
        if (   count(c) > 16
            || count(c) != pop_count(pieces(c))
            || 1 != std::count(piece.begin(), piece.end(), (c|KING))
            || 1 != count(c|KING)
            || !_ok(square(c|KING))
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
        || 0 != (pieces(PAWN) & (R1_bb|R8_bb))
        || 0 != pop_count(attackers_to(square(~active|KING)) & pieces( active))
        || 2 <  pop_count(attackers_to(square( active|KING)) & pieces(~active)))
    {
        assert(false && "Position OK: BITBOARD");
        return false;
    }
    for (const auto pt1 : { PAWN, NIHT, BSHP, ROOK, QUEN, KING })
    {
        for (const auto pt2 : { PAWN, NIHT, BSHP, ROOK, QUEN, KING })
        {
            if (   pt1 != pt2
                && 0 != (pieces(pt1) & pieces(pt2)))
            {
                assert(false && "Position OK: BITBOARD");
                return false;
            }
        }
    }
    for (const auto c : { WHITE, BLACK })
    {
        if (   1 != pop_count(pieces(c, KING))
            || (          (pop_count(pieces(c, PAWN))
                + std::max(pop_count(pieces(c, NIHT))-2, 0)
                + std::max(pop_count(pieces(c, BSHP))-2, 0)
                + std::max(pop_count(pieces(c, ROOK))-2, 0)
                + std::max(pop_count(pieces(c, QUEN))-1, 0)) > 8)
            || (          (pop_count(pieces(c, PAWN))
                + std::max(pop_count(pieces(c, BSHP) & Color_bb[WHITE])-1, 0)
                + std::max(pop_count(pieces(c, BSHP) & Color_bb[BLACK])-1, 0)) > 8))
        {
            assert(false && "Position OK: BITBOARD");
            return false;
        }
    }

    // PSQ and NPM
    if (   psq != compute_psq(*this)
        || npm[WHITE] != compute_npm<WHITE>(*this)
        || npm[BLACK] != compute_npm<BLACK>(*this))
    {
        assert(false && "Position OK: PSQ");
        return false;
    }

    if (Fast)
    {
        return true;
    }

    // SQUARE_LIST
    for (const auto pc : { W_PAWN, W_NIHT, W_BSHP, W_ROOK, W_QUEN, W_KING,
                           B_PAWN, B_NIHT, B_BSHP, B_ROOK, B_QUEN, B_KING })
    {
        if (count(pc) != pop_count(pieces(pc)))
        {
            assert(false && "Position OK: SQUARE_LIST");
            return false;
        }
        for (auto s : squares[pc])
        {
            if (   !_ok(s)
                || piece[s] != pc)
            {
                assert(false && "Position OK: SQUARE_LIST");
                return false;
            }
        }
    }

    // CASTLING
    for (const auto c : { WHITE, BLACK })
    {
        for (const auto cs : { CS_KING, CS_QUEN })
        {
            auto cr = c|cs;
            if (   si->can_castle(cr)
                && (   piece[castle_rook_sq[c][cs]] != (c|ROOK)
                    || castle_right[castle_rook_sq[c][cs]] != cr
                    || (castle_right[square(c|KING)] & cr) != cr))
            {
                assert(false && "Position OK: CASTLING");
                return false;
            }
        }
    }
    // STATE_INFO
    if (   si->matl_key != RandZob.compute_matl_key(*this)
        || si->pawn_key != RandZob.compute_pawn_key(*this)
        || si->posi_key != RandZob.compute_posi_key(*this)
        || si->checkers != (attackers_to(square(active|KING)) & pieces(~active))
        || 2 < pop_count(si->checkers)
        || si->clock_ply > 2*i32(Options["Draw MoveCount"])
        || (   NONE != si->capture
            && 0 != si->clock_ply)
        || (   SQ_NO != si->enpassant_sq
            && (   0 != si->clock_ply
                || R_6 != rel_rank(active, si->enpassant_sq)
                || !can_enpassant(active, si->enpassant_sq))))
    {
        assert(false && "Position OK: STATE_INFO");
        return false;
    }

    return true;
}
#endif
