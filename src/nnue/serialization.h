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

// Constants used in NNUE evaluation function

#ifndef NNUE_SERIALIZATION_H_INCLUDED
#define NNUE_SERIALIZATION_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "../misc.h"
#include "../utility/types.h"

namespace DON::NNUE {

// Utility to read an integer (signed or unsigned, any size)
// from a istream in little-endian order. Swap the byte order after the read
// if necessary to return a value with the byte ordering of the compiling machine.
template<typename IntType>
inline IntType read_little_endian(std::istream& is) noexcept {

    using UIntType = std::make_unsigned_t<IntType>;

    constexpr usize IntSize = sizeof(IntType);

    IntType value;

    if (IS_LITTLE_ENDIAN)
        is.read(reinterpret_cast<char*>(&value), IntSize);
    else
    {
        Array<u8, IntSize> arr;

        UIntType v = 0;

        is.read(reinterpret_cast<char*>(arr.data()), IntSize);
        for (usize i = 0; i < IntSize; ++i)
            v = (v << BYTE_BITS) | arr[IntSize - i - 1];

        std::memcpy(&value, &v, IntSize);
    }

    return value;
}

// Utility to write an integer (signed or unsigned, any size)
// to a ostream in little-endian order. Swap the byte order before the write
// if necessary to always write in little-endian order,
// independently of the byte ordering of the compiling machine.
template<typename IntType>
inline void write_little_endian(std::ostream& os, IntType value) noexcept {

    using UIntType = std::make_unsigned_t<IntType>;

    constexpr usize IntSize = sizeof(IntType);

    if (IS_LITTLE_ENDIAN)
        os.write(reinterpret_cast<const char*>(&value), IntSize);
    else
    {
        Array<u8, IntSize> arr;

        UIntType v = value;

        usize i = 0;
        // if constexpr to silence the warning about shift by BYTE_BITS
        if constexpr (IntSize > 1)
        {
            for (; i + 1 < IntSize; ++i)
            {
                arr[i] = v;
                v >>= BYTE_BITS;
            }
        }
        arr[i] = v;

        os.write(reinterpret_cast<const char*>(arr.data()), IntSize);
    }
}

// Read integers in bulk from a little-endian stream.
// This reads N integers from istream and puts them in array out.
template<typename IntType>
inline void read_little_endian(std::istream& is, IntType* out, const usize Size) noexcept {
    if (IS_LITTLE_ENDIAN)
        is.read(reinterpret_cast<char*>(out), sizeof(IntType) * Size);
    else
        for (usize i = 0; i < Size; ++i)
            out[i] = read_little_endian<IntType>(is);
}

// Read integers in bulk from a little-endian istream.
// This reads N integers from istream and puts them in array out.
template<typename IntType, usize Size>
inline void read_little_endian(std::istream& is, std::array<IntType, Size>& out) noexcept {
    read_little_endian(is, out.data(), Size);
}

// Write integers in bulk to a little-endian ostream.
// This takes N integers from array in and writes them on ostream.
template<typename IntType>
inline void write_little_endian(std::ostream& os, const IntType* in, const usize Size) noexcept {
    if (IS_LITTLE_ENDIAN)
        os.write(reinterpret_cast<const char*>(in), sizeof(IntType) * Size);
    else
        for (usize i = 0; i < Size; ++i)
            write_little_endian<IntType>(os, in[i]);
}

// Write integers in bulk to a little-endian ostream.
// This takes N integers from array in and writes them on ostream.
template<typename IntType, usize Size>
inline void write_little_endian(std::ostream& os, const std::array<IntType, Size>& in) noexcept {
    write_little_endian(os, in.data(), Size);
}

}  // namespace DON::NNUE

#endif  // NNUE_SERIALIZATION_H_INCLUDED
