#pragma once

#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

#include "Type.h"

/// Hash table
template<typename T, u32 Size>
class HashTable {
private:
    // Allocate on the heap
    std::vector<T> table;

public:

    HashTable()
        : table(Size) {}

    void clear() {
        table.assign(Size, T());
    }

    T* operator[](Key key) {
        return &table[u32(key) & (Size - 1)];
    }
};


/// Multi-dimensional Array
template<typename T, size_t Size, size_t... Sizes>
class Array_ {
    static_assert (Size != 0, "Size incorrect");
private:
    using NestedArray_ = typename Array_<T, Sizes...>::type;

public:
    using type = std::array<NestedArray_, Size>;
};

template<typename T, size_t Size>
class Array_<T, Size> {
    static_assert (Size != 0, "Size incorrect");
public:
    using type = std::array<T, Size>;
};

template<typename T, size_t... Sizes>
using Array = typename Array_<T, Sizes...>::type;

/// Table
template<typename T, size_t Size, size_t... Sizes>
class Table
    : public std::array<Table<T, Sizes...>, Size>
{
    static_assert (Size != 0, "Size incorrect");
private:
    using NestedTable = Table<T, Size, Sizes...>;

public:

    void fill(T const &value) {
        assert(std::is_standard_layout<NestedTable>::value);

        auto *p = reinterpret_cast<T*>(this);
        std::fill(p, p + sizeof (*this) / sizeof (T), value);
    }

};
template<typename T, size_t Size>
class Table<T, Size>
    : public std::array<T, Size>
{
    static_assert (Size != 0, "Size incorrect");
};


/// Stats stores the value. It is usually a number.
/// We use a class instead of naked value to directly call
/// history update operator<<() on the entry so to use stats
/// tables at caller sites as simple multi-dim arrays.
template<typename T, i32 D>
class Stats {
private:
    T entry;

public:

    void operator=(T const &e) {
        entry = e;
    }

    T* operator&() {
        return &entry;
    }
    T* operator->() {
        return &entry;
    }

    operator T const&() const {
        return entry;
    }

    void operator<<(i32 bonus) {
        static_assert (D <= std::numeric_limits<T>::max(), "D overflows T");
        assert(std::abs(bonus) <= D); // Ensure range is [-D, +D]

        entry += T(bonus - entry * std::abs(bonus) / D);

        assert(std::abs(entry) <= D);
    }
};

/// Stats is a generic N-dimensional array used to store various statistics.
/// The first template T parameter is the base type of the array,
/// the D parameter limits the range of updates (range is [-D, +D]), and
/// the last parameters (Size and Sizes) encode the dimensions of the array.
template<typename T, i32 D, size_t Size, size_t... Sizes>
class StatsTable
    : public std::array<StatsTable<T, D, Sizes...>, Size>
{
    static_assert (Size != 0, "Size incorrect");
private:
    using NestedStatsTable = StatsTable<T, D, Size, Sizes...>;

public:

    void fill(T const &value) {
        // For standard-layout 'this' points to first struct member
        assert(std::is_standard_layout<NestedStatsTable>::value);

        using Entry = Stats<T, D>;
        auto *p = reinterpret_cast<Entry*>(this);
        std::fill(p, p + sizeof (*this) / sizeof (Entry), value);
    }
};
template<typename T, i32 D, size_t Size>
class StatsTable<T, D, Size>
    : public std::array<Stats<T, D>, Size>
{
    static_assert (Size != 0, "Size incorrect");
};


/// ColorIndexStatsTable stores moves history according to color.
/// Used for reduction and move ordering decisions.
/// indexed by [color][moveIndex]
using ColorIndexStatsTable      = StatsTable<i16, 10692, COLORS, SQUARES*SQUARES>;

/// PlyIndexStatsTable stores moves history according to ply from 0 to MAX_LOWPLY-1
constexpr i16 MAX_LOWPLY = 4;
using PlyIndexStatsTable        = StatsTable<i16, 10692, MAX_LOWPLY, SQUARES*SQUARES>;

/// PieceSquareTypeStatsTable stores move history according to piece.
/// indexed by [piece][square][captureType]
using PieceSquareTypeStatsTable = StatsTable<i16, 10692, PIECES, SQUARES, PIECE_TYPES>;

/// PieceSquareStatsTable store moves history according to piece.
/// indexed by [piece][square]
using PieceSquareStatsTable     = StatsTable<i16, 29952, PIECES, SQUARES>;
/// ContinuationStatsTable is the combined history of a given pair of moves, usually the current one given a previous one.
/// The nested history table is based on PieceSquareStatsTable, indexed by [piece][square]
using ContinuationStatsTable    = Table<PieceSquareStatsTable, PIECES, SQUARES>;

/// PieceSquareMoveTable stores moves, indexed by [piece][square]
using PieceSquareMoveTable      = Table<Move, PIECES, SQUARES>;
