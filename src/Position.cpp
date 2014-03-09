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

const string CharPiece ("PNBRQK  pnbrqk");
const string CharColor ("wb-");

const Value PieceValue[PHASE_NO][TOTL] =
{
    { VALUE_MG_PAWN, VALUE_MG_KNIGHT, VALUE_MG_BISHOP, VALUE_MG_ROOK, VALUE_MG_QUEEN, VALUE_ZERO, VALUE_ZERO },
    { VALUE_EG_PAWN, VALUE_EG_KNIGHT, VALUE_EG_BISHOP, VALUE_EG_ROOK, VALUE_EG_QUEEN, VALUE_ZERO, VALUE_ZERO }
};


//const char *const FEN_N = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
//const char *const FEN_X = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1";
const string FEN_N ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
const string FEN_X ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1");


#ifndef NDEBUG
bool _ok (const   char *fen, bool c960, bool full)
{
    if (!fen)   return false;
    Position pos (0);
    return Position::parse (pos, fen, NULL, c960, full) && pos.ok ();
}
#endif

bool _ok (const string &fen, bool c960, bool full)
{
    if (fen.empty ()) return false;
    Position pos (0);
    return Position::parse (pos, fen, NULL, c960, full) && pos.ok ();
}


namespace {

    // do_move() copy current state info up to 'posi_key' excluded to the new one.
    // calculate the quad words (64bits) needed to be copied.
    const uint8_t STATE_COPY_SIZE = offsetof (StateInfo, posi_key);

    CACHE_ALIGN(64) Score PSQ[CLR_NO][NONE][SQ_NO];

#define S(mg, eg) mk_score (mg, eg)

    // PSQT[PieceType][Square] contains Piece-Square scores. For each piece type on
    // a given square a (midgame, endgame) score pair is assigned. PSQT is defined
    // for white side, for black side the tables are symmetric.
    const Score PSQT[NONE][SQ_NO] =
    {
        // Pawn
        {S(  0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(  0, 0),
        S(-20, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(-20, 0),
        S(-20, 0), S( 0, 0), S(10, 0), S(20, 0), S(20, 0), S(10, 0), S( 0, 0), S(-20, 0),
        S(-20, 0), S( 0, 0), S(20, 0), S(40, 0), S(40, 0), S(20, 0), S( 0, 0), S(-20, 0),
        S(-20, 0), S( 0, 0), S(10, 0), S(20, 0), S(20, 0), S(10, 0), S( 0, 0), S(-20, 0),
        S(-20, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(-20, 0),
        S(-20, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(-20, 0),
        S(  0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(  0, 0),
        },
        // Knight
        {S(-134,-98), S(-99,-83), S(-75,-51), S(-63,-16), S(-63,-16), S(-75,-51), S(-99,-83), S(-134,-98),
        S( -78,-68), S(-43,-53), S(-19,-21), S( -7, 14), S( -7, 14), S(-19,-21), S(-43,-53), S( -78,-68),
        S( -59,-53), S(-24,-38), S(  0, -6), S( 12, 29), S( 12, 29), S(  0, -6), S(-24,-38), S( -59,-53),
        S( -18,-42), S( 17,-27), S( 41,  5), S( 53, 40), S( 53, 40), S( 41,  5), S( 17,-27), S( -18,-42),
        S( -20,-42), S( 15,-27), S( 39,  5), S( 51, 40), S( 51, 40), S( 39,  5), S( 15,-27), S( -20,-42),
        S(   0,-53), S( 35,-38), S( 59, -6), S( 71, 29), S( 71, 29), S( 59, -6), S( 35,-38), S(   0,-53),
        S( -54,-68), S(-19,-53), S(  5,-21), S( 17, 14), S( 17, 14), S(  5,-21), S(-19,-53), S( -54,-68),
        S(-190,-98), S(-55,-83), S(-31,-51), S(-19,-16), S(-19,-16), S(-31,-51), S(-55,-83), S(-190,-98),
        },
        // Bishop
        {S(-44,-65), S(-17,-42), S(-24,-44), S(-33,-26), S(-33,-26), S(-24,-44), S(-17,-42), S(-44,-65),
        S(-19,-43), S(  8,-20), S(  1,-22), S( -8, -4), S( -8, -4), S(  1,-22), S(  8,-20), S(-19,-43),
        S(-10,-33), S( 17,-10), S( 10,-12), S(  1,  6), S(  1,  6), S( 10,-12), S( 17,-10), S(-10,-33),
        S( -9,-35), S( 18,-12), S( 11,-14), S(  2,  4), S(  2,  4), S( 11,-14), S( 18,-12), S( -9,-35),
        S(-12,-35), S( 15,-12), S(  8,-14), S( -1,  4), S( -1,  4), S(  8,-14), S( 15,-12), S(-12,-35),
        S(-18,-33), S(  9,-10), S(  2,-12), S( -7,  6), S( -7,  6), S(  2,-12), S(  9,-10), S(-18,-33),
        S(-22,-43), S(  5,-20), S( -2,-22), S(-11, -4), S(-11, -4), S( -2,-22), S(  5,-20), S(-22,-43),
        S(-39,-65), S(-12,-42), S(-19,-44), S(-28,-26), S(-28,-26), S(-19,-44), S(-12,-42), S(-39,-65),
        },
        // Rook
        {S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        },
        // Queen
        {S(8,-80), S(8,-54), S(8,-42), S(8,-30), S(8,-30), S(8,-42), S(8,-54), S(8,-80),
        S(8,-54), S(8,-30), S(8,-18), S(8, -6), S(8, -6), S(8,-18), S(8,-30), S(8,-54),
        S(8,-42), S(8,-18), S(8, -6), S(8,  6), S(8,  6), S(8, -6), S(8,-18), S(8,-42),
        S(8,-30), S(8, -6), S(8,  6), S(8, 18), S(8, 18), S(8,  6), S(8, -6), S(8,-30),
        S(8,-30), S(8, -6), S(8,  6), S(8, 18), S(8, 18), S(8,  6), S(8, -6), S(8,-30),
        S(8,-42), S(8,-18), S(8, -6), S(8,  6), S(8,  6), S(8, -6), S(8,-18), S(8,-42),
        S(8,-54), S(8,-30), S(8,-18), S(8, -6), S(8, -6), S(8,-18), S(8,-30), S(8,-54),
        S(8,-80), S(8,-54), S(8,-42), S(8,-30), S(8,-30), S(8,-42), S(8,-54), S(8,-80),
        },
        // King
        {S(298, 27), S(332, 81), S(273,108), S(225,116), S(225,116), S(273,108), S(332, 81), S(298, 27),
        S(287, 74), S(321,128), S(262,155), S(214,163), S(214,163), S(262,155), S(321,128), S(287, 74),
        S(224,111), S(258,165), S(199,192), S(151,200), S(151,200), S(199,192), S(258,165), S(224,111),
        S(196,135), S(230,189), S(171,216), S(123,224), S(123,224), S(171,216), S(230,189), S(196,135),
        S(173,135), S(207,189), S(148,216), S(100,224), S(100,224), S(148,216), S(207,189), S(173,135),
        S(146,111), S(180,165), S(121,192), S( 73,200), S( 73,200), S(121,192), S(180,165), S(146,111),
        S(119, 74), S(153,128), S( 94,155), S( 46,163), S( 46,163), S( 94,155), S(153,128), S(119, 74),
        S( 98, 27), S(132, 81), S( 73,108), S( 25,116), S( 25,116), S( 73,108), S(132, 81), S( 98, 27),
        },
    };

#undef S

    // min_attacker() is an helper function used by see() to locate the least
    // valuable attacker for the side to move, remove the attacker we just found
    // from the bitboards and scan for new X-ray attacks behind it.
    template<int8_t PT>
    INLINE PieceT min_attacker       (const Bitboard bb[], const Square &dst, const Bitboard &stm_attackers, Bitboard &occupied, Bitboard &attackers)
    {
        Bitboard b = stm_attackers & bb[PT];
        if (b)
        {
            occupied -= (b & ~(b - 1));

            if (PAWN == PT || BSHP == PT || QUEN == PT)
            {
                attackers |= attacks_bb<BSHP> (dst, occupied) & (bb[BSHP] | bb[QUEN]);
            }
            if (ROOK == PT || QUEN == PT)
            {
                attackers |= attacks_bb<ROOK> (dst, occupied) & (bb[ROOK] | bb[QUEN]);
            }
            attackers &= occupied; // After X-ray that may add already processed pieces

            return PieceT (PT);
        }

        return min_attacker<PT+1> (bb, dst, stm_attackers, occupied, attackers);
    }

    template<>
    INLINE PieceT min_attacker<KING> (const Bitboard [], const Square &, const Bitboard &, Bitboard &, Bitboard &)
    {
        return KING; // No need to update bitboards, it is the last cycle
    }

    // prefetch() preloads the given address in L1/L2 cache.
    // This is a non-blocking function that doesn't stall
    // the CPU waiting for data to be loaded from memory,
    // which can be quite slow.
#ifdef PREFETCH

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)

#   include <xmmintrin.h> // Intel and Microsoft header for _mm_prefetch()

    inline void prefetch (char *addr)
    {

#   if defined(__INTEL_COMPILER)
        {
            // This hack prevents prefetches from being optimized away by
            // Intel compiler. Both MSVC and gcc seem not be affected by this.
            __asm__ ("");
        }
#   endif

        _mm_prefetch (addr, _MM_HINT_T0);
    }

#else

    inline void prefetch (char *addr)
    {
        __builtin_prefetch (addr);
    }

#endif

#else

    inline void prefetch (char *) {}

#endif

    char toggle_case (char c) { return char (islower (c) ? toupper (c) : tolower (c)); }

} // namespace

uint8_t Position::fifty_move_distance;

void Position::initialize ()
{
    fifty_move_distance = 2 * int32_t (*(Options["Fifty Move Distance"]));  

    for (PieceT pt = PAWN; pt <= KING; ++pt)
    {
        Score score = mk_score (PieceValue[MG][pt], PieceValue[EG][pt]);

        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            Score psq_score = score + PSQT[pt][s];
            PSQ[WHITE][pt][ s] = +psq_score;
            PSQ[BLACK][pt][~s] = -psq_score;
        }
    }
}

// operator= (pos), copy the 'pos'.
// The new born Position object should not depend on any external data
// so that why detach the state info pointer from the source one.
Position& Position::operator= (const Position &pos)
{
    memcpy (this, &pos, sizeof (Position));

    _sb         = *_si;
    _si         = &_sb;
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
        <= VALUE_MG_BISHOP))
    {
        return true;
    }

    // Draw by 50 moves Rule?
    if (    fifty_move_distance <  _si->clock50
        || (fifty_move_distance == _si->clock50
        && (!_si->checkers || MoveList<LEGAL> (*this).size ())))
    {
        return true;
    }

    // Draw by Threefold Repetition?
    const StateInfo *psi = _si;
    uint8_t ply = min (_si->null_ply, _si->clock50);
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
    //    if (!generate<LEGAL> (*this).size ()) return true;
    //}

    return false;
}

// Check whether there has been at least one repetition of positions
// since the last capture or pawn move.
bool Position::has_repeated () const
{
    StateInfo *si = _si;
    while (true)
    {
        int32_t i = 4, e = std::min (si->clock50, si->null_ply);
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

}


// Position consistency test, for debugging
bool Position::ok (int8_t *failed_step) const
{
    int8_t dummy_step, *step = failed_step ? failed_step : &dummy_step;

    // What features of the position should be verified?
    const bool debug_all = true;

    const bool debug_king_count    = debug_all || false;
    const bool debug_piece_count   = debug_all || false;
    const bool debug_bitboards     = debug_all || false;
    const bool debug_piece_list    = debug_all || false;

    const bool debug_king_capture  = debug_all || false;
    const bool debug_checker_count = debug_all || false;

    const bool debug_castle_rights = debug_all || false;
    const bool debug_en_passant    = debug_all || false;

    const bool debug_clock50       = debug_all || false;

    const bool debug_matl_key      = debug_all || false;
    const bool debug_pawn_key      = debug_all || false;
    const bool debug_posi_key      = debug_all || false;

    const bool debug_incremental_eval  = debug_all || false;
    const bool debug_non_pawn_material = debug_all || false;

    *step = 0;
    // step 1
    if (++(*step), !_ok (_active)) return false;
    // step 2
    if (++(*step), W_KING != _board[king_sq (WHITE)]) return false;
    // step 3
    if (++(*step), B_KING != _board[king_sq (BLACK)]) return false;

    // step 4
    if (++(*step), debug_king_count)
    {
        uint8_t king_count[CLR_NO] = {0};
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            Piece p = _board[s];
            if (KING == _ptype (p)) ++king_count[_color (p)];
        }
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            if (1 != king_count[c]) return false;
            if (_piece_count[c][KING] != pop_count<FULL> (pieces<KING> (c))) return false;
        }
    }

    // step 5
    if (++(*step), debug_piece_count)
    {
        if (pop_count<FULL> (_types_bb[NONE]) > 32)
        {
            return false;
        }
        if (count () > 32)
        {
            return false;
        }
        if (count () != pop_count<FULL> (_types_bb[NONE]))
        {
            return false;
        }
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (PieceT pt = PAWN; pt <= KING; ++pt)
            {
                if (_piece_count[c][pt] != pop_count<FULL> (pieces (c, pt)))
                {
                    return false;
                }
            }
        }
    }

    // step 6
    if (++(*step), debug_bitboards)
    {
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            Bitboard colors = _color_bb[c];

            if (pop_count<FULL> (colors) > 16) return false; // Too many Piece of color

            // check if the number of Pawns plus the number of
            // extra Queens, Rooks, Bishops, Knights exceeds 8
            // (which can result only by promotion)
            if (    (_piece_count[c][PAWN] +
                max (_piece_count[c][NIHT] - 2, 0) +
                max (_piece_count[c][BSHP] - 2, 0) +
                max (_piece_count[c][ROOK] - 2, 0) +
                max (_piece_count[c][QUEN] - 1, 0)) > 8)
            {
                return false; // Too many Promoted Piece of color
            }

            if (_piece_count[c][BSHP] > 1)
            {
                Bitboard bishops = colors & _types_bb[BSHP];
                uint8_t bishop_count[CLR_NO] =
                {
                    pop_count<FULL> (LIHT_bb & bishops),
                    pop_count<FULL> (DARK_bb & bishops),
                };

                if (    (_piece_count[c][PAWN] +
                    max (bishop_count[WHITE] - 1, 0) +
                    max (bishop_count[BLACK] - 1, 0)) > 8)
                {
                    return false; // Too many Promoted BISHOP of color
                }
            }

            // There should be one and only one KING of color
            Bitboard kings = colors & _types_bb[KING];
            if (!kings || more_than_one (kings)) return false;
        }

        // The intersection of the white and black pieces must be empty
        if (_color_bb[WHITE] & _color_bb[BLACK]) return false;

        Bitboard occ = _types_bb[NONE];
        // The union of the white and black pieces must be equal to occupied squares
        if ((_color_bb[WHITE] | _color_bb[BLACK]) != occ) return false;
        if ((_color_bb[WHITE] ^ _color_bb[BLACK]) != occ) return false;

        // The intersection of separate piece type must be empty
        for (PieceT pt1 = PAWN; pt1 <= KING; ++pt1)
        {
            for (PieceT pt2 = PAWN; pt2 <= KING; ++pt2)
            {
                if (pt1 != pt2 && (_types_bb[pt1] & _types_bb[pt2])) return false;
            }
        }

        // The union of separate piece type must be equal to occupied squares
        if ( (_types_bb[PAWN] | _types_bb[NIHT] | _types_bb[BSHP]
            | _types_bb[ROOK] | _types_bb[QUEN] | _types_bb[KING]) != occ) return false;
        if ( (_types_bb[PAWN] ^ _types_bb[NIHT] ^ _types_bb[BSHP]
            ^ _types_bb[ROOK] ^ _types_bb[QUEN] ^ _types_bb[KING]) != occ) return false;

        // PAWN rank should not be 1/8
        if ((_types_bb[PAWN] & (R1_bb | R8_bb))) return false;
    }

    // step 7
    if (++(*step), debug_piece_list)
    {
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (PieceT pt = PAWN; pt <= KING; ++pt)
            {
                for (int32_t i = 0; i < _piece_count[c][pt]; ++i)
                {
                    if (!_ok (_piece_list[c][pt][i])) return false;
                    if (_board[_piece_list[c][pt][i]] != (c | pt)) return false;
                    if (_index[_piece_list[c][pt][i]] != i) return false;
                }
            }
        }
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            if (_index[s] >= 16)
            {
                return false;
            }
        }
    }

    // step 8
    if (++(*step), debug_king_capture)
    {
        if (checkers (~_active))
        {
            return false;
        }
    }

    // step 9
    if (++(*step), debug_checker_count)
    {
        if (pop_count<FULL> (checkers ()) > 2) return false;
    }

    // step 10
    if (++(*step), debug_castle_rights)
    {
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (CSide cs = CS_K; cs <= CS_Q; ++cs)
            {
                CRight cr = mk_castle_right (c, cs);

                if (!can_castle (cr)) continue;

                if ( (_castling_mask[king_sq (c)] & cr) != cr
                  || (_board[_castle_rooks[cr]] != (c | ROOK))
                  || (_castling_mask[_castle_rooks[cr]] != cr))
                {
                    return false;
                }
            }
        }
    }

    // step 11
    if (++(*step), debug_en_passant)
    {
        Square ep_sq = _si->en_passant_sq;
        if (SQ_NO != ep_sq)
        {
            if (!can_en_passant (ep_sq)) return false;
        }
    }

    // step 12
    if (++(*step), debug_clock50)
    {
        if (_si->clock50 > 100) return false;
    }

    // step 13
    if (++(*step), debug_matl_key)
    {
        if (Zob.compute_matl_key (*this) != _si->matl_key) return false;
    }
    // step 14
    if (++(*step), debug_pawn_key)
    {
        if (Zob.compute_pawn_key (*this) != _si->pawn_key) return false;
    }

    // step 15
    if (++(*step), debug_posi_key)
    {
        if (Zob.compute_posi_key (*this) != _si->posi_key) return false;
    }

    // step 16
    if (++(*step), debug_incremental_eval)
    {
        if (_si->psq_score != compute_psq_score ()) return false;
    }

    // step 17
    if (++(*step), debug_non_pawn_material)
    {
        if (   _si->non_pawn_matl[WHITE] != compute_non_pawn_material (WHITE)
            || _si->non_pawn_matl[BLACK] != compute_non_pawn_material (BLACK))
        {
            return false;
        }
    }

    *step = 0;
    return true;
}

// see() is a Static Exchange Evaluator (SEE):
// It tries to estimate the material gain or loss resulting from a move.
Value Position::see      (Move m) const
{
    Square org = org_sq (m);
    Square dst = dst_sq (m);

    // side to move
    Color stm = _color (_board[org]);

    // Gain list
    Value swap_list[32];

    int8_t depth = 1;
    swap_list[0] = PieceValue[MG][_ptype (_board[dst])];

    Bitboard occupied = _types_bb[NONE] - org;

    MoveT mt = mtype (m);

    if      (CASTLE    == mt)
    {
        // Castle moves are implemented as king capturing the rook so cannot be
        // handled correctly. Simply return 0 that is always the correct value
        // unless in the rare case the rook ends up under attack.
        return VALUE_ZERO;
    }
    else if (ENPASSANT == mt)
    {
        occupied -= (dst - pawn_push (stm)); // Remove the captured pawn
        swap_list[0] = PieceValue[MG][PAWN];
    }

    // Find all enemy attackers to the destination square, with the moving piece
    // removed, but possibly an X-ray attacker added behind it.
    Bitboard attackers = attackers_to (dst, occupied) & occupied;

    // If the opponent has no attackers we are finished
    stm = ~stm;
    Bitboard stm_attackers = attackers & _color_bb[stm];
    if (stm_attackers)
    {
        // The destination square is defended, which makes things rather more
        // difficult to compute. We proceed by building up a "swap list" containing
        // the material gain or loss at each stop in a sequence of captures to the
        // destination square, where the sides alternately capture, and always
        // capture with the least valuable piece. After each capture, we look for
        // new X-ray attacks from behind the capturing piece.
        PieceT ct = _ptype (_board[org]);

        do
        {
            ASSERT (depth < 32);

            // Add the new entry to the swap list
            swap_list[depth] = PieceValue[MG][ct] - swap_list[depth - 1];

            // Locate and remove the next least valuable attacker
            ct  = min_attacker<PAWN> (_types_bb, dst, stm_attackers, occupied, attackers);

            // Stop before processing a king capture
            if (KING == ct)
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
        while (stm_attackers);

        // Having built the swap list, we negamax through it to find the best
        // achievable score from the point of view of the side to move.
        while (--depth)
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
    // Early return if SEE cannot be negative because captured piece value
    // is not less then capturing one. Note that king moves always return
    // here because king midgame value is set to 0.
    if (PieceValue[MG][_ptype (_board[org_sq (m)])]
    <=  PieceValue[MG][_ptype (_board[dst_sq (m)])])
    {
        return VALUE_KNOWN_WIN;
    }

    return see (m);
}

Bitboard Position::check_blockers (Color c, Color king_c) const
{
    Square ksq = king_sq (king_c);
    // Pinners are sliders that give check when a pinned piece is removed
    Bitboard pinners =
        ( (PieceAttacks[ROOK][ksq] & pieces (QUEN, ROOK))
        | (PieceAttacks[BSHP][ksq] & pieces (QUEN, BSHP)))
        &  _color_bb[~king_c];

    Bitboard chk_blockers  = U64 (0);
    while (pinners)
    {
        Bitboard blocker = Between_bb[ksq][pop_lsq (pinners)] & _types_bb[NONE];
        if (!more_than_one (blocker))
        {
            chk_blockers |= (blocker & _color_bb[c]); // Defending piece
        }
    }

    return chk_blockers;
}


// pseudo_legal(m) tests whether a random move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal (Move m) const
{
    if (!_ok (m)) return false;

    Square org  = org_sq (m);
    Square dst  = dst_sq (m);

    Color activ = _active;
    Color pasiv = ~activ;

    Piece p = _board[org];

    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if ((EMPTY == p) || (activ != _color (p))) return false;

    Rank r_org = rel_rank (activ, org);
    Rank r_dst = rel_rank (activ, dst);

    PieceT pt = _ptype (p);
    PieceT ct = NONE;

    Square cap = dst;

    MoveT mt = mtype (m);

    if      (NORMAL    == mt)
    {
        // Is not a promotion, so promotion piece must be empty
        if (PAWN != (prom_type (m) - NIHT)) return false;
        ct = _ptype (_board[cap]);
    }
    else if (CASTLE    == mt)
    {
        // Check whether the destination square is attacked by the opponent.
        // Castling moves are checked for legality during move generation.
        if (KING != pt) return false;
        if ((activ | ROOK) != _board[dst]) return false;

        if (R_1 != r_org || R_1 != r_dst) return false;

        //if (castle_impeded (activ)) return false;
        if (!can_castle (activ)) return false;
        if (checkers ()) return false;

        bool king_side  = (dst > org); 
        if (castle_impeded (mk_castle_right(activ, king_side ? CS_K : CS_Q))) return false;

        // Castle is always encoded as "king captures friendly rook"
        ASSERT (dst == castle_rook (mk_castle_right(activ, king_side ? CS_K : CS_Q)));
        dst = rel_sq (activ, king_side ? SQ_WK_K : SQ_WK_Q);

        Delta step = king_side ? DEL_E : DEL_W;
        for (Square s  = org + step;
            s != dst + step; s += step)
        {
            if (attackers_to (s) & _color_bb[pasiv])
            {
                return false;
            }
        }

        //ct = NONE;
        return true;
    }
    else if (PROMOTE   == mt)
    {
        if (PAWN != pt) return false;
        if (R_7 != r_org) return false;
        if (R_8 != r_dst) return false;
        ct = _ptype (_board[cap]);
    }
    else if (ENPASSANT == mt)
    {
        if (   PAWN != pt
            || _si->en_passant_sq != dst
            || R_5 != r_org
            || R_6 != r_dst
            || !empty (dst))
        {
            return false;
        }

        cap += pawn_push (pasiv);
        if ((pasiv | PAWN) != _board[cap]) return false;

        ct = PAWN;
    }

    if (KING == ct) return false;

    // The destination square cannot be occupied by a friendly piece
    if (_color_bb[activ] & dst) return false;

    // Handle the special case of a piece move
    if (PAWN == pt)
    {
        // Move direction must be compatible with pawn color
        // We have already handled promotion moves, so destination
        Delta delta = dst - org;

        if ((activ == WHITE) != (delta > DEL_O)) return false;

        if (R_1 == r_org || R_8 == r_org) return false;
        if (R_1 == r_dst || R_2 == r_dst) return false;
        if (NORMAL == mt && (R_7 == r_org || R_8 == r_dst)) return false;

        // Proceed according to the square delta between the origin and destiny squares.
        switch (delta)
        {
        case DEL_N:
        case DEL_S:
            // Pawn push. The destination square must be empty.
            if (!empty (dst)) return false;
            break;

        case DEL_NE:
        case DEL_NW:
        case DEL_SE:
        case DEL_SW:
            // Capture. The destination square must be occupied by an enemy piece
            // (en passant captures was handled earlier).
            if (NONE == ct || activ == _color (_board[cap])) return false;
            // cap and org files must be one del apart, avoids a7h5
            if (1 != file_dist (cap, org)) return false;
            break;

        case DEL_NN:
        case DEL_SS:
            // Double pawn push. The destination square must be on the fourth
            // rank, and both the destination square and the square between the
            // source and destination squares must be empty.

            if (   R_2 != r_org
                || R_4 != r_dst
                || !empty (dst)
                || !empty (org + pawn_push (activ)))
            {
                return false;
            }
            break;

        default:
            return false;
        }
    }
    else
    {
        if (!(attacks_bb (p, org, _types_bb[NONE]) & dst)) return false;
    }

    // Evasions generator already takes care to avoid some kind of illegal moves
    // and pl_move_is_legal() relies on this. So we have to take care that the
    // same kind of moves are filtered out here.
    Bitboard chkrs = checkers ();
    if (chkrs)
    {
        if (KING != pt)
        {
            // Double check? In this case a king move is required
            if (more_than_one (chkrs)) return false;
            if ((PAWN == pt) && (ENPASSANT == mt))
            {
                // Our move must be a capture of the checking en-passant pawn
                // or a blocking evasion of the checking piece
                if (!((chkrs & cap) || (Between_bb[scan_lsq (chkrs)][king_sq (activ)] & dst)))
                {
                    return false;
                }
            }
            else
            {
                // Our move must be a blocking evasion or a capture of the checking piece
                if (!((Between_bb[scan_lsq (chkrs)][king_sq (activ)] | chkrs) & dst))
                {
                    return false;
                }
            }
        }
        else
        {
            // In case of king moves under check we have to remove king so to catch
            // as invalid moves like B1A1 when opposite queen is on C1.
            if (attackers_to (dst, _types_bb[NONE] - org) & _color_bb[pasiv]) return false;
        }
    }

    return true;
}

// legal(m, pinned) tests whether a pseudo-legal move is legal
bool Position::legal        (Move m, Bitboard pinned) const
{
    ASSERT (_ok (m));
    ASSERT (pinned == pinneds (_active));

    Square org  = org_sq (m);
    Square dst  = dst_sq (m);

    Color activ = _active;
    Color pasiv = ~activ;

    Piece  p  = _board[org];
    PieceT pt = _ptype  (p);
    ASSERT ((activ == _color (p)) && (NONE != pt));

    Square ksq = king_sq (activ);

    MoveT mt = mtype (m);

    if      (CASTLE    == mt)
    {
        // Castling moves are checked for legality during move generation.
        return (KING == pt);
    }
    else if (ENPASSANT == mt)
    {
        // En-passant captures are a tricky special case. Because they are rather uncommon,
        // we do it simply by testing whether the king is attacked after the move is made.
        Square cap = dst + pawn_push (pasiv);

        ASSERT (dst == _si->en_passant_sq);
        ASSERT ((activ | PAWN) == _board[org]);
        ASSERT ((pasiv | PAWN) == _board[cap]);
        ASSERT (empty (dst));

        Bitboard mocc = _types_bb[NONE] - org - cap + dst;
        // If any attacker then in check & not legal
        return !(
            (attacks_bb<ROOK> (ksq, mocc) & pieces (pasiv, QUEN, ROOK)) ||
            (attacks_bb<BSHP> (ksq, mocc) & pieces (pasiv, QUEN, BSHP)));
    }

    // If the moving piece is a king.
    if (KING == pt)
    {
        // In case of king moves under check we have to remove king so to catch
        // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
        // check whether the destination square is attacked by the opponent.
        return !(attackers_to (dst, _types_bb[NONE] - org) & _color_bb[pasiv]); // Remove 'org' but not place 'dst'
    }

    // A non-king move is legal if and only if it is not pinned or it
    // is moving along the ray towards or away from the king or
    // is a blocking evasion or a capture of the checking piece.
    return !(pinned)
        || !(pinned & org)
        || sqrs_aligned (org, dst, ksq);
}

// gives_check(m) tests whether a pseudo-legal move gives a check
bool Position::gives_check     (Move m, const CheckInfo &ci) const
{
    ASSERT (_color (_board[org_sq (m)]) == _active);
    ASSERT (ci.discoverers == discoverers (_active));

    Square org = org_sq (m);
    Square dst = dst_sq (m);

    Piece  p  = _board[org];
    PieceT pt = _ptype  (p);

    // Direct check ?
    if (ci.checking_bb[pt] & dst) return true;

    // Discovery check ?
    if (UNLIKELY (ci.discoverers) && ci.discoverers & org)
    {
        if (!sqrs_aligned (org, dst, ci.king_sq)) return true;
    }

    MoveT mt  = mtype (m);
    // Can we skip the ugly special cases ?
    if (NORMAL == mt) return false;

    Bitboard occ = _types_bb[NONE];

    if      (CASTLE    == mt)
    {
        // Castling with check ?
        bool  king_side = (dst > org);
        Square org_rook = dst; // 'King captures the rook' notation
        dst             = rel_sq (_active, king_side ? SQ_WK_K : SQ_WK_Q);
        Square dst_rook = rel_sq (_active, king_side ? SQ_WR_K : SQ_WR_Q);

        return
            (PieceAttacks[ROOK][dst_rook] & ci.king_sq) && // First x-ray check then full check
            (attacks_bb<ROOK> (dst_rook, (occ - org - org_rook + dst + dst_rook)) & ci.king_sq);
    }
    else if (PROMOTE   == mt)
    {
        // Promotion with check ?
        return (attacks_bb (Piece (prom_type (m)), dst, occ - org + dst) & ci.king_sq);
    }
    else if (ENPASSANT == mt)
    {
        // En passant capture with check ?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        Square cap = _file (dst) | _rank (org);
        Bitboard mocc = occ - org - cap + dst;
        // if any attacker then in check
        return (attacks_bb<ROOK> (ci.king_sq, mocc) & pieces (_active, QUEN, ROOK))
            || (attacks_bb<BSHP> (ci.king_sq, mocc) & pieces (_active, QUEN, BSHP));
    }

    ASSERT (false);
    return false;
}

// gives_checkmate(m) tests whether a pseudo-legal move gives a checkmate
bool Position::gives_checkmate (Move m, const CheckInfo &ci) const
{
    if (!gives_check (m, ci)) return false;

    Position pos = *this;
    StateInfo si;
    pos.do_move (m, si);
    return !MoveList<LEGAL> (pos).size ();
}

// clear() clear the position
void Position::clear ()
{
    memset (this, 0, sizeof (Position));

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        _board[s] = EMPTY;
        _index[s] = -1;
    }

    for (PieceT pt = PAWN; pt <= KING; ++pt)
    {
        for (int32_t i = 0; i < 16; ++i)
        {
            _piece_list[WHITE][pt][i] = SQ_NO;
            _piece_list[BLACK][pt][i] = SQ_NO;
        }
    }

    //fill (_castle_rooks, _castle_rooks + sizeof (_castle_rooks) / sizeof (*_castle_rooks), SQ_NO);
    memset (_castle_rooks, SQ_NO, sizeof (_castle_rooks));

    //_game_ply   = 1;

    _sb.en_passant_sq = SQ_NO;
    _sb.capture_type  = NONE;
    _si = &_sb;
}
// setup() sets the fen on the position
#ifndef NDEBUG
bool Position::setup (const   char *f, Thread *th, bool c960, bool full)
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
#endif
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
    Square org_king = king_sq (c);
    ASSERT (org_king != org_rook);

    bool king_side = (org_rook > org_king);
    CSide  cs = (king_side ? CS_K : CS_Q);
    CRight cr = mk_castle_right (c, cs);
    Square dst_rook = rel_sq (c, king_side ? SQ_WR_K : SQ_WR_Q);
    Square dst_king = rel_sq (c, king_side ? SQ_WK_K : SQ_WK_Q);

    _si->castle_rights |= cr;

    _castling_mask[org_king] |= cr;
    _castling_mask[org_rook] |= cr;
    _castle_rooks[cr] = org_rook;

    for (Square s = min (org_rook, dst_rook); s <= max (org_rook, dst_rook); ++s)
    {
        if (org_king != s && org_rook != s)
        {
            _castle_paths[cr] += s;
        }
    }
    for (Square s = min (org_king, dst_king); s <= max (org_king, dst_king); ++s)
    {
        if (org_king != s && org_rook != s)
        {
            _castle_paths[cr] += s;
        }
    }
}
// can_en_passant() tests the en-passant square
bool Position::can_en_passant (Square ep_sq) const
{
    ASSERT (_ok (ep_sq));

    Color activ = _active;
    Color pasiv = ~activ;
    ASSERT (R_6 == rel_rank (activ, ep_sq));

    Square cap = ep_sq + pawn_push (pasiv);
    if (!(pieces<PAWN> (pasiv) & cap)) return false;
    //if ((pasiv | PAWN) != _board[cap]) return false;

    Bitboard pawns_ep = PawnAttacks[pasiv][ep_sq] & pieces<PAWN> (activ);
    ASSERT (pop_count<FULL> (pawns_ep) <= 2);
    if (!pawns_ep) return false;

    vector<Move> mov_ep;
    while (pawns_ep)
    {
        mov_ep.push_back (mk_move<ENPASSANT> (pop_lsq (pawns_ep), ep_sq));
    }

    // Check en-passant is legal for the position
    Square fk_sq = king_sq (activ);
    Bitboard occ = _types_bb[NONE];
    for (vector<Move>::const_iterator itr = mov_ep.begin (); itr != mov_ep.end (); ++itr)
    {
        Move m = *itr;
        Bitboard mocc = occ - org_sq (m) - cap + dst_sq (m);
        if (!((attacks_bb<ROOK> (fk_sq, mocc) & pieces (pasiv, QUEN, ROOK))
            | (attacks_bb<BSHP> (fk_sq, mocc) & pieces (pasiv, QUEN, BSHP))))
        {
            return true;
        }
    }

    return false;
}
//bool Position::can_en_passant (File   ep_f) const
//{
//    return can_en_passant (ep_f | rel_rank (_active, R_6));
//}

// compute_psq_score () computes the incremental scores for the middle
// game and the endgame. These functions are used to initialize the incremental
// scores when a new position is set up, and to verify that the scores are correctly
// updated by do_move and undo_move when the program is running in debug mode.
Score Position::compute_psq_score () const
{
    Score score  = SCORE_ZERO;
    Bitboard occ = _types_bb[NONE];
    while (occ)
    {
        Square s = pop_lsq (occ);
        score += PSQ[_color (_board[s])][_ptype (_board[s])][s];
    }
    return score;
}

// compute_non_pawn_material () computes the total non-pawn middle
// game material value for the given side. Material values are updated
// incrementally during the search, this function is only used while
// initializing a new Position object.
Value Position::compute_non_pawn_material (Color c) const
{
    Value value = VALUE_ZERO;
    for (PieceT pt = NIHT; pt <= QUEN; ++pt)
    {
        value += PieceValue[MG][pt] * int32_t (_piece_count[c][pt]);
    }
    return value;
}

// do_move() do the move with checking info
void Position::do_move (Move m, StateInfo &n_si, const CheckInfo *ci)
{
    ASSERT (_ok (m));
    ASSERT (&n_si != _si);

    Key posi_k = _si->posi_key;

    // Copy some fields of old state to new StateInfo object except the ones
    // which are going to be recalculated from scratch anyway, 
    memcpy (&n_si, _si, STATE_COPY_SIZE);

    // Switch state pointer to point to the new, ready to be updated, state.
    n_si.p_si   = _si;
    _si         = &n_si;

    Color activ = _active;
    Color pasiv = ~activ;

    Square org  = org_sq (m);
    Square dst  = dst_sq (m);
    PieceT pt   = _ptype (_board[org]);

    ASSERT ((EMPTY != _board[org])
        &&  (activ == _color (_board[org]))
        &&  (NONE != pt));
    ASSERT ((EMPTY == _board[dst])
        ||  (pasiv == _color (_board[dst]))
        ||  (CASTLE == mtype (m)));

    Square cap = dst;
    PieceT  ct = NONE;

    MoveT mt   = mtype (m);

    // Pick capture piece and check validation
    if      (NORMAL    == mt)
    {
        ASSERT (PAWN == (prom_type (m) - NIHT));
        if (PAWN == pt)
        {
            uint8_t del_f = file_dist (cap, org);
            ASSERT (0 == del_f || 1 == del_f);
            if (1 == del_f) ct = _ptype (_board[cap]);
        }
        else
        {
            ct = _ptype (_board[cap]);
        }
    }
    else if (CASTLE    == mt)
    {
        ASSERT (KING == pt);
        ASSERT (ROOK == _ptype (_board[dst]));

        ct = NONE;
    }
    else if (PROMOTE   == mt)
    {
        ASSERT (PAWN == pt);        // Moving type must be PAWN
        ASSERT (R_7 == rel_rank (activ, org));
        ASSERT (R_8 == rel_rank (activ, dst));

        ct = _ptype (_board[cap]);
        ASSERT (PAWN != ct);
    }
    else if (ENPASSANT == mt)
    {
        ASSERT (PAWN == pt);                // Moving type must be pawn
        ASSERT (dst == _si->en_passant_sq); // Destination must be en-passant
        ASSERT (R_5 == rel_rank (activ, org));
        ASSERT (R_6 == rel_rank (activ, dst));
        ASSERT (empty (cap));  // Capture Square must be empty

        cap += pawn_push (pasiv);
        ASSERT ((pasiv | PAWN) == _board[cap]);
        ct = PAWN;
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
            _si->pawn_key ^= Zob._.piecesq[pasiv][PAWN][cap];
        }
        else            // Update non-pawn material
        {
            _si->non_pawn_matl[pasiv] -= PieceValue[MG][ct];
        }
        // Update Hash key of material situation
        _si->matl_key ^= Zob._.piecesq[pasiv][ct][_piece_count[pasiv][ct]];

        // Update prefetch access to material_table
#ifndef NDEBUG
        if (_thread)
#endif
            prefetch ((char *) _thread->material_table[_si->matl_key]);

        // Update Hash key of position
        posi_k ^= Zob._.piecesq[pasiv][ct][cap];
        // Update incremental scores
        _si->psq_score -= PSQ[pasiv][ct][cap];
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
        posi_k ^= Zob._.en_passant[_file (_si->en_passant_sq)];
        _si->en_passant_sq = SQ_NO;
    }

    // Do move according to move type
    if      (NORMAL  == mt
        || ENPASSANT == mt)
    {
        // Move the piece
        move_piece (org, dst);

        // Update pawns hash key
        if (PAWN == pt)
        {
            _si->pawn_key ^=
                Zob._.piecesq[activ][PAWN][org] ^
                Zob._.piecesq[activ][PAWN][dst];
        }
        posi_k ^= Zob._.piecesq[activ][pt][org] ^ Zob._.piecesq[activ][pt][dst];
        _si->psq_score += PSQ[activ][pt][dst] - PSQ[activ][pt][org];
    }
    else if (CASTLE  == mt)
    {
        // Move the piece. The tricky Chess960 castle is handled earlier
        bool king_side  = (dst > org);
        Square org_rook = dst; // castle is always encoded as "king captures friendly rook"
        dst             = rel_sq (activ, king_side ? SQ_WK_K : SQ_WK_Q);
        Square dst_rook = rel_sq (activ, king_side ? SQ_WR_K : SQ_WR_Q);

        //ASSERT (org_rook == castle_rook (activ, king_side ? CS_K : CS_Q));
        ASSERT (empty (dst_rook));

        exchange_king_rook (org, dst, org_rook, dst_rook);

        posi_k ^= Zob._.piecesq[_active][KING][org     ] ^ Zob._.piecesq[_active][KING][dst     ];
        posi_k ^= Zob._.piecesq[_active][ROOK][org_rook] ^ Zob._.piecesq[_active][ROOK][dst_rook];

        _si->psq_score += PSQ[activ][KING][dst     ] - PSQ[activ][KING][org     ];
        _si->psq_score += PSQ[activ][ROOK][dst_rook] - PSQ[activ][ROOK][org_rook];
    }
    else if (PROMOTE == mt)
    {
        PieceT ppt = prom_type (m);
        // Replace the PAWN with the Promoted piece
        remove_piece (org);
        place_piece (dst, activ, ppt);

        _si->matl_key ^=
            Zob._.piecesq[activ][PAWN][_piece_count[activ][PAWN]] ^
            Zob._.piecesq[activ][ppt][_piece_count[activ][ppt] - 1];

        _si->pawn_key ^= Zob._.piecesq[activ][PAWN][org];

        posi_k ^= Zob._.piecesq[activ][PAWN][org] ^ Zob._.piecesq[activ][ppt][dst];

        // Update incremental score
        _si->psq_score += PSQ[activ][ppt][dst] - PSQ[activ][PAWN][org];
        // Update material
        _si->non_pawn_matl[activ] += PieceValue[MG][ppt];
    }

    // Update castle rights if needed
    uint8_t cr = _si->castle_rights & (_castling_mask[org] | _castling_mask[dst]);
    if (cr)
    {
        Bitboard b = cr;
        _si->castle_rights &= ~cr;
        while (b)
        {
            posi_k ^= Zob._.castle_right[0][pop_lsq (b)];
        }
    }

    // Update checkers bitboard: piece must be already moved
    _si->checkers = U64 (0);
    if (ci)
    {
        if (NORMAL == mt)
        {
            // Direct check ?
            if (ci->checking_bb[pt] & dst) _si->checkers += dst;
            
            // Discovery check ?
            if (QUEN != pt)
            {
                if (ci->discoverers && (ci->discoverers & org))
                {
                    if (ROOK != pt)
                    {
                        _si->checkers |=
                            attacks_bb<ROOK> (king_sq (pasiv), _types_bb[NONE]) &
                            pieces (activ, QUEN, ROOK);
                    }
                    if (BSHP != pt)
                    {
                        _si->checkers |=
                            attacks_bb<BSHP> (king_sq (pasiv), _types_bb[NONE]) &
                            pieces (activ, QUEN, BSHP);
                    }
                }
            }
        }
        else
        {
            _si->checkers = checkers (pasiv);
        }
    }

    // Switch side to move
    _active = pasiv;
    posi_k ^= Zob._.mover_side;

    // Handle pawn en-passant square setting
    if (PAWN == pt)
    {
        uint8_t iorg = org;
        uint8_t idst = dst;
        if (16 == (idst ^ iorg))
        {
            Square ep_sq = Square ((idst + iorg) / 2);
            if (can_en_passant (ep_sq))
            {
                _si->en_passant_sq = ep_sq;
                posi_k ^= Zob._.en_passant[_file (ep_sq)];
            }
        }

        // Update prefetch access to pawns_table
#ifndef NDEBUG
        if (_thread)
#endif
            prefetch ((char *) _thread->pawns_table[_si->pawn_key]);
    }

    // Prefetch TT access as soon as we know the new hash key
    prefetch((char*) TT.get_cluster (posi_k));

    // Update the key with the final value
    _si->posi_key       = posi_k;
    _si->capture_type   = ct;
    _si->last_move      = m;
    ++_si->null_ply;
    ++_game_ply;
    ++_game_nodes;

    ASSERT (ok ());
}
void Position::do_move (Move m, StateInfo &n_si)
{
    CheckInfo ci (*this);
    do_move (m, n_si, gives_check (m, ci) ? &ci : NULL);
}
// do_move() do the move from string (CAN)
void Position::do_move (string &can, StateInfo &n_si)
{
    Move move = move_from_can (can, *this);
    if (MOVE_NONE != move) do_move (move, n_si);
}
// undo_move() undo the last move
void Position::undo_move ()
{
    ASSERT (_si->p_si);
    Move m = _si->last_move;
    ASSERT (_ok (m));

    Square org  = org_sq (m);
    Square dst  = dst_sq (m);

    Color pasiv = _active;
    Color activ = _active = ~_active;

    MoveT mt = mtype (m);
    ASSERT (empty (org) || CASTLE == mt);

    PieceT ct = _si->capture_type;
    ASSERT (KING != ct);

    Square cap = dst;

    // Undo move according to move type
    if      (NORMAL    == mt)
    {
        move_piece (dst, org); // Put the piece back at the origin square
    }
    else if (CASTLE    == mt)
    {
        bool king_side = (dst > org);
        Square org_rook = dst; // castle is always encoded as "king captures friendly rook"
        dst             = rel_sq (activ, king_side ? SQ_WK_K : SQ_WK_Q);
        Square dst_rook = rel_sq (activ, king_side ? SQ_WR_K : SQ_WR_Q);

        ct  = NONE;
        exchange_king_rook (dst, org, dst_rook, org_rook);
    }
    else if (PROMOTE   == mt)
    {
        ASSERT (prom_type (m) == _ptype (_board[dst]));
        ASSERT (R_8 == rel_rank (activ, dst));
        ASSERT (NIHT <= prom_type (m) && prom_type (m) <= QUEN);
        // Replace the promoted piece with the PAWN
        remove_piece (dst);
        place_piece (org, activ, PAWN);
    }
    else if (ENPASSANT == mt)
    {
        ASSERT (PAWN == _ptype (_board[dst]));
        ASSERT (PAWN == ct);
        ASSERT (R_5 == rel_rank (activ, org));
        ASSERT (R_6 == rel_rank (activ, dst));
        ASSERT (dst == _si->p_si->en_passant_sq);
        cap -= pawn_push (activ);
        ASSERT (empty (cap));
        move_piece (dst, org); // Put the piece back at the origin square
    }

    // If there was any capture piece
    if (NONE != ct)
    {
        place_piece (cap, pasiv, ct); // Restore the captured piece
    }

    --_game_ply;
    // Finally point our state pointer back to the previous state
    _si     = _si->p_si;

    ASSERT (ok ());
}

// do_null_move() do the null-move
void Position::do_null_move (StateInfo &n_si)
{
    ASSERT (&n_si != _si);
    ASSERT (!_si->checkers);

    // Full copy here
    memcpy (&n_si, _si, sizeof (StateInfo));

    // Switch our state pointer to point to the new, ready to be updated, state.
    n_si.p_si = _si;
    _si       = &n_si;

    if (SQ_NO != _si->en_passant_sq)
    {
        _si->posi_key ^= Zob._.en_passant[_file (_si->en_passant_sq)];
        _si->en_passant_sq = SQ_NO;
    }

    _active = ~_active;
    _si->posi_key ^= Zob._.mover_side;

    prefetch ((char *) TT.get_cluster (_si->posi_key));

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


// flip position with the white and black sides reversed.
// This is only useful for debugging especially for finding evaluation symmetry bugs.
void Position::flip ()
{
    string fen_, ch;
    stringstream ss (fen ());
    // 1. Piece placement
    for (Rank rank = R_8; rank >= R_1; --rank)
    {
        getline (ss, ch, rank > R_1 ? '/' : ' ');
        fen_.insert (0, ch + (fen_.empty () ? " " : "/"));
    }
    // 2. Active color
    ss >> ch;
    fen_ += (ch == "w" ? "B" : "W"); // Will be lowercased later
    fen_ += " ";
    // 3. Castling availability
    ss >> ch;
    fen_ += ch + " ";
    transform (fen_.begin (), fen_.end (), fen_.begin (), toggle_case);

    // 4. En-passant square
    ss >> ch;
    fen_ += ((ch == "-") ? ch : ch.replace (1, 1, (ch[1] == '3') ? "6" : "3"));
    // 5-6. Half and full moves
    getline (ss, ch);
    fen_ += ch;

    setup (fen_, _thread, _chess960);

    ASSERT (ok ());
}

#ifndef NDEBUG
bool   Position::fen (const char *fn, bool c960, bool full) const
{
    ASSERT (fn);
    ASSERT (ok ());

    char *ch = const_cast<char *> (fn);
    memset (ch, '\0', MAX_FEN);

#undef set_next

#define set_next(x)      *ch++ = x

    for (Rank r = R_8; r >= R_1; --r)
    {
        File f = F_A;
        while (f <= F_H)
        {
            Square s = f | r;
            Piece p  = _board[s];

            if (EMPTY == p)
            {
                uint32_t empty_count = 0;
                for ( ; f <= F_H && empty (s); ++f, ++s)
                {
                    ++empty_count;
                }
                if (1 > empty_count || empty_count > 8) return false;
                set_next ('0' + empty_count);
            }
            else if (_ok (p))
            {
                set_next (CharPiece[p]);
                ++f;
            }
            else
            {
                return false;
            }
        }
        if (R_1 < r) set_next ('/');
    }

    set_next (' ');
    set_next (CharColor[_active]);
    set_next (' ');

    if (can_castle (CR_A))
    {
        if (_chess960 || c960)
        {
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) set_next (to_char (_file (_castle_rooks[MakeCastling<WHITE, CS_K>::right]), false));
                if (can_castle (CR_W_Q)) set_next (to_char (_file (_castle_rooks[MakeCastling<WHITE, CS_Q>::right]), false));
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) set_next (to_char (_file (_castle_rooks[MakeCastling<BLACK, CS_K>::right]), true));
                if (can_castle (CR_B_Q)) set_next (to_char (_file (_castle_rooks[MakeCastling<BLACK, CS_Q>::right]), true));
            }
        }
        else
        {
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) set_next ('K');
                if (can_castle (CR_W_Q)) set_next ('Q');
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) set_next ('k');
                if (can_castle (CR_B_Q)) set_next ('q');
            }
        }
    }
    else
    {
        set_next ('-');
    }
    set_next (' ');
    Square ep_sq = _si->en_passant_sq;
    if (SQ_NO != ep_sq)
    {
        if (R_6 != rel_rank (_active, ep_sq)) return false;
        set_next (to_char (_file (ep_sq)));
        set_next (to_char (_rank (ep_sq)));
    }
    else
    {
        set_next ('-');
    }
    if (full)
    {
        set_next (' ');
        int32_t write = sprintf (ch, "%u %u", _si->clock50, game_move ());
            //_snprintf (ch, MAX_FEN - (ch - fn) - 1, "%u %u", _si->clock50, game_move ());
            //_snprintf_s (ch, MAX_FEN - (ch - fn) - 1, 8, "%u %u", _si->clock50, game_move ());

        ch += write;
    }
    set_next ('\0');

#undef set_next

    return true;
}
#endif
string Position::fen (bool                  c960, bool full) const
{
    ostringstream os;

    for (Rank r = R_8; r >= R_1; --r)
    {
        for (File f = F_A; f <= F_H; ++f)
        {
            Square s = f | r;
            int16_t empty_count = 0;
            while (F_H >= f && empty (s))
            {
                ++empty_count;
                ++f;
                ++s;
            }
            if (empty_count) os << empty_count;
            if (F_H >= f)  os << CharPiece[_board[s]];
        }

        if (R_1 < r) os << '/';
    }

    os << " " << CharColor[_active] << " ";

    if (can_castle (CR_A))
    {
        if (_chess960 || c960)
        {
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) os << to_char (_file (_castle_rooks[MakeCastling<WHITE, CS_K>::right]), false);
                if (can_castle (CR_W_Q)) os << to_char (_file (_castle_rooks[MakeCastling<WHITE, CS_Q>::right]), false);
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) os << to_char (_file (_castle_rooks[MakeCastling<BLACK, CS_K>::right]), true);
                if (can_castle (CR_B_Q)) os << to_char (_file (_castle_rooks[MakeCastling<BLACK, CS_Q>::right]), true);
            }
        }
        else
        {
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) os << 'K';
                if (can_castle (CR_W_Q)) os << 'Q';
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) os << 'k';
                if (can_castle (CR_B_Q)) os << 'q';
            }
        }
    }
    else
    {
        os << '-';
    }

    os << (SQ_NO == _si->en_passant_sq ?
        " - " : " " + to_string (_si->en_passant_sq) + " ");

    if (full) os << int16_t (_si->clock50) << " " << game_move ();

    return os.str ();
}

// string() returns an ASCII representation of the position to be
// printed to the standard output
Position::operator string () const
{
    const string edge = " +---+---+---+---+---+---+---+---+\n";
    const string row_1 = "| . |   | . |   | . |   | . |   |\n" + edge;
    const string row_2 = "|   | . |   | . |   | . |   | . |\n" + edge;
    const uint16_t row_len = row_1.length () + 1;

    string board = edge;

    for (Rank r = R_8; r >= R_1; --r)
    {
        board += to_char (r) + ((r % 2) ? row_1 : row_2);
    }
    for (File f = F_A; f <= F_H; ++f)
    {
        board += "   ";
        board += to_char (f, false);
    }

    Bitboard occ = _types_bb[NONE];
    while (occ)
    {
        Square s = pop_lsq (occ);
        int8_t r = _rank (s);
        int8_t f = _file (s);
        board[3 + row_len * (7.5 - r) + 4 * f] = CharPiece[_board[s]];
    }

    ostringstream ss;

    ss  << board << "\n";

    ss  << "\nFen: " << fen ()
        << "\nKey: " << hex << uppercase << setfill ('0') << setw (16) << _si->posi_key;

    ss  << "\nCheckers: ";
    Bitboard chkrs = checkers ();
    if (chkrs)
    {
        while (chkrs) ss << to_string (pop_lsq (chkrs)) << " ";
    }
    else
    {
        ss  << "<none>";
    }

    MoveList<LEGAL> itr (*this);
    ss  << "\nLegal moves (" << dec << itr.size () << "): ";
    for ( ; *itr; ++itr)
    {
        ss << move_to_san (*itr, *const_cast<Position*> (this)) << " ";
    }

    return ss.str ();
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

#ifndef NDEBUG
#undef SKIP_WHITESPACE
#define SKIP_WHITESPACE()  while (isspace (uint8_t (*fen))) ++fen

bool Position::parse (Position &pos, const   char *fen, Thread *thread, bool c960, bool full)
{
    if (!fen)   return false;

    pos.clear ();

    uint8_t ch;

#undef get_next

#define get_next()         ch = uint8_t (*fen++)

    // Piece placement on Board
    for (Rank r = R_8; r >= R_1; --r)
    {
        File f = F_A;
        while (f <= F_H)
        {
            Square s = (f | r);
            get_next ();
            if (!ch) return false;

            if      (isdigit (ch))
            {
                // empty square(s)
                if ('1' > ch || ch > '8') return false;

                int8_t empty_count = (ch - '0');
                f += empty_count;

                if (f > F_NO) return false;
                //while (empty_count-- > 0) place_piece (s++, EMPTY);
            }
            else if (isalpha (ch))
            {
                size_t idx = CharPiece.find (ch);
                if (idx != string::npos)
                {
                    Piece p = Piece (idx);
                    if (EMPTY == p) return false;
                    pos.place_piece (s, p);   // put the piece on board
                }
                ++f;
            }
            else
            {
                return false;
            }
        }
        if (R_1 < r)
        {
            get_next ();
            if ('/' != ch) return false;
        }
        else
        {
            for (Color c = WHITE; c <= BLACK; ++c)
            {
                if (1 != pos.count<KING> (c)) return false;
            }
        }
    }

    SKIP_WHITESPACE ();
    // Active color
    get_next ();
    pos._active = Color (CharColor.find (ch));

    SKIP_WHITESPACE ();
    // Castling rights availability
    // Compatible with 3 standards:
    // 1-Normal FEN standard,
    // 2-Shredder-FEN that uses the letters of the columns on which the rooks began the game instead of KQkq
    // 3-X-FEN standard that, in case of Chess960, if an inner rook is associated with the castling right, the castling
    // tag is replaced by the file letter of the involved rook, as for the Shredder-FEN.
    get_next ();
    if ('-' != ch)
    {
        if (c960)
        {
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                char sym = tolower (ch);
                if ('a' <= sym && sym <= 'h')
                {
                    rook = (to_file (sym) | rel_rank (c, R_1));
                    if (ROOK != _ptype (pos[rook])) return false;
                    pos.set_castle (c, rook);
                }
                else
                {
                    return false;
                }

                get_next ();
            }
            while (ch && !isspace (ch));
        }
        else
        {
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                switch (toupper (ch))
                {
                case 'K':
                    rook = rel_sq (c, SQ_H1);
                    while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != _ptype (pos[rook]))) --rook;
                    break;
                case 'Q':
                    rook = rel_sq (c, SQ_A1);
                    while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != _ptype (pos[rook]))) ++rook;
                    break;
                default: return false;
                }
                if (ROOK != _ptype (pos[rook])) return false;
                pos.set_castle (c, rook);

                get_next ();
            }
            while (ch && !isspace (ch));
        }
    }

    SKIP_WHITESPACE ();
    // En-passant square
    get_next ();
    if ('-' != ch)
    {
        uint8_t ep_f = tolower (ch);
        if (!isalpha (ep_f)) return false;
        if ('a' > ep_f || ep_f > 'h') return false;

        uint8_t ep_r = get_next ();

        if (!isdigit (ep_r)) return false;
        if ((WHITE == pos._active && '6' != ep_r) || (BLACK == pos._active && '3' != ep_r)) return false;

        Square ep_sq  = to_square (ep_f, ep_r);
        if (pos.can_en_passant (ep_sq))
        {
            pos._si->en_passant_sq = ep_sq;
        }
    }
    // 50-move clock and game-move count
    int32_t clk50 = 0, g_move = 1;
    get_next ();
    if (full && ch)
    {
        int32_t n = 0;
        --fen;

        int32_t read = sscanf (fen, " %d %d%n", &clk50, &g_move, &n);

        if (read != 2) return false;
        fen += n;

        // Rule 50 draw case
        if (100 < clk50) return false;
        if (0 >= g_move) g_move = 1;

        get_next ();
        if (ch) return false; // NOTE: extra characters
    }

#undef get_next

    // Convert from game_move starting from 1 to game_ply starting from 0,
    // handle also common incorrect FEN with game_move = 0.
    pos._si->clock50 = (SQ_NO != pos._si->en_passant_sq) ? 0 : clk50;
    pos._game_ply = max<int16_t> (2 * (g_move - 1), 0) + (BLACK == pos._active);

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
#undef SKIP_WHITESPACE
#endif

bool Position::parse (Position &pos, const string &fen, Thread *thread, bool c960, bool full)
{
    if (fen.empty ()) return false;

    pos.clear ();

    istringstream is (fen);
    uint8_t ch;

    is >> noskipws;

    // 1. Piece placement on Board
    size_t idx;
    Square s = SQ_A8;
    while ((is >> ch) && !isspace (ch))
    {
        if (isdigit (ch))
        {
            s += Delta (ch - '0'); // Advance the given number of files
        }
        else if (isalpha (ch) && (idx = CharPiece.find (ch)) != string::npos)
        {
            Piece p = Piece (idx);
            pos.place_piece (s, _color (p), _ptype (p));
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
    is >> ch;
    pos._active = Color (CharColor.find (ch));

    // 3. Castling rights availability
    // Compatible with 3 standards:
    // 1-Normal FEN standard,
    // 2-Shredder-FEN that uses the letters of the columns on which the rooks began the game instead of KQkq
    // 3-X-FEN standard that, in case of Chess960, if an inner rook is associated with the castling right, the castling
    // tag is replaced by the file letter of the involved rook, as for the Shredder-FEN.
    is >> ch;
    if (c960)
    {

        while ((is >> ch) && !isspace (ch))
        {
            Square rook;
            Color c = isupper (ch) ? WHITE : BLACK;
            char sym = tolower (ch);
            if ('a' <= sym && sym <= 'h')
            {
                rook = (to_file (sym) | rel_rank (c, R_1));
                //if (ROOK != _ptype (pos[rook])) return false;
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
        while ((is >> ch) && !isspace (ch))
        {
            Square rook;
            Color c = isupper (ch) ? WHITE : BLACK;
            switch (toupper (ch))
            {
            case 'K':
                rook = rel_sq (c, SQ_H1);
                while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != _ptype (pos[rook]))) --rook;
                break;
            case 'Q':
                rook = rel_sq (c, SQ_A1);
                while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != _ptype (pos[rook]))) ++rook;
                break;
            default: continue;
            }

            //if (ROOK != _ptype (pos[rook])) return false;
            pos.set_castle (c, rook);
        }
    }

    // 4. En-passant square. Ignore if no pawn capture is possible
    uint8_t col, row;
    if (   ((is >> col) && (col >= 'a' && col <= 'h'))
        && ((is >> row) && (row == '3' || row == '6')))
    {
        if (   !(WHITE == pos._active && '6' != row)
            && !(BLACK == pos._active && '3' != row))
        {
            Square ep_sq = to_square (col, row);
            if (pos.can_en_passant (ep_sq))
            {
                pos._si->en_passant_sq = ep_sq;
            }
        }
    }

    // 5-6. 50-move clock and game-move count
    int32_t clk50 = 0, g_move = 1;
    if (full)
    {
        is >> skipws >> clk50 >> g_move;
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
