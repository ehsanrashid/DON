#pragma once

#include <deque>
#include <list>
#include <memory>
#include <string>

#include "Bitboard.h"
#include "Table.h"
#include "Type.h"

/// Pre-loads the given address in L1/L2 cache.
/// This is a non-blocking function that doesn't stall the CPU
/// waiting for data to be loaded from memory, which can be quite slow.
#if defined(PREFETCH)

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#   include <xmmintrin.h> // Microsoft and Intel Header for _mm_prefetch()
#endif

inline void prefetch(const void *addr) {
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#   if defined(__INTEL_COMPILER)
    // This hack prevents prefetches from being optimized away by
    // Intel compiler. Both MSVC and gcc seem not be affected by this.
    __asm__("");
#   endif
    _mm_prefetch((const char*) (addr), _MM_HINT_T0);
#else
    __builtin_prefetch(addr);
#endif
}

#else
inline void prefetch(const void*) {}
#endif // (PREFETCH)

constexpr Array<Piece, 12> Pieces
{
    W_PAWN, W_NIHT, W_BSHP, W_ROOK, W_QUEN, W_KING,
    B_PAWN, B_NIHT, B_BSHP, B_ROOK, B_QUEN, B_KING
};

constexpr Array<Value, 2, PIECE_TYPES> PieceValues
{{
    { VALUE_ZERO, VALUE_MG_PAWN, VALUE_MG_NIHT, VALUE_MG_BSHP, VALUE_MG_ROOK, VALUE_MG_QUEN, VALUE_ZERO },
    { VALUE_ZERO, VALUE_EG_PAWN, VALUE_EG_NIHT, VALUE_EG_BSHP, VALUE_EG_ROOK, VALUE_EG_QUEN, VALUE_ZERO }
}};

/// StateInfo stores information needed to restore a Position object to its previous state when we retract a move.
///
///  - Castling-rights information.
///  - Enpassant square(SQ_NONE if no Enpassant capture is possible).
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
    // ---Copied when making a move---
    Key         matlKey;        // Hash key of materials
    Key         pawnKey;        // Hash key of pawns
    CastleRight castleRights;   // Castling-rights information
    Square      epSquare;       // Enpassant -> "In passing"
    i16         clockPly;       // Number of half moves clock since the last pawn advance or any capture
    i16         nullPly;

    // ---Not copied when making a move---
    Key         posiKey;        // Hash key of position
    PieceType   captured;       // Piece type captured
    Bitboard    checkers;       // Checkers
    i16         repetition;
    // Check info
    Array<Bitboard, COLORS> kingBlockers; // Absolute and Discover Blockers
    Array<Bitboard, COLORS> kingCheckers; // Absolute and Discover Checkers
    Array<Bitboard, PIECE_TYPES> checks;

    StateInfo *ptr;             // Previous StateInfo pointer

    void clear();
};

/// A list to keep track of the position states along the setup moves
/// (from the start position to the position just before the search starts).
/// Needed by 'draw by repetition' detection.
/// Use a std::deque because pointers to elements are not invalidated upon list resizing.
using StateListPtr = std::unique_ptr<std::deque<StateInfo>>;

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

    Array<Piece   , SQUARES> board;
    Array<Bitboard, COLORS> colors;
    Array<Bitboard, PIECE_TYPES> types;
    Array<std::list<Square>, PIECES> pieceList;

    Array<Value   , COLORS> npMaterial;

    Table<Square  , COLORS, CASTLE_SIDES> cslRookSq;
    Table<Bitboard, COLORS, CASTLE_SIDES> cslKingPath;
    Table<Bitboard, COLORS, CASTLE_SIDES> cslRookPath;
    Array<CastleRight, SQUARES> sqCastleRight;

    Score psq;

    StateInfo *si;

    void placePiece(Square, Piece);
    void removePiece(Square);
    void movePiece(Square, Square);

    void setCastle(Color, Square);
    void setCheckInfo();

    bool canEnpassant(Color, Square, bool = true) const;

public:

    i16   ply;
    Color active;

    Thread *thread;

    //static void initialize();

    Position() = default;
    Position(const Position&) = delete;
    Position& operator=(const Position&) = delete;

    Piece operator[](Square) const;
    bool empty(Square) const;

    Bitboard pieces() const;
    //Bitboard pieces(Piece) const;
    Bitboard pieces(Color) const;
    Bitboard pieces(PieceType) const;
    template<typename ...PieceTypes>
    Bitboard pieces(PieceType, PieceTypes...) const;
    template<typename ...PieceTypes>
    Bitboard pieces(Color, PieceTypes...) const;

    i32 count() const;
    i32 count(Piece) const;
    i32 count(Color) const;
    i32 count(PieceType) const;
    const std::list<Square>& squares(Piece) const;
    Square square(Piece, u08 = 0) const;

    Value nonPawnMaterial(Color) const;
    Value nonPawnMaterial() const;

    Square   castleRookSq(Color, CastleSide) const;
    Bitboard castleKingPath(Color, CastleSide) const;
    Bitboard castleRookPath(Color, CastleSide) const;
    //CastleRight castleRight(Square) const;

    CastleRight castleRights() const;
    bool canCastle(Color) const;
    bool canCastle(Color, CastleSide) const;
    Square epSquare() const;

    i16 clockPly() const;
    i16 nullPly() const;

    Key matlKey() const;
    Key pawnKey() const;
    Key posiKey() const;
    PieceType captured() const;
    Bitboard checkers() const;
    i16 repetition() const;

    Bitboard kingBlockers(Color) const;
    Bitboard kingCheckers(Color) const;
    Bitboard checks(PieceType) const;

    Score psqScore() const;
    bool castleExpeded(Color, CastleSide) const;

    Key pgKey() const;
    Key movePosiKey(Move) const;

    i16  moveCount() const;
    bool draw(i16) const;
    bool repeated() const;
    bool cycled(i16) const;

    Bitboard attackersTo(Square, Bitboard) const;
    Bitboard attackersTo(Square) const;
    Bitboard attacksFrom(PieceType, Square) const;
    Bitboard attacksFrom(Square) const;
    template<PieceType>
    Bitboard xattacksFrom(Square, Color) const;

    Bitboard sliderBlockersAt(Square, Bitboard, Bitboard&, Bitboard&) const;

    bool pseudoLegal(Move) const;
    bool legal(Move) const;

    bool capture(Move) const;
    bool captureOrPromotion(Move) const;
    bool giveCheck(Move) const;

    PieceType captureType(Move) const;

    bool see(Move, Value = VALUE_ZERO) const;

    bool pawnAdvanceAt(Color, Square) const;
    bool pawnPassedAt(Color, Square) const;

    bool pairedBishop(Color) const;
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

    std::string toString() const;

#if !defined(NDEBUG)
    bool ok() const;
#endif

};

extern std::ostream& operator<<(std::ostream&, const Position&);


inline Piece Position::operator[](Square s) const {
    return board[s];
}
inline bool Position::empty(Square s) const {
    return NO_PIECE == board[s];
}

inline Bitboard Position::pieces() const {
    return types[NONE];
}
//inline Bitboard Position::pieces(Piece p) const { return colors[pColor(p)] & types[pType(p)]; }
inline Bitboard Position::pieces(Color c) const {
    return colors[c];
}
inline Bitboard Position::pieces(PieceType pt) const {
    return types[pt];
}
template<typename ...PieceTypes>
inline Bitboard Position::pieces(PieceType pt, PieceTypes... pts) const {
    return types[pt] | pieces(pts...);
}
template<typename ...PieceTypes>
inline Bitboard Position::pieces(Color c, PieceTypes... pts) const {
    return colors[c] & pieces(pts...);
}
/// Position::count() counts all
inline i32 Position::count() const {
    return i32(pieceList[W_PAWN].size() + pieceList[B_PAWN].size()
             + pieceList[W_NIHT].size() + pieceList[B_NIHT].size()
             + pieceList[W_BSHP].size() + pieceList[B_BSHP].size()
             + pieceList[W_ROOK].size() + pieceList[B_ROOK].size()
             + pieceList[W_QUEN].size() + pieceList[B_QUEN].size()
             + pieceList[W_KING].size() + pieceList[B_KING].size());
}
inline i32 Position::count(Piece p) const {
    return i32(pieceList[p].size());
}
/// Position::count() counts specific color
inline i32 Position::count(Color c) const {
    return i32(pieceList[c|PAWN].size()
             + pieceList[c|NIHT].size()
             + pieceList[c|BSHP].size()
             + pieceList[c|ROOK].size()
             + pieceList[c|QUEN].size()
             + pieceList[c|KING].size());
}
/// Position::count() counts specific type
inline i32 Position::count(PieceType pt) const {
    return i32(pieceList[WHITE|pt].size()
             + pieceList[BLACK|pt].size());
}

inline const std::list<Square>& Position::squares(Piece p) const {
    return pieceList[p];
}

//inline CastleRight Position::castleRight(Square s) const { return sqCastleRight[s]; }

inline Value Position::nonPawnMaterial(Color c) const {
    return npMaterial[c];
}
inline Value Position::nonPawnMaterial() const {
    return nonPawnMaterial(WHITE) + nonPawnMaterial(BLACK);
}

inline Square Position::castleRookSq(Color c, CastleSide cs) const {
    return cslRookSq[c][cs];
}
inline Bitboard Position::castleKingPath(Color c, CastleSide cs) const {
    return cslKingPath[c][cs];
}
inline Bitboard Position::castleRookPath(Color c, CastleSide cs) const {
    return cslRookPath[c][cs];
}

inline Square Position::square(Piece p, u08 index) const {
    assert(isOk(p));
    assert(pieceList[p].size() > index);
    return *std::next(pieceList[p].begin(), index);
}

inline CastleRight Position::castleRights() const {
    return si->castleRights;
}
inline bool Position::canCastle(Color c) const {
    return CR_NONE != (castleRights() & makeCastleRight(c));
}
inline bool Position::canCastle(Color c, CastleSide cs) const {
    return CR_NONE != (castleRights() & makeCastleRight(c, cs));
}
inline Square Position::epSquare() const {
    return si->epSquare;
}

inline i16 Position::clockPly() const {
    return si->clockPly;
}
inline i16 Position::nullPly() const {
    return si->nullPly;
}

inline Key Position::matlKey() const {
    return si->matlKey;
}
inline Key Position::pawnKey() const {
    return si->pawnKey;
}
inline Key Position::posiKey() const {
    return si->posiKey;
}
inline PieceType Position::captured() const {
    return si->captured;
}
inline Bitboard Position::checkers() const {
    return si->checkers;
}
inline i16 Position::repetition() const {
    return si->repetition;
}

inline Bitboard Position::kingBlockers(Color c) const {
    return si->kingBlockers[c];
}
inline Bitboard Position::kingCheckers(Color c) const {
    return si->kingCheckers[c];
}
inline Bitboard Position::checks(PieceType pt) const {
    return si->checks[pt];
}

inline Score Position::psqScore() const {
    return psq;
}

inline bool Position::castleExpeded(Color c, CastleSide cs) const {
    return 0 == (castleRookPath(c, cs) & pieces());
}
/// Position::moveCount() starts at 1, and is incremented after BLACK's move.
inline i16 Position::moveCount() const {
    return i16(std::max((ply - active) / 2, 0) + 1);
}

/// Position::attackersTo() finds attackers to the square on occupancy.
inline Bitboard Position::attackersTo(Square s, Bitboard occ) const {
    return (pieces(BLACK, PAWN) & PawnAttacks[WHITE][s])
         | (pieces(WHITE, PAWN) & PawnAttacks[BLACK][s])
         | (pieces(NIHT)        & PieceAttacks[NIHT][s])
         | (pieces(BSHP, QUEN)  & attacksBB<BSHP>(s, occ))
         | (pieces(ROOK, QUEN)  & attacksBB<ROOK>(s, occ))
         | (pieces(KING)        & PieceAttacks[KING][s]);
}
/// Position::attackersTo() finds attackers to the square.
inline Bitboard Position::attackersTo(Square s) const {
    return attackersTo(s, pieces());
}

/// Position::attacksFrom() finds attacks of the piecetype from the square
inline Bitboard Position::attacksFrom(PieceType pt, Square s) const {
    return attacksBB(pt, s, pieces());
}
/// Position::attacksFrom() finds attacks from the square
inline Bitboard Position::attacksFrom(Square s) const {
    return attacksBB(board[s], s, pieces());
}

/// Position::xattacksFrom() finds xattacks of the piecetype of the color from the square.
template<>
inline Bitboard Position::xattacksFrom<NIHT>(Square s, Color) const {
    return PieceAttacks[NIHT][s];
}
template<>
inline Bitboard Position::xattacksFrom<BSHP>(Square s, Color c) const {
    return attacksBB<BSHP>(s, pieces() ^ ((pieces(c, QUEN, BSHP) & ~kingBlockers(c)) | pieces(~c, QUEN)));
}
template<>
inline Bitboard Position::xattacksFrom<ROOK>(Square s, Color c) const {
    return attacksBB<ROOK>(s, pieces() ^ ((pieces(c, QUEN, ROOK) & ~kingBlockers(c)) | pieces(~c, QUEN)));
}
template<>
inline Bitboard Position::xattacksFrom<QUEN>(Square s, Color c) const {
    return attacksBB<QUEN>(s, pieces() ^ ((pieces(c, QUEN)       & ~kingBlockers(c))));
}
// template<>
// inline Bitboard Position::xattacksFrom<KING>(Square s, Color) const {
//    return PieceAttacks[KING][s];
//}
inline bool Position::capture(Move m) const {
    return ((NORMAL == mType(m) || PROMOTE == mType(m)) && !empty(dstSq(m)))
        || (ENPASSANT == mType(m) && dstSq(m) == epSquare());
}
inline bool Position::captureOrPromotion(Move m) const {
    return (NORMAL == mType(m) && !empty(dstSq(m)))
        || (ENPASSANT == mType(m) && dstSq(m) == epSquare())
        || PROMOTE == mType(m);
}
inline PieceType Position::captureType(Move m) const {
    return ENPASSANT != mType(m) ? pType(board[dstSq(m)]) : PAWN;
}
/// Position::pawnAdvanceAt() check if pawn is advanced at the given square
inline bool Position::pawnAdvanceAt(Color c, Square s) const {
    return contains(pieces(c, PAWN) & Regions[~c], s);
}
/// Position::pawnPassedAt() check if pawn passed at the given square
inline bool Position::pawnPassedAt(Color c, Square s) const {
    return 0 == (pawnPassSpan(c, s) & pieces(~c, PAWN));
}

/// Position::pairedBishop() check the side has pair of opposite color bishops
inline bool Position::pairedBishop(Color c) const {
    return 2 <= count(c|BSHP)
        && 0 != (pieces(c, BSHP) & Colors[WHITE])
        && 0 != (pieces(c, BSHP) & Colors[BLACK]);
}
inline bool Position::semiopenFileOn(Color c, Square s) const {
    return 0 == (pieces(c, PAWN) & fileBB(s));
}

inline void Position::doMove(Move m, StateInfo &nsi) {
    doMove(m, nsi, giveCheck(m));
}


#if !defined(NDEBUG)

extern bool isOk(const std::string&);

#endif
