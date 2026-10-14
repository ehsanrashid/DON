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

#if defined(_WIN32)
    #include "../platform_win.h"  // GetCommandLineW(), PreFetchCacheLine()
    #include <shellapi.h>         // CommandLineToArgvW()
#endif

#include "../utility/command_line.h"
#include "../utility/types.h"

namespace DON {

CommandLine::CommandLine(const int argc, const char* const argv[]) noexcept {
#if defined(_WIN32)
    int wide_argc;
    if (LPWSTR* wide_argv = ::CommandLineToArgvW(::GetCommandLineW(), &wide_argc);  //
        wide_argv != nullptr)
    {
        const usize utf8_argc = usize(wide_argc);

        utf8_arguments.reserve(utf8_argc);

        for (usize i = 0; i < utf8_argc; ++i)
            utf8_arguments.emplace_back(wstring_to_utf8(wide_argv[i]));

        ::LocalFree(wide_argv);

        arguments_.reserve(utf8_arguments.size());

        for (const auto& utf8_argv : utf8_arguments)
            arguments_.emplace_back(std::string_view{utf8_argv});

        return;
    }
#endif

    const usize u_argc = usize(argc);

    arguments_.reserve(u_argc);

    for (usize i = 0; i < u_argc; ++i)
        arguments_.emplace_back(argv[i]);  // Store non-owning views.
}

fs::path CommandLine::binary_directory(fs::path path) noexcept {
#if defined(_WIN32)
    // Prefer the executable path reported by Windows.
    // Unlike _get_wpgmptr(), this does not depend on the CRT entry-point variant.
    // Windows paths cannot exceed 32767 characters, so a fixed buffer is sufficient.
    // Falls back to path if the API fails.
    Array<WCHAR, 0x8000> filename{};
    const DWORD length = ::GetModuleFileNameW(nullptr, filename.data(), DWORD(filename.size()));
    if (length != 0 && length < filename.size())
        path = fs::path{filename.data(), filename.data() + length};
#endif

    const auto binaryDirectory{path.parent_path()};
    return binaryDirectory.empty() ? fs::path(".") : binaryDirectory;
}

fs::path CommandLine::working_directory() noexcept { return fs::current_path(); }

const std::vector<std::string_view>& CommandLine::arguments() const noexcept { return arguments_; }

}  // namespace DON
