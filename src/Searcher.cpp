#include "Searcher.h"
#include <iostream>

#include "UCI.h"
#include "Time.h"
#include "TimeManager.h"
#include "Position.h"
#include "PolyglotBook.h"
#include "Transposition.h"
#include "MoveGenerator.h"
#include "Evaluator.h"
#include "atomicstream.h"
#include "Log.h"

using namespace Searcher;

namespace {

    // Set to true to force running with one thread. Used for debugging
    const bool FakeSplit = false;

    // This is the minimum interval in msec between two check_time() calls
    const int32_t TimerResolution = 5;

    // Different node types, used as template parameter
    enum NodeType { Root, PV, NonPV, SplitPointRoot, SplitPointPV, SplitPointNonPV };


    // Futility lookup tables (initialized at startup) and their access functions
    Value   FutilityMargins   [16][64]; // [depth][moveNumber]
    int32_t FutilityMoveCounts[2][32];  // [improving][depth]

    inline Value futility_margin(Depth d, int32_t mn)
    {
        return (d < 7 * ONE_PLY) ?
            FutilityMargins[std::max(int32_t (d), 1)][std::min(mn, 63)] : 2 * VALUE_INFINITE;
    }

    // Reduction lookup tables (initialized at startup) and their access function
    int8_t Reductions[2][2][64][64]; // [pv][improving][depth][moveNumber]

    template<bool PVNode>
    inline Depth reduction(bool i, Depth d, int32_t mn)
    {
        return Depth (Reductions[PVNode][i][std::min(int32_t (d) / ONE_PLY, 63)][std::min(mn, 63)]);
    }

    // Dynamic razoring margin based on depth
    inline Value razor_margin(Depth d) { return Value(512 + 16 * int32_t (d)); }

    size_t pv_size, PVIdx;
    TimeManager time_mgr;
    double best_move_changes;
    Value draw_value[CLR_NO];
    //HistoryStats History;
    //GainsStats Gains;
    //CountermovesStats Countermoves;

    //template <NodeType N>
    //Value  search(Position &pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cut_node);

    //template <NodeType N, bool InCheck>
    //Value qsearch(Position &pos, Stack* ss, Value alpha, Value beta, Depth depth);

    void iter_deep_loop(Position &pos);

    Value value_to_tt (Value v, int32_t ply);
    Value value_fr_tt (Value v, int32_t ply);

    bool allows (const Position &pos, Move first, Move second);
    bool refutes(const Position &pos, Move first, Move second);
    std::string uci_pv_info(const Position &pos, int32_t depth, Value alpha, Value beta);

    struct Skill
    {
        int32_t level;
        Move    best;

        Skill(int32_t lvl)
            : level(lvl), best(MOVE_NONE) {}

        ~Skill()
        {
            if (enabled ()) // Swap best PV line with the sub-optimal one
            {
                std::swap(rootMoves[0], *std::find(rootMoves.begin (), rootMoves.end (), best ? best : pick_move()));
            }
        }

        bool enabled ()                  const { return level < 20; }
        bool time_to_pick(int32_t depth) const { return depth == 1 + level; }
        Move pick_move();

    };

} // namespace

// check_time() is called by the timer thread when the timer triggers. It is
// used to print debug info and, more important, to detect when we are out of
// available time and so stop the search.
void check_time()
{
    static Time::point lastInfoTime = Time::now();
    int64_t nodes = 0; // Workaround silly 'uninitialized' gcc warning

    if (Time::now() - lastInfoTime >= 1000)
    {
        lastInfoTime = Time::now();
        //dbg_print();
    }

    if (limits.ponder) return;

    if (limits.nodes)
    {
        //Threads.mutex.lock();

        nodes = rootPos.game_nodes ();

        // Loop across all split points and sum accumulated SplitPoint nodes plus
        // all the currently active positions nodes.

        //for (size_t i = 0; i < Threads.size(); ++i)
        //{
        //    for (int j = 0; j < Threads[i]->splitPointsSize; ++j)
        //    {
        //        SplitPoint& sp = Threads[i]->splitPoints[j];

        //        sp.mutex.lock();

        //        nodes += sp.nodes;
        //        Bitboard sm = sp.slavesMask;
        //        while (sm)
        //        {
        //            Position* pos = Threads[pop_lsb(sm)]->activePosition;
        //            if (pos)
        //                nodes += pos->game_nodes();
        //        }

        //        sp.mutex.unlock();
        //    }
        //}
        //Threads.mutex.unlock();
    }

    Time::point elapsed = Time::point (Time::now() - searchTime);
    bool still_at_first_move = signals.first_root_move
        && !signals.failed_low_at_root
        &&  elapsed > time_mgr.available_time();

    bool noMoreTime = elapsed > time_mgr.maximum_time() - 2 * TimerResolution
        || still_at_first_move;

    if (   (limits.use_time_management() && noMoreTime)
        || (limits.move_time && elapsed >= limits.move_time)
        || (limits.nodes && nodes >= limits.nodes))
    {
        signals.stop = true;
    }
}


namespace Searcher {

    using namespace MoveGenerator;
    using std::atom;
    using std::endl;

    Limits                limits;
    volatile Signals      signals;

    std::vector<RootMove> rootMoves;
    Position              rootPos;
    Color                 rootColor;
    StateInfoStackPtr     setupStates;

    Time::point           searchTime;

    // initialize the PRNG only once
    PolyglotBook book;

#pragma region Root Move

    // RootMove::extract_pv_from_tt() builds a PV by adding moves from the TT table.
    // We consider also failing high nodes and not only BOUND_EXACT nodes so to
    // allow to always have a ponder move even when we fail high at root, and a
    // long PV to print that is important for position analysis.
    void RootMove::extract_pv_from_tt (Position &pos)
    {
        StateInfo states[MAX_PLY_PLUS_6], *st = states;

        const TranspositionEntry* te;
        uint16_t ply = 0;
        Move m = pv[ply];
        pv.clear ();

        do
        {
            pv.emplace_back (m);

            //ASSERT (MoveList<LEGAL>(pos).contains (pv[ply]));
            //ASSERT (generate<LEGAL>(pos).contains (pv[ply]));

            pos.do_move (pv[ply++], *st++);
            te = TT.retrieve (pos.posi_key ());

            // Local copy, TT could change
            if (!te || MOVE_NONE == (m = te->move ())) break;
            if (!pos.pseudo_legal (m) || !pos.legal (m)) break;
            if (!(ply < MAX_PLY && (!pos.draw () || ply < 2))) break;
        }
        while (true);

        pv.emplace_back (MOVE_NONE); // Must be zero-terminating

        while (ply)
        {
            pos.undo_move ();
            --ply;
        }

    }

    // RootMove::insert_pv_in_tt() is called at the end of a search iteration, and
    // inserts the PV back into the TT. This makes sure the old PV moves are searched
    // first, even if the old TT entries have been overwritten.
    void RootMove::insert_pv_into_tt (Position &pos)
    {
        StateInfo states[MAX_PLY_PLUS_6], *st = states;

        const TranspositionEntry* te;
        uint16_t ply = 0;

        do
        {
            te = TT.retrieve (pos.posi_key ());
            // Don't overwrite correct entries
            if (!te || te->move() != pv[ply])
            {
                TT.store (pos.posi_key (), pv[ply], DEPTH_NONE, UNKNOWN, VALUE_NONE, VALUE_NONE, VALUE_NONE);
            }

            //ASSERT (MoveList<LEGAL>(pos).contains (pv[ply]));
            //ASSERT (generate<LEGAL>(pos).contains (pv[ply]));

            pos.do_move (pv[ply++], *st++);
        }
        while (MOVE_NONE != pv[ply]);

        while (ply)
        {
            pos.undo_move ();
            --ply;
        }

    }


#pragma endregion

    void think ()
    {
        rootColor = rootPos.active ();

        //time_mgr.init(Limits, rootPos.game_ply(), rootColor);

        if (rootMoves.empty ())
        {
            rootMoves.push_back (MOVE_NONE);
            atom ()
                << "info depth 0 score "
                //<< score_to_uci (rootPos.checkers () ? -VALUE_MATE : VALUE_DRAW)
                << endl;

            goto finish;
        }

        if (*(Options["Own Book"]) && !limits.infinite && !limits.mate_in)
        {
            if (!book.is_open ()) book.open (*(Options["Book File"]), std::ios_base::in);
            Move book_move = book.probe_move (rootPos, *(Options["Best Book Move"]));
            if (book_move && std::count (rootMoves.begin (), rootMoves.end (), book_move))
            {
                std::swap (rootMoves[0], *std::find (rootMoves.begin (), rootMoves.end (), book_move));
                goto finish;
            }
        }

        if (*(Options["Write Search Log"]))
        {
            Log log (*(Options["Search Log File"]));
            log << "Searching: "    << rootPos.fen() << '\n'
                << " infinite: "    << limits.infinite
                << " ponder: "      << limits.ponder
                << " time: "        << limits.game_clock[rootColor].time
                << " increment: "   << limits.game_clock[rootColor].inc
                << " moves to go: " << limits.moves_to_go
                << endl;
        }



finish:

        // When search is stopped this info is not printed
        atom ()
            << "info nodes " << rootPos.game_nodes ()
            << " time " << Time::now() - searchTime + 1 << endl;

        // When we reach max depth we arrive here even without Signals.stop is raised,
        // but if we are pondering or in infinite search, according to UCI protocol,
        // we shouldn't print the best move before the GUI sends a "stop" or "ponderhit"
        // command. We simply wait here until GUI sends one of those commands (that
        // raise Signals.stop).
        if (!signals.stop && (limits.ponder || limits.infinite))
        {
            signals.stop_on_ponderhit = true;
            //rootPos.this_thread()->wait_for(signals.stop);
        }

        // Best move could be MOVE_NONE when searching on a stalemate position
        atom ()
            << "bestmove " //<< move_to_uci(rootMoves[0].pv[0], rootPos.chess960 ())
            << " ponder "  //<< move_to_uci(rootMoves[0].pv[1], rootPos.chess960 ())
            << endl;

    }

}

namespace {

    /*
    // iter_deep_loop() is the main iterative deepening loop. It calls search() repeatedly
    // with increasing depth until the allocated thinking time has been consumed,
    // user stops the search, or the maximum search depth is reached.
    void iter_deep_loop(Position &pos)
    {
    Stack stack[MAX_PLY_PLUS_6], *ss = stack+2; // To allow referencing (ss-2)
    int depth;
    Value best_value, alpha, beta, delta;

    std::memset(ss-2, 0, 5 * sizeof(Stack));
    (ss-1)->currentMove = MOVE_NULL; // Hack to skip update gains

    depth = 0;
    best_move_changes = 0;
    best_value = delta = alpha = -VALUE_INFINITE;
    beta = VALUE_INFINITE;

    TT.new_search();
    History.clear();
    Gains.clear();
    Countermoves.clear();

    pv_size = Options["MultiPV"];
    Skill skill(Options["Skill Level"]);

    // Do we have to play with skill handicap? In this case enable MultiPV search
    // that we will use behind the scenes to retrieve a set of possible moves.
    if (skill.enabled() && pv_size < 4)
    pv_size = 4;

    pv_size = std::min(pv_size, RootMoves.size());

    // Iterative deepening loop until requested to stop or target depth reached
    while (++depth <= MAX_PLY && !Signals.stop && (!Limits.depth || depth <= Limits.depth))
    {
    // Age out PV variability metric
    best_move_changes *= 0.8;

    // Save last iteration's scores before first PV line is searched and all
    // the move scores but the (new) PV are set to -VALUE_INFINITE.
    for (size_t i = 0; i < RootMoves.size(); ++i)
    RootMoves[i].prevScore = RootMoves[i].score;

    // MultiPV loop. We perform a full root search for each PV line
    for (PVIdx = 0; PVIdx < pv_size; ++PVIdx)
    {
    // Reset aspiration window starting size
    if (depth >= 5)
    {
    delta = Value(16);
    alpha = std::max(RootMoves[PVIdx].prevScore - delta,-VALUE_INFINITE);
    beta  = std::min(RootMoves[PVIdx].prevScore + delta, VALUE_INFINITE);
    }

    // Start with a small aspiration window and, in case of fail high/low,
    // research with bigger window until not failing high/low anymore.
    while (true)
    {
    best_value = search<Root>(pos, ss, alpha, beta, depth * ONE_PLY, false);

    // Bring to front the best move. It is critical that sorting is
    // done with a stable algorithm because all the values but the first
    // and eventually the new best one are set to -VALUE_INFINITE and
    // we want to keep the same order for all the moves but the new
    // PV that goes to the front. Note that in case of MultiPV search
    // the already searched PV lines are preserved.
    std::stable_sort(RootMoves.begin() + PVIdx, RootMoves.end());

    // Write PV back to transposition table in case the relevant
    // entries have been overwritten during the search.
    for (size_t i = 0; i <= PVIdx; ++i)
    RootMoves[i].insert_pv_in_tt(pos);

    // If search has been stopped return immediately. Sorting and
    // writing PV back to TT is safe becuase RootMoves is still
    // valid, although refers to previous iteration.
    if (Signals.stop)
    return;

    // When failing high/low give some update (without cluttering
    // the UI) before to research.
    if (  (best_value <= alpha || best_value >= beta)
    && Time::now() - SearchTime > 3000)
    sync_cout << uci_pv_info(pos, depth, alpha, beta) << sync_endl;

    // In case of failing low/high increase aspiration window and
    // research, otherwise exit the loop.
    if (best_value <= alpha)
    {
    alpha = std::max(best_value - delta, -VALUE_INFINITE);

    Signals.failedLowAtRoot = true;
    Signals.stopOnPonderhit = false;
    }
    else if (best_value >= beta)
    beta = std::min(best_value + delta, VALUE_INFINITE);

    else
    break;

    delta += delta / 2;

    assert(alpha >= -VALUE_INFINITE && beta <= VALUE_INFINITE);
    }

    // Sort the PV lines searched so far and update the GUI
    std::stable_sort(RootMoves.begin(), RootMoves.begin() + PVIdx + 1);

    if (PVIdx + 1 == pv_size || Time::now() - SearchTime > 3000)
    sync_cout << uci_pv_info(pos, depth, alpha, beta) << sync_endl;
    }

    // Do we need to pick now the sub-optimal best move ?
    if (skill.enabled() && skill.time_to_pick(depth))
    skill.pick_move();

    if (Options["Write Search Log"])
    {
    RootMove& rm = RootMoves[0];
    if (skill.best != MOVE_NONE)
    rm = *std::find(RootMoves.begin(), RootMoves.end(), skill.best);

    Log log(Options["Search Log Filename"]);
    log << pretty_pv(pos, depth, rm.score, Time::now() - SearchTime, &rm.pv[0])
    << std::endl;
    }

    // Do we have found a "mate in x"?
    if (   Limits.mate
    && best_value >= VALUE_MATE_IN_MAX_PLY
    && VALUE_MATE - best_value <= 2 * Limits.mate)
    Signals.stop = true;

    // Do we have time for the next iteration? Can we stop searching now?
    if (Limits.use_time_management() && !Signals.stopOnPonderhit)
    {
    bool stop = false; // Local variable, not the volatile Signals.stop

    // Take in account some extra time if the best move has changed
    if (depth > 4 && depth < 50 &&  pv_size == 1)
    time_mgr.pv_instability(best_move_changes);

    // Stop search if most of available time is already consumed. We
    // probably don't have enough time to search the first move at the
    // next iteration anyway.
    if (Time::now() - SearchTime > (time_mgr.available_time() * 62) / 100)
    stop = true;

    // Stop search early if one move seems to be much better than others
    if (    depth >= 12
    && !stop
    &&  pv_size == 1
    &&  best_value > VALUE_MATED_IN_MAX_PLY
    && (   RootMoves.size() == 1
    || Time::now() - SearchTime > (time_mgr.available_time() * 20) / 100))
    {
    Value rBeta = best_value - 2 * PawnValueMg;
    ss->excluded_move = RootMoves[0].pv[0];
    ss->skipNullMove = true;
    Value v = search<NonPV>(pos, ss, rBeta - 1, rBeta, (depth - 3) * ONE_PLY, true);
    ss->skipNullMove = false;
    ss->excluded_move = MOVE_NONE;

    if (v < rBeta)
    stop = true;
    }

    if (stop)
    {
    // If we are allowed to ponder do not stop the search now but
    // keep pondering until GUI sends "ponderhit" or "stop".
    if (Limits.ponder)
    Signals.stopOnPonderhit = true;
    else
    Signals.stop = true;
    }
    }
    }
    }
    */


    /*
    // search<>() is the main search function for both PV and non-PV nodes and for
    // normal and SplitPoint nodes. When called just after a split point the search
    // is simpler because we have already probed the hash table, done a null move
    // search, and searched the first move before splitting, we don't have to repeat
    // all this work again. We also don't need to store anything to the hash table
    // here: This is taken care of after we return from the split point.
    template <NodeType N>
    Value  search(Position &pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cut_node)
    {
    const bool PVNode   = (N == PV || N == Root || N == SplitPointPV || N == SplitPointRoot);
    const bool SPNode   = (N == SplitPointPV || N == SplitPointNonPV || N == SplitPointRoot);
    const bool RootNode = (N == Root || N == SplitPointRoot);

    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PVNode || (alpha == beta - 1));
    assert(depth > DEPTH_ZERO);

    Move quietsSearched[64];
    StateInfo st;
    const TTEntry *tte;
    SplitPoint* split_point;
    Key posi_key;
    Move tt_move, move, excluded_move, best_move, threat_move;
    Depth ext, new_depth;
    Value best_value, value, tt_value;
    Value eval, null_value, futility_value;
    bool in_check, gives_check, pvMove, singularExtensionNode, improving;
    bool captureOrPromotion, dangerous, doFullDepthSearch;
    int move_count, quiet_count;

    // Step 1. Initialize node
    Thread* thisThread = pos.this_thread();
    in_check = pos.checkers();

    if (SPNode)
    {
    split_point = ss->split_point;
    best_move   = split_point->best_move;
    threat_move = split_point->threat_move;
    best_value  = split_point->best_value;
    tte = NULL;
    tt_move = excluded_move = MOVE_NONE;
    tt_value = VALUE_NONE;

    assert(split_point->best_value > -VALUE_INFINITE && split_point->move_count > 0);

    goto moves_loop;
    }

    move_count = quiet_count = 0;
    best_value = -VALUE_INFINITE;
    ss->currentMove = threat_move = (ss+1)->excluded_move = best_move = MOVE_NONE;
    ss->ply = (ss-1)->ply + 1;
    ss->futilityMoveCount = 0;
    (ss+1)->skipNullMove = false; (ss+1)->reduction = DEPTH_ZERO;
    (ss+2)->killers[0] = (ss+2)->killers[1] = MOVE_NONE;

    // Used to send sel_depth info to GUI
    if (PVNode && thisThread->maxPly < ss->ply)
    thisThread->maxPly = ss->ply;

    if (!RootNode)
    {
    // Step 2. Check for aborted search and immediate draw
    if (Signals.stop || pos.is_draw() || ss->ply > MAX_PLY)
    return draw_value[pos.side_to_move()];

    // Step 3. Mate distance pruning. Even if we mate at the next move our score
    // would be at best mate_in(ss->ply+1), but if alpha is already bigger because
    // a shorter mate was found upward in the tree then there is no need to search
    // further, we will never beat current alpha. Same logic but with reversed signs
    // applies also in the opposite condition of being mated instead of giving mate,
    // in this case return a fail-high score.
    alpha = std::max(mated_in(ss->ply), alpha);
    beta = std::min(mate_in(ss->ply+1), beta);
    if (alpha >= beta)
    return alpha;
    }

    // Step 4. Transposition table lookup
    // We don't want the score of a partial search to overwrite a previous full search
    // TT value, so we use a different position key in case of an excluded move.
    excluded_move = ss->excluded_move;
    posi_key = excluded_move ? pos.exclusion_key() : pos.key();
    tte = TT.probe(posi_key);
    tt_move = RootNode ? RootMoves[PVIdx].pv[0] : tte ? tte->move() : MOVE_NONE;
    tt_value = tte ? value_fr_tt(tte->value(), ss->ply) : VALUE_NONE;

    // At PV nodes we check for exact scores, while at non-PV nodes we check for
    // a fail high/low. Biggest advantage at probing at PV nodes is to have a
    // smooth experience in analysis mode. We don't probe at Root nodes otherwise
    // we should also update RootMoveList to avoid bogus output.
    if (   !RootNode
    && tte
    && tte->depth() >= depth
    && tt_value != VALUE_NONE // Only in case of TT access race
    && (           PVNode ?  tte->bound() == BOUND_EXACT
    : tt_value >= beta ? (tte->bound() &  BOUND_LOWER)
    : (tte->bound() &  BOUND_UPPER)))
    {
    TT.refresh(tte);
    ss->currentMove = tt_move; // Can be MOVE_NONE

    if (    tt_value >= beta
    &&  tt_move
    && !pos.capture_or_promotion(tt_move)
    &&  tt_move != ss->killers[0])
    {
    ss->killers[1] = ss->killers[0];
    ss->killers[0] = tt_move;
    }
    return tt_value;
    }

    // Step 5. Evaluate the position statically and update parent's gain statistics
    if (in_check)
    {
    ss->staticEval = ss->evalMargin = eval = VALUE_NONE;
    goto moves_loop;
    }

    else if (tte)
    {
    // Never assume anything on values stored in TT
    if (  (ss->staticEval = eval = tte->eval_value()) == VALUE_NONE
    ||(ss->evalMargin = tte->eval_margin()) == VALUE_NONE)
    eval = ss->staticEval = evaluate(pos, ss->evalMargin);

    // Can tt_value be used as a better position evaluation?
    if (tt_value != VALUE_NONE)
    if (tte->bound() & (tt_value > eval ? BOUND_LOWER : BOUND_UPPER))
    eval = tt_value;
    }
    else
    {
    eval = ss->staticEval = evaluate(pos, ss->evalMargin);
    TT.store(posi_key, VALUE_NONE, BOUND_NONE, DEPTH_NONE, MOVE_NONE,
    ss->staticEval, ss->evalMargin);
    }

    // Update gain for the parent non-capture move given the static position
    // evaluation before and after the move.
    if (   !pos.captured_piece_type()
    &&  ss->staticEval != VALUE_NONE
    && (ss-1)->staticEval != VALUE_NONE
    && (move = (ss-1)->currentMove) != MOVE_NULL
    &&  type_of(move) == NORMAL)
    {
    Square to = to_sq(move);
    Gains.update(pos.piece_on(to), to, -(ss-1)->staticEval - ss->staticEval);
    }

    // Step 6. Razoring (skipped when in check)
    if (   !PVNode
    &&  depth < 4 * ONE_PLY
    &&  eval + razor_margin(depth) < beta
    &&  tt_move == MOVE_NONE
    &&  abs(beta) < VALUE_MATE_IN_MAX_PLY
    && !pos.pawn_on_7th(pos.side_to_move()))
    {
    Value rbeta = beta - razor_margin(depth);
    Value v = qsearch<NonPV, false>(pos, ss, rbeta-1, rbeta, DEPTH_ZERO);
    if (v < rbeta)
    // Logically we should return (v + razor_margin(depth)), but
    // surprisingly this did slightly weaker in tests.
    return v;
    }

    // Step 7. Static null move pruning (skipped when in check)
    // We're betting that the opponent doesn't have a move that will reduce
    // the score by more than futility_margin(depth) if we do a null move.
    if (   !PVNode
    && !ss->skipNullMove
    &&  depth < 4 * ONE_PLY
    &&  eval - futility_margin(depth, (ss-1)->futilityMoveCount) >= beta
    &&  abs(beta) < VALUE_MATE_IN_MAX_PLY
    &&  abs(eval) < VALUE_KNOWN_WIN
    &&  pos.non_pawn_material(pos.side_to_move()))
    return eval - futility_margin(depth, (ss-1)->futilityMoveCount);

    // Step 8. Null move search with verification search (is omitted in PV nodes)
    if (   !PVNode
    && !ss->skipNullMove
    &&  depth >= 2 * ONE_PLY
    &&  eval >= beta
    &&  abs(beta) < VALUE_MATE_IN_MAX_PLY
    &&  pos.non_pawn_material(pos.side_to_move()))
    {
    ss->currentMove = MOVE_NULL;

    // Null move dynamic reduction based on depth
    Depth R = 3 * ONE_PLY + depth / 4;

    // Null move dynamic reduction based on value
    if (eval - PawnValueMg > beta)
    R += ONE_PLY;

    pos.do_null_move(st);
    (ss+1)->skipNullMove = true;
    null_value = depth-R < ONE_PLY ? -qsearch<NonPV, false>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
    : - search<NonPV>(pos, ss+1, -beta, -alpha, depth-R, !cut_node);
    (ss+1)->skipNullMove = false;
    pos.undo_null_move();

    if (null_value >= beta)
    {
    // Do not return unproven mate scores
    if (null_value >= VALUE_MATE_IN_MAX_PLY)
    null_value = beta;

    if (depth < 12 * ONE_PLY)
    return null_value;

    // Do verification search at high depths
    ss->skipNullMove = true;
    Value v = search<NonPV>(pos, ss, alpha, beta, depth-R, false);
    ss->skipNullMove = false;

    if (v >= beta)
    return null_value;
    }
    else
    {
    // The null move failed low, which means that we may be faced with
    // some kind of threat. If the previous move was reduced, check if
    // the move that refuted the null move was somehow connected to the
    // move which was reduced. If a connection is found, return a fail
    // low score (which will cause the reduced move to fail high in the
    // parent node, which will trigger a re-search with full depth).
    threat_move = (ss+1)->currentMove;

    if (   depth < 5 * ONE_PLY
    && (ss-1)->reduction
    && threat_move != MOVE_NONE
    && allows(pos, (ss-1)->currentMove, threat_move))
    return alpha;
    }
    }

    // Step 9. ProbCut (skipped when in check)
    // If we have a very good capture (i.e. SEE > seeValues[captured_piece_type])
    // and a reduced search returns a value much above beta, we can (almost) safely
    // prune the previous move.
    if (   !PVNode
    &&  depth >= 5 * ONE_PLY
    && !ss->skipNullMove
    &&  abs(beta) < VALUE_MATE_IN_MAX_PLY)
    {
    Value rbeta = beta + 200;
    Depth rdepth = depth - ONE_PLY - 3 * ONE_PLY;

    assert(rdepth >= ONE_PLY);
    assert((ss-1)->currentMove != MOVE_NONE);
    assert((ss-1)->currentMove != MOVE_NULL);

    MovePicker mp(pos, tt_move, History, pos.captured_piece_type());
    CheckInfo ci(pos);

    while ((move = mp.next_move<false>()) != MOVE_NONE)
    if (pos.legal(move, ci.pinned))
    {
    ss->currentMove = move;
    pos.do_move(move, st, ci, pos.gives_check(move, ci));
    value = -search<NonPV>(pos, ss+1, -rbeta, -rbeta+1, rdepth, !cut_node);
    pos.undo_move(move);
    if (value >= rbeta)
    return value;
    }
    }

    // Step 10. Internal iterative deepening (skipped when in check)
    if (   depth >= (PVNode ? 5 * ONE_PLY : 8 * ONE_PLY)
    && tt_move == MOVE_NONE
    && (PVNode || ss->staticEval + Value(256) >= beta))
    {
    Depth d = depth - 2 * ONE_PLY - (PVNode ? DEPTH_ZERO : depth / 4);

    ss->skipNullMove = true;
    search<PVNode ? PV : NonPV>(pos, ss, alpha, beta, d, true);
    ss->skipNullMove = false;

    tte = TT.probe(posi_key);
    tt_move = tte ? tte->move() : MOVE_NONE;
    }

    moves_loop: // When in check and at SPNode search starts from here

    Square prevMoveSq = to_sq((ss-1)->currentMove);
    Move countermoves[] = { Countermoves[pos.piece_on(prevMoveSq)][prevMoveSq].first,
    Countermoves[pos.piece_on(prevMoveSq)][prevMoveSq].second };

    MovePicker mp(pos, tt_move, depth, History, countermoves, ss);
    CheckInfo ci(pos);
    value = best_value; // Workaround a bogus 'uninitialized' warning under gcc
    improving =   ss->staticEval >= (ss-2)->staticEval
    || ss->staticEval == VALUE_NONE
    ||(ss-2)->staticEval == VALUE_NONE;

    singularExtensionNode =   !RootNode
    && !SPNode
    &&  depth >= 8 * ONE_PLY
    &&  tt_move != MOVE_NONE
    && !excluded_move // Recursive singular search is not allowed
    && (tte->bound() & BOUND_LOWER)
    &&  tte->depth() >= depth - 3 * ONE_PLY;

    // Step 11. Loop through moves
    // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move<SPNode>()) != MOVE_NONE)
    {
    assert(is_ok(move));

    if (move == excluded_move)
    continue;

    // At root obey the "searchmoves" option and skip moves not listed in Root
    // Move List, as a consequence any illegal move is also skipped. In MultiPV
    // mode we also skip PV moves which have been already searched.
    if (RootNode && !std::count(RootMoves.begin() + PVIdx, RootMoves.end(), move))
    continue;

    if (SPNode)
    {
    // Shared counter cannot be decremented later if move turns out to be illegal
    if (!pos.legal(move, ci.pinned))
    continue;

    move_count = ++split_point->move_count;
    split_point->mutex.unlock();
    }
    else
    ++move_count;

    if (RootNode)
    {
    Signals.firstRootMove = (move_count == 1);

    if (thisThread == Threads.main() && Time::now() - SearchTime > 3000)
    sync_cout << "info depth " << depth / ONE_PLY
    << " currmove " << move_to_uci(move, pos.chess960 ())
    << " currmovenumber " << move_count + PVIdx << sync_endl;
    }

    ext = DEPTH_ZERO;
    captureOrPromotion = pos.capture_or_promotion(move);
    gives_check = pos.gives_check(move, ci);
    dangerous =   gives_check
    || pos.passed_pawn_push(move)
    || type_of(move) == CASTLE;

    // Step 12. Extend checks
    if (gives_check && pos.see_sign(move) >= 0)
    ext = ONE_PLY;

    // Singular extension search. If all moves but one fail low on a search of
    // (alpha-s, beta-s), and just one fails high on (alpha, beta), then that move
    // is singular and should be extended. To verify this we do a reduced search
    // on all the other moves but the tt_move, if result is lower than tt_value minus
    // a margin then we extend tt_move.
    if (    singularExtensionNode
    &&  move == tt_move
    && !ext
    &&  pos.legal(move, ci.pinned)
    &&  abs(tt_value) < VALUE_KNOWN_WIN)
    {
    assert(tt_value != VALUE_NONE);

    Value rBeta = tt_value - int(depth);
    ss->excluded_move = move;
    ss->skipNullMove = true;
    value = search<NonPV>(pos, ss, rBeta - 1, rBeta, depth / 2, cut_node);
    ss->skipNullMove = false;
    ss->excluded_move = MOVE_NONE;

    if (value < rBeta)
    ext = ONE_PLY;
    }

    // Update current move (this must be done after singular extension search)
    new_depth = depth - ONE_PLY + ext;

    // Step 13. Futility pruning (is omitted in PV nodes)
    if (   !PVNode
    && !captureOrPromotion
    && !in_check
    && !dangerous
    // &&  move != tt_move Already implicit in the next condition 
    &&  best_value > VALUE_MATED_IN_MAX_PLY)
    {
    // Move count based pruning
    if (   depth < 16 * ONE_PLY
    && move_count >= FutilityMoveCounts[improving][depth]
    && (!threat_move || !refutes(pos, move, threat_move)))
    {
    if (SPNode)
    split_point->mutex.lock();

    continue;
    }

    // Value based pruning
    // We illogically ignore reduction condition depth >= 3*ONE_PLY for predicted depth,
    // but fixing this made program slightly weaker.
    Depth predictedDepth = new_depth - reduction<PVNode>(improving, depth, move_count);
    futility_value =  ss->staticEval + ss->evalMargin + futility_margin(predictedDepth, move_count)
    + Gains[pos.moved_piece(move)][to_sq(move)];

    if (futility_value < beta)
    {
    best_value = std::max(best_value, futility_value);

    if (SPNode)
    {
    split_point->mutex.lock();
    if (best_value > split_point->best_value)
    split_point->best_value = best_value;
    }
    continue;
    }

    // Prune moves with negative SEE at low depths
    if (   predictedDepth < 4 * ONE_PLY
    && pos.see_sign(move) < 0)
    {
    if (SPNode)
    split_point->mutex.lock();

    continue;
    }

    // We have not pruned the move that will be searched, but remember how
    // far in the move list we are to be more aggressive in the child node.
    ss->futilityMoveCount = move_count;
    }
    else
    ss->futilityMoveCount = 0;

    // Check for legality only before to do the move
    if (!RootNode && !SPNode && !pos.legal(move, ci.pinned))
    {
    --move_count;
    continue;
    }

    pvMove = PVNode && move_count == 1;
    ss->currentMove = move;
    if (!SPNode && !captureOrPromotion && quiet_count < 64)
    quietsSearched[quiet_count++] = move;

    // Step 14. Make the move
    pos.do_move(move, st, ci, gives_check);

    // Step 15. Reduced depth search (LMR). If the move fails high will be
    // re-searched at full depth.
    if (    depth >= 3 * ONE_PLY
    && !pvMove
    && !captureOrPromotion
    &&  move != tt_move
    &&  move != ss->killers[0]
    &&  move != ss->killers[1])
    {
    ss->reduction = reduction<PVNode>(improving, depth, move_count);

    if (!PVNode && cut_node)
    ss->reduction += ONE_PLY;

    else if (History[pos.piece_on(to_sq(move))][to_sq(move)] < 0)
    ss->reduction += ONE_PLY / 2;

    if (move == countermoves[0] || move == countermoves[1])
    ss->reduction = std::max(DEPTH_ZERO, ss->reduction - ONE_PLY);

    Depth d = std::max(new_depth - ss->reduction, ONE_PLY);
    if (SPNode)
    alpha = split_point->alpha;

    value = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, d, true);

    doFullDepthSearch = (value > alpha && ss->reduction != DEPTH_ZERO);
    ss->reduction = DEPTH_ZERO;
    }
    else
    doFullDepthSearch = !pvMove;

    // Step 16. Full depth search, when LMR is skipped or fails high
    if (doFullDepthSearch)
    {
    if (SPNode)
    alpha = split_point->alpha;

    value = new_depth < ONE_PLY ?
    gives_check ? -qsearch<NonPV,  true>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
    : -qsearch<NonPV, false>(pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
    : - search<NonPV>(pos, ss+1, -(alpha+1), -alpha, new_depth, !cut_node);
    }

    // Only for PV nodes do a full PV search on the first move or after a fail
    // high, in the latter case search only if value < beta, otherwise let the
    // parent node to fail low with value <= alpha and to try another move.
    if (PVNode && (pvMove || (value > alpha && (RootNode || value < beta))))
    value = new_depth < ONE_PLY ?
    gives_check ? -qsearch<PV,  true>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
    : -qsearch<PV, false>(pos, ss+1, -beta, -alpha, DEPTH_ZERO)
    : - search<PV>(pos, ss+1, -beta, -alpha, new_depth, false);
    // Step 17. Undo move
    pos.undo_move(move);

    assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

    // Step 18. Check for new best move
    if (SPNode)
    {
    split_point->mutex.lock();
    best_value = split_point->best_value;
    alpha = split_point->alpha;
    }

    // Finished searching the move. If Signals.stop is true, the search
    // was aborted because the user interrupted the search or because we
    // ran out of time. In this case, the return value of the search cannot
    // be trusted, and we don't update the best move and/or PV.
    if (Signals.stop || thisThread->cutoff_occurred())
    return value; // To avoid returning VALUE_INFINITE

    if (RootNode)
    {
    RootMove& rm = *std::find(RootMoves.begin(), RootMoves.end(), move);

    // PV move or new best move ?
    if (pvMove || value > alpha)
    {
    rm.score = value;
    rm.extract_pv_from_tt(pos);

    // We record how often the best move has been changed in each
    // iteration. This information is used for time management: When
    // the best move changes frequently, we allocate some more time.
    if (!pvMove)
    ++best_move_changes;
    }
    else
    // All other moves but the PV are set to the lowest value, this
    // is not a problem when sorting becuase sort is stable and move
    // position in the list is preserved, just the PV is pushed up.
    rm.score = -VALUE_INFINITE;
    }

    if (value > best_value)
    {
    best_value = SPNode ? split_point->best_value = value : value;

    if (value > alpha)
    {
    best_move = SPNode ? split_point->best_move = move : move;

    if (PVNode && value < beta) // Update alpha! Always alpha < beta
    alpha = SPNode ? split_point->alpha = value : value;
    else
    {
    assert(value >= beta); // Fail high

    if (SPNode)
    split_point->cutoff = true;

    break;
    }
    }
    }

    // Step 19. Check for splitting the search
    if (   !SPNode
    &&  depth >= Threads.minimumSplitDepth
    &&  Threads.available_slave(thisThread)
    &&  thisThread->splitPointsSize < MAX_SPLITPOINTS_PER_THREAD)
    {
    assert(best_value < beta);

    thisThread->split<FakeSplit>(pos, ss, alpha, beta, &best_value, &best_move,
    depth, threat_move, move_count, &mp, N, cut_node);
    if (best_value >= beta)
    break;
    }
    }

    if (SPNode)
    return best_value;

    // Step 20. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it
    // must be mate or stalemate. Note that we can have a false positive in
    // case of Signals.stop or thread.cutoff_occurred() are set, but this is
    // harmless because return value is discarded anyhow in the parent nodes.
    // If we are in a singular extension search then return a fail low score.
    // A split node has at least one move, the one tried before to be splitted.
    if (!move_count)
    return  excluded_move ? alpha
    : in_check ? mated_in(ss->ply) : draw_value[pos.side_to_move()];

    // If we have pruned all the moves without searching return a fail-low score
    if (best_value == -VALUE_INFINITE) best_value = alpha;

    TT.store(posi_key, value_to_tt(best_value, ss->ply),
    best_value >= beta  ? BOUND_LOWER :
    PVNode && best_move ? BOUND_EXACT : BOUND_UPPER,
    depth, best_move, ss->staticEval, ss->evalMargin);

    // Quiet best move: update killers, history and countermoves
    if (    best_value >= beta
    && !pos.capture_or_promotion(best_move)
    && !in_check)
    {
    if (ss->killers[0] != best_move)
    {
    ss->killers[1] = ss->killers[0];
    ss->killers[0] = best_move;
    }

    // Increase history value of the cut-off move and decrease all the other
    // played non-capture moves.
    Value bonus = Value(int(depth) * int(depth));
    History.update(pos.moved_piece(best_move), to_sq(best_move), bonus);
    for (int i = 0; i < quiet_count - 1; ++i)
    {
    Move m = quietsSearched[i];
    History.update(pos.moved_piece(m), to_sq(m), -bonus);
    }

    if (is_ok((ss-1)->currentMove))
    Countermoves.update(pos.piece_on(prevMoveSq), prevMoveSq, best_move);
    }

    assert(best_value > -VALUE_INFINITE && best_value < VALUE_INFINITE);

    return best_value;
    }


    // qsearch() is the quiescence search function, which is called by the main
    // search function when the remaining depth is zero (or, to be more precise,
    // less than ONE_PLY).
    template <NodeType N, bool InCheck>
    Value qsearch(Position &pos, Stack* ss, Value alpha, Value beta, Depth depth)
    {
    const bool PVNode = (N == PV);

    assert(N == PV || N == NonPV);
    assert(InCheck == !!pos.checkers());
    assert(alpha >= -VALUE_INFINITE && alpha < beta && beta <= VALUE_INFINITE);
    assert(PVNode || (alpha == beta - 1));
    assert(depth <= DEPTH_ZERO);

    StateInfo st;
    const TTEntry* tte;
    Key posi_key;
    Move tt_move, move, best_move;
    Value best_value, value, tt_value, futility_value, futility_base, old_alpha;
    bool gives_check, evasion_prunable;
    Depth tt_depth;

    // To flag BOUND_EXACT a node with eval above alpha and no available moves
    if (PVNode) old_alpha = alpha;

    ss->currentMove = best_move = MOVE_NONE;
    ss->ply = (ss-1)->ply + 1;

    // Check for an instant draw or maximum ply reached
    if (pos.is_draw() || ss->ply > MAX_PLY)
    return draw_value[pos.side_to_move()];

    // Decide whether or not to include checks, this fixes also the type of
    // TT entry depth that we are going to use. Note that in qsearch we use
    // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
    tt_depth = InCheck || depth >= DEPTH_QS_CHECKS ? DEPTH_QS_CHECKS
    : DEPTH_QS_NO_CHECKS;

    // Transposition table lookup
    posi_key = pos.key();
    tte = TT.probe(posi_key);
    tt_move = tte ? tte->move() : MOVE_NONE;
    tt_value = tte ? value_fr_tt(tte->value(),ss->ply) : VALUE_NONE;

    if (   tte
    && tte->depth() >= tt_depth
    && tt_value != VALUE_NONE // Only in case of TT access race
    && (           PVNode ?  tte->bound() == BOUND_EXACT
    : tt_value >= beta ? (tte->bound() &  BOUND_LOWER)
    : (tte->bound() &  BOUND_UPPER)))
    {
    ss->currentMove = tt_move; // Can be MOVE_NONE
    return tt_value;
    }

    // Evaluate the position statically
    if (InCheck)
    {
    ss->staticEval = ss->evalMargin = VALUE_NONE;
    best_value = futility_base = -VALUE_INFINITE;
    }
    else
    {
    if (tte)
    {
    // Never assume anything on values stored in TT
    if (  (ss->staticEval = best_value = tte->eval_value()) == VALUE_NONE
    ||(ss->evalMargin = tte->eval_margin()) == VALUE_NONE)
    ss->staticEval = best_value = evaluate(pos, ss->evalMargin);

    // Can tt_value be used as a better position evaluation?
    if (tt_value != VALUE_NONE)
    if (tte->bound() & (tt_value > best_value ? BOUND_LOWER : BOUND_UPPER))
    best_value = tt_value;
    }
    else
    ss->staticEval = best_value = evaluate(pos, ss->evalMargin);

    // Stand pat. Return immediately if static value is at least beta
    if (best_value >= beta)
    {
    if (!tte)
    TT.store(pos.key(), value_to_tt(best_value, ss->ply), BOUND_LOWER,
    DEPTH_NONE, MOVE_NONE, ss->staticEval, ss->evalMargin);

    return best_value;
    }

    if (PVNode && best_value > alpha)
    alpha = best_value;

    futility_base = best_value + ss->evalMargin + Value(128);
    }

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= 0 here, only captures,
    // queen promotions and checks (only if depth >= DEPTH_QS_CHECKS) will
    // be generated.
    MovePicker mp(pos, tt_move, depth, History, to_sq((ss-1)->currentMove));
    CheckInfo ci(pos);

    // Loop through the moves until no moves remain or a beta cutoff occurs
    while ((move = mp.next_move<false>()) != MOVE_NONE)
    {
    assert(is_ok(move));

    gives_check = pos.gives_check(move, ci);

    // Futility pruning
    if (   !PVNode
    && !InCheck
    && !gives_check
    &&  move != tt_move
    &&  type_of(move) != PROMOTION
    &&  futility_base > -VALUE_KNOWN_WIN
    && !pos.passed_pawn_push(move))
    {
    futility_value =  futility_base
    + PieceValue[EG][pos.piece_on(to_sq(move))]
    + (type_of(move) == ENPASSANT ? PawnValueEg : VALUE_ZERO);

    if (futility_value < beta)
    {
    best_value = std::max(best_value, futility_value);
    continue;
    }

    // Prune moves with negative or equal SEE and also moves with positive
    // SEE where capturing piece loses a tempo and SEE < beta - futility_base.
    if (   futility_base < beta
    && pos.see(move, beta - futility_base) <= 0)
    {
    best_value = std::max(best_value, futility_base);
    continue;
    }
    }

    // Detect non-capture evasions that are candidate to be pruned
    evasion_prunable =    InCheck
    &&  best_value > VALUE_MATED_IN_MAX_PLY
    && !pos.capture(move)
    && !pos.can_castle(pos.side_to_move());

    // Don't search moves with negative SEE values
    if (   !PVNode
    && (!InCheck || evasion_prunable)
    &&  move != tt_move
    &&  type_of(move) != PROMOTION
    &&  pos.see_sign(move) < 0)
    continue;

    // Check for legality only before to do the move
    if (!pos.legal(move, ci.pinned))
    continue;

    ss->currentMove = move;

    // Make and search the move
    pos.do_move(move, st, ci, gives_check);
    value = gives_check ? -qsearch<N,  true>(pos, ss+1, -beta, -alpha, depth - ONE_PLY)
    : -qsearch<N, false>(pos, ss+1, -beta, -alpha, depth - ONE_PLY);
    pos.undo_move(move);

    assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

    // Check for new best move
    if (value > best_value)
    {
    best_value = value;

    if (value > alpha)
    {
    if (PVNode && value < beta) // Update alpha here! Always alpha < beta
    {
    alpha = value;
    best_move = move;
    }
    else // Fail high
    {
    TT.store(posi_key, value_to_tt(value, ss->ply), BOUND_LOWER,
    tt_depth, move, ss->staticEval, ss->evalMargin);

    return value;
    }
    }
    }
    }

    // All legal moves have been searched. A special case: If we're in check
    // and no legal moves were found, it is checkmate.
    if (InCheck && best_value == -VALUE_INFINITE)
    return mated_in(ss->ply); // Plies to mate from the root

    TT.store(posi_key, value_to_tt(best_value, ss->ply),
    PVNode && best_value > old_alpha ? BOUND_EXACT : BOUND_UPPER,
    tt_depth, best_move, ss->staticEval, ss->evalMargin);

    assert(best_value > -VALUE_INFINITE && best_value < VALUE_INFINITE);

    return best_value;
    }


    // value_to_tt() adjusts a mate score from "plies to mate from the root" to
    // "plies to mate from the current position". Non-mate scores are unchanged.
    // The function is called before storing a value to the transposition table.
    Value value_to_tt (Value v, int32_t ply)
    {
    assert(v != VALUE_NONE);
    return  v >= VALUE_MATE_IN_MAX_PLY  ? v + ply
    : v <= VALUE_MATED_IN_MAX_PLY ? v - ply : v;
    }

    // value_fr_tt() is the inverse of value_to_tt(): It adjusts a mate score
    // from the transposition table (where refers to the plies to mate/be mated
    // from current position) to "plies to mate/be mated from the root".
    Value value_fr_tt (Value v, int32_t ply)
    {
    return  v == VALUE_NONE             ? VALUE_NONE
    : v >= VALUE_MATE_IN_MAX_PLY  ? v - ply
    : v <= VALUE_MATED_IN_MAX_PLY ? v + ply : v;
    }


    // allows() tests whether the 'first' move at previous ply somehow makes the
    // 'second' move possible, for instance if the moving piece is the same in
    // both moves. Normally the second move is the threat (the best move returned
    // from a null search that fails low).
    bool allows(const Position &pos, Move first, Move second) {

    assert(is_ok(first));
    assert(is_ok(second));
    assert(color_of(pos.piece_on(from_sq(second))) == ~pos.side_to_move());
    assert(type_of(first) == CASTLE || color_of(pos.piece_on(to_sq(first))) == ~pos.side_to_move());

    Square m1from = from_sq(first);
    Square m2from = from_sq(second);
    Square m1to = to_sq(first);
    Square m2to = to_sq(second);

    // The piece is the same or second's destination was vacated by the first move
    // We exclude the trivial case where a sliding piece does in two moves what
    // it could do in one move: eg. Ra1a2, Ra2a3.
    if (    m2to == m1from
    || (m1to == m2from && !squares_aligned(m1from, m2from, m2to)))
    return true;

    // Second one moves through the square vacated by first one
    if (between_bb(m2from, m2to) & m1from)
    return true;

    // Second's destination is defended by the first move's piece
    Bitboard m1att = pos.attacks_from(pos.piece_on(m1to), m1to, pos.pieces() ^ m2from);
    if (m1att & m2to)
    return true;

    // Second move gives a discovered check through the first's checking piece
    if (m1att & pos.king_square(pos.side_to_move()))
    {
    assert(between_bb(m1to, pos.king_square(pos.side_to_move())) & m2from);
    return true;
    }

    return false;
    }

    // refutes() tests whether a 'first' move is able to defend against a 'second'
    // opponent's move. In this case will not be pruned. Normally the second move
    // is the threat (the best move returned from a null search that fails low).
    bool refutes(const Position &pos, Move first, Move second) {

    assert(is_ok(first));
    assert(is_ok(second));

    Square m1from = from_sq(first);
    Square m2from = from_sq(second);
    Square m1to = to_sq(first);
    Square m2to = to_sq(second);

    // Don't prune moves of the threatened piece
    if (m1from == m2to)
    return true;

    // If the threatened piece has value less than or equal to the value of the
    // threat piece, don't prune moves which defend it.
    if (    pos.capture(second)
    && (   PieceValue[MG][pos.piece_on(m2from)] >= PieceValue[MG][pos.piece_on(m2to)]
    || type_of(pos.piece_on(m2from)) == KING))
    {
    // Update occupancy as if the piece and the threat are moving
    Bitboard occ = pos.pieces() ^ m1from ^ m1to ^ m2from;
    Piece pc = pos.piece_on(m1from);

    // The moved piece attacks the square 'tto' ?
    if (pos.attacks_from(pc, m1to, occ) & m2to)
    return true;

    // Scan for possible X-ray attackers behind the moved piece
    Bitboard xray =  (attacks_bb<  ROOK>(m2to, occ) & pos.pieces(color_of(pc), QUEEN, ROOK))
    | (attacks_bb<BISHOP>(m2to, occ) & pos.pieces(color_of(pc), QUEEN, BISHOP));

    // Verify attackers are triggered by our move and not already existing
    if (unlikely(xray) && (xray & ~pos.attacks_from<QUEEN>(m2to)))
    return true;
    }

    // Don't prune safe moves which block the threat path
    if ((between_bb(m2from, m2to) & m1to) && pos.see_sign(first) >= 0)
    return true;

    return false;
    }


    // When playing with strength handicap choose best move among the MultiPV set
    // using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
    Move Skill::pick_move() {

    static RKISS rk;

    // PRNG sequence should be not deterministic
    for (int32_t i = Time::now() % 50; i > 0; --i)
    rk.rand<unsigned>();

    // RootMoves are already sorted by score in descending order
    int32_t variance = std::min(RootMoves[0].score - RootMoves[pv_size - 1].score, PawnValueMg);
    int32_t weakness = 120 - 2 * level;
    int32_t max_s = -VALUE_INFINITE;
    best = MOVE_NONE;

    // Choose best move. For each move score we add two terms both dependent on
    // weakness, one deterministic and bigger for weaker moves, and one random,
    // then we choose the move with the resulting highest score.
    for (size_t i = 0; i < pv_size; ++i)
    {
    int32_t s = RootMoves[i].score;

    // Don't allow crazy blunders even at very low skills
    if (i > 0 && RootMoves[i-1].score > s + 2 * PawnValueMg)
    break;

    // This is our magic formula
    s += (  weakness * int32_t(RootMoves[0].score - s)
    + variance * (rk.rand<unsigned>() % weakness)) / 128;

    if (s > max_s)
    {
    max_s = s;
    best = RootMoves[i].pv[0];
    }
    }
    return best;
    }
    */

    // uci_pv_info() formats PV information according to UCI protocol. UCI requires
    // to send all the PV lines also if are still to be searched and so refer to
    // the previous search score.
    std::string uci_pv_info(const Position &pos, int32_t depth, Value alpha, Value beta)
    {
        std::stringstream spv;

        Time::point elapsed = Time::point (Time::now() - searchTime + 1);
        size_t uci_pv_size = std::min(size_t (int32_t (*(Options["MultiPV"]))), rootMoves.size());
        int32_t sel_depth = 0;

        //for (size_t i = 0; i < Threads.size(); ++i)
        //{
        //    if (Threads[i]->maxPly > sel_depth)
        //        sel_depth = Threads[i]->maxPly;
        //}

        for (size_t i = 0; i < uci_pv_size; ++i)
        {
            bool updated = (i <= PVIdx);

            if ((1 == depth) && !updated) continue;

            int32_t d = updated ? depth : depth - 1;
            Value   v = updated ? rootMoves[i].curr_value : rootMoves[i].last_value;

            // Not at first line
            if (spv.rdbuf()->in_avail()) spv << "\n";

            spv << "info depth " << d
                << " seldepth "  << sel_depth
                //<< " score "     << (i == PVIdx ? score_to_uci(v, alpha, beta) : score_to_uci(v))
                //<< " nodes "     << pos.game_nodes ()
                //<< " nps "       << pos.game_nodes () * 1000 / elapsed
                << " time "      << elapsed
                << " multipv "   << i + 1
                << " pv";

            for (size_t j = 0; rootMoves[i].pv[j] != MOVE_NONE; ++j)
            {
                spv <<  " " << move_to_can(rootMoves[i].pv[j], pos.chess960 ());
            }
        }

        return spv.str();
    }

} // namespace
