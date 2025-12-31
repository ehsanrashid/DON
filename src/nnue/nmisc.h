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

#ifndef NNUE_NMISC_H_INCLUDED
#define NNUE_NMISC_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "../misc.h"
#include "architecture.h"

namespace DON {

class Position;

namespace NNUE {

struct Networks;
struct AccumulatorCaches;

// EvalFile uses fixed string types because it's part of the network structure which must be trivial.
struct EvalFile final {
   public:
    // Default net name, will use the *EvalFileDefaultName macros defined in evaluate.h
    FixedString<256> defaultName;
    // Selected net name, either via uci option or default
    FixedString<256> currentName;
    // Net description extracted from the net file
    FixedString<256> netDescription;
};

struct NetworkOutput final {
   public:
    std::int32_t psqt;
    std::int32_t positional;
};

struct NetworkTrace final {
   public:
    StdArray<NetworkOutput, LayerStacks> netOut;
    std::size_t                          correctBucket;
};

std::string trace(Position& pos, const Networks& networks, AccumulatorCaches& accCaches) noexcept;

}  // namespace NNUE
}  // namespace DON

template<>
struct std::hash<DON::NNUE::EvalFile> {
    std::size_t operator()(const DON::NNUE::EvalFile& evalFile) const noexcept {
        std::size_t h = 0;
        DON::combine_hash(h, evalFile.defaultName);
        DON::combine_hash(h, evalFile.currentName);
        DON::combine_hash(h, evalFile.netDescription);
        return h;
    }
};

#endif  // #ifndef NNUE_NMISC_H_INCLUDED
