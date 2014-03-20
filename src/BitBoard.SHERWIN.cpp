#include "BitBoard.h"

namespace BitBoard {

    namespace {

        // max moves for rook from any corner square
        //                2 ^ 12 = 4096 = 0x1000
        const u16 MAX_MOVES =   U32 (0x1000);

        // 4 * 2^9 + 4 * 2^6 + 12 * 2^7 + 44 * 2^5
        // 4 * 512 + 4 *  64 + 12 * 128 + 44 *  32
        //    2048 +     256 +     1536 +     1408
        //                                    5248 = 0x1480
        const u32 MAX_B_MOVES = U32 (0x1480);

        // 4 * 2^12 + 24 * 2^11 + 36 * 2^10
        // 4 * 4096 + 24 * 2048 + 36 * 1024
        //    16384 +     49152 +     36864
        //                           102400 = 0x19000
        const u32 MAX_R_MOVES = U32 (0x19000);

        //  64 = 0x040
        const u16 B_PATTERN = 0x040;
        // 256 = 0x100
        const u16 R_PATTERN = 0x100;


        Bitboard BTable_bb[MAX_B_MOVES];
        Bitboard RTable_bb[MAX_R_MOVES];

        const u08 BBits[SQ_NO] =
        {
            6, 5, 5, 5, 5, 5, 5, 6,
            5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 7, 7, 7, 7, 5, 5,
            5, 5, 7, 9, 9, 7, 5, 5,
            5, 5, 7, 9, 9, 7, 5, 5,
            5, 5, 7, 7, 7, 7, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5,
            6, 5, 5, 5, 5, 5, 5, 6,
        };
        const u08 RBits[SQ_NO] =
        {
            12, 11, 11, 11, 11, 11, 11, 12,
            11, 10, 10, 10, 10, 10, 10, 11,
            11, 10, 10, 10, 10, 10, 10, 11,
            11, 10, 10, 10, 10, 10, 10, 11,
            11, 10, 10, 10, 10, 10, 10, 11,
            11, 10, 10, 10, 10, 10, 10, 11,
            11, 10, 10, 10, 10, 10, 10, 11,
            12, 11, 11, 11, 11, 11, 11, 12,
        };

        u32 BRows[SQ_NO][6][B_PATTERN];
        u32 RRows[SQ_NO][8][R_PATTERN];

        typedef u32 (*Index) (Square s, Bitboard occ);

        template<PieceT PT>
        // Function 'magic_index(s, occ)' for computing index for sliding attack bitboards.
        // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
        // and returns a bitboard representing all squares attacked by PT (BISHOP or ROOK) on the given square.
        u32 magic_index (Square s, Bitboard occ);

        template<>
        inline u32 magic_index<BSHP> (Square s, Bitboard occ)
        {
            const Bitboard edges = board_edges (s);
            // remaining blocking pieces in the (x)-rays
            const Bitboard mocc = (occ & PieceAttacks[BSHP][s] & ~edges) >> 1;
            const u08*   r = (u08*) (&mocc);

            // Since every square has its set of row values the six row lookups
            // simply map any blockers to specific bits that when ored together
            // gives an offset in the bishop attack table.

            //const u32 *B_row = BRows[s][0]; // &BRows[s][0][0];
            const u32 (*B_brd)[B_PATTERN] = BRows[s];

            const u32 index
                //= (B_row + 0*B_PATTERN)[(mocc >>  8) & 0x3F]  // row 2
                //| (B_row + 1*B_PATTERN)[(mocc >> 16) & 0x3F]  // row 3
                //| (B_row + 2*B_PATTERN)[(mocc >> 24) & 0x3F]  // row 4
                //| (B_row + 3*B_PATTERN)[(mocc >> 32) & 0x3F]  // row 5
                //| (B_row + 4*B_PATTERN)[(mocc >> 40) & 0x3F]  // row 6
                //| (B_row + 5*B_PATTERN)[(mocc >> 48) & 0x3F]; // row 7

                //= (B_row + 0*B_PATTERN)[r[1]]  // row 2
                //| (B_row + 1*B_PATTERN)[r[2]]  // row 3
                //| (B_row + 2*B_PATTERN)[r[3]]  // row 4
                //| (B_row + 3*B_PATTERN)[r[4]]  // row 5
                //| (B_row + 4*B_PATTERN)[r[5]]  // row 6
                //| (B_row + 5*B_PATTERN)[r[6]]; // row 7

                = B_brd[0][r[1]] // row 2
                | B_brd[1][r[2]] // row 3
                | B_brd[2][r[3]] // row 4
                | B_brd[3][r[4]] // row 5
                | B_brd[4][r[5]] // row 6
                | B_brd[5][r[6]];// row 7

            return index;
        }

        template<>
        inline u32 magic_index<ROOK> (Square s, Bitboard occ)
        {
            const Bitboard edges = board_edges (s);
            // remaining blocking pieces in the (+)-rays
            const Bitboard mocc = (occ & PieceAttacks[ROOK][s] & ~edges);
            const u08*  r = (u08*) (&mocc);

            // Since every square has its set of row values the eight row lookups
            // simply map any blockers to specific bits that when ored together
            // gives an offset in the rook attack table.

            //const u32 *R_row = RRows[s][0]; // &RRows[s][0][0];
            const u32 (*R_brd)[R_PATTERN] = RRows[s];

            const u32 index
                //= (R_row + 0*R_PATTERN)[(mocc >>  0) & 0xFF]  // row 1
                //| (R_row + 1*R_PATTERN)[(mocc >>  8) & 0xFF]  // row 2
                //| (R_row + 2*R_PATTERN)[(mocc >> 16) & 0xFF]  // row 3
                //| (R_row + 3*R_PATTERN)[(mocc >> 24) & 0xFF]  // row 4
                //| (R_row + 4*R_PATTERN)[(mocc >> 32) & 0xFF]  // row 5
                //| (R_row + 5*R_PATTERN)[(mocc >> 40) & 0xFF]  // row 6
                //| (R_row + 6*R_PATTERN)[(mocc >> 48) & 0xFF]  // row 7
                //| (R_row + 7*R_PATTERN)[(mocc >> 56) & 0xFF]; // row 8

                //= (R_row + 0*R_PATTERN)[r[0]]  // row 1
                //| (R_row + 1*R_PATTERN)[r[1]]  // row 2
                //| (R_row + 2*R_PATTERN)[r[2]]  // row 3
                //| (R_row + 3*R_PATTERN)[r[3]]  // row 4
                //| (R_row + 4*R_PATTERN)[r[4]]  // row 5
                //| (R_row + 5*R_PATTERN)[r[5]]  // row 6
                //| (R_row + 6*R_PATTERN)[r[6]]  // row 7
                //| (R_row + 7*R_PATTERN)[r[7]]; // row 8

                = R_brd[0][r[0]] // row 1
                | R_brd[1][r[1]] // row 2
                | R_brd[2][r[2]] // row 3
                | R_brd[3][r[3]] // row 4
                | R_brd[4][r[4]] // row 5
                | R_brd[5][r[5]] // row 6
                | R_brd[6][r[6]] // row 7
                | R_brd[7][r[7]];// row 8

            return index;
        }


        void initialize_BTable ();
        void initialize_RTable ();

    }

    void initialize_sliding ()
    {
        initialize_BTable ();
        initialize_RTable ();
    }

    template<>
    // BISHOP Attacks with occupancy
    Bitboard attacks_bb<BSHP> (Square s, Bitboard occ) { return BTable_bb[magic_index<BSHP> (s, occ)]; }
    template<>
    // ROOK Attacks with occupancy
    Bitboard attacks_bb<ROOK> (Square s, Bitboard occ) { return RTable_bb[magic_index<ROOK> (s, occ)]; }
    template<>
    // QUEEN Attacks with occupancy
    Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return
            BTable_bb[magic_index<BSHP> (s, occ)] |
            RTable_bb[magic_index<ROOK> (s, occ)];
    }

    namespace {

        void initialize_BTable ()
        {
            u32 index_base = 0;
            for (u08 b = 9; b >= 5; --b)
            {
                for (Square s = SQ_A1; s <= SQ_H8; ++s)
                {
                    if (BBits[s] != b)  continue;

                    // Board edges are not considered in the relevant occupancies
                    const Bitboard edges = board_edges (s);
                    const Bitboard moves = PieceAttacks[BSHP][s];

                    const Bitboard mask = moves & ~edges;

                    u08 shift_base = 0;
                    for (u08 row = 0; row < 6; ++row)
                    {
                        const u16 maskB = (mask >> (((row + 1) << 3) + 1)) & 0x3F;

                        for (u16 pattern = 0; pattern < B_PATTERN; ++pattern)
                        {
                            u32 index = 0;
                            u08  shift = shift_base;

                            for (u08 i = 0; i < 6; ++i)
                            {
                                u16 m = (1 << i);

                                if (maskB & m)
                                {
                                    if (pattern & m)
                                    {
                                        index |= (1 << shift);
                                    }

                                    ++shift;
                                    if (0x3F == pattern)
                                    {
                                        ++shift_base;
                                    }
                                }
                            }

                            BRows[s][row][pattern] = index_base + index;
                        }
                    }

                    //u32 size = (1 << b);
                    //for (u32 index = 0; index < size; ++index)
                    //{
                    //    Bitboard occ = 0;
                    //    u32 i = index;
                    //    for (Square sq = SQ_A1; sq <= SQ_H8; ++sq)
                    //    {
                    //        if (mask & sq)
                    //        {
                    //            if (i & 1)
                    //                occ += sq;
                    //            i >>= 1;
                    //        }
                    //    }
                    //
                    //    Bitboard moves  = sliding_attacks (PieceDeltas[BSHP], s, occ);
                    //    BTable_bb[index_base + index] = moves;
                    //}

                    u32 index = 0;
                    Bitboard occ = U64 (0);
                    do
                    {
                        BTable_bb[index_base + index] = sliding_attacks (PieceDeltas[BSHP], s, occ);
                        ++index;
                        occ = (occ - mask) & mask;
                    }
                    while (occ);

                    index_base += index;  //size;
                }
            }
        }

        void initialize_RTable ()
        {
            u32 index_base = 0;
            for (u08 b = 12; b >= 10; --b)
            {
                for (Square s = SQ_A1; s <= SQ_H8; ++s)
                {
                    if (RBits[s] != b)  continue;

                    // Board edges are not considered in the relevant occupancies
                    const Bitboard edges = board_edges (s);
                    const Bitboard moves = PieceAttacks[ROOK][s];

                    const Bitboard mask = moves & ~edges;

                    u08 shift_base = 0;
                    for (u08 row = 0; row < 8; ++row)
                    {
                        const u16 maskR = (mask >> (row << 3)) & 0xFF;

                        for (u16 pattern = 0; pattern < R_PATTERN; ++pattern)
                        {
                            u32 index = 0;
                            u08  shift = shift_base;

                            for (u08 i = 0; i < 8; ++i)
                            {
                                u16 m = (1 << i);

                                if (maskR & m)
                                {
                                    if (pattern & m)
                                    {
                                        index |= (1 << shift);
                                    }

                                    ++shift;
                                    if (0xFF == pattern)
                                    {
                                        ++shift_base;
                                    }
                                }
                            }

                            RRows[s][row][pattern] = index_base + index;
                        }
                    }

                    //u32 size = (1 << b);
                    //for (u32 index = 0; index < size; ++index)
                    //{
                    //    Bitboard occ = U64 (0);
                    //
                    //    u32 i = index;
                    //    for (Square sq = SQ_A1; sq <= SQ_H8; ++sq)
                    //    {
                    //        if (mask & sq)
                    //        {
                    //            if (i & 1)
                    //                occ += sq;
                    //            i >>= 1;
                    //        }
                    //    }
                    //
                    //    Bitboard moves = sliding_attacks (PieceDeltas[ROOK], s, occ);
                    //    RTable_bb[index_base + index] = moves;
                    //}

                    u32 index = 0;
                    Bitboard occ = U64 (0);
                    do
                    {
                        RTable_bb[index_base + index] = sliding_attacks (PieceDeltas[ROOK], s, occ);
                        ++index;
                        occ = (occ - mask) & mask;
                    }
                    while (occ);

                    index_base += index;  //size;
                }
            }
        }
    }

}