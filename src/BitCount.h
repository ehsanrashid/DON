//#pragma once
#ifndef BITCOUNT_H_
#define BITCOUNT_H_


#include "Type.h"

#pragma warning (disable: 4244) // 'argument' : conversion from '-' to '-', possible loss of data

typedef enum BitCountType
{
    CNT_64_FULL,
    CNT_64_MAX15,
    CNT_32_FULL,
    CNT_32_MAX15,
    CNT_HW_POPCNT,

} BitCountType;

template<BitCountType CNT>
// pop_count () counts the number of set bits in a Bitboard
inline uint8_t pop_count (Bitboard bb);

// Determine at compile time the best pop_count<> specialization 
// according if platform is 32-bits or 64-bits,
// to the maximum number of nonzero bits to count and if hardware popcnt instruction is available.

#ifdef POPCNT

const BitCountType FULL  = CNT_HW_POPCNT;
const BitCountType MAX15 = CNT_HW_POPCNT;

#if defined(_MSC_VER)

#   ifdef __INTEL_COMPILER

#       include <nmmintrin.h> 
// Intel header for  SSE4.1 or SSE4.2 intrinsics.
// _mm_popcnt_u64() & _mm_popcnt_u32()

template<>
inline uint8_t pop_count<CNT_HW_POPCNT> (Bitboard bb)
{
#       ifdef _64BIT
    {
        return (_mm_popcnt_u64 (bb));
    }
#       else
    {
        return (_mm_popcnt_u32 (bb) + _mm_popcnt_u32 (bb >> 32));
    }
#       endif
}

#   else

#       include <intrin.h> // MSVC popcnt and bsfq instrinsics
// __popcnt64() & __popcnt()

template<>
inline uint8_t pop_count<CNT_HW_POPCNT> (Bitboard bb)
{
#   ifdef _64BIT
    {
        return (__popcnt64 (bb));
    }
#   else
    {
        return (__popcnt (bb) + __popcnt (bb >> 32));
    }
#   endif
}

#   endif


#else

template<>
inline uint8_t pop_count<CNT_HW_POPCNT> (Bitboard bb)
{
    // Assembly code by Heinz van Saanen
    __asm__ ("popcnt %1, %0" : "=r" (bb) : "r" (bb));
    return (bb);
}

#endif

#else

#   ifdef _64BIT

const BitCountType FULL  = CNT_64_FULL;
const BitCountType MAX15 = CNT_64_MAX15;

const Bitboard M1_64 = U64 (0x5555555555555555);
const Bitboard M2_64 = U64 (0x3333333333333333);
const Bitboard M4_64 = U64 (0x0F0F0F0F0F0F0F0F);
const Bitboard MX_64 = U64 (0x2222222222222222);
const Bitboard H4_64 = U64 (0x1111111111111111);
const Bitboard H8_64 = U64 (0x0101010101010101);

template<>
// Pop count of the Bitboard (64-bit)
inline uint8_t pop_count<CNT_64_FULL> (Bitboard bb)
{
    //Bitboard w0 = (bb & MX_64) + ((bb + bb) & MX_64);
    //Bitboard w1 = (bb >> 1 & MX_64) + (bb >> 2 & MX_64);
    //w0 = w0 + (w0 >> 4) & M4_64;
    //w1 = w1 + (w1 >> 4) & M4_64;
    //return ((w0 + w1) * H8_64) >> 0x39; // 57;

    bb -= (bb >> 1) & M1_64;
    bb = ((bb >> 2) & M2_64) + (bb & M2_64);
    bb = ((bb >> 4) + bb) & M4_64;
    return (bb * H8_64) >> 0x38; // 56;
}

template<>
// Pop count max 15 of the Bitboard (64-bit)
inline uint8_t pop_count<CNT_64_MAX15> (Bitboard bb)
{
    bb -= (bb >> 1) & M1_64;
    bb = ((bb >> 2) & M2_64) + (bb & M2_64);
    return (bb * H4_64) >> 0x3C; // 60;
}

#   else

const BitCountType FULL  = CNT_32_FULL;
const BitCountType MAX15 = CNT_32_MAX15;

const uint32_t M1_32 = U32 (0x55555555);
const uint32_t M2_32 = U32 (0x33333333);
const uint32_t M4_32 = U32 (0x0F0F0F0F);
const uint32_t H4_32 = U32 (0x11111111);
const uint32_t H8_32 = U32 (0x01010101);

template<>
// Pop count of the Bitboard (32-bit)
inline uint8_t pop_count<CNT_32_FULL> (Bitboard bb)
{
    //uint32_t *p = (uint32_t*) (&bb);
    //uint32_t *w0 = p+0;
    //uint32_t *w1 = p+1;
    //*w0 -= (*w0 >> 1) & M1_32;                  // 0-2 in 2 bits
    //*w1 -= (*w1 >> 1) & M1_32;
    //*w0 = ((*w0 >> 2) & M2_32) + (*w0 & M2_32); // 0-4 in 4 bits
    //*w1 = ((*w1 >> 2) & M2_32) + (*w1 & M2_32);
    //*w0 = ((*w0 >> 4) + *w0) & M4_32;
    //*w1 = ((*w1 >> 4) + *w1) & M4_32;
    //return ((*w0 + *w1) * H8_32) >> 0x18; // 24;

    uint32_t w0 = uint32_t (bb);
    uint32_t w1 = uint32_t (bb >> 32);
    w0 -= (w0 >> 1) & M1_32;                 // 0-2 in 2 bits
    w1 -= (w1 >> 1) & M1_32;
    w0 = ((w0 >> 2) & M2_32) + (w0 & M2_32); // 0-4 in 4 bits
    w1 = ((w1 >> 2) & M2_32) + (w1 & M2_32);
    w0 = ((w0 >> 4) + w0) & M4_32;
    w1 = ((w1 >> 4) + w1) & M4_32;
    return ((w0 + w1) * H8_32) >> 0x18; // 24;
}

template<>
// Pop count max 15 of the Bitboard (32-bit)
inline uint8_t pop_count<CNT_32_MAX15> (Bitboard bb)
{
    //uint32_t *p = (uint32_t*) (&bb);
    //uint32_t *w0 = p+0;
    //uint32_t *w1 = p+1;
    //*w0 -= (*w0 >> 1) & M1_32;                  // 0-2 in 2 bits
    //*w1 -= (*w1 >> 1) & M1_32;
    //*w0 = ((*w0 >> 2) & M2_32) + (*w0 & M2_32); // 0-4 in 4 bits
    //*w1 = ((*w1 >> 2) & M2_32) + (*w1 & M2_32);
    //return ((*w0 + *w1) * H4_32) >> 0x1C; // 28;

    uint32_t w0 = uint32_t (bb);
    uint32_t w1 = uint32_t (bb >> 32);
    w0 -= (w0 >> 1) & M1_32;                 // 0-2 in 2 bits
    w1 -= (w1 >> 1) & M1_32;
    w0 = ((w0 >> 2) & M2_32) + (w0 & M2_32); // 0-4 in 4 bits
    w1 = ((w1 >> 2) & M2_32) + (w1 & M2_32);
    return ((w0 + w1) * H4_32) >> 0x1C; // 28;
}

#   endif

#endif

//inline uint8_t pop_count (Bitboard bb)
//{
//    uint8_t count = 0;
//    while (bb)
//    {
//        ++count;
//        bb &= (bb - 1);
//    }
//    return count;
//}


#pragma region Extra

//static const uint8_t   _CountByte[_UI8_MAX + 1] =
//{
//#undef C_6
//#undef C_4
//#undef C_2
//#define C_2(n)      (n),      (n)+1,       (n)+1,       (n)+2
//#define C_4(n)   C_2(n),  C_2((n)+1),  C_2((n)+1),  C_2((n)+2)
//#define C_6(n)   C_4(n),  C_4((n)+1),  C_4((n)+1),  C_4((n)+2)
//    C_6 (0), C_6 (1), C_6 (1), C_6 (2),
//#undef C_6
//#undef C_4
//#undef C_2
//};

//inline uint8_t countBit (uint8_t b)
//{
//    /////"Counting in parallel"
//    //uint8_t M1 = 0x55;
//    //uint8_t M2 = 0x33;
//    //uint8_t M3 = 0x0F;
//    //uint8_t c = (b)  - ((b >> 1) & M1);
//    //c = (c & M2) + ((c >> 2) & M2);
//    ///// using multiplication
//    ////c = (c * 0x11 >> 4) & M3;
//    ///// using addition
//    ////c = (c & M3) + ((c >> 4) & M3);
//    //c = (c & M3) +  (c >> 4);
//    //return c;
//
//    /// Count Lookup
//    return _CountByte[b];
//}

//inline uint8_t pop_count (Bitboard bb)
//{
//    //Bitboard M1 = U64(0x5555555555555555);
//    //Bitboard M2 = U64(0x3333333333333333);
//    ////Bitboard M3 = U64(0x0F0F0F0F0F0F0F0F);
//    //Bitboard bb = (bb) - ((bb >> 1) & M1);
//    //bb = (bb & M2) + ((bb >> 2) & M2);
//    //uint32_t c = bb + (bb >> 32);
//    //c = (c & 0x0F0F0F0F) + ((c >> 4) & 0x0F0F0F0F);
//    //c = (c & 0xFFFF) + (c >> 16);
//    //c = (c & 0xFF) + (c >> 8);
//    //return c;
//
//    // ---
//
//    uint8_t* p = (uint8_t*) (&bb);
//    return
//        countBit (p[0]) +
//        countBit (p[1]) +
//        countBit (p[2]) +
//        countBit (p[3]) +
//        countBit (p[4]) +
//        countBit (p[5]) +
//        countBit (p[6]) +
//        countBit (p[7]);
//}


//static unsigned char wordbits[65536] = { bitcounts of ints between 0 and 65535 };
//inline uint8_t pop_count (uint32_t w)
//{
//    return( wordbits[w & 0xFFFF] + wordbits[w >> 0x10] );
//}

#pragma endregion

#endif
