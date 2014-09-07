#ifndef _MOVE_PICKER_H_INC_
#define _MOVE_PICKER_H_INC_

#include "Type.h"
#include "MoveGenerator.h"
#include "Searcher.h"

class Position;

namespace MovePick {

    using namespace MoveGen;

    // The Stats struct stores moves statistics.

    // Gain records the move's best evaluation gain from one ply to the next and is used
    // for pruning decisions.
    // Entries are stored according only to moving piece and destination square,
    // in particular two moves with different origin but same destination and same piece will be considered identical.
    struct GainStats
    {

    private:
        Value _values[TOT_PIECE][SQ_NO];

    public:

        inline const Value* operator[] (Piece p) const { return _values[p]; }

        inline void clear ()
        {
            std::fill (*_values, *_values + sizeof (_values) / sizeof (**_values), VALUE_ZERO);
        }

        inline void update (const Position &pos, Move m, Value g)
        {
            Square s = dst_sq (m);
            Piece p  = pos[s];

            _values[p][s] = std::max (g, _values[p][s]-1);
        }
    };

    // History records how often different moves have been successful or unsuccessful during the
    // current search and is used for reduction and move ordering decisions.
    // Entries are stored according only to moving piece and destination square,
    // in particular two moves with different origin but same destination and same piece will be considered identical.
    struct HistoryStats
    {

    private:
        i16   _counts[TOT_PIECE][SQ_NO][2];
        Value _values[TOT_PIECE][SQ_NO];

    public:
    
        static const Value MinValue = Value(-2001);
        static const Value MaxValue = Value(+2000);
    
        inline void clear ()
        {
            std::fill (**_counts, **_counts + sizeof (_counts) / sizeof (***_counts), 0x00);
            std::fill (*_values, *_values + sizeof (_values) / sizeof (**_values), MinValue);
        }

        inline void success (const Position &pos, Move m, i16 d)
        {
            Piece p     = pos[org_sq (m)];
            Square s    = dst_sq (m);

            i16 cnt = _counts[p][s][0] + d*d;
            if (cnt > 2000)
            {
                cnt /= 2;
                _counts[p][s][1] /= 2;
            }
            _counts[p][s][0] = cnt;
            _values[p][s] = MinValue;
        }

        inline void failure (const Position &pos, Move m, i16 d)
        {
            Piece p     = pos[org_sq (m)];
            Square s    = dst_sq (m);

            i16 cnt = _counts[p][s][1] + d*d;
            if (cnt > 2000)
            {
                cnt /= 2;
                _counts[p][s][0] /= 2;
            }
            _counts[p][s][1] = cnt;
            _values[p][s] = MinValue;
        }

        inline Value value (Piece p, Square s)
        {
            if (_values[p][s] <= MinValue)
            {
                i16 succ = _counts[p][s][0];
                i16 fail = _counts[p][s][1];
                _values[p][s] = (succ + fail > 0) ? MaxValue * i32((succ) / (succ+fail)) : VALUE_ZERO;
            }

            return _values[p][s];
        }

    };

    // CounterMoveStats & FollowupMoveStats store the move that refute a previous one.
    // Entries are stored according only to moving piece and destination square,
    // in particular two moves with different origin but same destination and same piece will be considered identical.
    struct MoveStats
    {

    private:
        Move _moves[TOT_PIECE][SQ_NO][2];

    public:

        inline void clear ()
        {
            std::fill (**_moves, **_moves + sizeof (_moves) / sizeof (***_moves), MOVE_NONE);
        }

        inline void update (const Position &pos, Move m1, Move m2)
        {
            Square s = dst_sq (m1);
            Piece p  = pos[s];
            if (m2 != _moves[p][s][0])
            {
                _moves[p][s][1] = _moves[p][s][0];
                _moves[p][s][0] = m2;
            }
        }

        inline Move* moves (const Position &pos, Square s)
        {
            return _moves[pos[s]][s];
        }
    };


    // MovePicker class is used to pick one pseudo legal move at a time from the
    // current position. The most important method is next_move(), which returns a
    // new pseudo legal move each time it is called, until there are no moves left,
    // when MOVE_NONE is returned. In order to improve the efficiency of the alpha
    // beta algorithm, MovePicker attempts to return the moves which are most likely
    // to get a cut-off first.
    class MovePicker
    {

    private:

        ValMove  moves[MAX_MOVES]
            ,   *cur
            ,   *end
            ,   *quiets_end
            ,   *bad_captures_end;

        const Position &pos;
        HistoryStats &history;

        Search::Stack *ss;

        Move   killers[6]
            ,  *counter_moves
            ,  *followup_moves
            ,  *kcur
            ,  *kend;
        Bitboard killers_org
            ,     killers_dst;
        u08     killers_size;

        Move    tt_move;
        Depth   depth;

        Square  recapture_sq;

        Value   capture_threshold;

        u08     stage;

        MovePicker& operator= (const MovePicker &); // Silence a warning under MSVC

        template<MoveGen::GenT>
        // value() assign a numerical move ordering score to each move in a move list.
        // The moves with highest scores will be picked first.
        void value ();

        void generate_next_stage ();


    public:

        MovePicker (const Position&, HistoryStats&, Move, Depth, Move*, Move*, Search::Stack*);
        MovePicker (const Position&, HistoryStats&, Move, Depth, Square);
        MovePicker (const Position&, HistoryStats&, Move,        PieceT);

        template<bool SPNode>
        Move next_move ();

    };

}

#endif // _MOVE_PICKER_H_INC_
