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
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

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
    static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
    static_assert(std::is_signed_v<T> && std::is_integral_v<T>,
                  "T must be a signed integral (expects [-D,+D])");
    static_assert(D > 0, "D must be positive");
    static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");

   public:
    void operator=(T v) noexcept { value = v; }

    operator T() const noexcept { return value; }

    // Overload operator<< to modify the value
    void operator<<(int bonus) noexcept {
        // Make sure that bonus is in range [-D, +D]
        int clampedBonus = std::clamp(bonus, -D, +D);

        value += clampedBonus - value * std::abs(clampedBonus) / D;

        assert(std::abs(value) <= D);
    }

   private:
    T value;
};

// clang-format off
inline constexpr int  CAPTURE_HISTORY_LIMIT = 10692;
inline constexpr int    QUIET_HISTORY_LIMIT =  7183;
inline constexpr int PIECE_SQ_HISTORY_LIMIT = 30000;

inline constexpr std::size_t QUIET_HISTORY_SIZE = 0x10000;

static_assert(exactly_one(QUIET_HISTORY_SIZE), "QUIET_HISTORY_SIZE has to be a power of 2");

inline constexpr int         PAWN_HISTORY_LIMIT = 8192;
inline constexpr std::size_t PAWN_HISTORY_SIZE  = 0x4000;

static_assert(exactly_one(PAWN_HISTORY_SIZE), "PAWN_HISTORY_SIZE has to be a power of 2");
// clang-format on

constexpr std::uint16_t pawn_index(Key pawnKey) noexcept {
    return compress_key16(pawnKey) & (PAWN_HISTORY_SIZE - 1);
}

inline constexpr std::uint16_t LOW_PLY_SIZE = 5;

enum HistoryType : std::uint8_t {
    HCapture,       // By move's [piece][dst][captured piece type]
    HQuiet,         // By color and move's org and dst squares
    HPawn,          // By pawn structure and a move's [piece][dst]
    HPieceSq,       // By move's [piece][sq]
    HContinuation,  // By combination of pair of moves
    HLowPlyQuiet,   // By ply and move's org and dst squares
    HTTMove,
};

namespace internal {
template<int D, std::size_t... Sizes>
using StatsVector = MultiVector<StatsEntry<std::int16_t, D>, Sizes...>;

template<HistoryType T>
struct HistoryDef;

template<>
struct HistoryDef<HCapture> final {
    using Type = StatsVector<CAPTURE_HISTORY_LIMIT, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;
};

// It records how often quiet moves have been successful or not during the current search,
// and is used for reduction and move ordering decisions.
// see https://www.chessprogramming.org/Butterfly_Boards
template<>
struct HistoryDef<HQuiet> final {
    using Type = StatsVector<QUIET_HISTORY_LIMIT, COLOR_NB, QUIET_HISTORY_SIZE>;
};

template<>
struct HistoryDef<HPawn> final {
    using Type = StatsVector<PAWN_HISTORY_LIMIT, PAWN_HISTORY_SIZE, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryDef<HPieceSq> final {
    using Type = StatsVector<PIECE_SQ_HISTORY_LIMIT, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryDef<HContinuation> final {
    using Type = MultiVector<HistoryDef<HPieceSq>::Type, PIECE_NB, SQUARE_NB>;
};

// It is used to improve quiet move ordering near the root.
template<>
struct HistoryDef<HLowPlyQuiet> final {
    using Type = StatsVector<QUIET_HISTORY_LIMIT, LOW_PLY_SIZE, QUIET_HISTORY_SIZE>;
};

template<>
struct HistoryDef<HTTMove> final {
    using Type = StatsEntry<std::int16_t, 8192>;
};
}  // namespace internal

// Alias template for convenience
template<HistoryType T>
using History = typename internal::HistoryDef<T>::Type;

// clang-format off
inline constexpr int         CORRECTION_HISTORY_LIMIT = 1024;
inline constexpr std::size_t CORRECTION_HISTORY_SIZE  = 0x10000;

static_assert(exactly_one(CORRECTION_HISTORY_SIZE), "CORRECTION_HISTORY_SIZE has to be a power of 2");
// clang-format on

constexpr std::uint16_t correction_index(Key corrKey) noexcept { return compress_key16(corrKey); }

// Correction histories record differences between the static evaluation of
// positions and their search score.
// It is used to improve the static evaluation used by some search heuristics.
// see https://www.chessprogramming.org/Static_Evaluation_Correction_History

enum CorrectionHistoryType : std::uint8_t {
    CHPawn,          // By color and pawn structure
    CHMinor,         // By color and minor piece (Knight, Bishop) structure
    CHNonPawn,       // By color and non-pawn structure
    CHPieceSq,       // By move's [piece][sq]
    CHContinuation,  // By combination of pair of moves
};

namespace internal {
template<std::size_t... Sizes>
using CorrectionStatsVector = StatsVector<CORRECTION_HISTORY_LIMIT, Sizes...>;

template<CorrectionHistoryType T>
struct CorrectionHistoryDef;

template<>
struct CorrectionHistoryDef<CHPawn> final {
    using Type = CorrectionStatsVector<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryDef<CHMinor> final {
    using Type = CorrectionStatsVector<CORRECTION_HISTORY_SIZE, COLOR_NB>;
};

template<>
struct CorrectionHistoryDef<CHNonPawn> final {
    using Type = CorrectionStatsVector<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryDef<CHPieceSq> final {
    using Type = CorrectionStatsVector<PIECE_NB, SQUARE_NB>;
};

template<>
struct CorrectionHistoryDef<CHContinuation> final {
    using Type = MultiVector<CorrectionHistoryDef<CHPieceSq>::Type, PIECE_NB, SQUARE_NB>;
};
}  // namespace internal

// Alias template for convenience
template<CorrectionHistoryType T>
using CorrectionHistory = typename internal::CorrectionHistoryDef<T>::Type;

}  // namespace DON

#endif  // #ifndef HISTORY_H_INCLUDED
