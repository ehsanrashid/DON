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
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
//#include <mutex>
#include <type_traits>  // IWYU pragma: keep

#include "misc.h"
#include "types.h"

namespace DON {

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

    // Overload operator*= to multiply the entry by a factor
    //void operator*=(double factor) noexcept { entry *= factor; }

    // Overload operator<< to modify the entry
    void operator<<(int bonus) noexcept {
        static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");
        //std::lock_guard lockGuard(mutex);
        // Make sure that bonus is in range [-D, D]
        int clampedBonus = std::clamp(bonus, -D, +D);
        entry += clampedBonus - entry * std::abs(clampedBonus) / D;

        //assert(std::abs(entry) <= D);
    }

   private:
    T entry;

    //std::mutex mutex;
};

// Stats is a generic N-dimensional array used to store various statistics.
// The first template parameter T is the base type of the array, and the second
// template parameter D limits the range of updates in [-D, +D] when update
// values with the << operator, while the last parameters (Size and Sizes)
// encode the dimensions of the array.
template<typename T, int D, std::size_t Size, std::size_t... Sizes>
struct Stats final: public std::array<Stats<T, D, Sizes...>, Size> {
    using StatsAlias = Stats<T, D, Size, Sizes...>;

    // Recursively fill all dimensions by calling the 1D fill method
    void fill(const T& v) noexcept {
        for (auto& subArray : *this)
            subArray.fill(v);  // Delegate to the fill method of the lower-dimensional array
    }
    /*
    // Overload operator* to apply multiplication to all entries
    void operator*=(double factor) noexcept {
        for (auto& subArray : *this)
            subArray *= factor;
    }
    */
};

template<typename T, int D, std::size_t Size>
struct Stats<T, D, Size> final: public std::array<StatsEntry<T, D>, Size> {
    using Base = std::array<StatsEntry<T, D>, Size>;

    // Fills the 1D array with the specified value using std::fill()
    void fill(const T& v) noexcept {  //
        std::fill(this->begin(), this->end(), v);
    }
    /*
    // Overload operator* to apply multiplication to all entries
    void operator*=(double factor) noexcept {
        for (auto& entry : *this)
            entry *= factor;
    }
    */
};

// clang-format off
// In stats table, D=0 means that the template parameter is not used
constexpr inline int STATS_PARAM_NOT_USED = 0;

constexpr inline int BUTTERFLY_HISTORY_LIMIT = 0x1C0F;
constexpr inline int   CAPTURE_HISTORY_LIMIT = 0x29C4;
constexpr inline int  PIECE_SQ_HISTORY_LIMIT = 0x7500;

constexpr inline std::int16_t LOW_PLY_SIZE = 4;

constexpr inline int   PAWN_HISTORY_LIMIT       = 0x2000;
constexpr inline Key16 PAWN_HISTORY_SIZE        = 0x400;
static_assert((PAWN_HISTORY_SIZE & (PAWN_HISTORY_SIZE - 1)) == 0,
              "PAWN_HISTORY_SIZE has to be a power of 2");
constexpr Key16 pawn_index(Key pawnKey) noexcept { return pawnKey & (PAWN_HISTORY_SIZE - 1); }

enum HistoryType : std::uint8_t {
    HButterfly,     // By color and move's org and dst squares
    HCapture,       // By move's [piece][dst][captured piece type]
    HPawn,          // By pawn structure and a move's [piece][dst]
    HPieceSq,       // By move's [piece][dst]
    HContinuation,  // By combination of pair of moves
    HLowPly,        // By ply and move's org and dst squares,
};

template<HistoryType T>
struct HistoryTypedef final {
    using type = Stats<std::int16_t, BUTTERFLY_HISTORY_LIMIT, COLOR_NB, SQUARE_NB * SQUARE_NB>;
};

// History<HButterfly> records how often quiet moves have been successful or not
// during the current search, and is used for reduction and move ordering decisions.
// It is addressed by color and move's org and dst squares,
// see www.chessprogramming.org/Butterfly_Boards (~11 Elo)
template<>
struct HistoryTypedef<HButterfly> final {
    using type = Stats<std::int16_t, BUTTERFLY_HISTORY_LIMIT, COLOR_NB, SQUARE_NB * SQUARE_NB>;
};

template<>
struct HistoryTypedef<HCapture> final {
    using type = Stats<std::int16_t, CAPTURE_HISTORY_LIMIT, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;
};

template<>
struct HistoryTypedef<HPawn> final {
    using type = Stats<std::int16_t, PAWN_HISTORY_LIMIT, PAWN_HISTORY_SIZE, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryTypedef<HPieceSq> final {
    using type = Stats<std::int16_t, PIECE_SQ_HISTORY_LIMIT, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryTypedef<HContinuation> final {
    using type = std::array2d<Stats<HistoryTypedef<HPieceSq>::type, STATS_PARAM_NOT_USED, PIECE_NB, SQUARE_NB>, 2, 2>;
};

// History<HLowPly> used to improve move ordering near the root.
// It is addressed by ply and move's org and dst squares,
template<>
struct HistoryTypedef<HLowPly> final {
    using type = Stats<std::int16_t, BUTTERFLY_HISTORY_LIMIT, LOW_PLY_SIZE, SQUARE_NB * SQUARE_NB>;
};

template<HistoryType T>
using History = typename HistoryTypedef<T>::type;


constexpr inline int   CORRECTION_HISTORY_LIMIT = 0x400;
constexpr inline Key16 CORRECTION_HISTORY_SIZE  = 0x8000;
static_assert((CORRECTION_HISTORY_SIZE & (CORRECTION_HISTORY_SIZE - 1)) == 0,
              "CORRECTION_HISTORY_SIZE has to be a power of 2");
constexpr Key16 correction_index(Key corrKey) noexcept { return corrKey & (CORRECTION_HISTORY_SIZE - 1); }

// Correction histories record differences between the static evaluation of
// positions and their search score.
// It is used to improve the static evaluation used by some search heuristics.
// see https://www.chessprogramming.org/Static_Evaluation_Correction_History

enum CorrectionHistoryType : std::uint8_t {
    CHPawn,          // By color and pawn structure
    CHNonPawn,       // By color and non-pawn structure
    CHMinor,         // By color and minor piece (Knight, Bishop) structure
    CHMajor,         // By color and major piece (Rook, Queen) structure
    CHPieceSq,       // By move's [piece][dst]
    CHContinuation,  // By combination of pair of moves
};

template<CorrectionHistoryType T>
struct CorrectionHistoryTypedef final {
    using type = Stats<std::int16_t, CORRECTION_HISTORY_LIMIT, COLOR_NB, CORRECTION_HISTORY_SIZE>;
};

template<>
struct CorrectionHistoryTypedef<CHPawn> final {
    using type = Stats<std::int16_t, CORRECTION_HISTORY_LIMIT, COLOR_NB, CORRECTION_HISTORY_SIZE, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHNonPawn> final {
    using type = Stats<std::int16_t, CORRECTION_HISTORY_LIMIT, COLOR_NB, CORRECTION_HISTORY_SIZE, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHPieceSq> final {
    using type = Stats<std::int16_t, CORRECTION_HISTORY_LIMIT, PIECE_NB, SQUARE_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHContinuation> final {
    using type = Stats<CorrectionHistoryTypedef<CHPieceSq>::type, STATS_PARAM_NOT_USED, PIECE_NB, SQUARE_NB>;
};

template<CorrectionHistoryType T>
using CorrectionHistory = typename CorrectionHistoryTypedef<T>::type;

// clang-format on

}  // namespace DON

#endif  // #ifndef HISTORY_H_INCLUDED
