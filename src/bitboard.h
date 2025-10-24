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

#ifndef BITBOARD_H_INCLUDED
#define BITBOARD_H_INCLUDED

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#if !defined(USE_POPCNT)
    #include <cstring>
#endif
#if !defined(NDEBUG)
    #include <string>
#endif

#include "types.h"

#if defined(_MSC_VER)
    #include <intrin.h>  // Microsoft header for _BitScanForward64() && _BitScanForward()
    #if defined(USE_POPCNT)
        #include <nmmintrin.h>  // Microsoft header for _mm_popcnt_u64()
    #endif
#endif

#if defined(USE_BMI2)
    #include <immintrin.h>  // Header for _pext_u64() intrinsic
#endif

namespace DON {

namespace BitBoard {

void init() noexcept;
#if !defined(NDEBUG)
std::string pretty(Bitboard b) noexcept;
#endif

}  // namespace BitBoard

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_B_BB = FILE_A_BB << (1 * 1);
constexpr Bitboard FILE_C_BB = FILE_A_BB << (1 * 2);
constexpr Bitboard FILE_D_BB = FILE_A_BB << (1 * 3);
constexpr Bitboard FILE_E_BB = FILE_A_BB << (1 * 4);
constexpr Bitboard FILE_F_BB = FILE_A_BB << (1 * 5);
constexpr Bitboard FILE_G_BB = FILE_A_BB << (1 * 6);
constexpr Bitboard FILE_H_BB = FILE_A_BB << (1 * 7);

constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;
constexpr Bitboard RANK_2_BB = RANK_1_BB << (8 * 1);
constexpr Bitboard RANK_3_BB = RANK_1_BB << (8 * 2);
constexpr Bitboard RANK_4_BB = RANK_1_BB << (8 * 3);
constexpr Bitboard RANK_5_BB = RANK_1_BB << (8 * 4);
constexpr Bitboard RANK_6_BB = RANK_1_BB << (8 * 5);
constexpr Bitboard RANK_7_BB = RANK_1_BB << (8 * 6);
constexpr Bitboard RANK_8_BB = RANK_1_BB << (8 * 7);

constexpr Bitboard EDGE_FILE_BB       = FILE_A_BB | FILE_H_BB;
constexpr Bitboard PROMOTION_RANK_BB  = RANK_8_BB | RANK_1_BB;
constexpr Bitboard COLOR_BB[COLOR_NB] = {0x55AA55AA55AA55AAULL, 0xAA55AA55AA55AA55ULL};

// Magic holds all magic bitboards relevant data for a single square
struct Magic final {
   public:
    Magic() noexcept                        = default;
    Magic(const Magic&) noexcept            = delete;
    Magic(Magic&&) noexcept                 = delete;
    Magic& operator=(const Magic&) noexcept = delete;
    Magic& operator=(Magic&&) noexcept      = delete;

    Bitboard* attacks;
    Bitboard  mask;
#if !defined(USE_BMI2)
    Bitboard     magic;
    std::uint8_t shift;
#else
    void attacks_bb(Bitboard occupied, Bitboard ref) noexcept { attacks[index(occupied)] = ref; }
#endif

    Bitboard attacks_bb(Bitboard occupied) const noexcept { return attacks[index(occupied)]; }

    // Compute the attack's index using the 'magic bitboards' approach
    std::uint16_t index(Bitboard occupied) const noexcept {

#if defined(USE_BMI2)
        // _pext_u64(Parallel Bits Extract) extracts bits from a 64-bit integer
        // according to a specified mask and compresses them into a contiguous block in the lower bits
        return _pext_u64(occupied, mask);
#else
    #if defined(IS_64BIT)
        return ((occupied & mask) * magic) >> shift;
    #else
        std::uint32_t lo = std::uint32_t(occupied >> 00) & std::uint32_t(mask >> 00);
        std::uint32_t hi = std::uint32_t(occupied >> 32) & std::uint32_t(mask >> 32);
        return (lo * std::uint32_t(magic >> 00) ^ hi * std::uint32_t(magic >> 32)) >> shift;
    #endif
#endif
    }
};

#if !defined(USE_POPCNT)
constexpr unsigned  POPCNT_SIZE = 1U << 16;
extern std::uint8_t PopCnt[POPCNT_SIZE];
#endif

// clang-format off
extern std::uint8_t Distances[SQUARE_NB][SQUARE_NB];

extern Bitboard        Lines[SQUARE_NB][SQUARE_NB];
extern Bitboard     Betweens[SQUARE_NB][SQUARE_NB];
extern Bitboard PieceAttacks[SQUARE_NB][PIECE_TYPE_NB];
extern Magic          Magics[SQUARE_NB][2];  // BISHOP or ROOK
// clang-format on

constexpr Bitboard square_bb(Square s) noexcept {
    assert(is_ok(s));
    return (1ULL << s);
}

// Overloads of bitwise operators between a Bitboard and a Square for testing
// whether a given bit is set in a bitboard, and for setting and clearing bits.
constexpr Bitboard operator&(Bitboard b, Square s) noexcept { return b & square_bb(s); }
constexpr Bitboard operator|(Bitboard b, Square s) noexcept { return b | square_bb(s); }
constexpr Bitboard operator^(Bitboard b, Square s) noexcept { return b ^ square_bb(s); }
constexpr Bitboard operator&(Square s, Bitboard b) noexcept { return b & s; }
constexpr Bitboard operator|(Square s, Bitboard b) noexcept { return b | s; }
constexpr Bitboard operator^(Square s, Bitboard b) noexcept { return b ^ s; }

constexpr Bitboard& operator&=(Bitboard& b, Square s) noexcept { return b = b & s; }
constexpr Bitboard& operator|=(Bitboard& b, Square s) noexcept { return b = b | s; }
constexpr Bitboard& operator^=(Bitboard& b, Square s) noexcept { return b = b ^ s; }

constexpr Bitboard operator|(Square s1, Square s2) noexcept { return square_bb(s1) | s2; }
//constexpr Bitboard operator&(Square s1, Square s2) noexcept { return square_bb(s1) & s2; }
//constexpr Bitboard operator^(Square s1, Square s2) noexcept { return square_bb(s1) ^ s2; }

// Returns a bitboard from a list of squares
template<typename... Squares>
constexpr Bitboard make_bitboard(Squares... squares) noexcept {
    return (square_bb(squares) | ...);
}

// Return a bitboard representing all the squares on the given file.
constexpr Bitboard file_bb(File f) noexcept { return FILE_A_BB << (1 * f); }
constexpr Bitboard file_bb(Square s) noexcept { return file_bb(file_of(s)); }

constexpr Bitboard operator&(Bitboard b, File f) noexcept { return b & file_bb(f); }
constexpr Bitboard operator|(Bitboard b, File f) noexcept { return b | file_bb(f); }
constexpr Bitboard operator^(Bitboard b, File f) noexcept { return b ^ file_bb(f); }

// Return a bitboard representing all the squares on the given rank.
constexpr Bitboard rank_bb(Rank r) noexcept { return RANK_1_BB << (8 * r); }
constexpr Bitboard rank_bb(Square s) noexcept { return rank_bb(rank_of(s)); }

constexpr Bitboard operator&(Bitboard b, Rank r) noexcept { return b & rank_bb(r); }
constexpr Bitboard operator|(Bitboard b, Rank r) noexcept { return b | rank_bb(r); }
constexpr Bitboard operator^(Bitboard b, Rank r) noexcept { return b ^ rank_bb(r); }

constexpr bool more_than_one(Bitboard b) noexcept { return b & (b - 1); }
constexpr bool exactly_one(Bitboard b) noexcept { return b && !more_than_one(b); }

// Returns a bitboard representing an entire line (from board edge to board edge)
// that intersects the two given squares. If the given squares are not on a same
// file/rank/diagonal, the function returns 0. For instance, line_bb(SQ_C4, SQ_F7)
// will return a bitboard with the A2-G8 diagonal.
inline Bitboard line_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return Lines[s1][s2];
}

// Returns a bitboard representing the squares in the semi-open segment
// between the squares s1 and s2 (excluding s1 but including s2).
// If the given squares are not on a same file/rank/diagonal, it returns s2.
// For instance, between_bb(SQ_C4, SQ_F7) will return a bitboard with squares D5, E6 and F7,
// but between_bb(SQ_E6, SQ_F8) will return a bitboard with the square F8.
// This trick allows to generate non-king evasion moves faster:
// the defending piece must either interpose itself to cover the check or capture the checking piece.
inline Bitboard between_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return Betweens[s1][s2];
}
// Returns a bitboard between the squares s1 and s2 (excluding s1 and s2)
inline Bitboard between_ex_bb(Square s1, Square s2) noexcept { return between_bb(s1, s2) ^ s2; }

// Returns true if the squares s1, s2 and s3 are aligned either on a straight or on a diagonal line.
inline bool aligned(Square s1, Square s2, Square s3) noexcept { return line_bb(s1, s2) & s3; }

// Return the distance between x and y, defined as the number of steps for a king in x to reach y.
template<typename T = Square>
inline std::uint8_t distance(Square s1, Square s2) noexcept;

template<>
inline std::uint8_t distance<File>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return std::abs(file_of(s1) - file_of(s2));
}

template<>
inline std::uint8_t distance<Rank>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return std::abs(rank_of(s1) - rank_of(s2));
}

template<>
inline std::uint8_t distance<Square>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return Distances[s1][s2];
}

constexpr File edge_distance(File f) noexcept { return std::min(f, File(FILE_H - f)); }
constexpr Rank edge_distance(Rank r) noexcept { return std::min(r, Rank(RANK_8 - r)); }

// Shifts a bitboard as specified by the direction
template<Direction D>
constexpr Bitboard shift(Bitboard b) noexcept {
    if constexpr (D == NORTH)
        return b << NORTH;
    if constexpr (D == SOUTH)
        return b >> NORTH;
    if constexpr (D == NORTH_2)
        return b << NORTH_2;
    if constexpr (D == SOUTH_2)
        return b >> NORTH_2;
    if constexpr (D == EAST)
        return (b & ~FILE_H_BB) << EAST;
    if constexpr (D == WEST)
        return (b & ~FILE_A_BB) >> EAST;
    if constexpr (D == NORTH_WEST)
        return (b & ~FILE_A_BB) << NORTH_WEST;
    if constexpr (D == SOUTH_EAST)
        return (b & ~FILE_H_BB) >> NORTH_WEST;
    if constexpr (D == NORTH_EAST)
        return (b & ~FILE_H_BB) << NORTH_EAST;
    if constexpr (D == SOUTH_WEST)
        return (b & ~FILE_A_BB) >> NORTH_EAST;
    assert(false);
    return 0;
}

template<Color C>
constexpr Bitboard push_pawn_bb(Bitboard b) noexcept {
    static_assert(is_ok(C), "Invalid color for push_pawn_bb()");
    return shift<pawn_spush(C)>(b);
}
constexpr Bitboard push_pawn_bb(Bitboard b, Color c) noexcept {
    assert(is_ok(c));
    return c == WHITE ? push_pawn_bb<WHITE>(b) : push_pawn_bb<BLACK>(b);
}

// Returns the squares attacked by pawns of the given color from the given bitboard.
template<Color C>
constexpr Bitboard attacks_pawn_bb(Bitboard b) noexcept {
    static_assert(is_ok(C), "Invalid color for attacks_pawn_bb()");
    return shift<(C == WHITE ? NORTH_WEST : SOUTH_WEST)>(b)
         | shift<(C == WHITE ? NORTH_EAST : SOUTH_EAST)>(b);
}
constexpr Bitboard attacks_pawn_bb(Bitboard b, Color c) noexcept {
    assert(is_ok(c));
    return c == WHITE ? attacks_pawn_bb<WHITE>(b) : attacks_pawn_bb<BLACK>(b);
}

// Returns the pseudo attacks of the given piece type assuming an empty board.
template<PieceType PT>
constexpr Bitboard attacks_bb(Square s, Color c = COLOR_NB) noexcept {
    static_assert(is_ok(PT), "Unsupported piece type in attacks_bb()");
    assert(is_ok(s) && (PT != PAWN || c < COLOR_NB));
    if constexpr (PT == PAWN)
        return PieceAttacks[s][c];
    return PieceAttacks[s][PT];
}

template<PieceType PT>
constexpr Bitboard attacks_bb(const Magic (*magic)[2], Bitboard occupied) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in attacks_bb()");
    return (*magic)[PT - BISHOP].attacks_bb(occupied);
}

// Returns the attacks by the given piece type
// assuming the board is occupied according to the passed Bitboard.
// Sliding piece attacks do not continue passed an occupied square.
template<PieceType PT>
constexpr Bitboard attacks_bb(Square s, Bitboard occupied) noexcept {
    static_assert(PT != PAWN, "Unsupported piece type in attacks_bb()");
    assert(is_ok(s));
    if constexpr (PT == KNIGHT)
        return attacks_bb<KNIGHT>(s);
    if constexpr (PT == BISHOP)
        return attacks_bb<BISHOP>(&Magics[s], occupied);
    if constexpr (PT == ROOK)
        return attacks_bb<ROOK>(&Magics[s], occupied);
    if constexpr (PT == QUEEN)
        return attacks_bb<BISHOP>(s, occupied) | attacks_bb<ROOK>(s, occupied);
    if constexpr (PT == KING)
        return attacks_bb<KING>(s);
    assert(false);
    return 0;
}

// Returns the attacks by the given piece type
// assuming the board is occupied according to the passed Bitboard.
// Sliding piece attacks do not continue passed an occupied square.
constexpr Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied = 0) noexcept {
    assert(pt != PAWN);
    assert(is_ok(s));
    switch (pt)
    {
    case KNIGHT :
        return attacks_bb<KNIGHT>(s);
    case BISHOP :
        return attacks_bb<BISHOP>(s, occupied);
    case ROOK :
        return attacks_bb<ROOK>(s, occupied);
    case QUEEN :
        return attacks_bb<QUEEN>(s, occupied);
    case KING :
        return attacks_bb<KING>(s);
    default :
        assert(false);
        return 0;
    }
}

// Counts the number of non-zero bits in the bitboard.
inline std::uint8_t popcount(Bitboard b) noexcept {

#if !defined(USE_POPCNT)
    std::uint16_t b16[4];
    static_assert(sizeof(b16) == sizeof(b));
    std::memcpy(b16, &b, sizeof(b16));
    return PopCnt[b16[0]] + PopCnt[b16[1]] + PopCnt[b16[2]] + PopCnt[b16[3]];
#elif defined(__GNUC__)  // (GCC, Clang, ICX)
    return __builtin_popcountll(b);
#elif defined(_MSC_VER)
    return _mm_popcnt_u64(b);
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation
    // std::uint8_t count = 0;
    // while (b)
    // {
    //     count += (b & 1);
    //     b >>= 1;
    // }
    // return count;
    b = b - ((b >> 1) & 0x5555555555555555ULL);
    b = (b & 0x3333333333333333ULL) + ((b >> 2) & 0x3333333333333333ULL);
    b = (b + (b >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (b * 0x0101010101010101ULL) >> 56;
#endif
}

// Returns the least significant bit in the non-zero bitboard
inline Square lsb(Bitboard b) noexcept {
    assert(b);

#if defined(__GNUC__)  // (GCC, Clang, ICX)
    return Square(__builtin_ctzll(b));
#elif defined(_MSC_VER)
    unsigned long idx;
    #if defined(_WIN64)  // (MSVC-> WIN64)
    _BitScanForward64(&idx, b);
    return Square(idx);
    #else                // (MSVC-> WIN32)
    if (auto bb = std::uint32_t(b); bb)
    {
        _BitScanForward(&idx, bb);
        return Square(idx);
    }
    else
    {
        _BitScanForward(&idx, std::uint32_t(b >> 32));
        return Square(idx + 32);
    }
    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation
    // std::uint8_t idx = 0;
    // while (!(b & 1))
    // {
    //     ++idx;
    //     b >>= 1;
    // }
    // return Square(idx);
    static constexpr std::uint8_t LsbIndices[64]{0,  47, 1,  56, 48, 27, 2,  60,  //
                                                 57, 49, 41, 37, 28, 16, 3,  61,  //
                                                 54, 58, 35, 52, 50, 42, 21, 44,  //
                                                 38, 32, 29, 23, 17, 11, 4,  62,  //
                                                 46, 55, 26, 59, 40, 36, 15, 53,  //
                                                 34, 51, 20, 43, 31, 22, 10, 45,  //
                                                 25, 39, 14, 33, 19, 30, 9,  24,  //
                                                 13, 18, 8,  12, 7,  6,  5,  63};

    return Square(LsbIndices[((b ^ (b - 1)) * 0x03F79D71B4CB0A89ULL) >> 58]);
#endif
}

// Returns the most significant bit in the non-zero bitboard
inline Square msb(Bitboard b) noexcept {
    assert(b);

#if defined(__GNUC__)  // (GCC, Clang, ICX)
    return Square(__builtin_clzll(b) ^ 63);
#elif defined(_MSC_VER)
    unsigned long idx;
    #if defined(_WIN64)  // (MSVC-> WIN64)
    _BitScanReverse64(&idx, b);
    return Square(idx);
    #else                // (MSVC-> WIN32)
    if (auto bb = std::uint32_t(b >> 32); bb)
    {
        _BitScanReverse(&idx, bb);
        return Square(idx + 32);
    }
    else
    {
        _BitScanReverse(&idx, std::uint32_t(b));
        return Square(idx);
    }
    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
    // Using a fallback implementation
    // std::uint8_t idx = 0;
    // while (b >>= 1)
    //     ++idx;
    // return Square(idx);
    static constexpr std::uint8_t MsbIndices[64]{0,  1,  2,  53, 3,  7,  54, 27,  //
                                                 4,  38, 41, 8,  55, 48, 28, 62,  //
                                                 5,  39, 46, 44, 42, 22, 9,  24,  //
                                                 56, 33, 49, 18, 29, 11, 63, 15,  //
                                                 6,  52, 26, 37, 40, 47, 61, 45,  //
                                                 43, 21, 23, 32, 17, 10, 14, 51,  //
                                                 25, 36, 60, 20, 31, 16, 13, 35,  //
                                                 59, 19, 30, 12, 34, 58, 57, 50};

    b |= b >> 1;
    b |= b >> 2;
    b |= b >> 4;
    b |= b >> 8;
    b |= b >> 16;
    b |= b >> 32;
    return Square(MsbIndices[(b * 0x03F79D71B4CB0A89ULL) >> 58]);
#endif
}

// Returns and clears the least significant bit in the non-zero bitboard
inline Square pop_lsb(Bitboard& b) noexcept {
    assert(b);
    Square s = lsb(b);
    b &= b - 1;
    return s;
}

// Returns and clears the most significant bit in the non-zero bitboard
inline Square pop_msb(Bitboard& b) noexcept {
    assert(b);
    Square s = msb(b);
    b ^= s;
    return s;
}

// Returns the bitboard of the least significant square of the non-zero bitboard.
// It is equivalent to square_bb(lsb(bb)).
constexpr Bitboard lsb_square_bb(Bitboard b) noexcept {
    assert(b);
    return b & -b;
}

}  // namespace DON

#endif  // #ifndef BITBOARD_H_INCLUDED
