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

#ifndef UTILITY_STREAMBUF_H_INCLUDED
#define UTILITY_STREAMBUF_H_INCLUDED

#include <ios>
#include <streambuf>
#include <string_view>

#include "../utility/types.h"

namespace DON {

// Custom streambuf that wraps std::string_view
class StringBuf final: public std::streambuf {
   public:
    explicit StringBuf(std::string_view sv) noexcept;
};

// Custom streambuf that wraps memory stream
class MemoryBuf final: public std::streambuf {
   public:
    MemoryBuf(char* p, usize size) noexcept;
};

// TieBuf (Fancy logging facility).
// Replaces std::cin.rdbuf() and std::cout.rdbuf() with two TieBuf objects
// that preserve normal I/O while optionally mirroring input and output to a
// secondary stream buffer.
// Logging of std::cin and std::cout can be toggled at runtime without changing
// their usual I/O usage.
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
// MSVC requires separate stream buffers for std::cin and std::cout.
//
// Forwards I/O to the primary buffer and mirrors it to the secondary buffer
// with line prefixes.
class TieBuf final: public std::streambuf {
   public:
    TieBuf() noexcept = default;
    TieBuf(std::streambuf* pBf, std::streambuf* mBf) noexcept;

   protected:
    // Synchronizes both the primary and mirror buffers.
    int sync() override;
    // Reads the next character from the primary buffer without consuming it.
    int_type underflow() override;
    // Writes one character to the primary buffer and mirrors it with an output prefix.
    int_type overflow(int_type ch) override;
    // Reads and consumes one character from the primary buffer, then mirrors it with an input prefix.
    int_type uflow() override;
    // Writes a block to the primary buffer and mirrors the written characters with an output prefix.
    std::streamsize xsputn(const char_type* s, std::streamsize count) override;

    [[nodiscard]] std::streambuf* pbuf() const noexcept;
    [[nodiscard]] std::streambuf* mbuf() const noexcept;

   private:
    // Mirrors a character to the secondary buffer, adding a prefix at the start of each line.
    int_type
    mirror_put_with_prefix(int_type ch, std::string_view prefix, char_type& preCh) noexcept;

    std::streambuf *pBuf = nullptr, *mBuf = nullptr;

    char_type opreCh = '\n', ipreCh = '\n';
};

}  // namespace DON

#endif  // UTILITY_STREAMBUF_H_INCLUDED
