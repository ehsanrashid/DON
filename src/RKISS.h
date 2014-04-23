#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _RKISS_H_INC_
#define _RKISS_H_INC_

#include "Platform.h"
#include "Time.h"

// RKISS is our pseudo random number generator (PRNG) used to compute hash keys.
// George Marsaglia invented the RNG-Kiss-family in the early 90's.
// This is a specific version that Heinz van Saanen derived from some public domain code by Bob Jenkins.
// Following the feature list, as tested by Heinz.
// A small "keep it simple and stupid" RNG with some fancy merits:
//
// - Quite platform independent
// - Passes ALL dieharder tests! Here *nix sys-rand() e.g. fails miserably:-)
// - ~12 times faster than my *nix sys-rand()
// - ~4 times faster than SSE2-version of Mersenne twister
// - Average cycle length: ~2^126
// - 64 bit seed
// - Return doubles with a full 53 bit mantissa
// - Thread safe
// - Small noncryptographic PRNG approach is suited for Zobrist Hashing.
// http://chessprogramming.wikispaces.com/Bob+Jenkins
class RKISS
{

private:

    u64 A, B, C, D;

    void initialize (u32 seed);

public:

    RKISS ()
    {
        // Make random number generation less deterministic by using random seed
        u32 seed = Time::now () % 10000;

        initialize (seed);
    }

    RKISS (u32 seed)
    {
        initialize (seed);
    }

    //static u64 rand64 (u64 &A, u64 &B, u64 &C, u64 &D);

    u64 rand64 ();

    template<class T>
    T rand ();

    template<class T>
    T magic_rand (u16 s);

};

#include "BitBoard.h"

// initialize given seed and scramble a few rounds
inline void RKISS::initialize (u32 seed)
{
    A = U64 (0xF1EA5EED);
    B =
    C =
    D = U64 (0xD4E12C77);

    // PRNG sequence should be not deterministic
    // Scramble a few rounds
    u16 round = (seed % 1000);
    for (u16 i = 0; i < round; ++i)
    {
        rand64 ();
    }
}

//// Return 64 bit unsigned integer in between [0, 2^64 - 1]
//inline u64 RKISS::rand64 (u64 &A, u64 &B, u64 &C, u64 &D)
//{
//    u64 E;
//    E = A - BitBoard::rotate_L (B, 7);
//    A = B ^ BitBoard::rotate_L (C, 13);
//    B = C + BitBoard::rotate_L (D, 37);
//    C = D + E;
//    return D = E + A;
//}

inline u64 RKISS::rand64 ()
{
    //return rand64 (A, B, C, D);
    u64 E;
    E = A - BitBoard::rotate_L (B,  7);
    A = B ^ BitBoard::rotate_L (C, 13);
    B = C + BitBoard::rotate_L (D, 37);
    C = D + E;
    return D = E + A;
}

template<class T>
inline T RKISS::rand ()
{
    return T (rand64 ());
}

template<class T>
// Special generator used to fast initialize magic numbers.
// Here the trick is to rotate the randoms of a given quantity 's'
// known to be optimal to quickly find a good magic candidate.
inline T RKISS::magic_rand (u16 s)
{
    return BitBoard::rotate_L (BitBoard::rotate_L (rand<T> (), (s >> 0) & 0x3F) & rand<T> ()
                                                             , (s >> 6) & 0x3F) & rand<T> ();
}

#endif // _RKISS_H_INC_
