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
#include <string>
#if defined(_MSC_VER)
    #include <intrin.h>  // Microsoft header for __popcnt() & _BitScanForward64()

    #if defined(USE_POPCNT)
        #include <nmmintrin.h>  // Microsoft header for _mm_popcnt_u64()
    #endif
#endif
#if defined(USE_PEXT)
    #include <immintrin.h>  // Header for _pext_u64() intrinsic
#endif

#include "types.h"

namespace DON {

namespace BitBoard {

void init() noexcept;
#if !defined(NDEBUG)
std::string pretty(Bitboard b) noexcept;
#endif

}  // namespace BitBoard

constexpr inline Bitboard FILE_A_BB = 0x0101010101010101ull;
constexpr inline Bitboard FILE_B_BB = FILE_A_BB << (1 * 1);
constexpr inline Bitboard FILE_C_BB = FILE_A_BB << (1 * 2);
constexpr inline Bitboard FILE_D_BB = FILE_A_BB << (1 * 3);
constexpr inline Bitboard FILE_E_BB = FILE_A_BB << (1 * 4);
constexpr inline Bitboard FILE_F_BB = FILE_A_BB << (1 * 5);
constexpr inline Bitboard FILE_G_BB = FILE_A_BB << (1 * 6);
constexpr inline Bitboard FILE_H_BB = FILE_A_BB << (1 * 7);

constexpr inline Bitboard RANK_1_BB = 0x00000000000000FFull;
constexpr inline Bitboard RANK_2_BB = RANK_1_BB << (8 * 1);
constexpr inline Bitboard RANK_3_BB = RANK_1_BB << (8 * 2);
constexpr inline Bitboard RANK_4_BB = RANK_1_BB << (8 * 3);
constexpr inline Bitboard RANK_5_BB = RANK_1_BB << (8 * 4);
constexpr inline Bitboard RANK_6_BB = RANK_1_BB << (8 * 5);
constexpr inline Bitboard RANK_7_BB = RANK_1_BB << (8 * 6);
constexpr inline Bitboard RANK_8_BB = RANK_1_BB << (8 * 7);

constexpr inline Bitboard ENPASSANT_RANK_BB = RANK_6_BB | RANK_3_BB;
constexpr inline Bitboard PROMOTION_RANK_BB = RANK_8_BB | RANK_1_BB;
constexpr inline Bitboard EDGE_FILE_BB      = FILE_A_BB | FILE_H_BB;
constexpr inline Bitboard COLOR_BB[COLOR_NB]{0x55AA55AA55AA55AAull, 0xAA55AA55AA55AA55ull};
constexpr inline Bitboard LOW_RANK_BB[COLOR_NB]{RANK_2_BB | RANK_3_BB, RANK_7_BB | RANK_6_BB};

// Magic holds all magic bitboards relevant data for a single square
struct Magic final {
   public:
    Magic() noexcept                        = default;
    Magic(const Magic&) noexcept            = delete;
    Magic(Magic&&) noexcept                 = delete;
    Magic& operator=(const Magic&) noexcept = delete;
    Magic& operator=(Magic&&) noexcept      = delete;

    Bitboard  mask    = 0;
    Bitboard* attacks = nullptr;
#if !defined(USE_PEXT)
    Bitboard     magic = 0;
    std::uint8_t shift = 0;
#else
    void attacks_bb(Bitboard occupied, Bitboard ref) noexcept { attacks[index(occupied)] = ref; }
#endif

    Bitboard attacks_bb(Bitboard occupied) const noexcept { return attacks[index(occupied)]; }

    // Compute the attack's index using the 'magic bitboards' approach
    std::uint16_t index(Bitboard occupied) const noexcept {

#if defined(USE_PEXT)
        // _pext_u64(Parallel Bits Extract) extracts bits from a 64-bit integer
        // according to a specified mask and compresses them into a contiguous block in the lower bits
        return _pext_u64(occupied, mask);
#else
    #if defined(IS_64BIT)
        return (occupied & mask) * magic >> shift;
    #else
        auto lo = std::uint32_t(occupied) & std::uint32_t(mask);
        auto hi = std::uint32_t(occupied >> 32) & std::uint32_t(mask >> 32);
        return (lo * std::uint32_t(magic) ^ hi * std::uint32_t(magic >> 32)) >> shift;
    #endif
#endif
    }
};

#if !defined(USE_POPCNT)
constexpr inline std::uint32_t POPCNT_SIZE = 1 << 16;
extern std::uint8_t            PopCnt16[POPCNT_SIZE];
#endif
// clang-format off
extern std::uint8_t Distances[SQUARE_NB][SQUARE_NB];

extern Bitboard         Lines[SQUARE_NB][SQUARE_NB];
extern Bitboard      Betweens[SQUARE_NB][SQUARE_NB];
extern Bitboard   PawnAttacks[SQUARE_NB][COLOR_NB];
extern Bitboard  PieceAttacks[SQUARE_NB][PIECE_TYPE_NB - 3];
extern Magic           Magics[SQUARE_NB][2];  // BISHOP or ROOK
// clang-format on

constexpr Bitboard square_bb(Square s) noexcept {
    assert(is_ok(s));
    return 1ull << s;
}

// Overloads of bitwise operators between a Bitboard and a Square for testing
// whether a given bit is set in a bitboard, and for setting and clearing bits.
constexpr Bitboard operator&(Bitboard b, Square s) noexcept { return b & square_bb(s); }
constexpr Bitboard operator|(Bitboard b, Square s) noexcept { return b | square_bb(s); }
constexpr Bitboard operator^(Bitboard b, Square s) noexcept { return b ^ square_bb(s); }
constexpr Bitboard operator&(Square s, Bitboard b) noexcept { return b & s; }
constexpr Bitboard operator|(Square s, Bitboard b) noexcept { return b | s; }
constexpr Bitboard operator^(Square s, Bitboard b) noexcept { return b ^ s; }

inline Bitboard& operator&=(Bitboard& b, Square s) noexcept { return b = b & s; }
inline Bitboard& operator|=(Bitboard& b, Square s) noexcept { return b = b | s; }
inline Bitboard& operator^=(Bitboard& b, Square s) noexcept { return b = b ^ s; }

constexpr Bitboard operator|(Square s1, Square s2) noexcept { return square_bb(s1) | s2; }

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

constexpr bool ep_is_ok(Square epSq) noexcept {
    return epSq != SQ_NONE && ENPASSANT_RANK_BB & epSq;
}

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
inline Bitboard ex_between_bb(Square s1, Square s2) noexcept { return between_bb(s1, s2) ^ s2; }

// Returns true if the squares s1, s2 and s3 are aligned
// either on a straight or on a diagonal line.
inline bool aligned(Square s1, Square s2, Square s3) noexcept { return line_bb(s1, s2) & s3; }

// Return the distance between x and y, defined as the
// number of steps for a king in x to reach y.
template<typename T = Square>
inline std::uint8_t distance(Square s1, Square s2) noexcept;

template<>
constexpr std::uint8_t distance<File>(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return std::abs(file_of(s1) - file_of(s2));
}

template<>
constexpr std::uint8_t distance<Rank>(Square s1, Square s2) noexcept {
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
constexpr Bitboard shift(Direction dir, Bitboard b) noexcept {
    // clang-format off
    switch (dir)
    {
    case NORTH :      return b << NORTH;
    case SOUTH :      return b >> NORTH;
    case NORTH_2 :    return b << NORTH_2;
    case SOUTH_2 :    return b >> NORTH_2;
    case EAST :       return (b & ~FILE_H_BB) << EAST;
    case WEST :       return (b & ~FILE_A_BB) >> EAST;
    case NORTH_WEST : return (b & ~FILE_A_BB) << NORTH_WEST;
    case SOUTH_EAST : return (b & ~FILE_H_BB) >> NORTH_WEST;
    case NORTH_EAST : return (b & ~FILE_H_BB) << NORTH_EAST;
    case SOUTH_WEST : return (b & ~FILE_A_BB) >> NORTH_EAST;
    default :         return b;
    }
    // clang-format on
}

constexpr Bitboard pawn_push_bb(Color c, Bitboard b) noexcept { return shift(pawn_spush(c), b); }

// Returns the squares attacked by pawns of the given color
// from the squares in the given bitboard.
constexpr Bitboard pawn_attacks_bb(Color c, Bitboard b) noexcept {
    return c == WHITE ? shift(NORTH_WEST, b) | shift(NORTH_EAST, b)
                      : shift(SOUTH_WEST, b) | shift(SOUTH_EAST, b);
}

template<Color C>
constexpr Bitboard pawn_attacks_bb(Square s) noexcept {
    assert(is_ok(s));
    return PawnAttacks[s][C];
}

constexpr Bitboard pawn_attacks_bb(Color c, Square s) noexcept {
    assert(is_ok(s));
    switch (c)
    {
    case WHITE :
        return pawn_attacks_bb<WHITE>(s);
    case BLACK :
        return pawn_attacks_bb<BLACK>(s);
    default :
        return 0;
    }
}

// Returns the pseudo attacks of the given piece type assuming an empty board.
template<PieceType PT>
constexpr Bitboard attacks_bb(Square s) noexcept {
    static_assert(PT != PAWN, "Unsupported piece type in attacks_bb()");
    assert(is_ok(s));
    return PieceAttacks[s][PT - 2];
}

template<PieceType PT>
constexpr Bitboard attacks_bb(const Magic (*magic)[2], Bitboard occupied = 0) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in attacks_bb()");
    return (*magic)[PT == ROOK].attacks_bb(occupied);
}

// Returns the attacks by the given piece type
// assuming the board is occupied according to the passed Bitboard.
// Sliding piece attacks do not continue passed an occupied square.
template<PieceType PT>
constexpr Bitboard attacks_bb(Square s, Bitboard occupied) noexcept {
    static_assert(PT != PAWN, "Unsupported piece type in attacks_bb()");
    assert(is_ok(s));
    switch (PT)
    {
    case KNIGHT :
        return attacks_bb<KNIGHT>(s);
    case BISHOP :
        return attacks_bb<BISHOP>(&Magics[s], occupied);
    case ROOK :
        return attacks_bb<ROOK>(&Magics[s], occupied);
    case QUEEN :
        return attacks_bb<BISHOP>(s, occupied) | attacks_bb<ROOK>(s, occupied);
    case KING :
        return attacks_bb<KING>(s);
    default :
        return 0;
    }
}

// Returns the attacks by the given piece type
// assuming the board is occupied according to the passed Bitboard.
// Sliding piece attacks do not continue passed an occupied square.
constexpr Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied = 0) noexcept {
    assert(pt != PAWN && is_ok(s));
    switch (pt)
    {
    case KNIGHT :
        return attacks_bb<KNIGHT>(s);
    case BISHOP :
        return attacks_bb<BISHOP>(s, occupied);
    case ROOK :
        return attacks_bb<ROOK>(s, occupied);
    case QUEEN :
        return attacks_bb<BISHOP>(s, occupied) | attacks_bb<ROOK>(s, occupied);
    case KING :
        return attacks_bb<KING>(s);
    default :
        return 0;
    }
}

// Counts the number of non-zero bits in a bitboard.
inline std::uint8_t popcount(Bitboard b) noexcept {

#if !defined(USE_POPCNT)

    union {
        Bitboard      bb;
        std::uint16_t u[4];
    } v = {b};
    return PopCnt16[v.u[0]] + PopCnt16[v.u[1]] + PopCnt16[v.u[2]] + PopCnt16[v.u[3]];

#elif defined(_MSC_VER)
    #if defined(_WIN64)
    return _mm_popcnt_u64(b);
    #else
    return __popcnt(std::uint32_t(b)) + __popcnt(std::uint32_t(b >> 32));
    #endif
#else  // Assumed gcc or compatible compiler

    return __builtin_popcountll(b);

#endif
}

// Returns the least significant bit in a non-zero bitboard
inline Square lsb(Bitboard b) noexcept {
    assert(b);

#if defined(__GNUC__)  // GCC, Clang, ICX

    return Square(__builtin_ctzll(b));

#elif defined(_MSC_VER)
    #if defined(_WIN64)  // MSVC, WIN64

    unsigned long idx;
    _BitScanForward64(&idx, b);
    return Square(idx);

    #else  // MSVC, WIN32

    unsigned long idx;

    if (std::uint32_t bb = std::uint32_t(b); bb)
    {
        _BitScanForward(&idx, bb);
        return Square(idx);
    }
    _BitScanForward(&idx, std::uint32_t(b >> 32));
    return Square(idx + 32);

    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
#endif
}

// Returns the most significant bit in a non-zero bitboard
inline Square msb(Bitboard b) noexcept {
    assert(b);

#if defined(__GNUC__)  // GCC, Clang, ICX

    return Square(__builtin_clzll(b) ^ 0x3F);

#elif defined(_MSC_VER)
    #if defined(_WIN64)  // MSVC, WIN64

    unsigned long idx;
    _BitScanReverse64(&idx, b);
    return Square(idx);

    #else  // MSVC, WIN32

    unsigned long idx;

    if (std::uint32_t bb = b >> 32; bb)
    {
        _BitScanReverse(&idx, bb);
        return Square(idx + 32);
    }
    _BitScanReverse(&idx, std::uint32_t(b));
    return Square(idx);

    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
#endif
}

// Returns the bitboard of the least significant square of a non-zero bitboard.
// It is equivalent to square_bb(lsb(bb)).
constexpr Bitboard lsb_square_bb(Bitboard b) noexcept {
    assert(b);
    return b & -b;
}

// Finds and clears the least significant bit in a non-zero bitboard
inline Square pop_lsb(Bitboard& b) noexcept {
    assert(b);
    Square s = lsb(b);
    b &= b - 1;
    return s;
}

inline Square pop_msb(Bitboard& b) noexcept {
    assert(b);
    Square s = msb(b);
    b ^= s;
    return s;
}

}  // namespace DON

#endif  // #ifndef BITBOARD_H_INCLUDED
