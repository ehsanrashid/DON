#ifndef _MOVE_PICKER_H_INC_
#define _MOVE_PICKER_H_INC_

#include "Type.h"
#include "MoveGenerator.h"
#include "Searcher.h"

class Position;

const Value MaxHistory = Value (2000);

template<bool Gain, class T>
// The Stats struct stores moves statistics.
// According to the template parameter the class can store
// History, Gain, CounterMoves & FollowupMoves.
// - History records how often different moves have been successful or unsuccessful during the
//   current search and is used for reduction and move ordering decisions.
// - Gain records the move's best evaluation gain from one ply to the next and is used
//   for pruning decisions.
// - CounterMoves & FollowupMoves store the move that refute a previous one.
// Entries are stored according only to moving piece and destination square,
// in particular two moves with different origin but same destination and same piece will be considered identical.
struct Stats
{

private:
    T _table[TOT_PIECE][SQ_NO];

public:

    inline const T* operator[] (Piece p) const { return _table[p]; }

    inline void clear ()
    {
        memset (_table, 0x00, sizeof (_table));
    }

    inline void update (Piece p, Square s, Move m)
    {
        if (m != _table[p][s].first)
        {
            _table[p][s].second = _table[p][s].first;
            _table[p][s].first = m;
        }
    }

    inline void update (Piece p, Square s, Value v)
    {
        if (Gain)
        {
            _table[p][s] = std::max (v, _table[p][s]-1);
        }
        else
        {
            if (abs (i32 (_table[p][s] + v)) < MaxHistory)
            {
                _table[p][s] += v;
            }
        }
    }

};

typedef Stats< true,                Value  > GainStats;
typedef Stats<false,                Value  > HistoryStats;
typedef Stats<false, std::pair<Move, Move> > MoveStats;

// MovePicker class is used to pick one pseudo legal move at a time from the
// current position. The most important method is next_move(), which returns a
// new pseudo legal move each time it is called, until there are no moves left,
// when MOVE_NONE is returned. In order to improve the efficiency of the alpha
// beta algorithm, MovePicker attempts to return the moves which are most likely
// to get a cut-off first.
class MovePicker
{

private:

    enum StageT : u08
    {
        MAIN      , CAPTURES_S1, KILLERS_S1, QUIETS_1_S1, QUIETS_2_S1, BAD_CAPTURES_S1,
        EVASIONS  , EVASIONS_S2,
        QSEARCH_0 , CAPTURES_S3, QUIET_CHECKS_S3,
        QSEARCH_1 , CAPTURES_S4,
        PROBCUT   , CAPTURES_S5,
        RECAPTURE , CAPTURES_S6,
        STOP
    };

    ValMove  moves[MAX_MOVES]
        ,   *cur
        ,   *end
        ,   *quiets_end
        ,   *bad_captures_end;

    const Position     &pos;

    const HistoryStats &history;

    Searcher::Stack    *ss;

    ValMove killers[6];
    Move   *counter_moves;
    Move   *followup_moves;

    Move    tt_move;
    Depth   depth;

    Square  recapture_sq;

    Value   capture_threshold;

    u08     stage;

    MovePicker& operator= (const MovePicker &); // Silence a warning under MSVC

    template<MoveGenerator::GenT>
    // value() assign a numerical move ordering score to each move in a move list.
    // The moves with highest scores will be picked first.
    void value ();

    void generate_next_stage ();


public:

    MovePicker (const Position&, const HistoryStats&, Move, Depth, Move*, Move*, Searcher::Stack*);
    MovePicker (const Position&, const HistoryStats&, Move, Depth, Square);
    MovePicker (const Position&, const HistoryStats&, Move,        PieceT);

    template<bool SPNode>
    Move next_move ();

};

#endif // _MOVE_PICKER_H_INC_
