#include "BitBoard.h"

#include "xstring.h"

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

    //const Bitboard NULL_bb =  U64(0);//U64 (0x0000000000000000);  // 00 NULL squares.
    //const Bitboard FULL_bb = ~U64(0);//U64 (0xFFFFFFFFFFFFFFFF);  // 64 FULL squares.

    const Bitboard R1_bb_ = ~R1_bb;//U64 (0xFFFFFFFFFFFFFF00);    // 56 Not RANK-1
    const Bitboard R8_bb_ = ~R8_bb;//U64 (0x00FFFFFFFFFFFFFF);    // 56 Not RANK-8
    const Bitboard FA_bb_ = ~FA_bb;//U64 (0xFEFEFEFEFEFEFEFE);    // 56 Not FILE-A
    const Bitboard FH_bb_ = ~FH_bb;//U64 (0x7F7F7F7F7F7F7F7F);    // 56 Not FILE-H

    const Bitboard D18_bb = U64 (0x8040201008040201);             // 08 DIAG-18 squares.
    const Bitboard D81_bb = U64 (0x0102040810204080);             // 08 DIAG-81 squares.

    const Bitboard LTSQ_bb = U64 (0x55AA55AA55AA55AA);            // 32 LIGHT squares.
    const Bitboard DRSQ_bb = U64 (0xAA55AA55AA55AA55);            // 32 DARK  squares.

    const Bitboard CORNER_bb = U64(0x8100000000000081);

    const Bitboard MID_EDGE_bb = (FA_bb | FH_bb) & (R2_bb | R3_bb);

    //const Bitboard QSQ_bb  = U64(0x0F0F0F0F0F0F0F0F); // 32 QUEEN side squares.
    //const Bitboard KSQ_bb  = ~QSQ_bb;//U64(0xF0F0F0F0F0F0F0F0); // 32 KING  side squares.
    //
    //const Bitboard COR_bb  = U64(0x8100000000000081); // 04 CORNER squares.
    //const Bitboard BOR_bb  = U64(0xFF818181818181FF); // 28 BORDER squares.
    //
    //const Bitboard CEN_bb    = U64(0x0000001818000000); // 04 CENTER          squares.
    //const Bitboard CEN_EX_bb = U64(0x00003C3C3C3C0000); // 16 CENTER EXPANDED squares.
    //const Bitboard HOL_EX_bb = U64(0x00003C24243C0000); // 12 C-HOLE EXPANDED squares.


    // FILE & RANK distance
    uint8_t _filerank_dist[F_NO][R_NO];
    uint8_t   _square_dist[SQ_NO][SQ_NO];
    uint8_t  _taxicab_dist[SQ_NO][SQ_NO];

    //uint8_t _shift_gap[_UI8_MAX + 1][F_NO];
    const Delta _deltas_pawn[CLR_NO][3] =
    {
        { DEL_NW, DEL_NE },
        { DEL_SE, DEL_SW },
    };
    const Delta _deltas_type[NONE][9] =
    {
        {},
        { DEL_SSW, DEL_SSE, DEL_WWS, DEL_EES, DEL_WWN, DEL_EEN, DEL_NNW, DEL_NNE },
        { DEL_SW, DEL_SE, DEL_NW, DEL_NE, },
        { DEL_S, DEL_W, DEL_E, DEL_N },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE },
    };

    //const uint8_t _center_dist[SQ_NO] = 
    //{
    //    3, 3, 3, 3, 3, 3, 3, 3,
    //    3, 2, 2, 2, 2, 2, 2, 3,
    //    3, 2, 1, 1, 1, 1, 2, 3,
    //    3, 2, 1, 0, 0, 1, 2, 3,
    //    3, 2, 1, 0, 0, 1, 2, 3,
    //    3, 2, 1, 1, 1, 1, 2, 3,
    //    3, 2, 2, 2, 2, 2, 2, 3,
    //    3, 3, 3, 3, 3, 3, 3, 3,
    //};

    //const uint8_t _manhattan_center_dist[SQ_NO] =
    //{
    //    6, 5, 4, 3, 3, 4, 5, 6,
    //    5, 4, 3, 2, 2, 3, 4, 5,
    //    4, 3, 2, 1, 1, 2, 3, 4,
    //    3, 2, 1, 0, 0, 1, 2, 3,
    //    3, 2, 1, 0, 0, 1, 2, 3,
    //    4, 3, 2, 1, 1, 2, 3, 4,
    //    5, 4, 3, 2, 2, 3, 4, 5,
    //    6, 5, 4, 3, 3, 4, 5, 6,
    //};

    // SQUARES
    CACHE_ALIGN(64) const Bitboard _square_bb[SQ_NO] =
    {
        //U64(0x0000000000000001), U64(0x0000000000000002), U64(0x0000000000000004), U64(0x0000000000000008),
        //U64(0x0000000000000010), U64(0x0000000000000020), U64(0x0000000000000040), U64(0x0000000000000080),
        //U64(0x0000000000000100), U64(0x0000000000000200), U64(0x0000000000000400), U64(0x0000000000000800),
        //U64(0x0000000000001000), U64(0x0000000000002000), U64(0x0000000000004000), U64(0x0000000000008000),
        //U64(0x0000000000010000), U64(0x0000000000020000), U64(0x0000000000040000), U64(0x0000000000080000),
        //U64(0x0000000000100000), U64(0x0000000000200000), U64(0x0000000000400000), U64(0x0000000000800000),
        //U64(0x0000000001000000), U64(0x0000000002000000), U64(0x0000000004000000), U64(0x0000000008000000),
        //U64(0x0000000010000000), U64(0x0000000020000000), U64(0x0000000040000000), U64(0x0000000080000000),
        //U64(0x0000000100000000), U64(0x0000000200000000), U64(0x0000000400000000), U64(0x0000000800000000),
        //U64(0x0000001000000000), U64(0x0000002000000000), U64(0x0000004000000000), U64(0x0000008000000000),
        //U64(0x0000010000000000), U64(0x0000020000000000), U64(0x0000040000000000), U64(0x0000080000000000),
        //U64(0x0000100000000000), U64(0x0000200000000000), U64(0x0000400000000000), U64(0x0000800000000000),
        //U64(0x0001000000000000), U64(0x0002000000000000), U64(0x0004000000000000), U64(0x0008000000000000),
        //U64(0x0010000000000000), U64(0x0020000000000000), U64(0x0040000000000000), U64(0x0080000000000000),
        //U64(0x0100000000000000), U64(0x0200000000000000), U64(0x0400000000000000), U64(0x0800000000000000),
        //U64(0x1000000000000000), U64(0x2000000000000000), U64(0x4000000000000000), U64(0x8000000000000000),

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
    CACHE_ALIGN(64) const Bitboard   _file_bb[F_NO] =
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
    CACHE_ALIGN(64) const Bitboard   _rank_bb[R_NO] =
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
    // DIAG-18
    CACHE_ALIGN(64) const Bitboard _diag18_bb[D_NO] =
    {
        D18_bb >> (8 * 7),
        D18_bb >> (8 * 6),
        D18_bb >> (8 * 5),
        D18_bb >> (8 * 4),
        D18_bb >> (8 * 3),
        D18_bb >> (8 * 2),
        D18_bb >> (8 * 1),
        D18_bb,
        D18_bb << (8 * 1),
        D18_bb << (8 * 2),
        D18_bb << (8 * 3),
        D18_bb << (8 * 4),
        D18_bb << (8 * 5),
        D18_bb << (8 * 6),
        D18_bb << (8 * 7),
    };
    // DIAG-81
    CACHE_ALIGN(64) const Bitboard _diag81_bb[D_NO] =
    {
        D81_bb >> (8 * 7),
        D81_bb >> (8 * 6),
        D81_bb >> (8 * 5),
        D81_bb >> (8 * 4),
        D81_bb >> (8 * 3),
        D81_bb >> (8 * 2),
        D81_bb >> (8 * 1),
        D81_bb,
        D81_bb << (8 * 1),
        D81_bb << (8 * 2),
        D81_bb << (8 * 3),
        D81_bb << (8 * 4),
        D81_bb << (8 * 5),
        D81_bb << (8 * 6),
        D81_bb << (8 * 7),
    };

    // ADJACENT FILES used for isolated-pawn
    CACHE_ALIGN(64) const Bitboard _adj_file_bb[F_NO] =
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
    CACHE_ALIGN(64) const Bitboard _adj_rank_bb[R_NO] =
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
    CACHE_ALIGN(64) const Bitboard _front_rank_bb[CLR_NO][R_NO] =
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
    CACHE_ALIGN(64) Bitboard _front_squares_bb[CLR_NO][SQ_NO];

    CACHE_ALIGN(64) Bitboard _dist_rings_bb[SQ_NO][F_NO];

    // ---

    CACHE_ALIGN(64) Bitboard _betwen_sq_bb[SQ_NO][SQ_NO];
    CACHE_ALIGN(64) Bitboard  _lines_sq_bb[SQ_NO][SQ_NO];

    // Span of the attacks of pawn
    CACHE_ALIGN(64) Bitboard _pawn_attack_span_bb[CLR_NO][SQ_NO];

    // Path of the passed pawn
    CACHE_ALIGN(64) Bitboard _passer_pawn_span_bb[CLR_NO][SQ_NO];

    // Attacks of the pawn
    CACHE_ALIGN(64) Bitboard _attacks_pawn_bb[CLR_NO][SQ_NO];

    // Attacks of the pieces
    CACHE_ALIGN(64) Bitboard _attacks_type_bb[NONE][SQ_NO];

    CACHE_ALIGN(64) Bitboard*BAttack_bb[SQ_NO];
    CACHE_ALIGN(64) Bitboard*RAttack_bb[SQ_NO];

    CACHE_ALIGN(64) Bitboard   BMask_bb[SQ_NO];
    CACHE_ALIGN(64) Bitboard   RMask_bb[SQ_NO];

    CACHE_ALIGN(64) Bitboard  BMagic_bb[SQ_NO];
    CACHE_ALIGN(64) Bitboard  RMagic_bb[SQ_NO];

    CACHE_ALIGN(8) uint8_t      BShift[SQ_NO];
    CACHE_ALIGN(8) uint8_t      RShift[SQ_NO];


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

        typedef uint16_t (*Indexer) (Square s, Bitboard occ);

        void initialize_table (Bitboard table_bb[], Bitboard* attacks_bb[], Bitboard magics_bb[], Bitboard masks_bb[], uint8_t shift[], const Delta deltas[], const Indexer indexer)
        {

            uint16_t _bMagicBoosters[R_NO] =
#if defined(_64BIT)
            { 0x423, 0xE18, 0x25D, 0xCA2, 0xCFE, 0x026, 0x7ED, 0xBE3, }; // 64-bit
#else
            { 0xC77, 0x888, 0x51E, 0xE22, 0x82B, 0x51C, 0x994, 0xF9C, }; // 32-bit
#endif

            Bitboard occupancy[MAX_MOVES];
            Bitboard reference[MAX_MOVES];

            RKISS rkiss;

            attacks_bb[SQ_A1] = table_bb;

            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                // Board edges are not considered in the relevant occupancies
                Bitboard edges = brd_edges_bb (s);

                // Given a square 's', the mask is the bitboard of sliding attacks from
                // 's' computed on an empty board. The index must be big enough to contain
                // all the attacks for each possible subset of the mask and so is 2 power
                // the number of 1s of the mask. Hence we deduce the size of the shift to
                // apply to the 64 or 32 bits word to get the index.
                Bitboard moves = attacks_sliding (s, deltas);

                Bitboard mask = masks_bb[s] = moves & ~edges;

                shift[s] =
#if defined(_64BIT)
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
                    reference[size] = attacks_sliding (s, deltas, occ);
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

                uint16_t booster = _bMagicBoosters[_rank (s)];

                // Find a magic for square 's' picking up an (almost) random number
                // until we find the one that passes the verification test.
                uint32_t i;

                do
                {
                    uint16_t index;
                    do
                    {
                        magics_bb[s] = rkiss.rand_boost<Bitboard> (booster);
                        index = (mask * magics_bb[s]) >> 0x38;
                        //if (pop_count<MAX15> (index) >= 6) break;
                    }
                    while (pop_count<MAX15> (index) < 6);

                    std::memset (attacks_bb[s], 0, size * sizeof (Bitboard));

                    // A good magic must map every possible occupancy to an index that
                    // looks up the correct sliding attack in the attacks_bb[s] database.
                    // Note that we build up the database for square 's' as a side
                    // effect of verifying the magic.
                    for (i = 0; i < size; ++i)
                    {
                        Bitboard &attacks = attacks_bb[s][indexer (s, occupancy[i])];

                        if (attacks && (attacks != reference[i]))
                            break;

                        ASSERT (reference[i]);
                        attacks = reference[i];
                    }
                }
                while (i < size);

            }
        }

        void initialize_sliding ()
        {
            initialize_table (BTable_bb, BAttack_bb, BMagic_bb, BMask_bb, BShift, _deltas_type[BSHP], indexer<BSHP>);
            initialize_table (RTable_bb, RAttack_bb, RMagic_bb, RMask_bb, RShift, _deltas_type[ROOK], indexer<ROOK>);

        }

    }

    void initialize ()
    {

        //for (Square s = SQ_A1; s <= SQ_H8; ++s)
        //{
        //    _square_bb[s] = U64(1) << s;
        //}

        //for (File f = F_A; f <= F_H; ++f)
        //{
        //    _file_bb[f] = f > F_A ? _file_bb[f - 1] << 1 : FA_bb;
        //}
        //for (Rank r = R_1; r <= R_8; ++r)
        //{
        //    _rank_bb[r] = r > R_1 ? _rank_bb[r - 1] << 8 : R1_bb;
        //}
        //for (File f = F_A; f <= F_H; ++f)
        //{
        //    _adj_file_bb[f] = (f > F_A ? _file_bb[f - 1] : 0) | (f < F_H ? _file_bb[f + 1] : 0);
        //}

        //for (Rank r = R_1; r < R_8; ++r)
        //{
        //    _front_rank_bb[WHITE][r] = ~(_front_rank_bb[BLACK][r + 1] = _front_rank_bb[BLACK][r] | _rank_bb[r]);
        //}

        for (File f = F_A; f <= F_H; ++f)
        {
            for (Rank r = R_1; r <= R_8; ++r)
            {
                _filerank_dist[f][r] = abs (int8_t (f) - int8_t (r));
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

                    uint8_t dFile = _filerank_dist[f1][f2];
                    uint8_t dRank = _filerank_dist[r1][r2];

                    _square_dist[s1][s2]  = max (dFile, dRank);
                    _taxicab_dist[s1][s2] = (dFile + dRank);

                    _dist_rings_bb[s1][_square_dist[s1][s2] - 1] += s2;
                }
            }
        }

        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                _front_squares_bb   [c][s] = _front_rank_bb[c][_rank (s)] & _file_bb[_file (s)];
                _pawn_attack_span_bb[c][s] = _front_rank_bb[c][_rank (s)] & _adj_file_bb[_file (s)];
                _passer_pawn_span_bb[c][s] = _front_squares_bb[c][s] | _pawn_attack_span_bb[c][s];
            }
        }

        //for (uint32_t occ = 0; occ <= _I8_MAX; ++occ)
        //{
        //    for (File f = F_A; f <= F_H; ++f)
        //    {
        //        if (!occ || (_square_bb[f] & occ))
        //        {
        //            _shift_gap[occ][f] = 0;
        //            continue;
        //        }
        //        // West Count
        //        int8_t count_w = 8;
        //        if (F_A < f) // west
        //        {
        //            count_w = 1;
        //            File fw = File (f - 1);
        //            while (F_A != fw && !(_square_bb[fw] & occ))
        //            {
        //                //if (F_A == fw || (_square_bb[fw] & occ)) break;
        //                ++count_w;
        //                --fw;
        //            }
        //        }
        //        // East Count
        //        int8_t count_e = 8;
        //        if (F_H > f) // east
        //        {
        //            count_e = 1;
        //            File fe = File (f + 1);
        //            while (F_H != fe && !(_square_bb[fe] & occ))
        //            {
        //                //if (F_H == fe || (_square_bb[fe] & occ)) break;
        //                ++count_e;
        //                ++fe;
        //            }
        //        }
        //
        //        _shift_gap[occ][f] = min (count_w, count_e);
        //    }
        //}

        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            for (Color c = WHITE; c <= BLACK; ++c)
            {
                for (uint32_t k = 0; _deltas_pawn[c][k]; ++k)
                {
                    Square sq = s + _deltas_pawn[c][k];

                    if (_ok (sq) && _square_dist[s][sq] == 1)
                    {
                        _attacks_pawn_bb[c][s] += sq;
                    }
                }
            }

            PieceT pt;

            pt = NIHT;
            for (uint32_t k = 0; _deltas_type[pt][k]; ++k)
            {
                Square sq = s + _deltas_type[pt][k];
                if (_ok (sq) && _square_dist[s][sq] == 2)
                {
                    _attacks_type_bb[pt][s] += sq;
                }
            }

            pt = KING;
            for (uint32_t k = 0; _deltas_type[pt][k]; ++k)
            {
                Square sq = s + _deltas_type[pt][k];
                if (_ok (sq) && _square_dist[s][sq] == 1)
                {
                    _attacks_type_bb[pt][s] += sq;
                }
            }

            _attacks_type_bb[BSHP][s] = attacks_sliding (s, _deltas_type[BSHP]);
            _attacks_type_bb[ROOK][s] = attacks_sliding (s, _deltas_type[ROOK]);
            _attacks_type_bb[QUEN][s] = _attacks_type_bb[BSHP][s] | _attacks_type_bb[ROOK][s];
        }

        initialize_sliding ();

        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                // NOTE:: must be called after initialize_sliding()

                PieceT pt = 
                    (attacks_bb<BSHP> (s1) & s2) ? BSHP :
                    (attacks_bb<ROOK> (s1) & s2) ? ROOK : NONE;

                if (NONE == pt) continue;

                _betwen_sq_bb[s1][s2] = attacks_bb (Piece (pt), s1, square_bb (s2)) & attacks_bb (Piece (pt), s2, square_bb (s1));
                //_lines_sq_bb[s1][s2] = (_attacks_type_bb[pt][s1] & _attacks_type_bb[pt][s2]) + s1 + s2;
                _lines_sq_bb [s1][s2] = (attacks_bb (Piece (pt), s1, U64 (0)) & attacks_bb (Piece (pt), s2, U64 (0))) + s1 + s2;
            }
        }

    }


    // Convert a char arr to a Bitboard (uint64_t) using radix
    Bitboard to_bitboard (const char s[], int32_t radix)
    {
        return _strtoui64 (s, NULL, radix);
    }
    // Convert a string to a Bitboard (uint64_t) using radix
    Bitboard to_bitboard (const string &s, int32_t radix)
    {
        return _strtoui64 (s.c_str (), NULL, radix);
    }
    // Convert bin string to hex string
    string to_hex_str (string &sbb)
    {
        remove_if (sbb, ::isspace);

        uint8_t length = sbb.length ();
        //ASSERT (SQ_NO == length);
        if (SQ_NO != length) return "";

        string shex = "0x";
        for (Rank r = R_1; r <= R_8; ++r)
        {
            string sb = sbb.substr (r * 8, 8);

            //for (int8_t n = 1; n >= 0; --n)
            //{
            //    string nibble_s = sb.substr(n * 4, 4);
            //    if (empty(nibble_s)) break;
            //    else if (nibble_s == "0000") shex += "0";
            //    else if (nibble_s == "1000") shex += "1";
            //    else if (nibble_s == "0100") shex += "2";
            //    else if (nibble_s == "1100") shex += "3";
            //    else if (nibble_s == "0010") shex += "4";
            //    else if (nibble_s == "1010") shex += "5";
            //    else if (nibble_s == "0110") shex += "6";
            //    else if (nibble_s == "1110") shex += "7";
            //    else if (nibble_s == "0001") shex += "8";
            //    else if (nibble_s == "1001") shex += "9";
            //    else if (nibble_s == "0101") shex += "A";
            //    else if (nibble_s == "1101") shex += "B";
            //    else if (nibble_s == "0011") shex += "C";
            //    else if (nibble_s == "1011") shex += "D";
            //    else if (nibble_s == "0111") shex += "E";
            //    else if (nibble_s == "1111") shex += "F";
            //    else break;
            //}

            reverse (sb);

            char buf[3];
            std::memset (buf, 0, sizeof (buf));
            sprintf (buf, "%02X", to_bitboard (sb, 2));
            //sprintf_s (buf, sizeof (buf), "%02X", to_bitboard (sb, 2));
            //_snprintf_s (buf, _countof (buf), sizeof (buf), "%02X", uint32_t (to_bitboard (sb, 2)));

            shex += buf;
        }
        return shex;
    }

    // Convert x-bits of Bitboard to string
    void print_bit (Bitboard bb, uint8_t x, char p)
    {
        //string sbit;
        string sbit (x + (x-1) / CHAR_BIT, '.');

        //size_t x = sizeof (bb) * CHAR_BIT; // if uint32_t
        uint64_t mask = U64 (1) << (x - 1);
        uint8_t sep = 0;
        for (uint8_t i = 0; i < x; ++i)
        {
            //sbit.append (1, (bb & mask) ? p : '.');
            if (bb & mask) sbit[i + sep] = p;

            if ((x - (i + 1)) % CHAR_BIT == 0 && (i + sep + 1) < (x))
            {
                //sbit.append (1, ' ');
                ++sep;
                sbit[i + sep] = ' ';
            }

            mask >>= 1;
        }
        cout << sbit << " = " << bb;
    }

    // Convert a Bitboard (uint64_t) to Bitboard (bin-string)
    void print_bin (Bitboard bb)
    {
        string sbin;
        for (Rank r = R_8; r >= R_1; --r)
        {
            for (File f = F_A; f <= F_H; ++f)
            {
                sbin.append (bb & (f | r) ? "1" : "0");
            }
            sbin.append ("\n");
        }
        cout << sbin;
    }

    // Print a Bitboard (uint64_t) to console output
    // Bitboard in an easily readable format. This is sometimes useful for debugging.
    void print (Bitboard bb, char p)
    {
        string sbb;

        //const string h_line = " -----------------";
        //const string v_line = "|";
        //sbb.append (h_line).append ("\n");
        //for (Rank r = R_8; r >= R_1; --r)
        //{
        //    sbb.append (1, to_char (r)).append (v_line);
        //    // print byte of rank [bitrank]
        //    for (File f = F_A; f <= F_H; ++f)
        //    {
        //        sbb.append (1, (bb & (f | r)) ? p1 : p0);
        //        if (F_H > f) sbb.append (" ");
        //    }
        //    sbb.append (v_line).append ("\n");
        //}
        //sbb.append (h_line).append ("\n").append (" ");
        //for (File f = F_A; f <= F_H; ++f) sbb.append (" ").append (1, to_char (f, false));
        //sbb.append ("\n");

        const string row   = "|. . . . . . . .|\n";
        const uint8_t row_len = row.length () + 1;
        //" <--------------->\n"
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

    //vector<Square> squares (Bitboard bb)
    //{
    //    vector<Square> sq_lst;
    //
    //    //for (Square s = SQ_A1; s <= SQ_H8; ++s)
    //    //{
    //    //    if (bb & s) sq_lst.push_back (s);
    //    //}
    //
    //    while (bb)
    //    {
    //        Square s = pop_lsq (bb);
    //        sq_lst.push_back (s);
    //    }
    //
    //    return sq_lst;
    //}

}
