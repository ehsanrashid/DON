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

#ifndef SYZYGY_TBBASE_H_INCLUDED
#define SYZYGY_TBBASE_H_INCLUDED

#include <cstdint>
#include <functional>
#include <string_view>

#include "../types.h"

namespace DON {

class Position;
class RootMoves;
class Options;

using TimeFunc = std::function<bool()>;

namespace Tablebases {

enum WDLScore : std::int8_t {
    WDL_LOSS         = -2,  // Loss
    WDL_BLESSED_LOSS = -1,  // Loss, but draw under 50-move rule
    WDL_DRAW         = 0,   // Draw
    WDL_CURSED_WIN   = +1,  // Win, but draw under 50-move rule
    WDL_WIN          = +2,  // Win
};
constexpr WDLScore operator-(WDLScore wdlScore) noexcept { return WDLScore(-int(wdlScore)); }

// Possible states after a probing operation
enum ProbeState : std::int8_t {
    PS_FAIL              = 0,   // Probe failed (missing file table)
    PS_OK                = +1,  // Probe successful
    PS_AC_CHANGED        = -1,  // DTZ should check the other side
    PS_BEST_MOVE_ZEROING = +2   // Best move zeroes DTZ (capture or pawn move)
};

struct Config final {
    bool         rootInTB     = false;
    std::uint8_t cardinality  = 0;
    Depth        probeDepth   = DEPTH_ZERO;
    bool         rule50Active = false;
};

inline std::uint8_t MaxCardinality;

void init() noexcept;
void init(std::string_view paths) noexcept;

WDLScore probe_wdl(Position& pos, ProbeState* ps) noexcept;
int      probe_dtz(Position& pos, ProbeState* ps) noexcept;

// clang-format off

bool probe_root_dtz(Position& pos, RootMoves& rootMoves, bool rule50Active, bool rankDTZ = false, TimeFunc time_to_abort = []() { return false; }) noexcept;
bool probe_root_wdl(Position& pos, RootMoves& rootMoves, bool rule50Active) noexcept;

Config rank_root_moves(Position& pos, RootMoves& rootMoves, const Options& options, bool rankDTZ = false, TimeFunc time_to_abort = []() { return false; }) noexcept;

// clang-format on

}  // namespace Tablebases
}  // namespace DON

#endif  // #ifndef TBPROBE_H_INCLUDED
