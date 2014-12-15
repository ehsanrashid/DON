#ifndef _PRNG_H_INC_
#define _PRNG_H_INC_

#include "Type.h"

// xorshift64* pseudo-random number generator
// Designed and placed into the public domain by Sebastiano Vigna
// For analysis see http://vigna.di.unimi.it/ftp/papers/xorshift.pdf

class PRNG
{

private:

    u64 s;

    u64 rand64 ()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * U64(0x2545F4914F6CDD1D);
    }

public:

    PRNG (u64 seed) : s (seed) { assert (seed != 0); }

    template<typename T> T rand () { return T(rand64 ()); }

    /// Special generator used to fast init magic numbers.
    /// Output values only have 1/8th of their bits set on average.
    template<typename T> T sparse_rand () { return T(rand64 () & rand64 () & rand64 ()); }
};

#endif // _PRNG_H_INC_
