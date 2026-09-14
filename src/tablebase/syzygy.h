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

#ifndef TABLEBASE_SYZYGY_H_INCLUDED
#define TABLEBASE_SYZYGY_H_INCLUDED

#include <functional>
#include <string_view>

#include "../misc.h"
#include "../types.h"

namespace DON {

class Options;
class Position;
class RootMoves;

namespace Tablebase::Syzygy {

using TimeFunc = std::function<bool()>;

// Max number of supported piece
inline constexpr usize TB_PIECES_MAX = 7;

enum WDLScore : i8 {
    WDL_LOSS         = -2,  // Loss
    WDL_BLESSED_LOSS = -1,  // Loss, but draw under 50-move rule
    WDL_DRAW         = 0,   // Draw
    WDL_CURSED_WIN   = +1,  // Win, but draw under 50-move rule
    WDL_WIN          = +2,  // Win
};

inline constexpr usize WDL_SCORE_NB = 5;

constexpr WDLScore operator-(WDLScore wdlScore) noexcept { return WDLScore(-int(wdlScore)); }

// Normalize any WDLScore to pure outcome: WDL_LOSS, WDL_DRAW, WDL_WIN
constexpr WDLScore normalize_wdl(WDLScore wdlScore) noexcept {
    return WDLScore(2 * ((wdlScore > WDL_DRAW) - (wdlScore < WDL_DRAW)));
}

[[nodiscard]] constexpr std::string_view to_string(const WDLScore wdlScore) noexcept {
    switch (wdlScore)
    {
    case WDL_LOSS :
        return "Loss";
    case WDL_BLESSED_LOSS :
        return "Blessed loss";
    case WDL_DRAW :
        return "Draw";
    case WDL_CURSED_WIN :
        return "Cursed win";
    case WDL_WIN :
        return "Win";
    }
    return "None";
}

// Possible states after a probing operation
enum ProbeState : u8 {
    PS_FAIL              = 0,   // Probe failed (missing file table)
    PS_OK                = +1,  // Probe successful
    PS_AC_CHANGED        = +2,  // DTZ should check the other side
    PS_BEST_MOVE_ZEROING = +3   // Best move zeroes DTZ (capture or pawn move)
};

[[nodiscard]] constexpr std::string_view to_string(const ProbeState ps) noexcept {
    switch (ps)
    {
    case PS_FAIL :
        return "Failed";
    case PS_OK :
        return "Success";
    case PS_AC_CHANGED :
        return "Active color changed";
    case PS_BEST_MOVE_ZEROING :
        return "Best move zeroing";
    }
    return "None";
}

struct Config final {
   public:
    bool  rootInTB    = false;
    u8    cardinality = 0;
    Depth probeDepth  = DEPTH_ZERO;
    bool  useRule50   = false;
};

inline u8 MaxCardinality;

// Called at startup to create the various tables
void init() noexcept;

// Called after every change to "SyzygyPath" UCI option
// to (re)create the various tables.
// It is not thread safe, nor it needs to be.
void init(std::string_view paths) noexcept;

// Probe the WDL table for a particular position.
// If *ps != FAIL, the probe was successful.
// The return WDL-score is from the point of view of the side to move:
// -2 : loss
// -1 : loss, but draw under 50-move rule
//  0 : draw
//  1 : win, but draw under 50-move rule
//  2 : win
WDLScore probe_wdl(Position& pos, ProbeState* ps) noexcept;

// Probe the DTZ table for a particular position.
// If *ps != FAIL, the probe was successful.
// The return WDL-score is from the point of view of the side to move:
//         n < -100 : loss, but draw under 50-move rule
// -100 <= n < -1   : loss in n ply (assuming 50-move counter == 0)
//        -1        : loss, the side to move is mated
//         0        : draw
//     1 < n <= 100 : win in n ply (assuming 50-move counter == 0)
//   100 < n        : win, but draw under 50-move rule
//
// The return WDL-score n can be off by 1:
//  - return WDL-score -n can mean a loss in n+1 ply and
//  - return WDL-score +n can mean a win in n+1 ply.
// This cannot happen for tables with positions exactly
// on the "edge" of the 50-move rule.
//
// This implies that if DTZ-score > 0 is returned,
// the position is certainly a win if DTZ-score + 50-move-counter < 100.
// Care must be taken that the engine picks moves that preserve DTZ-score + 50-move-counter < 100.
//
// If n = 100 immediately after a capture or pawn move,
// then the position is also certainly a win, and during the whole phase until the next
// capture or pawn move, the inequality to be preserved is DTZ-score + 50-move-counter <= 100.
int probe_dtz(Position& pos, ProbeState* ps) noexcept;

// Use the WDL-tables to rank root moves.
// This is a fallback for the case that some or all DTZ-tables are missing.
//
// A return value false indicates that not all probes were successful.
bool rank_root_moves_wdl(Position& pos, RootMoves& rootMoves, bool useRule50) noexcept;

// Use the DTZ-tables to rank root moves.
//
// A return value false indicates that not all probes were successful.
bool rank_root_moves_dtz(
  Position&  pos,
  RootMoves& rootMoves,
  bool       useRule50,
  bool       rankDTZ       = false,
  TimeFunc   time_to_abort = []() { return false; }) noexcept;

Config rank_root_moves(
  Position&      pos,
  RootMoves&     rootMoves,
  const Options& options,
  bool           rankDTZ       = false,
  TimeFunc       time_to_abort = []() { return false; }) noexcept;

}  // namespace Tablebase::Syzygy
}  // namespace DON

#endif  // TABLEBASE_SYZYGY_H_INCLUDED
