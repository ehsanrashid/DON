#pragma once

#include <cassert>
#include <deque>
#include <memory> // For std::unique_ptr
#include <string>

#include "bitboard.h"
#include "psqtable.h"
#include "evaluator.h"
#include "type.h"
#include "nnue/accumulator.h"

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
    // ---Copied when making a move
    Key         matlKey;        // Hash key of materials
    Key         pawnKey;        // Hash key of pawns
    int16_t     clockPly;       // Number of half moves clock since the last pawn advance or any capture
    int16_t     nullPly;
    CastleRight castleRights;   // Castling-rights information
    Square      epSquare;       // Enpassant -> "In passing"

    // ---Not copied when making a move (will be recomputed anyhow)
    Key         posiKey;        // Hash key of position
    Bitboard    checkers;       // Checkers
    int16_t     repetition;
    PieceType   captured;       // Piece type captured
    bool        promoted;
    
    // Check info
    Bitboard    kingBlockers[COLORS]; // Absolute and Discover Blockers
    Bitboard    kingCheckers[COLORS]; // Absolute and Discover Checkers
    Bitboard    checks[PIECE_TYPES];

    // Used by NNUE
    Evaluator::NNUE::Accumulator accumulator;
    MoveInfo moveInfo;
    
    StateInfo  *prevState;      // Previous StateInfo pointer
};

/// A list to keep track of the position states along the setup moves
/// (from the start position to the position just before the search starts).
/// Needed by 'draw by repetition' detection.
/// Use a std::deque because pointers to elements are not invalidated upon list resizing.
using StateList     = std::deque<StateInfo>;
using StateListPtr  = std::unique_ptr<StateList>;

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
class Position final {

public:
    //static void initialize();

    Position() = default;
    Position(Position const&) = delete;
    Position(Position&&) = delete;

    Position& operator=(Position const&) = delete;
    Position& operator=(Position&&) = delete;

    constexpr Piece operator[](Square) const noexcept;
    constexpr bool empty(Square) const noexcept;

    //Bitboard pieces(Piece) const noexcept;
    Bitboard pieces(Color) const noexcept;
    Bitboard pieces(PieceType = NONE) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(PieceType, PieceTypes...) const noexcept;
    template<typename... PieceTypes>
    Bitboard pieces(Color, PieceTypes...) const noexcept;

    int32_t count(Piece) const noexcept;
    int32_t count(PieceType) const noexcept;
    int32_t count(Color) const noexcept;
    int32_t count() const noexcept;
    Square const* squares(Piece) const noexcept;
    Square square(Piece, uint8_t = 0) const noexcept;

    Value nonPawnMaterial(Color) const noexcept;
    Value nonPawnMaterial() const noexcept;

    Square   castleRookSq(Color, CastleSide) const noexcept;
    Bitboard castleKingPath(Color, CastleSide) const noexcept;
    Bitboard castleRookPath(Color, CastleSide) const noexcept;
    //CastleRight castleRight(Square) const noexcept;

    CastleRight castleRights() const noexcept;
    bool canCastle(Color) const noexcept;
    bool canCastle(Color, CastleSide) const noexcept;
    Square epSquare() const noexcept;

    int16_t clockPly() const noexcept;
    int16_t nullPly() const noexcept;

    // Accessing hash keys
    Key matlKey() const noexcept;
    Key pawnKey() const noexcept;
    Key posiKey() const noexcept;
    Key pgKey() const noexcept;
    Key movePosiKey(Move) const noexcept;

    Bitboard checkers() const noexcept;
    PieceType captured() const noexcept;
    bool promoted() const noexcept;
    int16_t repetition() const noexcept;

    Bitboard kingBlockers(Color) const noexcept;
    Bitboard kingCheckers(Color) const noexcept;
    Bitboard checks(PieceType) const noexcept;

    Color activeSide() const noexcept;
    Score psqScore() const noexcept;
    int16_t plyCount() const noexcept;
    Thread* thread() const noexcept;
    void thread(Thread*) noexcept;

    bool castleExpeded(Color, CastleSide) const noexcept;

    int16_t moveCount() const noexcept;
    bool draw(int16_t) const noexcept;
    bool repeated() const noexcept;
    bool cycled(int16_t) const noexcept;

    Bitboard attackersTo(Square, Bitboard) const noexcept;
    Bitboard attackersTo(Square) const noexcept;

    Bitboard sliderBlockersAt(Square, Bitboard, Bitboard&, Bitboard&) const noexcept;

    constexpr Piece movedPiece(Move) const noexcept;
    constexpr Piece prevMovedPiece(Move) const noexcept;

    constexpr bool capture(Move) const noexcept;
    constexpr bool captureOrPromotion(Move) const noexcept;
    constexpr bool advancedPawnPush(Move) const noexcept;
    constexpr PieceType captured(Move) const noexcept;

    bool pseudoLegal(Move) const noexcept;
    bool legal(Move) const noexcept;
    bool giveCheck(Move) const noexcept;
    bool giveDblCheck(Move) const noexcept;

    bool see(Move, Value = VALUE_ZERO) const noexcept;

    bool pawnPassedAt(Color, Square) const noexcept;
    Bitboard pawnsOnColor(Color, Square) const noexcept;

    bool bishopPaired(Color) const noexcept;
    bool bishopOpposed() const noexcept;
    bool semiopenFileOn(Color, Square) const noexcept;

    Position& setup(std::string_view, StateInfo&, Thread* = nullptr);
    Position& setup(std::string_view, Color, StateInfo&);

    void doMove(Move, StateInfo&, bool) noexcept;
    void doMove(Move, StateInfo&) noexcept;
    void undoMove(Move) noexcept;
    void doNullMove(StateInfo&) noexcept;
    void undoNullMove() noexcept;

    void flip();
    void mirror();

    // Used by NNUE
    StateInfo* state() const noexcept;

    std::string fen(bool = true) const;

    std::string toString() const;

#if !defined(NDEBUG)
    bool ok() const noexcept;
#endif

private:

    void placePiece(Square, Piece) noexcept;
    void removePiece(Square) noexcept;
    void movePiece(Square, Square) noexcept;

    void setCastle(Color, Square);
    void setCheckInfo() noexcept;

    bool canEnpassant(Color, Square, bool = true) const noexcept;

    Bitboard colors[COLORS];
    Bitboard types[PIECE_TYPES];

    Piece   board[SQUARES];
    uint8_t pieceIndex[SQUARES];
    Square  pieceSquare[PIECES][12];
    uint8_t pieceCount[PIECES];

    Value   npm[COLORS];

    Square   cslRookSq[COLORS][CASTLE_SIDES];
    Bitboard cslKingPath[COLORS][CASTLE_SIDES];
    Bitboard cslRookPath[COLORS][CASTLE_SIDES];
    CastleRight sqCastleRight[SQUARES];

    Color   active;
    Score   psq;
    int16_t ply;

    StateInfo *_stateInfo;
    Thread    *_thread;

    friend std::ostream& operator<<(std::ostream&, Position const&);
};

constexpr Piece Position::operator[](Square s) const noexcept {
    //assert(isOk(s));
    return board[s];
}
constexpr bool Position::empty(Square s) const noexcept {
    return board[s] == NO_PIECE;
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
inline int32_t Position::count(Piece p) const noexcept {
    return pieceCount[p];
}
/// Position::count() counts specific type
inline int32_t Position::count(PieceType pt) const noexcept {
    return count(WHITE|pt) + count(BLACK|pt);
}
/// Position::count() counts specific color
inline int32_t Position::count(Color c) const noexcept {
    return count(c|PAWN) + count(c|NIHT) + count(c|BSHP) + count(c|ROOK) + count(c|QUEN) + count(c|KING);
}
/// Position::count() counts all
inline int32_t Position::count() const noexcept {
    return count(WHITE) + count(BLACK);
}

inline Square const* Position::squares(Piece p) const noexcept {
    return pieceSquare[p];
}
inline Square Position::square(Piece p, uint8_t idx) const noexcept {
    assert(isOk(p)
        && count(p) > idx);
    return squares(p)[idx];
}

inline Value Position::nonPawnMaterial(Color c) const noexcept {
    return npm[c];
}
inline Value Position::nonPawnMaterial() const noexcept {
    return nonPawnMaterial(WHITE) + nonPawnMaterial(BLACK);
}

inline Square Position::castleRookSq(Color c, CastleSide cs) const noexcept {
    return cslRookSq[c][cs];
}
inline Bitboard Position::castleKingPath(Color c, CastleSide cs) const noexcept {
    return cslKingPath[c][cs];
}
inline Bitboard Position::castleRookPath(Color c, CastleSide cs) const noexcept {
    return cslRookPath[c][cs];
}
//inline CastleRight Position::castleRight(Square s) const noexcept { return sqCastleRight[s]; }

inline CastleRight Position::castleRights() const noexcept {
    return _stateInfo->castleRights;
}
inline bool Position::canCastle(Color c) const noexcept {
    return (castleRights() & makeCastleRight(c)) != CR_NONE;
}
inline bool Position::canCastle(Color c, CastleSide cs) const noexcept {
    return (castleRights() & makeCastleRight(c, cs)) != CR_NONE;
}
inline Square Position::epSquare() const noexcept {
    return _stateInfo->epSquare;
}

inline int16_t Position::clockPly() const noexcept {
    return _stateInfo->clockPly;
}
inline int16_t Position::nullPly() const noexcept {
    return _stateInfo->nullPly;
}

inline Key Position::matlKey() const noexcept {
    return _stateInfo->matlKey;
}
inline Key Position::pawnKey() const noexcept {
    return _stateInfo->pawnKey;
}
inline Key Position::posiKey() const noexcept {
    return _stateInfo->posiKey;
}
inline Bitboard Position::checkers() const noexcept {
    return _stateInfo->checkers;
}
inline PieceType Position::captured() const noexcept {
    return _stateInfo->captured;
}
inline bool Position::promoted() const noexcept {
    return _stateInfo->promoted;
}
inline int16_t Position::repetition() const noexcept {
    return _stateInfo->repetition;
}

inline Bitboard Position::kingBlockers(Color c) const noexcept {
    return _stateInfo->kingBlockers[c];
}
inline Bitboard Position::kingCheckers(Color c) const noexcept {
    return _stateInfo->kingCheckers[c];
}
inline Bitboard Position::checks(PieceType pt) const noexcept {
    return _stateInfo->checks[pt];
}

inline Color Position::activeSide() const noexcept {
    return active;
}
inline Score Position::psqScore() const noexcept {
    return psq;
}
inline int16_t Position::plyCount() const noexcept {
    return ply;
}

inline Thread* Position::thread() const noexcept {
    return _thread;
}
inline void Position::thread(Thread *th) noexcept {
    _thread = th;
}

inline bool Position::castleExpeded(Color c, CastleSide cs) const noexcept {
    return (castleRookPath(c, cs) & pieces()) == 0;
}
/// Position::moveCount() starts at 1, and is incremented after BLACK's move.
inline int16_t Position::moveCount() const noexcept {
    return int16_t( std::max((ply - active) / 2, 0) + 1 );
}

inline void Position::placePiece(Square s, Piece p) noexcept {
    types[NONE] |= s;
    types[pType(p)] |= s;
    colors[pColor(p)] |= s;
    board[s] = p;
    pieceIndex[s] = pieceCount[p]++;
    pieceSquare[p][pieceIndex[s]] = s;
    ++pieceCount[pColor(p)|NONE];
    psq += PSQ[p][s];
}
inline void Position::removePiece(Square s) noexcept {
    auto const p{ board[s] };
    types[NONE] ^= s;
    types[pType(p)] ^= s;
    colors[pColor(p)] ^= s;
    //board[s] = NO_PIECE; // Not needed, overwritten by the capturing one
    auto const endSq{ pieceSquare[p][--pieceCount[p]] };
    pieceIndex[endSq] = pieceIndex[s];
    pieceSquare[p][pieceIndex[endSq]] = endSq;
    pieceSquare[p][pieceCount[p]] = SQ_NONE;
    --pieceCount[pColor(p)|NONE];
    psq -= PSQ[p][s];
}
inline void Position::movePiece(Square s1, Square s2) noexcept {
    auto const p{ board[s1] };
    Bitboard const bb{ s1 | s2 };
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
inline Bitboard Position::attackersTo(Square s, Bitboard occ) const noexcept {
    return (pieces(BLACK, PAWN) & pawnAttacksBB(WHITE, s))
         | (pieces(WHITE, PAWN) & pawnAttacksBB(BLACK, s))
         | (pieces(NIHT)        & attacksBB<NIHT>(s))
         | (pieces(BSHP, QUEN)  & attacksBB<BSHP>(s, occ))
         | (pieces(ROOK, QUEN)  & attacksBB<ROOK>(s, occ))
         | (pieces(KING)        & attacksBB<KING>(s));
}
/// Position::attackersTo() finds attackers to the square.
inline Bitboard Position::attackersTo(Square s) const noexcept {
    return attackersTo(s, pieces());
}

constexpr Piece Position::movedPiece(Move m) const noexcept {
    return board[orgSq(m)];
}
constexpr Piece Position::prevMovedPiece(Move m) const noexcept {
    return mType(m) != CASTLE ? board[dstSq(m)] : (~active|KING);
}

constexpr bool Position::capture(Move m) const noexcept {
    return  mType(m) == ENPASSANT
        || (mType(m) != CASTLE && contains(pieces(~active), dstSq(m)));
}
constexpr bool Position::captureOrPromotion(Move m) const noexcept {
    //return  mType(m) == ENPASSANT
    //    ||  mType(m) == PROMOTE
    //    || (mType(m) == SIMPLE && contains(pieces(~active), dstSq(m)));
    return mType(m) == SIMPLE ?
            contains(pieces(~active), dstSq(m)) : mType(m) != CASTLE;
}
/// Position::advancedPawnPush() check if advanced pawn is push
constexpr bool Position::advancedPawnPush(Move m) const noexcept {
    return pType(board[orgSq(m)]) == PAWN
        && relativeRank(active, dstSq(m)) > RANK_5;
}

constexpr PieceType Position::captured(Move m) const noexcept {
    return mType(m) == ENPASSANT ? PAWN :
           mType(m) != CASTLE    ? pType(board[dstSq(m)]) : NONE;
}

/// Position::pawnPassedAt() check if pawn passed at the given square
inline bool Position::pawnPassedAt(Color c, Square s) const noexcept {
    return (pieces(~c, PAWN) & pawnPassSpan(c, s)) == 0;
}

inline Bitboard Position::pawnsOnColor(Color c, Square s) const noexcept {
    return pieces(c, PAWN) & colorBB(sColor(s));
}

/// Position::bishopPaired() check the side has pair of opposite color bishops
inline bool Position::bishopPaired(Color c) const noexcept {
    return count(c|BSHP) >= 2
        && (pieces(c, BSHP) & colorBB(WHITE)) != 0
        && (pieces(c, BSHP) & colorBB(BLACK)) != 0;
}
inline bool Position::bishopOpposed() const noexcept {
    return count(W_BSHP) == 1
        && count(B_BSHP) == 1
        && colorOpposed(square(W_BSHP), square(B_BSHP));
}

inline bool Position::semiopenFileOn(Color c, Square s) const noexcept {
    return (pieces(c, PAWN) & fileBB(s)) == 0;
}

inline void Position::doMove(Move m, StateInfo &si) noexcept {
    doMove(m, si, giveCheck(m));
}


inline StateInfo* Position::state() const noexcept {
    return _stateInfo;
}


#if !defined(NDEBUG)
extern bool isOk(std::string_view);
#endif
