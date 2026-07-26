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

#include "evaluate.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <memory>

#include "misc.h"
#include "position.h"
#include "notation.h"
#include "nnue/accumulator.h"
#include "nnue/network.h"
#include "nnue/nmisc.h"

namespace DON::Evaluate {

// Evaluate is the evaluator for the outer world.
// It returns a static evaluation of the position
// from the point of view of the side to move.
Value evaluate(const Position&          pos,
               const NNUE::Network&     network,
               NNUE::AccumulatorCaches& accCaches,
               NNUE::AccumulatorStack&  accStack,
               i32                      optimism) noexcept {
    assert(pos.checkers_bb() == 0);

    auto [psqt, positional] = network.evaluate(pos, accCaches, accStack);

    i32 nnue = (125 * psqt + 131 * positional) / 128;

    double complexity = constexpr_abs(psqt - positional);
    // Blend eval and optimism with complexity
    nnue     = constexpr_round(double(nnue) * (1.0 - 54.8366e-6 * complexity));
    optimism = constexpr_round(double(optimism) * (1.0 + 21.0084e-4 * complexity));

    i32 v = nnue
          + constexpr_round(
              12.8417e-6
              * (double(nnue + optimism) * double(pos.material()) + 32496.3930 * double(optimism)));

    // Damp evaluation linearly based on the 50-move rule
    v = constexpr_round(v * std::max(1.0 - 5.1021e-3 * double(pos.rule50_count()), 0.0));

    // Guarantee evaluation does not hit the table-base range
    return in_range(v);
}

// Like evaluate(), but instead of returning a value,
// it returns a string (suitable for outputting to stdout)
// that contains the detailed descriptions and values of each evaluation term.
// Trace scores are from white's point of view.
std::string trace(Position& pos, const NNUE::Network& network) noexcept {
    if (pos.checkers_bb() != 0)
        return "Final evaluation     : none (in check)";

    auto accCaches = std::make_unique<NNUE::AccumulatorCaches>(network);
    auto accStack  = std::make_unique<NNUE::AccumulatorStack>();

    auto fmt = [](double value) noexcept -> std::string {
        Array<char, 8> buffer{};

        int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "%+01.2f", value);
        usize copiedSize  = writtenSize > 0  //
                            ? std::min<usize>(writtenSize, buffer.size() - 1)
                            : 0;

        return std::string{buffer.data(), copiedSize};
    };

    std::string output;
    output.reserve(3 * KB);

    output.assign(NNUE::trace(pos, network, *accCaches)).push_back('\n');

    auto [psqt, positional] = network.evaluate(pos, *accCaches, *accStack);

    Value v;

    v = psqt + positional;
    v = pos.active_color() == WHITE ? +v : -v;

    output  //
      .append("NNUE evaluation      : ")
      .append(fmt(0.01 * to_cp(v, pos)))
      .append(" (white side)\n");

    v = evaluate(pos, network, *accCaches, *accStack);
    v = pos.active_color() == WHITE ? +v : -v;

    output  //
      .append("Final evaluation     : ")
      .append(fmt(0.01 * to_cp(v, pos)))
      .append(" (white side) [with scaled NNUE, ...]\n");

    return output;
}

}  // namespace DON::Evaluate
