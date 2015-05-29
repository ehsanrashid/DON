#ifndef _MOVE_PICKER_H_INC_
#define _MOVE_PICKER_H_INC_

#include "Type.h"
#include "Position.h"
#include "MoveGenerator.h"
#include "Searcher.h"

namespace MovePick {

    using namespace MoveGen;

    // The Stats struct stores different statistics.
    template<typename T>
    struct Stats
    {
    private:
        T _table[PIECE_NO][SQ_NO];

    public:
        static const Value MaxValue = Value(+0x100);

        const T* operator[] (Piece p) const { return _table[p]; }
        T*       operator[] (Piece p)       { return _table[p]; }

        void clear() { std::memset (_table, 0x0, sizeof (_table)); }

        void update (const Position &pos, Move m, Value v)
        {
            Square s = dst_sq (m);
            Piece  p = pos[org_sq (m)];
            if (abs (_table[p][s] + v) < MaxValue) _table[p][s] += v;
        }
        
        void update (const Position &pos, Move m1, Move m2)
        {
            Square s = dst_sq (m1);
            Piece  p = pos[s];
            if (_table[p][s] != m2)
            {
                _table[p][s] = m2;
            }
        }
    };

    // ValueStats stores the value that records how often different moves have been successful/unsuccessful
    // during the current search and is used for reduction and move ordering decisions.
    typedef Stats<Value>        ValueStats;

    typedef Stats<ValueStats>   ValueValueStats;

    // MoveStats store the move that refute a previous move.
    // Entries are stored according only to moving piece and destination square,
    // in particular two moves with different origin but same piece & same destination
    // will be considered identical.
    typedef Stats<Move>         MoveStats;

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
            ,   *_moves_cur
            ,   *_moves_end
            ,   *_quiets_end
            ,   *_bad_captures_end;

        const Position          &_pos;
        const ValueStats        &_history_value;
        const ValueValueStats   &_countermoves_history_value;
        
        Searcher::Stack *_ss;
        
        Move    _tt_move;
        Move    _counter_move;

        ValMove _killers[3];

        Depth   _depth;

        Square  _recapture_sq;

        Value   _capture_threshold;

        u08     _stage;

        template<GenT GT>
        // value() assign a numerical move ordering score to each move in a move list.
        // The moves with highest scores will be picked first.
        void value ();

        void generate_next_stage ();

    public:
        MovePicker (const MovePicker&) = delete;
        MovePicker& operator= (const MovePicker&) = delete;

        MovePicker (const Position&, const ValueStats&, const ValueValueStats&, Move, Depth, Move, Searcher::Stack*);
        MovePicker (const Position&, const ValueStats&, const ValueValueStats&, Move, Depth, Square);
        MovePicker (const Position&, const ValueStats&, const ValueValueStats&, Move, PieceT);

        ValMove* begin () { return _moves_beg; }
        ValMove* end   () { return _moves_end; }

        template<bool SPNode>
        Move next_move ();

    };

}

#endif // _MOVE_PICKER_H_INC_
