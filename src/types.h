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

#ifndef TYPES_H_INCLUDED
    #define TYPES_H_INCLUDED

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
// -DUSE_POPCNT   | Add runtime support for use of popcnt asm-instruction. Works
//                | only in 64-bit mode and requires hardware with popcnt support.
//
// -DUSE_BMI2     | Add runtime support for use of pext asm-instruction. Works
//                | only in 64-bit mode and requires hardware with pext support.

    #include <algorithm>
    #include <array>
    #include <cassert>
    #include <cstddef>
    #include <cstdint>
    #include <string_view>
    #include <type_traits>
    #include <vector>

    #include "misc.h"

namespace DON {

// Predefined macros hell:
//
// __GNUC__                Compiler is GCC, Clang or ICX
// __clang__               Compiler is Clang or ICX
// __INTEL_LLVM_COMPILER   Compiler is ICX
// _MSC_VER                Compiler is MSVC
// _WIN32                  Building on Windows (any)
// _WIN64                  Building on Windows 64 bit

    // Enforce minimum GCC version
    #if defined(__GNUC__) && !defined(__clang__) \
      && (__GNUC__ < 9 || (__GNUC__ == 9 && __GNUC_MINOR__ < 3))
        #error "DON requires GCC 9.3 or later for correct compilation"
    #endif

    // Enforce minimum Clang version
    #if defined(__clang__) && (__clang_major__ < 10)
        #error "DON requires Clang 10.0 or later for correct compilation"
    #endif

    #if defined(_MSC_VER)
        // Disable some silly and noisy warnings from MSVC compiler
        #pragma warning(disable: 4127)  // Conditional expression is constant
        #pragma warning(disable: 4146)  // Unary minus operator applied to unsigned type
        #pragma warning(disable: 4800)  // Forcing value to bool 'true' or 'false'

        #if defined(_WIN64)  // No Makefile used
            #define IS_64BIT
        #endif
    #endif

using Bitboard = std::uint64_t;
using Key      = std::uint64_t;
using Key32    = std::uint32_t;
using Key16    = std::uint16_t;

static_assert(sizeof(Bitboard) == 8, "Expected 64-bit Bitboard");
static_assert(sizeof(Key) == 8, "Expected 64-bit Key");

inline constexpr std::uint16_t MAX_MOVES = 256;
inline constexpr std::uint16_t MAX_PLY   = 254;

// Size of cache line (in bytes)
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

inline constexpr std::string_view PIECE_CHAR{" PNBRQK  pnbrqk "};
inline constexpr std::string_view START_FEN{
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"};

enum Color : std::uint8_t {
    WHITE,
    BLACK,
    COLOR_NB = 2
};

enum CastlingRights : std::uint8_t {
    NO_CASTLING,
    WHITE_OO  = 1,
    WHITE_OOO = WHITE_OO << 1,
    BLACK_OO  = WHITE_OO << 2,
    BLACK_OOO = WHITE_OO << 3,

    KING_SIDE      = WHITE_OO | BLACK_OO,
    QUEEN_SIDE     = WHITE_OOO | BLACK_OOO,
    WHITE_CASTLING = WHITE_OO | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO | BLACK_OOO,
    ANY_CASTLING   = WHITE_CASTLING | BLACK_CASTLING,

    CASTLING_SIDE_NB   = 2,
    CASTLING_RIGHTS_NB = 16
};

// clang-format off
enum PieceType : std::int8_t {
    NO_PIECE_TYPE,
    PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    ALL_PIECE     = 0,
    PIECE_TYPE_NB = 8
};

constexpr StdArray<PieceType, PIECE_TYPE_NB - 2> PieceTypes{
  PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
};

enum Piece : std::uint8_t {
    NO_PIECE,
    W_ALL = 0,
    B_ALL = 8,
    W_PAWN = PAWN + W_ALL, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = PAWN + B_ALL, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};
// clang-format on
static_assert(sizeof(Piece) == 1);

constexpr StdArray<Piece, COLOR_NB, PIECE_TYPE_NB - 2> Pieces{{
  {W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING},  //
  {B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING}   //
}};
constexpr StdArray<Piece, COLOR_NB, PIECE_TYPE_NB - 4> NonPawnPieces{{
  {W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN},  //
  {B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN}   //
}};

// Value is used as an alias for std::int16_t, this is done to differentiate between
// a search value and any other integer value. The values used in search are always
// supposed to be in the range (-VALUE_NONE, +VALUE_NONE] and should not exceed this range.
using Value    = std::int16_t;
using SqrValue = std::int32_t;

inline constexpr Value VALUE_ZERO = 0;
inline constexpr Value VALUE_DRAW = VALUE_ZERO;

inline constexpr Value VALUE_NONE     = 0x7FFF;
inline constexpr Value VALUE_INFINITE = VALUE_NONE - 1;

inline constexpr Value VALUE_MATE             = VALUE_INFINITE - 1;
inline constexpr Value VALUE_MATES_IN_MAX_PLY = VALUE_MATE - MAX_PLY;
inline constexpr Value VALUE_MATED_IN_MAX_PLY = -VALUE_MATES_IN_MAX_PLY;

inline constexpr Value VALUE_TB                 = VALUE_MATES_IN_MAX_PLY - 1;
inline constexpr Value VALUE_TB_WIN_IN_MAX_PLY  = VALUE_TB - MAX_PLY;
inline constexpr Value VALUE_TB_LOSS_IN_MAX_PLY = -VALUE_TB_WIN_IN_MAX_PLY;

// In the code, make the assumption that these values
// are such that non_pawn_value() can be used to uniquely
// identify the material on the board.
inline constexpr Value VALUE_PAWN   = 208;
inline constexpr Value VALUE_KNIGHT = 781;
inline constexpr Value VALUE_BISHOP = 825;
inline constexpr Value VALUE_ROOK   = 1276;
inline constexpr Value VALUE_QUEEN  = 2538;

inline constexpr StdArray<Value, PIECE_TYPE_NB> PIECE_VALUE{
  VALUE_ZERO, VALUE_PAWN,  VALUE_KNIGHT, VALUE_BISHOP,
  VALUE_ROOK, VALUE_QUEEN, VALUE_ZERO,   VALUE_ZERO  //
};

constexpr bool is_valid(Value value) noexcept { return value != VALUE_NONE; }

constexpr bool is_ok(Value value) noexcept {
    assert(is_valid(value));
    return -VALUE_INFINITE < value && value < +VALUE_INFINITE;
}

constexpr Value in_range(std::int32_t value) noexcept {
    return std::clamp(value, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}

constexpr bool is_win(Value value) noexcept {
    assert(is_valid(value));
    return value >= VALUE_TB_WIN_IN_MAX_PLY;
}

constexpr bool is_loss(Value value) noexcept {
    assert(is_valid(value));
    return value <= VALUE_TB_LOSS_IN_MAX_PLY;
}

constexpr bool is_decisive(Value value) noexcept { return is_win(value) || is_loss(value); }

constexpr bool is_mate_win(Value value) noexcept {
    assert(is_valid(value));
    return value >= VALUE_MATES_IN_MAX_PLY;
}

constexpr bool is_mate_loss(Value value) noexcept {
    assert(is_valid(value));
    return value <= VALUE_MATED_IN_MAX_PLY;
}

constexpr bool is_mate(Value value) noexcept { return is_mate_win(value) || is_mate_loss(value); }

constexpr Value mates_in(std::int16_t ply) noexcept { return +VALUE_MATE - ply; }

constexpr Value mated_in(std::int16_t ply) noexcept { return -VALUE_MATE + ply; }

// Depth is used as an alias for std::int16_t
using Depth = std::int16_t;

inline constexpr Depth DEPTH_ZERO = 0;
inline constexpr Depth DEPTH_NONE = -1;
// Depth used only for TT entry occupancy check
inline constexpr Depth DEPTH_OFFSET = DEPTH_NONE - 1;
static_assert(DEPTH_OFFSET == MAX_PLY - 1 - 0xFF, "DEPTH_OFFSET == MAX_PLY - 1 - 0xFF");

// clang-format off
enum File : std::int8_t {
    FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H,
    FILE_NB = 8
};

enum Rank : std::int8_t {
    RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8,
    RANK_NB = 8
};

enum Square : std::int8_t {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE,
    SQUARE_ZERO = 0,
    SQUARE_NB = 64
};
// clang-format on

enum Direction : std::int8_t {
    EAST  = 1,
    NORTH = 8,
    WEST  = -EAST,
    SOUTH = -NORTH,

    NORTH_EAST = NORTH + EAST,
    SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST,
    NORTH_WEST = NORTH + WEST,

    EAST_2  = EAST + EAST,
    WEST_2  = WEST + WEST,
    NORTH_2 = NORTH + NORTH,
    SOUTH_2 = SOUTH + SOUTH,
};

enum Bound : std::uint8_t {
    BOUND_NONE,
    BOUND_UPPER,
    BOUND_LOWER,
    BOUND_EXACT = BOUND_UPPER | BOUND_LOWER
};

// Keep track of what a move changes on the board (used by NNUE)
struct DirtyPiece final {
   public:
    constexpr DirtyPiece() noexcept = default;

    Piece  pc  = NO_PIECE;                // this is never allowed to be NO_PIECE
    Square org = SQ_NONE, dst = SQ_NONE;  // dst should be SQ_NONE for promotions

    // if {add, remove}Sq is SQ_NONE, {add, remove}Pc is allowed to be uninitialized
    // castling uses addSq and removeSq to remove and add the rook
    Square removeSq = SQ_NONE, addSq = SQ_NONE;
    Piece  removePc = NO_PIECE, addPc = NO_PIECE;
};

// Keep track of what threats change on the board (used by NNUE)
struct DirtyThreat final {
   public:
    DirtyThreat() noexcept {
        // Don't initialize data
    }
    DirtyThreat(Square sq, Square threatenedSq, Piece pc, Piece threatenedPc, bool add) noexcept {
        data = (add << 31) | (threatenedPc << 20) | (pc << 16) | (threatenedSq << 8) | (sq << 0);
    }

    Square sq() const noexcept { return Square((data >> 0) & 0x3F); }
    Square threatened_sq() const noexcept { return Square((data >> 8) & 0x3F); }
    Piece  pc() const noexcept { return Piece((data >> 16) & 0xF); }
    Piece  threatened_pc() const noexcept { return Piece((data >> 20) & 0xF); }
    bool   add() const noexcept { return data >> 31; }

   private:
    std::uint32_t data;
};

// A piece can be involved in at most 8 outgoing attacks and 16 incoming attacks.
// Moving a piece also can reveal at most 8 discovered attacks.
// This implies that a non-castling move can change at most (8 + 16) * 3 + 8 = 80 features.
// By similar logic, a castling move can change at most (5 + 1 + 3 + 9) * 2 = 36 features.
// Thus, 80 should work as an upper bound.
using DirtyThreatList = FixedVector<DirtyThreat, 80>;

struct DirtyThreats final {
   public:
    template<bool Add>
    void add(Square sq, Square threatenedSq, Piece pc, Piece threatenedPc) noexcept;

    DirtyThreatList list;
    Color           ac;
    Square          kingSq, preKingSq;

    Bitboard threateningBB, threatenedBB;
};

struct DirtyBoard final {
   public:
    DirtyPiece   dp;
    DirtyThreats dts;
};

// clang-format off
    #define ENABLE_INCR_OPERATORS_ON(T) \
        static_assert(std::is_enum_v<T>, "ENABLE_INCR_OPERATORS_ON requires an enum"); \
        static_assert(std::is_convertible_v<T, int>, "ENABLE_INCR_OPERATORS_ON requires an *unscoped* enum (plain enum)"); \
        constexpr T& operator++(T& v) noexcept { return v = T(int(v) + 1); }    \
        constexpr T& operator--(T& v) noexcept { return v = T(int(v) - 1); }    \
        constexpr T  operator++(T& v, int) noexcept { T u = v; ++v; return u; } \
        constexpr T  operator--(T& v, int) noexcept { T u = v; --v; return u; }
// clang-format on

ENABLE_INCR_OPERATORS_ON(PieceType)
ENABLE_INCR_OPERATORS_ON(File)
ENABLE_INCR_OPERATORS_ON(Rank)
ENABLE_INCR_OPERATORS_ON(Square)

    #undef ENABLE_INCR_OPERATORS_ON

// Additional operators for File
constexpr File  operator+(File f, int i) noexcept { return File(int(f) + i); }
constexpr File  operator-(File f, int i) noexcept { return File(int(f) - i); }
constexpr File& operator+=(File& f, int i) noexcept { return f = f + i; }
constexpr File& operator-=(File& f, int i) noexcept { return f = f - i; }
// Additional operators for Rank
constexpr Rank  operator+(Rank r, int i) noexcept { return Rank(int(r) + i); }
constexpr Rank  operator-(Rank r, int i) noexcept { return Rank(int(r) - i); }
constexpr Rank& operator+=(Rank& r, int i) noexcept { return r = r + i; }
constexpr Rank& operator-=(Rank& r, int i) noexcept { return r = r - i; }
// Additional operators for Square to add a Direction
constexpr Square  operator+(Square s, int i) noexcept { return Square(int(s) + i); }
constexpr Square  operator-(Square s, int i) noexcept { return Square(int(s) - i); }
constexpr Square  operator+(Square s, Direction d) noexcept { return s + int(d); }
constexpr Square  operator-(Square s, Direction d) noexcept { return s - int(d); }
constexpr Square& operator+=(Square& s, Direction d) noexcept { return s = s + d; }
constexpr Square& operator-=(Square& s, Direction d) noexcept { return s = s - d; }

// clang-format off

constexpr Direction operator+(Direction d1, Direction d2) noexcept { return Direction(int(d1) + int(d2)); }
constexpr Direction operator-(Direction d1, Direction d2) noexcept { return Direction(int(d1) - int(d2)); }
//constexpr Direction operator-(Square s1, Square s2) noexcept { return Direction(int(s1) - int(s2)); }
constexpr Direction operator*(Direction d, int i) noexcept { return Direction(i * int(d)); }
constexpr Direction operator*(int i, Direction d) noexcept { return d * i; }

constexpr CastlingRights  operator|(CastlingRights cr, int i) noexcept { return CastlingRights(int(cr) | i); }
constexpr CastlingRights  operator&(CastlingRights cr, int i) noexcept { return CastlingRights(int(cr) & i); }
constexpr CastlingRights& operator|=(CastlingRights& cr, int i) noexcept { return cr = cr | i; }
constexpr CastlingRights& operator&=(CastlingRights& cr, int i) noexcept { return cr = cr & i; }
constexpr CastlingRights  operator&(Color c, CastlingRights cr) noexcept {
    assert(is_ok(c));
    return (c == WHITE ? WHITE_CASTLING : BLACK_CASTLING) & int(cr);
}

// clang-format on

[[nodiscard]] constexpr bool is_ok(Color c) noexcept { return (c == WHITE || c == BLACK); }

// Toggle color
constexpr Color operator~(Color c) noexcept { return Color(int(c) ^ 1); }

[[nodiscard]] constexpr bool is_ok(PieceType pt) noexcept { return (PAWN <= pt && pt <= KING); }

[[nodiscard]] constexpr bool is_major(PieceType pt) noexcept { return (pt >= ROOK); }

[[nodiscard]] constexpr Piece make_piece(Color c, PieceType pt) noexcept {
    assert(is_ok(c) && (is_ok(pt) || pt == ALL_PIECE));
    return Piece((int(c) << 3) | int(pt));
}

[[nodiscard]] constexpr bool is_ok(Piece pc) noexcept {
    return (W_PAWN <= pc && pc <= W_KING) || (B_PAWN <= pc && pc <= B_KING);
}

constexpr PieceType type_of(Piece pc) noexcept { return PieceType(int(pc) & 7); }

constexpr Color color_of(Piece pc) noexcept { return Color(int(pc) >> 3); }

// Swap color of piece B_KNIGHT <-> W_KNIGHT
constexpr Piece flip_color(Piece pc) noexcept { return Piece(int(pc) ^ int(PIECE_TYPE_NB)); }

constexpr Piece relative_piece(Color c, Piece pc) noexcept {
    return Piece(int(pc) ^ (c * int(PIECE_TYPE_NB)));
}

[[nodiscard]] constexpr bool is_ok(File f) noexcept { return (FILE_A <= f && f <= FILE_H); }

[[nodiscard]] constexpr bool is_ok(Rank r) noexcept { return (RANK_1 <= r && r <= RANK_8); }

[[nodiscard]] constexpr Square make_square(File f, Rank r) noexcept {
    assert(is_ok(f) && is_ok(r));
    return Square((int(r) << 3) | int(f));
}

[[nodiscard]] constexpr bool is_ok(Square s) noexcept { return (SQ_A1 <= s && s <= SQ_H8); }

constexpr File file_of(Square s) noexcept { return File(int(s) & 7); }

constexpr Rank rank_of(Square s) noexcept { return Rank((int(s) >> 3) & 7); }

[[nodiscard]] constexpr bool is_light(Square s) noexcept {
    return (int(/*file_of*/ s) ^ int(rank_of(s))) & 1;
}
[[nodiscard]] constexpr bool color_opposite(Square s1, Square s2) noexcept {
    return is_light(s1) != is_light(s2);
}

// Swap A1 <-> H1
constexpr Square flip_file(Square s) noexcept { return Square(int(s) ^ int(SQ_H1)); }
// Swap A1 <-> A8
constexpr Square flip_rank(Square s) noexcept { return Square(int(s) ^ int(SQ_A8)); }

constexpr File fold_to_edge(File f) noexcept { return std::min(f, File(FILE_H - f)); }
constexpr Rank fold_to_edge(Rank r) noexcept { return std::min(r, Rank(RANK_8 - r)); }

constexpr Square relative_sq(Color c, Square s) noexcept {
    return Square(int(s) ^ (c * int(SQ_A8)));
}

constexpr Rank relative_rank(Color c, Rank r) noexcept { return Rank(int(r) ^ (c * int(RANK_8))); }

constexpr Rank relative_rank(Color c, Square s) noexcept { return relative_rank(c, rank_of(s)); }

constexpr CastlingRights make_castling_rights(Color c, Square s1, Square s2) noexcept {
    return c & (s1 < s2 ? KING_SIDE : QUEEN_SIDE);
}

constexpr Square king_castle_sq(Color c, Square s1, Square s2) noexcept {
    return relative_sq(c, s1 < s2 ? SQ_G1 : SQ_C1);
}
constexpr Square rook_castle_sq(Color c, Square s1, Square s2) noexcept {
    return relative_sq(c, s1 < s2 ? SQ_F1 : SQ_D1);
}

constexpr Direction pawn_spush(Color c) noexcept {
    assert(is_ok(c));
    return c == WHITE ? NORTH : SOUTH;
}
constexpr Direction pawn_dpush(Color c) noexcept {
    assert(is_ok(c));
    return c == WHITE ? NORTH_2 : SOUTH_2;
}

[[nodiscard]] constexpr char to_char(PieceType pt) noexcept {
    return is_ok(pt) ? PIECE_CHAR[pt] : ' ';
}

[[nodiscard]] constexpr char to_char(Piece pc) noexcept { return is_ok(pc) ? PIECE_CHAR[pc] : ' '; }

[[nodiscard]] constexpr Piece to_piece(char pc) noexcept {
    const std::size_t pos = PIECE_CHAR.find(pc);
    return pos != std::string_view::npos ? Piece(pos) : NO_PIECE;
}

template<bool Upper = false>
[[nodiscard]] constexpr char to_char(File f) noexcept {
    return int(f) + (Upper ? 'A' : 'a');
}

[[nodiscard]] constexpr char to_char(Rank r) noexcept { return int(r) + '1'; }

[[nodiscard]] constexpr File to_file(char f) noexcept { return File(f - 'a'); }

[[nodiscard]] constexpr Rank to_rank(char r) noexcept { return Rank(r - '1'); }

// Flip file 'A'-'H' or 'a'-'h'; otherwise unchanged
[[nodiscard]] constexpr char flip_file(char f) noexcept {
    return ('A' <= f && f <= 'H') ? 'A' + ('H' - f) : ('a' <= f && f <= 'h') ? 'a' + ('h' - f) : f;
}
// Flip rank '1'-'8'; otherwise unchanged
[[nodiscard]] constexpr char flip_rank(char r) noexcept {
    return ('1' <= r && r <= '8') ? '1' + ('8' - r) : r;
}

// Build a compile-time table: "a1", "b1", ..., "h8"
alignas(CACHE_LINE_SIZE) inline constexpr auto SQUARE_CHARS = []() constexpr {
    StdArray<char, SQUARE_NB, 2> squareChars{};
    for (Square s = SQ_A1; s <= SQ_H8; ++s)
        squareChars[s] = {to_char(file_of(s)), to_char(rank_of(s))};
    return squareChars;
}();

[[nodiscard]] constexpr std::string_view to_square(Square s) noexcept {
    assert(is_ok(s));
    return {SQUARE_CHARS[s].data(), 2};
}

static_assert(to_square(SQ_A1) == "a1" && to_square(SQ_H8) == "h8",
              "to_square(): broken, expected 'a1' & 'h8'");

// Linear Congruential Generator (LCG): X{n+1} = (c + a * X{n})
// Based on a congruential pseudo-random number generator
constexpr std::uint64_t make_hash(std::uint64_t seed) noexcept {
    return 0x14057B7EF767814FULL + 0x5851F42D4C957F2DULL * seed;
}

constexpr Key32 compress_key32(Key key) noexcept {
    return ((key >> 00) & 0xFFFFFFFF)  //
         ^ ((key >> 32) & 0xFFFF0000);
}
constexpr Key16 compress_key16(Key key) noexcept {
    return ((key >> 00) & 0xFFFF)  //
         ^ ((key >> 16) & 0xFFF0)  //
         ^ ((key >> 32) & 0xFF00)  //
         ^ ((key >> 48) & 0xF000);
}

// A move needs 16 bits to be stored
//
// bit  0- 5: destination square (from 0 to 63)
// bit  6-11: origin square (from 0 to 63)
// bit 12-13: promotion piece type - 2 (from KNIGHT-2 to QUEEN-2)
// bit 14-15: special move flag: promotion (1), en-passant (2), castling (3)
// NOTE: en-passant bit is set only when a pawn can be captured
//
// Special cases are Move::None and Move::Null. Can sneak these in because
// in any normal move destination square is always different from origin square
// while Move::None and Move::Null have the same origin and destination square.
enum MoveType : std::uint16_t {
    NORMAL     = 0 << 14,
    PROMOTION  = 1 << 14,
    EN_PASSANT = 2 << 14,
    CASTLING   = 3 << 14,
    TYPE_MASK  = 0xC000
};

class Move {
   public:
    // Hash function for unordered containers (e.g., std::unordered_set, std::unordered_map).
    // Uses make_hash function to produce a unique hash value for move
    struct Hash final {
        std::size_t operator()(Move m) const noexcept { return make_hash(m.move); }
    };

    constexpr Move() noexcept = default;
    // Constructors using delegating syntax
    constexpr explicit Move(std::uint16_t m) noexcept :
        move(m) {}
    constexpr Move(MoveType T, Square org, Square dst) noexcept :
        Move(T | (int(org) << 6) | (int(dst) << 0)) {}
    constexpr Move(Square org, Square dst) noexcept :
        Move(NORMAL, org, dst) {}
    constexpr Move(Square org, Square dst, PieceType promo) noexcept :
        Move(MoveType(PROMOTION | ((int(promo) - int(KNIGHT)) << 12)), org, dst) {}

    // Accessors: extract parts of the move
    constexpr Square    org_sq() const noexcept { return Square((move >> 6) & 0x3F); }
    constexpr Square    dst_sq() const noexcept { return Square((move >> 0) & 0x3F); }
    constexpr MoveType  type_of() const noexcept { return MoveType(move & TYPE_MASK); }
    constexpr PieceType promotion_type() const noexcept {
        return PieceType(((move >> 12) & 0x3) + int(KNIGHT));
    }

    constexpr Value promotion_value() const noexcept {
        return type_of() == PROMOTION ? PIECE_VALUE[promotion_type()] - VALUE_PAWN : VALUE_ZERO;
    }

    constexpr std::uint16_t raw() const noexcept { return move; }

    friend constexpr bool operator==(Move m1, Move m2) noexcept { return m1.move == m2.move; }
    friend constexpr bool operator!=(Move m1, Move m2) noexcept { return !(m1 == m2); }

    // Validity check: ensures move is not None or Null
    constexpr bool is_ok() const noexcept { return org_sq() != dst_sq(); }

    //constexpr explicit operator bool() const noexcept { return move != 0; }

    constexpr Move reverse() const noexcept { return Move(dst_sq(), org_sq()); }

    // Declare static const members (to be defined later)
    static const Move None;
    static const Move Null;

   protected:
    std::uint16_t move{0};
};

// **Define the constexpr static members outside the class**
inline constexpr Move Move::None{SQ_A1, SQ_A1};
inline constexpr Move Move::Null{SQ_B1, SQ_B1};

using MoveVector = std::vector<Move>;

template<typename T, typename... Ts>
struct is_all_same final {
    static constexpr bool value = (std::is_same_v<T, Ts> && ...);
};

template<typename... Ts>
constexpr auto is_all_same_v = is_all_same<Ts...>::value;

}  // namespace DON

#endif  // #ifndef TYPES_H_INCLUDED

#include "tune.h"  // Global visibility to tuning setup
