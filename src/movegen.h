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

#include <cstddef>
#include <cstdint>

#include "types.h"

namespace DON {

class Position;

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
    using value_type      = Move;
    using const_pointer   = const value_type*;
    using pointer         = const_pointer;
    using const_reference = const value_type&;
    using reference       = const_reference;
    using const_iterator  = const_pointer;
    using iterator        = const_iterator;
    using size_type       = std::size_t;

    // Generate moves into the internal buffer.
    explicit MoveList(const Position& pos) noexcept :
        endMoves(generate<GT, Any>(pos, moves)) {
#if !defined(NDEBUG)
        assert(moves <= endMoves && endMoves <= moves + MAX_MOVES);
#endif
    }

    MoveList() noexcept                           = delete;
    MoveList(const MoveList&) noexcept            = delete;
    MoveList(MoveList&&) noexcept                 = delete;
    MoveList& operator=(const MoveList&) noexcept = delete;
    MoveList& operator=(MoveList&&) noexcept      = delete;

    [[nodiscard]] const_iterator begin() const noexcept { return moves; }
    [[nodiscard]] const_iterator end() const noexcept { return endMoves; }

    [[nodiscard]] size_type size() const noexcept { return end() - begin(); }
    [[nodiscard]] bool      empty() const noexcept { return begin() == end(); }

    [[nodiscard]] const_iterator find(const Move& m) const noexcept {
        return std::find(begin(), end(), m);
    }
    [[nodiscard]] bool contains(const Move& m) const noexcept { return find(m) != end(); }

    [[nodiscard]] const_pointer data() const noexcept { return moves; }

    //#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L
    //    // Optional: span view (C++20)
    //    [[nodiscard]] std::span<const Move> view() const noexcept { return {moves, size()}; }
    //#endif

   private:
    value_type     moves[MAX_MOVES];
    const_iterator endMoves;
};

}  // namespace DON

#endif  // #ifndef MOVEGEN_H_INCLUDED
