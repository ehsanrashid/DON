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

#include <algorithm>  // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <iterator>
#include <vector>

#include "types.h"

namespace DON {

class Position;

enum GenType : std::uint8_t {
    CAPTURES,
    QUIETS,
    ENCOUNTERS,
    EVASIONS,
    LEGAL
};

class ExtMove final: public Move {
   public:
    ExtMove() = default;
    ExtMove(Move m) noexcept :
        Move(m) {}
    //ExtMove(Move m, std::int32_t v) noexcept :
    //    Move(m),
    //    value(v) {}

    void operator=(Move m) noexcept { move = m.raw(); }

    bool operator==(const ExtMove& em) const noexcept { return move == em.move; }
    bool operator!=(const ExtMove& em) const noexcept { return !(*this == em); }

    bool operator<(const ExtMove& em) const noexcept { return value < em.value; }
    bool operator>(const ExtMove& em) const noexcept { return value > em.value; }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const noexcept = delete;

    std::int32_t value = 0;
};

//inline bool operator<(const ExtMove& em1, const ExtMove& em2) noexcept { return em1.value < em2.value; }
//inline bool operator>(const ExtMove& em1, const ExtMove& em2) noexcept { return em1.value > em2.value; }

class ExtMoves final {
   public:
    using ExtMoveDeque = std::deque<ExtMove>;
    using NormalItr    = ExtMoveDeque::iterator;
    using ConstItr     = ExtMoveDeque::const_iterator;

    ExtMoves() noexcept = default;

    template<typename... Args>
    void operator+=(Args&&... args) noexcept {
        extMoves.emplace_back(std::forward<Args>(args)...);
    }

    void clear() noexcept { extMoves.clear(); }

    auto begin() noexcept { return extMoves.begin(); }
    auto end() noexcept { return extMoves.end(); }

    auto begin() const noexcept { return extMoves.begin(); }
    auto end() const noexcept { return extMoves.end(); }

    auto size() const noexcept { return extMoves.size(); }

    ConstItr find(const ExtMove& em) const noexcept { return std::find(begin(), end(), em); }

    bool contains(const ExtMove& em) const noexcept { return find(em) != end(); }

    NormalItr remove(Move m) noexcept { return std::remove(begin(), end(), m); }

    template<typename Predicate>
    NormalItr remove_if(Predicate pred) noexcept {
        return std::remove_if(begin(), end(), pred);
    }

   private:
    ExtMoveDeque extMoves;
};

template<GenType GT>
ExtMoves::NormalItr generate(ExtMoves& extMoves, const Position& pos) noexcept;

ExtMoves::NormalItr filter_illegal(ExtMoves& extMoves, const Position& pos) noexcept;

// The MoveList struct wraps the generate() function and returns a convenient list of moves.
// Using MoveList is sometimes preferable to directly calling the lower level generate() function.
template<GenType GT>
struct MoveList final {

    explicit MoveList(const Position& pos) noexcept { endExtItr = generate<GT>(extMoves, pos); }
    MoveList() noexcept                           = delete;
    MoveList(MoveList const&) noexcept            = delete;
    MoveList(MoveList&&) noexcept                 = delete;
    MoveList& operator=(const MoveList&) noexcept = delete;
    MoveList& operator=(MoveList&&) noexcept      = delete;

    auto begin() noexcept { return extMoves.begin(); }
    auto end() noexcept { return endExtItr; }

    auto begin() const noexcept { return extMoves.begin(); }
    auto end() const noexcept { return (ExtMoves::ConstItr)(endExtItr); }

    std::size_t size() const noexcept { return std::distance(begin(), end()); }

    auto find(Move m) const noexcept { return std::find(begin(), end(), m); }
    bool contains(Move m) const noexcept { return find(m) != end(); }

   private:
    ExtMoves            extMoves;
    ExtMoves::NormalItr endExtItr;
};

}  // namespace DON

#endif  // #ifndef MOVEGEN_H_INCLUDED
