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

#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include <filesystem>
#include <iosfwd>
#include <string_view>

#include "engine.h"
#include "misc.h"
#include "types.h"

namespace DON {

class Options;

inline constexpr std::string_view QuitCommand{"quit"};

class UCI final {
   public:
    UCI(const std::filesystem::path& path = {}) noexcept;

    Options& options() noexcept;

    void process_input(std::istream& is) noexcept;

    void execute(std::string_view command) noexcept;

   private:
    UCI() noexcept                      = delete;
    UCI(const UCI&) noexcept            = delete;
    UCI& operator=(const UCI&) noexcept = delete;
    UCI(UCI&&) noexcept                 = delete;
    UCI& operator=(UCI&&) noexcept      = delete;

    void set_update_callbacks() noexcept;

    void position(std::istream& is) noexcept;
    void go(std::istream& is) noexcept;
    void setoption(std::istream& is) noexcept;
    void bench(std::istream& is) noexcept;
    void benchmark(std::istream& is) noexcept;

    u64 perft(Depth depth, bool detail = false) noexcept;

    Engine engine;
};

}  // namespace DON

#endif  // UCI_H_INCLUDED
