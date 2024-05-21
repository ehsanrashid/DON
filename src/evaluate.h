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

#ifndef EVALUATE_H_INCLUDED
#define EVALUATE_H_INCLUDED

#include <string>

#include "position.h"
#include "types.h"

namespace DON {
namespace Eval {

// The default net name MUST follow the format nn-[SHA256 first 12 digits].nnue
// for the build process (profile-build and fishtest) to work. Do not change the
// name of the macro or the location where this macro is defined, as it is used
// in the Makefile/Fishtest.
#define EvalFileDefaultNameBig "nn-c721dfca8cd3.nnue"
#define EvalFileDefaultNameSmall "nn-baff1ede1f90.nnue"

namespace NNUE {
struct Networks;
struct AccumulatorCaches;
}  // namespace NNUE

inline bool use_small_net(Value absEval, const Position& pos) noexcept {
    return (absEval > 1126 + 6 * pos.count<PAWN>())
        || (absEval > 975 - 36 * pos.count<PAWN>() && pos.count<ALL_PIECE>() <= 6);
}

Value evaluate(const Position&          pos,
               const NNUE::Networks&    networks,
               NNUE::AccumulatorCaches& accCaches,
               Value                    optimism = VALUE_ZERO) noexcept;

std::string trace(Position& pos, const NNUE::Networks& networks) noexcept;

}  // namespace Eval
}  // namespace DON

#endif  // #ifndef EVALUATE_H_INCLUDED
