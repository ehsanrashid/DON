#ifndef _BITBOARD_H_INC_
#define _BITBOARD_H_INC_

#include "Type.h"
#include "BitCount.h"
#include "BitScan.h"

#ifdef BM2
#   include <immintrin.h> // Header for bmi2 instructions
#endif

namespace BitBoard {

    const Bitboard FA_bb = U64(0x0101010101010101);
    const Bitboard FB_bb = FA_bb << 1;//U64(0x0202020202020202);
    const Bitboard FC_bb = FA_bb << 2;//U64(0x0404040404040404);
    const Bitboard FD_bb = FA_bb << 3;//U64(0x0808080808080808);
    const Bitboard FE_bb = FA_bb << 4;//U64(0x1010101010101010);
    const Bitboard FF_bb = FA_bb << 5;//U64(0x2020202020202020);
    const Bitboard FG_bb = FA_bb << 6;//U64(0x4040404040404040);
    const Bitboard FH_bb = FA_bb << 7;//U64(0x8080808080808080);

    const Bitboard R1_bb = U64(0x00000000000000FF);
    const Bitboard R2_bb = R1_bb << (8 * 1);//U64(0x000000000000FF00);
    const Bitboard R3_bb = R1_bb << (8 * 2);//U64(0x0000000000FF0000);
    const Bitboard R4_bb = R1_bb << (8 * 3);//U64(0x00000000FF000000);
    const Bitboard R5_bb = R1_bb << (8 * 4);//U64(0x000000FF00000000);
    const Bitboard R6_bb = R1_bb << (8 * 5);//U64(0x0000FF0000000000);
    const Bitboard R7_bb = R1_bb << (8 * 6);//U64(0x00FF000000000000);
    const Bitboard R8_bb = R1_bb << (8 * 7);//U64(0xFF00000000000000);

    const Bitboard R1_bb_ = ~R1_bb;//U64(0xFFFFFFFFFFFFFF00);    // 56 Not RANK-1
    const Bitboard R8_bb_ = ~R8_bb;//U64(0x00FFFFFFFFFFFFFF);    // 56 Not RANK-8
    const Bitboard FA_bb_ = ~FA_bb;//U64(0xFEFEFEFEFEFEFEFE);    // 56 Not FILE-A
    const Bitboard FH_bb_ = ~FH_bb;//U64(0x7F7F7F7F7F7F7F7F);    // 56 Not FILE-H

    const Bitboard D18_bb = U64(0x8040201008040201);             // 08 DIAG-18 squares.
    const Bitboard D81_bb = U64(0x0102040810204080);             // 08 DIAG-81 squares.

    const Bitboard LIHT_bb = U64(0x55AA55AA55AA55AA);            // 32 LIGHT squares.
    const Bitboard DARK_bb = U64(0xAA55AA55AA55AA55);            // 32 DARK  squares.

    const Bitboard CORNER_bb    = (FA_bb | FH_bb)&(R1_bb | R8_bb);  // 04 CORNER squares.
    const Bitboard FILE_EDGE_bb = (FA_bb | FH_bb);
    const Bitboard RIM_EDGE_bb  = (FILE_EDGE_bb | R1_bb | R8_bb);
    const Bitboard END_EDGE_bb  = (FA_bb | FH_bb)&(R2_bb | R3_bb);
    const Bitboard EXT_CENTER_bb[CLR_NO] =
    {
        (FB_bb | FC_bb | FD_bb | FE_bb | FF_bb | FG_bb) & (R2_bb | R3_bb | R4_bb | R5_bb | R6_bb),
        (FB_bb | FC_bb | FD_bb | FE_bb | FF_bb | FG_bb) & (R3_bb | R4_bb | R5_bb | R6_bb | R7_bb)
    };

    const Delta PAWN_DELTAS[CLR_NO][3] =
    {
        { DEL_NW, DEL_NE, DEL_O },
        { DEL_SE, DEL_SW, DEL_O },
    };
    const Delta PIECE_DELTAS[NONE][9] =
    {
        { DEL_O },
        { DEL_SSW, DEL_SSE, DEL_WWS, DEL_EES, DEL_WWN, DEL_EEN, DEL_NNW, DEL_NNE, DEL_O },
        { DEL_SW, DEL_SE, DEL_NW, DEL_NE, DEL_O },
        { DEL_S, DEL_W, DEL_E, DEL_N, DEL_O },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE, DEL_O },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE, DEL_O },
    };

    // SQUARES
    const Bitboard SQUARE_bb[SQ_NO] =
    {
#undef S_16
#undef S_8
#undef S_4
#undef S_2
#define S_2(n)  U64(1)<<(2*(n)),  U64(1)<<(2*(n)+1)
#define S_4(n)       S_2(2*(n)),       S_2(2*(n)+1)
#define S_8(n)       S_4(2*(n)),       S_4(2*(n)+1)
#define S_16(n)      S_8(2*(n)),       S_8(2*(n)+1)
        S_16 (0), S_16 (1), S_16 (2), S_16 (3),
#undef S_16
#undef S_8
#undef S_4
#undef S_2
    };
    // FILES
    const Bitboard   FILE_bb[F_NO] =
    {
        FA_bb, FB_bb, FC_bb, FD_bb, FE_bb, FF_bb, FG_bb, FH_bb
    };
    // RANKS
    const Bitboard   RANK_bb[R_NO] =
    {
        R1_bb, R2_bb, R3_bb, R4_bb, R5_bb, R6_bb, R7_bb, R8_bb
    };

    // ADJACENT FILES used for isolated-pawn
    const Bitboard ADJ_FILE_bb[F_NO] =
    {
        FB_bb,
        FA_bb|FC_bb,
        FB_bb|FD_bb,
        FC_bb|FE_bb,
        FD_bb|FF_bb,
        FE_bb|FG_bb,
        FF_bb|FH_bb,
        FG_bb
    };
    // ADJACENT RANKS
    const Bitboard ADJ_RANK_bb[R_NO] =
    {
        R2_bb,
        R1_bb|R3_bb,
        R2_bb|R4_bb,
        R3_bb|R5_bb,
        R4_bb|R6_bb,
        R5_bb|R7_bb,
        R6_bb|R8_bb,
        R7_bb,
    };
    // FRONT RANK
    const Bitboard FRONT_RANK_bb[CLR_NO][R_NO] =
    {
        {
        R2_bb|R3_bb|R4_bb|R5_bb|R6_bb|R7_bb|R8_bb,
        R3_bb|R4_bb|R5_bb|R6_bb|R7_bb|R8_bb,
        R4_bb|R5_bb|R6_bb|R7_bb|R8_bb,
        R5_bb|R6_bb|R7_bb|R8_bb,
        R6_bb|R7_bb|R8_bb,
        R7_bb|R8_bb,
        R8_bb,
        0,
        },
        {
        0,
        R1_bb,
        R2_bb|R1_bb,
        R3_bb|R2_bb|R1_bb,
        R4_bb|R3_bb|R2_bb|R1_bb,
        R5_bb|R4_bb|R3_bb|R2_bb|R1_bb,
        R6_bb|R5_bb|R4_bb|R3_bb|R2_bb|R1_bb,
        R7_bb|R6_bb|R5_bb|R4_bb|R3_bb|R2_bb|R1_bb
        }
    };

    extern Bitboard FRONT_SQRS_bb[CLR_NO][SQ_NO];

    extern Bitboard BETWEEN_SQRS_bb[SQ_NO][SQ_NO];
    extern Bitboard RAY_LINE_bb[SQ_NO][SQ_NO];

    extern Bitboard DIST_RINGS_bb[SQ_NO][F_NO];

    extern Bitboard PAWN_ATTACK_SPAN[CLR_NO][SQ_NO];
    extern Bitboard PAWN_PASS_SPAN[CLR_NO][SQ_NO];

    // attacks of the pawns & pieces
    extern Bitboard PAWN_ATTACKS[CLR_NO][SQ_NO];
    extern Bitboard PIECE_ATTACKS[NONE][SQ_NO];

    extern Bitboard *B_ATTACK_bb[SQ_NO];
    extern Bitboard *R_ATTACK_bb[SQ_NO];

    extern Bitboard    B_MASK_bb[SQ_NO];
    extern Bitboard    R_MASK_bb[SQ_NO];

#ifndef BM2
    extern Bitboard   B_MAGIC_bb[SQ_NO];
    extern Bitboard   R_MAGIC_bb[SQ_NO];

    extern u08          B_SHIFT  [SQ_NO];
    extern u08          R_SHIFT  [SQ_NO];
#endif

    extern u08     F_R_DIST[F_NO][R_NO];
    extern u08     SQR_DIST[SQ_NO][SQ_NO];

    inline Bitboard  operator&  (Bitboard  bb, Square s) { return bb &  SQUARE_bb[s]; }
    inline Bitboard  operator|  (Bitboard  bb, Square s) { return bb |  SQUARE_bb[s]; }
    inline Bitboard  operator^  (Bitboard  bb, Square s) { return bb ^  SQUARE_bb[s]; }
    inline Bitboard  operator+  (Bitboard  bb, Square s) { return bb |  SQUARE_bb[s]; }
    inline Bitboard  operator-  (Bitboard  bb, Square s) { return bb &~ SQUARE_bb[s]; }
    inline Bitboard& operator&= (Bitboard &bb, Square s) { return bb &= SQUARE_bb[s]; }
    inline Bitboard& operator|= (Bitboard &bb, Square s) { return bb |= SQUARE_bb[s]; }
    inline Bitboard& operator^= (Bitboard &bb, Square s) { return bb ^= SQUARE_bb[s]; }
    inline Bitboard& operator+= (Bitboard &bb, Square s) { return bb |= SQUARE_bb[s]; }
    inline Bitboard& operator-= (Bitboard &bb, Square s) { return bb &=~SQUARE_bb[s]; }
    /*
    inline Bitboard  operator&  (Bitboard  bb, File   f) { return bb &  FILE_bb[f]; }
    inline Bitboard  operator|  (Bitboard  bb, File   f) { return bb |  FILE_bb[f]; }
    inline Bitboard  operator^  (Bitboard  bb, File   f) { return bb ^  FILE_bb[f]; }
    inline Bitboard  operator+  (Bitboard  bb, File   f) { return bb |  FILE_bb[f]; }
    inline Bitboard  operator-  (Bitboard  bb, File   f) { return bb & ~FILE_bb[f]; }
    inline Bitboard& operator&= (Bitboard &bb, File   f) { return bb &= FILE_bb[f]; }
    inline Bitboard& operator|= (Bitboard &bb, File   f) { return bb |= FILE_bb[f]; }
    inline Bitboard& operator^= (Bitboard &bb, File   f) { return bb ^= FILE_bb[f]; }
    inline Bitboard& operator+= (Bitboard &bb, File   f) { return bb |= FILE_bb[f]; }
    inline Bitboard& operator-= (Bitboard &bb, File   f) { return bb &=~FILE_bb[f]; }

    inline Bitboard  operator&  (Bitboard  bb, Rank   r) { return bb &  RANK_bb[r]; }
    inline Bitboard  operator|  (Bitboard  bb, Rank   r) { return bb |  RANK_bb[r]; }
    inline Bitboard  operator^  (Bitboard  bb, Rank   r) { return bb ^  RANK_bb[r]; }
    inline Bitboard  operator+  (Bitboard  bb, Rank   r) { return bb |  RANK_bb[r]; }
    inline Bitboard  operator-  (Bitboard  bb, Rank   r) { return bb & ~RANK_bb[r]; }
    inline Bitboard& operator&= (Bitboard &bb, Rank   r) { return bb &= RANK_bb[r]; }
    inline Bitboard& operator|= (Bitboard &bb, Rank   r) { return bb |= RANK_bb[r]; }
    inline Bitboard& operator^= (Bitboard &bb, Rank   r) { return bb ^= RANK_bb[r]; }
    inline Bitboard& operator+= (Bitboard &bb, Rank   r) { return bb |= RANK_bb[r]; }
    inline Bitboard& operator-= (Bitboard &bb, Rank   r) { return bb &=~RANK_bb[r]; }
    */

    inline u08 file_dist (Square s1, Square s2) { return F_R_DIST[_file (s1)][_file (s2)]; }

    inline u08 rank_dist (Square s1, Square s2) { return F_R_DIST[_rank (s1)][_rank (s2)]; }

    // ----------------------------------------------------

    inline Bitboard file_bb (Square s) { return FILE_bb[_file (s)]; }

    inline Bitboard rank_bb (Square s) { return RANK_bb[_rank (s)]; }

    inline Bitboard rel_rank_bb (Color c, Rank   r) { return RANK_bb[rel_rank (c, r)]; }
    inline Bitboard rel_rank_bb (Color c, Square s) { return RANK_bb[rel_rank (c, s)]; }

    // board_edges() returns a bitboard of edges of the board
    inline Bitboard board_edges (Square s) { return (((FA_bb | FH_bb) & ~file_bb (s)) | ((R1_bb | R8_bb) & ~rank_bb (s))); }

    // squares_of_color() returns a bitboard of all squares with the same color of the given square.
    inline Bitboard squares_of_color (Square s) { return (DARK_bb & s) ? DARK_bb : LIHT_bb; }

    // Check the squares s1, s2 and s3 are aligned either on a straight/diagonal line.
    inline bool sqrs_aligned  (Square s1, Square s2, Square s3) { return RAY_LINE_bb[s1][s2] & s3; }

    inline bool more_than_one (Bitboard bb)
    {
#   ifndef BM2
        return (bb) & (bb - 1);
#   else
        return _blsr_u64 (bb);
#   endif
    }

    // Shift the bitboard using delta
    template<Delta DEL> inline Bitboard shift_del (Bitboard bb);

    template<> inline Bitboard shift_del<DEL_N > (Bitboard bb) { return (bb) << (+DEL_N); }
    template<> inline Bitboard shift_del<DEL_S > (Bitboard bb) { return (bb) >> (-DEL_S); }
    template<> inline Bitboard shift_del<DEL_NN> (Bitboard bb) { return (bb) << (+DEL_NN); }
    template<> inline Bitboard shift_del<DEL_SS> (Bitboard bb) { return (bb) >> (-DEL_SS); }
    template<> inline Bitboard shift_del<DEL_E > (Bitboard bb) { return (bb & FH_bb_) << (+DEL_E); }
    template<> inline Bitboard shift_del<DEL_W > (Bitboard bb) { return (bb & FA_bb_) >> (-DEL_W); }
    template<> inline Bitboard shift_del<DEL_NE> (Bitboard bb) { return (bb & FH_bb_) << (+DEL_NE); } //(bb << +DEL_NE) & FA_bb_;
    template<> inline Bitboard shift_del<DEL_SE> (Bitboard bb) { return (bb & FH_bb_) >> (-DEL_SE); } //(bb >> -DEL_SE) & FA_bb_;
    template<> inline Bitboard shift_del<DEL_NW> (Bitboard bb) { return (bb & FA_bb_) << (+DEL_NW); } //(bb << +DEL_NW) & FH_bb_;
    template<> inline Bitboard shift_del<DEL_SW> (Bitboard bb) { return (bb & FA_bb_) >> (-DEL_SW); } //(bb >> -DEL_SW) & FH_bb_;

    // Rotate RIGHT (toward LSB)
    inline Bitboard rotate_R (Bitboard bb, i08 k) { return (bb >> k) | (bb << (i08(SQ_NO) - k)); }
    // Rotate LEFT  (toward MSB)
    inline Bitboard rotate_L (Bitboard bb, i08 k) { return (bb << k) | (bb >> (i08(SQ_NO) - k)); }

    inline Bitboard sliding_attacks (const Delta deltas[], Square s, Bitboard occ = U64(0))
    {
        Bitboard slid_attacks = U64(0);
        u08 i = 0;
        Delta del;
        while ((del = deltas[i++]) != DEL_O)
        {
            Square sq = s + del;
            while (_ok (sq) && SQR_DIST[sq][sq - del] == 1)
            {
                slid_attacks += sq;
                if (occ & sq) break;
                sq += del;
            }
        }
        return slid_attacks;
    }

    // --------------------------------
    template<PieceT PT>
    // Attacks of the PieceT with occupancy
    extern Bitboard attacks_bb (Square s, Bitboard occ);

    template<>
    // KNIGHT attacks
    INLINE Bitboard attacks_bb<NIHT> (Square s, Bitboard) { return PIECE_ATTACKS[NIHT][s]; }
    template<>
    // KING attacks
    INLINE Bitboard attacks_bb<KING> (Square s, Bitboard) { return PIECE_ATTACKS[KING][s]; }
    // --------------------------------

    template<PieceT PT>
    // Function 'magic_index(s, occ)' for computing index for sliding attack bitboards.
    // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
    // and returns a bitboard representing all squares attacked by PT (BISHOP or ROOK) on the given square.
    extern INLINE u16 magic_index (Square s, Bitboard occ);

    template<>
    INLINE u16 magic_index<BSHP> (Square s, Bitboard occ)
    {
#   ifndef BM2
#       ifdef BIT64
            return u16(((occ & B_MASK_bb[s]) * B_MAGIC_bb[s]) >> B_SHIFT[s]);
#       else
            u32 lo = (u32(occ >> 0x00) & u32(B_MASK_bb[s] >> 0x00)) * u32(B_MAGIC_bb[s] >> 0x00);
            u32 hi = (u32(occ >> 0x20) & u32(B_MASK_bb[s] >> 0x20)) * u32(B_MAGIC_bb[s] >> 0x20);
            return ((lo ^ hi) >> B_SHIFT[s]);
#       endif
#   else
        // Parallel bits extract (pext)
        return u16(_pext_u64 (occ, B_MASK_bb[s]));
#   endif
    }

    template<>
    INLINE u16 magic_index<ROOK> (Square s, Bitboard occ)
    {
#   ifndef BM2
#       ifdef BIT64
            return u16(((occ & R_MASK_bb[s]) * R_MAGIC_bb[s]) >> R_SHIFT[s]);
#       else
            u32 lo = (u32(occ >> 0x00) & u32(R_MASK_bb[s] >> 0x00)) * u32(R_MAGIC_bb[s] >> 0x00);
            u32 hi = (u32(occ >> 0x20) & u32(R_MASK_bb[s] >> 0x20)) * u32(R_MAGIC_bb[s] >> 0x20);
            return ((lo ^ hi) >> R_SHIFT[s]);
#       endif
#   else
        // Parallel bits extract (pext)
        return u16(_pext_u64 (occ, R_MASK_bb[s]));
#   endif
    }

    template<>
    // Attacks of the BISHOP with occupancy
    INLINE Bitboard attacks_bb<BSHP> (Square s, Bitboard occ) { return B_ATTACK_bb[s][magic_index<BSHP> (s, occ)]; }
    template<>
    // Attacks of the ROOK with occupancy
    INLINE Bitboard attacks_bb<ROOK> (Square s, Bitboard occ) { return R_ATTACK_bb[s][magic_index<ROOK> (s, occ)]; }
    template<>
    // Attacks of the QUEEN with occupancy
    INLINE Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return B_ATTACK_bb[s][magic_index<BSHP> (s, occ)]
             | R_ATTACK_bb[s][magic_index<ROOK> (s, occ)];
    }
    // --------------------------------

    // Piece attacks from square
    INLINE Bitboard attacks_bb (Piece p, Square s, Bitboard occ)
    {
        PieceT pt = ptype (p);
        return (PAWN == pt) ? PAWN_ATTACKS[color (p)][s] :
               (BSHP == pt) ? attacks_bb<BSHP> (s, occ) :
               (ROOK == pt) ? attacks_bb<ROOK> (s, occ) :
               (QUEN == pt) ? attacks_bb<BSHP> (s, occ)
                            | attacks_bb<ROOK> (s, occ) :
               (NIHT == pt || KING == pt) ? PIECE_ATTACKS[pt][s] :
               U64(0);
    }

    extern void initialize ();

#ifndef NDEBUG
    extern std::string pretty (Bitboard bb, char p = 'o');
#endif

}

template<typename T>
inline i32 dist (T t1, T t2) { return t1 < t2 ? t2 - t1 : t1 - t2; }

template<> inline i32 dist<Square> (Square s1, Square s2) { return BitBoard::SQR_DIST[s1][s2]; }

template<typename T1, typename T2>
inline i32 dist (T2 x, T2 y);

template<> inline i32 dist<File> (Square s1, Square s2) { return dist (_file (s1), _file (s2)); }
template<> inline i32 dist<Rank> (Square s1, Square s2) { return dist (_rank (s1), _rank (s2)); }

#endif // _BITBOARD_H_INC_
