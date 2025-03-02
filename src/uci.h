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

#include <iosfwd>
#include <string>
#include <string_view>

#include "engine.h"
#include "misc.h"
#include "movegen.h"
#include "types.h"

namespace DON {

class Position;
class Score;

class UCI final {
   public:
    UCI(int argc, const char** argv) noexcept;

    auto& engine_options() noexcept { return engine.get_options(); }

    void handle_commands() noexcept;

    static void print_info_string(std::string_view infoStr) noexcept;

    static int         to_cp(Value v, const Position& pos) noexcept;
    static std::string to_wdl(Value v, const Position& pos) noexcept;
    static std::string format_score(const Score& score) noexcept;

    static char  piece(PieceType pt) noexcept;
    static char  piece(Piece pc) noexcept;
    static Piece piece(char pc) noexcept;

    static std::string piece_figure(Piece pc) noexcept;

    static char file(File f, bool upper = false) noexcept;
    static char rank(Rank r) noexcept;

    static std::string square(Square s) noexcept;

    static std::string move_to_can(const Move& m) noexcept;

    static Move can_to_move(std::string can, const MoveList<LEGAL>&) noexcept;
    static Move can_to_move(const std::string& can, const Position& pos) noexcept;

    static std::string move_to_san(const Move& m, Position& pos) noexcept;

    static Move san_to_move(std::string san, Position& pos, const MoveList<LEGAL>&) noexcept;
    static Move san_to_move(const std::string&, Position& pos) noexcept;

    static Move mix_to_move(const std::string& mix, Position& pos, const MoveList<LEGAL>&) noexcept;

    static bool InfoStringStop;

   private:
    void init_update_listeners() noexcept;

    void position(std::istringstream& iss) noexcept;
    void go(std::istringstream& iss) noexcept;
    void set_option(std::istringstream& iss) noexcept;
    void bench(std::istringstream& iss) noexcept;
    void benchmark(std::istringstream& iss) noexcept;

    Engine      engine;
    CommandLine commandLine;
};

}  // namespace DON

#endif  // #ifndef UCI_H_INCLUDED
