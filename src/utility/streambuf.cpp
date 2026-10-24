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

#include "../utility/types.h"

namespace DON {

StringBuf::StringBuf(const std::string_view sv) noexcept {
    // std::streambuf requires char* for the get area.
    // The buffer is read-only; no characters are modified.
    auto* const p    = const_cast<char*>(sv.data());
    const usize size = sv.size();
    setg(p, p, p + size);  // Only GET area (reading enabled)
    // Do NOT call setp(p, p + size) - no PUT area (writing disabled)
}

// ------------------

MemoryBuf::MemoryBuf(char* const p, const usize size) noexcept {
    setg(p, p, p + size);  // Set GET area (reading enabled)
    setp(p, p + size);     // Set PUT area (writing enabled)
}

// ------------------

TieBuf::TieBuf(std::streambuf* const pBf, std::streambuf* const mBf) noexcept :
    pBuf(pBf),
    mBuf(mBf) {}

int TieBuf::sync() {
    const int pR = pBuf != nullptr ? pBuf->pubsync() : 0;
    const int mR = mBuf != nullptr ? mBuf->pubsync() : 0;

    return pR == 0 && mR == 0 ? 0 : -1;
}

TieBuf::int_type TieBuf::underflow() {
    if (pBuf == nullptr)
        return traits_type::eof();

    return pBuf->sgetc();
}

TieBuf::int_type TieBuf::overflow(const int_type ch) {
    if (pBuf == nullptr)
        return traits_type::eof();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);

    int_type putCh = pBuf->sputc(traits_type::to_char_type(ch));

    if (traits_type::eq_int_type(putCh, traits_type::eof()))
        return putCh;

    return mirror_put_with_prefix(putCh, "<< ", opreCh);
}

TieBuf::int_type TieBuf::uflow() {
    if (pBuf == nullptr)
        return traits_type::eof();

    int_type ch = pBuf->sbumpc();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return ch;

    return mirror_put_with_prefix(ch, ">> ", ipreCh);
}

std::streamsize TieBuf::xsputn(const char_type* const s, const std::streamsize count) {
    if (pBuf == nullptr)
        return 0;

    const std::streamsize written = pBuf->sputn(s, count);

    if (mBuf == nullptr)
        return written;

    if (written <= 0)
        return written;

    if (opreCh == '\n')
        mBuf->sputn("<< ", 3);

    mBuf->sputn(s, written);

    opreCh = s[written - 1];

    return written;
}

std::streambuf* TieBuf::pbuf() const noexcept { return pBuf; }

std::streambuf* TieBuf::mbuf() const noexcept { return mBuf; }

TieBuf::int_type TieBuf::mirror_put_with_prefix(const int_type         ch,
                                                const std::string_view prefix,
                                                char_type&             preCh) noexcept {
    if (mBuf == nullptr)
        return traits_type::not_eof(ch);

    if (preCh == '\n')
        mBuf->sputn(prefix.data(), static_cast<std::streamsize>(prefix.size()));

    char_type c = traits_type::to_char_type(ch);
    preCh       = c;

    int_type r = mBuf->sputc(c);
    return traits_type::eq_int_type(r, traits_type::eof()) ? traits_type::eof()
                                                           : traits_type::not_eof(ch);
}

// ------------------

}  // namespace DON
