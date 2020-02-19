#include "Bitboard.h"

#include <bitset>
#include <memory>
#include <vector>

#include "PRNG.h"
#include "Notation.h"

using namespace std;

Array<Bitboard, COLORS, SQUARES> PawnAttacks;
Array<Bitboard, PIECE_TYPES, SQUARES> PieceAttacks;
Array<Bitboard, SQUARES, SQUARES> Lines;

Array<Magic, SQUARES> BMagics
    ,                 RMagics;

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

Array<u08, SQUARES, SQUARES> SquareDistance;


namespace {

    constexpr Array<Direction, PIECE_TYPES, 8> PieceDirections
    {{
        {}, {},
        { SOUTH_2_WEST, SOUTH_2_EAST, WEST_2_SOUTH, EAST_2_SOUTH, WEST_2_NORTH, EAST_2_NORTH, NORTH_2_WEST, NORTH_2_EAST },
        { SOUTH_WEST, SOUTH_EAST, NORTH_WEST, NORTH_EAST },
        { SOUTH , WEST , EAST , NORTH  },
        { SOUTH_WEST, SOUTH , SOUTH_EAST, WEST, EAST, NORTH_WEST, NORTH, NORTH_EAST },
        { SOUTH_WEST, SOUTH , SOUTH_EAST, WEST, EAST, NORTH_WEST, NORTH, NORTH_EAST },
    }};

    template<PieceType PT>
    Bitboard slideAttacks(Square s, Bitboard occ = 0) {
        static_assert (BSHP == PT || ROOK == PT || QUEN == PT, "PT incorrect");

        Bitboard attacks{ 0 };
        for (auto dir : PieceDirections[PT]) {
            for (Square sq = s + dir;
                isOk(sq) && 1 == dist(sq, sq - dir);
                sq += dir) {
                attacks |= sq;
                if (contains(occ, sq)) {
                    break;
                }
            }
        }
        return attacks;
    }
    /// Explicit template instantiations
    /// --------------------------------
    template Bitboard slideAttacks<BSHP>(Square, Bitboard);
    template Bitboard slideAttacks<ROOK>(Square, Bitboard);
    //template Bitboard slideAttacks<QUEN>(Square, Bitboard);

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
    template<PieceType PT>
    void initializeMagic(Bitboard *attacks, Array<Magic, SQUARES> &magics) {
        static_assert (BSHP == PT || ROOK == PT, "PT incorrect");

#   if !defined(BM2)
        constexpr u16 MaxIndex{ 0x1000 };
        Array<Bitboard, MaxIndex> occupancy
            ,                     reference;

        constexpr Array<u32, RANKS> Seeds
#       if defined(BIT64)
        { 0x002D8, 0x0284C, 0x0D6E5, 0x08023, 0x02FF9, 0x03AFC, 0x04105, 0x000FF };
#       else
        { 0x02311, 0x0AE10, 0x0D447, 0x09856, 0x01663, 0x173E5, 0x199D0, 0x0427C };
#       endif

#   endif

        u32 offset{ 0 };
        for (Square s = SQ_A1; s <= SQ_H8; ++s) {
            auto &magic = magics[s];

            // Given a square, the mask is the bitboard of sliding attacks from
            // computed on an empty board. The index must be big enough to contain
            // all the attacks for each possible subset of the mask and so is 2 power
            // the number of 1s of the mask. Hence deduce the size of the shift to
            // apply to the 64 or 32 bits word to get the index.
            magic.mask = PieceAttacks[PT][s]
                        // Board edges are not considered in the relevant occupancies
                       & ~(((FABB|FHBB) & ~fileBB(s)) | ((R1BB|R8BB) & ~rankBB(s)));

            u08 maskPopCount = u08(popCount(magic.mask));
            assert(maskPopCount < 32);

            // magics[s].attacks is a pointer to the beginning of the attacks table for square
            //magic.attacks = new Bitboard[(1U << maskPopCount)];
            magic.attacks = &attacks[offset];

#       if !defined(BM2)
#           if defined(BIT64)
            u08 bits{ 64 };
#           else
            u08 bits{ 32 };
#           endif
            magic.shift = bits - maskPopCount;

            u16 size{ 0 };
#       endif

            // Use Carry-Rippler trick to enumerate all subsets of magics[s].mask
            // Have individual table sizes for each square with "Fancy Magic Bitboards".
            Bitboard occ{ 0 };
            do {
#           if defined(BM2)
                magic.attacks[PEXT(occ, magic.mask)] = slideAttacks<PT>(s, occ);
#           else
                occupancy[size] = occ;
                // Store the corresponding slide attack bitboard in reference[].
                reference[size] = slideAttacks<PT>(s, occ);
                ++size;
#           endif

                occ = (occ - magic.mask) & magic.mask;
            } while (0 != occ);

#       if !defined(BM2)

            assert(size == (1U << maskPopCount));

            PRNG prng{ Seeds[sRank(s)] };

            u16 i;
            // Find a magic for square picking up an (almost) random number
            // until found the one that passes the verification test.
            do {
                do {
                    magic.number = prng.sparseRand<Bitboard>();
                } while (popCount((magic.mask * magic.number) >> 0x38) < 6);

                // A good magic must map every possible occupancy to an index that
                // looks up the correct slide attack in the magics[s].attacks database.
                // Note that build up the database for square as a side effect of verifying the magic.
                vector<bool> used(size, false);
                for (i = 0; i < size; ++i) {
                    u16 idx{ magic.index(occupancy[i]) };
                    assert(idx < size);
                    if (used[idx]) {
                        if (magic.attacks[idx] != reference[i]) {
                            break;
                        }
                        continue;
                    }
                    used[idx] = true;
                    magic.attacks[idx] = reference[i];
                }
            } while (i < size);
#       endif

            offset += (1U << maskPopCount);
        }
    }
    /// Explicit template instantiations
    /// --------------------------------
    template void initializeMagic<BSHP>(Bitboard*, Array<Magic, SQUARES>&);
    template void initializeMagic<ROOK>(Bitboard*, Array<Magic, SQUARES>&);

}

namespace BitBoard {

    void initialize() {

        //for (Square s = SQ_A1; s <= SQ_H8; ++s) {
        //    Squares[s] = U64(1) << s;
        //}
#   if !defined(ABM)
        for (u32 i = 0; i < PopCount16.size(); ++i) {
            PopCount16[i] = std::bitset<16>(i).count(); //pop_count16(i);
        }
#   endif

        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1) {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2) {
                SquareDistance[s1][s2] = u08(std::max(dist<File>(s1, s2), dist<Rank>(s1, s2)));
                assert(0 <= SquareDistance[s1][s2]
                    && 7 >= SquareDistance[s1][s2]);
            }
        }
        // Pawn and Pieces Attack Table
        for (Square s = SQ_A1; s <= SQ_H8; ++s) {
            PawnAttacks[WHITE][s] = pawnSglAttacks<WHITE>(squareBB(s));
            PawnAttacks[BLACK][s] = pawnSglAttacks<BLACK>(squareBB(s));
            assert(2 >= popCount(PawnAttacks[WHITE][s])
                && 2 >= popCount(PawnAttacks[BLACK][s]));

            for (auto dir : PieceDirections[NIHT]) {
                Square sq{ s + dir };
                if (isOk(sq)
                 && 2 == dist(s, sq)) {
                    PieceAttacks[NIHT][s] |= sq;
                }
            }
            for (auto dir : PieceDirections[KING]) {
                Square sq{ s + dir };
                if (isOk(sq)
                 && 1 == dist(s, sq)) {
                    PieceAttacks[KING][s] |= sq;
                }
            }
            PieceAttacks[BSHP][s] = slideAttacks<BSHP>(s);
            PieceAttacks[ROOK][s] = slideAttacks<ROOK>(s);
            PieceAttacks[QUEN][s] = PieceAttacks[BSHP][s]
                                  | PieceAttacks[ROOK][s];
        }

        // Initialize Magic Table
        initializeMagic<BSHP>(BAttacks, BMagics);
        initializeMagic<ROOK>(RAttacks, RMagics);

        // NOTE:: must be after initialize Bishop & Rook Table
        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1) {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2) {
                Lines[s1][s2] = 0;
                if (s1 != s2) {
                    for (PieceType pt : { BSHP, ROOK }) {
                        if (contains(PieceAttacks[pt][s1], s2)) {
                            Lines[s1][s2] = (PieceAttacks[pt][s1] & PieceAttacks[pt][s2]) | s1 | s2;
                        }
                    }
                }
            }
        }

    }

#if !defined(NDEBUG)
    /// Returns an ASCII representation of a bitboard to print on console output
    /// Bitboard in an easily readable format. This is sometimes useful for debugging.
    string toString(Bitboard bb) {
        ostringstream oss;

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
