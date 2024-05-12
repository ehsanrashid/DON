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

#ifndef MOVEPICK_H_INCLUDED
#define MOVEPICK_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>  // IWYU pragma: keep

#include "movegen.h"
#include "types.h"

namespace DON {

class Position;

using KillerMoves = std::array<Move, 2>;

constexpr std::uint16_t PAWN_HISTORY_SIZE        = 0x200;   // has to be a power of 2
constexpr std::uint16_t CORRECTION_HISTORY_SIZE  = 0x4000;  // has to be a power of 2
constexpr std::int32_t  CORRECTION_HISTORY_LIMIT = 1024;

static_assert((PAWN_HISTORY_SIZE & (PAWN_HISTORY_SIZE - 1)) == 0,
              "PAWN_HISTORY_SIZE has to be a power of 2");
static_assert((CORRECTION_HISTORY_SIZE & (CORRECTION_HISTORY_SIZE - 1)) == 0,
              "CORRECTION_HISTORY_SIZE has to be a power of 2");

constexpr std::uint16_t pawn_index(Key pawnKey) noexcept {
    return std::uint16_t(pawnKey) & (PAWN_HISTORY_SIZE - 1);
}
constexpr std::uint16_t correction_index(Key pawnKey) noexcept {
    return std::uint16_t(pawnKey) & (CORRECTION_HISTORY_SIZE - 1);
}

// StatsEntry stores the stat table value. It is usually a number but could
// be a move or even a nested history. Use a class instead of a naked value
// to directly call history update operator<<() on the entry so to use stats
// tables at caller sites as simple multi-dim arrays.
template<typename T, std::int32_t D>
class StatsEntry final {
    T entry;

   public:
    void operator=(const T& e) noexcept { entry = e; }
    T*   operator&() noexcept { return &entry; }
    T*   operator->() noexcept { return &entry; }
    operator const T&() const noexcept { return entry; }

    void operator<<(std::int32_t bonus) noexcept {
        static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");

        // Make sure that bonus is in range [-D, D]
        std::int32_t clampedBonus = std::clamp(bonus, -D, +D);
        entry += clampedBonus - entry * std::abs(clampedBonus) / D;

        assert(std::abs(entry) <= D);
    }
};

// Stats is a generic N-dimensional array used to store various statistics.
// The first template parameter T is the base type of the array, and the second
// template parameter D limits the range of updates in [-D, D] when update
// values with the << operator, while the last parameters (Size and Sizes)
// encode the dimensions of the array.
template<typename T, std::int32_t D, std::uint32_t Size, std::uint32_t... Sizes>
struct Stats final: public std::array<Stats<T, D, Sizes...>, Size> {
    using stats = Stats<T, D, Size, Sizes...>;

    void fill(const T& v) noexcept {

        // For standard-layout 'this' points to the first struct member
        assert(std::is_standard_layout_v<stats>);

        using Entry = StatsEntry<T, D>;
        auto* p     = reinterpret_cast<Entry*>(this);
        std::fill(p, p + sizeof(*this) / sizeof(Entry), v);
    }
};

template<typename T, std::int32_t D, std::uint32_t Size>
struct Stats<T, D, Size> final: public std::array<StatsEntry<T, D>, Size> {};

// In stats table, D=0 means that the template parameter is not used
constexpr std::int32_t NOT_USED = 0;

// ButterflyHistory records how often quiet moves have been successful or unsuccessful
// during the current search, and is used for reduction and move ordering decisions.
// It uses 2 tables (one for each color) indexed by the move's from and to squares,
// see www.chessprogramming.org/Butterfly_Boards (~11 Elo)
using ButterflyHistory = Stats<std::int16_t, 7183, COLOR_NB, SQUARE_NB * SQUARE_NB>;

// CounterMoveHistory stores counter moves indexed by [piece][to] of the previous move,
// see www.chessprogramming.org/Countermove_Heuristic
using CounterMoveHistory = Stats<Move, NOT_USED, PIECE_NB, SQUARE_NB>;

// CapturePieceDstHistory is addressed by a move's [piece][to][captured piece type]
using CapturePieceDstHistory = Stats<std::int16_t, 10692, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;

// PieceDstHistory is like ButterflyHistory but is addressed by a move's [piece][to]
using PieceDstHistory = Stats<std::int16_t, 29952, PIECE_NB, SQUARE_NB>;

// ContinuationHistory is the combined history of a given pair of moves, usually
// the current one given a previous one. The nested history table is based on
// PieceDstHistory instead of ButterflyBoards.
// (~63 Elo)
using ContinuationHistory = Stats<PieceDstHistory, NOT_USED, PIECE_NB, SQUARE_NB>;

// PawnHistory is addressed by the pawn structure and a move's [piece][to]
using PawnHistory = Stats<std::int16_t, 8192, PAWN_HISTORY_SIZE, PIECE_NB, SQUARE_NB>;

// CorrectionHistory is addressed by color and pawn structure
using CorrectionHistory =
  Stats<std::int16_t, CORRECTION_HISTORY_LIMIT, COLOR_NB, CORRECTION_HISTORY_SIZE>;

template<typename T, std::uint32_t M, std::uint32_t N>
using Array2D = std::array<std::array<T, N>, M>;

enum Stage : std::uint8_t {
    NO_STAGE,
    // Generate main-search moves
    MAIN_TT,
    CAPTURE_INIT,
    CAPTURE_GOOD,
    REFUTATION,
    QUIET_INIT,
    QUIET_GOOD,
    CAPTURE_BAD,
    QUIET_BAD,

    // Generate evasion moves
    EVASION_TT,
    EVASION_INIT,
    EVASION,

    // Generate probcut moves
    PROBCUT_TT,
    PROBCUT_INIT,
    PROBCUT,

    // Generate qsearch moves
    QSEARCH_TT,
    QCAPTURE_INIT,
    QCAPTURE,
    QCHECK_INIT,
    QCHECK
};

constexpr Stage operator+(Stage s, int i) noexcept { return Stage(int(s) + i); }
constexpr Stage operator-(Stage s, int i) noexcept { return Stage(int(s) - i); }
constexpr Stage operator+(Stage s, bool b) noexcept { return s + int(b); }
constexpr Stage operator-(Stage s, bool b) noexcept { return s - int(b); }
inline Stage&   operator++(Stage& s) noexcept { return s = s + 1; }
inline Stage&   operator--(Stage& s) noexcept { return s = s - 1; }

// MovePicker class is used to pick one pseudo-legal move at a time from the
// current position. The most important method is next_move(), which returns a
// new pseudo-legal move each time it is called, until there are no moves left,
// when Move::None() is returned. In order to improve the efficiency of the
// alpha-beta algorithm, MovePicker attempts to return the moves which are most
// likely to get a cut-off first.
class MovePicker final {
   public:
    MovePicker(const MovePicker&)            = delete;
    MovePicker& operator=(const MovePicker&) = delete;
    MovePicker(const Position&,
               Move,
               Depth,
               const ButterflyHistory*,
               const CapturePieceDstHistory*,
               const PieceDstHistory**,
               const PawnHistory*,
               const KillerMoves&,
               Move) noexcept;
    MovePicker(const Position&,
               Move,
               Depth,
               const ButterflyHistory*,
               const CapturePieceDstHistory*,
               const PieceDstHistory**,
               const PawnHistory*) noexcept;
    MovePicker(const Position&, Move, int, const CapturePieceDstHistory*) noexcept;

    Move next_move() noexcept;

    Stage stage      = NO_STAGE;
    bool  pickQuiets = true;

   private:
    template<GenType GT>
    void score() noexcept;

    void partial_sort(int limit) noexcept;

    template<bool PickBest = false, typename Predicate>
    Move pick(Predicate filter) noexcept;

    ExtMove* begin() const noexcept { return cur; }
    ExtMove* end() const noexcept { return endMoves; }

    constexpr std::uint8_t size() const noexcept { return endMoves - cur; }

    const Position&               pos;
    const ButterflyHistory*       mainHistory;
    const CapturePieceDstHistory* captureHistory;
    const PieceDstHistory**       continuationHistory;
    const PawnHistory*            pawnHistory;
    Move                          ttMove;
    Depth                         depth;
    int                           threshold;

    ExtMove *cur, *endMoves, *endBadCaptures, *beginBadQuiets, *endBadQuiets;
    ExtMove  refutations[3];
    ExtMove  moves[MAX_MOVES];
};

}  // namespace DON

#endif  // #ifndef MOVEPICK_H_INCLUDED
