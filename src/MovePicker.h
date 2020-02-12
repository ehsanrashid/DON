#pragma once

#include "MoveGenerator.h"
#include "Position.h"
#include "Tables.h"
#include "Types.h"

enum PickStage : u08
{
    NATURAL_TT = 0,
    NATURAL_INIT,
    NATURAL_GOOD_CAPTURES,
    NATURAL_REFUTATIONS,
    NATURAL_QUIETS,
    NATURAL_BAD_CAPTURES,

    EVASION_TT = 6,
    EVASION_INIT,
    EVASION_MOVES,

    PROBCUT_TT = 9,
    PROBCUT_INIT,
    PROBCUT_CAPTURE,

    QUIESCENCE_TT = 12,
    QUIESCENCE_INIT,
    QUIESCENCE_CAPTURES,
    QUIESCENCE_CHECKS,
};

inline PickStage& operator+=(PickStage &ps, i32 i) { return ps = PickStage(ps + i); }
//inline PickStage& operator-=(PickStage &ps, i32 i) { return ps = PickStage(ps - i); }
inline PickStage& operator++(PickStage &ps) { return ps = PickStage(ps + 1); }
//inline PickStage& operator--(PickStage &ps) { return ps = PickStage(ps - 1); }

/// MovePicker class is used to pick one legal moves from the current position.
/// nextMove() is the most important method, which returns a new legal move every time until there are no more moves
/// In order to improve the efficiency of the alpha-beta algorithm,
/// MovePicker attempts to return the moves which are most likely to get a cut-off first.
class MovePicker
{

private:

    const Position &pos;

    Move    ttMove;
    Depth   depth;

    Table<const PieceSquareStatsTable*, 6> pieceStats;

    Value   threshold;
    Square  recapSq;

    ValMoves vmoves;
    ValMoves::iterator vmItr, vmEnd;

    Moves   refutationMoves
        ,   badCaptureMoves;
    Moves::iterator mItr, mEnd;

    PickStage pickStage;

    template<GenType GT>
    void value();

    template<typename Pred>
    bool pick(Pred filter);

public:

    bool skipQuiets;

    MovePicker() = delete;
    MovePicker(const MovePicker&) = delete;
    MovePicker& operator=(const MovePicker&) = delete;

    MovePicker(const Position&
             , Move
             , Depth
             , const Table<const PieceSquareStatsTable*, 6>&
             , const Table<Move, 2>&
             , Move);
    MovePicker(const Position&
             , Move
             , Depth
             , const Table<const PieceSquareStatsTable*, 6>&
             , Square);
    MovePicker(const Position&
             , Move
             , Value);


    Move nextMove();

};

