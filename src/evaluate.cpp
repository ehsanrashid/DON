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

#include "position.h"
#include "uci.h"
#include "nnue/network.h"
#include "nnue/nnue_misc.h"

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
Value evaluate(const Position& pos, const NNUE::Networks& networks, Value optimism) noexcept {
    assert(!pos.checkers());

    Color stm        = pos.side_to_move();
    Value simpleEval = evaluate_simple(pos);
    bool  smallNet   = std::abs(simpleEval) > SmallNetThreshold;
    bool  psqtOnly   = std::abs(simpleEval) > PsqtOnlyThreshold;

    int nnueComplexity;

    Value nnue = smallNet ? networks.small.evaluate(pos, true, &nnueComplexity, psqtOnly)
                          : networks.big.evaluate(pos, true, &nnueComplexity, false);

    Value v;

    auto adjustEval = [&](int optDiv, int nnueDiv, int pawnCountConstant, int pawnCountMul,
                          int npmConstant, int evalDiv, int shufflingConstant,
                          int shufflingDiv) noexcept {
        // Blend optimism and eval with nnue complexity and material imbalance
        optimism += optimism * (nnueComplexity + std::abs(simpleEval - nnue)) / optDiv;
        nnue -= nnue * (nnueComplexity * 5 / 3) / nnueDiv;

        Value npm = pos.non_pawn_material() / 64;

        v = (nnue * (npm + pawnCountConstant + pawnCountMul * pos.count<PAWN>())
             + optimism * (npm + npmConstant))
          / evalDiv;

        v += 10 * (pos.bishop_paired(stm) - pos.bishop_paired(~stm));
        v += 30
           * ((pos.can_castle(stm & ANY_CASTLING) || pos.has_castled(stm))
              - (pos.can_castle(~stm & ANY_CASTLING) || pos.has_castled(~stm)));

        int shuffling = pos.rule50_count();
        // Damp down the evaluation linearly when shuffling
        v = v * (shufflingConstant - shuffling) / shufflingDiv;
    };

    if (!smallNet)
        adjustEval(513, 32395, 919, 11, 145, 1036, 178, 204);
    else if (psqtOnly)
        adjustEval(517, 32857, 908, 7, 155, 1019, 224, 238);
    else
        adjustEval(499, 32793, 903, 9, 147, 1067, 208, 211);

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
    oss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2);
    oss << '\n' << NNUE::trace(pos, networks) << '\n';

    oss << std::showpoint << std::showpos << std::fixed << std::setprecision(2);

    Value v;
    v = networks.big.evaluate(pos);
    v = pos.side_to_move() == WHITE ? +v : -v;
    oss << "NNUE evaluation        " << 0.01 * UCI::to_cp(v, pos) << " (white side)\n";

    v = evaluate(pos, networks);
    v = pos.side_to_move() == WHITE ? +v : -v;
    oss << "Final evaluation       " << 0.01 * UCI::to_cp(v, pos) << " (white side)";
    oss << " [with scaled NNUE, ...]";
    oss << '\n';

    return oss.str();
}

}  // namespace Eval
}  // namespace DON
