/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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
#include <limits>
#include <type_traits>
#include <unordered_map>

#include "memory.h"
#include "misc.h"
#include "types.h"

namespace DON {

inline constexpr u16 LOW_PLY_QUIET_SIZE = 5;

inline constexpr int CORRECTION_HISTORY_LIMIT = 1024;

inline constexpr usize QUIET_HISTORY_SIZE = 1u << 16;  // Max upto 16-bit
static_assert((QUIET_HISTORY_SIZE & (QUIET_HISTORY_SIZE - 1)) == 0,
              "QUIET_HISTORY_SIZE has to be power of 2");

inline constexpr usize PAWN_HISTORY_BASE_SIZE = 1u << 14;
static_assert((PAWN_HISTORY_BASE_SIZE & (PAWN_HISTORY_BASE_SIZE - 1)) == 0,
              "PAWN_HISTORY_BASE_SIZE has to be power of 2");

inline constexpr usize CORRECTION_HISTORY_BASE_SIZE = std::numeric_limits<u16>::max() + 1;
static_assert((CORRECTION_HISTORY_BASE_SIZE & (CORRECTION_HISTORY_BASE_SIZE - 1)) == 0,
              "CORRECTION_HISTORY_BASE_SIZE has to be power of 2");

// StatsEntry is the container of various numerical statistics.
// Use a class instead of a naked value to directly call history update operator<<() on the entry.
// The first template parameter T is the base type of the StatsEntry and
// the second template parameter D limits the range of updates in [-D, D] when update values with the << operator
template<typename T, int D, bool Atomic = false>
class StatsEntry final {
    static_assert(std::is_arithmetic_v<T>, "T must be arithmetic");
    static_assert(std::is_signed_v<T> && std::is_integral_v<T>,
                  "T must be a signed integral (expects [-D,+D])");
    static_assert(D > 0, "D must be positive");
    static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");

   public:
    operator T() const noexcept {
        if constexpr (Atomic)
            return value.load(std::memory_order_relaxed);
        else
            return value;
    }

    void operator=(const T& v) noexcept {
        if constexpr (Atomic)
            value.store(v, std::memory_order_relaxed);
        else
            value = v;
    }

    // Overload operator<< to modify the value
    void operator<<(int bonus) noexcept {
        // Make sure that bonus is in range [-D, +D]
        int clampedBonus = std::clamp(bonus, -D, +D);
        // Apply gravity-based adjustment
        T v   = *this;
        *this = v + clampedBonus - v * constexpr_abs(clampedBonus) / D;

        assert(constexpr_abs(T(*this)) <= D);
    }

    void operator*=(double m) noexcept {
        assert(constexpr_abs(m) <= 1.0);

        *this = constexpr_round(m * double(T(*this)));
    }

   private:
    std::conditional_t<Atomic, std::atomic<T>, T> value;
};

template<typename T>
class DynamicArray final {
   public:
    explicit DynamicArray(usize size) noexcept :
        size_(size) {
        assert(size != 0);

        data_ = make_unique_aligned_large_page<T[]>(size);
    }

    [[nodiscard]] usize size() const noexcept { return size_; }

    T*       data() noexcept { return data_.get(); }
    const T* data() const noexcept { return data_.get(); }

    T& operator[](usize idx) noexcept {
        assert(idx < size());
        return data()[idx];
    }
    const T& operator[](usize idx) const noexcept {
        assert(idx < size());
        return data()[idx];
    }

    template<typename U>
    void fill(usize beg, usize end, const U& v) noexcept {
        assert(beg <= end && end <= size());

        for (usize idx = beg; idx < end; ++idx)
            data()[idx].fill(v);
    }

   private:
    LargePagePtr<T[]> data_;
    usize             size_;
};

template<typename T, int D, usize... Sizes>
using Stats = MultiArray<StatsEntry<T, D>, Sizes...>;

template<typename T, int D, std::size_t... Sizes>
using AtomicStats = MultiArray<StatsEntry<T, D, true>, Sizes...>;

enum class HType : u8 {
    CAPTURE,       // By move's [piece][dstSq][captured piece type]
    QUIET,         // By color and move's orgSq and dstSq squares
    PAWN,          // By pawn structure and a move's [piece][dstSq]
    LOW_QUIET,     // By ply and move's orgSq and dstSq squares
    PIECE_SQ,      // By move's [piece][dstSq]
    CONTINUATION,  // By combination of pair of moves
};

using PawnHistory = AtomicStats<i16, 8192, PIECE_NB, SQUARE_NB>;

namespace Internal {

template<HType T>
struct HistoryDef;

template<>
struct HistoryDef<HType::CAPTURE> final {
    using Type = Stats<i16, 10692, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;
};

// It records how often quiet moves have been successful or not during the current search,
// It is used for reduction and move ordering decisions.
// see https://www.chessprogramming.org/Butterfly_Boards
template<>
struct HistoryDef<HType::QUIET> final {
    using Type = Stats<i16, 7183, COLOR_NB, QUIET_HISTORY_SIZE>;
};

template<>
struct HistoryDef<HType::PAWN> final {
    using Type = DynamicArray<PawnHistory>;
};

// It is used to improve quiet move ordering near the root.
template<>
struct HistoryDef<HType::LOW_QUIET> final {
    using Type = Stats<i16, 7183, LOW_PLY_QUIET_SIZE, QUIET_HISTORY_SIZE>;
};

template<>
struct HistoryDef<HType::PIECE_SQ> final {
    using Type = Stats<i16, 30000, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryDef<HType::CONTINUATION> final {
    using Type = MultiArray<HistoryDef<HType::PIECE_SQ>::Type, PIECE_NB, SQUARE_NB>;
};

}  // namespace Internal

// Alias template for convenience
template<HType T>
using History = typename Internal::HistoryDef<T>::Type;

// Correction histories record differences between the static evaluation of positions and their search score.
// It is used to improve the static evaluation used by some search heuristics.
// see https://www.chessprogramming.org/Static_Evaluation_Correction_History

enum class CHType : u8 {
    PAWN,          // By color and pawn structure
    MINOR,         // By color and minor piece (Knight, Bishop) structure
    NON_PAWN,      // By color and non-pawn piece structure
    PIECE_SQ,      // By move's [piece][dstSq]
    CONTINUATION,  // By combination of pair of moves
};

namespace Internal {

template<CHType T>
struct CorrectionHistoryDef;

template<>
struct CorrectionHistoryDef<CHType::PAWN> final {
    using Type = DynamicArray<Stats<i16, CORRECTION_HISTORY_LIMIT, COLOR_NB, COLOR_NB>>;
};

template<>
struct CorrectionHistoryDef<CHType::MINOR> final {
    using Type = DynamicArray<Stats<i16, CORRECTION_HISTORY_LIMIT, COLOR_NB, COLOR_NB>>;
};

template<>
struct CorrectionHistoryDef<CHType::NON_PAWN> final {
    using Type = DynamicArray<Stats<i16, CORRECTION_HISTORY_LIMIT, COLOR_NB, COLOR_NB>>;
};

template<>
struct CorrectionHistoryDef<CHType::PIECE_SQ> final {
    using Type = Stats<i16, CORRECTION_HISTORY_LIMIT, PIECE_NB, SQUARE_NB>;
};

template<>
struct CorrectionHistoryDef<CHType::CONTINUATION> final {
    using Type = MultiArray<CorrectionHistoryDef<CHType::PIECE_SQ>::Type, PIECE_NB, SQUARE_NB>;
};

}  // namespace Internal

// Alias template for convenience
template<CHType T>
using CorrectionHistory = typename Internal::CorrectionHistoryDef<T>::Type;

using TTMoveHistory = StatsEntry<i16, 8192>;


class Histories final {
   public:
    Histories() noexcept = delete;
    Histories(usize count) noexcept :
        historySize(count * PAWN_HISTORY_BASE_SIZE),
        correctionHistorySize(count * CORRECTION_HISTORY_BASE_SIZE),
        pawnHistory(history_size()),
        pawnCorrectionHistory(correction_history_size()),
        minorCorrectionHistory(correction_history_size()),
        nonPawnCorrectionHistory(correction_history_size()) {
#if !defined(NDEBUG)
        assert(count != 0 && (count & (count - 1)) == 0);
#endif
    }

    constexpr usize history_size() const noexcept {  //
        return historySize;
    }
    constexpr usize history_mask() const noexcept {  //
        return history_size() - 1;
    }

    constexpr usize pawn_index(Key pawnKey) const noexcept {  //
        return pawnKey & history_mask();
    }

    constexpr usize correction_history_size() const noexcept {  //
        return correctionHistorySize;
    }
    constexpr usize correction_history_mask() const noexcept {  //
        return correction_history_size() - 1;
    }

    constexpr usize correction_index(Key correctionKey) const noexcept {  //
        return correctionKey & correction_history_mask();
    }


    auto& pawn() noexcept { return pawnHistory; }

    auto&       pawn(Key pawnKey) noexcept { return pawnHistory[pawn_index(pawnKey)]; }
    const auto& pawn(Key pawnKey) const noexcept { return pawnHistory[pawn_index(pawnKey)]; }

    auto& pawn_correction() noexcept { return pawnCorrectionHistory; }

    template<Color C>
    auto& pawn_correction(Key pawnKey) noexcept {
        return pawnCorrectionHistory[correction_index(pawnKey)][C];
    }
    template<Color C>
    const auto& pawn_correction(Key pawnKey) const noexcept {
        return pawnCorrectionHistory[correction_index(pawnKey)][C];
    }

    auto& minor_correction() noexcept { return minorCorrectionHistory; }

    template<Color C>
    auto& minor_correction(Key minorKey) noexcept {
        return minorCorrectionHistory[correction_index(minorKey)][C];
    }
    template<Color C>
    const auto& minor_correction(Key minorKey) const noexcept {
        return minorCorrectionHistory[correction_index(minorKey)][C];
    }

    auto& non_pawn_correction() noexcept { return nonPawnCorrectionHistory; }

    template<Color C>
    auto& non_pawn_correction(Key nonPawnKey) noexcept {
        return nonPawnCorrectionHistory[correction_index(nonPawnKey)][C];
    }
    template<Color C>
    const auto& non_pawn_correction(Key nonPawnKey) const noexcept {
        return nonPawnCorrectionHistory[correction_index(nonPawnKey)][C];
    }

   private:
    const usize historySize;
    const usize correctionHistorySize;

    History<HType::PAWN>                pawnHistory;
    CorrectionHistory<CHType::PAWN>     pawnCorrectionHistory;
    CorrectionHistory<CHType::MINOR>    minorCorrectionHistory;
    CorrectionHistory<CHType::NON_PAWN> nonPawnCorrectionHistory;
};

using HistoriesMap = std::unordered_map<usize, Histories>;

void loop();

}  // namespace DON

#endif  // #ifndef HISTORY_H_INCLUDED
