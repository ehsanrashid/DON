#ifndef _TYPE_H_INC_
#define _TYPE_H_INC_

#include <cctype>
#include <climits>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <cassert>
#include <iosfwd>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <iterator>
//#include <locale>
//#include <utility>

#ifdef BM2
#   include <immintrin.h>               // Header for bmi2 instructions
#   define PEXT(b, m) _pext_u64 (b, m)  // Parallel bits extract
#   define BLSR(b)    _blsr_u64 (b)     // Reset lowest set bit
#endif

/// When compiling with provided Makefile (e.g. for Linux and OSX), configuration
/// is done automatically. To get started type 'make help'.
///
/// When Makefile is not used (e.g. with Microsoft Visual Studio) some switches
/// need to be set manually:
///
/// -DNDEBUG    | Disable debugging mode. Always use this.
/// -DPREFETCH  | Enable use of prefetch asm-instruction.
///             | Don't enable it if want the executable to run on some very old machines.
/// -DBSFQ      | Add runtime support for use of Bitscans asm-instruction.
/// -DPOP       | Enable use of internal pop count table. Works in both 32-bit & 64-bit mode.
///             | For compiling requires hardware without ABM support.
/// -DABM       | Add runtime support for use of ABM asm-instruction. Works only in 64-bit mode.
///             | For compiling requires hardware with ABM support.
/// -DBM2       | Add runtime support for use of BM2 asm-instruction. Works only in 64-bit mode.
///             | For compiling requires hardware with BM2 support.
/// -DLPAGES    | Add runtime support for large pages.


/// Predefined macros hell:
///
/// __GNUC__           Compiler is gcc, Clang or Intel on Linux
/// __INTEL_COMPILER   Compiler is Intel
/// _MSC_VER           Compiler is MSVC or Intel on Windows
/// _WIN32             Building on Windows (any)
/// _WIN64             Building on Windows 64 bit


#undef S32
#undef U32
#undef S64
#undef U64

#ifdef _MSC_VER
// Disable some silly and noisy warning from MSVC compiler
#   pragma warning (disable: 4127) // Conditional expression is constant
#   pragma warning (disable: 4146) // Unary minus operator applied to unsigned type
#   pragma warning (disable: 4800) // Forcing value to bool 'true' or 'false'

// MSVC does not support <inttypes.h>
//#   include <stdint.h>
//typedef         int8_t     i08;
//typedef        uint8_t     u08;
//typedef         int16_t    i16;
//typedef        uint16_t    u16;
//typedef         int32_t    i32;
//typedef        uint32_t    u32;
//typedef         int64_t    i64;
//typedef        uint64_t    u64;

typedef   signed __int8     i08;
typedef unsigned __int8     u08;
typedef   signed __int16    i16;
typedef unsigned __int16    u16;
typedef   signed __int32    i32;
typedef unsigned __int32    u32;
typedef   signed __int64    i64;
typedef unsigned __int64    u64;

#   define  S32(X) (X ##  i32)
#   define  U32(X) (X ## ui32)
#   define  S64(X) (X ##  i64)
#   define  U64(X) (X ## ui64)

#else

#   include <inttypes.h>

typedef         int8_t     i08;
typedef        uint8_t     u08;
typedef         int16_t    i16;
typedef        uint16_t    u16;
typedef         int32_t    i32;
typedef        uint32_t    u32;
typedef         int64_t    i64;
typedef        uint64_t    u64;

#   define S32(X) (X ##   L)
#   define U32(X) (X ##  UL)
#   define S64(X) (X ##  LL)
#   define U64(X) (X ## ULL)

#endif

// Windows or MinGW
#if defined(_WIN32)

// Auto make 64-bit compiles
#   ifdef _WIN64
#       ifndef BIT64
#           define BIT64
#       endif
#       ifndef BSFQ
#           define BSFQ
#       endif
#   endif

#endif


typedef u64     Key;
typedef u64     Bitboard;

const i32 MAX_DEPTH = 128; // Maximum Depth (Ply)

// File
enum File : i08 { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H, F_NO };
// Rank
enum Rank : i08 { R_1, R_2, R_3, R_4, R_5, R_6, R_7, R_8, R_NO };

// Color
enum Color : i08 { WHITE, BLACK, CLR_NO };

// Square
// File: 3-bit
// Rank: 3-bit
enum Square : i08
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

};

// Delta
enum Delta : i08
{
    DEL_O =  0,

    DEL_N =  8,
    DEL_E =  1,
    DEL_S = -i08(DEL_N),
    DEL_W = -i08(DEL_E),

    DEL_NN = i08(DEL_N) + i08(DEL_N),
    DEL_EE = i08(DEL_E) + i08(DEL_E),
    DEL_SS = i08(DEL_S) + i08(DEL_S),
    DEL_WW = i08(DEL_W) + i08(DEL_W),

    DEL_NE = i08(DEL_N) + i08(DEL_E),
    DEL_SE = i08(DEL_S) + i08(DEL_E),
    DEL_SW = i08(DEL_S) + i08(DEL_W),
    DEL_NW = i08(DEL_N) + i08(DEL_W),

    DEL_NNE = i08(DEL_NN) + i08(DEL_E),
    DEL_NNW = i08(DEL_NN) + i08(DEL_W),

    DEL_EEN = i08(DEL_EE) + i08(DEL_N),
    DEL_EES = i08(DEL_EE) + i08(DEL_S),

    DEL_SSE = i08(DEL_SS) + i08(DEL_E),
    DEL_SSW = i08(DEL_SS) + i08(DEL_W),

    DEL_WWN = i08(DEL_WW) + i08(DEL_N),
    DEL_WWS = i08(DEL_WW) + i08(DEL_S)

};

// Castle Side
enum CSide : i08
{
    CS_K ,    // (KING SIDE)-SHORT CASTLE
    CS_Q ,    // (QUEN SIDE)-LONG  CASTLE
    CS_NO
};

// Castle Right defined as in PolyGlot book hash key
enum CRight : u08
{
    CR_NO ,               // 0000
    CR_WK = 1,            // 0001
    CR_WQ = CR_WK << 1,   // 0010
    CR_BK = CR_WK << 2,   // 0100
    CR_BQ = CR_WK << 3,   // 1000

    CR_W = u08(CR_WK) | u08(CR_WQ), // 0011
    CR_B = u08(CR_BK) | u08(CR_BQ), // 1100
    CR_A = u08(CR_W)  | u08(CR_B),  // 1111
    CR_ALL = 16

};

// Types of Piece
enum PieceT : i08
{
    PAWN  , // 000
    NIHT  , // 001
    BSHP  , // 010
    ROOK  , // 011
    QUEN  , // 100
    KING  , // 101
    NONE  , // 110
    TOTL  , // 111
    NONPAWN
};

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
enum Piece : u08
{
    W_PAWN = 0, //  0000
    W_NIHT    , //  0001
    W_BSHP    , //  0010
    W_ROOK    , //  0011
    W_QUEN    , //  0100
    W_KING    , //  0101

    EMPTY  = 6, //  0110

    B_PAWN = 8, //  1000
    B_NIHT    , //  1001
    B_BSHP    , //  1010
    B_ROOK    , //  1011
    B_QUEN    , //  1100
    B_KING    , //  1101

    // TOTAL piece is 14
    PIECE_NO   //  1110

    //W_PIECE = 0x00, //  0...
    //B_PIECE = 0x08, //  1...
};

// Move Type
enum MoveT : u16
{
    NORMAL    = 0x0000, // 0- 0000
    CASTLE    = 0x4000, // 1- 0100
    ENPASSANT = 0x8000, // 2- 1000
    PROMOTE   = 0xC000, // 3- 11xx
};

// Move stored in 16-bits
//
// bit 00-05: destiny square: (0...63)
// bit 06-11:  origin square: (0...63)
// bit 12-13: promotion piece: (KNIGHT...QUEEN) - 1
// bit 14-15: special move flag: (1) CASTLE, (2) EN-PASSANT, (3) PROMOTION
// NOTE: EN-PASSANT bit is set only when a pawn can be captured
//
// Special cases are MOVE_NONE and MOVE_NULL. Can sneak these in because in
// any normal move destination square is always different from origin square
// while MOVE_NONE and MOVE_NULL have the same origin and destination square.
enum Move : u16
{
    MOVE_NONE = 0x00,
    MOVE_NULL = 0x41
};

enum Depth : i16
{
    DEPTH_ZERO          =  0,
    DEPTH_ONE           =  1,
    DEPTH_QS_CHECKS     =  0,
    DEPTH_QS_NO_CHECKS  = -1,
    DEPTH_QS_RECAPTURES = -5,

    DEPTH_NONE          = -6
};

enum Value : i32
{
    VALUE_ZERO      = 0,
    VALUE_DRAW      = 0,

    VALUE_NONE      = SHRT_MAX,
    VALUE_INFINITE  = +VALUE_NONE - 1,

    VALUE_MATE      = +VALUE_INFINITE - 1,
    VALUE_KNOWN_WIN = +VALUE_MATE / 3,

    VALUE_MATE_IN_MAX_DEPTH = +VALUE_MATE - 2 * MAX_DEPTH,

    VALUE_MG_PAWN =  198,  VALUE_EG_PAWN =  258,
    VALUE_MG_NIHT =  817,  VALUE_EG_NIHT =  846,
    VALUE_MG_BSHP =  836,  VALUE_EG_BSHP =  857,
    VALUE_MG_ROOK = 1270,  VALUE_EG_ROOK = 1281,
    VALUE_MG_QUEN = 2521,  VALUE_EG_QUEN = 2558,

    VALUE_MIDGAME = 15581, VALUE_SPACE = 11756, VALUE_ENDGAME = 3998
};

// Score enum stores a midgame and an endgame value in a single integer (enum),
// the lower 16 bits are used to store the endgame value and
// the upper 16 bits are used to store the midgame value.
enum Score : i32 { SCORE_ZERO = 0 };

enum Bound : u08
{
    // NONE BOUND           - NO_NODE
    BOUND_NONE  = 0,

    // UPPER (BETA) BOUND   - ALL_NODE
    // BETA evaluation, when do not reach up to ALPHA the move is 'Fail-Low' 
    // All moves were searched, but none improved ALPHA.
    // A fail-low indicates that this position was not good enough.
    // because there are some other means of reaching a position that is better.
    // Engine will not make the move that allowed the opponent to put in this position.
    // What the actual evaluation of the position was?
    // It was atmost ALPHA (or lower).
    BOUND_UPPER = 1,

    // LOWER (ALPHA) BOUND  - CUT_NODE
    // ALPHA evaluation, when exceed BETA the move is too good.
    // 'Fail-High' or 'BETA-Cutoff' and cut off the rest of the search.
    // Since some of the search is cut off, What the actual evaluation of the position was?
    // It was atleast BETA or higher.
    BOUND_LOWER = 2,

    // EXACT (-) BOUND      - PV_NODE
    // EXACT evaluation, when receive a definite evaluation,
    // that is searched all possible moves and received a new best move
    // (or received an evaluation from quiescent search that was between ALPHA and BETA).
    // if score for max-player was improved (score > alpha), alpha the max so far,
    // while the min-player improved his score as well (score < beta), beta the min so far.
    // The current node searched was an expected PV-Node,
    // which was confirmed by the search in finding and collecting a principal variation.
    BOUND_EXACT = BOUND_LOWER | BOUND_UPPER

};

enum Phase : i16
{
    PHASE_ENDGAME    =   0,
    PHASE_MIDGAME    = 128,

    MG       = 0,
    EG       = 1,
    PHASE_NO = 2
};

enum ScaleFactor : u08
{
    SCALE_FACTOR_DRAW    =   0,
    SCALE_FACTOR_ONEPAWN =  48,
    SCALE_FACTOR_NORMAL  =  64,
    SCALE_FACTOR_MAX     = 128,
    SCALE_FACTOR_NONE    = 255
};

#undef BASIC_OPERATORS
#undef ARTHMAT_OPERATORS
#undef INC_DEC_OPERATORS

#define BASIC_OPERATORS(T)                                                        \
    inline T  operator+  (T  d, i32 i) { return T (i32(d) + i); }                 \
    inline T  operator-  (T  d, i32 i) { return T (i32(d) - i); }                 \
    inline T& operator+= (T &d, i32 i) { d = T (i32(d) + i); return d; }          \
    inline T& operator-= (T &d, i32 i) { d = T (i32(d) - i); return d; }          \

#define ARTHMAT_OPERATORS(T)                                                      \
    BASIC_OPERATORS(T)                                                            \
    inline T  operator+  (T  d1, T d2) { return T (i32(d1) + i32(d2)); }          \
    inline T  operator-  (T  d1, T d2) { return T (i32(d1) - i32(d2)); }          \
    inline T  operator*  (T  d, i32 i) { return T (i32(d) * i); }                 \
    inline T  operator+  (T  d       ) { return T (+i32(d)); }                    \
    inline T  operator-  (T  d       ) { return T (-i32(d)); }                    \
    inline T& operator+= (T &d1, T d2) { d1 = T (i32(d1) + i32(d2)); return d1; } \
    inline T& operator-= (T &d1, T d2) { d1 = T (i32(d1) - i32(d2)); return d1; } \
    inline T  operator*  (i32 i, T  d) { return T (i * i32(d)); }                 \
    inline T& operator*= (T &d, i32 i) { d = T (i32(d) * i); return d; }

//inline T  operator+  (i32 i, T d) { return T (i + i32(d)); }                  
//inline T  operator-  (i32 i, T d) { return T (i - i32(d)); }                  
//inline T  operator/  (T  d, i32 i) { return T (i32(d) / i); }                 
//inline T& operator/= (T &d, i32 i) { d = T (i32(d) / i); return d; }

#define INC_DEC_OPERATORS(T)                                                     \
    inline T  operator++ (T &d, i32) { T o = d; d = T (i32(d) + 1); return o; }  \
    inline T  operator-- (T &d, i32) { T o = d; d = T (i32(d) - 1); return o; }  \
    inline T& operator++ (T &d     ) { d = T (i32(d) + 1); return d; }           \
    inline T& operator-- (T &d     ) { d = T (i32(d) - 1); return d; }

BASIC_OPERATORS (File)
INC_DEC_OPERATORS (File)

BASIC_OPERATORS (Rank)
INC_DEC_OPERATORS (Rank)

INC_DEC_OPERATORS (Color)

// Square operator
INC_DEC_OPERATORS (Square)
inline Square  operator+  (Square  s, Delta d) { return Square(i32(s) + i32(d)); }
inline Square  operator-  (Square  s, Delta d) { return Square(i32(s) - i32(d)); }
inline Square& operator+= (Square &s, Delta d) { s = Square(i32(s) + i32(d)); return s; }
inline Square& operator-= (Square &s, Delta d) { s = Square(i32(s) - i32(d)); return s; }
inline Delta   operator-  (Square s1, Square s2) { return Delta (i32(s1) - i32(s2)); }

ARTHMAT_OPERATORS (Delta)
inline Delta  operator/  (Delta  d, i32 i) { return Delta (i32(d) / i); }
inline Delta& operator/= (Delta &d, i32 i) { d = Delta (i32(d) / i); return d; }

INC_DEC_OPERATORS (CSide)

// CRight operator
inline CRight  operator|  (CRight  cr, i32 i) { return CRight(i32(cr) | i); }
inline CRight  operator&  (CRight  cr, i32 i) { return CRight(i32(cr) & i); }
inline CRight  operator^  (CRight  cr, i32 i) { return CRight(i32(cr) ^ i); }
inline CRight& operator|= (CRight &cr, i32 i) { cr = CRight(i32(cr) | i); return cr; }
inline CRight& operator&= (CRight &cr, i32 i) { cr = CRight(i32(cr) & i); return cr; }
inline CRight& operator^= (CRight &cr, i32 i) { cr = CRight(i32(cr) ^ i); return cr; }

INC_DEC_OPERATORS (PieceT)

// Move operator
inline Move& operator|= (Move &m, i32 i) { m = Move(i32(m) | i); return m; }
inline Move& operator&= (Move &m, i32 i) { m = Move(i32(m) & i); return m; }

ARTHMAT_OPERATORS (Value)
INC_DEC_OPERATORS (Value)
// Additional operators to a Value
inline Value  operator+  (i32 i, Value v) { return Value(i + i32(v)); }
inline Value  operator-  (i32 i, Value v) { return Value(i - i32(v)); }
inline Value  operator*  (Value  v, double f) { return Value(i32(i32(v) * f)); }
inline Value& operator*= (Value &v, double f) { v = Value(i32(i32(v) * f)); return v; }
inline Value  operator/  (Value  v, i32 i) { return Value(i32(v) / i); }
inline Value& operator/= (Value &v, i32 i) { v = Value(i32(v) / i); return v; }
inline i32    operator/  (Value v1, Value v2) { return i32(v1) / i32(v2); }

// Make score from mid and end values
inline Score mk_score (i32 mg, i32 eg) { return Score ((mg << 0x10) + eg); }

// Extracting the signed lower and upper 16 bits it not so trivial because
// according to the standard a simple cast to short is implementation defined
// and so is a right shift of a signed integer.

union ValueUnion { u16 u; i16 s; };

inline Value mg_value (Score s) { ValueUnion mg = { u16(u32(s + 0x8000) >> 0x10) }; return Value(mg.s); }
inline Value eg_value (Score s) { ValueUnion eg = { u16(u32(s         )        ) }; return Value(eg.s); }

ARTHMAT_OPERATORS (Score)
// Only declared but not defined. Don't want to multiply two scores due to
// a very high risk of overflow. So user should explicitly convert to integer.
inline Score  operator*  (Score s1, Score s2);
// Multiplication & Division of a Score must be handled separately for each term
inline Score  operator*  (Score  s, double f) { return mk_score (mg_value (s) * f, eg_value (s) * f); }
inline Score& operator*= (Score &s, double f) { s = mk_score (mg_value (s) * f, eg_value (s) * f); return s; }
inline Score  operator/  (Score  s, i32 i) { return mk_score (mg_value (s) / i, eg_value (s) / i); }
inline Score& operator/= (Score &s, i32 i) { s = mk_score (mg_value (s) / i, eg_value (s) / i); return s; }

ARTHMAT_OPERATORS (Depth)
INC_DEC_OPERATORS (Depth)
inline Depth  operator/  (Depth d, i32 i) { return Depth(i32(d) / i); }
inline i32    operator/  (Depth d1, Depth d2) { return i32(d1) / i32(d2); }

#undef INC_DEC_OPERATORS
#undef ARTHMAT_OPERATORS
#undef BASIC_OPERATORS

inline bool   _ok       (Color c) { return WHITE == c || BLACK == c; }
inline Color  operator~ (Color c) { return Color(c^BLACK); }

inline bool   _ok       (File f) { return    !(f & ~i08(F_H)); }
inline File   operator~ (File f) { return File(f ^  i08(F_H)); }
inline File   to_file   (char f) { return File(f - 'a'); }

inline bool   _ok       (Rank r) { return    !(r & ~i08(R_8)); }
inline Rank   operator~ (Rank r) { return Rank(r ^  i08(R_8)); }
inline Rank   to_rank   (char r) { return Rank(r - '1'); }

inline Square operator| (File f, Rank r) { return Square(( r << 3) | i08(f)); }
inline Square operator| (Rank r, File f) { return Square((~r << 3) | i08(f)); }
inline Square to_square (char f, char r) { return to_file (f) | to_rank (r); }

inline bool   _ok   (Square s) { return    !(s & ~i08(SQ_H8)); }
inline File   _file (Square s) { return File(s &  i08(F_H)); }
inline Rank   _rank (Square s) { return Rank(s >> 3); }
inline Color  color (Square s) { return Color(!((s ^ (s >> 3)) & BLACK)); }

// FLIP   => SQ_A1 -> SQ_A8
inline Square operator~ (Square s) { return Square(s ^ i08(SQ_A8)); }
// MIRROR => SQ_A1 -> SQ_H1
inline Square operator! (Square s) { return Square(s ^ i08(SQ_H1)); }

inline Rank   rel_rank  (Color c, Rank   r) { return   Rank(r ^ (c * i08(R_8))); }
inline Rank   rel_rank  (Color c, Square s) { return rel_rank (c, _rank (s)); }
inline Square rel_sq    (Color c, Square s) { return Square(s ^ (c * i08(SQ_A8))); }

inline bool   opposite_colors (Square s1, Square s2)
{
    i08 s = i08(s1) ^ i08(s2);
    return ((s >> 3) ^ s) & BLACK;
}

inline Delta  pawn_push (Color c) { return WHITE == c ? DEL_N : DEL_S; }

inline CRight mk_castle_right (Color c)           { return CRight(CR_W << (c << BLACK)); }
inline CRight mk_castle_right (Color c, CSide cs) { return CRight(CR_WK << ((CS_Q == cs) + (c << BLACK))); }
inline CRight operator~ (CRight cr) { return CRight(((cr >> 2) & 0x3) | ((cr << 2) & 0xC)); }

template<Color C, CSide CS>
struct Castling
{
    static const CRight
    Right = C == WHITE ?
                CS == CS_Q ? CR_WQ : CR_WK :
                CS == CS_Q ? CR_BQ : CR_BK;
};

inline bool   _ok   (PieceT pt) { return PAWN <= pt && pt <= KING; }

inline Piece  operator| (Color c, PieceT pt) { return Piece((c << 3) | pt); }
//inline Piece  mk_piece  (Color c, PieceT pt) { return (c|pt); }

inline bool   _ok   (Piece p) { return (W_PAWN <= p && p <= W_KING) || (B_PAWN <= p && p <= B_KING); }
inline PieceT ptype (Piece p) { return PieceT(p & TOTL); }
inline Color  color (Piece p) { return Color(p >> 3); }
inline Piece  operator~ (Piece p) { return Piece(p ^ 8/*(BLACK << 3)*/); }

inline Square org_sq  (Move m) { return Square((m >> 6) & i08(SQ_H8)); }
inline Square dst_sq  (Move m) { return Square((m >> 0) & i08(SQ_H8)); }
inline PieceT promote (Move m) { return PieceT(((m >> 12) & ROOK) + NIHT); }
inline MoveT  mtype   (Move m) { return MoveT(PROMOTE & m); }
inline bool   _ok     (Move m)
{
    //Square org = org_sq (m);
    //Square dst = dst_sq (m);
    //if (org != dst)
    //{
    //    u08 del_f = dist (_file (org), _file (dst));
    //    u08 del_r = dist (_rank (org), _rank (dst));
    //    if (  del_f == del_r
    //       || 0 == del_f
    //       || 0 == del_r
    //       || 5 == del_f*del_f + del_r*del_r
    //       )
    //    {
    //        return true;
    //    }
    //}
    //return false;

    return org_sq (m) != dst_sq (m); // Catch MOVE_NONE & MOVE_NULL
}

//inline void org_sq  (Move &m, Square org) { m &= 0xF03F; m |= (org << 6); }
//inline void dst_sq  (Move &m, Square dst) { m &= 0xFFC0; m |= (dst << 0); }
inline void promote (Move &m, PieceT pt)  { m &= 0x0FFF; m |= (PROMOTE | (pt - NIHT) << 12); }
//inline void mtype   (Move &m, MoveT mt)   { m &= ~PROMOTE; m |= mt; }
//inline Move operator~ (Move m)
//{
//    Move mm = m;
//    org_sq (mm, ~org_sq (m));
//    dst_sq (mm, ~dst_sq (m));
//    return mm;
//}

template<MoveT MT>
inline   Move mk_move            (Square org, Square dst) { return Move(MT | u16(org << 6) | u16(dst)); }
// --------------------------------
// explicit template instantiations
template Move mk_move<NORMAL>    (Square org, Square dst);
template Move mk_move<CASTLE>    (Square org, Square dst);
template Move mk_move<ENPASSANT> (Square org, Square dst);
// --------------------------------
template<MoveT MT>
inline Move mk_move              (Square org, Square dst, PieceT pt/*=QUEN*/) { return Move(PROMOTE | ((((pt - NIHT) << 6) | u16(org)) << 6) | u16(dst)); }
inline Move mk_move              (Square org, Square dst) { return mk_move<NORMAL> (org, dst); }

inline double value_to_cp (Value   v) { return double   (v) / i32(VALUE_EG_PAWN); }
inline Value  cp_to_value (double cp) { return Value(i32(cp * i32(VALUE_EG_PAWN))); }

inline Value mates_in (i32 ply) { return +VALUE_MATE - ply; }
inline Value mated_in (i32 ply) { return -VALUE_MATE + ply; }

typedef std::chrono::milliseconds::rep TimePoint; // Time in milliseconds

const TimePoint MILLI_SEC        = 1000;
const TimePoint MINUTE_MILLI_SEC = MILLI_SEC * 60;
const TimePoint HOUR_MILLI_SEC   = MINUTE_MILLI_SEC * 60;

inline TimePoint now ()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>
               (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
}


template<class Entry, u32 Size>
struct HashTable
{

private:
    std::vector<Entry> _table = std::vector<Entry> (Size);

public:

    Entry* operator[] (Key k) { return &_table[u32(k) & (Size-1)]; }

};

typedef std::vector<Move> MoveVector;

//template<class Iterator> 
//inline auto slide (Iterator beg, Iterator end, Iterator pos) -> std::pair<Iterator, Iterator>
//{
//    if (pos < beg) return { pos, std::rotate (pos, beg, end) };
//    if (end < pos) return { std::rotate (beg, end, pos), pos };
//    return { beg, end };
//}

inline bool white_spaces (const std::string &str)
{
    return str.empty () || str.find_first_not_of (" \t\n") == std::string::npos;
}

inline std::string& trim_left (std::string &str)
{
    str.erase (str.begin (), 
                std::find_if (str.begin (), str.end (), 
                    //[](char c) { return !std::isspace (c, std::locale ()); }
                    std::not1 (std::ptr_fun<int, int> (std::isspace))
              ));
    return str;
}
inline std::string& trim_right (std::string &str)
{
    str.erase (std::find_if (str.rbegin (), str.rend (), 
                //[](char c) { return !std::isspace (c, std::locale()); }).base (), 
                std::not1 (std::ptr_fun<int, int> (std::isspace))).base (),
                    str.end ());
    return str;
}
inline std::string& trim (std::string &str)
{
    //size_t p0 = str.find_first_not_of (" \t\n");
    //size_t p1 = str.find_last_not_of (" \t\n");
    //p0  = p0 == std::string::npos ?  0 : p0;
    //p1  = p1 == std::string::npos ? p0 : p1 - p0 + 1;
    //str = str.substr (p0, p1);
    //return str;

    return trim_left (trim_right (str));
}

inline void remove_extension (std::string &filename)
{
    size_t last_dot = filename.find_last_of ('.');
    if (last_dot != std::string::npos) filename = filename.substr (0, last_dot); 
}

inline void convert_path (std::string &path)
{
    std::replace (path.begin (), path.end (), '\\', '/'); // Replace all '\' to '/'
}



extern const Value PIECE_VALUE[PHASE_NO][TOTL];

extern const std::string PIECE_CHAR;
extern const std::string COLOR_CHAR;

#endif // _TYPE_H_INC_
