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
    MemoryBuf(char* data, usize size) noexcept;
};

class ConstMemoryBuf final: public std::streambuf {
   public:
    ConstMemoryBuf(const char* data, usize size) noexcept;
};

// Custom streambuf that wraps std::string_view
class StringBuf final: public std::streambuf {
   public:
    explicit StringBuf(std::string_view sv) noexcept;
};

// NullBuf — /dev/null for ostream
// Extremely useful for benchmarks and disabling output without changing call sites.
//
// NullBuf nullBuf;
// std::ostream os(&nullBuf);
//
// os << "This goes nowhere: " << 123 << '\n';
class NullBuf final: public std::streambuf {
   protected:
    int_type overflow(int_type ch) override;

    std::streamsize xsputn(const char_type*, std::streamsize size) override;
};

// PrefixBuf — automatically prefix every line
//
// info string hello
// info string world
//
// becomes:
//
// [DON] info string hello
// [DON] info string world
class PrefixBuf final: public std::streambuf {
   public:
    PrefixBuf(std::streambuf* dstBuf, std::string_view prefx) noexcept;

   protected:
    int_type overflow(int_type ch) override;

   private:
    std::streambuf*  dstBuf_;
    std::string_view prefix;
    bool             atLineStart;
};

// CountingBuf — count bytes written
// CountingBuf counting(std::cout.rdbuf());
// std::ostream out(&counting);
//
// out << "hello " << 123 << '\n';
//
// std::cerr << counting.bytes_written();
class CountingBuf final: public std::streambuf {
   public:
    explicit CountingBuf(std::streambuf* dstBuf) noexcept;

    usize bytes_written() const noexcept { return bytes; }

   protected:
    int_type overflow(int_type ch) override;

    std::streamsize xsputn(const char_type* s, std::streamsize size) override;

    int sync() override;

   private:
    std::streambuf* dstBuf_;
    usize           bytes = 0;
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
    TieBuf(std::streambuf* primBuf, std::streambuf* mirrBuf) noexcept;

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

    std::streambuf *primBuf_ = nullptr, *mirrBuf_ = nullptr;

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

// HashBuf — calculate a hash while writing
class FNV1aBuf final: public std::streambuf {
   public:
    explicit FNV1aBuf(std::streambuf* destination) noexcept;

    u64 hash() const noexcept { return state; }

   protected:
    int_type overflow(int_type ch) override;

    std::streamsize xsputn(const char_type* s, std::streamsize size) override;

   private:
    std::streambuf* buf_;

    u64 state = u64{0xCBF29CE484222325};
};

// This exact general pattern—wrapping another streambuf and implementing underflow() as a filtering layer—is a classic custom-streambuf technique.
// It is an input filtering std::streambuf. Specifically, it removes '\0' (NUL) characters from the input stream.
class NullFilter1Buf final: public std::streambuf {
   public:
    explicit NullFilter1Buf(std::streambuf* srcBuf) noexcept;

    NullFilter1Buf(const NullFilter1Buf&)            = delete;
    NullFilter1Buf& operator=(const NullFilter1Buf&) = delete;

   protected:
    int_type underflow() override;

   private:
    std::streambuf* srcBuf_;
    char            buffer{};
};

class NullFilterBuf final: public std::streambuf {
   public:
    static constexpr usize BufferSize = 4096;

    explicit NullFilterBuf(std::streambuf* srcBuf) noexcept;

    NullFilterBuf(const NullFilterBuf&)            = delete;
    NullFilterBuf& operator=(const NullFilterBuf&) = delete;

   protected:
    int_type underflow() override;

   private:
    std::streambuf* srcBuf_;

    char inputBuffer[BufferSize];
    char buffer[BufferSize];
};

// Very useful when you don't want a stream to exceed some amount.
class LimitBuf final: public std::streambuf {
   public:
    LimitBuf(std::streambuf* dstBuf, usize limit) noexcept;

    bool overflowed() const noexcept { return overflowedFlag; }

   protected:
    int_type overflow(int_type ch) override;

    std::streamsize xsputn(const char_type* s, std::streamsize size) override;

   private:
    std::streambuf* dstBuf_;
    usize           remaining;
    bool            overflowedFlag = false;
};

// This is essentially the classic example from libstdc++'s streambuf documentation: an output buffer intercepts characters and transforms them before sending them to the destination.
class UppercaseBuf final: public std::streambuf {
   public:
    explicit UppercaseBuf(std::streambuf* dstBuf) noexcept;

   protected:
    int_type overflow(int_type ch) override;

   private:
    std::streambuf* dstBuf_;
};

// ReplayBuf an input-side streambuf filter that transparently reads from another std::streambuf while recording everything consumed. It can then expose the captured input as a std::string_view for replay/testing.
// use a std::string as the recording buffer
class Replay1Buf final: public std::streambuf {
   public:
    explicit Replay1Buf(std::streambuf* srcBuf) noexcept;

    Replay1Buf(const Replay1Buf&)            = delete;
    Replay1Buf& operator=(const Replay1Buf&) = delete;

    std::string_view replay() const noexcept { return buffer; }

    const std::string& data() const noexcept { return buffer; }

    usize size() const noexcept { return buffer.size(); }

    void clear() noexcept { buffer.clear(); }

   protected:
    int_type underflow() override;

    int_type uflow() override;

    std::streamsize xsgetn(char_type* destination, std::streamsize count) override;

    int_type pbackfail(int_type ch) override;

   private:
    std::streambuf* srcBuf_;
    std::string     buffer;
};

// ReplayBuf an input-side streambuf filter that transparently reads from another std::streambuf while recording everything consumed. It can then expose the captured input as a std::string_view for replay/testing.
// So for a proper replay buffer, I recommend a slightly different design: buffer a chunk from the source and only append to the replay log when characters are actually consumed.
//
// ReplayBuf replayBuf(std::cin.rdbuf());
// std::istream is(&replayBuf);
//
// std::string command;
//
// while (is >> command)
// {
//    // process command
// }
//
// const std::string_view history = replayBuf.replay();
//
// then replayBuf.replay() contains exactly the characters that were actually extracted.
class ReplayBuf final: public std::streambuf {
   public:
    static constexpr usize BufferSize = 4096;

    explicit ReplayBuf(std::streambuf* srcBuf, usize expectedSize = 0) noexcept;

    ReplayBuf(const ReplayBuf&)            = delete;
    ReplayBuf& operator=(const ReplayBuf&) = delete;

    std::string_view replay() const noexcept { return recorded; }

    usize size() const noexcept { return recorded.size(); }

    void clear() noexcept { recorded.clear(); }

   protected:
    int_type underflow() override;

    int_type uflow() override;

    std::streamsize xsgetn(char_type* destination, std::streamsize count) override;

   private:
    std::streambuf* srcBuf_;

    char        buffer[BufferSize];
    std::string recorded;
};

}  // namespace DON

#endif  // UTILITY_STREAMBUF_H_INCLUDED
