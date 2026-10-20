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

#include "../utility/reader.h"

#include "../utility/functions.h"

namespace DON {

StringReader::StringReader(const std::string_view sv) noexcept :
    beg(sv.data()),
    cur(beg),
    end(beg + sv.size()) {}

char StringReader::peek() const noexcept { return cur != end ? *cur : Null; }

bool StringReader::is_not_space() const noexcept { return cur != end && !is_space(*cur); }

void StringReader::skip_spaces() noexcept {
    for (; cur != end && is_space(*cur); ++cur)
    {}
}

void StringReader::advance() noexcept {
    if (cur != end)
        ++cur;
}

char StringReader::get() noexcept { return cur != end ? *cur++ : Null; }

bool StringReader::get_int(int& out) noexcept {
    skip_spaces();

    const bool neg = cur != end && *cur == '-';
    if (cur != end && (*cur == '+' || *cur == '-'))
        ++cur;

    int        val = 0;
    const auto p   = cur;
    for (; cur != end && is_cdigit(*cur); ++cur)
        val = 10 * val + char_to_digit(*cur);

    if (p == cur)
        return false;

    out = neg ? -val : val;
    return true;
}

}  // namespace DON
