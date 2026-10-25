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

#ifndef UTILITY_PRNG_H_INCLUDED
#define UTILITY_PRNG_H_INCLUDED

#include "../utility/types.h"

namespace DON {

constexpr u64 rotl(const u64 x, const u8 k) noexcept {
    const u8 kk = k & 63;
    return (x << kk) | (x >> ((64 - kk) & 63));
}

constexpr u64 rotr(const u64 x, const u8 k) noexcept {
    const u8 kk = k & 63;
    return (x >> kk) | (x << ((64 - kk) & 63));
}

// SplitMix64 Pseudo-Random Number Generator
//
// Used to initialize the state of the main generator.
// Provides high-quality output from a single 64-bit seed.
// Classic fixed-increment generator with a fixed Weyl sequence.
//
// Characteristics:
// - Overcomes Poor Seeding: Even low-entropy seeds (like 1 or a sequential counter)
//   are thoroughly mixed so the main generator starts with a highly well-distributed state.
// - Avoids Zero-State Trap: Its output is used to seed xorshift64*, whose zero state
//   is explicitly replaced with 1 if necessary.
//
// used widely across many modern libraries,
// (including the Java 8 SplittableRandom and the standard initialization routines by Vigna)
class SplitMix64 final {
   public:
    explicit SplitMix64(u64 seed) noexcept;

    u64 next() noexcept;

   private:
    void update() noexcept;

    u64 mix() const noexcept;

    u64 state;
};

// xorshift64* Pseudo-Random Number Generator
//
// Uses Marsaglia's standard 64-bit shift triplet parameters (12, 25, 27).
// Other standard 64-bit shift triplet parameters, such as (13, 7, 17), can also be used.
//
// Based on Sebastiano Vigna's original xorshift64* generator (2014).
//
// Characteristics:
// - Internal state: single 64-bit integer.
// - Output: 64-bit.
// - Period: 2^64 - 1.
// - Jump function: advances the state by 2^32 steps for parallel streams.
// - Long-jump function: advances the state by 2^48 steps for parallel streams.
// - Speed: 1.60 ns/call (measured on a Core i7 @ 3.40 GHz).
// - Passes Dieharder and SmallCrush test batteries reported in the original paper.
// - The * scrambler improves output quality, but xorshift64* still has known
//   statistical weaknesses (including failures in BigCrush tests).
// - Warm-up: Not required; the state is initialized with non-zero values.
// - Zero-State Insurance: SplitMix64 initializes the state, avoiding the all-zero state.
// - Zero Overhead: No additional memory is required beyond the 64-bit internal state.
//
// See:
//   <https://vigna.di.unimi.it/ftp/papers/xorshift.pdf>
class Xorshift64s final {
   public:
    static constexpr usize StateSize = 1;

    using State = Array<u64, StateSize>;

    explicit Xorshift64s(u64 seed) noexcept;

    template<typename T>
    T rand() noexcept {
        return T(rand64());
    }

    // Sparse random (1/8 bits set on average).
    template<typename T>
    T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump ahead by 2^32 state transitions.
    //
    // This can be used to create independent streams for parallel computations.
    void jump() noexcept;

    // Jump ahead by 2^48 state transitions.
    //
    // This can be used to create distant independent streams for parallel computations.
    void long_jump() noexcept;

   private:
    // Advance the xorshift64* state using the (12, 25, 27) shift parameters.
    void update() noexcept;

    // Apply the xorshift64* output scrambler.
    u64 mix() const noexcept;

    u64 rand64() noexcept;

    static constexpr State DefaultState = {1};

    State state = DefaultState;
};

// xoroshiro128** Pseudo-Random Number Generator
//
// Fast, high-quality 64-bit pseudo-random number generator with a
// 128-bit internal state.
//
// Characteristics:
// - Internal state: 128 bits (two 64-bit integers).
// - Output: 64 bits.
// - Period: 2^128 - 1.
// - Jump function: advances the state by 2^64 steps for parallel streams.
// - Long-jump function: advances the state by 2^96 steps for parallel streams.
// - Initialization: SplitMix64 is used to initialize the internal state.
// - Warm-up: Not required.
// - Zero-State Insurance: SplitMix64 initializes the state, avoiding the all-zero state.
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
   public:
    static constexpr usize StateSize = 2;

    using State = Array<u64, StateSize>;

    explicit Xoroshiro128ss(u64 seed) noexcept;

    template<typename T>
    T rand() noexcept {
        return T(rand64());
    }

    // Sparse random (1/8 bits set on average).
    template<typename T>
    T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump ahead by 2^64 state transitions.
    //
    // This can be used to create independent streams for parallel computations.
    void jump() noexcept;

    // Jump ahead by 2^96 state transitions.
    //
    // This can be used to create distant independent streams for parallel computations.
    void long_jump() noexcept;

   private:
    // Advance the xoroshiro128** state using its linear recurrence.
    void update() noexcept;

    // Apply the xoroshiro128** output scrambler.
    u64 mix() const noexcept;

    u64 rand64() noexcept;

    static constexpr State DefaultState = {1, 0};

    State state = DefaultState;
};

// xoshiro256** Pseudo-Random Number Generator
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
// - Warm-up: Not required.
// - Zero-State Insurance: SplitMix64 initializes the state, avoiding the all-zero state.
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
   public:
    static constexpr usize StateSize = 4;

    using State = Array<u64, StateSize>;

    explicit Xoshiro256ss(u64 seed) noexcept;

    template<typename T>
    T rand() noexcept {
        return T(rand64());
    }

    // Sparse random (1/8 bits set on average).
    template<typename T>
    T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump ahead by 2^128 state transitions.
    //
    // This can be used to create independent streams for parallel computations.
    void jump() noexcept;

    // Long-jump ahead by 2^192 state transitions.
    //
    // This can be used to create distant independent streams for parallel computations.
    void long_jump() noexcept;

   private:
    // Advance the xoshiro256** state using its linear recurrence.
    void update() noexcept;

    // Apply the xoshiro256** output scrambler.
    u64 mix() const noexcept;

    u64 rand64() noexcept;

    static constexpr State DefaultState = {1, 0, 0, 0};

    State state = DefaultState;
};

// xoshiro512** Pseudo-Random Number Generator
//
// Fast, high-quality 64-bit pseudorandom number generator with a
// 512-bit internal state.
//
// Characteristics:
// - Output: 64 bits.
// - Period: 2^512 - 1.
// - Jump function: advances the state by 2^256 steps.
// - Long-jump function: advances the state by 2^384 steps.
// - Initialization: SplitMix64 is used to initialize the internal state.
// - The all-zero state is invalid.
//
// Based on:
//   David Blackman and Sebastiano Vigna,
//   "Scrambled Linear Pseudorandom Number Generators"
//   <https://vigna.di.unimi.it/ftp/papers/ScrambledLinear.pdf>
//
// Reference implementation:
//   <https://prng.di.unimi.it/xoshiro512starstar.c>
class Xoshiro512ss final {
   public:
    static constexpr usize StateSize = 8;

    using State = Array<u64, StateSize>;

    explicit Xoshiro512ss(u64 seed) noexcept;

    template<typename T>
    T rand() noexcept {
        return T(rand64());
    }

    // Sparse random (1/8 bits set on average).
    template<typename T>
    T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump ahead by 2^256 state transitions.
    void jump() noexcept;

    // Jump ahead by 2^384 state transitions.
    void long_jump() noexcept;

   private:
    // Advance the xoshiro512** state using its linear recurrence.
    void update() noexcept;

    // Apply the xoshiro512** output scrambler.
    u64 mix() const noexcept;

    u64 rand64() noexcept;

    static constexpr State DefaultState = {1, 0, 0, 0, 0, 0, 0, 0};

    State state = DefaultState;
};

// xoshiro1024** Pseudo-Random Number Generator
//
// Fast, high-quality 64-bit pseudorandom number generator with a
// 1024-bit internal state.
//
// Characteristics:
// - Output: 64 bits.
// - Period: 2^1024 - 1.
// - Jump function: advances the state by 2^512 steps.
// - Long-jump function: advances the state by 2^384 steps.
// - Initialization: SplitMix64 is used to initialize the internal state.
// - The all-zero state is invalid.
//
// Based on:
//   David Blackman and Sebastiano Vigna,
//   "Scrambled Linear Pseudorandom Number Generators"
//   <https://vigna.di.unimi.it/ftp/papers/ScrambledLinear.pdf>
//
// Reference implementation:
//   <https://prng.di.unimi.it/xoshiro1024starstar.c>
class Xoshiro1024ss final {
   public:
    static constexpr usize StateSize = 16;
    static constexpr usize StateMask = StateSize - 1;

    using State = Array<u64, StateSize>;

    explicit Xoshiro1024ss(u64 seed) noexcept;

    template<typename T>
    T rand() noexcept {
        return T(rand64());
    }

    // Sparse random (1/8 bits set on average).
    template<typename T>
    T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump ahead by 2^512 state transitions.
    void jump() noexcept;

    // Jump ahead by 2^768 state transitions.
    void long_jump() noexcept;

   private:
    // Advance the xoshiro1024** state using its linear recurrence.
    void update() noexcept;

    // Apply the xoshiro1024** output scrambler.
    u64 mix() const noexcept;

    u64 rand64() noexcept;

    static constexpr State DefaultState = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    State state = DefaultState;
    usize p     = 0;
};

}  // namespace DON

#endif  // UTILITY_PRNG_H_INCLUDED
