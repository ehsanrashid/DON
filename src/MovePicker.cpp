#include "MovePicker.h"

#include "Thread.h"

using namespace std;
using namespace Searcher;
using namespace MoveGenerator;

namespace {

    enum StageT : u08
    {
        MAIN_S    , CAPTURE_S1, KILLER_S1, QUIET_1_S1, QUIET_2_S1, BAD_CAPTURE_S1,
        EVASION_S1, EVASION_S2,
        QSEARCH_0 , CAPTURE_S3, QUIET_CHECK_S3,
        QSEARCH_1 , CAPTURE_S4,
        PROBCUT   , CAPTURE_S5,
        RECAPTURE , CAPTURE_S6,
        STOP
    };

    // Our insertion sort in the range [begin, end], guaranteed to be stable, as is needed
    inline void insertion_sort (ValMove* begin, ValMove* end)
    {
        for (ValMove *p = begin+1; p < end; ++p)
        {
            ValMove t = *p, *q;
            for (q = p; q != begin && *(q-1) < t; --q)
            {
                *q = *(q-1);
            }
            *q = t;
        }
    }

    // Picks and moves to the front the best move in the range [begin, end],
    // it is faster than sorting all the moves in advance when moves are few, as
    // normally are the possible captures.
    inline ValMove* pick_best (ValMove* begin, ValMove* end)
    {
        std::swap (*begin, *std::max_element (begin, end));
        return begin;
    }

}

// Constructors of the MovePicker class. As arguments pass information
// to help it to return the (presumably) good moves first, to decide which
// moves to return (in the quiescence search, for instance, only want to
// search captures, promotions and some checks) and about how important good
// move ordering is at the current node.

MovePicker::MovePicker (const Position &p, const HistoryStats &h, Move ttm, Depth d, Move *cm, Move *fm, Stack *s)
    : cur (moves)
    , end (moves)
    , pos (p)
    , history (h)
    , ss (s)
    , counter_moves (cm)
    , followup_moves (fm)
    , depth (d)
{
    ASSERT (d > DEPTH_ZERO);

    bad_captures_end = moves+MAX_MOVES-1;

    stage = (pos.checkers () ? EVASION_S1 : MAIN_S);

    tt_move = ttm != MOVE_NONE && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE;
    end += tt_move != MOVE_NONE;
}

MovePicker::MovePicker (const Position &p, const HistoryStats &h, Move ttm, Depth d, Square sq)
    : cur (moves)
    , end (moves)
    , pos (p)
    , history (h)
    , ss (NULL)
    , counter_moves (NULL)
    , followup_moves (NULL)
    , depth (d)
{
    ASSERT (d <= DEPTH_ZERO);

    if (pos.checkers ())
    {
        stage = EVASION_S1;
    }
    else
    if (d > DEPTH_QS_NO_CHECKS)
    {
        stage = QSEARCH_0;
    }
    else
    if (d > DEPTH_QS_RECAPTURES)
    {
        stage = QSEARCH_1;

        // Skip TT move if is not a capture or a promotion, this avoids search_quien
        // tree explosion due to a possible perpetual check or similar rare cases
        // when TT table is full.
        if (  ttm != MOVE_NONE
           && !pos.capture_or_promotion (ttm)
           )
        {
            ttm = MOVE_NONE;
        }
    }
    else
    {
        stage = RECAPTURE;
        recapture_sq = sq;
        ttm = MOVE_NONE;
    }

    tt_move = ttm != MOVE_NONE && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE;
    end += tt_move != MOVE_NONE;
}

MovePicker::MovePicker (const Position &p, const HistoryStats &h, Move ttm,          PieceT pt)
    : cur (moves)
    , end (moves)
    , pos (p)
    , history (h)
    , ss (NULL)
    , counter_moves (NULL)
    , followup_moves (NULL)
    , depth (DEPTH_ZERO)
{
    ASSERT (!pos.checkers ());

    stage = PROBCUT;

    // In ProbCut generate only captures better than parent's captured piece
    capture_threshold = PieceValue[MG][pt];

    tt_move = ttm != MOVE_NONE && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE;
    if (  tt_move != MOVE_NONE
       && (!pos.capture (tt_move) || pos.see (tt_move) <= capture_threshold)
       )
    {
        tt_move = MOVE_NONE;
    }
    end += tt_move != MOVE_NONE;
}


// value() assign a numerical move ordering score to each move in a move list.
// The moves with highest scores will be picked first.

template<>
void MovePicker::value<CAPTURE> ()
{
    // Winning and equal captures in the main search are ordered by MVV/LVA.
    // Suprisingly, this appears to perform slightly better than SEE based
    // move ordering. The reason is probably that in a position with a winning
    // capture, capturing a more valuable (but sufficiently defended) piece
    // first usually doesn't hurt. The opponent will have to recapture, and
    // the hanging piece will still be hanging (except in the unusual cases
    // where it is possible to recapture with the hanging piece). Exchanging
    // big pieces before capturing a hanging piece probably helps to reduce
    // the subtree size.
    // In main search want to push captures with negative SEE values to
    // bad_captures[] array, but instead of doing it now delay till when
    // the move has been picked up in pick_move(), this way save
    // some SEE calls in case get a cutoff (idea from Pablo Vazquez).
    for (ValMove *itr = moves; itr != end; ++itr)
    {
        Move m = itr->move;

        MoveT mt = mtype (m);
        if (mt == NORMAL)
        {
            itr->value = PieceValue[MG][ptype (pos[dst_sq (m)])] - i32(ptype (pos[org_sq (m)]));
        }
        else
        if (mt == ENPASSANT)
        {
            itr->value = PieceValue[MG][PAWN];
        }
        else
        if (mt == PROMOTE)
        {
            itr->value = PieceValue[MG][ptype (pos[dst_sq (m)])] + PieceValue[MG][promote (m)] - PieceValue[MG][PAWN];
        }
    }
}

template<>
void MovePicker::value<QUIET>   ()
{
    for (ValMove *itr = moves; itr != end; ++itr)
    {
        Move m = itr->move;
        itr->value = history[pos[org_sq (m)]][dst_sq (m)];
    }
}

template<>
void MovePicker::value<EVASION> ()
{
    // Try good captures ordered by MVV/LVA, then non-captures if destination square
    // is not under attack, ordered by history value, then bad-captures and quiet
    // moves with a negative SEE. This last group is ordered by the SEE value.
    for (ValMove *itr = moves; itr != end; ++itr)
    {
        Move m = itr->move;

        Value gain_value = pos.see_sign (m);
        if (gain_value < VALUE_ZERO)
        {
            itr->value = gain_value - MaxHistory; // At the bottom
        }
        else
        if (pos.capture (m))
        {
            MoveT mt = mtype (m);
            if (mt == NORMAL)
            {
                itr->value = PieceValue[MG][ptype (pos[dst_sq (m)])] - i32(ptype (pos[org_sq (m)])) + MaxHistory;
            }
            else
            if (mt == ENPASSANT)
            {
                itr->value = PieceValue[MG][PAWN] + MaxHistory;
            }
            else
            if (mt == PROMOTE)
            {
                itr->value = PieceValue[MG][ptype (pos[dst_sq (m)])] + PieceValue[MG][promote (m)] - PieceValue[MG][PAWN] + MaxHistory;
            }
        }
        else
        {
            itr->value = history[pos[org_sq (m)]][dst_sq (m)];
        }
    }
}

// generate_next_stage() generates, scores and sorts the next bunch of moves,
// when there are no more moves to try for the current stage.
void MovePicker::generate_next_stage ()
{
    cur = moves;

    switch (++stage)
    {

    case CAPTURE_S1:
    case CAPTURE_S3:
    case CAPTURE_S4:
    case CAPTURE_S5:
    case CAPTURE_S6:
        end = generate<CAPTURE> (moves, pos);
        if (cur < end-1)
        {
            value<CAPTURE> ();
        }
        return;

    case KILLER_S1:
        // Killer moves usually come right after after the hash move and (good) captures
        killers[0].move = ss->killer_moves[0];
        killers[1].move = (ss->killer_moves[0] != ss->killer_moves[1]) ? ss->killer_moves[1] : MOVE_NONE;
        killers[2].move =           //counter_moves[0]
        killers[3].move =           //counter_moves[1]
        killers[4].move =           //followup_moves[0]
        killers[5].move = MOVE_NONE;//followup_moves[1]
        
        cur = killers;
        end = cur + 2;
        Move m;
        // Be sure counter moves are not MOVE_NONE & different from killer moves
        for (i08 i = 0; i < 2; ++i)
        {
            m = counter_moves[i];
            if (  m != MOVE_NONE
               && m != cur[0].move
               && m != cur[1].move
               && m != cur[2].move
               )
            {
                (end++)->move = m;
            }
        }

        // Be sure followup moves are not MOVE_NONE & different from killer & counter moves
        for (i08 i = 0; i < 2; ++i)
        {
            m = followup_moves[i];
            if (  m != MOVE_NONE
               && m != cur[0].move
               && m != cur[1].move
               && m != cur[2].move
               && m != cur[3].move
               && m != cur[4].move
               )
            {
                (end++)->move = m;
            }
        }

        return;

    case QUIET_1_S1:
        end = quiets_end = generate<QUIET> (moves, pos);
        if (cur < end)
        {
            value<QUIET> ();
            end = partition (cur, end, ValMove ());
            if (cur < end-1)
            {
                insertion_sort (cur, end);
            }
        }
        return;

    case QUIET_2_S1:
        cur = end;
        end = quiets_end;
        if (depth >= (3*ONE_MOVE))
        {
            insertion_sort (cur, end);
        }
        return;

    case BAD_CAPTURE_S1:
        // Just pick them in reverse order to get MVV/LVA ordering
        cur = moves+MAX_MOVES-1;
        end = bad_captures_end;
        
        return;

    case EVASION_S2:
        end = generate<EVASION> (moves, pos);
        if (cur < end-1)
        {
            value<EVASION> ();
        }
        return;

    case QUIET_CHECK_S3:
        end = generate<QUIET_CHECK> (moves, pos);
        return;

    case EVASION_S1:
    case QSEARCH_0:
    case QSEARCH_1:
    case PROBCUT:
    case RECAPTURE:
        stage = STOP;

    case STOP:
        end = cur+1; // Avoid another generate_next_stage() call
        return;

    default:
        ASSERT (false);
    }
}

template<>
// next_move() is the most important method of the MovePicker class. It returns
// a new pseudo legal move every time is called, until there are no more moves
// left. It picks the move with the biggest score from a list of generated moves
// taking care not to return the tt_move if has already been searched previously.
Move MovePicker::next_move<false> ()
{
    do
    {
        Move move;
        while (cur == end)
        {
            generate_next_stage ();
        }

        switch (stage)
        {

        case MAIN_S:
        case EVASION_S1:
        case QSEARCH_0:
        case QSEARCH_1:
        case PROBCUT:
            ++cur;
            return tt_move;

        case CAPTURE_S1:
            do
            {
                move = pick_best (cur++, end)->move;
                if (move != tt_move)
                {
                    if (pos.see_sign (move) >= VALUE_ZERO)
                    {
                        return move;
                    }
                    // Losing capture, move it to the tail of the array
                    (bad_captures_end--)->move = move;
                }
            }
            while (cur < end);
            break;

        case KILLER_S1:
            do
            {
                move = (cur++)->move;
                if (  move != MOVE_NONE
                   && move != tt_move
                   && pos.pseudo_legal (move)
                   && !pos.capture (move)
                   )
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case QUIET_1_S1:
        case QUIET_2_S1:
            do
            {
                move = (cur++)->move;
                if (  move != tt_move
                   && move != killers[0].move
                   && move != killers[1].move
                   && move != killers[2].move
                   && move != killers[3].move
                   && move != killers[4].move
                   && move != killers[5].move
                   )
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case BAD_CAPTURE_S1:
            return (cur--)->move;

        case EVASION_S2:
        case CAPTURE_S3:
        case CAPTURE_S4:
            do
            {
                move = pick_best (cur++, end)->move;
                if (move != tt_move)
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case CAPTURE_S5:
            do
            {
                move = pick_best (cur++, end)->move;
                if (move != tt_move && pos.see (move) > capture_threshold)
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case CAPTURE_S6:
            do
            {
                move = pick_best (cur++, end)->move;
                if (recapture_sq == dst_sq (move))
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case QUIET_CHECK_S3:
            do
            {
                move = (cur++)->move;
                if (move != tt_move)
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case STOP:
            return MOVE_NONE;

        default:
            ASSERT (false);
        }
    }
    while (true); // (stage <= STOP)
}

template<>
// Version of next_move() to use at splitpoint nodes where the move is grabbed
// from the splitpoint's shared MovePicker object. This function is not thread
// safe so must be lock protected by the caller.
Move MovePicker::next_move<true> ()
{
    return ss->splitpoint->movepicker->next_move<false> ();
}
