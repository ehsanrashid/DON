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
#include <vector>

#include "types.h"

namespace DON {

class Position;

class Moves final {
   public:
    using Vector     = std::vector<Move>;
    using Itr        = Vector::iterator;
    using ReverseItr = Vector::reverse_iterator;
    using ConstItr   = Vector::const_iterator;

    Moves() noexcept = default;
    explicit Moves(const std::size_t count, const Move& m) noexcept :
        moves(count, m) {}
    explicit Moves(const std::size_t count) noexcept :
        moves(count) {}
    Moves(const std::initializer_list<Move>& initList) noexcept :
        moves(initList) {}

    auto begin() const noexcept { return moves.begin(); }
    auto end() const noexcept { return moves.end(); }
    auto begin() noexcept { return moves.begin(); }
    auto end() noexcept { return moves.end(); }

    auto rbegin() noexcept { return moves.rbegin(); }
    auto rend() noexcept { return moves.rend(); }

    auto& front() noexcept { return moves.front(); }
    auto& back() noexcept { return moves.back(); }

    auto size() const noexcept { return moves.size(); }
    auto empty() const noexcept { return moves.empty(); }

    template<typename... Args>
    auto& emplace_back(Args&&... args) noexcept {
        return moves.emplace_back(std::forward<Args>(args)...);
    }
    template<typename... Args>
    auto& emplace(ConstItr where, Args&&... args) noexcept {
        return moves.emplace(where, std::forward<Args>(args)...);
    }

    void push_back(const Move& m) noexcept { moves.push_back(m); }
    void push_back(Move&& m) noexcept { moves.push_back(std::move(m)); }

    void pop_back() noexcept { moves.pop_back(); }

    void clear() noexcept { moves.clear(); }

    void resize(std::size_t newSize) noexcept { moves.resize(newSize); }
    void reserve(std::size_t newCapacity) noexcept { moves.reserve(newCapacity); }

    //void append(const Move& m) noexcept { moves.insert(end(), m); }
    //void append(ConstItr begItr, ConstItr endItr) noexcept {  //
    //    moves.insert(end(), begItr, endItr);
    //}
    //void append(const std::initializer_list<Move>& initList) noexcept {  //
    //    moves.insert(end(), initList);
    //}
    //void append(const Moves& ms) noexcept { append(ms.begin(), ms.end()); }

    auto find(const Move& m) const noexcept { return std::find(begin(), end(), m); }

    bool contains(const Move& m) const noexcept { return find(m) != end(); }

    auto remove(const Move& m) noexcept { return std::remove(begin(), end(), m); }

    template<typename Predicate>
    auto remove_if(Predicate pred) noexcept {
        return std::remove_if(begin(), end(), pred);
    }

    auto erase(ConstItr where) noexcept { return moves.erase(where); }
    auto erase(ConstItr fst, ConstItr lst) noexcept { return moves.erase(fst, lst); }
    bool erase(const Move& m) noexcept {
        auto itr = find(m);
        if (itr != end())
        {
            erase(itr);
            return true;
        }
        return false;
    }

    auto& operator[](std::size_t idx) const noexcept { return moves[idx]; }
    auto& operator[](std::size_t idx) noexcept { return moves[idx]; }

   private:
    Vector moves;
};

struct ExtMove final: public Move {
   public:
    using Move::Move;

    explicit ExtMove(const Move& m, int v = 0) noexcept :
        value(v) {
        move = m.raw();
    }

    //void operator=(const Move& m) noexcept { move = m.raw(); }

    friend bool operator<(const ExtMove& em1, const ExtMove& em2) noexcept {  //
        return em1.value < em2.value;
    }
    friend bool operator>(const ExtMove& em1, const ExtMove& em2) noexcept {  //
        return (em2 < em1);
    }
    friend bool operator<=(const ExtMove& em1, const ExtMove& em2) noexcept {  //
        return !(em1 > em2);
    }
    friend bool operator>=(const ExtMove& em1, const ExtMove& em2) noexcept {  //
        return !(em1 < em2);
    }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const noexcept = delete;

    // No need as MovePicker set values
    int value;  // = 0;
};

class ExtMoves final {
   public:
    using Vector   = std::vector<ExtMove>;
    using Itr      = Vector::iterator;
    using ConstItr = Vector::const_iterator;

    ExtMoves() noexcept = default;

    auto begin() const noexcept { return extMoves.begin(); }
    auto end() const noexcept { return extMoves.end(); }
    auto begin() noexcept { return extMoves.begin(); }
    auto end() noexcept { return extMoves.end(); }

    auto rbegin() noexcept { return extMoves.rbegin(); }
    auto rend() noexcept { return extMoves.rend(); }

    auto& front() noexcept { return extMoves.front(); }
    auto& back() noexcept { return extMoves.back(); }

    auto size() const noexcept { return extMoves.size(); }
    auto empty() const noexcept { return extMoves.empty(); }

    template<typename... Args>
    auto& emplace_back(Args&&... args) noexcept {
        return extMoves.emplace_back(std::forward<Args>(args)...);
    }
    template<typename... Args>
    auto& emplace(ConstItr where, Args&&... args) noexcept {
        return extMoves.emplace(where, std::forward<Args>(args)...);
    }

    void push_back(const ExtMove& em) noexcept { extMoves.push_back(em); }
    void push_back(ExtMove&& em) noexcept { extMoves.push_back(std::move(em)); }

    void pop_back() noexcept { extMoves.pop_back(); }

    void clear() noexcept { extMoves.clear(); }

    void resize(std::size_t newSize) noexcept { extMoves.resize(newSize); }
    void reserve(std::size_t newCapacity) noexcept { extMoves.reserve(newCapacity); }

    auto find(const Move& m) const noexcept { return std::find(begin(), end(), m); }

    bool contains(const Move& m) const noexcept { return find(m) != end(); }

    auto remove(const Move& m) noexcept { return std::remove(begin(), end(), m); }

    template<typename Predicate>
    auto remove_if(Predicate pred) noexcept {
        return std::remove_if(begin(), end(), pred);
    }

    auto erase(ConstItr where) noexcept { return extMoves.erase(where); }
    auto erase(ConstItr fst, ConstItr lst) noexcept { return extMoves.erase(fst, lst); }
    bool erase(const Move& m) noexcept {
        auto itr = find(m);
        if (itr != end())
        {
            erase(itr);
            return true;
        }
        return false;
    }

    auto& operator[](std::size_t idx) const noexcept { return extMoves[idx]; }
    auto& operator[](std::size_t idx) noexcept { return extMoves[idx]; }

   private:
    Vector extMoves;
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

template<GenType GT>
ExtMoves::Itr generate(ExtMoves& extMoves, const Position& pos) noexcept;

ExtMoves::Itr filter_legal(ExtMoves& extMoves, const Position& pos) noexcept;

// The MoveList struct wraps the generate() function and returns a convenient list of moves.
// Using MoveList is sometimes preferable to directly calling the lower level generate() function.
template<GenType GT>
struct MoveList final {

    explicit MoveList(const Position& pos) noexcept {  //
        extEnd = generate<GT>(extMoves, pos);
    }
    MoveList() noexcept                           = delete;
    MoveList(MoveList const&) noexcept            = delete;
    MoveList(MoveList&&) noexcept                 = delete;
    MoveList& operator=(const MoveList&) noexcept = delete;
    MoveList& operator=(MoveList&&) noexcept      = delete;

    auto begin() const noexcept { return extMoves.begin(); }
    auto end() const noexcept { return ExtMoves::ConstItr(extEnd); }
    auto begin() noexcept { return extMoves.begin(); }
    auto end() noexcept { return extEnd; }

    auto size() const noexcept { return std::size_t(std::distance(begin(), end())); }
    auto empty() const noexcept { return size() == 0; }

    auto find(const Move& m) const noexcept { return std::find(begin(), end(), m); }
    bool contains(const Move& m) const noexcept { return find(m) != end(); }

   private:
    ExtMoves      extMoves;
    ExtMoves::Itr extEnd;
};

using LegalMoveList = MoveList<LEGAL>;

//static_assert(sizeof(LegalMoveList) == 56, "Unexpected LegalMoveList size");

}  // namespace DON

#endif  // #ifndef MOVEGEN_H_INCLUDED
