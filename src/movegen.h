/*
  DON, a UCI chess playing engine derived from Glaurung 2.1

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

#ifndef MOVEGEN_H_INCLUDED
#define MOVEGEN_H_INCLUDED

#include <algorithm>  // IWYU pragma: keep
#include <cstddef>
#include <cstdint>

#include "types.h"

namespace DON {

class Position;

enum GenType : std::uint8_t {
    CAPTURES,
    QUIETS,
    QUIET_CHECKS,
    EVASIONS,
    NON_EVASIONS,
    LEGAL
};

struct ExtMove final: public Move {
    int value;

    void operator=(const Move& m) noexcept { data = m.raw(); }

    bool operator<(const ExtMove& em) const noexcept { return value < em.value; }
    bool operator>(const ExtMove& em) const noexcept { return value > em.value; }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const noexcept = delete;
};

//inline bool operator<(const ExtMove& em1, const ExtMove& em2) noexcept { return em1.value < em2.value; }
//inline bool operator>(const ExtMove& em1, const ExtMove& em2) noexcept { return em1.value > em2.value; }

template<GenType GT>
ExtMove* generate(const Position& pos, ExtMove* moves) noexcept;

// The MoveList struct wraps the generate() function and returns a convenient list of moves.
// Using MoveList is sometimes preferable to directly calling the lower level generate() function.
template<GenType GT>
struct MoveList final {

    explicit MoveList(const Position& pos) noexcept :
        last(generate<GT>(pos, moves)) {}

    const ExtMove* begin() const noexcept { return moves; }
    const ExtMove* end() const noexcept { return last; }

    constexpr std::uint8_t size() const noexcept { return last - moves; }

    constexpr bool contains(const Move& m) const noexcept {
        return std::find(begin(), end(), m) != end();
    }

   private:
    ExtMove moves[MAX_MOVES], *last;
};

}  // namespace DON

#endif  // #ifndef MOVEGEN_H_INCLUDED
