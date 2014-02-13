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

public:
    // Rand keep variables always together
    typedef struct Rand { uint64_t A, B, C, D; } Rand;

private:
    Rand S;

    void init (uint32_t seed);

public:

    RKISS ()
    {
        // Make random number generation less deterministic
        // by using random seed

        //srand (uint32_t (time (NULL)));
        //uint32_t seed = rand ();

        uint32_t seed = uint64_t (Time::now ()) % 10000;

        init (seed);
    }

    RKISS (uint32_t seed)
    {
        init (seed);
    }

    static uint64_t rand64 (Rand &S);

    uint64_t rand64 ();

    template<class T>
    T randX ();

    template<class T>
    T rand_boost (uint16_t s);

} RKISS;


#include "BitRotate.h"

// initialize given seed and scramble a few rounds
inline void RKISS::init (uint32_t seed)
{
    S.A = U64 (0xF1EA5EED);
    S.B = S.C = S.D = U64 (0xD4E12C77);

    // PRNG sequence should be not deterministic
    // Scramble a few rounds
    uint32_t round = (seed % 1000);
    for (uint32_t i = 0; i < round; ++i) rand64 ();
}

// Return 64 bit unsigned integer in between [0, 2^64 - 1]
inline uint64_t RKISS::rand64 (Rand &S)
{
    const uint64_t
        E = S.A - rotate_L (S.B, 7);
    S.A = S.B ^ rotate_L (S.C, 13);
    S.B = S.C + rotate_L (S.D, 37);
    S.C = S.D + E;
    S.D = E + S.A;
    return S.D;
}

inline uint64_t RKISS::rand64 () { return rand64 (S); }

template<class T>
inline T RKISS::randX () { return T (rand64 ()); }

template<class T>
// Special generator used to fast init magic numbers.
// Here the trick is to rotate the randoms of a given quantity 's'
// known to be optimal to quickly find a good magic candidate.
inline T RKISS::rand_boost (uint16_t s)
{
    //uint8_t s1 = (s >> 0) & 0x3F;
    //uint8_t s2 = (s >> 6) & 0x3F;
    //T r;
    //r = randX<T> ();
    //r = rotate_R (r, s1);
    //r &= randX<T> ();
    //r = rotate_R (r, s2);
    //return T (r & randX<T> ());

    //return rotate_R (rotate_R (randX<T>(), (s >> 0) & 0x3F) & randX<T>()
    //    ,                                  (s >> 6) & 0x3F) & randX<T>();

    return rotate_L (rotate_L (randX<T>(), (s >> 0) & 0x3F) & randX<T>()
        ,                                  (s >> 6) & 0x3F) & randX<T>();
}

#endif // RKISS_H_
