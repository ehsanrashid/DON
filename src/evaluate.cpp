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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

#include "uci.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_common.h"
#include "nnue/nnue_misc.h"

namespace DON {

// Evaluate is the evaluator for the outer world. It returns a static evaluation
// of the position from the point of view of the side to move.
Value evaluate(const Position&          pos,
               const NNUE::Networks&    networks,
               NNUE::AccumulatorCaches& accCaches,
               std::int32_t             optimism) noexcept {
    assert(!pos.checkers());

    NNUE::NetworkOutput netOut{0, 0};

    auto compute_nnue = [&netOut = std::as_const(netOut)]() noexcept -> std::int32_t {
        constexpr std::uint8_t delta = 3;
        return ((128 - delta) * netOut.psqt + (128 + delta) * netOut.positional)
             / (128 * NNUE::OUTPUT_SCALE);
    };

    std::int32_t nnue = 0;

    bool netSmall = use_small_net(pos);
    if (netSmall)
    {
        netOut = networks.small.evaluate(pos, &accCaches.small);

        nnue = compute_nnue();

        // Re-evaluate the position when higher eval accuracy is worth the time spent
        netSmall = std::signbit(nnue) == std::signbit(netOut.psqt)
                && std::abs(nnue) > 225 + 2 * pos.count<ALL_PIECE>();
    }
    if (!netSmall)
    {
        netOut = networks.big.evaluate(pos, &accCaches.big);

        nnue = compute_nnue();
    }

    // Blend nnue and optimism with complexity
    // clang-format off
    std::int32_t complexity = std::abs(netOut.psqt - netOut.positional) / NNUE::OUTPUT_SCALE;

    nnue     -= nnue     * complexity * (55.9315e-6 - 06.5073e-6 * netSmall);
    optimism += optimism * complexity * (21.3675e-4);

    Value material = pos.count<PAWN>() * (532 + 21 * netSmall)
                   + pos.non_pawn_material();

    std::int32_t v = (nnue + 0.1 * optimism)
                   + (nnue + 1.0 * optimism) * material * 12.8573e-6
                   + pos.bonus();
    // clang-format on

    // Damp down the evaluation linearly when shuffling
    v *= (1.0 - 5.7142e-3 * pos.rule50_count());

    // Guarantee evaluation does not hit the tablebase range
    return std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}

// Like evaluate(), but instead of returning a value, it returns
// a string (suitable for outputting to stdout) that contains the detailed
// descriptions and values of each evaluation term. Useful for debugging.
// Trace scores are from white's point of view
std::string trace(Position& pos, const NNUE::Networks& networks) noexcept {
    if (pos.checkers())
        return "Final evaluation: none (in check)";

    std::ostringstream oss;

    auto accCaches = std::make_unique<NNUE::AccumulatorCaches>(networks);

    oss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    oss << '\n' << NNUE::trace(pos, networks, *accCaches) << '\n';

    oss << std::showpoint << std::showpos << std::fixed << std::setprecision(2);

    Value v;

    auto netOut = networks.big.evaluate(pos, &accCaches->big);

    v = (netOut.psqt + netOut.positional) / NNUE::OUTPUT_SCALE;
    v = pos.active_color() == WHITE ? +v : -v;
    oss << "NNUE evaluation        " << 0.01 * UCI::to_cp(v, pos) << " (white side)\n";

    v = evaluate(pos, networks, *accCaches);
    v = pos.active_color() == WHITE ? +v : -v;
    oss << "Final evaluation       " << 0.01 * UCI::to_cp(v, pos) << " (white side)";
    oss << " [with scaled NNUE, ...]\n";

    return oss.str();
}

}  // namespace DON