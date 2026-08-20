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

#ifndef NNUE_NMISC_H_INCLUDED
#define NNUE_NMISC_H_INCLUDED

#include <filesystem>
#include <string>
#include <string_view>
#include <optional>
#include <utility>

#include "../evaluate.h"
#include "../misc.h"
#include "architecture.h"

namespace DON {

class Position;

namespace NNUE {

class Network;
struct AccumulatorCache;

// EvalFile stores the currently selected evaluation network and its metadata.
// The network path may be explicitly selected through a UCI option or fall back to the default network,
// while the description is extracted from the loaded network file.
struct EvalFile final {
   public:
    EvalFile(std::optional<std::filesystem::path> curPath = std::nullopt,
             std::string_view                     netDesc = {}) noexcept :
        currentPath(std::move(curPath)),
        netDescription(netDesc) {}

    // Default net name, will use the EvalFileDefaultName macros defined in evaluate.h
    static constexpr std::string_view DefaultName = EvalFileDefaultName;
    // Selected net path, either via UCI option or default
    std::optional<std::filesystem::path> currentPath;
    // Net description extracted from the net file
    std::string netDescription;
};

struct NetworkOutput final {
   public:
    i32 psqt;
    i32 positional;
};

struct NetworkTrace final {
   public:
    Array<NetworkOutput, LayerStacks> netOut;
    usize                             correctBucket;
};

int loop_mics();

}  // namespace NNUE
}  // namespace DON

#endif  // #ifndef NNUE_NMISC_H_INCLUDED
