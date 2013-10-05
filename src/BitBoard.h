//#pragma once
#ifndef BITBOARD_H_
#define BITBOARD_H_

#include "Square.h"
#include "Piece.h"

namespace BitBoard {

#pragma region Constants

    extern const Bitboard bb_FA;
    extern const Bitboard bb_FB;
    extern const Bitboard bb_FC;
    extern const Bitboard bb_FD;
    extern const Bitboard bb_FE;
    extern const Bitboard bb_FF;
    extern const Bitboard bb_FG;
    extern const Bitboard bb_FH;

    extern const Bitboard bb_R1;
    extern const Bitboard bb_R2;
    extern const Bitboard bb_R3;
    extern const Bitboard bb_R4;
    extern const Bitboard bb_R5;
    extern const Bitboard bb_R6;
    extern const Bitboard bb_R7;
    extern const Bitboard bb_R8;

    extern const Bitboard bb_NULL;
    extern const Bitboard bb_FULL;

    extern const Bitboard bb_R1_;
    extern const Bitboard bb_R8_;
    extern const Bitboard bb_FA_;
    extern const Bitboard bb_FH_;

    extern const Bitboard bb_D18;
    extern const Bitboard bb_D81;

    extern const Bitboard bb_SQ_W;
    extern const Bitboard bb_SQ_B;

#pragma endregion

#pragma region LOOKUPs

    //namespace LookUp {

    extern Delta _del_file_rank[F_NO][R_NO];
    extern Delta _del_sq[SQ_NO][SQ_NO];
    extern Delta _del_taxi[SQ_NO][SQ_NO];

    extern uint8_t _b_shift_gap[_UI8_MAX + 1][F_NO];

    extern const Delta _deltas_pawn[CLR_NO][3];
    extern const Delta _deltas_type[PT_NO][9];

    extern const Bitboard _bb_sq[SQ_NO];
    extern const Bitboard _bb_file[F_NO];
    extern const Bitboard _bb_rank[R_NO];

    extern const Bitboard _bb_d18[D_NO];
    extern const Bitboard _bb_d81[D_NO];

    extern const Bitboard _bb_adj_file[F_NO];
    extern const Bitboard _bb_adj_rank[R_NO];
    extern const Bitboard _bb_front_rank[CLR_NO][R_NO];
    extern Bitboard _bb_front_sq[CLR_NO][SQ_NO];

    extern Bitboard _bb_betwen_sq[SQ_NO][SQ_NO];

    extern Bitboard _bb_dia_rings[SQ_NO][F_NO];

    extern Bitboard _bb_attack_span_pawn[CLR_NO][SQ_NO];
    extern Bitboard _bb_passer_span_pawn[CLR_NO][SQ_NO];

    // attacks of the pieces
    extern Bitboard _bb_attacks_pawn[CLR_NO][SQ_NO];
    extern Bitboard _bb_attacks_type[PT_NO][SQ_NO];
    //}

    //using namespace LookUp;

#pragma endregion

#pragma region Operators

    F_INLINE Bitboard  operator&  (Bitboard  bb, Square s)
    {
        return bb &   _bb_sq[s];
    }
    F_INLINE Bitboard  operator|  (Bitboard  bb, Square s)
    {
        return bb | _bb_sq[s];
    }
    F_INLINE Bitboard  operator^  (Bitboard  bb, Square s)
    {
        return bb ^   _bb_sq[s];
    }
    F_INLINE Bitboard  operator+  (Bitboard  bb, Square s)
    {
        return bb | _bb_sq[s];
    }
    F_INLINE Bitboard  operator-  (Bitboard  bb, Square s)
    {
        return bb &  ~_bb_sq[s];
    }
    F_INLINE Bitboard& operator&= (Bitboard &bb, Square s)
    {
        return bb &= _bb_sq[s];
    }
    F_INLINE Bitboard& operator|= (Bitboard &bb, Square s)
    {
        return bb |= _bb_sq[s];
    }
    F_INLINE Bitboard& operator^= (Bitboard &bb, Square s)
    {
        return bb ^= _bb_sq[s];
    }
    F_INLINE Bitboard& operator+= (Bitboard &bb, Square s)
    {
        return bb |= _bb_sq[s];
    }
    F_INLINE Bitboard& operator-= (Bitboard &bb, Square s)
    {
        return bb &= ~_bb_sq[s];
    }

    F_INLINE Bitboard  operator&  (Bitboard  bb, File   f)
    {
        return bb &   _bb_file[f];
    }
    F_INLINE Bitboard  operator|  (Bitboard  bb, File   f)
    {
        return bb | _bb_file[f];
    }
    F_INLINE Bitboard  operator^  (Bitboard  bb, File   f)
    {
        return bb ^   _bb_file[f];
    }
    F_INLINE Bitboard  operator+  (Bitboard  bb, File   f)
    {
        return bb | _bb_file[f];
    }
    F_INLINE Bitboard  operator-  (Bitboard  bb, File   f)
    {
        return bb &  ~_bb_file[f];
    }
    F_INLINE Bitboard& operator&= (Bitboard &bb, File   f)
    {
        return bb &= _bb_file[f];
    }
    F_INLINE Bitboard& operator|= (Bitboard &bb, File   f)
    {
        return bb |= _bb_file[f];
    }
    F_INLINE Bitboard& operator^= (Bitboard &bb, File   f)
    {
        return bb ^= _bb_file[f];
    }
    F_INLINE Bitboard& operator+= (Bitboard &bb, File   f)
    {
        return bb |= _bb_file[f];
    }
    F_INLINE Bitboard& operator-= (Bitboard &bb, File   f)
    {
        return bb &= ~_bb_file[f];
    }

    F_INLINE Bitboard  operator&  (Bitboard  bb, Rank   r)
    {
        return bb &   _bb_rank[r];
    }
    F_INLINE Bitboard  operator|  (Bitboard  bb, Rank   r)
    {
        return bb | _bb_rank[r];
    }
    F_INLINE Bitboard  operator^  (Bitboard  bb, Rank   r)
    {
        return bb ^   _bb_rank[r];
    }
    F_INLINE Bitboard  operator+  (Bitboard  bb, Rank   r)
    {
        return bb | _bb_rank[r];
    }
    F_INLINE Bitboard  operator-  (Bitboard  bb, Rank   r)
    {
        return bb &  ~_bb_rank[r];
    }
    F_INLINE Bitboard& operator&= (Bitboard &bb, Rank   r)
    {
        return bb &= _bb_rank[r];
    }
    F_INLINE Bitboard& operator|= (Bitboard &bb, Rank   r)
    {
        return bb |= _bb_rank[r];
    }
    F_INLINE Bitboard& operator^= (Bitboard &bb, Rank   r)
    {
        return bb ^= _bb_rank[r];
    }
    F_INLINE Bitboard& operator+= (Bitboard &bb, Rank   r)
    {
        return bb |= _bb_rank[r];
    }
    F_INLINE Bitboard& operator-= (Bitboard &bb, Rank   r)
    {
        return bb &= ~_bb_rank[r];
    }

#pragma endregion

#pragma region Deltas

    F_INLINE Delta dist_file (Square s1, Square s2)
    {
        //return abs(int8_t(_file(s1) - _file(s2)));
        return _del_file_rank[_file (s1)][_file (s2)];
    }
    F_INLINE Delta dist_rank (Square s1, Square s2)
    {
        //return abs(int8_t(_rank(s1) - _rank(s2)));
        return _del_file_rank[_rank (s1)][_rank (s2)];
    }
    F_INLINE Delta dist_sq (Square s1, Square s2)
    {
        return _del_sq[s1][s2];
    }
    F_INLINE Delta dist_taxi (Square s1, Square s2)
    {
        return _del_taxi[s1][s2];
    }

    // Absolute Difference of rank & file
    F_INLINE Delta diff_rank_file (Square s1, Square s2)
    {
        int8_t rd = (s1 | 7) - (s2 | 7);
        int8_t fd = (s1 & 7) - (s2 & 7);
        return Delta (abs (rd) + abs (fd));
    }

    F_INLINE Delta offset_sq (Square s1, Square s2)
    {
        return (s2 - s1) / dist_sq (s1, s2);
    }

#pragma endregion

#pragma region Masks

    inline Bitboard mask_sq (Square s)
    {
        return  _bb_sq[s];
    }
    inline Bitboard mask_sq_ (Square s)
    {
        return ~_bb_sq[s];
    }

    inline Bitboard mask_file (File   f)
    {
        return _bb_file[f];
    }
    inline Bitboard mask_file (Square s)
    {
        return _bb_file[_file (s)];
    }

    inline Bitboard mask_rank (Rank   r)
    {
        return _bb_rank[r];
    }
    inline Bitboard mask_rank (Square s)
    {
        return _bb_rank[_rank (s)];
    }

    inline Bitboard mask_diag18 (Diag   d)
    {
        return _bb_d18[d];
    }
    inline Bitboard mask_diag18 (Square s)
    {
        //  int8_t diag = 0x00 + (FileIndex(s) << 3) - RankIndex(s);
        //  uint8_t nort = -diag & (diag >> 31);
        //  uint8_t sout = diag & (-diag >> 31);
        //  return (bb_D18 >> sout) << nort;
        return _bb_d18[_diag18 (s)];
    }

    inline Bitboard mask_diag81 (Diag   d)
    {
        return _bb_d81[d];
    }
    inline Bitboard mask_diag81 (Square s)
    {
        //  int8_t diag = 0x38 - (FileIndex(s) << 3) - RankIndex(s);
        //  uint8_t nort = -diag & (diag >> 31);
        //  uint8_t sout = diag & (-diag >> 31);
        //  return (Diag81Squares >> sout) << nort;
        return _bb_d81[_diag81 (s)];
    }

    inline Bitboard mask_adj_files (File   f)
    {
        return _bb_adj_file[f];
    }
    inline Bitboard mask_adj_files (Square s)
    {
        return _bb_adj_file[_file (s)];
    }

    inline Bitboard mask_adj_ranks (Rank   r)
    {
        return _bb_adj_rank[r];
    }
    inline Bitboard mask_adj_ranks (Square s)
    {
        return _bb_adj_rank[_rank (s)];
    }

    inline Bitboard mask_rel_rank (Color c, Rank   r)
    {
        return _bb_rank[rel_rank (c, r)];//_bb_REL_R[c][r];
    }
    inline Bitboard mask_rel_rank (Color c, Square s)
    {
        return _bb_rank[rel_rank (c, s)];//mask_rel_rank (c, _rank(s));
    }

    // Bitboard of squares along the line in front of the square, from the point of view of the given color.
    inline Bitboard mask_front_ranks (Color c, Rank   r)
    {
        return _bb_front_rank[c][r];
    }
    inline Bitboard mask_front_sq (Color c, Square s)
    {
        return _bb_front_sq[c][s];
    }

    // Ring on the square with the distance 'd'
    inline Bitboard mask_dist_ring (Square s, uint8_t d)
    {
        return _bb_dia_rings[s][d];
    }

    inline Bitboard mask_brd_edges (Square s)
    {
        return (((bb_FA | bb_FH) & ~mask_file (s)) | ((bb_R1 | bb_R8) & ~mask_rank (s)));
    }

    inline Bitboard mask_btw_sq (Square s1, Square s2)
    {
        return _bb_betwen_sq[s1][s2];
    }

    /// attack_span_pawn() takes a color and a square as input, and returns a bitboard
    /// representing all squares that can be attacked by a pawn of the given color
    /// when it moves along its file starting from the given square. Definition is:
    /// PawnAttackSpan[c][s] = in_front_bb(c, s) & adjacent_files_bb(s);
    inline Bitboard attack_span_pawn (Color c, Square s)
    {
        return _bb_attack_span_pawn[c][s];
    }
    /// passer_span_pawn() takes a color and a square as input, and returns a
    /// bitboard mask which can be used to test if a pawn of the given color on
    /// the given square is a passed pawn. Definition of the table is:
    /// PassedPawnMask[c][s] = attack_span_pawn(c, s) | forward_bb(c, s)
    inline Bitboard passer_span_pawn (Color c, Square s)
    {
        return _bb_passer_span_pawn[c][s];
    }

    inline bool more_than_one (Bitboard bb)
    {
        return bool ((bb) & (bb - 1));
    }
    // Check the squares s1, s2 and s3 are aligned either on a straight/diagonal line.
    inline bool is_sqs_aligned (Square s1, Square s2, Square s3)
    {
        return
            (
            _bb_sq[s1] |
            _bb_sq[s2] |
            _bb_sq[s3]
        ) &
            (
            _bb_betwen_sq[s1][s2] |
            _bb_betwen_sq[s2][s3] |
            _bb_betwen_sq[s3][s1]
        );
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

    template<Delta DELTA>
    // Shift the Bitboard using delta
    extern Bitboard shift_del (Bitboard bb);

    template<>
    inline Bitboard shift_del<DEL_N> (Bitboard bb)
    {
        return (bb) << (8);
    }
    template<>
    inline Bitboard shift_del<DEL_S> (Bitboard bb)
    {
        return (bb) >> (8);
    }
    template<>
    inline Bitboard shift_del<DEL_E> (Bitboard bb)
    {
        return (bb & bb_FH_) << (1);
    }
    template<>
    inline Bitboard shift_del<DEL_W> (Bitboard bb)
    {
        return (bb & bb_FA_) >> (1);
    }
    template<>
    inline Bitboard shift_del<DEL_NE> (Bitboard bb)
    {
        return (bb & bb_FH_) << (9); //(bb << 9) & bb_FA_;
    }
    template<>
    inline Bitboard shift_del<DEL_SE> (Bitboard bb)
    {
        return (bb & bb_FH_) >> (7); //(bb >> 7) & bb_FA_;
    }
    template<>
    inline Bitboard shift_del<DEL_NW> (Bitboard bb)
    {
        return (bb & bb_FA_) << (7); //(bb << 7) & bb_FH_;
    }
    template<>
    inline Bitboard shift_del<DEL_SW> (Bitboard bb)
    {
        return (bb & bb_FA_) >> (9); //(bb >> 9) & bb_FH_;
    }

#pragma endregion

#pragma region Attacks

    extern Bitboard attacks_sliding (Square s, const Delta deltas[], Bitboard occ = BitBoard::bb_NULL);

    template<PType PT>
    // Attacks of the PAWN
    extern inline Bitboard attacks_bb (Color c, Square s);

    template<PType PT>
    // Attacks of the Piece
    extern inline Bitboard attacks_bb (Square s);
    template<PType PT>
    // Attacks of the Piece with occupancy
    extern inline Bitboard attacks_bb (Square s, Bitboard occ);

    extern inline Bitboard attacks_bb (Piece p, Square s, Bitboard occ);

#pragma endregion

    extern void initialize ();

#pragma region Printing

    extern Bitboard to_bitboard (const char s[], int32_t radix = 16);
    extern Bitboard to_bitboard (const ::std::string &s, int32_t radix = 16);

    extern ::std::string to_hex_str (std::string &sbitboard);

    extern void print_bit (Bitboard bb, uint8_t x = 64, char p = 'o');
    extern void print_bin (Bitboard bb);
    extern void print (Bitboard bb, char p = 'o');

#pragma endregion

    extern inline SquareList squares (Bitboard  bb);

}

#endif
