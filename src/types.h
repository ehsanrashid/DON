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
// -DUSE_PEXT     | Add runtime support for use of pext asm-instruction. Works
//                | only in 64-bit mode and requires hardware with pext support.

    #include <cassert>
    #include <cstdint>

// Predefined macros hell:
//
// __GNUC__                Compiler is GCC, Clang or ICX
// __clang__               Compiler is Clang or ICX
// __INTEL_LLVM_COMPILER   Compiler is ICX
// _MSC_VER                Compiler is MSVC
// _WIN32                  Building on Windows (any)
// _WIN64                  Building on Windows 64 bit

    #if defined(__GNUC__) && (__GNUC__ < 9 || (__GNUC__ == 9 && __GNUC_MINOR__ <= 2)) \
      && defined(_WIN32) && !defined(__clang__)
        #define ALIGNAS_ON_STACK_VARIABLES_BROKEN
    #endif

    #define ASSERT_ALIGNED(ptr, alignment) \
        assert(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0)

    #if defined(_MSC_VER)

        // Disable some silly and noisy warnings from MSVC compiler
        #pragma warning(disable: 4127)  // Conditional expression is constant
        #pragma warning(disable: 4146)  // Unary minus operator applied to unsigned type
        #pragma warning(disable: 4800)  // Forcing value to bool 'true' or 'false'

        #include <intrin.h>  // Microsoft header for __popcnt() & _BitScanForward64()

        #if defined(_WIN64)  // No Makefile used
            #define IS_64BIT
        #endif

        #if defined(USE_POPCNT)
            #include <nmmintrin.h>  // Microsoft header for _mm_popcnt_u64()
        #endif

        #if defined(USE_PREFETCH)
            #include <xmmintrin.h>  // Microsoft header for _mm_prefetch()
        #endif
    #endif

    #if defined(USE_PEXT)
        #include <immintrin.h>  // Header for _pext_u64() intrinsic
    #endif

namespace DON {

using Bitboard = std::uint64_t;
using Key      = std::uint64_t;
using Key16    = std::uint16_t;

constexpr inline std::uint16_t MAX_MOVES = 256;
constexpr inline std::uint16_t MAX_PLY   = 253;

enum Color : std::uint8_t {
    WHITE,
    BLACK,
    COLOR_NB = 2
};

enum CastlingRights : std::uint8_t {
    NO_CASTLING,
    WHITE_OO,
    WHITE_OOO = WHITE_OO << 1,
    BLACK_OO  = WHITE_OO << 2,
    BLACK_OOO = WHITE_OO << 3,

    KING_SIDE      = WHITE_OO | BLACK_OO,
    QUEEN_SIDE     = WHITE_OOO | BLACK_OOO,
    WHITE_CASTLING = WHITE_OO | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO | BLACK_OOO,
    ANY_CASTLING   = WHITE_CASTLING | BLACK_CASTLING,

    CASTLING_RIGHTS_NB = 16
};

enum Bound : std::uint8_t {
    BOUND_NONE,
    BOUND_UPPER,
    BOUND_LOWER,
    BOUND_EXACT = BOUND_UPPER | BOUND_LOWER
};

// clang-format off
enum PieceType : std::int8_t {
    NO_PIECE_TYPE, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    EX_PIECE,
    ALL_PIECE     = NO_PIECE_TYPE,
    MINOR         = BISHOP,
    PIECE_TYPE_NB = 8
};

enum Piece : std::uint8_t {
    NO_PIECE,
    W_PAWN = PAWN,     W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = PAWN + 8, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};
// clang-format on

// Value is used as an alias for std::int32_t, this is done to differentiate between
// a search value and any other integer value. The values used in search are always
// supposed to be in the range (-VALUE_NONE, +VALUE_NONE] and should not exceed this range.
using Value = std::int32_t;

constexpr inline Value VALUE_ZERO     = 0;
constexpr inline Value VALUE_DRAW     = 0;
constexpr inline Value VALUE_NONE     = 32002;
constexpr inline Value VALUE_INFINITE = 32001;

constexpr inline Value VALUE_MATE             = 32000;
constexpr inline Value VALUE_MATES_IN_MAX_PLY = VALUE_MATE - MAX_PLY;
constexpr inline Value VALUE_MATED_IN_MAX_PLY = -VALUE_MATES_IN_MAX_PLY;

constexpr inline Value VALUE_TB                 = VALUE_MATES_IN_MAX_PLY - 1;
constexpr inline Value VALUE_TB_WIN_IN_MAX_PLY  = VALUE_TB - MAX_PLY;
constexpr inline Value VALUE_TB_LOSS_IN_MAX_PLY = -VALUE_TB_WIN_IN_MAX_PLY;

// In the code, make the assumption that these values
// are such that non_pawn_material() can be used to uniquely
// identify the material on the board.
constexpr inline Value VALUE_PAWN   = 208;
constexpr inline Value VALUE_KNIGHT = 781;
constexpr inline Value VALUE_BISHOP = 825;
constexpr inline Value VALUE_ROOK   = 1276;
constexpr inline Value VALUE_QUEEN  = 2538;
// clang-format off
constexpr inline Value PieceValue[PIECE_NB]{
  VALUE_ZERO, VALUE_PAWN, VALUE_KNIGHT, VALUE_BISHOP, VALUE_ROOK, VALUE_QUEEN, VALUE_ZERO, VALUE_ZERO,
  VALUE_ZERO, VALUE_PAWN, VALUE_KNIGHT, VALUE_BISHOP, VALUE_ROOK, VALUE_QUEEN, VALUE_ZERO, VALUE_ZERO};
// clang-format on

using Depth = std::int16_t;

constexpr inline Depth DEPTH_ZERO      = 0;
constexpr inline Depth DEPTH_QS_CHECKS = 0;
constexpr inline Depth DEPTH_QS_NORMAL = -1;
constexpr inline Depth DEPTH_NONE      = -2;
// Depth used only for TT entry occupancy check
constexpr inline Depth DEPTH_OFFSET = DEPTH_NONE - 1;

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
    SQ_ZERO   = SQ_A1,
    SQUARE_NB = 64
};
// clang-format on

enum Direction : std::int8_t {
    NO_DIRECTION,
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

// Keep track of what a move changes on the board (used by NNUE)
struct DirtyPiece final {

    // Number of changed pieces
    int dirtyNum;

    // Max 3 pieces can change in one move.
    // A promotion with capture moves both the pawn and the captured piece to SQ_NONE
    // and the piece promoted to from SQ_NONE to the capture square.
    Piece piece[3];

    // Org and Dst squares, which may be SQ_NONE
    Square org[3];
    Square dst[3];
};

    #define ENABLE_INCR_OPERATORS_ON(T) \
        inline T& operator++(T& t) noexcept { return t = T(int(t) + 1); } \
        inline T& operator--(T& t) noexcept { return t = T(int(t) - 1); }

ENABLE_INCR_OPERATORS_ON(PieceType)
ENABLE_INCR_OPERATORS_ON(File)
ENABLE_INCR_OPERATORS_ON(Rank)
ENABLE_INCR_OPERATORS_ON(Square)

    #undef ENABLE_INCR_OPERATORS_ON

// clang-format off
constexpr Direction operator+(Direction d1, Direction d2) noexcept { return Direction(int(d1) + int(d2)); }
constexpr Direction operator-(Direction d1, Direction d2) noexcept { return Direction(int(d1) - int(d2)); }
//constexpr Direction operator-(Square s1, Square s2) noexcept { return Direction(int(s1) - int(s2)); }
constexpr Direction operator*(int i, Direction d) noexcept { return Direction(i * int(d)); }
// clang-format on

// Additional operators to add a Direction to a Square
constexpr Square operator+(Square s, Direction d) noexcept { return Square(int(s) + int(d)); }
constexpr Square operator-(Square s, Direction d) noexcept { return Square(int(s) - int(d)); }
inline Square&   operator+=(Square& s, Direction d) noexcept { return s = s + d; }
inline Square&   operator-=(Square& s, Direction d) noexcept { return s = s - d; }

// Toggle color
constexpr Color operator~(Color c) noexcept { return Color((c ^ 1) & 1); }

constexpr bool is_ok(PieceType pt) noexcept { return (PAWN <= pt && pt <= KING); }

constexpr bool is_ok(Piece pc) noexcept {
    return (W_PAWN <= pc && pc <= W_KING) || (B_PAWN <= pc && pc <= B_KING);
}

constexpr Piece make_piece(Color c, PieceType pt) noexcept { return Piece((c << 3) | int(pt)); }

constexpr PieceType type_of(Piece pc) noexcept { return PieceType(pc & 7); }

constexpr Color color_of(Piece pc) noexcept {
    assert(is_ok(pc));
    return Color((pc >> 3) & 1);
}
// Swap color of piece B_KNIGHT <-> W_KNIGHT
constexpr Piece operator~(Piece pc) noexcept { return Piece(pc ^ 8); }

constexpr CastlingRights operator&(Color c, CastlingRights cr) noexcept {
    return CastlingRights((c == WHITE ? WHITE_CASTLING : BLACK_CASTLING) & cr);
}

constexpr Value mates_in(std::int16_t ply) noexcept { return +VALUE_MATE - ply; }

constexpr Value mated_in(std::int16_t ply) noexcept { return -VALUE_MATE + ply; }

constexpr bool is_ok(Square s) noexcept { return (SQ_A1 <= s && s <= SQ_H8); }

constexpr Square make_square(File f, Rank r) noexcept { return Square((r << 3) | int(f)); }

constexpr File file_of(Square s) noexcept { return File(s & 0x7); }

constexpr Rank rank_of(Square s) noexcept { return Rank((s >> 3) & 0x7); }

constexpr bool opposite_color(Square s1, Square s2) noexcept {
    return (int(s1) + rank_of(s1) + int(s2) + rank_of(s2)) & 1;
}

// Swap A1 <-> H1
constexpr Square flip_file(Square s) noexcept { return Square(int(s) ^ 0x07); }
// Swap A1 <-> A8
constexpr Square flip_rank(Square s) noexcept { return Square(int(s) ^ 0x38); }

constexpr Square relative_square(Color c, Square s) noexcept { return Square(int(s) ^ (c * 0x38)); }

constexpr Rank relative_rank(Color c, Rank r) noexcept { return Rank(int(r) ^ (c * 0x7)); }

constexpr Rank relative_rank(Color c, Square s) noexcept { return relative_rank(c, rank_of(s)); }

constexpr CastlingRights castling_rights(Color c, Square org, Square dst) noexcept {
    return c & (org < dst ? KING_SIDE : QUEEN_SIDE);
}

constexpr Square king_castle_sq(Color c, Square org, Square dst) noexcept {
    return relative_square(c, org < dst ? SQ_G1 : SQ_C1);
}
constexpr Square rook_castle_sq(Color c, Square org, Square dst) noexcept {
    return relative_square(c, org < dst ? SQ_F1 : SQ_D1);
}

constexpr Direction pawn_spush(Color c) noexcept { return c == WHITE ? NORTH : SOUTH; }
constexpr Direction pawn_dpush(Color c) noexcept { return c == WHITE ? NORTH_2 : SOUTH_2; }

// Based on a congruential pseudo-random number generator
constexpr Key make_key(std::uint64_t seed) noexcept {
    return 0x14057B7EF767814FULL + 0x5851F42D4C957F2DULL * seed;
}

// A move needs 16 bits to be stored
//
// bit  0- 5: destination square (from 0 to 63)
// bit  6-11: origin square (from 0 to 63)
// bit 12-13: promotion piece type - 2 (from KNIGHT-2 to QUEEN-2)
// bit 14-15: special move flag: promotion (1), en-passant (2), castling (3)
// NOTE: en-passant bit is set only when a pawn can be captured
//
// Special cases are Move::None() and Move::Null(). Can sneak these in because in
// any normal move destination square is always different from origin square
// while Move::None() and Move::Null() have the same origin and destination square.
enum MoveType : std::uint16_t {
    NORMAL,
    PROMOTION     = 1 << 14,
    EN_PASSANT    = 2 << 14,
    CASTLING      = 3 << 14,
    MOVETYPE_MASK = CASTLING
};

class Move {
   public:
    Move() = default;
    constexpr explicit Move(std::uint16_t d) noexcept :
        data(d) {}

    constexpr Move(Square org, Square dst) noexcept :
        data(NORMAL | (org << 6) | int(dst)) {}

    template<MoveType T>
    static constexpr Move make(Square org, Square dst, PieceType promo = KNIGHT) noexcept {
        return Move(T | ((promo - KNIGHT) << 12) | (org << 6) | int(dst));
    }

    constexpr Square org_sq() const noexcept { return Square((data >> 6) & 0x3FU); }

    constexpr Square dst_sq() const noexcept { return Square(data & 0x3FU); }

    constexpr std::uint16_t org_dst() const noexcept { return data & 0xFFFU; }

    constexpr MoveType type_of() const noexcept { return MoveType(data & MOVETYPE_MASK); }

    constexpr PieceType promotion_type() const noexcept {
        return PieceType(((data >> 12) & 0x3) + KNIGHT);
    }

    // Catch Move::None() and Move::Null()
    constexpr bool is_ok() const noexcept { return org_sq() != dst_sq(); }

    constexpr Square prev_dst_sq(Color stm) const noexcept {
        return type_of() != CASTLING ? dst_sq() : king_castle_sq(~stm, org_sq(), dst_sq());
    }

    static constexpr Move None() noexcept { return Move(0x00); }
    static constexpr Move Null() noexcept { return Move(0x41); }

    constexpr bool operator==(Move m) const noexcept { return data == m.data; }
    constexpr bool operator!=(Move m) const noexcept { return data != m.data; }

    constexpr explicit operator bool() const noexcept { return data != 0; }

    constexpr auto raw() const noexcept { return data; }
    /*
    constexpr Move mirror() const noexcept { return Move(data ^ 0x0E38U); }

    void set_org_sq(Square org) noexcept { data = (data & 0xF03FU) | (org << 6); }
    void set_dst_sq(Square dst) noexcept { data = (data & 0xFFC0U) | int(dst); }
    void set_promotion_type(PieceType promo = KNIGHT) noexcept {
        data = (data & 0x0FFFU) | PROMOTION | ((promo - KNIGHT) << 12);
    }
    */

    struct MoveHash final {
        Key operator()(Move m) const { return make_key(m.data); }
    };

   protected:
    std::uint16_t data = 0;
};

}  // namespace DON

#endif  // #ifndef TYPES_H_INCLUDED

#include "tune.h"  // Global visibility to tuning setup
