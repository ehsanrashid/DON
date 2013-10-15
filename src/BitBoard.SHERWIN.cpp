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
        const uint32_t MAX_MOVES_B = U32 (0x1480);

        // 4 * 2^12 + 24 * 2^11 + 36 * 2^10
        // 4 * 4096 + 24 * 2048 + 36 * 1024
        //    16384 +     49152 +     36864
        //                           102400 = 0x19000
        const uint32_t MAX_MOVES_R = U32 (0x19000);

        //  64 = 0x040
        const uint16_t PATTERN_B = 0x040;
        // 256 = 0x100
        const uint16_t PATTERN_R = 0x100;


        Bitboard _bbTable_B[MAX_MOVES_B];
        Bitboard _bbTable_R[MAX_MOVES_R];

        const uint8_t _bBits_B[SQ_NO] =
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
        const uint8_t _bBits_R[SQ_NO] =
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

        uint32_t _bRows_B[SQ_NO][6][PATTERN_B];
        uint32_t _bRows_R[SQ_NO][8][PATTERN_R];

        typedef uint32_t (*Indexer) (Square s, Bitboard occ);

        template<PType T>
        // Function 'index_attacks(s, occ)' for computing index for sliding attack bitboards.
        // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
        // and returns a bitboard representing all squares attacked by T (BISHOP or ROOK) on the given square.
        uint32_t index_attacks (Square s, Bitboard occ);

        template<>
        uint32_t index_attacks<BSHP> (Square s, Bitboard occ)
        {
            const Bitboard edges = mask_brd_edges (s);
            // remaining blocking pieces in the (x)-rays
            const Bitboard mocc = (occ & _bb_attacks_type[BSHP][s] & ~edges) >> 1;
            const uint8_t*   r = (uint8_t*) (&mocc);

            // Since every square has its set of row values the six row lookups
            // simply map any blockers to specific bits that when ored together
            // gives an offset in the bishop attack table.

            //const uint32_t *rowB = _bRows_B[s][0]; // &_bRows_B[s][0][0];
            const uint32_t (*brdB)[PATTERN_B] = _bRows_B[s];

            const uint32_t index
                //= (rowB + 0*PATTERN_B)[(mocc >>  8) & 0x3F]  // row 2
                //| (rowB + 1*PATTERN_B)[(mocc >> 16) & 0x3F]  // row 3
                //| (rowB + 2*PATTERN_B)[(mocc >> 24) & 0x3F]  // row 4
                //| (rowB + 3*PATTERN_B)[(mocc >> 32) & 0x3F]  // row 5
                //| (rowB + 4*PATTERN_B)[(mocc >> 40) & 0x3F]  // row 6
                //| (rowB + 5*PATTERN_B)[(mocc >> 48) & 0x3F]; // row 7

                //= (rowB + 0*PATTERN_B)[r[1]]  // row 2
                //| (rowB + 1*PATTERN_B)[r[2]]  // row 3
                //| (rowB + 2*PATTERN_B)[r[3]]  // row 4
                //| (rowB + 3*PATTERN_B)[r[4]]  // row 5
                //| (rowB + 4*PATTERN_B)[r[5]]  // row 6
                //| (rowB + 5*PATTERN_B)[r[6]]; // row 7

                = brdB[0][r[1]] // row 2
                | brdB[1][r[2]] // row 3
                | brdB[2][r[3]] // row 4
                | brdB[3][r[4]] // row 5
                | brdB[4][r[5]] // row 6
                | brdB[5][r[6]];// row 7

            return index;
        }
        template<>
        uint32_t index_attacks<ROOK> (Square s, Bitboard occ)
        {
            const Bitboard edges = mask_brd_edges (s);
            // remaining blocking pieces in the (+)-rays
            const Bitboard mocc = (occ & _bb_attacks_type[ROOK][s] & ~edges);
            const uint8_t*   r = (uint8_t*) (&mocc);

            // Since every square has its set of row values the eight row lookups
            // simply map any blockers to specific bits that when ored together
            // gives an offset in the rook attack table.

            //const uint32_t *rowR = _bRows_R[s][0]; // &_bRows_R[s][0][0];
            const uint32_t (*brdR)[PATTERN_R] = _bRows_R[s];

            const uint32_t index
                //= (rowR + 0*PATTERN_R)[(mocc >>  0) & 0xFF]  // row 1
                //| (rowR + 1*PATTERN_R)[(mocc >>  8) & 0xFF]  // row 2
                //| (rowR + 2*PATTERN_R)[(mocc >> 16) & 0xFF]  // row 3
                //| (rowR + 3*PATTERN_R)[(mocc >> 24) & 0xFF]  // row 4
                //| (rowR + 4*PATTERN_R)[(mocc >> 32) & 0xFF]  // row 5
                //| (rowR + 5*PATTERN_R)[(mocc >> 40) & 0xFF]  // row 6
                //| (rowR + 6*PATTERN_R)[(mocc >> 48) & 0xFF]  // row 7
                //| (rowR + 7*PATTERN_R)[(mocc >> 56) & 0xFF]; // row 8

                //= (rowR + 0*PATTERN_R)[r[0]]  // row 1
                //| (rowR + 1*PATTERN_R)[r[1]]  // row 2
                //| (rowR + 2*PATTERN_R)[r[2]]  // row 3
                //| (rowR + 3*PATTERN_R)[r[3]]  // row 4
                //| (rowR + 4*PATTERN_R)[r[4]]  // row 5
                //| (rowR + 5*PATTERN_R)[r[5]]  // row 6
                //| (rowR + 6*PATTERN_R)[r[6]]  // row 7
                //| (rowR + 7*PATTERN_R)[r[7]]; // row 8

                = brdR[0][r[0]] // row 1
                | brdR[1][r[1]] // row 2
                | brdR[2][r[2]] // row 3
                | brdR[3][r[3]] // row 4
                | brdR[4][r[4]] // row 5
                | brdR[5][r[5]] // row 6
                | brdR[6][r[6]] // row 7
                | brdR[7][r[7]];// row 8

            return index;
        }


        void initialize_table_B ();
        void initialize_table_R ();

    }

    void initialize_sliding ()
    {
        initialize_table_B ();
        initialize_table_R ();
    }

#pragma region Attacks

    template<>
    // BISHOP Attacks with occupancy
    Bitboard attacks_bb<BSHP> (Square s, Bitboard occ)
    {
        return _bbTable_B[index_attacks<BSHP> (s, occ)];
    }
    template<>
    // ROOK Attacks with occupancy
    Bitboard attacks_bb<ROOK> (Square s, Bitboard occ)
    {
        return _bbTable_R[index_attacks<ROOK> (s, occ)];
    }
    template<>
    // QUEEN Attacks with occupancy
    Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return
            _bbTable_B[index_attacks<BSHP> (s, occ)] |
            _bbTable_R[index_attacks<ROOK> (s, occ)];
    }

#pragma endregion

    namespace {

        void initialize_table_B ()
        {
            uint32_t index_base = 0;
            for (uint8_t b = 9; b >= 5; --b)
            {
                for (Square s = SQ_A1; s <= SQ_H8; ++s)
                {
                    if (_bBits_B[s] != b)  continue;

                    // Board edges are not considered in the relevant occupancies
                    const Bitboard edges = mask_brd_edges (s);
                    const Bitboard moves = _bb_attacks_type[BSHP][s]; //attacks_sliding (s, _deltas_type[BSHP]);

                    const Bitboard mask = moves & ~edges;

                    uint8_t shift_base = 0;
                    for (uint8_t row = 0; row < 6; ++row)
                    {
                        const uint16_t maskB = (mask >> (((row + 1) << 3) + 1)) & 0x3F;
                        for (uint16_t pattern = 0; pattern < PATTERN_B; ++pattern)
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

                            _bRows_B[s][row][pattern] = index_base + index;
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
                    //    _bbTable_B[index_base + index] = moves;
                    //}

                    uint32_t index = 0;
                    Bitboard occ = 0;
                    do
                    {
                        _bbTable_B[index_base + index] = attacks_sliding (s, _deltas_type[BSHP], occ);
                        ++index;
                        occ = (occ - mask) & mask;
                    }
                    while (occ);
                    uint32_t size = index;

                    // ---

                    index_base += size;  //(1 << b);
                }
            }
        }

        void initialize_table_R ()
        {
            const Delta DeltasR[] = { DEL_N, DEL_E, DEL_S, DEL_W };

            uint32_t index_base = 0;
            for (uint8_t b = 12; b >= 10; --b)
            {
                for (Square s = SQ_A1; s <= SQ_H8; ++s)
                {
                    if (_bBits_R[s] != b)  continue;

                    // Board edges are not considered in the relevant occupancies
                    const Bitboard edges = mask_brd_edges (s);
                    const Bitboard moves = _bb_attacks_type[ROOK][s]; //attacks_sliding (s, _deltas_type[ROOK]);

                    const Bitboard mask = moves & ~edges;

                    uint8_t shift_base = 0;
                    for (uint8_t row = 0; row < 8; ++row)
                    {
                        const uint16_t maskR = (mask >> (row << 3)) & 0xFF;
                        for (uint16_t pattern = 0; pattern < PATTERN_R; ++pattern)
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
                            _bRows_R[s][row][pattern] = index_base + index;
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
                    //    _bbTable_R[index_base + index] = moves;
                    //}

                    uint32_t index = 0;
                    Bitboard occ = 0;
                    do
                    {
                        _bbTable_R[index_base + index] = attacks_sliding (s, _deltas_type[ROOK], occ);
                        ++index;
                        occ = (occ - mask) & mask;
                    }
                    while (occ);
                    uint32_t size = index;

                    // ---

                    index_base += size;  //(1 << b);
                }
            }
        }
    }

}