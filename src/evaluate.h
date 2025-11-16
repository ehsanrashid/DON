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

#include <cstdint>
#include <string>

#include "types.h"

namespace DON {

// The default net name MUST follow the format nn-[SHA256 first 12 digits].nnue
// for the build process (profile-build and fishtest) to work.
// Do not change the name of the macro or the location where this macro is defined,
// as it is used in the Makefile/Fishtest.
#define EvalFileDefaultNameBig "nn-c0ae49f08b40.nnue"
#define EvalFileDefaultNameSmall "nn-37f18f62d772.nnue"

class Position;

namespace NNUE {
struct Networks;
struct AccumulatorCaches;
struct AccumulatorStack;
}  // namespace NNUE

Value evaluate(const Position&          pos,
               const NNUE::Networks&    networks,
               NNUE::AccumulatorStack&  accStack,
               NNUE::AccumulatorCaches& accCaches,
               std::int32_t             optimism = 0) noexcept;

std::string trace(Position& pos, const NNUE::Networks& networks) noexcept;

}  // namespace DON

#endif  // #ifndef EVALUATE_H_INCLUDED
