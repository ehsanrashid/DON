#include "BitBoard.h"

#include <iostream>
#include "BitCount.h"
#include "BitScan.h"
#include "xstring.h"

namespace BitBoard {

    using namespace std;

#pragma region Constants

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

    const Bitboard LT_SQ_bb = U64 (0x55AA55AA55AA55AA);           // 32 LIGHT squares.
    const Bitboard DR_SQ_bb = U64 (0xAA55AA55AA55AA55);           // 32 DARK  squares.

    //const Bitboard bb_S_Q  = U64(0x0F0F0F0F0F0F0F0F); // 32 QUEEN side squares.
    //const Bitboard bb_S_K  = ~bb_S_Q;//U64(0xF0F0F0F0F0F0F0F0); // 32 KING  side squares.
    //
    //const Bitboard bb_COR  = U64(0x8100000000000081); // 04 CORNER squares.
    //const Bitboard bb_BRD  = U64(0xFF818181818181FF); // 28 BORDER squares.
    //
    //const Bitboard bb_CEN    = U64(0x0000001818000000); // 04 CENTER          squares.
    //const Bitboard bb_CEN_EX = U64(0x00003C3C3C3C0000); // 16 CENTER EXPANDED squares.
    //const Bitboard bb_HOL_EX = U64(0x00003C24243C0000); // 12 C-HOLE EXPANDED squares.

#pragma endregion

#pragma region LOOKUPs

    // FILE & RANK distance
    Delta _filerank_dist[F_NO][R_NO];
    Delta   _square_dist[SQ_NO][SQ_NO];
    Delta     _taxi_dist[SQ_NO][SQ_NO];

    //uint8_t _shift_gap[_UI8_MAX + 1][F_NO];
    const Delta _deltas_pawn[CLR_NO][3] =
    {
        { DEL_NW, DEL_NE },
        { DEL_SE, DEL_SW },
    };
    const Delta _deltas_type[PT_NO][9] =
    {
        {},
        { DEL_SSW, DEL_SSE, DEL_WWS, DEL_EES, DEL_WWN, DEL_EEN, DEL_NNW, DEL_NNE },
        { DEL_SW, DEL_SE, DEL_NW, DEL_NE, },
        { DEL_S, DEL_W, DEL_E, DEL_N },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE },
        { DEL_SW, DEL_S, DEL_SE, DEL_W, DEL_E, DEL_NW, DEL_N, DEL_NE },
    };

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

    CACHE_ALIGN(64) Bitboard _dia_rings_bb[SQ_NO][F_NO];

    // ---

    // Attacks of the pawn
    CACHE_ALIGN(64) Bitboard _attacks_pawn_bb[CLR_NO][SQ_NO];

    // Attacks of the pieces
    CACHE_ALIGN(64) Bitboard _attacks_type_bb[PT_NO][SQ_NO];

    // Span of the attacks of pawn
    CACHE_ALIGN(64) Bitboard _attack_span_pawn_bb[CLR_NO][SQ_NO];

    // Path of the passed pawn
    CACHE_ALIGN(64) Bitboard _passer_span_pawn_bb[CLR_NO][SQ_NO];

    CACHE_ALIGN(64) Bitboard _betwen_sq_bb[SQ_NO][SQ_NO];
    CACHE_ALIGN(64) Bitboard  _lines_sq_bb[SQ_NO][SQ_NO];

#pragma endregion

#pragma region Attacks

    Bitboard attacks_sliding (Square s, const Delta deltas[], Bitboard occ)
    {
        Bitboard attacks_slid = 0;
        int8_t i = 0;
        Delta del = deltas[i++];
        while (del)
        {
            Square sq = s + del;
            while (_ok (sq) && _square_dist[sq][sq - del] == 1)
            {
                attacks_slid += sq;
                if (occ & sq)
                    break;
                sq += del;
            }
            del = deltas[i++];
        }
        return attacks_slid;
    }

    template<>
    // PAWN attacks
    Bitboard attacks_bb<PAWN> (Color c, Square s) { return _attacks_pawn_bb[c][s]; }

    template<PType T>
    Bitboard attacks_bb (Square s) { return _attacks_type_bb[T][s]; }
    // --------------------------------
    // explicit template instantiations
    template Bitboard attacks_bb<NIHT> (Square s);
    template Bitboard attacks_bb<BSHP> (Square s);
    template Bitboard attacks_bb<ROOK> (Square s);
    template Bitboard attacks_bb<QUEN> (Square s);
    template Bitboard attacks_bb<KING> (Square s);
    // --------------------------------

    template<>
    // KNIGHT attacks
    Bitboard attacks_bb<NIHT> (Square s, Bitboard occ) { return _attacks_type_bb[NIHT][s]; }
    template<>
    // KING attacks
    Bitboard attacks_bb<KING> (Square s, Bitboard occ) { return _attacks_type_bb[KING][s]; }

    // Piece attacks from square
    Bitboard attacks_bb (Piece p, Square s, Bitboard occ)
    {
        switch (_ptype (p))
        {
        case PAWN: return attacks_bb<PAWN> (_color (p), s); break;
        case NIHT: return attacks_bb<NIHT>(s);      break;
        case BSHP: return attacks_bb<BSHP>(s, occ); break;
        case ROOK: return attacks_bb<ROOK>(s, occ); break;
        case QUEN: return attacks_bb<QUEN>(s, occ); break;
        case KING: return attacks_bb<KING>(s);      break;
        }
        return 0;
    }

#pragma endregion

    extern void initialize_sliding ();

    void initialize ()
    {

#pragma region Constant LOOKUPs

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

#pragma endregion

        for (File f = F_A; f <= F_H; ++f)
        {
            for (Rank r = R_1; r <= R_8; ++r)
            {
                int8_t d = int8_t (f) - int8_t (r);
                _filerank_dist[f][r] = Delta (abs (d));
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

                    Delta dFile = _filerank_dist[f1][f2];
                    Delta dRank = _filerank_dist[r1][r2];

                    _square_dist[s1][s2] = max (dFile, dRank);
                    _taxi_dist  [s1][s2] = (dFile + dRank);

                    _dia_rings_bb[s1][_square_dist[s1][s2] - 1] += s2;
                }
            }
        }

        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                _front_squares_bb   [c][s] = _front_rank_bb[c][_rank (s)] & _file_bb[_file (s)];
                _attack_span_pawn_bb[c][s] = _front_rank_bb[c][_rank (s)] & _adj_file_bb[_file (s)];
                _passer_span_pawn_bb[c][s] = _front_squares_bb[c][s] | _attack_span_pawn_bb[c][s];
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

            PType pt;

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

        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                if (_attacks_type_bb[QUEN][s1] & s2)
                {
                    Delta delta = offset_sq (s1, s2);
                    Square sq = s1 + delta;
                    while (sq != s2)
                    {
                        _betwen_sq_bb[s1][s2] += sq;
                        sq += delta;
                    }

                    PType pt = (_attacks_type_bb[BSHP][s1] & s2) ? BSHP : ROOK;
                    _lines_sq_bb[s1][s2] = (_attacks_type_bb[pt][s1] & _attacks_type_bb[pt][s2]) + s1 + s2;
                }
            }
        }

        initialize_sliding ();

    }

#pragma region Printing

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
        remove_if (sbb, isspace);

        size_t length = sbb.length ();
        ASSERT (SQ_NO == length);
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
            memset (buf, 0, sizeof (buf));
            _snprintf_s (buf, _countof (buf), sizeof (buf), "%02X", uint32_t (to_bitboard (sb, 2)));
            //sprintf_s(buf, sizeof (buf), "%02X", to_bitboard (sb, 2));
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

        const string dots = " -----------------\n";
        const string row   = "|. . . . . . . .|\n";
        const size_t len_row = row.length () + 1;

        sbb = dots;

        for (Rank r = R_8; r >= R_1; --r)
        {
            sbb += to_char (r) + row;
        }

        sbb += dots + " ";

        for (File f = F_A; f <= F_H; ++f)
        {
            sbb += " ";
            sbb += to_char (f);
        }

        sbb += "\n";

        while (bb)
        {
            Square s = pop_lsq (bb);
            int8_t r = _rank (s);
            int8_t f = _file (s);
            sbb[2 + len_row * (8 - r) + 2 * f] = p;
        }

        cout << sbb;
    }

#pragma endregion

    SquareList squares (Bitboard bb)
    {
        SquareList sq_list;

        //for (Square s = SQ_A1; s <= SQ_H8; ++s)
        //{
        //    if (bb & s) sq_list.emplace_back (s);
        //}

        // ---

        while (bb)
        {
            Square s = pop_lsq (bb);
            sq_list.emplace_back (s);
        }

        return sq_list;
    }

}
