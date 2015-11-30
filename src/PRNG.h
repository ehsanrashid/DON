#ifndef _PRNG_H_INC_
#define _PRNG_H_INC_

#include "Type.h"

// XOR Shift64*(Star) Pseudo-Random Number Generator
// Based on the original code design/written and dedicated
// to the public domain by Sebastiano Vigna (2014)
//
// It has the following characteristics:
//
//  -  Outputs 64-bit numbers
//  -  Passes Dieharder and SmallCrush test batteries
//  -  Does not require warm-up, no zeroland to escape
//  -  Internal state is a single 64-bit integer
//  -  Period is 2^64 - 1
//  -  Speed: 1.60 ns/call (Core i7 @3.40GHz)
//
// For further analysis see
//   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>

class PRNG
{

private:

    u64 s = U64(0);

    u64 rand64 ()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * U64(0x2545F4914F6CDD1D);
    }

public:

    PRNG () = delete;
    PRNG (u64 seed) : s (seed) { assert(seed != 0); }

    template<class T>
    T rand () { return T(rand64 ()); }

    // Special generator used to fast initialize magic numbers.
    // Output values only have 1/8th of their bits set on average.
    template<class T>
    T sparse_rand () { return T(rand64 () & rand64 () & rand64 ()); }
};

#endif // _PRNG_H_INC_
