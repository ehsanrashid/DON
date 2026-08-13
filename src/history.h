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
#include "types.h"

namespace DON {

// StatsEntry stores a signed integral statistic whose value is bounded by [-D, D].
//
// It allows a statistic to be updated directly using operator<<()
// while optionally supporting atomic storage.
//
// T specifies the underlying signed integral type.
// D specifies the maximum magnitude of the statistic and each update;
//   both the stored value and each update are limited to [-D, D].
// Atomic controls whether the stored value is accessed atomically.
template<typename T, int D, bool Atomic = false>
class StatsEntry final {
    static_assert(std::is_signed_v<T> && std::is_integral_v<T>, "T must be a signed integral");
    static_assert(D > 0, "D must be positive");
    static_assert(D <= std::numeric_limits<T>::max(), "D must fit in T");

   public:
    operator T() const noexcept {
        if constexpr (Atomic)
            return enrty.load(std::memory_order_relaxed);
        else
            return enrty;
    }

    void operator=(const T& e) noexcept {
        if constexpr (Atomic)
            enrty.store(e, std::memory_order_relaxed);
        else
            enrty = e;
    }

    // Update the statistic using bonus, clamped to the range [-D, +D]
    void operator<<(int bonus) noexcept {
        // Clamp the update to the range [-D, +D]
        int clampedBonus = std::clamp(bonus, -D, +D);
        // Apply gravity-based adjustment
        T v   = *this;
        *this = v + clampedBonus - v * T(constexpr_abs(clampedBonus)) / D;

        assert(constexpr_abs(T(*this)) <= D);
    }

    void operator*=(double m) noexcept {
        assert(constexpr_abs(m) <= 1.0);

        *this = constexpr_round(m * double(T(*this)));
    }

   private:
    std::conditional_t<Atomic, std::atomic<T>, T> enrty;
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

template<typename T, int D, usize... Sizes>
using AtomicStats = MultiArray<StatsEntry<T, D, true>, Sizes...>;

template<typename T>
constexpr bool is_power_of_2(const T x) noexcept {
    return x != 0 && (x & (x - 1)) == 0;
}

inline constexpr u16 LOW_PLY_SIZE = 5;

inline constexpr usize QUIET_HISTORY_SIZE = 0x10000;
static_assert(is_power_of_2(QUIET_HISTORY_SIZE), "QUIET_HISTORY_SIZE has to be power of 2");

inline constexpr usize PAWN_HISTORY_BASE_SIZE = 0x4000;
static_assert(is_power_of_2(PAWN_HISTORY_BASE_SIZE), "PAWN_HISTORY_BASE_SIZE has to be power of 2");

inline constexpr usize CORRECTION_HISTORY_BASE_SIZE = 0x10000;
static_assert(is_power_of_2(CORRECTION_HISTORY_BASE_SIZE),
              "CORRECTION_HISTORY_BASE_SIZE has to be power of 2");

inline constexpr int CORRECTION_HISTORY_LIMIT = 1024;


// CaptureHistory is addressed by move's [piece][dstSq][captured piece type]
using CaptureHistory = Stats<i16, 10692, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;

// QuietHistory records how often quiet moves succeed or fail during the current search
// and is used for move ordering and reduction decisions.
// is addressed by color and move's orgSq and dstSq squares.
// see https://www.chessprogramming.org/Butterfly_Boards
using QuietHistory = Stats<i16, 7183, COLOR_NB, QUIET_HISTORY_SIZE>;

// LowPlyHistory is used to improve move ordering near the root.
// is addressed by ply and move's orgSq and dstSq squares.
using LowPlyQuietHistory = Stats<i16, 7183, LOW_PLY_SIZE, QUIET_HISTORY_SIZE>;

// PieceSqHistory is addressed by move's [piece][dstSq]
using PieceSqHistory = Stats<i16, 30000, PIECE_NB, SQUARE_NB>;

// ContinuationHistory is the combined history of given pair of moves,
// usually the current move given the previous move.
using ContinuationHistory = MultiArray<PieceSqHistory, PIECE_NB, SQUARE_NB>;


enum class HType : u8 {

    PAWN,  // By pawn structure and a move's [piece][dstSq]
};

using PawnHistory = AtomicStats<i16, 8192, PIECE_NB, SQUARE_NB>;

namespace Internal {

template<HType T>
struct HistoryDef;

template<>
struct HistoryDef<HType::PAWN> final {
    using Type = DynamicArray<PawnHistory>;
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

int loop();

}  // namespace DON

#endif  // #ifndef HISTORY_H_INCLUDED
