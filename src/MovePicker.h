#pragma once

#include "MoveGenerator.h"
#include "Position.h"
#include "Type.h"

/// ColorIndexStatsTable stores moves history according to color.
/// Used for reduction and move ordering decisions.
/// indexed by [color][moveIndex]
using ColorIndexStatsTable      = StatsTable<i16, 10692, COLORS, SQUARES*SQUARES>;

/// PlyIndexStatsTable stores moves history according to ply from 0 to MAX_LOWPLY-1
constexpr i16 MAX_LOWPLY = 4;
using PlyIndexStatsTable        = StatsTable<i16, 10692, MAX_LOWPLY, SQUARES*SQUARES>;

/// PieceSquareTypeStatsTable stores move history according to piece.
/// indexed by [piece][square][captureType]
using PieceSquareTypeStatsTable = StatsTable<i16, 10692, PIECES, SQUARES, PIECE_TYPES>;

/// PieceSquareStatsTable store moves history according to piece.
/// indexed by [piece][square]
using PieceSquareStatsTable     = StatsTable<i16, 29952, PIECES, SQUARES>;
/// ContinuationStatsTable is the combined history of a given pair of moves, usually the current one given a previous one.
/// The nested history table is based on PieceSquareStatsTable, indexed by [piece][square]
using ContinuationStatsTable    = Table<PieceSquareStatsTable, PIECES, SQUARES>;

/// PieceSquareMoveTable stores moves, indexed by [piece][square]
using PieceSquareMoveTable      = Table<Move, PIECES, SQUARES>;


/// MovePicker class is used to pick one legal moves from the current position.
/// nextMove() is the most important method, which returns a new legal move every time until there are no more moves
/// In order to improve the efficiency of the alpha-beta algorithm,
/// MovePicker attempts to return the moves which are most likely to get a cut-off first.
class MovePicker {

private:

    Position const &pos;
    ColorIndexStatsTable const *butterFlyStats{ nullptr };
    PlyIndexStatsTable const *lowPlyStats{ nullptr };
    PieceSquareTypeStatsTable const *captureStats{ nullptr };
    PieceSquareStatsTable const **pieceStats{ nullptr };

    Move    ttMove{ MOVE_NONE };
    Depth   depth{ DEPTH_ZERO };
    i16     ply{ 0 };
    Value   threshold{ VALUE_ZERO };
    Square  recapSq{ SQ_NONE };

    u08     stage{ 0 };

    ValMoves vmoves;
    ValMoves::iterator vmBeg, vmEnd;

    Moves refutationMoves
        , badCaptureMoves;
    Moves::iterator mBeg, mEnd;

    template<GenType GT>
    void value();

    template<typename Pred>
    bool pick(Pred);

public:

    bool skipQuiets{ false };

    MovePicker() = delete;
    MovePicker(MovePicker const&) = delete;
    MovePicker& operator=(MovePicker const&) = delete;

    MovePicker(
          Position const&
        , ColorIndexStatsTable const*
        , PlyIndexStatsTable const*
        , PieceSquareTypeStatsTable const*
        , PieceSquareStatsTable const**
        , Move, Depth, i16
        , Array<Move, 2> const&, Move);

    MovePicker(
          Position const&
        , ColorIndexStatsTable const*
        , PieceSquareTypeStatsTable const*
        , PieceSquareStatsTable const**
        , Move, Depth, Square);

    MovePicker(
          Position const&
        , PieceSquareTypeStatsTable const*
        , Move, Value);


    Move nextMove();

};
