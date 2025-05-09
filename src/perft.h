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

#ifndef PERFT_H_INCLUDED
#define PERFT_H_INCLUDED

#include <cstddef>
#include <cstdint>

#include "types.h"

namespace DON {

class Position;
class ThreadPool;

namespace Benchmark {

std::uint64_t perft(Position&   pos,
                    std::size_t ptSize,
                    ThreadPool& threads,
                    Depth       depth,
                    bool        detail = false) noexcept;

}  // namespace Benchmark
}  // namespace DON

#endif  // #ifndef PERFT_H_INCLUDED
