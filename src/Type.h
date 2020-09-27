#pragma once

/// Compiling:
/// With Makefile (e.g. for Linux and OSX), configuration is done automatically.
/// To get started type 'make help'.
/// Without Makefile (e.g. with Microsoft Visual Studio) some switches need to be set manually:
///
/// -DNDEBUG    | Disable debugging mode. Always use this for release.
/// -DPREFETCH  | Enable use of prefetch asm-instruction.
///             | Don't enable it if want to run on some very old machines.
/// -DABMI      | Add runtime support for use of USE_POPCNT asm-instruction.
///             | Works only in 64-bit mode and requires hardware with USE_POPCNT support.
/// -DBMI2      | Add runtime support for use of USE_PEXT asm-instruction.
///             | Works only in 64-bit mode and requires hardware with USE_PEXT support.

#include <cassert>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdlib>

#include <algorithm>
#include <chrono>
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
    #pragma warning (disable: 4127) // Conditional expression is constant
    #pragma warning (disable: 4146) // Unary minus operator applied to unsigned type
    #pragma warning (disable: 4800) // Forcing value to bool 'true' or 'false'

    #if defined(_WIN64)
        #if !defined(IS_64BIT)
            #define IS_64BIT
        #endif
    #endif

    #define S32(X) (X ##  i32)
    #define U32(X) (X ## ui32)
    #define S64(X) (X ##  i64)
    #define U64(X) (X ## ui64)
#else
    #define S32(X) (X ##   L)
    #define U32(X) (X ##  UL)
    #define S64(X) (X ##  LL)
    #define U64(X) (X ## ULL)
#endif

// When no Makefile used

#if defined(_WIN64) && defined(_MSC_VER)
    #include <intrin.h>     // Microsoft Header for _BitScanForward64() & _BitScanReverse64()
#endif

#if defined(USE_POPCNT) && (defined(__INTEL_COMPILER) || defined(_MSC_VER))
    #include <nmmintrin.h>  // Intel and Microsoft Header for _mm_popcnt_u64()
#endif

#if defined(USE_PREFETCH) && (defined(__INTEL_COMPILER) || defined(_MSC_VER))
    #include <xmmintrin.h>  // Intel and Microsoft header for _mm_prefetch()
#endif

#if defined(USE_PEXT)
    #include <immintrin.h>  // Header for _pdep_u64() & _pext_u64() intrinsic
  //#define PDEP(b, m)  _pdep_u64(b, m) // Parallel bits deposit
    #define PEXT(b, m)  _pext_u64(b, m) // Parallel bits extract
#endif

#define XSTRING(x)      #x
#define STRINGIFY(x)    XSTRING(x)

using i08  =  int8_t;
using u08  = uint8_t;
using i16  =  int16_t;
using u16  = uint16_t;
using i32  =  int32_t;
using u32  = uint32_t;
using i64  =  int64_t;
using u64  = uint64_t;
using uPtr = uintptr_t;

using Key = u64;
using Bitboard = u64;

constexpr u32 nSqr(i16 n) {
    return u32(n) * n;
}
constexpr u64 nSqr(i32 n) {
    return u64(n) * n;
}

// Return the sign of a number (-1, 0, 1)
template<typename T>
constexpr i32 sign(T const &v) {
    //return (T{} < v) - (v < T{});
    return (0 < v) - (v < 0);
}

enum Color : i08 {
    WHITE, BLACK,
    COLORS = 2
};

enum File : i08 {
    FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H,
    FILES = 8
};

enum Rank : i08 {
    RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8,
    RANKS = 8
};

/// Square needs 6-bits to be stored
/// bit 0-2: File
/// bit 3-5: Rank
enum Square : i08 {
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

enum Direction : i08 {
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

using Depth = i16;

constexpr Depth DEPTH_ZERO       {  0 };
constexpr Depth DEPTH_QS_CHECK   {  0 };
constexpr Depth DEPTH_QS_NO_CHECK{ -1 };
constexpr Depth DEPTH_QS_RECAP   { -5 };
constexpr Depth DEPTH_NONE       { -6 };
constexpr Depth DEPTH_OFFSET     { DEPTH_NONE - 1 }; // Used only for TT entry occupancy check

// Maximum Depth
constexpr i32 MAX_PLY{ 256 + DEPTH_OFFSET - 4 };

enum CastleSide : i08 {
    CS_KING, CS_QUEN, CS_CENTRE, CASTLE_SIDES = 2
};

/// Castle Right defined as in Polyglot book hash key
enum CastleRight : u08 {
    CR_NONE  = 0,       // 0000

    CR_WKING = 1 << 0,  // 0001
    CR_WQUEN = 1 << 1,  // 0010
    CR_BKING = 1 << 2,  // 0100
    CR_BQUEN = 1 << 3,  // 1000

    CR_WHITE = CR_WKING | CR_WQUEN, // 0011
    CR_BLACK = CR_BKING | CR_BQUEN, // 1100
    CR_KING  = CR_WKING | CR_BKING, // 0101
    CR_QUEN  = CR_WQUEN | CR_BQUEN, // 1010
    CR_ANY   = CR_WHITE | CR_BLACK, // 1111

    CASTLE_RIGHTS = 16
};

enum PieceType : i08 {
    NONE, PAWN, NIHT, BSHP, ROOK, QUEN, KING,
    PIECE_TYPES = 7
};

/// Piece needs 4-bits to be stored
/// bit 0-2: Type of piece
/// bit   3: Color of piece { White = 0..., Black = 1... }
enum Piece : u08 {
    NO_PIECE,
    W_PAWN = 1, W_NIHT, W_BSHP, W_ROOK, W_QUEN, W_KING,
    B_PAWN = 9, B_NIHT, B_BSHP, B_ROOK, B_QUEN, B_KING,
    PIECES = 16
};

enum MoveType : u16 {
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
enum Move : u16 {
    MOVE_NONE = 0x000,
    MOVE_NULL = 0x041,
};

enum Value : i32 {
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
enum Score : u32 {
    SCORE_ZERO = 0,
};

enum Bound : u08 {
    BOUND_NONE,
    BOUND_UPPER = 1 << 0,
    BOUND_LOWER = 1 << 1,
    BOUND_EXACT = BOUND_UPPER | BOUND_LOWER,
};

enum Phase : u08 {
    MG,
    EG,
    PHASES = 2
};

enum Scale : u08 {
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

BASIC_OPERATORS(Direction)
BASIC_OPERATORS(Value)
BASIC_OPERATORS(Score)
#undef BASIC_OPERATORS

#define ARTHMAT_OPERATORS(T)                                             \
    constexpr T operator+(T t, i32 i) noexcept { return T(i32(t) + i); } \
    constexpr T operator-(T t, i32 i) noexcept { return T(i32(t) - i); } \
    constexpr T operator*(T t, i32 i) noexcept { return T(i32(t) * i); } \
    constexpr T operator*(i32 i, T t) noexcept { return T(i32(t) * i); } \
    constexpr T operator/(T t, i32 i) noexcept { return T(i32(t) / i); } \
    inline T& operator+=(T &t, i32 i) noexcept { return t = t + i; }     \
    inline T& operator-=(T &t, i32 i) noexcept { return t = t - i; }     \
    inline T& operator*=(T &t, i32 i) noexcept { return t = t * i; }     \
    inline T& operator/=(T &t, i32 i) noexcept { return t = t / i; }

ARTHMAT_OPERATORS(File)
ARTHMAT_OPERATORS(Direction)
ARTHMAT_OPERATORS(Value)
#undef ARTHMAT_OPERATORS

#define INC_DEC_OPERATORS(T)                                          \
    inline T& operator++(T &t) noexcept { return t = T(i32(t) + 1); } \
    inline T& operator--(T &t) noexcept { return t = T(i32(t) - 1); }

INC_DEC_OPERATORS(File)
INC_DEC_OPERATORS(Rank)
INC_DEC_OPERATORS(Square)
INC_DEC_OPERATORS(PieceType)
INC_DEC_OPERATORS(Piece)
INC_DEC_OPERATORS(CastleSide)
#undef INC_DEC_OPERATORS

#define BITWISE_OPERATORS(T)                                           \
    constexpr T operator~(T t) { return T(~i32(t)); }                  \
    constexpr T operator|(T t1, T t2) { return T(i32(t1) | i32(t2)); } \
    constexpr T operator&(T t1, T t2) { return T(i32(t1) & i32(t2)); } \
    constexpr T operator^(T t1, T t2) { return T(i32(t1) ^ i32(t2)); } \
    inline T& operator|=(T &t1, T t2) { return t1 = t1 | t2; }         \
    inline T& operator&=(T &t1, T t2) { return t1 = t1 & t2; }         \
    inline T& operator^=(T &t1, T t2) { return t1 = t1 ^ t2; }

BITWISE_OPERATORS(CastleRight)
//BITWISE_OPERATORS(Bound)
#undef BITWISE_OPERATORS

constexpr Square operator+(Square s, Direction d) {
    return Square(i32(s) + i32(d));
}
constexpr Square operator-(Square s, Direction d) {
    return Square(i32(s) - i32(d));
}
inline Square& operator+=(Square &s, Direction d) {
    return s = s + d;
}
inline Square& operator-=(Square &s, Direction d) {
    return s = s - d;
}

constexpr Direction operator-(Square s1, Square s2) {
    return Direction(i32(s1) - i32(s2));
}

constexpr Score makeScore(i32 mg, i32 eg) {
    return Score(i32(u32(eg) << 0x10) + mg);
}

// Keep track of what a move changes on the board (used by NNUE)
struct DirtyPiece {

    // Count of changed pieces
    int dirtyCount;

    // Max 3 pieces can change in one move.
    // A promotion with capture moves both
    // the pawn and the captured piece to SQ_NONE
    // and the piece promoted to from SQ_NONE to the capture square.
    Piece piece[3];

    // org and dst squares, which may be SQ_NONE
    Square org[3];
    Square dst[3];
};

/// Extracting the signed lower and upper 16 bits is not so trivial
/// because according to the standard a simple cast to short is implementation
/// defined and so is a right shift of a signed integer.
union Union16 { u16 u; i16 s; };
constexpr Value mgValue(u32 s) {
    return Value(Union16{ u16(u32(s + 0x0000) >> 0x00) }.s);
}
constexpr Value egValue(u32 s) {
    return Value(Union16{ u16(u32(s + 0x8000) >> 0x10) }.s);
}

/// Division of a Score must be handled separately for each term
constexpr Score operator/(Score s, i32 i) {
    return makeScore(mgValue(s) / i, egValue(s) / i);
}
/// Multiplication of a Score by an integer. We check for overflow in debug mode.
//inline Score operator*(Score s, i32 i) {
//    Score score{ Score(i32(s) * i) };
//    assert(egValue(score) == (egValue(s) * i));
//    assert(mgValue(score) == (mgValue(s) * i));
//    assert((i == 0) || (score / i) == s);
//    return score;
//}
constexpr Score operator*(Score s, i32 i) {
    return Score(i32(s) * i);
}

inline Score& operator/=(Score &s, i32 i) {
    return s = s / i;
}
inline Score& operator*=(Score &s, i32 i) {
    return s = s * i;
}
/// Multiplication of a Score by a boolean
constexpr Score operator*(Score s, bool b) {
    return s * i32(b);
}
/// Don't want to multiply two scores due to a very high risk of overflow.
/// So user should explicitly convert to integer.
Score operator*(Score, Score) = delete;
Score operator/(Score, Score) = delete;

constexpr bool isOk(Color c) {
    return WHITE <= c && c <= BLACK;
}
constexpr Color operator~(Color c) {
    return Color(BLACK - c);
}

constexpr bool isOk(File f) {
    return FILE_A <= f && f <= FILE_H;
}
constexpr File operator~(File f) {
    return File(FILE_H - f);
}

constexpr bool isOk(Rank r) {
    return RANK_1 <= r && r <= RANK_8;
}
constexpr Rank operator~(Rank r) {
    return Rank(RANK_8 - r);
}

constexpr i32 BaseRank[COLORS]{
    RANK_1, RANK_8
};
constexpr Rank relativeRank(Color c, Rank r) {
    return Rank(r ^ BaseRank[c]);
}

constexpr bool isOk(Square s) {
    return SQ_A1 <= s && s <= SQ_H8;
}
constexpr Square makeSquare(File f, Rank r) {
    return Square((r << 3) + f);
}
constexpr File sFile(Square s) {
    return File(i32(s) & 7);
}
constexpr Rank sRank(Square s) {
    return Rank(s >> 3);
}
constexpr Color sColor(Square s) {
    return Color(((s + sRank(s)) ^ 1) & 1);
}

template<typename T> constexpr Square flip(Square);
// Flip File: SQ_H1 -> SQ_A1
template<> constexpr Square flip<File>(Square s) {
    return Square(i32(s) ^ 0x07);
}
// Flip Rank: SQ_A8 -> SQ_A1
template<> constexpr Square flip<Rank>(Square s) {
    return Square(i32(s) ^ 0x38);
}


constexpr bool colorOpposed(Square s1, Square s2) {
    return (s1 + sRank(s1) + s2 + sRank(s2)) & 1; //sColor(s1) != sColor(s2);
}

constexpr i32 BaseSquare[COLORS]{
    SQ_A1, SQ_A8
};
constexpr Square relativeSq(Color c, Square s) {
    return Square(i32(s) ^ BaseSquare[c]);
}
constexpr Rank relativeRank(Color c, Square s) {
    return relativeRank(c, sRank(s));
}

constexpr Square kingCastleSq(Square org, Square dst) {
    return makeSquare(FILE_E + 2 * sign(dst - org), sRank(org));
}
constexpr Square rookCastleSq(Square org, Square dst) {
    return makeSquare(FILE_E + 1 * sign(dst - org), sRank(org));
}

constexpr bool isOk(PieceType pt) {
    return PAWN <= pt && pt <= KING;
}

constexpr bool isOk(Piece p) {
    return (W_PAWN <= p && p <= W_KING)
        || (B_PAWN <= p && p <= B_KING);
}
// makePiece()
constexpr Piece operator|(Color c, PieceType pt) {
    return Piece((c << 3) + pt);
}

constexpr PieceType pType(Piece p) {
    return PieceType(p & 7);
}
constexpr Color pColor(Piece p) {
    return Color(p >> 3);
}

constexpr Piece flipColor(Piece p) {
    return Piece(p ^ (BLACK << 3));
}

constexpr CastleRight makeCastleRight(Color c) {
    return CastleRight(CR_WHITE <<  (c << 1));
}
constexpr CastleRight makeCastleRight(Color c, CastleSide cs) {
    return CastleRight(CR_WKING << ((c << 1) + cs));
}

constexpr Square orgSq(Move m) {
    return Square((m >> 6) & 63);
}
constexpr Square dstSq(Move m) {
    return Square((m >> 0) & 63);
}
constexpr bool isOk(Move m) {
    return orgSq(m) != dstSq(m);
}
constexpr PieceType promoteType(Move m) {
    return PieceType(((m >> 12) & 3) + NIHT);
}
constexpr MoveType mType(Move m) {
    return MoveType(m & PROMOTE);
}
constexpr u16 mMask(Move m) {
    return u16(m & 0x0FFF);
}

template<MoveType MT>
constexpr Move makeMove(Square org, Square dst) {
    return Move(MT + (org << 6) + (dst << 0));
}

constexpr Move makePromoteMove(Square org, Square dst, PieceType pt = QUEN) {
    return Move(PROMOTE + ((pt - NIHT) << 12) + (org << 6) + (dst << 0));
}
template<>
constexpr Move makeMove<PROMOTE>(Square org, Square dst) {
    return makePromoteMove(org, dst);
}

constexpr Move reverseMove(Move m) {
    return makeMove<SIMPLE>(dstSq(m), orgSq(m));
}

/// Convert Value to Centipawn
constexpr double toCP(Value v) {
    return double(v) / VALUE_EG_PAWN * 100;
}
/// Convert Centipawn to Value
constexpr Value toValue(double cp) {
    return Value(i32(cp) / 100 * VALUE_EG_PAWN);
}

constexpr Value matesIn(i32 ply) {
    return +VALUE_MATE - ply;
}
constexpr Value matedIn(i32 ply) {
    return -VALUE_MATE + ply;
}

/// Based on a congruential pseudo random number generator
constexpr Key makeKey(u64 seed) {
    return( seed * U64(6364136223846793005) + U64(1442695040888963407) );
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

    Move move{ MOVE_NONE };
    i32  value{ 0 };

    ValMove() noexcept = default;
    explicit ValMove(Move m) noexcept :
        move{ m }
    {}

    operator Move() const noexcept { return move; }
    void operator=(Move m) noexcept { move = m; }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const = delete;
    operator double() const = delete;

    bool operator<(ValMove const &vm) const noexcept {
        return value < vm.value;
    }
    bool operator>(ValMove const &vm) const noexcept {
        return value > vm.value;
    }
    //bool operator<=(ValMove const &vm) const noexcept {
    //    return value <= vm.value;
    //}
    //bool operator>=(ValMove const &vm) const noexcept {
    //    return value >= vm.value;
    //}
};

class ValMoves :
    public std::vector<ValMove> {

public:
    using std::vector<ValMove>::vector;

    void operator+=(Move move) { emplace_back(move); }
    //void operator-=(Move move) { erase(std::find(begin(), end(), move)); }

    bool contains(Move move) const {
        return std::find(begin(), end(), move) != end();
    }
    //bool contains(ValMove const &vm) const {
    //    return std::find(begin(), end(), vm) != end();
    //}
};

using TimePoint = std::chrono::milliseconds::rep; // Time in milli-seconds
static_assert (sizeof (TimePoint) == sizeof (i64), "TimePoint should be 64 bits");

inline TimePoint now() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>
          (std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Hash table
template<typename T, size_t Size>
class HashTable {

public:
    HashTable() = default;

    void clear() {
        table.assign(Size, T{});
    }

    T* operator[](Key key) {
        return &table[u32(key) & (Size - 1)];
    }

private:
    std::vector<T> table = std::vector<T>(Size); // Allocate on the heap
};

constexpr Piece Pieces[2*KING]{
    W_PAWN, W_NIHT, W_BSHP, W_ROOK, W_QUEN, W_KING,
    B_PAWN, B_NIHT, B_BSHP, B_ROOK, B_QUEN, B_KING
};

constexpr Value PieceValues[PHASES][PIECE_TYPES]{
    { VALUE_ZERO, VALUE_MG_PAWN, VALUE_MG_NIHT, VALUE_MG_BSHP, VALUE_MG_ROOK, VALUE_MG_QUEN, VALUE_ZERO },
    { VALUE_ZERO, VALUE_EG_PAWN, VALUE_EG_NIHT, VALUE_EG_BSHP, VALUE_EG_ROOK, VALUE_EG_QUEN, VALUE_ZERO }
};
