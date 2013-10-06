//#pragma once
#ifndef TYPE_H_
#define TYPE_H_

#include <cctype>
#include "Platform.h"
#include <climits>
//#include <cstdlib>

#pragma region LIMITS
#ifndef   _I8_MIN
// minimum   signed  8 bit value
#   define _I8_MIN   (-0x7Fi8 -1)
#endif
#ifndef   _I8_MAX
// maximum   signed  8 bit value
#   define _I8_MAX   (0x7Fi8)
#endif
#ifndef  _UI8_MAX
// maximum unsigned  8 bit value
#   define _UI8_MAX  (0x7Fui8)
#endif
#ifndef  _I16_MIN
// minimum   signed 16 bit value
#   define _I16_MIN  (-0x7FFFi16 -1)
#endif
#ifndef  _I16_MAX
// maximum   signed 16 bit value
#   define _I16_MAX  (0x7FFFi16)
#endif
#ifndef _UI16_MAX
// maximum unsigned 16 bit value
#   define _UI16_MAX (0xFFFFui16)
#endif
#ifndef  _I32_MIN
// minimum   signed 32 bit value
#   define _I32_MIN  (-0x7FFFFFFFi32 -1)
#endif
#ifndef  _I32_MAX
// maximum   signed 32 bit value
#   define _I32_MAX  (0x7FFFFFFFi32)
#endif
#ifndef _UI32_MAX
// maximum unsigned 32 bit value
#   define _UI32_MAX (0xFFFFFFFFui32)
#endif
#ifndef  _I64_MIN
// minimum   signed 64 bit value
#   define _I64_MIN  (-0x7FFFFFFFFFFFFFFFi64 -1)
#endif
#ifndef  _I64_MAX
// maximum   signed 64 bit value
#   define _I64_MAX  (0x7FFFFFFFFFFFFFFFi64)
#endif
#ifndef _UI64_MAX
// maximum unsigned 64 bit value
#   define _UI64_MAX (0xFFFFFFFFFFFFFFFFui64)
#endif

#pragma endregion

#define UNLIKELY(x) (x) // For code annotation purposes

typedef uint64_t   Bitboard; // Type for Bitboard
typedef uint64_t   Key;      // Type for Zobrist Hash

const uint8_t MAX_MOVES      = 192;
const uint8_t MAX_PLY        = 100;
const uint8_t MAX_PLY_PLUS_6 = MAX_PLY + 6;

const uint8_t MAX_THREADS                 = 64;
const uint8_t MAX_SPLITPOINTS_PER_THREAD  = 8;
const uint8_t MAX_SPLIT_DEPTH             = 99;

#pragma warning (push)
#pragma warning (disable: 4341)

// File of Square
typedef enum File : int8_t
{
    F_A = 0,
    F_B = 1,
    F_C = 2,
    F_D = 3,
    F_E = 4,
    F_F = 5,
    F_G = 6,
    F_H = 7,
    F_NO = 8,

} File;

// Rank of Square
typedef enum Rank : int8_t
{
    R_1 = 0,
    R_2 = 1,
    R_3 = 2,
    R_4 = 3,
    R_5 = 4,
    R_6 = 5,
    R_7 = 6,
    R_8 = 7,
    R_NO = 8,

} Rank;

// Diagonal of Square
typedef enum Diag : int8_t
{
    D_01 =  0,
    D_02 =  1,
    D_03 =  2,
    D_04 =  3,
    D_05 =  4,
    D_06 =  5,
    D_07 =  6,
    D_08 =  7,
    D_09 =  8,
    D_10 =  9,
    D_11 = 10,
    D_12 = 11,
    D_13 = 12,
    D_14 = 13,
    D_15 = 14,
    D_NO = 15,

} Diag;

// Color of Square and Side
typedef enum Color : uint8_t
{
    WHITE  = 0,
    BLACK  = 1,
    CLR_NO = 2,

} Color;

// Square needs 6-bits (0-5) to be stored
// bit 0-2: File
// bit 3-5: Rank
typedef enum Square : int8_t
{
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NO,

    SQ_WK_Q  = SQ_C1,
    SQ_WK_K  = SQ_G1,
    SQ_WR_Q  = SQ_D1,
    SQ_WR_K  = SQ_F1,

    SQ_BK_Q  = SQ_C8,
    SQ_BK_K  = SQ_G8,
    SQ_BR_Q  = SQ_D8,
    SQ_BR_K  = SQ_F8,

} Square;

// Delta of Square
typedef enum Delta : int8_t
{

    DEL_O =  0,

    DEL_N =  8,
    DEL_E =  1,
    DEL_S = -DEL_N,
    DEL_W = -DEL_E,

    DEL_NN = DEL_N + DEL_N,
    DEL_EE = DEL_E + DEL_E,
    DEL_SS = DEL_S + DEL_S,
    DEL_WW = DEL_W + DEL_W,

    DEL_NE = DEL_N + DEL_E,
    DEL_SE = DEL_S + DEL_E,
    DEL_SW = DEL_S + DEL_W,
    DEL_NW = DEL_N + DEL_W,

    DEL_NNE = DEL_NN + DEL_E,
    DEL_NNW = DEL_NN + DEL_W,

    DEL_EEN = DEL_EE + DEL_N,
    DEL_EES = DEL_EE + DEL_S,

    DEL_SSE = DEL_SS + DEL_E,
    DEL_SSW = DEL_SS + DEL_W,

    DEL_WWN = DEL_WW + DEL_N,
    DEL_WWS = DEL_WW + DEL_S,

} Delta;

// Castle Side
typedef enum CSide : uint8_t
{
    CS_K  = 0,    // SHORT CASTLE
    CS_Q  = 1,    // LONG  CASTLE
    CS_NO = 2,

} CSide;

// Castle Right
typedef enum CRight : uint8_t
{
    CR_NO  = 0, // 0000
    CR_W_K = 1, // 0001
    CR_W_Q = 2, // 0010
    CR_B_K = 4, // 0100
    CR_B_Q = 8, // 1000

    CR_W = CR_W_K | CR_W_Q, // 0011
    CR_B = CR_B_K | CR_B_Q, // 1100
    CR_A = CR_W | CR_B,     // 1111

} CRight;

// Type of the Piece
typedef enum PType : int8_t
{
    PAWN   = 0, // 000 - PAWN
    NIHT   = 1, // 001 - KNIGHT
    BSHP   = 2, // 010 - BISHOP
    ROOK   = 3, // 011 - ROOK
    QUEN   = 4, // 100 - QUEEN
    KING   = 5, // 101 - KING
    PT_NO  = 6, // 110 - PT_NO

} PType;
// Piece needs 4 bits to be stored
// bit 0-2: TYPE of piece
// bit   3: COLOR of piece
//
// WHITE      = 0...
// BLACK      = 1...
//
// SLIDING    = .1..
// NONSLIDING = .0..
//
// RF SLIDING = .11.
// DG SLIDING = .1.1
//
// PAWN  & KING  < 3
// MINOR & MAJOR > 2
// ONLY MAJOR    > 5
typedef enum Piece : uint8_t
{
    PS_NO    = 0x00, //  0000

    W_PAWN   = 0x01, //  0001
    W_KING   = 0x02, //  0010
    W_NIHT   = 0x03, //  0011
    W_BSHP   = 0x05, //  0101
    W_ROOK   = 0x06, //  0110
    W_QUEN   = 0x07, //  0111

    B_PAWN   = 0x09, //  1001
    B_KING   = 0x0A, //  1010
    B_NIHT   = 0x0B, //  1011
    B_BSHP   = 0x0D, //  1101
    B_ROOK   = 0x0E, //  1110
    B_QUEN   = 0x0F, //  1111

    //W_PIEC    = 0x00, //  0...
    //B_PIEC    = 0x08, //  1...
} Piece;

// Type of Move
typedef enum MType : uint16_t
{
    NORMAL    = 0 << 14, //0x0000, // 0000
    CASTLE    = 1 << 14, //0x4000, // 0100
    ENPASSANT = 2 << 14, //0x8000, // 1000
    PROMOTE   = 3 << 14, //0xC000, // 11xx
} MType;

// Move stored in 16-bits
//
// bit 00-05: destiny square: (0...63)
// bit 06-11:  origin square: (0...63)
// bit 12-13: promotion piece: (KNIGHT...QUEEN) - 1
// bit 14-15: special move flag: (1) CASTLE, (2) EN-PASSANT, (3) PROMOTION
// NOTE: EN-PASSANT is set when capturing the pawn
//
// Special cases are MOVE_NONE and MOVE_NULL. We can sneak these in because in
// any normal move destination square is always different from origin square
// while MOVE_NONE and MOVE_NULL have the same origin and destination square.
typedef enum Move : uint16_t
{
    MOVE_NONE = 0x00,
    MOVE_NULL = 0x41,

    //MOVE_C2C4 = 0x029A,
    //MOVE_D2D4 = 0x02DB,
    //MOVE_E2E4 = 0x031C,
    //MOVE_F2F4 = 0x035D,

    //MOVE_B1C3 = 0x0052,
    //MOVE_G1F3 = 0x0195,

    //MOVE_C7C5 = 0x0CA2,
    //MOVE_D7D5 = 0x0CE3,
    //MOVE_E7E5 = 0x0D24,
    //MOVE_F7F5 = 0x0D65,

    //MOVE_NC6  = 0x0E6A,
    //MOVE_NF6  = 0x0FAD,

    //MOVE_W_CQ = 0xC102,
    //MOVE_W_CK = 0xC106,

    //MOVE_B_CQ = 0xCF3A,
    //MOVE_B_CK = 0xCF3E,

} Move;

typedef enum Score : int16_t
{
    SCORE_ZERO = 0,
    SCORE_DRAW = 0,

    SCORE_NONE      = _I16_MAX,
    SCORE_INFINITE  = SCORE_NONE - 1,
    SCORE_MATE      = SCORE_INFINITE - 1,
    SCORE_KNOWN_WIN = SCORE_MATE / 2,

    //S_MATE_IN_MAX_PLY  =  SCORE_MATE - MAX_PLY,
    //S_MATED_IN_MAX_PLY = -SCORE_MATE + MAX_PLY,

    MG_PAWN   = 198,   EG_PAWN   = 258,
    MG_KNIGHT = 817,   EG_KNIGHT = 846,
    MG_BISHOP = 836,   EG_BISHOP = 857,
    MG_ROOK   = 1270,  EG_ROOK   = 1278,
    MG_QUEEN  = 2521,  EG_QUEEN  = 2558,

} Score;

typedef enum Depth : int16_t
{
    ONE_PLY             =    1,

    DEPTH_ZERO          =    0 * ONE_PLY,
    DEPTH_QS_CHECKS     =   -1 * ONE_PLY,
    DEPTH_QS_NO_CHECKS  =   -2 * ONE_PLY,
    DEPTH_QS_RECAPTURES =   -5 * ONE_PLY,

    DEPTH_NONE          = -128 * ONE_PLY,

} Depth;

typedef enum Bound : uint8_t
{
    UNKNOWN = 0,

    // LOWER (ALPHA) BOUND  - CUT
    // ALPHA evaluation, when exceed BETA the move is too good.
    // 'Fail-High' or 'BETA-Cutoff' and cut off the rest of the search.
    // Since some of the search is cut off, What the actual evaluation of the position was?
    // It was atleast BETA or higher.
    LOWER   = 1,

    // UPPER (BETA) BOUND   - ALL
    // BETA evaluation, when do not reach up to ALPHA the move is 'Fail-Low' 
    // All moves were searched, but none improved ALPHA.
    // A fail-low indicates that this position was not good enough.
    // because there are some other means of reaching a position that is better.
    // Engine will not make the move that allowed the opponent to put in this position.
    // What the actual evaluation of the position was?
    // It was atmost ALPHA (or lower).
    UPPER   = 2,

    // EXACT (-) BOUND      - PV
    // EXACT evaluation, when receive a definite evaluation,
    // that is we searched all possible moves and received a new best move
    // (or received an evaluation from quiescent search that was between ALPHA and BETA).
    // if score for max-player was improved (score > alpha), alpha the max so far,
    // while the min-player improved his score as well (score < beta), beta the min so far.
    // The current node searched was an expected PV-Node,
    // which was confirmed by the search in finding and collecting a principal variation.
    EXACT   = 3,

    // Evaluation cache for lower bound
    EVAL_LOWER = 4,

    // Evaluation cache for upper bound
    EVAL_UPPER = 5,

    // Evaluation cache
    EVAL_EXACT  = 6,

} Bound;

#pragma warning (pop)

#pragma region Operators

#undef ARTHMAT_OPERATORS
#undef INC_DEC_OPERATORS

#define ARTHMAT_OPERATORS(T)                                                                \
    F_INLINE T  operator-  (T  d)        { return T (-int32_t(d)); }                        \
    F_INLINE T  operator+  (T  d1, T d2) { return T (int32_t(d1) + int32_t(d2)); }          \
    F_INLINE T  operator-  (T  d1, T d2) { return T (int32_t(d1) - int32_t(d2)); }          \
    F_INLINE T  operator*  (T  d, int32_t i) { return T (int32_t(d) * i); }                 \
    F_INLINE T  operator+  (T  d, int32_t i) { return T (int32_t(d) + i); }                 \
    F_INLINE T  operator-  (T  d, int32_t i) { return T (int32_t(d) - i); }                 \
    F_INLINE T  operator*  (int32_t i, T  d) { return T (i * int32_t(d)); }                 \
    F_INLINE T  operator/  (T  d, int32_t i) { return T (int32_t(d) / i); }                 \
    F_INLINE T& operator+= (T &d1, T d2) { d1 = T (int32_t(d1) + int32_t(d2)); return d1; } \
    F_INLINE T& operator-= (T &d1, T d2) { d1 = T (int32_t(d1) - int32_t(d2)); return d1; } \
    F_INLINE T& operator+= (T &d, int32_t i) { d = T (int32_t(d) + i); return d; }          \
    F_INLINE T& operator-= (T &d, int32_t i) { d = T (int32_t(d) - i); return d; }          \
    F_INLINE T& operator*= (T &d, int32_t i) { d = T (int32_t(d) * i); return d; }          \
    F_INLINE T& operator/= (T &d, int32_t i) { d = T (int32_t(d) / i); return d; }
//F_INLINE T  operator+  (int32_t i, T d) { return T (i + int32_t(d)); }
//F_INLINE T  operator-  (int32_t i, T d) { return T (i - int32_t(d)); }

#define INC_DEC_OPERATORS(T)                                                              \
    F_INLINE T  operator++ (T &d, int32_t) { T o = d; d = T (int32_t(d) + 1); return o; } \
    F_INLINE T  operator-- (T &d, int32_t) { T o = d; d = T (int32_t(d) - 1); return o; } \
    F_INLINE T& operator++ (T &d         ) { d = T (int32_t(d) + 1); return d; }          \
    F_INLINE T& operator-- (T &d         ) { d = T (int32_t(d) - 1); return d; }


F_INLINE File  operator+  (File  f, int32_t i) { return File (int32_t(f) + i); }
F_INLINE File  operator-  (File  f, int32_t i) { return File (int32_t(f) - i); }
F_INLINE File& operator+= (File &f, int32_t i) { f = File (int32_t(f) + i); return f; }
F_INLINE File& operator-= (File &f, int32_t i) { f = File (int32_t(f) - i); return f; }
INC_DEC_OPERATORS (File);


F_INLINE Rank  operator+  (Rank  r, int32_t i) { return Rank (int32_t(r) + i); }
F_INLINE Rank  operator-  (Rank  r, int32_t i) { return Rank (int32_t(r) - i); }
F_INLINE Rank& operator+= (Rank &r, int32_t i) { r = Rank (int32_t(r) + i); return r; }
F_INLINE Rank& operator-= (Rank &r, int32_t i) { r = Rank (int32_t(r) - i); return r; }
INC_DEC_OPERATORS (Rank);

//INC_DEC_OPERATORS (Diag);

INC_DEC_OPERATORS (Color);

// Square operator
F_INLINE Square  operator+  (Square  s, Delta d) { return Square (int32_t(s) + int32_t(d)); }
F_INLINE Square  operator-  (Square  s, Delta d) { return Square (int32_t(s) - int32_t(d)); }
F_INLINE Square& operator+= (Square &s, Delta d) { s = Square (int32_t(s) + int32_t(d)); return s; }
F_INLINE Square& operator-= (Square &s, Delta d) { s = Square (int32_t(s) - int32_t(d)); return s; }
F_INLINE Delta   operator-  (Square s1, Square s2) { return Delta (int32_t(s1) - int32_t(s2)); }
INC_DEC_OPERATORS (Square);

ARTHMAT_OPERATORS (Delta);

INC_DEC_OPERATORS (CSide);

// CRight operator
F_INLINE CRight  operator|  (CRight  cr, int32_t i) { return CRight (int32_t(cr) | i); }
F_INLINE CRight  operator&  (CRight  cr, int32_t i) { return CRight (int32_t(cr) & i); }
F_INLINE CRight  operator^  (CRight  cr, int32_t i) { return CRight (int32_t(cr) ^ i); }
F_INLINE CRight& operator|= (CRight &cr, int32_t i) { cr = CRight (int32_t(cr) | i); return cr; }
F_INLINE CRight& operator&= (CRight &cr, int32_t i) { cr = CRight (int32_t(cr) & i); return cr; }
F_INLINE CRight& operator^= (CRight &cr, int32_t i) { cr = CRight (int32_t(cr) ^ i); return cr; }

INC_DEC_OPERATORS (PType);

// Move operator
F_INLINE Move& operator|= (Move &m, int32_t i) { m = Move (int32_t(m) | i); return m; }
F_INLINE Move& operator&= (Move &m, int32_t i) { m = Move (int32_t(m) & i); return m; }


ARTHMAT_OPERATORS (Score);
/// Only declared but not defined. We don't want to multiply two scores due to
/// a very high risk of overflow. So user should explicitly convert to integer.
F_INLINE Score operator* (Score s1, Score s2);
/// Division of a Score must be handled separately for each term
//F_INLINE Score operator/ (Score s, int32_t i)
//{
//    //return make_score(mg_value(s) / i, eg_value(s) / i);
//}


ARTHMAT_OPERATORS (Depth);
INC_DEC_OPERATORS (Depth);

#undef ARTHMAT_OPERATORS
#undef INC_DEC_OPERATORS

#pragma endregion



//template <typename T, size_t N>
//inline size_t sizeof_array (const T(&)[N])
//{
//    return N;
//}
//
//#include <type_traits>
//template <typename A>
//typename std::enable_if <std::is_array <A> ::value, size_t> ::type sizeof_array (const A &a)
//{
//    return std::extent <A>::value;
//}
//
//const char s[] = "Hello world!";
//
//// sizeof_array (s)


#endif
