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

#include "../utility/streambuf.h"

#include <string>
#include <algorithm>
#include <cstring>
#include <streambuf>
#include <string>
#include <string_view>

#include "../utility/console.h"
#include "../utility/types.h"

namespace DON {

MemoryBuf::MemoryBuf(char* data, const usize size) noexcept {
    auto* const p = data;
    setg(p, p, p + size);  // Set GET area (reading enabled)
    setp(p, p + size);     // Set PUT area (writing enabled)
}

// ------------------

ConstMemoryBuf::ConstMemoryBuf(const char* const data, const usize size) noexcept {
    auto* const p = const_cast<char*>(data);
    setg(p, p, p + size);
}

// ------------------

StringBuf::StringBuf(const std::string_view sv) noexcept {
    // std::streambuf requires char* for the get area.
    // The buffer is read-only; no characters are modified.
    auto* const p    = const_cast<char*>(sv.data());
    const usize size = sv.size();
    setg(p, p, p + size);  // Only GET area (reading enabled)
    // Do NOT call setp(p, p + size) - no PUT area (writing disabled)
}

// ------------------

NullBuf::int_type NullBuf::overflow(int_type ch) { return traits_type::not_eof(ch); }

std::streamsize NullBuf::xsputn(const char_type*, std::streamsize size) { return size; }

// ------------------

PrefixBuf::PrefixBuf(std::streambuf* dstBuf, const std::string_view prefx) noexcept :
    dstBuf_(dstBuf),
    prefix(prefx),
    atLineStart(true) {}

PrefixBuf::int_type PrefixBuf::overflow(const int_type ch) {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);

    const char c = traits_type::to_char_type(ch);

    if (atLineStart)
    {
        if (dstBuf_->sputn(prefix.data(), static_cast<std::streamsize>(prefix.size()))
            != static_cast<std::streamsize>(prefix.size()))
            return traits_type::eof();

        atLineStart = false;
    }

    if (traits_type::eq_int_type(dstBuf_->sputc(c), traits_type::eof()))
        return traits_type::eof();

    if (c == '\n')
        atLineStart = true;

    return ch;
}

// ------------------

CountingBuf::CountingBuf(std::streambuf* const dstBuf) noexcept :
    dstBuf_(dstBuf) {}

CountingBuf::int_type CountingBuf::overflow(const int_type ch) {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);

    if (traits_type::eq_int_type(dstBuf_->sputc(ch), traits_type::eof()))
        return traits_type::eof();

    ++bytes;
    return ch;
}

std::streamsize CountingBuf::xsputn(const char_type* s, std::streamsize size) {
    const auto written = dstBuf_->sputn(s, size);

    bytes += static_cast<usize>(written);

    return written;
}

int CountingBuf::sync() { return dstBuf_ != nullptr ? dstBuf_->pubsync() : 0; }

// ------------------

TieBuf::TieBuf(std::streambuf* const primBuf, std::streambuf* const mirrBuf) noexcept :
    primBuf_(primBuf),
    mirrBuf_(mirrBuf) {}

std::streambuf* TieBuf::prim_buf() const noexcept { return primBuf_; }

std::streambuf* TieBuf::mirr_buf() const noexcept { return mirrBuf_; }

int TieBuf::sync() {
    const int primR = primBuf_ != nullptr ? primBuf_->pubsync() : 0;
    const int mirrR = mirrBuf_ != nullptr ? mirrBuf_->pubsync() : 0;

    return primR == 0 && mirrR == 0 ? 0 : -1;
}

TieBuf::int_type TieBuf::underflow() {
    if (primBuf_ == nullptr)
        return traits_type::eof();

    return primBuf_->sgetc();
}

TieBuf::int_type TieBuf::overflow(const int_type ch) {
    if (primBuf_ == nullptr)
        return traits_type::eof();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return sync() == 0 ? traits_type::not_eof(ch) : traits_type::eof();

    const int_type written = primBuf_->sputc(traits_type::to_char_type(ch));

    if (traits_type::eq_int_type(written, traits_type::eof()))
        return traits_type::eof();

    return mirror_write_with_prefix(written, "<< ", oPreCh);
}

TieBuf::int_type TieBuf::uflow() {
    if (primBuf_ == nullptr)
        return traits_type::eof();

    const int_type extracted = primBuf_->sbumpc();

    if (traits_type::eq_int_type(extracted, traits_type::eof()))
        return extracted;

    return mirror_write_with_prefix(extracted, ">> ", iPreCh);
}

std::streamsize TieBuf::xsgetn(char_type* const ptr, const std::streamsize count) {
    if (primBuf_ == nullptr || count <= 0)
        return 0;

    const std::streamsize read = primBuf_->sgetn(ptr, count);

    if (mirrBuf_ == nullptr || read <= 0)
        return read;

    if (iPreCh == '\n')
        mirrBuf_->sputn(">> ", 3);

    mirrBuf_->sputn(ptr, read);

    iPreCh = ptr[read - 1];

    return read;
}

std::streamsize TieBuf::xsputn(const char_type* const ptr, const std::streamsize count) {
    if (primBuf_ == nullptr || count <= 0)
        return 0;

    const std::streamsize written = primBuf_->sputn(ptr, count);

    if (mirrBuf_ == nullptr || written <= 0)
        return written;

    if (oPreCh == '\n')
        mirrBuf_->sputn("<< ", 3);

    mirrBuf_->sputn(ptr, written);

    oPreCh = ptr[written - 1];

    return written;
}

TieBuf::int_type TieBuf::mirror_write_with_prefix(const int_type         readed,
                                                  const std::string_view prefix,
                                                  char_type&             preCh) noexcept {
    if (mirrBuf_ == nullptr)
        return traits_type::not_eof(readed);

    if (preCh == '\n')
        mirrBuf_->sputn(prefix.data(), std::streamsize(prefix.size()));

    const char_type ch = traits_type::to_char_type(readed);
    preCh              = ch;

    const int_type written = mirrBuf_->sputc(ch);

    return traits_type::eq_int_type(written, traits_type::eof()) ? traits_type::eof()
                                                                 : traits_type::not_eof(readed);
}

// ------------------

ColorBuf::ColorBuf(std::streambuf* const bufPtr, const char* const clr) noexcept {
    set(bufPtr, clr);
}

void ColorBuf::set(std::streambuf* const bufPtr, const char* const clr) noexcept {
    buf_  = bufPtr;
    color = clr;
}

std::streambuf* ColorBuf::buf() const noexcept { return buf_; }

int ColorBuf::sync() { return buf_ != nullptr ? buf_->pubsync() : 0; }

ColorBuf::int_type ColorBuf::overflow(const int_type ch) {
    if (buf_ == nullptr)
        return traits_type::eof();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return sync() == 0 ? traits_type::not_eof(ch) : traits_type::eof();

    if (!write_color())
    {
        //(void) write_reset();
        return traits_type::eof();
    }

    const int_type written = buf_->sputc(traits_type::to_char_type(ch));

    (void) write_reset();

    return written;
}

std::streamsize ColorBuf::xsputn(const char_type* const s, const std::streamsize count) {
    if (buf_ == nullptr || count <= 0)
        return 0;

    if (!write_color())
    {
        //(void) write_reset();
        return 0;
    }

    const std::streamsize written = buf_->sputn(s, count);

    (void) write_reset();

    return written;
}

bool ColorBuf::write_color() noexcept {
    if (buf_ == nullptr || color == nullptr)
        return false;

    const std::streamsize length = traits_type::length(color);

    return length == 0 || buf_->sputn(color, length) == length;
}

bool ColorBuf::write_reset() noexcept {
    if (buf_ == nullptr)
        return false;

    constexpr const char*     reset  = ConsoleColor::RESET;
    constexpr std::streamsize length = traits_type::length(reset);

    return buf_->sputn(reset, length) == length;
}

// ------------------

FNV1aBuf::FNV1aBuf(std::streambuf* destination) noexcept :
    buf_(destination) {}

FNV1aBuf::int_type FNV1aBuf::overflow(int_type ch) {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);

    const auto c = static_cast<u8>(traits_type::to_char_type(ch));

    state ^= c;
    state *= u64{0x100000001B3};

    if (traits_type::eq_int_type(buf_->sputc(traits_type::to_char_type(ch)), traits_type::eof()))
        return traits_type::eof();

    return ch;
}

std::streamsize FNV1aBuf::xsputn(const char_type* s, const std::streamsize size) {
    for (std::streamsize i = 0; i < size; ++i)
    {
        const auto c = static_cast<u8>(s[i]);

        state ^= c;
        state *= u64{0x100000001B3};
    }

    return buf_->sputn(s, size);
}

// ------------------

NullFilter1Buf::NullFilter1Buf(std::streambuf* srcBuf) noexcept :
    srcBuf_(srcBuf) {
    setg(&buffer, &buffer + 1, &buffer + 1);
}

NullFilter1Buf::int_type NullFilter1Buf::underflow() {
    int_type ch;

    do
    {
        ch = srcBuf_->sbumpc();
    } while (!traits_type::eq_int_type(ch, traits_type::eof())
             && traits_type::to_char_type(ch) == '\0');

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::eof();

    buffer = traits_type::to_char_type(ch);
    setg(&buffer, &buffer, &buffer + 1);

    return ch;
}

// ------------------

NullFilterBuf::NullFilterBuf(std::streambuf* srcBuf) noexcept :
    srcBuf_(srcBuf) {
    setg(buffer, buffer, buffer);
}

NullFilterBuf::int_type NullFilterBuf::underflow() {
    if (gptr() < egptr())
        return traits_type::to_int_type(*gptr());

    const std::streamsize count = srcBuf_->sgetn(inputBuffer, BufferSize);

    if (count <= 0)
    {
        setg(buffer, buffer, buffer);
        return traits_type::eof();
    }

    char* out = buffer;

    for (std::streamsize i = 0; i < count; ++i)
    {
        const char c = inputBuffer[i];

        if (c != '\0')
            *out++ = c;
    }

    if (out == buffer)
    {
        // The entire source block consisted of NUL bytes.
        // Read another block.
        return underflow();
    }

    setg(buffer, buffer, out);

    return traits_type::to_int_type(*gptr());
}

// ------------------

LimitBuf::LimitBuf(std::streambuf* dstBuf, usize limit) noexcept :
    dstBuf_(dstBuf),
    remaining(limit) {}

LimitBuf::int_type LimitBuf::overflow(int_type ch) {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);

    if (remaining == 0)
    {
        overflowedFlag = true;
        return traits_type::eof();
    }

    if (traits_type::eq_int_type(dstBuf_->sputc(traits_type::to_char_type(ch)), traits_type::eof()))
        return traits_type::eof();

    --remaining;
    return ch;
}

std::streamsize LimitBuf::xsputn(const char_type* s, std::streamsize size) {
    const usize n = std::min<usize>(static_cast<usize>(size), remaining);

    const auto written = dstBuf_->sputn(s, static_cast<std::streamsize>(n));

    remaining -= static_cast<usize>(written);

    if (written != size)
        overflowedFlag = true;

    return written;
}

// ------------------

UppercaseBuf::UppercaseBuf(std::streambuf* dstBuf) noexcept :
    dstBuf_(dstBuf) {}

UppercaseBuf::int_type UppercaseBuf::overflow(int_type ch) {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);

    char c = traits_type::to_char_type(ch);

    c = char(std::toupper((unsigned char) (c)));

    return dstBuf_->sputc(c);
}

// ------------------

Replay1Buf::Replay1Buf(std::streambuf* srcBuf) noexcept :
    srcBuf_(srcBuf) {}

Replay1Buf::int_type Replay1Buf::underflow() {
    if (gptr() < egptr())
        return traits_type::to_int_type(*gptr());

    const int_type ch = srcBuf_->sgetc();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::eof();

    char c = traits_type::to_char_type(ch);

    buffer.push_back(c);

    return traits_type::to_int_type(c);
}

Replay1Buf::int_type Replay1Buf::uflow() {
    const int_type ch = srcBuf_->sbumpc();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::eof();

    buffer.push_back(traits_type::to_char_type(ch));

    return ch;
}

std::streamsize Replay1Buf::xsgetn(char_type* destination, std::streamsize count) {
    if (count <= 0)
        return 0;

    const std::streamsize n = srcBuf_->sgetn(destination, count);

    if (n > 0)
        buffer.append(destination, static_cast<usize>(n));

    return n;
}

Replay1Buf::int_type Replay1Buf::pbackfail(int_type ch) {
    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::eof();

    // We cannot safely remove from the replay buffer here because
    // the caller may put back a character that wasn't originally
    // recorded by this buffer.
    return traits_type::eof();
}

// ------------------

ReplayBuf::ReplayBuf(std::streambuf* srcBuf, usize expectedSize) noexcept :
    srcBuf_(srcBuf) {
    if (expectedSize != 0)
        recorded.reserve(expectedSize);

    setg(buffer, buffer, buffer);
}

ReplayBuf::int_type ReplayBuf::underflow() {
    if (gptr() < egptr())
        return traits_type::to_int_type(*gptr());

    const std::streamsize n = srcBuf_->sgetn(buffer, static_cast<std::streamsize>(BufferSize));

    if (n <= 0)
    {
        setg(buffer, buffer, buffer);
        return traits_type::eof();
    }

    setg(buffer, buffer, buffer + n);

    return traits_type::to_int_type(*gptr());
}

ReplayBuf::int_type ReplayBuf::uflow() {
    const int_type ch = underflow();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::eof();

    const char c = *gptr();

    gbump(1);

    recorded.push_back(c);

    return traits_type::to_int_type(c);
}

std::streamsize ReplayBuf::xsgetn(char_type* destination, std::streamsize count) {
    if (count <= 0)
        return 0;

    std::streamsize total = 0;

    while (count > 0)
    {
        if (gptr() == egptr())
        {
            const int_type ch = underflow();

            if (traits_type::eq_int_type(ch, traits_type::eof()))
                break;
        }

        const std::streamsize available = static_cast<std::streamsize>(egptr() - gptr());

        const std::streamsize n = std::min(count, available);

        std::memcpy(destination, gptr(), static_cast<usize>(n));

        recorded.append(gptr(), static_cast<usize>(n));

        gbump(static_cast<int>(n));

        destination += n;
        count -= n;
        total += n;
    }

    return total;
}

}  // namespace DON
