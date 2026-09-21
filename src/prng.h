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
class Xorshift64s final {
   private:
    using State = u64;

   public:
    explicit constexpr Xorshift64s(u64 seed = 1) noexcept {
        SplitMix64 seeder(seed);

        // Initialize the state with a mixed SplitMix64 output.
        state = seeder.next();

        // The all-zero state is not valid for Xorshift64*.
        // If SplitMix64 produces zero for state,
        // fall back to a non-zero state.
        if (UNLIKELY(state == 0))
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

    // Jump ahead by 2^32 steps.
    //
    // This can be used to create independent streams for parallel computations.
    constexpr void jump() noexcept {
        constexpr State Jump = u64{0xDD97D02513476FA5};

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

    // Jump ahead by 2^48 steps.
    //
    // This can be used to create distant independent streams for parallel computations.
    constexpr void long_jump() noexcept {
        constexpr State LongJump = u64{0xAE82CA9F848EBC6D};

        State tmpState = 0;

        for (u8 b = 0; b < 64; ++b)
        {
            if ((LongJump & bit(b)) != 0)
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
class Xoroshiro128ss final {
   private:
    static constexpr usize StateSize = 2;

    using State = Array<u64, StateSize>;

   public:
    explicit constexpr Xoroshiro128ss(u64 seed = 1) noexcept {
        SplitMix64 seeder(seed);

        // Initialize the state with two SplitMix64 outputs.
        for (u64& s : state)
            s = seeder.next();

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
        constexpr State Jump = {u64{0xDF900294D8F554A5}, u64{0x170865DF4B3201FC}};

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

    // Jump ahead by 2^96 steps.
    //
    // This can be used to create distant independent streams for parallel computations.
    constexpr void long_jump() noexcept {
        constexpr State LongJump = {u64{0xD2A98B26625EEE7B}, u64{0xDDDF9B1090AA7AC1}};

        State tmpState = {0, 0};

        for (const u64 longJump : LongJump)
        {
            for (u8 b = 0; b < 64; ++b)
            {
                if ((longJump & bit(b)) != 0)
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

// Xoshiro256** Pseudorandom Number Generator
//
// Fast, high-quality 64-bit pseudorandom number generator with a
// 256-bit internal state.
//
// Characteristics:
// - Internal state: 256 bits (four 64-bit integers).
// - Output: 64 bits.
// - Period: 2^256 - 1.
// - Jump function: advances the state by 2^128 steps for parallel streams.
// - Long-jump function: advances the state by 2^192 steps for parallel streams.
// - Initialization: SplitMix64 is used to initialize the internal state.
// - Zero-State Insurance: SplitMix64 is used to initialize the internal state.
// - The all-zero state is avoided during initialization.
// - State size: 256 bits with no additional generator state.
// - Zero Overhead: No additional memory is required beyond the 256-bit internal state.
//
// Based on:
//   David Blackman and Sebastiano Vigna,
//   "Scrambled Linear Pseudorandom Number Generators"
//   <https://vigna.di.unimi.it/ftp/papers/ScrambledLinear.pdf>
//
// Reference implementation:
//   <https://prng.di.unimi.it/xoshiro256starstar.c>
class Xoshiro256ss final {
    static constexpr usize StateSize = 4;

    using State = Array<u64, StateSize>;

   public:
    explicit constexpr Xoshiro256ss(u64 seed = 1) noexcept {
        SplitMix64 seeder(seed);

        // Initialize the state with four SplitMix64 outputs.
        for (u64& s : state)
            s = seeder.next();

        // The all-zero state is not valid for Xoshiro256**.
        // If SplitMix64 produces zero for all state words,
        // fall back to a non-zero state.
        if (UNLIKELY((state[0] | state[1] | state[2] | state[3]) == 0))
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

    // Jump ahead by 2^128 steps.
    //
    // This can be used to create independent streams for parallel computations.
    constexpr void jump() noexcept {
        constexpr State Jump = {u64{0x180EC6D33CFD0ABA}, u64{0xD5A61266F0C9392C},
                                u64{0xA9582618E03FC9AA}, u64{0x39ABDC4529B1661C}};

        State tmpState = {0, 0, 0, 0};

        for (const u64 jump : Jump)
        {
            for (u8 b = 0; b < 64; ++b)
            {
                if ((jump & bit(b)) != 0)
                {
                    tmpState[0] ^= state[0];
                    tmpState[1] ^= state[1];
                    tmpState[2] ^= state[2];
                    tmpState[3] ^= state[3];
                }

                // Advance the state through the underlying linear recurrence.
                update();
            }
        }

        state = tmpState;
    }

    // Long-jump ahead by 2^192 steps.
    //
    // This can be used to create distant independent streams for parallel computations.
    constexpr void long_jump() noexcept {
        constexpr State LongJump = {u64{0x76E15D3EFEFDCBBF}, u64{0xC5004E441C522FB3},
                                    u64{0x77710069854EE241}, u64{0x39109BB02ACBE635}};

        State tmpState = {0, 0, 0, 0};

        for (const u64 longJump : LongJump)
        {
            for (u8 b = 0; b < 64; ++b)
            {
                if ((longJump & bit(b)) != 0)
                {
                    tmpState[0] ^= state[0];
                    tmpState[1] ^= state[1];
                    tmpState[2] ^= state[2];
                    tmpState[3] ^= state[3];
                }

                // Advance the state through the underlying linear recurrence.
                update();
            }
        }

        state = tmpState;
    }

   private:
    // Advance the Xoshiro256** state using its linear recurrence.
    constexpr void update() noexcept {
        const u64 tmp = state[1] << 17;

        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];

        state[2] ^= tmp;

        state[3] = rotl(state[3], 45);
    }

    // Apply the Xoshiro256** output scrambler.
    constexpr u64 mix() const noexcept { return rotl(state[1] * 5, 7) * 9; }

    constexpr u64 rand64() noexcept {
        // Generate the output before advancing the state.
        const u64 rand = mix();

        update();

        return rand;
    }

    static constexpr State DefaultState = {1, 0, 0, 0};

    State state = DefaultState;
};

}  // namespace DON

#endif  // PRNG_H_INCLUDED
