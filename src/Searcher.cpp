#include "Searcher.h"
#include <iostream>

#include "atomicstream.h"
#include "Transposition.h"
#include "UCI.h"
#include "Log.h"

namespace Searcher {

    using namespace MoveGenerator;
    using std::atom;
    using std::endl;

    Limits               limits;
    volatile Signals     signals;

    std::vector<RootMove> root_moves;
    Position             rootPos;
    Color                rootColor;
    StateInfoStackPtr    setupStates;

    Time::point          searchTime;

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

        //TimeMgr.init(Limits, RootPos.game_ply(), RootColor);

        if (root_moves.empty ())
        {
            root_moves.push_back (MOVE_NONE);
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
            if (book_move && std::count (root_moves.begin (), root_moves.end (), book_move))
            {
                std::swap (root_moves[0], *std::find (root_moves.begin (), root_moves.end (), book_move));
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
            //<< "bestmove " << move_to_uci(RootMoves[0].pv[0], RootPos.is_chess960())
            //<< " ponder "  << move_to_uci(RootMoves[0].pv[1], RootPos.is_chess960())
            << endl;

    }


#pragma region search

    //    using namespace std;
    //
    //    static Score search_best (const Position &pos, Depth depth, Score alpha, Score beta);
    //
    //    static Score search_aspiration   (Position &pos, Depth depth, Score guess, Score threshold);
    //
    //    static Score search_iterative_deepening   (Position &pos, Depth depth, Score guess);
    //
    //    static Score alphaBetaSSS   (Position &pos, Depth depth);
    //
    //    static Score MTDf   (Position &pos, Depth depth, Score guess);
    //
    //
    //    //void Think  (Node &rootNode)
    //    //{
    //    //    Position &pos = rootNode.pos;
    //    //
    //    //
    //    //
    //    //}
    //
    //    //Score search(Node &node, Score alpha, Score beta, Depth depth, int16_t ply, bool nullMoveIsOK, int8_t totalExtension)
    //    //{
    //    //    return VALUE_DRAW;
    //    //}
    //
    //    // alpha-beta
    //    // nega-max
    //    // nega-scout
    //    //
    //    // search_best(pos, depth, -inf, +inf)
    //
    //    void search(const Position &pos, Depth depth)
    //    {
    //        Score s = search_best(pos, depth, -VALUE_INFINITE, VALUE_INFINITE);
    //        //std::cout << s;
    //        //tblTpos.FindMove(s);
    //    }
    //
    //    static Score search_best (const Position &pos, Depth depth, Score alpha, Score beta)
    //    {
    //        ASSERT ((-VALUE_INFINITE <= alpha) && (alpha < beta) && (beta  <= VALUE_INFINITE));
    //        ASSERT ((0 <= depth) && (depth < MAX_DEPTH));
    //
    //        //const TranspositionEntry *te = tblTpos.probeEntry(pos.KeyPosi());
    //        //if (te && te->Depth >= depth)
    //        //{
    //        //    //switch (te->Bound)
    //        //    //{
    //        //    //    // update lowerbound alpha if needed
    //        //    //case ALPHA: alpha = std::max<Score> (te->Score, alpha); break;
    //        //    //    // update upperbound beta if needed
    //        //    //case BETA:  beta  = std::min<Score> (te->Score, beta); break;
    //        //    //    // stored value is exact
    //        //    //case EXACT: return te->Score;   break;
    //
    //        //    //default:    break;
    //        //    //}
    //        //    // if lowerbound surpasses upperbound
    //        //    if (alpha >= beta) return te->Score;
    //        //}
    //
    //        if (depth <= 0 /*|| isTerminal(pos)*/)  // leaf-node test
    //        {
    //            Score eval = Evaluator::evaluate(pos);
    //
    //            //if (false);
    //            //else if (eval <= alpha) // a lowerbound eval
    //            //{
    //            //    tblTpos.storeEntry(pos.KeyPosi(), MOVE_NONE, eval, depth, ALPHA);
    //            //}
    //            //else if (eval >= beta) // an upperbound eval
    //            //{
    //            //    tblTpos.storeEntry(pos.KeyPosi(), MOVE_NONE, eval, depth, BETA);
    //            //}
    //            //else // a true minimax eval
    //            //{
    //            //    tblTpos.storeEntry(pos.KeyPosi(), MOVE_NONE, eval, depth, EXACT);
    //            //}
    //
    //            return eval;
    //        }
    //
    //        Score best      = -VALUE_INFINITE;
    //        Score adap_beta = beta;
    //
    //        Move  bestMove  = MOVE_NONE;
    //        const bool inCheck = pos.checkers();
    //
    //        MoveList lst_move;// legal lst_move
    //        const size_t n  = lst_move.size();
    //
    //        //for i=1 to sizeof (lst_move) do // rating all lst_move 
    //        //rating[i] = HistoryTable[ lst_move[i] ]; 
    //        //Sort(lst_move, rating);
    //
    //        StateInfo s_info;
    //
    //        //for (auto m : lst_move)
    //        for (size_t i = 0; i < n; ++i)
    //        {
    //            Move m  = lst_move[i];
    //
    //            //if (m == excludedMove)
    //            //{
    //            //    continue;
    //            //}
    //
    //            //std::string mm  = to_string(m);
    //            Position pos_i  = pos;
    //            pos_i.do_move(m, s_info);
    //
    //            Score curr  = -search_best(pos_i, depth-1, -adap_beta, -alpha);   // initial window is (-β, -α)
    //
    //            // check if null-window failed high
    //            if ((alpha < curr) && (curr < beta) && (i > 0) && (2 < depth) && (depth < MAX_DEPTH-1))
    //            { // full re-search
    //                curr    = -search_best(pos_i, depth-1, -beta, -curr);        // window is (-β, -α')
    //            }
    //
    //            if (curr > best)
    //            {
    //                best = curr;
    //
    //                if (best > alpha)
    //                {
    //                    bestMove = m;
    //
    //                    if (best >= beta)
    //                    {
    //                        goto finish;            // fail-hard beta cut-off
    //                    }
    //
    //                    ASSERT (best < beta);
    //                    alpha = best;
    //
    //                    adap_beta = alpha + 1;      // set new null window
    //                }
    //            }
    //        }
    //
    //        // No legal move was found. Check if it's checkmate or stalemate.
    //        if (-VALUE_INFINITE == best /*|| MOVE_NONE == bestMove*/)
    //        {
    //            if (inCheck)
    //            { // checkmate
    //                best = mated_in(depth);
    //            }
    //            else
    //            { // stalemate
    //                best = VALUE_DRAW;
    //                //best = variation->drawScore[position->activeColor];
    //            }
    //        }
    //
    //finish:
    //
    //        if (MOVE_NONE != bestMove)
    //        {
    //            // update history score 
    //            //HistoryTable[bestMove] = HistoryTable[bestMove] + Weight(d); 
    //        }
    //
    //
    //        //if (false);
    //        //else if (best <= alpha) // a lowerbound eval
    //        //{
    //        //    tblTpos.storeEntry(pos.KeyPosi(), bestMove, best, depth, ALPHA);
    //        //}
    //        //else if (best >= beta) // an upperbound eval
    //        //{
    //        //    tblTpos.storeEntry(pos.KeyPosi(), bestMove, best, depth, BETA);
    //        //}
    //        //else // a true minimax eval
    //        //{
    //        //    tblTpos.storeEntry(pos.KeyPosi(), bestMove, best, depth, EXACT);
    //        //}
    //
    //        ASSERT (-VALUE_INFINITE <= best && best <= VALUE_INFINITE);
    //        return best;
    //    }
    //
    //    // depth = 5, threshold = 100, window = 2*threshold;
    //    static Score search_aspiration   (Position &pos, Depth depth, Score guess, Score threshold)
    //    {
    //        ASSERT ((0 <= depth) && (depth < MAX_DEPTH));
    //        Score alpha, beta;
    //        Score upper_bound = guess - threshold;
    //        Score lower_bound = guess + threshold;
    //
    //        Score best  = search_best(pos, depth, upper_bound, lower_bound);
    //
    //        if (best >= beta)   // fail high - beta cut off
    //        {
    //            return search_best(pos, depth, best, VALUE_INFINITE);
    //        }
    //        if (best <= alpha)  // fail low - alpha cut off
    //        {
    //            return search_best(pos, depth, -VALUE_INFINITE, best);
    //        }
    //        return best;
    //    }
    //
    //    static Score search_iterative_deepening   (Position &pos, Depth depth, Score guess)
    //    {
    //        ASSERT ((0 <= depth) && (depth < MAX_DEPTH));
    //
    //        Score best = guess;
    //        for (int8_t d = 1; d <= depth; ++d)
    //        {
    //            //best = searchMethod(pos, d, best);
    //            //if timeUp() 
    //            //    break;
    //        }
    //        return best;
    //    }
    //
    //    static Score alphaBetaSSS   (Position &pos, Depth depth)
    //    {
    //        ASSERT ((0 <= depth) && (depth < MAX_DEPTH));
    //        Score best;
    //
    //        //best  = VALUE_INFINITE;
    //        //Score beta;
    //        //do
    //        //{
    //        //    beta    = best;
    //        //    best    = search_best(pos, depth, beta-1, beta);
    //        //}
    //        //while (best != beta);
    //
    //        best  = -VALUE_INFINITE;
    //        Score alpha;
    //        do
    //        {
    //            alpha   = best;
    //            best    = search_best(pos, depth, alpha, alpha+1);
    //        }
    //        while (best != alpha);
    //
    //        return best;
    //    }
    //
    //    static Score MTDf   (Position &pos, Depth depth, Score guess)
    //    {
    //        ASSERT ((0 <= depth) && (depth < MAX_DEPTH));
    //
    //        Score best = guess;
    //
    //        Score upper_bound = VALUE_INFINITE;
    //        Score lower_bound = -VALUE_INFINITE;
    //        do
    //        {
    //            // beta bound
    //            Score bound = (Score) (best == lower_bound) ? best + 1 : best;
    //
    //            best = search_best(pos, depth, bound - 1, bound);
    //
    //            if (best < bound) 
    //                upper_bound = best;
    //            else
    //                lower_bound = best;
    //        }
    //        while (lower_bound < upper_bound);
    //
    //        return best;
    //    }
    //
    //
    //    //static Score MaxiMin(Position &pos, Depth depth)
    //    //{
    //    //    if (depth <= 0 /*|| isTerminal(pos)*/)
    //    //    {
    //    //        return -evaluate(pos);
    //    //    }
    //    //
    //    //    Score minScore = VALUE_INFINITE;
    //    //
    //    //    MoveList lst_move = pos.GenerateLegalMoves();
    //    //    for (MoveList::const_iterator itr = lst_move.cbegin(); itr != lst_move.cend(); ++itr)
    //    //        //for (auto m : lst_move)
    //    //    {
    //    //        Move m  = *itr;
    //    //        Position c_pos = pos;
    //    //        //c_pos.do_move(m);
    //    //        Score curScore = MiniMax(c_pos, depth-1);
    //    //        if (curScore < minScore)
    //    //        {
    //    //            minScore = curScore;
    //    //        }
    //    //    }
    //    //
    //    //    return minScore;
    //    //}
    //    //static Score MiniMax(Position &pos, Depth depth) 
    //    //{
    //    //    if (depth <= 0 /*|| isTerminal(pos)*/)
    //    //    {
    //    //        return evaluate(pos);
    //    //    }
    //    //
    //    //    Score maxScore = -VALUE_INFINITE;
    //    //
    //    //    MoveList lst_move = pos.GenerateLegalMoves();
    //    //    for (MoveList::const_iterator itr = lst_move.cbegin(); itr != lst_move.cend(); ++itr)
    //    //        //for (auto m : lst_move)
    //    //    {
    //    //        Move m  = *itr;
    //    //        Position c_pos = pos;
    //    //        //c_pos.do_move(m);
    //    //        Score curScore = MaxiMin(c_pos, depth-1);
    //    //        if (curScore > maxScore)
    //    //        {
    //    //            maxScore = curScore;
    //    //        }
    //    //    }
    //    //
    //    //    return maxScore;
    //    //}
    //    //static Score NegaMax(Position &pos, Depth depth)
    //    //{
    //    //    if (depth <= 0 /*|| isTerminal(pos)*/)
    //    //    {
    //    //        return evaluate(pos);
    //    //    }
    //    //
    //    //    Score maxScore = -VALUE_INFINITE; // -oo;
    //    //
    //    //    MoveList lst_move = pos.GenerateLegalMoves();
    //    //    for (MoveList::const_iterator itr = lst_move.cbegin(); itr != lst_move.cend(); ++itr)
    //    //        //for (auto m : lst_move)
    //    //    {
    //    //        Move m  = *itr;
    //    //        Position c_pos = pos;
    //    //        //c_pos.do_move(m);
    //    //        Score curScore = -NegaMax(c_pos, depth-1);
    //    //        if (curScore > maxScore)
    //    //        {        
    //    //            maxScore = curScore;
    //    //        }
    //    //    }
    //    //
    //    //    return maxScore;
    //    //}
    //
    //    //static Score MaxiMinAlphaBeta   (Position &pos, Depth depth, Score alpha, Score beta)
    //    //{
    //    //    if (depth <= 0 /*|| isTerminal(pos)*/)
    //    //    {
    //    //        return -evaluate(pos);
    //    //    }
    //    //
    //    //    Score bestScore = VALUE_INFINITE;
    //    //
    //    //    MoveList lst_move = pos.GenerateLegalMoves();
    //    //    for (MoveList::const_iterator itr = lst_move.cbegin(); itr != lst_move.cend(); ++itr)
    //    //        //for (auto m : lst_move)
    //    //    {
    //    //        Move m  = *itr;
    //    //        Position c_pos  = pos;
    //    //        //c_pos.do_move(m);
    //    //        Score curScore  = MiniMaxAlphaBeta(c_pos, depth-1, alpha, beta);
    //    //
    //    //        if (curScore < bestScore)
    //    //        {
    //    //            bestScore = curScore;
    //    //
    //    //            if (bestScore < beta)
    //    //            {
    //    //                if (bestScore <= alpha)
    //    //                {
    //    //                    break;              // fail hard alpha-cutoff
    //    //                }
    //    //                ASSERT (bestScore > alpha);
    //    //                beta = bestScore;       // beta acts like min
    //    //            }
    //    //        }
    //    //    }
    //    //
    //    //    return bestScore;
    //    //}
    //    //static Score MiniMaxAlphaBeta   (Position &pos, Depth depth, Score alpha, Score beta)
    //    //{
    //    //    if (depth <= 0 /*|| isTerminal(pos)*/)
    //    //    {
    //    //        return evaluate(pos);
    //    //    }
    //    //
    //    //    Score bestScore = -VALUE_INFINITE;
    //    //
    //    //    MoveList lst_move  = pos.GenerateLegalMoves();
    //    //    for (MoveList::const_iterator itr = lst_move.cbegin(); itr != lst_move.cend(); ++itr)
    //    //        //for (auto m : lst_move)
    //    //    {
    //    //        Move m  = *itr;
    //    //        Position c_pos  = pos;
    //    //        //c_pos.do_move(m);
    //    //        Score curScore  = MaxiMinAlphaBeta(c_pos, depth-1, alpha, beta);
    //    //
    //    //        if (curScore > bestScore)
    //    //        {
    //    //            bestScore = curScore;
    //    //
    //    //            if (bestScore > alpha)
    //    //            {
    //    //                if (bestScore >= beta)
    //    //                {
    //    //                    break;              // fail hard beta-cutoff
    //    //                }
    //    //                ASSERT (bestScore < beta);
    //    //                alpha = bestScore;      // alpha acts like max
    //    //            }
    //    //        }
    //    //    }
    //    //
    //    //    return bestScore;
    //    //}
    //
    //    //static Score NegaMaxAlphaBeta   (Position &pos, Depth depth, Score alpha, Score beta)
    //    //{
    //    //    ASSERT (-VALUE_INFINITE <= alpha && alpha <= VALUE_INFINITE);
    //    //    ASSERT (-VALUE_INFINITE <= beta  && beta  <= VALUE_INFINITE);
    //    //    ASSERT (alpha < beta);
    //    //    ASSERT (0 < depth && depth < MAX_DEPTH);
    //    //
    //    //    if (depth <= 0 /*|| isTerminal(pos)*/)  // cutoff test
    //    //    {
    //    //        return evaluate(pos);
    //    //    }
    //    //
    //    //    Score bestScore = -VALUE_INFINITE;
    //    //    Move  bestMove  = MOVE_NONE;
    //    //
    //    //    Score alpha_old = alpha;
    //    //
    //    //    const bool inCheck = pos.checkers();
    //    //
    //    //    MoveList lst_move  = pos.GenerateLegalMoves();
    //    //
    //    //    //for i =1 to sizeof (lst_move) do // rating all lst_move 
    //    //    //rating[i] = HistoryTable[ lst_move[i] ]; 
    //    //    //Sort( lst_move, rating );
    //    //
    //    //    for (MoveList::const_iterator itr = lst_move.cbegin(); itr != lst_move.cend(); ++itr)
    //    //        //for (auto m : lst_move)
    //    //    {
    //    //        Move m  = *itr;
    //    //        Position c_pos  = pos;
    //    //        //c_pos.do_move(m);
    //    //        Score curScore  = -NegaMaxAlphaBeta(c_pos, depth-1, -beta, -alpha);
    //    //
    //    //        if (curScore > bestScore)
    //    //        {
    //    //            bestScore = curScore;
    //    //
    //    //            if (bestScore > alpha)
    //    //            {
    //    //                bestMove = m;
    //    //
    //    //                if (bestScore >= beta)
    //    //                {
    //    //                    //break;
    //    //                    goto finish;
    //    //                }
    //    //
    //    //                ASSERT (bestScore < beta);
    //    //                alpha = bestScore;
    //    //            }
    //    //        }
    //    //
    //    //
    //    //    }
    //    //
    //    //    // No legal move was found. Check if it's checkmate or stalemate.
    //    //    if (-VALUE_INFINITE == bestScore /*|| MOVE_NONE == bestMove*/)
    //    //    {
    //    //        if (inCheck)
    //    //        { // checkmate
    //    //            bestScore += depth;
    //    //        }
    //    //        else
    //    //        { // stalemate
    //    //            //bestScore = variation->drawScore[position->activeColor];
    //    //        }
    //    //    }
    //    //
    //    //
    //    //finish:
    //    //
    //    //    if (MOVE_NONE != bestMove)
    //    //    {
    //    //
    //    //        // update history score 
    //    //        //HistoryTable[bestMove] = HistoryTable[bestMove] + Weight(d); 
    //    //    }
    //    //
    //    //    //TEType type = (bestScore > alpha_old) ? (bestScore >= beta ? ALPHA : EXACT) : BETA;
    //    //
    //    //    ASSERT (-VALUE_INFINITE <= bestScore && bestScore <= VALUE_INFINITE);
    //    //    return bestScore;
    //    //}
    //    //static Score NegaMaxAlphaBetaTT (Position &pos, Depth depth, Score alpha, Score beta)
    //    //{
    //    //    ASSERT (-VALUE_INFINITE <= alpha && alpha <= VALUE_INFINITE);
    //    //    ASSERT (-VALUE_INFINITE <= beta  && beta  <= VALUE_INFINITE);
    //    //    ASSERT (alpha < beta);
    //    //    ASSERT (0 < depth && depth < MAX_DEPTH);
    //    //
    //    //    if (depth <= 0 /*|| isTerminal(pos)*/)  // cutoff test
    //    //    {
    //    //        return evaluate(pos);
    //    //    }
    //    //
    //    //    // Probe the transposition table
    //    //    // todo::
    //    //
    //    //
    //    //    if (depth >= MAX_DEPTH)
    //    //    {
    //    //
    //    //
    //    //    }
    //    //
    //    //    Score bestScore = -VALUE_INFINITE;
    //    //    Move  bestMove  = MOVE_NONE;
    //    //
    //    //    Score alpha_old = alpha;
    //    //
    //    //    const bool inCheck = pos.checkers();
    //    //
    //    //    MoveList lst_move  = pos.GenerateLegalMoves();
    //    //    for (MoveList::const_iterator itr = lst_move.cbegin(); itr != lst_move.cend(); ++itr)
    //    //        //for (auto m : lst_move)
    //    //    {
    //    //        Move m  = *itr;
    //    //        Position c_pos  = pos;
    //    //        //c_pos.do_move(m);
    //    //        Score curScore  = -NegaMaxAlphaBeta(c_pos, depth-1, -beta, -alpha);
    //    //
    //    //        if (curScore > bestScore)
    //    //        {
    //    //            bestScore = curScore;
    //    //
    //    //            if (bestScore > alpha)
    //    //            {
    //    //                bestMove = m;
    //    //
    //    //                if (bestScore >= beta)
    //    //                {
    //    //                    goto finish;
    //    //                }
    //    //
    //    //                ASSERT (bestScore < beta);
    //    //                alpha = bestScore;
    //    //            }
    //    //        }
    //    //    }
    //    //
    //    //    // No legal move was found. Check if it's checkmate or stalemate.
    //    //    if (-VALUE_INFINITE == bestScore /*|| MOVE_NONE == bestMove*/)
    //    //    {
    //    //        if (inCheck)
    //    //        { // checkmate
    //    //            bestScore += depth;
    //    //        }
    //    //        else
    //    //        { // stalemate
    //    //            //bestScore = variation->drawScore[position->activeColor];
    //    //        }
    //    //    }
    //    //
    //    //
    //    //finish:
    //    //
    //    //    if (MOVE_NONE != bestMove)
    //    //    {
    //    //
    //    //
    //    //    }
    //    //
    //    //    //TEType type = (bestScore > alpha_old) ? (bestScore >= beta ? ALPHA : EXACT) : BETA;
    //    //
    //    //    ASSERT (-VALUE_INFINITE <= bestScore && bestScore <= VALUE_INFINITE);
    //    //    return bestScore;
    //    //}
    //
    //    //void addNodeToTable(int TYPE, int SCORE, int DEPTHLEFT)
    //    //{
    //    //    // add node with parameters
    //    //}
    //
    //    //int alphaBetaMax(Position &pos, int alpha, int beta, int height)
    //    //{
    //    //    if (height == 0) return evaluate();
    //    //    for (all lst_move)
    //    //    {
    //    //
    //    //        tableEntry entry = getEntryForNode(currentBoardConfig);
    //    //        bool entryLookupResult = false;
    //    //
    //    //        if (entry.height <= height)
    //    //        {
    //    //            if (entry.type == EXACT)
    //    //            {
    //    //                score = entry.score;
    //    //                entryLookupResult = true;
    //    //                // do I return here?
    //    //            }
    //    //            else if (entry.type == UPPER)
    //    //            {
    //    //                if (entry.score <= alpha)
    //    //                {
    //    //                    return alpha;
    //    //                }
    //    //            }
    //    //            else if (entry.type == LOWER)
    //    //            {
    //    //                if (entry.score >= beta)
    //    //                {
    //    //                    return beta;
    //    //                }
    //    //            }
    //    //        }
    //    //
    //    //        if (entryLookupResult == false)
    //    //            score = alphaBetaMin(alpha, beta, height-1);
    //    //
    //    //
    //    //        if (score >= beta)
    //    //        {
    //    //             addNodeToTable(LOWER, beta); // should "beta" be "score" instead?
    //    //             return beta; // beta-cutoff
    //    //        }
    //    //        if (score > alpha)
    //    //        {
    //    //            addNodeToTable(EXACT, score);
    //    //            alpha = score; // alpha acts like max in MiniMax
    //    //        }
    //    //    }
    //    //
    //    //    return alpha;
    //    //}
    //
    //    //int alphaBetaMin(Position &pos, int alpha, int beta, int height)
    //    //{
    //    //    if (height == 0) return -evaluate();
    //    //    for (all lst_move)
    //    //    {
    //    //        tableEntry entry = getEntryForNode(currentBoardConfig);
    //    //        bool entryLookupResult = false;
    //    //
    //    //        if (entry.height <= height)
    //    //        {
    //    //            if (entry.type == EXACT)
    //    //            {
    //    //                score = entry.score;
    //    //                entryLookupResult = true;
    //    //                // do i return here?
    //    //            }
    //    //
    //    //            else if (entry.type == UPPER)
    //    //            {
    //    //                if (entry.score <= alpha)
    //    //                {
    //    //                    return alpha;
    //    //                }
    //    //            }
    //    //            else if (entry.type == LOWER)
    //    //            {
    //    //                if (entry.score >= beta)
    //    //                {
    //    //                    return beta;
    //    //                }
    //    //            }
    //    //        }
    //    //
    //    //
    //    //        if (entryLookupResult == false)
    //    //          score = alphaBetaMax(alpha, beta, height-1);
    //    //
    //    //       if (score <= alpha)
    //    //       {
    //    //            addNodeToTable(UPPER, alpha); // should "alpha" be "score" instead?
    //    //            return alpha// alpha-cutoff
    //    //       }
    //    //       if (score < beta)
    //    //       {
    //    //           addNodeToTable(EXACT, score);
    //    //           beta = score; // beta acts like min in MiniMax
    //    //       }
    //    //    }
    //    //
    //    //     return beta;
    //    //}
    //
    //    //int alphaBetaTT(ChessBoard board, int depth, int alpha, int beta) 
    //    //{
    //    //    int value;
    //    //    TranspositionEntry te = GetTTEntry(board.getHashKey());
    //    //    if (te != null && te.depth >= depth)
    //    //    {
    //    //        if (te.type == EXACT_VALUE) // stored value is exact
    //    //            return te.value;
    //    //        if (te.type == LOWERBOUND && te.value > alpha) 
    //    //            alpha = te.value; // update lowerbound alpha if needed
    //    //        else if (te.type == UPPERBOUND && te.value < beta)
    //    //            beta = te.value; // update upperbound beta if needed
    //    //        if (alpha >= beta)
    //    //            return te.value; // if lowerbound surpasses upperbound
    //    //    }
    //    //    if (depth == 0 || board.isEnded())
    //    //    {
    //    //        value = evaluate(board);
    //    //        if (value <= alpha) // a lowerbound value
    //    //            StoreTTEntry(board.getHashKey(), value, LOWERBOUND, depth);
    //    //        else if (value >= beta) // an upperbound value
    //    //            StoreTTEntry(board.getHashKey(), value, UPPERBOUND, depth);
    //    //        else // a true minimax value
    //    //            StoreTTEntry(board.getHashKey(), value, EXACT, depth);
    //    //        return value;
    //    //    }
    //    //    board.getOrderedMoves();
    //    //    int best = -MATE-1;
    //    //    int move; ChessBoard nextBoard; 
    //    //    while (board.hasMoreMoves()) 
    //    //    {
    //    //        move = board.getNextMove();
    //    //        nextBoard = board.makeMove(move);
    //    //        value = -alphaBetaTT(nextBoard, depth-1,-beta,-alpha);
    //    //        if (value > best) 
    //    //            best = value;
    //    //        if (best > alpha)
    //    //            alpha = best;
    //    //        if (best >= beta)
    //    //            break;
    //    //    }
    //    //    if (best <= alpha) // a lowerbound value
    //    //        StoreTTEntry(board.getHashKey(), best, LOWERBOUND, depth);
    //    //    else if (best >= beta) // an upperbound value
    //    //        StoreTTEntry(board.getHashKey(), best, UPPERBOUND, depth);
    //    //    else // a true minimax value
    //    //        StoreTTEntry(board.getHashKey(), best, EXACT, depth);
    //    //    return best;
    //    //}

#pragma endregion

}
