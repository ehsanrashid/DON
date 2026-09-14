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

#ifndef BENCHMARK_H_INCLUDED
#define BENCHMARK_H_INCLUDED

#include <iosfwd>
#include <string>
#include <string_view>

#include "misc.h"
#include "types.h"

namespace DON::Benchmark {

// Builds a list of UCI commands to be run by bench.
// There are five parameters:
// - size of hash (TT) in MB, (default 16)
// - number of threads that should be used, (default 1)
// - limit value spent for each position, (default 13)
// - fen filename where to look for positions in FEN format, (default "default")
// - limit type: depth, perft, nodes and movetime (in milliseconds). (default "depth")
// Examples:
//
// bench                            : search default positions with 1 thread up to depth 13 (TT = 16MB)
// bench 64 1 15                    : search default positions with 1 thread up to depth 15 (TT = 64MB)
// bench 64 1 100000 default nodes  : search default positions with 1 thread for 100K nodes each (TT = 64MB)
// bench 64 4 5000 current movetime : search current position with 4 threads for 5 seconds (TT = 64MB)
// bench 16 1 5 test.epd perft      : run perft 5 on positions in file "test.epd" (TT = 16MB)
Strings bench(std::istream& is, std::string_view currentFen = START_FEN) noexcept;

struct Setup final {
   public:
    usize       threads;
    usize       ttSize;
    std::string originalInvocation;
    std::string currentInvocation;
    Strings     commands;
};

// Examples:
// benchmark [threads] [hash_MiB = 128] [time_s = 150]
Setup benchmark(std::istream& is) noexcept;

}  // namespace DON::Benchmark

#endif  // BENCHMARK_H_INCLUDED
