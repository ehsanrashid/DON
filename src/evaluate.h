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

#ifndef EVALUATE_H_INCLUDED
#define EVALUATE_H_INCLUDED

#include <string>

#include "misc.h"
#include "types.h"

namespace DON {

// The default net name must follow the format nn-[SHA256 first 12 digits].nnue
// for the build process (profile-build) to work.
// It is also used in the Makefile.
#define EvalFileDefaultName "nn-71d6d32cb962.nnue"

class Position;

namespace NNUE {
class Network;
struct AccumulatorCache;
struct AccumulatorStack;
}  // namespace NNUE

namespace Evaluate {

Value evaluate(const Position&         pos,
               const NNUE::Network&    network,
               NNUE::AccumulatorCache& accCache,
               NNUE::AccumulatorStack& accStack,
               i32                     optimism = 0) noexcept;

std::string trace(Position& pos, const NNUE::Network& network) noexcept;

}  // namespace Evaluate
}  // namespace DON

#endif  // EVALUATE_H_INCLUDED
