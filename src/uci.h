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

#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

#include "engine.h"
#include "misc.h"
#include "types.h"

namespace DON {

class Position;
class Score;
enum GenType;
template<GenType GT>
struct MoveList;

class UCI final {
   public:
    UCI(int argc, const char** argv) noexcept;

    auto& engine_options() noexcept { return engine.get_options(); }

    void handle_commands() noexcept;

    static int         to_cp(Value v, const Position& pos) noexcept;
    static std::string to_wdl(Value v, const Position& pos) noexcept;
    static std::string format_score(const Score& score) noexcept;

    static char  piece(PieceType pt) noexcept;
    static char  piece(Piece pc) noexcept;
    static Piece piece(char pc) noexcept;

    static char file(File f, bool caseLower = true) noexcept;
    static char rank(Rank r) noexcept;

    static std::string square(Square s) noexcept;

    // clang-format off
    static std::string move_to_can(Move m) noexcept;

    static Move can_to_move(const std::string& can, const MoveList<LEGAL>& legalMoves) noexcept;
    static Move can_to_move(const std::string& can, const Position& pos) noexcept;

    static std::string move_to_san(Move m, Position& pos) noexcept;

    static Move san_to_move(const std::string& san, Position& pos, const MoveList<LEGAL>& legalMoves) noexcept;
    static Move san_to_move(const std::string& san, Position& pos) noexcept;

    static Move mix_to_move(const std::string& mix, Position& pos, const MoveList<LEGAL>& legalMoves) noexcept;
    // clang-format on
   private:
    void position(std::istringstream& iss) noexcept;
    void go(std::istringstream& iss) noexcept;
    void setoption(std::istringstream& iss) noexcept;
    void bench(std::istringstream& iss) noexcept;

    Engine      engine;
    CommandLine cmdLine;
};

}  // namespace DON

#endif  // #ifndef UCI_H_INCLUDED
