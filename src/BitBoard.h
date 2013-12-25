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
    extern Delta     _taxi_dist[SQ_NO][SQ_NO];

    //extern uint8_t _shift_gap[_UI8_MAX + 1][F_NO];

    extern const Delta _deltas_pawn[CLR_NO][3];
    extern const Delta _deltas_type[PT_NO][9];

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

    extern Bitboard _attack_span_pawn_bb[CLR_NO][SQ_NO];
    extern Bitboard _passer_span_pawn_bb[CLR_NO][SQ_NO];

    // attacks of the pieces
    extern Bitboard _attacks_pawn_bb[CLR_NO][SQ_NO];
    extern Bitboard _attacks_type_bb[PT_NO][SQ_NO];

#pragma endregion

#pragma region Operators

    F_INLINE Bitboard  operator&  (Bitboard  bb, Square s)
    {
        return bb &   _square_bb[s];
    }
    F_INLINE Bitboard  operator|  (Bitboard  bb, Square s)
    {
        return bb | _square_bb[s];
    }
    F_INLINE Bitboard  operator^  (Bitboard  bb, Square s)
    {
        return bb ^   _square_bb[s];
    }
    F_INLINE Bitboard  operator+  (Bitboard  bb, Square s)
    {
        return bb | _square_bb[s];
    }
    F_INLINE Bitboard  operator-  (Bitboard  bb, Square s)
    {
        return bb &  ~_square_bb[s];
    }
    F_INLINE Bitboard& operator&= (Bitboard &bb, Square s)
    {
        return bb &= _square_bb[s];
    }
    F_INLINE Bitboard& operator|= (Bitboard &bb, Square s)
    {
        return bb |= _square_bb[s];
    }
    F_INLINE Bitboard& operator^= (Bitboard &bb, Square s)
    {
        return bb ^= _square_bb[s];
    }
    F_INLINE Bitboard& operator+= (Bitboard &bb, Square s)
    {
        return bb |= _square_bb[s];
    }
    F_INLINE Bitboard& operator-= (Bitboard &bb, Square s)
    {
        return bb &= ~_square_bb[s];
    }

    F_INLINE Bitboard  operator&  (Bitboard  bb, File   f)
    {
        return bb &   _file_bb[f];
    }
    F_INLINE Bitboard  operator|  (Bitboard  bb, File   f)
    {
        return bb | _file_bb[f];
    }
    F_INLINE Bitboard  operator^  (Bitboard  bb, File   f)
    {
        return bb ^   _file_bb[f];
    }
    F_INLINE Bitboard  operator+  (Bitboard  bb, File   f)
    {
        return bb | _file_bb[f];
    }
    F_INLINE Bitboard  operator-  (Bitboard  bb, File   f)
    {
        return bb &  ~_file_bb[f];
    }
    F_INLINE Bitboard& operator&= (Bitboard &bb, File   f)
    {
        return bb &= _file_bb[f];
    }
    F_INLINE Bitboard& operator|= (Bitboard &bb, File   f)
    {
        return bb |= _file_bb[f];
    }
    F_INLINE Bitboard& operator^= (Bitboard &bb, File   f)
    {
        return bb ^= _file_bb[f];
    }
    F_INLINE Bitboard& operator+= (Bitboard &bb, File   f)
    {
        return bb |= _file_bb[f];
    }
    F_INLINE Bitboard& operator-= (Bitboard &bb, File   f)
    {
        return bb &= ~_file_bb[f];
    }

    F_INLINE Bitboard  operator&  (Bitboard  bb, Rank   r)
    {
        return bb &   _rank_bb[r];
    }
    F_INLINE Bitboard  operator|  (Bitboard  bb, Rank   r)
    {
        return bb | _rank_bb[r];
    }
    F_INLINE Bitboard  operator^  (Bitboard  bb, Rank   r)
    {
        return bb ^   _rank_bb[r];
    }
    F_INLINE Bitboard  operator+  (Bitboard  bb, Rank   r)
    {
        return bb | _rank_bb[r];
    }
    F_INLINE Bitboard  operator-  (Bitboard  bb, Rank   r)
    {
        return bb &  ~_rank_bb[r];
    }
    F_INLINE Bitboard& operator&= (Bitboard &bb, Rank   r)
    {
        return bb &= _rank_bb[r];
    }
    F_INLINE Bitboard& operator|= (Bitboard &bb, Rank   r)
    {
        return bb |= _rank_bb[r];
    }
    F_INLINE Bitboard& operator^= (Bitboard &bb, Rank   r)
    {
        return bb ^= _rank_bb[r];
    }
    F_INLINE Bitboard& operator+= (Bitboard &bb, Rank   r)
    {
        return bb |= _rank_bb[r];
    }
    F_INLINE Bitboard& operator-= (Bitboard &bb, Rank   r)
    {
        return bb &= ~_rank_bb[r];
    }

#pragma endregion

#pragma region Deltas

    inline Delta file_dist (File f1, File f2)
    {
        return _filerank_dist[f1][f2];
    }
    inline Delta file_dist (Square s1, Square s2)
    {
        return _filerank_dist[_file (s1)][_file (s2)];
    }

    inline Delta rank_dist (Rank r1, Rank r2)
    {
        return _filerank_dist[r1][r2];
    }
    inline Delta rank_dist (Square s1, Square s2)
    {
        return _filerank_dist[_rank (s1)][_rank (s2)];
    }

    inline Delta square_dist (Square s1, Square s2)
    {
        return _square_dist[s1][s2];
    }
    inline Delta taxi_dist (Square s1, Square s2)
    {
        return _taxi_dist[s1][s2];
    }

    // Absolute difference of file & rank
    inline Delta abs_file_rank_diff (Square s1, Square s2)
    {
        int8_t drank = (s1 | 7) - (s2 | 7);
        int8_t dfile = (s1 & 7) - (s2 & 7);
        return Delta (abs (drank) + abs (dfile));
    }

    inline Delta offset_sq (Square s1, Square s2)
    {
        return (s2 - s1) / square_dist (s1, s2);
    }

#pragma endregion

#pragma region Masks

    inline Bitboard square_bb (Square s)
    {
        return  _square_bb[s];
    }
    inline Bitboard square_bb_ (Square s)
    {
        return ~_square_bb[s];
    }

    inline Bitboard file_bb (File   f)
    {
        return _file_bb[f];
    }
    inline Bitboard file_bb (Square s)
    {
        return _file_bb[_file (s)];
    }

    inline Bitboard rank_bb (Rank   r)
    {
        return _rank_bb[r];
    }
    inline Bitboard rank_bb (Square s)
    {
        return _rank_bb[_rank (s)];
    }

    inline Bitboard diag18_bb (Diag   d)
    {
        return _diag18_bb[d];
    }
    inline Bitboard diag18_bb (Square s)
    {
        return _diag18_bb[_diag18 (s)];
    }

    inline Bitboard diag81_bb (Diag   d)
    {
        return _diag81_bb[d];
    }
    inline Bitboard diag81_bb (Square s)
    {
        return _diag81_bb[_diag81 (s)];
    }

    inline Bitboard adj_files_bb (File   f)
    {
        return _adj_file_bb[f];
    }
    inline Bitboard adj_files_bb (Square s)
    {
        return _adj_file_bb[_file (s)];
    }

    inline Bitboard adj_ranks_bb (Rank   r)
    {
        return _adj_rank_bb[r];
    }
    inline Bitboard adj_ranks_bb (Square s)
    {
        return _adj_rank_bb[_rank (s)];
    }

    inline Bitboard rel_rank_bb (Color c, Rank   r)
    {
        return _rank_bb[rel_rank (c, r)];
    }
    inline Bitboard rel_rank_bb (Color c, Square s)
    {
        return _rank_bb[rel_rank (c, s)];
        //return rel_rank_bb (c, _rank (s));
    }

    // Bitboard of ranks in front of the rank, from the point of view of the given color.
    inline Bitboard front_ranks_bb (Color c, Rank   r)
    {
        return _front_rank_bb[c][r];
    }
    // Bitboard of squares along the line in front of the square, from the point of view of the given color.
    inline Bitboard front_squares_bb (Color c, Square s)
    {
        return _front_squares_bb[c][s];
    }

    // Ring on the square with the distance 'd'
    inline Bitboard dia_rings_bb (Square s, uint8_t d)
    {
        return _dia_rings_bb[s][d];
    }

    inline Bitboard brd_edges_bb (Square s)
    {
        return (((FA_bb | FH_bb) & ~file_bb (s)) | ((R1_bb | R8_bb) & ~rank_bb (s)));
    }

    inline Bitboard betwen_sq_bb (Square s1, Square s2)
    {
        return _betwen_sq_bb[s1][s2];
    }

    // attack_span_pawn_bb() takes a color and a square as input, and returns a bitboard
    // representing all squares that can be attacked by a pawn of the given color
    // when it moves along its file starting from the given square. Definition is:
    // PawnAttackSpan[c][s] = in_front_bb(c, s) & adjacent_files_bb(s);
    inline Bitboard attack_span_pawn_bb (Color c, Square s)
    {
        return _attack_span_pawn_bb[c][s];
    }
    // passer_span_pawn_bb() takes a color and a square as input, and returns a
    // bitboard mask which can be used to test if a pawn of the given color on
    // the given square is a passed pawn. Definition of the table is:
    // PassedPawnMask[c][s] = attack_span_pawn_bb(c, s) | forward_bb(c, s)
    inline Bitboard passer_span_pawn_bb (Color c, Square s)
    {
        return _passer_span_pawn_bb[c][s];
    }

    // squares_of_color() returns a bitboard of all squares with the same color of the given square.
    inline Bitboard squares_of_color(Square s)
    {
        return (DR_SQ_bb & s) ? DR_SQ_bb : LT_SQ_bb;
    }

    inline bool more_than_one (Bitboard bb)
    {
        return bool ((bb) & (bb - 1));
    }

    // Check the squares s1, s2 and s3 are aligned either on a straight/diagonal line.
    inline bool sqrs_aligned (Square s1, Square s2, Square s3)
    {
        return _lines_sq_bb[s1][s2] & s3;
    }

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
    inline Bitboard shift_del (Bitboard bb);

    template<>
    inline Bitboard shift_del<DEL_N > (Bitboard bb)
    {
        return (bb) << (8);
    }
    template<>
    inline Bitboard shift_del<DEL_S > (Bitboard bb)
    {
        return (bb) >> (8);
    }
    template<>
    inline Bitboard shift_del<DEL_E > (Bitboard bb)
    {
        return (bb & FH_bb_) << (1);
    }
    template<>
    inline Bitboard shift_del<DEL_W > (Bitboard bb)
    {
        return (bb & FA_bb_) >> (1);
    }
    template<>
    inline Bitboard shift_del<DEL_NE> (Bitboard bb)
    {
        return (bb & FH_bb_) << (9); //(bb << 9) & FA_bb_;
    }
    template<>
    inline Bitboard shift_del<DEL_SE> (Bitboard bb)
    {
        return (bb & FH_bb_) >> (7); //(bb >> 7) & FA_bb_;
    }
    template<>
    inline Bitboard shift_del<DEL_NW> (Bitboard bb)
    {
        return (bb & FA_bb_) << (7); //(bb << 7) & FH_bb_;
    }
    template<>
    inline Bitboard shift_del<DEL_SW> (Bitboard bb)
    {
        return (bb & FA_bb_) >> (9); //(bb >> 9) & FH_bb_;
    }

#pragma endregion

#pragma region Attacks

    extern Bitboard attacks_sliding (Square s, const Delta deltas[], Bitboard occ = 0);

    template<PType PT>
    // Attacks of the PAWN
    extern inline Bitboard attacks_bb (Color c, Square s);

    template<PType PT>
    // Attacks of the PType
    extern inline Bitboard attacks_bb (Square s);

    template<PType PT>
    // Attacks of the PType with occupancy
    extern Bitboard attacks_bb (Square s, Bitboard occ);

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

    extern inline SquareList squares (Bitboard  bb);

}

#endif
