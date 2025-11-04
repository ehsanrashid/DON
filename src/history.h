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
    // Publish a value (release); pair with acquire on readers if needed.
    void operator=(T v) noexcept { value.store(v, std::memory_order_release); }

    // Read (acquire) to observe published writes.
    operator T() const noexcept { return value.load(std::memory_order_acquire); }

    // Overload operator<< to modify the value
    void operator<<(int bonus) noexcept {
        // Make sure that bonus is in range [-D, +D]
        bonus = std::clamp(bonus, -D, +D);

        for (T oldValue = value.load(std::memory_order_relaxed);;)
        {
            T newValue = bonus + (oldValue * (D - std::abs(bonus))) / D;
            assert(static_cast<T>(-D) <= newValue && newValue <= static_cast<T>(+D));
            if (value.compare_exchange_weak(oldValue, newValue, std::memory_order_release,
                                            std::memory_order_relaxed))
                break;
        }
    }

   private:
    std::atomic<T> value{};
};

template<typename T, std::size_t Size, std::size_t... Sizes>
class Entries;

namespace internal {
template<typename T, std::size_t Size, std::size_t... Sizes>
struct [[maybe_unused]] EntiresTypedef;

// Recursive template to define multi-dimensional Entries
template<typename T, std::size_t Size, std::size_t... Sizes>
struct EntiresTypedef final {
    using Type = Entries<T, Sizes...>;
};
// Base case: single-dimensional Entries
template<typename T, std::size_t Size>
struct EntiresTypedef<T, Size> final {
    using Type = T;
};
}  // namespace internal

// Entries is a generic N-dimensional Entry.
// The template parameter T is the base type of the Entries
// The template parameters (Size and Sizes) is the dimensions of the Entries.
template<typename T, std::size_t Size, std::size_t... Sizes>
class Entries final {
   private:
    using ChildType = typename internal::EntiresTypedef<T, Size, Sizes...>::Type;
    using EntryType = std::vector<ChildType>;

   public:
    using value_type             = typename EntryType::value_type;
    using size_type              = typename EntryType::size_type;
    using difference_type        = typename EntryType::difference_type;
    using reference              = typename EntryType::reference;
    using const_reference        = typename EntryType::const_reference;
    using pointer                = typename EntryType::pointer;
    using const_pointer          = typename EntryType::const_pointer;
    using iterator               = typename EntryType::iterator;
    using const_iterator         = typename EntryType::const_iterator;
    using reverse_iterator       = typename EntryType::reverse_iterator;
    using const_reverse_iterator = typename EntryType::const_reverse_iterator;

    Entries() noexcept :
        entries(Size) {}

    constexpr auto begin() const noexcept { return entries.begin(); }
    constexpr auto end() const noexcept { return entries.end(); }
    constexpr auto begin() noexcept { return entries.begin(); }
    constexpr auto end() noexcept { return entries.end(); }

    constexpr auto cbegin() const noexcept { return entries.cbegin(); }
    constexpr auto cend() const noexcept { return entries.cend(); }

    constexpr auto rbegin() const noexcept { return entries.rbegin(); }
    constexpr auto rend() const noexcept { return entries.rend(); }
    constexpr auto rbegin() noexcept { return entries.rbegin(); }
    constexpr auto rend() noexcept { return entries.rend(); }

    constexpr auto crbegin() const noexcept { return entries.crbegin(); }
    constexpr auto crend() const noexcept { return entries.crend(); }

    constexpr auto&       front() noexcept { return entries.front(); }
    constexpr const auto& front() const noexcept { return entries.front(); }
    constexpr auto&       back() noexcept { return entries.back(); }
    constexpr const auto& back() const noexcept { return entries.back(); }

    auto*       data() { return entries.data(); }
    const auto* data() const { return entries.data(); }

    constexpr auto max_size() const noexcept { return entries.max_size(); }

    constexpr auto size() const noexcept { return entries.size(); }
    constexpr auto empty() const noexcept { return entries.empty(); }

    constexpr const auto& at(size_type idx) const noexcept { return entries.at(idx); }
    constexpr auto&       at(size_type idx) noexcept { return entries.at(idx); }

    constexpr auto& operator[](size_type idx) const noexcept { return entries[idx]; }
    constexpr auto& operator[](size_type idx) noexcept { return entries[idx]; }

    constexpr void swap(Entries<T, Size, Sizes...>& _entries) noexcept {
        entries.swap(_entries.entries);
    }

    // Recursively fill all dimensions by calling the sub fill method
    template<typename U>
    void fill(U v) noexcept {
        static_assert(is_strictly_assignable_v<T, U>, "Cannot assign fill value to entry type");

        for (auto& entry : *this)
        {
            if constexpr (sizeof...(Sizes) == 0)
                entry = v;
            else
                entry.fill(v);
        }
    }

    /*
    void print() const noexcept {
        std::cout << Size << ':' << sizeof...(Sizes) << std::endl;
        for (auto& entry : *this)
        {
            if constexpr (sizeof...(Sizes) == 0)
                std::cout << entry << ' ';
            else
                entry.print();
        }
        std::cout << std::endl;
    }
    */

   private:
    EntryType entries;
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
using StatsEntires = Entries<StatsEntry<std::int16_t, D>, Sizes...>;

template<HistoryType T>
struct HistoryTypedef;

template<>
struct HistoryTypedef<HCapture> final {
    using Type = StatsEntires<CAPTURE_HISTORY_LIMIT, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;
};

// It records how often quiet moves have been successful or not during the current search,
// and is used for reduction and move ordering decisions.
// see https://www.chessprogramming.org/Butterfly_Boards
template<>
struct HistoryTypedef<HQuiet> final {
    using Type = StatsEntires<QUIET_HISTORY_LIMIT, COLOR_NB, QUIET_HISTORY_SIZE>;
};

template<>
struct HistoryTypedef<HPawn> final {
    using Type = StatsEntires<PAWN_HISTORY_LIMIT, PAWN_HISTORY_SIZE, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryTypedef<HPieceSq> final {
    using Type = StatsEntires<PIECE_SQ_HISTORY_LIMIT, PIECE_NB, SQUARE_NB>;
};

template<>
struct HistoryTypedef<HContinuation> final {
    using Type = Entries<HistoryTypedef<HPieceSq>::Type, PIECE_NB, SQUARE_NB>;
};

// It is used to improve quiet move ordering near the root.
template<>
struct HistoryTypedef<HLowPlyQuiet> final {
    using Type = StatsEntires<QUIET_HISTORY_LIMIT, LOW_PLY_SIZE, QUIET_HISTORY_SIZE>;
};

template<>
struct HistoryTypedef<HTTMove> final {
    using Type = StatsEntry<std::int16_t, 8192>;
};
}  // namespace internal

// Alias template for convenience
template<HistoryType T>
using History = typename internal::HistoryTypedef<T>::Type;

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
    CHMajor,         // By color and major piece (Rook, Queen) structure
    CHNonPawn,       // By color and non-pawn structure
    CHPieceSq,       // By move's [piece][sq]
    CHContinuation,  // By combination of pair of moves
};

namespace internal {
template<std::size_t... Sizes>
using CorrectionStatsEntires = StatsEntires<CORRECTION_HISTORY_LIMIT, Sizes...>;

template<CorrectionHistoryType T>
struct CorrectionHistoryTypedef;

template<>
struct CorrectionHistoryTypedef<CHPawn> final {
    using Type = CorrectionStatsEntires<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHMinor> final {
    using Type = CorrectionStatsEntires<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHMajor> final {
    using Type = CorrectionStatsEntires<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHNonPawn> final {
    using Type = CorrectionStatsEntires<CORRECTION_HISTORY_SIZE, COLOR_NB, COLOR_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHPieceSq> final {
    using Type = CorrectionStatsEntires<PIECE_NB, SQUARE_NB>;
};

template<>
struct CorrectionHistoryTypedef<CHContinuation> final {
    using Type = Entries<CorrectionHistoryTypedef<CHPieceSq>::Type, PIECE_NB, SQUARE_NB>;
};
}  // namespace internal

// Alias template for convenience
template<CorrectionHistoryType T>
using CorrectionHistory = typename internal::CorrectionHistoryTypedef<T>::Type;

}  // namespace DON

#endif  // #ifndef HISTORY_H_INCLUDED
