#include "MovePicker.h"

#include "Position.h"
#include "Thread.h"

using namespace std;
using namespace Searcher;
using namespace MoveGenerator;

const Value MaxValue = Value (2000);

namespace {

    enum Stages : uint8_t
    {
        MAIN_STAGE, CAPTURES_S1, KILLERS_S1, QUIETS_1_S1, QUIETS_2_S1, BAD_CAPTURES_S1,
        EVASIONS,    EVASIONS_S2,
        QSEARCH_0,   CAPTURES_S3, QUIET_CHECKS_S3,
        QSEARCH_1,   CAPTURES_S4,
        PROBCUT,     CAPTURES_S5,
        RECAPTURE,   CAPTURES_S6,
        STOP,
    };

    // Our insertion sort, guaranteed to be stable, as is needed
    void insertion_sort (ValMove *beg, ValMove *end)
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

    // Unary predicate used by std::partition to split positive scores from remaining
    // ones so to sort separately the two sets, and with the second sort delayed.
    inline bool positive_value (const ValMove &vm) { return vm.value > VALUE_ZERO; }

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

MovePicker::MovePicker (const Position &p, Move ttm, const HistoryStats &h, PType pt)
    : pos (p)
    , history (h)
    , cur (moves)
    , end (moves)
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

MovePicker::MovePicker (const Position &p, Move ttm, Depth d, const HistoryStats &h, Square sq)
    : pos (p)
    , history (h)
    , cur (moves)
    , end (moves)
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

MovePicker::MovePicker (const Position &p, Move ttm, Depth d, const HistoryStats &h, Move cm[], Stack s[])
    : pos (p)
    , history (h)
    , depth (d)
{
    ASSERT (d > DEPTH_ZERO);

    cur = end = moves;
    end_bad_captures = moves + MAX_MOVES - 1;
    counter_moves = cm;
    ss = s;

    stage = pos.checkers () ? EVASIONS : MAIN_STAGE;

    tt_move = (ttm && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE);
    end += (tt_move != MOVE_NONE);
}

MovePicker::MovePicker(const Position& p, Move ttm, Depth d, const HistoryStats& h, Move cm[], Move fm[], Stack s[])
    : pos (p)
    , history (h)
    , depth (d)
{

    ASSERT (d > DEPTH_ZERO);

    cur = end = moves;
    end_bad_captures = moves + MAX_MOVES - 1;
    counter_moves   = cm;
    followup_moves  = fm;
    ss = s;

    stage = pos.checkers () ? EVASIONS : MAIN_STAGE;

    tt_move = (ttm && pos.pseudo_legal (ttm) ? ttm : MOVE_NONE);
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
    // badCaptures[] array, but instead of doing it now we delay till when
    // the move has been picked up in pick_move_from_list(), this way we save
    // some SEE calls in case we get a cutoff (idea from Pablo Vazquez).
    for (ValMove *itr = moves; itr != end; ++itr)
    {
        Move m = itr->move;
        itr->value = PieceValue[MG][_type (pos[dst_sq (m)])] - _type (pos[org_sq (m)]);

        switch (m_type (m))
        {
        case PROMOTE:
            itr->value += PieceValue[MG][prom_type (m)] - PieceValue[MG][PAWN];
            break;
        case ENPASSANT:
            itr->value += PieceValue[MG][PAWN];
            break;
        }
    }
}

template<>
void MovePicker::value<QUIET> ()
{
    Move m;
    for (ValMove *itr = moves; itr != end; ++itr)
    {
        m = itr->move;
        itr->value = history[pos.moved_piece (m)][dst_sq (m)];
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
        int32_t see_value = pos.see_sign (m);
        if (see_value < 0)
        {
            itr->value = see_value - MaxValue; // At the bottom
        }
        else if (pos.capture (m))
        {
            itr->value = PieceValue[MG][_type (pos[dst_sq (m)])] - _type (pos[org_sq (m)]) + MaxValue;
        }
        else
        {
            itr->value = history[pos.moved_piece (m)][dst_sq (m)];
        }
    }
}

template<GType GT>
void MovePicker::generate_moves ()
{
    uint32_t index = 0;
    MoveList mov_lst = generate<GT> (pos);

    //MoveList::const_iterator itr = mov_lst.cbegin ();
    //while (itr != mov_lst.cend ())
    //{
    //    moves[index].move  = *itr;
    //    //moves[index].value = VALUE_ZERO;
    //    ++index;
    //    ++itr;
    //}

    for_each (mov_lst.cbegin (), mov_lst.cend (), [&] (Move m)
    {
        moves[index].move = m;
        //moves[index].value = VALUE_ZERO;
        ++index;
    });

    moves[index].move = MOVE_NONE;
    end = moves + index;
}

// generate_next () generates, scores and sorts the next bunch of moves,
// when there are no more moves to try for the current phase.
void MovePicker::generate_next ()
{
    cur = moves;
    switch (++stage)
    {

    case CAPTURES_S1:
    case CAPTURES_S3:
    case CAPTURES_S4:
    case CAPTURES_S5:
    case CAPTURES_S6:
        generate_moves<CAPTURE> ();
        value<CAPTURE> ();

        return;

    case KILLERS_S1:
        cur = killers;
        end = cur + 2;

        killers[0].move = ss->killers[0];
        killers[1].move = ss->killers[1];
        killers[2].move = MOVE_NONE;
        killers[3].move = MOVE_NONE;
        killers[4].move = MOVE_NONE;
        killers[5].move = MOVE_NONE;

        // Be sure counter_moves are different from killers
        for (int32_t i = 0; i < 2; ++i)
        {
            if ((counter_moves[i] != (cur+0)->move) &&
                (counter_moves[i] != (cur+1)->move))
            {
                (end++)->move = counter_moves[i];
            }
        }
        if (counter_moves[1] && (counter_moves[1] == counter_moves[0])) // Due to SMP races
        {
            killers[3].move = MOVE_NONE;
        }

        // Be sure followupmoves are different from killers and countermoves
        for (int i = 0; i < 2; ++i)
            if (   followup_moves[i] != (cur+0)->move
                && followup_moves[i] != (cur+1)->move
                && followup_moves[i] != (cur+2)->move
                && followup_moves[i] != (cur+3)->move)
                (end++)->move = followup_moves[i];

        if (followup_moves[1] && (followup_moves[1] == followup_moves[0])) // Due to SMP races
        {
            (--end)->move = MOVE_NONE;
        }

        return;

    case QUIETS_1_S1:
        generate_moves<QUIET> ();
        end_quiets = end;
        value<QUIET> ();
        end = partition (cur, end, positive_value);
        insertion_sort (cur, end);

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
        cur = moves + MAX_MOVES - 1;
        end = end_bad_captures;
        return;

    case EVASIONS_S2:
        generate_moves<EVASION> ();
        if (end > moves + 1) value<EVASION> ();
        return;

    case QUIET_CHECKS_S3:
        generate_moves<QUIET_CHECK> ();
        return;

    case EVASIONS:
    case QSEARCH_0:
    case QSEARCH_1:
    case PROBCUT:
    case RECAPTURE:
        stage = STOP;

    case STOP:
        end = cur + 1; // Avoid another next_phase() call
        return;

    default:
        ASSERT (false);
    }
}

template<>
// next_move() is the most important method of the MovePicker class. It returns
// a new pseudo legal move every time is called, until there are no more moves
// left. It picks the move with the biggest score from a list of generated moves
// taking care not returning the tt_move if has already been searched previously.
Move MovePicker::next_move<false> ()
{
    Move move;

    while (true)
    {
        while (cur == end)
        {
            generate_next ();
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
            break;

        case CAPTURES_S1:
            move = pick_best (cur++, end)->move;
            if (move != tt_move)
            {
                if (pos.see_sign (move) >= 0) return move;
                // Losing capture, move it to the tail of the array
                (end_bad_captures--)->move = move;
            }
            break;

        case KILLERS_S1:
            move = (cur++)->move;
            if (    move != MOVE_NONE
                &&  pos.pseudo_legal (move)
                &&  move != tt_move
                && !pos.capture (move))
            {
                return move;
            }
            
            break;

        case QUIETS_1_S1: case QUIETS_2_S1:
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

            break;

        case BAD_CAPTURES_S1:
            return (cur--)->move;

        case EVASIONS_S2: case CAPTURES_S3: case CAPTURES_S4:
            move = pick_best (cur++, end)->move;
            if (move != tt_move)
            {
                return move;
            }
            
            break;

        case CAPTURES_S5:
            move = pick_best (cur++, end)->move;
            if (move != tt_move && pos.see (move) > capture_threshold)
            {
                return move;
            }
            
            break;

        case CAPTURES_S6:
            move = pick_best (cur++, end)->move;
            if (dst_sq (move) == recapture_sq)
            {
                return move;
            }
            
            break;

        case QUIET_CHECKS_S3:
            move = (cur++)->move;
            if (move != tt_move)
            {
                return move;
            }
            
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
