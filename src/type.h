#pragma once

/// Compiling:
/// With Makefile (e.g. for Linux and OSX), configuration is done automatically.
/// To get started type 'make help'.
/// Without Makefile (e.g. with Microsoft Visual Studio) some switches need to be set manually:
///
/// -DNDEBUG        | Disable debugging mode. Always use this for release.
/// -DUSE_PREFETCH  | Enable use of prefetch asm-instruction.
///                 | Don't enable it if want to run on some very old machines.
/// -DUSE_POPCNT    | Add runtime support for use of USE_POPCNT asm-instruction.
///                 | Works only in 64-bit mode and requires hardware with USE_POPCNT support.
/// -DUSE_BMI2      | Add runtime support for use of USE_BMI2 asm-instruction.
///                 | Works only in 64-bit mode and requires hardware with USE_BMI2 support.

#include <cassert>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

/// Predefined macros hell:
///
/// __GNUC__           Compiler is gcc, Clang or Intel on Linux
/// __INTEL_COMPILER   Compiler is Intel
/// _MSC_VER           Compiler is MSVC or Intel on Windows
/// _WIN32             Compilation target is Windows (any)
/// _WIN64             Compilation target is Windows 64-bit

#if defined(_MSC_VER)
    // Disable some silly and noisy warning from MSVC compiler
    #pragma warning (disable:  4127)    // Conditional expression is constant
    #pragma warning (disable:  4146)    // Unary minus operator applied to unsigned type
    #pragma warning (disable:  4800)    // Forcing value to bool 'true' or 'false'
    #pragma warning (disable: 26429)    // USE_NOTNULL: Symbol is never tested for nullness, it can be marked as gsl::not_null
    #pragma warning (disable: 26446)    // Prefer to use gsl::at() instead of unchecked subscript operator (bounds.x)
    #pragma warning (disable: 26475)    // NO_FUNCTION_STYLE_CASTS: Do not use function style C - casts
    #pragma warning (disable: 26481)    // Don't use pointer arithmetic. Use span instead (bounds.x)
    #pragma warning (disable: 26482)    // Only index into arrays using constant expressions
    #pragma warning (disable: 26485)    // Expression 'array-name': No array to pointer decay (bounds.x)
    #pragma warning (disable: 26493)    // NO_CSTYLE_CAST: Don't use C-style casts (type.x)
    #pragma warning (disable: 26812)    // The enum type type-name is unscoped. Prefer 'enum class' over 'enum' (Enum.3)

    #if defined(_WIN64)
        #if !defined(IS_64BIT)
            #define IS_64BIT
        #endif
    #endif

    //#define I32(X) (X ##  i32)
    //#define U32(X) (X ## ui32)
    //#define I64(X) (X ##  i64)
    //#define U64(X) (X ## ui64)
#endif

#define I32(X) (X ##   L)
#define U32(X) (X ##  UL)
#define I64(X) (X ##  LL)
#define U64(X) (X ## ULL)

#if defined(_WIN64) && defined(_MSC_VER) // When no Makefile used
    #include <intrin.h>     // Microsoft Header for _BitScanForward64() & _BitScanReverse64()
#endif

#if defined(USE_POPCNT) && (defined(__INTEL_COMPILER) || defined(_MSC_VER))
    #include <nmmintrin.h>  // Intel and Microsoft Header for _mm_popcnt_u64()
#endif

#if defined(USE_PREFETCH) && (defined(__INTEL_COMPILER) || defined(_MSC_VER))
    #include <xmmintrin.h>  // Intel and Microsoft header for _mm_prefetch()
#endif

#if defined(USE_BMI2)
    #include <immintrin.h>  // Header for _pdep_u64() & _pext_u64() intrinsic
  //#define PDEP(b, m)  _pdep_u64(b, m) // Parallel bits deposit
    #define PEXT(b, m)  _pext_u64(b, m) // Parallel bits extract
#endif

#if defined(__GNUC__ ) && (__GNUC__ < 9 || (__GNUC__ == 9 && __GNUC_MINOR__ <= 2)) && defined(_WIN32) && !defined(__clang__)
    #define ALIGNAS_ON_STACK_VARIABLES_BROKEN
#endif

// Size of cache line (in bytes)
constexpr size_t CacheLineSize{ 64 };

#define ASSERT_ALIGNED(ptr, alignment) assert(reinterpret_cast<uintptr_t>(ptr) % alignment == 0)

#define XSTRING(x)      #x
#define STRINGIFY(x)    XSTRING(x)

using Key = uint64_t;
using Bitboard = uint64_t;

// Return the sign of a number (-1, 0, 1)
template<typename T>
inline constexpr int32_t sign(T const &v) {
    //return (T{} < v) - (v < T{});
    return (T(0) < v) - (v < T(0));
}

enum Color {
    WHITE, BLACK,
    COLORS = 2
};

enum File : int32_t {
    FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H,
    FILES = 8
};

enum Rank : int32_t {
    RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8,
    RANKS = 8
};

/// Square needs 6-bits to be stored
/// bit 0-2: File
/// bit 3-5: Rank
enum Square : int32_t {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE,
    SQUARES = 64
};

enum Direction : int32_t {
    EAST    =  1,
    NORTH   =  8,
    WEST    = -EAST,
    SOUTH   = -NORTH,

    EAST_2  = EAST + EAST,
    WEST_2  = WEST + WEST,
    NORTH_2 = NORTH + NORTH,
    SOUTH_2 = SOUTH + SOUTH,

    NORTH_EAST = NORTH + EAST,
    SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST,
    NORTH_WEST = NORTH + WEST
};

using Depth = int16_t;

constexpr Depth DEPTH_ZERO       {  0 };
constexpr Depth DEPTH_QS_CHECK   {  0 };
constexpr Depth DEPTH_QS_NO_CHECK{ -1 };
constexpr Depth DEPTH_QS_RECAP   { -5 };
constexpr Depth DEPTH_NONE       { -6 };
constexpr Depth DEPTH_OFFSET     { DEPTH_NONE - 1 }; // Used only for TT entry occupancy check

// Maximum Depth
constexpr int32_t MAX_PLY{ 256 + DEPTH_OFFSET - 4 };

enum CastleSide {
    CS_KING, CS_QUEN, CS_CENTRE,
    CASTLE_SIDES = 2
};

/// Castle Right defined as in Polyglot book hash key
enum CastleRight {
    CR_NONE,

    CR_WKING,                  // 0001
    CR_WQUEN = CR_WKING << 1,  // 0010
    CR_BKING = CR_WKING << 2,  // 0100
    CR_BQUEN = CR_WKING << 3,  // 1000

    CR_WHITE = CR_WKING | CR_WQUEN, // 0011
    CR_BLACK = CR_BKING | CR_BQUEN, // 1100
    CR_KING  = CR_WKING | CR_BKING, // 0101
    CR_QUEN  = CR_WQUEN | CR_BQUEN, // 1010
    CR_ANY   = CR_WHITE | CR_BLACK, // 1111

    CASTLE_RIGHTS = 16
};

enum PieceType {
    NONE, PAWN, NIHT, BSHP, ROOK, QUEN, KING,
    PIECE_TYPES = 7,
    PIECE_TYPES_EX = PIECE_TYPES - 1 // Exclude King
};

/// Piece needs 4-bits to be stored
/// bit 0-2: Type of piece
/// bit   3: Color of piece { White = 0..., Black = 1... }
enum Piece {
    NO_PIECE,
    W_PAWN = PAWN + 0, W_NIHT, W_BSHP, W_ROOK, W_QUEN, W_KING,
    B_PAWN = PAWN + 8, B_NIHT, B_BSHP, B_ROOK, B_QUEN, B_KING,
    PIECES = 16
};

enum MoveType {
    SIMPLE    = 0 << 14, // [00]-- ===
    CASTLE    = 1 << 14, // [01]-- ===
    ENPASSANT = 2 << 14, // [10]-- ===
    PROMOTE   = 3 << 14, // [11]xx ===
};

/// Move needs 16-bits to be stored
///
/// bit 00-05: Destiny square
/// bit 06-11: Origin square
/// bit 12-13: Promotion piece
/// bit 14-15: Move Type
///
/// Special cases are MOVE_NONE and MOVE_NULL.
enum Move : int32_t {
    MOVE_NONE = 0x000,
    MOVE_NULL = 0x041,
};

enum Value : int32_t {
    VALUE_ZERO      = 0,
    VALUE_DRAW      = 0,

    VALUE_NONE      = 32002,
    VALUE_INFINITE  = VALUE_NONE - 1,
    VALUE_MATE      = VALUE_INFINITE - 1,

    VALUE_MATE_1_MAX_PLY = VALUE_MATE - 1 * MAX_PLY,
    VALUE_MATE_2_MAX_PLY = VALUE_MATE - 2 * MAX_PLY,

    VALUE_KNOWN_WIN = 10000,

    VALUE_MG_PAWN =  126, VALUE_EG_PAWN =  208,
    VALUE_MG_NIHT =  781, VALUE_EG_NIHT =  854,
    VALUE_MG_BSHP =  825, VALUE_EG_BSHP =  915,
    VALUE_MG_ROOK = 1276, VALUE_EG_ROOK = 1380,
    VALUE_MG_QUEN = 2538, VALUE_EG_QUEN = 2682,

    VALUE_MIDGAME = 15258,
    VALUE_ENDGAME =  3915,

    VALUE_TEMPO = 28,
};

/// Score needs 32-bits to be stored
/// the lower 16-bits are used to store the midgame value
/// the upper 16-bits are used to store the endgame value
/// Take some care to avoid left-shifting a signed int to avoid undefined behavior.
enum Score : int32_t {
    SCORE_ZERO = 0,
};

enum Bound {
    BOUND_NONE,
    BOUND_UPPER,
    BOUND_LOWER,
    BOUND_EXACT = BOUND_UPPER | BOUND_LOWER,
};

enum Phase {
    MG, EG,
    PHASES = 2
};

enum Scale {
    SCALE_DRAW    =   0,
    SCALE_NORMAL  =  64,
    SCALE_MAX     = 128,
    SCALE_NONE    = 255,
};

#define BASIC_OPERATORS(T)                                                      \
    constexpr T operator+(T t)        noexcept { return T(+int(t)); }           \
    constexpr T operator-(T t)        noexcept { return T(-int(t)); }           \
    constexpr T operator+(T t1, T t2) noexcept { return T(int(t1) + int(t2)); } \
    constexpr T operator-(T t1, T t2) noexcept { return T(int(t1) - int(t2)); } \
    inline T& operator+=(T &t1, T t2) noexcept { return t1 = t1 + t2; }         \
    inline T& operator-=(T &t1, T t2) noexcept { return t1 = t1 - t2; }

BASIC_OPERATORS(Direction)
BASIC_OPERATORS(Value)
BASIC_OPERATORS(Score)
#undef BASIC_OPERATORS

#define ARTHMAT_OPERATORS(T)                                             \
    constexpr T operator+(T t, int i) noexcept { return T(int(t) + i); } \
    constexpr T operator-(T t, int i) noexcept { return T(int(t) - i); } \
    constexpr T operator*(T t, int i) noexcept { return T(int(t) * i); } \
    constexpr T operator*(int i, T t) noexcept { return T(int(t) * i); } \
    constexpr T operator/(T t, int i) noexcept { return T(int(t) / i); } \
    inline T& operator+=(T &t, int i) noexcept { return t = t + i; }     \
    inline T& operator-=(T &t, int i) noexcept { return t = t - i; }     \
    inline T& operator*=(T &t, int i) noexcept { return t = t * i; }     \
    inline T& operator/=(T &t, int i) noexcept { return t = t / i; }

ARTHMAT_OPERATORS(File)
ARTHMAT_OPERATORS(Direction)
ARTHMAT_OPERATORS(Value)
#undef ARTHMAT_OPERATORS

#define INC_DEC_OPERATORS(T)                                          \
    inline T& operator++(T &t) noexcept { return t = T(int(t) + 1); } \
    inline T& operator--(T &t) noexcept { return t = T(int(t) - 1); }

INC_DEC_OPERATORS(File)
INC_DEC_OPERATORS(Rank)
INC_DEC_OPERATORS(Square)
INC_DEC_OPERATORS(PieceType)
INC_DEC_OPERATORS(Piece)
INC_DEC_OPERATORS(CastleSide)
#undef INC_DEC_OPERATORS

#define BITWISE_OPERATORS(T)                                                    \
    constexpr T operator~(T t)        noexcept { return T(~int(t)); }           \
    constexpr T operator|(T t1, T t2) noexcept { return T(int(t1) | int(t2)); } \
    constexpr T operator&(T t1, T t2) noexcept { return T(int(t1) & int(t2)); } \
    constexpr T operator^(T t1, T t2) noexcept { return T(int(t1) ^ int(t2)); } \
    inline T& operator|=(T &t1, T t2) noexcept { return t1 = t1 | t2; }         \
    inline T& operator&=(T &t1, T t2) noexcept { return t1 = t1 & t2; }         \
    inline T& operator^=(T &t1, T t2) noexcept { return t1 = t1 ^ t2; }

BITWISE_OPERATORS(CastleRight)
//BITWISE_OPERATORS(Bound)
#undef BITWISE_OPERATORS

constexpr Square operator+(Square s, Direction d) noexcept { return Square(int(s) + int(d)); }
constexpr Square operator-(Square s, Direction d) noexcept { return Square(int(s) - int(d)); }
inline Square& operator+=(Square &s, Direction d) noexcept { return s = s + d; }
inline Square& operator-=(Square &s, Direction d) noexcept { return s = s - d; }

constexpr Direction operator-(Square s1, Square s2) noexcept { return Direction(int(s1) - int(s2)); }

constexpr Score makeScore(int32_t mg, int32_t eg) noexcept {
    return Score(int32_t(uint32_t(eg) << 0x10) + mg);
}

/// Extracting the signed lower and upper 16 bits is not so trivial
/// because according to the standard a simple cast to short is implementation
/// defined and so is a right shift of a signed integer.
union Union16{ uint16_t u; int16_t s; };
constexpr Value mgValue(uint32_t s) {
    return Value(Union16{ uint16_t(uint32_t(s + 0x0000) >> 0x00) }.s);
}
constexpr Value egValue(uint32_t s) {
    return Value(Union16{ uint16_t(uint32_t(s + 0x8000) >> 0x10) }.s);
}

/// Division of a Score must be handled separately for each term
constexpr Score operator/(Score s, int i) {
    return makeScore(mgValue(s) / i, egValue(s) / i);
}
/// Multiplication of a Score by an integer. We check for overflow in debug mode.
//inline Score operator*(Score s, int i) {
//    Score score{ Score(int(s) * i) };
//    assert(egValue(score) == (egValue(s) * i));
//    assert(mgValue(score) == (mgValue(s) * i));
//    assert((i == 0) || (score / i) == s);
//    return score;
//}
constexpr Score operator*(Score s, int i) { return Score(int(s) * i); }

inline Score& operator/=(Score &s, int i) { return s = s / i; }
inline Score& operator*=(Score &s, int i) { return s = s * i; }
/// Multiplication of a Score by a boolean
constexpr Score operator*(Score s, bool b)    { return b ? s : SCORE_ZERO; }

/// Don't want to multiply two scores due to a very high risk of overflow.
/// So user should explicitly convert to integer.
Score operator*(Score, Score) = delete;
Score operator/(Score, Score) = delete;

constexpr bool isOk(Color c) noexcept {
    return WHITE <= c && c <= BLACK;
}
constexpr Color operator~(Color c) noexcept {
    return Color(BLACK - c);
}

constexpr bool isOk(File f) noexcept {
    return FILE_A <= f && f <= FILE_H;
}
constexpr File operator~(File f) noexcept {
    return File(FILE_H - f);
}

constexpr bool isOk(Rank r) noexcept {
    return RANK_1 <= r && r <= RANK_8;
}
constexpr Rank operator~(Rank r) noexcept {
    return Rank(RANK_8 - r);
}

constexpr int32_t BaseRank[COLORS]{
    RANK_1, RANK_8
};
constexpr Rank relativeRank(Color c, Rank r) {
    return Rank(r ^ BaseRank[c]);
}

constexpr bool isOk(Square s) noexcept {
    return SQ_A1 <= s && s <= SQ_H8;
}
constexpr Square makeSquare(File f, Rank r) noexcept {
    return Square((r << 3) + f);
}
constexpr File sFile(Square s) noexcept {
    return File(int(s) & 7);
}
constexpr Rank sRank(Square s) noexcept {
    return Rank(s >> 3);
}
constexpr Color sColor(Square s) noexcept {
    return Color(((s + sRank(s)) ^ 1) & 1);
}

template<typename T> constexpr Square flip(Square) noexcept;
// Flip File: SQ_H1 -> SQ_A1
template<> constexpr Square flip<File>(Square s) noexcept {
    return Square(int(s) ^ 0x07);
}
// Flip Rank: SQ_A8 -> SQ_A1
template<> constexpr Square flip<Rank>(Square s) noexcept {
    return Square(int(s) ^ 0x38);
}


constexpr bool colorOpposed(Square s1, Square s2) noexcept {
    return (s1 + sRank(s1) + s2 + sRank(s2)) & 1; //sColor(s1) != sColor(s2);
}

constexpr int32_t BaseSquare[COLORS]{
    SQ_A1, SQ_A8
};
constexpr Square relativeSq(Color c, Square s) noexcept {
    return Square(int(s) ^ BaseSquare[c]);
}
constexpr Rank relativeRank(Color c, Square s) noexcept {
    return relativeRank(c, sRank(s));
}

constexpr Square kingCastleSq(Square org, Square dst) {
    return makeSquare(FILE_E + 2 * sign(dst - org), sRank(org));
}
constexpr Square rookCastleSq(Square org, Square dst) {
    return makeSquare(FILE_E + 1 * sign(dst - org), sRank(org));
}

constexpr bool isOk(PieceType pt) noexcept {
    return PAWN <= pt && pt <= KING;
}

constexpr bool isOk(Piece p) noexcept {
    return (W_PAWN <= p && p <= W_KING)
        || (B_PAWN <= p && p <= B_KING);
}
// makePiece()
constexpr Piece operator|(Color c, PieceType pt) noexcept {
    return Piece((c << 3) + pt);
}

constexpr PieceType pType(Piece p) noexcept {
    return PieceType(p & 7);
}
constexpr Color pColor(Piece p) noexcept {
    return Color(p >> 3);
}

constexpr Piece flipColor(Piece p) noexcept {
    return Piece(p ^ (BLACK << 3));
}

constexpr CastleRight makeCastleRight(Color c) {
    return CastleRight(CR_WHITE <<  (c << 1));
}
constexpr CastleRight makeCastleRight(Color c, CastleSide cs) {
    return CastleRight(CR_WKING << ((c << 1) + cs));
}

constexpr Square orgSq(Move m) noexcept {
    return Square((m >> 6) & 63);
}
constexpr Square dstSq(Move m) noexcept {
    return Square((m >> 0) & 63);
}
constexpr bool isOk(Move m) noexcept {
    return orgSq(m) != dstSq(m);
}
constexpr PieceType promoteType(Move m) noexcept {
    return PieceType(((m >> 12) & 3) + NIHT);
}
constexpr MoveType mType(Move m) noexcept {
    return MoveType(m & PROMOTE);
}
constexpr uint16_t mMask(Move m) noexcept {
    return uint16_t(m & 0x0FFF);
}

template<MoveType MT>
constexpr Move makeMove(Square org, Square dst) {
    return Move(MT + (org << 6) + dst);
}
constexpr Move makePromoteMove(Square org, Square dst, PieceType pt = QUEN) {
    return Move(PROMOTE + ((pt - NIHT) << 12) + (org << 6) + dst);
}
template<>
constexpr Move makeMove<PROMOTE>(Square org, Square dst) {
    return makePromoteMove(org, dst);
}

constexpr Move makeMove(Square org, Square dst) {
    return Move((org << 6) + dst);
}
constexpr Move reverseMove(Move m) {
    return makeMove(dstSq(m), orgSq(m));
}

/// Convert Value to Centipawn
constexpr double toCP(Value v) {
    return double(100 * v) / VALUE_EG_PAWN;
}
/// Convert Centipawn to Value
constexpr Value toValue(double cp) {
    return Value(int32_t(cp) * VALUE_EG_PAWN / 100);
}

constexpr Value matesIn(int32_t ply) {
    return +VALUE_MATE - ply;
}
constexpr Value matedIn(int32_t ply) {
    return -VALUE_MATE + ply;
}

class Moves :
    public std::vector<Move> {

public:
    using std::vector<Move>::vector;

    bool contains(Move move) const {
        return std::find(begin(), end(), move) != end();
    }

    void operator+=(Move move) { push_back(move); }
    void operator-=(Move move) { erase(std::find(begin(), end(), move)); }

};

struct ValMove {

    ValMove() noexcept :
        ValMove{ MOVE_NONE } {
    }
    explicit ValMove(Move m, int32_t v = 0) noexcept :
        move{ m },
        value{ v } {
    }

    bool operator<(ValMove const &vm) const noexcept {
        return value < vm.value;
    }
    bool operator>(ValMove const &vm) const noexcept {
        return value > vm.value;
    }

    operator Move() const noexcept { return move; }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const = delete;
    operator double() const = delete;

    void operator=(Move m) noexcept { move = m; }

    //std::string toString() const noexcept {
    //    return std::to_string(value);
    //}

    Move    move;
    int32_t value;
};

class ValMoves :
    public std::vector<ValMove> {

public:
    using std::vector<ValMove>::vector;

    void operator+=(Move move) noexcept { emplace_back(move); }
    //void operator-=(Move move) noexcept { erase(std::find(begin(), end(), move)); }

    bool contains(Move move) const noexcept {
        return std::find(begin(), end(), move) != end();
    }
};

using TimePoint = std::chrono::milliseconds::rep; // Time in milli-seconds
static_assert(sizeof(TimePoint) == sizeof(int64_t), "TimePoint should be 64 bits");

inline TimePoint now() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>
          (std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Hash table
template<typename T, size_t Size>
class HashTable {

public:

    void clear() {
        table.assign(Size, T{});
    }

    T* operator[](Key key) {
        return &table[uint32_t(key) & (Size - 1)];
    }

private:

    std::vector<T> table = std::vector<T>(Size); // Allocate on the heap
};

constexpr Piece Pieces[2 * PIECE_TYPES_EX]{
    W_PAWN, W_NIHT, W_BSHP, W_ROOK, W_QUEN, W_KING,
    B_PAWN, B_NIHT, B_BSHP, B_ROOK, B_QUEN, B_KING
};

constexpr Value PieceValues[PHASES][PIECE_TYPES]{
    { VALUE_ZERO, VALUE_MG_PAWN, VALUE_MG_NIHT, VALUE_MG_BSHP, VALUE_MG_ROOK, VALUE_MG_QUEN, VALUE_ZERO },
    { VALUE_ZERO, VALUE_EG_PAWN, VALUE_EG_NIHT, VALUE_EG_BSHP, VALUE_EG_ROOK, VALUE_EG_QUEN, VALUE_ZERO }
};

#include "tune.h" // Global visibility to tuning setup
