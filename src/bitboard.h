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

#include "types.h"

namespace DON {

namespace Bitboards {

void init() noexcept;
#if !defined(NDEBUG)
std::string pretty(Bitboard b) noexcept;
#endif

}  // namespace Bitboards

constexpr inline Bitboard FileABB = 0x0101010101010101ULL;
constexpr inline Bitboard FileBBB = FileABB << (1 * 1);
constexpr inline Bitboard FileCBB = FileABB << (1 * 2);
constexpr inline Bitboard FileDBB = FileABB << (1 * 3);
constexpr inline Bitboard FileEBB = FileABB << (1 * 4);
constexpr inline Bitboard FileFBB = FileABB << (1 * 5);
constexpr inline Bitboard FileGBB = FileABB << (1 * 6);
constexpr inline Bitboard FileHBB = FileABB << (1 * 7);

constexpr inline Bitboard Rank1BB = 0x00000000000000FFULL;
constexpr inline Bitboard Rank2BB = Rank1BB << (8 * 1);
constexpr inline Bitboard Rank3BB = Rank1BB << (8 * 2);
constexpr inline Bitboard Rank4BB = Rank1BB << (8 * 3);
constexpr inline Bitboard Rank5BB = Rank1BB << (8 * 4);
constexpr inline Bitboard Rank6BB = Rank1BB << (8 * 5);
constexpr inline Bitboard Rank7BB = Rank1BB << (8 * 6);
constexpr inline Bitboard Rank8BB = Rank1BB << (8 * 7);

constexpr inline Bitboard EnpassantRankBB = Rank6BB | Rank3BB;
constexpr inline Bitboard PromotionRankBB = Rank8BB | Rank1BB;
constexpr inline Bitboard ColorBB[COLOR_NB]{0x55AA55AA55AA55AAULL, 0xAA55AA55AA55AA55ULL};
constexpr inline Bitboard LowRankBB[COLOR_NB]{Rank2BB | Rank3BB, Rank7BB | Rank6BB};

#if !defined(USE_POPCNT)
constexpr inline std::uint32_t PopCntSize = 1U << 16;
extern std::uint8_t            PopCnt16[PopCntSize];
#endif
extern std::uint8_t SquareDistance[SQUARE_NB][SQUARE_NB];

extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

// Magic holds all magic bitboards relevant data for a single square
struct Magic final {
    Bitboard  mask;
    Bitboard* attacks;
#if !defined(USE_PEXT)
    Bitboard     magic;
    std::uint8_t shift;
#endif

    // Compute the attack's index using the 'magic bitboards' approach
    std::uint16_t index(Bitboard occupied) const noexcept {

#if defined(USE_PEXT)
        return _pext_u64(occupied, mask);
#else
    #if defined(IS_64BIT)
        return ((occupied & mask) * magic) >> shift;
    #else
        auto lo = std::uint32_t(occupied >> 00) & std::uint32_t(mask >> 00);
        auto hi = std::uint32_t(occupied >> 32) & std::uint32_t(mask >> 32);
        return (lo * std::uint32_t(magic >> 00) ^ hi * std::uint32_t(magic >> 32)) >> shift;
    #endif
#endif
    }
};

extern Magic BishopMagics[SQUARE_NB];
extern Magic RookMagics[SQUARE_NB];

constexpr Bitboard square_bb(Square s) noexcept {
    assert(is_ok(s));
    return 1ULL << s;
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
constexpr Bitboard file_bb(File f) noexcept { return FileABB << (1 * f); }
constexpr Bitboard file_bb(Square s) noexcept { return file_bb(file_of(s)); }

constexpr Bitboard operator&(Bitboard b, File f) noexcept { return b & file_bb(f); }
constexpr Bitboard operator|(Bitboard b, File f) noexcept { return b | file_bb(f); }
constexpr Bitboard operator^(Bitboard b, File f) noexcept { return b ^ file_bb(f); }

// Return a bitboard representing all the squares on the given rank.
constexpr Bitboard rank_bb(Rank r) noexcept { return Rank1BB << (8 * r); }
constexpr Bitboard rank_bb(Square s) noexcept { return rank_bb(rank_of(s)); }

constexpr Bitboard operator&(Bitboard b, Rank r) noexcept { return b & rank_bb(r); }
constexpr Bitboard operator|(Bitboard b, Rank r) noexcept { return b | rank_bb(r); }
constexpr Bitboard operator^(Bitboard b, Rank r) noexcept { return b ^ rank_bb(r); }

constexpr bool is_ok_ep(Square epSq) noexcept { return epSq != SQ_NONE && EnpassantRankBB & epSq; }

constexpr bool more_than_one(Bitboard b) noexcept { return b & (b - 1); }

// Returns a bitboard representing an entire line (from board edge to board edge)
// that intersects the two given squares. If the given squares are not on a same
// file/rank/diagonal, the function returns 0. For instance, line_bb(SQ_C4, SQ_F7)
// will return a bitboard with the A2-G8 diagonal.
inline Bitboard line_bb(Square s1, Square s2) noexcept {
    assert(is_ok(s1) && is_ok(s2));
    return LineBB[s1][s2];
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
    return BetweenBB[s1][s2];
}

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
    return SquareDistance[s1][s2];
}

constexpr File edge_distance(File f) noexcept { return std::min(f, File(FILE_H - f)); }
constexpr Rank edge_distance(Rank r) noexcept { return std::min(r, Rank(RANK_8 - r)); }

// Shifts a bitboard one or two steps as specified by the direction D
template<Direction D>
constexpr Bitboard shift(Bitboard b) noexcept {
    // clang-format off
    switch (D)
    {
    case NORTH :      return b << NORTH;
    case SOUTH :      return b >> NORTH;
    case NORTH_2 :    return b << NORTH_2;
    case SOUTH_2 :    return b >> NORTH_2;
    case EAST :       return (b & ~FileHBB) << EAST;
    case WEST :       return (b & ~FileABB) >> EAST;
    case NORTH_WEST : return (b & ~FileABB) << NORTH_WEST;
    case SOUTH_EAST : return (b & ~FileHBB) >> NORTH_WEST;
    case NORTH_EAST : return (b & ~FileHBB) << NORTH_EAST;
    case SOUTH_WEST : return (b & ~FileABB) >> NORTH_EAST;
    default :         return b;
    }
    // clang-format on
}

constexpr Bitboard pawn_push_bb(Color c, Bitboard b) noexcept {
    return c == WHITE ? shift<NORTH>(b) : shift<SOUTH>(b);
}

// Returns the squares attacked by pawns of the given color
// from the squares in the given bitboard.
constexpr Bitboard pawn_attacks_bb(Color c, Bitboard b) noexcept {
    return c == WHITE ? shift<NORTH_WEST>(b) | shift<NORTH_EAST>(b)
                      : shift<SOUTH_WEST>(b) | shift<SOUTH_EAST>(b);
}

inline Bitboard pawn_attacks_bb(Color c, Square s) noexcept {
    assert(is_ok(s));
    return PawnAttacks[c][s];
}

// Returns the pseudo attacks of the given piece type assuming an empty board.
template<PieceType PT>
inline Bitboard attacks_bb(Square s) noexcept {
    assert(PT != PAWN && is_ok(s));
    return PseudoAttacks[PT][s];
}

// Returns the attacks by the given piece type
// assuming the board is occupied according to the passed Bitboard.
// Sliding piece attacks do not continue passed an occupied square.
template<PieceType PT>
inline Bitboard attacks_bb(Square s, Bitboard occupied) noexcept {
    assert(PT != PAWN && is_ok(s));
    switch (PT)
    {
    case BISHOP :
        return BishopMagics[s].attacks[BishopMagics[s].index(occupied)];
    case ROOK :
        return RookMagics[s].attacks[RookMagics[s].index(occupied)];
    case QUEEN :
        return attacks_bb<BISHOP>(s, occupied) | attacks_bb<ROOK>(s, occupied);
    default :
        return PseudoAttacks[PT][s];
    }
}

// Returns the attacks by the given piece type
// assuming the board is occupied according to the passed Bitboard.
// Sliding piece attacks do not continue passed an occupied square.
inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied = 0) noexcept {
    assert(pt != PAWN && is_ok(s));
    switch (pt)
    {
    case BISHOP :
        return attacks_bb<BISHOP>(s, occupied);
    case ROOK :
        return attacks_bb<ROOK>(s, occupied);
    case QUEEN :
        return attacks_bb<BISHOP>(s, occupied) | attacks_bb<ROOK>(s, occupied);
    default :
        return PseudoAttacks[pt][s];
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

// Returns the least significant bit in a non-zero bitboard.
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

    if (b & 0xFFFFFFFF)
    {
        _BitScanForward(&idx, std::uint32_t(b));
        return Square(idx);
    }
    _BitScanForward(&idx, std::uint32_t(b >> 32));
    return Square(idx + 32);

    #endif
#else  // Compiler is neither GCC nor MSVC compatible
    #error "Compiler not supported."
#endif
}

// Returns the most significant bit in a non-zero bitboard.
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

    if (b >> 32)
    {
        _BitScanReverse(&idx, std::uint32_t(b >> 32));
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

// Finds and clears the least significant bit in a non-zero bitboard.
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
