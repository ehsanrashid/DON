#include "BitBoard.h"

#include <iostream>
#include "BitCount.h"
#include "BitScan.h"
#include "xstring.h"
#include "RKISS.h"

namespace BitBoard {

#pragma region Constants

    const Bitboard bb_FA = U64 (0x0101010101010101);
    const Bitboard bb_FB = bb_FA << 1;//U64 (0x0202020202020202);
    const Bitboard bb_FC = bb_FA << 2;//U64 (0x0404040404040404);
    const Bitboard bb_FD = bb_FA << 3;//U64 (0x0808080808080808);
    const Bitboard bb_FE = bb_FA << 4;//U64 (0x1010101010101010);
    const Bitboard bb_FF = bb_FA << 5;//U64 (0x2020202020202020);
    const Bitboard bb_FG = bb_FA << 6;//U64 (0x4040404040404040);
    const Bitboard bb_FH = bb_FA << 7;//U64 (0x8080808080808080);

    const Bitboard bb_R1 = U64 (0x00000000000000FF);
    const Bitboard bb_R2 = bb_R1 << (8 * 1);//U64 (0x000000000000FF00);
    const Bitboard bb_R3 = bb_R1 << (8 * 2);//U64 (0x0000000000FF0000);
    const Bitboard bb_R4 = bb_R1 << (8 * 3);//U64 (0x00000000FF000000);
    const Bitboard bb_R5 = bb_R1 << (8 * 4);//U64 (0x000000FF00000000);
    const Bitboard bb_R6 = bb_R1 << (8 * 5);//U64 (0x0000FF0000000000);
    const Bitboard bb_R7 = bb_R1 << (8 * 6);//U64 (0x00FF000000000000);
    const Bitboard bb_R8 = bb_R1 << (8 * 7);//U64 (0xFF00000000000000);

    const Bitboard bb_NULL = U64 (0x0000000000000000);             // 00 NULL squares.
    const Bitboard bb_FULL = ~bb_NULL;//U64 (0xFFFFFFFFFFFFFFFF);  // 64 FULL squares.

    const Bitboard bb_R1_ = ~bb_R1;//U64 (0xFFFFFFFFFFFFFF00);    // 56 Not RANK-1
    const Bitboard bb_R8_ = ~bb_R8;//U64 (0x00FFFFFFFFFFFFFF);    // 56 Not RANK-8
    const Bitboard bb_FA_ = ~bb_FA;//U64 (0xFEFEFEFEFEFEFEFE);    // 56 Not FILE-A
    const Bitboard bb_FH_ = ~bb_FH;//U64 (0x7F7F7F7F7F7F7F7F);    // 56 Not FILE-H

    const Bitboard bb_D18 = U64 (0x8040201008040201);             // 08 DIAG-18 squares.
    const Bitboard bb_D81 = U64 (0x0102040810204080);             // 08 DIAG-81 squares.

    const Bitboard bb_SQ_W = U64 (0x55AA55AA55AA55AA); // 32 WHITE squares.
    const Bitboard bb_SQ_B = ~bb_SQ_W;//U64 (0xAA55AA55AA55AA55);  // 32 BLACK squares.

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

    //namespace LookUp {

    Delta _del_file_rank[F_NO][R_NO];
    Delta _del_sq[SQ_NO][SQ_NO];
    Delta _del_taxi[SQ_NO][SQ_NO];

    uint8_t _b_shift_gap[_UI8_MAX + 1][F_NO];


    CACHE_ALIGN64

        // SQUARES
        const Bitboard _bb_sq[SQ_NO] =
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
    const Bitboard _bb_file[F_NO] =
    {
        bb_FA,
        bb_FB,
        bb_FC,
        bb_FD,
        bb_FE,
        bb_FF,
        bb_FG,
        bb_FH
    };
    // RANKS
    const Bitboard _bb_rank[R_NO] =
    {
        bb_R1,
        bb_R2,
        bb_R3,
        bb_R4,
        bb_R5,
        bb_R6,
        bb_R7,
        bb_R8
    };
    // DIAG-18
    const Bitboard _bb_d18[D_NO] =
    {
        bb_D18 >> (8 * 7),
        bb_D18 >> (8 * 6),
        bb_D18 >> (8 * 5),
        bb_D18 >> (8 * 4),
        bb_D18 >> (8 * 3),
        bb_D18 >> (8 * 2),
        bb_D18 >> (8 * 1),
        bb_D18,
        bb_D18 << (8 * 1),
        bb_D18 << (8 * 2),
        bb_D18 << (8 * 3),
        bb_D18 << (8 * 4),
        bb_D18 << (8 * 5),
        bb_D18 << (8 * 6),
        bb_D18 << (8 * 7),
    };
    // DIAG-81
    const Bitboard _bb_d81[D_NO] =
    {
        bb_D81 >> (8 * 7),
        bb_D81 >> (8 * 6),
        bb_D81 >> (8 * 5),
        bb_D81 >> (8 * 4),
        bb_D81 >> (8 * 3),
        bb_D81 >> (8 * 2),
        bb_D81 >> (8 * 1),
        bb_D81,
        bb_D81 << (8 * 1),
        bb_D81 << (8 * 2),
        bb_D81 << (8 * 3),
        bb_D81 << (8 * 4),
        bb_D81 << (8 * 5),
        bb_D81 << (8 * 6),
        bb_D81 << (8 * 7),
    };

    // ADJACENT FILES used for isolated-pawn
    Bitboard _bb_adj_file[F_NO] =
    {
        bb_FB,
        bb_FA | bb_FC,
        bb_FB | bb_FD,
        bb_FC | bb_FE,
        bb_FD | bb_FF,
        bb_FE | bb_FG,
        bb_FF | bb_FH,
        bb_FG
    };
    // ADJACENT RANKS
    Bitboard _bb_adj_rank[R_NO] =
    {
        bb_R2,
        bb_R1 | bb_R3,
        bb_R2 | bb_R4,
        bb_R3 | bb_R5,
        bb_R4 | bb_R6,
        bb_R5 | bb_R7,
        bb_R6 | bb_R8,
        bb_R7,
    };
    // FRONT RANK
    Bitboard _bb_front_rank[CLR_NO][R_NO] =
    {
        bb_R2 | bb_R3 | bb_R4 | bb_R5 | bb_R6 | bb_R7 | bb_R8,
        bb_R3 | bb_R4 | bb_R5 | bb_R6 | bb_R7 | bb_R8,
        bb_R4 | bb_R5 | bb_R6 | bb_R7 | bb_R8,
        bb_R5 | bb_R6 | bb_R7 | bb_R8,
        bb_R6 | bb_R7 | bb_R8,
        bb_R7 | bb_R8,
        bb_R8,
        bb_NULL,

        bb_NULL,
        bb_R1,
        bb_R2 | bb_R1,
        bb_R3 | bb_R2 | bb_R1,
        bb_R4 | bb_R3 | bb_R2 | bb_R1,
        bb_R5 | bb_R4 | bb_R3 | bb_R2 | bb_R1,
        bb_R6 | bb_R5 | bb_R4 | bb_R3 | bb_R2 | bb_R1,
        bb_R7 | bb_R6 | bb_R5 | bb_R4 | bb_R3 | bb_R2 | bb_R1
    };
    // FRONT SQUARES
    Bitboard _bb_front_sq[CLR_NO][SQ_NO];

    Bitboard _bb_dia_rings[SQ_NO][F_NO];

    // ---

    // Attacks of the pawn
    Bitboard _bb_attacks_pawn[CLR_NO][SQ_NO];

    // Attacks of the pieces
    Bitboard _bb_attacks_type[PT_NO][SQ_NO];

    // Span of the attacks of pawn
    Bitboard _bb_attack_span_pawn[CLR_NO][SQ_NO];

    // Path of the passed pawn
    Bitboard _bb_passer_span_pawn[CLR_NO][SQ_NO];

    Bitboard _bb_betwen_sq[SQ_NO][SQ_NO];


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
    //}

#pragma endregion

#pragma region Attacks

    Bitboard attacks_sliding (Square s, const Delta deltas[], Bitboard occ)
    {
        Bitboard attacks_slid = bb_NULL;
        int8_t i = 0;
        Delta del = deltas[i++];
        while (DEL_O != del)
        {
            Square sq = s + del;
            while (_ok (sq) && _del_sq[sq][sq - del] == 1)
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
    Bitboard attacks_bb<PAWN> (Color c, Square s)
    {
        return _bb_attacks_pawn[c][s];
    }

    template<>
    // KNIGHT attacks
    Bitboard attacks_bb<NIHT> (Square s)
    {
        return _bb_attacks_type[NIHT][s];
    }
    template<>
    // KNIGHT attacks
    Bitboard attacks_bb<NIHT> (Square s, Bitboard occ)
    {
        return _bb_attacks_type[NIHT][s];
    }

    template<>
    // KING attacks
    Bitboard attacks_bb<KING> (Square s)
    {
        return _bb_attacks_type[KING][s];
    }
    template<>
    // KING attacks
    Bitboard attacks_bb<KING> (Square s, Bitboard occ)
    {
        return _bb_attacks_type[KING][s];
    }


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
        return bb_NULL;
    }

#pragma endregion

    extern void initialize_sliding ();

    void initialize ()
    {

#pragma region Constant LOOKUPs

        //for (Square s = SQ_A1; s <= SQ_H8; ++s)
        //{
        //    _bb_sq[s] = U64(1) << s;
        //}

        //_bb_file[F_A] = bb_FA;
        //_bb_rank[R_1] = bb_R1;
        //for (uint8_t i = 1; i < 8; ++i)
        //{
        //    _bb_file[i] = _bb_file[i - 1] << 1;
        //    _bb_rank[i] = _bb_rank[i - 1] << 8;
        //}

        //for (File f = F_A; f <= F_H; ++f)
        //{
        //    _BB_ADJ_F[f] = 
        //        (f > F_A ? _bb_file[f - 1] : 0) | 
        //        (f < F_H ? _bb_file[f + 1] : 0);
        //    _FileAdjFilesBB[f] = _bb_file[f] | _BB_ADJ_F[f];
        //}

        //for (Rank r = R_1; r < R_8; ++r)
        //{
        //    _BB_FRT_R[WHITE][r] = ~(_BB_FRT_R[BLACK][r + 1] = _BB_FRT_R[BLACK][r] | _bb_rank[r]);
        //}

        //_CountByte[0] = 0;
        //for (uint8_t i = 1; ; ++i)
        //{
        //    _CountByte[i] = (i & 1) + _CountByte[i / 2];
        //    if (_UI8_MAX == i) break;
        //}

#pragma endregion

        for (File f = F_A; f <= F_H; ++f)
        {
            for (Rank r = R_1; r <= R_8; ++r)
            {
                int8_t d = int8_t (f) - int8_t (r);
                _del_file_rank[f][r] = Delta (abs (d));
            }
        }

        for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        {
            File f1 = _file (s1);
            Rank r1 = _rank (s1);
            for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            {
                File f2 = _file (s2);
                Rank r2 = _rank (s2);

                Delta dFile = _del_file_rank[f1][f2];
                Delta dRank = _del_file_rank[r1][r2];

                _del_sq[s1][s2] = ::std::max<Delta> (dFile, dRank);
                _del_taxi[s1][s2] = (dFile + dRank);
                if (s1 != s2)
                {
                    _bb_dia_rings[s1][_del_sq[s1][s2] - 1] |= s2;
                }
            }
        }

        for (Color c = WHITE; c <= BLACK; ++c)
        {
            for (Square s = SQ_A1; s <= SQ_H8; ++s)
            {
                _bb_front_sq[c][s] = _bb_front_rank[c][_rank (s)] & _bb_file[_file (s)];
                _bb_attack_span_pawn[c][s] = _bb_front_rank[c][_rank (s)] & _bb_adj_file[_file (s)];
                _bb_passer_span_pawn[c][s] = _bb_front_sq[c][s] | _bb_attack_span_pawn[c][s];
            }
        }

        for (size_t occ = 0; occ <= _I8_MAX; ++occ)
        {
            for (File f = F_A; f <= F_H; ++f)
            {
                if (!occ || (_bb_sq[f] & occ))
                {
                    _b_shift_gap[occ][f] = 0;
                    continue;
                }
                // West Count
                int8_t countW = 8;
                if (F_A < f) // west
                {
                    countW = 1;
                    File w = File (f - 1);
                    while (F_A != w && !(_bb_sq[w] & occ))
                    {
                        //if (F_A == w || (_bb_sq[w] & occ)) break;
                        ++countW;
                        --w;
                    }
                }
                // East Count
                int8_t countE = 8;
                if (F_H > f) // east
                {
                    countE = 1;
                    File e = File (f + 1);
                    while (F_H != e && !(_bb_sq[e] & occ))
                    {
                        //if (F_H == e || (_bb_sq[e] & occ)) break;
                        ++countE;
                        ++e;
                    }
                }

                _b_shift_gap[occ][f] = ::std::min (countW, countE);
            }
        }

        for (Square s = SQ_A1; s <= SQ_H8; ++s)
        {
            for (Color c = WHITE; c <= BLACK; ++c)
            {
                for (size_t k = 0; _deltas_pawn[c][k]; ++k)
                {
                    Square sq = s + _deltas_pawn[c][k];

                    if (_ok (sq) && _del_sq[s][sq] == 1)
                    {
                        _bb_attacks_pawn[c][s] += sq;
                    }
                }
            }

            PType type;

            type = NIHT;
            for (size_t k = 0; _deltas_type[type][k]; ++k)
            {
                Square sq = s + _deltas_type[type][k];
                if (_ok (sq) && _del_sq[s][sq] == 2)
                {
                    _bb_attacks_type[type][s] += sq;
                }
            }

            type = KING;
            for (size_t k = 0; _deltas_type[type][k]; ++k)
            {
                Square sq = s + _deltas_type[type][k];
                if (_ok (sq) && _del_sq[s][sq] == 1)
                {
                    _bb_attacks_type[type][s] += sq;
                }
            }

            _bb_attacks_type[BSHP][s] = attacks_sliding (s, _deltas_type[BSHP]);
            _bb_attacks_type[ROOK][s] = attacks_sliding (s, _deltas_type[ROOK]);;
            _bb_attacks_type[QUEN][s] = _bb_attacks_type[BSHP][s] | _bb_attacks_type[ROOK][s];

            for (Square d = SQ_A1; d <= SQ_H8; ++d)
            {
                if (_bb_attacks_type[QUEN][s] & d)
                {
                    Delta delta = offset_sq (s, d);
                    Square sq = s + delta;
                    while (sq != d)
                    {
                        _bb_betwen_sq[s][d] += sq;
                        sq += delta;
                    }
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
    Bitboard to_bitboard (const ::std::string &s, int32_t radix)
    {
        return _strtoui64 (s.c_str (), NULL, radix);
    }
    // Convert bin string to hex string
    ::std::string to_hex_str (std::string &sbb)
    {
        remove_if (sbb, isspace);

        size_t length = sbb.length ();
        ASSERT (SQ_NO == length);
        if (SQ_NO != length) return "";

        ::std::string shex = "0x";
        for (Rank r = R_1; r <= R_8; ++r)
        {
            ::std::string sb = sbb.substr (r * 8, 8);

            //// Invert
            //rforeach (int8_t, 0, 1, n)
            //{
            //    string nibble_s = sb.substr(n * 4, 4);
            //    if (isempty(nibble_s)) break;
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

            ::std::reverse (sb);

            char buf[3];
            ::std::memset (buf, 0, sizeof (buf));
            _snprintf_s (buf, _countof (buf), sizeof (buf), "%02X", uint32_t (to_bitboard (sb, 2)));
            //sprintf_s(buf, sizeof (buf), "%02X", to_bitboard (sb, 2));
            shex += buf;
        }
        return shex;
    }

    // Convert x-bits of Bitboard to string
    void print_bit (Bitboard bb, uint8_t x, char p)
    {
        //std::string sbit;
        ::std::string sbit (x + (x-1) / CHAR_BIT, '.');

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
        ::std::cout << sbit << " = " << bb;
    }

    // Convert a Bitboard (uint64_t) to Bitboard (bin-string)
    void print_bin (Bitboard bb)
    {
        ::std::string sbin;
        for (Rank r = R_8; r >= R_1; --r)
        {
            for (File f = F_A; f <= F_H; ++f)
            {
                sbin.append (bb & (f | r) ? "1" : "0");
            }
            sbin.append ("\n");
        }
        ::std::cout << sbin;
    }

    // Print a Bitboard (uint64_t) to console output
    // Bitboard in an easily readable format. This is sometimes useful for debugging.
    void print (Bitboard bb, char p)
    {
        ::std::string sbb;

        //const ::std::string h_line = " -----------------";
        //const ::std::string v_line = "|";
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

        const ::std::string dots = " -----------------\n";
        const ::std::string row = "|. . . . . . . .|\n";
        const size_t len_row = row.length () + 1;
        sbb = dots;
        for (Rank r = R_8; r >= R_1; --r)
        {
            sbb += to_char (r) + row;
        }
        sbb += dots;
        sbb += " ";
        for (File f = F_A; f <= F_H; ++f)
        {
            sbb += " ";
            sbb += to_char (f);
        }
        sbb += "\n";

        while (bb)
        {
            Square s = pop_lsb (bb);
            int8_t r = _rank (s);
            int8_t f = _file (s);
            sbb[2 + len_row * (8 - r) + 2 * f] = p;
        }

        ::std::cout << sbb;
    }

#pragma endregion

    SquareList square_list (Bitboard bb)
    {
        SquareList lst_sq;

        //for (Square s = SQ_A1; s <= SQ_H8; ++s)
        //{
        //    if (bb & s) lst_sq.emplace_back (s);
        //}

        // ---

        while (bb)
        {
            Square s = pop_lsb (bb);
            lst_sq.emplace_back (s);
        }

        return lst_sq;
    }

}
