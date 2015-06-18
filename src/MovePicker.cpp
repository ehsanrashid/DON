#include "MovePicker.h"

#include "Thread.h"

namespace MovePick {

    using namespace std;
    using namespace MoveGen;
    using namespace Searcher;
    using namespace BitBoard;

    namespace {

        enum : u08 // Stages
        {
            S_MAIN    , S_GOOD_CAPTURE, S_KILLER, S_GOOD_QUIET, S_BAD_QUIET, S_BAD_CAPTURE,
            S_EVASION , S_ALL_EVASION,
            S_QSEARCH_WITH_CHECK   , S_QCAPTURE_1, S_QUIET_CHECK,
            S_QSEARCH_WITHOUT_CHECK, S_QCAPTURE_2,
            S_PROBCUT  , S_PROBCUT_CAPTURE,
            S_RECAPTURE, S_ALL_RECAPTURE,
            S_STOP
        };

        // Insertion sort in the range [beg, end), which is guaranteed to be stable, as it should be
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

        // Generic insertion sort
        //template<class Iterator, class BinaryPredicate = greater<>>
        //void insertion_sort (Iterator beg, Iterator end, BinaryPredicate pred = BinaryPredicate{})
        //{
        //    for (auto it = beg; it != end; ++it)
        //    {
        //        rotate (upper_bound (beg, it, *it, pred), it, next (it));
        //    }
        //}

        // pick_best() finds the best move in the range [beg, end) and moves it to front,
        // it is faster than sorting all the moves in advance when there are few moves
        // e.g. the possible captures.
        Move pick_best (ValMove *beg, ValMove *end)
        {
            swap (*beg, *max_element (beg, end));
            return *beg;
        }

    }

    // Constructors of the MovePicker class. As arguments pass information
    // to help it to return the (presumably) good moves first, to decide which
    // moves to return (in the quiescence search, for instance, only want to
    // search captures, promotions and some checks) and about how important good
    // move ordering is at the current node.

    MovePicker::MovePicker (const Position &p, const ValueStats &hv, const Value2DStats& cmhv, Move ttm, Depth d, Move cm, const Stack *s)
        : _Pos (p)
        , _HistoryValues (hv)
        , _CounterMovesHistoryValues (cmhv)
        , _ss (s)
        , _counter_move (cm)
        , _depth (d)
    {
        assert (_depth > DEPTH_ZERO);

        _stage = _Pos.checkers () != U64(0) ? S_EVASION : S_MAIN;

        _tt_move =   ttm != MOVE_NONE
                  && _Pos.pseudo_legal (ttm) ?
                        ttm : MOVE_NONE;

        _moves_end += _tt_move != MOVE_NONE;
    }

    MovePicker::MovePicker (const Position &p, const ValueStats &hv, const Value2DStats& cmhv, Move ttm, Depth d, Square dst_sq)
        : _Pos (p)
        , _HistoryValues (hv)
        , _CounterMovesHistoryValues (cmhv)
        , _depth (d)
    {
        assert (_depth <= DEPTH_ZERO);

        if (_Pos.checkers () != U64(0))
        {
            _stage = S_EVASION;
        }
        else
        if (_depth > DEPTH_QS_NO_CHECKS)
        {
            _stage = S_QSEARCH_WITH_CHECK;
        }
        else
        if (_depth > DEPTH_QS_RECAPTURES)
        {
            _stage = S_QSEARCH_WITHOUT_CHECK;
        }
        else
        {
            _stage = S_RECAPTURE;
            _recapture_sq = dst_sq;
            ttm = MOVE_NONE;
        }

        _tt_move =   ttm != MOVE_NONE
                  && _Pos.pseudo_legal (ttm) ?
                        ttm : MOVE_NONE;

        _moves_end += _tt_move != MOVE_NONE;
    }

    MovePicker::MovePicker (const Position &p, const ValueStats &hv, const Value2DStats& cmhv, Move ttm, PieceT cpt)
        : _Pos (p)
        , _HistoryValues (hv)
        , _CounterMovesHistoryValues (cmhv)
    {
        assert (_Pos.checkers () == U64(0));

        _stage = S_PROBCUT;

        // In ProbCut generate only captures better than parent's captured piece
        _capture_threshold = PIECE_VALUE[MG][cpt];

        _tt_move =   ttm != MOVE_NONE
                  && _Pos.pseudo_legal (ttm)
                  && _Pos.capture (ttm)
                  && _Pos.see (ttm) > _capture_threshold ?
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
            m.value = PIECE_VALUE[MG][ptype (_Pos[dst_sq (m)])]
                    - 200 * rel_rank (_Pos.active (), dst_sq (m));
        }
    }

    template<>
    void MovePicker::value<QUIET>   ()
    {
        Move opp_move = (_ss-1)->current_move;
        Square opp_move_dst = _ok (opp_move) ? dst_sq (opp_move) : SQ_NO;
        const ValueStats &cmhv = opp_move_dst != SQ_NO ? _CounterMovesHistoryValues[_Pos[opp_move_dst]][opp_move_dst] :
                                                         _CounterMovesHistoryValues[EMPTY][SQ_A1];

        for (auto &m : *this)
        {
            m.value = _HistoryValues[_Pos[org_sq (m)]][dst_sq (m)]
                    + 3 * cmhv[_Pos[org_sq (m)]][dst_sq (m)];
        }
    }

    template<>
    // Try good captures ordered by MVV/LVA, then non-captures if destination square
    // is not under attack, ordered by _HistoryValues, then bad-captures and quiet moves
    // with a negative SEE. This last group is ordered by the SEE value.
    void MovePicker::value<EVASION> ()
    {
        for (auto &m : *this)
        {
            assert (ptype (_Pos[org_sq (m)]) != NONE);

            Value gain_value = _Pos.see_sign (m);
            if (gain_value < VALUE_ZERO)
            {
                m.value = gain_value - ValueStats::MaxValue; // At the bottom
            }
            else
            if (_Pos.capture (m))
            {
                if (mtype (m) == NORMAL)
                {
                    m.value = PIECE_VALUE[MG][ptype (_Pos[dst_sq (m)])]
                            - Value(ptype (_Pos[org_sq (m)]))
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
                    m.value = PIECE_VALUE[MG][ptype (_Pos[dst_sq (m)])]
                            + PIECE_VALUE[MG][promote (m)]
                            - PIECE_VALUE[MG][PAWN]
                            + ValueStats::MaxValue-1;
                }
            }
            else
            {
                m.value = _HistoryValues[_Pos[org_sq (m)]][dst_sq (m)];
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

        case S_GOOD_CAPTURE:
        case S_QCAPTURE_1:
        case S_QCAPTURE_2:
        case S_PROBCUT_CAPTURE:
        case S_ALL_RECAPTURE:
            _moves_end = generate<CAPTURE> (_moves_beg, _Pos);
            if (_moves_cur < _moves_end-1)
            {
                value<CAPTURE> ();
            }
            break;

        case S_KILLER:
            _moves_cur = _killers;
            _moves_end = _killers + sizeof (_ss->killer_moves)/sizeof (*_ss->killer_moves);
            copy (::begin (_ss->killer_moves), ::end (_ss->killer_moves), ::begin (_killers));
            *_moves_end = MOVE_NONE;

            // Be sure countermoves are different from _killers
            if (count (::begin (_killers), ::end (_killers)-1, _counter_move) == 0)
            {
                *_moves_end++ = _counter_move;
            }
            break;

        case S_GOOD_QUIET:
            _moves_end = _quiets_end = generate<QUIET> (_moves_beg, _Pos);
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

        case S_BAD_QUIET:
            _moves_cur = _moves_end;
            _moves_end = _quiets_end;
            if (_depth >= 3*DEPTH_ONE)
            {
                insertion_sort (_moves_cur, _moves_end);
            }
            break;

        case S_BAD_CAPTURE:
            // Just pick them in reverse order to get MVV/LVA ordering
            _moves_cur = _moves_beg+MAX_MOVES-1;
            _moves_end = _bad_captures_end;
            break;

        case S_ALL_EVASION:
            _moves_end = generate<EVASION> (_moves_beg, _Pos);
            if (_moves_cur < _moves_end-1)
            {
                value<EVASION> ();
            }
            break;

        case S_QUIET_CHECK:
            _moves_end = generate<QUIET_CHECK> (_moves_beg, _Pos);
            break;

        case S_EVASION:
        case S_QSEARCH_WITH_CHECK:
        case S_QSEARCH_WITHOUT_CHECK:
        case S_PROBCUT:
        case S_RECAPTURE:
            _stage = S_STOP;

        case S_STOP:
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

            case S_MAIN:
            case S_EVASION:
            case S_QSEARCH_WITH_CHECK:
            case S_QSEARCH_WITHOUT_CHECK:
            case S_PROBCUT:
                ++_moves_cur;
                return _tt_move;
                break;

            case S_GOOD_CAPTURE:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (move != _tt_move)
                    {
                        if (_Pos.see_sign (move) >= VALUE_ZERO)
                        {
                            return move;
                        }
                        // Losing capture, move it to the tail of the array
                        *_bad_captures_end-- = move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case S_KILLER:
                do
                {
                    move = *_moves_cur++;
                    if (   move != MOVE_NONE
                        && move != _tt_move 
                        && _Pos.pseudo_legal (move)
                        && !_Pos.capture (move)
                       )
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case S_GOOD_QUIET:
            case S_BAD_QUIET:
                do
                {
                    move = *_moves_cur++;
                    if (   move != _tt_move
                        && count (::begin (_killers), ::end (_killers), move) == 0 // Not killer move
                       )
                    {
                        return move;
                    }
                    
                } while (_moves_cur < _moves_end);
                break;

            case S_BAD_CAPTURE:
                return *_moves_cur--;
                break;

            case S_ALL_EVASION:
            case S_QCAPTURE_1:
            case S_QCAPTURE_2:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (move != _tt_move)
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case S_PROBCUT_CAPTURE:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (move != _tt_move && _Pos.see (move) > _capture_threshold)
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case S_ALL_RECAPTURE:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (move != _tt_move && dst_sq (move) == _recapture_sq)
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case S_QUIET_CHECK:
                do
                {
                    move = *_moves_cur++;
                    if (move != _tt_move)
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case S_STOP:
                return MOVE_NONE;
                break;

            default:
                assert (false);
                break;
            }
        } while (true); // (_stage <= S_STOP)
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
