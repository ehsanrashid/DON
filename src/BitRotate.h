//#pragma once
#ifndef BITROTATE_H_
#define BITROTATE_H_

#include "Type.h"

// Rotate RIGHT (toward LSB)
inline Bitboard rotate_R (Bitboard bb, int8_t k) { return (bb >> k) | (bb << (int8_t (SQ_NO) - k)); }
// Rotate LEFT (toward MSB)
inline Bitboard rotate_L (Bitboard bb, int8_t k) { return (bb << k) | (bb >> (int8_t (SQ_NO) - k)); }

//// Flip a bitboard vertically about the centre ranks.
//inline Bitboard  flip_verti (Bitboard bb)
//{
//    //return ((bb << 0x38)                          ) |
//    //       ((bb << 0x28) & U64(0x00FF000000000000)) |
//    //       ((bb << 0x18) & U64(0x0000FF0000000000)) |
//    //       ((bb << 0x08) & U64(0x000000FF00000000)) |
//    //       ((bb >> 0x08) & U64(0x00000000FF000000)) |
//    //       ((bb >> 0x18) & U64(0x0000000000FF0000)) |
//    //       ((bb >> 0x28) & U64(0x000000000000FF00)) |
//    //       ((bb >> 0x38));
//
//    Bitboard K1 = U64 (0x00FF00FF00FF00FF);
//    Bitboard K2 = U64 (0x0000FFFF0000FFFF);
//    //Bitboard K4 = U64 (0x00000000FFFFFFFF);
//    bb = ((bb >> 0x08) & K1) | ((bb & K1) << 0x08);
//    bb = ((bb >> 0x10) & K2) | ((bb & K2) << 0x10);
//    //bb = ((bb >> 0x20) & K4) | ((bb & K4) << 0x20);
//    bb = ((bb >> 0x20)) | ((bb) << 0x20);
//    return bb;
//}
//// Mirror a bitboard horizontally about the center files.
//inline Bitboard mirror_hori (Bitboard bb)
//{
//
//    Bitboard K1 = U64 (0x5555555555555555);
//    Bitboard K2 = U64 (0x3333333333333333);
//    Bitboard K4 = U64 (0x0F0F0F0F0F0F0F0F);
//
//    bb ^= K4 & (bb ^ rotate_L (bb, 8));
//    bb ^= K2 & (bb ^ rotate_L (bb, 4));
//    bb ^= K1 & (bb ^ rotate_L (bb, 2));
//    bb = rotate_R (bb, 7);
//    // ---
//    bb = ((bb >> 1) & K1) | ((bb & K1) << 1);
//    bb = ((bb >> 2) & K2) | ((bb & K2) << 2);
//    bb = ((bb >> 4) & K4) | ((bb & K4) << 4);
//
//    // ===
//
//    //uint8_t *p = (uint8_t*) (&bb);
//    //p[0] = reverse (p[0]);
//    //p[1] = reverse (p[1]);
//    //p[2] = reverse (p[2]);
//    //p[3] = reverse (p[3]);
//    //p[4] = reverse (p[4]);
//    //p[5] = reverse (p[5]);
//    //p[6] = reverse (p[6]);
//    //p[7] = reverse (p[7]);
//
//    return bb;
//}
//
//// Flip a bitboard about the diagonal A1-H8.
//inline Bitboard flip_A1H8 (Bitboard bb)
//{
//    Bitboard t;
//    Bitboard K1 = U64 (0x5500550055005500);
//    Bitboard K2 = U64 (0x3333000033330000);
//    Bitboard K4 = U64 (0x0F0F0F0F00000000);
//    t = K4 & (bb ^ (bb << 0x1C));
//    bb ^= (t  ^ (t >> 0x1C));
//    t = K2 & (bb ^ (bb << 0x0E));
//    bb ^= (t  ^ (t >> 0x0E));
//    t = K1 & (bb ^ (bb << 0x07));
//    bb ^= (t  ^ (t >> 0x07));
//    return bb;
//}
//// Flip a bitboard about the antidiagonal A8-H1.
//inline Bitboard flip_A8H1 (Bitboard bb)
//{
//    Bitboard t;
//    Bitboard K1 = U64 (0xAA00AA00AA00AA00);
//    Bitboard K2 = U64 (0xCCCC0000CCCC0000);
//    Bitboard K4 = U64 (0xF0F0F0F00F0F0F0F);
//    t = (bb ^ (bb << 0x24));
//    bb ^= K4 & (t  ^ (bb >> 0x24));
//    t = K2 & (bb ^ (bb << 0x12));
//    bb ^= (t  ^ (t >> 0x12));
//    t = K1 & (bb ^ (bb << 0x09));
//    bb ^= (t  ^ (t >> 0x09));
//    return bb;
//}
//
//// Rotate a bitboard by 90 degrees clockwise.
//inline Bitboard rotate_90C (Bitboard bb)
//{
//    return flip_verti (flip_A1H8 (bb));
//    //return flip_A8H1 (flip_verti (bb));
//}
//// Rotate a bitboard by 90 degrees anticlockwise.
//inline Bitboard rotate_90A (Bitboard bb)
//{
//    return flip_verti (flip_A8H1 (bb));
//    //return flip_A1H8 (flip_verti (bb));
//}
//
//// Rotate a bitboard by 45 degrees clockwise.
//inline Bitboard rotate_45C (Bitboard bb)
//{
//    Bitboard K1 = U64 (0xAAAAAAAAAAAAAAAA);
//    Bitboard K2 = U64 (0xCCCCCCCCCCCCCCCC);
//    Bitboard K4 = U64 (0xF0F0F0F0F0F0F0F0);
//    bb ^= K1 & (bb ^ rotate_R (bb, 0x08));
//    bb ^= K2 & (bb ^ rotate_R (bb, 0x10));
//    bb ^= K4 & (bb ^ rotate_R (bb, 0x20));
//    return bb;
//}
//// Rotate a bitboard by 45 degrees anticlockwise.
//inline Bitboard rotate_45A (Bitboard bb)
//{
//    Bitboard K1 = U64 (0x5555555555555555);
//    Bitboard K2 = U64 (0x3333333333333333);
//    Bitboard K4 = U64 (0x0F0F0F0F0F0F0F0F);
//    bb ^= K1 & (bb ^ rotate_R (bb, 0x08));
//    bb ^= K2 & (bb ^ rotate_R (bb, 0x10));
//    bb ^= K4 & (bb ^ rotate_R (bb, 0x20));
//    return bb;
//}
//
//// Rotate a bitboard by 180 degrees.
//inline Bitboard rotate_180 (Bitboard bb)
//{
//    Bitboard H1 = U64 (0x5555555555555555);
//    Bitboard H2 = U64 (0x3333333333333333);
//    Bitboard H4 = U64 (0x0F0F0F0F0F0F0F0F);
//    Bitboard V1 = U64 (0x00FF00FF00FF00FF);
//    Bitboard V2 = U64 (0x0000FFFF0000FFFF);
//    bb = ((bb >> 0x01) & H1) | ((bb & H1) << 0x01);
//    bb = ((bb >> 0x02) & H2) | ((bb & H2) << 0x02);
//    bb = ((bb >> 0x04) & H4) | ((bb & H4) << 0x04);
//    bb = ((bb >> 0x08) & V1) | ((bb & V1) << 0x08);
//    bb = ((bb >> 0x10) & V2) | ((bb & V2) << 0x10);
//    bb = ((bb >> 0x20)) | (bb << 0x20);
//    return bb;
//}
//
//// Flip, mirror or reverse a bitboard
//inline Bitboard flip_mirror_reverse (Bitboard bb, bool flip, bool mirror)
//{
//    for (int8_t i = 3 * !uint8_t (mirror); i < 3 * (1 + uint8_t (flip)); ++i)
//    {
//        uint16_t s  = ((1) << i);
//        Bitboard F  = (U64 (+1) << s);
//        Bitboard K  = (U64 (-1) / (F + 1));
//        bb = ((bb >> s) & K) + F * (bb & K);
//    }
//    return bb;
//}


//// Collapse all FILEs to 1st RANK (File overlay)
//inline uint8_t CollapsedRANKsIndex (Bitboard bb)
//{
//    //bb |= bb >> 0x04;
//    //bb |= bb >> 0x02;
//    //bb |= bb >> 0x01;
//    //bb &= FA_bb;
//    //bb |= bb >> 0x07;
//    //bb |= bb >> 0x0E;
//    //bb |= bb >> 0x1C;
//    //return (bb & 0xFF);
//
//    // ---
//
//    bb |= bb >> 0x04;
//    bb |= bb >> 0x02;
//    bb |= bb >> 0x01;
//    return (((bb & FA_bb) * D81_bb) >> 0x38);
//}
//
//// Collapse all RANKs to 1st RANK (Rank overlay)
//inline uint8_t CollapsedFILEsIndex (Bitboard bb)
//{
//    // LOGIC failed
//    //uint32_t folded = (bb) | (bb >> 0x20);
//    //return (folded * FA_bb) >> 0x18;
//    // -
//
//    //return (bb * FA_bb) >> 0x38;
//
//    // ---
//
//    //bb |= bb >> 0x20;
//    //bb |= bb >> 0x10;
//    //bb |= bb >> 0x08;
//    //return (bb & 0xFF);
//
//    // ---
//
//    uint8_t* p = (uint8_t*) (&bb);
//    return p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7];
//}
//
//// Collapse FILE 'f' to 1st RANK
//inline uint8_t CollapsedRANKsIndex (Bitboard bb, File f)
//{
//    bb = (bb >> f);
//    bb &= FA_bb;
//    bb |= bb >> 0x07;
//    bb |= bb >> 0x0E;
//    bb |= bb >> 0x1C;
//    return (bb & 0xFF);
//
//    // ---
//
//    //bb = (bb >> f);
//    //return (((bb & FA_bb) * D81_bb) >> 0x38);
//}
//// Collapse RANK 'r' to 1st RANK
//inline uint8_t CollapsedFILEsIndex (Bitboard bb, Rank r)
//{
//    return ((bb >> (r * 8)) & 0xFF);
//}
//
//
//inline uint8_t GetRANK (Bitboard bb, Square s)
//{
//    return bb >> IndexRank (s);
//}
//inline uint8_t GetFILE (Bitboard bb, Square s)
//{
//    Bitboard t;
//    Bitboard k1 = U64 (0x5500550055005500);
//    Bitboard k2 = U64 (0x3333000033330000);
//    Bitboard k4 = U64 (0x0F0F0F0F00000000);
//    t = k4 & (bb ^ (bb << 28));
//    bb ^= t ^ (t >> 28);
//    t = k2 & (bb ^ (bb << 14));
//    bb ^= t ^ (t >> 14);
//    t = k1 & (bb ^ (bb << 7));
//    bb ^= t ^ (t >> 7);
//    return bb >> (IndexFile (s) * 8);
//}
//
//inline  uint8_t GetDIAG18 (Bitboard bb, Square s)
//{
//    return bb >> IndexRank (s);
//}
//inline uint8_t GetDIAG81 (Bitboard bb, Square s)
//{
//    return bb >> IndexRank (s);
//}

//inline Bitboard   Include(Bitboard &bb, Bitboard include)
//{
//    return bb |=  include;
//}
//inline Bitboard   Exclude(Bitboard &bb, Bitboard exclude)
//{
//    return bb &= ~exclude;
//}
//inline Bitboard Intersect(Bitboard &bb, Bitboard intersect)
//{
//    return bb &= intersect;
//}
//inline Bitboard    Negate(Bitboard &bb, Bitboard negate)
//{
//    return bb |= ~negate;
//}
//inline Bitboard    Toggle(Bitboard &bb, Bitboard toggle)
//{
//    return bb ^=  toggle;
//}


// gen = generator  (sliders),
// pro = propagator (empty)
//inline Bitboard     NorthOcclude(Bitboard gen, Bitboard pro)
//{
//    gen |= pro &    (gen <<  8);
//    pro &=          (pro <<  8);
//    gen |= pro &    (gen << 16);
//    pro &=          (pro << 16);
//    gen |= pro &    (gen << 32);
//    return gen;
//}
//
//inline Bitboard     SouthOcclude(Bitboard gen, Bitboard pro)
//{
//    gen |= pro &    (gen >>  8);
//    pro &=          (pro >>  8);
//    gen |= pro &    (gen >> 16);
//    pro &=          (pro >> 16);
//    gen |= pro &    (gen >> 32);
//    return gen;
//}
//
//inline Bitboard      EastOcclude(Bitboard gen, Bitboard pro)
//{
//    pro &= FA_bb_;
//    gen |= pro &    (gen <<  1);
//    pro &=          (pro <<  1);
//    gen |= pro &    (gen <<  2);
//    pro &=          (pro <<  2);
//    gen |= pro &    (gen <<  4);
//    return gen;
//}
//
//inline Bitboard      WestOcclude(Bitboard gen, Bitboard pro)
//{
//    pro &= FH_bb_;
//    gen |= pro &    (gen >>  1);
//    pro &=          (pro >>  1);
//    gen |= pro &    (gen >>  2);
//    pro &=          (pro >>  2);
//    gen |= pro &    (gen >>  4);
//    return gen;
//}
//
//inline Bitboard NorthEastOcclude(Bitboard gen, Bitboard pro)
//{
//    pro &= FA_bb_;
//    gen |= pro &    (gen <<  9);
//    pro &=          (pro <<  9);
//    gen |= pro &    (gen << 18);
//    pro &=          (pro << 18);
//    gen |= pro &    (gen << 36);
//    return gen;
//}
//
//inline Bitboard SouthEastOcclude(Bitboard gen, Bitboard pro)
//{
//    pro &= FA_bb_;
//    gen |= pro &    (gen >>  7);
//    pro &=          (pro >>  7);
//    gen |= pro &    (gen >> 14);
//    pro &=          (pro >> 14);
//    gen |= pro &    (gen >> 28);
//    return gen;
//}
//
//inline Bitboard NorthWestOcclude(Bitboard gen, Bitboard pro)
//{
//    pro &= FH_bb_;
//    gen |= pro &    (gen <<  7);
//    pro &=          (pro <<  7);
//    gen |= pro &    (gen << 14);
//    pro &=          (pro << 14);
//    gen |= pro &    (gen << 28);
//    return gen;
//}
//
//inline Bitboard SouthWestOcclude(Bitboard gen, Bitboard pro)
//{
//    pro &= FH_bb_;
//    gen |= pro &    (gen >>  9);
//    pro &=          (pro >>  9);
//    gen |= pro &    (gen >> 18);
//    pro &=          (pro >> 18);
//    gen |= pro &    (gen >> 36);
//    return gen;
//}


//static const uint8_t _reverse_byte[_UI8_MAX + 1] =
//{
//#undef R_6
//#undef R_4
//#undef R_2
//#define R_2(n)       (n),      (n)+2*0x40,      (n)+1*0x40,      (n)+3*0x40
//#define R_4(n)    R_2(n),  R_2((n)+2*0x10), R_2((n)+1*0x10), R_2((n)+3*0x10)
//#define R_6(n)    R_4(n),  R_4((n)+2*0x04), R_4((n)+1*0x04), R_4((n)+3*0x04)
//    R_6 (0), R_6 (2), R_6 (1), R_6 (3),
//#undef R_6
//#undef R_4
//#undef R_2
//};
//
//inline uint8_t reverse (uint8_t b)
//{
//    ///"Swap and Merge"  in parallel
//    //b = ((b >> 1) & 0x55) | ((b & 0x55) << 1); // swap odd and even bits
//    //b = ((b >> 2) & 0x33) | ((b & 0x33) << 2); // swap consecutive pairs
//    //b = b >> 4 | b << 4;                       // swap nibles
//    //return uint8_t(b);
//
//    ///"7 Operations (no division, no 64-bit)"
//    //return uint8_t(((b * U32(0x802) & U32(0x22110)) | (b * U32(0x8020) & U32(0x88440))) * U32(0x10101) >> 0x10);
//
//    ///"4 Operations (no division, 64-bit multiply)"
//    //return uint8_t(((b * U64(0x80200802)) & U64(0x884422110)) * U64(0x101010101) >> 0x20);
//
//    ///"3 Operations (64-bit multiply & modulus division)"
//    //return uint8_t((b * U64(0x202020202) & U64(0x10884422010)) % 0x3FF); // 1023
//
//    // reverse Lookup
//    return _reverse_byte[b];
//}

//// reverse() reverse the bits in a Bitboard
//inline Bitboard reverse (Bitboard bb)
//{
//    Bitboard rr;
//    uint8_t* p = (uint8_t*) (&bb);
//    uint8_t* q = (uint8_t*) (&rr);
//    q[7] = reverse (p[0]);
//    q[6] = reverse (p[1]);
//    q[5] = reverse (p[2]);
//    q[4] = reverse (p[3]);
//    q[3] = reverse (p[4]);
//    q[2] = reverse (p[5]);
//    q[1] = reverse (p[6]);
//    q[0] = reverse (p[7]);
//    return rr;
//}

//inline bool     parity (uint8_t b)
//{
//    return ((((b) * U64 (0x0101010101010101)) & U64 (0x8040201008040201)) % 0x1FF) & 1;
//}


#endif
