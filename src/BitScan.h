#ifndef _BITSCAN_H_INC_
#define _BITSCAN_H_INC_

#include "Type.h"

namespace BitBoard {

#ifdef BSFQ

#   ifdef _MSC_VER

#   include <intrin.h> // MSVC popcnt and bsfq instrinsics
// _BitScanForward64() & _BitScanReverse64()

inline Square scan_lsq (Bitboard bb)
{
    unsigned long index;

#ifdef BIT64
    _BitScanForward64 (&index, bb);
#else
    if (u32(bb) != 0)
    {
        _BitScanForward (&index, bb);
    }
    else
    {
        _BitScanForward (&index, bb >> 32);
        index += 32;
    }
#endif

    return Square(index);
}

inline Square scan_msq (Bitboard bb)
{
    unsigned long index;

#ifdef BIT64
    _BitScanReverse64 (&index, bb);
#else
    if (u32(bb >> 32) != 0)
    {
        _BitScanReverse (&index, bb >> 32);
        index += 32;
    }
    else
    {
        _BitScanReverse (&index, bb);
    }
#endif

    return Square(index);
}

#   elif __arm__

inline Square scan_lsq (Bitboard bb)
{
#ifdef BIT64
    return Square(__builtin_ctzll (bb));
#else
    return Square((bb & 0x00000000FFFFFFFF) ?
        __builtin_ctz (bb >> 00) :
        __builtin_ctz (bb >> 32) + 32);
#endif
}

inline Square scan_msq (Bitboard bb)
{
#ifdef BIT64
    return Square(i32(SQ_H8) - __builtin_clzll (bb));
#else
    return Square(i32(SQ_H8) - ((bb & 0xFFFFFFFF00000000) ?
        __builtin_clz (bb >> 32) :
        __builtin_clz (bb >> 00) + 32));
#endif
}

#   else

// Assembly code by Heinz van Saanen
inline Square scan_lsq (Bitboard bb)
{
    Bitboard sq;
    __asm__ ("bsfq %1, %0": "=r" (sq) : "rm" (bb));
    return Square(sq);
    //return Square(__builtin_ctzll (bb));
}

inline Square scan_msq (Bitboard bb)
{
    Bitboard sq;
    __asm__ ("bsrq %1, %0": "=r" (sq) : "rm" (bb));
    return Square(sq);
    //return Square(i32(SQ_H8) - __builtin_clzll (bb));
}

#   endif

#else   // ifndef BSFQ

#ifdef BIT64

    const u64 DE_BRUIJN_64 = U64(0x03F79D71B4CB0A89);
    // * @author Kim Walisch (2012)
    // * DeBruijn(U32(0x4000000)) = U64(0x03F79D71B4CB0A89)
    const u08 BSF_TABLE[SQ_NO] =
    {
        00, 47, 01, 56, 48, 27, 02, 60,
        57, 49, 41, 37, 28, 16, 03, 61,
        54, 58, 35, 52, 50, 42, 21, 44,
        38, 32, 29, 23, 17, 11, 04, 62,
        46, 55, 26, 59, 40, 36, 15, 53,
        34, 51, 20, 43, 31, 22, 10, 45,
        25, 39, 14, 33, 19, 30,  9, 24,
        13, 18,  8, 12, 07, 06, 05, 63
    };

#else

    const u32 DE_BRUIJN_32 = U32(0x783A9B23);

    const u08 BSF_TABLE[SQ_NO] =
    {
        63, 30, 03, 32, 25, 41, 22, 33,
        15, 50, 42, 13, 11, 53, 19, 34,
        61, 29, 02, 51, 21, 43, 45, 10,
        18, 47, 01, 54,  9, 57, 00, 35,
        62, 31, 40, 04, 49, 05, 52, 26,
        60, 06, 23, 44, 46, 27, 56, 16,
        07, 39, 48, 24, 59, 14, 12, 55,
        38, 28, 58, 20, 37, 17, 36,  8
    };

    const u08 MSB_TABLE[UCHAR_MAX+1] =
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

#endif

inline Square  scan_lsq (Bitboard bb)
{

#ifdef BIT64

    if (bb == U64(0)) return SQ_NO;
    u64 x = bb ^ (bb - 1); // set all bits including the LS1B and below
    u08 index = (x * DE_BRUIJN_64) >> 0x3A; // 58
    return Square(BSF_TABLE[index]);

#else

    if (bb == U64(0)) return SQ_NO;
    // Use Matt Taylor's folding trick for 32-bit
    u64 x = bb ^ (bb - 1);
    u32 fold = u32(x ^ (x >> 32));
    u08 index = (fold * DE_BRUIJN_32) >> 0x1A; // 26
    return Square(BSF_TABLE[index]);

#endif

}

inline Square  scan_msq (Bitboard bb)
{

#ifdef BIT64

    if (bb == U64(0)) return SQ_NO;
    // set all bits including the MS1B and below
    bb |= bb >> 0x01;
    bb |= bb >> 0x02;
    bb |= bb >> 0x04;
    bb |= bb >> 0x08;
    bb |= bb >> 0x10;
    bb |= bb >> 0x20;

    u08 index = (bb * DE_BRUIJN_64) >> 0x3A; // 58
    return Square(BSF_TABLE[index]);

#else

    if (bb == U64(0)) return SQ_NO;
    u08 msb = 0;
    if (bb > 0xFFFFFFFF)
    {
        bb >>= 32;
        msb = 32;
    }

    u32 b = u32(bb);
    if (b > 0xFFFF)
    {
        b >>= 16;
        msb += 16;
    }
    if (b > 0xFF)
    {
        b >>= 8;
        msb += 8;
    }

    return Square(msb + MSB_TABLE[b]);

#endif

}

#endif

// scan_frntmost_sq() and scan_backmost_sq() find the square
// corresponding to the most/least advanced bit relative to the given color.
inline Square scan_frntmost_sq (Color c, Bitboard bb) { return WHITE == c ? scan_msq (bb) : scan_lsq (bb); }
inline Square scan_backmost_sq (Color c, Bitboard bb) { return WHITE == c ? scan_lsq (bb) : scan_msq (bb); }

inline Square pop_lsq (Bitboard &bb)
{
    Square sq = scan_lsq (bb);
#ifndef BM2
    bb &= (bb - 1);
#else
    bb = BLSR (bb);
#endif
    return sq;
}

}

#endif // _BITSCAN_H_INC_
