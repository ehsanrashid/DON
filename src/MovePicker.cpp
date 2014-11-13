#include "MovePicker.h"

#include "Thread.h"

namespace MovePick {

    using namespace std;
    using namespace Search;
    using namespace MoveGen;
    using namespace BitBoard;

    namespace {

        enum StageT : u08
        {
            MAIN_S    , CAPTURE_S1, KILLER_S1, QUIET_1_S1, QUIET_2_S1, BAD_CAPTURE_S1,
            EVASION_S1, EVASION_S2,
            QSEARCH_0 , CAPTURE_S3, QUIET_CHECK_S3,
            QSEARCH_1 , CAPTURE_S4,
            PROB_CUT  , CAPTURE_S5,
            RECAPTURE , CAPTURE_S6,
            STOP
        };

        // Insertion sort in the range [begin, end], guaranteed to be stable, as is needed
        inline void insertion_sort (ValMove *begin, ValMove *end)
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

    }

    // Constructors of the MovePicker class. As arguments pass information
    // to help it to return the (presumably) good moves first, to decide which
    // moves to return (in the quiescence search, for instance, only want to
    // search captures, promotions and some checks) and about how important good
    // move ordering is at the current node.

    const Value HistoryStats::MaxValue = Value(+0x800);

    MovePicker::MovePicker (const Position &p, HistoryStats &h, Move ttm, Depth d, Move *cm, Move *fm, Stack *s)
        : cur (moves)
        , end (moves)
        , pos (p)
        , history (h)
        , ss (s)
        , counter_moves (cm)
        , followup_moves (fm)
        , depth (d)
    {
        assert (d > DEPTH_ZERO);

        bad_captures_end = moves+MAX_MOVES-1;

        stage = pos.checkers () != U64(0) ? EVASION_S1 : MAIN_S;

        tt_move = ttm != MOVE_NONE
               && pos.pseudo_legal (ttm) ?
                    ttm : MOVE_NONE;

        end += tt_move != MOVE_NONE;
    }

    MovePicker::MovePicker (const Position &p, HistoryStats &h, Move ttm, Depth d, Square dst_sq)
        : cur (moves)
        , end (moves)
        , pos (p)
        , history (h)
        , ss (NULL)
        , counter_moves (NULL)
        , followup_moves (NULL)
        , depth (d)
    {
        assert (d <= DEPTH_ZERO);

        if (pos.checkers () != U64(0))
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
        }
        else
        {
            stage = RECAPTURE;
            recapture_sq = dst_sq;
            ttm   = MOVE_NONE;
        }

        tt_move = ttm != MOVE_NONE
               && pos.pseudo_legal (ttm) ?
                    ttm : MOVE_NONE;

        end += tt_move != MOVE_NONE;
    }

    MovePicker::MovePicker (const Position &p, HistoryStats &h, Move ttm, PieceT pt)
        : cur (moves)
        , end (moves)
        , pos (p)
        , history (h)
        , ss (NULL)
        , counter_moves (NULL)
        , followup_moves (NULL)
        , depth (DEPTH_ZERO)
    {
        assert (pos.checkers () == U64(0));

        stage = PROB_CUT;

        // In ProbCut generate only captures better than parent's captured piece
        capture_threshold = PIECE_VALUE[MG][pt];

        tt_move = ttm != MOVE_NONE
               && pos.pseudo_legal (ttm)
               && pos.capture (ttm)
               && pos.see (ttm) > capture_threshold ?
                    ttm : MOVE_NONE;

        end += tt_move != MOVE_NONE;
    }

    // value() assign a numerical move ordering score to each move in a move list.
    // The moves with highest scores will be picked first.

    template<GenT GT>
    void MovePicker::value ()
    {}

    template<>
    // Winning and equal captures in the main search are ordered by MVV/LVA.
    // Suprisingly, this appears to perform slightly better than SEE based
    // move ordering. The reason is probably that in a position with a winning
    // capture, capturing a more valuable (but sufficiently defended) piece
    // first usually doesn't hurt. The opponent will have to recapture, and
    // the hanging piece will still be hanging (except in the unusual cases
    // where it is possible to recapture with the hanging piece).
    // Exchanging big pieces before capturing a hanging piece probably
    // helps to reduce the subtree size.
    // In main search want to push captures with negative SEE values to
    // bad_captures[] array, but instead of doing it now delay till when
    // the move has been picked up in pick_move(), this way save
    // some SEE calls in case get a cutoff (idea from Pablo Vazquez).
    void MovePicker::value<CAPTURE> ()
    {
        for (ValMove *itr = moves; itr != end; ++itr)
        {
            Move m = itr->move;
            assert (ptype (pos[org_sq (m)]) != NONE);
            if (mtype (m) == NORMAL)
            {
                itr->value = PIECE_VALUE[MG][ptype (pos[dst_sq (m)])] - i32(ptype (pos[org_sq (m)]))-1;
            }
            else
            if (mtype (m) == ENPASSANT)
            {
                itr->value = PIECE_VALUE[MG][PAWN] -1;
            }
            else
            if (mtype (m) == PROMOTE)
            {
                itr->value = PIECE_VALUE[MG][ptype (pos[dst_sq (m)])] + PIECE_VALUE[MG][promote (m)] - PIECE_VALUE[MG][PAWN] -1;
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
    // Try good captures ordered by MVV/LVA, then non-captures if destination square
    // is not under attack, ordered by history value, then bad-captures and quiet
    // moves with a negative SEE. This last group is ordered by the SEE value.
    void MovePicker::value<EVASION> ()
    {
        for (ValMove *itr = moves; itr != end; ++itr)
        {
            Move m = itr->move;
            assert (ptype (pos[org_sq (m)]) != NONE);
            Value gain_value = pos.see_sign (m);
            if (gain_value < VALUE_ZERO)
            {
                itr->value = gain_value - HistoryStats::MaxValue; // At the bottom
            }
            else
            if (pos.capture (m))
            {
                if (mtype (m) == NORMAL)
                {
                    itr->value = PIECE_VALUE[MG][ptype (pos[dst_sq (m)])] - i32(ptype (pos[org_sq (m)]))-1 + HistoryStats::MaxValue;
                }
                else
                if (mtype (m) == ENPASSANT)
                {
                    itr->value = PIECE_VALUE[MG][PAWN] -1 + HistoryStats::MaxValue;
                }
                else
                if (mtype (m) == PROMOTE)
                {
                    itr->value = PIECE_VALUE[MG][ptype (pos[dst_sq (m)])] + PIECE_VALUE[MG][promote (m)] - PIECE_VALUE[MG][PAWN] -1 + HistoryStats::MaxValue;
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
        break;

        case KILLER_S1:
            kcur = kend = killers;
            // Killer moves usually come right after the hash move and (good) captures
            fill (killers, killers + sizeof (killers) / sizeof (*killers), MOVE_NONE);
            // Init killers bitboards to shortcut move's validity check later on
            killers_org = killers_dst = U64(0);
            Move m;
            for (i08 i = 0; i < 2; ++i)
            {
                m = ss->killer_moves[i];
                if (  m != MOVE_NONE
                   && m != tt_move
                   && (  !(killers_org & org_sq (m))
                      || !(killers_dst & dst_sq (m))
                      || (m != kcur[0])
                      )
                   )
                {
                    *(kend++) = m;
                    killers_org += org_sq (m);
                    killers_dst += dst_sq (m);
                }
            }

            // Be sure counter moves are not MOVE_NONE & different from killer moves
            if (counter_moves != NULL)
            for (i08 i = 0; i < 2; ++i)
            {
                m = counter_moves[i];
                if (  m != MOVE_NONE
                   && m != tt_move
                   && (  !(killers_org & org_sq (m))
                      || !(killers_dst & dst_sq (m))
                      || (  m != kcur[0]
                         && m != kcur[1]
                         && m != kcur[2]
                         )
                      )
                   )
                {
                    *(kend++) = m;
                    killers_org += org_sq (m);
                    killers_dst += dst_sq (m);
                }
            }
            
            // Be sure followup moves are not MOVE_NONE & different from killer & counter moves
            if (followup_moves != NULL)
            for (i08 i = 0; i < 2; ++i)
            {
                m = followup_moves[i];
                if (  m != MOVE_NONE
                   && m != tt_move
                   && (  !(killers_org & org_sq (m))
                      || !(killers_dst & dst_sq (m))
                      || (  m != kcur[0]
                         && m != kcur[1]
                         && m != kcur[2]
                         && m != kcur[3]
                         && m != kcur[4]
                         )
                      )
                   )
                {
                    *(kend++) = m;
                    killers_org += org_sq (m);
                    killers_dst += dst_sq (m);
                }
            }

            killers_size = u08(kend - kcur);
            end = cur + 1;
        break;

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
        break;

        case QUIET_2_S1:
            cur = end;
            end = quiets_end;
            if (depth >= 3*DEPTH_ONE)
            {
                insertion_sort (cur, end);
            }
        break;

        case BAD_CAPTURE_S1:
            // Just pick them in reverse order to get MVV/LVA ordering
            cur = moves+MAX_MOVES-1;
            end = bad_captures_end;
        break;

        case EVASION_S2:
            end = generate<EVASION> (moves, pos);
            if (cur < end-1)
            {
                value<EVASION> ();
            }
        break;

        case QUIET_CHECK_S3:
            end = generate<QUIET_CHECK> (moves, pos);
        break;

        case EVASION_S1:
        case QSEARCH_0:
        case QSEARCH_1:
        case PROB_CUT:
        case RECAPTURE:
            stage = STOP;

        case STOP:
            end = cur+1; // Avoid another generate_next_stage() call
        break;

        default:
            assert (false);
        break;
        }
    }

    template<bool SPNode>
    Move MovePicker::next_move ()
    {
        return MOVE_NONE;
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
            case PROB_CUT:
                ++cur;
                return tt_move;
            break;

            case CAPTURE_S1:
                do
                {
                    move = pick_best ()->move;
                    if (move != tt_move)
                    {
                        if (pos.see_sign (move) >= VALUE_ZERO)
                        {
                            return move;
                        }
                        // Losing capture, move it to the tail of the array
                        (bad_captures_end--)->move = move;
                    }
                } while (cur < end);
            break;

            case KILLER_S1:
                do
                {
                    move = *kcur++;
                    if (  move != MOVE_NONE
                       //&& move != tt_move // Not needed as filter out tt_move at move generation 
                       && pos.pseudo_legal (move)
                       && !pos.capture (move)
                       )
                    {
                        return move;
                    }
                } while (kcur < kend);
                cur = end;
            break;

            case QUIET_1_S1:
            case QUIET_2_S1:
                do
                {
                    move = (cur++)->move;

                    if (  move != tt_move
                       && (  !(killers_org & org_sq (move))
                          || !(killers_dst & dst_sq (move))
                          || (  killers_size == 0 || (move != killers[0]
                             && (  killers_size == 1 || (move != killers[1]
                                && (  killers_size == 2 || (move != killers[2]
                                   && (  killers_size == 3 || (move != killers[3]
                                      && (  killers_size == 4 || (move != killers[4]
                                         && (killers_size == 5 || (move != killers[5]))
                                         ))
                                      ))
                                   ))
                                ))
                             ))
                          )
                       )
                    {
                        return move;
                    }
                    
                } while (cur < end);
            break;

            case BAD_CAPTURE_S1:
                return (cur--)->move;
            break;

            case EVASION_S2:
            case CAPTURE_S3:
            case CAPTURE_S4:
                do
                {
                    move = pick_best ()->move;
                    if (move != tt_move)
                    {
                        return move;
                    }
                } while (cur < end);
            break;

            case CAPTURE_S5:
                do
                {
                    move = pick_best ()->move;
                    if (move != tt_move && pos.see (move) > capture_threshold)
                    {
                        return move;
                    }
                } while (cur < end);
            break;

            case CAPTURE_S6:
                do
                {
                    move = pick_best ()->move;
                    if (move != tt_move && recapture_sq == dst_sq (move))
                    {
                        return move;
                    }
                } while (cur < end);
            break;

            case QUIET_CHECK_S3:
                do
                {
                    move = (cur++)->move;
                    if (move != tt_move)
                    {
                        return move;
                    }
                } while (cur < end);
            break;

            case STOP:
                return MOVE_NONE;
            break;

            default:
                assert (false);
            break;
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
        return (ss)->splitpoint->movepicker->next_move<false> ();
    }

}
