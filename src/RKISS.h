//#pragma once
#ifndef RKISS_H_
#define RKISS_H_

#include "Type.h"
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
// - small noncryptographic PRNG approach is suited for Zobrist Hashing.
// http://chessprogramming.wikispaces.com/Bob+Jenkins
typedef class RKISS
{

private:

    uint64_t A, B, C, D;

    void initialize (uint32_t seed);

public:

    RKISS ()
    {
        // Make random number generation less deterministic by using random seed
        uint32_t seed = Time::now () % 10000;

        initialize (seed);
    }

    RKISS (uint32_t seed)
    {
        initialize (seed);
    }

    //static uint64_t rand64 (uint64_t &A, uint64_t &B, uint64_t &C, uint64_t &D);

    uint64_t rand64 ();

    template<class T>
    T rand ();

    template<class T>
    T magic_rand (uint16_t s);

} RKISS;


#include "BitBoard.h"

// initialize given seed and scramble a few rounds
inline void RKISS::initialize (uint32_t seed)
{
    A = U64 (0xF1EA5EED);
    B = C = D = U64 (0xD4E12C77);

    // PRNG sequence should be not deterministic
    // Scramble a few rounds
    uint32_t round = (seed % 1000);
    for (uint32_t i = 0; i < round; ++i)
    {
        rand64 ();
    }
}

//// Return 64 bit unsigned integer in between [0, 2^64 - 1]
//inline uint64_t RKISS::rand64 (uint64_t &A, uint64_t &B, uint64_t &C, uint64_t &D)
//{
//    using namespace BitBoard;
//
//    uint64_t E;
//    E = A - rotate_L (B, 7);
//    A = B ^ rotate_L (C, 13);
//    B = C + rotate_L (D, 37);
//    C = D + E;
//    return D = E + A;
//}

inline uint64_t RKISS::rand64 ()
{
    //return rand64 (A, B, C, D);

    using namespace BitBoard;

    uint64_t E;
    E = A - rotate_L (B, 7);
    A = B ^ rotate_L (C, 13);
    B = C + rotate_L (D, 37);
    C = D + E;
    return D = E + A;
}

template<class T>
inline T RKISS::rand () { return T (rand64 ()); }

template<class T>
// Special generator used to fast initialize magic numbers.
// Here the trick is to rotate the randoms of a given quantity 's'
// known to be optimal to quickly find a good magic candidate.
inline T RKISS::magic_rand (uint16_t s)
{
    using namespace BitBoard;

    return rotate_L (rotate_L (rand<T>(), (s >> 0) & 0x3F) & rand<T>()
        ,                                 (s >> 6) & 0x3F) & rand<T>();
}

#endif // RKISS_H_
