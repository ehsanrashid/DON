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

#include "../utility/console.h"
#include "../utility/types.h"

namespace DON {

MemoryBuf::MemoryBuf(char* const p, const usize size) noexcept {
    setg(p, p, p + size);  // Set GET area (reading enabled)
    setp(p, p + size);     // Set PUT area (writing enabled)
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

TieBuf::TieBuf(std::streambuf* const pBuf, std::streambuf* const mBuf) noexcept :
    primBuf(pBuf),
    mirrBuf(mBuf) {}

std::streambuf* TieBuf::prim_buf() const noexcept { return primBuf; }

std::streambuf* TieBuf::mirr_buf() const noexcept { return mirrBuf; }

int TieBuf::sync() {
    const int primR = primBuf != nullptr ? primBuf->pubsync() : 0;
    const int mirrR = mirrBuf != nullptr ? mirrBuf->pubsync() : 0;

    return primR == 0 && mirrR == 0 ? 0 : -1;
}

TieBuf::int_type TieBuf::underflow() {
    if (primBuf == nullptr)
        return traits_type::eof();

    return primBuf->sgetc();
}

TieBuf::int_type TieBuf::overflow(const int_type ch) {
    if (primBuf == nullptr)
        return traits_type::eof();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return sync() == 0 ? traits_type::not_eof(ch) : traits_type::eof();

    const int_type written = primBuf->sputc(traits_type::to_char_type(ch));

    if (traits_type::eq_int_type(written, traits_type::eof()))
        return traits_type::eof();

    return mirror_write_with_prefix(written, "<< ", oPreCh);
}

TieBuf::int_type TieBuf::uflow() {
    if (primBuf == nullptr)
        return traits_type::eof();

    const int_type extracted = primBuf->sbumpc();

    if (traits_type::eq_int_type(extracted, traits_type::eof()))
        return extracted;

    return mirror_write_with_prefix(extracted, ">> ", iPreCh);
}

std::streamsize TieBuf::xsgetn(char_type* const ptr, const std::streamsize count) {
    if (primBuf == nullptr || count <= 0)
        return 0;

    const std::streamsize read = primBuf->sgetn(ptr, count);

    if (mirrBuf == nullptr || read <= 0)
        return read;

    if (iPreCh == '\n')
        mirrBuf->sputn(">> ", 3);

    mirrBuf->sputn(ptr, read);

    iPreCh = ptr[read - 1];

    return read;
}

std::streamsize TieBuf::xsputn(const char_type* const ptr, const std::streamsize count) {
    if (primBuf == nullptr || count <= 0)
        return 0;

    const std::streamsize written = primBuf->sputn(ptr, count);

    if (mirrBuf == nullptr || written <= 0)
        return written;

    if (oPreCh == '\n')
        mirrBuf->sputn("<< ", 3);

    mirrBuf->sputn(ptr, written);

    oPreCh = ptr[written - 1];

    return written;
}

TieBuf::int_type TieBuf::mirror_write_with_prefix(const int_type         readed,
                                                  const std::string_view prefix,
                                                  char_type&             preCh) noexcept {
    if (mirrBuf == nullptr)
        return traits_type::not_eof(readed);

    if (preCh == '\n')
        mirrBuf->sputn(prefix.data(), std::streamsize(prefix.size()));

    const char_type ch = traits_type::to_char_type(readed);
    preCh              = ch;

    const int_type written = mirrBuf->sputc(ch);

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

}  // namespace DON
