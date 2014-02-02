#include "MovePicker.h"

#include "Thread.h"

using namespace std;
using namespace Searcher;
using namespace MoveGenerator;

namespace {

    enum Stages : uint8_t
    {
        MAIN_STAGE,  CAPTURES_S1, KILLERS_S1, QUIETS_1_S1, QUIETS_2_S1, BAD_CAPTURES_S1,
        EVASIONS,    EVASIONS_S2,
        QSEARCH_0,   CAPTURES_S3, QUIET_CHECKS_S3,
        QSEARCH_1,   CAPTURES_S4,
        PROBCUT,     CAPTURES_S5,
        RECAPTURE,   CAPTURES_S6,
        STOP,
    };

    // Our insertion sort, guaranteed to be stable, as is needed
    inline void insertion_sort (ValMove *beg, ValMove *end)
    {
        for (ValMove *p = beg + 1; p < end; ++p)
        {
            ValMove tmp = *p;
            ValMove *q;
            for (q = p; q != beg && *(q-1) < tmp; --q)
            {
                *q = *(q-1);
            }
            *q = tmp;
        }
    }

    // Picks and moves to the front the best move in the range [beg, end),
    // it is faster than sorting all the moves in advance when moves are few, as
    // normally are the possible captures.
    inline ValMove* pick_best (ValMove *beg, ValMove *end)
    {
        swap (*beg, *max_element (beg, end));
        return beg;
    }
}

// Constructors of the MovePicker class. As arguments we pass information
// to help it to return the (presumably) good moves first, to decide which
// moves to return (in the quiescence search, for instance, we only want to
// search captures, promotions and some checks) and about how important good
// move ordering is at the current node.


MovePicker::MovePicker (const Position &p, Move ttm, Depth d, const HistoryStats &h, Move cm[], Move fm[], Stack s[])
    : pos (p)
    , history (h)
    , depth (d)
    , counter_moves (cm)
    , followup_moves (fm)
    , ss (s)
    , cur (m_list)
    , end (m_list)
{
    ASSERT (d > DEPTH_ZERO);

    end_bad_captures = m_list + MAX_MOVES - 1;

    stage = pos.checkers () ? EVASIONS : MAIN_STAGE;

    tt_move = (ttm && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE);
    end += (tt_move != MOVE_NONE);
}

MovePicker::MovePicker (const Position &p, Move ttm, Depth d, const HistoryStats &h, Square sq)
    : pos (p)
    , history (h)
    , cur (m_list)
    , end (m_list)
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

MovePicker::MovePicker (const Position &p, Move ttm,          const HistoryStats &h, PieceT pt)
    : pos (p)
    , history (h)
    , cur (m_list)
    , end (m_list)
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
        itr->value = PieceValue[MG][_type (pos[dst_sq (m)])] - _type (pos[org_sq (m)]);

        switch (m_type (m))
        {
        case PROMOTE:
            itr->value += PieceValue[MG][prom_type (m)]
            - PieceValue[MG][PAWN];
            break;
        case ENPASSANT:
            itr->value += PieceValue[MG][PAWN];
            break;
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
        int32_t gain = pos.see_sign (m);
        if (gain < 0)
        {
            itr->value = gain - VALUE_KNOWN_WIN; // At the bottom
        }
        else if (pos.capture (m))
        {
            itr->value = PieceValue[MG][_type (pos[dst_sq (m)])]
            - _type (pos[org_sq (m)]) + VALUE_KNOWN_WIN;
        }
        else
        {
            itr->value = history[pos[org_sq (m)]][dst_sq (m)];
        }
    }
}

// generate_next_stage () generates, scores and sorts the next bunch of moves,
// when there are no more moves to try for the current phase.
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
        if (end > cur + 1)
        {   
            value<CAPTURE> ();
            insertion_sort  (cur, end);
        }

        return;

    case KILLERS_S1:
        // Killer moves usually come right after after the hash move and (good) captures
        cur = end = killers;

        killers[0].move = MOVE_NONE; //killer[0];
        killers[1].move = MOVE_NONE; //killer[1];
        killers[2].move = MOVE_NONE; //counter_moves[0]
        killers[3].move = MOVE_NONE; //counter_moves[1]
        killers[4].move = MOVE_NONE; //followup_moves[0]
        killers[5].move = MOVE_NONE; //followup_moves[1]

        // Be sure killer moves are not MOVE_NONE
        for (int32_t i = 0; i < 2; ++i)
        {
            if (ss->killers[i])
            {
                (end++)->move = ss->killers[i];
            }
        }
        // If killer moves are same
        if (ss->killers[1] && ss->killers[1] == ss->killers[0]) // Due to SMP races
        {
            (--end)->move = MOVE_NONE;
        }

        // Be sure counter moves are not MOVE_NONE & different from killer moves
        for (int32_t i = 0; i < 2; ++i)
        {
            if (counter_moves[i] &&
                counter_moves[i] != (cur+0)->move &&
                counter_moves[i] != (cur+1)->move)
            {
                (end++)->move = counter_moves[i];
            }
        }
        // If counter moves are same
        if (counter_moves[1] && counter_moves[1] == counter_moves[0]) // Due to SMP races
        {
            (--end)->move = MOVE_NONE;
        }

        // Be sure followup moves are not MOVE_NONE & different from killers and countermoves
        for (int32_t i = 0; i < 2; ++i)
        {
            if (followup_moves[i] &&
                followup_moves[i] != (cur+0)->move &&
                followup_moves[i] != (cur+1)->move &&
                followup_moves[i] != (cur+2)->move &&
                followup_moves[i] != (cur+3)->move)
            {
                (end++)->move = followup_moves[i];
            }
        }
        // If followup moves are same
        if (followup_moves[1] && followup_moves[1] == followup_moves[0]) // Due to SMP races
        {
            (--end)->move = MOVE_NONE;
        }

        return;

    case QUIETS_1_S1:
        end = end_quiets = generate<QUIET> (m_list, pos);
        if (end > cur)
        {
            value<QUIET> ();
            end = partition (cur, end, ValMove ());
            insertion_sort  (cur, end);
        }
        return;

    case QUIETS_2_S1:
        cur = end;
        end = end_quiets;
        if (depth >= 3 * ONE_MOVE)
        {
            insertion_sort (cur, end);
        }

        return;

    case BAD_CAPTURES_S1:
        // Just pick them in reverse order to get MVV/LVA ordering
        cur = m_list + MAX_MOVES - 1;
        end = end_bad_captures;

        return;

    case EVASIONS_S2:
        end = generate<EVASION> (m_list, pos);
        if (end > cur + 1)
        {
            value<EVASION> ();
            insertion_sort (cur, end);
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
        end = cur + 1; // Avoid another generate_next_stage() call

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
    while (true) // (stage <= STOP)
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
                move = pick_best (cur++, end)->move;
                if (move != tt_move)
                {
                    if (pos.see_sign (move) >= 0) return move;
                    // Losing capture, move it to the tail of the array
                    (end_bad_captures--)->move = move;
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

        case QUIETS_1_S1: case QUIETS_2_S1:
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

        case EVASIONS_S2: case CAPTURES_S3: case CAPTURES_S4:
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

        case CAPTURES_S5:
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

        case CAPTURES_S6:
            do
            {
                move = pick_best (cur++, end)->move;
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
}

template<>
// Version of next_move() to use at split point nodes where the move is grabbed
// from the split point's shared MovePicker object. This function is not thread
// safe so must be lock protected by the caller.
Move MovePicker::next_move<true> ()
{
    return ss->split_point->move_picker->next_move<false> ();
}
