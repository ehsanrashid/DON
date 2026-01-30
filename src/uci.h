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

#ifndef UCI_H_INCLUDED
#define UCI_H_INCLUDED

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

#include "engine.h"
#include "misc.h"
#include "movegen.h"
#include "types.h"

namespace DON {

class Position;
class Options;
class Score;

class UCI final {
   public:
    static void init(int argc, const char* argv[]) noexcept;

    static bool is_initialized() noexcept { return initialized; }

    static StringViews& arguments() noexcept;
    static Options&     options() noexcept;

    static void run() noexcept;

    static void execute(std::string_view command) noexcept;

    static void print_info_string(std::string_view infoStr) noexcept;

    [[nodiscard]] static int         to_cp(Value v, const Position& pos) noexcept;
    [[nodiscard]] static std::string to_wdl(Value v, const Position& pos) noexcept;
    [[nodiscard]] static std::string to_score(const Score& score) noexcept;

    [[nodiscard]] static std::string move_to_can(Move m) noexcept;

    [[nodiscard]] static Move can_to_move(std::string                     can,
                                          const MoveList<GenType::LEGAL>& legalMoves) noexcept;
    [[nodiscard]] static Move can_to_move(std::string can, const Position& pos) noexcept;

    [[nodiscard]] static std::string move_to_san(Move m, Position& pos) noexcept;

    [[nodiscard]] static Move san_to_move(std::string                     san,
                                          Position&                       pos,
                                          const MoveList<GenType::LEGAL>& legalMoves) noexcept;
    [[nodiscard]] static Move san_to_move(std::string san, Position& pos) noexcept;

    [[nodiscard]] static Move mix_to_move(std::string                     mix,
                                          Position&                       pos,
                                          const MoveList<GenType::LEGAL>& legalMoves) noexcept;

    static std::string build_pv_string(const Moves& pvMoves) noexcept;

    static inline bool InfoStringStop = false;

   private:
    static void set_update_callbacks() noexcept;

    static void position(std::istream& is) noexcept;
    static void go(std::istream& is) noexcept;
    static void setoption(std::istream& is) noexcept;
    static void bench(std::istream& is) noexcept;
    static void benchmark(std::istream& is) noexcept;

    static std::uint64_t perft(Depth depth, bool detail = false) noexcept;

    UCI() noexcept                      = delete;
    ~UCI() noexcept                     = delete;
    UCI(const UCI&) noexcept            = delete;
    UCI(UCI&&) noexcept                 = delete;
    UCI& operator=(const UCI&) noexcept = delete;
    UCI& operator=(UCI&&) noexcept      = delete;

    static inline std::unique_ptr<CommandLine> commandLine;
    static inline std::unique_ptr<Engine>      engine;
    static inline bool                         initialized = false;
};

}  // namespace DON

#endif  // #ifndef UCI_H_INCLUDED
