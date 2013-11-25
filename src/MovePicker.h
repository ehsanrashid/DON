//#pragma once
#ifndef MOVEPICKER_H_
#define MOVEPICKER_H_

#include <vector>
#include <set>
#include "Type.h"
#include "Searcher.h"
#include "MoveGenerator.h"

class Position;

typedef struct ValMove
{
    Move    move;
    Value   value;

    friend bool operator<  (const ValMove &vm1, const ValMove &vm2) { return (vm1.value <  vm2.value); }
    friend bool operator>  (const ValMove &vm1, const ValMove &vm2) { return (vm1.value >  vm2.value); }
    friend bool operator<= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value <= vm2.value); }
    friend bool operator>= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value >= vm2.value); }
    friend bool operator== (const ValMove &vm1, const ValMove &vm2) { return (vm1.value == vm2.value); }
    friend bool operator!= (const ValMove &vm1, const ValMove &vm2) { return (vm1.value != vm2.value); }

} ValMove;


typedef std::vector<ValMove>     ValMoveList;
typedef std::set<ValMove>        ValMoveSet;

extern void order (ValMoveList &vm_list, bool full = true);


template<bool Gain, typename T>
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
    T _table[PT_NO][SQ_NO];

public:
    static const Value MaxValue = Value (2000);

    const T* operator[] (Piece p) const { return &_table[p][0]; }
    //const T& operator[] (Piece p) const { return  _table[p][0]; }

    void clear ()
    {
        std::memset (_table, 0, sizeof (_table));
    }

    void update (Piece p, Square s, Move m)
    {
        if (m == _table[p][s].first) return;

        _table[p][s].second = _table[p][s].first;
        _table[p][s].first = m;
    }

    void update (Piece p, Square s, Value v)
    {
        if (Gain)
        {
            _table[p][s] = std::max(v, _table[p][s] - 1);
        }
        else if (abs (_table[p][s] + v) < MaxValue)
        {
            _table[p][s] += v;
        }
    }

};

typedef Stats< true, Value> GainsStats;
typedef Stats<false, Value> HistoryStats;
typedef Stats<false, std::pair<Move, Move> > CountermovesStats;


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
    //value() assign a numerical move ordering score to each move in a move list.
    //The moves with highest scores will be picked first.
    void value();

    void generate_next();

    const Position     &pos;
    const HistoryStats &history;
    Searcher::Stack    *ss;
    Move               *counter_moves;

    Move                tt_move;
    Depth               depth;

    ValMove             killers[4];
    Square              recapture_sq;
    int32_t             capture_threshold;
    int32_t             stage;
    ValMove            *cur;
    ValMove            *end;
    ValMove            *end_quiets;
    ValMove            *end_bad_captures;
    ValMove             moves[MAX_MOVES];

    MovePicker& operator= (const MovePicker &); // Silence a warning under MSVC

public:

    MovePicker(const Position &, Move, Depth, const HistoryStats &, Square);
    MovePicker(const Position &, Move, const HistoryStats &, PType);
    MovePicker(const Position &, Move, Depth, const HistoryStats &, Move*, Searcher::Stack*);

    template<bool SpNode>
    Move next_move();

};



#endif