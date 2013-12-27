#include "Position.h"
#include <sstream>
#include <iostream>
#include <algorithm>

#include "BitBoard.h"
#include "BitScan.h"
#include "BitCount.h"
#include "BitRotate.h"
#include "MoveGenerator.h"
#include "Transposition.h"
#include "Notation.h"
#include "Thread.h"

using namespace std;
using namespace BitBoard;
using namespace MoveGenerator;

#pragma region FEN

const char *const FEN_N = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char *const FEN_X = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1";
//const string FEN_N ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
//const string FEN_X ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w HAha - 0 1");

bool _ok (const   char *fen, bool c960, bool full)
{
    ASSERT (fen);
    if (!fen)   return false;
    Position pos (int8_t (0));
    return Position::parse (pos, fen, NULL, c960, full) && pos.ok ();
}
bool _ok (const string &fen, bool c960, bool full)
{
    if (fen.empty ()) return false;
    Position pos (int8_t (0));
    return Position::parse (pos, fen, NULL, c960, full) && pos.ok ();
}

#pragma endregion

#pragma region StateInfo

// do_move() copy current state info up to 'posi_key' excluded to the new one.
// calculate the quad words (64bits) needed to be copied.
static const uint32_t SIZE_COPY_STATE = offsetof (StateInfo, posi_key); // / sizeof (uint32_t);// + 1;

void StateInfo::clear ()
{
    castle_rights = CR_NO;
    en_passant  = SQ_NO;
    cap_type    = PT_NO;
    clock50     = 0;
    null_ply    = 0;

    last_move = MOVE_NONE;

    checkers = U64 (0);

    matl_key = U64 (0);
    pawn_key = U64 (0);
    posi_key = U64 (0);
    non_pawn_matl[WHITE] = VALUE_ZERO;
    non_pawn_matl[BLACK] = VALUE_ZERO;
    psq_score = SCORE_ZERO;

    p_si    = NULL;
}

StateInfo::operator string () const
{
    return "";
}

#pragma endregion

#pragma region CheckInfo

CheckInfo::CheckInfo (const Position &pos)
{
    king_sq = pos.king_sq (~pos.active ());
    pinneds = pos.pinneds (pos.active ());
    check_discovers = pos.check_discovers (pos.active ());

    checking_bb[PAWN] = attacks_bb<PAWN> (~pos.active (), king_sq);
    checking_bb[NIHT] = attacks_bb<NIHT> (king_sq);
    checking_bb[BSHP] = attacks_bb<BSHP> (king_sq, pos.pieces ());
    checking_bb[ROOK] = attacks_bb<ROOK> (king_sq, pos.pieces ());
    checking_bb[QUEN] = checking_bb[BSHP] | checking_bb[ROOK];
    checking_bb[KING] = U64 (0);
}
void CheckInfo::clear ()
{
    //for (PType t = PAWN; t <= KING; ++t) checking_bb[t] = U64 (0);
    fill_n (checking_bb, sizeof (checking_bb) / sizeof (*checking_bb), U64 (0));

    king_sq         = SQ_NO;
    pinneds         = U64 (0);
    check_discovers = U64 (0);
}

#pragma endregion

#pragma region Position

namespace {

    CACHE_ALIGN(64)
        Score psq[CLR_NO][PT_NO][SQ_NO];

#define S(mg, eg) mk_score (mg, eg)

    // PSQT[PieceType][Square] contains Piece-Square scores. For each piece type on
    // a given square a (midgame, endgame) score pair is assigned. PSQT is defined
    // for white side, for black side the tables are symmetric.
    const Score PSQT[PT_NO][SQ_NO] =
    {
        // Pawn
        {S(  0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(  0, 0),
        S(-20,-8), S(-6,-8), S( 4,-8), S(14,-8), S(14,-8), S( 4,-8), S(-6,-8), S(-20,-8),
        S(-20,-8), S(-6,-8), S( 9,-8), S(34,-8), S(34,-8), S( 9,-8), S(-6,-8), S(-20,-8),
        S(-20,-8), S(-6,-8), S(17,-8), S(54,-8), S(54,-8), S(17,-8), S(-6,-8), S(-20,-8),
        S(-20,-8), S(-6,-8), S(17,-8), S(34,-8), S(34,-8), S(17,-8), S(-6,-8), S(-20,-8),
        S(-20,-8), S(-6,-8), S( 9,-8), S(14,-8), S(14,-8), S( 9,-8), S(-6,-8), S(-20,-8),
        S(-20,-8), S(-6,-8), S( 4,-8), S(14,-8), S(14,-8), S( 4,-8), S(-6,-8), S(-20,-8),
        S(  0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S(0,  0), S( 0, 0), S( 0, 0), S(  0, 0), },
        // Knight
        {S(-135,-104), S(-107,-79), S(-80,-55), S(-67,-42), S(-67,-42), S(-80,-55), S(-107,-79), S(-135,-104),
        S( -93, -79), S( -67,-55), S(-39,-30), S(-25,-17), S(-25,-17), S(-39,-30), S( -67,-55), S( -93, -79),
        S( -53, -55), S( -25,-30), S(  1, -6), S( 13,  5), S( 13,  5), S(  1, -6), S( -25,-30), S( -53, -55),
        S( -25, -42), S(   1,-17), S( 27,  5), S( 41, 18), S( 41, 18), S( 27,  5), S(   1,-17), S( -25, -42),
        S( -11, -42), S(  13,-17), S( 41,  5), S( 55, 18), S( 55, 18), S( 41,  5), S(  13,-17), S( -11, -42),
        S( -11, -55), S(  13,-30), S( 41, -6), S( 55,  5), S( 55,  5), S( 41, -6), S(  13,-30), S( -11, -55),
        S( -53, -79), S( -25,-55), S(  1,-30), S( 13,-17), S( 13,-17), S(  1,-30), S( -25,-55), S( -53, -79),
        S(-193,-104), S( -67,-79), S(-39,-55), S(-25,-42), S(-25,-42), S(-39,-55), S( -67,-79), S(-193,-104), },
        // Bishop
        {S(-40,-59), S(-40,-42), S(-35,-35), S(-30,-26), S(-30,-26), S(-35,-35), S(-40,-42), S(-40,-59),
        S(-17,-42), S(  0,-26), S( -4,-18), S(  0,-11), S(  0,-11), S( -4,-18), S(  0,-26), S(-17,-42),
        S(-13,-35), S( -4,-18), S(  8,-11), S(  4, -4), S(  4, -4), S(  8,-11), S( -4,-18), S(-13,-35),
        S( -8,-26), S(  0,-11), S(  4, -4), S( 17,  4), S( 17,  4), S(  4, -4), S(  0,-11), S( -8,-26),
        S( -8,-26), S(  0,-11), S(  4, -4), S( 17,  4), S( 17,  4), S(  4, -4), S(  0,-11), S( -8,-26),
        S(-13,-35), S( -4,-18), S(  8,-11), S(  4, -4), S(  4, -4), S(  8,-11), S( -4,-18), S(-13,-35),
        S(-17,-42), S(  0,-26), S( -4,-18), S(  0,-11), S(  0,-11), S( -4,-18), S(  0,-26), S(-17,-42),
        S(-17,-59), S(-17,-42), S(-13,-35), S( -8,-26), S( -8,-26), S(-13,-35), S(-17,-42), S(-17,-59), },
        // Rook
        {S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3),
        S(-12, 3), S(-7, 3), S(-2, 3), S(2, 3), S(2, 3), S(-2, 3), S(-7, 3), S(-12, 3), },
        // Queen
        {S(8,-80), S(8,-54), S(8,-42), S(8,-30), S(8,-30), S(8,-42), S(8,-54), S(8,-80),
        S(8,-54), S(8,-30), S(8,-18), S(8, -6), S(8, -6), S(8,-18), S(8,-30), S(8,-54),
        S(8,-42), S(8,-18), S(8, -6), S(8,  6), S(8,  6), S(8, -6), S(8,-18), S(8,-42),
        S(8,-30), S(8, -6), S(8,  6), S(8, 18), S(8, 18), S(8,  6), S(8, -6), S(8,-30),
        S(8,-30), S(8, -6), S(8,  6), S(8, 18), S(8, 18), S(8,  6), S(8, -6), S(8,-30),
        S(8,-42), S(8,-18), S(8, -6), S(8,  6), S(8,  6), S(8, -6), S(8,-18), S(8,-42),
        S(8,-54), S(8,-30), S(8,-18), S(8, -6), S(8, -6), S(8,-18), S(8,-30), S(8,-54),
        S(8,-80), S(8,-54), S(8,-42), S(8,-30), S(8,-30), S(8,-42), S(8,-54), S(8,-80), },
        // King
        {S(287, 18), S(311, 77), S(262,105), S(214,135), S(214,135), S(262,105), S(311, 77), S(287, 18),
        S(262, 77), S(287,135), S(238,165), S(190,193), S(190,193), S(238,165), S(287,135), S(262, 77),
        S(214,105), S(238,165), S(190,193), S(142,222), S(142,222), S(190,193), S(238,165), S(214,105),
        S(190,135), S(214,193), S(167,222), S(119,251), S(119,251), S(167,222), S(214,193), S(190,135),
        S(167,135), S(190,193), S(142,222), S( 94,251), S( 94,251), S(142,222), S(190,193), S(167,135),
        S(142,105), S(167,165), S(119,193), S( 69,222), S( 69,222), S(119,193), S(167,165), S(142,105),
        S(119, 77), S(142,135), S( 94,165), S( 46,193), S( 46,193), S( 94,165), S(142,135), S(119, 77),
        S(94,  18), S(119, 77), S( 69,105), S( 21,135), S( 21,135), S( 69,105), S(119, 77), S( 94, 18), },
    };

#undef S

    // min_attacker() is an helper function used by see() to locate the least
    // valuable attacker for the side to move, remove the attacker we just found
    // from the bitboards and scan for new X-ray attacks behind it.
    template<int32_t PT> F_INLINE
        PType min_attacker(const Bitboard bb[], const Square &dst, const Bitboard &stm_attackers, Bitboard &occupied, Bitboard &attackers)
    {
        Bitboard b = stm_attackers & bb[PT];
        if (!b) return min_attacker<PT+1>(bb, dst, stm_attackers, occupied, attackers);

        occupied -= (b & ~(b - 1));

        if (PT == PAWN || PT == BSHP || PT == QUEN)
        {
            attackers |= attacks_bb<BSHP>(dst, occupied) & (bb[BSHP] | bb[QUEN]);
        }
        if (PT == ROOK || PT == QUEN)
        {
            attackers |= attacks_bb<ROOK>(dst, occupied) & (bb[ROOK] | bb[QUEN]);
        }
        attackers &= occupied; // After X-ray that may add already processed pieces
        return PType (PT);
    }

    template<> F_INLINE
        PType min_attacker<KING>(const Bitboard bb[], const Square &dst, const Bitboard &stm_attackers, Bitboard &occupied, Bitboard &attackers)
    {
        return KING; // No need to update bitboards, it is the last cycle
    }

} // namespace

void Position::initialize ()
{
    for (PType pt = PAWN; pt <= KING; ++pt)
    {
        Score score = mk_score (PieceValue[MG][pt], PieceValue[EG][pt]);

        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            Score psq_score = score + PSQT[pt][s];
            psq[WHITE][pt][ s] = +psq_score;
            psq[BLACK][pt][~s] = -psq_score;
        }
    }
}

// operator= (pos), copy the 'pos'.
// The new born Position object should not depend on any external data
// so that why detach the state info pointer from the source one.
Position& Position::operator= (const Position &pos)
{
    memcpy (this, &pos, sizeof (Position));

    _sb = *_si;
    _si = &_sb;
    _game_nodes = 0;

    return *this;
}

#pragma region Basic methods

void  Position::place_piece (Square s, Color c, PType pt)
{
    //if (PS_NO != _piece_arr[s]) return;
    _piece_arr[s] = (c | pt);
    _color_bb[c]     += s;
    _types_bb[pt]    += s;
    _types_bb[PT_NO] += s;
    _piece_count[c][PT_NO]++;
    // Update piece list, put piece at [s] index
    _piece_index[s] = _piece_count[c][pt]++;
    _piece_list[c][pt][_piece_index[s]] = s;
}
void  Position::place_piece (Square s, Piece p)
{
    place_piece (s, p_color (p), p_type (p));
}
Piece Position::remove_piece (Square s)
{
    // WARNING: This is not a reversible operation. If we remove a piece in
    // do_move() and then replace it in undo_move() we will put it at the end of
    // the list and not in its original place, it means index[] and pieceList[]
    // are not guaranteed to be invariant to a do_move() + undo_move() sequence.

    Piece p = _piece_arr[s];
    ASSERT (PS_NO != p);

    Color c = p_color (p);
    PType pt = p_type (p);
    int32_t ps_count = _piece_count[c][pt];
    ASSERT (0 < ps_count);
    if (0 >= ps_count) return PS_NO;

    _piece_arr[s] = PS_NO;
    _color_bb[c]     -= s;
    _types_bb[pt]    -= s;
    _types_bb[PT_NO] -= s;
    _piece_count[c][PT_NO]--;
    _piece_count[c][pt]--;
    // Update piece list, remove piece at [s] index and shrink the list.
    Square last_sq = _piece_list[c][pt][_piece_count[c][pt]];
    _piece_index[last_sq] = _piece_index[s];
    _piece_list[c][pt][_piece_index[last_sq]] = last_sq;
    _piece_list[c][pt][_piece_count[c][pt]]   = SQ_NO;
    return p;
}
Piece Position::  move_piece (Square s1, Square s2)
{
    if (s1 == s2) return _piece_arr[s1];

    Piece p = _piece_arr[s1];
    if (!_ok (p)) return PS_NO;
    //if (PS_NO != _piece_arr[s2]) return PS_NO;

    Color c = p_color (p);
    PType pt = p_type (p);

    _piece_arr[s1] = PS_NO;
    _piece_arr[s2] = p;

    _color_bb[c]     -= s1;
    _types_bb[pt]    -= s1;
    _types_bb[PT_NO] -= s1;

    _color_bb[c]     += s2;
    _types_bb[pt]    += s2;
    _types_bb[PT_NO] += s2;

    // _piece_index[s1] is not updated and becomes stale. This works as long
    // as _piece_index[] is accessed just by known occupied squares.
    _piece_index[s2] = _piece_index[s1];
    _piece_index[s1] = 0;
    _piece_list[c][pt][_piece_index[s2]] = s2;

    return p;
}

#pragma endregion

#pragma region Basic properties

// Draw by: Material, 50 Move Rule, Threefold repetition, [Stalemate].
// It does not detect stalemates, this must be done by the search.
bool Position::draw () const
{
    // Draw by Material?
    if (!pieces (PAWN) && (non_pawn_material (WHITE) + non_pawn_material (BLACK) <= VALUE_MG_BISHOP))
    {
        return true;
    }

    // Draw by 50 moves Rule?
    if ( 100 <  _si->clock50 ||
        (100 == _si->clock50 && (!checkers () || generate<LEGAL> (*this).size ())))
    {
        return true;
    }

    // Draw by Threefold Repetition?
    const StateInfo *sip = _si;
    int8_t ply = min (_si->null_ply, _si->clock50);
    while (ply >= 2)
    {
        if (sip->p_si && sip->p_si->p_si)
        {
            sip = sip->p_si->p_si;
            if (sip->posi_key == _si->posi_key)
                return true; // Draw at first repetition
            ply -= 2;
        }
        else break;
    }

    //// Draw by Stalemate?
    //if (!in_check)
    //{
    //    if (!generate<LEGAL> (*this).size ()) return true;
    //}

    return false;
}
// Position consistency test, for debugging
bool Position::ok (int8_t *failed_step) const
{
    int8_t dummy_step, *step = failed_step ? failed_step : &dummy_step;

    // What features of the position should be verified?
    const bool debug_all = true;

    const bool debug_king_count  = debug_all || false;
    const bool debug_piece_count = debug_all || false;
    const bool debug_bitboards   = debug_all || false;
    const bool debug_piece_list  = debug_all || false;

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
    if (++(*step), W_KING != _piece_arr[king_sq (WHITE)]) return false;
    // step 3
    if (++(*step), B_KING != _piece_arr[king_sq (BLACK)]) return false;

    // step 4
    if (++(*step), debug_king_count)
    {
        uint8_t king_count[CLR_NO] = {};
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            Piece p = _piece_arr[s];
            if (KING == p_type (p)) ++king_count[p_color (p)];
        }
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            if (1 != king_count[c]) return false;
            if (_piece_count[c][KING] != pop_count<FULL> (pieces (c, KING))) return false;
        }
    }

    // step 5
    if (++(*step), debug_piece_count)
    {
        if (pop_count<FULL> (pieces ()) > 32) return false;
        if (piece_count () > 32) return false;
        if (piece_count () != pop_count<FULL> (pieces ())) return false;

        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (PType pt = PAWN; pt <= KING; ++pt)
            {
                if (_piece_count[c][pt] != pop_count<FULL> (pieces (c, pt))) return false;
            }
        }
    }

    // step 6
    if (++(*step), debug_bitboards)
    {
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            Bitboard colors = pieces (c);

            if (pop_count<FULL> (colors) > 16) return false; // Too many Piece of color

            // check if the number of Pawns plus the number of
            // extra Queens, Rooks, Bishops, Knights exceeds 8
            // (which can result only by promotion)
            if ((_piece_count[c][PAWN] +
                max (_piece_count[c][NIHT] - 2, 0) +
                max (_piece_count[c][BSHP] - 2, 0) +
                max (_piece_count[c][ROOK] - 2, 0) +
                max (_piece_count[c][QUEN] - 1, 0)) > 8)
            {
                return false; // Too many Promoted Piece of color
            }

            if (_piece_count[c][BSHP] > 1)
            {
                Bitboard bishops = colors & pieces (BSHP);
                uint8_t bishop_count[CLR_NO] =
                {
                    pop_count<FULL> (LT_SQ_bb & bishops),
                    pop_count<FULL> (DR_SQ_bb & bishops),
                };

                if ((_piece_count[c][PAWN] +
                    max (bishop_count[WHITE] - 1, 0) +
                    max (bishop_count[BLACK] - 1, 0)) > 8)
                {
                    return false; // Too many Promoted BISHOP of color
                }
            }

            // There should be one and only one KING of color
            Bitboard kings = colors & pieces (KING);
            if (!kings || more_than_one (kings)) return false;
        }

        // The intersection of the white and black pieces must be empty
        if (pieces (WHITE) & pieces (BLACK)) return false;

        Bitboard occ = pieces ();
        // The union of the white and black pieces must be equal to occupied squares
        if ((pieces (WHITE) | pieces (BLACK)) != occ) return false;
        if ((pieces (WHITE) ^ pieces (BLACK)) != occ) return false;

        // The intersection of separate piece type must be empty
        for (PType pt1 = PAWN; pt1 <= KING; ++pt1)
        {
            for (PType pt2 = PAWN; pt2 <= KING; ++pt2)
            {
                if (pt1 != pt2 && (pieces (pt1) & pieces (pt2))) return false;
            }
        }

        // The union of separate piece type must be equal to occupied squares
        if ((pieces (PAWN) | pieces (NIHT) | pieces (BSHP) | pieces (ROOK) | pieces (QUEN) | pieces (KING)) != occ) return false;
        if ((pieces (PAWN) ^ pieces (NIHT) ^ pieces (BSHP) ^ pieces (ROOK) ^ pieces (QUEN) ^ pieces (KING)) != occ) return false;

        // PAWN rank should not be 1/8
        if ((pieces (PAWN) & (R1_bb | R8_bb))) return false;
    }

    // step 7
    if (++(*step), debug_piece_list)
    {
        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (PType pt = PAWN; pt <= KING; ++pt)
            {
                for (int32_t i = 0; i < _piece_count[c][pt]; ++i)
                {
                    if (!_ok (_piece_list[c][pt][i]))
                        return false;
                    if (_piece_arr[_piece_list[c][pt][i]] != (c | pt)) return false;
                    if (_piece_index[_piece_list[c][pt][i]] != i) return false;
                }
            }
        }
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            if (_piece_index[s] > 16)
                return false;
        }
    }

    // step 8
    if (++(*step), debug_king_capture)
    {
        if (checkers (~_active)) return false;
    }

    // step 9
    if (++(*step), debug_checker_count)
    {
        if (pop_count<FULL>(checkers ()) > 2) return false;
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
                if ((castle_right (c, king_sq (c)) & cr) != cr) return false;
                Square rook = castle_rook (c, cs);
                if ((c | ROOK) != _piece_arr[rook] || castle_right (c, rook) != cr) return false;
            }
        }
    }

    // step 11
    if (++(*step), debug_en_passant)
    {
        Square ep_sq = _si->en_passant;
        if (SQ_NO != ep_sq)
        {
            if (!can_en_passant (ep_sq)) return false;
        }
    }

    // step 12
    if (++(*step), debug_clock50)
    {
        if (clock50 () > 100) return false;
    }

    // step 13
    if (++(*step), debug_matl_key)
    {
        if (ZobGlob.compute_matl_key (*this) != matl_key ()) return false;
    }
    // step 14
    if (++(*step), debug_pawn_key)
    {
        if (ZobGlob.compute_pawn_key (*this) != pawn_key ()) return false;
    }

    // step 15
    if (++(*step), debug_posi_key)
    {
        //cout << hex << uppercase << posi_key () << endl;
        if (ZobGlob.compute_posi_key (*this) != posi_key ()) return false;
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

// see() is a static exchange evaluator:
// It tries to estimate the material gain or loss resulting from a move.
// Parameter 'asymm_threshold' takes tempi into account.
// If the side who initiated the capturing sequence does the last capture,
// he loses a tempo and if the result is below 'asymm_threshold'
// the capturing sequence is considered bad.
int32_t Position::see (Move m, int32_t asymm_threshold) const
{
    ASSERT (_ok(m));

    Square org = org_sq(m);
    Square dst = dst_sq(m);

    Color stm = p_color (_piece_arr[org]);
    int32_t swap_list[32], index = 1;
    swap_list[0] = PieceValue[MG][p_type (_piece_arr[dst])];

    Bitboard occupied = pieces () - org;

    MType mt = m_type (m);
    // Castle moves are implemented as king capturing the rook so cannot be
    // handled correctly. Simply return 0 that is always the correct value
    // unless in the rare case the rook ends up under attack.
    if (mt == CASTLE) return 0;

    if (mt == ENPASSANT)
    {
        occupied -= dst - pawn_push (stm); // Remove the captured pawn
        swap_list[0] = PieceValue[MG][PAWN];
    }

    // Find all attackers to the destination square, with the moving piece
    // removed, but possibly an X-ray attacker added behind it.
    Bitboard attackers = attackers_to(dst, occupied) & occupied;

    // If the opponent has no attackers we are finished
    stm = ~stm;
    Bitboard stm_attackers = attackers & pieces (stm);
    if (!stm_attackers) return swap_list[0];

    // The destination square is defended, which makes things rather more
    // difficult to compute. We proceed by building up a "swap list" containing
    // the material gain or loss at each stop in a sequence of captures to the
    // destination square, where the sides alternately capture, and always
    // capture with the least valuable piece. After each capture, we look for
    // new X-ray attacks from behind the capturing piece.
    PType cpt = p_type (_piece_arr[org]);

    do
    {
        ASSERT (index < 32);

        // Add the new entry to the swap list
        swap_list[index] = -swap_list[index - 1] + PieceValue[MG][cpt];
        ++index;

        // Locate and remove the next least valuable attacker
        cpt = min_attacker<PAWN>(_types_bb, dst, stm_attackers, occupied, attackers);
        stm = ~stm;
        stm_attackers = attackers & pieces (stm);

        // Stop before processing a king capture
        if (cpt == KING && stm_attackers)
        {
            swap_list[index++] = VALUE_MG_QUEEN * 16;
            break;
        }
    }
    while (stm_attackers);

    // If we are doing asymmetric SEE evaluation and the same side does the first
    // and the last capture, he loses a tempo and gain must be at least worth
    // 'asymm_threshold', otherwise we replace the score with a very low value,
    // before negamaxing.
    if (asymm_threshold)
    {
        for (int32_t i = 0; i < index; i += 2)
        {
            if (swap_list[i] < asymm_threshold)
                swap_list[i] = -VALUE_MG_QUEEN * 16;
        }
    }

    // Having built the swap list, we negamax through it to find the best
    // achievable score from the point of view of the side to move.
    while (--index)
    {
        swap_list[index - 1] = min (-swap_list[index], swap_list[index - 1]);
    }

    return swap_list[0];
}

int32_t Position::see_sign (Move m) const
{
    ASSERT (_ok(m));

    // Early return if SEE cannot be negative because captured piece value
    // is not less then capturing one. Note that king moves always return
    // here because king midgame value is set to 0.
    if (PieceValue[MG][p_type (moved_piece (m))] <= PieceValue[MG][p_type (_piece_arr[dst_sq (m)])])
    {
        return 1;
    }

    return see (m);
}

#pragma endregion

#pragma region Move properties

// moved_piece() return piece moved on move
Piece Position::   moved_piece (Move m) const
{
    ASSERT (_ok (m));
    if (!_ok (m)) return PS_NO;
    return _piece_arr[org_sq (m)];
}
// captured_piece() return piece captured by moving piece
Piece Position::captured_piece (Move m) const
{
    ASSERT (_ok (m));
    if (!_ok (m)) return PS_NO;

    Square org = org_sq (m);
    Square dst = dst_sq (m);
    Color pasive = ~_active;
    Piece mp =  _piece_arr[org];
    PType mpt = p_type (mp);

    Square cap = dst;

    switch (m_type (m))
    {
    case CASTLE:   return PS_NO; break;

    case ENPASSANT:
        if (PAWN == mpt)
        {
            cap += ((WHITE == _active) ? DEL_S : DEL_N);

            Bitboard captures = attacks_bb<PAWN> (pasive, en_passant ()) & pieces (_active, PAWN);

            return (captures) ? _piece_arr[cap] : PS_NO;
        }
        return PS_NO;
        break;

    case PROMOTE:
        if (PAWN != mpt) return PS_NO;
        if (R_7 != rel_rank (_active, org)) return PS_NO;
        if (R_8 != rel_rank (_active, dst)) return PS_NO;

        // NOTE: no break
    case NORMAL:
        if (PAWN == mpt)
        {
            // check not pawn push and can capture
            if (file_dist (dst, org) != 1) return PS_NO;
            Bitboard captures = attacks_bb<PAWN> (pasive, dst) & pieces (_active);
            return ((captures) ? _piece_arr[cap] : PS_NO);
        }
        return _piece_arr[cap];

        break;
    }
    return PS_NO;
}

// pseudo_legal(m) tests whether a random move is pseudo-legal.
// It is used to validate moves from TT that can be corrupted
// due to SMP concurrent access or hash position key aliasing.
bool Position::pseudo_legal (Move m) const
{
    if (!_ok (m)) return false;
    Square org = org_sq (m);
    Square dst = dst_sq (m);

    Color active = _active;
    Color pasive = ~active;

    Piece p = _piece_arr[org];
    PType pt = p_type (p);

    // If the org square is not occupied by a piece belonging to the side to move,
    // then the move is obviously not legal.
    if ((PS_NO == p) || (active != p_color (p)) || (PT_NO == pt)) return false;

    Square cap = dst;
    PType ct;

    MType mt = m_type (m);

    switch (mt)
    {
    case CASTLE:
        {
            // Check whether the destination square is attacked by the opponent.
            // Castling moves are checked for legality during move generation.
            if (KING != pt) return false;

            if (R_1 != rel_rank (active, org) ||
                R_1 != rel_rank (active, dst))
            {
                return false;
            }

            if (castle_impeded (active)) return false;
            if (!can_castle (active)) return false;
            if (checkers ()) return false;

            ct = PT_NO;

            bool king_side = (dst > org);
            //CSide cs = king_side ? CS_K : CS_Q;
            Delta step = king_side ? DEL_E : DEL_W;
            Bitboard enemies = pieces (pasive);
            Square s = org + step;
            while (s != dst + step)
            {
                if (attackers_to (s) & enemies)
                {
                    return false;
                }
                s += step;
            }

            return true;
        }
        break;

    case ENPASSANT:
        {
            if (PAWN != pt) return false;
            if (en_passant () != dst) return false;
            if (R_5 != rel_rank (active, org)) return false;
            if (R_6 != rel_rank (active, dst)) return false;
            if (!empty (dst)) return false;

            cap += pawn_push (pasive);
            ct = PAWN;
            if ((pasive | PAWN) != _piece_arr[cap]) return false;
        }
        break;

    case PROMOTE:
        {
            if (PAWN != pt) return false;
            if (R_7 != rel_rank (active, org)) return false;
            if (R_8 != rel_rank (active, dst)) return false;
        }
        ct = p_type (_piece_arr[cap]);
        break;

    case NORMAL:
        // Is not a promotion, so promotion piece must be empty
        if (PAWN != (prom_type (m) - NIHT)) return false;
        ct = p_type (_piece_arr[cap]);
        break;
    }

    if (PT_NO != ct)
    {
        if (!_ok (ct)) return false;
        if (KING == ct) return false;
    }

    // The destination square cannot be occupied by a friendly piece
    if (pieces (active) & dst) return false;

    // Handle the special case of a piece move
    if (PAWN == pt)
    {
        // Move direction must be compatible with pawn color
        // We have already handled promotion moves, so destination
        Delta delta = dst - org;
        switch (active)
        {
        case WHITE:
            {
                if (delta < DEL_O) return false;
                Rank r_org = _rank (org);
                if (r_org == R_1 || r_org == R_8) return false;
                Rank r_dst = _rank (dst);
                if (r_dst == R_1 || r_dst == R_2) return false;
            }
            break;
        case BLACK:
            {
                if (delta > DEL_O) return false;
                Rank r_org = _rank (org);
                if (r_org == R_8 || r_org == R_1) return false;
                Rank r_dst = _rank (dst);
                if (r_dst == R_8 || r_dst == R_7) return false;
            }
            break;
        }
        // Proceed according to the square delta between the origin and destiny squares.
        switch (delta)
        {
        case DEL_N:
        case DEL_S:
            // Pawn push. The destination square must be empty.
            if (PS_NO != _piece_arr[cap]) return false;
            break;

        case DEL_NE:
        case DEL_NW:
        case DEL_SE:
        case DEL_SW:
            // Capture. The destination square must be occupied by an enemy piece
            // (en passant captures was handled earlier).
            if (PT_NO == ct || active == p_color (_piece_arr[cap])) return false;
            // cap and org files must be one del apart, avoids a7h5
            if (1 != file_dist (cap, org)) return false;
            break;

        case DEL_NN:
            // Double white pawn push. The destination square must be on the fourth
            // rank, and both the destination square and the square between the
            // source and destination squares must be empty.

            //if (WHITE != active) return false;
            if (R_2 != _rank (org) ||
                R_4 != _rank (dst) ||
                PS_NO != _piece_arr[cap] ||
                PS_NO != _piece_arr[org + DEL_N])
                return false;
            break;

        case DEL_SS:
            // Double black pawn push. The destination square must be on the fifth
            // rank, and both the destination square and the square between the
            // source and destination squares must be empty.

            //if (BLACK != active) return false;
            if (R_7 != _rank (org) ||
                R_5 != _rank (dst) ||
                PS_NO != _piece_arr[cap] ||
                PS_NO != _piece_arr[org + DEL_S])
                return false;
            break;

        default:
            return false;
            break;
        }
    }
    else
    {
        if (!(attacks_from (p, org) & dst)) return false;
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
                // Our move must be a blocking evasion of the checking piece or a capture of the checking en-passant pawn
                if (!(chkrs & cap) &&
                    !(betwen_sq_bb (scan_lsq (chkrs), king_sq (active)) & dst)) return false;
            }
            else
            {
                // Our move must be a blocking evasion or a capture of the checking piece
                if (!((betwen_sq_bb (scan_lsq (chkrs), king_sq (active)) | chkrs) & dst)) return false;
            }
        }
        // In case of king moves under check we have to remove king so to catch
        // as invalid moves like B1A1 when opposite queen is on C1.
        else
        {
            if (attackers_to (dst, pieces () - org) & pieces (pasive)) return false;
        }
    }

    return true;
}
// legal(m, pinned) tests whether a pseudo-legal move is legal
bool Position::       legal (Move m, Bitboard pinned) const
{
    ASSERT (_ok (m));
    //ASSERT (pseudo_legal (m));
    ASSERT (pinned == pinneds (_active));

    //Position c_pos(pos);
    //if (c_pos.do_move(m))
    //{
    //    Color active = c_pos.active ();
    //    Color pasive = ~active;
    //    Square fk_sq = c_pos.king_sq (pasive);
    //    Bitboard enemies  = c_pos.pieces (active);
    //    Bitboard checkers = attackers_to(c_pos, fk_sq) & enemies;
    //    uint8_t numChecker = pop_count<FULL> (checkers);
    //    return !numChecker;
    //}
    //return false;

    Color active = _active;
    Color pasive = ~active;
    Square org = org_sq (m);
    Square dst = dst_sq (m);

    Piece p  = _piece_arr[org];
    Color pc = p_color (p);
    PType pt = p_type  (p);
    ASSERT ((active == pc) && (PT_NO != pt));

    Square fk_sq = king_sq (active);
    ASSERT ((active | KING) == _piece_arr[fk_sq]);

    MType mt = m_type (m);
    switch (mt)
    {
    case CASTLE:
        // Castling moves are checked for legality during move generation.
        return true;
        break;
    case ENPASSANT:
        // En-passant captures are a tricky special case. Because they are rather uncommon,
        // we do it simply by testing whether the king is attacked after the move is made.
        {
            Square cap = dst + pawn_push (pasive);

            ASSERT (dst == en_passant ());
            ASSERT ((active | PAWN) == _piece_arr[org]);
            ASSERT ((pasive | PAWN) == _piece_arr[cap]);
            ASSERT (PS_NO == _piece_arr[dst]);
            ASSERT ((pasive | PAWN) == _piece_arr[cap]);

            Bitboard mocc = pieces () - org - cap + dst;

            // if any attacker then in check & not legal
            return !(
                (attacks_bb<ROOK> (fk_sq, mocc) & pieces (pasive, QUEN, ROOK)) |
                (attacks_bb<BSHP> (fk_sq, mocc) & pieces (pasive, QUEN, BSHP)));
        }
        break;
    }

    if (KING == pt)
    {
        // In case of king moves under check we have to remove king so to catch
        // as invalid moves like B1-A1 when opposite queen is on SQ_C1.
        // check whether the destination square is attacked by the opponent.
        Bitboard mocc = pieces () - org; // + dst;
        return !(attackers_to (dst, mocc) & pieces (pasive));
    }

    // A non-king move is legal if and only if it is not pinned or it
    // is moving along the ray towards or away from the king or
    // is a blocking evasion or a capture of the checking piece.
    return !pinned || !(pinned & org) || sqrs_aligned (org, dst, fk_sq);
}
bool Position::       legal (Move m) const
{
    return legal (m, pinneds (_active));
}
// capture(m) tests move is capture
bool Position::capture (Move m) const
{
    ASSERT (_ok (m));
    //ASSERT (pseudo_legal (m));

    MType mt = m_type (m);
    switch (mt)
    {
    case CASTLE:
        return false;
        break;
    case ENPASSANT:
        return  (SQ_NO != _si->en_passant);
        break;

    case NORMAL:
    case PROMOTE:
        {
            Square dst = dst_sq (m);
            Piece cp   = _piece_arr[dst];
            return (~_active == p_color (cp)) && (KING != p_type (cp));
        }
        break;
    }
    return false;
}
// capture_or_promotion(m) tests move is capture or promotion
bool Position::capture_or_promotion (Move m) const
{
    ASSERT (_ok (m));
    //ASSERT (pseudo_legal (m));

    //MType mt = m_type (m);
    //return (NORMAL != mt) ? (CASTLE != mt) : !empty (dst_sq (m));

    switch (m_type (m))
    {
    case CASTLE:    return false; break;
    case PROMOTE:   return true;  break;
    case ENPASSANT: return (SQ_NO != _si->en_passant); break;
    case NORMAL:
        {
            Square dst  = dst_sq (m);
            Piece cp    = _piece_arr[dst];
            return (PS_NO != cp) && (~_active == p_color (cp)) && (KING != p_type (cp));
        }
    }
    return false;
}
// check(m) tests whether a pseudo-legal move gives a check
bool Position::check     (Move m, const CheckInfo &ci) const
{
    ASSERT (_ok (m));
    //ASSERT (pseudo_legal (m));
    ASSERT (ci.check_discovers == check_discovers (_active));

    //if (!legal (m)) return false;

    Square org = org_sq (m);
    Square dst = dst_sq (m);

    Piece p = _piece_arr[org];
    Color pc = p_color (p);
    PType pt = p_type  (p);

    // Direct check ?
    if (ci.checking_bb[pt] & dst) return true;

    Color active = _active;
    Color pasive = ~active;

    // Discovery check ?
    if (UNLIKELY (ci.check_discovers))
    {
        if (ci.check_discovers & org)
        {
            //  need to verify also direction for pawn and king moves
            if (((PAWN != pt) && (KING != pt)) ||
                !sqrs_aligned (org, dst, king_sq (pasive)))
                return true;
        }
    }

    MType mt = m_type (m);
    // Can we skip the ugly special cases ?
    if (NORMAL == mt) return false;

    Square ek_sq = king_sq (pasive);
    Bitboard occ = pieces ();
    switch (mt)
    {
    case CASTLE:
        // Castling with check ?
        {
            bool king_side = (dst > org);
            Square org_king = org;
            Square org_rook = dst; // 'King captures the rook' notation
            Square dst_king = rel_sq (_active, king_side ? SQ_WK_K : SQ_WK_Q);
            Square dst_rook = rel_sq (_active, king_side ? SQ_WR_K : SQ_WR_Q);

            return
                (attacks_bb<ROOK> (dst_rook) & ek_sq) &&
                (attacks_bb<ROOK> (dst_rook, (occ - org_king - org_rook + dst_king + dst_rook)) & ek_sq);
        }
        break;

    case ENPASSANT:
        // En passant capture with check ?
        // already handled the case of direct checks and ordinary discovered check,
        // the only case need to handle is the unusual case of a discovered check through the captured pawn.
        {
            Square cap = _file (dst) | _rank (org);
            Bitboard mocc = occ - org - cap + dst;
            return // if any attacker then in check
                (attacks_bb<ROOK> (ek_sq, mocc) & pieces (_active, QUEN, ROOK)) |
                (attacks_bb<BSHP> (ek_sq, mocc) & pieces (_active, QUEN, BSHP));
        }
        break;

    case PROMOTE:
        // Promotion with check ?
        return (attacks_from ((active | prom_type (m)), dst, occ - org + dst) & ek_sq);
        break;

    default:
        ASSERT (false);
        return false;
    }
}
// checkmate(m) tests whether a pseudo-legal move gives a checkmate
bool Position::checkmate (Move m, const CheckInfo &ci) const
{
    ASSERT (_ok (m));
    if (!check (m, ci)) return false;

    Position pos = *this;
    StateInfo si;
    pos.do_move (m, si);
    return !generate<EVASION> (pos).size ();
}

#pragma endregion

#pragma region Basic methods
// clear() clear the position
void Position::clear ()
{
    memset (this, 0, sizeof (Position));

    for (int32_t i = 0; i < PT_NO; ++i)
    {
        for (int32_t j = 0; j < 16; ++j)
        {
            _piece_list[WHITE][i][j] = _piece_list[BLACK][i][j] = SQ_NO;
        }
    }
    //fill (
    //    _castle_rooks[0] + 0,
    //    _castle_rooks[0] + sizeof (_castle_rooks) / sizeof (**_castle_rooks),
    //    SQ_NO);
    //
    //_game_ply   = 1;

    _sb.en_passant = SQ_NO;
    _sb.cap_type   = PT_NO;
    _si = &_sb;
}
// setup() sets the fen on the position
bool Position::setup (const   char *fen, Thread *thread, bool c960, bool full)
{
    Position pos (int8_t (0));
    if (parse (pos, fen, thread, c960, full) && pos.ok ())
    {
        *this = pos;
        return true;
    }
    return false;
}
bool Position::setup (const string &fen, Thread *thread, bool c960, bool full)
{
    Position pos (int8_t (0));
    if (parse (pos, fen, thread, c960, full) && pos.ok ())
    {
        *this = pos;
        return true;
    }
    return false;
}

// set_castle() set the castling for the particular color & rook
void Position::set_castle (Color c, Square org_rook)
{
    Square org_king = king_sq (c);

    ASSERT ((org_king != org_rook));
    if (org_king == org_rook) return;

    bool king_side = (org_rook > org_king);
    CSide cs = (king_side ? CS_K : CS_Q);
    CRight cr = mk_castle_right (c, cs);
    Square dst_rook = rel_sq (c, king_side ? SQ_WR_K : SQ_WR_Q);
    Square dst_king = rel_sq (c, king_side ? SQ_WK_K : SQ_WK_Q);

    _si->castle_rights |= cr;

    _castle_rights[c][_file (org_king)] |= cr;
    _castle_rights[c][_file (org_rook)] |= cr;

    _castle_rooks[c][cs] = org_rook;

    for (Square s = min (org_rook, dst_rook); s <= max (org_rook, dst_rook); ++s)
    {
        if (org_king != s && org_rook != s)
        {
            _castle_paths[c][cs] += s;
        }
    }
    for (Square s = min (org_king, dst_king); s <= max (org_king, dst_king); ++s)
    {
        if (org_king != s && org_rook != s)
        {
            _castle_paths[c][cs] += s;
        }
    }
}
// can_en_passant() tests the en-passant square
bool Position::can_en_passant (Square ep_sq) const
{
    if (SQ_NO == ep_sq) return false;
    Color active = _active;
    Color pasive = ~active;
    if (R_6 != rel_rank (active, ep_sq)) return false;
    Square cap = ep_sq + pawn_push (pasive);
    //if (!(pieces (pasive, PAWN) & cap)) return false;
    if ((pasive | PAWN) != _piece_arr[cap]) return false;

    Bitboard pawns_ep = attacks_bb<PAWN> (pasive, ep_sq) & pieces (active, PAWN);
    if (!pawns_ep) return false;
    ASSERT (pop_count<FULL> (pawns_ep) <= 2);

    MoveList mov_lst;
    while (pawns_ep) mov_lst.emplace_back (mk_move<ENPASSANT> (pop_lsq (pawns_ep), ep_sq));

    // Check en-passant is legal for the position

    Square fk_sq = king_sq (active);
    Bitboard occ = pieces ();
    for (MoveList::const_iterator itr = mov_lst.cbegin (); itr != mov_lst.cend (); ++itr)
    {
        Move m = *itr;
        Square org = org_sq (m);
        Square dst = dst_sq (m);
        Bitboard mocc = occ - org - cap + dst;
        if (!(
            (attacks_bb<ROOK> (fk_sq, mocc) & pieces (pasive, QUEN, ROOK)) |
            (attacks_bb<BSHP> (fk_sq, mocc) & pieces (pasive, QUEN, BSHP))))
        {
            return true;
        }
    }

    return false;
}
bool Position::can_en_passant (File   ep_f) const
{
    return can_en_passant (ep_f | rel_rank (_active, R_6));
}

// compute_psq_score () computes the incremental scores for the middle
// game and the endgame. These functions are used to initialize the incremental
// scores when a new position is set up, and to verify that the scores are correctly
// updated by do_move and undo_move when the program is running in debug mode.
Score Position::compute_psq_score () const
{
    Score score = SCORE_ZERO;
    Bitboard occ = pieces ();
    while (occ)
    {
        Square s = pop_lsq (occ);
        Piece  p = _piece_arr[s];
        score += psq[p_color (p)][p_type (p)][s];
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
    for (PType pt = NIHT; pt <= QUEN; ++pt)
    {
        value += _piece_count[c][pt] * PieceValue[MG][pt];
    }
    return value;
}

#pragma region Do/Undo Move
// castle_king_rook() exchanges the king and rook
void Position::castle_king_rook (Square org_king, Square dst_king, Square org_rook, Square dst_rook)
{
    // Remove both pieces first since squares could overlap in chess960
    remove_piece (org_king);
    remove_piece (org_rook);

    place_piece (dst_king, _active, KING);
    place_piece (dst_rook, _active, ROOK);
}

// do_move() do the move with checking info
void Position::do_move (Move m, StateInfo &si_n, const CheckInfo *ci)
{
    //ASSERT (_ok (m));
    ASSERT (pseudo_legal (m));
    ASSERT (&si_n != _si);

    Key posi_k = _si->posi_key;

    // Copy some fields of old state to new StateInfo object except the ones
    // which are going to be recalculated from scratch anyway, 
    memcpy (&si_n, _si, SIZE_COPY_STATE);

    // switch state pointer to point to the new, ready to be updated, state.
    si_n.p_si = _si;
    _si = &si_n;

    Color active = _active;
    Color pasive = ~active;

    Square org   = org_sq (m);
    Square dst   = dst_sq (m);
    Piece p      = _piece_arr[org];
    PType pt     = p_type (p);

    ASSERT ((PS_NO != p) &&
        (active == p_color (p)) &&
        (PT_NO != pt));
    ASSERT ((PS_NO == _piece_arr[dst]) ||
        (pasive == p_color (_piece_arr[dst])) ||
        (CASTLE == m_type (m)));

    Square cap = dst;
    PType  ct  = PT_NO;

    MType mt = m_type (m);
    // Pick capture piece and check validation
    switch (mt)
    {
    case CASTLE:
        ASSERT (KING == pt);
        ASSERT ((active | ROOK) == _piece_arr[dst]);
        ct = PT_NO;
        break;

    case ENPASSANT:
        ASSERT (PAWN == pt);               // Moving type must be pawn
        ASSERT (dst == _si->en_passant);    // Destination must be en-passant
        ASSERT (R_5 == rel_rank (active, org));
        ASSERT (R_6 == rel_rank (active, dst));
        ASSERT (PS_NO == _piece_arr[cap]);      // Capture Square must be empty

        cap += pawn_push (pasive);
        //ASSERT (!(pieces (pasive, PAWN) & cap));
        ASSERT ((pasive | PAWN) == _piece_arr[cap]);
        ct = PAWN;
        break;

    case PROMOTE:
        ASSERT (PAWN == pt);        // Moving type must be PAWN
        ASSERT (R_7 == rel_rank (active, org));
        ASSERT (R_8 == rel_rank (active, dst));
        ct = p_type (_piece_arr[cap]);
        ASSERT (PAWN == ct);
        break;

    case NORMAL:
        ASSERT (PAWN == (prom_type (m) - NIHT));
        if (PAWN == pt)
        {
            uint8_t del_f = file_dist (cap, org);
            ASSERT (0 == del_f || 1 == del_f);
            if (0 == del_f) break;
        }
        ct = p_type (_piece_arr[cap]);
        break;
    }

    ASSERT (KING != ct);   // can't capture the KING

    // ------------------------

    // Handle all captures
    if (PT_NO != ct)
    {
        // Remove captured piece
        remove_piece (cap);

        // If the captured piece is a pawn
        if (PAWN == ct) // Update pawn hash key
        {
            _si->pawn_key ^= ZobGlob._.ps_sq[pasive][PAWN][cap];
        }
        else             // Update non-pawn material
        {
            _si->non_pawn_matl[pasive] -= PieceValue[MG][ct];
        }

        // Update Hash key of material situation
        _si->matl_key ^= ZobGlob._.ps_sq[pasive][ct][piece_count (pasive, ct)];
        // Update Hash key of position
        posi_k ^= ZobGlob._.ps_sq[pasive][ct][cap];

        if (_thread) prefetch ((char*) _thread->material_table[_si->matl_key]);

        // Update incremental scores
        _si->psq_score -= psq[pasive][ct][cap];

        // Reset Rule-50 draw counter
        _si->clock50 = 0;
    }
    else
    {
        _si->clock50 = (PAWN == pt) ? 0 : _si->clock50 + 1;
    }

    // Reset old en-passant square
    if (SQ_NO != _si->en_passant)
    {
        posi_k ^= ZobGlob._.en_passant[_file (_si->en_passant)];
        _si->en_passant = SQ_NO;
    }

    // do move according to move type
    switch (mt)
    {
    case CASTLE:
        // Move the piece. The tricky Chess960 castle is handled earlier
        {
            bool king_side = (dst > org);
            Square org_king = org;
            Square dst_king = rel_sq (active, king_side ? SQ_WK_K : SQ_WK_Q);
            Square org_rook = dst; // castle is always encoded as "king captures friendly rook"
            Square dst_rook = rel_sq (active, king_side ? SQ_WR_K : SQ_WR_Q);

            ASSERT (org_rook == castle_rook (active, king_side ? CS_K : CS_Q));
            castle_king_rook (org_king, dst_king, org_rook, dst_rook);

            //_si->psq_score += psq_delta(make_piece(_active, ROOK), org_rook, dst_rook);
            posi_k ^= ZobGlob._.ps_sq[_active][KING][org_king] ^ ZobGlob._.ps_sq[_active][KING][dst_king];
            posi_k ^= ZobGlob._.ps_sq[_active][ROOK][org_rook] ^ ZobGlob._.ps_sq[_active][ROOK][dst_rook];

            _si->psq_score += psq[active][KING][dst_king] - psq[active][KING][org_king];
            _si->psq_score += psq[active][ROOK][dst_rook] - psq[active][ROOK][org_rook];
        }
        break;
    case PROMOTE:
        {
            PType ppt = prom_type (m);
            // Replace the PAWN with the Promoted piece
            remove_piece (org);
            place_piece (dst, active, ppt);

            _si->matl_key ^=
                ZobGlob._.ps_sq[active][PAWN][piece_count (active, PAWN)] ^
                ZobGlob._.ps_sq[active][ppt][piece_count (active, ppt) - 1];

            _si->pawn_key ^= ZobGlob._.ps_sq[active][PAWN][org];

            posi_k ^= ZobGlob._.ps_sq[active][PAWN][org] ^ ZobGlob._.ps_sq[active][ppt][dst];

            // Update incremental score
            _si->psq_score += psq[active][ppt][dst] - psq[active][PAWN][org];
            // Update material
            _si->non_pawn_matl[active] += PieceValue[MG][ppt];
        }
        break;

    case ENPASSANT:
    case NORMAL:

        move_piece (org, dst);
        posi_k ^= ZobGlob._.ps_sq[active][pt][org] ^ ZobGlob._.ps_sq[active][pt][dst];

        if (PAWN == pt)
        {
            // Update pawns hash key
            _si->pawn_key ^= ZobGlob._.ps_sq[active][PAWN][org] ^ ZobGlob._.ps_sq[active][PAWN][dst];
        }

        _si->psq_score += psq[active][pt][dst] - psq[active][pt][org];

        break;
    }

    // Update castle rights if needed
    if (_si->castle_rights)
    {
        int32_t cr = _si->castle_rights & (castle_right (active, org) | castle_right (pasive, dst));
        if (cr)
        {
            Bitboard b = cr;
            _si->castle_rights &= ~cr;
            while (b)
            {
                posi_k ^= ZobGlob._.castle_right[0][pop_lsq (b)];
            }
        }
    }

    // Updates checkers if any
    _si->checkers = U64 (0);
    if (ci)
    {
        if (NORMAL == mt)
        {
            // Direct check ?
            if (ci->checking_bb[pt] & dst)
            {
                _si->checkers += dst;
            }
            // Discovery check ?
            if (QUEN != pt)
            {
                if ((ci->check_discovers) && (ci->check_discovers & org))
                {
                    if (ROOK != pt)
                    {
                        _si->checkers |= attacks_bb<ROOK> (king_sq (pasive)) & pieces (active, QUEN, ROOK);
                    }
                    if (BSHP != pt)
                    {
                        _si->checkers |= attacks_bb<BSHP> (king_sq (pasive)) & pieces (active, QUEN, BSHP);
                    }
                }
            }
        }
        else
        {
            _si->checkers = checkers (pasive);
        }
    }

    // switch side to move
    _active = pasive;
    posi_k ^= ZobGlob._.mover_side;

    // Handle pawn en-passant square setting
    if (PAWN == pt)
    {
        int8_t iorg = org;
        int8_t idst = dst;
        if (16 == (idst ^ iorg))
        {
            Square ep_sq = Square ((idst + iorg) / 2);
            if (can_en_passant (ep_sq))
            {
                _si->en_passant = ep_sq;
                posi_k ^= ZobGlob._.en_passant[_file (ep_sq)];
            }
        }

        if (_thread) prefetch ((char*) _thread->pawns_table[_si->pawn_key]);
    }

    // Update the key with the final value
    _si->posi_key   = posi_k;

    _si->cap_type   = ct;
    _si->last_move  = m;
    _si->null_ply++;
    ++_game_ply;
    ++_game_nodes;

    ASSERT (ok ());
}
void Position::do_move (Move m, StateInfo &si_n)
{
    CheckInfo ci (*this);
    do_move (m, si_n, check (m, ci) ? &ci : NULL);
}
// do_move() do the move from string (CAN)
void Position::do_move (string &can, StateInfo &si_n)
{
    Move move = move_from_can (can, *this);
    if (MOVE_NONE != move) do_move (move, si_n);
}
// undo_move() undo last move for state info
void Position::undo_move ()
{
    ASSERT (_si->p_si);
    if (!(_si->p_si)) return;

    if (!_ok (_piece_list[0][2][0]))
    {
        int x = 0;
    }

    Move m = _si->last_move;
    ASSERT (_ok (m));

    Square org = org_sq (m);
    Square dst = dst_sq (m);

    Color pasive = _active;
    Color active = _active = ~_active; // switch

    Piece mp  = _piece_arr[dst];
    PType mpt = p_type (mp);

    MType mt = m_type (m);
    ASSERT (PS_NO == _piece_arr[org] || CASTLE == mt);

    PType cpt = _si->cap_type;
    ASSERT (KING != cpt);

    Square cap = dst;

    // undo move according to move type
    switch (mt)
    {
    case PROMOTE:
        {
            PType prom = prom_type (m);

            ASSERT (prom == mpt);
            ASSERT (R_8 == rel_rank (active, dst));
            ASSERT (NIHT <= prom && prom <= QUEN);
            // Replace the promoted piece with the PAWN
            remove_piece (dst);
            place_piece (org, active, PAWN);
            mpt = PAWN;
        }
        break;
    case CASTLE:
        {
            mpt = KING;
            cpt = PT_NO;

            bool king_side = (dst > org);
            Square org_king = org;
            Square dst_king = rel_sq (active, king_side ? SQ_WK_K : SQ_WK_Q);
            Square org_rook = dst; // castle is always encoded as "king captures friendly rook"
            Square dst_rook = rel_sq (active, king_side ? SQ_WR_K : SQ_WR_Q);
            castle_king_rook (dst_king, org_king, dst_rook, org_rook);
        }
        break;
    case ENPASSANT:
        {
            ASSERT (PAWN == mpt);
            ASSERT (PAWN == cpt);
            ASSERT (R_5 == rel_rank (active, org));
            ASSERT (R_6 == rel_rank (active, dst));
            ASSERT (dst == _si->p_si->en_passant);
            //cpt = PAWN;
            cap += pawn_push (pasive);

            ASSERT (PS_NO == _piece_arr[cap]);
        }
        // NOTE:: no break;
    case NORMAL:
        {
            move_piece (dst, org); // Put the piece back at the origin square
        }
        break;
    }

    // If there was any capture piece
    if (PT_NO != cpt)
    {
        place_piece (cap, pasive, cpt); // Restore the captured piece
    }

    --_game_ply;
    // Finally point our state pointer back to the previous state
    _si = _si->p_si;

    ASSERT (ok ());
}

// do_null_move() do the null-move
void Position::do_null_move (StateInfo &si_n)
{
    ASSERT (&si_n != _si);
    ASSERT (!_si->checkers);
    if (&si_n == _si)   return;
    if (_si->checkers)  return;

    // Full copy here
    memcpy (&si_n, _si, sizeof (StateInfo));

    // switch our state pointer to point to the new, ready to be updated, state.
    si_n.p_si = _si;
    _si       = &si_n;

    if (SQ_NO != _si->en_passant)
    {
        _si->posi_key ^= ZobGlob._.en_passant[_file (_si->en_passant)];
        _si->en_passant = SQ_NO;
    }

    _si->posi_key ^= ZobGlob._.mover_side;

    prefetch ((char *) TT.get_cluster (_si->posi_key));

    _si->clock50++;
    _si->null_ply = 0;
    _active       = ~_active;

    ASSERT (ok ());
}
// undo_null_move() undo the null-move
void Position::undo_null_move ()
{
    ASSERT (_si->p_si);
    ASSERT (!_si->checkers);
    if (!(_si->p_si))   return;
    if (_si->checkers)  return;

    _si = _si->p_si;
    _active = ~_active;

    ASSERT (ok ());
}

#pragma endregion

#pragma endregion

// flip position with the white and black sides reversed.
// This is only useful for debugging especially for finding evaluation symmetry bugs.
void Position::flip ()
{

    //string f, token;
    //stringstream ss (fen ());
    //
    //for (Rank rank = R_8; rank >= R_1; --rank) // Piece placement
    //{
    //    getline (ss, token, rank > R_1 ? '/' : ' ');
    //    f.insert (0, token + (f.empty () ? ' ' : '/'));
    //}
    //
    //ss >> token; // Active color
    //f += (token == "w" ? "B" : "W"); // Will be lowercased later
    //f += ' ';
    //ss >> token; // Castling availability
    //f += token;
    //f += ' ';
    //transform (f.begin (), f.end (), f.begin (),
    //    [] (char c) { return char (islower (c) ? toupper (c) : tolower (c)); });
    //
    //ss >> token; // En passant square
    //f += (token == "-" ? token : token.replace (1, 1, token[1] == '3' ? "6" : "3"));
    //getline (ss, token); // Half and full moves
    //f += token;
    //
    //setup (f, chess960 ());

    Position pos (*this);
    clear ();

    //for (Square s = SQ_A1; s <= SQ_H8; ++s)
    //{
    //    Piece p = pos[s];
    //    if (PS_NO != p)
    //    {
    //        place_piece (~s, ~p);
    //    }
    //}
    Bitboard occ = pos.pieces ();
    while (occ)
    {
        Square s = pop_lsq (occ);
        Piece p = pos[s];
        if (PS_NO != p)
        {
            place_piece (~s, ~p);
        }
    }

    if (pos.can_castle (CR_W_K)) set_castle (BLACK, ~pos.castle_rook (WHITE, CS_K));
    if (pos.can_castle (CR_W_Q)) set_castle (BLACK, ~pos.castle_rook (WHITE, CS_Q));
    if (pos.can_castle (CR_B_K)) set_castle (WHITE, ~pos.castle_rook (BLACK, CS_K));
    if (pos.can_castle (CR_B_Q)) set_castle (WHITE, ~pos.castle_rook (BLACK, CS_Q));

    _si->castle_rights = ~pos._si->castle_rights;

    Square ep_sq = pos._si->en_passant;
    if (SQ_NO != ep_sq)
    {
        _si->en_passant = ~ep_sq;
    }

    _si->cap_type   = pos._si->cap_type;
    _si->clock50    = pos._si->clock50;
    _si->last_move  = MOVE_NONE;
    _si->checkers   = flip_verti (pos._si->checkers);
    _active         = ~pos._active;
    _si->matl_key   = ZobGlob.compute_matl_key (*this);
    _si->pawn_key   = ZobGlob.compute_pawn_key (*this);
    _si->posi_key   = ZobGlob.compute_posi_key (*this);
    _si->psq_score  = compute_psq_score ();
    _si->non_pawn_matl[WHITE] = compute_non_pawn_material (WHITE);
    _si->non_pawn_matl[BLACK] = compute_non_pawn_material (BLACK);
    _game_ply       = pos._game_ply;
    _chess960       = pos._chess960;
    _game_nodes     = 0; //pos._game_nodes;

    ASSERT (ok ());
}

#pragma region Conversions

bool   Position::fen (const char *fen, bool c960, bool full) const
{
    ASSERT (fen);
    ASSERT (ok ());
    if (!fen)   return false;
    if (!ok ()) return false;

    char *ch = (char*) fen;
    memset (ch, '\0', MAX_FEN);

#undef set_next

#define set_next(x)      *ch++ = x

    for (Rank r = R_8; r >= R_1; --r)
    {
        //uint8_t empty = 0;
        //for (File f = F_A; f <= F_H; ++f)
        //{
        //    bool empty = true;
        //    for (Color c = WHITE; c <= BLACK; ++c)
        //    {
        //        Bitboard colors = pieces (c);
        //        Square s = _Square(f, r);
        //        if (colors & s)
        //        {
        //            for (PType t = PAWN; t <= KING; ++t)
        //            {
        //                Bitboard types = pieces (t);
        //                if (types & s)
        //                {
        //                    empty = false;
        //                    if (0 < empty)
        //                    {
        //                        if (8 < empty) return false;  
        //                        set_next ('0' + empty);
        //                        empty = 0;
        //                    }
        //                    set_next (to_string(c, t));
        //                    break;
        //                }
        //            }
        //        }
        //    }
        //    if (empty) ++empty;
        //}

        File f = F_A;
        while (f <= F_H)
        {
            Square s = f | r;
            Piece p = _piece_arr[s];
            ASSERT (PS_NO == p || _ok (p));

            if (false);
            else if (PS_NO == p)
            {
                uint32_t empty = 0;
                for ( ; f <= F_H && PS_NO == _piece_arr[s]; ++f, ++s)
                    ++empty;
                ASSERT (1 <= empty && empty <= 8);
                if (1 > empty || empty > 8) return false;
                set_next ('0' + empty);
            }
            else if (_ok (p))
            {
                set_next (to_char (p));
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
    set_next (to_char (_active));
    set_next (' ');
    if (can_castle (CR_A))
    {
        if (chess960 () || c960)
        {
#pragma region X-FEN
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) set_next (to_char (_file (castle_rook (WHITE, CS_K)), false));
                if (can_castle (CR_W_Q)) set_next (to_char (_file (castle_rook (WHITE, CS_Q)), false));
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) set_next (to_char (_file (castle_rook (BLACK, CS_K)), true));
                if (can_castle (CR_B_Q)) set_next (to_char (_file (castle_rook (BLACK, CS_Q)), true));
            }
#pragma endregion
        }
        else
        {
#pragma region N-FEN
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
#pragma endregion
        }
    }
    else
    {
        set_next ('-');
    }
    set_next (' ');
    Square ep_sq = en_passant ();
    if (SQ_NO != ep_sq)
    {
        ASSERT (_ok (ep_sq));
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
        try
        {
            int32_t write =
                //_snprintf (ch, MAX_FEN - (ch - fen) - 1, "%u %u", clock50 (), game_move ());
                _snprintf_s (ch, MAX_FEN - (ch - fen) - 1, 8, "%u %u", clock50 (), game_move ());
            ch += write;
        }
        catch (...)
        {
            return false;
        }
    }
    set_next ('\0');

#undef set_next

    return true;
}
string Position::fen (bool                  c960, bool full) const
{
    ostringstream sfen;

    for (Rank r = R_8; r >= R_1; --r)
    {
        File f = F_A;
        while (f <= F_H)
        {
            Square s = f | r;
            Piece p = _piece_arr[s];
            ASSERT (PS_NO == p || _ok (p));

            if (false);
            else if (PS_NO == p)
            {
                uint32_t empty = 0;
                for ( ; f <= F_H && PS_NO == _piece_arr[s]; ++f, ++s)
                    ++empty;
                ASSERT (1 <= empty && empty <= 8);
                if (1 > empty || empty > 8) return "";
                sfen << (empty);
            }
            else if (_ok (p))
            {
                sfen << to_char (p);
                ++f;
            }
            else
            {
                return "";
            }
        }
        if (R_1 < r)
        {
            sfen << '/';
        }
    }
    sfen << ' ';
    sfen << to_char (_active);
    sfen << ' ';
    if (can_castle (CR_A))
    {
        if (chess960 () || c960)
        {
#pragma region X-FEN
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) sfen << to_char (_file (castle_rook (WHITE, CS_K)), false);
                if (can_castle (CR_W_Q)) sfen << to_char (_file (castle_rook (WHITE, CS_Q)), false);
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) sfen << to_char (_file (castle_rook (BLACK, CS_K)), true);
                if (can_castle (CR_B_Q)) sfen << to_char (_file (castle_rook (BLACK, CS_Q)), true);
            }
#pragma endregion
        }
        else
        {
#pragma region N-FEN
            if (can_castle (WHITE))
            {
                if (can_castle (CR_W_K)) sfen << 'K';
                if (can_castle (CR_W_Q)) sfen << 'Q';
            }
            if (can_castle (BLACK))
            {
                if (can_castle (CR_B_K)) sfen << 'k';
                if (can_castle (CR_B_Q)) sfen << 'q';
            }
#pragma endregion
        }
    }
    else
    {
        sfen << '-';
    }
    sfen << ' ';
    Square ep_sq = en_passant ();
    if (SQ_NO != ep_sq)
    {
        ASSERT (_ok (ep_sq));
        if (R_6 != rel_rank (_active, ep_sq)) return "";
        sfen << ::to_string (ep_sq);
    }
    else
    {
        sfen << '-';
    }
    if (full)
    {
        sfen << ' ';
        sfen << uint32_t (clock50 ());
        sfen << ' ';
        sfen << uint32_t (game_move ());
    }
    sfen << '\0';

    return sfen.str ();
}

// string() return string representation of position
Position::operator string () const
{

#pragma region Board
    const string edge = " +---+---+---+---+---+---+---+---+\n";
    const string row_1 = "| . |   | . |   | . |   | . |   |\n" + edge;
    const string row_2 = "|   | . |   | . |   | . |   | . |\n" + edge;
    const size_t row_len = row_1.length () + 1;

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

    Bitboard occ = pieces ();
    while (occ)
    {
        Square s = pop_lsq (occ);
        int8_t r = _rank (s);
        int8_t f = _file (s);
        board[3 + size_t (row_len * (7.5 - r)) + 4 * f] = to_char (_piece_arr[s]);
    }

#pragma endregion

    ostringstream spos;

    spos
        << board                          << '\n'
        << to_char (_active)              << '\n'
        << to_string (_si->castle_rights) << '\n'
        << to_string (_si->en_passant)    << '\n'
        << uint32_t (_si->clock50)        << ' '
        << game_move ()                   << endl;

    return spos.str ();
}

// A FEN string defines a particular position using only the ASCII character set.
//
// A FEN string contains six fields separated by a space. The fields are:
//
// 1) Piece placement (from white's perspective).
// Each rank is described, starting with rank 8 and ending with rank 1;
// within each rank, the contents of each square are described from file A through file H.
// Following the Standard Algebraic Notation (SAN),
// each piece is identified by a single letter taken from the standard English names.
// White pieces are designated using upper-case letters ("PNBRQK") while Black take lowercase ("pnbrqk").
// Blank squares are noted using digits 1 through 8 (the number of blank squares),
// and "/" separates ranks.
//
// 2) Active color. "w" means white, "b" means black - moves next,.
//
// 3) Castling availability. If neither side can castle, this is "-". 
// Otherwise, this has one or more letters:
// "K" (White can castle  Kingside),
// "Q" (White can castle Queenside),
// "k" (Black can castle  Kingside),
// "q" (Black can castle Queenside).
//
// 4) En passant target square (in algebraic notation).
// If there's no en passant target square, this is "-".
// If a pawn has just made a 2-square move, this is the position "behind" the pawn.
// This is recorded regardless of whether there is a pawn in position to make an en passant capture.
//
// 5) Halfmove clock. This is the number of halfmoves since the last pawn advance or capture.
// This is used to determine if a draw can be claimed under the fifty-move rule.
//
// 6) Fullmove number. The number of the full move.
// It starts at 1, and is incremented after Black's move.

#undef SKIP_WHITESPACE
#define SKIP_WHITESPACE()  while (isspace ((unsigned char) (*fen))) ++fen

bool Position::parse (Position &pos, const   char *fen, Thread *thread, bool c960, bool full)
{
    ASSERT (fen);
    if (!fen)   return false;

    pos.clear ();

    unsigned char ch;

#undef get_next

#define get_next()         ch = (unsigned char) (*fen++)

    // Piece placement on Board
    for (Rank r = R_8; r >= R_1; --r)
    {
        File f = F_A;
        while (f <= F_H)
        {
            Square s = (f | r);
            get_next ();
            if (!ch) return false;

            if (false);
            else if (isdigit (ch))
            {
                // empty square(s)
                ASSERT ('1' <= ch && ch <= '8');
                if ('1' > ch || ch > '8') return false;

                int8_t empty = (ch - '0');
                f += empty;

                ASSERT (f <= F_NO);
                if (f > F_NO) return false;
                //while (empty-- > 0) place_piece(s++, PS_NO);
            }
            else if (isalpha (ch))
            {
                // piece
                Piece p = to_piece (ch);
                if (PS_NO == p) return false;
                pos.place_piece (s, p);   // put the piece on board

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
                if (1 != pos.piece_count<KING> (c)) return false;
            }
        }
    }

    SKIP_WHITESPACE ();
    // Active color
    get_next ();
    pos._active = to_color (ch);
    if (CLR_NO == pos._active) return false;

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
#pragma region X-FEN
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                char sym = toupper (ch);
                if ('A' <= sym && sym <= 'H')
                {
                    rook = (to_file (sym) | rel_rank (c, R_1));
                    if (ROOK != p_type (pos[rook])) return false;
                    pos.set_castle (c, rook);
                }
                else
                {
                    return false;
                }

                get_next ();
            }
            while (ch && !isspace (ch));
#pragma endregion
        }
        else
        {
#pragma region N-FEN
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                switch (toupper (ch))
                {
                case 'K':
                    rook = rel_sq (c, SQ_H1);
                    while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != p_type (pos[rook]))) --rook;
                    break;
                case 'Q':
                    rook = rel_sq (c, SQ_A1);
                    while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != p_type (pos[rook]))) ++rook;
                    break;
                default: return false;
                }
                if (ROOK != p_type (pos[rook])) return false;
                pos.set_castle (c, rook);

                get_next ();
            }
            while (ch && !isspace (ch));
#pragma endregion
        }
    }

    SKIP_WHITESPACE ();
    // En-passant square
    get_next ();
    if ('-' != ch)
    {
        unsigned char ep_f = tolower (ch);
        ASSERT (isalpha (ep_f));
        ASSERT ('a' <= ep_f && ep_f <= 'h');
        if (!isalpha (ep_f)) return false;
        if ('a' > ep_f || ep_f > 'h') return false;

        unsigned char ep_r = get_next ();
        ASSERT (isdigit (ep_r));
        ASSERT ((WHITE == pos._active && '6' == ep_r) || (BLACK == pos._active && '3' == ep_r));

        if (!isdigit (ep_r)) return false;
        if ((WHITE == pos._active && '6' != ep_r) || (BLACK == pos._active && '3' != ep_r)) return false;

        Square ep_sq  = _Square (ep_f, ep_r);
        if (pos.can_en_passant (ep_sq))
        {
            pos._si->en_passant = ep_sq;
        }
    }
    // 50-move clock and game-move count
    int32_t clk50 = 0, g_move = 1;
    get_next ();
    if (full && ch)
    {
        int32_t n = 0;
        --fen;

        int32_t read =
            //sscanf (fen, " %d %d%n", &clk50, &g_move, &n);
            //_snscanf (fen, strlen (fen), " %d %d%n", &clk50, &g_move, &n);
            _snscanf_s (fen, strlen (fen), " %d %d%n", &clk50, &g_move, &n);

        if (read != 2) return false;
        fen += n;

        // Rule 50 draw case
        if (100 < clk50) return false;
        //if (0 >= g_move) g_move = 1;

        get_next ();
        if (ch) return false; // NOTE: extra characters
    }

#undef get_next

    // Convert from game_move starting from 1 to game_ply starting from 0,
    // handle also common incorrect FEN with game_move = 0.
    pos._si->clock50 = (SQ_NO != pos._si->en_passant) ? 0 : clk50;
    pos._game_ply = max<int16_t> (2 * (g_move - 1), 0) + (BLACK == pos._active);

    pos._si->checkers = pos.checkers (pos._active);
    pos._si->matl_key = ZobGlob.compute_matl_key (pos);
    pos._si->pawn_key = ZobGlob.compute_pawn_key (pos);
    pos._si->posi_key = ZobGlob.compute_posi_key (pos);
    pos._si->psq_score = pos.compute_psq_score ();
    pos._si->non_pawn_matl[WHITE] = pos.compute_non_pawn_material (WHITE);
    pos._si->non_pawn_matl[BLACK] = pos.compute_non_pawn_material (BLACK);
    pos._chess960     = c960;
    pos._game_nodes   = 0;
    pos._thread       = thread;

    return true;
}
#undef SKIP_WHITESPACE

bool Position::parse (Position &pos, const string &fen, Thread *thread, bool c960, bool full)
{
    if (fen.empty ()) return false;

    pos.clear ();

#pragma region String Splits

    //const vector<string> sp_fen = str_splits (fen, ' ');
    //size_t size_sp_fen = sp_fen.size ();
    //
    //if (full)
    //{
    //    if (6 != size_sp_fen) return false;
    //}
    //else
    //{
    //    if (4 != size_sp_fen) return false;
    //}
    //
    //// Piece placement on Board 
    //const vector<string> sp_brd = str_splits (sp_fen[0], '/', false, true);
    //size_t size_sp_brd = sp_brd.size ();
    //
    //if (R_NO != size_sp_brd) return false;
    //
    //Rank r = R_8;
    //for (size_t j = 0; j < size_sp_brd; ++j)
    //{
    //    const string &row = sp_brd[j];
    //    File f = F_A;
    //    for (size_t i = 0; i < row.length (); ++i)
    //    {
    //        char ch = row[i];
    //        const Square s = (f | r);
    //        if (false);
    //        else if (isdigit (ch))
    //        {
    //            // empty square(s)
    //            ASSERT ('1' <= ch && ch <= '8');
    //            if ('1' > ch || ch > '8') return false;
    //
    //            uint8_t empty = (ch - '0');
    //            f += empty;
    //
    //            ASSERT (f <= F_NO);
    //            if (f > F_NO) return false;
    //            ////while (empty-- > 0) place_piece (s++, PS_NO);
    //        }
    //        else if (isalpha (ch))
    //        {
    //            // piece
    //            Piece p = to_piece (ch);
    //            if (PS_NO == p) return false;
    //            pos.place_piece (s, p);   // put the piece on Board
    //            if (KING == p_type (p))
    //            {
    //                Color c = p_color (p);
    //                if (1 != pos[p].size ()) return false;
    //            }
    //            ++f;
    //        }
    //        else
    //        {
    //            return false;
    //        }
    //    }
    //    --r;
    //}
    //
    //ASSERT (1 == sp_fen[1].length ());
    //if (1 != sp_fen[1].length ()) return false;
    //// Active Color
    //pos._active = p_color (sp_fen[1][0]);
    //if (CLR_NO == pos._active) return false;
    //
    //ASSERT (4 >= sp_fen[2].length ());
    //if (4 < sp_fen[2].length ()) return false;
    //
    //// Castling rights availability
    //// Compatible with 3 standards:
    //// * Normal FEN standard,
    //// * Shredder-FEN that uses the letters of the columns on which the rooks began the game instead of KQkq
    //// * X-FEN standard that, in case of Chess960, if an inner rook is associated with the castling right, the castling
    //// tag is replaced by the file letter of the involved rook, as for the Shredder-FEN.
    //const string &castle_s = sp_fen[2];
    //if ('-' != castle_s[0])
    //{
    //    if (c960)
    //    {
    //#pragma region X-FEN
    //        for (size_t i = 0; i < castle_s.length (); ++i)
    //        {
    //            char ch = castle_s[i];
    //
    //            Square rook;
    //            Color c = isupper (ch) ? WHITE : BLACK;
    //            char sym = toupper (ch);
    //            if ('A' <= sym && sym <= 'H')
    //            {
    //                rook = (_file (sym) | rel_rank (c, R_1));
    //                if (ROOK != p_type (pos[rook])) return false;
    //                pos.set_castle (c, rook);
    //            }
    //            else
    //            {
    //                return false;
    //            }
    //        }
    //#pragma endregion
    //    }
    //    else
    //    {
    //#pragma region N-FEN
    //        for (size_t i = 0; i < castle_s.length (); ++i)
    //        {
    //            char ch = castle_s[i];
    //            Square rook;
    //            Color c = isupper (ch) ? WHITE : BLACK;
    //            switch (toupper (ch))
    //            {
    //            case 'K':
    //                rook = rel_sq (c, SQ_H1);
    //                while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != p_type (pos[rook]))) --rook;
    //                break;
    //            case 'Q':
    //                rook = rel_sq (c, SQ_A1);
    //                while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != p_type (pos[rook]))) ++rook;
    //                break;
    //            default: return false;
    //            }
    //            if (ROOK != p_type (pos[rook])) return false;
    //            pos.set_castle (c, rook);
    //        }
    //#pragma endregion
    //    }
    //}
    //
    //ASSERT (2 >= sp_fen[3].length ());
    //if (2 < sp_fen[3].length ()) return false;
    //
    //// En-passant square
    //const string &en_pas_s = sp_fen[3];
    //if ('-' != en_pas_s[0])
    //{
    //    unsigned char ep_f = tolower (en_pas_s[0]);
    //    ASSERT (isalpha (ep_f));
    //    ASSERT ('a' <= ep_f && ep_f <= 'h');
    //
    //    if (!isalpha (ep_f)) return false;
    //    if ('a' > ep_f || ep_f > 'h') return false;
    //
    //    unsigned char ep_r = en_pas_s[1];
    //    ASSERT (isdigit (ep_r));
    //    ASSERT ((WHITE == pos._active && '6' == ep_r) || (BLACK == pos._active && '3' == ep_r));
    //
    //    if (!isdigit (ep_r)) return false;
    //    if ((WHITE == pos._active && '6' != ep_r) || (BLACK == pos._active && '3' != ep_r)) return false;
    //    Square ep_sq  = _Square (ep_f, ep_r);
    //    if (pos.can_en_passant (ep_sq))
    //    {
    //        pos._si->en_passant = ep_sq;
    //    }
    //}
    //// 50-move clock and game-move count
    //int32_t clk50 = 0, g_move = 1;
    //if (full && (6 == size_sp_fen))
    //{
    //    clk50  = to_int (sp_fen[4]);
    //    g_move = to_int (sp_fen[5]);
    //
    //    // Rule 50 draw case
    //    if (100 < clk50) return false;
    //    //if (0 >= g_move) g_move = 1;
    //}
    //// Convert from game_move starting from 1 to game_ply starting from 0,
    //// handle also common incorrect FEN with game_move = 0.
    //pos._si->clock50 = (SQ_NO != pos._si->en_passant) ? 0 : clk50;
    //pos._game_ply = max<int16_t> (2 * (g_move - 1), 0) + (BLACK == pos._active);

#pragma endregion

#pragma region Input String Stream

    istringstream sfen (fen);
    unsigned char ch;

    // Piece placement on Board
    sfen >> noskipws;
    for (Rank r = R_8; r >= R_1; --r)
    {
        File f = F_A;
        while (f <= F_H)
        {
            Square s = (f | r);
            sfen >> ch;
            if (sfen.eof () || !sfen.good () || !ch) return false;

            if (false);
            else if (isdigit (ch))
            {
                // empty square(s)
                ASSERT ('1' <= ch && ch <= '8');
                if ('1' > ch || ch > '8') return false;

                int8_t empty = (ch - '0');
                f += empty;

                ASSERT (f <= F_NO);
                if (f > F_NO) return false;
                ////while (empty-- > 0) place_piece (s++, PS_NO);
            }
            else if (isalpha (ch))
            {
                // piece
                Piece p = to_piece (ch);
                if (PS_NO == p) return false;
                pos.place_piece (s, p);   // put the piece on Board

                ++f;
            }
            else
            {
                return false;
            }
        }

        if (R_1 < r)
        {
            sfen >> ch;
            if (sfen.eof () || !sfen.good () || '/' != ch) return false;
        }
        else
        {
            for (Color c = WHITE; c <= BLACK; ++c)
            {
                if (1 != pos.piece_count<KING> (c)) return false;
            }
        }
    }

    // Active color
    sfen >> skipws >> ch;
    pos._active = to_color (ch);
    if (CLR_NO == pos._active) return false;

    // Castling rights availability
    // Compatible with 3 standards:
    // 1-Normal FEN standard,
    // 2-Shredder-FEN that uses the letters of the columns on which the rooks began the game instead of KQkq
    // 3-X-FEN standard that, in case of Chess960, if an inner rook is associated with the castling right, the castling
    // tag is replaced by the file letter of the involved rook, as for the Shredder-FEN.
    sfen >> skipws >> ch;
    if ('-' != ch)
    {
        sfen >> noskipws;

        if (c960)
        {
#pragma region X-FEN
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                char sym = toupper (ch);
                if ('A' <= sym && sym <= 'H')
                {
                    rook = (to_file (sym) | rel_rank (c, R_1));
                    if (ROOK != p_type (pos[rook])) return false;
                    pos.set_castle (c, rook);
                }
                else
                {
                    return false;
                }

                sfen >> ch;
            }
            while (ch && !isspace (ch));
#pragma endregion
        }
        else
        {
#pragma region N-FEN
            do
            {
                Square rook;
                Color c = isupper (ch) ? WHITE : BLACK;
                switch (toupper (ch))
                {
                case 'K':
                    rook = rel_sq (c, SQ_H1);
                    while ((rel_sq (c, SQ_A1) <= rook) && (ROOK != p_type (pos[rook]))) --rook;
                    break;
                case 'Q':
                    rook = rel_sq (c, SQ_A1);
                    while ((rel_sq (c, SQ_H1) >= rook) && (ROOK != p_type (pos[rook]))) ++rook;
                    break;
                default: return false;
                }
                if (ROOK != p_type (pos[rook])) return false;
                pos.set_castle (c, rook);

                sfen >> ch;
            }
            while (ch && !isspace (ch));
#pragma endregion
        }
    }
    // En-passant square
    sfen >> skipws >> ch;
    if ('-' != ch)
    {
        unsigned char ep_f = tolower (ch);
        ASSERT (isalpha (ep_f));
        ASSERT ('a' <= ep_f && ep_f <= 'h');
        if (!isalpha (ep_f)) return false;
        if ('a' > ep_f || ep_f > 'h') return false;

        sfen >> noskipws >> ch;
        unsigned char ep_r = ch;
        ASSERT (isdigit (ep_r));
        ASSERT ((WHITE == pos._active && '6' == ep_r) || (BLACK == pos._active && '3' == ep_r));

        if (!isdigit (ep_r)) return false;
        if ((WHITE == pos._active && '6' != ep_r) || (BLACK == pos._active && '3' != ep_r)) return false;

        Square ep_sq = _Square (ep_f, ep_r);
        if (pos.can_en_passant (ep_sq))
        {
            pos._si->en_passant = ep_sq;
        }
    }
    // 50-move clock and game-move count
    int32_t clk50 = 0, g_move = 1;
    if (full)
    {
        sfen >> skipws >> clk50 >> g_move;
    }
    // Convert from game_move starting from 1 to game_ply starting from 0,
    // handle also common incorrect FEN with game_move = 0.
    pos._si->clock50 = (SQ_NO != pos._si->en_passant) ? 0 : clk50;
    pos._game_ply = max<int16_t> (2 * (g_move - 1), 0) + (BLACK == pos._active);

#pragma endregion

    pos._si->checkers = pos.checkers (pos._active);
    pos._si->matl_key = ZobGlob.compute_matl_key (pos);
    pos._si->pawn_key = ZobGlob.compute_pawn_key (pos);
    pos._si->posi_key = ZobGlob.compute_posi_key (pos);
    pos._si->psq_score = pos.compute_psq_score ();
    pos._si->non_pawn_matl[WHITE] = pos.compute_non_pawn_material (WHITE);
    pos._si->non_pawn_matl[BLACK] = pos.compute_non_pawn_material (BLACK);
    pos._chess960     = c960;
    pos._game_nodes   = 0;
    pos._thread       = thread;

    return true;
}

#pragma endregion

#pragma endregion
