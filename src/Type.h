#pragma once

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iosfwd>
#include <sstream>
#include <string>
#include <array>
#include <vector>

/// Compiling:
/// With Makefile (e.g. for Linux and OSX), configuration is done automatically, to get started type 'make help'.
/// Without Makefile (e.g. with Microsoft Visual Studio) some switches need to be set manually:
///
/// -DNDEBUG    | Disable debugging mode. Always use this.
/// -DPREFETCH  | Enable use of prefetch asm-instruction.
///             | Don't enable it if want the executable to run on some very old machines.
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
/// _WIN32             Compilation target is Windows (any)
/// _WIN64             Compilation target is Windows 64-bit

#if defined(_MSC_VER)
// Disable some silly and noisy warning from MSVC compiler
#   pragma warning (disable: 4127) // Conditional expression is constant
#   pragma warning (disable: 4146) // Unary minus operator applied to unsigned type
#   pragma warning (disable: 4800) // Forcing value to bool 'true' or 'false'

#   if defined(_WIN64)
#       if !defined(BIT64)
#           define BIT64
#       endif
#   endif

typedef   signed __int8     i08;
typedef unsigned __int8     u08;
typedef   signed __int16    i16;
typedef unsigned __int16    u16;
typedef   signed __int32    i32;
typedef unsigned __int32    u32;
typedef   signed __int64    i64;
typedef unsigned __int64    u64;

#   define S32(X) (X ##  i32)
#   define U32(X) (X ## ui32)
#   define S64(X) (X ##  i64)
#   define U64(X) (X ## ui64)

#else

#   include <inttypes.h>

typedef          int8_t    i08;
typedef         uint8_t    u08;
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

#if defined(BM2)
#   include <immintrin.h>   // Header for BMI2 instructions
// BEXTR = Bit field extract (with register)
// PDEP  = Parallel bits deposit
// PEXT  = Parallel bits extract
// BLSR  = Reset lowest set bit
#   if defined(BIT64)
#       define BEXTR(b, m, l)   _bextr_u64(b, m, l)
#       define PDEP(b, m)       _pdep_u64(b, m)
#       define PEXT(b, m)       _pext_u64(b, m)
#       define BLSR(b)          _blsr_u64(b)
#   else
//#       define BEXTR(b, m, l)   _bextr_u32(b, m, l)
//#       define PDEP(b, m)       _pdep_u32(b, m)
//#       define PEXT(b, m)       _pext_u32(b, m)
//#       define BLSR(b)          _blsr_u32(b)
#   endif
#endif

typedef u64 Key;
typedef u64 Bitboard;

enum Color : i08 { WHITE, BLACK, CLR_NO };

enum File : i08 { F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H, F_NO };

enum Rank : i08 { R_1, R_2, R_3, R_4, R_5, R_6, R_7, R_8, R_NO };

/// Square needs 6-bits to be stored
/// bit 0-2: File
/// bit 3-5: Rank
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

enum Delta : i08
{
    DEL_O =  000,

    DEL_E =  001,
    DEL_N =  010,

    DEL_W = -DEL_E,
    DEL_S = -DEL_N,

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
};

typedef i16 Depth;

constexpr Depth DEP_ZERO        =  0;
constexpr Depth DEP_QS_CHECK    =  0;
constexpr Depth DEP_QS_NO_CHECK = -1;
constexpr Depth DEP_QS_RECAP    = -5;
constexpr Depth DEP_NONE        = -6;
constexpr Depth DEP_OFFSET      = -7;
// Maximum Plies
constexpr Depth DEP_MAX         = 245; // = 256 + DEP_OFFSET - 4


enum CastleSide : i08 { CS_KING, CS_QUEN, CS_NO };

/// Castle Right defined as in Polyglot book hash key
enum CastleRight : u08
{
    CR_NONE  = 0,                   // 0000
    CR_WKING = 1,                   // 0001
    CR_WQUEN = CR_WKING << 1,       // 0010
    CR_BKING = CR_WKING << 2,       // 0100
    CR_BQUEN = CR_WKING << 3,       // 1000

    CR_WHITE = CR_WKING + CR_WQUEN, // 0011
    CR_BLACK = CR_BKING + CR_BQUEN, // 1100
    CR_KING  = CR_WKING + CR_BKING, // 0101
    CR_QUEN  = CR_WQUEN + CR_BQUEN, // 1010
    CR_ANY   = CR_WHITE + CR_BLACK, // 1111
    CR_NO,
};

enum PieceType : i08
{
    PAWN , // 0
    NIHT , // 1
    BSHP , // 2
    ROOK , // 3
    QUEN , // 4
    KING , // 5
    NONE , // 6
    PT_NO, // 7
};
/// Piece needs 4-bits to be stored
/// bit 0-2: Type of piece
/// bit   3: Color of piece { White = 0..., Black = 1... }
enum Piece : u08
{
    W_PAWN   = 0, //  0000
    W_NIHT      , //  0001
    W_BSHP      , //  0010
    W_ROOK      , //  0011
    W_QUEN      , //  0100
    W_KING      , //  0101

    NO_PIECE = 6, //  0110

    B_PAWN   = 8, //  1000
    B_NIHT      , //  1001
    B_BSHP      , //  1010
    B_ROOK      , //  1011
    B_QUEN      , //  1100
    B_KING      , //  1101

    MAX_PIECE   , //  1110
};

enum MoveType : u16
{
    NORMAL    = 0 << 14, // [00]-- ===
    CASTLE    = 1 << 14, // [01]-- ===
    ENPASSANT = 2 << 14, // [10]-- ===
    PROMOTE   = 3 << 14, // [11]xx ===
};

//constexpr i16 MaxMoves          = 256;

/// Move needs 16-bits to be stored
///
/// bit 00-05: Destiny square
/// bit 06-11: Origin square
/// bit 12-13: Promotion piece
/// bit 14-15: Move Type
///
/// Special cases are MOVE_NONE and MOVE_NULL.
enum Move : u16
{
    MOVE_NONE = 0x00,
    MOVE_NULL = 0x41,
};

enum Value : i32
{
    VALUE_ZERO      = 0,
    VALUE_DRAW      = 0,

    VALUE_NONE      = 32767, //SHRT_MAX,
    VALUE_INFINITE  = VALUE_NONE - 1,
    VALUE_MATE      = VALUE_INFINITE - 1,

    VALUE_MATE_MAX_PLY = VALUE_MATE - 2*DEP_MAX,

    VALUE_KNOWN_WIN = 10000,

    VALUE_MG_PAWN =  128, VALUE_EG_PAWN =  213,
    VALUE_MG_NIHT =  781, VALUE_EG_NIHT =  854,
    VALUE_MG_BSHP =  825, VALUE_EG_BSHP =  915,
    VALUE_MG_ROOK = 1276, VALUE_EG_ROOK = 1380,
    VALUE_MG_QUEN = 2538, VALUE_EG_QUEN = 2682,

    VALUE_MIDGAME = 15258,
    VALUE_ENDGAME =  3915,

    //VALUE_MG_FULL = VALUE_MG_NIHT * 4 + VALUE_MG_BSHP * 4 + VALUE_MG_ROOK * 4 + VALUE_MG_QUEN * 2,
    //VALUE_EG_FULL = VALUE_EG_NIHT * 4 + VALUE_EG_BSHP * 4 + VALUE_EG_ROOK * 4 + VALUE_EG_QUEN * 2,
};
/// Score needs 32-bits to be stored
/// the lower 16-bits are used to store the midgame value
/// the upper 16-bits are used to store the endgame value
/// Take some care to avoid left-shifting a signed int to avoid undefined behavior.
enum Score : u32
{
    SCORE_ZERO = 0,
};

enum Bound : u08
{
    BOUND_NONE  = 0,
    BOUND_UPPER = 1,
    BOUND_LOWER = 2,
    BOUND_EXACT = 3,
};

enum Phase : u08
{
    MG    = 0,
    EG    = 1,
};

enum Scale : u08
{
    SCALE_DRAW    =   0,
    SCALE_NORMAL  =  64,
    SCALE_MAX     = 128,
    SCALE_NONE    = 255,
};

#define BASIC_OPERATORS(T)                                             \
    constexpr T operator+(T t) { return T(+i32(t)); }                  \
    constexpr T operator-(T t) { return T(-i32(t)); }                  \
    constexpr T operator+(T t1, T t2) { return T(i32(t1) + i32(t2)); } \
    constexpr T operator-(T t1, T t2) { return T(i32(t1) - i32(t2)); } \
    inline T& operator+=(T &t1, T t2) { return t1 = t1 + t2; }         \
    inline T& operator-=(T &t1, T t2) { return t1 = t1 - t2; }

#define ARTHMAT_OPERATORS(T)                                    \
    constexpr T operator+(T t, i32 i) { return T(i32(t) + i); } \
    constexpr T operator-(T t, i32 i) { return T(i32(t) - i); } \
    constexpr T operator*(T t, i32 i) { return T(i32(t) * i); } \
    constexpr T operator*(i32 i, T t) { return T(i32(t) * i); } \
    constexpr T operator/(T t, i32 i) { return T(i32(t) / i); } \
    inline T& operator+=(T &t, i32 i) { return t = t + i; }     \
    inline T& operator-=(T &t, i32 i) { return t = t - i; }     \
    inline T& operator*=(T &t, i32 i) { return t = t * i; }     \
    inline T& operator/=(T &t, i32 i) { return t = t / i; }

#define INC_DEC_OPERATORS(T)                                 \
    inline T& operator++(T &t) { return t = T(i32(t) + 1); } \
    inline T& operator--(T &t) { return t = T(i32(t) - 1); }

#define BITWISE_OPERATORS(T)                                           \
    constexpr T operator~(T t) { return T(~i32(t)); }                  \
    constexpr T operator|(T t1, T t2) { return T(i32(t1) | i32(t2)); } \
    constexpr T operator&(T t1, T t2) { return T(i32(t1) & i32(t2)); } \
    constexpr T operator^(T t1, T t2) { return T(i32(t1) ^ i32(t2)); } \
    inline T& operator|=(T &t1, T t2) { return t1 = t1 | t2; }         \
    inline T& operator&=(T &t1, T t2) { return t1 = t1 & t2; }         \
    inline T& operator^=(T &t1, T t2) { return t1 = t1 ^ t2; }

BASIC_OPERATORS(File)
//ARTHMAT_OPERATORS(File)
INC_DEC_OPERATORS(File)

BASIC_OPERATORS(Rank)
//ARTHMAT_OPERATORS(Rank)
INC_DEC_OPERATORS(Rank)

BASIC_OPERATORS(Delta)
ARTHMAT_OPERATORS(Delta)

inline Square operator+(Square s, Delta d) { return Square(i32(s) + i32(d)); }
inline Square operator-(Square s, Delta d) { return Square(i32(s) - i32(d)); }

inline Square& operator+=(Square &s, Delta d) { s = s + d; return s; }
inline Square& operator-=(Square &s, Delta d) { s = s - d; return s; }

inline Delta operator-(Square s1, Square s2) { return Delta(i32(s1) - i32(s2)); }

INC_DEC_OPERATORS(Square)

INC_DEC_OPERATORS(CastleSide)

BITWISE_OPERATORS(CastleRight)

INC_DEC_OPERATORS(PieceType)

BASIC_OPERATORS(Value)
ARTHMAT_OPERATORS(Value)
INC_DEC_OPERATORS(Value)

constexpr Score make_score(i32 mg, i32 eg)
{
    return Score(i32(u32(eg) << 0x10) + mg);
}

/// Extracting the signed lower and upper 16 bits is not so trivial because
/// according to the standard a simple cast to short is implementation defined
/// and so is a right shift of a signed integer.

inline Value mg_value(u32 s)
{
    union { u16 u; i16 s; } mg = { u16(u32(s + 0x0000) >> 0x00) };
    return Value(mg.s);
}
inline Value eg_value(u32 s)
{
    union { u16 u; i16 s; } eg = { u16(u32(s + 0x8000) >> 0x10) };
    return Value(eg.s);
}

BASIC_OPERATORS(Score)

/// Division of a Score must be handled separately for each term
inline Score operator/(Score s, i32 i) { return make_score(mg_value(s) / i, eg_value(s) / i); }
/// Multiplication of a Score by an integer. We check for overflow in debug mode.
inline Score operator*(Score s, i32 i)
{
    Score score = Score(i32(s) * i);

    assert(eg_value(score) == (eg_value(s) * i));
    assert(mg_value(score) == (mg_value(s) * i));
    assert((0 == i) || (score / i) == s);

    return score;
}

inline Score& operator/=(Score &s, i32 i) { s = s / i; return s; }
inline Score& operator*=(Score &s, i32 i) { s = s * i; return s; }

/// Multiplication of a Score by a boolean
inline Score operator*(Score s, bool b) { return s * i32(b); }

/// Don't want to multiply two scores due to a very high risk of overflow.
/// So user should explicitly convert to integer.
Score operator*(Score, Score) = delete;
Score operator/(Score, Score) = delete;

BITWISE_OPERATORS(Bound)

#undef BITWISE_OPERATORS
#undef INC_DEC_OPERATORS
#undef ARTHMAT_OPERATORS
#undef BASIC_OPERATORS

constexpr bool        _ok(Color c) { return WHITE == c || BLACK == c; }
constexpr Color operator~(Color c) { return Color(c ^ BLACK); }

constexpr bool       _ok(File f) { return F_A <= f && f <= F_H; }
constexpr File operator~(File f) { return File(f ^ F_H); }
constexpr File   to_file(char f) { return File(f - 'a'); }
// Map file [ABCDEFGH] to file [ABCDDCBA]
inline File     map_file(File f) { return std::min(f, ~f); }

constexpr bool       _ok(Rank r) { return R_1 <= r && r <= R_8; }
constexpr Rank operator~(Rank r) { return Rank(r ^ R_8); }
constexpr Rank   to_rank(char r) { return Rank(r - '1'); }

constexpr Square operator|(File f, Rank r) { return Square(( r << 3) + f); }
constexpr Square operator|(Rank r, File f) { return Square((~r << 3) + f); }
constexpr Square to_square(char f, char r) { return to_file(f) | to_rank(r); }

constexpr bool    _ok(Square s) { return SQ_A1 <= s && s <= SQ_H8; }
constexpr File  _file(Square s) { return File((s >> 0) & F_H); }
constexpr Rank  _rank(Square s) { return Rank((s >> 3) & R_8); }
constexpr Color color(Square s) { return Color(0 == ((_file(s) ^ _rank(s)) & BLACK)); }

// SQ_A1 -> SQ_A8
constexpr Square operator~(Square s) { return Square(i08(s) ^ i08(SQ_A8)); }
// SQ_A1 -> SQ_H1
constexpr Square operator!(Square s) { return Square(i08(s) ^ i08(SQ_H1)); }

constexpr bool opposite_colors(Square s1, Square s2)
{
    //i08 s = i08(s1) ^ i08(s2);
    //return 0 != (((s >> 3) ^ s) & BLACK);
    return 0 != ((_file(s1) ^ _rank(s1) ^ _file(s2) ^ _rank(s2)) & BLACK);
}

constexpr Square rel_sq(Color c, Square s) { return Square(i08(s) ^ (c*SQ_A8)); }

constexpr Rank rel_rank(Color c, Rank r)   { return Rank(r ^ (c*R_8)); }
constexpr Rank rel_rank(Color c, Square s) { return rel_rank(c, _rank(s)); }

constexpr Delta pawn_push (Color c) { return DEL_N  + 2 * c * DEL_S; }
constexpr Delta pawn_l_att(Color c) { return DEL_NW + 2 * c * DEL_SE; }
constexpr Delta pawn_r_att(Color c) { return DEL_NE + 2 * c * DEL_SW; }

constexpr CastleRight operator|(Color c, CastleSide cs)
{
    return CastleRight(CR_WKING << (2 * c + (cs == CS_QUEN)));
}

constexpr bool   _ok(PieceType pt) { return PAWN <= pt && pt <= KING; }

constexpr bool        _ok(Piece p) { return (W_PAWN <= p && p <= W_KING) || (B_PAWN <= p && p <= B_KING); }
constexpr PieceType ptype(Piece p) { return PieceType(p & PT_NO); }
constexpr Color     color(Piece p) { return Color((p >> 3) & BLACK); }
constexpr Piece operator~(Piece p) { return Piece(p ^ 8); }
constexpr Piece operator|(Color c, PieceType pt) { return Piece((c << 3) + pt); }

constexpr Square     org_sq(Move m) { return Square((m >> 6) & SQ_H8); }
constexpr Square     dst_sq(Move m) { return Square((m >> 0) & SQ_H8); }
constexpr bool          _ok(Move m) { return org_sq(m) != dst_sq(m); }
constexpr PieceType promote(Move m) { return PieceType(((m >> 12) & 3) + NIHT); }
constexpr MoveType    mtype(Move m) { return MoveType(m & PROMOTE); }
constexpr u16    move_index(Move m) { return u16(m & 0x0FFF); }
constexpr Square fix_dst_sq(Move m, bool chess960 = false)
{
    return CASTLE != mtype(m)
        || chess960 ?
            dst_sq(m) :
            (dst_sq(m) > org_sq(m) ? F_G : F_C) | _rank(dst_sq(m));
}

template<MoveType MT>
constexpr Move make_move(Square org, Square dst)
{
    return Move(MT + (org << 6) + dst);
}

constexpr Move make_promote_move(Square org, Square dst, PieceType pt)
{
    return Move(PROMOTE + ((pt - NIHT) << 12) + (org << 6) + dst);
}
template<>
constexpr Move make_move<PROMOTE>(Square org, Square dst)
{
    return make_promote_move(org, dst, QUEN);
}

constexpr Move reverse_move(Move m)
{
  return make_move<NORMAL>(dst_sq(m), org_sq(m));
}

constexpr i16   value_to_cp(Value v) { return i16((v*100)/VALUE_EG_PAWN); }
constexpr Value cp_to_value(i16  cp) { return Value((i32(cp)*VALUE_EG_PAWN)/100); }
/// It adjusts a mate score from "plies to mate from the root" to "plies to mate from the current position".
/// Non-mate scores are unchanged.
/// The function is called before storing a value to the transposition table.
constexpr Value value_to_tt(Value v, i32 ply)
{
    //assert(VALUE_NONE != v);
    return v >= +VALUE_MATE_MAX_PLY ? v + ply :
           v <= -VALUE_MATE_MAX_PLY ? v - ply :
                                      v;
}
/// It adjusts a mate score from "plies to mate from the current position" to "plies to mate from the root".
/// Non-mate scores are unchanged.
/// The function is called after retrieving a value of the transposition table.
constexpr Value value_of_tt(Value v, i32 ply, u08 clock_ply)
{
    return v ==  VALUE_NONE         ? VALUE_NONE :
           v >= +VALUE_MATE_MAX_PLY ? VALUE_MATE - v > 99 - clock_ply ? +VALUE_MATE_MAX_PLY : v - ply :
           v <= -VALUE_MATE_MAX_PLY ? VALUE_MATE + v > 99 - clock_ply ? -VALUE_MATE_MAX_PLY : v + ply :
                                      v;
}

constexpr Value mates_in(i32 ply) { return +VALUE_MATE - ply; }
constexpr Value mated_in(i32 ply) { return -VALUE_MATE + ply; }

typedef std::chrono::milliseconds::rep TimePoint; // Time in milli-seconds

static_assert (sizeof (TimePoint) == sizeof (i64), "TimePoint should be 64 bits");

inline TimePoint now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct ValMove
{
public:
    Move move;
    i32  value;

    ValMove(Move m, i32 v)
        : move(m)
        , value(v)
    {}
    explicit ValMove(Move m = MOVE_NONE)
        : ValMove(m, 0)
    {}

    operator Move() const { return move; }
    void operator=(Move m) { move = m; }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const = delete;
    operator double() const = delete;

    bool operator<(const ValMove &vm) const { return value < vm.value; }
    bool operator>(const ValMove &vm) const { return value > vm.value; }
    //bool operator<=(const ValMove &vm) const { return value <= vm.value; }
    //bool operator>=(const ValMove &vm) const { return value >= vm.value; }
};

class ValMoves
    : public std::vector<ValMove>
{
public:

    void operator+=(Move move) { emplace_back(move); }
    void operator-=(Move move) { erase(std::remove(begin(), end(), move), end()); }
};

template<class T, u32 Size>
struct HashTable
{
private:
    std::array<T, Size> table;

public:

    void clear()
    {
        table.fill(T());
    }

    T* operator[](Key key)
    {
        return &table[u32(key) & (Size - 1)];
    }
};

// Return the sign of a number (-1, 0, 1)
template<class T>
constexpr i32 sign(const T val)
{
    return (T(0) < val) - (val < T(0));
}

template<class T>
const T& clamp(const T &v, const T &minimum, const T &maximum)
{
    return (minimum > v) ? minimum :
           (v > maximum) ? maximum : v;
}

template<class Container>
inline void replace(Container &container,
                    const typename Container::value_type &old_value,
                    const typename Container::value_type &new_value)
{
    std::replace(container.begin(), container.end(), old_value, new_value);
}

inline bool white_spaces(const std::string &str)
{
    return str.empty()
        || str.find_first_not_of(" \t\n") == std::string::npos
        || str == "<empty>";
}

inline std::string& to_lower(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}
inline std::string& to_upper(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}
inline std::string& toggle(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(),
                   [](int c) -> int
                   { return islower(c) ? toupper(c) : tolower(c); });
    return str;
}

inline std::string& ltrim(std::string &str)
{
    str.erase(str.begin(),
              std::find_if(str.begin(), str.end(),
                           std::not1(std::function<bool(const std::string::value_type&)>(::isspace))));
    return str;
}
inline std::string& rtrim(std::string &str)
{
    str.erase(std::find_if(str.rbegin(), str.rend(),
                           std::not1(std::function<bool(const std::string::value_type&)>(::isspace))).base(),
              str.end());
    return str;
}
inline std::string& trim(std::string &str)
{
    return ltrim(rtrim(str));
}
inline std::string append_path(const std::string &base_path, const std::string &file_path)
{
    return base_path[base_path.length() - 1] != '/' ?
            base_path + '/' + file_path :
            base_path + file_path;
}

constexpr std::array<Square, SQ_NO> SQ
{
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
};
constexpr std::array<std::array<Value, PT_NO>, 2> PieceValues
{{
    { VALUE_MG_PAWN, VALUE_MG_NIHT, VALUE_MG_BSHP, VALUE_MG_ROOK, VALUE_MG_QUEN, VALUE_ZERO, VALUE_ZERO },
    { VALUE_EG_PAWN, VALUE_EG_NIHT, VALUE_EG_BSHP, VALUE_EG_ROOK, VALUE_EG_QUEN, VALUE_ZERO, VALUE_ZERO }
}};

//inline std::vector<std::string> split(const std::string str, char delimiter = ' ', bool keep_empty = true, bool do_trim = false)
//{
//    std::vector<std::string> tokens;
//    std::istringstream iss{str};
//    while (iss.good())
//    {
//        std::string token;
//        const bool fail = !std::getline(iss, token, delimiter);
//        if (do_trim)
//        {
//            token = trim(token);
//        }
//        if (   keep_empty
//            || !token.empty())
//        {
//            tokens.push_back(token);
//        }
//        if (fail)
//        {
//            break;
//        }
//    }
//
//    return tokens;
//}

//inline void erase_substring(std::string &str, const std::string &sub)
//{
//    std::string::size_type pos;
//    while ((pos = str.find(sub)) != std::string::npos)
//    {
//        str.erase(pos, sub.length());
//    }
//}
//
//inline void erase_substrings(std::string &str, const std::vector<std::string> &sub_list)
//{
//    std::for_each(sub_list.begin(), sub_list.end(), std::bind(erase_substring, std::ref(str), std::placeholders::_1));
//}
//
//inline void erase_extension(std::string &filename)
//{
//    std::string::size_type pos = filename.find_last_of('.');
//    if (pos != std::string::npos)
//    {
//        //filename = filename.substr(0, pos);
//        filename.erase(pos, std::string::npos);
//    }
//}
