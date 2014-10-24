#ifndef _TYPE_H_INC_
#define _TYPE_H_INC_

#include <cctype>
#include <climits>
#include <string>
#include <vector>
#include <algorithm>
#include <iosfwd>
#include "Platform.h"

typedef u64     Key;
typedef u64     Bitboard;

const u08   MAX_DEPTH    = 120;           // Maximum Depth (Ply)
const u08   MAX_DEPTH_6  = MAX_DEPTH + 6; // Maximum Stack Size

// File of Square
enum File : i08 { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H, F_NO };
// Rank of Square
enum Rank : i08 { R_1, R_2, R_3, R_4, R_5, R_6, R_7, R_8, R_NO };
// Diagonal of Square
enum Diag : i08
{
    D_01, D_02, D_03, D_04, D_05, D_06, D_07, D_08,
    D_09, D_10, D_11, D_12, D_13, D_14, D_15, D_NO

};

// Color of Square and Side
enum Color : i08 { WHITE, BLACK, CLR_NO };

// Square needs 6-bits (0-5) to be stored
// bit 0-2: File
// bit 3-5: Rank
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

// Delta of Square
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
    CS_K ,    // (KING)-SHORT CASTLE
    CS_Q ,    // (QUEEN)-LONG CASTLE
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
    PAWN  , // 000 - PAWN
    NIHT  , // 001 - KNIGHT
    BSHP  , // 010 - BISHOP
    ROOK  , // 011 - ROOK
    QUEN  , // 100 - QUEEN
    KING  , // 101 - KING
    NONE  , // 110 - NONE
    TOTL  , // 111 - TOTL
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

// Types of Move
enum MoveT : u16
{
    NORMAL    = 0 << 14, //0x0000, // 0000
    CASTLE    = 1 << 14, //0x4000, // 0100
    ENPASSANT = 2 << 14, //0x8000, // 1000
    PROMOTE   = 3 << 14  //0xC000, // 11xx
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

enum Value : i32
{
    VALUE_ZERO      = 0,
    VALUE_DRAW      = 0,

    VALUE_NONE      = SHRT_MAX,
    VALUE_INFINITE  = +i16(VALUE_NONE) - 1,

    VALUE_MATE      = +i16(VALUE_INFINITE) - 1,
    VALUE_KNOWN_WIN = +i16(VALUE_MATE) / 3,

    VALUE_MATE_IN_MAX_DEPTH = +i16(VALUE_MATE) - i16(MAX_DEPTH),

    VALUE_MG_PAWN =  198,  VALUE_EG_PAWN =  258,
    VALUE_MG_NIHT =  817,  VALUE_EG_NIHT =  846,
    VALUE_MG_BSHP =  836,  VALUE_EG_BSHP =  857,
    VALUE_MG_ROOK = 1270,  VALUE_EG_ROOK = 1278,
    VALUE_MG_QUEN = 2521,  VALUE_EG_QUEN = 2558,

    VALUE_MIDGAME = 15581, VALUE_ENDGAME = 3998
};

// Score enum keeps a midgame and an endgame value in a single integer (enum),
// first LSB 16 bits are used to store endgame value, while upper bits are used
// for midgame value. Compiler is free to choose the enum type as long as can
// keep its data, so ensure Score to be an integer type.
enum Score : i32 { SCORE_ZERO = 0 };

enum Depth : i16
{
    DEPTH_ZERO          =  0,
    DEPTH_ONE           =  1, // One Ply
    DEPTH_QS_CHECKS     =  0,
    DEPTH_QS_NO_CHECKS  = -1,
    DEPTH_QS_RECAPTURES = -5,

    DEPTH_NONE          = -6
};

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
    PHASE_KINGSAFETY =  20,

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
inline Value  operator/  (Value  v, i32 i) { return Value(i32(v) / i); }
inline Value& operator/= (Value &v, i32 i) { v = Value(i32(v) / i); return v; }
inline Value  operator*  (Value  v, float f) { return Value(i32(i32(v) * f)); }
inline Value& operator*= (Value &v, float f) { v = Value(i32(i32(v) * f)); return v; }

// Score
inline Score mk_score (i32 mg, i32 eg) { return Score ((mg << 16) + eg); }

// Extracting the signed lower and upper 16 bits it not so trivial because
// according to the standard a simple cast to short is implementation defined
// and so is a right shift of a signed integer.

inline Value mg_value (Score s) { return Value(((s + 0x8000) & ~0xFFFF) / 0x10000); }

inline Value eg_value (Score s) { return Value(i32(u32(s) & 0x7FFFU) - i32(u32(s) & 0x8000U)); }

ARTHMAT_OPERATORS (Score)
/// Only declared but not defined. Don't want to multiply two scores due to
/// a very high risk of overflow. So user should explicitly convert to integer.
inline Score operator* (Score s1, Score s2);
/// Division of a Score must be handled separately for each term
inline Score operator/ (Score s, i32 i) { return mk_score (mg_value (s) / i, eg_value (s) / i); }
inline Score  operator*  (Score  s, float f) { return mk_score (mg_value (s) * f, eg_value (s) * f); }
inline Score& operator*= (Score &s, float f) { s = mk_score (mg_value (s) * f, eg_value (s) * f); return s; }

ARTHMAT_OPERATORS (Depth)
INC_DEC_OPERATORS (Depth)
inline Depth  operator/  (Depth  d, i32 i) { return Depth(u08(d) / i); }
inline Depth  operator*  (Depth  d, float f) { return Depth(i32(i32(d) * f)); }
inline Depth& operator*= (Depth &d, float f) { d = Depth(i32(i32(d) * f)); return d; }

#undef INC_DEC_OPERATORS
#undef ARTHMAT_OPERATORS
#undef BASIC_OPERATORS

inline bool  _ok       (Color c) { return (WHITE == c) || (BLACK == c); }
inline Color operator~ (Color c) { return Color(c^BLACK); }

inline bool _ok       (File f) { return !(f & ~i08(F_H)); }
inline File operator~ (File f) { return File(f ^ i08(F_H)); }
inline File to_file   (char f) { return File(f - 'a'); }

inline bool _ok       (Rank r) { return !(r & ~i08(R_8)); }
inline Rank operator~ (Rank r) { return Rank(r ^ i08(R_8)); }
inline Rank to_rank   (char r) { return Rank(r - '1'); }

inline Square operator| (File f, Rank r) { return Square(( r << 3) | i08(f)); }
inline Square operator| (Rank r, File f) { return Square((~r << 3) | i08(f)); }
inline Square to_square (char f, char r) { return to_file (f) | to_rank (r); }
inline bool _ok     (Square s) { return !(s & ~i08(SQ_H8)); }
inline File _file   (Square s) { return File(s & i08(F_H)); }
inline Rank _rank   (Square s) { return Rank(s >> 3); }
inline Diag _diag18 (Square s) { return Diag((s >> 3) - (s & i08(SQ_H1)) + i08(SQ_H1)); } // R - F + 7
inline Diag _diag81 (Square s) { return Diag((s >> 3) + (s & i08(SQ_H1))); }              // R + F
inline Color color (Square s) { return Color(!((s ^ (s >> 3)) & BLACK)); }
// FLIP   => SQ_A1 -> SQ_A8
inline Square operator~ (Square s) { return Square(s ^ i08(SQ_A8)); }
// MIRROR => SQ_A1 -> SQ_H1
inline Square operator! (Square s) { return Square(s ^ i08(SQ_H1)); }

inline Rank   rel_rank  (Color c, Rank   r) { return   Rank(r ^ (c * i08(R_8))); }
inline Rank   rel_rank  (Color c, Square s) { return rel_rank (c, _rank (s)); }
inline Square rel_sq    (Color c, Square s) { return Square(s ^ (c * i08(SQ_A8))); }

inline bool opposite_colors (Square s1, Square s2)
{
    i08 s = i08(s1) ^ i08(s2);
    return ((s >> 3) ^ s) & BLACK;
}

inline Delta pawn_push (Color c) { return WHITE == c ? DEL_N : DEL_S; }

inline CRight mk_castle_right (Color c)           { return CRight(CR_W << (c << BLACK)); }
inline CRight mk_castle_right (Color c, CSide cs) { return CRight(CR_WK << ((CS_Q == cs) + (c << BLACK))); }
inline CRight operator~ (CRight cr) { return CRight(((cr >> 2) & 0x3) | ((cr << 2) & 0xC)); }

template<Color C, CSide CS>
struct Castling
{
    static const CRight
    Right = (C == WHITE) ?
            (CS == CS_Q) ? CR_WQ : CR_WK :
            (CS == CS_Q) ? CR_BQ : CR_BK;
};

inline bool   _ok    (PieceT pt) { return (PAWN <= pt && pt <= KING); }

inline Piece  operator| (Color c, PieceT pt) { return Piece((c << 3) | pt); }
//inline Piece mk_piece  (Color c, PieceT pt) { return (c|pt); }

inline bool   _ok   (Piece p) { return (W_PAWN <= p && p <= W_KING) || (B_PAWN <= p && p <= B_KING); }
inline PieceT ptype (Piece p) { return PieceT(p & TOTL); }
inline Color  color (Piece p) { return Color(p >> 3); }
inline Piece  operator~ (Piece p) { return Piece(p ^ (BLACK << 3)); }

inline Square org_sq  (Move m) { return Square((m >> 6) & i08(SQ_H8)); }
inline Square dst_sq  (Move m) { return Square((m >> 0) & i08(SQ_H8)); }
inline PieceT promote (Move m) { return PieceT(((m >> 12) & ROOK) + NIHT); }
inline MoveT  mtype   (Move m) { return MoveT(PROMOTE & m); }
inline bool   _ok     (Move m)
{
    //if (MOVE_NONE == m || MOVE_NULL == m)
    //{
    //    return false;
    //}
    //Square org = org_sq (m);
    //Square dst = dst_sq (m);
    //if (org == dst) return false;
    //
    //u08 del_f = BitBoard::file_dist (org, dst);
    //u08 del_r = BitBoard::rank_dist (org, dst);
    //if (  (del_f == del_r)
    //    || (0 == del_f) || (0 == del_r)
    //    || (5 == del_f*del_f + del_r*del_r))
    //{
    //    return true;
    //}
    //return false;
    //return (org_sq (m) != dst_sq (m));

    return (MOVE_NONE != m) && (MOVE_NULL != m) && (org_sq (m) != dst_sq (m));
}

inline void org_sq    (Move &m, Square org) { m &= 0xF03F; m |= (org << 6); }
inline void dst_sq    (Move &m, Square dst) { m &= 0xFFC0; m |= (dst << 0); }
inline void   promote (Move &m, PieceT pt)  { m &= 0x0FFF; m |= (PROMOTE | ((pt - NIHT) & ROOK) << 12); }
//inline void mtype     (Move &m, MoveT mt)   { m &= ~PROMOTE; m |= mt; }
//inline Move operator~ (Move m)
//{
//    Move mm = m;
//    org_sq (mm, ~org_sq (m));
//    dst_sq (mm, ~dst_sq (m));
//    return mm;
//}

template<MoveT MT>
inline Move mk_move (Square org, Square dst) { return Move(MT | (org << 6) | (dst << 0)); }
// --------------------------------
// explicit template instantiations
template Move mk_move<NORMAL>    (Square org, Square dst);
template Move mk_move<CASTLE>    (Square org, Square dst);
template Move mk_move<ENPASSANT> (Square org, Square dst);
// --------------------------------
//template<>
//inline Move mk_move<PROMOTE> (Square org, Square dst) { return mk_move<PROMOTE> (org, dst, QUEN); }

template<MoveT MT>
inline Move mk_move (Square org, Square dst, PieceT pt) { return MOVE_NONE; }
template<>
inline Move mk_move<PROMOTE> (Square org, Square dst, PieceT pt) { return Move(PROMOTE | (( i08(pt) - i08(NIHT)) << 12) | (org << 6) | (dst << 0)); }

inline Move mk_move (Square org, Square dst) { return mk_move<NORMAL> (org, dst); }

inline float value_to_cp (Value  v) { return (float)     v / i32(VALUE_EG_PAWN); }
inline Value cp_to_value (float cp) { return (Value)i32(cp * i32(VALUE_EG_PAWN)); }

inline Value mates_in (i32 ply) { return +VALUE_MATE - ply; }
inline Value mated_in (i32 ply) { return -VALUE_MATE + ply; }

// GameClock stores the available time and time-gain per move
struct GameClock
{
    // unit: milli-seconds
    u32 time;   // Time left
    u32 inc;    // Time gain

    GameClock ()
        : time (0)
        , inc  (0)
    {}
};

template<class Entry, u32 Size>
struct HashTable
{

private:
    std::vector<Entry> _table;

public:

    HashTable ()
        : _table (Size, Entry ())
    {}

    inline Entry* operator[] (Key k) { return &_table[u32(k) & (Size - 1)]; }

};

// prefetch() preloads the given address in L1/L2 cache.
// This is a non-blocking function that doesn't stall
// the CPU waiting for data to be loaded from memory,
// which can be quite slow.
#ifdef PREFETCH

#   if (defined(_MSC_VER) || defined(__INTEL_COMPILER))

#   include <xmmintrin.h> // Intel and Microsoft header for _mm_prefetch()

    INLINE void prefetch (const char *addr)
    {
#       if defined(__INTEL_COMPILER)
        {
            // This hack prevents prefetches from being optimized away by
            // Intel compiler. Both MSVC and gcc seem not be affected by this.
            __asm__ ("");
        }
#       endif
        _mm_prefetch (addr, _MM_HINT_T0);
    }

#   else

    INLINE void prefetch (const char *addr)
    {
        __builtin_prefetch (addr);
    }

#   endif

#else

    INLINE void prefetch (const char *) {}

#endif

inline char toggle_case (unsigned char c) { return char (islower (c) ? toupper (c) : tolower (c)); }

inline bool white_spaces (const std::string &str)
{
    return str.empty () || str.find_first_not_of (" \t\n") == std::string::npos;
}

inline void trim (std::string &str)
{
    size_t pb = str.find_first_not_of (" \t\n");
    size_t pe = str.find_last_not_of (" \t\n");
    pb = pb == std::string::npos ? 0 : pb;
    pe = pe == std::string::npos ? pb : pe - pb + 1;
    str = str.substr (pb, pe);
}

inline void remove_extension (std::string &filename)
{
    size_t last_dot = filename.find_last_of ('.');
    filename = last_dot == std::string::npos ? filename : filename.substr (0, last_dot); 
}

inline void convert_path (std::string &path)
{
    std::replace (path.begin (), path.end (), '\\', '/'); // Replace all '\' to '/'
}

CACHE_ALIGN(8) extern const Value PIECE_VALUE[PHASE_NO][TOTL];

extern const std::string PIECE_CHAR;
extern const std::string COLOR_CHAR;

#endif // _TYPE_H_INC_
