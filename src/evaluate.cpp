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

#include "evaluate.h"

#include <algorithm>  // IWYU pragma: keep
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

#include "position.h"
#include "uci.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_common.h"
#include "nnue/nnue_misc.h"

namespace DON {

bool use_small_net(const Position& pos) noexcept { return std::abs(pos.evaluate()) > 962; }

// Evaluate is the evaluator for the outer world. It returns a static evaluation
// of the position from the point of view of the side to move.
Value evaluate(const Position&          pos,
               const NNUE::Networks&    networks,
               NNUE::AccumulatorCaches& accCaches,
               std::int32_t             optimism) noexcept {
    assert(!pos.checkers());

    NNUE::NetworkOutput netOut{0, 0};

    bool smallNetUse = use_small_net(pos);

    const auto compute_nnue = [&netOut = std::as_const(netOut)]() noexcept -> std::int32_t {
        return (1004 * netOut.psqt + 1044 * netOut.positional) / (1024 * NNUE::OUTPUT_SCALE);
    };

    std::int32_t nnue = 0;

    Value bonus = pos.bonus();

    if (smallNetUse)
    {
        netOut = networks.small.evaluate(pos, &accCaches.small);

        netOut.positional += bonus;

        nnue = compute_nnue();

        // Re-evaluate with the big-net if the small-net's NNUE evaluation is below a certain threshold
        smallNetUse = std::abs(nnue) >= 236;
    }
    if (!smallNetUse)
    {
        netOut = networks.big.evaluate(pos, &accCaches.big);

        netOut.positional += bonus;

        nnue = compute_nnue();
    }

    // Blend nnue and optimism with complexity
    // clang-format off
    std::int32_t complexity = std::abs(netOut.psqt - netOut.positional) / NNUE::OUTPUT_SCALE;

    nnue     = std::lround(nnue     * std::max(1.0f - 51.8753e-6f * complexity, 0.0001f));
    optimism = std::lround(optimism *         (1.0f + 18.7617e-4f * complexity));

    std::int32_t v = (0.9513f * nnue + 0.08737f * optimism)
                   + (1.0000f * nnue + 0.99999f * optimism) * pos.material() * 12.4642e-6f;
    // clang-format on

    // Damp down the evaluation linearly when shuffling
    auto rule50 = pos.rule50_count();
    auto damp =
      std::max(1.0f - 2.0e-3f * (rule50 <= 4 ? rule50 : 4 + 3.3333f * (rule50 - 4)), 0.0f);

    v = std::lround(v * damp);

    // Guarantee evaluation does not hit the table-base range
    return in_range(v);
}

// Like evaluate(), but instead of returning a value, it returns
// a string (suitable for outputting to stdout) that contains the detailed
// descriptions and values of each evaluation term. Useful for debugging.
// Trace scores are from white's point of view
std::string trace(Position&             pos,  //
                  const NNUE::Networks& networks) noexcept {
    if (pos.checkers())
        return "Final evaluation     : none (in check)";

    auto accCaches = std::make_unique<NNUE::AccumulatorCaches>(networks);

    std::ostringstream oss;

    oss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    oss << '\n' << NNUE::trace(pos, networks, *accCaches) << '\n';

    oss << std::showpoint << std::showpos << std::fixed << std::setprecision(2);

    Value v;

    auto netOut = networks.big.evaluate(pos, &accCaches->big);

    v = (netOut.psqt + netOut.positional) / NNUE::OUTPUT_SCALE;
    v = pos.active_color() == WHITE ? +v : -v;
    oss << "NNUE evaluation      : " << 0.01f * UCI::to_cp(v, pos) << " (white side)\n";

    v = evaluate(pos, networks, *accCaches);
    v = pos.active_color() == WHITE ? +v : -v;
    oss << "Final evaluation     : " << 0.01f * UCI::to_cp(v, pos) << " (white side)";
    oss << " [with scaled NNUE, ...]\n";

    return oss.str();
}

}  // namespace DON
