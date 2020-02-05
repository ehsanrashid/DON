#pragma once

#include <array>
#include <deque>
#include <list>
#include <memory> // For std::unique_ptr
#include <string>

#include "BitBoard.h"
#include "Type.h"
#include "Util.h"

/// Pre-loads the given address in L1/L2 cache.
/// This is a non-blocking function that doesn't stall the CPU
/// waiting for data to be loaded from memory, which can be quite slow.
#if defined(PREFETCH)
#   if defined(_MSC_VER) || defined(__INTEL_COMPILER)

#   include <xmmintrin.h> // Intel and Microsoft header for _mm_prefetch()

inline void prefetch(const void *addr)
{
#   if defined(__INTEL_COMPILER)
    // This hack prevents prefetches from being optimized away by
    // Intel compiler. Both MSVC and gcc seem not be affected by this.
    __asm__ ("");
#   endif
    _mm_prefetch((const char*)(addr), _MM_HINT_T0);
}

#   else

inline void prefetch(const void *addr)
{
    __builtin_prefetch(addr);
}

#   endif

#else

inline void prefetch(const void*)
{}

#endif

using namespace BitBoard;

/// StateInfo stores information needed to restore a Position object to its previous state when we retract a move.
///
///  - Castling-rights information.
///  - Enpassant square(SQ_NO if no Enpassant capture is possible).
///  - Counter (clock) for detecting 50 move rule draws.
///  - Hash key of the material situation.
///  - Hash key of the pawn structure.
///  - Hash key of the position.
///  - Move played on the last position.
///  - Piece type captured on last position.
///  - Bitboard of all checking pieces.
///  - Pointer to previous StateInfo.
///  - Hash keys for all previous positions in the game for detecting repetition draws.
struct StateInfo
{
public:
    static const StateInfo Empty;

    // ---Copied when making a move---
    Key         matlKey;        // Hash key of materials
    Key         pawnKey;        // Hash key of pawns
    CastleRight castleRights;   // Castling-rights information
    Square      enpassantSq;    // Enpassant -> "In passing"
    u08         clockPly;       // Number of half moves clock since the last pawn advance or any capture
    u08         nullPly;

    // ---Not copied when making a move---
    Key         posiKey;        // Hash key of position
    PieceType   capture;        // Piece type captured
    PieceType   promote;        // Piece type promoted
    Bitboard    checkers;       // Checkers
    i16         repetition;
    // Check info
    std::array<Bitboard, CLR_NO> kingBlockers; // Absolute and Discover Blockers
    std::array<Bitboard, CLR_NO> kingCheckers; // Absolute and Discover Checkers
    std::array<Bitboard, NONE>   checks;

    StateInfo *ptr;             // Previous StateInfo pointer.

    bool canCastle(CastleRight cr) const { return CR_NONE != (castleRights & cr); }
    CastleRight castleRight(Color c) const { return castleRights & CastleRight(CR_WHITE << (2 * c)); }
};

/// A list to keep track of the position states along the setup moves
/// (from the start position to the position just before the search starts).
/// Needed by 'draw by repetition' detection.
/// Use a std::deque because pointers to elements are not invalidated upon list resizing.
typedef std::unique_ptr<std::deque<StateInfo>> StateListPtr;

class Thread;

/// Position class stores information regarding the board representation:
///  - 64-entry array of pieces, indexed by the square.
///  - Bitboards of each piece type.
///  - Bitboards of each color
///  - Bitboard of occupied square.
///  - List of square for the pieces.
///  - Information about the castling rights.
///  - Initial files of both pairs of rooks, castle path and kings path, this is used to implement the Chess960 castling rules.
///  - Color of side on move.
///  - Ply of the game.
///  - StateInfo pointer for the current status.
class Position
{
private:

    void placePiece(Square, Piece);
    void removePiece(Square);
    void movePiece(Square, Square);

    void setCastle(Color, Square);
    void setCheckInfo(StateInfo*);

    bool canEnpassant(Color, Square, bool = true) const;

public:

    std::array<Piece   , SQ_NO>  piece;
    std::array<Bitboard, CLR_NO> colors;
    std::array<Bitboard, PT_NO>  types;

    std::array<Value   , CLR_NO> npm;

    std::array<CastleRight, SQ_NO> castleRights;

    std::array<std::list<Square>, MAX_PIECE> squares;

    std::array<std::array<Square, CS_NO>  , CLR_NO> castleRookSq;
    std::array<std::array<Bitboard, CS_NO>, CLR_NO> castleKingPath;
    std::array<std::array<Bitboard, CS_NO>, CLR_NO> castleRookPath;

    Score   psq;
    i16     ply;
    Color   active;
    Thread  *thread;

    StateInfo *si; // Current state information pointer

    static void initialize();

    Position() = default;
    Position(const Position&) = delete;
    Position& operator=(const Position&) = delete;

    Piece operator[](Square) const;
    bool empty(Square)  const;

    Bitboard pieces() const;
    Bitboard pieces(Piece) const;
    Bitboard pieces(Color) const;
    Bitboard pieces(PieceType) const;
    template<typename ...PieceTypes>
    Bitboard pieces(PieceType, PieceTypes...) const;
    template<typename ...PieceTypes>
    Bitboard pieces(Color, PieceTypes...) const;

    Bitboard colorPawns(Color, Color) const;

    i32 count() const;
    i32 count(Piece) const;
    i32 count(Color) const;
    i32 count(PieceType) const;

    Square square(Piece, u08 = 0) const;

    Value nonpawnMaterial() const;
    Value nonpawnMaterial(Color) const;

    Key pgKey() const;
    Key movePosiKey(Move) const;

    bool castleExpeded(Color, CastleSide) const;

    i16  moveCount() const;
    bool draw(i16) const;
    bool repeated() const;
    bool cycled(i16) const;

    Bitboard attackersTo(Square, Bitboard) const;
    Bitboard attackersTo(Square) const;
    Bitboard attacksFrom(PieceType, Square, Bitboard) const;
    Bitboard attacksFrom(PieceType, Square) const;
    Bitboard attacksFrom(Square, Bitboard) const;
    Bitboard attacksFrom(Square) const;
    template<PieceType>
    Bitboard xattacksFrom(Square, Color) const;

    Bitboard sliderBlockersAt(Square, Color, Bitboard, Bitboard&, Bitboard&) const;

    bool pseudoLegal(Move) const;
    bool legal(Move) const;
    bool fullLegal(Move) const;
    bool capture(Move) const;
    bool captureOrPromotion(Move) const;
    bool givesCheck(Move) const;

    PieceType captureType(Move) const;

    bool see(Move, Value = VALUE_ZERO) const;

    bool pawnAdvanceAt(Color, Square) const;
    bool pawnPassedAt(Color, Square) const;

    bool bishopPaired(Color) const;
    bool semiopenFileOn(Color, Square) const;

    void clear();

    Position& setup(const std::string&, StateInfo&, Thread *const = nullptr);
    Position& setup(const std::string&, Color, StateInfo&);

    void doMove(Move, StateInfo&, bool);
    void doMove(Move, StateInfo&);
    void undoMove(Move);
    void doNullMove(StateInfo&);
    void undoNullMove();

    void flip();
    void mirror();

    std::string fen(bool full = true) const;

    explicit operator std::string() const;

#if !defined(NDEBUG)
    bool ok() const;
#endif

};

inline Piece Position::operator[](Square s) const
{
    assert(isOk(s));
    return piece[s];
}
inline bool Position::empty(Square s)  const
{
    assert(isOk(s));
    return NO_PIECE == piece[s];
}

inline Bitboard Position::pieces() const
{
    return types[NONE];
}
inline Bitboard Position::pieces(Piece pc) const
{
    return colors[colorOf(pc)] & types[typeOf(pc)];
}
inline Bitboard Position::pieces(Color c) const
{
    return colors[c];
}
inline Bitboard Position::pieces(PieceType pt) const
{
    assert(isOk(pt));
    return types[pt];
}
template<typename ...PieceTypes>
inline Bitboard Position::pieces(PieceType pt, PieceTypes... pts) const
{
    assert(isOk(pt));
    return types[pt] | pieces(pts...);
}
template<typename ...PieceTypes>
inline Bitboard Position::pieces(Color c, PieceTypes... pts) const
{
    return colors[c] & pieces(pts...);
}
/// Position::count() counts all
inline i32 Position::count() const
{
    return i32(squares[W_PAWN].size() + squares[B_PAWN].size()
             + squares[W_NIHT].size() + squares[B_NIHT].size()
             + squares[W_BSHP].size() + squares[B_BSHP].size()
             + squares[W_ROOK].size() + squares[B_ROOK].size()
             + squares[W_QUEN].size() + squares[B_QUEN].size()
             + squares[W_KING].size() + squares[B_KING].size());
}
inline i32 Position::count(Piece pc) const
{
    return i32(squares[pc].size());
}
/// Position::count() counts specific color
inline i32 Position::count(Color c) const
{
    return i32(squares[c|PAWN].size()
             + squares[c|NIHT].size()
             + squares[c|BSHP].size()
             + squares[c|ROOK].size()
             + squares[c|QUEN].size()
             + squares[c|KING].size());
}
/// Position::count() counts specific type
inline i32 Position::count(PieceType pt) const
{
    assert(isOk(pt));
    return i32(squares[WHITE|pt].size() + squares[BLACK|pt].size());
}

inline Bitboard Position::colorPawns(Color c, Color s) const
{
    return pieces(c, PAWN) & Colors[s];
}

inline Square Position::square(Piece pc, u08 index) const
{
    assert(isOk(pc));
    assert(squares[pc].size() > index);
    return *std::next(squares[pc].begin(), index);
}

inline Value Position::nonpawnMaterial() const
{
    return npm[WHITE]
         + npm[BLACK];
}
inline Value Position::nonpawnMaterial(Color c) const
{
    return npm[c];
}

inline bool Position::castleExpeded(Color c, CastleSide cs) const
{
    return 0 == (castleRookPath[c][cs] & pieces());
}
/// Position::moveCount() starts at 1, and is incremented after BLACK's move.
inline i16 Position::moveCount() const
{
    return i16(std::max((ply - (BLACK == active)) / 2, 0) + 1);
}

/// Position::attackersTo() finds attackers to the square on occupancy.
inline Bitboard Position::attackersTo(Square s, Bitboard occ) const
{
    return (pieces(BLACK, PAWN) & PawnAttacks[WHITE][s])
         | (pieces(WHITE, PAWN) & PawnAttacks[BLACK][s])
         | (pieces(NIHT)        & PieceAttacks[NIHT][s])
         | (pieces(BSHP, QUEN)  & attacksBB<BSHP>(s, occ))
         | (pieces(ROOK, QUEN)  & attacksBB<ROOK>(s, occ))
         | (pieces(KING)        & PieceAttacks[KING][s]);
}
/// Position::attackersTo() finds attackers to the square.
inline Bitboard Position::attackersTo(Square s) const
{
    return attackersTo(s, pieces());
}

/// Position::attacksFrom() finds attacks of the piecetype from the square on occupancy.
inline Bitboard Position::attacksFrom(PieceType pt, Square s, Bitboard occ) const
{
    assert(PAWN != pt);
    return BitBoard::attacksFrom(pt, s, occ);
}
/// Position::attacksFrom() finds attacks of the piecetype from the square.
inline Bitboard Position::attacksFrom(PieceType pt, Square s) const
{
    return attacksFrom(pt, s, pieces());
}
/// Position::attacksFrom() finds attacks from the square on occupancy.
inline Bitboard Position::attacksFrom(Square s, Bitboard occ) const
{
    return BitBoard::attacksFrom(piece[s], s, occ);
}
/// Position::attacksFrom() finds attacks from the square.
inline Bitboard Position::attacksFrom(Square s) const
{
    return attacksFrom(s, pieces());
}

/// Position::xattacksFrom() finds xattacks of the piecetype of the color from the square.

template<>
inline Bitboard Position::xattacksFrom<NIHT>(Square s, Color) const
{
    return PieceAttacks[NIHT][s];
}
template<>
inline Bitboard Position::xattacksFrom<BSHP>(Square s, Color c) const
{
    return attacksBB<BSHP>(s, pieces() ^ ((pieces(c, QUEN, BSHP) & ~si->kingBlockers[c]) | pieces(~c, QUEN)));
}
template<>
inline Bitboard Position::xattacksFrom<ROOK>(Square s, Color c) const
{
    return attacksBB<ROOK>(s, pieces() ^ ((pieces(c, QUEN, ROOK) & ~si->kingBlockers[c]) | pieces(~c, QUEN)));
}
template<>
inline Bitboard Position::xattacksFrom<QUEN>(Square s, Color c) const
{
    return attacksBB<QUEN>(s, pieces() ^ ((pieces(c, QUEN)       & ~si->kingBlockers[c])));
}

inline bool Position::capture(Move m) const
{
    return (   !empty(dstOf(m))
            && CASTLE != typeOf(m))
        || ENPASSANT == typeOf(m);
}

inline bool Position::captureOrPromotion(Move m) const
{
    return NORMAL != typeOf(m) ?
            CASTLE != typeOf(m) :
            !empty(dstOf(m));
}

inline PieceType Position::captureType(Move m) const
{
    return ENPASSANT != typeOf(m) ?
            typeOf(piece[dstOf(m)]) :
            PAWN;
}

inline bool Position::pawnAdvanceAt(Color c, Square s) const
{
    return contains(pieces(c, PAWN) & Regions[~c], s);
}
/// Position::pawnPassedAt() check if pawn passed at the given square.
inline bool Position::pawnPassedAt(Color c, Square s) const
{
    return 0 == (pawnPassSpan(c, s) & pieces(~c, PAWN));
}

/// Position::bishopPaired() check the side has pair of opposite color bishops.
inline bool Position::bishopPaired(Color c) const
{
    return 2 <= count(c|BSHP)
        && 0 != (pieces(c, BSHP) & Colors[WHITE])
        && 0 != (pieces(c, BSHP) & Colors[BLACK]);
}
inline bool Position::semiopenFileOn(Color c, Square s) const
{
    return 0 == (pieces(c, PAWN) & fileBB(s));
}

inline void Position::doMove(Move m, StateInfo &nsi)
{
    doMove(m, nsi, givesCheck(m));
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
operator<<(std::basic_ostream<Elem, Traits> &os, const Position &pos)
{
    os << std::string(pos);
    return os;
}

/// StateInfo::setCheckInfo() sets check info used for fast check detection.
inline void Position::setCheckInfo(StateInfo *si)
{
    si->kingCheckers[WHITE] = 0;
    si->kingCheckers[BLACK] = 0;
    si->kingBlockers[WHITE] = sliderBlockersAt(square(WHITE|KING), BLACK, 0, si->kingCheckers[WHITE], si->kingCheckers[BLACK]);
    si->kingBlockers[BLACK] = sliderBlockersAt(square(BLACK|KING), WHITE, 0, si->kingCheckers[BLACK], si->kingCheckers[WHITE]);
    assert(/*0 == (si->kingBlockers[WHITE] & pieces(BLACK, QUEN)) &&*/ (attacksBB<QUEN>(square(WHITE|KING), pieces()) & si->kingBlockers[WHITE]) == si->kingBlockers[WHITE]);
    assert(/*0 == (si->kingBlockers[BLACK] & pieces(WHITE, QUEN)) &&*/ (attacksBB<QUEN>(square(BLACK|KING), pieces()) & si->kingBlockers[BLACK]) == si->kingBlockers[BLACK]);

    si->checks[PAWN] = PawnAttacks[~active][square(~active|KING)];
    si->checks[NIHT] = PieceAttacks[NIHT][square(~active|KING)];
    si->checks[BSHP] = attacksBB<BSHP>(square(~active|KING), pieces());
    si->checks[ROOK] = attacksBB<ROOK>(square(~active|KING), pieces());
    si->checks[QUEN] = si->checks[BSHP]
                     | si->checks[ROOK];
    si->checks[KING] = 0;
}

#if !defined(NDEBUG)
/// isOk() Check the validity of FEN string.
inline bool isOk(const std::string &fen)
{
    Position pos;
    StateInfo si;
    return !white_spaces(fen)
        && pos.setup(fen, si, nullptr).ok();
}
#endif
