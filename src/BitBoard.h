//#pragma once
#ifndef BITBOARD_H_
#define BITBOARD_H_

#include "Type.h"

namespace BitBoard {

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

    extern const Bitboard R1_bb_;  // 56 Not RANK-1
    extern const Bitboard R8_bb_;  // 56 Not RANK-8
    extern const Bitboard FA_bb_;  // 56 Not FILE-A
    extern const Bitboard FH_bb_;  // 56 Not FILE-H

    extern const Bitboard D18_bb;  // 08 DIAG-18 squares.
    extern const Bitboard D81_bb;  // 08 DIAG-81 squares.

    extern const Bitboard LIHT_bb; // 32 LIGHT squares.
    extern const Bitboard DARK_bb; // 32 DARK  squares.

    extern const Bitboard CRNR_bb;
    extern const Bitboard MID_EDGE_bb;

    extern uint8_t _filerank_dist[F_NO][R_NO];
    extern uint8_t   _square_dist[SQ_NO][SQ_NO];
    extern uint8_t  _taxicab_dist[SQ_NO][SQ_NO];

    extern const Delta _deltas_pawn[CLR_NO][3];
    extern const Delta _deltas_type[NONE][9];

    extern const int8_t _center_dist[SQ_NO]; 
    extern const int8_t _manhattan_center_dist[SQ_NO];

    CACHE_ALIGN(64) extern const Bitboard _square_bb[SQ_NO];
    CACHE_ALIGN(64) extern const Bitboard   _file_bb[F_NO];
    CACHE_ALIGN(64) extern const Bitboard   _rank_bb[R_NO];

    CACHE_ALIGN(64) extern const Bitboard _diag18_bb[D_NO];
    CACHE_ALIGN(64) extern const Bitboard _diag81_bb[D_NO];

    CACHE_ALIGN(64) extern const Bitboard _adj_file_bb[F_NO];
    CACHE_ALIGN(64) extern const Bitboard _adj_rank_bb[R_NO];
    CACHE_ALIGN(64) extern const Bitboard _front_rank_bb[CLR_NO][R_NO];
    CACHE_ALIGN(64) extern Bitboard _front_squares_bb[CLR_NO][SQ_NO];

    CACHE_ALIGN(64) extern Bitboard _betwen_sq_bb[SQ_NO][SQ_NO];
    CACHE_ALIGN(64) extern Bitboard  _lines_sq_bb[SQ_NO][SQ_NO];

    CACHE_ALIGN(64) extern Bitboard _dist_rings_bb[SQ_NO][F_NO];

    CACHE_ALIGN(64) extern Bitboard _pawn_attack_span_bb[CLR_NO][SQ_NO];
    CACHE_ALIGN(64) extern Bitboard _passer_pawn_span_bb[CLR_NO][SQ_NO];


    // attacks of the pieces
    CACHE_ALIGN(64) extern Bitboard _attacks_pawn_bb[CLR_NO][SQ_NO];
    CACHE_ALIGN(64) extern Bitboard _attacks_type_bb[NONE][SQ_NO];

    CACHE_ALIGN(64) extern Bitboard *BAttack_bb[SQ_NO];
    CACHE_ALIGN(64) extern Bitboard *RAttack_bb[SQ_NO];

    CACHE_ALIGN(64) extern Bitboard    BMask_bb[SQ_NO];
    CACHE_ALIGN(64) extern Bitboard    RMask_bb[SQ_NO];

    CACHE_ALIGN(64) extern Bitboard   BMagic_bb[SQ_NO];
    CACHE_ALIGN(64) extern Bitboard   RMagic_bb[SQ_NO];

    CACHE_ALIGN(8) extern uint8_t        BShift[SQ_NO];
    CACHE_ALIGN(8) extern uint8_t        RShift[SQ_NO];


    inline Bitboard  operator&  (Bitboard  bb, Square s) { return bb &  _square_bb[s]; }
    inline Bitboard  operator|  (Bitboard  bb, Square s) { return bb |  _square_bb[s]; }
    inline Bitboard  operator^  (Bitboard  bb, Square s) { return bb ^  _square_bb[s]; }
    inline Bitboard  operator+  (Bitboard  bb, Square s) { return bb |  _square_bb[s]; }
    inline Bitboard  operator-  (Bitboard  bb, Square s) { return bb &~ _square_bb[s]; }
    inline Bitboard& operator&= (Bitboard &bb, Square s) { return bb &= _square_bb[s]; }
    inline Bitboard& operator|= (Bitboard &bb, Square s) { return bb |= _square_bb[s]; }
    inline Bitboard& operator^= (Bitboard &bb, Square s) { return bb ^= _square_bb[s]; }
    inline Bitboard& operator+= (Bitboard &bb, Square s) { return bb |= _square_bb[s]; }
    inline Bitboard& operator-= (Bitboard &bb, Square s) { return bb &=~_square_bb[s]; }

    inline Bitboard  operator&  (Bitboard  bb, File   f) { return bb &  _file_bb[f]; }
    inline Bitboard  operator|  (Bitboard  bb, File   f) { return bb |  _file_bb[f]; }
    inline Bitboard  operator^  (Bitboard  bb, File   f) { return bb ^  _file_bb[f]; }
    inline Bitboard  operator+  (Bitboard  bb, File   f) { return bb |  _file_bb[f]; }
    inline Bitboard  operator-  (Bitboard  bb, File   f) { return bb & ~_file_bb[f]; }
    inline Bitboard& operator&= (Bitboard &bb, File   f) { return bb &= _file_bb[f]; }
    inline Bitboard& operator|= (Bitboard &bb, File   f) { return bb |= _file_bb[f]; }
    inline Bitboard& operator^= (Bitboard &bb, File   f) { return bb ^= _file_bb[f]; }
    inline Bitboard& operator+= (Bitboard &bb, File   f) { return bb |= _file_bb[f]; }
    inline Bitboard& operator-= (Bitboard &bb, File   f) { return bb &=~_file_bb[f]; }

    inline Bitboard  operator&  (Bitboard  bb, Rank   r) { return bb &  _rank_bb[r]; }
    inline Bitboard  operator|  (Bitboard  bb, Rank   r) { return bb |  _rank_bb[r]; }
    inline Bitboard  operator^  (Bitboard  bb, Rank   r) { return bb ^  _rank_bb[r]; }
    inline Bitboard  operator+  (Bitboard  bb, Rank   r) { return bb |  _rank_bb[r]; }
    inline Bitboard  operator-  (Bitboard  bb, Rank   r) { return bb & ~_rank_bb[r]; }
    inline Bitboard& operator&= (Bitboard &bb, Rank   r) { return bb &= _rank_bb[r]; }
    inline Bitboard& operator|= (Bitboard &bb, Rank   r) { return bb |= _rank_bb[r]; }
    inline Bitboard& operator^= (Bitboard &bb, Rank   r) { return bb ^= _rank_bb[r]; }
    inline Bitboard& operator+= (Bitboard &bb, Rank   r) { return bb |= _rank_bb[r]; }
    inline Bitboard& operator-= (Bitboard &bb, Rank   r) { return bb &=~_rank_bb[r]; }

    inline uint8_t file_dist (File f1, File f2)     { return _filerank_dist[f1][f2]; }
    inline uint8_t file_dist (Square s1, Square s2) { return _filerank_dist[_file (s1)][_file (s2)]; }

    inline uint8_t rank_dist (Rank r1, Rank r2)     { return _filerank_dist[r1][r2]; }
    inline uint8_t rank_dist (Square s1, Square s2) { return _filerank_dist[_rank (s1)][_rank (s2)]; }

    inline uint8_t  square_dist (Square s1, Square s2) { return  _square_dist[s1][s2]; }
    inline uint8_t taxicab_dist (Square s1, Square s2) { return _taxicab_dist[s1][s2]; }

    // ----------------------------------------------------

    inline Bitboard square_bb  (Square s) { return  _square_bb[s]; }
    inline Bitboard square_bb_ (Square s) { return ~_square_bb[s]; }

    inline Bitboard file_bb (File   f) { return _file_bb[f]; }
    inline Bitboard file_bb (Square s) { return _file_bb[_file (s)]; }

    inline Bitboard rank_bb (Rank   r) { return _rank_bb[r]; }
    inline Bitboard rank_bb (Square s) { return _rank_bb[_rank (s)]; }

    inline Bitboard diag18_bb (Diag   d) { return _diag18_bb[d]; }
    inline Bitboard diag18_bb (Square s) { return _diag18_bb[_diag18 (s)]; }

    inline Bitboard diag81_bb (Diag   d) { return _diag81_bb[d]; }
    inline Bitboard diag81_bb (Square s) { return _diag81_bb[_diag81 (s)]; }

    inline Bitboard adj_files_bb (File   f) { return _adj_file_bb[f]; }
    inline Bitboard adj_files_bb (Square s) { return _adj_file_bb[_file (s)]; }

    inline Bitboard adj_ranks_bb (Rank   r) { return _adj_rank_bb[r]; }
    inline Bitboard adj_ranks_bb (Square s) { return _adj_rank_bb[_rank (s)]; }

    inline Bitboard rel_rank_bb (Color c, Rank   r) { return _rank_bb[rel_rank (c, r)]; }
    inline Bitboard rel_rank_bb (Color c, Square s)
    {
        return _rank_bb[rel_rank (c, s)];
    }

    // Bitboard of ranks in front of the rank, from the point of view of the given color.
    inline Bitboard front_ranks_bb   (Color c, Rank   r) { return _front_rank_bb[c][r]; }
    // Bitboard of squares along the line in front of the square, from the point of view of the given color.
    inline Bitboard front_squares_bb (Color c, Square s) { return _front_squares_bb[c][s]; }

    // Ring on the square with the distance 'd'
    inline Bitboard dist_rings_bb   (Square s, uint8_t d) { return _dist_rings_bb[s][d]; }

    inline Bitboard brd_edges_bb    (Square s) { return (((FA_bb | FH_bb) & ~file_bb (s)) | ((R1_bb | R8_bb) & ~rank_bb (s))); }

    // pawn_attack_span_bb() takes a color and a square as input, and returns a bitboard
    // representing all squares that can be attacked by a pawn of the given color
    // when it moves along its file starting from the given square. Definition is:
    // PawnAttackSpan[c][s] = in_front_bb(c, s) & adjacent_files_bb(s);
    inline Bitboard pawn_attack_span_bb (Color c, Square s) { return _pawn_attack_span_bb[c][s]; }
    // passer_pawn_span_bb() takes a color and a square as input, and returns a
    // bitboard mask which can be used to test if a pawn of the given color on
    // the given square is a passed pawn. Definition of the table is:
    // PassedPawnMask[c][s] = pawn_attack_span_bb(c, s) | forward_bb(c, s)
    inline Bitboard passer_pawn_span_bb (Color c, Square s) { return _passer_pawn_span_bb[c][s]; }

    // squares_of_color() returns a bitboard of all squares with the same color of the given square.
    inline Bitboard squares_of_color (Square s) { return (DARK_bb & s) ? DARK_bb : LIHT_bb; }

    // between_bb() returns a bitboard representing all squares between two squares.
    // For instance,
    // between_bb(SQ_C4, SQ_F7) returns a bitboard with the bits for square d5 and e6 set.
    // If s1 and s2 are not on the same rank, file or diagonal, 0 is returned.
    inline Bitboard betwen_sq_bb (Square s1, Square s2) { return _betwen_sq_bb[s1][s2]; }

    // Check the squares s1, s2 and s3 are aligned either on a straight/diagonal line.
    inline bool sqrs_aligned    (Square s1, Square s2, Square s3) { return _lines_sq_bb[s1][s2] & s3; }

    inline bool more_than_one (Bitboard bb) { return bool ((bb) & (bb - 1)); }

    template<Delta D>
    // Shift the Bitboard using delta
    inline Bitboard shift_del (Bitboard bb);

    template<>
    inline Bitboard shift_del<DEL_N > (Bitboard bb) { return (bb) << (8); }
    template<>
    inline Bitboard shift_del<DEL_S > (Bitboard bb) { return (bb) >> (8); }
    template<>
    inline Bitboard shift_del<DEL_E > (Bitboard bb) { return (bb & FH_bb_) << (1); }
    template<>
    inline Bitboard shift_del<DEL_W > (Bitboard bb) { return (bb & FA_bb_) >> (1); }
    template<>
    inline Bitboard shift_del<DEL_NE> (Bitboard bb) { return (bb & FH_bb_) << (9); } //(bb << 9) & FA_bb_;
    template<>
    inline Bitboard shift_del<DEL_SE> (Bitboard bb) { return (bb & FH_bb_) >> (7); } //(bb >> 7) & FA_bb_;
    template<>
    inline Bitboard shift_del<DEL_NW> (Bitboard bb) { return (bb & FA_bb_) << (7); } //(bb << 7) & FH_bb_;
    template<>
    inline Bitboard shift_del<DEL_SW> (Bitboard bb) { return (bb & FA_bb_) >> (9); } //(bb >> 9) & FH_bb_;

    // Rotate RIGHT (toward LSB)
    inline Bitboard rotate_R (Bitboard bb, int8_t k) { return (bb >> k) | (bb << (int8_t (SQ_NO) - k)); }
    // Rotate LEFT (toward MSB)
    inline Bitboard rotate_L (Bitboard bb, int8_t k) { return (bb << k) | (bb >> (int8_t (SQ_NO) - k)); }


    inline Bitboard attacks_sliding (Square s, const Delta deltas[], Bitboard occ = U64 (0))
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

    template<PieceT PT>
    // Attacks of the PAWN
    extern INLINE Bitboard attacks_bb (Color c, Square s);

    template<>
    // PAWN attacks
    INLINE Bitboard attacks_bb<PAWN> (Color c, Square s) { return _attacks_pawn_bb[c][s]; }

    // --------------------------------
    template<PieceT PT>
    // Attacks of the PieceT
    extern INLINE Bitboard attacks_bb (Square s);

    template<PieceT PT>
    // Attacks of the PieceT
    INLINE Bitboard attacks_bb (Square s) { return _attacks_type_bb[PT][s]; }
    // --------------------------------
    // explicit template instantiations
    template Bitboard attacks_bb<NIHT> (Square s);
    template Bitboard attacks_bb<BSHP> (Square s);
    template Bitboard attacks_bb<ROOK> (Square s);
    template Bitboard attacks_bb<QUEN> (Square s);
    template Bitboard attacks_bb<KING> (Square s);
    // --------------------------------

    template<PieceT PT>
    // Attacks of the PieceT with occupancy
    extern INLINE Bitboard attacks_bb (Square s, Bitboard occ);

    template<>
    // KNIGHT attacks
    INLINE Bitboard attacks_bb<NIHT> (Square s, Bitboard occ) { (void)occ; return _attacks_type_bb[NIHT][s]; }
    template<>
    // KING attacks
    INLINE Bitboard attacks_bb<KING> (Square s, Bitboard occ) { (void)occ; return _attacks_type_bb[KING][s]; }
    // --------------------------------

    template<PieceT PT>
    // Function 'indexer(s, occ)' for computing index for sliding attack bitboards.
    // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
    // and returns a bitboard representing all squares attacked by PT (BISHOP or ROOK) on the given square.
    extern INLINE uint16_t indexer   (Square s, Bitboard occ);

    template<>
    INLINE uint16_t indexer   <BSHP> (Square s, Bitboard occ)
    {

#ifdef _64BIT
        return uint16_t (((occ & BMask_bb[s]) * BMagic_bb[s]) >> BShift[s]);
#else
        uint32_t lo = (uint32_t (occ >>  0) & uint32_t (BMask_bb[s] >>  0)) * uint32_t (BMagic_bb[s] >>  0);
        uint32_t hi = (uint32_t (occ >> 32) & uint32_t (BMask_bb[s] >> 32)) * uint32_t (BMagic_bb[s] >> 32);
        return ((lo ^ hi) >> BShift[s]);
#endif

    }

    template<>
    INLINE uint16_t indexer   <ROOK> (Square s, Bitboard occ)
    {

#ifdef _64BIT
        return uint16_t (((occ & RMask_bb[s]) * RMagic_bb[s]) >> RShift[s]);
#else
        uint32_t lo = (uint32_t (occ >>  0) & uint32_t (RMask_bb[s] >>  0)) * uint32_t (RMagic_bb[s] >>  0);
        uint32_t hi = (uint32_t (occ >> 32) & uint32_t (RMask_bb[s] >> 32)) * uint32_t (RMagic_bb[s] >> 32);
        return ((lo ^ hi) >> RShift[s]);
#endif

    }

    template<>
    // Attacks of the BISHOP with occupancy
    INLINE Bitboard attacks_bb<BSHP> (Square s, Bitboard occ) { return BAttack_bb[s][indexer<BSHP> (s, occ)]; }
    template<>
    // Attacks of the ROOK with occupancy
    INLINE Bitboard attacks_bb<ROOK> (Square s, Bitboard occ) { return RAttack_bb[s][indexer<ROOK> (s, occ)]; }
    template<>
    // QUEEN Attacks with occ
    INLINE Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return
            BAttack_bb[s][indexer<BSHP> (s, occ)] |
            RAttack_bb[s][indexer<ROOK> (s, occ)];
    }
    // --------------------------------

    // Piece attacks from square
    INLINE Bitboard attacks_bb (Piece p, Square s, Bitboard occ)
    {
        switch (_ptype (p))
        {
        case PAWN: return attacks_bb<PAWN> (_color (p), s);
        case BSHP: return attacks_bb<BSHP> (s, occ);
        case ROOK: return attacks_bb<ROOK> (s, occ);
        case QUEN: return attacks_bb<BSHP> (s, occ)
                       |  attacks_bb<ROOK> (s, occ);
        case NIHT: return attacks_bb<NIHT>(s);
        case KING: return attacks_bb<KING>(s);
        default  : return U64 (0);
        }
        return U64 (0);
    }

    extern void initialize ();

}

#endif
