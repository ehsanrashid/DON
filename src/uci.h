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
    UCI(int argc, const char* argv[]) noexcept;

    auto& options() noexcept { return engine.get_options(); }

    void run() noexcept;

    void execute(std::string_view command) noexcept;

    static void print_info_string(std::string_view infoStr) noexcept;

    template<bool Upper = false>
    [[nodiscard]] static constexpr char to_char(File f) noexcept;
    [[nodiscard]] static constexpr char to_char(Rank r) noexcept;

    [[nodiscard]] static constexpr File to_file(char f) noexcept;
    [[nodiscard]] static constexpr Rank to_rank(char r) noexcept;

    [[nodiscard]] static constexpr char flip_file(char f) noexcept;
    [[nodiscard]] static constexpr char flip_rank(char r) noexcept;

    [[nodiscard]] static std::string to_string(Square s) noexcept;

    [[nodiscard]] static char  to_char(PieceType pt) noexcept;
    [[nodiscard]] static char  to_char(Piece pc) noexcept;
    [[nodiscard]] static Piece to_piece(char pc) noexcept;

    [[nodiscard]] static int         to_cp(Value v, const Position& pos) noexcept;
    [[nodiscard]] static std::string to_string(Value v, const Position& pos) noexcept;
    [[nodiscard]] static std::string to_string(const Score& score) noexcept;

    [[nodiscard]] static std::string move_to_can(Move m) noexcept;

    [[nodiscard]] static Move can_to_move(std::string can, const MoveList<LEGAL>&) noexcept;
    [[nodiscard]] static Move can_to_move(std::string_view can, const Position& pos) noexcept;

    [[nodiscard]] static std::string move_to_san(Move m, Position& pos) noexcept;

    [[nodiscard]] static Move
    san_to_move(std::string san, Position& pos, const MoveList<LEGAL>&) noexcept;
    [[nodiscard]] static Move san_to_move(std::string_view san, Position& pos) noexcept;

    [[nodiscard]] static Move
    mix_to_move(std::string_view mix, Position& pos, const MoveList<LEGAL>&) noexcept;

    static inline bool InfoStringEnabled = true;

   private:
    void set_update_listeners() noexcept;

    void position(std::istringstream& iss) noexcept;
    void go(std::istringstream& iss) noexcept;
    void setoption(std::istringstream& iss) noexcept;
    void bench(std::istringstream& iss) noexcept;
    void benchmark(std::istringstream& iss) noexcept;

    Engine      engine;
    CommandLine commandLine;
};

template<bool Upper>
inline constexpr char UCI::to_char(File f) noexcept {
    return int(f) + (Upper ? 'A' : 'a');
}

inline constexpr char UCI::to_char(Rank r) noexcept { return int(r) + '1'; }

inline constexpr File UCI::to_file(char f) noexcept { return File(f - 'a'); }

inline constexpr Rank UCI::to_rank(char r) noexcept { return Rank(r - '1'); }

inline constexpr char UCI::flip_file(char f) noexcept {
    // Flip file 'A'-'H' or 'a'-'h'; otherwise unchanged
    return ('A' <= f && f <= 'H') ? 'A' + ('H' - f) : ('a' <= f && f <= 'h') ? 'a' + ('h' - f) : f;
}

inline constexpr char UCI::flip_rank(char r) noexcept {
    // Flip rank '1'-'8'; otherwise unchanged
    return ('1' <= r && r <= '8') ? '1' + ('8' - r) : r;
}

}  // namespace DON

#endif  // #ifndef UCI_H_INCLUDED
