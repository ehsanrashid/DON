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
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_map>

#include "misc.h"
#include "types.h"

namespace DON {

using NumaIndex = std::size_t;

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
        // Apply gravity-based adjustment
        value += clampedBonus - value * std::abs(clampedBonus) / D;

        assert(std::abs(value) <= D);
    }

   private:
    T value;
};


inline constexpr std::uint16_t LOW_PLY_QUIET_SIZE = 5;

inline constexpr int CORRECTION_HISTORY_LIMIT = 1024;

inline constexpr std::size_t UINT16_HISTORY_SIZE = 0x10000;
static_assert((UINT16_HISTORY_SIZE & (UINT16_HISTORY_SIZE - 1)) == 0,
              "UINT16_HISTORY_SIZE has to be a power of 2");

inline constexpr std::size_t PAWN_HISTORY_SIZE = 0x4000;
static_assert((PAWN_HISTORY_SIZE & (PAWN_HISTORY_SIZE - 1)) == 0,
              "PAWN_HISTORY_SIZE has to be a power of 2");

constexpr std::uint16_t pawn_index(Key pawnKey) noexcept {
    return compress_key16(pawnKey) & (PAWN_HISTORY_SIZE - 1);
}

inline constexpr std::size_t BASE_CORRECTION_HISTORY_SIZE = UINT16_HISTORY_SIZE;
static_assert((BASE_CORRECTION_HISTORY_SIZE & (BASE_CORRECTION_HISTORY_SIZE - 1)) == 0,
              "BASE_CORRECTION_HISTORY_SIZE has to be a power of 2");

constexpr std::uint16_t correction_index(Key corrKey) noexcept {  //
    return compress_key16(corrKey);
}

enum HistoryType : std::uint8_t {
    H_CAPTURE,        // By move's [piece][dstSq][captured piece type]
    H_QUIET,          // By color and move's orgSq and dstSq squares
    H_PAWN,           // By pawn structure and a move's [piece][dstSq]
    H_LOW_PLY_QUIET,  // By ply and move's orgSq and dstSq squares
    H_TT_MOVE,        //
    H_PIECE_SQ,       // By move's [piece][dstSq]
    H_CONTINUATION,   // By combination of pair of moves
};

namespace internal {
template<int D, std::size_t... Sizes>
using StatsMultiArray = MultiArray<StatsEntry<std::int16_t, D>, Sizes...>;

template<HistoryType T>
struct HistoryDef;

template<>
struct HistoryDef<H_CAPTURE> final {
    using Type = StatsMultiArray<10692, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;
};

// It records how often quiet moves have been successful or not during the current search,
// It is used for reduction and move ordering decisions.
// see https://www.chessprogramming.org/Butterfly_Boards
template<>
struct HistoryDef<H_QUIET> final {
    using Type = StatsMultiArray<7183, COLOR_NB, UINT16_HISTORY_SIZE>;
};

template<>
struct HistoryDef<H_PAWN> final {
    using Type = StatsMultiArray<8192, PAWN_HISTORY_SIZE, PIECE_NB, SQUARE_NB>;
};

// It is used to improve quiet move ordering near the root.
template<>
struct HistoryDef<H_LOW_PLY_QUIET> final {
    using Type = StatsMultiArray<7183, LOW_PLY_QUIET_SIZE, UINT16_HISTORY_SIZE>;
};

template<>
struct HistoryDef<H_TT_MOVE> final {
    using Type = StatsEntry<std::int16_t, 8192>;
};

template<>
struct HistoryDef<H_PIECE_SQ> final {
    using Type = StatsMultiArray<30000, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryDef<H_CONTINUATION> final {
    using Type = MultiArray<HistoryDef<H_PIECE_SQ>::Type, PIECE_NB, SQUARE_NB>;
};
}  // namespace internal

// Alias template for convenience
template<HistoryType T>
using History = typename internal::HistoryDef<T>::Type;

// Correction histories record differences between the static evaluation of positions and their search score.
// It is used to improve the static evaluation used by some search heuristics.
// see https://www.chessprogramming.org/Static_Evaluation_Correction_History

enum CorrectionHistoryType : std::uint8_t {
    CH_PAWN,          // By color and pawn structure
    CH_MINOR,         // By color and minor piece (Knight, Bishop) structure
    CH_NON_PAWN,      // By color and non-pawn piece structure
    CH_PIECE_SQ,      // By move's [piece][dstSq]
    CH_CONTINUATION,  // By combination of pair of moves
};

namespace internal {
template<std::size_t... Sizes>
using CorrectionStatsMultiArray = StatsMultiArray<CORRECTION_HISTORY_LIMIT, Sizes...>;

template<CorrectionHistoryType T>
struct CorrectionHistoryDef;

template<>
struct CorrectionHistoryDef<CH_PAWN> final {
    using Type = CorrectionStatsMultiArray<UINT16_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryDef<CH_MINOR> final {
    using Type = CorrectionStatsMultiArray<UINT16_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryDef<CH_NON_PAWN> final {
    using Type = CorrectionStatsMultiArray<UINT16_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryDef<CH_PIECE_SQ> final {
    using Type = CorrectionStatsMultiArray<PIECE_NB, SQUARE_NB>;
};

template<>
struct CorrectionHistoryDef<CH_CONTINUATION> final {
    using Type = MultiArray<CorrectionHistoryDef<CH_PIECE_SQ>::Type, PIECE_NB, SQUARE_NB>;
};
}  // namespace internal

// Alias template for convenience
template<CorrectionHistoryType T>
using CorrectionHistory = typename internal::CorrectionHistoryDef<T>::Type;


struct CorrectionHistories final {
   public:
    CorrectionHistories(std::size_t threadCount) noexcept :
        Size(threadCount * BASE_CORRECTION_HISTORY_SIZE) {}

    std::size_t size() const noexcept { return Size; }

   private:
    const std::size_t Size;
};

using CorrectionHistoriesMap = std::unordered_map<NumaIndex, CorrectionHistories>;

}  // namespace DON

#endif  // #ifndef HISTORY_H_INCLUDED
