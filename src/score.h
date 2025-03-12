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

#ifndef SCORE_H_INCLUDED
#define SCORE_H_INCLUDED

#include <utility>
#include <variant>

#include "types.h"

namespace DON {

class Position;

class Score final {
   public:
    struct Unit final {
        int value;
    };

    struct Tablebase final {
        int  ply;
        bool win;
    };

    struct Mate final {
        int ply;
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

}  // namespace DON

#endif  // #ifndef SCORE_H_INCLUDED
