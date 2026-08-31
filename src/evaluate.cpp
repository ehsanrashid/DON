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
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string_view>

#include "notation.h"
#include "position.h"
#include "nnue/accumulator.h"
#include "nnue/network.h"
#include "nnue/ntypes.h"

namespace DON::Evaluate {

// Evaluate is the evaluator for the outer world.
// It returns a static evaluation of the position
// from the point of view of the side to move.
Value evaluate(const Position&         pos,
               const NNUE::Network&    network,
               NNUE::AccumulatorCache& accCache,
               NNUE::AccumulatorStack& accStack,
               i32                     optimism) noexcept {
    assert(pos.checkers_bb() == 0);

    const auto [psqt, positional] = network.evaluate(pos, accCache, accStack);

    i32 nnue = psqt + positional;

    double complexity = constexpr_abs(psqt - positional);
    // Blend eval and optimism with complexity
    nnue     = constexpr_round(nnue * (1.0 - complexity / 18236.0));
    optimism = constexpr_round(optimism * (1.0 + complexity / 476.0));

    i32 v = constexpr_round(
      (nnue * 77871.0 + optimism * 7191.0 + (nnue + optimism) * pos.material()) / 77871.0);

    // Damp evaluation linearly based on the 50-move rule
    v = constexpr_round(v * std::max(1.0 - pos.rule50_count() / 195.0, 0.0));

    // Guarantee evaluation does not hit the table-base range
    return in_range(v);
}

namespace {

// Converts a Value into centi-pawns and writes it in a buffer.
// The buffer must have capacity for at least 5 chars.
void format_cp_compact(char* buffer, const Value v, const Position& pos) noexcept {
    // Set the sign character
    buffer[0] = (v < 0 ? '-' : v > 0 ? '+' : ' ');
    // Convert to centipawns and take absolute value
    if (int cp = constexpr_abs(to_cp(v, pos)); cp >= 10000)
    {
        buffer[1] = digit_to_char(cp / 10000);
        cp %= 10000;
        buffer[2] = digit_to_char(cp / 1000);
        cp %= 1000;
        buffer[3] = digit_to_char(cp / 100);
        buffer[4] = ' ';
    }
    else if (cp >= 1000)
    {
        buffer[1] = digit_to_char(cp / 1000);
        cp %= 1000;
        buffer[2] = digit_to_char(cp / 100);
        cp %= 100;
        buffer[3] = '.';
        buffer[4] = digit_to_char(cp / 10);
    }
    else
    {
        buffer[1] = digit_to_char(cp / 100);
        cp %= 100;
        buffer[2] = '.';
        buffer[3] = digit_to_char(cp / 10);
        cp %= 10;
        buffer[4] = digit_to_char(cp / 1);
    }
}

// Converts a value into pawns, always keeping two decimals
void format_cp_aligned_dot(std::ostringstream& oss, const i32 val, const Position& pos) noexcept {

    const auto v    = in_range(val);
    const char sign = (v < 0 ? '-' : v > 0 ? '+' : ' ');
    const auto cp   = 0.01 * constexpr_abs(to_cp(v, pos));
    oss << sign << std::setw(6) << std::fixed << std::setprecision(2) << cp;
}

// Returns a string with the value of each piece on a board,
// and a table for (PSQT, Layers) values bucket by bucket.
std::string
nnue_trace(Position& pos, const NNUE::Network& network, NNUE::AccumulatorCache& accCache) noexcept {
    constexpr std::string_view Sep{"+------------+------------+------------+------------+\n"};

    char board[3 * 8 + 1][8 * 8 + 2];
    std::memset(board, ' ', sizeof(board));
    for (auto& row : board)
        row[8 * 8 + 1] = '\0';

    // A lambda to output one box of the board
    const auto write_square = [&board, &pos](const File file, const Rank rank,  //
                                             const Piece pc, const Value value) noexcept {
        const usize x = 8 * file;
        const usize y = 3 * (7 - rank);
        for (usize i = 1; i < 8; ++i)
            board[y][x + i] = board[y + 3][x + i] = '-';
        for (usize j = 1; j < 3; ++j)
            board[y + j][x] = board[y + j][x + 8] = '|';
        board[y][x] = board[y][x + 8] = board[y + 3][x + 8] = board[y + 3][x] = '+';
        if (is_ok(pc))
            board[y + 1][x + 4] = to_char(pc);
        if (is_valid(value))
            format_cp_compact(&board[y + 2][x + 2], value, pos);
    };

    std::ostringstream oss{};

    auto accStack = std::make_unique<NNUE::AccumulatorStack>();

    accStack->reset();

    // Estimate the value of each piece by doing a differential evaluation from
    // the current base eval, simulating the removal of the piece from its square.
    const auto  baseNetOut = network.evaluate(pos, accCache, *accStack);
    const Value baseValue  = pos.active_color() == WHITE
                             ? +in_range(baseNetOut.psqt + baseNetOut.positional)
                             : -in_range(baseNetOut.psqt + baseNetOut.positional);

    for (File f = FILE_A; f <= FILE_H; ++f)
        for (Rank r = RANK_1; r <= RANK_8; ++r)
        {
            const Square sq = make_square(f, r);
            const Piece  pc = pos[sq];

            Value v = VALUE_NONE;

            if (is_ok(pc) && type_of(pc) != KING)
            {
                pos.remove(sq);

                accStack->reset();

                const auto  newNetOut = network.evaluate(pos, accCache, *accStack);
                const Value newValue  = pos.active_color() == WHITE
                                        ? +in_range(newNetOut.psqt + newNetOut.positional)
                                        : -in_range(newNetOut.psqt + newNetOut.positional);

                v = baseValue - newValue;

                pos.put(sq, pc);
            }

            write_square(f, r, pc, v);
        }


    oss << " NNUE derived piece values:\n";

    for (const auto& row : board)
        oss << row << '\n';
    oss << '\n';

    accStack->reset();

    auto netTrace = network.trace(pos, accCache, *accStack);

    oss << " NNUE network contributions (Normalized, ";
    oss << (pos.active_color() == WHITE ? "White" : "Black") << " to move):\n";
    oss << Sep;
    oss << "|   Bucket   |  Material  | Positional |   Total    |\n";
    oss << "|            |   (PSQT)   |  (Layers)  |            |\n";
    oss << Sep;

    for (usize bucket = 0; bucket < NNUE::LayerStacks; ++bucket)
    {
        oss << "|  " << bucket << "         |  ";
        format_cp_aligned_dot(oss, netTrace.netOut[bucket].psqt, pos);
        oss << "   |  ";
        format_cp_aligned_dot(oss, netTrace.netOut[bucket].positional, pos);
        oss << "   |  ";
        format_cp_aligned_dot(
          oss, netTrace.netOut[bucket].psqt + netTrace.netOut[bucket].positional, pos);
        oss << "   |";
        if (bucket == netTrace.correctBucket)
            oss << " <-- this bucket is used";
        oss << '\n';
    }

    oss << Sep;

    return oss.str();
}

}  // namespace

// Like evaluate(), but instead of returning a value,
// it returns a string (suitable for outputting to stdout)
// that contains the detailed descriptions and values of each evaluation term.
// Trace scores are from white's point of view.
std::string trace(Position& pos, const NNUE::Network& network) noexcept {
    if (pos.checkers_bb() != 0)
        return "Final evaluation     : none (in check)";

    auto accCache = std::make_unique<NNUE::AccumulatorCache>(network);
    auto accStack = std::make_unique<NNUE::AccumulatorStack>();

    auto fmt = [](const double d) noexcept -> std::string {
        Array<char, 8> buffer{};

        int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "%+01.2f", d);
        usize copiedSize  = writtenSize > 0  //
                            ? std::min<usize>(writtenSize, buffer.size() - 1)
                            : 0;

        return std::string{buffer.data(), copiedSize};
    };

    std::string output;
    output.reserve(3 * KB);

    output  //
      .assign(nnue_trace(pos, network, *accCache))
      .append("\n");

    auto [psqt, positional] = network.evaluate(pos, *accCache, *accStack);

    Value v;

    v = psqt + positional;
    v = pos.active_color() == WHITE ? +v : -v;

    output  //
      .append("NNUE evaluation      : ")
      .append(fmt(0.01 * to_cp(v, pos)))
      .append(" (white side)\n");

    v = evaluate(pos, network, *accCache, *accStack);
    v = pos.active_color() == WHITE ? +v : -v;

    output  //
      .append("Final evaluation     : ")
      .append(fmt(0.01 * to_cp(v, pos)))
      .append(" (white side) [with scaled NNUE, ...]\n");

    return output;
}

}  // namespace DON::Evaluate
