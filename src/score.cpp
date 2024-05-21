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

#include "score.h"

#include <cassert>
#include <cmath>

#include "uci.h"

namespace DON {

Score::Score(Value v, const Position& pos) noexcept {
    assert(-VALUE_INFINITE < v && v < +VALUE_INFINITE);

    Value absV = std::abs(v);
    if (absV < VALUE_TB_WIN_IN_MAX_PLY)
    {
        variant = Unit{UCI::to_cp(v, pos)};
    }
    else if (absV <= VALUE_TB)
    {
        auto distance = VALUE_TB - absV;
        variant       = v > 0 ? Tablebase{+distance, true} : Tablebase{-distance, false};
    }
    else
    {
        auto distance = VALUE_MATE - absV;
        variant       = Mate{v > 0 ? +distance : -distance};
    }
}

}  // namespace DON
