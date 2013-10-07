#include "BitBoard.h"
#include "BitCount.h"
#include "RKISS.h"

namespace BitBoard {

    namespace {

        // max moves for rook from any corner square
        // 2 ^ 12 = 4096 = 0x1000
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


        Bitboard _bbTable_B[MAX_MOVES_B];
        Bitboard _bbTable_R[MAX_MOVES_R];

        Bitboard*_bbAttacksB[SQ_NO];
        Bitboard*_bbAttacksR[SQ_NO];

        Bitboard _bbMasks_B[SQ_NO];
        Bitboard _bbMasks_R[SQ_NO];

        Bitboard _bbMagics_B[SQ_NO];
        Bitboard _bbMagics_R[SQ_NO];

        uint8_t _bShifts_B[SQ_NO];
        uint8_t _bShifts_R[SQ_NO];

        typedef uint16_t (*Indexer) (Square s, Bitboard occ);

        template<PType T>
        // Function 'index_attacks(s, occ)' for computing index for sliding attack bitboards.
        // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
        // and returns a bitboard representing all squares attacked by T (BISHOP or ROOK) on the given square.
        uint16_t index_attacks (Square s, Bitboard occ);

        template<>
        uint16_t index_attacks<BSHP> (Square s, Bitboard occ)
        {
#ifdef _WIN64

            return uint16_t (((occ & _bbMasks_B[s]) * _bbMagics_B[s]) >> _bShifts_B[s]);

#else

            uint32_t lo = (uint32_t (occ >>  0) & uint32_t (_bbMasks_B[s] >>  0)) * uint32_t (_bbMagics_B[s] >>  0);
            uint32_t hi = (uint32_t (occ >> 32) & uint32_t (_bbMasks_B[s] >> 32)) * uint32_t (_bbMagics_B[s] >> 32);
            return ((lo ^ hi) >> _bShifts_B[s]);

#endif
        }
        template<>
        uint16_t index_attacks<ROOK> (Square s, Bitboard occ)
        {
#ifdef _WIN64

            return uint16_t (((occ & _bbMasks_R[s]) * _bbMagics_R[s]) >> _bShifts_R[s]);

#else

            uint32_t lo = (uint32_t (occ >>  0) & uint32_t (_bbMasks_R[s] >>  0)) * uint32_t (_bbMagics_R[s] >>  0);
            uint32_t hi = (uint32_t (occ >> 32) & uint32_t (_bbMasks_R[s] >> 32)) * uint32_t (_bbMagics_R[s] >> 32);
            return ((lo ^ hi) >> _bShifts_R[s]);

#endif
        }


        void initialize_table (Bitboard bbTable[], Bitboard* bbAttacks[], Bitboard bbMagics[], Bitboard bbMasks[], uint8_t bShifts[], const Delta Deltas[], const Indexer indexer);

    }


    void initialize_sliding ()
    {
        initialize_table (_bbTable_B, _bbAttacksB, _bbMagics_B, _bbMasks_B, _bShifts_B, _deltas_type[BSHP], index_attacks<BSHP>);
        initialize_table (_bbTable_R, _bbAttacksR, _bbMagics_R, _bbMasks_R, _bShifts_R, _deltas_type[ROOK], index_attacks<ROOK>);
    }


#pragma region Attacks

    template<>
    // BISHOP Attacks
    Bitboard attacks_bb<BSHP> (Square s)
    {
        return _bb_attacks_type[BSHP][s];
    }
    template<>
    // Attacks of the BISHOP with occupancy
    Bitboard attacks_bb<BSHP> (Square s, Bitboard occ)
    {
        return _bbAttacksB[s][index_attacks<BSHP>(s, occ)];
    }

    template<>
    // ROOK Attacks
    Bitboard attacks_bb<ROOK> (Square s)
    {
        return _bb_attacks_type[ROOK][s];
    }
    template<>
    // Attacks of the ROOK with occupancy
    Bitboard attacks_bb<ROOK> (Square s, Bitboard occ)
    {
        return _bbAttacksR[s][index_attacks<ROOK>(s, occ)];
    }

    template<>
    // QUEEN Attacks
    Bitboard attacks_bb<QUEN> (Square s)
    {
        return _bb_attacks_type[QUEN][s];
    }
    template<>
    // QUEEN Attacks with occ
    Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return 
            _bbAttacksB[s][index_attacks<BSHP>(s, occ)] |
            _bbAttacksR[s][index_attacks<ROOK>(s, occ)];
    }

#pragma endregion

    namespace {

        void initialize_table (Bitboard bbTable[], Bitboard* bbAttacks[], Bitboard bbMagics[], Bitboard bbMasks[], uint8_t bShifts[], const Delta Deltas[], const Indexer indexer)
        {

            uint16_t _bMagicBoosters[R_NO] =
#if defined(_WIN64)
            { 0x423, 0xE18, 0x25D, 0xCA2, 0xCFE, 0x026, 0x7ED, 0xBE3, }; // 64-bit
#else
            { 0xC77, 0x888, 0x51E, 0xE22, 0x82B, 0x51C, 0x994, 0xF9C, }; // 32-bit
#endif


            Bitboard occupancy[MAX_MOVES];
            Bitboard reference[MAX_MOVES];

            RKISS rkiss;

            bbAttacks[SQ_A1] = bbTable;

            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                // Board edges are not considered in the relevant occupancies
                Bitboard edges = mask_brd_edges (s);

                // Given a square 's', the mask is the bitboard of sliding attacks from
                // 's' computed on an empty board. The index must be big enough to contain
                // all the attacks for each possible subset of the mask and so is 2 power
                // the number of 1s of the mask. Hence we deduce the size of the shift to
                // apply to the 64 or 32 bits word to get the index.
                Bitboard moves = attacks_sliding (s, Deltas);

                //print (moves);

                Bitboard mask = bbMasks[s] = moves & ~edges;

                bShifts[s] =
#if defined(_WIN64)
                    64 - pop_count<MAX15> (mask);
#else
                    32 - pop_count<MAX15> (mask);
#endif

                // Use Carry-Rippler trick to enumerate all subsets of bbMasks[s] and
                // store the corresponding sliding attack bitboard in reference[].
                uint32_t size   = 0;
                Bitboard occ    = 0;

                do
                {
                    occupancy[size] = occ;
                    reference[size] = attacks_sliding (s, Deltas, occ);
                    ++size;

                    occ = (occ - mask) & mask;
                }
                while (occ);

                // Set the offset for the bbTable of the next square. We have individual
                // bbTable sizes for each square with "Fancy Magic Bitboards".
                if (s < SQ_H8)
                {
                    bbAttacks[s + 1] = bbAttacks[s] + size;
                }

                uint16_t booster = _bMagicBoosters[_rank (s)];

                // Find a magic for square 's' picking up an (almost) random number
                // until we find the one that passes the verification test.
                uint32_t i;

                do
                {
                    uint8_t index;
                    do
                    {
                        bbMagics[s] = rkiss.rand_boost<Bitboard>(booster);
                        index = (mask * bbMagics[s]) >> 0x38;
                        //if (pop_count<MAX15> (index) >= 6) break;
                    }
                    while (pop_count<MAX15> (index) < 6);

                    memset (bbAttacks[s], 0, size * sizeof (Bitboard));

                    // A good magic must map every possible occupancy to an index that
                    // looks up the correct sliding attack in the bbAttacks[s] database.
                    // Note that we build up the database for square 's' as a side
                    // effect of verifying the magic.
                    for (i = 0; i < size; ++i)
                    {
                        Bitboard &attacks = bbAttacks[s][indexer (s, occupancy[i])];

                        if (attacks && (attacks != reference[i]))
                            break;

                        ASSERT (reference[i]);
                        attacks = reference[i];
                    }
                }
                while (i < size);

            }
        }

    }


}