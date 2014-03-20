#include "BitBoard.h"
#include "BitCount.h"
#include "RKISS.h"

namespace BitBoard {

    namespace {

        // max moves for rook from any corner square
        // 2 ^ 12 = 4096 = 0x1000
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


        Bitboard BTable_bb[MAX_B_MOVES];
        Bitboard RTable_bb[MAX_R_MOVES];

        Bitboard*BAttack_bb[SQ_NO];
        Bitboard*RAttack_bb[SQ_NO];

        Bitboard   BMask_bb[SQ_NO];
        Bitboard   RMask_bb[SQ_NO];

        Bitboard  BMagic_bb[SQ_NO];
        Bitboard  RMagic_bb[SQ_NO];

        u08      BShift[SQ_NO];
        u08      RShift[SQ_NO];

        typedef u16 (*Index) (Square s, Bitboard occ);

        template<PieceT PT>
        // Function 'magic_index(s, occ)' for computing index for sliding attack bitboards.
        // Function 'attacks_bb(s, occ)' takes a square and a bitboard of occupied squares as input,
        // and returns a bitboard representing all squares attacked by PT (BISHOP or ROOK) on the given square.
        u16 magic_index (Square s, Bitboard occ);

        template<>
        inline u16 magic_index<BSHP> (Square s, Bitboard occ)
        {

#ifdef _64BIT
            return u16 (((occ & BMask_bb[s]) * BMagic_bb[s]) >> BShift[s]);
#else
            u32 lo = (u32 (occ >>  0) & u32 (BMask_bb[s] >>  0)) * u32 (BMagic_bb[s] >>  0);
            u32 hi = (u32 (occ >> 32) & u32 (BMask_bb[s] >> 32)) * u32 (BMagic_bb[s] >> 32);
            return ((lo ^ hi) >> BShift[s]);
#endif

        }

        template<>
        inline u16 magic_index<ROOK> (Square s, Bitboard occ)
        {

#ifdef _64BIT
            return u16 (((occ & RMask_bb[s]) * RMagic_bb[s]) >> RShift[s]);
#else
            u32 lo = (u32 (occ >>  0) & u32 (RMask_bb[s] >>  0)) * u32 (RMagic_bb[s] >>  0);
            u32 hi = (u32 (occ >> 32) & u32 (RMask_bb[s] >> 32)) * u32 (RMagic_bb[s] >> 32);
            return ((lo ^ hi) >> RShift[s]);
#endif

        }

        void initialize_table (Bitboard table_bb[], Bitboard* attacks_bb[], Bitboard magics_bb[], Bitboard masks_bb[], u08 shift[], const Delta deltas[], const Index m_index);

    }

    void initialize_sliding ()
    {
        initialize_table (BTable_bb, BAttack_bb, BMagic_bb, BMask_bb, BShift, PieceDeltas[BSHP], magic_index<BSHP>);
        initialize_table (RTable_bb, RAttack_bb, RMagic_bb, RMask_bb, RShift, PieceDeltas[ROOK], magic_index<ROOK>);
    }

    template<>
    // Attacks of the BISHOP with occupancy
    Bitboard attacks_bb<BSHP> (Square s, Bitboard occ) { return BAttack_bb[s][magic_index<BSHP> (s, occ)]; }
    template<>
    // Attacks of the ROOK with occupancy
    Bitboard attacks_bb<ROOK> (Square s, Bitboard occ) { return RAttack_bb[s][magic_index<ROOK> (s, occ)]; }
    template<>
    // QUEEN Attacks with occ
    Bitboard attacks_bb<QUEN> (Square s, Bitboard occ)
    {
        return 
            BAttack_bb[s][magic_index<BSHP> (s, occ)] |
            RAttack_bb[s][magic_index<ROOK> (s, occ)];
    }

    namespace {

        void initialize_table (Bitboard table_bb[], Bitboard* attacks_bb[], Bitboard magics_bb[], Bitboard masks_bb[], u08 shift[], const Delta deltas[], const Index m_index)
        {

            u16 MagicBoosters[R_NO] =
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
                u32 size   = 0;
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
                        //if (pop_count<MAX15> (index) >= 6) break;
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
    }

}