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
            for (auto *p = beg+1; p < end; ++p)
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
            std::swap (*beg, *std::max_element (beg, end));
            return *beg;
        }

    }

    // Constructors of the MovePicker class. As arguments pass information
    // to help it to return the (presumably) good moves first, to decide which
    // moves to return (in the quiescence search, for instance, only want to
    // search captures, promotions and some checks) and about how important good
    // move ordering is at the current node.

    MovePicker::MovePicker (const Position &pos, const HValueStats &hv, const CMValueStats &cmv, Move ttm, Depth depth, Move cm, const Stack *ss)
        : _pos (pos)
        , _history_values (hv)
        , _counter_moves_values (&cmv)
        , _ss (ss)
        , _counter_move (cm)
        , _depth (depth)
    {
        assert(_depth > DEPTH_ZERO);

        _stage = _pos.checkers () != U64(0) ? S_EVASION : S_MAIN;

        _tt_move =   ttm != MOVE_NONE
                  && _pos.pseudo_legal (ttm) ?
                        ttm : MOVE_NONE;

        _moves_end += _tt_move != MOVE_NONE;
    }


    MovePicker::MovePicker (const Position &pos, const HValueStats &hv, Move ttm, Depth depth, Square dst_sq)
        : _pos (pos)
        , _history_values (hv)
        , _depth (depth)
    {
        assert(_depth <= DEPTH_ZERO);

        if (_pos.checkers () != U64(0))
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
                  && _pos.pseudo_legal (ttm) ?
                        ttm : MOVE_NONE;

        _moves_end += _tt_move != MOVE_NONE;
    }

    MovePicker::MovePicker (const Position &pos, const HValueStats &hv, Move ttm, Value thr)
        : _pos (pos)
        , _history_values (hv)
        , _threshold (thr)
    {
        assert(_pos.checkers () == U64(0));

        _stage = S_PROBCUT;

        // In ProbCut generate captures with SEE higher than the given threshold
        _tt_move =   ttm != MOVE_NONE
                  && _pos.pseudo_legal (ttm)
                  && _pos.capture (ttm)
                  && _pos.see (ttm) > _threshold ?
                        ttm : MOVE_NONE;

        _moves_end += _tt_move != MOVE_NONE;
    }

    // value() assigns a numerical move ordering score to each move in a move list.
    // The moves with highest scores will be picked first.

    template<>
    // Winning and equal captures in the main search are ordered by MVV, preferring
    // captures near our home rank. Surprisingly, this appears to perform slightly
    // better than SEE based move ordering: exchanging big pieces before capturing
    // a hanging piece probably helps to reduce the subtree size.
    // In main search we want to push captures with negative SEE values to the
    // badCaptures[] array, but instead of doing it now we delay until the move
    // has been picked up, saving some SEE calls in case we get a cutoff.
    void MovePicker::value<CAPTURE> ()
    {
        for (auto &m : *this)
        {
            m = PIECE_VALUE[MG][mtype (m) == ENPASSANT && _pos.en_passant_sq () == dst_sq (m) ? PAWN : ptype (_pos[dst_sq (m)])]
              - Value(200 * rel_rank (_pos.active (), dst_sq (m)));
        }
    }

    template<>
    void MovePicker::value<QUIET>   ()
    {
        for (auto &m : *this)
        {
            m = _history_values[_pos[org_sq (m)]][dst_sq (m)]
              + (*_counter_moves_values)[_pos[org_sq (m)]][dst_sq (m)];
        }
    }

    template<>
    // Try good captures ordered by MVV/LVA, then non-captures if destination square
    // is not under attack, ordered by _history_values, then bad-captures and quiet moves
    // with a negative SEE. This last group is ordered by the SEE value.
    void MovePicker::value<EVASION> ()
    {
        for (auto &m : *this)
        {
            auto see_value = _pos.see_sign (m);
            if (see_value < VALUE_ZERO)
            {
                m = see_value - MAX_STATS_VALUE; // At the bottom
            }
            else
            if (_pos.capture (m))
            {
                m = PIECE_VALUE[MG][mtype (m) == ENPASSANT && _pos.en_passant_sq () == dst_sq (m) ? PAWN : ptype (_pos[dst_sq (m)])]
                  - Value(ptype (_pos[org_sq (m)])) -1 + MAX_STATS_VALUE;
            }
            else
            {
                m = _history_values[_pos[org_sq (m)]][dst_sq (m)];
            }
        }
    }

    // generate_next_stage() generates, scores and sorts the next bunch of moves,
    // when there are no more moves to try for the current stage.
    void MovePicker::generate_next_stage ()
    {
        assert(_stage != S_STOP);

        _moves_cur = _moves_beg;

        switch (++_stage)
        {

        case S_GOOD_CAPTURE:
        case S_QCAPTURE_1:
        case S_QCAPTURE_2:
        case S_PROBCUT_CAPTURE:
        case S_ALL_RECAPTURE:
            _moves_end = generate<CAPTURE> (_moves_beg, _pos);
            if (_moves_cur < _moves_end-1)
            {
                value<CAPTURE> ();
            }
            break;

        case S_KILLER:
            _moves_cur = _killers;
            _moves_end = _killers + sizeof (_ss->killer_moves)/sizeof (*_ss->killer_moves);
            std::copy (std::begin (_ss->killer_moves), std::end (_ss->killer_moves), std::begin (_killers));
            *_moves_end = MOVE_NONE;

            // Be sure countermoves are different from _killers
            if (   _counter_move != MOVE_NONE
                && std::count (std::begin (_killers), std::prev (std::end (_killers)), _counter_move) == 0
               )
            {
                *_moves_end++ = _counter_move;
            }
            break;

        case S_GOOD_QUIET:
            _moves_end = _quiets_end = generate<QUIET> (_moves_beg, _pos);
            if (_moves_cur < _moves_end)
            {
                value<QUIET> ();
                // Split positive(+ve) value from the list
                _moves_end = std::partition (_moves_cur, _moves_end, [](const ValMove &m) { return m.value > VALUE_ZERO; });
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
            _moves_end = generate<EVASION> (_moves_beg, _pos);
            if (_moves_cur < _moves_end-1)
            {
                value<EVASION> ();
            }
            break;

        case S_QUIET_CHECK:
            _moves_end = generate<QUIET_CHECK> (_moves_beg, _pos);
            break;

        case S_EVASION:
        case S_QSEARCH_WITH_CHECK:
        case S_QSEARCH_WITHOUT_CHECK:
        case S_PROBCUT:
        case S_RECAPTURE:
        case S_STOP:
            _stage = S_STOP;
            break;

        default:
            assert(false);
            break;
        }
    }

    /// next_move() is the most important method of the MovePicker class. It returns
    /// a new pseudo legal move every time it is called, until there are no more moves
    /// left. It picks the move with the biggest value from a list of generated moves
    /// taking care not to return the ttMove if it has already been searched.
    Move MovePicker::next_move ()
    {
        do
        {
            while (_moves_cur == _moves_end && _stage != S_STOP)
            {
                generate_next_stage ();
            }

            Move move;
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
                        if (_pos.see_sign (move) >= VALUE_ZERO)
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
                        && _pos.pseudo_legal (move)
                        && !_pos.capture (move)
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
                        && std::count (std::begin (_killers), std::end (_killers), move) == 0 // Not killer move
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

            case S_PROBCUT_CAPTURE:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (   move != _tt_move
                        && _pos.see (move) > _threshold
                       )
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            //case S_RECAPTURE:
            case S_ALL_RECAPTURE:
                do
                {
                    move = pick_best (_moves_cur++, _moves_end);
                    if (   move != _tt_move
                        && dst_sq (move) == _recapture_sq
                       )
                    {
                        return move;
                    }
                } while (_moves_cur < _moves_end);
                break;

            case S_STOP:
                return MOVE_NONE;
                break;

            default:
                assert(false);
                break;
            }
        } while (true);
    }

}
