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

#ifndef HISTORY_H_INCLUDED
#define HISTORY_H_INCLUDED

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include "bitboard.h"
#include "misc.h"
#include "types.h"

namespace DON {

// StatsEntry is the container of various numerical statistics.
// Use a class instead of a naked value to directly call history update operator<<() on the entry.
// The first template parameter T is the base type of the StatsEntry and
// the second template parameter D limits the range of updates in [-D, D] when update values with the << operator
template<typename T, int D>
class StatsEntry final {
   public:
    static_assert(std::is_arithmetic<T>::value, "Not an arithmetic type");
    static_assert(D > 0, "D must be positive");
    static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");

    void operator=(T v) noexcept { value.store(v, std::memory_order_relaxed); }

    operator T() const noexcept { return value.load(std::memory_order_acquire); }

    // Overload operator<< to modify the value
    void operator<<(int bonus) noexcept {
        // Make sure that bonus is in range [-D, D]
        int clampedBonus = std::clamp(bonus, -D, +D);

        T oldValue = value.load(std::memory_order_acquire);

        while (true)
        {
            T newValue = clampedBonus + oldValue * ((D - std::abs(clampedBonus)) / D);
            assert(std::abs(newValue) <= D);

            if (value.compare_exchange_weak(oldValue, newValue, std::memory_order_release,
                                            std::memory_order_relaxed))
                break;
        }
    }

   private:
    std::atomic<T> value{};
};


// clang-format off
constexpr inline int  CAPTURE_HISTORY_LIMIT = 10692u;
constexpr inline int    QUIET_HISTORY_LIMIT =  7183u;
constexpr inline int PIECE_SQ_HISTORY_LIMIT = 30000u;

constexpr inline std::size_t IMBALANCE_SIZE = 4u;

constexpr inline int         PAWN_HISTORY_LIMIT = 8192u;
constexpr inline std::size_t PAWN_HISTORY_SIZE  = 0x400u;
static_assert(exactly_one(PAWN_HISTORY_SIZE), "PAWN_HISTORY_SIZE has to be a power of 2");
constexpr std::size_t pawn_index(Key pawnKey) noexcept { return pawnKey & (PAWN_HISTORY_SIZE - 1); }

constexpr inline std::uint16_t LOW_PLY_SIZE = 4u;

enum HistoryType : std::uint8_t {
    HCapture,       // By move's [piece][dst][captured piece type]
    HQuiet,         // By color and move's org and dst squares
    HPawn,          // By pawn structure and a move's [piece][dst]
    HPieceSq,       // By move's [piece][sq]
    HContinuation,  // By combination of pair of moves
    HLowPlyQuiet,   // By ply and move's org and dst squares
};

namespace Internal {
template<int D, std::size_t... Sizes>
using StatsArray = MultiArray<StatsEntry<std::int16_t, D>, Sizes...>;

template<HistoryType T>
struct HistoryTypedef;

template<>
struct HistoryTypedef<HCapture> final {
    using Type = StatsArray<CAPTURE_HISTORY_LIMIT, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB, IMBALANCE_SIZE + 1>;
};

// It records how often quiet moves have been successful or not during the current search,
// and is used for reduction and move ordering decisions.
// see https://www.chessprogramming.org/Butterfly_Boards
template<>
struct HistoryTypedef<HQuiet> final {
    using Type = StatsArray<QUIET_HISTORY_LIMIT, COLOR_NB, SQUARE_NB * SQUARE_NB>;
};

template<>
struct HistoryTypedef<HPawn> final {
    using Type = StatsArray<PAWN_HISTORY_LIMIT, PAWN_HISTORY_SIZE, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryTypedef<HPieceSq> final {
    using Type = StatsArray<PIECE_SQ_HISTORY_LIMIT, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryTypedef<HContinuation> final {
    using Type = MultiArray<HistoryTypedef<HPieceSq>::Type, PIECE_NB, SQUARE_NB>;
};

// It is used to improve quiet move ordering near the root.
template<>
struct HistoryTypedef<HLowPlyQuiet> final {
    using Type = StatsArray<QUIET_HISTORY_LIMIT, LOW_PLY_SIZE, SQUARE_NB * SQUARE_NB>;
};
}  // namespace Internal

// Alias template for convenience
template<HistoryType T>
using History = typename Internal::HistoryTypedef<T>::Type;


constexpr inline int         CORRECTION_HISTORY_LIMIT = 1024u;
constexpr inline std::size_t CORRECTION_HISTORY_SIZE  = 0x8000u;
static_assert(exactly_one(CORRECTION_HISTORY_SIZE), "CORRECTION_HISTORY_SIZE has to be a power of 2");
constexpr std::size_t correction_index(Key corrKey) noexcept { return corrKey & (CORRECTION_HISTORY_SIZE - 1); }

// Correction histories record differences between the static evaluation of
// positions and their search score.
// It is used to improve the static evaluation used by some search heuristics.
// see https://www.chessprogramming.org/Static_Evaluation_Correction_History

enum CorrectionHistoryType : std::uint8_t {
    CHPawn,          // By color and pawn structure
    CHMinor,         // By color and minor piece (Knight, Bishop) structure
    CHMajor,         // By color and major piece (Rook, Queen) structure
    CHPieceSq,       // By move's [piece][sq]
    CHContinuation,  // By combination of pair of moves
};

namespace Internal {
template<std::size_t... Sizes>
using CorrectionStatsArray = StatsArray<CORRECTION_HISTORY_LIMIT, Sizes...>;

template<CorrectionHistoryType T>
struct CorrectionHistoryTypedef;

template<>
struct CorrectionHistoryTypedef<CHPawn> final {
    using Type = CorrectionStatsArray<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHMinor> final {
    using Type = CorrectionStatsArray<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHMajor> final {
    using Type = CorrectionStatsArray<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHPieceSq> final {
    using Type = CorrectionStatsArray<PIECE_NB, SQUARE_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHContinuation> final {
    using Type = MultiArray<CorrectionHistoryTypedef<CHPieceSq>::Type, PIECE_NB, SQUARE_NB>;
};
}  // namespace Internal

// Alias template for convenience
template<CorrectionHistoryType T>
using CorrectionHistory = typename Internal::CorrectionHistoryTypedef<T>::Type;

// clang-format on

}  // namespace DON

#endif  // #ifndef HISTORY_H_INCLUDED
