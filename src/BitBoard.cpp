#include "BitBoard.h"

#include "RKISS.h"
#include "Notation.h"

namespace BitBoard {

    using namespace std;
    using namespace Notation;

    // FRONT SQUARES
    Bitboard FRONT_SQRS_bb[CLR_NO][SQ_NO];

    Bitboard BETWEEN_SQRS_bb[SQ_NO][SQ_NO];
    Bitboard RAY_LINE_bb[SQ_NO][SQ_NO];

    Bitboard DIST_RINGS_bb[SQ_NO][F_NO];

    // Span of the attacks of pawn
    Bitboard PAWN_ATTACK_SPAN[CLR_NO][SQ_NO];

    // Path of the passed pawn
    Bitboard PAWN_PASS_SPAN[CLR_NO][SQ_NO];

    // Attacks of the pawns
    Bitboard PAWN_ATTACKS[CLR_NO][SQ_NO];

    // Attacks of the pieces
    Bitboard PIECE_ATTACKS[NONE][SQ_NO];

    Bitboard*B_ATTACK_bb[SQ_NO];
    Bitboard*R_ATTACK_bb[SQ_NO];

    Bitboard   B_MASK_bb[SQ_NO];
    Bitboard   R_MASK_bb[SQ_NO];

#ifndef BM2
    Bitboard  B_MAGIC_bb[SQ_NO];
    Bitboard  R_MAGIC_bb[SQ_NO];

    u08         B_SHIFT [SQ_NO];
    u08         R_SHIFT [SQ_NO];
#endif

    u08        SQR_DIST [SQ_NO][SQ_NO];

    namespace {

//        // De Bruijn sequences. See chessprogramming.wikispaces.com/BitScan
//        const u64 DE_BRUIJN_64 = U64(0x3F79D71B4CB0A89);
//        const u32 DE_BRUIJN_32 = U32(0x783A9B23);
//
//        i08 MSB_TABLE[UCHAR_MAX + 1];
//        Square BSF_TABLE[SQ_NO];
//
//        INLINE unsigned bsf_index (Bitboard bb)
//        {
//            // Matt Taylor's folding for 32 bit systems, extended to 64 bits by Kim Walisch
//            bb ^= (bb - 1);
//
//            return
//#       ifdef BIT64
//                (bb * DE_BRUIJN_64) >> 58;
//#       else
//                ((u32(bb) ^ u32(bb >> 32)) * DE_BRUIJN_32) >> 26;
//#       endif
//        }

        // max moves for rook from any corner square (LINEAR)
        // 2 ^ 12 = 4096 = 0x1000
        const u16 MAX_LMOVES =   U32(0x1000);

        // 4 * 2^9 + 4 * 2^6 + 12 * 2^7 + 44 * 2^5
        // 4 * 512 + 4 *  64 + 12 * 128 + 44 *  32
        //    2048 +     256 +     1536 +     1408
        //                                    5248 = 0x1480
        const u32 MAX_BMOVES = U32(0x1480);

        // 4 * 2^12 + 24 * 2^11 + 36 * 2^10
        // 4 * 4096 + 24 * 2048 + 36 * 1024
        //    16384 +     49152 +     36864
        //                           102400 = 0x19000
        const u32 MAX_RMOVES = U32(0x19000);

        Bitboard B_TABLE_bb[MAX_BMOVES];
        Bitboard R_TABLE_bb[MAX_RMOVES];

        typedef u16(*Indexer) (Square s, Bitboard occ);

#   ifndef BM2
        const u16 MAGIC_BOOSTERS[R_NO] =
#       ifdef BIT64
            { 0xC1D, 0x228, 0xDE3, 0x39E, 0x342, 0x01A, 0x853, 0x45D }; // 64-bit
#       else
            { 0x3C9, 0x7B8, 0xB22, 0x21E, 0x815, 0xB24, 0x6AC, 0x0A4 }; // 32-bit
#       endif
#   endif

        inline void initialize_table (Bitboard table_bb[], Bitboard *attacks_bb[], Bitboard masks_bb[], Bitboard magics_bb[], u08 shift[], const Delta deltas[], const Indexer m_index)
        {
#       ifndef BM2
            Bitboard occupancy[MAX_LMOVES];
            Bitboard reference[MAX_LMOVES];
            RKISS rkiss;
#       endif

            attacks_bb[SQ_A1] = table_bb;

            for (i08 s = SQ_A1; s <= SQ_H8; ++s)
            {
                // Board edges are not considered in the relevant occupancies
                Bitboard edges = board_edges (Square(s));

                // Given a square 's', the mask is the bitboard of sliding attacks from 's'
                // computed on an empty board. The index must be big enough to contain
                // all the attacks for each possible subset of the mask and so is 2 power
                // the number of 1s of the mask. Hence deduce the size of the shift to
                // apply to the 64 or 32 bits word to get the index.
                Bitboard moves = sliding_attacks (deltas, Square(s));

                Bitboard mask = masks_bb[s] = moves & ~edges;

#       ifndef BM2
                shift[s] =
#           ifdef BIT64
                    64
#           else
                    32
#           endif
                    - pop_count<MAX15> (mask);
#       else
                (void) shift;
#       endif

                // Use Carry-Rippler trick to enumerate all subsets of masks_bb[s] and
                // store the corresponding sliding attack bitboard in reference[].
                u32 size     = 0;
                Bitboard occ = U64(0);
                do
                {
#               ifndef BM2
                    occupancy[size] = occ;
                    reference[size] = sliding_attacks (deltas, Square(s), occ);
#               else
                    attacks_bb[s][_pext_u64 (occ, mask)] = sliding_attacks (deltas, Square(s), occ);
#               endif

                    ++size;
                    occ = (occ - mask) & mask;
                } while (occ != U64(0));

                // Set the offset for the table_bb of the next square. Have individual
                // table_bb sizes for each square with "Fancy Magic Bitboards".
                if (s < SQ_H8)
                {
                    attacks_bb[s + 1] = attacks_bb[s] + size;
                }

#       ifndef BM2
                u16 booster = MAGIC_BOOSTERS[_rank (Square(s))];

                // Find a magic for square 's' picking up an (almost) random number
                // until found the one that passes the verification test.
                u32 i;

                do
                {
                    u16 index;
                    do
                    {
                        magics_bb[s] = rkiss.magic_rand<Bitboard> (booster);
                        index = (mask * magics_bb[s]) >> 0x38;
                    } while (pop_count<MAX15> (index) < 6);

                    fill (attacks_bb[s], attacks_bb[s] + size, U64(0)); //fill_n (attacks_bb[s], size, U64(0));

                    // A good magic must map every possible occupancy to an index that
                    // looks up the correct sliding attack in the attacks_bb[s] database.
                    // Note that build up the database for square 's' as a side
                    // effect of verifying the magic.
                    for (i = 0; i < size; ++i)
                    {
                        Bitboard &attacks = attacks_bb[s][m_index (Square(s), occupancy[i])];

                        if (attacks && (attacks != reference[i]))
                        {
                            break;
                        }

                        assert (reference[i]);
                        attacks = reference[i];
                    }
                } while (i < size);
#       else
                (void) magics_bb; 
                (void) m_index; 
#       endif
            }
        }

        inline void initialize_sliding ()
        {
#       ifndef BM2
            initialize_table (B_TABLE_bb, B_ATTACK_bb, B_MASK_bb, B_MAGIC_bb, B_SHIFT, PIECE_DELTAS[BSHP], magic_index<BSHP>);
            initialize_table (R_TABLE_bb, R_ATTACK_bb, R_MASK_bb, R_MAGIC_bb, R_SHIFT, PIECE_DELTAS[ROOK], magic_index<ROOK>);
#       else
            initialize_table (B_TABLE_bb, B_ATTACK_bb, B_MASK_bb, NULL, NULL, PIECE_DELTAS[BSHP], magic_index<BSHP>);
            initialize_table (R_TABLE_bb, R_ATTACK_bb, R_MASK_bb, NULL, NULL, PIECE_DELTAS[ROOK], magic_index<ROOK>);
#       endif
        }

    }

    void initialize ()
    {

        //for (i08 s = SQ_A1; s <= SQ_H8; ++s)
        //{
        //    BSF_TABLE[bsf_index (SQUARE_bb[s] = 1ULL << s)] = s;
        //    BSF_TABLE[bsf_index (SQUARE_bb[s])] = s;
        //}
        //for (Bitboard b = 1; b <= UCHAR_MAX; ++b)
        //{
        //    MSB_TABLE[b] = more_than_one (b) ? MSB_TABLE[b - 1] : scan_lsq (b);
        //}

        for (i08 s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            for (i08 s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                if (s1 != s2)
                {
                    SQR_DIST[s1][s2]  = u08(max (dist<File> (Square(s1), Square(s2)) , dist<Rank> (Square(s1), Square(s2))));
                    DIST_RINGS_bb[s1][SQR_DIST[s1][s2] - 1] += Square(s2);
                }
            }
        }

        for (i08 c = WHITE; c <= BLACK; ++c)
        {
            for (i08 s = SQ_A1; s <= SQ_H8; ++s)
            {
                FRONT_SQRS_bb   [c][s] = FRONT_RANK_bb[c][_rank (Square(s))] &     FILE_bb[_file (Square(s))];
                PAWN_ATTACK_SPAN[c][s] = FRONT_RANK_bb[c][_rank (Square(s))] & ADJ_FILE_bb[_file (Square(s))];
                PAWN_PASS_SPAN  [c][s] = FRONT_SQRS_bb[c][s] | PAWN_ATTACK_SPAN[c][s];
            }
        }

        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            u08 k;
            Delta del;

            for (i08 c = WHITE; c <= BLACK; ++c)
            {
                k = 0;
                while ((del = PAWN_DELTAS[c][k++]) != DEL_O)
                {
                    Square sq = s + del;
                    if (_ok (sq) && SQR_DIST[s][sq] == 1)
                    {
                        PAWN_ATTACKS[c][s] += sq;
                    }
                }
            }

            PieceT pt;

            pt = NIHT;
            k = 0;
            while ((del = PIECE_DELTAS[pt][k++]) != DEL_O)
            {
                Square sq = s + del;
                if (_ok (sq) && SQR_DIST[s][sq] == 2)
                {
                    PIECE_ATTACKS[pt][s] += sq;
                }
            }

            pt = KING;
            k = 0;
            while ((del = PIECE_DELTAS[pt][k++]) != DEL_O)
            {
                Square sq = s + del;
                if (_ok (sq) && SQR_DIST[s][sq] == 1)
                {
                    PIECE_ATTACKS[pt][s] += sq;
                }
            }

            PIECE_ATTACKS[BSHP][s] = sliding_attacks (PIECE_DELTAS[BSHP], s);
            PIECE_ATTACKS[ROOK][s] = sliding_attacks (PIECE_DELTAS[ROOK], s);
            PIECE_ATTACKS[QUEN][s] = PIECE_ATTACKS[BSHP][s] | PIECE_ATTACKS[ROOK][s];
        }

        initialize_sliding ();
        
        // NOTE:: must be after initialize_sliding()
        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                PieceT pt =  
                    (PIECE_ATTACKS[BSHP][s1] & s2) ? BSHP :
                    (PIECE_ATTACKS[ROOK][s1] & s2) ? ROOK :
                    NONE;

                if (NONE == pt) continue;

                BETWEEN_SQRS_bb[s1][s2] = (attacks_bb (Piece(pt), s1, SQUARE_bb[s2]) & attacks_bb (Piece(pt), s2, SQUARE_bb[s1]));
                RAY_LINE_bb[s1][s2] = (attacks_bb (Piece(pt), s1,       U64(0)) & attacks_bb (Piece(pt), s2,       U64(0))) + s1 + s2;
            }
        }

    }

#ifndef NDEBUG

    // pretty() returns an ASCII representation of a bitboard to print on console output
    // Bitboard in an easily readable format. This is sometimes useful for debugging.
    string pretty (Bitboard bb, char p)
    {
        static string ROW  = "|. . . . . . . .|\n";
        static u16 ROW_LEN = ROW.length () + 1;

        string sbb;
        sbb = " /---------------\\\n";
        for (i08 r = R_8; r >= R_1; --r)
        {
            sbb += to_char (Rank(r)) + ROW;
        }
        sbb += " \\---------------/\n ";
        for (i08 f = F_A; f <= F_H; ++f)
        {
            sbb += " "; sbb += to_char (File(f));
        }
        sbb += "\n";

        while (bb != U64(0))
        {
            Square s = pop_lsq (bb);
            i08 r = _rank (s);
            i08 f = _file (s);
            sbb[2 + ROW_LEN * (8 - r) + 2 * f] = p;
        }

        return sbb;
    }

#endif

}
