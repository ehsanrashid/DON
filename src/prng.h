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

#include <cstdint>

namespace DON {

// SplitMix64 is used to initialize the state of the main generator.
// This is the standard, high-quality way to expand a single seed.
class SplitMix64 final {
   public:
    explicit constexpr SplitMix64(std::uint64_t seed = 1ULL) noexcept :
        s(seed != 0 ? seed : 1ULL) {}

    constexpr std::uint64_t next() noexcept {
        s += 0x9E3779B97F4A7C15ULL;

        std::uint64_t t = s;
        t               = (t ^ (t >> 30)) * 0xBF58476D1CE4E5B9ULL;
        t               = (t ^ (t >> 27)) * 0x94D049BB133111EBULL;
        t               = (t ^ (t >> 31));

        return t;
    }

   private:
    std::uint64_t s;
};

constexpr std::uint64_t bit(std::uint8_t b) noexcept { return (1ULL << b); }

// XorShift64* Pseudo-Random Number Generator
// It is based on original code written and dedicated
// to the public domain by Sebastiano Vigna (2014).
// It has the following characteristics:
//
//  - Outputs 64-bit numbers
//  - Passes Dieharder and SmallCrush test batteries
//  - Does not require warm-up, no zero-land to escape
//  - Internal state is a single 64-bit integer
//  - Period is 2^64 - 1
//  - Speed: 1.60 ns/call (measured on a Core i7 @3.40GHz)
//
// For further analysis see
//   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>
class XorShift64Star final {
   public:
    explicit constexpr XorShift64Star(std::uint64_t seed = 1ULL) noexcept :
        s(SplitMix64(seed).next()) {
        // Avoid zero state
        if (s == 0)
            s = 1ULL;
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
        constexpr std::uint64_t JumpMask = 0x9E3779B97F4A7C15ULL;

        std::uint64_t t = 0;

        for (std::uint8_t b = 0; b < 64; ++b)
        {
            if ((JumpMask & bit(b)) != 0)
                t ^= s;

            rand64();
        }

        s = t;
    }

   private:
    // XorShift64* algorithm implementation
    constexpr std::uint64_t rand64() noexcept {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return 0x2545F4914F6CDD1DULL * s;
    }

    std::uint64_t s;
};

}  // namespace DON

#endif  // #ifndef PRNG_H_INCLUDED
