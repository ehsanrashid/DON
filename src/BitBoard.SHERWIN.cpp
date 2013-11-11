#include "BitBoard.h"

namespace BitBoard {

    namespace {

        // max moves for rook from any corner square
        //                2 ^ 12 = 4096 = 0x1000
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

        //  64 = 0x040
        const uint16_t B_PATTERN = 0x040;
        // 256 = 0x100
        const uint16_t R_PATTERN = 0x100;


        Bitboard BTable_bb[MAX_B_MOVES];
        Bitboard RTable_bb[MAX_R_MOVES];

        const uint8_t BBits[SQ_NO] =
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
        const uint8_t RBits[SQ_NO] =
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

        uint32_t BRows[SQ_NO][6][B_PATTERN];
        uint32_t RRows[SQ_NO][8][R_PATTERN];

        typedef uint32_t (*Indexer) (Square s, Bitboard occ);

        template<PType T>
        // Function 'attack_index(s, occ)' for computing index for sliding attack bitboards.
        // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
        // and returns a bitboard representing all squares attacked by T (BISHOP or ROOK) on the given square.
        uint32_t attack_index (Square s, Bitboard occ);

        template<>
        uint32_t attack_index<BSHP> (Square s, Bitboard occ)
        {
            const Bitboard edges = brd_edges_bb (s);
            // remaining blocking pieces in the (x)-rays
            const Bitboard mocc = (occ & _attacks_type_bb[BSHP][s] & ~edges) >> 1;
            const uint8_t*   r = (uint8_t*) (&mocc);

            // Since every square has its set of row values the six row lookups
            // simply map any blockers to specific bits that when ored together
            // gives an offset in the bishop attack table.

            //const uint32_t *B_row = BRows[s][0]; // &BRows[s][0][0];
            const uint32_t (*B_brd)[B_PATTERN] = BRows[s];

            const uint32_t index
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
        uint32_t attack_index<ROOK> (Square s, Bitboard occ)
        {
            const Bitboard edges = brd_edges_bb (s);
            // remaining blocking pieces in the (+)-rays
            const Bitboard mocc = (occ & _attacks_type_bb[ROOK][s] & ~edges);
            const uint8_t*   r = (uint8_t*) (&mocc);

            // Since every square has its set of row values the eight row lookups
            // simply map any blockers to specific bits that when ored together
            // gives an offset in the rook attack table.

            //const uint32_t *R_row = RRows[s][0]; // &RRows[s][0][0];
            const uint32_t (*R_brd)[R_PATTERN] = RRows[s];

            const uint32_t index
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
    Bitboard attacks_bb<BSHP> (Square s, Bitboard occ)
    {
        return BTable_bb[attack_index<BSHP> (s, occ)];
    }
    template<>
    // ROOK Attacks with occupancy
    Bitboard attacks_bb<ROOK> (Square s, Bitboard occ)
    {
        return RTable_bb[attack_index<ROOK> (s, occ)];
    }
    template<>
    // QUEEN Attacks with occupancy
    Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return
            BTable_bb[attack_index<BSHP> (s, occ)] |
            RTable_bb[attack_index<ROOK> (s, occ)];
    }

    namespace {

        void initialize_BTable ()
        {
            uint32_t index_base = 0;
            for (uint8_t b = 9; b >= 5; --b)
            {
                for (Square s = SQ_A1; s <= SQ_H8; ++s)
                {
                    if (BBits[s] != b)  continue;

                    // Board edges are not considered in the relevant occupancies
                    const Bitboard edges = brd_edges_bb (s);
                    const Bitboard moves = _attacks_type_bb[BSHP][s];

                    const Bitboard mask = moves & ~edges;

                    uint8_t shift_base = 0;
                    for (uint8_t row = 0; row < 6; ++row)
                    {
                        const uint16_t maskB = (mask >> (((row + 1) << 3) + 1)) & 0x3F;

                        for (uint16_t pattern = 0; pattern < B_PATTERN; ++pattern)
                        {
                            uint32_t index = 0;
                            uint8_t  shift = shift_base;

                            for (uint8_t i = 0; i < 6; ++i)
                            {
                                uint16_t m = (1 << i);

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

                    //uint32_t size = (1 << b);
                    //for (uint32_t index = 0; index < size; ++index)
                    //{
                    //    Bitboard occ = 0;
                    //    uint32_t i = index;
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
                    //    Bitboard moves  = attacks_sliding (s, _deltas_type[BSHP], occ);
                    //    BTable_bb[index_base + index] = moves;
                    //}

                    uint32_t index = 0;
                    Bitboard occ = 0;
                    do
                    {
                        BTable_bb[index_base + index] = attacks_sliding (s, _deltas_type[BSHP], occ);
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
            uint32_t index_base = 0;
            for (uint8_t b = 12; b >= 10; --b)
            {
                for (Square s = SQ_A1; s <= SQ_H8; ++s)
                {
                    if (RBits[s] != b)  continue;

                    // Board edges are not considered in the relevant occupancies
                    const Bitboard edges = brd_edges_bb (s);
                    const Bitboard moves = _attacks_type_bb[ROOK][s];

                    const Bitboard mask = moves & ~edges;

                    uint8_t shift_base = 0;
                    for (uint8_t row = 0; row < 8; ++row)
                    {
                        const uint16_t maskR = (mask >> (row << 3)) & 0xFF;

                        for (uint16_t pattern = 0; pattern < R_PATTERN; ++pattern)
                        {
                            uint32_t index = 0;
                            uint8_t  shift = shift_base;

                            for (uint8_t i = 0; i < 8; ++i)
                            {
                                uint16_t m = (1 << i);

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

                    //uint32_t size = (1 << b);
                    //for (uint32_t index = 0; index < size; ++index)
                    //{
                    //    Bitboard occ = 0;
                    //
                    //    uint32_t i = index;
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
                    //    Bitboard moves = attacks_sliding (s, _deltas_type[ROOK], occ);
                    //    RTable_bb[index_base + index] = moves;
                    //}

                    uint32_t index = 0;
                    Bitboard occ = 0;
                    do
                    {
                        RTable_bb[index_base + index] = attacks_sliding (s, _deltas_type[ROOK], occ);
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