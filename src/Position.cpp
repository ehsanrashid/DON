#include "Position.h"

#include <cstring>
#include <iomanip>
#include <sstream>

#include "MoveGenerator.h"
#include "Transposition.h"
#include "Thread.h"
#include "Notation.h"
#include "UCI.h"

using namespace std;
using namespace BitBoard;
using namespace Transposition;
using namespace MoveGen;
using namespace Threads;
using namespace Notation;

const Value PIECE_VALUE[PHASE_NO][TOTL] =
{
    { VALUE_MG_PAWN, VALUE_MG_NIHT, VALUE_MG_BSHP, VALUE_MG_ROOK, VALUE_MG_QUEN, VALUE_ZERO, VALUE_ZERO },
    { VALUE_EG_PAWN, VALUE_EG_NIHT, VALUE_EG_BSHP, VALUE_EG_ROOK, VALUE_EG_QUEN, VALUE_ZERO, VALUE_ZERO }
};

// PSQT[Color][PieceType][Square] contains Color-PieceType-Square scores.
Score PSQT[CLR_NO][NONE][SQ_NO];

const string PIECE_CHAR ("PNBRQK  pnbrqk");
const string COLOR_CHAR ("wb-");
const string STARTUP_FEN ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

bool _ok (const string &fen, bool c960, bool full)
{
    if (white_spaces (fen)) return false;
    Position pos (fen, NULL, c960, full);
    return pos.ok ();
}

namespace {

    // do_move() copy current state info up to 'posi_key' excluded to the new one.
    // calculate the bits needed to be copied.
    const u08 STATEINFO_COPY_SIZE = offsetof (StateInfo, last_move);

#define S(mg, eg) mk_score (mg, eg)
    // SQT[PieceType][Square] contains PieceType-Square scores.
    // For each piece type on a given square a (midgame, endgame) score pair is assigned.
    // SQT table is defined for white side +ve,
    // the table for black side is symmetric -ve.
    const Score SQT[NONE][SQ_NO] =
    {
        // Pawn
        {
            S(  0, 0), S( 0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0), S(  0, 0),
            S(-20, 0), S( 0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0), S(-20, 0),
            S(-20, 0), S( 0, 0), S(+10, 0), S(+20, 0), S(+20, 0), S(+10, 0), S( 0, 0), S(-20, 0),
            S(-20, 0), S( 0, 0), S(+20, 0), S(+40, 0), S(+40, 0), S(+20, 0), S( 0, 0), S(-20, 0),
            S(-20, 0), S( 0, 0), S(+10, 0), S(+20, 0), S(+20, 0), S(+10, 0), S( 0, 0), S(-20, 0),
            S(-20, 0), S( 0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0), S(-20, 0),
            S(-20, 0), S( 0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0), S(-20, 0),
            S(  0, 0), S( 0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S(  0, 0), S( 0, 0), S(  0, 0)
        },
        // Knight
        {
            S(-144,-98), S(-109,-83), S(-85,-51), S(-73,-16), S(-73,-16), S(-85,-51), S(-109,-83), S(-144,-98),
            S(- 88,-68), S(- 43,-53), S(-19,-21), S(- 7,+14), S(- 7,+14), S(-19,-21), S(- 43,-53), S(- 88,-68),
            S(- 69,-53), S(- 24,-38), S(+ 0,- 6), S(+12,+29), S(+12,+29), S(+ 0,- 6), S(- 24,-38), S(- 69,-53),
            S(- 28,-42), S(+ 17,-27), S(+41,+ 5), S(+53,+40), S(+53,+40), S(+41,+ 5), S(+ 17,-27), S(- 28,-42),
            S(- 30,-42), S(+ 15,-27), S(+39,+ 5), S(+51,+40), S(+51,+40), S(+39,+ 5), S(+ 15,-27), S(- 30,-42),
            S(- 10,-53), S(+ 35,-38), S(+59,- 6), S(+71,+29), S(+71,+29), S(+59,- 6), S(+ 35,-38), S(- 10,-53),
            S(- 64,-68), S(- 19,-53), S(+ 5,-21), S(+17,+14), S(+17,+14), S(+ 5,-21), S(- 19,-53), S(- 64,-68),
            S(-200,-98), S(- 65,-83), S(-41,-51), S(-29,-16), S(-29,-16), S(-41,-51), S(- 65,-83), S(-200,-98)
        },
        // Bishop
        {
            S(-54,-65), S(-27,-42), S(-34,-44), S(-43,-26), S(-43,-26), S(-34,-44), S(-27,-42), S(-54,-65),
            S(-29,-43), S(+ 8,-20), S(+ 1,-22), S(- 8,- 4), S(- 8,- 4), S(+ 1,-22), S(+ 8,-20), S(-29,-43),
            S(-20,-33), S(+17,-10), S(+10,-12), S(+ 1,+ 6), S(+ 1,+ 6), S(+10,-12), S(+17,-10), S(-20,-33),
            S(-19,-35), S(+18,-12), S(+11,-14), S(+ 2,+ 4), S(+ 2,+ 4), S(+11,-14), S(+18,-12), S(-19,-35),
            S(-22,-35), S(+15,-12), S(+ 8,-14), S(- 1,+ 4), S(- 1,+ 4), S(+ 8,-14), S(+15,-12), S(-22,-35),
            S(-28,-33), S(+ 9,-10), S(+ 2,-12), S(- 7,+ 6), S(- 7,+ 6), S(+ 2,-12), S(+ 9,-10), S(-28,-33),
            S(-32,-43), S(+ 5,-20), S(- 2,-22), S(-11,- 4), S(-11,- 4), S(- 2,-22), S(+ 5,-20), S(-32,-43),
            S(-49,-65), S(-22,-42), S(-29,-44), S(-38,-26), S(-38,-26), S(-29,-44), S(-22,-42), S(-49,-65)
        },
        // Rook
        {
            S(-22,+ 3), S(-17,+ 3), S(-12,+ 3), S(- 8,+ 3), S(- 8,+ 3), S(-12,+ 3), S(-17,+ 3), S(-22,+ 3),
            S(-22,+ 3), S(- 7,+ 3), S(- 2,+ 3), S(+ 2,+ 3), S(+ 2,+ 3), S(- 2,+ 3), S(- 7,+ 3), S(-22,+ 3),
            S(-22,+ 3), S(- 7,+ 3), S(- 2,+ 3), S(+ 2,+ 3), S(+ 2,+ 3), S(- 2,+ 3), S(- 7,+ 3), S(-22,+ 3),
            S(-22,+ 3), S(- 7,+ 3), S(- 2,+ 3), S(+ 2,+ 3), S(+ 2,+ 3), S(- 2,+ 3), S(- 7,+ 3), S(-22,+ 3),
            S(-22,+ 3), S(- 7,+ 3), S(- 2,+ 3), S(+ 2,+ 3), S(+ 2,+ 3), S(- 2,+ 3), S(- 7,+ 3), S(-22,+ 3),
            S(-22,+ 3), S(- 7,+ 3), S(- 2,+ 3), S(+ 2,+ 3), S(+ 2,+ 3), S(- 2,+ 3), S(- 7,+ 3), S(-22,+ 3),
            S(-11,+ 3), S(+ 4,+ 3), S(+ 9,+ 3), S(+13,+ 3), S(+13,+ 3), S(+ 9,+ 3), S(+ 4,+ 3), S(-11,+ 3),
            S(-22,+ 3), S(-17,+ 3), S(-12,+ 3), S(- 8,+ 3), S(- 8,+ 3), S(-12,+ 3), S(-17,+ 3), S(-22,+ 3)
        },
        // Queen
        {
            S(- 2,-80), S(- 2,-54), S(- 2,-42), S(- 2,-30), S(- 2,-30), S(- 2,-42), S(- 2,-54), S(- 2,-80),
            S(- 2,-54), S(+ 8,-30), S(+ 8,-18), S(+ 8,- 6), S(+ 8,- 6), S(+ 8,-18), S(+ 8,-30), S(- 2,-54),
            S(- 2,-42), S(+ 8,-18), S(+ 8,- 6), S(+ 8,+ 6), S(+ 8,+ 6), S(+ 8,- 6), S(+ 8,-18), S(- 2,-42),
            S(- 2,-30), S(+ 8,- 6), S(+ 8,+ 6), S(+ 8,+18), S(+ 8,+18), S(+ 8,+ 6), S(+ 8,- 6), S(- 2,-30),
            S(- 2,-30), S(+ 8,- 6), S(+ 8,+ 6), S(+ 8,+18), S(+ 8,+18), S(+ 8,+ 6), S(+ 8,- 6), S(- 2,-30),
            S(- 2,-42), S(+ 8,-18), S(+ 8,- 6), S(+ 8,+ 6), S(+ 8,+ 6), S(+ 8,- 6), S(+ 8,-18), S(- 2,-42),
            S(- 2,-54), S(+ 8,-30), S(+ 8,-18), S(+ 8,- 6), S(+ 8,- 6), S(+ 8,-18), S(+ 8,-30), S(- 2,-54),
            S(- 2,-80), S(- 2,-54), S(- 2,-42), S(- 2,-30), S(- 2,-30), S(- 2,-42), S(- 2,-54), S(- 2,-80)
        },
        // King
        {
            S(+298,+ 27), S(+332,+ 81), S(+273,+108), S(+225,+116), S(+225,+116), S(+273,+108), S(+332,+ 81), S(+298,+ 27),
            S(+287,+ 74), S(+321,+128), S(+262,+155), S(+214,+163), S(+214,+163), S(+262,+155), S(+321,+128), S(+287,+ 74),
            S(+224,+111), S(+258,+165), S(+199,+192), S(+151,+200), S(+151,+200), S(+199,+192), S(+258,+165), S(+224,+111),
            S(+196,+135), S(+230,+189), S(+171,+216), S(+123,+224), S(+123,+224), S(+171,+216), S(+230,+189), S(+196,+135),
            S(+173,+135), S(+207,+189), S(+148,+216), S(+100,+224), S(+100,+224), S(+148,+216), S(+207,+189), S(+173,+135),
            S(+146,+111), S(+180,+165), S(+121,+192), S(+ 73,+200), S(+ 73,+200), S(+121,+192), S(+180,+165), S(+146,+111),
            S(+119,+ 74), S(+153,+128), S(+ 94,+155), S(+ 46,+163), S(+ 46,+163), S(+ 94,+155), S(+153,+128), S(+119,+ 74),
            S(+ 98,+ 27), S(+132,+ 81), S(+ 73,+108), S(+ 25,+116), S(+ 25,+116), S(+ 73,+108), S(+132,+ 81), S(+ 98,+ 27)
        }
    };
#undef S

} // namespace

u08 Position::FiftyMoveDist = 100;

void Position::initialize ()
{
    for (i08 pt = PAWN; pt <= KING; ++pt)
    {
        Score score = mk_score (PIECE_VALUE[MG][pt], PIECE_VALUE[EG][pt]);

        for (i08 s = SQ_A1; s <= SQ_H8; ++s)
        {
            Score psq_score = score + SQT[pt][s];
            PSQT[WHITE][pt][ Square(s)] = +psq_score;
            PSQT[BLACK][pt][~Square(s)] = -psq_score;
        }
    }
}

// operator= (pos), copy the 'pos'.
// The new born Position object should not depend on any external data
// so that why detach the state info pointer from the source one.
Position& Position::operator= (const Position &pos)
{
    memcpy (this, &pos, sizeof (*this));

    _sb = *_si;
    _si = &_sb;
    _game_nodes = 0;

    return *this;
}

// Draw by: Material, 50 Move Rule, Threefold repetition, [Stalemate].
// It does not detect stalemates, this must be done by the search.
bool Position::draw () const
{
    // Draw by 50 moves Rule?
    // Not in check or in check have legal moves 
    if (  _si->clock50 >= FiftyMoveDist
       && (_si->checkers == U64(0) || MoveList<LEGAL> (*this).size () != 0)
       )
    {
        return true;
    }
    // Draw by Threefold Repetition?
    const StateInfo *psi = _si;
    //u08 cnt = 1;
    for (u08 ply = std::min (_si->clock50, _si->null_ply); ply >= 2; ply -= 2)
    {
        psi = psi->ptr->ptr;
        if (psi->posi_key == _si->posi_key)
        {
            //if (++cnt >= 3)
            return true;
        }
    }

    /*
    // Draw by Material?
    if (  _types_bb[PAWN] == U64(0)
       && _si->non_pawn_matl[WHITE] + _si->non_pawn_matl[BLACK] <= VALUE_MG_BSHP
       )
    {
        return true;
    }
    */
    /*
    // Draw by Stalemate?
    if (  _si->checkers == U64(0)
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
    StateInfo *si = _si;
    while (si != NULL)
    {
        u08 ply = min (si->clock50, si->null_ply);
        if (4 > ply) return false;
        StateInfo *psi = si->ptr->ptr;
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
bool Position::ok (i08 *step) const
{
    // What features of the position should be verified?
    const bool test_all = true;

    const bool test_king_count    = test_all || false;
    const bool test_piece_count   = test_all || false;
    const bool test_bitboards     = test_all || false;
    const bool test_piece_list    = test_all || false;
    const bool test_king_capture  = test_all || false;
    const bool test_castle_rights = test_all || false;
    const bool test_en_passant    = test_all || false;
    const bool test_matl_key      = test_all || false;
    const bool test_pawn_key      = test_all || false;
    const bool test_posi_key      = test_all || false;
    const bool test_inc_eval      = test_all || false;
    const bool test_np_material   = test_all || false;

    if (step) *step = 1;
    // step 1
    if (  (WHITE != _active && BLACK != _active)
       || (W_KING != _board[_piece_list[WHITE][KING][0]])
       || (B_KING != _board[_piece_list[BLACK][KING][0]])
       || (_si->clock50 > 100)
       )
    {
        return false;
    }
    // step 2
    if (step && ++(*step), test_king_count)
    {
        for (i08 c = WHITE; c <= BLACK; ++c)
        {
            if (1 != std::count (_board, _board + SQ_NO, Color(c)|KING))
            {
                return false;
            }
            if (_piece_count[c][KING] != pop_count<FULL> (_color_bb[c]&_types_bb[KING]))
            {
                return false;
            }
        }
    }
    // step 3
    if (step && ++(*step), test_piece_count)
    {
        if (  pop_count<FULL> (_types_bb[NONE]) > 32
           || count<NONE> () > 32
           || count<NONE> () != pop_count<FULL> (_types_bb[NONE])
           )
        {
            return false;
        }

        for (i08 c = WHITE; c <= BLACK; ++c)
        {
            for (i08 pt = PAWN; pt <= KING; ++pt)
            {
                if (_piece_count[c][pt] != pop_count<MAX15> (_color_bb[c]&_types_bb[pt]))
                {
                    return false;
                }
            }
        }
    }
    // step 4
    if (step && ++(*step), test_bitboards)
    {
        // The intersection of the white and black pieces must be empty
        if (_color_bb[WHITE]&_color_bb[BLACK])
        {
            return false;
        }
        // The intersection of separate piece type must be empty
        for (i08 pt1 = PAWN; pt1 <= KING; ++pt1)
        {
            for (i08 pt2 = PAWN; pt2 <= KING; ++pt2)
            {
                if (pt1 != pt2 && (_types_bb[pt1]&_types_bb[pt2]))
                {
                    return false;
                }
            }
        }

        // The union of the white and black pieces must be equal to occupied squares
        if (  (_color_bb[WHITE]|_color_bb[BLACK]) != _types_bb[NONE]
           || (_color_bb[WHITE]^_color_bb[BLACK]) != _types_bb[NONE]
           )
        {
            return false;
        }

        // The union of separate piece type must be equal to occupied squares
        if (  (_types_bb[PAWN]|_types_bb[NIHT]|_types_bb[BSHP]|_types_bb[ROOK]|_types_bb[QUEN]|_types_bb[KING]) != _types_bb[NONE]
           || (_types_bb[PAWN]^_types_bb[NIHT]^_types_bb[BSHP]^_types_bb[ROOK]^_types_bb[QUEN]^_types_bb[KING]) != _types_bb[NONE]
           )
        {
            return false;
        }

        // PAWN rank should not be 1/8
        if ((R1_bb|R8_bb) & _types_bb[PAWN])
        {
            return false;
        }

        for (i08 c = WHITE; c <= BLACK; ++c)
        {
            Bitboard colors = _color_bb[c];

            if (pop_count<FULL> (colors) > 16) // Too many Piece of color
            {
                return false;
            }
            // check if the number of Pawns plus the number of
            // extra Queens, Rooks, Bishops, Knights exceeds 8
            // (which can result only by promotion)
            if (  (   _piece_count[c][PAWN]
               + max (_piece_count[c][NIHT] - 2, 0)
               + max (_piece_count[c][BSHP] - 2, 0)
               + max (_piece_count[c][ROOK] - 2, 0)
               + max (_piece_count[c][QUEN] - 1, 0)) > 8
               )
            {
                return false; // Too many Promoted Piece of color
            }

            if (_piece_count[c][BSHP] > 1)
            {
                Bitboard bishops = colors & _types_bb[BSHP];
                u08 bishop_count[CLR_NO] =
                {
                    u08(pop_count<MAX15> (LIHT_bb & bishops)),
                    u08(pop_count<MAX15> (DARK_bb & bishops)),
                };

                if (  (   _piece_count[c][PAWN]
                   + max (bishop_count[WHITE] - 1, 0)
                   + max (bishop_count[BLACK] - 1, 0)) > 8
                   )
                {
                    return false; // Too many Promoted BISHOP of color
                }
            }

            // There should be one and only one KING of color
            Bitboard kings = colors & _types_bb[KING];
            if (kings == U64(0) || more_than_one (kings))
            {
                return false;
            }
        }
    }
    // step 5
    if (step && ++(*step), test_piece_list)
    {
        for (i08 c = WHITE; c <= BLACK; ++c)
        {
            for (i08 pt = PAWN; pt <= KING; ++pt)
            {
                for (i08 i = 0; i < _piece_count[c][pt]; ++i)
                {
                    if (  !_ok  (_piece_list[c][pt][i])
                       || _board[_piece_list[c][pt][i]] != (Color(c) | PieceT(pt))
                       || _piece_index[_piece_list[c][pt][i]] != i
                       )
                    {
                        return false;
                    }
                }
            }
        }
        for (i08 s = SQ_A1; s <= SQ_H8; ++s)
        {
            if (_piece_index[s] >= 16)
            {
                return false;
            }
        }
    }
    // step 6
    if (step && ++(*step), test_king_capture)
    {
        if (  attackers_to (_piece_list[~_active][KING][0], _active)
           || pop_count<MAX15> (_si->checkers) > 2
           )
        {
            return false;
        }
    }
    // step 7
    if (step && ++(*step), test_castle_rights)
    {
        for (i08 c = WHITE; c <= BLACK; ++c)
        {
            for (i08 cs = CS_K; cs <= CS_Q; ++cs)
            {
                CRight cr = mk_castle_right (Color(c), CSide (cs));

                if (!can_castle (cr)) continue;

                if (  (_castle_mask[_piece_list[c][KING][0]] & cr) != cr
                   || _board[_castle_rook[cr]] != (Color(c) | ROOK)
                   || _castle_mask[_castle_rook[cr]] != cr
                   )
                {
                    return false;
                }
            }
        }
    }
    // step 8
    if (step && ++(*step), test_en_passant)
    {
        Square ep_sq = _si->en_passant_sq;
        if (SQ_NO != ep_sq)
        {
            if (_si->clock50 != 0) return false;
            if (R_6 != rel_rank (_active, ep_sq)) return false;
            if (!can_en_passant (ep_sq)) return false;
        }
    }
    // step 9
    if (step && ++(*step), test_matl_key)
    {
        if (Zob.compute_matl_key (*this) != _si->matl_key) return false;
    }
    // step 10
    if (step && ++(*step), test_pawn_key)
    {
        if (Zob.compute_pawn_key (*this) != _si->pawn_key) return false;
    }
    // step 11
    if (step && ++(*step), test_posi_key)
    {
        if (Zob.compute_posi_key (*this) != _si->posi_key) return false;
    }
    // step 12
    if (step && ++(*step), test_inc_eval)
    {
        if (compute_psq_score () != _si->psq_score) return false;
    }
    // step 13
    if (step && ++(*step), test_np_material)
    {
        if (  compute_non_pawn_material (WHITE) != _si->non_pawn_matl[WHITE]
           || compute_non_pawn_material (BLACK) != _si->non_pawn_matl[BLACK]
           )
        {
            return false;
        }
    }

    return true;
}

// least_valuable_attacker() is a helper function used by see()
// to locate the least valuable attacker for the side to move,
// remove the attacker just found from the bitboards and
// scan for new X-ray attacks behind it.
template<PieceT PT>
PieceT Position::least_valuable_attacker (Square dst, Bitboard stm_attackers, Bitboard &occupied, Bitboard &attackers) const
{
    Bitboard bb = stm_attackers & _types_bb[PT];
    if (bb != U64(0))
    {
        occupied ^= (bb & ~(bb - 1));

        switch (PT)
        {
        case PAWN:
        case BSHP:
            attackers |= (attacks_bb<BSHP> (dst, occupied) & (_types_bb[BSHP]|_types_bb[QUEN]));
        break;
        case ROOK:
            attackers |= (attacks_bb<ROOK> (dst, occupied) & (_types_bb[ROOK]|_types_bb[QUEN]));
        break;
        case QUEN:
            attackers |= (attacks_bb<BSHP> (dst, occupied) & (_types_bb[BSHP]|_types_bb[QUEN]))
                      |  (attacks_bb<ROOK> (dst, occupied) & (_types_bb[ROOK]|_types_bb[QUEN]));
        break;
        default:
        break;
        }
        attackers &= occupied; // After X-ray that may add already processed pieces

        return PT;
    }

    return least_valuable_attacker<PieceT(PT+1)> (dst, stm_attackers, occupied, attackers);
}
template<>
PieceT Position::least_valuable_attacker<KING> (Square, Bitboard, Bitboard&, Bitboard&) const
{
    return KING; // No need to update bitboards, it is the last cycle
}

// game_phase() calculates the phase interpolating total
// non-pawn material between endgame and midgame limits.
Phase Position::game_phase () const
{
    Value npm = max (VALUE_ENDGAME, min (_si->non_pawn_matl[WHITE] + _si->non_pawn_matl[BLACK], VALUE_MIDGAME));
    return Phase ((npm - VALUE_ENDGAME) * i32(PHASE_MIDGAME) / i32(VALUE_MIDGAME - VALUE_ENDGAME));
}

// see() is a Static Exchange Evaluator (SEE):
// It tries to estimate the material gain or loss resulting from a move.
Value Position::see      (Move m) const
{
    assert (_ok (m));

    Square org = org_sq (m)
        ,  dst = dst_sq (m);

    // Side to move
    Color stm = color (_board[org]);

    // Gain list
    Value swap_list[32];
    i08   depth = 1;
    Bitboard occupied = _types_bb[NONE] - org;

    switch (mtype (m))
    {
    case CASTLE:
    {
        // Castle moves are implemented as king capturing the rook so cannot be
        // handled correctly. Simply return 0 that is always the correct value
        // unless in the rare case the rook ends up under attack.
        return VALUE_ZERO;
    }
    break;

    case ENPASSANT:
    {
        // Remove the captured pawn
        occupied -= (dst - pawn_push (stm));
        //occupied += dst;
        swap_list[0] = PIECE_VALUE[MG][PAWN];
    }
    break;

    default:
    {
        swap_list[0] = PIECE_VALUE[MG][ptype (_board[dst])];
    }
    break;
    }

    // Find all enemy attackers to the destination square, with the moving piece
    // removed, but possibly an X-ray attacker added behind it.
    Bitboard attackers = attackers_to (dst, occupied) & occupied;

    // If the opponent has no attackers are finished
    stm = ~stm;
    Bitboard stm_attackers = attackers & _color_bb[stm];
    if (stm_attackers != U64(0))
    {
        // The destination square is defended, which makes things rather more
        // difficult to compute. Proceed by building up a "swap list" containing
        // the material gain or loss at each stop in a sequence of captures to the
        // destination square, where the sides alternately capture, and always
        // capture with the least valuable piece. After each capture, look for
        // new X-ray attacks from behind the capturing piece.
        do
        {
            assert (depth < 32);

            // Add the new entry to the swap list
            swap_list[depth] = PIECE_VALUE[MG][ptype (_board[org])] - swap_list[depth - 1];

            // Locate and remove the next least valuable attacker
            PieceT captured = least_valuable_attacker<PAWN> (dst, stm_attackers, occupied, attackers);

            // Stop before processing a king capture
            if (KING == captured)
            {
                if (stm_attackers == attackers) ++depth;
                break;
            }

            stm = ~stm;
            stm_attackers = attackers & _color_bb[stm];

            ++depth;
        } while (stm_attackers != U64(0));

        // Having built the swap list, negamax through it to find the best
        // achievable score from the point of view of the side to move.
        while (--depth > 0)
        {
            swap_list[depth - 1] = min (-swap_list[depth], swap_list[depth - 1]);
        }
    }

    return swap_list[0];
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
    Square ksq = _piece_list[king_c][KING][0];
    // Pinners are sliders that give check when a pinned piece is removed
    // Only one real pinner exist other are fake pinner
    Bitboard pinners =
        ( (PIECE_ATTACKS[ROOK][ksq] & (_types_bb[QUEN]|_types_bb[ROOK]))
        | (PIECE_ATTACKS[BSHP][ksq] & (_types_bb[QUEN]|_types_bb[BSHP]))
        ) &  _color_bb[~king_c];

    Bitboard chk_blockers = U64(0);
    while (pinners != U64(0))
    {
        Bitboard blocker = BETWEEN_SQRS_bb[ksq][pop_lsq (pinners)] & _types_bb[NONE];
        if (blocker && !more_than_one (blocker))
        {
            chk_blockers |= (blocker & _color_bb[piece_c]); // Defending piece
        }
    }

    return chk_blockers;
}

// pseudo_legal() tests whether a random move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal (Move m) const
{
    if (!_ok (m)) return false;

    Square org = org_sq (m)
        ,  dst = dst_sq (m);

    Rank r_org = rel_rank (_active, org);
    Rank r_dst = rel_rank (_active, dst);
    
    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if (NONE == ptype (_board[org]) || _active != color (_board[org])) return false;

    PieceT ct = NONE;

    Square cap = dst;

    switch (mtype (m))
    {
    case NORMAL:
    {
        // Is not a promotion, so promotion piece must be empty
        if (PAWN != promote (m) - NIHT)
        {
            return false;
        }
        ct = ptype (_board[cap]);
    }
    break;

    case CASTLE:
    {
        // Check whether the destination square is attacked by the opponent.
        // Castling moves are checked for legality during move generation.
        if (!(  KING == ptype (_board[org])
             && R_1 == r_org
             && R_1 == r_dst
             && _board[dst] == (_active|ROOK)
             && _si->checkers == U64(0)
             && _si->castle_rights & mk_castle_right (_active)
             )
           )
            return false;

        if (castle_impeded (mk_castle_right (_active, (dst > org) ? CS_K : CS_Q))) return false;

        // Castle is always encoded as "King captures friendly Rook"
        assert (dst == castle_rook (mk_castle_right (_active, (dst > org) ? CS_K : CS_Q)));
        dst = rel_sq (_active, (dst > org) ? SQ_G1 : SQ_C1);
        Delta step = ((dst > org) ? DEL_E : DEL_W);
        for (i08 s = dst; s != org; s -= step)
        {
            if (attackers_to (Square(s), ~_active, _types_bb[NONE]))
            {
                return false;
            }
        }

        //ct = NONE;
        return true;
    }
    break;

    case ENPASSANT:
    {
        if (!(  PAWN == ptype (_board[org])
             && _si->en_passant_sq == dst
             && R_5 == r_org
             && R_6 == r_dst
             && empty (dst)
             )
           )
            return false;

        cap += pawn_push (~_active);
        if ((~_active|PAWN) != _board[cap]) return false;
        ct = PAWN;
    }
    break;

    case PROMOTE:
    {
        if (!(  PAWN == ptype (_board[org])
             && R_7 == r_org
             && R_8 == r_dst
             )
           )
            return false;

        ct = ptype (_board[cap]);
    }
    break;

    default:
        assert (false);
    break;
    }

    if (KING == ct) return false;

    // The destination square cannot be occupied by a friendly piece
    if (_color_bb[_active] & dst) return false;

    // Handle the special case of a piece move
    if (PAWN == ptype (_board[org]))
    {
        // Have already handled promotion moves, so destination
        // cannot be on the 8th/1st rank.
        if (R_1 == r_org || R_8 == r_org) return false;
        if (R_1 == r_dst || R_2 == r_dst) return false;
        if (NORMAL == mtype (m) && (R_7 == r_org || R_8 == r_dst)) return false;
        
        // Move direction must be compatible with pawn color
        Delta delta = dst - org;
        if ((_active == WHITE) != (delta > DEL_O)) return false;

        // Proceed according to the delta between the origin and destiny squares.
        switch (delta)
        {
        case DEL_N:
        case DEL_S:
            // Pawn push. The destination square must be empty.
            if (!( empty (dst)
                && 0 == dist<File> (dst, org)
                 )
               )
                return false;
        break;
        case DEL_NE:
        case DEL_NW:
        case DEL_SE:
        case DEL_SW:
            // The destination square must be occupied by an enemy piece
            // (en passant captures was handled earlier).
            // File distance b/w cap and org must be one, avoids a7h5
            if (!( NONE != ct
                && ~_active == color (_board[cap])
                && 1 == dist<File> (cap, org)
                 )
               )
                return false;
        break;
        case DEL_NN:
        case DEL_SS:
            // Double pawn push. The destination square must be on the fourth
            // rank, and both the destination square and the square between the
            // source and destination squares must be empty.
            if (!( R_2 == r_org
                && R_4 == r_dst
                && empty (dst)
                && empty (dst - pawn_push (_active))
                && 0 == dist<File> (dst, org)
                 )
               )
                return false;
        break;
        default:
            return false;
        break;
        }

    }
    else
    {
        if (!(attacks_bb (_board[org], org, _types_bb[NONE]) & dst)) return false;
    }

    // Evasions generator already takes care to avoid some kind of illegal moves
    // and legal() relies on this. So have to take care that the
    // same kind of moves are filtered out here.
    if (_si->checkers != U64(0))
    {
        // In case of king moves under check, remove king so to catch
        // as invalid moves like B1A1 when opposite queen is on C1.
        if (KING == ptype (_board[org])) return !attackers_to (dst, ~_active, _types_bb[NONE] - org); // Remove 'org' but not place 'dst'

        // Double check? In this case a king move is required
        if (more_than_one (_si->checkers)) return false;

        return ENPASSANT == mtype (m) && PAWN == ptype (_board[org]) ?
            // Move must be a capture of the checking en-passant pawn
            // or a blocking evasion of the checking piece
            _si->checkers & cap || BETWEEN_SQRS_bb[scan_lsq (_si->checkers)][_piece_list[_active][KING][0]] & dst :
            // Move must be a capture or a blocking evasion of the checking piece
            (_si->checkers | BETWEEN_SQRS_bb[scan_lsq (_si->checkers)][_piece_list[_active][KING][0]]) & dst;
    }
    return true;
}

// legal() tests whether a pseudo-legal move is legal
bool Position::legal        (Move m, Bitboard pinned) const
{
    assert (_ok (m));
    assert (pinned == pinneds (_active));

    Square org = org_sq (m)
        ,  dst = dst_sq (m);

    assert (_active == color (_board[org]) && NONE != ptype (_board[org]));

    switch (mtype (m))
    {
    case NORMAL:
    case PROMOTE:
    {
        // In case of king moves under check have to remove king so to catch
        // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
        // check whether the destination square is attacked by the opponent.
        if (KING == ptype (_board[org])) return !attackers_to (dst, ~_active, _types_bb[NONE] - org); // Remove 'org' but not place 'dst'

        // A non-king move is legal if and only if it is not pinned or
        // it is moving along the ray towards or away from the king or
        // it is a blocking evasion or a capture of the checking piece.
        return   pinned == U64(0)
            || !(pinned & org)
            || sqrs_aligned (org, dst, _piece_list[_active][KING][0]);
    }
    break;

    case CASTLE:
    {
        // Castling moves are checked for legality during move generation.
        return KING == ptype (_board[org]) && ROOK == ptype (_board[dst]);
    }
    break;

    case ENPASSANT:
    {
        // En-passant captures are a tricky special case. Because they are rather uncommon,
        // do it simply by testing whether the king is attacked after the move is made.
        Square cap = dst + pawn_push (~_active);

        assert (dst == _si->en_passant_sq && empty (dst) && ( _active|PAWN) == _board[org] && (~_active|PAWN) == _board[cap]);

        Bitboard mocc = _types_bb[NONE] - org - cap + dst;
        // If any attacker then in check & not legal
        return !(attacks_bb<ROOK> (_piece_list[_active][KING][0], mocc) & (_color_bb[~_active]&(_types_bb[QUEN]|_types_bb[ROOK])))
            && !(attacks_bb<BSHP> (_piece_list[_active][KING][0], mocc) & (_color_bb[~_active]&(_types_bb[QUEN]|_types_bb[BSHP])));
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
    Square org = org_sq (m)
        ,  dst = dst_sq (m);

    assert (color (_board[org]) == _active);
    assert (ci.discoverers == discoverers (_active));
    
    // Is there a Direct check ?
    if (ci.checking_bb[ptype (_board[org])] & dst) return true;
    // Is there a Discovered check ?
    // For discovery check we need to verify also direction
    if (  ci.discoverers && (ci.discoverers & org)
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
        Square rook_org = dst; // 'King captures the rook' notation
        dst             = rel_sq (_active, (dst > org) ? SQ_G1 : SQ_C1);
        Square rook_dst = rel_sq (_active, (dst > org) ? SQ_F1 : SQ_D1);

        return PIECE_ATTACKS[ROOK][rook_dst] & ci.king_sq // First x-ray check then full check
            && attacks_bb<ROOK> (rook_dst, (_types_bb[NONE] - org - rook_org + dst + rook_dst)) & ci.king_sq;
    }
    break;
    
    case ENPASSANT:
    {
        // En-passant capture with check ?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        Square cap = _file (dst) | _rank (org);
        Bitboard mocc = _types_bb[NONE] - org - cap + dst;
        // if any attacker then in check
        return attacks_bb<ROOK> (ci.king_sq, mocc) & (_color_bb[_active]&(_types_bb[QUEN]|_types_bb[ROOK]))
            || attacks_bb<BSHP> (ci.king_sq, mocc) & (_color_bb[_active]&(_types_bb[QUEN]|_types_bb[BSHP]));
               
    }
    break;

    case PROMOTE:
    {
        // Promotion with check ?
        return attacks_bb (Piece(promote (m)), dst, _types_bb[NONE] - org + dst) & ci.king_sq;
    }
    break;
    
    default:
        assert (false);
        return false;
    break;
    }
}

//// gives_checkmate() tests whether a pseudo-legal move gives a checkmate
//bool Position::gives_checkmate (Move m, const CheckInfo &ci) const
//{
//    if (gives_check (m, ci))
//    {
//        Position pos = *this;
//        StateInfo si;
//        pos.do_move (m, si, &ci);
//        return MoveList<LEGAL> (pos).size () == 0;
//    }
//    return false;
//}

// clear() clear the position
void Position::clear ()
{
    memset (this, 0x00, sizeof (*this));

    for (i08 s = SQ_A1; s <= SQ_H8; ++s)
    {
        _board[s] = EMPTY;
        _piece_index[s] = -1;
    }
    for (i08 c = WHITE; c <= BLACK; ++c)
    {
        for (i08 pt = PAWN; pt <= KING; ++pt)
        {
            for (i08 i = 0; i < 16; ++i)
            {
                _piece_list[c][pt][i] = SQ_NO;
            }
        }
    }

    fill (_castle_rook, _castle_rook + sizeof (_castle_rook)/sizeof (*_castle_rook), SQ_NO);

    _sb.en_passant_sq = SQ_NO;
    _sb.capture_type  = NONE;
    _si = &_sb;
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
bool Position::setup (const string &f, Thread *th, bool c960, bool full)
{
    if (white_spaces (f)) return false;
    istringstream iss (f);
    iss >> noskipws;

    clear ();
    
    u08 ch;
    // 1. Piece placement on Board
    size_t idx;
    Square s = SQ_A8;
    while (iss >> ch && !isspace (ch))
    {
        if (isdigit (ch))
        {
            s += Delta (ch - '0'); // Advance the given number of files
        }
        else
        if (isalpha (ch) && (idx = PIECE_CHAR.find (ch)) != string::npos)
        {
            Piece p = Piece(idx);
            place_piece (s, color (p), ptype (p));
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

    assert (_piece_list[WHITE][KING][0] != SQ_NO);
    assert (_piece_list[BLACK][KING][0] != SQ_NO);

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
    if (c960)
    {
        while ((iss >> ch) && !isspace (ch))
        {
            Color c = isupper (ch) ? WHITE : BLACK;
            u08 sym = u08(tolower (ch));
            if ('a' <= sym && sym <= 'h')
            {
                Square rsq = (to_file (sym) | rel_rank (c, R_1));
                //if (ROOK != ptype (_board[rsq])) return false;
                set_castle (c, rsq);
            }
            else
            {
                continue;
            }
        }
    }
    else
    {
        while ((iss >> ch) && !isspace (ch))
        {
            i08 rsq; // Rook Square
            Color c = isupper (ch) ? WHITE : BLACK;
            switch (toupper (ch))
            {
            case 'K':
                rsq = rel_sq (c, SQ_H1);
                while ((rel_sq (c, SQ_A1) <= rsq) && (ROOK != ptype (_board[rsq]))) --rsq;
                break;
            case 'Q':
                rsq = rel_sq (c, SQ_A1);
                while ((rel_sq (c, SQ_H1) >= rsq) && (ROOK != ptype (_board[rsq]))) ++rsq;
                break;
            default:
                continue;
            }

            //if (ROOK != ptype (_board[rsq])) return false;
            set_castle (c, Square(rsq));
        }
    }

    // 4. En-passant square. Ignore if no pawn capture is possible
    u08 col, row;
    if (  (iss >> col && (col >= 'a' && col <= 'h'))
       && (iss >> row && (row == '3' || row == '6'))
       )
    {
        if (  (WHITE == _active && '6' == row)
           || (BLACK == _active && '3' == row)
           )
        {
            Square ep_sq = to_square (col, row);
            if (can_en_passant (ep_sq))
            {
                _si->en_passant_sq = ep_sq;
            }
        }
    }

    // 5-6. 50-move clock and game-move count
    i32 clk50 = 0, g_move = 1;
    if (full)
    {
        iss >> skipws >> clk50 >> g_move;
        // Rule 50 draw case
        //if (clk50 >100) return false;
        if (g_move <= 0) g_move = 1;
    }

    // Convert from game_move starting from 1 to game_ply starting from 0,
    // handle also common incorrect FEN with game_move = 0.
    _si->clock50 = u08(SQ_NO != _si->en_passant_sq ? 0 : clk50);
    _game_ply = max (2*(g_move - 1), 0) + (BLACK == _active);

    _si->matl_key = Zob.compute_matl_key (*this);
    _si->pawn_key = Zob.compute_pawn_key (*this);
    _si->posi_key = Zob.compute_posi_key (*this);
    _si->psq_score = compute_psq_score ();
    _si->non_pawn_matl[WHITE] = compute_non_pawn_material (WHITE);
    _si->non_pawn_matl[BLACK] = compute_non_pawn_material (BLACK);
    _si->checkers = checkers (_active);
    _game_nodes   = 0;
    _chess960     = c960;
    _thread       = th;

    return true;
}

// set_castle() set the castling for the particular color & rook
void Position::set_castle (Color c, Square rook_org)
{
    Square king_org = _piece_list[c][KING][0];
    assert (king_org != rook_org);

    CRight cr = mk_castle_right (c, (rook_org > king_org) ? CS_K : CS_Q);
    Square rook_dst = rel_sq (c, (rook_org > king_org) ? SQ_F1 : SQ_D1);
    Square king_dst = rel_sq (c, (rook_org > king_org) ? SQ_G1 : SQ_C1);

    _si->castle_rights     |= cr;

    _castle_mask[king_org] |= cr;
    _castle_mask[rook_org] |= cr;
    _castle_rook[cr] = rook_org;

    for (i08 s = min (rook_org, rook_dst); s <= max (rook_org, rook_dst); ++s)
    {
        if (king_org != s && rook_org != s)
        {
            _castle_path[cr] += Square(s);
        }
    }
    for (i08 s = min (king_org, king_dst); s <= max (king_org, king_dst); ++s)
    {
        if (king_org != s && rook_org != s)
        {
            _castle_path[cr] += Square(s);
              _king_path[cr] += Square(s);
        }
    }
}
// can_en_passant() tests the en-passant square
bool Position::can_en_passant (Square ep_sq) const
{
    assert (_ok (ep_sq));
    assert (R_6 == rel_rank (_active, ep_sq));

    Square cap = ep_sq + pawn_push (~_active);
    //if (!((_color_bb[~_active]&_types_bb[PAWN]) & cap)) return false;
    if ((~_active | PAWN) != _board[cap]) return false;
    
    // En-passant attackes
    Bitboard attacks = PAWN_ATTACKS[~_active][ep_sq] & _color_bb[_active]&_types_bb[PAWN];
    assert (pop_count<FULL> (attacks) <= 2);
    if (attacks == U64(0)) return false;

    Move moves[3], *m = moves;

    fill (moves, moves + sizeof (moves)/sizeof (*moves), MOVE_NONE);
    while (attacks != U64(0))
    {
        *(m++) = mk_move<ENPASSANT> (pop_lsq (attacks), ep_sq);
    }

    // Check en-passant is legal for the position
    Bitboard occ = _types_bb[NONE] + ep_sq - cap;
    for (m = moves; *m != MOVE_NONE; ++m)
    {
        Bitboard mocc = occ - org_sq (*m);
        if (  !(attacks_bb<ROOK> (_piece_list[_active][KING][0], mocc) & (_color_bb[~_active]&(_types_bb[QUEN]|_types_bb[ROOK])))
           && !(attacks_bb<BSHP> (_piece_list[_active][KING][0], mocc) & (_color_bb[~_active]&(_types_bb[QUEN]|_types_bb[BSHP])))
           )
        {
            return true;
        }
    }

    return false;
}

// compute_psq_score() computes the incremental scores for the middle
// game and the endgame. These functions are used to initialize the incremental
// scores when a new position is set up, and to verify that the scores are correctly
// updated by do_move and undo_move when the program is running in debug mode.
Score Position::compute_psq_score () const
{
    Score  score = SCORE_ZERO;
    Bitboard occ = _types_bb[NONE];
    while (occ != U64(0))
    {
        Square s = pop_lsq (occ);
        score += PSQT[color (_board[s])][ptype (_board[s])][s];
    }
    return score;
}

// compute_non_pawn_material() computes the total non-pawn middle
// game material value for the given side. Material values are updated
// incrementally during the search, this function is only used while
// initializing a new Position object.
Value Position::compute_non_pawn_material (Color c) const
{
    Value value = VALUE_ZERO;
    for (i08 pt = NIHT; pt <= QUEN; ++pt)
    {
        value += PIECE_VALUE[MG][pt] * i32(_piece_count[c][pt]);
    }
    return value;
}

#undef do_capture

#define do_capture() {                                                            \
    remove_piece (cap);                                                           \
    if (PAWN == ct)                                                               \
    {                                                                             \
        _si->pawn_key ^= Zob._.piece_square[~_active][PAWN][cap];                 \
    }                                                                             \
    else                                                                          \
    {                                                                             \
        _si->non_pawn_matl[~_active] -= PIECE_VALUE[MG][ct];                      \
    }                                                                             \
    _si->matl_key ^= Zob._.piece_square[~_active][ct][_piece_count[~_active][ct]];\
    key           ^= Zob._.piece_square[~_active][ct][cap];                       \
    _si->psq_score -= PSQT[~_active][ct][cap];                                    \
    _si->clock50 = 0;                                                             \
}

// do_move() do the move with checking info
void Position::  do_move (Move m, StateInfo &si, const CheckInfo *ci)
{
    assert (_ok (m));
    assert (&si != _si);

    Key key = _si->posi_key;
    // Copy some fields of old state to new StateInfo object except the ones
    // which are going to be recalculated from scratch anyway, 
    memcpy (&si, _si, STATEINFO_COPY_SIZE);

    // Switch state pointer to point to the new, ready to be updated, state.
    si.ptr = _si;
    _si    = &si;

    Color pasive = ~_active;

    Square org = org_sq (m)
        ,  dst = dst_sq (m);
    PieceT pt  = ptype (_board[org]);

    assert (!empty (org) && _active == color (_board[org]) && NONE != pt);
    assert ( empty (dst) || pasive == color (_board[dst]) || CASTLE == mtype (m));

    Square cap = dst;
    PieceT ct  = NONE;

    // Do move according to move type
    switch (mtype (m))
    {
    case NORMAL:
    {
        ct = ptype (_board[cap]);

        assert (PAWN == promote (m) - NIHT && KING != ct);
        
        if (NONE != ct)
        {
            do_capture ();
        }
        else
        {
            (PAWN == pt) ? _si->clock50 = 0 : _si->clock50++;
        }

        move_piece (org, dst);

        if (PAWN == pt)
        {
            _si->pawn_key ^=
                 Zob._.piece_square[_active][PAWN][org]
                ^Zob._.piece_square[_active][PAWN][dst];
        }

        key ^=
             Zob._.piece_square[_active][pt][org]
            ^Zob._.piece_square[_active][pt][dst];

        _si->psq_score +=
            -PSQT[_active][pt][org]
            +PSQT[_active][pt][dst];
    }
    break;

    case CASTLE:
    {
        assert (KING == pt && ROOK == ptype (_board[dst]));

        Square rook_org, rook_dst;
        do_castling<true> (org, dst, rook_org, rook_dst);

        key ^=
             Zob._.piece_square[_active][KING][     org]
            ^Zob._.piece_square[_active][KING][     dst]
            ^Zob._.piece_square[_active][ROOK][rook_org]
            ^Zob._.piece_square[_active][ROOK][rook_dst];

        _si->psq_score +=
            -PSQT[_active][KING][     org]
            +PSQT[_active][KING][     dst]
            -PSQT[_active][ROOK][rook_org]
            +PSQT[_active][ROOK][rook_dst];

        _si->clock50++;
    }
    break;

    case ENPASSANT:
    {
        assert (PAWN == pt && dst == _si->en_passant_sq && empty (dst));
        assert (R_5 == rel_rank (_active, org) && R_6 == rel_rank (_active, dst));

        cap += pawn_push (pasive);
        assert (!empty (cap) && (pasive|PAWN) == _board[cap]);

        ct = PAWN;
        do_capture ();

        move_piece (org, dst);

        _si->pawn_key ^=
             Zob._.piece_square[_active][PAWN][org]
            ^Zob._.piece_square[_active][PAWN][dst];

        key ^=
             Zob._.piece_square[_active][PAWN][org]
            ^Zob._.piece_square[_active][PAWN][dst];

        _si->psq_score +=
            -PSQT[_active][PAWN][org]
            +PSQT[_active][PAWN][dst];
    }
    break;

    case PROMOTE:
    {
        ct = ptype (_board[cap]);
        assert (PAWN == pt && R_7 == rel_rank (_active, org) && R_8 == rel_rank (_active, dst) && PAWN != ct && KING != ct);

        if (NONE != ct)
        {
            do_capture ();
        }
        else
        {
            _si->clock50 = 0;
        }

        PieceT ppt = promote (m);
        // Replace the PAWN with the Promoted piece
        remove_piece (org);
        place_piece (dst, _active, ppt);

        _si->matl_key ^=
             Zob._.piece_square[_active][PAWN][_piece_count[_active][PAWN]]
            ^Zob._.piece_square[_active][ppt ][_piece_count[_active][ppt] - 1];

        _si->pawn_key ^=
             Zob._.piece_square[_active][PAWN][org];

        key ^=
             Zob._.piece_square[_active][PAWN][org]
            ^Zob._.piece_square[_active][ppt ][dst];

        _si->psq_score +=
            +PSQT[_active][ppt ][dst]
            -PSQT[_active][PAWN][org];

        _si->non_pawn_matl[_active] += PIECE_VALUE[MG][ppt];
    }
    break;

    default:
        assert (false);
    break;
    }

    u08 cr = _si->castle_rights & (_castle_mask[org]|_castle_mask[dst]);
    if (cr != 0)
    {
        Bitboard b = cr;
        _si->castle_rights &= ~cr;
        while (b != U64(0))
        {
            key ^= Zob._.castle_right[0][pop_lsq (b)];
        }
    }

    _si->checkers = U64(0);
    if (ci != NULL)
    {
        if (mtype (m) == NORMAL)
        {
            // Direct check ?
            if (ci->checking_bb[pt] & dst)
            {
                _si->checkers += dst;
            }
            // Discovery check ?
            if (QUEN != pt)
            {
                if (ci->discoverers && ci->discoverers & org)
                {
                    if (ROOK != pt)
                    {
                        _si->checkers |=
                            attacks_bb<ROOK> (_piece_list[pasive][KING][0], _types_bb[NONE]) &
                            _color_bb[_active]&(_types_bb[QUEN]|_types_bb[ROOK]);
                    }
                    if (BSHP != pt)
                    {
                        _si->checkers |=
                            attacks_bb<BSHP> (_piece_list[pasive][KING][0], _types_bb[NONE]) &
                            _color_bb[_active]&(_types_bb[QUEN]|_types_bb[BSHP]);
                    }
                }
            }
        }
        else
        {
            _si->checkers = attackers_to (_piece_list[pasive][KING][0], _active);
        }
    }

    _active = pasive;
    key ^= Zob._.act_side;

    if (SQ_NO != _si->en_passant_sq)
    {
        key ^= Zob._.en_passant[_file (_si->en_passant_sq)];
        _si->en_passant_sq = SQ_NO;
    }
    if (PAWN == pt)
    {
        if (DEL_NN == (u08(dst) ^ u08(org)))
        {
            Square ep_sq = Square((u08(dst) + u08(org)) / 2);
            if (can_en_passant (ep_sq))
            {
                _si->en_passant_sq = ep_sq;
                key ^= Zob._.en_passant[_file (ep_sq)];
            }
        }
    }

    _si->posi_key     = key;
    _si->last_move    = m;
    _si->capture_type = ct;
    ++_si->null_ply;
    ++_game_ply;
    ++_game_nodes;

    assert (ok ());
}
#undef do_capture
void Position::  do_move (Move m, StateInfo &si)
{
    CheckInfo ci (*this);
    do_move (m, si, gives_check (m, ci) ? &ci : NULL);
}
// do_move() do the move from string (CAN)
void Position::  do_move (string &can, StateInfo &si)
{
    Move m = move_from_can (can, *this);
    if (MOVE_NONE != m) do_move (m, si);
}
// undo_move() undo the last move
void Position::undo_move ()
{
    assert (_si->ptr != NULL);
    Move m = _si->last_move;
    assert (_ok (m));

    Square org = org_sq (m)
        ,  dst = dst_sq (m);

    _active = ~_active;

    assert (empty (org) || mtype (m) == CASTLE);

    assert (KING != _si->capture_type);

    Square cap = dst;

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
        //ct  = NONE;
    }
    break;

    case ENPASSANT:
    {
        assert (PAWN == ptype (_board[dst]));
        assert (PAWN == _si->capture_type);
        assert (R_5 == rel_rank (_active, org));
        assert (R_6 == rel_rank (_active, dst));
        assert (dst == _si->ptr->en_passant_sq);
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
        place_piece (org, _active, PAWN);
    }
    break;

    default:
        assert (false);
    break;
    }

    if (NONE != _si->capture_type)
    {
        place_piece (cap, ~_active, _si->capture_type);
    }

    --_game_ply;
    _si = _si->ptr;

    assert (ok ());
}

// do_null_move() do the null-move
void Position::  do_null_move (StateInfo &si)
{
    assert (&si != _si);
    assert (_si->checkers == U64(0));

    // Full copy here
    memcpy (&si, _si, sizeof (si));

    // Switch our state pointer to point to the new, ready to be updated, state.
    si.ptr = _si;
    _si    = &si;

    if (SQ_NO != _si->en_passant_sq)
    {
        _si->posi_key ^= Zob._.en_passant[_file (_si->en_passant_sq)];
        _si->en_passant_sq = SQ_NO;
    }
    _si->posi_key ^= Zob._.act_side;
    _si->clock50++;
    _si->null_ply = 0;

    _active = ~_active;

    assert (ok ());
}
// undo_null_move() undo the last null-move
void Position::undo_null_move ()
{
    assert (_si->ptr != NULL);
    assert (_si->checkers == U64(0));

    _active = ~_active;
    _si     = _si->ptr;

    assert (ok ());
}

// flip() flips position with the white and black sides reversed.
// This is only useful for debugging especially for finding evaluation symmetry bugs.
void Position::flip ()
{
    string fen_, s;
    stringstream ss (fen ());
    // 1. Piece placement
    for (i08 rank = R_8; rank >= R_1; --rank)
    {
        getline (ss, s, rank > R_1 ? '/' : ' ');
        fen_.insert (0, s + (white_spaces (fen_) ? " " : "/"));
    }
    // 2. Active color
    ss >> s;
    fen_ += (s == "w" ? "B" : "W"); // Will be lowercased later
    fen_ += " ";
    // 3. Castling availability
    ss >> s;
    fen_ += s + " ";
    transform (fen_.begin (), fen_.end (), fen_.begin (), toggle_case);

    // 4. En-passant square
    ss >> s;
    fen_ += (s == "-" ? s : s.replace (1, 1, s[1] == '3' ? "6" : s[1] == '6' ? "3" : "-"));
    // 5-6. Half and full moves
    getline (ss, s);
    fen_ += s;

    setup (fen_, _thread, _chess960);

    assert (ok ());
}

string Position::fen (bool c960, bool full) const
{
    ostringstream oss;

    for (i08 r = R_8; r >= R_1; --r)
    {
        for (i08 f = F_A; f <= F_H; ++f)
        {
            Square s = File(f) | Rank(r);
            i16 empty_count = 0;
            while (F_H >= f && empty (s))
            {
                ++empty_count;
                ++f;
                ++s;
            }
            if (empty_count) oss << empty_count;
            if (F_H >= f)  oss << PIECE_CHAR[_board[s]];
        }

        if (R_1 < r) oss << "/";
    }

    oss << " " << COLOR_CHAR[_active] << " ";

    if (can_castle (CR_A))
    {
        if (_chess960 || c960)
        {
            if (can_castle (WHITE))
            {
                if (can_castle (CR_WK)) oss << to_char (_file (_castle_rook[Castling<WHITE, CS_K>::Right]), false);
                if (can_castle (CR_WQ)) oss << to_char (_file (_castle_rook[Castling<WHITE, CS_Q>::Right]), false);
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_BK)) oss << to_char (_file (_castle_rook[Castling<BLACK, CS_K>::Right]), true);
                if (can_castle (CR_BQ)) oss << to_char (_file (_castle_rook[Castling<BLACK, CS_Q>::Right]), true);
            }
        }
        else
        {
            if (can_castle (WHITE))
            {
                if (can_castle (CR_WK)) oss << "K";
                if (can_castle (CR_WQ)) oss << "Q";
            }
            if (can_castle (BLACK))
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

    oss << " " << ((SQ_NO == _si->en_passant_sq) ? "-" : to_string (_si->en_passant_sq)) << " ";

    if (full) oss << i16(_si->clock50) << " " << game_move ();

    return oss.str ();
}

// string() returns an ASCII representation of the position to be
// printed to the standard output
Position::operator string () const
{
    static string EDGE  = " +---+---+---+---+---+---+---+---+\n";
    static string ROW_1 = "| . |   | . |   | . |   | . |   |\n" + EDGE;
    static string ROW_2 = "|   | . |   | . |   | . |   | . |\n" + EDGE;
    static u16 ROW_LEN  = ROW_1.length () + 1;

    string board = EDGE;

    for (i08 r = R_8; r >= R_1; --r)
    {
        board += to_char (Rank(r)) + ((r % 2) ? ROW_1 : ROW_2);
    }
    for (i08 f = F_A; f <= F_H; ++f)
    {
        board += "   ";
        board += to_char (File(f), false);
    }

    Bitboard occ = _types_bb[NONE];
    while (occ != U64(0))
    {
        Square s = pop_lsq (occ);
        board[3 + i32(ROW_LEN * (7.5 - _rank (s)) + 4 * _file (s))] = PIECE_CHAR[_board[s]];
    }

    ostringstream oss;

    oss << board << "\n\n";

    oss << "Fen: " << fen () << "\n"
        << "Key: " << hex << uppercase << setfill ('0') << setw (16) 
        << _si->posi_key << dec << setfill (' ') << "\n";

    oss << "Checkers: ";
    Bitboard chkrs = _si->checkers;
    if (chkrs != U64(0))
    {
        while (chkrs != U64(0))
        {
            Square chk_sq = pop_lsq (chkrs);
            oss << PIECE_CHAR[ptype (_board[chk_sq])] << to_string (chk_sq) << " ";
        }
    }
    else
    {
        oss << "<none>";
    }

    oss << "\n";
    /*
    MoveList<LEGAL> ms (*this);
    oss << "Legal moves (" << ms.size () << "): ";
    for ( ; *ms; ++ms)
    {
        oss << move_to_san (*ms, *const_cast<Position*> (this)) << " ";
    }
    */
    return oss.str ();
}
