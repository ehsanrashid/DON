#include "MovePicker.h"

#include "Position.h"
#include "Thread.h"

using namespace std;
using namespace Searcher;
using namespace MoveGenerator;

// Constructors of the MovePicker class. As arguments we pass information
// to help it to return the (presumably) good moves first, to decide which
// moves to return (in the quiescence search, for instance, we only want to
// search captures, promotions and some checks) and about how important good
// move ordering is at the current node.

MovePicker::MovePicker (const Position &p, const HistoryStats &h, Move ttm, Depth d, Move *cm, Move *fm, Stack *s)
    : cur (m_list)
    , end (m_list)
    , pos (p)
    , history (h)
    , ss (s)
    , counter_moves (cm)
    , followup_moves (fm)
    , depth (d)
{
    ASSERT (d > DEPTH_ZERO);

    bad_captures_end = m_list+MAX_MOVES-1;

    stage = pos.checkers () ? EVASIONS : MAIN_STAGE;

    tt_move = (ttm && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE);
    end += (tt_move != MOVE_NONE);
}

MovePicker::MovePicker (const Position &p, const HistoryStats &h, Move ttm, Depth d, Square sq)
    : cur (m_list)
    , end (m_list)
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
        stage = EVASIONS;
    }
    else if (d > DEPTH_QS_NO_CHECKS)
    {
        stage = QSEARCH_0;
    }
    else if (d > DEPTH_QS_RECAPTURES)
    {
        stage = QSEARCH_1;

        // Skip TT move if is not a capture or a promotion, this avoids search_quien
        // tree explosion due to a possible perpetual check or similar rare cases
        // when TT table is full.
        if (ttm && !pos.capture_or_promotion (ttm))
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

    tt_move = (ttm && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE);
    end += (tt_move != MOVE_NONE);
}

MovePicker::MovePicker (const Position &p, const HistoryStats &h, Move ttm,          PieceT pt)
    : cur (m_list)
    , end (m_list)
    , pos (p)
    , history (h)
    , ss (NULL)
    , counter_moves (NULL)
    , followup_moves (NULL)
    , depth (DEPTH_ZERO)
{
    ASSERT (!pos.checkers ());

    stage = PROBCUT;

    // In ProbCut we generate only captures better than parent's captured piece
    capture_threshold = PieceValue[MG][pt];

    tt_move = (ttm && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE);
    if (tt_move && (!pos.capture (tt_move) || pos.see (tt_move) <= capture_threshold))
    {
        tt_move = MOVE_NONE;
    }
    end += (tt_move != MOVE_NONE);
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
    // In main search we want to push captures with negative SEE values to
    // bad_captures[] array, but instead of doing it now we delay till when
    // the move has been picked up in pick_move(), this way we save
    // some SEE calls in case we get a cutoff (idea from Pablo Vazquez).
    for (ValMove *itr = m_list; itr != end; ++itr)
    {
        Move m = itr->move;
        itr->value = PieceValue[MG][_ptype (pos[dst_sq (m)])] - _ptype (pos[org_sq (m)]);

        MoveT mt = mtype (m);
        if      (PROMOTE == mt)
        {
            itr->value += PieceValue[MG][prom_type (m)] - PieceValue[MG][PAWN];
        }
        else if (ENPASSANT == mt)
        {
            itr->value += PieceValue[MG][PAWN];
        }
    }
}

template<>
void MovePicker::value<QUIET>   ()
{
    for (ValMove *itr = m_list; itr != end; ++itr)
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
    for (ValMove *itr = m_list; itr != end; ++itr)
    {
        Move m = itr->move;

        Value gain_value = pos.see_sign (m);
        if      (gain_value < VALUE_ZERO)
        {
            itr->value = gain_value - VALUE_KNOWN_WIN; // At the bottom
        }
        else
        {
            if (pos.capture (m))
            {
                itr->value = PieceValue[MG][_ptype (pos[dst_sq (m)])]
                - _ptype (pos[org_sq (m)]) + VALUE_KNOWN_WIN;
            }
            else
            {
                itr->value = history[pos[org_sq (m)]][dst_sq (m)];
            }
        }
    }
}

// generate_next_stage() generates, scores and sorts the next bunch of moves,
// when there are no more moves to try for the current stage.
void MovePicker::generate_next_stage ()
{
    cur = m_list;

    switch (++stage)
    {

    case CAPTURES_S1:
    case CAPTURES_S3:
    case CAPTURES_S4:
    case CAPTURES_S5:
    case CAPTURES_S6:
        end = generate<CAPTURE> (m_list, pos);
        if (m_list < end-1)
        {
            value<CAPTURE> ();
        }
        return;

    case KILLERS_S1:
        // Killer moves usually come right after after the hash move and (good) captures
        cur = end = killers;

        killers[0].move =           //killer[0];
        killers[1].move =           //killer[1];
        killers[2].move =           //counter_moves[0]
        killers[3].move =           //counter_moves[1]
        killers[4].move =           //followup_moves[0]
        killers[5].move = MOVE_NONE;//followup_moves[1]

        // Be sure killer moves are not MOVE_NONE
        for (int32_t i = 0; i < 2; ++i)
        {
            if (ss->killers[i])
            {
                (end++)->move = ss->killers[i];
            }
        }
        //// If killer moves are same
        //if (ss->killers[1] && ss->killers[1] == ss->killers[0]) // Due to SMP races
        //{
        //    (--end)->move = MOVE_NONE;
        //}

        // Be sure counter moves are not MOVE_NONE & different from killer moves
        for (int32_t i = 0; i < 2; ++i)
        {
            if (counter_moves[i] &&
                counter_moves[i] != cur[0].move &&
                counter_moves[i] != cur[1].move)
            {
                (end++)->move = counter_moves[i];
            }
        }
        //// If counter moves are same
        //if (counter_moves[1] && counter_moves[1] == counter_moves[0]) // Due to SMP races
        //{
        //    (--end)->move = MOVE_NONE;
        //}

        // Be sure followup moves are not MOVE_NONE & different from killers and countermoves
        for (int32_t i = 0; i < 2; ++i)
        {
            if (followup_moves[i] &&
                followup_moves[i] != cur[0].move &&
                followup_moves[i] != cur[1].move &&
                followup_moves[i] != cur[2].move &&
                followup_moves[i] != cur[3].move)
            {
                (end++)->move = followup_moves[i];
            }
        }
        //// If followup moves are same
        //if (followup_moves[1] && followup_moves[1] == followup_moves[0]) // Due to SMP races
        //{
        //    (--end)->move = MOVE_NONE;
        //}

        return;

    case QUIETS_1_S1:
        end = quiets_end = generate<QUIET> (m_list, pos);
        if (m_list < end)
        {
            value<QUIET> ();
            end = partition (cur, end, ValMove ());
            if (m_list < end-1)
            {
                insertion_sort ();
            }
        }
        return;

    case QUIETS_2_S1:
        cur = end;
        end = quiets_end;
        if (depth >= 3 * ONE_MOVE)
        {
            insertion_sort ();
        }
        return;

    case BAD_CAPTURES_S1:
        // Just pick them in reverse order to get MVV/LVA ordering
        cur = m_list+MAX_MOVES-1;
        end = bad_captures_end;
        return;

    case EVASIONS_S2:
        end = generate<EVASION> (m_list, pos);
        if (m_list < end-1)
        {
            value<EVASION> ();
        }
        return;

    case QUIET_CHECKS_S3:
        end = generate<QUIET_CHECK> (m_list, pos);
        return;

    case EVASIONS:
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

        case MAIN_STAGE:
        case EVASIONS:
        case QSEARCH_0:
        case QSEARCH_1:
        case PROBCUT:
            ++cur;
            return tt_move;

        case CAPTURES_S1:
            do
            {
                pick_best ();
                move = (cur++)->move;
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

        case KILLERS_S1:
            do
            {
                move = (cur++)->move;
                if (    move != MOVE_NONE
                    &&  pos.pseudo_legal (move)
                    &&  move != tt_move
                    && !pos.capture (move))
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case QUIETS_1_S1:
        case QUIETS_2_S1:
            do
            {
                move = (cur++)->move;
                if (   move != tt_move
                    && move != killers[0].move
                    && move != killers[1].move
                    && move != killers[2].move
                    && move != killers[3].move
                    && move != killers[4].move
                    && move != killers[5].move)
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case BAD_CAPTURES_S1:
            return (cur--)->move;

        case EVASIONS_S2:
        case CAPTURES_S3:
        case CAPTURES_S4:
            do
            {
                pick_best ();
                move = (cur++)->move;
                if (move != tt_move)
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case CAPTURES_S5:
            do
            {
                pick_best ();
                move = (cur++)->move;
                if (move != tt_move && pos.see (move) > capture_threshold)
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case CAPTURES_S6:
            do
            {
                pick_best ();
                move = (cur++)->move;
                if (dst_sq (move) == recapture_sq)
                {
                    return move;
                }
            }
            while (cur < end);
            break;

        case QUIET_CHECKS_S3:
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
// Version of next_move() to use at split point nodes where the move is grabbed
// from the split point's shared MovePicker object. This function is not thread
// safe so must be lock protected by the caller.
Move MovePicker::next_move<true> ()
{
    return ss->split_point->move_picker->next_move<false> ();
}
