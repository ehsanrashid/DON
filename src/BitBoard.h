//#pragma once
#ifndef BITBOARD_H_
#define BITBOARD_H_

#include "Square.h"
#include "Piece.h"

namespace BitBoard {

#pragma region Constants

    extern const Bitboard FA_bb;
    extern const Bitboard FB_bb;
    extern const Bitboard FC_bb;
    extern const Bitboard FD_bb;
    extern const Bitboard FE_bb;
    extern const Bitboard FF_bb;
    extern const Bitboard FG_bb;
    extern const Bitboard FH_bb;

    extern const Bitboard R1_bb;
    extern const Bitboard R2_bb;
    extern const Bitboard R3_bb;
    extern const Bitboard R4_bb;
    extern const Bitboard R5_bb;
    extern const Bitboard R6_bb;
    extern const Bitboard R7_bb;
    extern const Bitboard R8_bb;

    //extern const Bitboard NULL_bb;
    //extern const Bitboard FULL_bb;

    extern const Bitboard R1_bb_;  // 56 Not RANK-1
    extern const Bitboard R8_bb_;  // 56 Not RANK-8
    extern const Bitboard FA_bb_;  // 56 Not FILE-A
    extern const Bitboard FH_bb_;  // 56 Not FILE-H

    extern const Bitboard D18_bb;  // 08 DIAG-18 squares.
    extern const Bitboard D81_bb;  // 08 DIAG-81 squares.

    extern const Bitboard LT_SQ_bb; // 32 LIGHT squares.
    extern const Bitboard DR_SQ_bb; // 32 DARK  squares.

#pragma endregion

#pragma region LOOKUPs

    extern Delta _filerank_dist[F_NO][R_NO];
    extern Delta   _square_dist[SQ_NO][SQ_NO];
    extern Delta  _taxicab_dist[SQ_NO][SQ_NO];

    //extern uint8_t _shift_gap[_UI8_MAX + 1][F_NO];

    extern const Delta _deltas_pawn[CLR_NO][3];
    extern const Delta _deltas_type[PT_NO][9];

    extern const int8_t _center_dist[SQ_NO]; 
    extern const int8_t _manhattan_center_dist[SQ_NO];

    extern const Bitboard _square_bb[SQ_NO];
    extern const Bitboard   _file_bb[F_NO];
    extern const Bitboard   _rank_bb[R_NO];

    extern const Bitboard _diag18_bb[D_NO];
    extern const Bitboard _diag81_bb[D_NO];

    extern const Bitboard _adj_file_bb[F_NO];
    extern const Bitboard _adj_rank_bb[R_NO];
    extern const Bitboard _front_rank_bb[CLR_NO][R_NO];
    extern Bitboard _front_squares_bb[CLR_NO][SQ_NO];

    extern Bitboard _betwen_sq_bb[SQ_NO][SQ_NO];
    extern Bitboard  _lines_sq_bb[SQ_NO][SQ_NO];

    extern Bitboard _dia_rings_bb[SQ_NO][F_NO];

    extern Bitboard _pawn_attack_span_bb[CLR_NO][SQ_NO];
    extern Bitboard _passer_pawn_span_bb[CLR_NO][SQ_NO];

    // attacks of the pieces
    extern Bitboard _attacks_pawn_bb[CLR_NO][SQ_NO];
    extern Bitboard _attacks_type_bb[PT_NO][SQ_NO];

#pragma endregion

#pragma region Operators

    INLINE Bitboard  operator&  (Bitboard  bb, Square s) { return bb &  _square_bb[s]; }
    INLINE Bitboard  operator|  (Bitboard  bb, Square s) { return bb |  _square_bb[s]; }
    INLINE Bitboard  operator^  (Bitboard  bb, Square s) { return bb ^  _square_bb[s]; }
    INLINE Bitboard  operator+  (Bitboard  bb, Square s) { return bb |  _square_bb[s]; }
    INLINE Bitboard  operator-  (Bitboard  bb, Square s) { return bb &~ _square_bb[s]; }
    INLINE Bitboard& operator&= (Bitboard &bb, Square s) { return bb &= _square_bb[s]; }
    INLINE Bitboard& operator|= (Bitboard &bb, Square s) { return bb |= _square_bb[s]; }
    INLINE Bitboard& operator^= (Bitboard &bb, Square s) { return bb ^= _square_bb[s]; }
    INLINE Bitboard& operator+= (Bitboard &bb, Square s) { return bb |= _square_bb[s]; }
    INLINE Bitboard& operator-= (Bitboard &bb, Square s) { return bb &=~_square_bb[s]; }

    INLINE Bitboard  operator&  (Bitboard  bb, File   f) { return bb &  _file_bb[f]; }
    INLINE Bitboard  operator|  (Bitboard  bb, File   f) { return bb |  _file_bb[f]; }
    INLINE Bitboard  operator^  (Bitboard  bb, File   f) { return bb ^  _file_bb[f]; }
    INLINE Bitboard  operator+  (Bitboard  bb, File   f) { return bb |  _file_bb[f]; }
    INLINE Bitboard  operator-  (Bitboard  bb, File   f) { return bb & ~_file_bb[f]; }
    INLINE Bitboard& operator&= (Bitboard &bb, File   f) { return bb &= _file_bb[f]; }
    INLINE Bitboard& operator|= (Bitboard &bb, File   f) { return bb |= _file_bb[f]; }
    INLINE Bitboard& operator^= (Bitboard &bb, File   f) { return bb ^= _file_bb[f]; }
    INLINE Bitboard& operator+= (Bitboard &bb, File   f) { return bb |= _file_bb[f]; }
    INLINE Bitboard& operator-= (Bitboard &bb, File   f) { return bb &=~_file_bb[f]; }

    INLINE Bitboard  operator&  (Bitboard  bb, Rank   r) { return bb &  _rank_bb[r]; }
    INLINE Bitboard  operator|  (Bitboard  bb, Rank   r) { return bb |  _rank_bb[r]; }
    INLINE Bitboard  operator^  (Bitboard  bb, Rank   r) { return bb ^  _rank_bb[r]; }
    INLINE Bitboard  operator+  (Bitboard  bb, Rank   r) { return bb |  _rank_bb[r]; }
    INLINE Bitboard  operator-  (Bitboard  bb, Rank   r) { return bb & ~_rank_bb[r]; }
    INLINE Bitboard& operator&= (Bitboard &bb, Rank   r) { return bb &= _rank_bb[r]; }
    INLINE Bitboard& operator|= (Bitboard &bb, Rank   r) { return bb |= _rank_bb[r]; }
    INLINE Bitboard& operator^= (Bitboard &bb, Rank   r) { return bb ^= _rank_bb[r]; }
    INLINE Bitboard& operator+= (Bitboard &bb, Rank   r) { return bb |= _rank_bb[r]; }
    INLINE Bitboard& operator-= (Bitboard &bb, Rank   r) { return bb &=~_rank_bb[r]; }

#pragma endregion

#pragma region Deltas

    INLINE Delta file_dist (File f1, File f2)     { return _filerank_dist[f1][f2]; }
    INLINE Delta file_dist (Square s1, Square s2) { return _filerank_dist[_file (s1)][_file (s2)]; }

    INLINE Delta rank_dist (Rank r1, Rank r2)     { return _filerank_dist[r1][r2]; }
    INLINE Delta rank_dist (Square s1, Square s2) { return _filerank_dist[_rank (s1)][_rank (s2)]; }

    INLINE Delta  square_dist (Square s1, Square s2) { return  _square_dist[s1][s2]; }
    INLINE Delta taxicab_dist (Square s1, Square s2) { return _taxicab_dist[s1][s2]; }

    // Absolute difference of file & rank
    INLINE Delta abs_file_rank_diff (Square s1, Square s2)
    {
        int8_t del_r = (s1 | 7) - (s2 | 7);
        int8_t del_f = (s1 & 7) - (s2 & 7);
        return Delta (abs (del_r) + abs (del_f));
    }

    INLINE Delta offset_sq (Square s1, Square s2) { return (s2 - s1) / _square_dist[s1][s2]; }

    // ----------------------------------------------------

    //inline int8_t center_dist (Square s)
    //{
    //    //return _center_dist[s];
    //
    //    const Bitboard bit0 = U64 (0xFF81BDA5A5BD81FF);
    //    const Bitboard bit1 = U64 (0xFFFFC3C3C3C3FFFF);
    //    return 2 * ((bit1 >> s) & 1) + ((bit0 >> s) & 1); 
    //}

    ///**
    //* manhattan_center_dist
    //* @author Gerd Isenberg
    //* @param s = square 0...63
    //* @return Manhattan Center Distance
    //*/
    //inline int8_t manhattan_center_dist (Square s)
    //{
    //    int8_t f = _file (s);
    //    int8_t r = _rank (s);
    //    f ^= (f-4) >> 8;
    //    r ^= (r-4) >> 8;
    //    return (f + r) & 7;
    //}

    ///**
    //* manhattan_dist_bishop_sq_closest_corner
    //*   for KBNK purpose
    //* @author Gerd Isenberg
    //* @param bs bishop square (to determine its square color)
    //* @param s opponent king square (0..63)
    //* @return manhattanDistance to the closest corner square
    //*         of the bishop square color
    //*/
    //inline int8_t manhattan_dist_bishop_sq_closest_corner(Square bs, Square s)
    //{
    //    int8_t b = -1879048192*bs >> 31; // 0 | -1 to mirror
    //    int8_t k;
    //    k = (s>>3) + ((s^b) & 7);        // rank + (mirrored) file
    //    k = (15 * (k>>3) ^ k) - (k>>3);  // if (k > 7) k = 14 - k
    //    return k;
    //}

#pragma endregion

#pragma region Masks

    INLINE Bitboard square_bb  (Square s) { return  _square_bb[s]; }
    INLINE Bitboard square_bb_ (Square s) { return ~_square_bb[s]; }

    INLINE Bitboard file_bb (File   f) { return _file_bb[f]; }
    INLINE Bitboard file_bb (Square s) { return _file_bb[_file (s)]; }

    INLINE Bitboard rank_bb (Rank   r) { return _rank_bb[r]; }
    INLINE Bitboard rank_bb (Square s) { return _rank_bb[_rank (s)]; }

    INLINE Bitboard diag18_bb (Diag   d) { return _diag18_bb[d]; }
    INLINE Bitboard diag18_bb (Square s) { return _diag18_bb[_diag18 (s)]; }

    INLINE Bitboard diag81_bb (Diag   d) { return _diag81_bb[d]; }
    INLINE Bitboard diag81_bb (Square s) { return _diag81_bb[_diag81 (s)]; }

    INLINE Bitboard adj_files_bb (File   f) { return _adj_file_bb[f]; }
    INLINE Bitboard adj_files_bb (Square s) { return _adj_file_bb[_file (s)]; }

    INLINE Bitboard adj_ranks_bb (Rank   r) { return _adj_rank_bb[r]; }
    INLINE Bitboard adj_ranks_bb (Square s) { return _adj_rank_bb[_rank (s)]; }

    INLINE Bitboard rel_rank_bb (Color c, Rank   r) { return _rank_bb[rel_rank (c, r)]; }
    INLINE Bitboard rel_rank_bb (Color c, Square s)
    {
        return _rank_bb[rel_rank (c, s)];
        //return rel_rank_bb (c, _rank (s));
    }

    // Bitboard of ranks in front of the rank, from the point of view of the given color.
    INLINE Bitboard front_ranks_bb   (Color c, Rank   r) { return _front_rank_bb[c][r]; }
    // Bitboard of squares along the line in front of the square, from the point of view of the given color.
    INLINE Bitboard front_squares_bb (Color c, Square s) { return _front_squares_bb[c][s]; }

    // Ring on the square with the distance 'd'
    INLINE Bitboard dia_rings_bb    (Square s, uint8_t d) { return _dia_rings_bb[s][d]; }

    INLINE Bitboard brd_edges_bb    (Square s) { return (((FA_bb | FH_bb) & ~file_bb (s)) | ((R1_bb | R8_bb) & ~rank_bb (s))); }

    // pawn_attack_span_bb() takes a color and a square as input, and returns a bitboard
    // representing all squares that can be attacked by a pawn of the given color
    // when it moves along its file starting from the given square. Definition is:
    // PawnAttackSpan[c][s] = in_front_bb(c, s) & adjacent_files_bb(s);
    INLINE Bitboard pawn_attack_span_bb (Color c, Square s) { return _pawn_attack_span_bb[c][s]; }
    // passer_pawn_span_bb() takes a color and a square as input, and returns a
    // bitboard mask which can be used to test if a pawn of the given color on
    // the given square is a passed pawn. Definition of the table is:
    // PassedPawnMask[c][s] = pawn_attack_span_bb(c, s) | forward_bb(c, s)
    INLINE Bitboard passer_pawn_span_bb (Color c, Square s) { return _passer_pawn_span_bb[c][s]; }

    // squares_of_color() returns a bitboard of all squares with the same color of the given square.
    INLINE Bitboard squares_of_color (Square s) { return (DR_SQ_bb & s) ? DR_SQ_bb : LT_SQ_bb; }

    // between_bb() returns a bitboard representing all squares between two squares.
    // For instance,
    // between_bb(SQ_C4, SQ_F7) returns a bitboard with the bits for square d5 and e6 set.
    // If s1 and s2 are not on the same rank, file or diagonal, 0 is returned.
    INLINE Bitboard betwen_sq_bb (Square s1, Square s2) { return _betwen_sq_bb[s1][s2]; }

    // Check the squares s1, s2 and s3 are aligned either on a straight/diagonal line.
    INLINE bool sqrs_aligned    (Square s1, Square s2, Square s3) { return _lines_sq_bb[s1][s2] & s3; }

    INLINE bool more_than_one (Bitboard bb) { return bool ((bb) & (bb - 1)); }

#pragma endregion

#pragma region Shifts using Delta

    //template<Delta DELTA>
    //extern Bitboard shift_del (Bitboard bb, int8_t x);
    //
    //template<>
    //inline Bitboard shift_del<DEL_N> (Bitboard bb, int8_t x)
    //{
    //    return (bb) << (x << 3);
    //}
    //template<>
    //inline Bitboard shift_del<DEL_S> (Bitboard bb, int8_t x)
    //{
    //    return (bb) >> (x << 3);
    //}

    template<Delta D>
    // Shift the Bitboard using delta
    INLINE Bitboard shift_del (Bitboard bb);

    template<>
    INLINE Bitboard shift_del<DEL_N > (Bitboard bb)
    {
        return (bb) << (8);
    }
    template<>
    INLINE Bitboard shift_del<DEL_S > (Bitboard bb)
    {
        return (bb) >> (8);
    }
    template<>
    INLINE Bitboard shift_del<DEL_E > (Bitboard bb)
    {
        return (bb & FH_bb_) << (1);
    }
    template<>
    INLINE Bitboard shift_del<DEL_W > (Bitboard bb)
    {
        return (bb & FA_bb_) >> (1);
    }
    template<>
    INLINE Bitboard shift_del<DEL_NE> (Bitboard bb)
    {
        return (bb & FH_bb_) << (9); //(bb << 9) & FA_bb_;
    }
    template<>
    INLINE Bitboard shift_del<DEL_SE> (Bitboard bb)
    {
        return (bb & FH_bb_) >> (7); //(bb >> 7) & FA_bb_;
    }
    template<>
    INLINE Bitboard shift_del<DEL_NW> (Bitboard bb)
    {
        return (bb & FA_bb_) << (7); //(bb << 7) & FH_bb_;
    }
    template<>
    INLINE Bitboard shift_del<DEL_SW> (Bitboard bb)
    {
        return (bb & FA_bb_) >> (9); //(bb >> 9) & FH_bb_;
    }

#pragma endregion

#pragma region Attacks

    INLINE Bitboard attacks_sliding (Square s, const Delta deltas[], Bitboard occ = U64 (0))
    {
        Bitboard attacks_slid = U64 (0);
        int8_t i = 0;
        Delta del = deltas[i++];
        while (del)
        {
            Square sq = s + del;
            while (_ok (sq) && _square_dist[sq][sq - del] == 1)
            {
                attacks_slid += sq;
                if (occ & sq) break;
                sq += del;
            }
            del = deltas[i++];
        }
        return attacks_slid;
    }

    template<PType PT>
    // Attacks of the PAWN
    extern INLINE Bitboard attacks_bb (Color c, Square s);

    template<>
    // PAWN attacks
    INLINE Bitboard attacks_bb<PAWN> (Color c, Square s) { return _attacks_pawn_bb[c][s]; }

    // --------------------------------
    template<PType PT>
    // Attacks of the PType
    extern INLINE Bitboard attacks_bb (Square s);

    template<PType PT>
    // Attacks of the PType
    INLINE Bitboard attacks_bb (Square s) { return _attacks_type_bb[PT][s]; }
    // --------------------------------
    // explicit template instantiations
    template INLINE Bitboard attacks_bb<NIHT> (Square s);
    template INLINE Bitboard attacks_bb<BSHP> (Square s);
    template INLINE Bitboard attacks_bb<ROOK> (Square s);
    template INLINE Bitboard attacks_bb<QUEN> (Square s);
    template INLINE Bitboard attacks_bb<KING> (Square s);
    // --------------------------------

    template<PType PT>
    // Attacks of the PType with occupancy
    extern INLINE Bitboard attacks_bb (Square s, Bitboard occ);

    template<>
    // KNIGHT attacks
    INLINE Bitboard attacks_bb<NIHT> (Square s, Bitboard occ) { return _attacks_type_bb[NIHT][s]; }
    template<>
    // KING attacks
    INLINE Bitboard attacks_bb<KING> (Square s, Bitboard occ) { return _attacks_type_bb[KING][s]; }
    // --------------------------------

    extern inline Bitboard attacks_bb (Piece p, Square s, Bitboard occ);

#pragma endregion

    extern void initialize ();

#pragma region Printing

    extern Bitboard to_bitboard (const char s[], int32_t radix = 16);
    extern Bitboard to_bitboard (const std::string &s, int32_t radix = 16);

    extern std::string to_hex_str (std::string &sbitboard);

    extern void print_bit (Bitboard bb, uint8_t x = 64, char p = 'o');
    extern void print_bin (Bitboard bb);
    extern void print (Bitboard bb, char p = 'o');

#pragma endregion

    //extern SquareList squares (Bitboard  bb);

}

#endif