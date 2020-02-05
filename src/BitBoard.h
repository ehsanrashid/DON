#pragma once

#include <array>

#include "Type.h"

namespace BitBoard {

    ///// makeBitboard() returns a bitboard compile-time constructed from a list of squares, files, ranks
    //constexpr Bitboard makeBitboard() { return 0; }

    //template<typename ...Squares>
    //constexpr Bitboard makeBitboard(Square s, Squares... squares)
    //{
    //    return U64(0x0000000000000001) << s | makeBitboard(squares...);
    //}
    //template<typename ...Files>
    //constexpr Bitboard makeBitboard(File f, Files... files)
    //{
    //    return U64(0x0101010101010101) << f | makeBitboard(files...);
    //}
    //template<typename ...Ranks>
    //constexpr Bitboard makeBitboard(Rank r, Ranks... ranks)
    //{
    //    return U64(0x00000000000000FF) << (r * 8) | makeBitboard(ranks...);
    //}

    constexpr Bitboard All_bb = U64(0xFFFFFFFFFFFFFFFF);

    constexpr Bitboard FABB = U64(0x0101010101010101);
    constexpr Bitboard FBBB = FABB << 1;
    constexpr Bitboard FCBB = FABB << 2;
    constexpr Bitboard FDBB = FABB << 3;
    constexpr Bitboard FEBB = FABB << 4;
    constexpr Bitboard FFBB = FABB << 5;
    constexpr Bitboard FGBB = FABB << 6;
    constexpr Bitboard FHBB = FABB << 7;

    constexpr Bitboard R1BB = U64(0x00000000000000FF);
    constexpr Bitboard R2BB = R1BB << (8 * 1);
    constexpr Bitboard R3BB = R1BB << (8 * 2);
    constexpr Bitboard R4BB = R1BB << (8 * 3);
    constexpr Bitboard R5BB = R1BB << (8 * 4);
    constexpr Bitboard R6BB = R1BB << (8 * 5);
    constexpr Bitboard R7BB = R1BB << (8 * 6);
    constexpr Bitboard R8BB = R1BB << (8 * 7);

    //constexpr Bitboard DiagonalsBB = U64(0x8142241818244281); // A1..H8 | H1..A8
    constexpr Bitboard CenterBB = (FDBB|FEBB) & (R4BB|R5BB);

    constexpr std::array<Bitboard, CLR_NO> Colors
    {
        U64(0x55AA55AA55AA55AA),
        U64(0xAA55AA55AA55AA55)
    };
    constexpr std::array<Bitboard, 3> Sides
    {
        FEBB|FFBB|FGBB|FHBB,
        FABB|FBBB|FCBB|FDBB,
        FCBB|FDBB|FEBB|FFBB
    };
    constexpr std::array<Bitboard, F_NO> KingFlanks
    {
        Sides[CS_QUEN] ^ FDBB,
        Sides[CS_QUEN],
        Sides[CS_QUEN],
        Sides[CS_NO],
        Sides[CS_NO],
        Sides[CS_KING],
        Sides[CS_KING],
        Sides[CS_KING] ^ FEBB
    };
    constexpr std::array<Bitboard, CLR_NO> Outposts
    {
        R4BB|R5BB|R6BB,
        R5BB|R4BB|R3BB
    };
    constexpr std::array<Bitboard, CLR_NO> Camps
    {
        R1BB|R2BB|R3BB|R4BB|R5BB,
        R8BB|R7BB|R6BB|R5BB|R4BB
    };
    constexpr std::array<Bitboard, CLR_NO> LowRanks
    {
        R2BB|R3BB,
        R7BB|R6BB
    };
    constexpr std::array<Bitboard, CLR_NO> Regions
    {
        R2BB|R3BB|R4BB,
        R7BB|R6BB|R5BB
    };

#   define S_02(n)  U64(1)<<(2*(n)),  U64(1)<<(2*(n)+1)
#   define S_04(n)      S_02(2*(n)),      S_02(2*(n)+1)
#   define S_08(n)      S_04(2*(n)),      S_04(2*(n)+1)
#   define S_16(n)      S_08(2*(n)),      S_08(2*(n)+1)
    constexpr std::array<Bitboard, SQ_NO> Squares
    {
        S_16(0), S_16(1), S_16(2), S_16(3),
    };
#   undef S_16
#   undef S_08
#   undef S_04
#   undef S_02

    extern std::array<std::array<Bitboard, SQ_NO>, CLR_NO>  PawnAttacks;
    extern std::array<std::array<Bitboard, SQ_NO>, NONE>    PieceAttacks;

    extern std::array<std::array<Bitboard, SQ_NO>, SQ_NO>   Lines;

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

        Bitboard attacksBB(Bitboard occ) const
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
    // If (shifting & 7) != 0 then  bound clipping is done (~FABB or ~FHBB)
    template<> constexpr Bitboard shift<DEL_E >(Bitboard bb) { return (bb & ~FHBB) <<  1; }
    template<> constexpr Bitboard shift<DEL_W >(Bitboard bb) { return (bb & ~FABB) >>  1; }
    template<> constexpr Bitboard shift<DEL_NE>(Bitboard bb) { return (bb & ~FHBB) <<  9; }
    template<> constexpr Bitboard shift<DEL_SE>(Bitboard bb) { return (bb & ~FHBB) >>  7; }
    template<> constexpr Bitboard shift<DEL_NW>(Bitboard bb) { return (bb & ~FABB) <<  7; }
    template<> constexpr Bitboard shift<DEL_SW>(Bitboard bb) { return (bb & ~FABB) >>  9; }

    ///// Rotate Right (toward LSB)
    //constexpr Bitboard rotate_R(Bitboard bb, i08 k) { return (bb >> k) | (bb << (SQ_NO - k)); }
    ///// Rotate Left  (toward MSB)
    //constexpr Bitboard rotate_L(Bitboard bb, i08 k) { return (bb << k) | (bb >> (SQ_NO - k)); }

    constexpr Bitboard squareBB(Square s) { return Squares[s]; }

    constexpr bool contains(Bitboard bb, Square s) { return 0 != (bb & squareBB(s)); }
    
    constexpr Bitboard operator&(Bitboard bb, Square s) { return bb & squareBB(s); }
    constexpr Bitboard operator|(Bitboard bb, Square s) { return bb | squareBB(s); }
    constexpr Bitboard operator^(Bitboard bb, Square s) { return bb ^ squareBB(s); }

    constexpr Bitboard operator&(Square s, Bitboard bb) { return bb & squareBB(s); }
    constexpr Bitboard operator|(Square s, Bitboard bb) { return bb | squareBB(s); }
    constexpr Bitboard operator^(Square s, Bitboard bb) { return bb ^ squareBB(s); }

    inline Bitboard& operator|=(Bitboard &bb, Square s) { return bb |= squareBB(s); }
    inline Bitboard& operator^=(Bitboard &bb, Square s) { return bb ^= squareBB(s); }

    constexpr Bitboard operator|(Square s1, Square s2) { return squareBB(s1) | squareBB(s2); }

    constexpr Bitboard fileBB(File f)   { return FABB << f; }
    constexpr Bitboard fileBB(Square s) { return fileBB(fileOf(s)); }

    constexpr Bitboard rankBB(Rank r)   { return R1BB << (8 * r); }
    constexpr Bitboard rankBB(Square s) { return rankBB(rankOf(s)); }

    // frontRanks() returns ranks in front of the given rank
    constexpr Bitboard frontRanks(Color c, Rank r)
    {
        return WHITE == c ?
                ~R1BB << (8 * (r - R_1)) :
                ~R8BB >> (8 * (R_8 - r));
    }
    // frontRanks() returns ranks in front of the given square
    constexpr Bitboard frontRanks(Color c, Square s) { return frontRanks(c, rankOf(s)); }

    constexpr Bitboard adjacentFiles(Square s)
    {
        return shift<DEL_E>(fileBB(s))
             | shift<DEL_W>(fileBB(s));
    }
    //constexpr Bitboard adjacentRanks(Square s)
    //{
    //    return shift<DEL_N>(rankBB(s))
    //         | shift<DEL_S>(rankBB(s));
    //}

    constexpr Bitboard frontSquares(Color c, Square s) { return frontRanks(c, s) & fileBB(s); }

    constexpr Bitboard pawnAttackSpan(Color c, Square s) { return frontRanks(c, s) & adjacentFiles(s); }
    constexpr Bitboard   pawnPassSpan(Color c, Square s) { return frontSquares(c, s) | pawnAttackSpan(c, s); }

    /// dist() functions return the distance between s1 and s2, defined as the
    /// number of steps for a king in s1 to reach s2.

    template<typename T = Square> inline i32 dist(Square, Square);
    template<> inline i32 dist<  File>(Square s1, Square s2) { return std::abs(fileOf(s1) - fileOf(s2)); }
    template<> inline i32 dist<  Rank>(Square s1, Square s2) { return std::abs(rankOf(s1) - rankOf(s2)); }
    template<> inline i32 dist<Square>(Square s1, Square s2) { return std::max(dist<File>(s1, s2), dist<Rank>(s1, s2)); }

    inline Bitboard    lines(Square s1, Square s2) { return Lines[s1][s2]; }
    inline Bitboard betweens(Square s1, Square s2)
    {
        return lines(s1, s2)
             & (  (All_bb << (s1 + (s1 < s2)))
                ^ (All_bb << (s2 + (s2 < s1))));
    }
    /// Check the squares s1, s2 and s3 are aligned on a straight line.
    inline bool squaresAligned(Square s1, Square s2, Square s3) { return contains(lines(s1, s2), s3); }

    constexpr bool moreThanOne(Bitboard bb)
    {
        return
//#   if defined(BM2)
//      0 != BLSR(bb);
//#   else
        0 != (bb & (bb - 1));
//#   endif
    }

    //constexpr bool oppositeColor(Square s1, Square s2)
    //{
    //    return contains(Colors[WHITE], s1) == contains(Colors[BLACK], s2);
    //}

    constexpr Bitboard pawnSglPushes(Color c, Bitboard bb)
    {
        return WHITE == c ?
                shift<DEL_N>(bb) :
                shift<DEL_S>(bb);
    }
    constexpr Bitboard pawnDblPushes(Color c, Bitboard bb)
    {
        return WHITE == c ?
                shift<DEL_NN>(bb) :
                shift<DEL_SS>(bb);
    }
    constexpr Bitboard pawnLAttacks(Color c, Bitboard bb)
    {
        return WHITE == c ?
                shift<DEL_NW>(bb) :
                shift<DEL_SE>(bb);
    }
    constexpr Bitboard pawnRAttacks(Color c, Bitboard bb)
    {
        return WHITE == c ?
                shift<DEL_NE>(bb) :
                shift<DEL_SW>(bb);
    }

    /// pawnSglAttacks() returns the single attackes by pawns of the given color
    constexpr Bitboard pawnSglAttacks(Color c, Bitboard bb)
    {
        return pawnLAttacks(c, bb) | pawnRAttacks(c, bb);
    }
    /// pawnDblAttacks() returns the double attackes by pawns of the given color
    constexpr Bitboard pawnDblAttacks(Color c, Bitboard bb)
    {
        return pawnLAttacks(c, bb) & pawnRAttacks(c, bb);
    }

    /// attacksBB(s, occ) takes a square and a bitboard of occupied squares,
    /// and returns a bitboard representing all squares attacked by PT (Bishop or Rook or Queen) on the given square.
    template<PieceType PT> Bitboard attacksBB(Square, Bitboard);

    template<> inline Bitboard attacksBB<NIHT>(Square s, Bitboard    ) { return PieceAttacks[NIHT][s]; }
    template<> inline Bitboard attacksBB<KING>(Square s, Bitboard    ) { return PieceAttacks[KING][s]; }
    /// Attacks of the Bishop with occupancy
    template<> inline Bitboard attacksBB<BSHP>(Square s, Bitboard occ) { return BMagics[s].attacksBB(occ); }
    /// Attacks of the Rook with occupancy
    template<> inline Bitboard attacksBB<ROOK>(Square s, Bitboard occ) { return RMagics[s].attacksBB(occ); }
    /// Attacks of the Queen with occupancy
    template<> inline Bitboard attacksBB<QUEN>(Square s, Bitboard occ) { return BMagics[s].attacksBB(occ)
                                                                              | RMagics[s].attacksBB(occ); }

    /// Position::attacksFrom() finds attacks of the piecetype from the square on occupancy.
    inline Bitboard attacksFrom(PieceType pt, Square s, Bitboard occ)
    {
        switch (pt)
        {
        case NIHT: return PieceAttacks[NIHT][s];
        case BSHP: return attacksBB<BSHP>(s, occ);
        case ROOK: return attacksBB<ROOK>(s, occ);
        case QUEN: return attacksBB<QUEN>(s, occ);
        case KING: return PieceAttacks[KING][s];
        default: assert(false); return 0;
        }
    }
    /// Position::attacksFrom() finds attacks from the square on occupancy.
    inline Bitboard attacksFrom(Piece pc, Square s, Bitboard occ)
    {
        switch (typeOf(pc))
        {
        case PAWN: return PawnAttacks[colorOf(pc)][s];
        case NIHT: return PieceAttacks[NIHT][s];
        case BSHP: return attacksBB<BSHP>(s, occ);
        case ROOK: return attacksBB<ROOK>(s, occ);
        case QUEN: return attacksBB<QUEN>(s, occ);
        case KING: return PieceAttacks[KING][s];
        default: assert(false); return 0;
        }
    }

#if !defined(ABM) // PopCount Table

    inline i32 popCount(Bitboard bb)
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
    inline i32 popCount(Bitboard bb)
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

    inline i32 popCount(Bitboard bb)
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

    inline Square scanLSq(Bitboard bb)
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

    inline Square scanMSq(Bitboard bb)
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

    inline Square scanLSq(Bitboard bb)
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
    inline Square scanMSq(Bitboard bb)
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
    //inline Square scanLSq(Bitboard bb)
    //{
    //    assert(0 != bb);
    //    Bitboard index;
    //    __asm__("bsfq %1, %0": "=r" (index) : "rm" (bb));
    //    return Square(index);
    //}
    //
    //inline Square scanMSq(Bitboard bb)
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
    constexpr std::array<u08, SQ_NO> BSFTable
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
    constexpr std::array<u08, SQ_NO> BSFTable
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

    constexpr std::array<u08, (1 << 8)> MSBTable
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

    inline Square scanLSq(Bitboard bb)
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
        return Square(BSFTable[index]);
    }

    inline Square scanMSq(Bitboard bb)
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
        return Square(BSFTable[index]);
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
        return Square(msb + MSBTable[bb16]);
#   endif

    }

#endif

    // Find the most advanced square in the given bitboard relative to the given color.
    inline Square scanFrontMostSq(Color c, Bitboard bb)
    {
        return WHITE == c ?
                scanMSq(bb) :
                scanLSq(bb);
    }

    inline Square popLSq(Bitboard &bb)
    {
        Square sq = scanLSq(bb);
//#   if defined(BM2)
//        bb = BLSR(bb);
//#   else
        bb &= (bb - 1);
//#   endif
        return sq;
    }

    template<PieceType PT>
    extern Bitboard slideAttacks(Square, Bitboard = 0);

    extern void initialize();

#if !defined(NDEBUG)

    extern std::string pretty (Bitboard);

#endif

}
