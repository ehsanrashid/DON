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
namespace Eval {

// Evaluate is the evaluator for the outer world. It returns a static evaluation
// of the position from the point of view of the side to move.
Value evaluate(const Position&          pos,
               const NNUE::Networks&    networks,
               NNUE::AccumulatorCaches& accCaches,
               const Value              optimism) noexcept {
    assert(!pos.checkers());

    const Value Eval      = pos.evaluate();
    const Value AbsEval   = std::abs(Eval);
    const Value Bonus     = pos.bonus();
    const Value Materials = pos.materials();

    const short Delta = std::min(28 + pos.non_pawn_material(pos.side_to_move()) / 450, 44);

    // Blend nnue, optimism and complexity
    // clang-format off
    auto blend = [=](const Value Psqt, const Value Positional) {
        Value complexity = std::abs((Psqt - Positional)) / NNUE::OutputScale;
        // Give more value to positional than psqt
        Value nnue       = ((2048 - Delta) * Psqt + (2048 + Delta) * Positional) / (2048 * NNUE::OutputScale);
        nnue             = nnue     - nnue     * complexity / 19157;
        Value mOptimism  = optimism + optimism * complexity / 457;
        return (nnue      * (73921 + Materials)
              + mOptimism * ( 8112 + Materials)) / 73260
              + Bonus;
    };
    // clang-format on

    Value v;

    bool useBigNet = true;
    // Re-evaluate the position when higher eval accuracy is worth the time spent
    if (use_small_net(AbsEval, pos))
    {
        auto [psqt, positional] = networks.small.evaluate(pos, &accCaches.small);

        v = blend(psqt, positional);

        useBigNet = std::abs(v + Eval) < 1.229 * AbsEval;
    }
    if (useBigNet)
    {
        auto [psqt, positional] = networks.big.evaluate(pos, &accCaches.big);

        v = blend(psqt, positional);
    }

    const int shuffling = pos.rule50_count();
    // Damp down the evaluation linearly when shuffling
    v -= v * shuffling / 212;

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

    auto accCaches = std::make_unique<NNUE::AccumulatorCaches>(networks);

    std::ostringstream oss;
    oss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    oss << '\n' << NNUE::trace(pos, networks, *accCaches) << '\n';

    oss << std::showpoint << std::showpos << std::fixed << std::setprecision(2);

    Value v;

    auto [psqt, positional] = networks.big.evaluate(pos, &accCaches->big);

    v = (psqt + positional) / NNUE::OutputScale;
    v = pos.side_to_move() == WHITE ? +v : -v;
    oss << "NNUE evaluation        " << 0.01 * UCI::to_cp(v, pos) << " (white side)\n";

    v = evaluate(pos, networks, *accCaches);
    v = pos.side_to_move() == WHITE ? +v : -v;
    oss << "Final evaluation       " << 0.01 * UCI::to_cp(v, pos) << " (white side)";
    oss << " [with scaled NNUE, ...]";
    oss << '\n';

    return oss.str();
}

}  // namespace Eval
}  // namespace DON
