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

#include "../utility/functions.h"

#if defined(_WIN32)
    #include "../utility/platform_win.h"  // WideCharToMultiByte()
#endif

#include "../utility/types.h"

namespace DON {

std::string wstring_to_utf8(const std::wstring_view wsv) noexcept {
#if defined(_WIN32)
    if (wsv.empty())
        return {};

    constexpr UINT   codePage       = CP_UTF8;
    constexpr DWORD  flags          = 0;
    const LPCWCH     wideCharStr    = wsv.data();
    const int        wideCharCount  = int(wsv.size());
    constexpr LPSTR  multiByteStr   = nullptr;
    constexpr int    multiByteCount = 0;
    constexpr LPCCH  defaultChar    = nullptr;
    constexpr LPBOOL useDefaultChar = nullptr;

    const int strByteCount = ::WideCharToMultiByte(codePage, flags,               //
                                                   wideCharStr, wideCharCount,    //
                                                   multiByteStr, multiByteCount,  //
                                                   defaultChar, useDefaultChar);
    if (strByteCount <= 0)
        return {};

    std::string str(usize(strByteCount), '\0');

    const int writtenByteCount = ::WideCharToMultiByte(codePage, flags,             //
                                                       wideCharStr, wideCharCount,  //
                                                       str.data(), strByteCount,    //
                                                       defaultChar, useDefaultChar);
    if (writtenByteCount != strByteCount)
        return {};

    return str;
#else
    return std::string{wsv.begin(), wsv.end()};
#endif
}

fs::path utf8_to_path(const std::string_view utf8) noexcept {
#if defined(_WIN32)
    if (utf8.size() > usize(std::numeric_limits<int>::max()))
        return {};

    const LPCCH multiByteStr   = utf8.data();
    const int   multiByteCount = int(utf8.size());

    // First attempt UTF-8, then fall back to ANSI for old GUIs like Arena
    constexpr Array<UINT, 2> CodePages{CP_UTF8, CP_ACP};

    for (const UINT codePage : CodePages)
    {
        const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;

        const int wideCharCount = ::MultiByteToWideChar(codePage, flags,               //
                                                        multiByteStr, multiByteCount,  //
                                                        nullptr, 0);
        if (wideCharCount <= 0)
            continue;

        std::wstring widePath(usize(wideCharCount), L'\0');

        const int writtenCharCount = ::MultiByteToWideChar(codePage, flags,               //
                                                           multiByteStr, multiByteCount,  //
                                                           widePath.data(), wideCharCount);
        if (writtenCharCount != wideCharCount)
            continue;

        return {widePath};
    }
#endif
    return {utf8};
}

}  // namespace DON
