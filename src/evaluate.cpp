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

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

#include "nnue/accumulator.h"
#include "nnue/network.h"
#include "nnue/nmisc.h"
#include "position.h"
#include "uci.h"

namespace DON::Evaluate {

// Evaluate is the evaluator for the outer world.
// It returns a static evaluation of the position
// from the point of view of the side to move.
Value evaluate(const Position&          pos,
               const NNUE::Networks&    networks,
               NNUE::AccumulatorStack&  accStack,
               NNUE::AccumulatorCaches& accCaches,
               std::int32_t             optimism) noexcept {
    assert(pos.checkers_bb() == 0);

    Value absEvaluate = std::abs(pos.evaluate());

    bool smallNet = absEvaluate > 962;

    NNUE::NetworkOutput netOut{0, 0};

    const auto compute_nnue = [&netOut = std::as_const(netOut)]() noexcept -> std::int32_t {
        return (125 * netOut.psqt + 131 * netOut.positional) / 128;
    };

    std::int32_t nnue = 0;

    if (smallNet)
    {
        netOut = networks.small.evaluate(pos, accStack, accCaches.small);
        nnue   = compute_nnue();

        // Re-evaluate with the big-net if the small-net's NNUE evaluation is below a certain threshold
        if (std::abs(nnue) < 277)
        {
            smallNet = false;

            netOut = networks.big.evaluate(pos, accStack, accCaches.big);
            nnue   = compute_nnue();
        }
    }
    else
    {
        netOut = networks.big.evaluate(pos, accStack, accCaches.big);
        nnue   = compute_nnue();
    }

    std::int32_t complexity = std::abs(netOut.psqt - netOut.positional);

    // clang-format off

    // Blend nnue and optimism with complexity
    nnue     *= 1.0 - 54.8366e-6 * complexity;
    optimism *= 1.0 + 21.0084e-4 * complexity;

    std::int32_t v = int( nnue + int(92.3450e-3 * optimism))
                   + int((nnue +                  optimism) * pos.material() * 12.8417e-6);
    // clang-format on

    // Damp down the evaluation linearly when shuffling
    auto dampFactor = 1.0 - 5.0505e-3 * pos.rule50_count();

    if (dampFactor < 0.0)
        dampFactor = 0.0;

    v *= dampFactor;

    // Guarantee evaluation does not hit the table-base range
    return in_range(v);
}

// Like evaluate(), but instead of returning a value,
// it returns a string (suitable for outputting to stdout)
// that contains the detailed descriptions and values of each evaluation term.
// Trace scores are from white's point of view.
std::string trace(Position& pos, const NNUE::Networks& networks) noexcept {
    if (pos.checkers_bb() != 0)
        return "Final evaluation     : none (in check)";

    auto accStack  = std::make_unique<NNUE::AccumulatorStack>();
    auto accCaches = std::make_unique<NNUE::AccumulatorCaches>(networks);

    std::ostringstream oss;

    oss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    oss << '\n' << NNUE::trace(pos, networks, *accCaches) << '\n';

    oss << std::showpoint << std::showpos << std::fixed << std::setprecision(2);

    auto netOut = networks.big.evaluate(pos, *accStack, accCaches->big);

    Value v;
    v = netOut.psqt + netOut.positional;
    v = pos.active_color() == WHITE ? +v : -v;
    oss << "NNUE evaluation      : " << 0.01 * UCI::to_cp(v, pos) << " (white side)\n";

    v = evaluate(pos, networks, *accStack, *accCaches);
    v = pos.active_color() == WHITE ? +v : -v;
    oss << "Final evaluation     : " << 0.01 * UCI::to_cp(v, pos) << " (white side)";
    oss << " [with scaled NNUE, ...]\n";

    return oss.str();
}

}  // namespace DON::Evaluate
