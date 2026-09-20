/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef PRNG_H_INCLUDED
#define PRNG_H_INCLUDED

#include "misc.h"

namespace DON {

constexpr u64 rotl(const u64 x, const u8 k) noexcept { return (x << k) | (x >> ((64 - k) & 63)); }

constexpr u64 rotr(const u64 x, const u8 k) noexcept { return (x >> k) | (x << ((64 - k) & 63)); }

// SplitMix64 Pseudo-Random Number Generator
//
// Used to initialize the state of the main generator.
// Provides high-quality output from a single 64-bit seed.
// Classic fixed-increment generator with a fixed Weyl sequence.
//
// Characteristics:
// - Overcomes Poor Seeding: Even low-entropy seeds (like 1 or a sequential counter)
//   are thoroughly mixed so the main generator starts with a highly well-distributed state.
// - Avoids Zero-State Trap: Its output is used to seed XorShift64*, whose zero state
//   is explicitly replaced with 1 if necessary.
//
// used widely across many modern libraries,
// (including the Java 8 SplittableRandom and the standard initialization routines by Vigna)
class SplitMix64 final {
   public:
    explicit constexpr SplitMix64(u64 seed = 1) noexcept :
        s(seed) {}

    constexpr u64 next() noexcept {
        update();

        return mix();
    }

   private:
    constexpr void update() noexcept {
        s += u64{0x9E3779B97F4A7C15};  // Derived from the Golden Ratio
    }

    constexpr u64 mix() const noexcept {
        u64 t = s;
        t     = (t ^ (t >> 30)) * u64{0xBF58476D1CE4E5B9};
        t     = (t ^ (t >> 27)) * u64{0x94D049BB133111EB};
        t     = (t ^ (t >> 31));

        return t;
    }

    u64 s;
};

// XorShift64* Pseudo-Random Number Generator
//
// using Marsaglia's standard 64-bit shift triplet parameters (12, 25, 27)
// can use other standard 64-bit shift triplet parameters (13, 7, 17)
//
// Based on Sebastiano Vigna's original xorshift64* generator (2014).
//
// Characteristics:
// - Internal state: single 64-bit integer.
// - Output: 64-bit.
// - Period: 2^64 - 1.
// - Speed: 1.60 ns/call (measured on a Core i7 @3.40 GHz)
// - Passes Dieharder and SmallCrush test batteries.
// - Does not require warm-up; nonzero state is ensured during initialization.
// - Zero-State Insurance: SplitMix64 generator used produce non-zero state.
// - Zero Overhead: it leaves absolutely zero memory footprint.
//
// See:
//   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>
class XorShift64Star final {
   public:
    explicit constexpr XorShift64Star(u64 seed = 1) noexcept {
        SplitMix64 seeder(seed);

        // Populate the main state with mixed entropy
        s = seeder.next();

        // Zero-State Insurance:
        // Safety check: avoid the absorbing zero state
        // XorShift states must non-zero, never be 0.
        // If SplitMix64 returns 0, fall back to a non-zero state.
        if (UNLIKELY(s == 0))
        {
            s = DefaultState;
        }
    }

    template<typename T>
    constexpr T rand() noexcept {
        return T(rand64());
    }

    // Sparse random (1/8 bits set on average)
    template<typename T>
    constexpr T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // XorShift64* jump implementation
    constexpr void jump() noexcept {
        constexpr u64 JumpMask =   // Jump under (12, 25, 27) parameters
          u64{0XDD97D02513476FA5}  // Jump by 2^32 steps (Useful for partitioning parallel streams)
        //u64{0XAE82CA9F848EBC6D}  // Jump by 2^48 steps
        ;

        u64 t = 0;

        for (u8 b = 0; b < 64; ++b)
        {
            if ((JumpMask & bit(b)) != 0)
            {
                t ^= s;
            }

            // Advances state over the linear system
            update();
        }

        s = t;
    }

   private:
    // XorShift64* algorithm implementation
    constexpr void update() noexcept {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
    }

    constexpr u64 mix() const noexcept { return u64{0x2545F4914F6CDD1D} * s; }

    constexpr u64 rand64() noexcept {
        update();

        return mix();
    }

    static constexpr u64 DefaultState = 1;  // u64{0x5555555555555555}

    u64 s = DefaultState;
};

}  // namespace DON

#endif  // PRNG_H_INCLUDED
