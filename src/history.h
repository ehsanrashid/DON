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
#include <atomic>
#include <cassert>
#include <limits>
#include <type_traits>
#include <unordered_map>

#include "memory.h"
#include "misc.h"
#include "position.h"
#include "types.h"

namespace DON {

// HistoryEntry stores a signed integral statistic whose value is bounded by [-D, D].
//
// It allows a statistic to be updated directly using operator<<()
// while optionally supporting atomic storage.
//
// T specifies the underlying signed integral type.
// D specifies the maximum magnitude of the statistic and each update;
//   both the stored value and each update are limited to [-D, D].
// Atomic controls whether the stored value is accessed atomically.
template<typename T, int D, bool Atomic = false>
class HistoryEntry final {
    static_assert(std::is_signed_v<T> && std::is_integral_v<T>, "T must be a signed integral");
    static_assert(D > 0, "D must be positive");
    static_assert(D <= std::numeric_limits<T>::max(), "D must fit in T");
    static_assert(D * D <= std::numeric_limits<int>::max(), "D can lead to overflows");

   public:
    operator T() const noexcept { return value; }

    void operator=(const T& v) noexcept { value = v; }

    // Update the statistic using bonus, clamped to the range [-D, +D]
    void operator<<(const int bonus) noexcept {
        // Clamp the update to the range [-D, +D]
        int clampedBonus = std::clamp(bonus, -D, +D);
        // Apply gravity-based adjustment
        T v   = *this;
        *this = v + clampedBonus - v * T(constexpr_abs(clampedBonus)) / D;

        assert(constexpr_abs(T(*this)) <= D);
    }

   private:
    std::conditional_t<Atomic, RelaxedAtomic<T>, T> value;
};

template<typename T>
class DynamicArray final {
   public:
    explicit DynamicArray(const usize size) noexcept :
        size_(size) {
        assert(size != 0);

        data_ = make_unique_aligned_large_page<T[]>(size);
    }

    [[nodiscard]] usize size() const noexcept { return size_; }

    T*       data() noexcept { return data_.get(); }
    const T* data() const noexcept { return data_.get(); }

    T& operator[](const usize idx) noexcept {
        assert(idx < size());
        return data_[idx];
    }
    const T& operator[](const usize idx) const noexcept {
        assert(idx < size());
        return data_[idx];
    }

    template<typename U>
    void fill(const usize beg, const usize end, const U& v) noexcept {
        assert(beg <= end && end <= size());

        for (usize idx = beg; idx < end; ++idx)
            data_[idx].fill(v);
    }

   private:
    LargePagePtr<T[]> data_;
    usize             size_;
};

template<typename T, int D, usize... Sizes>
using History = MultiArray<HistoryEntry<T, D>, Sizes...>;

template<typename T, int D, usize... Sizes>
using AtomicHistory = MultiArray<HistoryEntry<T, D, true>, Sizes...>;

inline constexpr usize UINT_16_SIZE = std::numeric_limits<u16>::max() + 1;
static_assert(is_power_of_2(UINT_16_SIZE), "UINT_16_SIZE has to be power of 2");

inline constexpr usize QUIET_HISTORY_SIZE = UINT_16_SIZE;
static_assert(is_power_of_2(QUIET_HISTORY_SIZE), "QUIET_HISTORY_SIZE has to be power of 2");

inline constexpr usize PAWN_HISTORY_BASE_SIZE = 0x4000;
static_assert(is_power_of_2(PAWN_HISTORY_BASE_SIZE), "PAWN_HISTORY_BASE_SIZE has to be power of 2");

inline constexpr usize CORRECTION_HISTORY_BASE_SIZE = UINT_16_SIZE;
static_assert(is_power_of_2(CORRECTION_HISTORY_BASE_SIZE),
              "CORRECTION_HISTORY_BASE_SIZE has to be power of 2");

inline constexpr u16 LOW_PLY_SIZE = 5;

inline constexpr int CORRECTION_HISTORY_LIMIT = 1024;


// CaptureHistory is addressed by move's [piece][dstSq][captured piece type]
using CaptureHistory = History<i16, 10692, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;

// QuietHistory records how often quiet moves succeed or fail during the current search
// and is used for move ordering and reduction decisions.
// is addressed by the color and move's orgSq and dstSq squares.
// see https://www.chessprogramming.org/Butterfly_Boards
using QuietHistory = History<i16, 7183, COLOR_NB, QUIET_HISTORY_SIZE>;

// LowPlyHistory is used to improve move ordering near the root.
// is addressed by the ply and move's orgSq and dstSq squares.
using LowPlyQuietHistory = History<i16, 7183, LOW_PLY_SIZE, QUIET_HISTORY_SIZE>;

// PieceSqHistory is addressed by the move's [piece][dstSq]
using PieceSqHistory = AtomicHistory<i16, 30000, PIECE_NB, SQUARE_NB>;

// ContinuationHistory is the combined history of given pair of moves,
// usually the current move given the previous move.
using ContinuationHistory = MultiArray<PieceSqHistory, PIECE_NB, SQUARE_NB>;

// PawnHistory is addressed by the pawn structure and a move's [piece][dstSq]
using PawnHistory = DynamicArray<AtomicHistory<i16, 8192, PIECE_NB, SQUARE_NB>>;

// TTMoveHistory
using TTMoveHistory = HistoryEntry<i16, 8192>;

// Correction histories record differences between the static evaluation of positions and their search score.
// It is used to improve the static evaluation used by some search heuristics.
// see https://www.chessprogramming.org/Static_Evaluation_Correction_History

// PawnCorrectionHistory is addressed by the color and pawn structure
using PawnCorrectionHistory =
  DynamicArray<AtomicHistory<i16, CORRECTION_HISTORY_LIMIT, COLOR_NB, COLOR_NB>>;
// MinorCorrectionHistory is addressed by the color and minor piece (Knight, Bishop) structure
using MinorCorrectionHistory =
  DynamicArray<AtomicHistory<i16, CORRECTION_HISTORY_LIMIT, COLOR_NB, COLOR_NB>>;
// NonPawnCorrectionHistory is addressed by the color and non-pawn piece structure
using NonPawnCorrectionHistory =
  DynamicArray<AtomicHistory<i16, CORRECTION_HISTORY_LIMIT, COLOR_NB, COLOR_NB>>;
// PieceSqCorrectionHistory is addressed by the move's [piece][dstSq]
using PieceSqCorrectionHistory = History<i16, CORRECTION_HISTORY_LIMIT, PIECE_NB, SQUARE_NB>;
// ContinuationCorrectionHistory is the combined history of given pair of moves,
// usually the current move given the previous move.
using ContinuationCorrectionHistory = MultiArray<PieceSqCorrectionHistory, PIECE_NB, SQUARE_NB>;

class AtomicHistories final {
   public:
    AtomicHistories() noexcept = delete;
    AtomicHistories(const usize threadCount) noexcept :
        correctionHistorySize(threadCount * CORRECTION_HISTORY_BASE_SIZE),
        pawnHistorySize(threadCount * PAWN_HISTORY_BASE_SIZE),
        pawnCorrectionHistory(correction_history_size()),
        minorCorrectionHistory(correction_history_size()),
        nonPawnCorrectionHistory(correction_history_size()),
        pawnHistory(pawn_history_size()) {
#if !defined(NDEBUG)
        assert(is_power_of_2(threadCount));
#endif
    }

    constexpr usize correction_history_size() const noexcept { return correctionHistorySize; }
    constexpr usize correction_history_mask() const noexcept {
        return correction_history_size() - 1;
    }
    constexpr usize correction_index(const Key correctionKey) const noexcept {
        return correctionKey & correction_history_mask();
    }

    auto& pawn_correction_history() noexcept { return pawnCorrectionHistory; }

    template<Color C>
    auto& pawn_correction_entry(const Position& pos) noexcept {
        return pawnCorrectionHistory[correction_index(pos.pawn_key(C))][C];
    }
    template<Color C>
    const auto& pawn_correction_entry(const Position& pos) const noexcept {
        return pawnCorrectionHistory[correction_index(pos.pawn_key(C))][C];
    }

    auto& minor_correction_history() noexcept { return minorCorrectionHistory; }

    template<Color C>
    auto& minor_correction_entry(const Position& pos) noexcept {
        return minorCorrectionHistory[correction_index(pos.minor_key(C))][C];
    }
    template<Color C>
    const auto& minor_correction_entry(const Position& pos) const noexcept {
        return minorCorrectionHistory[correction_index(pos.minor_key(C))][C];
    }

    auto& non_pawn_correction_history() noexcept { return nonPawnCorrectionHistory; }

    template<Color C>
    auto& non_pawn_correction_entry(const Position& pos) noexcept {
        return nonPawnCorrectionHistory[correction_index(pos.non_pawn_key(C))][C];
    }
    template<Color C>
    const auto& non_pawn_correction_entry(const Position& pos) const noexcept {
        return nonPawnCorrectionHistory[correction_index(pos.non_pawn_key(C))][C];
    }

    // --------------------------------

    constexpr usize pawn_history_size() const noexcept { return pawnHistorySize; }
    constexpr usize pawn_history_mask() const noexcept { return pawn_history_size() - 1; }
    constexpr usize pawn_index(const Key pawnKey) const noexcept {
        return pawnKey & pawn_history_mask();
    }

    auto& pawn_history() noexcept { return pawnHistory; }

    auto& pawn_entry(const Position& pos) noexcept {
        return pawnHistory[pawn_index(pos.pawn_key())];
    }
    const auto& pawn_entry(const Position& pos) const noexcept {
        return pawnHistory[pawn_index(pos.pawn_key())];
    }

    // --------------------------------

    auto& continuation_history() noexcept { return continuationHistory; }

   private:
    const usize correctionHistorySize;
    const usize pawnHistorySize;

    PawnCorrectionHistory    pawnCorrectionHistory;
    MinorCorrectionHistory   minorCorrectionHistory;
    NonPawnCorrectionHistory nonPawnCorrectionHistory;
    PawnHistory              pawnHistory;

    Array<ContinuationHistory, 2, 2> continuationHistory;  // [inCheck][capture]
};

using AtomicHistoriesMap = std::unordered_map<usize, AtomicHistories>;

}  // namespace DON

#endif  // HISTORY_H_INCLUDED
