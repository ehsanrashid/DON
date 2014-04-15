#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _BITSCAN_H_INC_
#define _BITSCAN_H_INC_

#include "Type.h"

#ifdef _MSC_VER
#   pragma warning (disable: 4244) // 'argument' : conversion from '-' to '-', possible loss of data
#endif

#ifdef BSFQ

#   ifdef _MSC_VER

#   include <intrin.h> // MSVC popcnt and bsfq instrinsics
// _BitScanForward64() & _BitScanReverse64()

INLINE Square scan_lsq (Bitboard bb)
{
    unsigned long index;

#ifdef _64BIT
    _BitScanForward64 (&index, bb);
#else
    if (u32 (bb))
    {
        _BitScanForward (&index, bb);
    }
    else
    {
        _BitScanForward (&index, bb >> 32);
        index += 32;
    }
#endif

    return Square (index);
}

INLINE Square scan_msq (Bitboard bb)
{
    unsigned long index;

#ifdef _64BIT
    _BitScanReverse64 (&index, bb);
#else
    if (u32 (bb >> 32))
    {
        _BitScanReverse (&index, bb >> 32);
        index += 32;
    }
    else
    {
        _BitScanReverse (&index, bb);
    }
#endif

    return Square (index);
}

#   elif __arm__

#ifndef _64BIT

INLINE u08 scan_lsb32 (u32 w)
{
    __asm__ ("rbit %0, %1" : "=r" (w) : "r" (w));
    return __builtin_clz (w);
}

#endif

INLINE Square scan_lsq (Bitboard bb)
{
#ifdef _64BIT
    return Square (__builtin_clzll (bb));
#else
    return Square  (u32 (bb) ?
        scan_lsb32 (u32 (bb      )) :
        scan_lsb32 (u32 (bb >> 32)) + 32);
#endif
}

INLINE Square scan_msq (Bitboard bb)
{
#ifdef _64BIT
    return Square (63 - __builtin_clzll (bb));
#else
    return Square (63 - (u32 (bb) ?
        __builtin_clz (bb) :
        __builtin_clz (bb >> 32) + 32));
#endif
}

#   else

// Assembly code by Heinz van Saanen
INLINE Square scan_lsq (Bitboard bb)
{
    Bitboard sq;
    __asm__ ("bsfq %1, %0": "=r" (sq) : "rm" (bb));
    return Square (sq);
}

INLINE Square scan_msq (Bitboard bb)
{
    Bitboard sq;
    __asm__ ("bsrq %1, %0": "=r" (sq) : "rm" (bb));
    return Square (sq);
}

#   endif

#else   // ifndef BSFQ

INLINE Square  scan_lsq (Bitboard bb)
{

#ifdef _64BIT

    // * @author Kim Walisch (2012)
    // * DeBruijn(U32(0x4000000)) = U64 (0X03F79D71B4CB0A89)
    if (bb == U64 (0)) return SQ_NO;
    CACHE_ALIGN(8)
        const u08 BSF_Table[SQ_NO] =
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
    const u64 DeBruijn_64 = U64 (0X03F79D71B4CB0A89);
    u64 x = bb ^ (bb - 1); // set all bits including the LS1B and below
    u08 index = (x * DeBruijn_64) >> 0x3A; // 58
    return Square (BSF_Table[index]);

#else

    CACHE_ALIGN(8)
        const u08 BSF_Table[SQ_NO] =
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
    // Use Matt Taylor's folding trick for 32-bit
    const u32 DeBruijn_32 = U32 (0x783A9B23);
    u64 x = bb ^ (bb - 1);
    u32 fold = u32 (x ^ (x >> 32));
    u08 index = (fold * DeBruijn_32) >> 0x1A; // 26
    return Square (BSF_Table[index]);

#endif

}

inline Square  scan_msq (Bitboard bb)
{

#ifdef _64BIT

    // * @authors Kim Walisch, Mark Dickinson (2012)
    // * DeBruijn(U32(0x4000000)) = U64 (0X03F79D71B4CB0A89)
    if (bb == U64 (0)) return SQ_NO;
    CACHE_ALIGN(8)
        const u08 BSF_Table[SQ_NO] =
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

    const u64 DeBruijn_64 = U64 (0X03F79D71B4CB0A89);
    // set all bits including the MS1B and below
    bb |= bb >> 0x01;
    bb |= bb >> 0x02;
    bb |= bb >> 0x04;
    bb |= bb >> 0x08;
    bb |= bb >> 0x10;
    bb |= bb >> 0x20;

    u08 index = (bb * DeBruijn_64) >> 0x3A; // 58
    return (Square) BSF_Table[index];

#else

    CACHE_ALIGN(8)
        const u08 MSB_Table[_UI8_MAX + 1] =
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

    if (bb == U64 (0)) return SQ_NO;
    u08 msb = 0;
    if (bb > 0xFFFFFFFF)
    {
        bb >>= 32;
        msb = 32;
    }

    u32 b = u32 (bb);
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

    return Square (msb + MSB_Table[b]);

#endif

}

#endif

// scan_frntmost_sq() and scan_backmost_sq() find the square
// corresponding to the most/least advanced bit relative to the given color.
INLINE Square scan_frntmost_sq (Color c, Bitboard bb) { return (WHITE == c) ? scan_msq (bb) : scan_lsq (bb); }
INLINE Square scan_backmost_sq (Color c, Bitboard bb) { return (WHITE == c) ? scan_lsq (bb) : scan_msq (bb); }

INLINE Square pop_lsq (Bitboard &bb)
{
    Square s = scan_lsq (bb);
    bb &= (bb - 1); // reset the LS1B
    return s;
}

#endif // _BITSCAN_H_INC_
