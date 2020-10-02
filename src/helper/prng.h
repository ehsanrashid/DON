#pragma once

#include <cassert>
#include <cstdint>

/// XOR Shift64*(Star) Pseudo-Random Number Generator
/// Based on the original code design/written and dedicated
/// to the public domain by Sebastiano Vigna (2014)
///
/// It has the following characteristics:
///
///  -  Outputs 64-bit numbers
///  -  Passes Dieharder and SmallCrush test batteries
///  -  Does not require warm-up, no zeroland to escape
///  -  Internal state is a single 64-bit integer
///  -  Period is 2^64 - 1
///  -  Speed: 1.60 ns/call (Core i7 @3.40GHz)
///
/// For further analysis see
///   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>
class PRNG {

public:

    PRNG(uint64_t seed) noexcept :
        x{ seed } {
        assert(x != 0);
    }
    PRNG() = delete;
    PRNG(PRNG const&) = delete;
    PRNG(PRNG&&) = delete;

    PRNG& operator=(PRNG const&) = delete;
    PRNG& operator=(PRNG&&) = delete;

    template<typename T>
    T rand() noexcept {
        return T(rand64());
    }

//#if !defined(USE_PEXT)
    /// Special generator used to fast initialize magic numbers.
    /// Output values only have 1/8th of their bits set on average.
    template<typename T>
    T sparseRand() noexcept {
        return T(rand64() & rand64() & rand64());
    }
//#endif

private:
    uint64_t rand64() noexcept {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        return x * 0x2545F4914F6CDD1DULL;
    }

    uint64_t x;
};
