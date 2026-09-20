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
        state(seed) {}

    constexpr u64 next() noexcept {
        update();

        return mix();
    }

   private:
    constexpr void update() noexcept {
        state += u64{0x9E3779B97F4A7C15};  // Derived from the Golden Ratio
    }

    constexpr u64 mix() const noexcept {
        u64 tmp = state;
        tmp     = (tmp ^ (tmp >> 30)) * u64{0xBF58476D1CE4E5B9};
        tmp     = (tmp ^ (tmp >> 27)) * u64{0x94D049BB133111EB};
        tmp     = (tmp ^ (tmp >> 31));

        return tmp;
    }

    u64 state;
};

// Xorshift64* Pseudo-Random Number Generator
//
// Uses Marsaglia's standard 64-bit shift triplet parameters (12, 25, 27).
// Other standard 64-bit shift triplet parameters, such as (13, 7, 17), can also be used.
//
// Based on Sebastiano Vigna's original Xorshift64* generator (2014).
//
// Characteristics:
// - Internal state: single 64-bit integer.
// - Output: 64-bit.
// - Period: 2^64 - 1.
// - Speed: 1.60 ns/call (measured on a Core i7 @ 3.40 GHz).
// - Passes Dieharder and SmallCrush test batteries reported in the original paper.
// - Does not require warm-up; non-zero state is ensured during initialization.
// - Zero-State Insurance: SplitMix64 is used to initialize the internal state.
// - Zero Overhead: No additional memory is required beyond the 64-bit internal state.
//
// See:
//   <https://vigna.di.unimi.it/ftp/papers/xorshift.pdf>
class Xorshift64Star final {
   private:
    using State = u64;

   public:
    explicit constexpr Xorshift64Star(u64 seed = 1) noexcept {
        SplitMix64 seeder(seed);

        // Initialize the state with a mixed SplitMix64 output.
        state = seeder.next();

        // The all-zero state is not valid for Xorshift64*.
        // If SplitMix64 produces zero for state,
        // fall back to a non-zero state.
        if (UNLIKELY(state == 0))
        {
            state = DefaultState;
        }
    }

    template<typename T>
    constexpr T rand() noexcept {
        return T(rand64());
    }

    // Sparse random (1/8 bits set on average).
    template<typename T>
    constexpr T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump ahead by 2^32 steps.
    //
    // This can be used to create independent streams for parallel computations.
    constexpr void jump() noexcept {
        constexpr State Jump =     // Jump under (12, 25, 27) parameters
          u64{0xDD97D02513476FA5}  // Jump by 2^32 steps
        //u64{0xAE82CA9F848EBC6D}  // Jump by 2^48 steps
        ;

        State tmpState = 0;

        for (u8 b = 0; b < 64; ++b)
        {
            if ((Jump & bit(b)) != 0)
            {
                tmpState ^= state;
            }

            // Advance the state through the underlying linear recurrence.
            update();
        }

        state = tmpState;
    }

   private:
    // Advance the Xorshift64* state using the (12, 25, 27) shift parameters.
    constexpr void update() noexcept {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
    }

    // Apply the Xorshift64* output scrambler.
    constexpr u64 mix() const noexcept { return u64{0x2545F4914F6CDD1D} * state; }

    constexpr u64 rand64() noexcept {
        update();

        return mix();
    }

    static constexpr State DefaultState = 1;

    State state = DefaultState;
};

// Xoroshiro128** Pseudo-Random Number Generator
//
// Fast, high-quality 64-bit pseudo-random number generator with a
// 128-bit internal state.
//
// Characteristics:
// - Internal state: 128 bits (two 64-bit integers).
// - Output: 64 bits.
// - Period: 2^128 - 1.
// - Jump function: advances the state by 2^64 steps for parallel streams.
// - Initialization: SplitMix64 is used to initialize the internal state.
// - Zero-State Insurance: SplitMix64 is used to initialize the internal state.
// - The all-zero state is avoided during initialization.
// - State size: 128 bits with no additional generator state.
// - Zero Overhead: No additional memory is required beyond the 128-bit internal state.
//
// Based on:
//   David Blackman and Sebastiano Vigna,
//   "Scrambled Linear Pseudorandom Number Generators"
//   <https://vigna.di.unimi.it/ftp/papers/ScrambledLinear.pdf>
//
// Reference implementation:
//   <https://prng.di.unimi.it/xoroshiro128starstar.c>
class Xoroshiro128StarStar final {
   private:
    static constexpr usize StateSize = 2;

    using State = Array<u64, StateSize>;

   public:
    explicit constexpr Xoroshiro128StarStar(u64 seed = 1) noexcept {
        SplitMix64 seeder(seed);

        // Initialize the state with two SplitMix64 outputs.
        state[0] = seeder.next();
        state[1] = seeder.next();

        // The all-zero state is not valid for Xoroshiro128**.
        // If SplitMix64 produces zero for both state words,
        // fall back to a non-zero state.
        if (UNLIKELY((state[0] | state[1]) == 0))
            state = DefaultState;
    }

    template<typename T>
    constexpr T rand() noexcept {
        return T(rand64());
    }

    // Sparse random (1/8 bits set on average).
    template<typename T>
    constexpr T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump ahead by 2^64 steps.
    //
    // This can be used to create independent streams for parallel computations.
    constexpr void jump() noexcept {
        constexpr State Jump = {0xDF900294D8F554A5, 0x170865DF4B3201FC};

        State tmpState = {0, 0};

        for (const u64 jump : Jump)
        {
            for (u8 b = 0; b < 64; ++b)
            {
                if ((jump & bit(b)) != 0)
                {
                    tmpState[0] ^= state[0];
                    tmpState[1] ^= state[1];
                }

                // Advance the state through the underlying linear recurrence.
                update();
            }
        }

        state = tmpState;
    }

   private:
    // Advance the Xoroshiro128** state using its linear recurrence.
    constexpr void update() noexcept {
        state[1] ^= state[0];

        state[0] = rotl(state[0], 24) ^ state[1] ^ (state[1] << 16);
        state[1] = rotl(state[1], 37);
    }

    // Apply the Xoroshiro128** output scrambler.
    constexpr u64 mix() const noexcept { return rotl(state[0] * 5, 7) * 9; }

    constexpr u64 rand64() noexcept {
        // Generate the output before advancing the state.
        const u64 rand = mix();

        update();

        return rand;
    }

    static constexpr State DefaultState = {1, 0};

    State state = DefaultState;
};

}  // namespace DON

#endif  // PRNG_H_INCLUDED
