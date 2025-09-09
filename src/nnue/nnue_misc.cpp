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

// Code for calculating NNUE evaluation function

#include "nnue_misc.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "../uci.h"
#include "network.h"
#include "nnue_accumulator.h"
#include "nnue_common.h"

namespace DON::NNUE {

namespace {

// Converts a Value into centi-pawns and writes it in a buffer.
// The buffer must have capacity for at least 5 chars.
void format_cp_compact(char*           buffer,  //
                       Value           v,
                       const Position& pos) noexcept {
    // Set the sign character
    buffer[0] = (v < 0 ? '-' : v > 0 ? '+' : ' ');
    // Convert to centipawns and take absolute value
    int cp = std::abs(UCI::to_cp(v, pos));
    if (cp >= 10000)
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

// Converts a Value into pawns, always keeping two decimals
void format_cp_aligned_dot(std::ostringstream& oss,
                           std::int32_t        val,
                           const Position&     pos) noexcept {

    Value v    = in_range(val);
    char  sign = (v < 0 ? '-' : v > 0 ? '+' : ' ');
    float cp   = 0.01 * std::abs(UCI::to_cp(v, pos));
    oss << sign << std::setw(6) << std::fixed << std::setprecision(2) << cp;
}

}  // namespace

// Returns a string with the value of each piece on a board,
// and a table for (PSQT, Layers) values bucket by bucket.
std::string trace(Position& pos, const Networks& networks, AccumulatorCaches& caches) noexcept {
    std::ostringstream oss;

    char board[3 * 8 + 1][8 * 8 + 2];
    std::memset(board, ' ', sizeof(board));
    for (auto& row : board)
        row[8 * 8 + 1] = '\0';

    // A lambda to output one box of the board
    auto write_square = [&board, &pos](File file, Rank rank, Piece pc, Value value) noexcept {
        std::size_t x = 8 * int(file);
        std::size_t y = 3 * (7 - int(rank));
        for (std::size_t i = 1; i < 8; ++i)
            board[y][x + i] = board[y + 3][x + i] = '-';
        for (std::size_t j = 1; j < 3; ++j)
            board[y + j][x] = board[y + j][x + 8] = '|';
        board[y][x] = board[y][x + 8] = board[y + 3][x + 8] = board[y + 3][x] = '+';
        if (is_ok(pc))
            board[y + 1][x + 4] = UCI::piece(pc);
        if (is_valid(value))
            format_cp_compact(&board[y + 2][x + 2], value, pos);
    };

    AccumulatorStack accStack;

    // Estimate the value of each piece by doing a differential evaluation from
    // the current base eval, simulating the removal of the piece from its square.
    auto         baseNetOut = networks.big.evaluate(pos, accStack, &caches.big);
    std::int32_t baseEval   = (baseNetOut.psqt + baseNetOut.positional) / OUTPUT_SCALE;

    baseEval = pos.active_color() == WHITE ? +baseEval : -baseEval;

    for (Rank r = RANK_8; r >= RANK_1; --r)
        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            Square sq = make_square(f, r);
            Piece  pc = pos.piece_on(sq);
            Value  v  = VALUE_NONE;

            if (is_ok(pc) && type_of(pc) != KING)
            {
                pos.remove_piece(sq);

                accStack.reset();
                auto netOut = networks.big.evaluate(pos, accStack, &caches.big);

                std::int32_t eval = (netOut.psqt + netOut.positional) / OUTPUT_SCALE;

                eval = pos.active_color() == WHITE ? +eval : -eval;

                v = baseEval - eval;

                pos.put_piece(sq, pc);
            }

            write_square(f, r, pc, v);
        }

    oss << " NNUE derived piece values:\n";
    for (const auto& row : board)
        oss << row << '\n';
    oss << '\n';

    accStack.reset();
    auto trace = networks.big.trace(pos, accStack, &caches.big);

    oss << " NNUE network contributions ("  //
        << (pos.active_color() == WHITE ? "White" : "Black") << " to move):\n"
        << "+------------+------------+------------+------------+\n"
        << "|   Bucket   |  Material  | Positional |   Total    |\n"
        << "|            |   (PSQT)   |  (Layers)  |            |\n"
        << "+------------+------------+------------+------------+\n";

    for (std::size_t bucket = 0; bucket < LayerStacks; ++bucket)
    {
        std::int32_t val;

        oss << "|  " << bucket << "         |  ";
        val = trace.netOut[bucket].psqt / OUTPUT_SCALE;
        format_cp_aligned_dot(oss, val, pos);
        oss << "   |  ";
        val = trace.netOut[bucket].positional / OUTPUT_SCALE;
        format_cp_aligned_dot(oss, val, pos);
        oss << "   |  ";
        val = (trace.netOut[bucket].psqt + trace.netOut[bucket].positional) / OUTPUT_SCALE;
        format_cp_aligned_dot(oss, val, pos);
        oss << "   |";
        if (bucket == trace.correctBucket)
            oss << " <-- this bucket is used";
        oss << '\n';
    }

    oss << "+------------+------------+------------+------------+\n";

    return oss.str();
}

}  // namespace DON::NNUE
