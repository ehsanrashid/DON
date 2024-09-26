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
#include <deque>
#include <iterator>
#include <limits>
#include <type_traits>  // IWYU pragma: keep

#include "misc.h"
#include "movegen.h"
#include "types.h"

namespace DON {

class Position;

// clang-format off
// In stats table, D=0 means that the template parameter is not used
constexpr inline int STATS_PARAM_NOT_USED = 0;

constexpr inline int BUTTERFLY_HISTORY_LIMIT = 0x1C0F;
constexpr inline int CAPTURE_HISTORY_LIMIT   = 0x29C4;
constexpr inline int PIECE_SQ_HISTORY_LIMIT  = 0x7500;

constexpr inline int   PAWN_HISTORY_LIMIT = 0x2000;
constexpr inline Key16 PAWN_HISTORY_SIZE  = 0x400;
static_assert((PAWN_HISTORY_SIZE & (PAWN_HISTORY_SIZE - 1)) == 0,
              "PAWN_HISTORY_SIZE has to be a power of 2");
constexpr Key16 pawn_index               (Key pawnKey)     noexcept { return pawnKey     & (PAWN_HISTORY_SIZE - 1); }

constexpr inline int   PAWN_CORRECTION_HISTORY_LIMIT = 0x400;
constexpr inline Key16 PAWN_CORRECTION_HISTORY_SIZE  = 0x4000;
static_assert((PAWN_CORRECTION_HISTORY_SIZE & (PAWN_CORRECTION_HISTORY_SIZE - 1)) == 0,
              "PAWN_CORRECTION_HISTORY_SIZE has to be a power of 2");
constexpr Key16 pawn_correction_index    (Key pawnKey)     noexcept { return pawnKey     & (PAWN_CORRECTION_HISTORY_SIZE - 1); }

constexpr inline int   OTHER_CORRECTION_HISTORY_LIMIT = 0x400;
constexpr inline Key16 OTHER_CORRECTION_HISTORY_SIZE  = 0x8000;
static_assert((OTHER_CORRECTION_HISTORY_SIZE & (OTHER_CORRECTION_HISTORY_SIZE - 1)) == 0,
              "OTHER_CORRECTION_HISTORY_SIZE has to be a power of 2");

constexpr Key16 material_correction_index(Key materialKey) noexcept { return materialKey & (OTHER_CORRECTION_HISTORY_SIZE - 1); }
constexpr Key16 major_correction_index   (Key majorKey)    noexcept { return majorKey    & (OTHER_CORRECTION_HISTORY_SIZE - 1); }
constexpr Key16 minor_correction_index   (Key minorKey)    noexcept { return minorKey    & (OTHER_CORRECTION_HISTORY_SIZE - 1); }
constexpr Key16 non_pawn_correction_index(Key nonPawnKey)  noexcept { return nonPawnKey  & (OTHER_CORRECTION_HISTORY_SIZE - 1); }
// clang-format on

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

// StatsEntry stores the stat table value. It is usually a integer but also could be a nested Stats.
// Use a class instead of a naked value to directly call history update operator<<() on the entry
// so to use stats tables at caller sites as simple multi-dim arrays.
template<typename T, int D>
class StatsEntry final {
   public:
    void operator=(const T& e) noexcept { entry = e; }
    T*   operator&() noexcept { return &entry; }
    T*   operator->() noexcept { return &entry; }
    operator const T&() const noexcept { return entry; }

    void operator<<(int bonus) noexcept {
        static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");

        // Make sure that bonus is in range [-D, D]
        int clampedBonus = std::clamp(bonus, -D, +D);
        entry += clampedBonus - entry * std::abs(clampedBonus) / D;

        assert(std::abs(entry) <= D);
    }

   private:
    T entry;
};

// Stats is a generic N-dimensional array used to store various statistics.
// The first template parameter T is the base type of the array, and the second
// template parameter D limits the range of updates in [-D, +D] when update
// values with the << operator, while the last parameters (Size and Sizes)
// encode the dimensions of the array.
template<typename T, int D, std::size_t Size, std::size_t... Sizes>
struct Stats final: public std::array<Stats<T, D, Sizes...>, Size> {
    using StatsAlias = Stats<T, D, Size, Sizes...>;

    void fill(const T& v) noexcept {

        // For standard-layout 'this' points to the first struct member
        assert(std::is_standard_layout_v<StatsAlias>);

        using Entry = StatsEntry<T, D>;

        auto* p = reinterpret_cast<Entry*>(this);
        std::fill(p, p + sizeof(*this) / sizeof(Entry), v);
    }
};

template<typename T, int D, std::size_t Size>
struct Stats<T, D, Size> final: public std::array<StatsEntry<T, D>, Size> {};

// clang-format off

// ButterflyHistory records how often quiet moves have been successful or not
// during the current search, and is used for reduction and move ordering decisions.
// It is addressed by color and move's from and to squares,
// see www.chessprogramming.org/Butterfly_Boards (~11 Elo)
using ButterflyHistory    = Stats<std::int16_t, BUTTERFLY_HISTORY_LIMIT, COLOR_NB, SQUARE_NB * SQUARE_NB>;

// CaptureHistory is addressed by a move's [piece][dst][captured piece type]
using CaptureHistory      = Stats<std::int16_t, CAPTURE_HISTORY_LIMIT, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;

// PieceSqHistory is like ButterflyHistory but is addressed by a move's [piece][dst]
using PieceSqHistory      = Stats<std::int16_t, PIECE_SQ_HISTORY_LIMIT, PIECE_NB, SQUARE_NB>;

// ContinuationHistory is the combined history of a given pair of moves, usually
// the current one given a previous one. The nested history table is based on
// PieceSqHistory instead of ButterflyBoards. (~63 Elo)
using ContinuationHistory = std::array2d<Stats<PieceSqHistory, STATS_PARAM_NOT_USED, PIECE_NB, SQUARE_NB>, 2, 2>;

// PawnHistory is addressed by the pawn structure and a move's [piece][dst]
using PawnHistory = Stats<std::int16_t, PAWN_HISTORY_LIMIT, PAWN_HISTORY_SIZE, PIECE_NB, SQUARE_NB>;

// Correction histories record differences between the static evaluation of
// positions and their search score.
// It is used to improve the static evaluation used by some search heuristics.
// see https://www.chessprogramming.org/Static_Evaluation_Correction_History

// PawnCorrectionHistory       is addressed by color and pawn structure
using PawnCorrectionHistory     = Stats<std::int16_t, PAWN_CORRECTION_HISTORY_LIMIT,  COLOR_NB, PAWN_CORRECTION_HISTORY_SIZE>;
// NonPawnCorrectionHistory    is addressed by color and non-pawn material structure
using NonPawnCorrectionHistory  = std::array<Stats<std::int16_t, OTHER_CORRECTION_HISTORY_LIMIT, COLOR_NB, OTHER_CORRECTION_HISTORY_SIZE>, COLOR_NB>;
// MinorPieceCorrectionHistory is addressed by color and minor piece (Bishop, Knight) structure
using MinorCorrectionHistory    = Stats<std::int16_t, OTHER_CORRECTION_HISTORY_LIMIT, COLOR_NB, OTHER_CORRECTION_HISTORY_SIZE>;
// MajorPieceCorrectionHistory is addressed by color and major piece (Queen, Rook) structure
using MajorCorrectionHistory    = Stats<std::int16_t, OTHER_CORRECTION_HISTORY_LIMIT, COLOR_NB, OTHER_CORRECTION_HISTORY_SIZE>;
// MaterialCorrectionHistory   is addressed by color and material count structure
using MaterialCorrectionHistory = Stats<std::int16_t, OTHER_CORRECTION_HISTORY_LIMIT, COLOR_NB, OTHER_CORRECTION_HISTORY_SIZE>;
// clang-format on

enum Stage : std::uint8_t {
    STAGE_NONE,

    // Generate main-search moves
    MAIN_TT,
    CAPTURE_INIT,
    CAPTURE_GOOD,
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
};

constexpr Stage operator+(Stage s, int i) noexcept { return Stage(int(s) + i); }
inline Stage&   operator++(Stage& s) noexcept { return s = s + 1; }

// MovePicker class is used to pick one pseudo-legal move at a time from the given current position.
// The most important method is next_move(), which returns a new pseudo-legal move each time it is called,
// until there are no moves left, when Move::None() is returned. In order to improve the efficiency of the
// alpha-beta algorithm, MovePicker attempts to return the moves which are most likely to get a cut-off first.
class MovePicker final {
   public:
    // Constructors
    MovePicker(const Position&         p,
               Move                    ttm,
               const ButterflyHistory* mainHist,
               const CaptureHistory*   capHist,
               const PieceSqHistory**  conHist,
               const PawnHistory*      pawnHist,
               const ButterflyHistory* rootHist = nullptr,
               Value                   th       = 0) noexcept;
    MovePicker() noexcept                             = delete;
    MovePicker(const MovePicker&) noexcept            = delete;
    MovePicker(MovePicker&&) noexcept                 = delete;
    MovePicker& operator=(const MovePicker&) noexcept = delete;
    MovePicker& operator=(MovePicker&&) noexcept      = delete;

    Move next_move(bool pickQuiets = false) noexcept;

    Stage stage = STAGE_NONE;

   private:
    void next_stage() noexcept { ++stage; }

    template<GenType GT>
    void score() noexcept;

    void sort_partial(int limit = std::numeric_limits<int>::min()) noexcept;

    auto begin() const noexcept { return curExtItr; }
    auto end() const noexcept { return endExtItr; }
    auto next() noexcept { return ++curExtItr; }

    auto current() const noexcept { return *curExtItr; }
    auto current_next() noexcept { return *curExtItr++; }

    void swap_maximum(int tolerance = 0) noexcept;

    //auto size() const noexcept { return std::distance(begin(), end()); }

    const Position&         pos;
    const Move              ttMove;
    const ButterflyHistory* mainHistory;
    const CaptureHistory*   captureHistory;
    const PieceSqHistory**  continuationHistory;
    const PawnHistory*      pawnHistory;
    const ButterflyHistory* rootHistory;
    const Value             threshold;

    int curMin = std::numeric_limits<int>::min();
    int curMax = std::numeric_limits<int>::max();

    ExtMoves            extMoves;
    ExtMoves::NormalItr curExtItr, endExtItr;

    Moves            badCaptures;
    Moves::NormalItr curItr, endItr;
};

}  // namespace DON

#endif  // #ifndef MOVEPICK_H_INCLUDED
