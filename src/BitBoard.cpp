#include "BitBoard.h"

#include <cstring>
#include <algorithm>

#include "BitCount.h"
#include "BitScan.h"
#include "RKISS.h"

namespace BitBoard {

    using namespace std;

    // FRONT SQUARES
    CACHE_ALIGN(64) Bitboard FrontSqs_bb[CLR_NO][SQ_NO];

    CACHE_ALIGN(64) Bitboard Between_bb[SQ_NO][SQ_NO];
    CACHE_ALIGN(64) Bitboard LineRay_bb[SQ_NO][SQ_NO];

    CACHE_ALIGN (64) Bitboard DistanceRings[SQ_NO][F_NO];

    // Span of the attacks of pawn
    CACHE_ALIGN(64) Bitboard PawnAttackSpan[CLR_NO][SQ_NO];

    // Path of the passed pawn
    CACHE_ALIGN(64) Bitboard PasserPawnSpan[CLR_NO][SQ_NO];

    // Attacks of the pawns
    CACHE_ALIGN(64) Bitboard PawnAttacks[CLR_NO][SQ_NO];

    // Attacks of the pieces
    CACHE_ALIGN(64) Bitboard PieceAttacks[NONE][SQ_NO];

    CACHE_ALIGN(64) Bitboard*BAttack_bb[SQ_NO];
    CACHE_ALIGN(64) Bitboard*RAttack_bb[SQ_NO];

    CACHE_ALIGN(64) Bitboard   BMask_bb[SQ_NO];
    CACHE_ALIGN(64) Bitboard   RMask_bb[SQ_NO];

#ifndef BM2
    CACHE_ALIGN(64) Bitboard  BMagic_bb[SQ_NO];
    CACHE_ALIGN(64) Bitboard  RMagic_bb[SQ_NO];

    CACHE_ALIGN(64) u08       BShift   [SQ_NO];
    CACHE_ALIGN(64) u08       RShift   [SQ_NO];
#endif

    // FILE & RANK distance
    u08 FileRankDist[F_NO][R_NO];
    u08   SquareDist[SQ_NO][SQ_NO];

    namespace {

//        // De Bruijn sequences. See chessprogramming.wikispaces.com/BitScan
//        const u64 DeBruijn_64 = 0x3F79D71B4CB0A89ULL;
//        const u32 DeBruijn_32 = 0x783A9B23;
//
//        CACHE_ALIGN (8) i08 MSB_Table[_UI8_MAX + 1];
//        CACHE_ALIGN (8) Square BSF_Table[SQ_NO];
//
//        INLINE unsigned bsf_index (Bitboard bb)
//        {
//            // Matt Taylor's folding for 32 bit systems, extended to 64 bits by Kim Walisch
//            bb ^= (bb - 1);
//
//            return
//#       ifdef _64BIT
//                (bb * DeBruijn_64) >> 58;
//#       else
//                ((u32 (bb) ^ u32 (bb >> 32)) * DeBruijn_32) >> 26;
//#       endif
//        }

        // max moves for rook from any corner square (LINEAR)
        // 2 ^ 12 = 4096 = 0x1000
        const u16 MAX_LMOVES =   U32 (0x1000);

        // 4 * 2^9 + 4 * 2^6 + 12 * 2^7 + 44 * 2^5
        // 4 * 512 + 4 *  64 + 12 * 128 + 44 *  32
        //    2048 +     256 +     1536 +     1408
        //                                    5248 = 0x1480
        const u32 MAX_BMOVES = U32 (0x1480);

        // 4 * 2^12 + 24 * 2^11 + 36 * 2^10
        // 4 * 4096 + 24 * 2048 + 36 * 1024
        //    16384 +     49152 +     36864
        //                           102400 = 0x19000
        const u32 MAX_RMOVES = U32 (0x19000);

        CACHE_ALIGN(64) Bitboard BTable_bb[MAX_BMOVES];
        CACHE_ALIGN(64) Bitboard RTable_bb[MAX_RMOVES];

        typedef u16 (*Indexer) (Square s, Bitboard occ);

        inline void initialize_table (Bitboard table_bb[], Bitboard *attacks_bb[], Bitboard magics_bb[], Bitboard masks_bb[], u08 shift[], const Delta deltas[], const Indexer m_index)
        {
#   ifndef BM2
            const u16 MagicBoosters[R_NO] =
#       ifdef _64BIT
            { 0xC1D, 0x228, 0xDE3, 0x39E, 0x342, 0x01A, 0x853, 0x45D }; // 64-bit
#       else
            { 0x3C9, 0x7B8, 0xB22, 0x21E, 0x815, 0xB24, 0x6AC, 0x0A4 }; // 32-bit
#       endif
            
            Bitboard occupancy[MAX_LMOVES];
            Bitboard reference[MAX_LMOVES];
            RKISS rkiss;
#   endif

            attacks_bb[SQ_A1] = table_bb;

            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                // Board edges are not considered in the relevant occupancies
                Bitboard edges = board_edges (s);

                // Given a square 's', the mask is the bitboard of sliding attacks from
                // 's' computed on an empty board. The index must be big enough to contain
                // all the attacks for each possible subset of the mask and so is 2 power
                // the number of 1s of the mask. Hence we deduce the size of the shift to
                // apply to the 64 or 32 bits word to get the index.
                Bitboard moves = sliding_attacks (deltas, s);

                Bitboard mask = masks_bb[s] = moves & ~edges;

#           ifndef BM2
                shift[s] =
#               ifdef _64BIT
                    64
#               else
                    32
#               endif
                    - pop_count<MAX15> (mask);
#           else
                (void) shift;
#           endif

                // Use Carry-Rippler trick to enumerate all subsets of masks_bb[s] and
                // store the corresponding sliding attack bitboard in reference[].
                u32 size     = 0;
                Bitboard occ = U64 (0);
                do
                {
#               ifdef BM2
                    attacks_bb[s][_pext_u64 (occ, mask)] = sliding_attacks (deltas, s, occ);
#               else
                    occupancy[size] = occ;
                    reference[size] = sliding_attacks (deltas, s, occ);
#               endif

                    ++size;
                    occ = (occ - mask) & mask;
                }
                while (occ);

                // Set the offset for the table_bb of the next square. We have individual
                // table_bb sizes for each square with "Fancy Magic Bitboards".
                if (s < SQ_H8)
                {
                    attacks_bb[s + 1] = attacks_bb[s] + size;
                }

#           ifndef BM2
                u16 booster = MagicBoosters[_rank (s)];

                // Find a magic for square 's' picking up an (almost) random number
                // until we find the one that passes the verification test.
                u32 i;

                do
                {
                    u16 index;
                    do
                    {
                        magics_bb[s] = rkiss.magic_rand<Bitboard> (booster);
                        index = (mask * magics_bb[s]) >> 0x38;
                    }
                    while (pop_count<MAX15> (index) < 6);

                    memset (attacks_bb[s], U64 (0), size*sizeof (*attacks_bb[s]));

                    // A good magic must map every possible occupancy to an index that
                    // looks up the correct sliding attack in the attacks_bb[s] database.
                    // Note that we build up the database for square 's' as a side
                    // effect of verifying the magic.
                    for (i = 0; i < size; ++i)
                    {
                        Bitboard &attacks = attacks_bb[s][m_index (s, occupancy[i])];

                        if ((attacks != U64 (0)) && (attacks != reference[i]))
                        {
                            break;
                        }

                        ASSERT (reference[i] != U64 (0));
                        attacks = reference[i];
                    }
                }
                while (i < size);
#           else
                (void) magics_bb; 
                (void) m_index; 
#           endif
            }
        }

        inline void initialize_sliding ()
        {
#       ifndef BM2
            initialize_table (BTable_bb, BAttack_bb, BMagic_bb, BMask_bb, BShift, PieceDeltas[BSHP], magic_index<BSHP>);
            initialize_table (RTable_bb, RAttack_bb, RMagic_bb, RMask_bb, RShift, PieceDeltas[ROOK], magic_index<ROOK>);
#       else
            initialize_table (BTable_bb, BAttack_bb, NULL, BMask_bb, NULL, PieceDeltas[BSHP], magic_index<BSHP>);
            initialize_table (RTable_bb, RAttack_bb, NULL, RMask_bb, NULL, PieceDeltas[ROOK], magic_index<ROOK>);
#       endif
        }

    }

    void initialize ()
    {

        //for (i08 s = SQ_A1; s <= SQ_H8; ++s)
        //{
        //    BSF_Table[bsf_index (Square_bb[s] = 1ULL << s)] = s;
        //    BSF_Table[bsf_index (Square_bb[s])] = s;
        //}
        //for (Bitboard b = 1; b < 256; ++b)
        //{
        //    MSB_Table[b] = more_than_one (b) ? MSB_Table[b - 1] : scan_lsq (b);
        //}

        for (i08 f = F_A; f <= F_H; ++f)
        {
            for (i08 r = R_1; r <= R_8; ++r)
            {
                FileRankDist[f][r] = abs (f - r);
            }
        }

        for (i08 s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            for (i08 s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                if (s1 != s2)
                {
                    File f1 = _file (Square (s1));
                    Rank r1 = _rank (Square (s1));
                    File f2 = _file (Square (s2));
                    Rank r2 = _rank (Square (s2));

                    u08 dFile = FileRankDist[f1][f2];
                    u08 dRank = FileRankDist[r1][r2];

                    SquareDist[s1][s2]  = max (dFile , dRank);
                    //TaxicabDist[s1][s2] =     (dFile + dRank);

                    DistanceRings[s1][SquareDist[s1][s2] - 1] += Square (s2);
                }
            }
        }

        for (i08 c = WHITE; c <= BLACK; ++c)
        {
            for (i08 s = SQ_A1; s <= SQ_H8; ++s)
            {
                FrontSqs_bb   [c][s] = FrontRank_bb[c][_rank (Square (s))] &    File_bb[_file (Square (s))];
                PawnAttackSpan[c][s] = FrontRank_bb[c][_rank (Square (s))] & AdjFile_bb[_file (Square (s))];
                PasserPawnSpan[c][s] =  FrontSqs_bb[c][s]         | PawnAttackSpan[c][s];
            }
        }

        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            u08 k;
            Delta del;

            for (i08 c = WHITE; c <= BLACK; ++c)
            {
                k = 0;
                while ((del = PawnDeltas[c][k++]) != DEL_O)
                {
                    Square sq = s + del;
                    if (_ok (sq) && SquareDist[s][sq] == 1)
                    {
                        PawnAttacks[c][s] += sq;
                    }
                }
            }

            PieceT pt;

            pt = NIHT;
            k = 0;
            while ((del = PieceDeltas[pt][k++]) != DEL_O)
            {
                Square sq = s + del;
                if (_ok (sq) && SquareDist[s][sq] == 2)
                {
                    PieceAttacks[pt][s] += sq;
                }
            }

            pt = KING;
            k = 0;
            while ((del = PieceDeltas[pt][k++]) != DEL_O)
            {
                Square sq = s + del;
                if (_ok (sq) && SquareDist[s][sq] == 1)
                {
                    PieceAttacks[pt][s] += sq;
                }
            }

            PieceAttacks[BSHP][s] = sliding_attacks (PieceDeltas[BSHP], s);
            PieceAttacks[ROOK][s] = sliding_attacks (PieceDeltas[ROOK], s);
            PieceAttacks[QUEN][s] = PieceAttacks[BSHP][s] | PieceAttacks[ROOK][s];
        }

        initialize_sliding ();

        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                // NOTE:: must be called after initialize_sliding()

                PieceT pt =  (PieceAttacks[BSHP][s1] & s2)
                    ? BSHP : (PieceAttacks[ROOK][s1] & s2)
                    ? ROOK : NONE;

                if (NONE == pt) continue;

                Between_bb[s1][s2] = (attacks_bb (Piece (pt), s1, Square_bb[s2]) & attacks_bb (Piece (pt), s2, Square_bb[s1]));
                LineRay_bb[s1][s2] = (attacks_bb (Piece (pt), s1,       U64 (0)) & attacks_bb (Piece (pt), s2,       U64 (0))) + s1 + s2;
            }
        }

    }

#ifndef NDEBUG

    // pretty() returns an ASCII representation of a bitboard to print on console output
    // Bitboard in an easily readable format. This is sometimes useful for debugging.
    const string pretty (Bitboard bb, char p)
    {
        string sbb;

        const string row   = "|. . . . . . . .|\n";
        const u16 row_len = row.length () + 1;
        sbb = " /---------------\\\n";
        for (i08 r = R_8; r >= R_1; --r)
        {
            sbb += to_char (Rank (r)) + row;
        }
        sbb += " \\---------------/\n ";
        for (i08 f = F_A; f <= F_H; ++f)
        {
            sbb += " "; sbb += to_char (File (f));
        }
        sbb += "\n";

        while (bb != U64 (0))
        {
            Square s = pop_lsq (bb);
            i08 r = _rank (s);
            i08 f = _file (s);
            sbb[2 + row_len * (8 - r) + 2 * f] = p;
        }

        return sbb;
    }

#endif

}
