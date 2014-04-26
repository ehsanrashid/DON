#include "Position.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "BitBoard.h"
#include "BitScan.h"
#include "BitCount.h"
#include "MoveGenerator.h"
#include "Transposition.h"
#include "Thread.h"
#include "Notation.h"
#include "UCI.h"

using namespace std;
using namespace BitBoard;
using namespace MoveGenerator;
using namespace Threads;
using namespace Notation;

const string PieceChar ("PNBRQK  pnbrqk");
const string ColorChar ("wb-");

const Value PieceValue[PHASE_NO][TOTL] =
{
    { VALUE_MG_PAWN, VALUE_MG_NIHT, VALUE_MG_BSHP, VALUE_MG_ROOK, VALUE_MG_QUEN, VALUE_ZERO, VALUE_ZERO },
    { VALUE_EG_PAWN, VALUE_EG_NIHT, VALUE_EG_BSHP, VALUE_EG_ROOK, VALUE_EG_QUEN, VALUE_ZERO, VALUE_ZERO }
};

const string StartFEN ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

bool _ok (const string &fen, bool c960, bool full)
{
    if (fen.empty ()) return false;
    Position pos (0);
    return Position::parse (pos, fen, NULL, c960, full) && pos.ok ();
}

namespace {

    // do_move() copy current state info up to 'posi_key' excluded to the new one.
    // calculate the quad words (64bits) needed to be copied.
    const u08 STATE_COPY_SIZE = offsetof (StateInfo, posi_key);

    CACHE_ALIGN(64) Score PSQ[CLR_NO][NONE][SQ_NO];

#define S(mg, eg) mk_score (mg, eg)
    // PSQT[PieceType][Square] contains Piece-Square scores. For each piece type on
    // a given square a (midgame, endgame) score pair is assigned. PSQT is defined
    // for white side, for black side the tables are symmetric.
    const Score PSQT[NONE][SQ_NO] =
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

    // prefetch() preloads the given address in L1/L2 cache.
    // This is a non-blocking function that doesn't stall
    // the CPU waiting for data to be loaded from memory,
    // which can be quite slow.
#ifdef PREFETCH

#   if defined(_MSC_VER) || defined(__INTEL_COMPILER)

#   include <xmmintrin.h> // Intel and Microsoft header for _mm_prefetch()

    inline void prefetch (const char *addr)
    {
#       if defined(__INTEL_COMPILER)
        {
            // This hack prevents prefetches from being optimized away by
            // Intel compiler. Both MSVC and gcc seem not be affected by this.
            __asm__ ("");
        }
#       endif
        _mm_prefetch (addr, _MM_HINT_T0);
    }

#   else

    inline void prefetch (const char *addr)
    {
        __builtin_prefetch (addr);
    }

#   endif

#else

    inline void prefetch (const char *) {}

#endif

    char toggle_case (unsigned char c) { return char (islower (c) ? toupper (c) : tolower (c)); }

} // namespace

u08 Position::_50_move_dist;

void Position::initialize ()
{
    _50_move_dist = 2*i32 (Options["50 Move Distance"]);

    for (u08 pt = PAWN; pt <= KING; ++pt)
    {
        Score score = mk_score (PieceValue[MG][pt], PieceValue[EG][pt]);

        for (i08 s = SQ_A1; s <= SQ_H8; ++s)
        {
            Score psq_score = score + PSQT[pt][s];
            PSQ[WHITE][pt][ Square (s)] = +psq_score;
            PSQ[BLACK][pt][~Square (s)] = -psq_score;
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
    // Draw by Material?
    if (   (_types_bb[PAWN] == U64 (0))
        && (_si->non_pawn_matl[WHITE] + _si->non_pawn_matl[BLACK]
          <= VALUE_MG_BSHP)
       )
    {
        return true;
    }

    // Draw by 50 moves Rule?
    if (    _50_move_dist <  _si->clock50
        || (_50_move_dist == _si->clock50
          && (_si->checkers == U64 (0) || MoveList<LEGAL> (*this).size () != 0)
           )
       )
    {
        return true;
    }

    // Draw by Threefold Repetition?
    const StateInfo *psi = _si;
    u08 ply = min (_si->null_ply, _si->clock50);
    while (ply >= 2)
    {
        //psi = psi->p_si; if (psi == NULL) break; 
        //psi = psi->p_si; if (psi == NULL) break;
        psi = psi->p_si->p_si;
        if (psi->posi_key == _si->posi_key)
        {
            return true; // Draw at first repetition
        }
        ply -= 2;
    }

    //// Draw by Stalemate?
    //if (!in_check)
    //{
    //    if (MoveList<LEGAL> (*this).size () == 0) return true;
    //}

    return false;
}

// Check whether there has been at least one repetition of positions
// since the last capture or pawn move.
bool Position::repeated () const
{
    StateInfo *si = _si;
    while (si != NULL)
    {
        i32 i = 4, e = min (si->clock50, si->null_ply);
        if (e < i) return false;
        StateInfo *psi = si->p_si->p_si;
        do
        {
            psi = psi->p_si->p_si;
            if (psi->posi_key == si->posi_key)
            {
                return true;
            }
            i += 2;
        }
        while (i <= e);
        si = si->p_si;
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
    if ( (WHITE != _active && BLACK != _active)
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
            if (1 != std::count (_board, _board + SQ_NO, Color (c)|KING))
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
        if (   pop_count<FULL> (_types_bb[NONE]) > 32
            || count<NONE> () > 32
            || count<NONE> () != pop_count<FULL> (_types_bb[NONE])
           )
        {
            return false;
        }

        for (i08 c = WHITE; c <= BLACK; ++c)
        {
            for (u08 pt = PAWN; pt <= KING; ++pt)
            {
                if (_piece_count[c][pt] != pop_count<FULL> (_color_bb[c]&_types_bb[pt]))
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
        for (u08 pt1 = PAWN; pt1 <= KING; ++pt1)
        {
            for (u08 pt2 = PAWN; pt2 <= KING; ++pt2)
            {
                if (pt1 != pt2 && (_types_bb[pt1]&_types_bb[pt2]))
                {
                    return false;
                }
            }
        }

        // The union of the white and black pieces must be equal to occupied squares
        if (  ((_color_bb[WHITE]|_color_bb[BLACK]) != _types_bb[NONE])
           || ((_color_bb[WHITE]^_color_bb[BLACK]) != _types_bb[NONE])
           )
        {
            return false;
        }

        // The union of separate piece type must be equal to occupied squares
        if ( ((_types_bb[PAWN]|_types_bb[NIHT]|_types_bb[BSHP]
              |_types_bb[ROOK]|_types_bb[QUEN]|_types_bb[KING]) != _types_bb[NONE]
             )
          || ((_types_bb[PAWN]^_types_bb[NIHT]^_types_bb[BSHP]
              ^_types_bb[ROOK]^_types_bb[QUEN]^_types_bb[KING]) != _types_bb[NONE]
             )
           )
        {
            return false;
        }

        // PAWN rank should not be 1/8
        if ((_types_bb[PAWN]&(R1_bb|R8_bb)))
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
            if (    (_piece_count[c][PAWN] +
                max (_piece_count[c][NIHT] - 2, 0) +
                max (_piece_count[c][BSHP] - 2, 0) +
                max (_piece_count[c][ROOK] - 2, 0) +
                max (_piece_count[c][QUEN] - 1, 0)) > 8
               )
            {
                return false; // Too many Promoted Piece of color
            }

            if (_piece_count[c][BSHP] > 1)
            {
                Bitboard bishops = colors & _types_bb[BSHP];
                u08 bishop_count[CLR_NO] =
                {
                    pop_count<FULL> (LIHT_bb & bishops),
                    pop_count<FULL> (DARK_bb & bishops),
                };

                if (    (_piece_count[c][PAWN] +
                    max (bishop_count[WHITE] - 1, 0) +
                    max (bishop_count[BLACK] - 1, 0)) > 8
                   )
                {
                    return false; // Too many Promoted BISHOP of color
                }
            }

            // There should be one and only one KING of color
            Bitboard kings = colors & _types_bb[KING];
            if (kings == U64 (0) || more_than_one (kings))
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
            for (u08 pt = PAWN; pt <= KING; ++pt)
            {
                for (i32 i = 0; i < _piece_count[c][pt]; ++i)
                {
                    if (   !_ok  (_piece_list[c][pt][i])
                        || _board[_piece_list[c][pt][i]] != (Color (c) | PieceT (pt))
                        || _index[_piece_list[c][pt][i]] != i
                       )
                    {
                        return false;
                    }
                }
            }
        }
        for (i08 s = SQ_A1; s <= SQ_H8; ++s)
        {
            if (_index[s] >= 16)
            {
                return false;
            }
        }
    }
    // step 6
    if (step && ++(*step), test_king_capture)
    {
        if (  (attackers_to (_piece_list[~_active][KING][0]) & _color_bb[_active])
           || (pop_count<FULL> (_si->checkers)) > 2
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
                CRight cr = mk_castle_right (Color (c), CSide (cs));

                if (!can_castle (cr)) continue;

                if ( (_castle_mask[_piece_list[c][KING][0]] & cr) != cr
                  || (_board[_castle_rook[cr]] != (Color (c) | ROOK))
                  || (_castle_mask[_castle_rook[cr]] != cr)
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
        if (  (compute_non_pawn_material (WHITE) != _si->non_pawn_matl[WHITE])
           || (compute_non_pawn_material (BLACK) != _si->non_pawn_matl[BLACK])
           )
        {
            return false;
        }
    }

    return true;
}

// least_valuable_attacker() is a helper function used by see()
// to locate the least valuable attacker for the side to move,
// remove the attacker we just found from the bitboards and
// scan for new X-ray attacks behind it.
template<PieceT PT>
PieceT Position::least_valuable_attacker (Square dst, Bitboard stm_attackers, Bitboard &occupied, Bitboard &attackers) const
{
    Bitboard bb = stm_attackers & _types_bb[PT];
    if (bb != U64 (0))
    {
        occupied ^= (bb & ~(bb - 1));

        if (PAWN == PT || BSHP == PT || QUEN == PT)
        {
            attackers |= attacks_bb<BSHP> (dst, occupied) & (_types_bb[BSHP]|_types_bb[QUEN]);
        }
        if (ROOK == PT || QUEN == PT)
        {
            attackers |= attacks_bb<ROOK> (dst, occupied) & (_types_bb[ROOK]|_types_bb[QUEN]);
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

// see() is a Static Exchange Evaluator (SEE):
// It tries to estimate the material gain or loss resulting from a move.
Value Position::see      (Move m) const
{
    ASSERT (_ok (m));

    Square org = org_sq (m);
    Square dst = dst_sq (m);

    // side to move
    Color stm = color (_board[org]);

    // Gain list
    Value swap_list[32];
    i08   depth = 1;
    swap_list[0] = PieceValue[MG][ptype (_board[dst])];

    Bitboard occupied = _types_bb[NONE] - org;

    MoveT mt = mtype (m);

    if (mt == CASTLE)
    {
        // Castle moves are implemented as king capturing the rook so cannot be
        // handled correctly. Simply return 0 that is always the correct value
        // unless in the rare case the rook ends up under attack.
        return VALUE_ZERO;
    }
    if (mt == ENPASSANT)
    {
        // Remove the captured pawn
        occupied -= (dst - pawn_push (stm));
        swap_list[0] = PieceValue[MG][PAWN];
    }

    // Find all enemy attackers to the destination square, with the moving piece
    // removed, but possibly an X-ray attacker added behind it.
    Bitboard attackers = attackers_to (dst, occupied) & occupied;

    // If the opponent has no attackers we are finished
    stm = ~stm;
    Bitboard stm_attackers = attackers & _color_bb[stm];
    if (stm_attackers != U64 (0))
    {
        // The destination square is defended, which makes things rather more
        // difficult to compute. We proceed by building up a "swap list" containing
        // the material gain or loss at each stop in a sequence of captures to the
        // destination square, where the sides alternately capture, and always
        // capture with the least valuable piece. After each capture, we look for
        // new X-ray attacks from behind the capturing piece.
        do
        {
            ASSERT (depth < 32);

            // Add the new entry to the swap list
            swap_list[depth] = PieceValue[MG][ptype (_board[org])] - swap_list[depth - 1];

            // Locate and remove the next least valuable attacker
            PieceT captured = least_valuable_attacker<PAWN> (dst, stm_attackers, occupied, attackers);

            // Stop before processing a king capture
            if (KING == captured)
            {
                if (stm_attackers == attackers)
                {
                    ++depth;
                }
                break;
            }

            stm = ~stm;
            stm_attackers = attackers & _color_bb[stm];

            ++depth;
        }
        while (stm_attackers != U64 (0));

        // Having built the swap list, we negamax through it to find the best
        // achievable score from the point of view of the side to move.
        while (--depth > 0)
        {
            if (swap_list[depth - 1] > -swap_list[depth])
            {
                swap_list[depth - 1] = -swap_list[depth];
            }
        }
    }

    return swap_list[0];
}

Value Position::see_sign (Move m) const
{
    ASSERT (_ok (m));

    // Early return if SEE cannot be negative because captured piece value
    // is not less then capturing one. Note that king moves always return
    // here because king midgame value is set to 0.
    if ( PieceValue[MG][ptype (_board[org_sq (m)])]
      <= PieceValue[MG][ptype (_board[dst_sq (m)])]
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
    Bitboard pinners =
        ( (PieceAttacks[ROOK][ksq] & (_types_bb[QUEN]|_types_bb[ROOK]))
        | (PieceAttacks[BSHP][ksq] & (_types_bb[QUEN]|_types_bb[BSHP]))
        ) &  _color_bb[~king_c];

    Bitboard chk_blockers = U64 (0);
    while (pinners != U64 (0))
    {
        Bitboard blocker = Between_bb[ksq][pop_lsq (pinners)] & _types_bb[NONE];
        if (!more_than_one (blocker))
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

    Square org  = org_sq (m);
    Square dst  = dst_sq (m);

    Color pasive = ~_active;

    Piece p = _board[org];

    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if ((EMPTY == p) || (_active != color (p))) return false;

    Rank r_org = rel_rank (_active, org);
    Rank r_dst = rel_rank (_active, dst);

    PieceT pt = ptype (p);
    PieceT ct = NONE;

    Square cap = dst;

    MoveT mt = mtype (m);

    if      (mt == NORMAL)
    {
        // Is not a promotion, so promotion piece must be empty
        if (PAWN != (promote (m) - NIHT))
        {
            return false;
        }
        ct = ptype (_board[cap]);
    }
    else if (mt == CASTLE)
    {
        // Check whether the destination square is attacked by the opponent.
        // Castling moves are checked for legality during move generation.
        if (!( (KING == pt)
            && (R_1 == r_org)
            && (R_1 == r_dst)
            && (_active|ROOK) == _board[dst]
            && (_si->castle_rights & mk_castle_right (_active))
            && (!_si->checkers)
            //&& !castle_impeded (_active)
             )
           )
        {
            return false;
        }

        bool king_side = (dst > org); 
        if (castle_impeded (mk_castle_right (_active, king_side ? CS_K : CS_Q)))
        {
            return false;
        }
        // Castle is always encoded as "King captures friendly Rook"
        ASSERT (dst == castle_rook (mk_castle_right (_active, king_side ? CS_K : CS_Q)));
        dst = rel_sq (_active, king_side ? SQ_G1 : SQ_C1);

        Delta step = (king_side ? DEL_W : DEL_E);
        for (i08 s = dst; s != org; s += step)
        {
            if (attackers_to (Square (s)) & _color_bb[pasive])
            {
                return false;
            }
        }

        //ct = NONE;
        return true;
    }
    else if (mt == ENPASSANT)
    {
        if (!( (PAWN == pt)
            && (_si->en_passant_sq == dst)
            && (R_5 == r_org)
            && (R_6 == r_dst)
            && (empty (dst))
             )
           )
        {
            return false;
        }

        cap += pawn_push (pasive);
        if ((pasive|PAWN) != _board[cap])
        {
            return false;
        }
        ct = PAWN;
    }
    else if (mt == PROMOTE)
    {
        if (!( (PAWN == pt)
            && (R_7 == r_org)
            && (R_8 == r_dst)
             )
           )
        {
            return false;
        }
        ct = ptype (_board[cap]);
    }

    if (KING == ct)
    {
        return false;
    }
    // The destination square cannot be occupied by a friendly piece
    if (_color_bb[_active] & dst)
    {
        return false;
    }

    // Handle the special case of a piece move
    if (PAWN == pt)
    {
        // We have already handled promotion moves, so destination
        // cannot be on the 8th/1st rank.
        if ((R_1 == r_org) || (R_8 == r_org)) return false;
        if ((R_1 == r_dst) || (R_2 == r_dst)) return false;
        if (mt == NORMAL)
        {
            if (R_7 == r_org || R_8 == r_dst) return false;
        }
        
        // Move direction must be compatible with pawn color
        Delta delta = dst - org;
        if ((_active == WHITE) != (delta > DEL_O))
        {
            return false;
        }
        // Proceed according to the delta between the origin and destiny squares.
        switch (delta)
        {
        case DEL_N:
        case DEL_S:
            // Pawn push. The destination square must be empty.
            if (!( (empty (dst))
                && (0 == FileRankDist[_file (dst)][_file (org)])
                 )
               )
            {
                return false;
            }
            break;
        case DEL_NE:
        case DEL_NW:
        case DEL_SE:
        case DEL_SW:
            // The destination square must be occupied by an enemy piece
            // (en passant captures was handled earlier).
            // File distance b/w cap and org must be one, avoids a7h5
            if (!( (NONE != ct)
                && (pasive == color (_board[cap]))
                && (1 == FileRankDist[_file (cap)][_file (org)])
                 )
               )
            {
                return false;
            }
            break;
        case DEL_NN:
        case DEL_SS:
            // Double pawn push. The destination square must be on the fourth
            // rank, and both the destination square and the square between the
            // source and destination squares must be empty.
            if (!( (R_2 == r_org)
                && (R_4 == r_dst)
                && (empty (dst))
                && (empty (dst - pawn_push (_active)))
                && (0 == FileRankDist[_file (dst)][_file (org)])
                 )
               )
            {
                return false;
            }
            break;
        default:
            return false;
        }
        
        //if (!( (PawnAttacks[_active][org] & _color_bb[pasive] & dst   // Not a capture
        //        && (NONE != ct)
        //        && (_active != color (_board[cap])))
        //    || (   (org + pawn_push (_active) == dst)                 // Not a single push
        //        && empty (dst))
        //    || (   (R_2 == r_org)                                     // Not a double push
        //        && (R_4 == r_dst)
        //        && (0 == file_dist (cap, org))
        //        //&& (org + 2*pawn_push (_active) == dst)
        //        && empty (dst)
        //        && empty (dst - pawn_push (_active)))))
        //{
        //    return false;
        //}
        
    }
    else
    {
        if (!(attacks_bb (p, org, _types_bb[NONE]) & dst)) return false;
    }

    // Evasions generator already takes care to avoid some kind of illegal moves
    // and legal() relies on this. So we have to take care that the
    // same kind of moves are filtered out here.
    Bitboard chkrs = checkers ();
    if (chkrs != U64 (0))
    {
        if (KING == pt)
        {
            // In case of king moves under check, remove king so to catch
            // as invalid moves like B1A1 when opposite queen is on C1.
            if (attackers_to (dst, _types_bb[NONE] - org) & _color_bb[pasive])
            {
                return false;
            }
        }
        else
        {
            // Double check? In this case a king move is required
            if (more_than_one (chkrs)) return false;
            if ((PAWN == pt) && (ENPASSANT == mt))
            {
                // Move must be a capture of the checking en-passant pawn
                // or a blocking evasion of the checking piece
                if (!((chkrs & cap) || (Between_bb[scan_lsq (chkrs)][_piece_list[_active][KING][0]] & dst)))
                {
                    return false;
                }
            }
            else
            {
                // Move must be a capture or a blocking evasion of the checking piece
                if (!((chkrs | Between_bb[scan_lsq (chkrs)][_piece_list[_active][KING][0]]) & dst))
                {
                    return false;
                }
            }
        }
    }

    return true;
}

// legal() tests whether a pseudo-legal move is legal
bool Position::legal        (Move m, Bitboard pinned) const
{
    ASSERT (_ok (m));
    ASSERT (pinned == pinneds (_active));

    Square org  = org_sq (m);
    Square dst  = dst_sq (m);

    Color pasive = ~_active;

    PieceT pt = ptype (_board[org]);
    ASSERT ((_active == color (_board[org])) && (NONE != pt));

    Square ksq = _piece_list[_active][KING][0];

    MoveT mt = mtype (m);

    if      (mt == NORMAL
        ||   mt == PROMOTE)
    {
        // If the moving piece is a king.
        if (KING == pt)
        {
            // In case of king moves under check we have to remove king so to catch
            // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
            // check whether the destination square is attacked by the opponent.
            return !(attackers_to (dst, _types_bb[NONE] - org) & _color_bb[pasive]); // Remove 'org' but not place 'dst'
        }

        // A non-king move is legal if and only if it is not pinned or
        // it is moving along the ray towards or away from the king or
        // it is a blocking evasion or a capture of the checking piece.
        return  (pinned == U64 (0))
            || !(pinned & org)
            || sqrs_aligned (org, dst, ksq);
    }
    else if (mt == CASTLE)
    {
        // Castling moves are checked for legality during move generation.
        return (KING == pt);
    }
    else if (mt == ENPASSANT)
    {
        // En-passant captures are a tricky special case. Because they are rather uncommon,
        // we do it simply by testing whether the king is attacked after the move is made.
        Square cap = dst + pawn_push (pasive);

        ASSERT (dst == _si->en_passant_sq);
        ASSERT ((_active|PAWN) == _board[org]);
        ASSERT (( pasive|PAWN) == _board[cap]);
        ASSERT (empty (dst));

        Bitboard mocc = _types_bb[NONE] - org - cap + dst;
        // If any attacker then in check & not legal
        return !( (attacks_bb<ROOK> (ksq, mocc) & (_color_bb[pasive]&(_types_bb[QUEN]|_types_bb[ROOK])))
               || (attacks_bb<BSHP> (ksq, mocc) & (_color_bb[pasive]&(_types_bb[QUEN]|_types_bb[BSHP])))
                );
    }

    ASSERT (false);
    return false;
}

// gives_check() tests whether a pseudo-legal move gives a check
bool Position::gives_check  (Move m, const CheckInfo &ci) const
{
    Square org = org_sq (m);
    Square dst = dst_sq (m);

    ASSERT (color (_board[org]) == _active);
    ASSERT (ci.discoverers == discoverers (_active));

    PieceT pt = ptype (_board[org]);

    // Direct check ?
    if (ci.checking_bb[pt] & dst) return true;

    // Discovery check ?
    if (UNLIKELY (ci.discoverers) && ci.discoverers & org)
    {
        if (!sqrs_aligned (org, dst, ci.king_sq)) return true;
    }

    MoveT mt  = mtype (m);
    // Can we skip the ugly special cases ?
    if (mt == NORMAL) return false;

    const Bitboard occ = _types_bb[NONE];

    if      (mt == CASTLE)
    {
        // Castling with check ?
        bool  king_side = (dst > org);
        Square org_rook = dst; // 'King captures the rook' notation
        dst             = rel_sq (_active, king_side ? SQ_G1 : SQ_C1);
        Square dst_rook = rel_sq (_active, king_side ? SQ_F1 : SQ_D1);

        return (PieceAttacks[ROOK][dst_rook] & ci.king_sq) // First x-ray check then full check
            && (attacks_bb<ROOK> (dst_rook, (occ - org - org_rook + dst + dst_rook)) & ci.king_sq);
    }
    else if (mt == ENPASSANT)
    {
        // En passant capture with check ?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        Square cap = _file (dst) | _rank (org);
        Bitboard mocc = occ - org - cap + dst;
        // if any attacker then in check
        return ( (attacks_bb<ROOK> (ci.king_sq, mocc) & (_color_bb[_active]&(_types_bb[QUEN]|_types_bb[ROOK])))
              || (attacks_bb<BSHP> (ci.king_sq, mocc) & (_color_bb[_active]&(_types_bb[QUEN]|_types_bb[BSHP])))
               );
    }
    else if (mt == PROMOTE)
    {
        // Promotion with check ?
        return (attacks_bb (Piece (promote (m)), dst, occ - org + dst) & ci.king_sq);
    }

    ASSERT (false);
    return false;
}

//// gives_checkmate() tests whether a pseudo-legal move gives a checkmate
//bool Position::gives_checkmate (Move m, const CheckInfo &ci) const
//{
//    if (gives_check (m, ci))
//    {
//        Position pos = *this;
//        StateInfo si;
//        pos.do_move (m, si, &ci);
//        return (MoveList<LEGAL> (pos).size () == 0);
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
        _index[s] = -1;
    }
    for (i08 c = WHITE; c <= BLACK; ++c)
    {
        for (u08 pt = PAWN; pt <= KING; ++pt)
        {
            for (i08 i = 0; i < 16; ++i)
            {
                _piece_list[c][pt][i] = SQ_NO;
            }
        }
    }

    //fill (_castle_rook, _castle_rook + sizeof (_castle_rook) / sizeof (*_castle_rook), SQ_NO);
    memset (_castle_rook, SQ_NO, sizeof (_castle_rook));

    //_game_ply   = 1;

    _sb.en_passant_sq = SQ_NO;
    _sb.capture_type  = NONE;
    _si = &_sb;
}
// setup() sets the fen on the position
bool Position::setup (const string &f, Thread *th, bool c960, bool full)
{
    //Position pos (0);
    //if (parse (pos, f, th, c960, full) && pos.ok ())
    //{
    //    *this = pos;
    //    return true;
    //}
    //return false;

    return parse (*const_cast<Position*> (this), f, th, c960, full);
}

// set_castle() set the castling for the particular color & rook
void Position::set_castle (Color c, Square org_rook)
{
    Square org_king = _piece_list[c][KING][0];
    ASSERT (org_king != org_rook);

    bool king_side = (org_rook > org_king);
    CRight cr = mk_castle_right (c, king_side ? CS_K : CS_Q);
    Square dst_rook = rel_sq (c, king_side ? SQ_F1 : SQ_D1);
    Square dst_king = rel_sq (c, king_side ? SQ_G1 : SQ_C1);

    _si->castle_rights     |= cr;

    _castle_mask[org_king] |= cr;
    _castle_mask[org_rook] |= cr;
    _castle_rook[cr] = org_rook;

    for (i08 s = min (org_rook, dst_rook); s <= max (org_rook, dst_rook); ++s)
    {
        if (org_king != s && org_rook != s)
        {
            _castle_path[cr] += Square (s);
        }
    }
    for (i08 s = min (org_king, dst_king); s <= max (org_king, dst_king); ++s)
    {
        if (org_king != s && org_rook != s)
        {
            _castle_path[cr] += Square (s);
        }
    }
}
// can_en_passant() tests the en-passant square
bool Position::can_en_passant (Square ep_sq) const
{
    ASSERT (_ok (ep_sq));

    Color pasive = ~_active;
    ASSERT (R_6 == rel_rank (_active, ep_sq));

    Square cap = ep_sq + pawn_push (pasive);
    if (!((_color_bb[pasive]&_types_bb[PAWN]) & cap)) return false;
    //if ((pasive | PAWN) != _board[cap]) return false;

    Bitboard ep_pawns = PawnAttacks[pasive][ep_sq] & _color_bb[_active]&_types_bb[PAWN];
    ASSERT (pop_count<FULL> (ep_pawns) <= 2);
    if (ep_pawns == U64 (0)) return false;

    vector<Move> ep_mlist;
    while (ep_pawns != U64 (0))
    {
        ep_mlist.push_back (mk_move<ENPASSANT> (pop_lsq (ep_pawns), ep_sq));
    }

    // Check en-passant is legal for the position
    Square   ksq = _piece_list[_active][KING][0];
    const Bitboard occ = _types_bb[NONE];
    for (vector<Move>::const_iterator itr = ep_mlist.begin (); itr != ep_mlist.end (); ++itr)
    {
        Move m = *itr;
        Bitboard mocc = occ - org_sq (m) - cap + dst_sq (m);
        if (!( (attacks_bb<ROOK> (ksq, mocc) & (_color_bb[pasive]&(_types_bb[QUEN]|_types_bb[ROOK])))
            || (attacks_bb<BSHP> (ksq, mocc) & (_color_bb[pasive]&(_types_bb[QUEN]|_types_bb[BSHP])))
             )
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
    Score score  = SCORE_ZERO;
    Bitboard occ = _types_bb[NONE];
    while (occ != U64 (0))
    {
        Square s = pop_lsq (occ);
        score += PSQ[color (_board[s])][ptype (_board[s])][s];
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
    for (u08 pt = NIHT; pt <= QUEN; ++pt)
    {
        value += PieceValue[MG][pt] * i32 (_piece_count[c][pt]);
    }
    return value;
}

// do_move() do the move with checking info
void Position::  do_move (Move m, StateInfo &si, const CheckInfo *ci)
{
    ASSERT (_ok (m));
    ASSERT (&si != _si);

    Key p_key = _si->posi_key;

    // Copy some fields of old state to new StateInfo object except the ones
    // which are going to be recalculated from scratch anyway, 
    memcpy (&si, _si, STATE_COPY_SIZE);

    // Switch state pointer to point to the new, ready to be updated, state.
    si.p_si = _si;
    _si     = &si;

    Color pasive = ~_active;

    Square org  = org_sq (m);
    Square dst  = dst_sq (m);
    PieceT pt   = ptype (_board[org]);

    ASSERT ((!empty (org))
        &&  (_active == color (_board[org]))
        &&  (NONE != pt));
    
    MoveT mt   = mtype (m);
    
    ASSERT ((empty (dst))
        ||  (pasive == color (_board[dst]))
        ||  (CASTLE == mt));

    Square cap = dst;
    PieceT  ct = NONE;

    // Pick capture piece and check validation
    if      (mt == NORMAL)
    {
        ASSERT (PAWN == (promote (m) - NIHT));

        if (PAWN == pt)
        {
            u08 f_del = file_dist (cap, org);
            ASSERT (0 == f_del || 1 == f_del);
            if (1 == f_del) ct = ptype (_board[cap]);
        }
        else
        {
            ct = ptype (_board[cap]);
        }
    }
    else if (mt == CASTLE)
    {
        ASSERT (KING == pt);
        ASSERT (ROOK == ptype (_board[dst]));

        ct = NONE;
    }
    else if (mt == ENPASSANT)
    {
        ASSERT (PAWN == pt);                // Moving type must be pawn
        ASSERT (dst == _si->en_passant_sq); // Destination must be en-passant
        ASSERT (R_5 == rel_rank (_active, org));
        ASSERT (R_6 == rel_rank (_active, dst));
        ASSERT (empty (cap));      // Capture Square must be empty

        cap += pawn_push (pasive);
        ASSERT ((pasive | PAWN) == _board[cap]);
        ct = PAWN;
    }
    else if (mt == PROMOTE)
    {
        ASSERT (PAWN == pt);        // Moving type must be PAWN
        ASSERT (R_7 == rel_rank (_active, org));
        ASSERT (R_8 == rel_rank (_active, dst));

        ct = ptype (_board[cap]);
        ASSERT (PAWN != ct);
    }

    ASSERT (KING != ct);   // can't capture the KING

    // ------------------------

    // Handle all captures
    if (NONE != ct)
    {
        // Remove captured piece
        remove_piece (cap);
        // If the captured piece is a pawn
        if (PAWN == ct) // Update pawn hash key
        {
            _si->pawn_key ^= Zob._.piecesq[pasive][PAWN][cap];
        }
        else            // Update non-pawn material
        {
            _si->non_pawn_matl[pasive] -= PieceValue[MG][ct];
        }
        // Update Hash key of material situation
        _si->matl_key ^= Zob._.piecesq[pasive][ct][_piece_count[pasive][ct]];

        // Update prefetch access to material_table
        prefetch ((char *) _thread->material_table[_si->matl_key]);

        // Update Hash key of position
        p_key ^= Zob._.piecesq[pasive][ct][cap];
        // Update incremental scores
        _si->psq_score -= PSQ[pasive][ct][cap];
        // Reset Rule-50 draw counter
        _si->clock50 = 0;
    }
    else
    {
        if (PAWN == pt)
        {
            _si->clock50 = 0;
        }
        else
        {
            _si->clock50++;
        }
    }

    // Reset old en-passant square
    if (SQ_NO != _si->en_passant_sq)
    {
        p_key ^= Zob._.en_passant[_file (_si->en_passant_sq)];
        _si->en_passant_sq = SQ_NO;
    }

    // Do move according to move type
    if      (mt == NORMAL
        ||   mt == ENPASSANT)
    {
        // Move the piece
        move_piece (org, dst);

        // Update pawns hash key
        if (PAWN == pt)
        {
            _si->pawn_key ^=
                Zob._.piecesq[_active][PAWN][org] ^
                Zob._.piecesq[_active][PAWN][dst];
        }
        
        p_key ^= Zob._.piecesq[_active][pt][org] ^ Zob._.piecesq[_active][pt][dst];
        
        _si->psq_score += PSQ[_active][pt][dst] - PSQ[_active][pt][org];
    }
    else if (mt == CASTLE)
    {
        Square org_rook, dst_rook;
        do_castling<true> (org, dst, org_rook, dst_rook);

        p_key ^= Zob._.piecesq[_active][KING][org     ] ^ Zob._.piecesq[_active][KING][dst     ];
        p_key ^= Zob._.piecesq[_active][ROOK][org_rook] ^ Zob._.piecesq[_active][ROOK][dst_rook];

        _si->psq_score += PSQ[_active][KING][dst     ] - PSQ[_active][KING][org     ];
        _si->psq_score += PSQ[_active][ROOK][dst_rook] - PSQ[_active][ROOK][org_rook];
    }
    else if (mt == PROMOTE)
    {
        PieceT ppt = promote (m);
        // Replace the PAWN with the Promoted piece
        remove_piece (org);
        place_piece (dst, _active, ppt);

        _si->matl_key ^=
            Zob._.piecesq[_active][PAWN][_piece_count[_active][PAWN]] ^
            Zob._.piecesq[_active][ppt][_piece_count[_active][ppt] - 1];

        _si->pawn_key ^= Zob._.piecesq[_active][PAWN][org];

        p_key ^= Zob._.piecesq[_active][PAWN][org] ^ Zob._.piecesq[_active][ppt][dst];

        // Update incremental score
        _si->psq_score += PSQ[_active][ppt][dst] - PSQ[_active][PAWN][org];
        // Update material
        _si->non_pawn_matl[_active] += PieceValue[MG][ppt];
    }

    // Update castle rights if needed
    u08 cr = _si->castle_rights & (_castle_mask[org] | _castle_mask[dst]);
    if (cr)
    {
        Bitboard b = cr;
        _si->castle_rights &= ~cr;
        while (b != U64 (0))
        {
            p_key ^= Zob._.castle_right[0][pop_lsq (b)];
        }
    }

    // Update checkers bitboard: piece must be already moved due to attacks_bb()
    _si->checkers = U64 (0);
    if (ci != NULL)
    {
        if (mt == NORMAL)
        {
            // Direct check ?
            if (ci->checking_bb[pt] & dst)
            {
                _si->checkers += dst;
            }
            // Discovery check ?
            if (QUEN != pt)
            {
                if (UNLIKELY(ci->discoverers) && (ci->discoverers & org))
                {
                    if (ROOK != pt)
                    {
                        _si->checkers |=
                            attacks_bb<ROOK> (_piece_list[pasive][KING][0], _types_bb[NONE]) &
                            (_color_bb[_active]&(_types_bb[QUEN]|_types_bb[ROOK]));
                    }
                    if (BSHP != pt)
                    {
                        _si->checkers |=
                            attacks_bb<BSHP> (_piece_list[pasive][KING][0], _types_bb[NONE]) &
                            (_color_bb[_active]&(_types_bb[QUEN]|_types_bb[BSHP]));
                    }
                }
            }
        }
        else
        {
            _si->checkers = attackers_to (_piece_list[pasive][KING][0]) & _color_bb[_active];
        }
    }

    // Switch side to move
    _active = pasive;
    p_key ^= Zob._.mover_side;

    // Handle pawn en-passant square setting
    if (PAWN == pt)
    {
        u08 iorg = org;
        u08 idst = dst;
        if (16 == (idst ^ iorg))
        {
            Square ep_sq = Square ((idst + iorg) / 2);
            if (can_en_passant (ep_sq))
            {
                _si->en_passant_sq = ep_sq;
                p_key ^= Zob._.en_passant[_file (ep_sq)];
            }
        }

        // Update prefetch access to pawns_table
        prefetch ((char *) _thread->pawns_table[_si->pawn_key]);
    }

    // Prefetch TT access as soon as we know the new hash key
    prefetch ((char*) TT.cluster_entry (p_key));

    // Update the key with the final value
    _si->posi_key     = p_key;
    _si->capture_type = ct;
    _si->last_move    = m;
    ++_si->null_ply;
    ++_game_ply;
    ++_game_nodes;

    ASSERT (ok ());
}
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
    ASSERT (_si->p_si);
    Move m = _si->last_move;
    ASSERT (_ok (m));

    Square org  = org_sq (m);
    Square dst  = dst_sq (m);

    _active = ~_active;

    MoveT mt = mtype (m);
    ASSERT (empty (org) || (mt == CASTLE));

    ASSERT (KING != _si->capture_type);

    Square cap = dst;

    // Undo move according to move type
    if      (mt == NORMAL)
    {
        move_piece (dst, org); // Put the piece back at the origin square
    }
    else if (mt == CASTLE)
    {
        Square org_rook, dst_rook;
        do_castling<false> (org, dst, org_rook, dst_rook);
        //ct  = NONE;
    }
    else if (mt == ENPASSANT)
    {
        ASSERT (PAWN == ptype (_board[dst]));
        ASSERT (PAWN == _si->capture_type);
        ASSERT (R_5 == rel_rank (_active, org));
        ASSERT (R_6 == rel_rank (_active, dst));
        ASSERT (dst == _si->p_si->en_passant_sq);
        cap -= pawn_push (_active);
        ASSERT (empty (cap));
        move_piece (dst, org); // Put the piece back at the origin square
    }
    else if (mt == PROMOTE)
    {
        ASSERT (promote (m) == ptype (_board[dst]));
        ASSERT (R_8 == rel_rank (_active, dst));
        ASSERT (NIHT <= promote (m) && promote (m) <= QUEN);
        // Replace the promoted piece with the PAWN
        remove_piece (dst);
        place_piece (org, _active, PAWN);
        //pt = PAWN;
    }

    // If there was any capture piece
    if (NONE != _si->capture_type)
    {
        place_piece (cap, ~_active, _si->capture_type); // Restore the captured piece
    }

    --_game_ply;
    // Finally point our state pointer back to the previous state
    _si     = _si->p_si;

    ASSERT (ok ());
}

// do_null_move() do the null-move
void Position::  do_null_move (StateInfo &si)
{
    ASSERT (&si != _si);
    ASSERT (!_si->checkers);

    // Full copy here
    memcpy (&si, _si, sizeof (si));

    // Switch our state pointer to point to the new, ready to be updated, state.
    si.p_si = _si;
    _si     = &si;

    if (SQ_NO != _si->en_passant_sq)
    {
        _si->posi_key ^= Zob._.en_passant[_file (_si->en_passant_sq)];
        _si->en_passant_sq = SQ_NO;
    }

    _active = ~_active;
    _si->posi_key ^= Zob._.mover_side;

    prefetch ((char *) TT.cluster_entry (_si->posi_key));

    _si->clock50++;
    _si->null_ply = 0;

    ASSERT (ok ());
}
// undo_null_move() undo the last null-move
void Position::undo_null_move ()
{
    ASSERT (_si->p_si);
    ASSERT (!_si->checkers);

    _active = ~_active;
    _si     = _si->p_si;

    ASSERT (ok ());
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
        fen_.insert (0, s + (fen_.empty () ? " " : "/"));
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
    fen_ += ((s == "-") ? s : s.replace (1, 1, (s[1] == '3') ? "6" : (s[1] == '6') ? "3" : "-"));
    // 5-6. Half and full moves
    getline (ss, s);
    fen_ += s;

    setup (fen_, _thread, _chess960);

    ASSERT (ok ());
}

string Position::fen (bool c960, bool full) const
{
    ostringstream oss;

    for (i08 r = R_8; r >= R_1; --r)
    {
        for (i08 f = F_A; f <= F_H; ++f)
        {
            Square s = File (f) | Rank (r);
            i16 empty_count = 0;
            while (F_H >= f && empty (s))
            {
                ++empty_count;
                ++f;
                ++s;
            }
            if (empty_count) oss << empty_count;
            if (F_H >= f)  oss << PieceChar[_board[s]];
        }

        if (R_1 < r) oss << '/';
    }

    oss << " " << ColorChar[_active] << " ";

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
                if (can_castle (CR_WK)) oss << 'K';
                if (can_castle (CR_WQ)) oss << 'Q';
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_BK)) oss << 'k';
                if (can_castle (CR_BQ)) oss << 'q';
            }
        }
    }
    else
    {
        oss << '-';
    }

    oss << " " << ((SQ_NO == _si->en_passant_sq) ? "-" : to_string (_si->en_passant_sq)) << " ";

    if (full) oss << i16 (_si->clock50) << " " << game_move ();

    return oss.str ();
}

// string() returns an ASCII representation of the position to be
// printed to the standard output
Position::operator string () const
{
    const string edge  = " +---+---+---+---+---+---+---+---+\n";
    const string row_1 = "| . |   | . |   | . |   | . |   |\n" + edge;
    const string row_2 = "|   | . |   | . |   | . |   | . |\n" + edge;
    const u16 row_len  = row_1.length () + 1;

    string board = edge;

    for (i08 r = R_8; r >= R_1; --r)
    {
        board += to_char (Rank (r)) + ((r % 2) ? row_1 : row_2);
    }
    for (i08 f = F_A; f <= F_H; ++f)
    {
        board += "   ";
        board += to_char (File (f), false);
    }

    Bitboard occ = _types_bb[NONE];
    while (occ != U64 (0))
    {
        Square s = pop_lsq (occ);
        i08 r = _rank (s);
        i08 f = _file (s);
        board[3 + row_len * (7.5 - r) + 4 * f] = PieceChar[_board[s]];
    }

    ostringstream oss;

    oss << board << "\n\n";

    oss << "Fen: " << fen () << "\n"
        << "Key: " << hex << uppercase << setfill ('0') << setw (16) 
        << _si->posi_key << dec << "\n";

    oss << "Checkers: ";
    Bitboard chkrs = checkers ();
    if (chkrs != U64 (0))
    {
        while (chkrs != U64 (0))
        {
            Square chk = pop_lsq (chkrs);
            oss << PieceChar[ptype (_board[chk])] << to_string (chk) << " ";
        }
    }
    else
    {
        oss << "<none>";
    }
    oss << "\n";
    
    MoveList<LEGAL> ms (*this);
    oss << "Legal moves (" << ms.size () << "): ";
    for ( ; *ms; ++ms)
    {
        oss << move_to_san (*ms, *const_cast<Position*> (this)) << " ";
    }

    return oss.str ();
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
bool Position::parse (Position &pos, const string &fen, Thread *thread, bool c960, bool full)
{
    if (fen.empty ()) return false;

    pos.clear ();

    istringstream iss (fen);
    unsigned char ch;

    iss >> noskipws;

    // 1. Piece placement on Board
    size_t idx;
    Square s = SQ_A8;
    while ((iss >> ch) && !isspace (ch))
    {
        if (isdigit (ch))
        {
            s += Delta (ch - '0'); // Advance the given number of files
        }
        else if (isalpha (ch) && (idx = PieceChar.find (ch)) != string::npos)
        {
            Piece p = Piece (idx);
            pos.place_piece (s, color (p), ptype (p));
            ++s;
        }
        else if (ch == '/')
        {
            s += DEL_SS;
        }
        else
        {
            return false;
        }
    }

    // 2. Active color
    iss >> ch;
    pos._active = Color (ColorChar.find (ch));

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
            Square rook;
            Color c = isupper (ch) ? WHITE : BLACK;
            char sym = tolower (ch);
            if ('a' <= sym && sym <= 'h')
            {
                rook = (to_file (sym) | rel_rank (c, R_1));
                //if (ROOK != ptype (pos[rook])) return false;
                pos.set_castle (c, rook);
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
            i08 rook;
            Color c = isupper (ch) ? WHITE : BLACK;
            switch (toupper (ch))
            {
            case 'K':
                rook = rel_sq (c, SQ_H1);
                while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != ptype (pos[Square (rook)]))) --rook;
                break;
            case 'Q':
                rook = rel_sq (c, SQ_A1);
                while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != ptype (pos[Square (rook)]))) ++rook;
                break;
            default:
                continue;
            }

            //if (ROOK != ptype (pos[rook])) return false;
            pos.set_castle (c, Square (rook));
        }
    }

    // 4. En-passant square. Ignore if no pawn capture is possible
    char col, row;
    if (   ((iss >> col) && (col >= 'a' && col <= 'h'))
        && ((iss >> row) && (row == '3' || row == '6')))
    {
        if (!( (WHITE == pos._active && '6' != row)
            || (BLACK == pos._active && '3' != row)))
        {
            Square ep_sq = to_square (col, row);
            if (pos.can_en_passant (ep_sq))
            {
                pos._si->en_passant_sq = ep_sq;
            }
        }
    }

    // 5-6. 50-move clock and game-move count
    i32 clk50 = 0, g_move = 1;
    if (full)
    {
        iss >> skipws >> clk50 >> g_move;
        // Rule 50 draw case
        if (100 < clk50) return false;
        if (0 >= g_move) g_move = 1;
    }

    // Convert from game_move starting from 1 to game_ply starting from 0,
    // handle also common incorrect FEN with game_move = 0.
    pos._si->clock50 = (SQ_NO != pos._si->en_passant_sq) ? 0 : clk50;
    pos._game_ply = max (2 * (g_move - 1), 0) + (BLACK == pos._active);

    pos._si->matl_key = Zob.compute_matl_key (pos);
    pos._si->pawn_key = Zob.compute_pawn_key (pos);
    pos._si->posi_key = Zob.compute_posi_key (pos);
    pos._si->psq_score = pos.compute_psq_score ();
    pos._si->non_pawn_matl[WHITE] = pos.compute_non_pawn_material (WHITE);
    pos._si->non_pawn_matl[BLACK] = pos.compute_non_pawn_material (BLACK);
    pos._si->checkers = pos.checkers (pos._active);
    pos._chess960     = c960;
    pos._game_nodes   = 0;
    pos._thread       = thread;

    return true;
}
