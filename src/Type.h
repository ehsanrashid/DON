//#pragma once
#ifndef TYPE_H_
#define TYPE_H_

#include <cctype>
#include <climits>
#include <vector>

#include "Platform.h"

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

const uint16_t MAX_MOVES    = 192;
const uint16_t MAX_PLY      = 100;
const uint16_t MAX_PLY_6    = MAX_PLY + 6;

#pragma warning (push)
#pragma warning (disable: 4341)

// File of Square
typedef enum File : int8_t
{
    F_A,
    F_B,
    F_C,
    F_D,
    F_E,
    F_F,
    F_G,
    F_H,
    F_NO,

} File;

// Rank of Square
typedef enum Rank : int8_t
{
    R_1,
    R_2,
    R_3,
    R_4,
    R_5,
    R_6,
    R_7,
    R_8,
    R_NO,

} Rank;

// Diagonal of Square
typedef enum Diag : int8_t
{
    D_01,
    D_02,
    D_03,
    D_04,
    D_05,
    D_06,
    D_07,
    D_08,
    D_09,
    D_10,
    D_11,
    D_12,
    D_13,
    D_14,
    D_15,
    D_NO,

} Diag;

// Color of Square and Side
typedef enum Color : uint8_t
{
    WHITE,
    BLACK,
    CLR_NO,

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
    CR_NO  = 0,             // 0000
    CR_W_K = 1,             // 0001
    CR_W_Q = CR_W_K << 1,   // 0010
    CR_B_K = CR_W_K << 2,   // 0100
    CR_B_Q = CR_W_K << 3,   // 1000

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
    PT_ALL = 7,

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

    W_PAWN = 0, //  0000
    W_NIHT    , //  0001
    W_BSHP    , //  0010
    W_ROOK    , //  0011
    W_QUEN    , //  0100
    W_KING    , //  0101

    PS_NO  = 6, //  0110

    B_PAWN = 8, //  1000
    B_NIHT    , //  1001
    B_BSHP    , //  1010
    B_ROOK    , //  1011
    B_QUEN    , //  1100
    B_KING    , //  1101

    PS_ALL =14,
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

typedef enum Value : int32_t
{
    VALUE_ZERO      = 0,
    VALUE_DRAW      = 0,

    VALUE_NONE      = _I16_MAX,
    VALUE_INFINITE  = VALUE_NONE - 1,

    VALUE_MATE      = 32000,
    VALUE_KNOWN_WIN = VALUE_MATE / 2,

    VALUE_MATES_IN_MAX_PLY =  VALUE_MATE - MAX_PLY,
    VALUE_MATED_IN_MAX_PLY = -VALUE_MATE + MAX_PLY,

    VALUE_ENSURE_INTEGER_SIZE_P = _I16_MAX,
    VALUE_ENSURE_INTEGER_SIZE_N = _I16_MIN,

    VALUE_MG_PAWN   =  198,  VALUE_EG_PAWN   =  258,
    VALUE_MG_KNIGHT =  817,  VALUE_EG_KNIGHT =  846,
    VALUE_MG_BISHOP =  836,  VALUE_EG_BISHOP =  857,
    VALUE_MG_ROOK   = 1270,  VALUE_EG_ROOK   = 1278,
    VALUE_MG_QUEEN  = 2521,  VALUE_EG_QUEEN  = 2558,

} Value;

// Score enum keeps a midgame and an endgame value in a single integer (enum),
// first LSB 16 bits are used to store endgame value, while upper bits are used
// for midgame value. Compiler is free to choose the enum type as long as can
// keep its data, so ensure Score to be an integer type.
typedef enum Score : int32_t
{
    SCORE_ZERO      = 0,

    SCORE_ENSURE_INTEGER_SIZE_P = INT_MAX,
    SCORE_ENSURE_INTEGER_SIZE_N = INT_MIN,

} Score;

typedef enum Depth : int16_t
{
    ONE_PLY             =    1,
    ONE_MOVE            =    2,

    DEPTH_ZERO          =    0 * ONE_MOVE,
    DEPTH_QS_CHECKS     =    0 * ONE_MOVE,
    DEPTH_QS_NO_CHECKS  =   -1 * ONE_MOVE,
    DEPTH_QS_RECAPTURES =   -5 * ONE_MOVE,

    DEPTH_NONE          = -128 * ONE_MOVE,

} Depth;

typedef enum Bound : uint8_t
{
    BND_NONE    = 0,

    // UPPER (BETA) BOUND   - ALL
    // BETA evaluation, when do not reach up to ALPHA the move is 'Fail-Low' 
    // All moves were searched, but none improved ALPHA.
    // A fail-low indicates that this position was not good enough.
    // because there are some other means of reaching a position that is better.
    // Engine will not make the move that allowed the opponent to put in this position.
    // What the actual evaluation of the position was?
    // It was atmost ALPHA (or lower).
    BND_UPPER   = 1,

    // LOWER (ALPHA) BOUND  - CUT
    // ALPHA evaluation, when exceed BETA the move is too good.
    // 'Fail-High' or 'BETA-Cutoff' and cut off the rest of the search.
    // Since some of the search is cut off, What the actual evaluation of the position was?
    // It was atleast BETA or higher.
    BND_LOWER   = 2,

    // EXACT (-) BOUND      - PV
    // EXACT evaluation, when receive a definite evaluation,
    // that is we searched all possible moves and received a new best move
    // (or received an evaluation from quiescent search that was between ALPHA and BETA).
    // if score for max-player was improved (score > alpha), alpha the max so far,
    // while the min-player improved his score as well (score < beta), beta the min so far.
    // The current node searched was an expected PV-Node,
    // which was confirmed by the search in finding and collecting a principal variation.
    BND_EXACT   = BND_LOWER | BND_UPPER,

} Bound;

typedef enum Phase : int16_t
{
    PHASE_ENDGAME =   0,
    PHASE_MIDGAME = 128,

    MG = 0,
    EG = 1,
    PHASE_NO = 2,

} Phase;

typedef enum ScaleFactor : uint8_t
{
    SCALE_FACTOR_DRAW    = 0,
    SCALE_FACTOR_ONEPAWN = 48,
    SCALE_FACTOR_NORMAL  = 64,
    SCALE_FACTOR_MAX     = 128,
    SCALE_FACTOR_NONE    = 255
} ScaleFactor;

#pragma warning (pop)

inline Score mk_score (int32_t mg, int32_t eg)
{
    return Score ((mg << 16) + eg);
}

// Extracting the signed lower and upper 16 bits it not so trivial because
// according to the standard a simple cast to short is implementation defined
// and so is a right shift of a signed integer.
inline Value mg_value (Score s)
{
    return Value (((s + 0x8000) & ~0xFFFF) / 0x10000);
}

// On Intel 64 bit we have a small speed regression with the standard conforming
// version, so use a faster code in this case that, although not 100% standard
// compliant it seems to work for Intel and MSVC.
#if defined(_WIN64) && (!defined(__GNUC__) || defined(__INTEL_COMPILER))

inline Value eg_value (Score s)
{
    return Value (int16_t (s & 0xFFFF));
}

#else

inline Value eg_value (Score s)
{
    return Value (int32_t (uint32_t (s) & 0x7FFFU) - int32_t (uint32_t (s) & 0x8000U));
}

#endif

#pragma region Operators

#undef ARTHMAT_OPERATORS
#undef INC_DEC_OPERATORS

#define ARTHMAT_OPERATORS(T)                                                                \
    inline T  operator+  (T  d)        { return T (+int32_t (d)); }                         \
    inline T  operator-  (T  d)        { return T (-int32_t (d)); }                         \
    inline T  operator+  (T  d1, T d2) { return T (int32_t (d1) + int32_t (d2)); }          \
    inline T  operator-  (T  d1, T d2) { return T (int32_t (d1) - int32_t (d2)); }          \
    inline T  operator*  (T  d, int32_t i) { return T (int32_t (d) * i); }                  \
    inline T  operator+  (T  d, int32_t i) { return T (int32_t (d) + i); }                  \
    inline T  operator-  (T  d, int32_t i) { return T (int32_t (d) - i); }                  \
    inline T& operator+= (T &d1, T d2) { d1 = T (int32_t (d1) + int32_t (d2)); return d1; } \
    inline T& operator-= (T &d1, T d2) { d1 = T (int32_t (d1) - int32_t (d2)); return d1; } \
    inline T& operator+= (T &d, int32_t i) { d = T (int32_t (d) + i); return d; }           \
    inline T& operator-= (T &d, int32_t i) { d = T (int32_t (d) - i); return d; }           \
    inline T  operator*  (int32_t i, T  d) { return T (i * int32_t (d)); }                  \
    inline T& operator*= (T &d, int32_t i) { d = T (int32_t (d) * i); return d; }
//inline T  operator+  (int32_t i, T d) { return T (i + int32_t (d)); }                  \
//inline T  operator-  (int32_t i, T d) { return T (i - int32_t (d)); }                  \
//inline T  operator/  (T  d, int32_t i) { return T (int32_t (d) / i); }                 \
//inline T& operator/= (T &d, int32_t i) { d = T (int32_t (d) / i); return d; }

#define INC_DEC_OPERATORS(T)                                                                \
    inline T  operator++ (T &d, int32_t) { T o = d; d = T (int32_t (d) + 1); return o; }    \
    inline T  operator-- (T &d, int32_t) { T o = d; d = T (int32_t (d) - 1); return o; }    \
    inline T& operator++ (T &d         ) { d = T (int32_t (d) + 1); return d; }             \
    inline T& operator-- (T &d         ) { d = T (int32_t (d) - 1); return d; }


INC_DEC_OPERATORS (File);
inline File  operator+  (File  f, int32_t i) { return File (int32_t (f) + i); }
inline File  operator-  (File  f, int32_t i) { return File (int32_t (f) - i); }
inline File& operator+= (File &f, int32_t i) { f = File (int32_t (f) + i); return f; }
inline File& operator-= (File &f, int32_t i) { f = File (int32_t (f) - i); return f; }


INC_DEC_OPERATORS (Rank);
inline Rank  operator+  (Rank  r, int32_t i) { return Rank (int32_t (r) + i); }
inline Rank  operator-  (Rank  r, int32_t i) { return Rank (int32_t (r) - i); }
inline Rank& operator+= (Rank &r, int32_t i) { r = Rank (int32_t (r) + i); return r; }
inline Rank& operator-= (Rank &r, int32_t i) { r = Rank (int32_t (r) - i); return r; }

//INC_DEC_OPERATORS (Diag);

INC_DEC_OPERATORS (Color);

// Square operator
INC_DEC_OPERATORS (Square);
inline Square  operator+  (Square  s, Delta d) { return Square (int32_t (s) + int32_t (d)); }
inline Square  operator-  (Square  s, Delta d) { return Square (int32_t (s) - int32_t (d)); }
inline Square& operator+= (Square &s, Delta d) { s = Square (int32_t (s) + int32_t (d)); return s; }
inline Square& operator-= (Square &s, Delta d) { s = Square (int32_t (s) - int32_t (d)); return s; }
inline Delta   operator-  (Square s1, Square s2) { return Delta (int32_t (s1) - int32_t (s2)); }

ARTHMAT_OPERATORS (Delta);
inline Delta  operator/  (Delta  d, int32_t i) { return Delta (int32_t (d) / i); }
inline Delta& operator/= (Delta &d, int32_t i) { d = Delta (int32_t (d) / i); return d; }

INC_DEC_OPERATORS (CSide);

// CRight operator
inline CRight  operator|  (CRight  cr, int32_t i) { return CRight (int32_t (cr) | i); }
inline CRight  operator&  (CRight  cr, int32_t i) { return CRight (int32_t (cr) & i); }
inline CRight  operator^  (CRight  cr, int32_t i) { return CRight (int32_t (cr) ^ i); }
inline CRight& operator|= (CRight &cr, int32_t i) { cr = CRight (int32_t (cr) | i); return cr; }
inline CRight& operator&= (CRight &cr, int32_t i) { cr = CRight (int32_t (cr) & i); return cr; }
inline CRight& operator^= (CRight &cr, int32_t i) { cr = CRight (int32_t (cr) ^ i); return cr; }

INC_DEC_OPERATORS (PType);

// Move operator
inline Move& operator|= (Move &m, int32_t i) { m = Move (int32_t (m) | i); return m; }
inline Move& operator&= (Move &m, int32_t i) { m = Move (int32_t (m) & i); return m; }

ARTHMAT_OPERATORS (Value);
INC_DEC_OPERATORS (Value);
// Added operators for adding integers to a Value
//inline Value  operator+  (Value v, int32_t i) { return Value (int32_t (v) + i); }
//inline Value  operator-  (Value v, int32_t i) { return Value (int32_t (v) - i); }
inline Value  operator+  (int32_t i, Value v) { return Value (i + int32_t (v)); }
inline Value  operator-  (int32_t i, Value v) { return Value (i - int32_t (v)); }
inline Value  operator/  (Value  v, int32_t i) { return Value (int32_t (v) / i); }
inline Value& operator/= (Value &v, int32_t i) { v = Value (int32_t (v) / i); return v; }

ARTHMAT_OPERATORS (Score);
/// Only declared but not defined. We don't want to multiply two scores due to
/// a very high risk of overflow. So user should explicitly convert to integer.
inline Score operator* (Score s1, Score s2);
/// Division of a Score must be handled separately for each term
inline Score operator/ (Score s, int32_t i)
{
    return mk_score (mg_value (s) / i, eg_value (s) / i);
}

ARTHMAT_OPERATORS (Depth);
INC_DEC_OPERATORS (Depth);
inline Depth  operator/ (Depth  d, int32_t i) { return Depth (int32_t (d) / i); }


#undef ARTHMAT_OPERATORS
#undef INC_DEC_OPERATORS

#pragma endregion

typedef std::vector<Move>   MoveList;

inline Value mates_in (int32_t ply) { return (+VALUE_MATE - ply); }
inline Value mated_in (int32_t ply) { return (-VALUE_MATE + ply); }

template<class Entry, int32_t SIZE>
struct HashTable
{

private:
    std::vector<Entry> _table;

public:
    HashTable ()
        : _table (SIZE, Entry ())
    {}

    Entry* operator[] (Key k)
    {
        return &_table[(uint32_t) (k) & (SIZE - 1)];
    }

};

#endif
