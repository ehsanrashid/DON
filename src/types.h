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

#ifndef TYPES_H_INCLUDED
#define TYPES_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>  // std::hash<>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "misc.h"

// When compiling with provided Makefile (e.g. for Linux and OSX), configuration
// is done automatically. To get started type 'make help'.
//
// When Makefile is not used (e.g. with Microsoft Visual Studio) some switches
// need to be set manually:
//
// -DNDEBUG       | Disable debugging mode. Always use this for release.
//
// -DUSE_PREFETCH | Add runtime support for use of prefetch asm-instruction.
//                | Need to remove this to run on some very old machines.
//
// -DUSE_POPCNT   | Add runtime support for use of popcnt asm-instruction.
//                | Works only in 64-bit mode and requires hardware with popcnt support.
//
// -DUSE_BMI2     | Add runtime support for use of pext/pdep asm-instructions.
//                | Works only in 64-bit mode and requires hardware with pext/pdep support.

// Predefined macros hell:
//
// __GNUC__                Compiler is GCC, Clang or ICX
// __clang__               Compiler is Clang or ICX
// __INTEL_LLVM_COMPILER   Compiler is ICX
// _MSC_VER                Compiler is MSVC
// _WIN32                  Building on Windows (any)
// _WIN64                  Building on Windows 64 bit

#if defined(_MSC_VER)
    // Disable some silly and noisy warnings from MSVC compiler
    #pragma warning(disable: 4127)  // Conditional expression is constant
    #pragma warning(disable: 4146)  // Unary minus operator applied to unsigned type
    #pragma warning(disable: 4800)  // Forcing value to bool 'true' or 'false'

    #if defined(_WIN64)  // No Makefile used
        #define IS_64BIT
    #endif
#endif

// Enforce minimum Clang version
#if defined(__clang__)
    #if __clang_major__ < 10
        #error "DON requires Clang 10.0 or later for correct compilation"
    #endif
// Enforce minimum GCC version
#elif defined(__GNUC__)
    #if (__GNUC__ < 9) || (__GNUC__ == 9 && __GNUC_MINOR__ < 3)
        #error "DON requires GCC 9.3 or later for correct compilation"
    #endif
#endif

#define ASSERT_ALIGNED(ptr, alignment) assert(reinterpret_cast<uptr>(ptr) % alignment == 0)

namespace DON {

using Bitboard = u64;
static_assert(sizeof(Bitboard) == 8, "Bitboard size must be 8 bytes");

using Key = u64;
static_assert(sizeof(Key) == 8, "Key size must be 8 bytes");

inline constexpr u16 MOVE_MAX = 256;
inline constexpr u16 PLY_MAX  = 254;

// Maximum signed 16-bit value: 2**15 - 1
inline constexpr u16 RULE50_COUNT_MAX = std::numeric_limits<i16>::max();

// Size of cache line (in bytes)
inline constexpr usize CACHE_LINE_SIZE = 64;

inline constexpr std::string_view            PIECE_UNI{".PNBRQK..pnbrqk."};
inline constexpr Array<std::string_view, 16> PIECE_UTF8{
  ".", "♙", "♘", "♗", "♖", "♕", "♔", ".",  //
  ".", "♟", "♞", "♝", "♜", "♛", "♚", "."   //
};

inline constexpr std::string_view START_FEN{
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"};

// clang-format off
enum File : u8 {
    FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H
};

inline constexpr usize FILE_NB = 8;

enum Rank : u8 {
    RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8
};

inline constexpr usize RANK_NB = 8;

enum Square : u8 {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE,
    SQUARE_ZERO = 0
};

inline constexpr usize SQUARE_NB = 64;

enum PieceType : u8 {
    NO_PIECE_TYPE = 0,
    PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING, ALL
};
// clang-format on

inline constexpr usize PIECE_TYPE_NB  = 8;
inline constexpr usize PIECE_TYPE_CNT = 6;

[[nodiscard]] constexpr bool is_ok(const PieceType pt) noexcept {
    return (PAWN <= pt && pt <= KING);
}

[[nodiscard]] constexpr bool is_major(const PieceType pt) noexcept { return (pt >= ROOK); }

constexpr Array<PieceType, PIECE_TYPE_CNT> PIECE_TYPES{
  PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING  //
};
constexpr Array<PieceType, PIECE_TYPE_CNT - 1> EX_KING_PIECE_TYPES{
  PAWN, KNIGHT, BISHOP, ROOK, QUEEN  //
};
constexpr Array<PieceType, PIECE_TYPE_CNT - 2> NON_PAWN_PIECE_TYPES{
  KNIGHT, BISHOP, ROOK, QUEEN  //
};

// clang-format off
#define ENABLE_INCR_OPERATORS_ON(T) \
    static_assert(std::is_enum_v<T>, "ENABLE_INCR_OPERATORS_ON requires an enum"); \
    static_assert(std::is_convertible_v<T, int>, "ENABLE_INCR_OPERATORS_ON requires an *unscoped* enum (plain enum)"); \
    constexpr T& operator++(T& v) noexcept { return v = T(int(v) + 1); } \
    constexpr T& operator--(T& v) noexcept { return v = T(int(v) - 1); } \
    constexpr T  operator++(T& v, const int) noexcept { T u = v; ++v; return u; } \
    constexpr T  operator--(T& v, const int) noexcept { T u = v; --v; return u; }
// clang-format on

ENABLE_INCR_OPERATORS_ON(File)
ENABLE_INCR_OPERATORS_ON(Rank)
ENABLE_INCR_OPERATORS_ON(Square)
ENABLE_INCR_OPERATORS_ON(PieceType)

#undef ENABLE_INCR_OPERATORS_ON

enum class Direction : i8 {
    EAST  = 1,
    NORTH = 8,
    WEST  = -EAST,
    SOUTH = -NORTH,

    SOUTH_WEST = SOUTH + WEST,
    SOUTH_EAST = SOUTH + EAST,
    NORTH_WEST = NORTH + WEST,
    NORTH_EAST = NORTH + EAST,

    SOUTH_2 = SOUTH + SOUTH,
    WEST_2  = WEST + WEST,
    EAST_2  = EAST + EAST,
    NORTH_2 = NORTH + NORTH,
};

constexpr auto operator+(const Direction d) noexcept { return i8(d); }

constexpr Direction operator+(const Direction d1, const Direction d2) noexcept {
    return Direction(+d1 + +d2);
}
constexpr Direction operator-(const Direction d1, const Direction d2) noexcept {
    return Direction(+d1 - +d2);
}

constexpr Direction operator*(const Direction d, const int i) noexcept { return Direction(i * +d); }
constexpr Direction operator*(const int i, const Direction d) noexcept { return d * i; }

// Additional operators for File
constexpr File  operator+(const File f, const int i) noexcept { return File(u8(f) + i); }
constexpr File  operator-(const File f, const int i) noexcept { return File(u8(f) - i); }
constexpr File& operator+=(File& f, const int i) noexcept { return f = f + i; }
constexpr File& operator-=(File& f, const int i) noexcept { return f = f - i; }
constexpr i8    operator-(const File f1, const File f2) noexcept { return u8(f1) - u8(f2); }
// Additional operators for Rank
constexpr Rank  operator+(const Rank r, const int i) noexcept { return Rank(u8(r) + i); }
constexpr Rank  operator-(const Rank r, const int i) noexcept { return Rank(u8(r) - i); }
constexpr Rank& operator+=(Rank& r, const int i) noexcept { return r = r + i; }
constexpr Rank& operator-=(Rank& r, const int i) noexcept { return r = r - i; }
constexpr i8    operator-(const Rank r1, const Rank r2) noexcept { return u8(r1) - u8(r2); }
// Additional operators for Square to add a Direction
constexpr Square operator+(const Square s, const int i) noexcept { return Square(u8(s) + i); }
constexpr Square operator-(const Square s, const int i) noexcept { return Square(u8(s) - i); }
constexpr Square operator+(const Square s, const Direction d) noexcept {
    return Square(s + int(d));
}
constexpr Square operator-(const Square s, const Direction d) noexcept {
    return Square(s - int(d));
}
constexpr Square& operator+=(Square& s, const Direction d) noexcept { return s = s + d; }
constexpr Square& operator-=(Square& s, const Direction d) noexcept { return s = s - d; }

[[nodiscard]] constexpr bool is_ok(const File f) noexcept { return (f <= FILE_H); }

[[nodiscard]] constexpr bool is_ok(const Rank r) noexcept { return (r <= RANK_8); }

[[nodiscard]] constexpr Square make_square(const File f, const Rank r) noexcept {
    assert(is_ok(f) && is_ok(r));

    return Square((u8(r) << 3) | u8(f));
}

[[nodiscard]] constexpr bool is_ok(const Square s) noexcept { return (s <= SQ_H8); }

[[nodiscard]] constexpr File file_of(const Square s) noexcept { return File((u8(s) >> 0) & 7); }
[[nodiscard]] constexpr Rank rank_of(const Square s) noexcept { return Rank((u8(s) >> 3) & 7); }

[[nodiscard]] constexpr Square reverse_sq(const Square s) noexcept { return Square(SQ_H8 - s); }

[[nodiscard]] constexpr bool is_light(const Square s) noexcept {
    return ((u8(s) ^ u8(rank_of(s))) & 1) != 0;
}
[[nodiscard]] constexpr bool color_opposite(const Square s1, const Square s2) noexcept {
    return is_light(s1) != is_light(s2);
}

// Swap A1 <-> H1, B1 <-> G1, ...
[[nodiscard]] constexpr Square flip_file(const Square s) noexcept {
    return Square(u8(s) ^ u8(SQ_H1));
}
// Swap A1 <-> H8, B1 <-> G8, ...
[[nodiscard]] constexpr Square flip_rank(const Square s) noexcept {
    return Square(u8(s) ^ u8(SQ_A8));
}

enum Color : u8 {
    WHITE,
    BLACK,
    NONE
};

inline constexpr usize COLOR_NB = 2;

[[nodiscard]] constexpr bool is_ok(const Color c) noexcept { return (c <= BLACK); }

// Toggle color
[[nodiscard]] constexpr Color operator~(const Color c) noexcept { return Color(c ^ BLACK); }

[[nodiscard]] constexpr std::string_view to_string(const Color c) noexcept {
    switch (c)
    {
    case WHITE :
        return "White";
    case BLACK :
        return "Black";
    case NONE :
        return "None";
    }
    return "Unknown";
}

// clang-format off
enum class Piece : u8 {
    NO_PIECE,
    W_PAWN = 0 + PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 8 + PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING
};
// clang-format on
static_assert(sizeof(Piece) == 1, "Piece size must be 1 byte");

inline constexpr usize PIECE_NB = 16;

constexpr u8 operator+(const Piece pc) noexcept { return u8(pc); }

constexpr Piece operator^(const Piece pc1, const Piece pc2) noexcept { return Piece(+pc1 ^ +pc2); }

[[nodiscard]] constexpr bool is_ok(const Piece pc) noexcept {
    return (Piece::W_PAWN <= pc && pc <= Piece::W_KING)
        || (Piece::B_PAWN <= pc && pc <= Piece::B_KING);
}

[[nodiscard]] constexpr Piece make_piece(const Color c, const PieceType pt) noexcept {
    assert(is_ok(c) && is_ok(pt));

    return Piece((u8(c) << 3) | u8(pt));
}

constexpr PieceType type_of(const Piece pc) noexcept { return PieceType((+pc >> 0) & 7); }

constexpr Color color_of(const Piece pc) noexcept { return Color((+pc >> 3) & 1); }

// Swap color of piece B_KNIGHT <-> W_KNIGHT
[[nodiscard]] constexpr Piece flip_color(const Piece pc) noexcept {
    return Piece(+pc ^ PIECE_TYPE_NB);
}

[[nodiscard]] constexpr Piece relative_piece(const Color c, const Piece pc) noexcept {
    return Piece(+pc ^ (c * PIECE_TYPE_NB));
}

constexpr bool slider_can_threaten(const Piece pc, const Piece sliderPc) noexcept {
    return type_of(pc) != QUEEN || type_of(sliderPc) == QUEEN;
}

using PieceMap = Array<Piece, SQUARE_NB>;

[[nodiscard]] constexpr File fold_to_edge(const File f) noexcept {
    return std::min(f, FILE_H - int(f));
}
[[nodiscard]] constexpr Rank fold_to_edge(const Rank r) noexcept {
    return std::min(r, RANK_8 - int(r));
}

[[nodiscard]] constexpr Square relative_sq(const Color c, const Square s) noexcept {
    return Square(u8(s) ^ (c * u8(SQ_A8)));
}

[[nodiscard]] constexpr Rank relative_rank(const Color c, const Rank r) noexcept {
    return Rank(u8(r) ^ (c * u8(RANK_8)));
}

[[nodiscard]] constexpr Rank relative_rank(const Color c, const Square s) noexcept {
    return relative_rank(c, rank_of(s));
}

[[nodiscard]] constexpr Square king_castle_sq(const Square kingOrgSq,
                                              const Square kingDstSq) noexcept {
    return make_square(kingOrgSq < kingDstSq ? FILE_G : FILE_C, rank_of(kingOrgSq));
}
[[nodiscard]] constexpr Square rook_castle_sq(const Square kingOrgSq,
                                              const Square kingDstSq) noexcept {
    return make_square(kingOrgSq < kingDstSq ? FILE_F : FILE_D, rank_of(kingOrgSq));
}

[[nodiscard]] constexpr Direction pawn_spush(const Color c) noexcept {
    assert(is_ok(c));
    return c == WHITE ? Direction::NORTH : Direction::SOUTH;
}
[[nodiscard]] constexpr Direction pawn_dpush(const Color c) noexcept {
    assert(is_ok(c));
    return c == WHITE ? Direction::NORTH_2 : Direction::SOUTH_2;
}

[[nodiscard]] constexpr char to_char(const PieceType pt) noexcept {  //
    return is_ok(pt) ? PIECE_UNI[pt] : ' ';
}

[[nodiscard]] constexpr char to_char(const Piece pc) noexcept {  //
    return is_ok(pc) ? PIECE_UNI[+pc] : ' ';
}

[[nodiscard]] constexpr Piece to_piece(const char pc) noexcept {
    usize pos = PIECE_UNI.find(pc);

    return pos != std::string_view::npos ? Piece(pos) : Piece::NO_PIECE;
}

[[nodiscard]] constexpr std::string_view to_figure(const Piece pc) noexcept {  //
    return is_ok(pc) ? PIECE_UTF8[+pc] : " ";
}

template<bool Upper = false>
[[nodiscard]] constexpr char to_char(const File f) noexcept {
    return (Upper ? 'A' : 'a') + f;
}

[[nodiscard]] constexpr char to_char(const Rank r) noexcept { return '1' + r; }

[[nodiscard]] constexpr File to_file(const char f) noexcept { return File(f - 'a'); }

[[nodiscard]] constexpr Rank to_rank(const char r) noexcept { return Rank(r - '1'); }

// Flip file 'A'-'H' or 'a'-'h'; otherwise unchanged
[[nodiscard]] constexpr char flip_file(const char f) noexcept {
    return ('A' <= f && f <= 'H') ? 'A' + ('H' - f) : ('a' <= f && f <= 'h') ? 'a' + ('h' - f) : f;
}
// Flip rank '1'-'8'; otherwise unchanged
[[nodiscard]] constexpr char flip_rank(const char r) noexcept {
    return ('1' <= r && r <= '8') ? '1' + ('8' - r) : r;
}

// Build a compile-time table: "a1", "b1", ..., "h8"
alignas(CACHE_LINE_SIZE) inline constexpr auto SQUARES = []() constexpr noexcept {
    Array<char, SQUARE_NB, 3> squares{};

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        squares[s] = {to_char(file_of(s)), to_char(rank_of(s)), '\0'};

    return squares;
}();

[[nodiscard]] constexpr std::string_view to_square(const Square s) noexcept {
    assert(is_ok(s));

    return {SQUARES[s].data(), SQUARES[s].size() - 1};
}

static_assert(to_square(SQ_A1) == "a1" && to_square(SQ_H8) == "h8",
              "to_square(): broken, expected 'a1' & 'h8'");

// Value is used as an alias for i16.
// This is done to differentiate between a search value and any other integer value.
// The values used in search are always supposed to be in the range (-VALUE_NONE, +VALUE_NONE]
// and should not exceed this range.
using Value = i16;

inline constexpr Value VALUE_ZERO = 0;
inline constexpr Value VALUE_DRAW = VALUE_ZERO;

inline constexpr Value VALUE_NONE     = std::numeric_limits<i16>::max();
inline constexpr Value VALUE_INFINITE = VALUE_NONE - 1;

inline constexpr Value VALUE_MATE                 = VALUE_INFINITE - 1;
inline constexpr Value VALUE_MATE_WIN_IN_1        = VALUE_MATE - 1;
inline constexpr Value VALUE_MATE_WIN_IN_PLY_MAX  = VALUE_MATE - PLY_MAX;
inline constexpr Value VALUE_MATE_LOSS_IN_PLY_MAX = -VALUE_MATE_WIN_IN_PLY_MAX;

inline constexpr Value VALUE_TB                 = VALUE_MATE_WIN_IN_PLY_MAX - 1;
inline constexpr Value VALUE_TB_WIN_IN_PLY_MAX  = VALUE_TB - PLY_MAX;
inline constexpr Value VALUE_TB_LOSS_IN_PLY_MAX = -VALUE_TB_WIN_IN_PLY_MAX;

// Piece values in centipawns
inline constexpr Value VALUE_PAWN   = 208;
inline constexpr Value VALUE_KNIGHT = 781;
inline constexpr Value VALUE_BISHOP = 825;
inline constexpr Value VALUE_ROOK   = 1276;
inline constexpr Value VALUE_QUEEN  = 2538;

using SqrValue = i32;

inline constexpr SqrValue SQR_VALUE_INFINITE = VALUE_INFINITE * VALUE_INFINITE;

inline constexpr int DELTA_MAX = 2 * VALUE_INFINITE;

// Returns the value of the given piece type
constexpr Value piece_value(const PieceType pt) noexcept {
    constexpr Array<Value, PIECE_TYPE_CNT + 1> PieceValues{
      VALUE_ZERO, VALUE_PAWN, VALUE_KNIGHT, VALUE_BISHOP, VALUE_ROOK, VALUE_QUEEN, VALUE_ZERO};

    return PieceValues[pt];
}

constexpr bool is_valid(const Value value) noexcept { return value != VALUE_NONE; }

constexpr bool is_ok(const Value value) noexcept {
    assert(is_valid(value));

    return -VALUE_INFINITE < value && value < +VALUE_INFINITE;
}

// Clamp value to the range (VALUE_TB_LOSS_IN_PLY_MAX, VALUE_TB_WIN_IN_PLY_MAX)
constexpr Value in_range(const i32 value) noexcept {
    return std::clamp(value, VALUE_TB_LOSS_IN_PLY_MAX + 1, VALUE_TB_WIN_IN_PLY_MAX - 1);
}

constexpr bool is_win(const Value value) noexcept {
    assert(is_valid(value));

    return value >= VALUE_TB_WIN_IN_PLY_MAX;
}

constexpr bool is_loss(const Value value) noexcept {
    assert(is_valid(value));

    return value <= VALUE_TB_LOSS_IN_PLY_MAX;
}

// Check if the value represents a decisive outcome (win or loss)
constexpr bool is_decisive(const Value value) noexcept { return is_win(value) || is_loss(value); }

constexpr bool is_mate_win(const Value value) noexcept {
    assert(is_valid(value));

    return value >= VALUE_MATE_WIN_IN_PLY_MAX;
}

constexpr bool is_mate_loss(const Value value) noexcept {
    assert(is_valid(value));

    return value <= VALUE_MATE_LOSS_IN_PLY_MAX;
}

// Check if the value represents a mate score (win or loss)
constexpr bool is_mate(const Value value) noexcept {
    return is_mate_win(value) || is_mate_loss(value);
}

constexpr Value mates_in(const i16 ply) noexcept { return +VALUE_MATE - ply; }

constexpr Value mated_in(const i16 ply) noexcept { return -VALUE_MATE + ply; }

// Depth is used as an alias for i16
using Depth = i16;

inline constexpr Depth DEPTH_MAX  = PLY_MAX - 1;
inline constexpr Depth DEPTH_ZERO = 0;
inline constexpr Depth DEPTH_NONE = -1;
// Offset to convert depth to a non-negative depth.
// It is used only for TT entry occupancy check, should thus be lower than DEPTH_NONE.
inline constexpr Depth DEPTH_OFFSET = DEPTH_NONE - 1;
static_assert(DEPTH_OFFSET == DEPTH_MAX - 0xFF, "DEPTH_OFFSET == DEPTH_MAX - 0xFF");

enum class CastlingSide : u8 {
    KING,
    QUEEN,
    ANY
};

inline constexpr usize CASTLING_SIDE_NB = 2;

[[nodiscard]] constexpr bool is_ok(const CastlingSide cs) noexcept {
    return (cs <= CastlingSide::QUEEN);
}

[[nodiscard]] constexpr std::string_view to_string(const CastlingSide cs) noexcept {
    switch (cs)
    {
    case CastlingSide::KING :
        return "O-O";
    case CastlingSide::QUEEN :
        return "O-O-O";
    case CastlingSide::ANY :
        return "O-O / O-O-O";
    }
    return {};
}

enum class CastlingRights : u8 {
    NO_CASTLING = 0,

    WHITE_OO  = 1 << 0,
    WHITE_OOO = 1 << 1,

    WHITE_CASTLING = WHITE_OO | WHITE_OOO,

    BLACK_OO  = WHITE_OO << 2,
    BLACK_OOO = WHITE_OOO << 2,

    BLACK_CASTLING = BLACK_OO | BLACK_OOO,

    ANY_CASTLING = WHITE_CASTLING | BLACK_CASTLING
};

inline constexpr usize CASTLING_RIGHTS_NB = 16;

// Bound type for alpha-beta search
enum class Bound : u8 {
    NONE = 0,
    UPPER,
    LOWER,
    EXACT = UPPER | LOWER
};

inline constexpr usize BOUND_NB = 4;

constexpr bool is_ok(const Bound bound) noexcept {
    return (Bound::UPPER <= bound && bound <= Bound::EXACT);
}

[[nodiscard]] constexpr std::string_view to_string(const Bound bound) noexcept {
    switch (bound)
    {
    case Bound::UPPER :
        return " upperbound";
    case Bound::LOWER :
        return " lowerbound";
    case Bound::EXACT :
        return " exactbound";
    case Bound::NONE :;
    }
    return {};
}

// clang-format off
#define ENABLE_BIT_OPERATORS_ON(T) \
    static_assert(std::is_enum_v<T>, "ENABLE_BIT_OPERATORS_ON requires an enum"); \
    constexpr auto operator+(const T t) noexcept { using U = std::underlying_type_t<T>; return U(t); }                 \
    constexpr T operator~(const T t) noexcept { using U = std::underlying_type_t<T>; return T(~U(t)); }                \
    constexpr T operator&(const T t1, const T t2) noexcept { using U = std::underlying_type_t<T>; return T(U(t1) & U(t2)); } \
    constexpr T operator|(const T t1, const T t2) noexcept { using U = std::underlying_type_t<T>; return T(U(t1) | U(t2)); } \
    constexpr T operator^(const T t1, const T t2) noexcept { using U = std::underlying_type_t<T>; return T(U(t1) ^ U(t2)); } \
    constexpr T operator&(const T t, const int i) noexcept { return t & T(i); }         \
    constexpr T operator|(const T t, const int i) noexcept { return t | T(i); }         \
    constexpr T operator^(const T t, const int i) noexcept { return t ^ T(i); }         \
    constexpr T& operator&=(T& t1, const T t2) noexcept { return t1 = t1 & t2; }  \
    constexpr T& operator|=(T& t1, const T t2) noexcept { return t1 = t1 | t2; }  \
    constexpr T& operator^=(T& t1, const T t2) noexcept { return t1 = t1 ^ t2; }  \
    constexpr T& operator&=(T& t, const int i) noexcept { return t = t & i; }     \
    constexpr T& operator|=(T& t, const int i) noexcept { return t = t | i; }     \
    constexpr T& operator^=(T& t, const int i) noexcept { return t = t ^ i; }
// clang-format on

ENABLE_BIT_OPERATORS_ON(CastlingSide)
ENABLE_BIT_OPERATORS_ON(CastlingRights)
ENABLE_BIT_OPERATORS_ON(Bound)

#undef ENABLE_BIT_OPERATORS_ON

constexpr CastlingSide make_cs(const Square kingOrgSq, const Square kingDstSq) noexcept {
    return kingOrgSq < kingDstSq ? CastlingSide::KING : CastlingSide::QUEEN;
}

constexpr CastlingRights make_cr(const Color c, const CastlingSide cs) noexcept {
    assert(is_ok(c));

    const CastlingRights cr = cs == CastlingSide::KING  ? CastlingRights::WHITE_OO
                            : cs == CastlingSide::QUEEN ? CastlingRights::WHITE_OOO
                                                        : CastlingRights::WHITE_CASTLING;
    return CastlingRights(+cr << (c << 1));
}

// Move representation (16 bits)
// Each move is compactly stored in a 16-bit unsigned integer.
//
// Bit layout (from LSB to MSB):
//  6-bits  0- 5 : Destination square (0-63)
//  6-bits  6-11 : Origin square (0-63)
//  2-bits 12-13 : Promotion piece type offset:
//                  KNIGHT = 0
//                  BISHOP = 1
//                  ROOK   = 2
//                  QUEEN  = 3
//  2-bits 14-15 : Move type flag:
//                  NORMAL     = 0
//                  PROMOTION  = 1
//                  EN_PASSANT = 2
//                  CASTLING   = 3
// Notes:
// - En-passant flag is set only when a pawn can capture en-passant.
// - Special moves Move::None and Move::Null are represented by having the same
//   origin and destination squares, which is invalid for normal moves.
//   This guarantees they never collide with any normal move.
// - This compact encoding allows fast move generation, comparison, and storage.
class Move {
   public:
    enum class MT : u8 {
        NORMAL,
        PROMOTION,
        EN_PASSANT,
        CASTLING
    };

    static constexpr u8 DstSqShift = 0;
    static constexpr u8 OrgSqShift = 6;
    static constexpr u8 PromoShift = 12;
    static constexpr u8 TypeShift  = 14;

    static constexpr u16 SqMask    = (1u << 6) - 1;
    static constexpr u16 PromoMask = (1u << 2) - 1;
    static constexpr u16 TypeMask  = ((1u << 2) - 1) << TypeShift;

    Move() noexcept = default;
    constexpr explicit Move(const u16 d) noexcept :
        data(d) {}
    constexpr Move(const Square orgSq, const Square dstSq, const MT mt = MT::NORMAL) noexcept :
        data((u16(mt) << TypeShift)        //
             | (u16(orgSq) << OrgSqShift)  //
             | (u16(dstSq) << DstSqShift)) {
        assert(DON::is_ok(orgSq) && DON::is_ok(dstSq));
    }

    constexpr Move(const Square orgSq, const Square dstSq, const PieceType promoPt) noexcept :
        data((u16(MT::PROMOTION) << TypeShift)        //
             | (u16(promoPt - KNIGHT) << PromoShift)  //
             | (u16(orgSq) << OrgSqShift)             //
             | (u16(dstSq) << DstSqShift)) {
        assert(DON::is_ok(orgSq) && DON::is_ok(dstSq) && KNIGHT <= promoPt && promoPt <= QUEEN);
    }

    // Accessors: extract parts of the move
    [[nodiscard]] constexpr Square org_sq() const noexcept {
        assert(is_ok());

        return Square((data >> OrgSqShift) & SqMask);
    }

    [[nodiscard]] constexpr Square dst_sq() const noexcept {
        assert(is_ok());

        return Square((data >> DstSqShift) & SqMask);
    }

    [[nodiscard]] constexpr MT type() const noexcept { return MT((data & TypeMask) >> TypeShift); }

    [[nodiscard]] constexpr PieceType promotion_type() const noexcept {
        return PieceType(((data >> PromoShift) & PromoMask) + KNIGHT);
    }

    [[nodiscard]] constexpr Value promotion_value() const noexcept {
        return type() == MT::PROMOTION  //
               ? piece_value(promotion_type()) - VALUE_PAWN
               : VALUE_ZERO;
    }

    [[nodiscard]] constexpr u16 raw() const noexcept { return data; }

    constexpr bool operator==(const Move m) const noexcept { return data == m.data; }
    constexpr bool operator!=(const Move m) const noexcept { return !(*this == m); }

    // Validity check: ensures move is not None or Null
    [[nodiscard]] constexpr bool is_ok() const noexcept { return data != 0x000 && data != 0xFFF; }

    [[nodiscard]] constexpr Move reverse() const noexcept {
        assert(type() == MT::NORMAL);

        return Move{dst_sq(), org_sq()};
    }

    // Declare static const members (to be defined later)
    static const Move None;
    static const Move Null;

   protected:
    u16 data;
};

// **Define the constexpr static members outside the class**
inline constexpr Move Move::None{0x000};
inline constexpr Move Move::Null{0xFFF};

using MT = Move::MT;

using Moves = std::vector<Move>;

// Keep track of what piece changes on the board by a move
struct DirtyPiece final {
   public:
    Piece  movedPc = Piece::NO_PIECE;         // this is never allowed to be NO_PIECE
    Square orgSq = SQ_NONE, dstSq = SQ_NONE;  // dstSq should be SQ_NONE for promotions

    // if {add, remove}Sq is SQ_NONE, {add, remove}Pc is allowed to be uninitialized
    // castling uses addSq and removeSq to remove and add the rook
    Square removedSq = SQ_NONE, addedSq = SQ_NONE;
    Piece  removedPc = Piece::NO_PIECE, addedPc = Piece::NO_PIECE;
};

// Keep track of what threats change on the board
struct DirtyThreats final {
   public:
    struct Threat final {
       public:
        static constexpr u8 SqShift           = 0;
        static constexpr u8 ThreatenedSqShift = 8;
        static constexpr u8 PcShift           = 16;
        static constexpr u8 ThreatenedPcShift = 20;
        static constexpr u8 AddShift          = 31;

        static constexpr u16 SqMask  = (1u << 8) - 1;
        static constexpr u16 PcMask  = (1u << 4) - 1;
        static constexpr u16 AddMask = (1u << 1) - 1;

        Threat() noexcept = default;
        constexpr explicit Threat(const u32 d) noexcept :
            data(d) {}
        constexpr Threat(const Square sq,
                         const Square threatenedSq,
                         const Piece  pc,
                         const Piece  threatenedPc,
                         const bool   add) noexcept :
            data((u32(add) << AddShift)                      //
                 | (u32(threatenedPc) << ThreatenedPcShift)  //
                 | (u32(pc) << PcShift)                      //
                 | (u32(threatenedSq) << ThreatenedSqShift)  //
                 | (u32(sq) << SqShift)) {}

        constexpr Square sq() const noexcept {  //
            return Square((data >> SqShift) & SqMask);
        }
        constexpr Square threatened_sq() const noexcept {
            return Square((data >> ThreatenedSqShift) & SqMask);
        }
        constexpr Piece pc() const noexcept {  //
            return Piece((data >> PcShift) & PcMask);
        }
        constexpr Piece threatened_pc() const noexcept {
            return Piece((data >> ThreatenedPcShift) & PcMask);
        }
        constexpr bool add() const noexcept { return ((data >> AddShift) & AddMask) != 0; }

        constexpr u32 raw() const noexcept { return data; }

       private:
        u32 data;
    };

    void add(const Square sq,
             const Square threatenedSq,
             const Piece  pc,
             const Piece  threatenedPc,
             const bool   put) noexcept {
        threats_.emplace_back(sq, threatenedSq, pc, threatenedPc, put);
    }

    [[nodiscard]] const Threat* begin() const noexcept { return threats_.begin(); }
    [[nodiscard]] const Threat* end() const noexcept { return threats_.end(); }

    [[nodiscard]] bool empty() const noexcept { return threats_.empty(); }

    [[nodiscard]] Threat* make_space(const u8 space) noexcept { return threats_.make_space(space); }

   private:
    // A piece can be involved in at most 8 outgoing attacks and 16 incoming attacks.
    // Moving a piece also can reveal at most 8 discovered attacks.
    // This implies that a non-castling move can change at most (8 + 16) * 3 + 8 = 80 features.
    // By similar logic, a castling move can change at most (5 + 1 + 3 + 9) * 2 = 36 features.
    // Thus, 80 should work as an upper bound.
    // Finally, 16 entries are added to accommodate unmasked vector stores near the end of the list.
    // So, 80 + 16 = 96.
    using ThreatVector = FixedVector<Threat, 96, u8>;

    ThreatVector threats_;
};

struct DirtyPawnPairs final {
   public:
    Array<Bitboard, COLOR_NB> before;
    Array<Bitboard, COLOR_NB> after;
};

using Threat = DirtyThreats::Threat;

struct Dirties final {
   public:
    DirtyPiece     dirtyPiece;
    DirtyThreats   dirtyThreats;
    DirtyPawnPairs dirtyPawnPairs;
};

// Linear Congruential Generator (LCG): X{n+1} = (c + a * X{n})
// Based on a congruential pseudo-random number generator.
constexpr u64 make_hash(const u64 seed) noexcept {
    return u64{0x14057B7EF767814F} + u64{0x5851F42D4C957F2D} * seed;
}

}  // namespace DON

// Hash function for unordered containers (e.g., std::unordered_set, std::unordered_map).
// Uses make_hash function to produce a unique hash value for move.
template<>
struct std::hash<DON::Move> {
    DON::usize operator()(const DON::Move m) const noexcept { return DON::make_hash(m.raw()); }
};

#endif  // TYPES_H_INCLUDED
