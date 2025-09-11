/*
  DON, a UCI chess playing engine derived from Stockfish

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

#ifndef BENCHMARK_H_INCLUDED
#define BENCHMARK_H_INCLUDED

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>

#include "misc.h"
#include "types.h"

namespace DON::Benchmark {

Strings setup_bench(std::istringstream& iss, std::string_view currentFen = START_FEN) noexcept;

struct Benchmark final {
    std::size_t threads;
    std::size_t ttSize;
    Strings     commands;
    std::string originalInvocation;
    std::string filledInvocation;
};

Benchmark setup_benchmark(std::istringstream& iss) noexcept;

}  // namespace DON::Benchmark

#endif  // #ifndef BENCHMARK_H_INCLUDED
