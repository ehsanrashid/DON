#pragma once

#include <array>

#include "Type.h"

namespace BitBoard {

    ///// make_bitboard() returns a bitboard compile-time constructed from a list of squares, files, ranks
    //constexpr Bitboard make_bitboard() { return 0; }

    //template<typename ...Squares>
    //constexpr Bitboard make_bitboard(Square s, Squares... squares)
    //{
    //    return U64(0x0000000000000001) << s | make_bitboard(squares...);
    //}
    //template<typename ...Files>
    //constexpr Bitboard make_bitboard(File f, Files... files)
    //{
    //    return U64(0x0101010101010101) << f | make_bitboard(files...);
    //}
    //template<typename ...Ranks>
    //constexpr Bitboard make_bitboard(Rank r, Ranks... ranks)
    //{
    //    return U64(0x00000000000000FF) << (r * 8) | make_bitboard(ranks...);
    //}

    constexpr Bitboard All_bb = U64(0xFFFFFFFFFFFFFFFF);

    constexpr Bitboard FA_bb = U64(0x0101010101010101);
    constexpr Bitboard FB_bb = FA_bb << 1;
    constexpr Bitboard FC_bb = FA_bb << 2;
    constexpr Bitboard FD_bb = FA_bb << 3;
    constexpr Bitboard FE_bb = FA_bb << 4;
    constexpr Bitboard FF_bb = FA_bb << 5;
    constexpr Bitboard FG_bb = FA_bb << 6;
    constexpr Bitboard FH_bb = FA_bb << 7;

    constexpr Bitboard R1_bb = U64(0x00000000000000FF);
    constexpr Bitboard R2_bb = R1_bb << (8 * 1);
    constexpr Bitboard R3_bb = R1_bb << (8 * 2);
    constexpr Bitboard R4_bb = R1_bb << (8 * 3);
    constexpr Bitboard R5_bb = R1_bb << (8 * 4);
    constexpr Bitboard R6_bb = R1_bb << (8 * 5);
    constexpr Bitboard R7_bb = R1_bb << (8 * 6);
    constexpr Bitboard R8_bb = R1_bb << (8 * 7);

    //constexpr Bitboard Diagonals_bb = U64(0x8142241818244281); // A1..H8 | H1..A8
    constexpr Bitboard Center_bb = (FD_bb|FE_bb) & (R4_bb|R5_bb);

    constexpr std::array<Bitboard, CLR_NO> Color_bb
    {
        U64(0x55AA55AA55AA55AA),
        U64(0xAA55AA55AA55AA55)
    };
    constexpr std::array<Bitboard, 3> Side_bb
    {
        FE_bb|FF_bb|FG_bb|FH_bb,
        FA_bb|FB_bb|FC_bb|FD_bb,
        FC_bb|FD_bb|FE_bb|FF_bb
    };
    constexpr std::array<Bitboard, F_NO> KingFlank_bb
    {
        Side_bb[CS_QUEN] ^ FD_bb,
        Side_bb[CS_QUEN],
        Side_bb[CS_QUEN],
        Side_bb[CS_NO],
        Side_bb[CS_NO],
        Side_bb[CS_KING],
        Side_bb[CS_KING],
        Side_bb[CS_KING] ^ FE_bb
    };
    constexpr std::array<Bitboard, CLR_NO> Outposts_bb
    {
        R4_bb|R5_bb|R6_bb,
        R5_bb|R4_bb|R3_bb
    };
    constexpr std::array<Bitboard, CLR_NO> Camp_bb
    {
        R1_bb|R2_bb|R3_bb|R4_bb|R5_bb,
        R8_bb|R7_bb|R6_bb|R5_bb|R4_bb
    };
    constexpr std::array<Bitboard, CLR_NO> LowRanks_bb
    {
        R2_bb|R3_bb,
        R7_bb|R6_bb
    };
    constexpr std::array<Bitboard, CLR_NO> Region_bb
    {
        R2_bb|R3_bb|R4_bb,
        R7_bb|R6_bb|R5_bb
    };

#   define S_02(n)  U64(1)<<(2*(n)),  U64(1)<<(2*(n)+1)
#   define S_04(n)      S_02(2*(n)),      S_02(2*(n)+1)
#   define S_08(n)      S_04(2*(n)),      S_04(2*(n)+1)
#   define S_16(n)      S_08(2*(n)),      S_08(2*(n)+1)
    constexpr std::array<Bitboard, SQ_NO> Square_bb
    {
        S_16(0), S_16(1), S_16(2), S_16(3),
    };
#   undef S_16
#   undef S_08
#   undef S_04
#   undef S_02

    extern std::array<std::array<Bitboard, SQ_NO>, CLR_NO> PawnAttacks;
    extern std::array<std::array<Bitboard, SQ_NO>, NONE> PieceAttacks;

    extern std::array<std::array<Bitboard, SQ_NO>, SQ_NO> Line_bb;

    // Magic holds all magic relevant data for a single square
    struct Magic
    {
        Bitboard  mask;

#   if !defined(BM2)
        Bitboard  number;
        u08       shift;
#   endif

        Bitboard *attacks;

        u16 index(Bitboard occ) const
        {
            return
#       if defined(BM2)
            u16(PEXT(occ, mask));
#       elif defined(BIT64)
            u16(((occ & mask) * number) >> shift);
#       else
            u16((  u32((u32(occ >> 0x00) & u32(mask >> 0x00)) * u32(number >> 0x00))
                 ^ u32((u32(occ >> 0x20) & u32(mask >> 0x20)) * u32(number >> 0x20))) >> shift);
#       endif
        }

        Bitboard attacks_bb(Bitboard occ) const
        {
            return attacks[index(occ)];
        }
    };

    extern std::array<Magic, SQ_NO> BMagics
        ,                           RMagics;

#if !defined(ABM)
    extern std::array<u08, 1 << 16> PopCount16;
#endif

    /// Shift the bitboard using delta
    template<Delta DEL>
    constexpr Bitboard shift(Bitboard bb) { return 0; }

    template<> constexpr Bitboard shift<DEL_N >(Bitboard bb) { return (bb         ) <<  8; }
    template<> constexpr Bitboard shift<DEL_S >(Bitboard bb) { return (bb         ) >>  8; }
    template<> constexpr Bitboard shift<DEL_NN>(Bitboard bb) { return (bb         ) << 16; }
    template<> constexpr Bitboard shift<DEL_SS>(Bitboard bb) { return (bb         ) >> 16; }
    // If (shifting & 7) != 0 then  bound clipping is done (~FA_bb or ~FH_bb)
    template<> constexpr Bitboard shift<DEL_E >(Bitboard bb) { return (bb & ~FH_bb) <<  1; }
    template<> constexpr Bitboard shift<DEL_W >(Bitboard bb) { return (bb & ~FA_bb) >>  1; }
    template<> constexpr Bitboard shift<DEL_NE>(Bitboard bb) { return (bb & ~FH_bb) <<  9; }
    template<> constexpr Bitboard shift<DEL_SE>(Bitboard bb) { return (bb & ~FH_bb) >>  7; }
    template<> constexpr Bitboard shift<DEL_NW>(Bitboard bb) { return (bb & ~FA_bb) <<  7; }
    template<> constexpr Bitboard shift<DEL_SW>(Bitboard bb) { return (bb & ~FA_bb) >>  9; }

    ///// Rotate Right (toward LSB)
    //constexpr Bitboard rotate_R(Bitboard bb, i08 k) { return (bb >> k) | (bb << (SQ_NO - k)); }
    ///// Rotate Left  (toward MSB)
    //constexpr Bitboard rotate_L(Bitboard bb, i08 k) { return (bb << k) | (bb >> (SQ_NO - k)); }

    constexpr Bitboard square_bb(Square s) { return Square_bb[s]; }

    constexpr bool contains(Bitboard bb, Square s) { return 0 != (bb & square_bb(s)); }

    constexpr Bitboard operator|(Bitboard  bb, Square s) { return bb | square_bb(s); }
    constexpr Bitboard operator^(Bitboard  bb, Square s) { return bb ^ square_bb(s); }

    constexpr Bitboard operator|(Square s, Bitboard bb) { return bb | s; }
    constexpr Bitboard operator^(Square s, Bitboard bb) { return bb ^ s; }

    inline Bitboard& operator|=(Bitboard &bb, Square s) { bb = bb | s; return bb; }
    inline Bitboard& operator^=(Bitboard &bb, Square s) { bb = bb ^ s; return bb; }

    constexpr Bitboard operator|(Square s1, Square s2) { return square_bb(s1) | square_bb(s2); }

    constexpr Bitboard file_bb(File f)   { return FA_bb << f; }
    constexpr Bitboard file_bb(Square s) { return file_bb(_file(s)); }

    constexpr Bitboard rank_bb(Rank r)   { return R1_bb << (8 * r); }
    constexpr Bitboard rank_bb(Square s) { return rank_bb(_rank(s)); }

    // front_rank_bb() returns ranks in front of the given rank
    constexpr Bitboard front_rank_bb(Color c, Rank r)
    {
        return WHITE == c ?
                ~R1_bb << (8 * (r - R_1)) :
                ~R8_bb >> (8 * (R_8 - r));
    }
    // front_rank_bb() returns ranks in front of the given square
    constexpr Bitboard front_rank_bb(Color c, Square s) { return front_rank_bb(c, _rank(s)); }

    constexpr Bitboard adj_file_bb(Square s)
    {
        return shift<DEL_E>(file_bb(s))
             | shift<DEL_W>(file_bb(s));
    }
    //constexpr Bitboard adj_rank_bb(Square s)
    //{
    //    return shift<DEL_N>(rank_bb(s))
    //         | shift<DEL_S>(rank_bb(s));
    //}

    constexpr Bitboard front_squares_bb(Color c, Square s) { return front_rank_bb(c, s) & file_bb(s); }

    constexpr Bitboard pawn_attack_span(Color c, Square s) { return front_rank_bb(c, s) & adj_file_bb(s); }
    constexpr Bitboard   pawn_pass_span(Color c, Square s) { return front_squares_bb(c, s) | pawn_attack_span(c, s); }

    /// dist() functions return the distance between s1 and s2, defined as the
    /// number of steps for a king in s1 to reach s2.

    template<typename T = Square> inline i32 dist(Square, Square);
    template<> inline i32 dist<  File>(Square s1, Square s2) { return std::abs(_file(s1) - _file(s2)); }
    template<> inline i32 dist<  Rank>(Square s1, Square s2) { return std::abs(_rank(s1) - _rank(s2)); }
    template<> inline i32 dist<Square>(Square s1, Square s2) { return std::max(dist<File>(s1, s2), dist<Rank>(s1, s2)); }

    inline Bitboard    line_bb(Square s1, Square s2) { return Line_bb[s1][s2]; }
    inline Bitboard between_bb(Square s1, Square s2)
    {
        return line_bb(s1, s2)
             & (  (All_bb << (s1 + (s1 < s2)))
                ^ (All_bb << (s2 + (s2 < s1))));
    }
    /// Check the squares s1, s2 and s3 are aligned on a straight line.
    inline bool squares_aligned(Square s1, Square s2, Square s3) { return contains(line_bb(s1, s2), s3); }

    constexpr bool more_than_one(Bitboard bb)
    {
        return
//#   if defined(BM2)
//      0 != BLSR(bb);
//#   else
        0 != (bb & (bb - 1));
//#   endif
    }

    constexpr bool opposite_colors(Square s1, Square s2)
    {
        //i08 s = i08(s1) ^ i08(s2);
        //return 0 != (((s >> 3) ^ s) & 1);
        return contains(Color_bb[WHITE], s1) == contains(Color_bb[BLACK], s2);
    }

    constexpr Bitboard pawn_sgl_pushes_bb(Color c, Bitboard bb)
    {
        return WHITE == c ?
                shift<DEL_N>(bb) :
                shift<DEL_S>(bb);
    }
    constexpr Bitboard pawn_dbl_pushes_bb(Color c, Bitboard bb)
    {
        return WHITE == c ?
                shift<DEL_NN>(bb) :
                shift<DEL_SS>(bb);
    }
    constexpr Bitboard pawn_l_attacks_bb(Color c, Bitboard bb)
    {
        return WHITE == c ?
                shift<DEL_NW>(bb) :
                shift<DEL_SE>(bb);
    }
    constexpr Bitboard pawn_r_attacks_bb(Color c, Bitboard bb)
    {
        return WHITE == c ?
                shift<DEL_NE>(bb) :
                shift<DEL_SW>(bb);
    }

    /// pawn_sgl_attacks_bb() returns the single attackes by pawns of the given color
    constexpr Bitboard pawn_sgl_attacks_bb(Color c, Bitboard bb)
    {
        return pawn_l_attacks_bb(c, bb) | pawn_r_attacks_bb(c, bb);
    }
    /// pawn_dbl_attacks_bb() returns the double attackes by pawns of the given color
    constexpr Bitboard pawn_dbl_attacks_bb(Color c, Bitboard bb)
    {
        return pawn_l_attacks_bb(c, bb) & pawn_r_attacks_bb(c, bb);
    }

    /// attacks_bb(s, occ) takes a square and a bitboard of occupied squares,
    /// and returns a bitboard representing all squares attacked by PT (Bishop or Rook or Queen) on the given square.
    template<PieceType PT> Bitboard attacks_bb(Square, Bitboard);

    template<> inline Bitboard attacks_bb<NIHT>(Square s, Bitboard    ) { return PieceAttacks[NIHT][s]; }
    template<> inline Bitboard attacks_bb<KING>(Square s, Bitboard    ) { return PieceAttacks[KING][s]; }
    /// Attacks of the Bishop with occupancy
    template<> inline Bitboard attacks_bb<BSHP>(Square s, Bitboard occ) { return BMagics[s].attacks_bb(occ); }
    /// Attacks of the Rook with occupancy
    template<> inline Bitboard attacks_bb<ROOK>(Square s, Bitboard occ) { return RMagics[s].attacks_bb(occ); }
    /// Attacks of the Queen with occupancy
    template<> inline Bitboard attacks_bb<QUEN>(Square s, Bitboard occ) { return BMagics[s].attacks_bb(occ)
                                                                               | RMagics[s].attacks_bb(occ); }

    /// Position::attacks_from() finds attacks of the piecetype from the square on occupancy.
    inline Bitboard attacks_of_from(PieceType pt, Square s, Bitboard occ)
    {
        switch (pt)
        {
        case NIHT: return PieceAttacks[NIHT][s];
        case BSHP: return attacks_bb<BSHP>(s, occ);
        case ROOK: return attacks_bb<ROOK>(s, occ);
        case QUEN: return attacks_bb<QUEN>(s, occ);
        case KING: return PieceAttacks[KING][s];
        default: assert(false); return 0;
        }
    }
    /// Position::attacks_from() finds attacks from the square on occupancy.
    inline Bitboard attacks_of_from(Piece pc, Square s, Bitboard occ)
    {
        switch (ptype(pc))
        {
        case PAWN: return PawnAttacks[color(pc)][s];
        case NIHT: return PieceAttacks[NIHT][s];
        case BSHP: return attacks_bb<BSHP>(s, occ);
        case ROOK: return attacks_bb<ROOK>(s, occ);
        case QUEN: return attacks_bb<QUEN>(s, occ);
        case KING: return PieceAttacks[KING][s];
        default: assert(false); return 0;
        }
    }

#if !defined(ABM) // PopCount Table

    inline i32 pop_count(Bitboard bb)
    {
        //Bitboard x = bb;
        //x -= (x >> 1) & 0x5555555555555555;
        //x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
        //x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0F;
        //return (x * 0x0101010101010101) >> 56;

        union
        {
            Bitboard b;
            u16      u_16[4];
        } v = { bb };
        return PopCount16[v.u_16[0]]
             + PopCount16[v.u_16[1]]
             + PopCount16[v.u_16[2]]
             + PopCount16[v.u_16[3]];
    }

#else

#   if defined(_MSC_VER) || defined(__INTEL_COMPILER) // MSVC or Intel compiler
//#       include <intrin.h> // Microsoft header for pop count instrinsics __popcnt64() & __popcnt()
#       include <nmmintrin.h> // Microsoft or Intel header for pop count intrinsics _mm_popcnt_u64() & _mm_popcnt_u32()
    inline i32 pop_count(Bitboard bb)
    {
        return
#   if defined(BIT64)
        //i32(__popcnt64(bb));
        i32(_mm_popcnt_u64(bb));
#   else
        //i32(__popcnt(u32(bb >> 0x00))
        //  + __popcnt(u32(bb >> 0x20)));
        i32(_mm_popcnt_u32(bb >> 0x00)
          + _mm_popcnt_u32(bb >> 0x20));
#   endif
    }

#   else // GCC, Clang, ICC or compatible compiler

    inline i32 pop_count(Bitboard bb)
    {
        return
#   if defined(BIT64)
        i32(__builtin_popcountll(bb));
#   else
        i32(__builtin_popcountl(bb >> 0x00)
          + __builtin_popcountl(bb >> 0x20));
#   endif
    }

#   endif

#endif

#if defined(_MSC_VER) // MSVC compiler

#   include <intrin.h> // Microsoft header for instrinsics _BitScanForward64() & _BitScanReverse64()

    inline Square scan_lsq(Bitboard bb)
    {
        assert(0 != bb);

        unsigned long index;
#   if defined(BIT64)
        _BitScanForward64(&index, bb);
#   else
        if (0 != u32(bb >> 0))
        {
            _BitScanForward(&index, u32(bb >> 0x00));
        }
        else
        {
            _BitScanForward(&index, u32(bb >> 0x20));
            index += 0x20;
        }
#   endif
        return Square(index);
    }

    inline Square scan_msq(Bitboard bb)
    {
        assert(0 != bb);

        unsigned long index;
#   if defined(BIT64)
        _BitScanReverse64(&index, bb);
#   else
        if (0 != u32(bb >> 0x20))
        {
            _BitScanReverse(&index, u32(bb >> 0x20));
            index += 0x20;
        }
        else
        {
            _BitScanReverse(&index, u32(bb >> 0x00));
        }
#   endif
        return Square(index);
    }

#elif defined(__GNUC__) // GCC, Clang, ICC compiler

    inline Square scan_lsq(Bitboard bb)
    {
        assert(0 != bb);
        return
#   if defined(BIT64)
        Square(__builtin_ctzll(bb));
#   else
        Square(0 != u32(bb >> 0x00) ?
                __builtin_ctz(bb >> 0x00) :
                __builtin_ctz(bb >> 0x20) + 0x20);
#   endif
    }
    inline Square scan_msq(Bitboard bb)
    {
        assert(0 != bb);
        return
#   if defined(BIT64)
        Square(__builtin_clzll(bb) ^ i08(SQ_H8));
#   else
        Square(0 != ((u32(bb >> 0x20) ^ i08(SQ_H8)) ?
                __builtin_clz(bb >> 0x20) :
                __builtin_clz(bb >> 0x00) + 0x20));
#   endif
    }

//#else

    //// Assembly code by Heinz van Saanen
    //inline Square scan_lsq(Bitboard bb)
    //{
    //    assert(0 != bb);
    //    Bitboard index;
    //    __asm__("bsfq %1, %0": "=r" (index) : "rm" (bb));
    //    return Square(index);
    //}
    //
    //inline Square scan_msq(Bitboard bb)
    //{
    //    assert(0 != bb);
    //    Bitboard index;
    //    __asm__("bsrq %1, %0": "=r" (index) : "rm" (bb));
    //    return Square(index);
    //}

#else // Compiler is neither GCC nor MSVC compatible

#   if defined(BIT64)

    // * @author Kim Walisch (2012)
    constexpr u64 DeBruijn_64 = U64(0x03F79D71B4CB0A89);
    constexpr std::array<u08, SQ_NO> BSF_Table
    {
         0, 47,  1, 56, 48, 27,  2, 60,
        57, 49, 41, 37, 28, 16,  3, 61,
        54, 58, 35, 52, 50, 42, 21, 44,
        38, 32, 29, 23, 17, 11,  4, 62,
        46, 55, 26, 59, 40, 36, 15, 53,
        34, 51, 20, 43, 31, 22, 10, 45,
        25, 39, 14, 33, 19, 30,  9, 24,
        13, 18,  8, 12,  7,  6,  5, 63
    };

#   else

    constexpr u32 DeBruijn_32 = U32(0x783A9B23);
    constexpr std::array<u08, SQ_NO> BSF_Table
    {
        63, 30,  3, 32, 25, 41, 22, 33,
        15, 50, 42, 13, 11, 53, 19, 34,
        61, 29,  2, 51, 21, 43, 45, 10,
        18, 47,  1, 54,  9, 57,  0, 35,
        62, 31, 40,  4, 49,  5, 52, 26,
        60,  6, 23, 44, 46, 27, 56, 16,
         7, 39, 48, 24, 59, 14, 12, 55,
        38, 28, 58, 20, 37, 17, 36,  8
    };

    constexpr std::array<u08, (1 << 8)> MSB_Table
    {
        0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
        4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    };

#   endif

    inline Square scan_lsq(Bitboard bb)
    {
        assert(0 != bb);
        bb ^= (bb - 1); // Set all bits including the LS1B and below
        u08 index =
#   if defined(BIT64)
        // Use Kim Walisch extending trick for 64-bit
        (bb * DeBruijn_64) >> 58;
#   else
        // Use Matt Taylor's folding trick for 32-bit
        (u32((bb >> 0) ^ (bb >> 32)) * DeBruijn_32) >> 26;
#   endif
        return Square(BSF_Table[index]);
    }

    inline Square scan_msq(Bitboard bb)
    {
        assert(0 != bb);

#   if defined(BIT64)
        // Set all bits including the MS1B and below
        bb |= bb >> 0x01;
        bb |= bb >> 0x02;
        bb |= bb >> 0x04;
        bb |= bb >> 0x08;
        bb |= bb >> 0x10;
        bb |= bb >> 0x20;
        u08 index = (bb * DeBruijn_64) >> 58;
        return Square(BSF_Table[index]);
#   else
        u08 msb = 0;
        if (bb > 0xFFFFFFFF)
        {
            bb >>= 32;
            msb = 32;
        }
        u32 bb32 = u32(bb);
        if (bb32 > 0xFFFF)
        {
            bb32 >>= 16;
            msb += 16;
        }
        u16 bb16 = u16(bb32);
        if (bb16 > 0xFF)
        {
            bb16 >>= 8;
            msb += 8;
        }
        return Square(msb + MSB_Table[bb16]);
#   endif

    }

#endif

    // Find the most advanced square in the given bitboard relative to the given color.
    inline Square scan_frontmost_sq(Color c, Bitboard bb)
    {
        return WHITE == c ?
                scan_msq(bb) :
                scan_lsq(bb);
    }

    inline Square pop_lsq(Bitboard &bb)
    {
        Square sq = scan_lsq(bb);
//#   if defined(BM2)
//        bb = BLSR(bb);
//#   else
        bb &= (bb - 1);
//#   endif
        return sq;
    }

    template<PieceType PT>
    extern Bitboard slide_attacks(Square, Bitboard = 0);

    extern void initialize();

#if !defined(NDEBUG)

    extern std::string pretty (Bitboard);

#endif

}
