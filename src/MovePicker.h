#ifndef _MOVE_PICKER_H_INC_
#define _MOVE_PICKER_H_INC_

#include "Type.h"
#include "Position.h"
#include "MoveGenerator.h"
#include "Searcher.h"

namespace MovePick {

    using namespace MoveGen;
    using namespace Searcher;

    const Value MAX_STATS_VALUE = Value(0x100);

    // The Stats struct stores different statistics.
    template<class T, int N = PIECE_NO>
    struct Stats
    {
    private:
        T _table[N][SQ_NO];

        void _clear (Value &v) { v = VALUE_ZERO; }
        void _clear (Move  &m) { m = MOVE_NONE; }
        void _clear (Stats<Value> &vs) { vs.clear (); }

        void _age (Value &v) { v *= 0.75; }
        void _age (Stats<Value> &vs) { vs.age (); }

    public:

        const T* operator[] (Piece  pc) const { return _table[pc]; }
        T*       operator[] (Piece  pc)       { return _table[pc]; }
        const T* operator[] (PieceT pt) const { return _table[pt]; }
        T*       operator[] (PieceT pt)       { return _table[pt]; }

        void clear ()
        {
            for (auto &t : _table)
                for (auto &e : t)
                    _clear (e);
        }

        // ------

        void age ()
        {
            for (auto &t : _table)
                for (auto &e : t)
                    _age (e);
        }

        void update (const Position &pos, Move m, Value v)
        {
            auto s = dst_sq (m);
            auto p = pos[org_sq (m)];
            auto &e = _table[p][s];
            e = std::min (std::max (e + v, -MAX_STATS_VALUE), +MAX_STATS_VALUE);
        }

        // ------

        void update (const Position &pos, Move m1, Move m2)
        {
            auto s = dst_sq (m1);
            auto p = pos[s];
            auto &e = _table[p][s];
            if (e != m2) e = m2;
        }

        // ------
    };

    // ValueStats stores the value that records how often different moves have been successful/unsuccessful
    // during the current search and is used for reduction and move ordering decisions.
    typedef Stats<Value>            ValueStats;
    // Value2DStats
    typedef Stats<ValueStats, TOTL> Value2DStats;

    // MoveStats store the move that refute a previous move.
    // Entries are stored according only to moving piece and destination square,
    // in particular two moves with different origin but same piece & same destination
    // will be considered identical.
    typedef Stats<Move>             MoveStats;


    // MovePicker class is used to pick one pseudo legal move at a time from the
    // current position. The most important method is next_move(), which returns a
    // new pseudo legal move each time it is called, until there are no moves left,
    // when MOVE_NONE is returned. In order to improve the efficiency of the alpha
    // beta algorithm, MovePicker attempts to return the moves which are most likely
    // to get a cut-off first.
    class MovePicker
    {

    private:

        ValMove  _moves_beg[MAX_MOVES]
            ,   *_moves_cur         = _moves_beg
            ,   *_moves_end         = _moves_beg
            ,   *_quiets_end        = _moves_beg
            ,   *_bad_captures_end  = _moves_beg+MAX_MOVES-1;

        const Position      &_Pos;
        const ValueStats    &_HistoryValues;
        const Value2DStats  &_CounterMovesHistoryValues;

        const Stack  *_ss           = nullptr;

        Move    _tt_move            = MOVE_NONE;
        Move    _counter_move       = MOVE_NONE;
        Depth   _depth              = DEPTH_ZERO;
        Square  _recapture_sq       = SQ_NO;
        Value   _capture_threshold  = VALUE_NONE;

        ValMove _killers[4];

        u08     _stage;

        template<GenT GT>
        // value() assign a numerical move ordering score to each move in a move list.
        // The moves with highest scores will be picked first.
        void value ();

        void generate_next_stage ();

    public:

        MovePicker () = delete;
        MovePicker (const Position&, const ValueStats&, const Value2DStats&, Move, Depth, Move, const Stack*);
        MovePicker (const Position&, const ValueStats&, const Value2DStats&, Move, Depth, Square);
        MovePicker (const Position&, const ValueStats&, const Value2DStats&, Move, PieceT);
        MovePicker (const MovePicker&) = delete;
        MovePicker& operator= (const MovePicker&) = delete;

        ValMove* begin () { return _moves_beg; }
        ValMove* end   () { return _moves_end; }

        template<bool SPNode>
        Move next_move ();

    };

}

#endif // _MOVE_PICKER_H_INC_
