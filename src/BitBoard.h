#ifndef _BITBOARD_H_INC_
#define _BITBOARD_H_INC_

#include "Type.h"
#include "BitCount.h"
#include "BitScan.h"

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

    //const Bitboard D18_bb = U64(0x8040201008040201);             // 08 DIAG-18 squares.
    //const Bitboard D81_bb = U64(0x0102040810204080);             // 08 DIAG-81 squares.

    const Bitboard LIHT_bb = U64(0x55AA55AA55AA55AA);            // 32 LIGHT squares.
    const Bitboard DARK_bb = U64(0xAA55AA55AA55AA55);            // 32 DARK  squares.

    const Bitboard CORNER_bb = (FA_bb|FH_bb)&(R1_bb|R8_bb);     // 04 CORNER squares.

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
#undef S_08
#undef S_04
#undef S_02
#define S_02(n)  U64(1)<<(2*(n)),  U64(1)<<(2*(n)+1)
#define S_04(n)      S_02(2*(n)),      S_02(2*(n)+1)
#define S_08(n)      S_04(2*(n)),      S_04(2*(n)+1)
#define S_16(n)      S_08(2*(n)),      S_08(2*(n)+1)
        S_16 (0), S_16 (1), S_16 (2), S_16 (3),
#undef S_16
#undef S_08
#undef S_04
#undef S_02
    };
    // FILES
    const Bitboard FILE_bb[F_NO] =
    {
        FA_bb, FB_bb, FC_bb, FD_bb, FE_bb, FF_bb, FG_bb, FH_bb
    };
    // RANKS
    const Bitboard RANK_bb[R_NO] =
    {
        R1_bb, R2_bb, R3_bb, R4_bb, R5_bb, R6_bb, R7_bb, R8_bb
    };

    // ADJACENT FILES used for isolated-pawn
    const Bitboard ADJ_FILE_bb  [F_NO] =
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
    const Bitboard ADJ_RANK_bb  [R_NO] =
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
    const Bitboard FRONT_RANK_bb    [CLR_NO][R_NO] =
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

    extern Bitboard    FRONT_SQRS_bb[CLR_NO][SQ_NO];

    extern Bitboard       BETWEEN_bb[SQ_NO][SQ_NO];
    extern Bitboard       RAYLINE_bb[SQ_NO][SQ_NO];

    extern Bitboard    DIST_RINGS_bb[SQ_NO][F_NO];

    extern Bitboard PAWN_ATTACK_SPAN[CLR_NO][SQ_NO];
    extern Bitboard   PAWN_PASS_SPAN[CLR_NO][SQ_NO];

    // attacks of the pawns & pieces
    extern Bitboard     PAWN_ATTACKS[CLR_NO][SQ_NO];
    extern Bitboard    PIECE_ATTACKS[NONE][SQ_NO];

    extern Bitboard *B_ATTACK_bb[SQ_NO];
    extern Bitboard *R_ATTACK_bb[SQ_NO];

    extern Bitboard    B_MASK_bb[SQ_NO];
    extern Bitboard    R_MASK_bb[SQ_NO];

#ifndef BM2
    extern Bitboard   B_MAGIC_bb[SQ_NO];
    extern Bitboard   R_MAGIC_bb[SQ_NO];

    extern u08           B_SHIFT[SQ_NO];
    extern u08           R_SHIFT[SQ_NO];
#endif

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

    // ----------------------------------------------------

    inline Bitboard file_bb (Square s) { return FILE_bb[_file (s)]; }

    inline Bitboard rank_bb (Square s) { return RANK_bb[_rank (s)]; }

    inline Bitboard rel_rank_bb (Color c, Rank   r) { return RANK_bb[rel_rank (c, r)]; }
    inline Bitboard rel_rank_bb (Color c, Square s) { return RANK_bb[rel_rank (c, s)]; }

    // board_edges() returns a bitboard of edges of the board for given square
    inline Bitboard board_edges (Square s) { return ((FA_bb|FH_bb) & ~file_bb (s)) | ((R1_bb|R8_bb) & ~rank_bb (s)); }

    // Check the squares s1, s2 and s3 are aligned either on a straight/diagonal line.
    inline bool sqrs_aligned  (Square s1, Square s2, Square s3) { return (RAYLINE_bb[s1][s2] & s3) != U64(0); }

    inline bool more_than_one (Bitboard bb)
    {
#   ifndef BM2
        return (bb & (bb - 1)) != U64(0);
#   else
        return BLSR (bb) != U64(0);
#   endif
    }

    // Shift the bitboard using delta
    template<Delta DEL> inline Bitboard shift_bb (Bitboard bb);

    template<> inline Bitboard shift_bb<DEL_N > (Bitboard bb) { return (bb) << (+DEL_N); }
    template<> inline Bitboard shift_bb<DEL_S > (Bitboard bb) { return (bb) >> (-DEL_S); }
    template<> inline Bitboard shift_bb<DEL_NN> (Bitboard bb) { return (bb) << (+DEL_NN); }
    template<> inline Bitboard shift_bb<DEL_SS> (Bitboard bb) { return (bb) >> (-DEL_SS); }
    template<> inline Bitboard shift_bb<DEL_E > (Bitboard bb) { return (bb & ~FH_bb) << (+DEL_E); }
    template<> inline Bitboard shift_bb<DEL_W > (Bitboard bb) { return (bb & ~FA_bb) >> (-DEL_W); }
    template<> inline Bitboard shift_bb<DEL_NE> (Bitboard bb) { return (bb & ~FH_bb) << (+DEL_NE); } //(bb << +DEL_NE) & ~FA_bb;
    template<> inline Bitboard shift_bb<DEL_SE> (Bitboard bb) { return (bb & ~FH_bb) >> (-DEL_SE); } //(bb >> -DEL_SE) & ~FA_bb;
    template<> inline Bitboard shift_bb<DEL_NW> (Bitboard bb) { return (bb & ~FA_bb) << (+DEL_NW); } //(bb << +DEL_NW) & ~FH_bb;
    template<> inline Bitboard shift_bb<DEL_SW> (Bitboard bb) { return (bb & ~FA_bb) >> (-DEL_SW); } //(bb >> -DEL_SW) & ~FH_bb;

    //// Rotate RIGHT (toward LSB)
    //inline Bitboard rotate_R (Bitboard bb, i08 k) { return (bb >> k) | (bb << (i08(SQ_NO) - k)); }
    //// Rotate LEFT  (toward MSB)
    //inline Bitboard rotate_L (Bitboard bb, i08 k) { return (bb << k) | (bb >> (i08(SQ_NO) - k)); }

    inline Bitboard sliding_attacks (const Delta deltas[], Square s, Bitboard occ = U64(0))
    {
        auto slid_attacks = U64(0);
        u08 i = 0;
        Delta del;
        while ((del = deltas[i++]) != DEL_O)
        {
            auto sq = s + del;
            while (_ok (sq) && dist (sq, sq - del) == 1)
            {
                slid_attacks += sq;
                if ((occ & sq) != U64(0)) break;
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
    inline Bitboard attacks_bb<NIHT> (Square s, Bitboard) { return PIECE_ATTACKS[NIHT][s]; }
    template<>
    // KING attacks
    inline Bitboard attacks_bb<KING> (Square s, Bitboard) { return PIECE_ATTACKS[KING][s]; }
    // --------------------------------

    template<PieceT PT>
    // Function 'magic_index(s, occ)' for computing index for sliding attack bitboards.
    // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
    // and returns a bitboard representing all squares attacked by PT (BISHOP or ROOK) on the given square.
    extern inline u16 magic_index (Square s, Bitboard occ);

    template<>
    inline u16 magic_index<BSHP> (Square s, Bitboard occ)
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
        return u16(PEXT (occ, B_MASK_bb[s]));
#   endif
    }

    template<>
    inline u16 magic_index<ROOK> (Square s, Bitboard occ)
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
        return u16(PEXT (occ, R_MASK_bb[s]));
#   endif
    }

    template<>
    // Attacks of the BISHOP with occupancy
    inline Bitboard attacks_bb<BSHP> (Square s, Bitboard occ) { return B_ATTACK_bb[s][magic_index<BSHP> (s, occ)]; }
    template<>
    // Attacks of the ROOK with occupancy
    inline Bitboard attacks_bb<ROOK> (Square s, Bitboard occ) { return R_ATTACK_bb[s][magic_index<ROOK> (s, occ)]; }
    template<>
    // Attacks of the QUEEN with occupancy
    inline Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return B_ATTACK_bb[s][magic_index<BSHP> (s, occ)]
             | R_ATTACK_bb[s][magic_index<ROOK> (s, occ)];
    }
    // --------------------------------

    // Piece attacks from square
    inline Bitboard attacks_bb (Piece p, Square s, Bitboard occ)
    {
        return PAWN == ptype (p) ? PAWN_ATTACKS[color (p)][s] :
               BSHP == ptype (p) ? attacks_bb<BSHP> (s, occ) :
               ROOK == ptype (p) ? attacks_bb<ROOK> (s, occ) :
               QUEN == ptype (p) ? attacks_bb<BSHP> (s, occ)
                                 | attacks_bb<ROOK> (s, occ) :
               NIHT == ptype (p) || KING == ptype (p) ? PIECE_ATTACKS[ptype (p)][s] : U64(0);
    }

    extern void initialize ();

#ifndef NDEBUG
    extern std::string pretty (Bitboard bb, char p = 'o');

    extern void test_attacks ();
#endif

}

#endif // _BITBOARD_H_INC_
