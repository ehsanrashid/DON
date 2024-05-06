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
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <memory>

#include "position.h"
#include "uci.h"
#include "nnue/network.h"
#include "nnue/nnue_misc.h"
#include "nnue/nnue_accumulator.h"

namespace DON {

namespace Eval {

// Returns a static, purely materialistic evaluation of the position from
// the point of view of the side to move. It can be divided by VALUE_PAWN to get
// an approximation of the material advantage on the board in terms of pawns.
Value evaluate_simple(const Position& pos) noexcept {
    Color stm = pos.side_to_move();
    return VALUE_PAWN * (pos.count<PAWN>(stm) - pos.count<PAWN>(~stm))
         + (pos.non_pawn_material(stm) - pos.non_pawn_material(~stm));
}

// Evaluate is the evaluator for the outer world. It returns a static evaluation
// of the position from the point of view of the side to move.
Value evaluate(const Position&          pos,
               const NNUE::Networks&    networks,
               NNUE::AccumulatorCaches& accCaches,
               Value                    optimism) noexcept {
    assert(!pos.checkers());

    Color stm        = pos.side_to_move();
    Value simpleEval = evaluate_simple(pos);
    bool  smallNet   = std::abs(simpleEval) > SmallNetThreshold;

    int nnueComplexity;

    Value nnue = smallNet ? networks.small.evaluate(pos, &accCaches.small, true, &nnueComplexity)
                          : networks.big.evaluate(pos, &accCaches.big, true, &nnueComplexity);

    Value v;

    auto adjustEval = [&](int nnueDiv, int nnueConstant, int pawnCountMul, int optConstant,
                          int evalDiv, int shufflingConstant) noexcept {
        // Blend optimism and eval with nnue complexity and material imbalance
        optimism += optimism * (nnueComplexity + std::abs(simpleEval - nnue)) / 584;
        nnue -= nnue * (5 * nnueComplexity / 3) / nnueDiv;

        Value npm = pos.non_pawn_material() / 64;

        v = (nnue * (npm + nnueConstant + pawnCountMul * pos.count<PAWN>())
             + optimism * (npm + optConstant))
          / evalDiv;

        v += 4 * (pos.mobility(stm) - pos.mobility(~stm));
        v += 10 * (pos.bishop_paired(stm) - pos.bishop_paired(~stm));
        v += 30
           * ((pos.can_castle(stm & ANY_CASTLING) || pos.has_castled(stm))
              - (pos.can_castle(~stm & ANY_CASTLING) || pos.has_castled(~stm)));

        int shuffling = pos.rule50_count();
        // Damp down the evaluation linearly when shuffling
        v = v * (shufflingConstant - shuffling) / 207;
    };

    smallNet ? adjustEval(32793, 944, 9, 140, 1067, 206)
             : adjustEval(32395, 942, 11, 139, 1058, 178);

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
    v = networks.big.evaluate(pos, &accCaches->big, false);
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
