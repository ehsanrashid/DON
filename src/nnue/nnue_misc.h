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

#ifndef NNUE_MISC_H_INCLUDED
#define NNUE_MISC_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <iosfwd>  // IWYU pragma: keep
#include <string>

#include "nnue_architecture.h"

namespace DON {

class Position;

namespace NNUE {

struct Networks;
struct AccumulatorCaches;

struct EvalFile final {
    // Default net name, will use one of the EvalFileDefaultName* macros defined
    // in evaluate.h
    std::string defaultName;
    // Selected net name, either via uci option or default
    std::string current;
    // Net description extracted from the net file
    std::string netDescription;
};

struct NetworkOutput final {
    std::int32_t psqt;
    std::int32_t positional;
};

struct EvalTrace final {
    static_assert(LayerStacks == PSQTBuckets);

    NetworkOutput netOut[LayerStacks];
    std::size_t   correctBucket;
};

std::string trace(Position&          pos,  //
                  const Networks&    networks,
                  AccumulatorCaches& caches) noexcept;

}  // namespace NNUE
}  // namespace DON

#endif  // #ifndef NNUE_MISC_H_INCLUDED
