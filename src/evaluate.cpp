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
#include <sstream>
#include <memory>

#include "uci.h"
#include "nnue/network.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/nnue_misc.h"

namespace DON {
namespace Eval {

// Evaluate is the evaluator for the outer world. It returns a static evaluation
// of the position from the point of view of the side to move.
Value evaluate(const Position&          pos,
               const NNUE::Networks&    networks,
               NNUE::AccumulatorCaches& accCaches,
               Value                    optimism) noexcept {
    assert(!pos.checkers());

    const Value eval    = pos.evaluate();
    const Value absEval = std::abs(eval);
    const Value bonus   = pos.bonus();

    int   complexity = 0;
    Value nnue       = VALUE_ZERO;

    Value v;

    // Blend optimism and eval with nnue complexity and material imbalance
    // clang-format off
    auto blend = [=, &nnue, &pos = std::as_const(pos)]() mutable {
        optimism += optimism * (complexity + std::abs(nnue - eval)) / 584;
        nnue -= nnue * (5 * complexity / 3) / 32395;
        v = (nnue
               * (32961 + 381 * pos.count<PAWN>() + 349 * pos.count<KNIGHT>()
                  + 392 * pos.count<BISHOP>() + 649 * pos.count<ROOK>() + 1211 * pos.count<QUEEN>())
           + optimism
               * (4835 + 136 * pos.count<PAWN>() + 375 * pos.count<KNIGHT>()
                  + 403 * pos.count<BISHOP>() + 628 * pos.count<ROOK>() + 1124 * pos.count<QUEEN>()))
          / 32768;
        v += bonus;
        return v;
    };
    // clang-format on

    bool useBigNet = true;
    if (use_small_net(absEval, pos))
    {
        nnue = networks.small.evaluate(pos, &accCaches.small, true, &complexity);

        v = blend();

        useBigNet = std::signbit(v) != std::signbit(eval) || std::abs(v - eval) >= 760;
    }
    if (useBigNet)
    {
        nnue = networks.big.evaluate(pos, &accCaches.big, true, &complexity);

        v = blend();
    }

    if (std::signbit(v) != std::signbit(nnue) && std::abs(v) < 100)
        v = 7 * v / 8;

    const int shuffling = pos.rule50_count();
    // Damp down the evaluation linearly when shuffling
    v = v * (204 - shuffling) / 208;

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
    v = networks.big.evaluate(pos, &accCaches->big);
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
