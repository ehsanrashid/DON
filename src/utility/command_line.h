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

#ifndef UTILITY_COMMAND_LINE_H_INCLUDED
#define UTILITY_COMMAND_LINE_H_INCLUDED

#include <filesystem>
#include <string_view>
#include <vector>
#if defined(_WIN32)
    #include <string>
#endif

#include "../utility/types.h"

namespace DON {

struct CommandLine final {
   public:
    CommandLine(const int argc, const char* const argv[]) noexcept;

    // Returns the directory containing the executable, or "." if the directory is empty.
    // This is useful because, when the NNUE network is not embedded in the binary,
    // this directory is one of the locations to look for the NNUE file.
    static fs::path binary_directory(fs::path path) noexcept;
    // Returns the process's current working directory.
    static fs::path working_directory() noexcept;

    [[nodiscard]] const std::vector<std::string_view>& arguments() const noexcept;

   private:
    CommandLine(const CommandLine&)            = delete;
    CommandLine& operator=(const CommandLine&) = delete;
    CommandLine(CommandLine&&)                 = delete;
    CommandLine& operator=(CommandLine&&)      = delete;

    std::vector<std::string_view> arguments_;
#if defined(_WIN32)
    std::vector<std::string> utf8_arguments;
#endif
};

}  // namespace DON

#endif  // UTILITY_COMMAND_LINE_H_INCLUDED
