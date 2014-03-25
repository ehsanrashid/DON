#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _BITCOUNT_H_INC_
#define _BITCOUNT_H_INC_

#include "Type.h"

#ifdef _MSC_VER
#   pragma warning (disable: 4244) // 'argument' : conversion from '-' to '-', possible loss of data
#endif

enum BitCountT
{
    CNT_64_FULL,
    CNT_64_MAX15,
    CNT_32_FULL,
    CNT_32_MAX15,
    CNT_HW_POPCNT

};


template<BitCountT>
// pop_count () counts the number of set bits in a Bitboard
INLINE u08 pop_count (Bitboard bb);

// Determine at compile time the best pop_count<> specialization 
// according if platform is 32 or 64 bit,
// the maximum number of nonzero bits to count
// and if hardware popcnt instruction is available.

#ifdef POPCNT

const BitCountT FULL  = CNT_HW_POPCNT;
const BitCountT MAX15 = CNT_HW_POPCNT;

#ifdef _MSC_VER

#   ifdef __INTEL_COMPILER

#       include <nmmintrin.h> 
// Intel header for  SSE4.1 or SSE4.2 intrinsics.
// _mm_popcnt_u64() & _mm_popcnt_u32()

template<>
INLINE u08 pop_count<CNT_HW_POPCNT> (Bitboard bb)
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

#       include <intrin.h> // MSVC popcnt and bsfq instrinsics __popcnt64() & __popcnt()

template<>
INLINE u08 pop_count<CNT_HW_POPCNT> (Bitboard bb)
{
#      ifdef _64BIT
    {
        return (__popcnt64 (bb));
    }
#      else
    {
        return (__popcnt (bb) + __popcnt (bb >> 32));
    }
#      endif
}

#   endif

//#elif defined(_WIN32) && defined(_64BIT)
//
//#   include <intrin.h> // MSVC popcnt and  __popcnt()
//
//template<>
//INLINE u08 pop_count<CNT_HW_POPCNT> (Bitboard bb)
//{
//    return (__m64_popcnt (bb));
//}

#else

template<>
INLINE u08 pop_count<CNT_HW_POPCNT> (Bitboard bb)
{
    // Assembly code by Heinz van Saanen
    __asm__ ("popcnt %1, %0" : "=r" (bb) : "r" (bb));
    return bb;
}

#endif


#else   // BY Calculation

#   ifdef _64BIT

const BitCountT FULL  = CNT_64_FULL;
const BitCountT MAX15 = CNT_64_MAX15;

namespace {

    const Bitboard M1_64 = U64 (0x5555555555555555);
    const Bitboard M2_64 = U64 (0x3333333333333333);
    const Bitboard M4_64 = U64 (0x0F0F0F0F0F0F0F0F);
    const Bitboard MX_64 = U64 (0x2222222222222222);
    const Bitboard H4_64 = U64 (0x1111111111111111);
    const Bitboard H8_64 = U64 (0x0101010101010101);

}

template<>
// Pop count of the Bitboard (64-bit)
INLINE u08 pop_count<CNT_64_FULL> (Bitboard bb)
{
    //u64 w0 = (bb & MX_64) + ((bb + bb) & MX_64);
    //u64 w1 = (bb >> 1 & MX_64) + (bb >> 2 & MX_64);
    //w0 = w0 + (w0 >> 4) & M4_64;
    //w1 = w1 + (w1 >> 4) & M4_64;
    //return ((w0 + w1) * H8_64) >> 0x39;     // 57;

    bb -= (bb >> 1) & M1_64;
    bb = ((bb >> 2) & M2_64) + (bb & M2_64);
    bb = ((bb >> 4) + bb) & M4_64;
    return (bb * H8_64) >> 0x38;             // 56;
}

template<>
// Pop count max 15 of the Bitboard (64-bit)
INLINE u08 pop_count<CNT_64_MAX15> (Bitboard bb)
{
    bb -= (bb >> 1) & M1_64;
    bb = ((bb >> 2) & M2_64) + (bb & M2_64);
    return (bb * H4_64) >> 0x3C;            // 60;
}

#   else

const BitCountT FULL  = CNT_32_FULL;
const BitCountT MAX15 = CNT_32_MAX15;

namespace {

    const u32 M1_32 = U32 (0x55555555);
    const u32 M2_32 = U32 (0x33333333);
    const u32 M4_32 = U32 (0x0F0F0F0F);
    const u32 H4_32 = U32 (0x11111111);
    const u32 H8_32 = U32 (0x01010101);

}

template<>
// Pop count of the Bitboard (32-bit)
INLINE u08 pop_count<CNT_32_FULL> (Bitboard bb)
{
    //u32 *p = (u32*) (&bb);
    //u32 *w0 = p+0;
    //u32 *w1 = p+1;
    //*w0 -= (*w0 >> 1) & M1_32;                 // 0-2 in 2 bits
    //*w1 -= (*w1 >> 1) & M1_32;
    //*w0 = ((*w0 >> 2) & M2_32) + (*w0 & M2_32);// 0-4 in 4 bits
    //*w1 = ((*w1 >> 2) & M2_32) + (*w1 & M2_32);
    //*w0 = ((*w0 >> 4) + *w0) & M4_32;
    //*w1 = ((*w1 >> 4) + *w1) & M4_32;
    //return ((*w0 + *w1) * H8_32) >> 0x18;      // 24;

    u32 w0 = u32 (bb);
    u32 w1 = u32 (bb >> 32);
    w0 -= (w0 >> 1) & M1_32;                 // 0-2 in 2 bits
    w1 -= (w1 >> 1) & M1_32;
    w0 = ((w0 >> 2) & M2_32) + (w0 & M2_32);// 0-4 in 4 bits
    w1 = ((w1 >> 2) & M2_32) + (w1 & M2_32);
    w0 = ((w0 >> 4) + w0) & M4_32;
    w1 = ((w1 >> 4) + w1) & M4_32;
    return ((w0 + w1) * H8_32) >> 0x18;      // 24;
}

template<>
// Pop count max 15 of the Bitboard (32-bit)
INLINE u08 pop_count<CNT_32_MAX15> (Bitboard bb)
{
    //u32 *p = (u32*) (&bb);
    //u32 *w0 = p+0;
    //u32 *w1 = p+1;
    //*w0 -= (*w0 >> 1) & M1_32;                 // 0-2 in 2 bits
    //*w1 -= (*w1 >> 1) & M1_32;
    //*w0 = ((*w0 >> 2) & M2_32) + (*w0 & M2_32);// 0-4 in 4 bits
    //*w1 = ((*w1 >> 2) & M2_32) + (*w1 & M2_32);
    //return ((*w0 + *w1) * H4_32) >> 0x1C;      // 28;

    u32 w0 = u32 (bb);
    u32 w1 = u32 (bb >> 32);
    w0 -= (w0 >> 1) & M1_32;                 // 0-2 in 2 bits
    w1 -= (w1 >> 1) & M1_32;
    w0 = ((w0 >> 2) & M2_32) + (w0 & M2_32);// 0-4 in 4 bits
    w1 = ((w1 >> 2) & M2_32) + (w1 & M2_32);
    return ((w0 + w1) * H4_32) >> 0x1C;      // 28;
}

#   endif

#endif

#endif // _BITCOUNT_H_INC_
