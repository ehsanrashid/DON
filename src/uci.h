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

#include <cstdint>
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
    UCI(int argc, const char* argv[]) noexcept;

    auto& options() noexcept { return engine.get_options(); }

    void run() noexcept;

    void execute(std::string_view command) noexcept;

    static void print_info_string(std::string_view infoStr) noexcept;

    [[nodiscard]] static int         to_cp(Value v, const Position& pos) noexcept;
    [[nodiscard]] static std::string to_wdl(Value v, const Position& pos) noexcept;
    [[nodiscard]] static std::string to_score(const Score& score) noexcept;

    [[nodiscard]] static std::string move_to_can(Move m) noexcept;

    [[nodiscard]] static Move can_to_move(std::string can, const MoveList<LEGAL>&) noexcept;
    [[nodiscard]] static Move can_to_move(std::string can, const Position& pos) noexcept;

    [[nodiscard]] static std::string move_to_san(Move m, Position& pos) noexcept;

    [[nodiscard]] static Move
    san_to_move(std::string san, Position& pos, const MoveList<LEGAL>&) noexcept;
    [[nodiscard]] static Move san_to_move(std::string san, Position& pos) noexcept;

    [[nodiscard]] static Move
    mix_to_move(std::string mix, Position& pos, const MoveList<LEGAL>&) noexcept;

    static inline bool InfoStringStop = false;

   private:
    void set_update_listeners() noexcept;

    void position(std::istream& is) noexcept;
    void go(std::istream& is) noexcept;
    void setoption(std::istream& is) noexcept;
    void bench(std::istream& is) noexcept;
    void benchmark(std::istream& is) noexcept;

    std::uint64_t perft(Depth depth, bool detail = false) noexcept;

    Engine      engine;
    CommandLine commandLine;
};

}  // namespace DON

#endif  // #ifndef UCI_H_INCLUDED
