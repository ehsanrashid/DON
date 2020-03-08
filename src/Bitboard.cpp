#include "Bitboard.h"

#include <bitset>
#include <memory>
#include <sstream>
#include <vector>

#include "PRNG.h"
#include "Notation.h"

Array<u08, SQUARES, SQUARES> SquareDistance;

Array<Bitboard, SQUARES, SQUARES> LineBB;

Array<Bitboard, COLORS, SQUARES> PawnAttackBB;
Array<Bitboard, PIECE_TYPES, SQUARES> PieceAttackBB;

Array<Magic, SQUARES> BMagics;
Array<Magic, SQUARES> RMagics;

#if !defined(ABM)

Array<u08, 1 << 16> PopCount16;

/*
// Counts the non-zero bits using SWAR-Popcount algorithm
u08 popCount16(u32 u) {
    u -= (u >> 1) & 0x5555U;
    u = ((u >> 2) & 0x3333U) + (u & 0x3333U);
    u = ((u >> 4) + u) & 0x0F0FU;
    return u08((u * 0x0101U) >> 8);
}
*/

#endif

namespace {

    Bitboard slideAttacks(Square s, Array<Direction, 4> const &directions, Bitboard occ = 0) {

        Bitboard attacks{ 0 };
        for (Direction dir : directions) {

            Square sq{ s + dir };
            while (isOk(sq)
                && 1 == distance(sq, sq - dir)) {

                attacks |= sq;
                if (contains(occ, sq)) {
                    break;
                }

                sq += dir;
            }
        }
        return attacks;
    }

    // Max Bishop Table Size
    // 4 * 2^6 + 12 * 2^7 + 44 * 2^5 + 4 * 2^9
    // 4 *  64 + 12 * 128 + 44 *  32 + 4 * 512
    //     256 +     1536 +     1408 +    2048 = 5248
    Bitboard BAttacks[0x1480];

    // Max Rook Table Size
    // 4 * 2^12 + 24 * 2^11 + 36 * 2^10
    // 4 * 4096 + 24 * 2048 + 36 * 1024
    //    16384 +     49152 +     36864 = 102400
    Bitboard RAttacks[0x19000];

    /// Initialize all bishop and rook attacks at startup.
    /// Magic bitboards are used to look up attacks of sliding pieces.
    /// In particular, here we use the so called "fancy" approach.
#if !defined(NDEBUG)
    void initializeMagic(
        Bitboard *attacks,
        Array<Magic, SQUARES> &magics,
        Array<Direction, 4> const &directions,
        u08 maxMaskPopCount)
#else
    void initializeMagic(
        Bitboard *attacks,
        Array<Magic, SQUARES> &magics,
        Array<Direction, 4> const &directions)
#endif
    {

#if !defined(BM2)

        constexpr u16 MaxIndex{ 0x1000 };
        Array<Bitboard, MaxIndex> occupancy;
        Array<Bitboard, MaxIndex> reference;

        constexpr Array<u32, RANKS> Seeds
#   if defined(BIT64)
        { 0x002D8, 0x0284C, 0x0D6E5, 0x08023, 0x02FF9, 0x03AFC, 0x04105, 0x000FF };
#   else
        { 0x02311, 0x0AE10, 0x0D447, 0x09856, 0x01663, 0x173E5, 0x199D0, 0x0427C };
#   endif

#endif
        u16 size{ 0 };
        for (Square s = SQ_A1; s <= SQ_H8; ++s) {

            Magic &magic{ magics[s] };

            // Board edges are not considered in the relevant occupancies
            Bitboard edge = ((FileBB[FILE_A]|FileBB[FILE_H]) & ~fileBB(s))
                          | ((RankBB[RANK_1]|RankBB[RANK_8]) & ~rankBB(s));

            // Given a square, the mask is the bitboard of sliding attacks from
            // computed on an empty board. The index must be big enough to contain
            // all the attacks for each possible subset of the mask and so is 2 power
            // the number of 1s of the mask. Hence deduce the size of the shift to
            // apply to the 64 or 32 bits word to get the index.
            magic.mask = slideAttacks(s, directions, 0) & ~edge;
            assert(popCount(magic.mask) <= maxMaskPopCount);

            // magics[s].attacks is a pointer to the beginning of the attacks table for the square
            // Set the offset for the attacks table of the square.
            // For each square got individual table sizes with "Fancy Magic Bitboards".
            // new Bitboard[1 << popCount(magic.mask)];
            magic.attacks = SQ_A1 == s ? attacks : magics[s - 1].attacks + size;

#if !defined(BM2)

            u08 bits
#   if defined(BIT64)
            { 64 };
#   else
            { 32 };
#   endif

            magic.shift = bits - popCount(magic.mask);
#endif
            size = 0;
            // Use Carry-Rippler trick to enumerate all subsets of magic.mask
            // Store the corresponding slide attack bitboard in reference[].
            Bitboard occ{ 0 };
            do {

#if defined(BM2)
                magic.attacks[PEXT(occ, magic.mask)] = slideAttacks(s, directions, occ);
#else
                occupancy[size] = occ;
                reference[size] = slideAttacks(s, directions, occ);
#endif
                ++size;
                occ = (occ - magic.mask) & magic.mask;
            } while (0 != occ);

            assert(size == 1 << popCount(magic.mask));

#if !defined(BM2)

            PRNG prng{ Seeds[SRank[s]] };
            // Find a magic for square picking up an (almost) random number
            // until found the one that passes the verification test.
            for (u16 i = 0; i < size; ) {

                magic.number = 0;
                while (6 > popCount((magic.mask * magic.number) >> 56)) {
                    magic.number = prng.sparseRand<Bitboard>();
                }

                // A good magic must map every possible occupancy to an index that
                // looks up the correct slide attack in the magics[s].attacks database.
                // Note that build up the database for square as a side effect of verifying the magic.
                std::vector<bool> used(size, false);
                for (i = 0; i < size; ++i) {

                    u16 idx{ magic.index(occupancy[i]) };
                    assert(idx < size);

                    if (used[idx]) {
                        if (magic.attacks[idx] != reference[i]) {
                            break;
                        }
                    }
                    else {
                        used[idx] = true;
                        magic.attacks[idx] = reference[i];
                    }
                }
            }
#endif
        }
    }
}

namespace BitBoard {

    void initialize() {

        //for (Square s = SQ_A1; s <= SQ_H8; ++s) {
        //    SquareBB[s] = U64(1) << s;
        //}

        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1) {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2) {
                SquareDistance[s1][s2] = u08(std::max(distance<File>(s1, s2), distance<Rank>(s1, s2)));
                assert(0 <= SquareDistance[s1][s2]
                    && 7 >= SquareDistance[s1][s2]);
            }
        }

#if !defined(ABM)

        for (u32 i = 0; i < PopCount16.size(); ++i) {
            PopCount16[i] = std::bitset<16>(i).count(); //pop_count16(i);
        }

#endif

        // Pawn and Pieces Attack Table
        for (Square s = SQ_A1; s <= SQ_H8; ++s) {
            PawnAttackBB[WHITE][s] = pawnSglAttackBB<WHITE>(SquareBB[s]);
            PawnAttackBB[BLACK][s] = pawnSglAttackBB<BLACK>(SquareBB[s]);
            assert(2 >= popCount(PawnAttackBB[WHITE][s])
                && 2 >= popCount(PawnAttackBB[BLACK][s]));

            for (auto dir : { SOUTH_2_WEST, SOUTH_2_EAST, WEST_2_SOUTH, EAST_2_SOUTH,
                              WEST_2_NORTH, EAST_2_NORTH, NORTH_2_WEST, NORTH_2_EAST }) {
                Square sq{ s + dir };
                if (isOk(sq)
                 && 2 == distance(s, sq)) {
                    PieceAttackBB[NIHT][s] |= sq;
                }
            }
            for (auto dir : { SOUTH_WEST, SOUTH, SOUTH_EAST, WEST,
                              EAST, NORTH_WEST, NORTH, NORTH_EAST }) {
                Square sq{ s + dir };
                if (isOk(sq)
                 && 1 == distance(s, sq)) {
                    PieceAttackBB[KING][s] |= sq;
                }
            }
        }

        // Initialize Magic Table
#if !defined(NDEBUG)
        initializeMagic(BAttacks, BMagics, { SOUTH_WEST, SOUTH_EAST, NORTH_WEST, NORTH_EAST }, 9);
        initializeMagic(RAttacks, RMagics, { SOUTH     , WEST      , EAST      , NORTH      }, 12);
#else
        initializeMagic(BAttacks, BMagics, { SOUTH_WEST, SOUTH_EAST, NORTH_WEST, NORTH_EAST });
        initializeMagic(RAttacks, RMagics, { SOUTH     , WEST      , EAST      , NORTH      });
#endif

        // NOTE:: must be after initialize Bishop & Rook Table
        for (Square s = SQ_A1; s <= SQ_H8; ++s) {

            PieceAttackBB[BSHP][s] = attacksBB<BSHP>(s, 0);
            PieceAttackBB[ROOK][s] = attacksBB<ROOK>(s, 0);
            PieceAttackBB[QUEN][s] = PieceAttackBB[BSHP][s]
                                   | PieceAttackBB[ROOK][s];
        }

        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1) {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2) {

                LineBB[s1][s2] = 0;

                if (s1 != s2) {
                    for (PieceType pt : { BSHP, ROOK }) {
                        if (contains(PieceAttackBB[pt][s1], s2)) {

                            LineBB[s1][s2] = (PieceAttackBB[pt][s1]
                                            & PieceAttackBB[pt][s2])
                                           | s1 | s2;
                        }
                    }
                }
            }
        }

    }

#if !defined(NDEBUG)

    /// Returns an ASCII representation of a bitboard to print on console output
    /// Bitboard in an easily readable format. This is sometimes useful for debugging.
    std::string toString(Bitboard bb) {
        std::ostringstream oss;

        oss << " /---------------\\\n";
        for (Rank r = RANK_8; r >= RANK_1; --r) {
            oss << r << '|';
            for (File f = FILE_A; f <= FILE_H; ++f) {
                oss << (contains(bb, makeSquare(f, r)) ? '+' : '-');
                if (f < FILE_H) {
                    oss << ' ';
                }
            }
            oss << "|\n";
        }
        oss << " \\---------------/\n ";
        for (File f = FILE_A; f <= FILE_H; ++f) {
            oss << ' ' << toChar(f, false);
        }
        oss << '\n';

        return oss.str();
    }

#endif

}
