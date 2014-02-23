#include "BitBoard.h"

#include <cstring>
#include <sstream>

#include "BitCount.h"
#include "BitScan.h"
#include "RKISS.h"

namespace BitBoard {

    using namespace std;

    const Bitboard FA_bb = U64 (0x0101010101010101);
    const Bitboard FB_bb = FA_bb << 1;//U64 (0x0202020202020202);
    const Bitboard FC_bb = FA_bb << 2;//U64 (0x0404040404040404);
    const Bitboard FD_bb = FA_bb << 3;//U64 (0x0808080808080808);
    const Bitboard FE_bb = FA_bb << 4;//U64 (0x1010101010101010);
    const Bitboard FF_bb = FA_bb << 5;//U64 (0x2020202020202020);
    const Bitboard FG_bb = FA_bb << 6;//U64 (0x4040404040404040);
    const Bitboard FH_bb = FA_bb << 7;//U64 (0x8080808080808080);

    const Bitboard R1_bb = U64 (0x00000000000000FF);
    const Bitboard R2_bb = R1_bb << (8 * 1);//U64 (0x000000000000FF00);
    const Bitboard R3_bb = R1_bb << (8 * 2);//U64 (0x0000000000FF0000);
    const Bitboard R4_bb = R1_bb << (8 * 3);//U64 (0x00000000FF000000);
    const Bitboard R5_bb = R1_bb << (8 * 4);//U64 (0x000000FF00000000);
    const Bitboard R6_bb = R1_bb << (8 * 5);//U64 (0x0000FF0000000000);
    const Bitboard R7_bb = R1_bb << (8 * 6);//U64 (0x00FF000000000000);
    const Bitboard R8_bb = R1_bb << (8 * 7);//U64 (0xFF00000000000000);

    const Bitboard R1_bb_ = ~R1_bb;//U64 (0xFFFFFFFFFFFFFF00);    // 56 Not RANK-1
    const Bitboard R8_bb_ = ~R8_bb;//U64 (0x00FFFFFFFFFFFFFF);    // 56 Not RANK-8
    const Bitboard FA_bb_ = ~FA_bb;//U64 (0xFEFEFEFEFEFEFEFE);    // 56 Not FILE-A
    const Bitboard FH_bb_ = ~FH_bb;//U64 (0x7F7F7F7F7F7F7F7F);    // 56 Not FILE-H

    const Bitboard D18_bb = U64 (0x8040201008040201);             // 08 DIAG-18 squares.
    const Bitboard D81_bb = U64 (0x0102040810204080);             // 08 DIAG-81 squares.

    const Bitboard LIHT_bb = U64 (0x55AA55AA55AA55AA);            // 32 LIGHT squares.
    const Bitboard DARK_bb = U64 (0xAA55AA55AA55AA55);            // 32 DARK  squares.

    const Bitboard CRNR_bb = U64(0x8100000000000081);             // 04 CORNER squares.
    const Bitboard MID_EDGE_bb = (FA_bb | FH_bb) & (R2_bb | R3_bb);

    // FILE & RANK distance
    uint8_t FileRankDist[F_NO][R_NO];
    uint8_t   SquareDist[SQ_NO][SQ_NO];

    const Delta PawnDeltas[CLR_NO][3] =
    {
        { DEL_NW, DEL_NE, DEL_O },
        { DEL_SE, DEL_SW, DEL_O },
    };
    const Delta PieceDeltas[NONE][9] =
    {
        { DEL_O },
        { DEL_SSW, DEL_SSE, DEL_WWS, DEL_EES, DEL_WWN, DEL_EEN, DEL_NNW, DEL_NNE, DEL_O },
        { DEL_SW, DEL_SE, DEL_NW, DEL_NE, DEL_O },
        { DEL_S, DEL_W, DEL_E, DEL_N, DEL_O },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE, DEL_O },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE, DEL_O },
    };

    // SQUARES
    CACHE_ALIGN(64) const Bitboard Square_bb[SQ_NO] =
    {
#undef S_16
#undef S_8
#undef S_4
#undef S_2
#define S_2(n)  U64(1)<<(2*(n)),  U64(1)<<(2*(n)+1)
#define S_4(n)       S_2(2*(n)),       S_2(2*(n)+1)
#define S_8(n)       S_4(2*(n)),       S_4(2*(n)+1)
#define S_16(n)      S_8(2*(n)),       S_8(2*(n)+1)
        S_16 (0), S_16 (1), S_16 (2), S_16 (3),
#undef S_16
#undef S_8
#undef S_4
#undef S_2
    };
    // FILES
    CACHE_ALIGN(64) const Bitboard   File_bb[F_NO] =
    {
        FA_bb,
        FB_bb,
        FC_bb,
        FD_bb,
        FE_bb,
        FF_bb,
        FG_bb,
        FH_bb
    };
    // RANKS
    CACHE_ALIGN(64) const Bitboard   Rank_bb[R_NO] =
    {
        R1_bb,
        R2_bb,
        R3_bb,
        R4_bb,
        R5_bb,
        R6_bb,
        R7_bb,
        R8_bb
    };

    // ADJACENT FILES used for isolated-pawn
    CACHE_ALIGN(64) const Bitboard AdjFile_bb[F_NO] =
    {
        FB_bb,
        FA_bb | FC_bb,
        FB_bb | FD_bb,
        FC_bb | FE_bb,
        FD_bb | FF_bb,
        FE_bb | FG_bb,
        FF_bb | FH_bb,
        FG_bb
    };
    // ADJACENT RANKS
    CACHE_ALIGN(64) const Bitboard AdjRank_bb[R_NO] =
    {
        R2_bb,
        R1_bb | R3_bb,
        R2_bb | R4_bb,
        R3_bb | R5_bb,
        R4_bb | R6_bb,
        R5_bb | R7_bb,
        R6_bb | R8_bb,
        R7_bb,
    };
    // FRONT RANK
    CACHE_ALIGN(64) const Bitboard FrontRank_bb[CLR_NO][R_NO] =
    {
        R2_bb | R3_bb | R4_bb | R5_bb | R6_bb | R7_bb | R8_bb,
        R3_bb | R4_bb | R5_bb | R6_bb | R7_bb | R8_bb,
        R4_bb | R5_bb | R6_bb | R7_bb | R8_bb,
        R5_bb | R6_bb | R7_bb | R8_bb,
        R6_bb | R7_bb | R8_bb,
        R7_bb | R8_bb,
        R8_bb,
        0,

        0,
        R1_bb,
        R2_bb | R1_bb,
        R3_bb | R2_bb | R1_bb,
        R4_bb | R3_bb | R2_bb | R1_bb,
        R5_bb | R4_bb | R3_bb | R2_bb | R1_bb,
        R6_bb | R5_bb | R4_bb | R3_bb | R2_bb | R1_bb,
        R7_bb | R6_bb | R5_bb | R4_bb | R3_bb | R2_bb | R1_bb
    };
    // FRONT SQUARES
    CACHE_ALIGN(64)       Bitboard FrontSqs_bb[CLR_NO][SQ_NO];

    // ---

    CACHE_ALIGN(64) Bitboard BetweenSq[SQ_NO][SQ_NO];
    CACHE_ALIGN(64) Bitboard    LineSq[SQ_NO][SQ_NO];

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

    CACHE_ALIGN(64) Bitboard  BMagic_bb[SQ_NO];
    CACHE_ALIGN(64) Bitboard  RMagic_bb[SQ_NO];

    CACHE_ALIGN(64) uint8_t      BShift[SQ_NO];
    CACHE_ALIGN(64) uint8_t      RShift[SQ_NO];

    namespace {

        // max moves for rook from any corner square
        // 2 ^ 12 = 4096 = 0x1000
        const uint16_t MAX_MOVES =   U32 (0x1000);

        // 4 * 2^9 + 4 * 2^6 + 12 * 2^7 + 44 * 2^5
        // 4 * 512 + 4 *  64 + 12 * 128 + 44 *  32
        //    2048 +     256 +     1536 +     1408
        //                                    5248 = 0x1480
        const uint32_t MAX_B_MOVES = U32 (0x1480);

        // 4 * 2^12 + 24 * 2^11 + 36 * 2^10
        // 4 * 4096 + 24 * 2048 + 36 * 1024
        //    16384 +     49152 +     36864
        //                           102400 = 0x19000
        const uint32_t MAX_R_MOVES = U32 (0x19000);


        CACHE_ALIGN(64) Bitboard BTable_bb[MAX_B_MOVES];
        CACHE_ALIGN(64) Bitboard RTable_bb[MAX_R_MOVES];

        typedef uint16_t (*Index) (Square s, Bitboard occ);

        void initialize_table (Bitboard table_bb[], Bitboard *attacks_bb[], Bitboard magics_bb[], Bitboard masks_bb[], uint8_t shift[], const Delta deltas[], const Index m_index)
        {

            const uint16_t MagicBoosters[R_NO] =
#ifdef _64BIT
            { 0xC1D, 0x228, 0xDE3, 0x39E, 0x342, 0x01A, 0x853, 0x45D }; // 64-bit
#else
            { 0x3C9, 0x7B8, 0xB22, 0x21E, 0x815, 0xB24, 0x6AC, 0x0A4 }; // 32-bit
#endif

            Bitboard occupancy[MAX_MOVES];
            Bitboard reference[MAX_MOVES];

            RKISS rkiss;

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

                shift[s] =
#ifdef _64BIT
                    64 - pop_count<MAX15> (mask);
#else
                    32 - pop_count<MAX15> (mask);
#endif

                // Use Carry-Rippler trick to enumerate all subsets of masks_bb[s] and
                // store the corresponding sliding attack bitboard in reference[].
                uint32_t size   = 0;
                Bitboard occ    = U64 (0);
                do
                {
                    occupancy[size] = occ;
                    reference[size] = sliding_attacks (deltas, s, occ);
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

                uint16_t booster = MagicBoosters[_rank (s)];

                // Find a magic for square 's' picking up an (almost) random number
                // until we find the one that passes the verification test.
                uint32_t i;

                do
                {
                    uint16_t index;
                    do
                    {
                        magics_bb[s] = rkiss.magic_rand<Bitboard> (booster);
                        index = (mask * magics_bb[s]) >> 0x38;
                    }
                    while (pop_count<MAX15> (index) < 6);

                    memset (attacks_bb[s], 0, size * sizeof (Bitboard));

                    // A good magic must map every possible occupancy to an index that
                    // looks up the correct sliding attack in the attacks_bb[s] database.
                    // Note that we build up the database for square 's' as a side
                    // effect of verifying the magic.
                    for (i = 0; i < size; ++i)
                    {
                        Bitboard &attacks = attacks_bb[s][m_index (s, occupancy[i])];

                        if (attacks && (attacks != reference[i]))
                        {
                            break;
                        }

                        ASSERT (reference[i]);
                        attacks = reference[i];
                    }
                }
                while (i < size);

            }
        }

        void initialize_sliding ()
        {
            initialize_table (BTable_bb, BAttack_bb, BMagic_bb, BMask_bb, BShift, PieceDeltas[BSHP], magic_index<BSHP>);
            initialize_table (RTable_bb, RAttack_bb, RMagic_bb, RMask_bb, RShift, PieceDeltas[ROOK], magic_index<ROOK>);
        }

    }

    void initialize ()
    {

        for (File f = F_A; f <= F_H; ++f)
        {
            for (Rank r = R_1; r <= R_8; ++r)
            {
                FileRankDist[f][r] = abs (int8_t (f) - int8_t (r));
            }
        }

        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                if (s1 != s2)
                {
                    File f1 = _file (s1);
                    Rank r1 = _rank (s1);
                    File f2 = _file (s2);
                    Rank r2 = _rank (s2);

                    uint8_t dFile = FileRankDist[f1][f2];
                    uint8_t dRank = FileRankDist[r1][r2];

                    SquareDist[s1][s2]  = max (dFile , dRank);
                    //TaxicabDist[s1][s2] =     (dFile + dRank);

                    DistanceRings[s1][SquareDist[s1][s2] - 1] += s2;
                }
            }
        }

        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                FrontSqs_bb   [c][s] = FrontRank_bb[c][_rank (s)] &    File_bb[_file (s)];
                PawnAttackSpan[c][s] = FrontRank_bb[c][_rank (s)] & AdjFile_bb[_file (s)];
                PasserPawnSpan[c][s] =  FrontSqs_bb[c][s]         | PawnAttackSpan[c][s];
            }
        }

        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            uint8_t k;
            Delta del;

            for (Color c = WHITE; c <= BLACK; ++c)
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

                BetweenSq[s1][s2] = (attacks_bb (Piece (pt), s1, Square_bb[s2]) & attacks_bb (Piece (pt), s2, Square_bb[s1]));
                LineSq   [s1][s2] = (attacks_bb (Piece (pt), s1,       U64 (0)) & attacks_bb (Piece (pt), s2,       U64 (0))) + s1 + s2;
            }
        }

    }

#ifndef NDEBUG
    // Print a Bitboard representation to console output
    // Bitboard in an easily readable format. This is sometimes useful for debugging.
    void print (Bitboard bb, char p)
    {
        string sbb;

        const string row   = "|. . . . . . . .|\n";
        const uint16_t row_len = row.length () + 1;
        sbb = " /---------------\\\n";
        for (Rank r = R_8; r >= R_1; --r)
        {
            sbb += to_char (r) + row;
        }
        sbb += " \\---------------/\n ";
        for (File f = F_A; f <= F_H; ++f)
        {
            sbb += " "; sbb += to_char (f);
        }
        sbb += "\n";
        while (bb)
        {
            Square s = pop_lsq (bb);
            int8_t r = _rank (s);
            int8_t f = _file (s);
            sbb[2 + row_len * (8 - r) + 2 * f] = p;
        }

        cout << sbb;
    }
#endif

}
