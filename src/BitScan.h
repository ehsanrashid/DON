//#pragma once
#ifndef BITSCAN_H_
#define BITSCAN_H_

#include "Type.h"

#pragma warning (disable: 4244) // 'argument' : conversion from '-' to '-', possible loss of data

#ifdef BSFQ

#   if defined(_MSC_VER)

#   include <intrin.h> // MSVC popcnt and bsfq instrinsics
// _BitScanForward64() & _BitScanReverse64()

inline Square scan_lsq (Bitboard bb)
{
    unsigned long index;

#ifdef _64BIT

    _BitScanForward64 (&index, bb);

#else

    if (uint32_t (bb))
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

inline Square scan_msq (Bitboard bb)
{
    unsigned long index;

#ifdef _64BIT

    _BitScanReverse64 (&index, bb);

#else

    if (uint32_t (bb >> 32))
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

#   elif defined(__arm__)

#ifndef _64BIT

inline uint8_t scan_lsb32 (uint32_t w)
{
    __asm__ ("rbit %0, %1" : "=r" (w) : "r" (w));
    return __builtin_clz (w);
}

#endif

inline Square scan_lsq (Bitboard bb)
{

#ifdef _64BIT
    // TODO::
    //return __builtin_clzll (bb);

#else

    return Square (uint32_t (bb) ?
        scan_lsb32 (uint32_t (bb)) :
        scan_lsb32 (uint32_t (bb >> 32)) + 32);

#endif
}

inline Square scan_msq (Bitboard bb)
{
#ifdef _64BIT

    return Square (63 - __builtin_clzll (bb));

#else

    return Square (63 - (uint32_t (bb) ?
        __builtin_clz (bb) :
        __builtin_clz (bb >> 32) + 32));

#endif
}

#   else

// Assembly code by Heinz van Saanen
inline Square scan_lsq (Bitboard bb)
{
    uint8_t index;
    __asm__ ("bsfq %1, %0": "=r" (index) : "rm" (bb));
    return Square (index);
}
inline Square scan_msq (Bitboard bb)
{
    uint8_t index;
    __asm__ ("bsrq %1, %0": "=r" (index) : "rm" (bb));
    return Square (index);
}

#   endif

#else

inline Square  scan_lsq (Bitboard bb)
{

    /// ---> (X & -X) == X & (~X + 1) != (X ^ (X - 1))
    /// two's complement (-X) and ones' decrement (X-1) are complement sets

#ifdef _64BIT

    /// Modulo operation of the isolated LS1B by the prime number 67.
    /// The remainder 0..66 can be used to perfectly hash the bit - index table. Three gaps are 0, 17, and 34
    //static const uint8_t Prime_67 = 0x43; // 67
    //CACHE_ALIGN8
    //    static const int8_t BSF_Table[Prime_67 + 1] =
    //{
    //    64, 00, 01, 39, 02, 15, 40, 23,
    //    03, 12, 16, 59, 41, 19, 24, 54,
    //    04, -1, 13, 10, 17, 62, 60, 28,
    //    42, 30, 20, 51, 25, 44, 55, 47,
    //    05, 32, -1, 38, 14, 22, 11, 58,
    //    18, 53, 63,  9, 61, 27, 29, 50,
    //    43, 46, 31, 37, 21, 57, 52,  8,
    //    26, 49, 45, 36, 56, 07, 48, 35,
    //    06, 34, 33, -1
    //};
    //uint64_t x = bb & -bb;  // isolated the LS1B
    //uint8_t index = x % Prime_67;
    //return Square (BSF_Table[index]);

    //-- -

    /// * DeBruijn (U32 (0x01)) = U64 (0X0218A392CD3D5DBF)
    //if (!bb) return SQ_NO;
    //CACHE_ALIGN8
    //    static const uint8_t BSF_Table[SQ_NO] =
    //{
    //    00, 01, 02, 07, 03, 13,  8, 19,
    //    04, 25, 14, 28,  9, 34, 20, 40,
    //    05, 17, 26, 38, 15, 46, 29, 48,
    //    10, 31, 35, 54, 21, 50, 41, 57,
    //    63, 06, 12, 18, 24, 27, 33, 39,
    //    16, 37, 45, 47, 30, 53, 49, 56,
    //    62, 11, 23, 32, 36, 44, 52, 55,
    //    61, 22, 43, 51, 60, 42, 59, 58
    //};
    //static const uint64_t DeBruijn_64 = U64 (0X0218A392CD3D5DBF);
    //uint64_t x = bb & -bb;  // isolated the LS1B
    //uint8_t index = (x * DeBruijn_64) >> 0x3A; // 58
    //return Square (BSF_Table[index]);

    //-- -

    /// *@author Martin Läuter (1997), Charles E.Leiserson, Harald Prokop, Keith H.Randall
    /// * DeBruijn (U32 (0x4000000)) = U64 (0X03F79D71B4CB0A89)
    //if (!bb) return SQ_NO;
    //CACHE_ALIGN8
    //    static const uint8_t BSF_Table[SQ_NO] =
    //{
    //    00, 01, 48, 02, 57, 49, 28, 03,
    //    61, 58, 50, 42, 38, 29, 17, 04,
    //    62, 55, 59, 36, 53, 51, 43, 22,
    //    45, 39, 33, 30, 24, 18, 12, 05,
    //    63, 47, 56, 27, 60, 41, 37, 16,
    //    54, 35, 52, 21, 44, 32, 23, 11,
    //    46, 26, 40, 15, 34, 20, 31, 10,
    //    25, 14, 19,  9, 13,  8, 07, 06,
    //};
    //static const uint64_t DeBruijn_64 = U64 (0X03F79D71B4CB0A89);
    //uint64_t x = bb & -bb;  // isolated the LS1B
    //uint8_t index = (x * DeBruijn_64) >> 0x3A; // 58
    //return Square (BSF_Table[index]);

    // ---

    /// * @author Kim Walisch (2012)
    /// * DeBruijn(U32(0x4000000)) = U64(0X03F79D71B4CB0A89)
    if (!bb) return SQ_NO;
    CACHE_ALIGN8
        static const uint8_t BSF_Table[SQ_NO] =
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
    static const uint64_t DeBruijn_64 = U64 (0X03F79D71B4CB0A89);
    uint64_t x = bb ^ (bb - 1); // set all bits including the LS1B and below
    uint8_t index = (x * DeBruijn_64) >> 0x3A; // 58
    return Square (BSF_Table[index]);

#else
    CACHE_ALIGN8
        static const uint8_t BSF_Table[SQ_NO] =
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
    static const uint32_t DeBruijn_32 = U32 (0x783A9B23);
    uint64_t x = bb ^ (bb - 1);
    uint32_t fold = uint32_t (x ^ (x >> 32));
    uint8_t index = (fold * DeBruijn_32) >> 0x1A; // 26
    return Square (BSF_Table[index]);

#endif

}

inline Square  scan_msq (Bitboard bb)
{

#ifdef _64BIT

    /// * @authors Kim Walisch, Mark Dickinson (2012)
    /// * DeBruijn(U32(0x4000000)) = U64(0X03F79D71B4CB0A89)
    if (!bb) return SQ_NO;
    CACHE_ALIGN8
        static const uint8_t BSF_Table[SQ_NO] =
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

    static const uint64_t DeBruijn_64 = U64 (0X03F79D71B4CB0A89);
    // set all bits including the MS1B and below
    bb |= bb >> 0x01;
    bb |= bb >> 0x02;
    bb |= bb >> 0x04;
    bb |= bb >> 0x08;
    bb |= bb >> 0x10;
    bb |= bb >> 0x20;

    uint8_t index = (bb * DeBruijn_64) >> 0x3A; // 58
    return (Square) BSF_Table[index];

#else

    CACHE_ALIGN8
        static const uint8_t MSB_Table[_UI8_MAX + 1] =
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

    //size_t i = 0;
    //uint8_t k = 0;
    //while (i <= _UI8_MAX) // k < 8
    //{
    //    const size_t size = (2 << k);
    //    while (i < size)
    //    {
    //        MSB_Table[i] = k;
    //        ++i;
    //    }
    //    ++k;
    //}

    if (!bb) return SQ_NO;
    uint8_t msb = 0;
    if (bb > 0xFFFFFFFF)
    {
        bb >>= 32;
        msb = 32;
    }

    uint32_t b = uint32_t (bb);
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

// scan_rel_lsq() finds least significant bit relative to the given color
inline Square scan_rel_lsq         (Color c, Bitboard bb) { return (WHITE == c) ? scan_lsq (bb) : scan_msq (bb); }

// scan_rel_frntmost_sq() and scan_rel_backmost_sq() find the square
// corresponding to the most/least advanced bit relative to the given color.
inline Square scan_rel_frntmost_sq (Color c, Bitboard bb) { return (WHITE == c) ? scan_msq (bb) : scan_lsq (bb); }
inline Square scan_rel_backmost_sq (Color c, Bitboard bb) { return (WHITE == c) ? scan_lsq (bb) : scan_msq (bb); }

inline Square pop_lsq (Bitboard &bb)
{
    Square s = scan_lsq (bb);
    bb &= (bb - 1); // reset the LS1B
    return s;
}

//// b = [b0...b3]
//uint32_t pack (uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
//{
//    uint32_t b;
//    b = b0;
//    b = (b << CHAR_BIT) | b1;
//    b = (b << CHAR_BIT) | b2;
//    b = (b << CHAR_BIT) | b3;
//    return b;
//}
//// b = [b0...b3], k = [0...3]
//uint8_t unpack (uint32_t b, uint8_t k)
//{
//    uint8_t  n    = k * CHAR_BIT;
//    uint32_t mask = 0xFF << n;
//    return (b & mask) >> n;
//}

#endif
