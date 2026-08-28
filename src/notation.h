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

#ifndef NOTATION_H_INCLUDED
#define NOTATION_H_INCLUDED

#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "misc.h"
#include "movegen.h"
#include "types.h"

namespace DON {

class Position;

// Score represents the evaluation score of a position
class Score final {
   public:
    struct Unit final {
       public:
        int value;
    };

    struct Tablebase final {
       public:
        int value;
    };

    struct Mate final {
       public:
        int value;
    };

    Score() noexcept = delete;
    Score(Value v, const Position& pos) noexcept;

    template<typename T>
    bool is() const noexcept {
        return std::holds_alternative<T>(score);
    }

    template<typename T>
    T get() const noexcept {
        return std::get<T>(score);
    }

    template<typename F>
    decltype(auto) visit(F&& f) const noexcept {
        return std::visit(std::forward<F>(f), score);
    }

   private:
    std::variant<Unit, Tablebase, Mate> score;
};

[[nodiscard]] int       to_cp(Value v, const Position& pos) noexcept;
[[nodiscard]] FixedText to_wdl(Value v, const Position& pos) noexcept;
[[nodiscard]] FixedText to_score(const Score& score) noexcept;

[[nodiscard]] std::string move_to_can(Move m) noexcept;

[[nodiscard]] Move can_to_move(std::string                     can,
                               const MoveList<GenType::LEGAL>& legalMoves) noexcept;
[[nodiscard]] Move can_to_move(std::string_view can, const Position& pos) noexcept;

[[nodiscard]] std::string move_to_san(Move m, Position& pos) noexcept;

[[nodiscard]] Move
san_to_move(std::string san, Position& pos, const MoveList<GenType::LEGAL>& legalMoves) noexcept;
[[nodiscard]] Move san_to_move(std::string_view san, Position& pos) noexcept;

[[nodiscard]] Move
mix_to_move(std::string mix, Position& pos, const MoveList<GenType::LEGAL>& legalMoves) noexcept;

}  // namespace DON

#endif  // NOTATION_H_INCLUDED
