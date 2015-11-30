#ifndef _MOVE_PICKER_H_INC_
#define _MOVE_PICKER_H_INC_

#include "Type.h"
#include "Position.h"
#include "MoveGenerator.h"
#include "Searcher.h"

namespace MovePick {

    using namespace MoveGen;
    using namespace Searcher;

    const Value MAX_STATS_VALUE = Value(1 << 28);

    // The Stats struct stores different statistics.
    template<class T, bool CM = false>
    struct Stats
    {
    private:
        T _table[PIECE_NO][SQ_NO];

        //void _clear (Value &v) { std::memset (_table, 0x0, sizeof (_table)); }
        void _clear (Value &v) { v = VALUE_ZERO; }
        void _clear (Stats<Value, false> &vs) { vs.clear (); }
        void _clear (Stats<Value, true > &vs) { vs.clear (); }
        void _clear (Move  &m) { m = MOVE_NONE; }

    public:

        const T* operator[] (Piece  pc) const { return _table[pc]; }
        T*       operator[] (Piece  pc)       { return _table[pc]; }

        void clear ()
        {
            for (auto &t : _table)
            {
                for (auto &e : t)
                {
                    _clear (e);
                }
            }
        }
        // Piece, destiny square, value
        void update (Piece p, Square s, Value v)
        {
            if (abs (i32(v)) < 324)
            {
                auto &e = _table[p][s];
                e = std::min (std::max (e*(1.0 - (double)abs (i32(v)) / (CM ? 512 : 324)) + i32(v)*(CM ? 64 : 32), -MAX_STATS_VALUE), +MAX_STATS_VALUE);
            }
        }
        // Piece, destiny square, move
        void update (Piece p, Square s, Move m)
        {
            auto &e = _table[p][s];
            if (e != m) e = m;
        }

    };

    // ValueStats stores the value that records how often different moves have been successful/unsuccessful
    // during the current search and is used for reduction and move ordering decisions.
    typedef Stats<Value, false>     HValueStats;
    typedef Stats<Value, true >     CMValueStats;

    // CMValue2DStats
    typedef Stats<CMValueStats>     CMValue2DStats;

    // MoveStats store the move that refute a previous move.
    // Entries are stored according only to moving piece and destination square,
    // in particular two moves with different origin but same piece and same destination
    // will be considered identical.
    typedef Stats<Move>             MoveStats;


    // MovePicker class is used to pick one pseudo legal move at a time from the
    // current position. The most important method is next_move(), which returns a
    // new pseudo legal move each time it is called, until there are no moves left,
    // when MOVE_NONE is returned. In order to improve the efficiency of the
    // alfa-beta algorithm, MovePicker attempts to return the moves which are most
    // likely to get a cut-off first.
    class MovePicker
    {

    private:

        ValMove  _moves_beg[MAX_MOVES]
            ,   *_moves_cur         = _moves_beg
            ,   *_moves_end         = _moves_beg
            ,   *_quiets_end        = _moves_beg
            ,   *_bad_captures_end  = _moves_beg+MAX_MOVES-1;

        const Position      &_pos;
        const HValueStats   &_history_values;
        const CMValueStats  *_counter_moves_values = nullptr;
        const Stack         *_ss    = nullptr;

        Move    _tt_move        = MOVE_NONE;
        Move    _counter_move   = MOVE_NONE;
        Depth   _depth          = DEPTH_ZERO;
        Square  _recapture_sq   = SQ_NO;
        Value   _threshold      = VALUE_NONE;

        ValMove _killers[3];

        u08     _stage;

        template<GenT GT>
        // value() assign a numerical move ordering score to each move in a move list.
        // The moves with highest scores will be picked first.
        void value ();

        void generate_next_stage ();

    public:

        MovePicker () = delete;
        MovePicker (const MovePicker&) = delete;
        MovePicker& operator= (const MovePicker&) = delete;

        MovePicker (const Position&, const HValueStats&, const CMValueStats&, Move, Depth, Move, const Stack*);
        MovePicker (const Position&, const HValueStats&, Move, Depth, Square);
        MovePicker (const Position&, const HValueStats&, Move, Value);

        ValMove* begin () { return _moves_beg; }
        ValMove* end   () { return _moves_end; }

        Move next_move ();

    };

}

#endif // _MOVE_PICKER_H_INC_
