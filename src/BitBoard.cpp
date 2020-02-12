#include "BitBoard.h"

#include <bitset>
#include <memory>
#if !defined(BM2)
#   include <vector>
#endif

#include "PRNG.h"
#include "Notation.h"

namespace BitBoard {

    using namespace std;

    Table<Bitboard, COLORS, SQUARES>   PawnAttacks;
    Table<Bitboard, NONE, SQUARES>     PieceAttacks;

    Table<Bitboard, SQUARES, SQUARES>    Lines;

    Table<Magic, SQUARES> BMagics
        ,               RMagics;

#if !defined(ABM)

    Table<u08, 1 << 16> PopCount16;

    /*
    // Counts the non-zero bits using SWAR-Popcount algorithm
    u08 pop_count16(u32 u)
    {
        u -= (u >> 1) & 0x5555U;
        u = ((u >> 2) & 0x3333U) + (u & 0x3333U);
        u = ((u >> 4) + u) & 0x0F0FU;
        return u08((u * 0x0101U) >> 8);
    }
    */
#endif

    namespace {

        constexpr Table<Direction, NONE, 8> PieceDeltas
        {{
            { },
            { SOUTH_2_WEST, SOUTH_2_EAST, WEST_2_SOUTH, EAST_2_SOUTH, WEST_2_NORTH, EAST_2_NORTH, NORTH_2_WEST, NORTH_2_EAST },
            { SOUTH_WEST, SOUTH_EAST, NORTH_WEST, NORTH_EAST },
            { SOUTH , WEST , EAST , NORTH  },
            { SOUTH_WEST, SOUTH , SOUTH_EAST, WEST, EAST, NORTH_WEST, NORTH, NORTH_EAST },
            { SOUTH_WEST, SOUTH , SOUTH_EAST, WEST, EAST, NORTH_WEST, NORTH, NORTH_EAST },
        }};
/*
        // De Bruijn sequences.
#   if defined(BIT64)
        constexpr u64 DeBruijn_64 = U64(0x3F79D71B4CB0A89);
#   else
        constexpr u32 DeBruijn_32 = U32(0x783A9B23);
#   endif

        Table<Square, SQUARES> BSFTable;
        unsigned bsfIndex(Bitboard bb)
        {
            assert(0 != bb);
            bb ^= (bb - 1);
            return
#       if defined(BIT64)
            // Use Kim Walisch extending trick for 64-bit
            (bb * DeBruijn_64) >> 58;
#       else
            // Use Matt Taylor's folding trick for 32-bit
            (u32 ((bb >> 0) ^ (bb >> 32)) * DeBruijn_32) >> 26;
#       endif
        }

        Table<u08, 1 << 8> MSBTable;
*/

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
        void initializeMagic(Bitboard *attacks, Table<Magic, SQUARES> &magics)
        {
            static_assert (BSHP == PT || ROOK == PT, "PT incorrect");

#       if !defined(BM2)
            //             Max Index
            Table<Bitboard, 0x1000> occupancy
                ,                   reference;

            constexpr Table<u32, RANKS> Seeds
#           if defined(BIT64)
                { 0x002D8, 0x0284C, 0x0D6E5, 0x08023, 0x02FF9, 0x03AFC, 0x04105, 0x000FF };
#           else
                { 0x02311, 0x0AE10, 0x0D447, 0x09856, 0x01663, 0x173E5, 0x199D0, 0x0427C };
#           endif

#       endif

            u32 offset = 0;
            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                auto &magic = magics[s];

                // Given a square, the mask is the bitboard of sliding attacks from
                // computed on an empty board. The index must be big enough to contain
                // all the attacks for each possible subset of the mask and so is 2 power
                // the number of 1s of the mask. Hence deduce the size of the shift to
                // apply to the 64 or 32 bits word to get the index.
                magic.mask = PieceAttacks[PT][s]
                            // Board edges are not considered in the relevant occupancies
                           & ~(((FABB|FHBB) & ~fileBB(s)) | ((R1BB|R8BB) & ~rankBB(s)));

                auto maskPopCount = popCount(magic.mask);
                assert(maskPopCount < 32);

                // magics[s].attacks is a pointer to the beginning of the attacks table for square
                //magic.attacks = new Bitboard[(1U << maskPopCount)];
                magic.attacks = &attacks[offset];

#           if !defined(BM2)
                auto bits =
#                   if defined(BIT64)
                        64
#                   else
                        32
#                   endif
                    ;
                magic.shift = bits - maskPopCount;

                u16 size = 0;
#           endif

                // Use Carry-Rippler trick to enumerate all subsets of magics[s].mask
                // Have individual table sizes for each square with "Fancy Magic Bitboards".
                Bitboard occ = 0;
                do
                {
#               if defined(BM2)
                    magic.attacks[PEXT(occ, magic.mask)] = slideAttacks<PT>(s, occ);
#               else
                    occupancy[size] = occ;
                    // Store the corresponding slide attack bitboard in reference[].
                    reference[size] = slideAttacks<PT>(s, occ);
                    ++size;
#               endif

                    occ = (occ - magic.mask) & magic.mask;
                } while (0 != occ);

#           if !defined(BM2)

                assert(size == (1U << maskPopCount));

                PRNG prng{Seeds[sRank(s)]};

                u16 i;
                // Find a magic for square picking up an (almost) random number
                // until found the one that passes the verification test.
                do
                {
                    do
                    {
                        magic.number = prng.sparseRand<Bitboard>();
                    } while (popCount((magic.mask * magic.number) >> 0x38) < 6);

                    // A good magic must map every possible occupancy to an index that
                    // looks up the correct slide attack in the magics[s].attacks database.
                    // Note that build up the database for square as a side effect of verifying the magic.
                    vector<bool> used(size, false);
                    for (i = 0; i < size; ++i)
                    {
                        u16 idx = magic.index(occupancy[i]);
                        assert(idx < size);
                        if (used[idx])
                        {
                            if (magic.attacks[idx] != reference[i])
                            {
                                break;
                            }
                            continue;
                        }
                        used[idx] = true;
                        magic.attacks[idx] = reference[i];
                    }
                } while (i < size);
#           endif

                offset += (1U << maskPopCount);
            }
        }
        /// Explicit template instantiations
        /// --------------------------------
        template void initializeMagic<BSHP>(Bitboard*, Table<Magic, SQUARES>&);
        template void initializeMagic<ROOK>(Bitboard*, Table<Magic, SQUARES>&);
    }

    template<PieceType PT>
    Bitboard slideAttacks(Square s, Bitboard occ)
    {
        static_assert (BSHP == PT || ROOK == PT || QUEN == PT, "PT incorrect");

        Bitboard attacks = 0;
        for (auto del : PieceDeltas[PT])
        {
            for (auto sq = s + del;
                 isOk(sq) && 1 == dist(sq, sq - del);
                 sq += del)
            {
                attacks |= sq;
                if (contains(occ, sq))
                {
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
    template Bitboard slideAttacks<QUEN>(Square, Bitboard);

    void initialize()
    {
        //for (Square s = SQ_A1; s <= SQ_H8; ++s)
        //{
        //    //Squares[s] = U64(1) << s;
        //    BSFTable[bsfIndex(squareBB(s))] = s;
        //}
        //for (u32 b = 1; b < MSBTable.size(); ++b)
        //{
        //    MSBTable[b] =  MSBTable[b - 1] + !moreThanOne(b);
        //}

#   if !defined(ABM)
        for (u32 i = 0; i < PopCount16.size(); ++i)
        {
            PopCount16[i] = std::bitset<16>(i).count(); //pop_count16(i);
        }
#   endif

        // Pawn and Pieces Attack Table
        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            for (Color c : { WHITE, BLACK })
            {
                PawnAttacks[c][s] |= pawnSglAttacks(c, squareBB(s));
                assert(2 >= popCount(PawnAttacks[c][s]));
            }

            for (auto del : PieceDeltas[NIHT])
            {
                auto sq = s + del;
                if (   isOk(sq)
                    && 2 == dist(s, sq))
                {
                    PieceAttacks[NIHT][s] |= sq;
                }
            }
            for (auto del : PieceDeltas[KING])
            {
                auto sq = s + del;
                if (   isOk(sq)
                    && 1 == dist(s, sq))
                {
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
        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                Lines[s1][s2] = 0;
                if (s1 != s2)
                {
                    for (auto pt : { BSHP, ROOK })
                    {
                        if (contains(PieceAttacks[pt][s1], s2))
                        {
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
    string pretty(Bitboard bb)
    {
        ostringstream oss;

        oss << " /---------------\\\n";
        for (Rank r = RANK_8; r >= RANK_1; --r)
        {
            oss << toChar(r) << '|';
            for (File f = FILE_A; f <= FILE_H; ++f)
            {
                oss << (contains(bb, makeSquare(f, r)) ? '+' : '-');
                if (f < FILE_H)
                {
                    oss << ' ';
                }
            }
            oss << "|\n";
        }
        oss << " \\---------------/\n ";
        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            oss << ' ' << toChar(f, false);
        }
        oss << '\n';

        return oss.str();
    }

#endif

}
