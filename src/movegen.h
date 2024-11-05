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

class Moves final {
   public:
    using MoveDeque  = std::deque<Move>;
    using NormalItr  = MoveDeque::iterator;
    using ReverseItr = MoveDeque::reverse_iterator;
    using ConstItr   = MoveDeque::const_iterator;

    Moves() = default;
    explicit Moves(std::size_t count, Move m) noexcept :
        moves(count, m) {}
    //explicit Moves(std::size_t count) noexcept :
    //    moves(count) {}
    Moves(const std::initializer_list<Move>& initList) noexcept :
        moves(initList) {}

    template<typename... Args>
    auto& operator+=(Args&&... args) noexcept {
        return moves.emplace_back(std::forward<Args>(args)...);
    }

    void push_back(Move m) noexcept { moves.push_back(m); }
    void push_back(Move&& m) noexcept { moves.push_back(std::move(m)); }
    void push_front(Move m) noexcept { moves.push_front(m); }
    void push_front(Move&& m) noexcept { moves.push_front(std::move(m)); }
    void append(Move m) noexcept { moves.insert(end(), m); }
    void append(ConstItr begItr, ConstItr endItr) noexcept {  //
        moves.insert(end(), begItr, endItr);
    }
    void append(const std::initializer_list<Move>& initList) noexcept {  //
        moves.insert(end(), initList);
    }
    void append(const Moves& ms) noexcept { append(ms.begin(), ms.end()); }
    void pop() noexcept { moves.pop_back(); }

    void resize(std::size_t newSize) noexcept { moves.resize(newSize); }
    void clear() noexcept { moves.clear(); }

    NormalItr begin() noexcept { return moves.begin(); }
    NormalItr end() noexcept { return moves.end(); }

    ReverseItr rbegin() noexcept { return moves.rbegin(); }
    ReverseItr rend() noexcept { return moves.rend(); }

    ConstItr begin() const noexcept { return moves.begin(); }
    ConstItr end() const noexcept { return moves.end(); }

    auto& front() noexcept { return moves.front(); }
    auto& back() noexcept { return moves.back(); }

    auto size() const noexcept { return moves.size(); }
    bool empty() const noexcept { return moves.empty(); }

    ConstItr find(Move m) const noexcept { return std::find(begin(), end(), m); }

    bool contains(Move m) const noexcept { return find(m) != end(); }

    NormalItr remove(Move m) noexcept { return std::remove(begin(), end(), m); }
    template<typename Predicate>
    NormalItr remove_if(Predicate pred) noexcept {
        return std::remove_if(begin(), end(), pred);
    }

    auto& operator[](std::size_t idx) const noexcept { return moves[idx]; }
    auto& operator[](std::size_t idx) noexcept { return moves[idx]; }

   private:
    MoveDeque moves;
};

struct ExtMove final: public Move {
   public:
    using Move::Move;

    explicit ExtMove(Move&& m) noexcept :
        Move(std::move(m)) {}

    void operator=(Move m) noexcept { move = m.raw(); }

    friend bool operator<(const ExtMove& em1, const ExtMove& em2) noexcept {
        return em1.value < em2.value;
    }
    friend bool operator>(const ExtMove& em1, const ExtMove& em2) noexcept { return (em2 < em1); }
    friend bool operator<=(const ExtMove& em1, const ExtMove& em2) noexcept { return !(em1 > em2); }
    friend bool operator>=(const ExtMove& em1, const ExtMove& em2) noexcept { return !(em1 < em2); }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const noexcept = delete;

    int value = 0;
};

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

enum GenType : std::uint8_t {
    CAPTURES,
    QUIETS,
    ENCOUNTERS,
    EVASIONS,
    LEGAL
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
    auto end() const noexcept { return ExtMoves::ConstItr(endExtItr); }

    auto size() const noexcept { return std::size_t(std::distance(begin(), end())); }
    bool empty() const noexcept { return size() == 0; }

    auto find(Move m) const noexcept { return std::find(begin(), end(), m); }
    bool contains(Move m) const noexcept { return find(m) != end(); }

   private:
    ExtMoves            extMoves;
    ExtMoves::NormalItr endExtItr;
};

using LegalMoveList = MoveList<LEGAL>;

}  // namespace DON

#endif  // #ifndef MOVEGEN_H_INCLUDED
