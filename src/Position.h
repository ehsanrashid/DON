#pragma once

#include <deque>
#include <memory> // For std::unique_ptr
#include <string>

#include "Bitboard.h"
#include "Evaluator.h"
#include "Type.h"
#include "nnue/nnue_accumulator.h"

/// StateInfo stores information needed to restore a Position object to its previous state when we retract a move.
///
///  - Hash key of the material situation
///  - Hash key of the pawn structure
///  - Hash key of the position
///  - Castling-rights information
///  - Enpassant square(SQ_NONE if no Enpassant capture is possible)
///  - Clock for detecting 50 move rule draws
///  - Piece type captured on last position
///  - Repetition info
///  - Bitboards of kingBlockers & kingCheckers
///  - Bitboards of all checking pieces
///  - Pointer to previous StateInfo
struct StateInfo {
    // ---Copied when making a move---
    Key         matlKey;        // Hash key of materials
    Key         pawnKey;        // Hash key of pawns
    CastleRight castleRights;   // Castling-rights information
    Square      epSquare;       // Enpassant -> "In passing"
    i16         clockPly;       // Number of half moves clock since the last pawn advance or any capture
    i16         nullPly;

    // ---Not copied when making a move---
    Key         posiKey;        // Hash key of position
    Bitboard    checkers;       // Checkers
    PieceType   captured;       // Piece type captured
    bool        promoted;
    i16         repetition;
    // Check info
    Bitboard kingBlockers[COLORS]; // Absolute and Discover Blockers
    Bitboard kingCheckers[COLORS]; // Absolute and Discover Checkers
    Bitboard checks[PIECE_TYPES];

    StateInfo *prevState; // Previous StateInfo pointer

    // Used by NNUE
    Evaluator::NNUE::Accumulator accumulator;
    DirtyPiece dirtyPiece;
};

/// A list to keep track of the position states along the setup moves
/// (from the start position to the position just before the search starts).
/// Needed by 'draw by repetition' detection.
/// Use a std::deque because pointers to elements are not invalidated upon list resizing.
using StateListPtr = std::unique_ptr<std::deque<StateInfo>>;

class Thread;

extern Score PSQ[PIECES][SQUARES];

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
class Position {

private:
    Bitboard colors[COLORS];
    Bitboard types[PIECE_TYPES];

    Piece  board[SQUARES];
    u08    pieceIndex[SQUARES];
    Square pieceSquare[PIECES][12];
    u08    pieceCount[PIECES];

    Value  npMaterial[COLORS];

    Square   cslRookSq[COLORS][CASTLE_SIDES];
    Bitboard cslKingPath[COLORS][CASTLE_SIDES];
    Bitboard cslRookPath[COLORS][CASTLE_SIDES];
    CastleRight sqCastleRight[SQUARES];

    Color active;
    Score psq;
    i16   ply;

    StateInfo *_stateInfo;
    Thread *_thread;
    // List of pieces used in NNUE evaluation function
    EvalList _evalList;

    void placePiece(Square, Piece);
    void removePiece(Square);
    void movePiece(Square, Square);

    void setCastle(Color, Square);
    void setCheckInfo();

    bool canEnpassant(Color, Square, bool = true) const;

    // ID of a piece on a given square
    PieceId pieceIdOn(Square sq) const;

public:
    //static void initialize();

    Position() = default;
    Position(Position const&) = delete;
    Position(Position&&) = delete;
    Position& operator=(Position const&) = delete;
    Position& operator=(Position&&) = delete;

    Piece operator[](Square) const;
    bool empty(Square) const;

    //Bitboard pieces(Piece) const noexcept;
    Bitboard pieces(Color) const noexcept;
    Bitboard pieces(PieceType = NONE) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(PieceType, PieceTypes...) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(Color, PieceTypes...) const noexcept;

    i32 count(Piece) const noexcept;
    i32 count(PieceType) const noexcept;
    i32 count(Color) const noexcept;
    i32 count() const noexcept;
    Square const *squares(Piece) const noexcept;
    Square square(Piece, u08 = 0) const noexcept;

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
    Bitboard checkers() const;
    PieceType captured() const;
    bool promoted() const;
    i16 repetition() const;

    Bitboard kingBlockers(Color) const;
    Bitboard kingCheckers(Color) const;
    Bitboard checks(PieceType) const;

    Color activeSide() const;
    Score psqScore() const;
    i16 gamePly() const;
    Thread* thread() const;
    void thread(Thread*);

    bool castleExpeded(Color, CastleSide) const;

    Key pgKey() const;
    Key movePosiKey(Move) const;

    i16  moveCount() const;
    bool draw(i16) const;
    bool repeated() const;
    bool cycled(i16) const;

    Bitboard attackersTo(Square, Bitboard) const;
    Bitboard attackersTo(Square) const;

    Bitboard sliderBlockersAt(Square, Bitboard, Bitboard&, Bitboard&) const;

    bool capture(Move) const;
    bool captureOrPromotion(Move) const;

    bool pseudoLegal(Move) const;
    bool legal(Move) const;
    bool giveCheck(Move) const;
    bool giveDblCheck(Move) const;

    PieceType captured(Move) const;

    bool see(Move, Value = VALUE_ZERO) const;

    bool pawnAdvanceAt(Color, Square) const;
    bool pawnPassedAt(Color, Square) const;
    Bitboard pawnsOnSqColor(Color, Color) const;

    bool bishopPaired(Color) const;
    bool bishopOpposed() const;
    bool semiopenFileOn(Color, Square) const;

    Position& setup(std::string const&, StateInfo&, Thread *const = nullptr);
    Position& setup(std::string const&, Color, StateInfo&);

    void doMove(Move, StateInfo&, bool);
    void doMove(Move, StateInfo&);
    void undoMove(Move);
    void doNullMove(StateInfo&);
    void undoNullMove();

    void flip();
    void mirror();

    // Used by NNUE
    StateInfo* state() const;
    EvalList const* evalList() const;

    std::string fen(bool full = true) const;

    std::string toString() const;

#if !defined(NDEBUG)
    bool ok() const;
#endif

};

extern std::ostream& operator<<(std::ostream&, Position const&);


inline Piece Position::operator[](Square s) const {
    assert(isOk(s));
    return board[s];
}
inline bool Position::empty(Square s) const {
    return operator[](s) == NO_PIECE;
}

//inline Bitboard Position::pieces(Piece p) const noexcept { return colors[pColor(p)] & types[pType(p)]; }
inline Bitboard Position::pieces(Color c) const noexcept {
    return colors[c];
}
inline Bitboard Position::pieces(PieceType pt) const noexcept {
    return types[pt];
}
template<typename... PieceTypes>
inline Bitboard Position::pieces(PieceType pt, PieceTypes... pts) const noexcept {
    return types[pt] | pieces(pts...);
}
template<typename... PieceTypes>
inline Bitboard Position::pieces(Color c, PieceTypes... pts) const noexcept {
    return colors[c] & pieces(pts...);
}

/// Position::count() counts specific piece
inline i32 Position::count(Piece p) const noexcept {
    return pieceCount[p];
}
/// Position::count() counts specific type
inline i32 Position::count(PieceType pt) const noexcept {
    return count(WHITE|pt) + count(BLACK|pt);
}
/// Position::count() counts specific color
inline i32 Position::count(Color c) const noexcept {
    return count(c|PAWN) + count(c|NIHT) + count(c|BSHP) + count(c|ROOK) + count(c|QUEN) + count(c|KING);
}
/// Position::count() counts all
inline i32 Position::count() const noexcept {
    return count(WHITE) + count(BLACK);
}

inline Square const *Position::squares(Piece p) const noexcept {
    return pieceSquare[p];
}
inline Square Position::square(Piece p, u08 idx) const noexcept {
    assert(isOk(p));
    assert(count(p) > idx);
    return squares(p)[idx];
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

inline CastleRight Position::castleRights() const {
    return _stateInfo->castleRights;
}
inline bool Position::canCastle(Color c) const {
    return (castleRights() & makeCastleRight(c)) != CR_NONE;
}
inline bool Position::canCastle(Color c, CastleSide cs) const {
    return (castleRights() & makeCastleRight(c, cs)) != CR_NONE;
}
inline Square Position::epSquare() const {
    return _stateInfo->epSquare;
}

inline i16 Position::clockPly() const {
    return _stateInfo->clockPly;
}
inline i16 Position::nullPly() const {
    return _stateInfo->nullPly;
}

inline Key Position::matlKey() const {
    return _stateInfo->matlKey;
}
inline Key Position::pawnKey() const {
    return _stateInfo->pawnKey;
}
inline Key Position::posiKey() const {
    return _stateInfo->posiKey;
}
inline Bitboard Position::checkers() const {
    return _stateInfo->checkers;
}
inline PieceType Position::captured() const {
    return _stateInfo->captured;
}
inline bool Position::promoted() const {
    return _stateInfo->promoted;
}
inline i16 Position::repetition() const {
    return _stateInfo->repetition;
}

inline Bitboard Position::kingBlockers(Color c) const {
    return _stateInfo->kingBlockers[c];
}
inline Bitboard Position::kingCheckers(Color c) const {
    return _stateInfo->kingCheckers[c];
}
inline Bitboard Position::checks(PieceType pt) const {
    return _stateInfo->checks[pt];
}

inline Color Position::activeSide() const {
    return active;
}
inline Score Position::psqScore() const {
    return psq;
}
inline i16 Position::gamePly() const {
    return ply;
}

inline Thread* Position::thread() const {
    return _thread;
}
inline void Position::thread(Thread *th) {
    _thread = th;
}

inline bool Position::castleExpeded(Color c, CastleSide cs) const {
    return (castleRookPath(c, cs) & pieces()) == 0;
}
/// Position::moveCount() starts at 1, and is incremented after BLACK's move.
inline i16 Position::moveCount() const {
    return i16(std::max((ply - active) / 2, 0) + 1);
}

inline void Position::placePiece(Square s, Piece p) {
    types[NONE] |= s;
    types[pType(p)] |= s;
    colors[pColor(p)] |= s;
    board[s] = p;
    pieceIndex[s] = pieceCount[p]++;
    pieceSquare[p][pieceIndex[s]] = s;
    ++pieceCount[pColor(p)|NONE];
    psq += PSQ[p][s];
}
inline void Position::removePiece(Square s) {
    auto p{ board[s] };
    types[NONE] ^= s;
    types[pType(p)] ^= s;
    colors[pColor(p)] ^= s;
    //board[s] = NO_PIECE; // Not needed, overwritten by the capturing one
    Square endSq = pieceSquare[p][--pieceCount[p]];
    pieceIndex[endSq] = pieceIndex[s];
    pieceSquare[p][pieceIndex[endSq]] = endSq;
    pieceSquare[p][pieceCount[p]] = SQ_NONE;
    --pieceCount[pColor(p)|NONE];
    psq -= PSQ[p][s];
}
inline void Position::movePiece(Square s1, Square s2) {
    auto p{ board[s1] };
    Bitboard bb{ s1 | s2 };
    types[NONE] ^= bb;
    types[pType(p)] ^= bb;
    colors[pColor(p)] ^= bb;
    board[s2] = p;
    board[s1] = NO_PIECE;
    pieceIndex[s2] = pieceIndex[s1];
    pieceSquare[p][pieceIndex[s2]] = s2;
    psq += PSQ[p][s2]
         - PSQ[p][s1];
}

/// Position::attackersTo() finds attackers to the square on occupancy.
inline Bitboard Position::attackersTo(Square s, Bitboard occ) const {
    return (pieces(BLACK, PAWN) & pawnAttacksBB(WHITE, s))
         | (pieces(WHITE, PAWN) & pawnAttacksBB(BLACK, s))
         | (pieces(NIHT)        & attacksBB<NIHT>(s))
         | (pieces(BSHP, QUEN)  & attacksBB<BSHP>(s, occ))
         | (pieces(ROOK, QUEN)  & attacksBB<ROOK>(s, occ))
         | (pieces(KING)        & attacksBB<KING>(s));
}
/// Position::attackersTo() finds attackers to the square.
inline Bitboard Position::attackersTo(Square s) const {
    return attackersTo(s, pieces());
}

inline bool Position::capture(Move m) const {
    assert(isOk(m));
    return mType(m) != ENPASSANT ?
            contains(pieces(~active), dstSq(m)) : true;
}
inline bool Position::captureOrPromotion(Move m) const {
    assert(isOk(m));
    return mType(m) == SIMPLE ?
            contains(pieces(~active), dstSq(m)) : mType(m) != CASTLE;
}
inline PieceType Position::captured(Move m) const {
    assert(isOk(m));
    return mType(m) != ENPASSANT ? pType(operator[](dstSq(m))) : PAWN;
}
/// Position::pawnAdvanceAt() check if pawn is advanced at the given square
inline bool Position::pawnAdvanceAt(Color c, Square s) const {
    return contains(/*pieces(c, PAWN) & */PawnSideBB[~c], s);
}
/// Position::pawnPassedAt() check if pawn passed at the given square
inline bool Position::pawnPassedAt(Color c, Square s) const {
    return (pieces(~c, PAWN) & pawnPassSpan(c, s)) == 0;
}

inline Bitboard Position::pawnsOnSqColor(Color c, Color sqC) const {
    return pieces(c, PAWN) & ColorBB[sqC];
}

/// Position::bishopPaired() check the side has pair of opposite color bishops
inline bool Position::bishopPaired(Color c) const {
    Bitboard b{ pieces(c, BSHP) };
    return moreThanOne(b)
        && ((b & ColorBB[WHITE]) != 0) == ((b & ColorBB[BLACK]) != 0);
}
inline bool Position::bishopOpposed() const {
    return count(W_BSHP) == 1
        && count(B_BSHP) == 1
        && colorOpposed(square(W_BSHP), square(B_BSHP));
}

inline bool Position::semiopenFileOn(Color c, Square s) const {
    return (pieces(c, PAWN) & fileBB(s)) == 0;
}

inline void Position::doMove(Move m, StateInfo &si) {
    doMove(m, si, giveCheck(m));
}


inline StateInfo* Position::state() const {
    return _stateInfo;
}
inline EvalList const* Position::evalList() const {
    return &_evalList;
}

inline PieceId Position::pieceIdOn(Square sq) const {
    assert(board[sq] != NO_PIECE);

    PieceId pid = _evalList.pieceIdList[sq];
    assert(isOk(pid));
    return pid;
}


#if !defined(NDEBUG)
extern bool isOk(std::string const&);
#endif
