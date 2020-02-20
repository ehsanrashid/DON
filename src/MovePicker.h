#pragma once

#include "MoveGenerator.h"
#include "Position.h"
#include "Table.h"
#include "Type.h"

enum PickStage : u08 {
    NATURAL_TT = 0,
    NATURAL_INIT,
    NATURAL_GOOD_CAPTURES,
    NATURAL_REFUTATIONS,
    NATURAL_QUIETS,
    NATURAL_BAD_CAPTURES,

    EVASION_TT = 8,
    EVASION_INIT,
    EVASION_MOVES,

    PROBCUT_TT = 12,
    PROBCUT_INIT,
    PROBCUT_CAPTURE,

    QUIESCENCE_TT = 16,
    QUIESCENCE_INIT,
    QUIESCENCE_CAPTURES,
    QUIESCENCE_CHECKS,
};

constexpr PickStage operator+(PickStage ps, i32 i) { return PickStage(i32(ps) + i); }
constexpr PickStage operator+(PickStage ps, bool b) { return ps + i32(b); }
inline PickStage& operator+=(PickStage &ps, i32 i) { return ps = ps + i; }
inline PickStage& operator++(PickStage &ps) { return ps = ps + 1; }

/// MovePicker class is used to pick one legal moves from the current position.
/// nextMove() is the most important method, which returns a new legal move every time until there are no more moves
/// In order to improve the efficiency of the alpha-beta algorithm,
/// MovePicker attempts to return the moves which are most likely to get a cut-off first.
class MovePicker {
private:

    const Position &pos;
    const ColorIndexStatsTable *quietStats{ nullptr };
    const PieceSquareTypeStatsTable *captureStats{ nullptr };
    const PieceSquareStatsTable **pieceStats{ nullptr };

    Move    ttMove;
    Depth   depth;
    Value   threshold{ VALUE_ZERO };
    Square  recapSq{ SQ_NONE };

    PickStage pickStage;

    ValMoves vmoves;
    ValMoves::iterator vmItr, vmEnd;

    std::vector<Move> refutationMoves
        ,             badCaptureMoves;
    std::vector<Move>::iterator mItr, mEnd;

    template<GenType GT>
    void value();

    template<typename Pred>
    bool pick(Pred);

public:

    bool skipQuiets{ false };

    MovePicker() = delete;
    MovePicker(const MovePicker&) = delete;
    MovePicker& operator=(const MovePicker&) = delete;

    MovePicker(
          const Position&
        , const ColorIndexStatsTable*
        , const PieceSquareTypeStatsTable*
        , const PieceSquareStatsTable**
        , Move, Depth, const Array<Move, 2>&, Move);
    MovePicker(
          const Position&
        , const ColorIndexStatsTable*
        , const PieceSquareTypeStatsTable*
        , const PieceSquareStatsTable**
        , Move, Depth, Square);
    MovePicker(
          const Position&
        , const PieceSquareTypeStatsTable*
        , Move, Value);

    Move nextMove();

};
