#include "BitBoard.h"

#include "PRNG.h"
#include "Notation.h"

namespace BitBoard {

    using namespace std;
    using namespace Notation;

    // FRONT SQUARES
    Bitboard FRONT_SQRS_bb  [CLR_NO][SQ_NO];

    Bitboard BETWEEN_bb     [SQ_NO][SQ_NO];
    Bitboard RAYLINE_bb     [SQ_NO][SQ_NO];

    Bitboard DIST_RINGS_bb  [SQ_NO][F_NO];

    // Span of the attacks of pawn
    Bitboard PAWN_ATTACK_SPAN[CLR_NO][SQ_NO];

    // Path of the passed pawn
    Bitboard PAWN_PASS_SPAN [CLR_NO][SQ_NO];

    // Attacks of the pawns
    Bitboard PAWN_ATTACKS   [CLR_NO][SQ_NO];

    // Attacks of the pieces
    Bitboard PIECE_ATTACKS  [NONE][SQ_NO];

    Bitboard*B_ATTACK_bb[SQ_NO];
    Bitboard*R_ATTACK_bb[SQ_NO];

    Bitboard  B_MASK_bb [SQ_NO];
    Bitboard  R_MASK_bb [SQ_NO];

#ifndef BM2
    Bitboard  B_MAGIC_bb[SQ_NO];
    Bitboard  R_MAGIC_bb[SQ_NO];

    u08       B_SHIFT   [SQ_NO];
    u08       R_SHIFT   [SQ_NO];
#endif

    u08       SQR_DIST  [SQ_NO][SQ_NO];

    namespace {

//        // De Bruijn sequences. See chessprogramming.wikispaces.com/BitScan
//        const u64 DE_BRUIJN_64 = U64(0x3F79D71B4CB0A89);
//        const u32 DE_BRUIJN_32 = U32(0x783A9B23);
//
//        i08 MSB_TABLE[UCHAR_MAX + 1];
//        Square BSF_TABLE[SQ_NO];
//
//        unsigned bsf_index (Bitboard bb)
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

        // Max Linear Moves (for rook from any corner square)
        // 2 ^ 12 = 4096 = 0x1000
        const u16 MAX_LMOVES =   U32(0x1000);
        
        // Max Bishop Moves
        // 4 * 2^9 + 4 * 2^6 + 12 * 2^7 + 44 * 2^5
        // 4 * 512 + 4 *  64 + 12 * 128 + 44 *  32
        //    2048 +     256 +     1536 +     1408
        //                                    5248 = 0x1480
        const u32 MAX_BMOVES = U32(0x1480);
        
        // Max Rook Moves
        // 4 * 2^12 + 24 * 2^11 + 36 * 2^10
        // 4 * 4096 + 24 * 2048 + 36 * 1024
        //    16384 +     49152 +     36864
        //                           102400 = 0x19000
        const u32 MAX_RMOVES = U32(0x19000);

        Bitboard B_TABLE_bb[MAX_BMOVES];
        Bitboard R_TABLE_bb[MAX_RMOVES];

        typedef u16(*Indexer) (Square s, Bitboard occ);

        // initialize_table() computes all rook and bishop attacks at startup.
        // Magic bitboards are used to look up attacks of sliding pieces.
        // As a reference see chessprogramming.wikispaces.com/Magic+Bitboards.
        // In particular, here we use the so called "fancy" approach.
        void initialize_table (Bitboard table_bb[], Bitboard *attacks_bb[], Bitboard masks_bb[], Bitboard magics_bb[], u08 shift[], const Delta deltas[], const Indexer magic_index)
        {

#       ifndef BM2
            const u32 SEEDS[R_NO] =
#           ifdef BIT64
                { 0x002D8, 0x0284C, 0x0D6E5, 0x08023, 0x02FF9, 0x03AFC, 0x04105, 0x000FF }; // 64-bit
#           else
                { 0x02311, 0x0AE10, 0x0D447, 0x09856, 0x01663, 0x173E5, 0x199D0, 0x0427C }; // 32-bit
#           endif

            Bitboard occupancy[MAX_LMOVES]
                   , reference[MAX_LMOVES];
            
            i32      ages     [MAX_LMOVES]
                   , cur_age = 0;
            
            fill (begin (ages), end (ages), 0);

#       endif
            
            // attacks_bb[s] is a pointer to the beginning of the attacks table for square 's'
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
                    - u08(pop_count<MAX15> (mask));
#       else
                (void) shift;
#       endif

                // Use Carry-Rippler trick to enumerate all subsets of masks_bb[s] and
                // store the corresponding sliding attack bitboard in reference[].
                u32     size = 0;
                Bitboard occ = U64(0);
                do
                {
#               ifndef BM2
                    occupancy[size] = occ;
                    reference[size] = sliding_attacks (deltas, Square(s), occ);
#               else
                    attacks_bb[s][PEXT (occ, mask)] = sliding_attacks (deltas, Square(s), occ);
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

                PRNG rng(SEEDS[_rank (Square(s))]);
                u32 i;
                
                // Find a magic for square 's' picking up an (almost) random number
                // until found the one that passes the verification test.
                do
                {
                    do
                    {
                        magics_bb[s] = rng.sparse_rand<Bitboard> ();
                    } while (pop_count<MAX15> ((mask * magics_bb[s]) >> 0x38) < 6);

                    // A good magic must map every possible occupancy to an index that
                    // looks up the correct sliding attack in the attacks_bb[s] database.
                    // Note that build up the database for square 's' as a side
                    // effect of verifying the magic.
                    for (++cur_age, i = 0; i < size; ++i)
                    {
                        u16 idx = magic_index (Square(s), occupancy[i]);
                        
                        if (ages[idx] < cur_age)
                        {
                            ages[idx] = cur_age;
                            attacks_bb[s][idx] = reference[i];
                        }
                        else
                        {
                            if (attacks_bb[s][idx] != reference[i]) break;
                        }
                    }
                } while (i < size);
#       else
                (void) magics_bb; 
                (void) magic_index;
#       endif
            }
        }

        void initialize_sliding ()
        {
#       ifndef BM2
            initialize_table (B_TABLE_bb, B_ATTACK_bb, B_MASK_bb, B_MAGIC_bb, B_SHIFT, PIECE_DELTAS[BSHP], magic_index<BSHP>);
            initialize_table (R_TABLE_bb, R_ATTACK_bb, R_MASK_bb, R_MAGIC_bb, R_SHIFT, PIECE_DELTAS[ROOK], magic_index<ROOK>);
#       else
            initialize_table (B_TABLE_bb, B_ATTACK_bb, B_MASK_bb, nullptr, nullptr, PIECE_DELTAS[BSHP], magic_index<BSHP>);
            initialize_table (R_TABLE_bb, R_ATTACK_bb, R_MASK_bb, nullptr, nullptr, PIECE_DELTAS[ROOK], magic_index<ROOK>);
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
            for (PieceT pt = BSHP; pt <= ROOK; ++pt)
            {
                for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
                {
                    if (!(PIECE_ATTACKS[pt][s1] & s2)) continue;
                    
                    BETWEEN_bb[s1][s2] = (attacks_bb (Piece(pt), s1, SQUARE_bb[s2]) & attacks_bb (Piece(pt), s2, SQUARE_bb[s1]));
                    RAYLINE_bb[s1][s2] = (attacks_bb (Piece(pt), s1,        U64(0)) & attacks_bb (Piece(pt), s2,        U64(0))) + s1 + s2;
                }
            }
        }

    }

#ifndef NDEBUG

    // pretty() returns an ASCII representation of a bitboard to print on console output
    // Bitboard in an easily readable format. This is sometimes useful for debugging.
    string pretty (Bitboard bb, char p)
    {
        static string ROW  = "|. . . . . . . .|\n";

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
            sbb[2 + (ROW.length () + 1) * (8 - _rank (s)) + 2 * _file (s)] = p;
        }

        return sbb;
    }

#endif

}
