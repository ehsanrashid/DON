#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _BITBOARD_H_INC_
#define _BITBOARD_H_INC_

#include "Type.h"

#ifdef BM2
#   include <immintrin.h> // Header for _pext_u64() intrinsic
#endif

namespace BitBoard {

    const Bitboard FA_bb = U64 (0x0101010101010101);
    const Bitboard FB_bb = FA_bb << 1;//U64 (0x0202020202020202);
    const Bitboard FC_bb = FA_bb << 2;//U64 (0x0404040404040404);
    const Bitboard FD_bb = FA_bb << 3;//U64 (0x0808080808080808);
    const Bitboard FE_bb = FA_bb << 4;//U64 (0x1010101010101010);
    const Bitboard FF_bb = FA_bb << 5;//U64 (0x2020202020202020);
    const Bitboard FG_bb = FA_bb << 6;//U64 (0x4040404040404040);
    const Bitboard FH_bb = FA_bb << 7;//U64 (0x8080808080808080);

    const Bitboard R1_bb = U64 (0x00000000000000FF);
    const Bitboard R2_bb = R1_bb << (8 * 1);//U64 (0x000000000000FF00);
    const Bitboard R3_bb = R1_bb << (8 * 2);//U64 (0x0000000000FF0000);
    const Bitboard R4_bb = R1_bb << (8 * 3);//U64 (0x00000000FF000000);
    const Bitboard R5_bb = R1_bb << (8 * 4);//U64 (0x000000FF00000000);
    const Bitboard R6_bb = R1_bb << (8 * 5);//U64 (0x0000FF0000000000);
    const Bitboard R7_bb = R1_bb << (8 * 6);//U64 (0x00FF000000000000);
    const Bitboard R8_bb = R1_bb << (8 * 7);//U64 (0xFF00000000000000);

    const Bitboard R1_bb_ = ~R1_bb;//U64 (0xFFFFFFFFFFFFFF00);    // 56 Not RANK-1
    const Bitboard R8_bb_ = ~R8_bb;//U64 (0x00FFFFFFFFFFFFFF);    // 56 Not RANK-8
    const Bitboard FA_bb_ = ~FA_bb;//U64 (0xFEFEFEFEFEFEFEFE);    // 56 Not FILE-A
    const Bitboard FH_bb_ = ~FH_bb;//U64 (0x7F7F7F7F7F7F7F7F);    // 56 Not FILE-H

    const Bitboard D18_bb = U64 (0x8040201008040201);             // 08 DIAG-18 squares.
    const Bitboard D81_bb = U64 (0x0102040810204080);             // 08 DIAG-81 squares.

    const Bitboard LIHT_bb = U64 (0x55AA55AA55AA55AA);            // 32 LIGHT squares.
    const Bitboard DARK_bb = U64 (0xAA55AA55AA55AA55);            // 32 DARK  squares.

    const Bitboard CRNR_bb = U64 (0x8100000000000081);            // 04 CORNER squares.
    const Bitboard RIMEDGE_bb = (R1_bb | R8_bb | FA_bb | FH_bb);
    const Bitboard MIDEDGE_bb = (FA_bb | FH_bb)&(R2_bb | R3_bb);

    const Delta PawnDeltas[CLR_NO][3] =
    {
        { DEL_NW, DEL_NE, DEL_O },
        { DEL_SE, DEL_SW, DEL_O },
    };
    const Delta PieceDeltas[NONE][9] =
    {
        { DEL_O },
        { DEL_SSW, DEL_SSE, DEL_WWS, DEL_EES, DEL_WWN, DEL_EEN, DEL_NNW, DEL_NNE, DEL_O },
        { DEL_SW, DEL_SE, DEL_NW, DEL_NE, DEL_O },
        { DEL_S, DEL_W, DEL_E, DEL_N, DEL_O },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE, DEL_O },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE, DEL_O },
    };

    // SQUARES
    CACHE_ALIGN(64) const Bitboard Square_bb[SQ_NO] =
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
    CACHE_ALIGN(64) const Bitboard   File_bb[F_NO] =
    {
        FA_bb, FB_bb, FC_bb, FD_bb, FE_bb, FF_bb, FG_bb, FH_bb
    };
    // RANKS
    CACHE_ALIGN(64) const Bitboard   Rank_bb[R_NO] =
    {
        R1_bb, R2_bb, R3_bb, R4_bb, R5_bb, R6_bb, R7_bb, R8_bb
    };

    // ADJACENT FILES used for isolated-pawn
    CACHE_ALIGN(64) const Bitboard AdjFile_bb[F_NO] =
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
    CACHE_ALIGN(64) const Bitboard AdjRank_bb[R_NO] =
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
    CACHE_ALIGN(64) const Bitboard FrontRank_bb[CLR_NO][R_NO] =
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

    CACHE_ALIGN(64) extern Bitboard FrontSqs_bb[CLR_NO][SQ_NO];

    CACHE_ALIGN(64) extern Bitboard Between_bb[SQ_NO][SQ_NO];
    CACHE_ALIGN(64) extern Bitboard LineRay_bb[SQ_NO][SQ_NO];

    CACHE_ALIGN(64) extern Bitboard DistanceRings[SQ_NO][F_NO];

    CACHE_ALIGN(64) extern Bitboard PawnAttackSpan[CLR_NO][SQ_NO];
    CACHE_ALIGN(64) extern Bitboard PasserPawnSpan[CLR_NO][SQ_NO];

    // attacks of the pawns & pieces
    CACHE_ALIGN(64) extern Bitboard PawnAttacks[CLR_NO][SQ_NO];
    CACHE_ALIGN(64) extern Bitboard PieceAttacks[NONE][SQ_NO];

    CACHE_ALIGN(64) extern Bitboard *BAttack_bb[SQ_NO];
    CACHE_ALIGN(64) extern Bitboard *RAttack_bb[SQ_NO];

    CACHE_ALIGN(64) extern Bitboard    BMask_bb[SQ_NO];
    CACHE_ALIGN(64) extern Bitboard    RMask_bb[SQ_NO];

#ifndef BM2
    CACHE_ALIGN(64) extern Bitboard   BMagic_bb[SQ_NO];
    CACHE_ALIGN(64) extern Bitboard   RMagic_bb[SQ_NO];

    CACHE_ALIGN(64) extern u08        BShift   [SQ_NO];
    CACHE_ALIGN(64) extern u08        RShift   [SQ_NO];
#endif

    extern u08 FileRankDist[F_NO][R_NO];
    extern u08   SquareDist[SQ_NO][SQ_NO];

    inline Bitboard  operator&  (Bitboard  bb, Square s) { return bb &  Square_bb[s]; }
    inline Bitboard  operator|  (Bitboard  bb, Square s) { return bb |  Square_bb[s]; }
    inline Bitboard  operator^  (Bitboard  bb, Square s) { return bb ^  Square_bb[s]; }
    inline Bitboard  operator+  (Bitboard  bb, Square s) { return bb |  Square_bb[s]; }
    inline Bitboard  operator-  (Bitboard  bb, Square s) { return bb &~ Square_bb[s]; }
    inline Bitboard& operator&= (Bitboard &bb, Square s) { return bb &= Square_bb[s]; }
    inline Bitboard& operator|= (Bitboard &bb, Square s) { return bb |= Square_bb[s]; }
    inline Bitboard& operator^= (Bitboard &bb, Square s) { return bb ^= Square_bb[s]; }
    inline Bitboard& operator+= (Bitboard &bb, Square s) { return bb |= Square_bb[s]; }
    inline Bitboard& operator-= (Bitboard &bb, Square s) { return bb &=~Square_bb[s]; }
    /*
    inline Bitboard  operator&  (Bitboard  bb, File   f) { return bb &  File_bb[f]; }
    inline Bitboard  operator|  (Bitboard  bb, File   f) { return bb |  File_bb[f]; }
    inline Bitboard  operator^  (Bitboard  bb, File   f) { return bb ^  File_bb[f]; }
    inline Bitboard  operator+  (Bitboard  bb, File   f) { return bb |  File_bb[f]; }
    inline Bitboard  operator-  (Bitboard  bb, File   f) { return bb & ~File_bb[f]; }
    inline Bitboard& operator&= (Bitboard &bb, File   f) { return bb &= File_bb[f]; }
    inline Bitboard& operator|= (Bitboard &bb, File   f) { return bb |= File_bb[f]; }
    inline Bitboard& operator^= (Bitboard &bb, File   f) { return bb ^= File_bb[f]; }
    inline Bitboard& operator+= (Bitboard &bb, File   f) { return bb |= File_bb[f]; }
    inline Bitboard& operator-= (Bitboard &bb, File   f) { return bb &=~File_bb[f]; }

    inline Bitboard  operator&  (Bitboard  bb, Rank   r) { return bb &  Rank_bb[r]; }
    inline Bitboard  operator|  (Bitboard  bb, Rank   r) { return bb |  Rank_bb[r]; }
    inline Bitboard  operator^  (Bitboard  bb, Rank   r) { return bb ^  Rank_bb[r]; }
    inline Bitboard  operator+  (Bitboard  bb, Rank   r) { return bb |  Rank_bb[r]; }
    inline Bitboard  operator-  (Bitboard  bb, Rank   r) { return bb & ~Rank_bb[r]; }
    inline Bitboard& operator&= (Bitboard &bb, Rank   r) { return bb &= Rank_bb[r]; }
    inline Bitboard& operator|= (Bitboard &bb, Rank   r) { return bb |= Rank_bb[r]; }
    inline Bitboard& operator^= (Bitboard &bb, Rank   r) { return bb ^= Rank_bb[r]; }
    inline Bitboard& operator+= (Bitboard &bb, Rank   r) { return bb |= Rank_bb[r]; }
    inline Bitboard& operator-= (Bitboard &bb, Rank   r) { return bb &=~Rank_bb[r]; }
    */
    //inline u08 file_dist (File f1, File f2)     { return FileRankDist[f1][f2]; }
    inline u08 file_dist (Square s1, Square s2) { return FileRankDist[_file (s1)][_file (s2)]; }

    //inline u08 rank_dist (Rank r1, Rank r2)     { return FileRankDist[r1][r2]; }
    inline u08 rank_dist (Square s1, Square s2) { return FileRankDist[_rank (s1)][_rank (s2)]; }

    // ----------------------------------------------------

    inline Bitboard file_bb (Square s) { return File_bb[_file (s)]; }

    inline Bitboard rank_bb (Square s) { return Rank_bb[_rank (s)]; }

    //inline Bitboard adj_files_bb (Square s) { return AdjFile_bb[_file (s)]; }
    //inline Bitboard adj_ranks_bb (Square s) { return AdjRank_bb[_rank (s)]; }

    inline Bitboard rel_rank_bb (Color c, Rank   r) { return Rank_bb[rel_rank (c, r)]; }
    inline Bitboard rel_rank_bb (Color c, Square s) { return Rank_bb[rel_rank (c, s)]; }

    // Bitboard of ranks in front of the rank, from the point of view of the given color.
    //inline Bitboard front_ranks_bb   (Color c, Rank   r) { return FrontRank_bb[c][r]; }
    // Bitboard of squares along the line in front of the square, from the point of view of the given color.
    //inline Bitboard front_sqs_bb (Color c, Square s) { return FrontSqs_bb[c][s]; }

    // Ring on the square with the distance 'd'
    //inline Bitboard distance_rings   (Square s, u08 d) { return DistanceRings[s][d]; }

    // Edges of the board
    inline Bitboard board_edges (Square s) { return (((FA_bb | FH_bb) & ~file_bb (s)) | ((R1_bb | R8_bb) & ~rank_bb (s))); }

    // squares_of_color() returns a bitboard of all squares with the same color of the given square.
    inline Bitboard squares_of_color (Square s) { return (DARK_bb & s) ? DARK_bb : LIHT_bb; }

    // pawn_attack_span() takes a color and a square as input, and returns a bitboard
    // representing all squares that can be attacked by a pawn of the given color
    // when it moves along its file starting from the given square. Definition is:
    // PawnAttackSpan[c][s] = front_ranks_bb(c, s) & adjacent_files_bb(s);
    //inline Bitboard pawn_attack_span (Color c, Square s) { return PawnAttackSpan[c][s]; }

    // passer_pawn_span() takes a color and a square as input, and returns a
    // bitboard mask which can be used to test if a pawn of the given color on
    // the given square is a passed pawn. Definition of the table is:
    // PassedPawnMask[c][s] = PawnAttackSpan[c][s] | forward_bb(c, s)
    //inline Bitboard passer_pawn_span (Color c, Square s) { return PasserPawnSpan[c][s]; }

    // between_bb() returns a bitboard representing all squares between two squares.
    // For instance,
    // between_bb(SQ_C4, SQ_F7) returns a bitboard with the bits for square d5 and e6 set.
    // If s1 and s2 are not on the same rank, file or diagonal, 0 is returned.
    //inline Bitboard between_sq (Square s1, Square s2) { return Between_bb[s1][s2]; }

    // Check the squares s1, s2 and s3 are aligned either on a straight/diagonal line.
    inline bool sqrs_aligned  (Square s1, Square s2, Square s3) { return LineRay_bb[s1][s2] & s3; }

    inline bool more_than_one (Bitboard bb) { return ((bb) & (bb - 1)) != U64 (0); }

    // Shift the Bitboard using delta
    template<Delta DEL> inline Bitboard shift_del (Bitboard bb);

    template<> inline Bitboard shift_del<DEL_N > (Bitboard bb) { return (bb) << (+DEL_N); }
    template<> inline Bitboard shift_del<DEL_S > (Bitboard bb) { return (bb) >> (-DEL_S); }
    template<> inline Bitboard shift_del<DEL_NN> (Bitboard bb) { return (bb) << (+DEL_NN); }
    template<> inline Bitboard shift_del<DEL_SS> (Bitboard bb) { return (bb) >> (-DEL_SS); }
    //template<> inline Bitboard shift_del<DEL_E > (Bitboard bb) { return (bb & FH_bb_) << (+DEL_E); }
    //template<> inline Bitboard shift_del<DEL_W > (Bitboard bb) { return (bb & FA_bb_) >> (-DEL_W); }
    template<> inline Bitboard shift_del<DEL_NE> (Bitboard bb) { return (bb & FH_bb_) << (+DEL_NE); } //(bb << +DEL_NE) & FA_bb_;
    template<> inline Bitboard shift_del<DEL_SE> (Bitboard bb) { return (bb & FH_bb_) >> (-DEL_SE); } //(bb >> -DEL_SE) & FA_bb_;
    template<> inline Bitboard shift_del<DEL_NW> (Bitboard bb) { return (bb & FA_bb_) << (+DEL_NW); } //(bb << +DEL_NW) & FH_bb_;
    template<> inline Bitboard shift_del<DEL_SW> (Bitboard bb) { return (bb & FA_bb_) >> (-DEL_SW); } //(bb >> -DEL_SW) & FH_bb_;

    // Rotate RIGHT (toward LSB)
    inline Bitboard rotate_R (Bitboard bb, i08 k) { return (bb >> k) | (bb << (i08 (SQ_NO) - k)); }
    // Rotate LEFT  (toward MSB)
    inline Bitboard rotate_L (Bitboard bb, i08 k) { return (bb << k) | (bb >> (i08 (SQ_NO) - k)); }

    inline Bitboard sliding_attacks (const Delta deltas[], Square s, Bitboard occ = U64 (0))
    {
        Bitboard slid_attacks = U64 (0);
        u08 i = 0;
        Delta del;
        while ((del = deltas[i++]) != DEL_O)
        {
            Square sq = s + del;
            while (_ok (sq) && SquareDist[sq][sq - del] == 1)
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
    INLINE Bitboard attacks_bb<NIHT> (Square s, Bitboard) { return PieceAttacks[NIHT][s]; }
    template<>
    // KING attacks
    INLINE Bitboard attacks_bb<KING> (Square s, Bitboard) { return PieceAttacks[KING][s]; }
    // --------------------------------

    template<PieceT PT>
    // Function 'magic_index(s, occ)' for computing index for sliding attack bitboards.
    // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
    // and returns a bitboard representing all squares attacked by PT (BISHOP or ROOK) on the given square.
    extern INLINE u16 magic_index(Square s, Bitboard occ);

    template<>
    INLINE u16 magic_index<BSHP> (Square s, Bitboard occ)
    {
#ifdef BM2
        return u16 (_pext_u64 (occ, BMask_bb[s]));
#else
#   ifdef _64BIT
        return u16 (((occ & BMask_bb[s]) * BMagic_bb[s]) >> BShift[s]);
#   else
        u32 lo = (u32 (occ >> 0x00) & u32 (BMask_bb[s] >> 0x00)) * u32 (BMagic_bb[s] >> 0x00);
        u32 hi = (u32 (occ >> 0x20) & u32 (BMask_bb[s] >> 0x20)) * u32 (BMagic_bb[s] >> 0x20);
        return ((lo ^ hi) >> BShift[s]);
#   endif
#endif
    }

    template<>
    INLINE u16 magic_index<ROOK> (Square s, Bitboard occ)
    {
#ifdef BM2
        return u16 (_pext_u64 (occ, RMask_bb[s]));
#else
#   ifdef _64BIT
        return u16 (((occ & RMask_bb[s]) * RMagic_bb[s]) >> RShift[s]);
#   else
        u32 lo = (u32 (occ >> 0x00) & u32 (RMask_bb[s] >> 0x00)) * u32 (RMagic_bb[s] >> 0x00);
        u32 hi = (u32 (occ >> 0x20) & u32 (RMask_bb[s] >> 0x20)) * u32 (RMagic_bb[s] >> 0x20);
        return ((lo ^ hi) >> RShift[s]);
#   endif
#endif
    }

    template<>
    // Attacks of the BISHOP with occupancy
    INLINE Bitboard attacks_bb<BSHP> (Square s, Bitboard occ) { return BAttack_bb[s][magic_index<BSHP> (s, occ)]; }
    template<>
    // Attacks of the ROOK with occupancy
    INLINE Bitboard attacks_bb<ROOK> (Square s, Bitboard occ) { return RAttack_bb[s][magic_index<ROOK> (s, occ)]; }
    template<>
    // QUEEN Attacks with occ
    INLINE Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return BAttack_bb[s][magic_index<BSHP> (s, occ)]
            |  RAttack_bb[s][magic_index<ROOK> (s, occ)];
    }
    // --------------------------------

    // Piece attacks from square
    INLINE Bitboard attacks_bb (Piece p, Square s, Bitboard occ)
    {
        PieceT pt = ptype (p);
        return (PAWN == pt) ? PawnAttacks[color (p)][s]
             : (BSHP == pt) ? attacks_bb<BSHP> (s, occ)
             : (ROOK == pt) ? attacks_bb<ROOK> (s, occ)
             : (QUEN == pt) ? attacks_bb<BSHP> (s, occ) | attacks_bb<ROOK> (s, occ)
             : (NIHT == pt || KING == pt) ? PieceAttacks[pt][s]
             : U64 (0);
    }

    extern void initialize ();

#ifndef NDEBUG
    extern const std::string pretty (Bitboard bb, char p = 'o');
#endif

}

#endif // _BITBOARD_H_INC_
