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

#ifndef MOVEGEN_H_INCLUDED
#define MOVEGEN_H_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "types.h"

namespace DON {

class Position;

struct ExtMove final: public Move {
   public:
    using Move::Move;

    void operator=(Move m) { move = m.raw(); }

    friend bool operator<(const ExtMove& em1, const ExtMove& em2) noexcept {
        return em1.value < em2.value;
    }
    friend bool operator>(const ExtMove& em1, const ExtMove& em2) noexcept { return (em2 < em1); }
    friend bool operator<=(const ExtMove& em1, const ExtMove& em2) noexcept { return !(em1 > em2); }
    friend bool operator>=(const ExtMove& em1, const ExtMove& em2) noexcept { return !(em1 < em2); }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const = delete;

    int value;
};

enum GenType : std::uint8_t {
    ENCOUNTER,
    ENC_CAPTURE,
    ENC_QUIET,
    EVASION,
    EVA_CAPTURE,
    EVA_QUIET,
    LEGAL
};

template<GenType GT, bool Any = false>
Move* generate(const Position& pos, Move* moves) noexcept;

// MoveList struct wraps the generate() function and returns a convenient list of moves.
// Using MoveList is sometimes preferable to directly calling the lower level generate() function.
template<GenType GT, bool Any = false>
struct MoveList final {
   public:
    explicit MoveList(const Position& pos) noexcept :
        endCur(generate<GT>(pos, moves)) {}
    MoveList() noexcept                           = delete;
    MoveList(MoveList const&) noexcept            = delete;
    MoveList(MoveList&&) noexcept                 = delete;
    MoveList& operator=(const MoveList&) noexcept = delete;
    MoveList& operator=(MoveList&&) noexcept      = delete;

    const Move* begin() const noexcept { return moves; }
    const Move* end() const noexcept { return endCur; }
    Move*       begin() noexcept { return moves; }
    Move*       end() noexcept { return endCur; }

    std::size_t size() const noexcept { return end() - begin(); }
    bool        empty() const noexcept { return end() == begin(); }

    auto find(const Move& m) const noexcept { return std::find(begin(), end(), m); }
    bool contains(const Move& m) const noexcept { return find(m) != end(); }

   private:
    Move  moves[MAX_MOVES];
    Move* endCur;
};

}  // namespace DON

#endif  // #ifndef MOVEGEN_H_INCLUDED
