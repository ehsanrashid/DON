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

#include "../utility/types.h"

#if defined(_WIN32)
    #include "../platform_win.h"  // WideCharToMultiByte()
#endif

namespace DON {

std::string wstring_to_utf8(const std::wstring_view wsv) noexcept {
#if defined(_WIN32)
    if (wsv.empty())
        return {};

    constexpr UINT codePage = CP_UTF8;

    const int strSize =
      ::WideCharToMultiByte(codePage, 0, wsv.data(), int(wsv.size()), nullptr, 0, nullptr, nullptr);

    if (strSize <= 0)
        return {};

    std::string str(usize(strSize), '\0');
    ::WideCharToMultiByte(codePage, 0, wsv.data(), int(wsv.size()), str.data(), strSize, nullptr,
                          nullptr);

    return str;
#else
    return std::string{wsv.begin(), wsv.end()};
#endif
}

}  // namespace DON
