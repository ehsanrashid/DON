#include "MovePicker.h"

#include "Thread.h"

namespace MovePick {

    using namespace std;
    using namespace Searcher;
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

        // Insertion sort in the range [beg, end], which is guaranteed to be stable, as it should be
        void insertion_sort (ValMove *beg, ValMove *end)
        {
            for (ValMove *p = beg+1; p < end; ++p)
            {
                ValMove t = *p, *q;
                for (q = p; q != beg && *(q-1) < t; --q)
                {
                    *q = *(q-1);
                }
                *q = t;
            }
        }

        // pick_best() finds the best move in the range [beg, end] and moves it to front,
        // it is faster than sorting all the moves in advance when there are few moves
        // e.g. the possible captures.
        Move pick_best (ValMove *beg, ValMove *end)
        {
            std::swap (*beg, *std::max_element (beg, end));
            return *beg;
        }

    }

    // Constructors of the MovePicker class. As arguments pass information
    // to help it to return the (presumably) good moves first, to decide which
    // moves to return (in the quiescence search, for instance, only want to
    // search captures, promotions and some checks) and about how important good
    // move ordering is at the current node.

    MovePicker::MovePicker (const Position &p, const ValueStats &hv, const ValueValueStats& cmhv, Move ttm, Depth d, Move cm, Stack *s)
        : _moves_cur (_moves_beg)
        , _moves_end (_moves_beg)
        , _pos (p)
        , _history_value (hv)
        , _countermoves_history_value (cmhv)
        , _ss (s)
        , _counter_move (cm)
        , _depth (d)
    {
        assert (d > DEPTH_ZERO);

        _bad_captures_end = _moves_beg+MAX_MOVES-1;

        _stage = _pos.checkers () != U64(0) ? EVASION_S1 : MAIN_S;

        _tt_move =   ttm != MOVE_NONE
                  && _pos.pseudo_legal (ttm) ?
                        ttm : MOVE_NONE;

        _moves_end += _tt_move != MOVE_NONE;
    }

    MovePicker::MovePicker (const Position &p, const ValueStats &hv, const ValueValueStats& cmhv, Move ttm, Depth d, Square dst_sq)
        : _moves_cur (_moves_beg)
        , _moves_end (_moves_beg)
        , _pos (p)
        , _history_value (hv)
        , _countermoves_history_value (cmhv)
        , _ss (NULL)
        , _counter_move (MOVE_NONE)
        , _depth (d)
    {
        assert (d <= DEPTH_ZERO);

        if (_pos.checkers () != U64(0))
        {
            _stage = EVASION_S1;
        }
        else
        if (d > DEPTH_QS_NO_CHECKS)
        {
            _stage = QSEARCH_0;
        }
        else
        if (d > DEPTH_QS_RECAPTURES)
        {
            _stage = QSEARCH_1;
        }
        else
        {
            _stage = RECAPTURE;
            _recapture_sq = dst_sq;
            ttm   = MOVE_NONE;
        }

        _tt_move =   ttm != MOVE_NONE
                  && _pos.pseudo_legal (ttm) ?
                        ttm : MOVE_NONE;

        _moves_end += _tt_move != MOVE_NONE;
    }

    MovePicker::MovePicker (const Position &p, const ValueStats &hv, const ValueValueStats& cmhv, Move ttm, PieceT pt)
        : _moves_cur (_moves_beg)
        , _moves_end (_moves_beg)
        , _pos (p)
        , _history_value (hv)
        , _countermoves_history_value (cmhv)
        , _ss (NULL)
        , _counter_move (MOVE_NONE)
        , _depth (DEPTH_ZERO)
    {
        assert (_pos.checkers () == U64(0));

        _stage = PROB_CUT;

        // In ProbCut generate only captures better than parent's captured piece
        _capture_threshold = PIECE_VALUE[MG][pt];

        _tt_move =   ttm != MOVE_NONE
                  && _pos.pseudo_legal (ttm)
                  && _pos.capture (ttm)
                  && _pos.see (ttm) > _capture_threshold ?
                        ttm : MOVE_NONE;

        _moves_end += _tt_move != MOVE_NONE;
    }

    // value() assign a numerical move ordering score to each move in a move list.
    // The moves with highest scores will be picked first.

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
    // some SEE calls in case get a cutoff.
    void MovePicker::value<CAPTURE> ()
    {
        for (auto &m : *this)
        {
            m.value = PIECE_VALUE[MG][_pos[dst_sq (m)]]
                    - 200 * rel_rank (_pos.active (), dst_sq (m));
        }
    }

    template<>
    void MovePicker::value<QUIET>   ()
    {
        Square opp_move_dst = dst_sq ((_ss-1)->current_move);
        const ValueStats& cmh = _countermoves_history_value[_pos[opp_move_dst]][opp_move_dst];

        for (auto &m : *this)
        {
            m.value = _history_value[_pos[org_sq (m)]][dst_sq (m)]
                    + cmh[_pos[org_sq (m)]][dst_sq (m)] * 3;
        }
    }

    template<>
    // Try good captures ordered by MVV/LVA, then non-captures if destination square
    // is not under attack, ordered by _history_value value, then bad-captures and quiet
    // moves with a negative SEE. This last group is ordered by the SEE value.
    void MovePicker::value<EVASION> ()
    {
        for (auto &m : *this)
        {
            assert (ptype (_pos[org_sq (m)]) != NONE);

            Value gain_value = _pos.see_sign (m);
            if (gain_value < VALUE_ZERO)
            {
                m.value = gain_value - ValueStats::MaxValue; // At the bottom
            }
            else
            if (_pos.capture (m))
            {
                if (mtype (m) == NORMAL)
                {
                    m.value = PIECE_VALUE[MG][ptype (_pos[dst_sq (m)])]
                            - Value(ptype (_pos[org_sq (m)]))
                            + ValueStats::MaxValue-1;
                }
                else
                if (mtype (m) == ENPASSANT)
                {
                    m.value = PIECE_VALUE[MG][PAWN]
                            + ValueStats::MaxValue-1;
                }
                else
                if (mtype (m) == PROMOTE)
                {
                    m.value = PIECE_VALUE[MG][ptype (_pos[dst_sq (m)])]
                            + PIECE_VALUE[MG][promote (m)]
                            - PIECE_VALUE[MG][PAWN]
                            + ValueStats::MaxValue-1;
                }
            }
            else
            {
                m.value = _history_value[_pos[org_sq (m)]][dst_sq (m)];
            }
        }
    }

    // generate_next_stage() generates, scores and sorts the next bunch of moves,
    // when there are no more moves to try for the current stage.
    void MovePicker::generate_next_stage ()
    {
        _moves_cur = _moves_beg;

        switch (++_stage)
        {

        case CAPTURE_S1:
        case CAPTURE_S3:
        case CAPTURE_S4:
        case CAPTURE_S5:
        case CAPTURE_S6:
            _moves_end = generate<CAPTURE> (_moves_beg, _pos);
            if (_moves_cur < _moves_end-1)
            {
                value<CAPTURE> ();
            }
            break;

        case KILLER_S1:
            _moves_cur = _killers;
            _moves_end = _killers + 2;

            _killers[0] = _ss->killer_moves[0];
            _killers[1] = _ss->killer_moves[1];
            _killers[2] = MOVE_NONE;

            // Be sure countermoves are different from _killers
            if (   _counter_move != _killers[0]
                && _counter_move != _killers[1]
               )
            {
                *_moves_end++ = _counter_move;
            }
            break;

        case QUIET_1_S1:
            _moves_end = _quiets_end = generate<QUIET> (_moves_beg, _pos);
            if (_moves_cur < _moves_end)
            {
                value<QUIET> ();
                // Split positive(+ve) value from the list
                _moves_end = partition (_moves_cur, _moves_end, [](const ValMove &m) { return m.value > VALUE_ZERO; });
                if (_moves_cur < _moves_end-1)
                {
                    insertion_sort (_moves_cur, _moves_end);
                }
            }
            break;

        case QUIET_2_S1:
            _moves_cur = _moves_end;
            _moves_end = _quiets_end;
            if (_depth >= 3*DEPTH_ONE)
            {
                insertion_sort (_moves_cur, _moves_end);
            }
            break;

        case BAD_CAPTURE_S1:
            // Just pick them in reverse order to get MVV/LVA ordering
            _moves_cur = _moves_beg+MAX_MOVES-1;
            _moves_end = _bad_captures_end;
            break;

        case EVASION_S2:
            _moves_end = generate<EVASION> (_moves_beg, _pos);
            if (_moves_cur < _moves_end-1)
            {
                value<EVASION> ();
            }
            break;

        case QUIET_CHECK_S3:
            _moves_end = generate<QUIET_CHECK> (_moves_beg, _pos);
            break;

        case EVASION_S1:
        case QSEARCH_0:
        case QSEARCH_1:
        case PROB_CUT:
        case RECAPTURE:
            _stage = STOP;

        case STOP:
            _moves_end = _moves_cur+1; // Avoid another generate_next_stage() call
            break;

        default:
            assert (false);
            break;
        }
    }

    template<>
    // next_move<false>() is the most important method of the MovePicker class.
    // It returns a new pseudo legal move every time is called, until there are no more moves
    // It picks the move with the biggest score from a list of generated moves
    // taking care not to return the tt_move if has already been searched previously.
    Move MovePicker::next_move<false> ()
    {
        do
        {
            Move move;
            while (_moves_cur == _moves_end)
            {
                generate_next_stage ();
            }

            switch (_stage)
            {

            case MAIN_S:
            case EVASION_S1:
            case QSEARCH_0:
            case QSEARCH_1:
            case PROB_CUT:
                ++_moves_cur;
                return _tt_move;
                break;

            case CAPTURE_S1:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (move != _tt_move)
                    {
                        if (_pos.see_sign (move) >= VALUE_ZERO)
                        {
                            return move;
                        }
                        // Losing capture, move it to the tail of the array
                        *_bad_captures_end-- = move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case KILLER_S1:
                do
                {
                    move = *_moves_cur++;
                    if (   move != MOVE_NONE
                        && move != _tt_move 
                        && _pos.pseudo_legal (move)
                        && !_pos.capture (move)
                       )
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case QUIET_1_S1:
            case QUIET_2_S1:
                do
                {
                    move = *_moves_cur++;
                    if (   move != _tt_move
                        && move != _killers[0]
                        && move != _killers[1]
                        && move != _killers[2]
                       )
                    {
                        return move;
                    }
                    
                } while (_moves_cur < _moves_end);
                break;

            case BAD_CAPTURE_S1:
                return *_moves_cur--;
                break;

            case EVASION_S2:
            case CAPTURE_S3:
            case CAPTURE_S4:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (move != _tt_move)
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case CAPTURE_S5:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (move != _tt_move && _pos.see (move) > _capture_threshold)
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case CAPTURE_S6:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (move != _tt_move && dst_sq (move) == _recapture_sq)
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case QUIET_CHECK_S3:
                do
                {
                    move = *_moves_cur++;
                    if (move != _tt_move)
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case STOP:
                return MOVE_NONE;
                break;

            default:
                assert (false);
                break;
            }
        } while (true); // (_stage <= STOP)
    }

    template<>
    // next_move<true>() is to use at splitpoint nodes where the move is grabbed
    // from the splitpoint's shared MovePicker object. This function is not thread
    // safe so must be lock protected by the caller.
    Move MovePicker::next_move<true> ()
    {
        return _ss->splitpoint->movepicker->next_move<false> ();
    }

}
