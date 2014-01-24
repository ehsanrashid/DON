//#pragma once
#ifndef MOVEPICKER_H_
#define MOVEPICKER_H_

#include <vector>
#include <set>
#include "Type.h"
#include "MoveGenerator.h"
#include "Position.h"
#include "Searcher.h"

template<bool GAIN, class T>
// The Stats struct stores moves statistics.
// According to the template parameter the class can store History, Gains and Countermoves.
// History records how often different moves have been successful or unsuccessful during the
// current search and is used for reduction and move ordering decisions.
// Gains records the move's best evaluation gain from one ply to the next and is used
// for pruning decisions.
// Countermoves store the move that refute a previous one.
// Entries are stored according only to moving piece and destination square,
// in particular two moves with different origin but same destination and same piece will be considered identical.
struct Stats
{

private:
    T _table[14][SQ_NO];

public:

    inline const T* operator[] (Piece p) const { return _table[p]; }

    inline void clear ()
    {
        memset (_table, 0, sizeof (_table));
    }

    inline void update (Piece p, Square s, Move m)
    {
        if (m == _table[p][s].first) return;

        _table[p][s].second = _table[p][s].first;
        _table[p][s].first = m;
    }

    inline void update (Piece p, Square s, Value v)
    {
        if (GAIN)
        {
            _table[p][s] = std::max (v, _table[p][s]);
        }
        else
        {
            if (abs (int32_t (_table[p][s] + v)) < VALUE_KNOWN_WIN)
            {
                _table[p][s] += v;
            }
        }
    }

};

typedef Stats< true, Value> GainsStats;
typedef Stats<false, Value> HistoryStats;

typedef Stats<false, std::pair<Move, Move> > MovesStats;


// MovePicker class is used to pick one pseudo legal move at a time from the
// current position. The most important method is next_move(), which returns a
// new pseudo legal move each time it is called, until there are no moves left,
// when MOVE_NONE is returned. In order to improve the efficiency of the alpha
// beta algorithm, MovePicker attempts to return the moves which are most likely
// to get a cut-off first.
class MovePicker
{

private:

    template<MoveGenerator::GType>
    // value() assign a numerical move ordering score to each move in a move list.
    // The moves with highest scores will be picked first.
    void value ();

    void generate_next ();

    const Position     &pos;

    const HistoryStats &history;

    Searcher::Stack    *ss;

    ValMove             killers[6];
    Move               *counter_moves
        ,              *followup_moves;

    Move                tt_move;
    Depth               depth;

    Square              recapture_sq;
    int32_t             capture_threshold;

    uint8_t             stage;

    ValMove             mlist[MAX_MOVES];
    ValMove            *cur
        ,              *end
        ,              *end_quiets
        ,              *end_bad_captures;

    MovePicker& operator= (const MovePicker &); // Silence a warning under MSVC

public:

    MovePicker (const Position &, Move,        const HistoryStats &, PType);
    MovePicker (const Position &, Move, Depth, const HistoryStats &, Square);
    MovePicker (const Position &, Move, Depth, const HistoryStats &, Move[], Move[], Searcher::Stack[]);

    template<bool SpNode>
    Move next_move ();

};

#endif
