#include "BitBoard.h"

#include <bitset>
#include <cmath>
#include <memory>

#include "PRNG.h"
#include "Notation.h"

namespace BitBoard {

    using namespace std;

    array<array<Bitboard, SQ_NO>, CLR_NO> PawnAttacks;
    array<array<Bitboard, SQ_NO>, NONE> PieceAttacks;

    array<array<Bitboard, SQ_NO>, SQ_NO> Line_bb;

    array<Magic, SQ_NO> BMagics
        ,               RMagics;

#if !defined(ABM)

    array<u08, 1 << 16> PopCount16;

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

        constexpr array<array<Delta, 8>, NONE> PieceDeltas
        {{
            { },
            { DEL_SSW, DEL_SSE, DEL_WWS, DEL_EES, DEL_WWN, DEL_EEN, DEL_NNW, DEL_NNE },
            { DEL_SW, DEL_SE, DEL_NW, DEL_NE },
            { DEL_S , DEL_W , DEL_E , DEL_N  },
            { DEL_SW, DEL_S , DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE },
            { DEL_SW, DEL_S , DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE },
        }};
/*
        // De Bruijn sequences.
#   if defined(BIT64)
        constexpr u64 DeBruijn_64 = U64(0x3F79D71B4CB0A89);
#   else
        constexpr u32 DeBruijn_32 = U32(0x783A9B23);
#   endif

        array<Square, SQ_NO> BSF_Table;
        unsigned bsf_index(Bitboard bb)
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

        array<u08, (1 << 8)> MSB_Table;
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
        void initialize_magic(Bitboard *attacks, array<Magic, SQ_NO> &magics)
        {
            static_assert (BSHP == PT || ROOK == PT, "PT incorrect");

#       if !defined(BM2)
            //             Max Index
            array<Bitboard, 0x1000> occupancy
                ,                   reference;

            constexpr array<u32, R_NO> Seeds
#           if defined(BIT64)
                { 0x002D8, 0x0284C, 0x0D6E5, 0x08023, 0x02FF9, 0x03AFC, 0x04105, 0x000FF };
#           else
                { 0x02311, 0x0AE10, 0x0D447, 0x09856, 0x01663, 0x173E5, 0x199D0, 0x0427C };
#           endif

#       endif

            u32 offset = 0;
            for (const auto s : SQ)
            {
                auto &magic = magics[s];

                // Given a square, the mask is the bitboard of sliding attacks from
                // computed on an empty board. The index must be big enough to contain
                // all the attacks for each possible subset of the mask and so is 2 power
                // the number of 1s of the mask. Hence deduce the size of the shift to
                // apply to the 64 or 32 bits word to get the index.
                magic.mask = PieceAttacks[PT][s]
                            // Board edges are not considered in the relevant occupancies
                           & ~(((FA_bb|FH_bb) & ~file_bb(s)) | ((R1_bb|R8_bb) & ~rank_bb(s)));

                auto mask_popcount = pop_count(magic.mask);
                assert(mask_popcount < 32);

                // magics[s].attacks is a pointer to the beginning of the attacks table for square
                //magic.attacks = new Bitboard[(1U << mask_popcount)];
                magic.attacks = &attacks[offset];

#           if !defined(BM2)
                auto bits =
#                   if defined(BIT64)
                        64
#                   else
                        32
#                   endif
                    ;
                magic.shift = bits - mask_popcount;

                u16 size = 0;
#           endif

                // Use Carry-Rippler trick to enumerate all subsets of magics[s].mask
                // Have individual table sizes for each square with "Fancy Magic Bitboards".
                Bitboard occ = 0;
                do
                {
#               if defined(BM2)
                    magic.attacks[PEXT(occ, magic.mask)] = slide_attacks<PT>(s, occ);
#               else
                    occupancy[size] = occ;
                    // Store the corresponding slide attack bitboard in reference[].
                    reference[size] = slide_attacks<PT>(s, occ);
                    ++size;
#               endif

                    occ = (occ - magic.mask) & magic.mask;
                } while (0 != occ);

#           if !defined(BM2)

                assert(size == (1U << mask_popcount));

                PRNG prng{Seeds[_rank(s)]};

                u16 i;
                // Find a magic for square picking up an (almost) random number
                // until found the one that passes the verification test.
                do
                {
                    do
                    {
                        magic.number = prng.sparse_rand<Bitboard>();
                    } while (pop_count((magic.mask * magic.number) >> 0x38) < 6);

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

                offset += (1U << mask_popcount);
            }
        }
        /// Explicit template instantiations
        /// --------------------------------
        template void initialize_magic<BSHP>(Bitboard*, array<Magic, SQ_NO>&);
        template void initialize_magic<ROOK>(Bitboard*, array<Magic, SQ_NO>&);
    }

    template<PieceType PT>
    Bitboard slide_attacks(Square s, Bitboard occ)
    {
        static_assert (BSHP == PT || ROOK == PT || QUEN == PT, "PT incorrect");

        Bitboard attacks = 0;
        for (auto del : PieceDeltas[PT])
        {
            for (auto sq = s + del;
                 _ok(sq) && 1 == dist(sq, sq - del);
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
    template Bitboard slide_attacks<BSHP>(Square, Bitboard);
    template Bitboard slide_attacks<ROOK>(Square, Bitboard);
    template Bitboard slide_attacks<QUEN>(Square, Bitboard);

    void initialize()
    {
        //for (const auto s : SQ)
        //{
        //    //Square_bb[s] = U64(1) << s;
        //    BSF_Table[bsf_index(square_bb(s))] = s;
        //}
        //for (u32 b = 1; b < MSB_Table.size(); ++b)
        //{
        //    MSB_Table[b] =  MSB_Table[b - 1] + !more_than_one(b);
        //}

#   if !defined(ABM)
        for (u32 i = 0; i < PopCount16.size(); ++i)
        {
            PopCount16[i] = std::bitset<16>(i).count(); //pop_count16(i);
        }
#   endif

        // Pawn and Pieces Attack Table
        for (const auto s : SQ)
        {
            for (const auto c : { WHITE, BLACK })
            {
                PawnAttacks[c][s] |= pawn_sgl_attacks_bb(c, square_bb(s));
                assert(2 >= pop_count(PawnAttacks[c][s]));
            }

            for (auto del : PieceDeltas[NIHT])
            {
                auto sq = s + del;
                if (   _ok(sq)
                    && 2 == dist(s, sq))
                {
                    PieceAttacks[NIHT][s] |= sq;
                }
            }
            for (auto del : PieceDeltas[KING])
            {
                auto sq = s + del;
                if (   _ok(sq)
                    && 1 == dist(s, sq))
                {
                    PieceAttacks[KING][s] |= sq;
                }
            }
            PieceAttacks[BSHP][s] = slide_attacks<BSHP>(s);
            PieceAttacks[ROOK][s] = slide_attacks<ROOK>(s);
            PieceAttacks[QUEN][s] = PieceAttacks[BSHP][s]
                                  | PieceAttacks[ROOK][s];
        }

        // Initialize Magic Table
        initialize_magic<BSHP>(BAttacks, BMagics);
        initialize_magic<ROOK>(RAttacks, RMagics);

        // NOTE:: must be after initialize Bishop & Rook Table
        for (const auto s1 : SQ)
        {
            for (const auto s2 : SQ)
            {
                Line_bb[s1][s2] = 0;
                if (s1 != s2)
                {
                    for (const auto pt : { BSHP, ROOK })
                    {
                        if (contains(PieceAttacks[pt][s1], s2))
                        {
                            Line_bb[s1][s2] = (PieceAttacks[pt][s1] & PieceAttacks[pt][s2]) | s1 | s2;
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
        for (const auto r : { R_8, R_7, R_6, R_5, R_4, R_3, R_2, R_1 })
        {
            oss << to_char(r) << '|';
            for (const auto f : { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H })
            {
                oss << (contains(bb, f|r) ? '+' : '-');
                if (f < F_H)
                {
                    oss << ' ';
                }
            }
            oss << "|\n";
        }
        oss << " \\---------------/\n ";
        for (const auto f : { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H })
        {
            oss << ' ' << to_char(f, false);
        }
        oss << '\n';

        return oss.str();
    }

#endif

}
