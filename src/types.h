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

    #include <algorithm>
    #include <array>
    #include <cassert>
    #include <cstddef>
    #include <cstdint>
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
// -DUSE_POPCNT   | Add runtime support for use of popcnt asm-instruction. Works
//                | only in 64-bit mode and requires hardware with popcnt support.
//
// -DUSE_BMI2     | Add runtime support for use of pext asm-instruction. Works
//                | only in 64-bit mode and requires hardware with pext support.

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

    // Enforce minimum GCC version
    #if defined(__GNUC__) && !defined(__clang__) \
      && (__GNUC__ < 9 || (__GNUC__ == 9 && __GNUC_MINOR__ < 3))
        #error "DON requires GCC 9.3 or later for correct compilation"
    #endif

    // Enforce minimum Clang version
    #if defined(__clang__) && (__clang_major__ < 10)
        #error "DON requires Clang 10.0 or later for correct compilation"
    #endif

namespace DON {

using Bitboard = std::uint64_t;
using Key      = std::uint64_t;

static_assert(sizeof(Bitboard) == 8, "Expected 64-bit Bitboard");
static_assert(sizeof(Key) == 8, "Expected 64-bit Key");

inline constexpr std::uint16_t MAX_MOVES = 256;
inline constexpr std::uint16_t MAX_PLY   = 254;

// Size of cache line (in bytes)
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

inline constexpr std::string_view PIECE_CHAR{" PNBRQK  pnbrqk "};
inline constexpr std::string_view START_FEN{
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"};

// clang-format off
enum File : std::int8_t {
    FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H
};

enum Rank : std::int8_t {
    RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8
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
    SQUARE_ZERO = 0
};
// clang-format on

inline constexpr std::size_t FILE_NB   = 8;
inline constexpr std::size_t RANK_NB   = 8;
inline constexpr std::size_t SQUARE_NB = 64;

enum Direction : std::int8_t {
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

// clang-format off
enum PieceType : std::int8_t {
    NO_PIECE_TYPE,
    PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING, ALL
};
// clang-format on

inline constexpr std::size_t PIECE_TYPE_NB  = 8;
inline constexpr std::size_t PIECE_TYPE_CNT = 6;

[[nodiscard]] constexpr bool is_ok(PieceType pt) noexcept { return (PAWN <= pt && pt <= KING); }

[[nodiscard]] constexpr bool is_major(PieceType pt) noexcept { return (pt >= ROOK); }

constexpr StdArray<PieceType, PIECE_TYPE_CNT> PIECE_TYPES{
  PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING  //
};
constexpr StdArray<PieceType, PIECE_TYPE_CNT - 2> NON_PAWN_PIECE_TYPES{
  KNIGHT, BISHOP, ROOK, QUEEN  //
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

ENABLE_INCR_OPERATORS_ON(File)
ENABLE_INCR_OPERATORS_ON(Rank)
ENABLE_INCR_OPERATORS_ON(Square)
ENABLE_INCR_OPERATORS_ON(PieceType)

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

constexpr Direction operator+(Direction d1, Direction d2) noexcept {
    return Direction(std::int8_t(d1) + std::int8_t(d2));
}
constexpr Direction operator-(Direction d1, Direction d2) noexcept {
    return Direction(std::int8_t(d1) - std::int8_t(d2));
}
//constexpr Direction operator-(Square s1, Square s2) noexcept { return Direction(int(s1) - int(s2)); }
constexpr Direction operator*(Direction d, int i) noexcept { return Direction(i * std::int8_t(d)); }
constexpr Direction operator*(int i, Direction d) noexcept { return d * i; }

[[nodiscard]] constexpr bool is_ok(File f) noexcept { return (FILE_A <= f && f <= FILE_H); }

[[nodiscard]] constexpr bool is_ok(Rank r) noexcept { return (RANK_1 <= r && r <= RANK_8); }

[[nodiscard]] constexpr Square make_square(File f, Rank r) noexcept {
    assert(is_ok(f) && is_ok(r));
    return Square((r << 3) | f);
}

[[nodiscard]] constexpr bool is_ok(Square s) noexcept { return (SQ_A1 <= s && s <= SQ_H8); }

constexpr File file_of(Square s) noexcept { return File((s >> 0) & 0x7); }

constexpr Rank rank_of(Square s) noexcept { return Rank((s >> 3) & 0x7); }

[[nodiscard]] constexpr bool is_light(Square s) noexcept {
    return ((/*file_of*/ s ^ rank_of(s)) & 0x1) != 0;
}
[[nodiscard]] constexpr bool color_opposite(Square s1, Square s2) noexcept {
    return is_light(s1) != is_light(s2);
}

// Swap A1 <-> H1, B1 <-> G1, ...
constexpr Square flip_file(Square s) noexcept { return Square(s ^ SQ_H1); }
// Swap A1 <-> H8, B1 <-> G8, ...
constexpr Square flip_rank(Square s) noexcept { return Square(s ^ SQ_A8); }

enum Color : std::uint8_t {
    WHITE,
    BLACK,
    NONE
};

inline constexpr std::size_t COLOR_NB = 2;

[[nodiscard]] constexpr bool is_ok(Color c) noexcept { return (c == WHITE || c == BLACK); }

// Toggle color
constexpr Color operator~(Color c) noexcept { return Color(c ^ BLACK); }

// clang-format off
enum class Piece : std::uint8_t {
    NO_PIECE,
    W_PAWN = 0 + PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 8 + PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING
};
// clang-format on
static_assert(sizeof(Piece) == 1);

inline constexpr std::size_t PIECE_NB = 16;

constexpr std::uint8_t operator+(Piece pc) noexcept { return std::uint8_t(pc); }

constexpr Piece operator^(Piece pc1, Piece pc2) noexcept { return Piece(+pc1 ^ +pc2); }

[[nodiscard]] constexpr Piece make_piece(Color c, PieceType pt) noexcept {
    assert(is_ok(c) && is_ok(pt));

    return Piece((c << 3) | pt);
}

[[nodiscard]] constexpr bool is_ok(Piece pc) noexcept {
    return (Piece::W_PAWN <= pc && pc <= Piece::W_KING)
        || (Piece::B_PAWN <= pc && pc <= Piece::B_KING);
}

constexpr PieceType type_of(Piece pc) noexcept { return PieceType((+pc >> 0) & 0x7); }

constexpr Color color_of(Piece pc) noexcept { return Color((+pc >> 3) & 0x1); }

// Swap color of piece B_KNIGHT <-> W_KNIGHT
constexpr Piece flip_color(Piece pc) noexcept { return Piece(+pc ^ PIECE_TYPE_NB); }

constexpr Piece relative_piece(Color c, Piece pc) noexcept {
    return Piece(+pc ^ (c * PIECE_TYPE_NB));
}

enum class CastlingSide : std::uint8_t {
    KING,
    QUEEN,
    ANY
};

inline constexpr std::size_t CASTLING_SIDE_NB = 2;

constexpr std::uint8_t operator+(CastlingSide cs) noexcept { return std::uint8_t(cs); }

[[nodiscard]] constexpr bool is_ok(CastlingSide cs) noexcept {
    return (cs == CastlingSide::KING || cs == CastlingSide::QUEEN);
}

constexpr CastlingSide castling_side(Square kingOrgSq, Square kingDstSq) noexcept {
    return kingOrgSq < kingDstSq ? CastlingSide::KING : CastlingSide::QUEEN;
}

inline std::string to_string(CastlingSide cs) noexcept {
    switch (cs)
    {
    case CastlingSide::KING :
        return "O-O";
    case CastlingSide::QUEEN :
        return "O-O-O";
    case CastlingSide::ANY :
        return "O-O / O-O-O";
    default :
        return "";
    }
}

enum CastlingRights : std::uint8_t {
    NO_CASTLING,

    WHITE_OO  = 1 << 0,
    WHITE_OOO = 1 << 1,

    WHITE_CASTLING = WHITE_OO | WHITE_OOO,

    BLACK_OO  = WHITE_OO << 2,
    BLACK_OOO = WHITE_OOO << 2,

    BLACK_CASTLING = BLACK_OO | BLACK_OOO,

    ANY_CASTLING = WHITE_CASTLING | BLACK_CASTLING
};

inline constexpr std::size_t CASTLING_RIGHTS_NB = 16;

constexpr CastlingRights operator|(CastlingRights cr1, CastlingRights cr2) noexcept {
    return CastlingRights(std::uint8_t(cr1) | std::uint8_t(cr2));
}
constexpr CastlingRights operator&(CastlingRights cr1, CastlingRights cr2) noexcept {
    return CastlingRights(std::uint8_t(cr1) & std::uint8_t(cr2));
}
constexpr CastlingRights operator|(CastlingRights cr, int i) noexcept {
    return cr | CastlingRights(i);
}
constexpr CastlingRights operator&(CastlingRights cr, int i) noexcept {
    return cr & CastlingRights(i);
}
constexpr CastlingRights& operator|=(CastlingRights& cr, int i) noexcept { return cr = cr | i; }
constexpr CastlingRights& operator&=(CastlingRights& cr, int i) noexcept { return cr = cr & i; }

constexpr CastlingRights make_cr(Color c, CastlingSide cs) noexcept {
    assert(is_ok(c));

    return CastlingRights((cs == CastlingSide::KING    ? WHITE_OO
                           : cs == CastlingSide::QUEEN ? WHITE_OOO
                                                       : WHITE_CASTLING)
                          << (c << 1));
}

constexpr File fold_to_edge(File f) noexcept { return std::min(f, File(FILE_H - f)); }
constexpr Rank fold_to_edge(Rank r) noexcept { return std::min(r, Rank(RANK_8 - r)); }

constexpr Square relative_sq(Color c, Square s) noexcept { return Square(s ^ (c * SQ_A8)); }

constexpr Rank relative_rank(Color c, Rank r) noexcept { return Rank(r ^ (c * RANK_8)); }

constexpr Rank relative_rank(Color c, Square s) noexcept { return relative_rank(c, rank_of(s)); }

constexpr Square king_castle_sq(Square kingOrgSq, Square kingDstSq) noexcept {
    return make_square(kingOrgSq < kingDstSq ? FILE_G : FILE_C, rank_of(kingOrgSq));
}
constexpr Square rook_castle_sq(Square kingOrgSq, Square kingDstSq) noexcept {
    return make_square(kingOrgSq < kingDstSq ? FILE_F : FILE_D, rank_of(kingOrgSq));
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

[[nodiscard]] constexpr char to_char(Piece pc) noexcept {  //
    return is_ok(pc) ? PIECE_CHAR[+pc] : ' ';
}

[[nodiscard]] constexpr Piece to_piece(char pc) noexcept {
    const std::size_t pos = PIECE_CHAR.find(pc);
    return pos != std::string_view::npos ? Piece(pos) : Piece::NO_PIECE;
}

template<bool Upper = false>
[[nodiscard]] constexpr char to_char(File f) noexcept {
    return (Upper ? 'A' : 'a') + f;
}

[[nodiscard]] constexpr char to_char(Rank r) noexcept { return '1' + r; }

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
alignas(CACHE_LINE_SIZE) inline constexpr auto SQUARE_CHARS = []() constexpr noexcept {
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

// Value is used as an alias for std::int16_t.
// This is done to differentiate between a search value and any other integer value.
// The values used in search are always supposed to be in the range (-VALUE_NONE, +VALUE_NONE]
// and should not exceed this range.
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

// Piece values in centipawns
inline constexpr Value VALUE_PAWN   = 208;
inline constexpr Value VALUE_KNIGHT = 781;
inline constexpr Value VALUE_BISHOP = 825;
inline constexpr Value VALUE_ROOK   = 1276;
inline constexpr Value VALUE_QUEEN  = 2538;

// Returns the value of the given piece type
constexpr Value piece_value(PieceType pt) noexcept {
    constexpr StdArray<Value, 1 + PIECE_TYPE_CNT> PieceValues{
      VALUE_ZERO, VALUE_PAWN, VALUE_KNIGHT, VALUE_BISHOP, VALUE_ROOK, VALUE_QUEEN, VALUE_ZERO};

    return PieceValues[pt];
}

constexpr bool is_valid(Value value) noexcept { return value != VALUE_NONE; }

constexpr bool is_ok(Value value) noexcept {
    assert(is_valid(value));

    return -VALUE_INFINITE < value && value < +VALUE_INFINITE;
}

// Clamp value to the range (VALUE_TB_LOSS_IN_MAX_PLY, VALUE_TB_WIN_IN_MAX_PLY)
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

// Check if the value represents a decisive outcome (win or loss)
constexpr bool is_decisive(Value value) noexcept { return is_win(value) || is_loss(value); }

constexpr bool is_mate_win(Value value) noexcept {
    assert(is_valid(value));

    return value >= VALUE_MATES_IN_MAX_PLY;
}

constexpr bool is_mate_loss(Value value) noexcept {
    assert(is_valid(value));

    return value <= VALUE_MATED_IN_MAX_PLY;
}

// Check if the value represents a mate score (win or loss)
constexpr bool is_mate(Value value) noexcept { return is_mate_win(value) || is_mate_loss(value); }

constexpr Value mates_in(std::int16_t ply) noexcept { return +VALUE_MATE - ply; }

constexpr Value mated_in(std::int16_t ply) noexcept { return -VALUE_MATE + ply; }

// Depth is used as an alias for std::int16_t
using Depth = std::int16_t;

inline constexpr Depth DEPTH_ZERO = 0;
inline constexpr Depth DEPTH_NONE = -1;
// Offset to convert depth to a non-negative array index.
// It is used only for TT entry occupancy check.
inline constexpr Depth DEPTH_OFFSET = DEPTH_NONE - 1;
static_assert(DEPTH_OFFSET == MAX_PLY - 1 - 0xFF, "DEPTH_OFFSET == MAX_PLY - 1 - 0xFF");

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
    enum class MT : std::uint8_t {
        NORMAL,
        PROMOTION,
        EN_PASSANT,
        CASTLING
    };

    static constexpr std::uint8_t ORG_SQ_OFFSET = 0;
    static constexpr std::uint8_t DST_SQ_OFFSET = 6;
    static constexpr std::uint8_t PROMO_OFFSET  = 12;
    static constexpr std::uint8_t TYPE_OFFSET   = 14;

    static constexpr std::uint16_t TYPE_MASK = 0x3 << TYPE_OFFSET;

    // Factory method to create moves
    template<MT T = MT::NORMAL>
    static constexpr Move make(Square orgSq, Square dstSq, PieceType promoPt = KNIGHT) noexcept;

    Move() noexcept = default;
    constexpr explicit Move(std::uint16_t d) noexcept :
        data(d) {}
    constexpr Move(Square orgSq, Square dstSq) noexcept :
        Move((int(MT::NORMAL) << TYPE_OFFSET) | (dstSq << DST_SQ_OFFSET)
             | (orgSq << ORG_SQ_OFFSET)) {}

    // Accessors: extract parts of the move
    constexpr Square org_sq() const noexcept { return Square((data >> ORG_SQ_OFFSET) & 0x3F); }
    constexpr Square dst_sq() const noexcept { return Square((data >> DST_SQ_OFFSET) & 0x3F); }

    constexpr PieceType promotion_type() const noexcept {
        return PieceType(KNIGHT + ((data >> PROMO_OFFSET) & 0x3));
    }

    constexpr MT type() const noexcept { return MT((data >> TYPE_OFFSET) & 0x3); }

    constexpr Value promotion_value() const noexcept {
        return type() == MT::PROMOTION  //
               ? piece_value(promotion_type()) - VALUE_PAWN
               : VALUE_ZERO;
    }

    constexpr std::uint16_t raw() const noexcept { return data; }

    friend constexpr bool operator==(Move m1, Move m2) noexcept { return m1.data == m2.data; }
    friend constexpr bool operator!=(Move m1, Move m2) noexcept { return !(m1 == m2); }

    // Validity check: ensures move is not None or Null
    constexpr bool is_ok() const noexcept { return org_sq() != dst_sq(); }

    //constexpr explicit operator bool() const noexcept { return move != 0; }

    constexpr Move reverse() const noexcept {
        assert(type() == MT::NORMAL);

        return Move{dst_sq(), org_sq()};
    }

    // Declare static const members (to be defined later)
    static const Move None;
    static const Move Null;

   protected:
    std::uint16_t data;
};

using MT = Move::MT;

// Implementation of the factory method
template<MT T>
inline constexpr Move Move::make(Square orgSq, Square dstSq, PieceType) noexcept {
    static_assert(T != MT::PROMOTION, "Use make<PROMOTION>() for PROMOTION moves");

    return Move((int(T) << TYPE_OFFSET) | (dstSq << DST_SQ_OFFSET) | (orgSq << ORG_SQ_OFFSET));
}
// Specialization for PROMOTION moves
template<>
inline constexpr Move
Move::make<MT::PROMOTION>(Square orgSq, Square dstSq, PieceType promoPt) noexcept {
    assert(KNIGHT <= promoPt && promoPt <= QUEEN);

    return Move((int(MT::PROMOTION) << TYPE_OFFSET) | ((promoPt - KNIGHT) << PROMO_OFFSET)
                | (dstSq << DST_SQ_OFFSET) | (orgSq << ORG_SQ_OFFSET));
}

// **Define the constexpr static members outside the class**
inline constexpr Move Move::None{SQ_A1, SQ_A1};
inline constexpr Move Move::Null{SQ_H8, SQ_H8};

using Moves = std::vector<Move>;

// Bound type for alpha-beta search
enum class Bound : std::uint8_t {
    NONE,
    UPPER,
    LOWER,
    EXACT = UPPER | LOWER
};

// --- Bitmask operators for Bound ---
constexpr Bound operator&(Bound bnd1, Bound bnd2) noexcept {
    return Bound(std::uint8_t(bnd1) & std::uint8_t(bnd2));
}
constexpr Bound operator|(Bound bnd1, Bound bnd2) noexcept {
    return Bound(std::uint8_t(bnd1) | std::uint8_t(bnd2));
}
constexpr Bound operator^(Bound bnd1, Bound bnd2) noexcept {
    return Bound(std::uint8_t(bnd1) ^ std::uint8_t(bnd2));
}
constexpr Bound& operator|=(Bound& bnd1, Bound bnd2) noexcept { return bnd1 = bnd1 | bnd2; }
constexpr Bound& operator&=(Bound& bnd1, Bound bnd2) noexcept { return bnd1 = bnd1 & bnd2; }
constexpr Bound& operator^=(Bound& bnd1, Bound bnd2) noexcept { return bnd1 = bnd1 ^ bnd2; }

constexpr bool is_ok(Bound bound) noexcept {
    return (Bound::UPPER <= bound && bound <= Bound::EXACT);
}

// Keep track of what piece changes on the board by a move
struct DirtyPiece final {
   public:
    constexpr DirtyPiece() noexcept = default;

    Piece  movedPc = Piece::NO_PIECE;         // this is never allowed to be NO_PIECE
    Square orgSq = SQ_NONE, dstSq = SQ_NONE;  // dstSq should be SQ_NONE for promotions

    // if {add, remove}Sq is SQ_NONE, {add, remove}Pc is allowed to be uninitialized
    // castling uses addSq and removeSq to remove and add the rook
    Square removeSq = SQ_NONE, addSq = SQ_NONE;
    Piece  removePc = Piece::NO_PIECE, addPc = Piece::NO_PIECE;
};

// Keep track of what threats (attacks) change on the board by a move
struct DirtyThreat final {
   public:
    static constexpr std::uint8_t SQ_OFFSET            = 0;
    static constexpr std::uint8_t THREATENED_SQ_OFFSET = 8;
    static constexpr std::uint8_t PC_OFFSET            = 16;
    static constexpr std::uint8_t THREATENED_PC_OFFSET = 20;

    DirtyThreat() noexcept {
        // Don't initialize data
    }
    constexpr explicit DirtyThreat(std::uint32_t d) noexcept :
        data(d) {}
    constexpr DirtyThreat(
      Square sq, Square threatenedSq, Piece pc, Piece threatenedPc, bool add) noexcept :
        DirtyThreat((add << 31) | (+threatenedPc << THREATENED_PC_OFFSET) | (+pc << PC_OFFSET)
                    | (threatenedSq << THREATENED_SQ_OFFSET) | (sq << SQ_OFFSET)) {}

    constexpr Square sq() const noexcept {  //
        return Square((data >> SQ_OFFSET) & 0xFF);
    }
    constexpr Square threatened_sq() const noexcept {
        return Square((data >> THREATENED_SQ_OFFSET) & 0xFF);
    }
    constexpr Piece pc() const noexcept {  //
        return Piece((data >> PC_OFFSET) & 0xF);
    }
    constexpr Piece threatened_pc() const noexcept {
        return Piece((data >> THREATENED_PC_OFFSET) & 0xF);
    }
    constexpr bool add() const noexcept { return ((data >> 31) & 0x1) != 0; }

    constexpr std::uint32_t raw() const noexcept { return data; }

   private:
    std::uint32_t data;
};

static_assert(sizeof(DirtyThreat) == 4, "DirtyThreat Size");

// A piece can be involved in at most 8 outgoing attacks and 16 incoming attacks.
// Moving a piece also can reveal at most 8 discovered attacks.
// This implies that a non-castling move can change at most (8 + 16) * 3 + 8 = 80 features.
// By similar logic, a castling move can change at most (5 + 1 + 3 + 9) * 2 = 36 features.
// Thus, 80 should work as an upper bound.
// Finally, 16 entries are added to accommodate unmasked vector stores near the end of the list.
using DirtyThreatList = FixedVector<DirtyThreat, 96>;

// Keep track of all threats (attacks) that change on the board by a move
struct DirtyThreats final {
   public:
    template<bool Add>
    void add(Square sq, Square threatenedSq, Piece pc, Piece threatenedPc) noexcept;

    Color  ac;
    Square kingSq, preKingSq;

    Bitboard threateningBB, threatenedBB;

    DirtyThreatList dtList;
};

// Keep track of all changes on the board by a move
struct DirtyBoard final {
   public:
    DirtyPiece   dp;
    DirtyThreats dts;
};

// Linear Congruential Generator (LCG): X{n+1} = (c + a * X{n})
// Based on a congruential pseudo-random number generator.
constexpr std::uint64_t make_hash(std::uint64_t seed) noexcept {
    return 0x14057B7EF767814FULL + 0x5851F42D4C957F2DULL * seed;
}

template<typename T, typename... Ts>
struct is_all_same final {
    static constexpr bool value = (std::is_same_v<T, Ts> && ...);
};

template<typename... Ts>
constexpr auto is_all_same_v = is_all_same<Ts...>::value;

}  // namespace DON

// Hash function for unordered containers (e.g., std::unordered_set, std::unordered_map).
// Uses make_hash function to produce a unique hash value for move.
template<>
struct std::hash<DON::Move> {
    std::size_t operator()(DON::Move m) const noexcept { return DON::make_hash(m.raw()); }
};

#endif  // #ifndef TYPES_H_INCLUDED

#include "tune.h"  // Global visibility to tuning setup
