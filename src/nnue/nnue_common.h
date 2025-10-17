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

// Constants used in NNUE evaluation function

#ifndef NNUE_COMMON_H_INCLUDED
#define NNUE_COMMON_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "../misc.h"
#include "../types.h"

#if defined(USE_AVX2)
    #include <immintrin.h>
#elif defined(USE_SSE41)
    #include <smmintrin.h>
#elif defined(USE_SSSE3)
    #include <tmmintrin.h>
#elif defined(USE_SSE2)
    #include <emmintrin.h>
#elif defined(USE_NEON)
    #include <arm_neon.h>
#endif

namespace DON::NNUE {

using BiasType       = std::int16_t;
using WeightType     = std::int16_t;
using PSQTWeightType = std::int32_t;
using IndexType      = std::uint32_t;

// Type of input feature after conversion
using TransformedFeatureType = std::uint8_t;

// Version of the evaluation file
constexpr std::uint32_t FILE_VERSION = 0x7AF32F20U;

// Constant used in evaluation value calculation
constexpr int OUTPUT_SCALE      = 16;
constexpr int WEIGHT_SCALE_BITS = 6;

constexpr const char  LEB128_MAGIC_STRING[]    = "COMPRESSED_LEB128";
constexpr std::size_t LEB128_MAGIC_STRING_SIZE = sizeof(LEB128_MAGIC_STRING) - 1;

// SIMD width (in bytes)
#if defined(USE_AVX2)
constexpr std::size_t SIMD_WIDTH = 32;
#elif defined(USE_SSE2)
constexpr std::size_t SIMD_WIDTH = 16;
#elif defined(USE_NEON)
constexpr std::size_t SIMD_WIDTH = 16;
#endif

constexpr std::size_t MAX_SIMD_WIDTH = 32;

// Round n up to be a multiple of base
template<typename IntType>
constexpr IntType ceil_to_multiple(IntType n, IntType base) noexcept {
    return (n + base - 1) / base * base;
}

// Utility to read an integer (signed or unsigned, any size)
// from a istream in little-endian order. Swap the byte order after the read
// if necessary to return a result with the byte ordering of the compiling machine.
template<typename IntType>
inline IntType read_little_endian(std::istream& istream) noexcept {
    IntType result;

    constexpr std::size_t Size = sizeof(IntType);

    if (IsLittleEndian)
        istream.read(reinterpret_cast<char*>(&result), Size);
    else
    {
        std::uint8_t                  u[Size];
        std::make_unsigned_t<IntType> v = 0;

        istream.read(reinterpret_cast<char*>(u), Size);
        for (std::size_t i = 0; i < Size; ++i)
            v = (v << 8) | u[Size - i - 1];

        std::memcpy(&result, &v, Size);
    }

    return result;
}

// Utility to write an integer (signed or unsigned, any size)
// to a ostream in little-endian order. Swap the byte order before the write
// if necessary to always write in little-endian order,
// independently of the byte ordering of the compiling machine.
template<typename IntType>
inline void write_little_endian(std::ostream& ostream, IntType value) noexcept {

    constexpr std::size_t Size = sizeof(IntType);

    if (IsLittleEndian)
        ostream.write(reinterpret_cast<const char*>(&value), Size);
    else
    {
        std::uint8_t                  u[Size];
        std::make_unsigned_t<IntType> v = value;

        std::size_t i = 0;
        // if constexpr to silence the warning about shift by 8
        if constexpr (Size > 1)
        {
            for (; i + 1 < Size; ++i)
            {
                u[i] = std::uint8_t(v);
                v >>= 8;
            }
        }
        u[i] = std::uint8_t(v);

        ostream.write(reinterpret_cast<char*>(u), Size);
    }
}

// Read integers in bulk from a little-endian istream.
// This reads N integers from istream and puts them in array out.
template<typename IntType>
inline void read_little_endian(std::istream& istream, IntType* out, std::size_t count) noexcept {
    if (IsLittleEndian)
        istream.read(reinterpret_cast<char*>(out), count * sizeof(IntType));
    else
        for (std::size_t i = 0; i < count; ++i)
            out[i] = read_little_endian<IntType>(istream);
}

// Write integers in bulk to a little-endian ostream.
// This takes N integers from array values and writes them on ostream.
template<typename IntType>
inline void
write_little_endian(std::ostream& ostream, const IntType* values, std::size_t count) noexcept {
    if (IsLittleEndian)
        ostream.write(reinterpret_cast<const char*>(values), count * sizeof(IntType));
    else
        for (std::size_t i = 0; i < count; ++i)
            write_little_endian<IntType>(ostream, values[i]);
}

// Read signed integers from a istream with LEB128 compression.
// This puts N signed integers in the array out, compresses them with
// the LEB128 algorithm and read the value from the istream.
// See https://en.wikipedia.org/wiki/LEB128 for a description of the compression scheme.
template<typename IntType>
inline void read_leb_128(std::istream& istream, IntType* out, std::size_t count) noexcept {
    static_assert(std::is_signed_v<IntType>, "Not implemented for unsigned types");

    // Read and check the presence of our LEB128 magic string
    char leb128MagicString[LEB128_MAGIC_STRING_SIZE];
    istream.read(leb128MagicString, LEB128_MAGIC_STRING_SIZE);
    assert(strncmp(leb128MagicString, LEB128_MAGIC_STRING, LEB128_MAGIC_STRING_SIZE) == 0);

    constexpr std::size_t Size = sizeof(IntType);

    constexpr std::size_t BufSize = 4096;
    std::uint8_t          buffer[BufSize];
    std::size_t           bufPos = BufSize;

    std::size_t byteCount = read_little_endian<std::uint32_t>(istream);

    for (std::size_t i = 0; i < count; ++i)
    {
        IntType value = 0;

        std::size_t shift = 0;
        do
        {
            if (bufPos == BufSize)
            {
                istream.read(reinterpret_cast<char*>(buffer), std::min(bufPos, byteCount));
                bufPos = 0;
            }

            std::uint8_t byt = buffer[bufPos++];
            --byteCount;
            value |= (byt & 0x7F) << shift;
            shift += 7;

            if ((byt & 0x80) == 0)
            {
                out[i] =
                  (shift >= 8 * Size || (byt & 0x40) == 0) ? value : value | ~((1 << shift) - 1);
                break;
            }
        } while (shift < 8 * Size);
    }

    assert(byteCount == 0);
}

// Write signed integers to a ostream with LEB128 compression.
// This takes N integers from array in, compresses them with
// the LEB128 algorithm and writes the value to the ostream.
// See https://en.wikipedia.org/wiki/LEB128 for a description of the compression scheme.
template<typename IntType>
inline void write_leb_128(std::ostream& ostream, const IntType* in, std::size_t count) noexcept {
    static_assert(std::is_signed_v<IntType>, "Not implemented for unsigned types");

    // Write our LEB128 magic string
    ostream.write(LEB128_MAGIC_STRING, LEB128_MAGIC_STRING_SIZE);

    std::size_t byteCount = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        IntType value = in[i];

        std::uint8_t byt;
        do
        {
            byt = value & 0x7F;
            value >>= 7;
            ++byteCount;
        } while ((byt & 0x40) == 0 ? value != 0 : value != -1);
    }

    write_little_endian<std::uint32_t>(ostream, byteCount);

    constexpr std::size_t BufSize = 4096;
    std::uint8_t          buffer[BufSize];
    std::size_t           bufPos = 0;

    auto flush = [&]() {
        if (bufPos == 0)
            return;
        ostream.write(reinterpret_cast<char*>(buffer), bufPos);
        bufPos = 0;
    };

    auto write = [&](std::uint8_t byt) {
        buffer[bufPos++] = byt;
        if (bufPos == BufSize)
            flush();
    };

    for (std::size_t i = 0; i < count; ++i)
    {
        IntType value = in[i];

        while (true)
        {
            std::uint8_t byt = value & 0x7F;
            value >>= 7;
            if ((byt & 0x40) == 0 ? value == 0 : value == -1)
            {
                write(byt);
                break;
            }
            write(byt | 0x80);
        }
    }

    flush();
}

}  // namespace DON::NNUE

#endif  // #ifndef NNUE_COMMON_H_INCLUDED
