#pragma once

#include "Type.h"

// When no Makefile used

#if defined(_WIN64) && defined(_MSC_VER)
#  include <intrin.h>       // Microsoft Header for _BitScanForward64() & _BitScanReverse64()
#endif

#if defined(ABM) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))
#  include <nmmintrin.h>    // Microsoft and Intel Header for _mm_popcnt_u64() & _mm_popcnt_u32()
#endif

#if defined(BM2)
#   include <immintrin.h>   // Header for BMI2 instructions
// PDEP  = Parallel bits deposit
// PEXT  = Parallel bits extract
#   if defined(BIT64)
//#       define PDEP(b, m)   _pdep_u64(b, m)
#       define PEXT(b, m)   _pext_u64(b, m)
// #   else
// #       define PDEP(b, m)   _pdep_u32(b, m)
// #       define PEXT(b, m)   _pext_u32(b, m)
#   endif
#endif

// Magic holds all magic relevant data for a single square
struct Magic {

    Bitboard *attacks;
    Bitboard  mask;

#if !defined(BM2)
    Bitboard  number;
    u08       shift;
#endif

    u16 index(Bitboard) const;

    Bitboard attacksBB(Bitboard occ) const { return attacks[index(occ)]; }
};

inline u16 Magic::index(Bitboard occ) const {

#if defined(BM2)
    return u16(PEXT(occ, mask));
#elif defined(BIT64)
    return u16(((occ & mask) * number) >> shift);
#else
    return u16((u32((u32(occ >> 0x00) & u32(mask >> 0x00)) * u32(number >> 0x00))
              ^ u32((u32(occ >> 0x20) & u32(mask >> 0x20)) * u32(number >> 0x20))) >> shift);
#endif
}


constexpr Bitboard BoardBB{ U64(0xFFFFFFFFFFFFFFFF) };
//constexpr Bitboard DiagonalBB{ U64(0x8142241818244281) }; // A1..H8 | H1..A8

constexpr Bitboard SquareBB[SQUARES]
{
#   define S_02(n)  U64(1)<<(2*(n)),  U64(1)<<(2*(n)+1)
#   define S_04(n)      S_02(2*(n)),      S_02(2*(n)+1)
#   define S_08(n)      S_04(2*(n)),      S_04(2*(n)+1)
#   define S_16(n)      S_08(2*(n)),      S_08(2*(n)+1)
    S_16(0), S_16(1), S_16(2), S_16(3),
#   undef S_16
#   undef S_08
#   undef S_04
#   undef S_02
};

constexpr Bitboard FileBB[FILES]
{
    U64(0x0101010101010101),
    U64(0x0202020202020202),
    U64(0x0404040404040404),
    U64(0x0808080808080808),
    U64(0x1010101010101010),
    U64(0x2020202020202020),
    U64(0x4040404040404040),
    U64(0x8080808080808080)
};

constexpr Bitboard RankBB[RANKS]
{
    U64(0x00000000000000FF),
    U64(0x000000000000FF00),
    U64(0x0000000000FF0000),
    U64(0x00000000FF000000),
    U64(0x000000FF00000000),
    U64(0x0000FF0000000000),
    U64(0x00FF000000000000),
    U64(0xFF00000000000000)
};

constexpr Bitboard ColorBB[COLORS]
{
    U64(0x55AA55AA55AA55AA),
    U64(0xAA55AA55AA55AA55)
};

constexpr Bitboard PawnSideBB[COLORS]
{
    RankBB[RANK_2]|RankBB[RANK_3]|RankBB[RANK_4],
    RankBB[RANK_7]|RankBB[RANK_6]|RankBB[RANK_5]
};

constexpr Bitboard FrontRankBB[COLORS][RANKS]
{
    {
        RankBB[RANK_8]|RankBB[RANK_7]|RankBB[RANK_6]|RankBB[RANK_5]|RankBB[RANK_4]|RankBB[RANK_3]|RankBB[RANK_2],
        RankBB[RANK_8]|RankBB[RANK_7]|RankBB[RANK_6]|RankBB[RANK_5]|RankBB[RANK_4]|RankBB[RANK_3],
        RankBB[RANK_8]|RankBB[RANK_7]|RankBB[RANK_6]|RankBB[RANK_5]|RankBB[RANK_4],
        RankBB[RANK_8]|RankBB[RANK_7]|RankBB[RANK_6]|RankBB[RANK_5],
        RankBB[RANK_8]|RankBB[RANK_7]|RankBB[RANK_6],
        RankBB[RANK_8]|RankBB[RANK_7],
        RankBB[RANK_8],
        0,
    },
    {
        0,
        RankBB[RANK_1],
        RankBB[RANK_1]|RankBB[RANK_2],
        RankBB[RANK_1]|RankBB[RANK_2]|RankBB[RANK_3],
        RankBB[RANK_1]|RankBB[RANK_2]|RankBB[RANK_3]|RankBB[RANK_4],
        RankBB[RANK_1]|RankBB[RANK_2]|RankBB[RANK_3]|RankBB[RANK_4]|RankBB[RANK_5],
        RankBB[RANK_1]|RankBB[RANK_2]|RankBB[RANK_3]|RankBB[RANK_4]|RankBB[RANK_5]|RankBB[RANK_6],
        RankBB[RANK_1]|RankBB[RANK_2]|RankBB[RANK_3]|RankBB[RANK_4]|RankBB[RANK_5]|RankBB[RANK_6]|RankBB[RANK_7],
    }
};

constexpr Bitboard SlotFileBB[3]
{
    FileBB[FILE_E]|FileBB[FILE_F]|FileBB[FILE_G]|FileBB[FILE_H],    // K-File
    FileBB[FILE_A]|FileBB[FILE_B]|FileBB[FILE_C]|FileBB[FILE_D],    // Q-File
    FileBB[FILE_C]|FileBB[FILE_D]|FileBB[FILE_E]|FileBB[FILE_F]     // C-File
};

extern Bitboard LineBB[SQUARES][SQUARES];

extern Bitboard PawnAttacksBB[COLORS][SQUARES];
extern Bitboard PieceAttacksBB[PIECE_TYPES][SQUARES];

extern Magic BMagics[SQUARES];
extern Magic RMagics[SQUARES];


#if !defined(ABM)

extern u08 PopCount[USHRT_MAX+1]; // 16-bit

#endif

constexpr Bitboard operator~(Square s) { return ~SquareBB[s]; }

constexpr Bitboard fileBB(Square s) { return FileBB[sFile(s)]; }
constexpr Bitboard rankBB(Square s) { return RankBB[sRank(s)]; }
/// frontRanksBB() returns ranks in front of the given square
constexpr Bitboard frontRanksBB(Color c, Square s) { return FrontRankBB[c][sRank(s)]; }

constexpr bool contains(Bitboard bb, Square s) { return (bb & SquareBB[s]) != 0; }

constexpr Bitboard operator&(Square s, Bitboard bb) { return bb & SquareBB[s]; }
constexpr Bitboard operator|(Square s, Bitboard bb) { return bb | SquareBB[s]; }
constexpr Bitboard operator^(Square s, Bitboard bb) { return bb ^ SquareBB[s]; }

constexpr Bitboard operator&(Bitboard bb, Square s) { return bb & SquareBB[s]; }
constexpr Bitboard operator|(Bitboard bb, Square s) { return bb | SquareBB[s]; }
constexpr Bitboard operator^(Bitboard bb, Square s) { return bb ^ SquareBB[s]; }

inline Bitboard& operator|=(Bitboard &bb, Square s) { return bb |= SquareBB[s]; }
inline Bitboard& operator^=(Bitboard &bb, Square s) { return bb ^= SquareBB[s]; }

constexpr Bitboard operator|(Square s1, Square s2) { return SquareBB[s1] | SquareBB[s2]; }

inline bool moreThanOne(Bitboard bb) { return (bb & (bb - 1)) != 0; }

/// Shift the bitboard using delta
template<Direction D> constexpr Bitboard shift(Bitboard) { return 0; }
template<> constexpr Bitboard shift<NORTH     >(Bitboard bb) { return (bb) <<  8; }
template<> constexpr Bitboard shift<SOUTH     >(Bitboard bb) { return (bb) >>  8; }
template<> constexpr Bitboard shift<NORTH_2   >(Bitboard bb) { return (bb) << 16; }
template<> constexpr Bitboard shift<SOUTH_2   >(Bitboard bb) { return (bb) >> 16; }
// If (shifting & 7) != 0 then  bound clipping is done (~FileBB[FILE_A] or ~FileBB[FILE_H])
template<> constexpr Bitboard shift<EAST      >(Bitboard bb) { return (bb & ~FileBB[FILE_H]) << 1; }
template<> constexpr Bitboard shift<WEST      >(Bitboard bb) { return (bb & ~FileBB[FILE_A]) >> 1; }
template<> constexpr Bitboard shift<NORTH_EAST>(Bitboard bb) { return (bb & ~FileBB[FILE_H]) << 9; }
template<> constexpr Bitboard shift<NORTH_WEST>(Bitboard bb) { return (bb & ~FileBB[FILE_A]) << 7; }
template<> constexpr Bitboard shift<SOUTH_EAST>(Bitboard bb) { return (bb & ~FileBB[FILE_H]) >> 7; }
template<> constexpr Bitboard shift<SOUTH_WEST>(Bitboard bb) { return (bb & ~FileBB[FILE_A]) >> 9; }

constexpr Bitboard adjacentFilesBB(Square s) {
    return shift<EAST >(fileBB(s))
         | shift<WEST >(fileBB(s));
}
//constexpr Bitboard adjacentRanksBB(Square s) {
//    return shift<NORTH>(rankBB(s))
//         | shift<SOUTH>(rankBB(s));
//}

constexpr Bitboard frontSquaresBB(Color c, Square s) { return frontRanksBB(c, s) & fileBB(s); }
constexpr Bitboard pawnAttackSpan(Color c, Square s) { return frontRanksBB(c, s) & adjacentFilesBB(s); }
constexpr Bitboard pawnPassSpan  (Color c, Square s) { return frontRanksBB(c, s) & (fileBB(s) | adjacentFilesBB(s)); }

/// lineBB() returns a Bitboard representing an entire line
/// (from board edge to board edge) that intersects the given squares.
/// If the given squares are not on a same file/rank/diagonal, return 0.
/// Ex. lineBB(SQ_C4, SQ_F7) returns a bitboard with the A2-G8 diagonal.
inline Bitboard lineBB(Square s1, Square s2) {
    assert(isOk(s1)
        && isOk(s2));
    return LineBB[s1][s2];
}
/// betweenBB() returns squares that are linearly between the given squares
/// If the given squares are not on a same file/rank/diagonal, return 0.
/// Ex. betweenBB(SQ_C4, SQ_F7) returns a bitboard with squares D5 and E6.
inline Bitboard betweenBB(Square s1, Square s2) {
    Bitboard sLine{ lineBB(s1, s2)
                  & ((BoardBB << s1)
                   ^ (BoardBB << s2)) };
    // Exclude LSB
    return //sLine & ~std::min(s1, s2);
           sLine & (sLine - 1);
}
/// aligned() Check the squares s1, s2 and s3 are aligned on a straight line.
inline bool aligned(Square s1, Square s2, Square s3) {
    assert(isOk(s3));
    return contains(lineBB(s1, s2), s3);
}

constexpr Direction PawnPush[COLORS] { NORTH, SOUTH };

template<Color C> constexpr Bitboard pawnSglPushBB(Bitboard bb) { return shift<PawnPush[C]>(bb); }
template<Color C> constexpr Bitboard pawnDblPushBB(Bitboard bb) { return shift<PawnPush[C] * 2>(bb); }

constexpr Direction PawnLAtt[COLORS] { NORTH_WEST, SOUTH_EAST };
constexpr Direction PawnRAtt[COLORS] { NORTH_EAST, SOUTH_WEST };

template<Color C> constexpr Bitboard pawnLAttackBB(Bitboard bb) { return shift<PawnLAtt[C]>(bb); }
template<Color C> constexpr Bitboard pawnRAttackBB(Bitboard bb) { return shift<PawnRAtt[C]>(bb); }
template<Color C> constexpr Bitboard pawnSglAttackBB(Bitboard bb) { return pawnLAttackBB<C>(bb) | pawnRAttackBB<C>(bb); }
template<Color C> constexpr Bitboard pawnDblAttackBB(Bitboard bb) { return pawnLAttackBB<C>(bb) & pawnRAttackBB<C>(bb); }

inline Bitboard pawnAttacksBB(Color c, Square s) {
    return PawnAttacksBB[c][s];
}

/// attacksBB() returns the pseudo-attacks by piece-type assuming an empty board
template<PieceType PT> inline Bitboard attacksBB(Square s) {
    assert(PT != PAWN);
    return PieceAttacksBB[PT][s];
}

/// attacksBB() returns attacks by piece-type from the square on occupancy
template<PieceType PT> Bitboard attacksBB(Square, Bitboard);

template<> inline Bitboard attacksBB<NIHT>(Square s, Bitboard) {
    return PieceAttacksBB[NIHT][s];
}
template<> inline Bitboard attacksBB<BSHP>(Square s, Bitboard occ) {
    return BMagics[s].attacksBB(occ);
}
template<> inline Bitboard attacksBB<ROOK>(Square s, Bitboard occ) {
    return RMagics[s].attacksBB(occ);
}
template<> inline Bitboard attacksBB<QUEN>(Square s, Bitboard occ) {
    return attacksBB<BSHP>(s, occ)
         | attacksBB<ROOK>(s, occ);
}

/// attacksBB() returns attacks of the piece-type from the square on occupancy
inline Bitboard attacksBB(PieceType pt, Square s, Bitboard occ) {
    assert(NIHT <= pt && pt <= KING);
    return
        pt == NIHT ? attacksBB<NIHT>(s) :
        pt == BSHP ? attacksBB<BSHP>(s, occ) :
        pt == ROOK ? attacksBB<ROOK>(s, occ) :
        pt == QUEN ? attacksBB<QUEN>(s, occ) :
        pt == KING ? attacksBB<KING>(s) : 0;
}

inline Bitboard floodFill(Bitboard b) {
    b &= ~(FileBB[FILE_A]|FileBB[FILE_H]);
    b |= b << 1 | b >> 1;
    return b | shift<SOUTH>(b) | shift<NORTH>(b);
}

/// popCount() counts the number of non-zero bits in a bitboard
inline i32 popCount(Bitboard bb) {

#if defined(ABM)

#   if defined(_MSC_VER) || defined(__INTEL_COMPILER)

    return i32(_mm_popcnt_u64(bb));

#   else // #elif defined(__GNUC__) // GCC, Clang, ICC or compatible compiler

    return i32(__builtin_popcountll(bb));

#   endif

#else // PopCount Table

    //Bitboard x = bb;
    //x -= (x >> 1) & 0x5555555555555555;
    //x = ((x >> 0) & 0x3333333333333333)
    //  + ((x >> 2) & 0x3333333333333333);
    //x = ((x >> 0) + (x >> 4)) & 0x0F0F0F0F0F0F0F0F;
    //return (x * 0x0101010101010101) >> 56;

    union { Bitboard b; u16 u[4]; } v{ bb };
    return PopCount[v.u[0]]
         + PopCount[v.u[1]]
         + PopCount[v.u[2]]
         + PopCount[v.u[3]];

#endif

}

/// scanLSq() return the least significant bit in a non-zero bitboard
inline Square scanLSq(Bitboard bb) {
    assert(bb != 0);

#if defined(__GNUC__) // GCC, Clang, ICC

    return Square(__builtin_ctzll(bb));

#elif defined(_MSC_VER) // MSVC

    unsigned long index;

#   if defined(BIT64)

    _BitScanForward64(&index, bb);
    return Square(index);

#   else

    if (u32(bb >> 0) != 0) {
        _BitScanForward(&index, u32(bb >> 0x00));
        return Square(index);
    }
    else {
        _BitScanForward(&index, u32(bb >> 0x20));
        return Square(index + 0x20);
    }

#   endif

#else // Compiler is neither GCC nor MSVC compatible

    // Assembly code by Heinz van Saanen
    Bitboard sq;
    __asm__("bsfq %1, %0": "=r"(sq) : "rm"(bb));
    return Square(sq);

#endif

}
/// scanLSq() return the most significant bit in a non-zero bitboard
inline Square scanMSq(Bitboard bb) {
    assert(bb != 0);

#if defined(__GNUC__) // GCC, Clang, ICC

    return Square(SQ_H8 - __builtin_clzll(bb));

#elif defined(_MSC_VER) // MSVC

    unsigned long index;

#   if defined(BIT64)

    _BitScanReverse64(&index, bb);
    return Square(index);

#   else

    if (u32(bb >> 0x20) != 0) {
        _BitScanReverse(&index, u32(bb >> 0x20));
        return Square(index + 0x20);
    }
    else {
        _BitScanReverse(&index, u32(bb >> 0x00));
        return Square(index);
    }

#   endif

#else // Compiler is neither GCC nor MSVC compatible

    // Assembly code by Heinz van Saanen

    Bitboard sq;
    __asm__("bsrq %1, %0": "=r"(sq) : "rm"(bb));
    return Square(sq);

#endif
}

// Find the most advanced square in the given bitboard relative to the given color.
template<Color C> inline Square scanFrontMostSq(Bitboard) { return 0; }
template<> inline Square scanFrontMostSq<WHITE>(Bitboard bb) { assert(bb != 0); return scanMSq(bb); }
template<> inline Square scanFrontMostSq<BLACK>(Bitboard bb) { assert(bb != 0); return scanLSq(bb); }

inline Square popLSq(Bitboard &bb) {
    assert(bb != 0);
    Square sq{ scanLSq(bb) };
    bb &= (bb - 1); // bb &= ~(1ULL << sq);
    return sq;
}
//inline Square popMSq(Bitboard &bb) {
//    assert(bb != 0);
//    Square sq{ scanMSq(bb) };
//    bb &= ~(1ULL << sq);
//    return sq;
//}

namespace BitBoard {

    extern void initialize();

#if !defined(NDEBUG)

    extern std::string toString(Bitboard);

#endif

}
