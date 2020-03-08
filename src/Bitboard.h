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
// BLSR  = Reset lowest set bit
#   if defined(BIT64)
#       define PDEP(b, m)   _pdep_u64(b, m)
#       define PEXT(b, m)   _pext_u64(b, m)
#       define BLSR(b)      _blsr_u64(b)
// #   else
// #       define PDEP(b, m)   _pdep_u32(b, m)
// #       define PEXT(b, m)   _pext_u32(b, m)
// #       define BLSR(b)      _blsr_u32(b)
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

constexpr Array<Bitboard, SQUARES> SquareBB
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

constexpr Array<Bitboard, FILES> FileBB
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

constexpr Array<Bitboard, RANKS> RankBB
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

constexpr Array<Bitboard, COLORS> ColorBB
{
    U64(0x55AA55AA55AA55AA),
    U64(0xAA55AA55AA55AA55)
};

constexpr Array<Bitboard, COLORS> PawnSideBB
{
    RankBB[RANK_2]|RankBB[RANK_3]|RankBB[RANK_4],
    RankBB[RANK_7]|RankBB[RANK_6]|RankBB[RANK_5]
};

constexpr Array<Bitboard, COLORS, RANKS> FrontRankBB
{{
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
}};

extern Array<u08, SQUARES, SQUARES> SquareDistance;

extern Array<Bitboard, SQUARES, SQUARES> LineBB;

extern Array<Bitboard, COLORS, SQUARES> PawnAttackBB;
extern Array<Bitboard, PIECE_TYPES, SQUARES> PieceAttackBB;

extern Array<Magic, SQUARES> BMagics;
extern Array<Magic, SQUARES> RMagics;


/// distance() functions return the distance between s1 and s2, defined as the
/// number of steps for a king in s1 to reach s2.

template<typename T = Square> inline i32 distance(Square, Square);

template<> inline i32 distance<File>(Square s1, Square s2) {
    return std::abs(SFile[s1] - SFile[s2]);
}
template<> inline i32 distance<Rank>(Square s1, Square s2) {
    return std::abs(SRank[s1] - SRank[s2]);
}
template<> inline i32 distance<Square>(Square s1, Square s2) {
    return
        //std::max(distance<File>(s1, s2), distance<Rank>(s1, s2));
        SquareDistance[s1][s2];
}

#if !defined(ABM)

extern Array<u08, 1 << 16> PopCount16;

#endif

constexpr Bitboard operator~(Square s) {
    return ~SquareBB[s];
}

constexpr Bitboard fileBB(Square s) {
    return FileBB[SFile[s]];
}

constexpr Bitboard rankBB(Square s) {
    return RankBB[SRank[s]];
}

constexpr bool contains(Bitboard bb, Square s) {
    return 0 != (bb & SquareBB[s]);
}

constexpr Bitboard operator&(Square s, Bitboard bb) {
    return bb & SquareBB[s];
}
constexpr Bitboard operator|(Square s, Bitboard bb) {
    return bb | SquareBB[s];
}
constexpr Bitboard operator^(Square s, Bitboard bb) {
    return bb ^ SquareBB[s];
}

constexpr Bitboard operator&(Bitboard bb, Square s) {
    return bb & SquareBB[s];
}
constexpr Bitboard operator|(Bitboard bb, Square s) {
    return bb | SquareBB[s];
}
constexpr Bitboard operator^(Bitboard bb, Square s) {
    return bb ^ SquareBB[s];
}

inline Bitboard& operator|=(Bitboard &bb, Square s) {
    return bb |= SquareBB[s];
}
inline Bitboard& operator^=(Bitboard &bb, Square s) {
    return bb ^= SquareBB[s];
}

constexpr Bitboard operator|(Square s1, Square s2) {
    return SquareBB[s1] | SquareBB[s2];
}

inline bool moreThanOne(Bitboard bb) {

#if defined(BM2)
    return 0 != BLSR(bb);
#else
    return 0 != (bb & (bb - 1));
#endif
}

/// Shift the bitboard using delta
template<Direction D>
constexpr Bitboard shift(Bitboard) {
    return 0;
}
template<> constexpr Bitboard shift<NORTH>(Bitboard bb) {
    return (bb) <<  8;
}
template<> constexpr Bitboard shift<SOUTH>(Bitboard bb) {
    return (bb) >>  8;
}
template<> constexpr Bitboard shift<NORTH_2>(Bitboard bb) {
    return (bb) << 16;
}
template<> constexpr Bitboard shift<SOUTH_2>(Bitboard bb) {
    return (bb) >> 16;
}
// If (shifting & 7) != 0 then  bound clipping is done (~FileBB[FILE_A] or ~FileBB[FILE_H])
template<> constexpr Bitboard shift<EAST>(Bitboard bb) {
    return (bb & ~FileBB[FILE_H]) << 1;
}
template<> constexpr Bitboard shift<WEST>(Bitboard bb) {
    return (bb & ~FileBB[FILE_A]) >> 1;
}
template<> constexpr Bitboard shift<NORTH_EAST>(Bitboard bb) {
    return (bb & ~FileBB[FILE_H]) << 9;
}
template<> constexpr Bitboard shift<NORTH_WEST>(Bitboard bb) {
    return (bb & ~FileBB[FILE_A]) << 7;
}
template<> constexpr Bitboard shift<SOUTH_EAST>(Bitboard bb) {
    return (bb & ~FileBB[FILE_H]) >> 7;
}
template<> constexpr Bitboard shift<SOUTH_WEST>(Bitboard bb) {
    return (bb & ~FileBB[FILE_A]) >> 9;
}

/// frontRanksBB() returns ranks in front of the given square
constexpr Bitboard frontRanksBB(Color c, Square s) {
    return FrontRankBB[c][SRank[s]];
}

constexpr Bitboard adjacentFilesBB(Square s) {
    return shift<EAST>(fileBB(s))
         | shift<WEST>(fileBB(s));
}
//constexpr Bitboard adjacentRanksBB(Square s) {
//    return shift<NORTH>(rankBB(s))
//         | shift<SOUTH>(rankBB(s));
//}

constexpr Bitboard frontSquaresBB(Color c, Square s) {
    return frontRanksBB(c, s) & fileBB(s);
}
constexpr Bitboard pawnAttackSpan(Color c, Square s) {
    return frontRanksBB(c, s) & adjacentFilesBB(s);
}
constexpr Bitboard pawnPassSpan(Color c, Square s) {
    return frontRanksBB(c, s) & (fileBB(s) | adjacentFilesBB(s));
}

/// between_bb() returns squares that are linearly between the given squares
/// If the given squares are not on a same file/rank/diagonal, return 0.
inline Bitboard betweenBB(Square s1, Square s2) {

#if defined(BM2)
    return BLSR(LineBB[s1][s2]
              & ((BoardBB << s1) ^ (BoardBB << s2)));
#else
    return LineBB[s1][s2]
         & ((BoardBB << s1) ^ (BoardBB << s2))
         & ~std::min(s1, s2);
#endif
}
/// aligned() Check the squares s1, s2 and s3 are aligned on a straight line.
inline bool aligned(Square s1, Square s2, Square s3) {
    return contains(LineBB[s1][s2], s3);
}

constexpr Array<Direction, COLORS> PawnPush{ NORTH, SOUTH };

template<Color C>
constexpr Bitboard pawnSglPushBB(Bitboard bb) {
    return shift<PawnPush[C]>(bb);
}
//template<Color C>
//constexpr Bitboard pawnDblPushBB(Bitboard bb) {
//    return shift<2 * PawnPush[C]>(bb);
//}

constexpr Array<Direction, COLORS> PawnLAtt{ NORTH_WEST, SOUTH_EAST };
constexpr Array<Direction, COLORS> PawnRAtt{ NORTH_EAST, SOUTH_WEST };

template<Color C>
constexpr Bitboard pawnLAttackBB(Bitboard bb) {
    return shift<PawnLAtt[C]>(bb);
}
template<Color C>
constexpr Bitboard pawnRAttackBB(Bitboard bb) {
    return shift<PawnRAtt[C]>(bb);;
}
/// pawnSglAttackBB() returns the single attackes by pawns of the given color
template<Color C>
constexpr Bitboard pawnSglAttackBB(Bitboard bb) {
    return pawnLAttackBB<C>(bb) | pawnRAttackBB<C>(bb);
}
/// pawnDblAttackBB() returns the double attackes by pawns of the given color
template<Color C>
constexpr Bitboard pawnDblAttackBB(Bitboard bb) {
    return pawnLAttackBB<C>(bb) & pawnRAttackBB<C>(bb);
}

/// attacksBB(s, occ) takes a square and a bitboard of occupied squares,
/// and returns a bitboard representing all squares attacked by PT (Bishop or Rook or Queen) on the given square.
template<PieceType PT> Bitboard attacksBB(Square, Bitboard);
//template<> inline Bitboard attacksBB<NIHT>(Square s, Bitboard) { return PieceAttackBB[NIHT][s]; }
//template<> inline Bitboard attacksBB<KING>(Square s, Bitboard) { return PieceAttackBB[KING][s]; }
/// Attacks of the Bishop with occupancy
template<> inline Bitboard attacksBB<BSHP>(Square s, Bitboard occ) {
    return BMagics[s].attacksBB(occ);
}
/// Attacks of the Rook with occupancy
template<> inline Bitboard attacksBB<ROOK>(Square s, Bitboard occ) {
    return RMagics[s].attacksBB(occ); }
/// Attacks of the Queen with occupancy
template<> inline Bitboard attacksBB<QUEN>(Square s, Bitboard occ) {
    return BMagics[s].attacksBB(occ)
         | RMagics[s].attacksBB(occ);
}

/// Position::attacksBB() finds attacks of the piecetype from the square on occupancy.
inline Bitboard attacksBB(PieceType pt, Square s, Bitboard occ) {
    assert(PAWN != pt);
    switch (pt)
    {
    case BSHP: return attacksBB<BSHP>(s, occ);
    case ROOK: return attacksBB<ROOK>(s, occ);
    case QUEN: return attacksBB<QUEN>(s, occ);
    // case NIHT: case KING:
    default:   return PieceAttackBB[pt][s];
    }
}
/// Position::attacksBB() finds attacks of the piece from the square on occupancy.
inline Bitboard attacksBB(Piece p, Square s, Bitboard occ) {
    auto pt{ PType[p] };
    switch (pt)
    {
    case PAWN: return PawnAttackBB[PColor[p]][s];
    case BSHP: return attacksBB<BSHP>(s, occ);
    case ROOK: return attacksBB<ROOK>(s, occ);
    case QUEN: return attacksBB<QUEN>(s, occ);
    // case NIHT: case KING:
    default:   return PieceAttackBB[pt][s];
    }
}

/// popCount() counts the number of non-zero bits in a bitboard
inline i32 popCount(Bitboard bb) {

#if !defined(ABM) // PopCount Table

    //Bitboard x = bb;
    //x -= (x >> 1) & 0x5555555555555555;
    //x = ((x >> 0) & 0x3333333333333333)
    //  + ((x >> 2) & 0x3333333333333333);
    //x = ((x >> 0) + (x >> 4)) & 0x0F0F0F0F0F0F0F0F;
    //return (x * 0x0101010101010101) >> 56;

    union { Bitboard b; u16 u[4]; } v{ bb };
    return PopCount16[v.u[0]]
         + PopCount16[v.u[1]]
         + PopCount16[v.u[2]]
         + PopCount16[v.u[3]];

#elif defined(_MSC_VER) || defined(__INTEL_COMPILER)

    return i32(_mm_popcnt_u64(bb));

#else // GCC, Clang, ICC or compatible compiler

    return i32(__builtin_popcountll(bb));

#endif

}

/// scanLSq() return the least significant bit in a non-zero bitboard
inline Square scanLSq(Bitboard bb) {
    assert(0 != bb);

#if defined(__GNUC__)   // GCC, Clang, ICC

    return Square(__builtin_ctzll(bb));

#elif defined(_MSC_VER) // MSVC

    unsigned long index;
#   if defined(BIT64)

    _BitScanForward64(&index, bb);

#   else

    if (0 != u32(bb >> 0))
    {
        _BitScanForward(&index, u32(bb >> 0x00));
    }
    else
    {
        _BitScanForward(&index, u32(bb >> 0x20));
        index += 0x20;
    }

#   endif

    return Square(index);

#else // Compiler is neither GCC nor MSVC compatible

    // Assembly code by Heinz van Saanen
    Bitboard index;
    __asm__("bsfq %1, %0": "=r" (index) : "rm" (bb));
    return Square(index);

#endif

}
/// scanLSq() return the most significant bit in a non-zero bitboard
inline Square scanMSq(Bitboard bb) {
    assert(0 != bb);

#if defined(__GNUC__)   // GCC, Clang, ICC

    return Square(__builtin_clzll(bb) ^ i32(SQ_H8));

#elif defined(_MSC_VER) // MSVC

    unsigned long index;
#   if defined(BIT64)

    _BitScanReverse64(&index, bb);

#   else

    if (0 != u32(bb >> 0x20))
    {
        _BitScanReverse(&index, u32(bb >> 0x20));
        index += 0x20;
    }
    else
    {
        _BitScanReverse(&index, u32(bb >> 0x00));
    }

#   endif

    return Square(index);

#else // Compiler is neither GCC nor MSVC compatible

    // Assembly code by Heinz van Saanen

    Bitboard index;
    __asm__("bsrq %1, %0": "=r" (index) : "rm" (bb));
    return Square(index);

#endif
}

// Find the most advanced square in the given bitboard relative to the given color.
inline Square scanFrontMostSq(Color c, Bitboard bb) {
    return WHITE == c ? scanMSq(bb) : scanLSq(bb);
}

inline Square popLSq(Bitboard &bb) {

    Square sq = scanLSq(bb);
#if defined(BM2)
    bb = BLSR(bb);
#else
    bb &= (bb - 1);
#endif
    return sq;
}

///// makeBitboard() returns a bitboard compile-time constructed from a list of squares, files, ranks
//constexpr Bitboard makeBitboard() { return 0; }
//template<typename ...Squares>
//constexpr Bitboard makeBitboard(Square s, Squares... squares) {
//    return U64(0x0000000000000001) << s | makeBitboard(squares...);
//}
//template<typename ...Files>
//constexpr Bitboard makeBitboard(File f, Files... files) {
//    return U64(0x0101010101010101) << f | makeBitboard(files...);
//}
//template<typename ...Ranks>
//constexpr Bitboard makeBitboard(Rank r, Ranks... ranks) {
//    return U64(0x00000000000000FF) << (r * 8) | makeBitboard(ranks...);
//}

///// Rotate Right (toward LSB)
//constexpr Bitboard rotateR(Bitboard bb, i08 k) { return (bb >> k) | (bb << (SQUARES - k)); }
///// Rotate Left  (toward MSB)
//constexpr Bitboard rotateL(Bitboard bb, i08 k) { return (bb << k) | (bb >> (SQUARES - k)); }

namespace BitBoard {

    extern void initialize();

#if !defined(NDEBUG)
    extern std::string toString(Bitboard);
#endif
}
