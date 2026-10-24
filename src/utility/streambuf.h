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
#include <string_view>

#include "../utility/types.h"

namespace DON {

// Custom streambuf that wraps memory stream
class MemoryBuf final: public std::streambuf {
   public:
    MemoryBuf(char* p, usize size) noexcept;
};

// Custom streambuf that wraps std::string_view
class StringBuf final: public std::streambuf {
   public:
    explicit StringBuf(std::string_view sv) noexcept;
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

    [[nodiscard]] std::streambuf* prim_buf() const noexcept;

    [[nodiscard]] std::streambuf* mirr_buf() const noexcept;

   protected:
    // Synchronizes both the primary and mirror buffers.
    int sync() override;

    // Reads the next character from the primary buffer without consuming it.
    int_type underflow() override;

    // Writes one character to the primary buffer and mirrors it with an output prefix.
    int_type overflow(int_type ch) override;

    // Reads and consumes one character from the primary buffer and mirrors it with an input prefix.
    int_type uflow() override;

    // Reads a block from the primary buffer and mirrors the read characters with an input prefix.
    std::streamsize xsgetn(char_type* ptr, std::streamsize count) override;

    // Writes a block to the primary buffer and mirrors the written characters with an output prefix.
    std::streamsize xsputn(const char_type* ptr, std::streamsize count) override;

   private:
    // Mirrors a character to the secondary buffer, adding a prefix at the start of each line.
    int_type
    mirror_write_with_prefix(int_type readed, std::string_view prefix, char_type& preCh) noexcept;

    std::streambuf *primBuf = nullptr, *mirrBuf = nullptr;

    char_type oPreCh = '\n', iPreCh = '\n';
};

class ColorBuf final: public std::streambuf {
   public:
    ColorBuf() noexcept = default;
    ColorBuf(std::streambuf* bufPtr, const char* clr) noexcept;

    void set(std::streambuf* bufPtr, const char* clr) noexcept;

    [[nodiscard]] std::streambuf* buf() const noexcept;

   protected:
    // Synchronizes the underlying buffer.
    int sync() override;

    // Writes one character to the underlying buffer with color handling.
    int_type overflow(int_type ch) override;

    // Writes a block of characters to the underlying buffer with color handling.
    std::streamsize xsputn(const char_type* s, std::streamsize count) override;

   private:
    // Writes the configured color escape sequence.
    bool write_color() noexcept;

    // Writes the color reset escape sequence.
    bool write_reset() noexcept;

    std::streambuf* buf_  = nullptr;
    const char*     color = nullptr;
};

}  // namespace DON

#endif  // UTILITY_STREAMBUF_H_INCLUDED
