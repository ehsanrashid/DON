#include "Position.h"

#include <cstring>
#include <iomanip>
#include <sstream>

#include "Transposition.h"
#include "MoveGenerator.h"
#include "Thread.h"
#include "Notation.h"

using namespace std;
using namespace BitBoard;
using namespace Transposition;
using namespace MoveGen;
using namespace Threading;
using namespace Notation;

const Value PIECE_VALUE[PHASE_NO][TOTL] =
{
    { VALUE_MG_PAWN, VALUE_MG_NIHT, VALUE_MG_BSHP, VALUE_MG_ROOK, VALUE_MG_QUEN, VALUE_ZERO, VALUE_ZERO },
    { VALUE_EG_PAWN, VALUE_EG_NIHT, VALUE_EG_BSHP, VALUE_EG_ROOK, VALUE_EG_QUEN, VALUE_ZERO, VALUE_ZERO }
};

// PSQ[Color][PieceType][Square] contains Color-PieceType-Square scores.
Score PSQ[CLR_NO][NONE][SQ_NO];

const string PIECE_CHAR ("PNBRQK  pnbrqk");
const string COLOR_CHAR ("wb-");
const string STARTUP_FEN ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

bool _ok (const string &fen, bool c960, bool full)
{
    return Position (fen, nullptr, c960, full).ok ();
}

namespace {

#define S(mg, eg) mk_score (mg, eg)

    // PSQ_BONUS[PieceType][Rank][File/2] contains Piece-Square scores.
    // For each piece type on a given square a (middlegame, endgame) score pair is assigned.
    // Table is defined for files A..D and white side: it is symmetric for black side and
    // second half of the files.
    const Score PSQ_BONUS[NONE][R_NO][F_NO/2] =
    {
        { // Pawn
            { S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0) },
            { S(-19, 5), S(  1,-4), S(  7, 8), S( 3,-2) },
            { S(-26,-6), S( -7,-5), S( 19, 5), S(24, 4) },
            { S(-25, 1), S(-14, 3), S( 16,-8), S(31,-3) },
            { S(-14, 6), S(  0, 9), S( -1, 7), S(17,-6) },
            { S(-14, 6), S(-13,-5), S(-10, 2), S(-6, 4) },
            { S(-12, 1), S( 15,-9), S( -8, 1), S(-4,18) },
            { S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0) }
        },
        { // Knight
            { S(-143, -97), S(-96,-82), S(-80,-46), S(-73,-14) },
            { S( -83, -69), S(-43,-55), S(-21,-17), S(-10,  9) },
            { S( -71, -50), S(-22,-39), S(  0, -8), S(  9, 28) },
            { S( -25, -41), S( 18,-25), S( 43,  7), S( 47, 38) },
            { S( -26, -46), S( 16,-25), S( 38,  2), S( 50, 41) },
            { S( -11, -55), S( 37,-38), S( 56, -8), S( 71, 27) },
            { S( -62, -64), S(-17,-50), S(  5,-24), S( 14, 13) },
            { S(-195,-110), S(-66,-90), S(-42,-50), S(-29,-13) }
        },
        { // Bishop
            { S(-54,-68), S(-23,-40), S(-35,-46), S(-44,-28) },
            { S(-30,-43), S( 10,-17), S(  2,-23), S( -9, -5) },
            { S(-19,-32), S( 17, -9), S( 11,-13), S(  1,  8) },
            { S(-21,-36), S( 18,-13), S( 11,-15), S(  0,  7) },
            { S(-21,-36), S( 14,-14), S(  6,-17), S( -1,  3) },
            { S(-27,-35), S(  6,-13), S(  2,-10), S( -8,  1) },
            { S(-33,-44), S(  7,-21), S( -4,-22), S(-12, -4) },
            { S(-45,-65), S(-21,-42), S(-29,-46), S(-39,-27) }
        },
        { // Rook
            { S(-25, 0), S(-16, 0), S(-16, 0), S(-9, 0) },
            { S(-21, 0), S( -8, 0), S( -3, 0), S( 0, 0) },
            { S(-21, 0), S( -9, 0), S( -4, 0), S( 2, 0) },
            { S(-22, 0), S( -6, 0), S( -1, 0), S( 2, 0) },
            { S(-22, 0), S( -7, 0), S(  0, 0), S( 1, 0) },
            { S(-21, 0), S( -7, 0), S(  0, 0), S( 2, 0) },
            { S(-12, 0), S(  4, 0), S(  8, 0), S(12, 0) },
            { S(-23, 0), S(-15, 0), S(-11, 0), S(-5, 0) }
        },
        { // Queen
            { S( 0,-70), S(-3,-57), S(-4,-41), S(-1,-29) },
            { S(-4,-58), S( 6,-30), S( 9,-21), S( 8, -4) },
            { S(-2,-39), S( 6,-17), S( 9, -7), S( 9,  5) },
            { S(-1,-29), S( 8, -5), S(10,  9), S( 7, 17) },
            { S(-3,-27), S( 9, -5), S( 8, 10), S( 7, 23) },
            { S(-2,-40), S( 6,-16), S( 8,-11), S(10,  3) },
            { S(-2,-54), S( 7,-30), S( 7,-21), S( 6, -7) },
            { S(-1,-75), S(-4,-54), S(-1,-44), S( 0,-30) }
        },
        { // King
            { S(291, 28), S(344, 76), S(294,103), S(219,112) },
            { S(289, 70), S(329,119), S(263,170), S(205,159) },
            { S(226,109), S(271,164), S(202,195), S(136,191) },
            { S(204,131), S(212,194), S(175,194), S(137,204) },
            { S(177,132), S(205,187), S(143,224), S( 94,227) },
            { S(147,118), S(188,178), S(113,199), S( 70,197) },
            { S(116, 72), S(158,121), S( 93,142), S( 48,161) },
            { S( 94, 30), S(120, 76), S( 78,101), S( 31,111) }
        }
    };

#undef S

}

u08 Position::FiftyMoveDist = 100;

void Position::initialize ()
{
    for (auto pt = PAWN; pt <= KING; ++pt)
    {
        auto score = mk_score (PIECE_VALUE[MG][pt], PIECE_VALUE[EG][pt]);

        for (auto s = SQ_A1; s <= SQ_H8; ++s)
        {
            auto psq_bonus = score + PSQ_BONUS[pt][_rank (s)][_file (s) < F_E ? _file (s) : F_H - _file (s)];
            PSQ[WHITE][pt][ s] = +psq_bonus;
            PSQ[BLACK][pt][~s] = -psq_bonus;
        }
    }
}

// Position::operator=() creates a copy of 'pos' but detaching the state pointer
// from the source to be self-consistent and not depending on any external data.
Position& Position::operator= (const Position &pos)
{
    std::memcpy (this, &pos, sizeof (*this));
    _ssi = *_psi;
    _psi = &_ssi;
    _game_nodes = 0;

    assert (ok ());
    return *this;
}

// Draw by: 50 Move Rule, Threefold repetition.
// It does not detect draw by Material and Stalemate, this must be done by the search.
bool Position::draw () const
{
    // Draw by 50 moves Rule?
    // Not in check or in check have legal moves 
    if (    _psi->clock50 >= FiftyMoveDist
        && (_psi->checkers == U64(0) || MoveList<LEGAL> (*this).size () != 0)
       )
    {
        return true;
    }
    // Draw by Threefold Repetition?
    const auto *psi = _psi;
    //u08 cnt = 1;
    for (i08 ply = std::min (_psi->clock50, _psi->null_ply); ply >= 2; ply -= 2)
    {
        psi = psi->ptr->ptr;
        if (psi->posi_key == _psi->posi_key)
        {
            //if (++cnt >= 3)
            return true; // Draw at first repetition
        }
    }

    /*
    // Draw by Material?
    if (   _types_bb[PAWN] == U64(0)
        && _psi->non_pawn_matl[WHITE] + _psi->non_pawn_matl[BLACK] <= VALUE_MG_BSHP
       )
    {
        return true;
    }
    */
    /*
    // Draw by Stalemate?
    if (   _psi->checkers == U64(0)
        //&& game_phase () < PHASE_MIDGAME - 50
        && count<NONPAWN> (_active) < count<NONPAWN> (~_active)
        && (count<NONPAWN> (_active) < 3 || (count<NONPAWN> (_active) < 5 && pinneds (_active)))
        && MoveList<LEGAL> (*this).size () == 0
       )
    {
        return true;
    }
    */

    return false;
}

// Check whether there has been at least one repetition of positions
// since the last capture or pawn move.
bool Position::repeated () const
{
    auto *si = _psi;
    while (si != nullptr)
    {
        i08 ply = std::min (si->clock50, si->null_ply);
        if (4 > ply) return false;
        auto *psi = si->ptr->ptr;
        do
        {
            psi = psi->ptr->ptr;
            if (psi->posi_key == si->posi_key) return true;
            ply -= 2;
        } while (4 <= ply);

        si = si->ptr;
    }
    return false;
}

// Position consistency test, for debugging
bool Position::ok (i08 *failed_step) const
{
    const bool Fast = true; // Quick (default) or full check?

    enum { Quick, King, Bitboards, State, Lists, Castling };

    for (i08 step = Quick; step <= (Fast ? Quick : Castling); ++step)
    {
        if (failed_step != nullptr) *failed_step = step;

        if (step == Quick)
        {
            if (   (WHITE != _active && BLACK != _active)
                || (W_KING != _board[_piece_square[WHITE][KING][0]])
                || (B_KING != _board[_piece_square[BLACK][KING][0]])
                || count<NONE> () > 32 || count<NONE> () != pop_count<FULL> (_types_bb[NONE])
                || (SQ_NO != en_passant_sq () && (R_6 != rel_rank (_active, en_passant_sq ()) || !can_en_passant (en_passant_sq ())))
                || (_psi->clock50 > 100)
               )
            {
                return false;
            }
        }

        if (step == King)
        {
            if (   1 != std::count (_board, _board + SQ_NO, W_KING)
                || 1 != std::count (_board, _board + SQ_NO, B_KING)
                || attackers_to (_piece_square[~_active][KING][0], _active)
                || pop_count<MAX15> (_psi->checkers) > 2
               )
            {
                return false;
            }
        }

        if (step == Bitboards)
        {
            if (   (_color_bb[WHITE] & _color_bb[BLACK]) != U64(0)
                || (_color_bb[WHITE]|_color_bb[BLACK]) != _types_bb[NONE]
                || (_color_bb[WHITE]^_color_bb[BLACK]) != _types_bb[NONE]
               )
            {
                return false;
            }

            for (auto pt1 = PAWN; pt1 <= KING; ++pt1)
            {
                for (auto pt2 = PAWN; pt2 <= KING; ++pt2)
                {
                    if (pt1 != pt2 && (_types_bb[pt1]&_types_bb[pt2]))
                    {
                        return false;
                    }
                }
            }
            if (   (_types_bb[PAWN]|_types_bb[NIHT]|_types_bb[BSHP]|_types_bb[ROOK]|_types_bb[QUEN]|_types_bb[KING]) != _types_bb[NONE]
                || (_types_bb[PAWN]^_types_bb[NIHT]^_types_bb[BSHP]^_types_bb[ROOK]^_types_bb[QUEN]^_types_bb[KING]) != _types_bb[NONE]
               )
            {
                return false;
            }

            if ((R1_bb|R8_bb) & _types_bb[PAWN])
            {
                return false;
            }

            for (auto c = WHITE; c <= BLACK; ++c)
            {
                auto colors = _color_bb[c];

                if (pop_count<FULL> (colors) > 16) // Too many Piece of color
                {
                    return false;
                }
                // check if the number of Pawns plus the number of
                // extra Queens, Rooks, Bishops, Knights exceeds 8
                // (which can result only by promotion)
                if (      (_piece_count[c][PAWN]
                    + std::max (_piece_count[c][NIHT] - 2, 0)
                    + std::max (_piece_count[c][BSHP] - 2, 0)
                    + std::max (_piece_count[c][ROOK] - 2, 0)
                    + std::max (_piece_count[c][QUEN] - 1, 0)) > 8
                   )
                {
                    return false; // Too many Promoted Piece of color
                }

                if (_piece_count[c][BSHP] > 1)
                {
                    auto bishops = colors & _types_bb[BSHP];
                    u08 bishop_count[CLR_NO] =
                    {
                        u08(pop_count<MAX15> (LIHT_bb & bishops)),
                        u08(pop_count<MAX15> (DARK_bb & bishops)),
                    };

                    if (      (_piece_count[c][PAWN]
                        + std::max (bishop_count[WHITE] - 1, 0)
                        + std::max (bishop_count[BLACK] - 1, 0)) > 8
                       )
                    {
                        return false; // Too many Promoted BISHOP of color
                    }
                }

                // There should be one and only one KING of color
                auto kings = colors & _types_bb[KING];
                if (kings == U64(0) || more_than_one (kings))
                {
                    return false;
                }
            }
        }

        if (step == State)
        {
            if (   Zob.compute_matl_key (*this) != _psi->matl_key
                || Zob.compute_pawn_key (*this) != _psi->pawn_key
                || Zob.compute_posi_key (*this) != _psi->posi_key
                || compute_psq_score () != _psi->psq_score
                || compute_non_pawn_material (WHITE) != _psi->non_pawn_matl[WHITE]
                || compute_non_pawn_material (BLACK) != _psi->non_pawn_matl[BLACK]
               )
            {
                return false;
            }
        }

        if (step == Lists)
        {
            for (auto c = WHITE; c <= BLACK; ++c)
            {
                for (auto pt = PAWN; pt <= KING; ++pt)
                {
                    if (_piece_count[c][pt] != pop_count<MAX15> (_color_bb[c]&_types_bb[pt]))
                    {
                        return false;
                    }

                    for (auto i = 0; i < _piece_count[c][pt]; ++i)
                    {
                        if (   !_ok  (_piece_square[c][pt][i])
                            || _board[_piece_square[c][pt][i]] != (c|pt)
                            || _piece_index[_piece_square[c][pt][i]] != i
                           )
                        {
                            return false;
                        }
                    }
                }
            }
            for (auto s = SQ_A1; s <= SQ_H8; ++s)
            {
                if (_piece_index[s] >= 16)
                {
                    return false;
                }
            }
        }

        if (step == Castling)
        {
            for (auto c = WHITE; c <= BLACK; ++c)
            {
                for (auto cs = CS_KING; cs <= CS_QUEN; ++cs)
                {
                    auto cr = mk_castle_right (c, cs);

                    if (!can_castle (cr)) continue;

                    if (    _board[_castle_rook[cr]] != (c|ROOK)
                        ||  _castle_mask[_castle_rook[cr]] != cr
                        || (_castle_mask[_piece_square[c][KING][0]] & cr) != cr
                       )
                    {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

// pick_least_val_att() is a helper function used by see()
// to locate the least valuable attacker for the side to move,
// remove the attacker just found from the bitboards and
// scan for new X-ray attacks behind it.
template<PieceT PT>
PieceT Position::pick_least_val_att (Square dst, Bitboard stm_attackers, Bitboard &mocc, Bitboard &attackers) const
{
    auto b = stm_attackers & _types_bb[PT];
    if (b != U64(0))
    {
        mocc ^= b & ~(b - 1);

        switch (PT)
        {
        case PAWN:
        case BSHP:
            attackers |= (attacks_bb<BSHP> (dst, mocc)&(_types_bb[BSHP]|_types_bb[QUEN]));
            break;
        case ROOK:
            attackers |= (attacks_bb<ROOK> (dst, mocc)&(_types_bb[ROOK]|_types_bb[QUEN]));
            break;
        case QUEN:
            attackers |= (attacks_bb<BSHP> (dst, mocc)&(_types_bb[BSHP]|_types_bb[QUEN]))
                      |  (attacks_bb<ROOK> (dst, mocc)&(_types_bb[ROOK]|_types_bb[QUEN]));
            break;
        default: break;
        }

        attackers &= mocc; // After X-ray that may add already processed pieces
        
        return PT;
    }

    return pick_least_val_att<PieceT(PT+1)> (dst, stm_attackers, mocc, attackers);
}
template<>
PieceT Position::pick_least_val_att<KING> (Square, Bitboard, Bitboard&, Bitboard&) const
{
    return KING; // No need to update bitboards, it is the last cycle
}

// see() is a Static Exchange Evaluator (SEE):
// It tries to estimate the material gain or loss resulting from a move.
Value Position::see      (Move m) const
{
    assert (_ok (m));

    auto org = org_sq (m);
    auto dst = dst_sq (m);

    // Side to move
    auto stm = color (_board[org]);

    const i08 MAX_GAINS = 32;
    // Gain list
    Value gain_list[MAX_GAINS];
    i08   depth = 1;

    auto mocc = _types_bb[NONE] - org;

    switch (mtype (m))
    {
    case CASTLE:
        // Castle moves are implemented as king capturing the rook so cannot be
        // handled correctly. Simply return 0 that is always the correct value
        // unless in the rare case the rook ends up under attack.
        return VALUE_ZERO;
        break;

    case ENPASSANT:
        // Remove the captured pawn
        mocc -= dst - pawn_push (stm);
        gain_list[0] = PIECE_VALUE[MG][PAWN];
        break;

    default:
        gain_list[0] = PIECE_VALUE[MG][ptype (_board[dst])];
        break;
    }

    // Find all attackers to the destination square, with the moving piece
    // removed, but possibly an X-ray attacker added behind it.
    auto attackers = attackers_to (dst, mocc) & mocc;

    // If the opponent has any attackers
    stm = ~stm;
    auto stm_attackers = attackers & _color_bb[stm];

    if (stm_attackers != U64(0))
    {
        // The destination square is defended, which makes things rather more
        // difficult to compute. Proceed by building up a "swap list" containing
        // the material gain or loss at each stop in a sequence of captures to the
        // destination square, where the sides alternately capture, and always
        // capture with the least valuable piece. After each capture, look for
        // new X-ray attacks from behind the capturing piece.
        PieceT captured = ptype (_board[org]);

        do
        {
            assert (depth < MAX_GAINS);

            // Add the new entry to the swap list
            gain_list[depth] = PIECE_VALUE[MG][captured] - gain_list[depth - 1];

            // Locate and remove the next least valuable attacker
            captured = pick_least_val_att<PAWN> (dst, stm_attackers, mocc, attackers);

            stm = ~stm;
            stm_attackers = attackers & _color_bb[stm];

            ++depth;
        } while (stm_attackers != U64(0) && (captured != KING || (--depth, false))); // Stop before a king capture

        // Having built the swap list, negamax through it to find the best
        // achievable score from the point of view of the side to move.
        while (--depth != 0)
        {
            gain_list[depth - 1] = std::min (-gain_list[depth], gain_list[depth - 1]);
        }
    }

    return gain_list[0];
}

Value Position::see_sign (Move m) const
{
    assert (_ok (m));

    // Early return if SEE cannot be negative because captured piece value
    // is not less then capturing one. Note that king moves always return
    // here because king midgame value is set to 0.
    if (  PIECE_VALUE[MG][ptype (_board[org_sq (m)])]
       <= PIECE_VALUE[MG][ptype (_board[dst_sq (m)])]
       )
    {
        return VALUE_KNOWN_WIN;
    }

    return see (m);
}

Bitboard Position::check_blockers (Color piece_c, Color king_c) const
{
    auto ksq = _piece_square[king_c][KING][0];
    // Pinners are sliders that give check when a pinned piece is removed
    // Only one real pinner exist other are fake pinner
    auto pinners =
        (  ((_types_bb[ROOK]|_types_bb[QUEN]) & PIECE_ATTACKS[ROOK][ksq])
         | ((_types_bb[BSHP]|_types_bb[QUEN]) & PIECE_ATTACKS[BSHP][ksq])
        ) &  _color_bb[~king_c];

    auto chk_blockers = U64(0);
    while (pinners != U64(0))
    {
        auto blocker = BETWEEN_bb[ksq][pop_lsq (pinners)] & _types_bb[NONE];
        if (blocker != U64(0) && !more_than_one (blocker))
        {
            chk_blockers |= blocker & _color_bb[piece_c];
        }
    }

    return chk_blockers;
}

// pseudo_legal() tests whether a random move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal (Move m) const
{
    assert (_ok (m));
    auto org = org_sq (m);
    auto dst = dst_sq (m);
    auto mpc = _board[org];
    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if (NONE == ptype (mpc) || _active != color (mpc)) return false;

    auto cpt = NONE;

    auto cap = dst;

    switch (mtype (m))
    {
    case NORMAL:
    {
        // Is not a promotion, so promotion piece must be empty
        if (PAWN != promote (m) - NIHT) return false;
        cpt = ptype (_board[cap]);
    }
        break;

    case CASTLE:
    {
        // Check whether the destination square is attacked by the opponent.
        // Castling moves are checked for legality during move generation.
        if (!(   KING == ptype (mpc)
              && R_1 == rel_rank (_active, org)
              && R_1 == rel_rank (_active, dst)
              && _board[dst] == (_active|ROOK)
              && _psi->checkers == U64(0)
              && _psi->castle_rights & mk_castle_right (_active)
              && !castle_impeded (mk_castle_right (_active, dst > org ? CS_KING : CS_QUEN))
             )
           )
        {
            return false;
        }

        // Castle is always encoded as "King captures friendly Rook"
        assert (dst == castle_rook (mk_castle_right (_active, dst > org ? CS_KING : CS_QUEN)));
        dst = rel_sq (_active, dst > org ? SQ_G1 : SQ_C1);
        auto step = dst > org ? DEL_E : DEL_W;
        for (auto s = dst; s != org; s -= step)
        {
            if (attackers_to (s, ~_active) != U64(0))
            {
                return false;
            }
        }

        //cpt = NONE;
        return true;
    }
        break;

    case ENPASSANT:
    {
        if (!(   PAWN == ptype (mpc)
              && _psi->en_passant_sq == dst
              && R_5 == rel_rank (_active, org)
              && R_6 == rel_rank (_active, dst)
              && empty (dst)
             )
           )
        {
            return false;
        }
        cap += pawn_push (~_active);
        if ((~_active|PAWN) != _board[cap]) return false;
        cpt = PAWN;
    }
        break;

    case PROMOTE:
    {
        if (!(   PAWN == ptype (mpc)
              && R_7 == rel_rank (_active, org)
              && R_8 == rel_rank (_active, dst)
              && NIHT <= promote (m) && promote (m) <= QUEN
             )
           )
        {
            return false;
        }
        cpt = ptype (_board[cap]);
    }
        break;

    default:
        assert (false);
        break;
    }

    if (KING == cpt) return false;

    // The destination square cannot be occupied by a friendly piece
    if (_color_bb[_active] & dst) return false;

    // Handle the special case of a piece move
    if (PAWN == ptype (mpc))
    {
        // Have already handled promotion moves, so destination
        // cannot be on the 8th/1st rank.
        if (R_1 == rel_rank (_active, org) || R_8 == rel_rank (_active, org) || R_1 == rel_rank (_active, dst) || R_2 == rel_rank (_active, dst)) return false;
        if (NORMAL == mtype (m) && (R_7 == rel_rank (_active, org) || R_8 == rel_rank (_active, dst))) return false;
        
        if (   // Not a capture
               !(   (PAWN_ATTACKS[_active][org] & _color_bb[~_active] & dst)
                 && 1 == dist<File> (dst, org)
                 && 1 == dist<Rank> (dst, org)
                )
               // Not a single push
            && !(   empty (dst)
                 && 0 == dist<File> (dst, org)
                 && 1 == dist<Rank> (dst, org)
                 && (org + pawn_push (_active) == dst)
                )
               // Not a double push
            && !(   _rank (org) == rel_rank (_active, R_2)
                 && empty (dst)
                 && empty (dst - pawn_push (_active))
                 && 0 == dist<File> (dst, org)
                 && 2 == dist<Rank> (dst, org)
                 && (org + 2*pawn_push (_active) == dst)
                )
           )
        {
            return false;
        }

    }
    else
    {
        if ((attacks_bb (mpc, org, _types_bb[NONE]) & dst) == U64(0)) return false;
    }

    // Evasions generator already takes care to avoid some kind of illegal moves
    // and legal() relies on this. So have to take care that the
    // same kind of moves are filtered out here.
    if (_psi->checkers != U64(0))
    {
        // In case of king moves under check, remove king so to catch
        // as invalid moves like B1A1 when opposite queen is on C1.
        if (KING == ptype (mpc)) return attackers_to (dst, ~_active, _types_bb[NONE] - org) == U64(0); // Remove 'org' but not place 'dst'

        // Double check? In this case a king move is required
        if (more_than_one (_psi->checkers)) return false;

        return ENPASSANT == mtype (m) && PAWN == ptype (mpc) ?
            // Move must be a capture of the checking en-passant pawn
            // or a blocking evasion of the checking piece
            (_psi->checkers & cap) != U64(0) || (BETWEEN_bb[scan_lsq (_psi->checkers)][_piece_square[_active][KING][0]] & dst) != U64(0) :
            // Move must be a capture or a blocking evasion of the checking piece
            ((_psi->checkers | BETWEEN_bb[scan_lsq (_psi->checkers)][_piece_square[_active][KING][0]]) & dst) != U64(0);
    }
    return true;
}

// legal() tests whether a pseudo-legal move is legal
bool Position::legal        (Move m, Bitboard pinned) const
{
    assert (_ok (m));
    assert (pinned == pinneds (_active));

    auto org = org_sq (m);
    auto dst = dst_sq (m);
    auto mpc = _board[org];

    assert (_active == color (mpc) && NONE != ptype (mpc));

    switch (mtype (m))
    {
    case NORMAL:
    {
        if ((_color_bb[_active] & dst) != U64(0)) return false;

        // Only king moves to non attacked squares, sliding check x-rays the king
        // In case of king moves under check have to remove king so to catch
        // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
        // check whether the destination square is attacked by the opponent.
        if (KING == ptype (mpc))
        {
            return attackers_to (dst, ~_active, _types_bb[NONE] - org) == U64(0); // Remove 'org' but not place 'dst'
        }
    }
    // NOTE: no break
    case PROMOTE:
    {
        // A non-king move is legal if and only if it is not pinned or
        // it is moving along the ray towards or away from the king or
        // it is a blocking evasion or a capture of the checking piece.
        return  pinned == U64(0)
            || (pinned & org) == U64(0)
            || sqrs_aligned (org, dst, _piece_square[_active][KING][0]);
    }
        break;

    case CASTLE:
    {
        // Castling moves are checked for legality during move generation.
        return KING == ptype (mpc) && ROOK == ptype (_board[dst]);
    }
        break;

    case ENPASSANT:
    {
        // En-passant captures are a tricky special case. Because they are rather uncommon,
        // do it simply by testing whether the king is attacked after the move is made.
        auto cap = dst + pawn_push (~_active);

        assert (dst == _psi->en_passant_sq && empty (dst) && (_active|PAWN) == mpc && !empty (cap) && (~_active|PAWN) == _board[cap]);

        auto mocc = _types_bb[NONE] - org - cap + dst;
        // If any attacker then in check & not legal
        return (attacks_bb<ROOK> (_piece_square[_active][KING][0], mocc) & (_color_bb[~_active]&(_types_bb[QUEN]|_types_bb[ROOK]))) == U64(0)
            && (attacks_bb<BSHP> (_piece_square[_active][KING][0], mocc) & (_color_bb[~_active]&(_types_bb[QUEN]|_types_bb[BSHP]))) == U64(0);
    }
        break;

    default:
        assert (false);
        return false;
        break;
    }
}

// gives_check() tests whether a pseudo-legal move gives a check
bool Position::gives_check  (Move m, const CheckInfo &ci) const
{
    auto org = org_sq (m);
    auto dst = dst_sq (m);

    assert (color (_board[org]) == _active);
    assert (ci.discoverers == discoverers (_active));
    
    // Is there a Direct check ?
    if ((ci.checking_bb[ptype (_board[org])] & dst) != U64(0)) return true;
    // Is there a Discovered check ?
    // For discovery check we need to verify also direction
    if (    ci.discoverers != U64(0)
        && (ci.discoverers & org) != U64(0)
        && !sqrs_aligned (org, dst, ci.king_sq)
       )
    {
        return true;
    }

    switch (mtype (m))
    {
    case NORMAL:
        return false;
        break;

    case CASTLE:
    {
        // Castling with check ?
        auto rook_org = dst; // 'King captures the rook' notation
        dst           = rel_sq (_active, (dst > org) ? SQ_G1 : SQ_C1);
        auto rook_dst = rel_sq (_active, (dst > org) ? SQ_F1 : SQ_D1);

        return (PIECE_ATTACKS[ROOK][rook_dst] & ci.king_sq) != U64(0) // First x-ray check then full check
            && (attacks_bb<ROOK> (rook_dst, (_types_bb[NONE] - org - rook_org + dst + rook_dst)) & ci.king_sq) != U64(0);
    }
        break;
    
    case ENPASSANT:
    {
        // En-passant capture with check ?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        auto cap = _file (dst)|_rank (org);
        auto mocc = _types_bb[NONE] - org - cap + dst;
        // if any attacker then in check
        return (attacks_bb<ROOK> (ci.king_sq, mocc) & (_color_bb[_active]&(_types_bb[QUEN]|_types_bb[ROOK]))) != U64(0)
            || (attacks_bb<BSHP> (ci.king_sq, mocc) & (_color_bb[_active]&(_types_bb[QUEN]|_types_bb[BSHP]))) != U64(0);
    }
        break;

    case PROMOTE:
    {
        // Promotion with check ?
        return (attacks_bb (Piece(promote (m)), dst, _types_bb[NONE] - org + dst) & ci.king_sq) != U64(0);
    }
        break;
    
    default:
        assert (false);
        return false;
        break;
    }
}

//// gives_checkmate() tests whether a pseudo-legal move gives a checkmate
//bool Position::gives_checkmate (Move m, const CheckInfo &ci)
//{
//    bool checkmate = false;
//    if (gives_check (m, ci))
//    {
//        StateInfo si;
//        do_move (m, si, true);
//        checkmate = MoveList<LEGAL> (*this).size () == 0;
//        undo_move ();
//    }
//    return checkmate;
//}

// clear() clear the position
void Position::clear ()
{
    std::memset (this, 0x00, sizeof (*this));

    for (auto s = SQ_A1; s <= SQ_H8; ++s)
    {
        _board[s] = EMPTY;
        _piece_index[s] = -1;
    }
    for (auto c = WHITE; c <= BLACK; ++c)
    {
        for (auto pt = PAWN; pt <= KING; ++pt)
        {
            for (auto i = 0; i < 16; ++i)
            {
                _piece_square[c][pt][i] = SQ_NO;
            }
        }
    }

    std::fill (std::begin (_castle_rook), std::end (_castle_rook), SQ_NO);

    _ssi.en_passant_sq = SQ_NO;
    _ssi.capture_type  = NONE;
    _ssi.last_move     = MOVE_NONE;
    _psi = &_ssi;
}

// set_castle() set the castling for the particular color & rook
void Position::set_castle (Color c, Square rook_org)
{
    auto king_org = _piece_square[c][KING][0];
    assert (king_org != rook_org);

    auto cr = mk_castle_right (c, (rook_org > king_org) ? CS_KING : CS_QUEN);
    auto rook_dst = rel_sq (c, (rook_org > king_org) ? SQ_F1 : SQ_D1);
    auto king_dst = rel_sq (c, (rook_org > king_org) ? SQ_G1 : SQ_C1);

    _psi->castle_rights     |= cr;

    _castle_mask[king_org] |= cr;
    _castle_mask[rook_org] |= cr;
    _castle_rook[cr] = rook_org;

    for (auto s = std::min (rook_org, rook_dst); s <= std::max (rook_org, rook_dst); ++s)
    {
        if (king_org != s && rook_org != s)
        {
            _castle_path[cr] += s;
        }
    }
    for (auto s = std::min (king_org, king_dst); s <= std::max (king_org, king_dst); ++s)
    {
        if (king_org != s && rook_org != s)
        {
            _castle_path[cr] += s;
            _king_path[cr] += s;
        }
    }
}
// can_en_passant() tests the en-passant square
bool Position::can_en_passant (Square ep_sq) const
{
    assert (_ok (ep_sq));
    assert (R_6 == rel_rank (_active, ep_sq));

    auto cap = ep_sq + pawn_push (~_active);
    //if (!((_color_bb[~_active]&_types_bb[PAWN]) & cap)) return false;
    if ((~_active|PAWN) != _board[cap]) return false;

    // En-passant attackes
    auto attacks = PAWN_ATTACKS[~_active][ep_sq] & _color_bb[_active]&_types_bb[PAWN];
    assert (pop_count<FULL> (attacks) <= 2);
    if (attacks == U64(0)) return false;

    Move moves[3], *m = moves;
    while (attacks != U64(0))
    {
        *(m++) = mk_move<ENPASSANT> (pop_lsq (attacks), ep_sq);
    }
    *m = MOVE_NONE;

    // Check en-passant is legal for the position
    auto occ = _types_bb[NONE] + ep_sq - cap;
    for (m = moves; *m != MOVE_NONE; ++m)
    {
        auto mocc = occ - org_sq (*m);
        if ((attacks_bb<ROOK> (_piece_square[_active][KING][0], mocc) & (_color_bb[~_active]&(_types_bb[QUEN]|_types_bb[ROOK]))) == U64(0)
            && (attacks_bb<BSHP> (_piece_square[_active][KING][0], mocc) & (_color_bb[~_active]&(_types_bb[QUEN]|_types_bb[BSHP]))) == U64(0)
            )
        {
            return true;
        }
    }

    return false;
}
// A FEN string defines a particular position using only the ASCII character set.
//
// A FEN string contains six fields separated by a space. The fields are:
//
// 1) Piece placement (from white's perspective).
//    Each rank is described, starting with rank 8 and ending with rank 1;
//    within each rank, the contents of each square are described from file A through file H.
//    Following the Standard Algebraic Notation (SAN),
//    each piece is identified by a single letter taken from the standard English names.
//    White pieces are designated using upper-case letters ("PNBRQK") while Black take lowercase ("pnbrqk").
//    Blank squares are noted using digits 1 through 8 (the number of blank squares),
//    and "/" separates ranks.
//
// 2) Active color. "w" means white, "b" means black - moves next,.
//
// 3) Castling availability. If neither side can castle, this is "-". 
//    Otherwise, this has one or more letters:
//    "K" (White can castle  Kingside),
//    "Q" (White can castle Queenside),
//    "k" (Black can castle  Kingside),
//    "q" (Black can castle Queenside).
//
// 4) En passant target square (in algebraic notation).
//    If there's no en passant target square, this is "-".
//    If a pawn has just made a 2-square move, this is the position "behind" the pawn.
//    This is recorded regardless of whether there is a pawn in position to make an en passant capture.
//
// 5) Halfmove clock. This is the number of halfmoves since the last pawn advance or capture.
//    This is used to determine if a draw can be claimed under the fifty-move rule.
//
// 6) Fullmove number. The number of the full move.
//    It starts at 1, and is incremented after Black's move.
bool Position::setup (const string &f, Thread *const th, bool c960, bool full)
{
    if (white_spaces (f)) return false;

    istringstream iss (f);
    iss >> noskipws;

    clear ();
    
    u08 ch;
    // 1. Piece placement on Board
    size_t idx;
    auto s = SQ_A8;
    while (iss >> ch && !isspace (ch))
    {
        if (isdigit (ch))
        {
            s += Delta (ch - '0'); // Advance the given number of files
        }
        else
        if (isalpha (ch) && (idx = PIECE_CHAR.find (ch)) != string::npos)
        {
            auto pc = Piece(idx);
            place_piece (s, color (pc), ptype (pc));
            ++s;
        }
        else
        if (ch == '/')
        {
            s += DEL_SS;
        }
        else
        {
            return false;
        }
    }

    assert (_piece_square[WHITE][KING][0] != SQ_NO);
    assert (_piece_square[BLACK][KING][0] != SQ_NO);

    // 2. Active color
    iss >> ch;
    _active = Color(COLOR_CHAR.find (ch));

    // 3. Castling rights availability
    // Compatible with 3 standards:
    // 1-Normal FEN standard,
    // 2-Shredder-FEN that uses the letters of the columns on which the rooks began the game instead of KQkq
    // 3-X-FEN standard that, in case of Chess960, if an inner rook is associated with the castling right, the castling
    // tag is replaced by the file letter of the involved rook, as for the Shredder-FEN.
    iss >> ch;
    while ((iss >> ch) && !isspace (ch))
    {
        Square r_sq;
        auto c = isupper (ch) ? WHITE : BLACK;
        
        if (ch == '-') continue;

        if (c960)
        {
            if (!('a' <= tolower (ch) && tolower (ch) <= 'h')) return false;
            r_sq = to_file (char(tolower (ch))) | rel_rank (c, R_1);
        }
        else
        {
            switch (char(toupper (ch)))
            {
            case 'K':
                for (r_sq  = rel_sq (c, SQ_H1); r_sq >= rel_sq (c, SQ_A1) && (c|ROOK) != _board[r_sq]; --r_sq) {}
                break;
            case 'Q':
                for (r_sq  = rel_sq (c, SQ_A1); r_sq <= rel_sq (c, SQ_H1) && (c|ROOK) != _board[r_sq]; ++r_sq) {}
                break;
            default:
                return false;
                break;
            }
        }

        if ((c|ROOK) != _board[r_sq]) return false;
        set_castle (c, r_sq);
    }

    // 4. En-passant square. Ignore if no pawn capture is possible
    u08 file, rank;
    if (   (iss >> file && (file >= 'a' && file <= 'h'))
        && (iss >> rank && (rank == '3' || rank == '6'))
       )
    {
        if (   (WHITE == _active && '6' == rank)
            || (BLACK == _active && '3' == rank)
           )
        {
            auto ep_sq = to_square (file, rank);
            if (can_en_passant (ep_sq))
            {
                _psi->en_passant_sq = ep_sq;
            }
        }
    }

    // 5-6. 50-move clock and game-move count
    i32 clk50 = 0, g_move = 1;
    if (full)
    {
        iss >> skipws;
        iss >> clk50 >> g_move;
        // Rule 50 draw case
        //if (clk50 >100) return false;
        if (g_move <= 0) g_move = 1;
    }

    // Convert from game_move starting from 1 to game_ply starting from 0,
    // handle also common incorrect FEN with game_move = 0.
    _psi->clock50 = u08(SQ_NO != _psi->en_passant_sq ? 0 : clk50);
    _game_ply = std::max (2*(g_move - 1), 0) + (BLACK == _active);

    _psi->matl_key = Zob.compute_matl_key (*this);
    _psi->pawn_key = Zob.compute_pawn_key (*this);
    _psi->posi_key = Zob.compute_posi_key (*this);
    _psi->psq_score = compute_psq_score ();
    _psi->non_pawn_matl[WHITE] = compute_non_pawn_material (WHITE);
    _psi->non_pawn_matl[BLACK] = compute_non_pawn_material (BLACK);
    _psi->checkers = checkers (_active);
    _game_nodes   = 0;
    _chess960     = c960;
    _thread       = th;

    return true;
}

// compute_psq_score() computes the incremental scores for the middle
// game and the endgame. These functions are used to initialize the incremental
// scores when a new position is set up, and to verify that the scores are correctly
// updated by do_move and undo_move when the program is running in debug mode.
Score Position::compute_psq_score () const
{
    auto psq_score = SCORE_ZERO;
    auto occ = _types_bb[NONE];
    while (occ != U64(0))
    {
        auto s = pop_lsq (occ);
        auto p = _board[s];
        psq_score += PSQ[color (p)][ptype (p)][s];
    }
    return psq_score;
}

// compute_non_pawn_material() computes the total non-pawn middle
// game material value for the given side. Material values are updated
// incrementally during the search, this function is only used while
// initializing a new Position object.
Value Position::compute_non_pawn_material (Color c) const
{
    auto npm_value = VALUE_ZERO;
    for (auto pt = NIHT; pt <= QUEN; ++pt)
    {
        npm_value += PIECE_VALUE[MG][pt] * i32(_piece_count[c][pt]);
    }
    return npm_value;
}

#undef do_capture

#define do_capture() {                                                               \
    remove_piece (cap);                                                              \
    if (PAWN == cpt)                                                                 \
    {                                                                                \
        _psi->pawn_key ^= Zob._.piece_square[~_active][PAWN][cap];                   \
    }                                                                                \
    else                                                                             \
    {                                                                                \
        _psi->non_pawn_matl[~_active] -= PIECE_VALUE[MG][cpt];                       \
    }                                                                                \
    _psi->matl_key ^= Zob._.piece_square[~_active][cpt][_piece_count[~_active][cpt]];\
    key            ^= Zob._.piece_square[~_active][cpt][cap];                        \
    _psi->psq_score -= PSQ[~_active][cpt][cap];                                      \
    _psi->clock50 = 0;                                                               \
}
// do_move() do the move
void Position::do_move (Move m, StateInfo &nsi, bool gives_check)
{
    assert (_ok (m));
    assert (&nsi != _psi);

    Key key = _psi->posi_key ^ Zob._.act_side;
    // Copy some fields of old state to new StateInfo object except the ones
    // which are going to be recalculated from scratch anyway, 
    std::memcpy (&nsi, _psi, offsetof (StateInfo, last_move));

    // Switch state pointer to point to the new, ready to be updated, state.
    nsi.ptr = _psi;
    _psi    = &nsi;

    auto pasive = ~_active;

    auto org = org_sq (m);
    auto dst = dst_sq (m);
    auto mpt = ptype (_board[org]);

    assert (!empty (org) && _active == color (_board[org]) && NONE != mpt);
    assert ( empty (dst) ||  pasive == color (_board[dst]) || CASTLE == mtype (m));

    auto cap = dst;
    auto cpt = NONE;

    // Do move according to move type
    switch (mtype (m))
    {
    case NORMAL:
    {
        cpt = ptype (_board[cap]);

        assert (PAWN == promote (m) - NIHT && KING != cpt);
        
        if (NONE != cpt)
        {
            do_capture ();
        }
        else
        {
            _psi->clock50 = PAWN == mpt ? 0 : _psi->clock50 + 1;
        }

        move_piece (org, dst);

        if (PAWN == mpt)
        {
            _psi->pawn_key ^=
                 Zob._.piece_square[_active][PAWN][org]
                ^Zob._.piece_square[_active][PAWN][dst];
        }

        key ^=
             Zob._.piece_square[_active][mpt][org]
            ^Zob._.piece_square[_active][mpt][dst];

        _psi->psq_score +=
            -PSQ[_active][mpt][org]
            +PSQ[_active][mpt][dst];
    }
        break;

    case CASTLE:
    {
        assert (KING == mpt && ROOK == ptype (_board[dst]));

        Square rook_org, rook_dst;
        do_castling<true> (org, dst, rook_org, rook_dst);

        key ^=
             Zob._.piece_square[_active][KING][     org]
            ^Zob._.piece_square[_active][KING][     dst]
            ^Zob._.piece_square[_active][ROOK][rook_org]
            ^Zob._.piece_square[_active][ROOK][rook_dst];

        _psi->psq_score +=
            -PSQ[_active][KING][     org]
            +PSQ[_active][KING][     dst]
            -PSQ[_active][ROOK][rook_org]
            +PSQ[_active][ROOK][rook_dst];

        _psi->clock50++;
    }
        break;

    case ENPASSANT:
    {
        assert (PAWN == mpt && dst == _psi->en_passant_sq && empty (dst));
        assert (R_5 == rel_rank (_active, org) && R_6 == rel_rank (_active, dst));

        cap += pawn_push (pasive);
        assert (!empty (cap) && (pasive|PAWN) == _board[cap]);

        cpt = PAWN;
        do_capture ();
        _board[cap] = EMPTY; // Not done by remove_piece()
        
        move_piece (org, dst);

        _psi->pawn_key ^=
             Zob._.piece_square[_active][PAWN][org]
            ^Zob._.piece_square[_active][PAWN][dst];

        key ^=
             Zob._.piece_square[_active][PAWN][org]
            ^Zob._.piece_square[_active][PAWN][dst];

        _psi->psq_score +=
            -PSQ[_active][PAWN][org]
            +PSQ[_active][PAWN][dst];
    }
        break;

    case PROMOTE:
    {
        cpt = ptype (_board[cap]);
        assert (PAWN == mpt && R_7 == rel_rank (_active, org) && R_8 == rel_rank (_active, dst) && PAWN != cpt && KING != cpt);

        if (NONE != cpt)
        {
            do_capture ();
        }
        else
        {
            _psi->clock50 = 0;
        }

        auto ppt = promote (m);
        assert (NIHT <= ppt && ppt <= QUEN);
        // Replace the PAWN with the Promoted piece
        remove_piece (org);
        _board[org] = EMPTY; // Not done by remove_piece()
        place_piece (dst, _active, ppt);

        _psi->matl_key ^=
             Zob._.piece_square[_active][PAWN][_piece_count[_active][PAWN]]
            ^Zob._.piece_square[_active][ppt ][_piece_count[_active][ppt] - 1];

        _psi->pawn_key ^=
             Zob._.piece_square[_active][PAWN][org];

        key ^=
             Zob._.piece_square[_active][PAWN][org]
            ^Zob._.piece_square[_active][ppt ][dst];

        _psi->psq_score +=
            -PSQ[_active][PAWN][org]
            +PSQ[_active][ppt ][dst];

        _psi->non_pawn_matl[_active] += PIECE_VALUE[MG][ppt];
    }
        break;

    default:
        assert (false);
        break;
    }

    u08 cr = _psi->castle_rights & (_castle_mask[org]|_castle_mask[dst]);
    if (cr != 0)
    {
        Bitboard b = cr;
        _psi->castle_rights &= ~cr;
        while (b != U64(0))
        {
            key ^= Zob._.castle_right[0][pop_lsq (b)];
        }
    }

    // Calculate checkers bitboard (if move is check)
    _psi->checkers = gives_check ? attackers_to (_piece_square[pasive][KING][0], _active) : U64(0);

    _active = pasive;

    if (SQ_NO != _psi->en_passant_sq)
    {
        key ^= Zob._.en_passant[_file (_psi->en_passant_sq)];
        _psi->en_passant_sq = SQ_NO;
    }
    if (PAWN == mpt)
    {
        if (16 == (u08(dst) ^ u08(org)))
        {
            auto ep_sq = org + (dst - org)/2;
            if (can_en_passant (ep_sq))
            {
                _psi->en_passant_sq = ep_sq;
                key ^= Zob._.en_passant[_file (ep_sq)];
            }
        }
    }
    // Update state information
    _psi->posi_key     = key;
    _psi->last_move    = m;
    _psi->capture_type = cpt;
    ++_psi->null_ply;
    ++_game_ply;
    ++_game_nodes;

    assert (ok ());
}
#undef do_capture
// do_move() do the move (CAN)
void Position::do_move (string &can, StateInfo &nsi)
{
    auto m = move_from_can (can, *this);
    if (MOVE_NONE != m) do_move (m, nsi, gives_check (m, CheckInfo (*this)));
}
// undo_move() undo the last move
void Position::undo_move ()
{
    assert (_psi->ptr != nullptr);
    auto m = _psi->last_move;
    assert (_ok (m));

    auto org = org_sq (m);
    auto dst = dst_sq (m);

    _active = ~_active;

    assert (empty (org) || mtype (m) == CASTLE);

    assert (KING != _psi->capture_type);

    auto cap = dst;

    // Undo move according to move type
    switch (mtype (m))
    {
    case NORMAL:
    {
        move_piece (dst, org);
    }
        break;

    case CASTLE:
    {
        Square rook_org, rook_dst;
        do_castling<false> (org, dst, rook_org, rook_dst);
        //cpt  = NONE;
    }
        break;

    case ENPASSANT:
    {
        assert (PAWN == ptype (_board[dst]));
        assert (PAWN == _psi->capture_type);
        assert (R_5 == rel_rank (_active, org));
        assert (R_6 == rel_rank (_active, dst));
        assert (dst == _psi->ptr->en_passant_sq);
        cap -= pawn_push (_active);
        assert (empty (cap));
        move_piece (dst, org);
    }
        break;

    case PROMOTE:
    {
        assert (promote (m) == ptype (_board[dst]));
        assert (R_8 == rel_rank (_active, dst));
        assert (NIHT <= promote (m) && promote (m) <= QUEN);
        remove_piece (dst);
        _board[dst] = EMPTY; // Not done by remove_piece()
        place_piece (org, _active, PAWN);
    }
        break;

    default:
        assert (false);
        break;
    }

    if (NONE != _psi->capture_type)
    {
        place_piece (cap, ~_active, _psi->capture_type);
    }

    --_game_ply;
    _psi = _psi->ptr;

    assert (ok ());
}
// do_null_move() do the null-move
void Position::do_null_move (StateInfo &nsi)
{
    assert (&nsi != _psi);
    assert (_psi->checkers == U64(0));

    // Full copy here
    std::memcpy (&nsi, _psi, sizeof (nsi));

    // Switch our state pointer to point to the new, ready to be updated, state.
    nsi.ptr = _psi;
    _psi    = &nsi;

    if (SQ_NO != _psi->en_passant_sq)
    {
        _psi->posi_key ^= Zob._.en_passant[_file (_psi->en_passant_sq)];
        _psi->en_passant_sq = SQ_NO;
    }
    _psi->posi_key ^= Zob._.act_side;
    _psi->clock50++;
    _psi->null_ply = 0;

    _active = ~_active;

    assert (ok ());
}
// undo_null_move() undo the last null-move
void Position::undo_null_move ()
{
    assert (_psi->ptr != nullptr);
    assert (_psi->checkers == U64(0));

    _active = ~_active;
    _psi    = _psi->ptr;

    assert (ok ());
}

// flip() flips position with the white and black sides reversed.
// This is only useful for debugging especially for finding evaluation symmetry bugs.
void Position::flip ()
{
    string flip_fen, token;
    stringstream ss (fen ());
    // 1. Piece placement
    for (auto rank = R_8; rank >= R_1; --rank)
    {
        getline (ss, token, rank > R_1 ? '/' : ' ');
        flip_fen.insert (0, token + (white_spaces (flip_fen) ? " " : "/"));
    }
    // 2. Active color
    ss >> token;
    flip_fen += (token == "w" ? "B" : "W"); // Will be lowercased later
    flip_fen += " ";
    // 3. Castling availability
    ss >> token;
    flip_fen += token + " ";
    transform (flip_fen.begin (), flip_fen.end (), flip_fen.begin (),
        // Toggle case
        [](char c) { return char (islower (c) ? toupper (c) : tolower (c)); });

    // 4. En-passant square
    ss >> token;
    flip_fen += (token == "-" ? token : token.replace (1, 1, token[1] == '3' ? "6" : token[1] == '6' ? "3" : "-"));
    // 5-6. Half and full moves
    getline (ss, token);
    flip_fen += token;

    setup (flip_fen, _thread, _chess960);

    assert (ok ());
}

string Position::fen (bool c960, bool full) const
{
    ostringstream oss;

    for (auto r = R_8; r >= R_1; --r)
    {
        for (auto f = F_A; f <= F_H; ++f)
        {
            auto s = f|r;
            auto empty_count = 0;
            while (F_H >= f && empty (s))
            {
                ++empty_count;
                ++f;
                ++s;
            }
            if (empty_count != 0) oss << empty_count;
            if (F_H >= f) oss << PIECE_CHAR[_board[s]];
        }

        if (R_1 < r) oss << "/";
    }

    oss << " " << COLOR_CHAR[_active] << " ";

    if (can_castle (CR_A))
    {
        if (_chess960 || c960)
        {
            if (can_castle (CR_W))
            {
                if (can_castle (CR_WK)) oss << to_char (_file (_castle_rook[Castling<WHITE, CS_KING>::Right]), false);
                if (can_castle (CR_WQ)) oss << to_char (_file (_castle_rook[Castling<WHITE, CS_QUEN>::Right]), false);
            }
            if (can_castle (CR_B))
            {
                if (can_castle (CR_BK)) oss << to_char (_file (_castle_rook[Castling<BLACK, CS_KING>::Right]), true);
                if (can_castle (CR_BQ)) oss << to_char (_file (_castle_rook[Castling<BLACK, CS_QUEN>::Right]), true);
            }
        }
        else
        {
            if (can_castle (CR_W))
            {
                if (can_castle (CR_WK)) oss << "K";
                if (can_castle (CR_WQ)) oss << "Q";
            }
            if (can_castle (CR_B))
            {
                if (can_castle (CR_BK)) oss << "k";
                if (can_castle (CR_BQ)) oss << "q";
            }
        }
    }
    else
    {
        oss << "-";
    }

    oss << " " << ((SQ_NO == _psi->en_passant_sq) ? "-" : to_string (_psi->en_passant_sq)) << " ";

    if (full) oss << i16(_psi->clock50) << " " << game_move ();

    return oss.str ();
}

// string() returns an ASCII representation of the position to be
// printed to the standard output
Position::operator string () const
{
    const string EDGE  = " +---+---+---+---+---+---+---+---+\n";
    const string ROW_1 = "| . |   | . |   | . |   | . |   |\n" + EDGE;
    const string ROW_2 = "|   | . |   | . |   | . |   | . |\n" + EDGE;

    auto board = EDGE;

    for (auto r = R_8; r >= R_1; --r)
    {
        board += to_char (r) + ((r % 2) != 0 ? ROW_1 : ROW_2);
    }
    for (auto f = F_A; f <= F_H; ++f)
    {
        board += "   ";
        board += to_char (f, false);
    }

    auto occ = _types_bb[NONE];
    while (occ != U64(0))
    {
        auto s = pop_lsq (occ);
        board[3 + i32((ROW_1.length () + 1) * (7.5 - _rank (s)) + 4 * _file (s))] = PIECE_CHAR[_board[s]];
    }

    ostringstream oss;

    oss << board << "\n\n";

    oss << "FEN: " << fen () << "\n"
        << "Key: " << setfill ('0') << hex << uppercase << setw (16)
        << _psi->posi_key << nouppercase << dec << setfill (' ') << "\n";

    oss << "Checkers: ";
    auto chkrs = _psi->checkers;
    if (chkrs != U64(0))
    {
        while (chkrs != U64(0))
        {
            auto chk_sq = pop_lsq (chkrs);
            oss << PIECE_CHAR[ptype (_board[chk_sq])] << to_string (chk_sq) << " ";
        }
    }
    else
    {
        oss << "<none>";
    }

    return oss.str ();
}
