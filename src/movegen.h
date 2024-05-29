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
#include <initializer_list>
#include <iterator>
#include <vector>

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

class ExtMove final: public Move {
   public:
    ExtMove() = default;
    ExtMove(Move m) noexcept :
        Move(m) {}

    void operator=(Move m) noexcept { data = m.raw(); }

    bool operator<(const ExtMove& em) const noexcept { return value < em.value; }
    bool operator>(const ExtMove& em) const noexcept { return value > em.value; }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const noexcept = delete;

    int value = 0;
};

//inline bool operator<(const ExtMove& em1, const ExtMove& em2) noexcept { return em1.value < em2.value; }
//inline bool operator>(const ExtMove& em1, const ExtMove& em2) noexcept { return em1.value > em2.value; }

class ExtMoves final {
   public:
    using ExtMoveVector = std::vector<ExtMove>;
    using NormalItr     = ExtMoveVector::iterator;
    using ConstItr      = ExtMoveVector::const_iterator;

    ExtMoves() = default;
    explicit ExtMoves(std::size_t count, const ExtMove& em) noexcept :
        extMoves(count, em) {}
    explicit ExtMoves(std::size_t count) noexcept :
        extMoves(count) {}
    ExtMoves(const std::initializer_list<ExtMove>& initList) noexcept :
        extMoves(initList) {}

    template<typename... Args>
    void emplace(Args&&... args) noexcept {
        extMoves.emplace_back(std::forward<Args>(args)...);
    }

    void push(const ExtMove& em) noexcept { extMoves.push_back(em); }
    void push(ExtMove&& em) noexcept { extMoves.push_back(std::move(em)); }
    void pop() noexcept { extMoves.pop_back(); }

    void reserve(std::size_t newSize) noexcept { extMoves.reserve(newSize); }
    void resize(std::size_t newSize) noexcept { extMoves.resize(newSize); }
    void clear() noexcept { extMoves.clear(); }

    auto begin() noexcept { return extMoves.begin(); }
    auto end() noexcept { return extMoves.end(); }

    auto begin() const noexcept { return extMoves.begin(); }
    auto end() const noexcept { return extMoves.end(); }

    auto& front() noexcept { return extMoves.front(); }
    auto& back() noexcept { return extMoves.back(); }

    auto size() const noexcept { return extMoves.size(); }
    auto max_size() const noexcept { return extMoves.max_size(); }
    bool empty() const noexcept { return extMoves.empty(); }

    auto erase(ConstItr itr) noexcept { return extMoves.erase(itr); }
    auto erase(ConstItr begItr, ConstItr endItr) noexcept {  //
        assert(begItr <= endItr);
        return extMoves.erase(begItr, endItr);
    }
    bool erase(const ExtMove& em) noexcept {
        auto itr = find(em);
        if (itr != end())
            return erase(itr), true;
        return false;
    }

    ConstItr find(const ExtMove& em) const noexcept { return std::find(begin(), end(), em); }

    bool contains(const ExtMove& em) const noexcept { return find(em) != end(); }

    NormalItr remove(Move m) noexcept { return std::remove(begin(), end(), m); }

    template<typename Predicate>
    NormalItr remove_if(Predicate pred) noexcept {
        return std::remove_if(begin(), end(), pred);
    }

    auto& operator[](std::size_t idx) const noexcept { return extMoves[idx]; }
    auto& operator[](std::size_t idx) noexcept { return extMoves[idx]; }

    void operator+=(Move m) noexcept { push(m); }
    void operator-=(Move m) noexcept { erase(m); }

    void operator+=(const ExtMove& em) noexcept { push(em); }
    void operator-=(const ExtMove& em) noexcept { erase(em); }

   private:
    ExtMoveVector extMoves;
};

template<GenType GT>
ExtMoves::NormalItr generate(ExtMoves& extMoves, const Position& pos) noexcept;

ExtMoves::NormalItr filter_illegal(ExtMoves& extMoves, const Position& pos) noexcept;

// The MoveList struct wraps the generate() function and returns a convenient list of moves.
// Using MoveList is sometimes preferable to directly calling the lower level generate() function.
template<GenType GT>
struct MoveList final {

    MoveList()                           = delete;
    MoveList(MoveList const&)            = delete;
    MoveList& operator=(MoveList const&) = delete;
    explicit MoveList(const Position& pos) noexcept { endExtItr = generate<GT>(extMoves, pos); }

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
