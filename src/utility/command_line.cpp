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

#include "../utility/command_line.h"

#include <cassert>

#if defined(_WIN32)
    #include "../utility/platform_win.h"  // GetCommandLineW(), PreFetchCacheLine()
    #include <shellapi.h>                 // CommandLineToArgvW()
    #include "../utility/functions.h"
#endif

#include "../utility/types.h"

namespace DON {

CommandLine::CommandLine(const int argc, const char* const argv[]) noexcept {
    if (argc <= 0 || argv == nullptr)
        return;
#if defined(_WIN32)
    int w_argc = 0;
    if (LPWSTR* w_argv = ::CommandLineToArgvW(::GetCommandLineW(), &w_argc);  //
        w_argv != nullptr)
    {
        const usize utf8_argc = usize(w_argc);

        utf8_arguments.reserve(utf8_argc);

        for (usize i = 0; i < utf8_argc; ++i)
            utf8_arguments.emplace_back(wstring_to_utf8(w_argv[i]));

        ::LocalFree(w_argv);

        sv_arguments.reserve(utf8_arguments.size());

        for (const auto& utf8_argv : utf8_arguments)
            sv_arguments.emplace_back(utf8_argv);

        return;
    }
#endif

    const usize u_argc = usize(argc);

    sv_arguments.reserve(u_argc);

    for (usize i = 0; i < u_argc; ++i)
        sv_arguments.emplace_back(argv[i]);  // Store non-owning views.
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

usize CommandLine::size() const noexcept { return arguments().size(); }

bool CommandLine::empty() const noexcept { return arguments().empty(); }

const std::vector<std::string_view>& CommandLine::arguments() const noexcept {
    return sv_arguments;
}

std::string_view CommandLine::operator[](const usize idx) const noexcept {
    assert(idx < size());
    return arguments()[idx];
}

std::string CommandLine::join_arguments(const usize beg) const noexcept {
    std::string join;

    if (beg < size())
    {
        join.reserve(KB / 2);

        join.append(arguments()[beg]);

        for (usize i = beg + 1; i < size(); ++i)
        {
            join.push_back(' ');
            join.append(arguments()[i]);
        }
    }

    return join;
}

std::string CommandLine::to_string() const noexcept { return join_arguments(0); }

std::string CommandLine::arguments_string() const noexcept { return join_arguments(1); }

}  // namespace DON
