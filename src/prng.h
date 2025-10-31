/*
  DON, a UCI chess playing engine derived from Stockfish

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

#include <cstddef>
#include <cstdint>

// Bitwise rotate left
constexpr std::uint64_t rotl(std::uint64_t x, unsigned k) noexcept {
    k &= 63;
    return (x << k) | (x >> (64 - k));
}

// Bitwise rotate right
constexpr std::uint64_t rotr(std::uint64_t x, unsigned k) noexcept {
    k &= 63;
    return (x >> k) | (x << (64 - k));
}

// SplitMix64 is used to initialize the state of the main generator.
// This is the standard, high-quality way to expand a single seed.
class SplitMix64 final {
   public:
    explicit constexpr SplitMix64(std::uint64_t seed = 1ULL) noexcept :
        s{seed != 0 ? seed : 1ULL} {}

    constexpr std::uint64_t next() noexcept {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);

        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

   private:
    std::uint64_t s;
};

// KISS (Keep It Simple, Stupid) Pseudo-Random Number Generator
// This class is based on original code written and dedicated
// to the public domain by George Marsaglia (1999).
// George Marsaglia died in 2011.
// This is specifc version derived by Heinz van Saanen (2006).
// It has the following characteristics:
//
//  - Outputs 64-bit numbers
//  - Passes Diehard and BigCrush test batteries
//  - Does not require warm-up, no zero-land to escape
//  - Internal state is four 64-bit integers
//  - Period is approximately 2^250
//  - Average cycle length: ~2^126
//  - Speed: 1.78 ns/call (measured on a Core i7 @3.40GHz)
// For further analysis see
//   http://www.cse.yorku.ca/~oz/marsaglia-rng.html
//   https://link.springer.com/content/pdf/10.1007/s12095-017-0225-x.pdf
class KISS final {
   public:
    explicit KISS(std::uint64_t seed = 1ULL) noexcept {
        SplitMix64 sm64(seed);
        bool       allZero = true;
        for (std::size_t i = 0; i < 4; ++i)
        {
            s[i] = sm64.next();
            if (s[i] != 0)
                allZero = false;
        }
        // Avoid all-zero state
        if (allZero)
        {
            s[0] = 0xF1EA5EEDULL;
            s[1] = s[2] = s[3] = 0xD4E12C77ULL;
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

    constexpr void jump() noexcept {
        // Scramble few rounds to decorrelate state
        for (std::size_t i = 0; i < 4; ++i)
            rand64();
    }

    // Magic generator used to initialize magic numbers
    // 'k' encodes rotation amounts; function consumes several PRNs
    template<typename T>
    T magic_rand(unsigned k) noexcept {
        const std::uint64_t r1 = rand64();
        const std::uint64_t r2 = rand64();
        const std::uint64_t r3 = rand64();

        std::uint64_t x = rotl(r1, k >> 0) & r2;
        x               = rotl(x, k >> 6) & r3;
        return T(x);
    }

   private:
    // RKISS algorithm implementation
    inline std::uint64_t rand64() noexcept {
        const std::uint64_t x = s[0] - rotl(s[1], 7);
        s[0]                  = s[1] ^ rotl(s[2], 13);
        s[1]                  = s[2] + rotl(s[3], 37);
        s[2]                  = s[3] + x;
        s[3]                  = x + s[0];
        return s[3];
    }

    std::uint64_t s[4]{};
};

// XorShift64* Pseudo-Random Number Generator
// This class is based on original code written and dedicated
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
    explicit constexpr XorShift64Star(std::uint64_t seed = 1ULL) noexcept {
        SplitMix64 sm64(seed);
        s = sm64.next();
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
            if ((JumpMask >> b) & 1)
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

    std::uint64_t s{};
};

// XorShift1024* Pseudo-Random Number Generator
class XorShift1024Star final {
   public:
    explicit constexpr XorShift1024Star(std::uint64_t seed = 1ULL) noexcept {
        SplitMix64 sm64(seed);
        bool       allZero = true;
        for (std::size_t i = 0; i < 16; ++i)
        {
            s[i] = sm64.next();
            if (s[i] != 0)
                allZero = false;
        }
        // Avoid all-zero state
        if (allZero)
            s[0] = 1ULL;
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

    // XorShift1024* jump implementation
    constexpr void jump() noexcept {
        constexpr std::uint64_t JumpMasks[16]             //
          {0x84242F96ECA9C41DULL, 0xA3C65B8776F96855ULL,  //
           0x5B34A39F070B5837ULL, 0x4489AFFCE4F31A1EULL,  //
           0x2FFEEB0A48316F40ULL, 0xDC2D9891FE68C022ULL,  //
           0x3659132BB12FEA70ULL, 0xAAC17D8EFA43CAB8ULL,  //
           0xC4CB815590989B13ULL, 0x5EE975283D71C93BULL,  //
           0x691548C86C1BD540ULL, 0x7910C41D10A1E6A5ULL,  //
           0x0B5FC64563B3E2A8ULL, 0x047F7684E9FC949DULL,  //
           0xB99181F2D8F685CAULL, 0x284600E3F30E38C3ULL};

        std::uint64_t t[16]{};
        for (const std::uint64_t jumpMask : JumpMasks)
            for (std::uint8_t b = 0; b < 64; ++b)
            {
                if ((jumpMask >> b) & 1)
                    for (std::size_t i = 0; i < 16; ++i)
                        t[i] ^= s[index(i)];
                rand64();
            }

        for (std::size_t i = 0; i < 16; ++i)
            s[index(i)] = t[i];
    }

   private:
    constexpr std::size_t index(std::size_t k) const noexcept { return (p + k) & (16 - 1); }

    // XorShift1024* algorithm implementation
    constexpr std::uint64_t rand64() noexcept {
        std::uint64_t s0 = s[p];
        std::uint64_t s1 = s[p = index(1)];
        s1 ^= s1 << 31;
        s[p] = s0 ^ s1 ^ (s0 >> 30) ^ (s1 >> 11);
        return 0x106689D45497FDB5ULL * s[p];
    }

    std::uint64_t s[16]{};
    std::size_t   p{0};
};

// Modern XoShiRo256++ (short for "xor, shift, rotate") Pseudo-Random Number Generator
class XoShiRo256Plus final {
   public:
    explicit constexpr XoShiRo256Plus(std::uint64_t seed = 1ULL) noexcept {
        SplitMix64 sm64(seed);
        bool       allZero = true;
        for (std::size_t i = 0; i < 4; ++i)
        {
            s[i] = sm64.next();
            if (s[i] != 0)
                allZero = false;
        }
        // Avoid all-zero state
        if (allZero)
            s[0] = 1ULL;
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

    // XoShiRo256++ jump implementation
    constexpr void jump() noexcept {
        constexpr std::uint64_t JumpMasks[4]              //
          {0x180EC6D33CFD0ABAULL, 0xD5A61266F0C9392CULL,  //
           0xA9582618E03FC9AAULL, 0x39ABDC4529B1661CULL};

        std::uint64_t t[4]{};
        for (const std::uint64_t jumpMask : JumpMasks)
            for (std::uint8_t b = 0; b < 64; ++b)
            {
                if ((jumpMask >> b) & 1)
                    for (std::size_t i = 0; i < 4; ++i)
                        t[i] ^= s[i];
                rand64();
            }

        for (std::size_t i = 0; i < 4; ++i)
            s[i] = t[i];
    }

   private:
    // XoShiRo256++ algorithm implementation
    constexpr std::uint64_t rand64() noexcept {
        const std::uint64_t rs0 = rotl(s[0] + s[3], 23) + s[0];
        const std::uint64_t ss1 = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= ss1;
        s[3] = rotl(s[3], 45);
        return rs0;
    }

    std::uint64_t s[4]{};
};

// Modern XoShiRo256** (short for "xor, shift, rotate") Pseudo-Random Number Generator
class XoShiRo256Star final {
   public:
    explicit constexpr XoShiRo256Star(std::uint64_t seed = 1ULL) noexcept {
        SplitMix64 sm64(seed);
        bool       allZero = true;
        for (std::size_t i = 0; i < 4; ++i)
        {
            s[i] = sm64.next();
            if (s[i] != 0)
                allZero = false;
        }
        // Avoid all-zero state
        if (allZero)
            s[0] = 1ULL;
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

    // XoShiRo256** jump implementation
    constexpr void jump() noexcept {
        constexpr std::uint64_t JumpMasks[4]              //
          {0x180EC6D33CFD0ABAULL, 0xD5A61266F0C9392CULL,  //
           0xA9582618E03FC9AAULL, 0x39ABDC4529B1661CULL};

        std::uint64_t t[4]{};
        for (const std::uint64_t jumpMask : JumpMasks)
            for (std::uint8_t b = 0; b < 64; ++b)
            {
                if ((jumpMask >> b) & 1)
                    for (std::size_t i = 0; i < 4; ++i)
                        t[i] ^= s[i];
                rand64();
            }

        for (std::size_t i = 0; i < 4; ++i)
            s[i] = t[i];
    }

   private:
    // XoShiRo256** algorithm implementation
    constexpr std::uint64_t rand64() noexcept {
        const std::uint64_t rs1 = rotl(s[1] * 5, 7) * 9;
        const std::uint64_t ss1 = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= ss1;
        s[3] = rotl(s[3], 45);
        return rs1;
    }

    std::uint64_t s[4]{};
};

// Template PRNG wrapper class
template<typename Generator>
class PRNG final {
   public:
    explicit constexpr PRNG(std::uint64_t seed = 1ULL) noexcept :
        generator(seed) {}

    template<typename T>
    constexpr T rand() noexcept {
        return generator.template rand<T>();
    }

    template<typename T>
    constexpr T sparse_rand() noexcept {
        return generator.template sparse_rand<T>();
    }

    constexpr void jump() noexcept { generator.jump(); }

   private:
    Generator generator;
};

#endif  // PRNG_H_INCLUDED
