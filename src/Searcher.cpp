#include "Searcher.h"

#include <cfloat>
#include <cmath>
#include <sstream>
#include <iomanip>

#include "Transposition.h"
#include "MoveGenerator.h"
#include "MovePicker.h"
#include "Material.h"
#include "Pawns.h"
#include "Evaluator.h"
#include "PolyglotBook.h"
#include "PRNG.h"
#include "Notation.h"
#include "Debugger.h"

using namespace std;

namespace Searcher {

    using namespace BitBoard;
    using namespace MoveGen;
    using namespace MovePick;
    using namespace Transposition;
    using namespace OpeningBook;
    using namespace Evaluator;
    using namespace Notation;
    using namespace Debugger;
    
    // MoveManager class is used to detect a so called 'easy move'; when PV is
    // stable across multiple search iterations we can fast return the best move.
    class MoveManager
    {
    private:
        Key  _posi_key = U64(0);
        Move _pv[3];
        
    public:
        i08 stable_count = 0;
        
        MoveManager () { clear (); }
        
        void clear ()
        {
            stable_count = 0;
            _posi_key = U64(0);
            std::fill (std::begin (_pv), std::end (_pv), MOVE_NONE);
        }
        
        Move easy_move (Key posi_key) const { return _posi_key == posi_key ? _pv[2] : MOVE_NONE; }
        
        void update (Position& pos, const MoveVector &pv)
        {
            assert (pv.size () >= 3);
            
            // Keep track of how many times in a row 3rd ply remains stable
            stable_count = (pv[2] == _pv[2]) ? stable_count + 1 : 0;
            
            if (!equal (pv.begin (), pv.begin () + 3, _pv))
            {
                std::copy (pv.begin (), pv.begin () + 3, _pv);
                
                StateInfo si[2];
                pos.do_move (pv[0], si[0], pos.gives_check (pv[0], CheckInfo (pos)));
                pos.do_move (pv[1], si[1], pos.gives_check (pv[1], CheckInfo (pos)));
                _posi_key = pos.posi_key ();
                pos.undo_move ();
                pos.undo_move ();
            }
        }
        
    };

    namespace {

// prefetch() preloads the given address in L1/L2 cache.
// This is a non-blocking function that doesn't stall
// the CPU waiting for data to be loaded from memory,
// which can be quite slow.
#ifdef PREFETCH

#   if defined(_MSC_VER) || defined(__INTEL_COMPILER)

#   include <xmmintrin.h> // Intel and Microsoft header for _mm_prefetch()

    void prefetch (const void *addr)
    {
#       if defined(__INTEL_COMPILER)
        {
            // This hack prevents prefetches from being optimized away by
            // Intel compiler. Both MSVC and gcc seem not be affected by this.
            __asm__ ("");
        }
#       endif
        _mm_prefetch (reinterpret_cast<const char*> (addr), _MM_HINT_T0);
    }

#   else

    void prefetch (const void *addr)
    {
        __builtin_prefetch (addr);
    }

#   endif

#else

    void prefetch (const void *) {}

#endif

        const i32 FutilityMarginDepth   = 7;
        // Futility margin lookup table (initialized at startup)
        // [depth]
        Value FutilityMargins[FutilityMarginDepth];

        const i32 RazorDepth    = 4;
        // Razoring margin lookup table (initialized at startup)
        // [depth]
        Value RazorMargins[RazorDepth];

        const i32 FutilityMoveCountDepth = 16;
        // Futility move count lookup table (initialized at startup)
        // [improving][depth]
        u08   FutilityMoveCounts[2][FutilityMoveCountDepth];

        const i32 ReductionDepth = 64;
        const u08 ReductionMoveCount = 64;
        // ReductionDepths lookup table (initialized at startup)
        // [pv][improving][depth][move_num]
        Depth ReductionDepths[2][2][ReductionDepth][ReductionMoveCount];

        template<bool PVNode>
        Depth reduction_depths (bool imp, Depth d, u08 mc)
        {
            return ReductionDepths[PVNode][imp][min (d/DEPTH_ONE, ReductionDepth-1)][min (mc/1, ReductionMoveCount-1)];
        }

        const i32 ProbCutDepth = 4;

        const i32 LateMoveReductionDepth = 2;
        const u08 FullDepthMoveCount = 1;

        Color   RootColor;
        i32     RootPly;
        
        u16     LimitPV;

        Value   DrawValue[CLR_NO]
            ,   BaseContempt[CLR_NO];

        bool    MateSearch;

        ofstream SearchLog;

        bool    FirstAutoSave;

        // Counter move history value statistics
        Value2DStats    CounterMovesHistoryValues;

        MoveManager     MoveMgr;

        // update_stats() updates killers, history, countermoves and countermoves history
        // stats for a quiet best move.
        void update_stats (const Position &pos, Stack *ss, Move move, Depth depth, Move *quiet_moves, u08 quiet_count)
        {
            if (ss->killer_moves[0] != move)
            {
                ss->killer_moves[1] = ss->killer_moves[0];
                ss->killer_moves[0] = move;
            }
            //// If more then 2 killer moves
            //if (std::count (std::begin (ss->killer_moves), std::end (ss->killer_moves), move) == 0)
            //{
            //    std::copy_backward (std::begin (ss->killer_moves), std::prev (std::end (ss->killer_moves)), std::end (ss->killer_moves));
            //    ss->killer_moves[0] = move;
            //}
            //else
            //if (ss->killer_moves[0] != move)
            //{
            //    std::swap (ss->killer_moves[0], *std::find (std::begin (ss->killer_moves), std::end (ss->killer_moves), move));
            //}

            auto bonus = Value((depth/DEPTH_ONE)*(depth/DEPTH_ONE));

            auto opp_move = (ss-1)->current_move;
            auto opp_move_dst = _ok (opp_move) ? dst_sq (opp_move) : SQ_NO;
            auto &opp_cmhv = opp_move_dst != SQ_NO ?
                CounterMovesHistoryValues[pos[opp_move_dst]][opp_move_dst] :
                CounterMovesHistoryValues[EMPTY][SQ_A1];

            auto *thread = pos.thread ();

            thread->history_values.update_history (pos, move, bonus);

            if (opp_move_dst != SQ_NO)
            {
                thread->counter_moves.update_move (pos, opp_move, move);
                 opp_cmhv.update_cm_history (pos, move, bonus);
            }

            // Decrease all the other played quiet moves
            for (u08 i = 0; i < quiet_count; ++i)
            {
                assert (quiet_moves[i] != move);

                thread->history_values.update_history (pos, quiet_moves[i], -bonus);

                if (opp_move_dst != SQ_NO)
                {
                    opp_cmhv.update_cm_history (pos, quiet_moves[i], -bonus);
                }
            }

            // Extra penalty for PV move in previous ply when it gets refuted
            if (   ss->firstmove_pv
                && opp_move_dst != SQ_NO
                && mtype (opp_move) != PROMOTE
                && pos.capture_type () == NONE
               )
            {
                auto own_move = (ss-2)->current_move;
                auto own_move_dst = _ok (own_move) ? dst_sq (own_move) : SQ_NO;
                if (own_move_dst != SQ_NO)
                {
                    auto &own_cmhv = CounterMovesHistoryValues[pos[own_move_dst]][own_move_dst];
                    own_cmhv.update_cm_history (pos, opp_move, -bonus - 2 * depth/DEPTH_ONE - 1);
                }
            }

        }

        // update_pv() add current move and appends child pv[]
        void update_pv (Move *pv, Move move, const Move *child_pv)
        {
            *pv++ = move;
            while (child_pv != nullptr && *child_pv != MOVE_NONE)
            {
                *pv++ = *child_pv++;
            }
            *pv = MOVE_NONE;
        }

        // value_to_tt() adjusts a mate score from "plies to mate from the root" to
        // "plies to mate from the current position". Non-mate scores are unchanged.
        // The function is called before storing a value to the transposition table.
        Value value_to_tt (Value v, i32 ply)
        {
            assert (v != VALUE_NONE);
            return v >= +VALUE_MATE_IN_MAX_DEPTH ? v + ply :
                   v <= -VALUE_MATE_IN_MAX_DEPTH ? v - ply :
                   v;
        }
        // value_of_tt() is the inverse of value_to_tt ():
        // It adjusts a mate score from the transposition table
        // (where refers to the plies to mate/be mated from current position)
        // to "plies to mate/be mated from the root".
        Value value_of_tt (Value v, i32 ply)
        {
            return v == VALUE_NONE               ? VALUE_NONE :
                   v >= +VALUE_MATE_IN_MAX_DEPTH ? v - ply :
                   v <= -VALUE_MATE_IN_MAX_DEPTH ? v + ply :
                   v;
        }

        // multipv_info() formats PV information according to UCI protocol.
        // UCI requires to send all the PV lines also if are still to be searched
        // and so refer to the previous search score.
        string multipv_info (const Position &pos, Depth depth, Value alpha, Value beta)
        {
            u32 elapsed_time = std::max (TimeMgr.elapsed_time (), 1U);
            assert (elapsed_time > 0);
            const RootMoveVector &root_moves = pos.thread ()->root_moves;
            size_t pv_index = pos.thread ()->pv_index;
            u64 game_nodes = Threadpool.game_nodes ();

            stringstream ss;

            for (u16 i = 0; i < LimitPV; ++i)
            {
                Depth d;
                Value v;

                if (i <= pv_index) // New updated value?
                {
                    d = depth;
                    v = root_moves[i].new_value;
                }
                else
                {
                    if (DEPTH_ONE == depth) continue;

                    d = depth - DEPTH_ONE;
                    v = root_moves[i].old_value;
                }

                // Not at first line
                if (ss.rdbuf ()->in_avail ()) ss << "\n";

                ss  << "info"
                    << " multipv "  << i + 1
                    << " depth "    << d/DEPTH_ONE
                    << " seldepth " << pos.thread ()->max_ply
                    << " score "    << to_string (v)
                    << (i == pv_index ? beta <= v ? " lowerbound" : v <= alpha ? " upperbound" : "" : "")
                    << " time "     << elapsed_time
                    << " nodes "    << game_nodes
                    << " nps "      << game_nodes * MILLI_SEC / elapsed_time;
                if (elapsed_time > MILLI_SEC) ss  << " hashfull " << TT.hash_full ();
                ss  << " pv"        << root_moves[i];

            }

            return ss.str ();
        }

        template<NodeT NT, bool InCheck>
        // quien_search<>() is the quiescence search function,
        // which is called by the main depth limited search function
        // when the remaining depth is ZERO or less.
        Value quien_search  (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth)
        {
            const bool    PVNode = NT == PV;

            assert (NT == PV || NT == NonPV);
            assert (InCheck == (pos.checkers () != U64(0)));
            assert (alpha >= -VALUE_INFINITE && alpha < beta && beta <= +VALUE_INFINITE);
            assert (PVNode || alpha == beta-1);
            assert (depth <= DEPTH_ZERO);

            ss->current_move = MOVE_NONE;
            ss->ply = (ss-1)->ply + 1;
            
            Value pv_alpha = -VALUE_INFINITE;
            Move  pv[MAX_DEPTH+1];

            if (PVNode)
            {
                // To flag EXACT a node with eval above alpha and no available moves
                pv_alpha = alpha;

                (ss+1)->pv = pv;
                (ss+0)->pv[0] = MOVE_NONE;
            }

            // Check for an immediate draw or maximum ply reached
            if (pos.draw () || ss->ply >= MAX_DEPTH)
            {
                return ss->ply >= MAX_DEPTH && !InCheck ? evaluate (pos) : DrawValue[pos.active ()];
            }

            assert (0 <= ss->ply && ss->ply < MAX_DEPTH);

            Move  tt_move    = MOVE_NONE
                , best_move  = MOVE_NONE;
            Value tt_value   = VALUE_NONE
                , best_value = -VALUE_INFINITE;
            Depth tt_depth   = DEPTH_NONE;
            Bound tt_bound   = BOUND_NONE;

            // Transposition table lookup
            Key posi_key = pos.posi_key ();
            bool tt_hit = false;
            auto *tte = TT.probe (posi_key, tt_hit);
            if (tt_hit)
            {
                tt_move  = tte->move ();
                tt_value = value_of_tt (tte->value (), ss->ply);
                tt_depth = tte->depth ();
                tt_bound = tte->bound ();
            }

            auto *thread = pos.thread ();
            // Decide whether or not to include checks, this fixes also the type of
            // TT entry depth that are going to use. Note that in quien_search use
            // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
            auto qs_depth = InCheck || depth >= DEPTH_QS_CHECKS ?
                                DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;

            if (   !PVNode
                && tt_hit
                && tt_depth >= qs_depth
                && tt_value != VALUE_NONE // Only in case of TT access race
                && (tt_value >= beta ? (tt_bound & BOUND_LOWER) :
                                       (tt_bound & BOUND_UPPER))
               )
            {
                ss->current_move = tt_move; // Can be MOVE_NONE
                return tt_value;
            }

            auto futility_base = -VALUE_INFINITE;
            // Evaluate the position statically
            if (InCheck)
            {
                ss->static_eval = VALUE_NONE;
            }
            else
            {
                if (tt_hit)
                {
                    best_value = tte->eval ();
                    // Never assume anything on values stored in TT
                    if (VALUE_NONE == best_value) best_value = evaluate (pos);
                    ss->static_eval = best_value;

                    // Can tt_value be used as a better position evaluation?
                    if (   tt_value != VALUE_NONE
                        && (tt_bound & (best_value < tt_value ? BOUND_LOWER : BOUND_UPPER))
                       )
                    {
                        best_value = tt_value;
                    }
                }
                else
                {
                    ss->static_eval = best_value =
                        (ss-1)->current_move != MOVE_NULL ? evaluate (pos) : -(ss-1)->static_eval + 2*TEMPO;
                }

                if (alpha < best_value)
                {
                    // Stand pat. Return immediately if static value is at least beta
                    if (best_value >= beta)
                    {
                        if (!tt_hit)
                        {
                            tte->save (posi_key, MOVE_NONE, value_to_tt (best_value, ss->ply), ss->static_eval, DEPTH_NONE, BOUND_LOWER, TT.generation ());
                        }

                        assert (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
                        return best_value;
                    }

                    assert (best_value < beta);
                    // Update alpha here! always alpha < beta
                    if (PVNode) alpha = best_value;
                }

                futility_base = best_value + VALUE_EG_PAWN/2; // QS Futility Margin
            }

            // Initialize a MovePicker object for the current position, and prepare
            // to search the moves. Because the depth is <= 0 here, only captures,
            // queen promotions and checks (only if depth >= DEPTH_QS_CHECKS) will
            // be generated.
            auto opp_move = (ss-1)->current_move; 
            MovePicker mp (pos, thread->history_values, CounterMovesHistoryValues, tt_move, depth, _ok (opp_move) ? dst_sq (opp_move) : SQ_NO);
            CheckInfo ci (pos);
            StateInfo si;
            Move move;
            // Loop through the moves until no moves remain or a beta cutoff occurs
            while ((move = mp.next_move ()) != MOVE_NONE)
            {
                assert (_ok (move));

                bool gives_check = pos.gives_check (move, ci);

                if (!MateSearch)
                {
                    // Futility pruning
                    if (   !InCheck
                        && !gives_check
                        && futility_base > -VALUE_KNOWN_WIN
                        && futility_base <= alpha
                        && !pos.advanced_pawn_push (move)
                       )
                    {
                        assert (mtype (move) != ENPASSANT); // Due to !pos.advanced_pawn_push()

                        auto futility_value = futility_base + PIECE_VALUE[EG][ptype (pos[dst_sq (move)])];

                        if (futility_value <= alpha)
                        {
                            best_value = std::max (futility_value, best_value);
                            continue;
                        }
                        // Prune moves with negative or zero SEE
                        if (pos.see (move) <= VALUE_ZERO)
                        {
                            best_value = std::max (futility_base, best_value);
                            continue;
                        }
                    }

                    // Don't search moves with negative SEE values
                    if (   mtype (move) != PROMOTE
                        && (  !InCheck
                            // Detect non-capture evasions that are candidate to be pruned (evasion_prunable)
                            || (   best_value > -VALUE_MATE_IN_MAX_DEPTH
                                && !pos.capture (move)
                               )
                           )
                        && pos.see_sign (move) < VALUE_ZERO
                       )
                    {
                        continue;
                    }
                }

                // Check for legality just before making the move
                if (!pos.legal (move, ci.pinneds)) continue;

                ss->current_move = move;

                // Speculative prefetch as early as possible
                prefetch (TT.cluster_entry (pos.move_posi_key (move)));

                // Make and search the move
                pos.do_move (move, si, gives_check);

                prefetch (thread->pawn_table[pos.pawn_key ()]);
                prefetch (thread->matl_table[pos.matl_key ()]);

                auto value =
                    gives_check ?
                        -quien_search<NT, true > (pos, ss+1, -beta, -alpha, depth-DEPTH_ONE) :
                        -quien_search<NT, false> (pos, ss+1, -beta, -alpha, depth-DEPTH_ONE);

                // Undo the move
                pos.undo_move ();

                assert (-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Check for new best move
                if (best_value < value)
                {
                    best_value = value;

                    if (alpha < value)
                    {
                        best_move = move;

                        if (PVNode)
                        {
                            update_pv (ss->pv, move, (ss+1)->pv);
                        }
                        // Fail high
                        if (value >= beta)
                        {
                            tte->save (posi_key, move, value_to_tt (value, ss->ply), ss->static_eval, qs_depth, BOUND_LOWER, TT.generation ());

                            assert (-VALUE_INFINITE < value && value < +VALUE_INFINITE);
                            return value;
                        }

                        assert (value < beta);
                        // Update alpha here! always alpha < beta
                        if (PVNode) alpha = value;
                    }
                }
            }
            
            // All legal moves have been searched.
            // A special case: If in check and no legal moves were found, it is checkmate.
            if (InCheck && best_value == -VALUE_INFINITE)
            {
                return mated_in (ss->ply); // Plies to mate from the root
            }

            tte->save (posi_key, best_move, value_to_tt (best_value, ss->ply), ss->static_eval, qs_depth,
                PVNode && pv_alpha < best_value ? BOUND_EXACT : BOUND_UPPER, TT.generation ());

            assert (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        template<NodeT NT, bool EarlyPruning>
        // depth_search<>() is the main depth limited search function
        // for Root/PV/NonPV nodes also for normal/splitpoint nodes.
        // It calls itself recursively with decreasing (remaining) depth
        // until we run out of depth, and then drops into quien_search.
        // When called just after a splitpoint the search is simpler because
        // already probed the hash table, done a null move search, and searched
        // the first move before splitting, don't have to repeat all this work again.
        // Also don't need to store anything to the hash table here.
        // This is taken care of after return from the splitpoint.
        Value depth_search  (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth, bool cut_node)
        {
            const bool RootNode = NT == Root;
            const bool   PVNode = NT == Root || NT == PV;

            assert (-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
            assert (PVNode || alpha == beta-1);
            assert (depth > DEPTH_ZERO);

            Key posi_key;
            bool tt_hit = false;
            auto *tte = (Entry*) nullptr;

            Move  move
                , tt_move     = MOVE_NONE
                , exclude_move= MOVE_NONE
                , best_move   = MOVE_NONE;

            Value tt_value    = VALUE_NONE
                , static_eval = VALUE_NONE
                , best_value  = -VALUE_INFINITE;

            Depth tt_depth    = DEPTH_NONE;
            Bound tt_bound    = BOUND_NONE;

            // Step 1. Initialize node
            auto *thread  = pos.thread ();
            bool in_check = pos.checkers () != U64(0);

            CheckInfo ci (pos);
            StateInfo si;

            ss->ply = (ss-1)->ply + 1;

            // Used to send 'seldepth' info to GUI
            if (PVNode && thread->max_ply < ss->ply)
            {
                thread->max_ply = ss->ply;
            }

            if (!RootNode)
            {
                // Step 2. Check end condition
                // Check for aborted search, immediate draw or maximum ply reached
                if (Signals.force_stop || pos.draw () || ss->ply >= MAX_DEPTH)
                {
                    return ss->ply >= MAX_DEPTH && !in_check ? evaluate (pos) : DrawValue[pos.active ()];
                }

                // Step 3. Mate distance pruning. Even if mate at the next move our score
                // would be at best mates_in(ss->ply+1), but if alpha is already bigger because
                // a shorter mate was found upward in the tree then there is no need to search
                // further, will never beat current alpha. Same logic but with reversed signs
                // applies also in the opposite condition of being mated instead of giving mate,
                // in this case return a fail-high score.
                alpha = std::max (mated_in (ss->ply +0), alpha);
                beta  = std::min (mates_in (ss->ply +1), beta);

                if (alpha >= beta) return alpha;
            }

            assert (0 <= ss->ply && ss->ply < MAX_DEPTH);
                
            (ss+0)->firstmove_pv = false;
            (ss+0)->current_move = MOVE_NONE;
            (ss+1)->exclude_move = MOVE_NONE;
            std::fill (std::begin ((ss+2)->killer_moves), std::end ((ss+2)->killer_moves), MOVE_NONE);

            // Step 4. Transposition table lookup
            // Don't want the score of a partial search to overwrite a previous full search
            // TT value, so use a different position key in case of an excluded move.
            exclude_move = ss->exclude_move;
            posi_key = exclude_move == MOVE_NONE ?
                        pos.posi_key () :
                        pos.posi_key () ^ Zobrist::EXC_KEY;

            tte      = TT.probe (posi_key, tt_hit);
            ss->tt_move = tt_move = RootNode ? thread->root_moves[thread->pv_index].pv[0] :
                                            tt_hit ? tte->move () : MOVE_NONE;
            if (tt_hit)
            {
                tt_value = value_of_tt (tte->value (), ss->ply);
                tt_depth = tte->depth ();
                tt_bound = tte->bound ();
            }

            // Don't prune at PV nodes. At non-PV nodes we check for a fail high/low.
            if (   !PVNode
                && tt_hit
                && tt_depth >= depth
                && tt_value != VALUE_NONE // Only in case of TT access race
                && (tt_value >= beta ? (tt_bound & BOUND_LOWER) :
                                        (tt_bound & BOUND_UPPER))
               )
            {
                ss->current_move = tt_move; // Can be MOVE_NONE

                // If tt_move is quiet, update killers, history, countermove and countermoves history on TT hit
                if (   tt_value >= beta
                    && tt_move != MOVE_NONE
                    && !pos.capture_or_promotion (tt_move)
                   )
                {
                    update_stats (pos, ss, tt_move, depth, nullptr, 0);
                }

                return tt_value;
            }

            // Step 5. Evaluate the position statically
            if (in_check)
            {
                ss->static_eval = static_eval = VALUE_NONE;
            }
            else
            {
                if (tt_hit)
                {
                    static_eval = tte->eval ();
                    // Never assume anything on values stored in TT
                    if (VALUE_NONE == static_eval) static_eval = evaluate (pos);
                    ss->static_eval = static_eval;

                    // Can tt_value be used as a better position evaluation?
                    if (   tt_value != VALUE_NONE
                        && (tt_bound & (static_eval < tt_value ? BOUND_LOWER : BOUND_UPPER))
                       )
                    {
                        static_eval = tt_value;
                    }
                }
                else
                {
                    ss->static_eval = static_eval =
                        (ss-1)->current_move != MOVE_NULL ? evaluate (pos) : -(ss-1)->static_eval + 2*TEMPO;

                    tte->save (posi_key, MOVE_NONE, VALUE_NONE, ss->static_eval, DEPTH_NONE, BOUND_NONE, TT.generation ());
                }

                if (EarlyPruning)
                {
                    // Step 6. Razoring sort of forward pruning where rather than skipping an entire subtree,
                    // you search it to a reduced depth, typically one less than normal depth.
                    if (   !PVNode && !MateSearch
                        && depth < RazorDepth*DEPTH_ONE
                        && static_eval + RazorMargins[depth/DEPTH_ONE] <= alpha
                        && tt_move == MOVE_NONE
                       )
                    {
                        if (   depth <= 1*DEPTH_ONE
                            && static_eval + RazorMargins[3] <= alpha
                           )
                        {
                            return quien_search<NonPV, false> (pos, ss, alpha, beta, DEPTH_ZERO);
                        }

                        auto reduced_alpha = std::max (alpha - RazorMargins[depth/DEPTH_ONE], -VALUE_INFINITE);

                        auto value = quien_search<NonPV, false> (pos, ss, reduced_alpha, reduced_alpha+1, DEPTH_ZERO);

                        if (value <= reduced_alpha)
                        {
                            return value;
                        }
                    }

                    // Step 7. Futility pruning: child node
                    // Betting that the opponent doesn't have a move that will reduce
                    // the score by more than FutilityMargins[depth] if do a null move.
                    if (   !RootNode && !MateSearch
                        && depth < FutilityMarginDepth*DEPTH_ONE
                        && static_eval < +VALUE_KNOWN_WIN // Do not return unproven wins
                        && pos.non_pawn_material (pos.active ()) > VALUE_ZERO
                       )
                    {
                        auto stand_pat = static_eval - FutilityMargins[depth/DEPTH_ONE];

                        if (stand_pat >= beta)
                        {
                            return stand_pat;
                        }
                    }

                    // Step 8. Null move search with verification search
                    if (   !PVNode && !MateSearch
                        && depth > 1*DEPTH_ONE
                        && static_eval >= beta
                        && pos.non_pawn_material (pos.active ()) > VALUE_ZERO
                       )
                    {
                        assert ((ss-1)->current_move != MOVE_NONE && (ss-1)->current_move != MOVE_NULL);
                        assert (exclude_move == MOVE_NONE);

                        ss->current_move = MOVE_NULL;
                            
                        // Null move dynamic reduction based on depth and static evaluation
                        auto reduced_depth = depth - ((0x337 + 0x43 * depth) / 0x100 + std::min ((static_eval - beta)/VALUE_EG_PAWN, 3))*DEPTH_ONE;

                        // Speculative prefetch as early as possible
                        prefetch (TT.cluster_entry (pos.posi_key ()));
                            
                        // Do null move
                        pos.do_null_move (si);

                        prefetch (thread->pawn_table[pos.pawn_key ()]);
                        prefetch (thread->matl_table[pos.matl_key ()]);

                        // Null (zero) window (alpha, beta) = (beta-1, beta):
                        auto null_value =
                            reduced_depth < DEPTH_ONE ?
                                -quien_search<NonPV, false> (pos, ss+1, -beta, -(beta-1), DEPTH_ZERO) :
                                -depth_search<NonPV, false> (pos, ss+1, -beta, -(beta-1), reduced_depth, !cut_node);

                        // Undo null move
                        pos.undo_null_move ();

                        if (null_value >= beta)
                        {
                            // Don't do verification search at low depths
                            if (depth < 12*DEPTH_ONE && abs (beta) < +VALUE_KNOWN_WIN)
                            {
                                // Don't return unproven unproven mates
                                return null_value < +VALUE_MATE_IN_MAX_DEPTH ? null_value : beta;
                            }

                            // Do verification search at high depths
                            auto value =
                                reduced_depth < DEPTH_ONE ?
                                    quien_search<NonPV, false> (pos, ss, beta-1, beta, DEPTH_ZERO) :
                                    depth_search<NonPV, false> (pos, ss, beta-1, beta, reduced_depth, false);

                            if (value >= beta)
                            {
                                // Don't return unproven unproven mates
                                return null_value < +VALUE_MATE_IN_MAX_DEPTH ? null_value : beta;
                            }
                        }
                    }

                    // Step 9. ProbCut
                    // If have a very good capture (i.e. SEE > see[captured_piece_type])
                    // and a reduced search returns a value much above beta,
                    // can (almost) safely prune the previous move.
                    if (   !PVNode && !MateSearch
                        && depth > ProbCutDepth*DEPTH_ONE
                        && abs (beta) < +VALUE_MATE_IN_MAX_DEPTH
                       )
                    {
                        auto reduced_depth = depth - ProbCutDepth*DEPTH_ONE; // Shallow Depth
                        auto extended_beta = std::min (beta + VALUE_MG_PAWN, +VALUE_INFINITE); // ProbCut Threshold

                        assert (reduced_depth >= DEPTH_ONE);
                        assert ((ss-1)->current_move != MOVE_NONE);
                        assert ((ss-1)->current_move != MOVE_NULL);
                        assert (extended_beta > alpha);

                        // Initialize a MovePicker object for the current position,
                        // and prepare to search the moves.
                        MovePicker mp (pos, thread->history_values, CounterMovesHistoryValues, tt_move, PIECE_VALUE[MG][pos.capture_type ()]);

                        while ((move = mp.next_move ()) != MOVE_NONE)
                        {
                            if (!pos.legal (move, ci.pinneds)) continue;

                            ss->current_move = move;

                            // Speculative prefetch as early as possible
                            prefetch (TT.cluster_entry (pos.move_posi_key (move)));

                            pos.do_move (move, si, pos.gives_check (move, ci));

                            prefetch (thread->pawn_table[pos.pawn_key ()]);
                            prefetch (thread->matl_table[pos.matl_key ()]);

                            auto value = -depth_search<NonPV, true> (pos, ss+1, -extended_beta, -extended_beta+1, reduced_depth, !cut_node);

                            pos.undo_move ();

                            if (value >= extended_beta)
                            {
                                return value;
                            }
                        }
                    }

                    // Step 10. Internal iterative deepening
                    if (   tt_move == MOVE_NONE
                        && depth > (PVNode ? 4 : 7)*DEPTH_ONE        // IID Activation Depth
                        && (PVNode || ss->static_eval + VALUE_EG_PAWN >= beta) // IID Margin
                       )
                    {
                        auto iid_depth = depth - 2*DEPTH_ONE - (PVNode ? DEPTH_ZERO : depth/4); // IID Reduced Depth

                        depth_search<PVNode ? PV : NonPV, false> (pos, ss, alpha, beta, iid_depth, true);

                        tte = TT.probe (posi_key, tt_hit);
                        if (tt_hit)
                        {
                            tt_move  = tte->move ();
                            tt_value = value_of_tt (tte->value (), ss->ply);
                            tt_depth = tte->depth ();
                            tt_bound = tte->bound ();
                        }
                    }
                }
            }

            // When in check search starts from here
            auto value = best_value;

            bool improving =
                   (ss-0)->static_eval >= (ss-2)->static_eval
                || (ss-0)->static_eval == VALUE_NONE
                || (ss-2)->static_eval == VALUE_NONE;

            bool singular_ext_node =
                   !RootNode
                && exclude_move == MOVE_NONE // Recursive singular search is not allowed
                && tt_move != MOVE_NONE
                &&    depth >= (PVNode ? 6 : 8)*DEPTH_ONE
                && tt_depth >= depth - 3*DEPTH_ONE
                && abs (tt_value) < +VALUE_KNOWN_WIN
                && (tt_bound & BOUND_LOWER);

            if (   RootNode
                && Threadpool.main () == thread
                && TimeMgr.elapsed_time () > 3*MILLI_SEC
               )
            {
                sync_cout
                    << "info"
                    << " depth " << depth/DEPTH_ONE
                    << " time "  << TimeMgr.elapsed_time ()
                    << sync_endl;
            }
            
            const u08 MAX_QUIETS = 64;
            Move  quiet_moves[MAX_QUIETS]
                , pv[MAX_DEPTH + 1];
            u08   move_count = 0
                , quiet_count = 0;

            auto opp_move = (ss-1)->current_move;
            auto opp_move_dst = _ok (opp_move) ? dst_sq (opp_move) : SQ_NO;
            auto counter_move = opp_move_dst != SQ_NO ? thread->counter_moves[pos[opp_move_dst]][opp_move_dst] : MOVE_NONE;
            auto &opp_cmhv = opp_move_dst != SQ_NO ?
                CounterMovesHistoryValues[pos[opp_move_dst]][opp_move_dst] :
                CounterMovesHistoryValues[EMPTY][SQ_A1];

            MovePicker mp (pos, thread->history_values, CounterMovesHistoryValues, tt_move, depth, counter_move, ss);

            // Step 11. Loop through moves
            // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
            while ((move = mp.next_move ()) != MOVE_NONE)
            {
                assert (_ok (move));

                if (move == exclude_move) continue;
                
                // At root obey the "searchmoves" option and skip moves not listed in
                // RootMove list, as a consequence any illegal move is also skipped.
                // In MultiPV mode also skip PV moves which have been already searched.
                if (RootNode && std::count (thread->root_moves.begin () + thread->pv_index, thread->root_moves.end (), move) == 0) continue;

                bool move_legal = RootNode || pos.legal (move, ci.pinneds);

                ss->firstmove_pv = 1 == ++move_count;

                if (   RootNode
                    && Threadpool.main () == thread
                   )
                {
                    Signals.firstmove_root = ss->firstmove_pv;

                    if (TimeMgr.elapsed_time () > 3*MILLI_SEC)
                    {
                        sync_cout
                            << "info"
                            //<< " depth "          << depth/DEPTH_ONE
                            << " currmovenumber " << setfill ('0') << setw (2) << thread->pv_index + move_count << setfill (' ')
                            << " currmove "       << move_to_can (move, Chess960)
                            << " time "           << TimeMgr.elapsed_time ()
                            << sync_endl;
                    }
                }
                
                if (PVNode)
                {
                    (ss+1)->pv = nullptr;
                }

                auto extension = DEPTH_ZERO;
                bool gives_check = pos.gives_check (move, ci);

                // Step 12. Extend the move which seems dangerous like ...checks etc.
                if (gives_check && pos.see_sign (move) >= VALUE_ZERO)
                {
                    extension = DEPTH_ONE;
                }

                // Singular extension(SE) search.
                // We extend the TT move if its value is much better than its siblings.
                // If all moves but one fail low on a search of (alpha-s, beta-s),
                // and just one fails high on (alpha, beta), then that move is singular
                // and should be extended. To verify this do a reduced search on all the other moves
                // but the tt_move, if result is lower than tt_value minus a margin then extend tt_move.
                if (   move_legal
                    && singular_ext_node
                    && move == tt_move
                    && extension == DEPTH_ZERO
                   )
                {
                    auto r_beta = tt_value - 2*(depth/DEPTH_ONE);

                    ss->exclude_move = move;
                    value = depth_search<NonPV, false> (pos, ss, r_beta-1, r_beta, depth/2, cut_node);
                    ss->exclude_move = MOVE_NONE;

                    if (value < r_beta) extension = DEPTH_ONE;
                }

                // Update the current move (this must be done after singular extension search)
                auto new_depth = depth - DEPTH_ONE + extension;
                bool capture_or_promotion = pos.capture_or_promotion (move);

                // Step 13. Pruning at shallow depth
                if (   !RootNode && !MateSearch
                    && !in_check
                    && !capture_or_promotion
                    && best_value > -VALUE_MATE_IN_MAX_DEPTH
                    // ! Dangerous (below)
                    && !gives_check
                    && !pos.advanced_pawn_push (move)
                   )
                {
                    // Move count based pruning
                    if (   depth < FutilityMoveCountDepth*DEPTH_ONE
                        && move_count >= FutilityMoveCounts[improving][depth/DEPTH_ONE]
                       )
                    {
                        continue;
                    }

                    // Value based pruning
                    auto predicted_depth = new_depth - reduction_depths<PVNode> (improving, depth, move_count);

                    // Futility pruning: parent node
                    if (predicted_depth < FutilityMarginDepth*DEPTH_ONE)
                    {
                        auto futility_value = ss->static_eval + FutilityMargins[predicted_depth/DEPTH_ONE] + VALUE_EG_PAWN;

                        if (alpha >= futility_value)
                        {
                            best_value = std::max (futility_value, best_value);
                            continue;
                        }
                    }

                    // Prune moves with negative SEE at low depths
                    if (   predicted_depth < RazorDepth*DEPTH_ONE
                        && pos.see_sign (move) < VALUE_ZERO
                       )
                    {
                        continue;
                    }
                }

                // Check for legality just before making the move
                if (!RootNode && !move_legal)
                {
                    ss->firstmove_pv = 1 == --move_count;
                    continue;
                }

                ss->current_move = move;

                // Speculative prefetch as early as possible
                prefetch (TT.cluster_entry (pos.move_posi_key (move)));

                // Step 14. Make the move
                pos.do_move (move, si, gives_check);

                prefetch (thread->pawn_table[pos.pawn_key ()]);
                prefetch (thread->matl_table[pos.matl_key ()]);

                bool search_full_depth;

                // Step 15. Reduced depth search (LMR).
                // If the move fails high will be re-searched at full depth.
                if (   depth > LateMoveReductionDepth*DEPTH_ONE
                    && move_count > FullDepthMoveCount
                    && !capture_or_promotion
                    && std::count (std::begin (ss->killer_moves), std::end (ss->killer_moves), move) == 0 // Not killer move
                   )
                {
                    auto reduction_depth = reduction_depths<PVNode> (improving, depth, move_count);

                    auto   hv = thread->history_values[pos[dst_sq (move)]][dst_sq (move)];
                    auto cmhv = opp_cmhv[pos[dst_sq (move)]][dst_sq (move)];

                    // Increase reduction for cut node or negative history
                    if (   (!PVNode && cut_node)
                        || (hv < VALUE_ZERO && cmhv <= VALUE_ZERO)
                       )
                    {
                        reduction_depth += DEPTH_ONE;
                    }
                    // Decrease reduction for positive history
                    if (   reduction_depth != DEPTH_ZERO
                        && (hv > VALUE_ZERO && cmhv > VALUE_ZERO)
                       )
                    {
                        reduction_depth = std::max (reduction_depth-DEPTH_ONE, DEPTH_ZERO);
                    }
                    // Decrease reduction for moves that escape a capture
                    if (   reduction_depth != DEPTH_ZERO
                        && mtype (move) == NORMAL
                        && ptype (pos[dst_sq (move)]) != PAWN
                        && pos.see (mk_move<NORMAL> (dst_sq (move), org_sq (move))) < VALUE_ZERO // Reverse move
                       )
                    {
                        reduction_depth = std::max (reduction_depth-DEPTH_ONE, DEPTH_ZERO);
                    }

                    // Search with reduced depth
                    auto reduced_depth = std::max (new_depth - reduction_depth, DEPTH_ONE);
                    value = -depth_search<NonPV, true> (pos, ss+1, -(alpha+1), -alpha, reduced_depth, true);

                    search_full_depth = alpha < value && reduction_depth != DEPTH_ZERO;
                }
                else
                {
                    search_full_depth = !PVNode || move_count > FullDepthMoveCount;
                }

                // Step 16. Full depth search, when LMR is skipped or fails high
                if (search_full_depth)
                {
                    value =
                        new_depth < DEPTH_ONE ?
                            gives_check ?
                                -quien_search<NonPV, true > (pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO) :
                                -quien_search<NonPV, false> (pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO) :
                            -depth_search<NonPV, true> (pos, ss+1, -(alpha+1), -alpha, new_depth, !cut_node);
                }

                // Do a full PV search on:
                // - 'full depth move count' move
                // - 'fail high' move (search only if value < beta)
                // otherwise let the parent node fail low with
                // alpha >= value and to try another better move.
                if (PVNode && ((0 < move_count && move_count <= FullDepthMoveCount) || (alpha < value && (RootNode || value < beta))))
                {
                    (ss+1)->pv = pv;
                    (ss+1)->pv[0] = MOVE_NONE;

                    value =
                        new_depth < DEPTH_ONE ?
                            gives_check ?
                                -quien_search<PV, true > (pos, ss+1, -beta, -alpha, DEPTH_ZERO) :
                                -quien_search<PV, false> (pos, ss+1, -beta, -alpha, DEPTH_ZERO) :
                            -depth_search<PV, true> (pos, ss+1, -beta, -alpha, new_depth, false);
                }

                // Step 17. Undo move
                pos.undo_move ();

                assert (-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Step 18. Check for new best move
                // Finished searching the move. If a stop or a cutoff occurred,
                // the return value of the search cannot be trusted,
                // and return immediately without updating best move, PV and TT.
                if (Signals.force_stop)
                {
                    return VALUE_ZERO;
                }

                if (RootNode)
                {
                    auto &rm = *std::find (thread->root_moves.begin (), thread->root_moves.end (), move);
                    // 1st legal move or new best move ?
                    if (ss->firstmove_pv || alpha < value)
                    {
                        rm.new_value = value;
                        rm.pv.resize (1);

                        assert ((ss+1)->pv != nullptr);

                        for (auto *m = (ss+1)->pv; *m != MOVE_NONE; ++m)
                        {
                            rm += *m;
                        }

                        // Record how often the best move has been changed in each iteration.
                        // This information is used for time management:
                        // When the best move changes frequently, allocate some more time.
                        if (Limits.use_time_manager () && move_count > 1)
                        {
                            TimeMgr.best_move_change++;
                        }
                    }
                    else
                    {
                        // All other moves but the PV are set to the lowest value, this
                        // is not a problem when sorting becuase sort is stable and move
                        // position in the list is preserved, just the PV is pushed up.
                        rm.new_value = -VALUE_INFINITE;
                    }
                }

                if (best_value < value)
                {
                    best_value = value;

                    if (alpha < value)
                    {
                        // If there is an easy move for this position, clear it if unstable
                        if (   PVNode
                            && Limits.use_time_manager ()
                            && MoveMgr.easy_move (pos.posi_key ()) != MOVE_NONE
                            && (move != MoveMgr.easy_move (pos.posi_key ()) || move_count > 1)
                           )
                        {
                            MoveMgr.clear ();
                        }

                        best_move = move;

                        if (PVNode && !RootNode)
                        {
                            update_pv (ss->pv, move, (ss+1)->pv);
                        }
                        // Fail high
                        if (value >= beta) 
                        {
                            break;
                        }

                        assert (value < beta);
                        // Update alpha here! always alpha < beta
                        if (PVNode) alpha = value;
                    }
                }

                if (   move != best_move
                    && !capture_or_promotion
                    && quiet_count < MAX_QUIETS
                   )
                {
                    quiet_moves[quiet_count++] = move;
                }
            }
            
            // Step 19.
            // Following condition would detect a stop only after move loop has been
            // completed. But in this case bestValue is valid because we have fully
            // searched our subtree, and we can anyhow save the result in TT.
            /*
            if (Signals.force_stop) return VALUE_DRAW;
            */
            
            // Step 20. Check for checkmate and stalemate
            // If all possible moves have been searched and if there are no legal moves,
            // If in a singular extension search then return a fail low score (alpha).
            // Otherwise it must be checkmate or stalemate, so return value accordingly.
            if (0 == move_count)
            {
                best_value = 
                    exclude_move != MOVE_NONE ?
                        alpha : in_check ?
                            mated_in (ss->ply) : DrawValue[pos.active ()];
            }
            else
            // Quiet best move: update killers, history, countermoves and countermoves history
            if (   best_move != MOVE_NONE
                && !pos.capture_or_promotion (best_move)
               )
            {
                update_stats (pos, ss, best_move, depth, quiet_moves, quiet_count);
            }
            else
            // Bonus for prior countermove that caused the fail low
            if (best_move == MOVE_NONE)
            {
                if (  !in_check
                    && depth >= 3*DEPTH_ONE
                    && pos.capture_type () == NONE
                    && opp_move_dst != SQ_NO
                    && mtype (opp_move) != PROMOTE
                   )
                {
                    auto own_move = (ss-2)->current_move;
                    auto own_move_dst = _ok (own_move) ? dst_sq (own_move) : SQ_NO;
                    if (own_move_dst != SQ_NO)
                    {
                        auto bonus = Value((depth / DEPTH_ONE)*(depth / DEPTH_ONE));
                        auto &own_cmhv = CounterMovesHistoryValues[pos[own_move_dst]][own_move_dst];
                        own_cmhv.update_cm_history (pos, opp_move, bonus);
                    }
                }
            }

            tte->save (posi_key, best_move,
                value_to_tt (best_value, ss->ply), ss->static_eval, depth,
                best_value >= beta ? BOUND_LOWER :
                    PVNode && best_move != MOVE_NONE ? BOUND_EXACT : BOUND_UPPER,
                TT.generation ());

            assert (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        // perft<>() is utility to verify move generation.
        // All the leaf nodes up to the given depth are generated and the sum returned.
        template<bool RootNode>
        u64 perft (Position &pos, Depth depth)
        {
            u64 leaf_nodes = U64(0);
            for (const auto &m : MoveList<LEGAL> (pos))
            {
                u64 inter_nodes;
                if (RootNode && depth <= 1*DEPTH_ONE)
                {
                    inter_nodes = 1;
                }
                else
                {
                    StateInfo si;
                    pos.do_move (m, si, pos.gives_check (m, CheckInfo (pos)));
                    inter_nodes = depth <= 2*DEPTH_ONE ?
                                    MoveList<LEGAL> (pos).size () :
                                    perft<false> (pos, depth-DEPTH_ONE);
                    pos.undo_move ();
                }

                if (RootNode)
                {
                    sync_cout << left
                              << setw ( 7)
                              //<< move_to_can (m, Chess960)
                              << move_to_san (m, pos)
                              << right << setfill ('.')
                              << setw (16) << inter_nodes
                              << setfill (' ') << left
                              << sync_endl;
                }

                leaf_nodes += inter_nodes;
            }

            return leaf_nodes;
        }

        // ------------------------------------

        enum RemainTimeT { RT_OPTIMUM, RT_MAXIMUM };

        // move_importance() is a skew-logistic function based on naive statistical
        // analysis of "how many games are still undecided after 'n' half-moves".
        // Game is considered "undecided" as long as neither side has >275cp advantage.
        // Data was extracted from CCRL game database with some simple filtering criteria.
        double move_importance (i32 game_ply)
        {
            //                               PLY_SHIFT  PLY_SCALE  SKEW_RATE
            return pow ((1 + exp ((game_ply - 59.800) / 09.300)), -00.172) + DBL_MIN; // Ensure non-zero
        }

        template<RemainTimeT TT>
        // remaining_time<>() calculate the time remaining
        u32 remaining_time (u32 time, u08 movestogo, i32 game_ply)
        {
            // When in trouble, can step over reserved time with this ratio
            const double StepRatio  = RT_OPTIMUM == TT ? 1.0 : 7.00;
            // However must not steal time from remaining moves over this ratio
            const double StealRatio = RT_MAXIMUM == TT ? 0.0 : 0.33;

            double this_move_imp = move_importance (game_ply) * MoveSlowness / 100;
            double that_move_imp = 0.0;
            for (u08 i = 1; i < movestogo; ++i)
            {
                that_move_imp += move_importance (game_ply + 2 * i);
            }

            double time_ratio_1 = (0             + this_move_imp * StepRatio ) / (this_move_imp * StepRatio + that_move_imp);
            double time_ratio_2 = (this_move_imp + that_move_imp * StealRatio) / (this_move_imp * 1         + that_move_imp);

            return u32(time * std::min (time_ratio_1, time_ratio_2));
        }

    }

    bool                Chess960        = false;

    LimitsT             Limits;
    SignalsT volatile   Signals;
    StateStackPtr       SetupStates;

    u16                 MultiPV         = 1;
    //i32                 MultiPV_cp      = 0;

    i16                 FixedContempt   = 0
        ,               ContemptTime    = 30
        ,               ContemptValue   = 50;

    string              HashFile        = "Hash.dat";
    u16                 AutoSaveHashTime= 0;

    bool                OwnBook         = false;
    string              BookFile        = "Book.bin";
    bool                BookMoveBest    = true;

    string              SearchFile      = "";

    SkillManager        SkillMgr;

    // ------------------------------------

    u08  MaximumMoveHorizon  =  50; // Plan time management at most this many moves ahead, in num of moves.
    u08  ReadyMoveHorizon    =  40; // Be prepared to always play at least this many moves, in num of moves.
    u32  OverheadClockTime   =  60; // Attempt to keep at least this much time at clock, in milliseconds.
    u32  OverheadMoveTime    =  30; // Attempt to keep at least this much time for each remaining move, in milliseconds.
    u32  MinimumMoveTime     =  20; // No matter what, use at least this much time before doing the move, in milliseconds.
    u32  MoveSlowness        = 100; // Move Slowness, in %age.
    u32  NodesTime           =   0;
    bool Ponder              = true; // Whether or not the engine should analyze when it is the opponent's turn.

    TimeManager TimeMgr;

    // ------------------------------------

    // RootMove::insert_pv_in_tt() is called at the end of a search iteration, and
    // inserts the PV back into the TT. This makes sure the old PV moves are searched
    // first, even if the old TT entries have been overwritten.
    void RootMove::insert_pv_into_tt (Position &pos)
    {
        StateInfo states[MAX_DEPTH], *si = states;

        u08 ply = 0;
        for (auto m : pv)
        {
            assert (MoveList<LEGAL> (pos).contains (m));

            bool tt_hit;
            auto *tte = TT.probe (pos.posi_key (), tt_hit);
            // Don't overwrite correct entries
            if (!tt_hit || tte->move () != m)
            {
                tte->save (pos.posi_key (), m, VALUE_NONE, VALUE_NONE, DEPTH_NONE, BOUND_NONE, TT.generation ());
            }

            pos.do_move (m, *si++, pos.gives_check (m, CheckInfo (pos)));
            ++ply;
        }

        while (ply != 0)
        {
            pos.undo_move ();
            --ply;
        }
    }
    
    // RootMove::extract_ponder_move_from_tt() is called in case we have no ponder move before
    // exiting the search, for instance in case we stop the search during a fail high at
    // root. We try hard to have a ponder move to return to the GUI, otherwise in case of
    // 'ponder on' we have nothing to think on.
    bool RootMove::extract_ponder_move_from_tt (Position &pos)
    {
        assert (size () == 1);
        assert (pv[0] != MOVE_NONE);
        
        bool extracted = false;

        StateInfo si;
        pos.do_move (pv[0], si, pos.gives_check (pv[0], CheckInfo (pos)));

        bool tt_hit;
        auto *tte = TT.probe (pos.posi_key (), tt_hit);
        if (tt_hit)
        {
            auto m = tte->move (); // Local copy to be SMP safe
            if (   m != MOVE_NONE
                && MoveList<LEGAL> (pos).contains (m)
               )
            {
               *this += m;
               extracted = true;
            }
        }

        pos.undo_move ();
        
        return extracted;
    }
    
    RootMove::operator string () const
    {
        stringstream ss;
        for (auto m : pv)
        {
            ss << " " << move_to_can (m, Chess960);
        }
        return ss.str ();
    }

    // ------------------------------------

    void RootMoveVector::initialize (const Position &pos)
    {
        clear ();
        for (const auto &m : MoveList<LEGAL> (pos))
        {
            if (   Limits.root_moves.empty ()
                || std::count (Limits.root_moves.begin (), Limits.root_moves.end (), m) != 0
               )
            {
                *this += RootMove (m);
            }
        }
    }

    RootMoveVector::operator string () const
    {
        stringstream ss;
        for (const auto &rm : *this)
        {
            ss << rm << "\n";
        }
        return ss.str ();
    }

    // ------------------------------------

    u32 TimeManager::elapsed_time () const { return u32(Limits.npmsec != 0 ? Threadpool.game_nodes () : now () - _start_time); }

    // TimeManager::initialize() is called at the beginning of the search and
    // calculates the allowed thinking time out of the time control and current game ply.
    void TimeManager::initialize ()
    {
        // If we have to play in 'nodes as time' mode, then convert from time
        // to nodes, and use resulting values in time management formulas.
        // WARNING: Given npms (nodes per millisecond) must be much lower then
        // real engine speed to avoid time losses.
        if (NodesTime != 0)
        {
            // Only once at game start
            if (available_nodes == 0)
            {
                available_nodes = NodesTime * Limits.clock[RootColor].time; // Time is in msec
            }

            // Convert from millisecs to nodes
            Limits.clock[RootColor].time = i32 (available_nodes);
            Limits.clock[RootColor].inc *= NodesTime;
            Limits.npmsec = NodesTime;
        }

        _start_time = Limits.start_time;
        _instability_factor = 1.0;

        _optimum_time =
        _maximum_time =
            std::max (Limits.clock[RootColor].time, MinimumMoveTime);

        const u08 MaxMovesToGo = Limits.movestogo != 0 ? std::min (Limits.movestogo, MaximumMoveHorizon) : MaximumMoveHorizon;
        // Calculate optimum time usage for different hypothetic "moves to go"-values and choose the
        // minimum of calculated search time values. Usually the greatest hyp_movestogo gives the minimum values.
        for (u08 hyp_movestogo = 1; hyp_movestogo <= MaxMovesToGo; ++hyp_movestogo)
        {
            // Calculate thinking time for hypothetic "moves to go"-value
            i32 hyp_time = std::max (
                + Limits.clock[RootColor].time
                + Limits.clock[RootColor].inc * (hyp_movestogo-1)
                - OverheadClockTime
                - OverheadMoveTime * std::min (hyp_movestogo, ReadyMoveHorizon), 0U);

            u32 opt_time = MinimumMoveTime + remaining_time<RT_OPTIMUM> (hyp_time, hyp_movestogo, RootPly);
            u32 max_time = MinimumMoveTime + remaining_time<RT_MAXIMUM> (hyp_time, hyp_movestogo, RootPly);

            _optimum_time = std::min (opt_time, _optimum_time);
            _maximum_time = std::min (max_time, _maximum_time);
        }

        if (Ponder) _optimum_time += _optimum_time / 4;

        // Make sure that _optimum_time is not over _maximum_time
        _optimum_time = std::min (_maximum_time, _optimum_time);
    }

    // ------------------------------------

    // When playing with a strength handicap, choose best move among the first 'candidates'
    // RootMoves using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
    Move SkillManager::pick_best_move ()
    {
        static PRNG prng (now ());

        _best_move = MOVE_NONE;
        const RootMoveVector &root_moves = Threadpool.main ()->root_moves;
        // RootMoves are already sorted by score in descending order
        auto variance   = std::min (root_moves[0].new_value - root_moves[LimitPV - 1].new_value, VALUE_MG_PAWN);
        auto weakness   = Value(MAX_DEPTH - 4 * _level);
        auto best_value = -VALUE_INFINITE;
        // Choose best move. For each move score add two terms both dependent on weakness,
        // one deterministic and bigger for weaker moves, and one random with variance,
        // then choose the move with the resulting highest score.
        for (u16 i = 0; i < LimitPV; ++i)
        {
            auto v = root_moves[i].new_value
                   // push value
                   + weakness * i32(root_moves[0].new_value - root_moves[i].new_value)
                   + variance * i32(prng.rand<u32> () % weakness) * 2 / i32(VALUE_EG_PAWN);

            if (best_value < v)
            {
                best_value = v;
                _best_move = root_moves[i][0];
            }
        }
        return _best_move;
    }

    // ------------------------------------

    // perft() is utility to verify move generation. All the leaf nodes
    // up to the given depth are generated and counted and the sum returned.
    u64  perft (Position &pos, Depth depth)
    {
        return perft<true> (pos, depth);
    }

    // think() is the external interface to search, and is called by the
    // main thread when the program receives the UCI 'go' command.
    // It searches from RootPos and at the end prints the "bestmove" to output.
    

    // reset() clears all search memory to obtain reproducible search results
    void reset ()
    {
        TT.clear ();
        CounterMovesHistoryValues.clear ();
        for (auto *th : Threadpool)
        {
            th->history_values.clear ();
            th->counter_moves.clear ();
        }
    }

    // initialize() is called during startup to initialize various lookup tables
    void initialize ()
    {
        u08 d;  // depth
        u08 mc; // move count
        // Initialize lookup tables
        for (d = 0; d < RazorDepth; ++d)
        {
            RazorMargins         [d] = Value(i32(0x200 + (0x20 + 0*d)*d));
        }
        for (d = 0; d < FutilityMarginDepth; ++d)
        {
            FutilityMargins      [d] = Value(i32(0x00 + (0xC8 + 0*d)*d));
        }
        for (d = 0; d < FutilityMoveCountDepth; ++d)
        {
            FutilityMoveCounts[0][d] = u08(2.40 + 0.773 * pow (0.00 + d, 1.80));
            FutilityMoveCounts[1][d] = u08(2.90 + 1.045 * pow (0.49 + d, 1.80));
        }

        const double K[2][2] = {{ 0.799, 2.281 }, { 0.484, 3.023 }};

        for (u08 pv = 0; pv <= 1; ++pv)
        {
            for (u08 imp = 0; imp <= 1; ++imp)
            {
                for (d = 1; d < ReductionDepth; ++d)
                {
                    for (mc = 1; mc < ReductionMoveCount; ++mc)
                    {
                        double r = K[pv][0] + log (d) * log (mc) / K[pv][1];

                        if (r >= 1.5)
                        {
                            ReductionDepths[pv][imp][d][mc] = i32(r)*DEPTH_ONE;
                        }
                        // Increase reduction when eval is not improving
                        if (!pv && !imp && ReductionDepths[pv][imp][d][mc] >= 2*DEPTH_ONE)
                        {
                            ReductionDepths[pv][imp][d][mc] += 1*DEPTH_ONE;
                        }
                    }
                }
            }
        }

    }

}

namespace Threading {

    using namespace Searcher;

    // check_limits() is called by the timer thread when the timer triggers.
    // It is used to print debug info and, more importantly,
    // to detect when out of available time or reached limits
    // and thus stop the search.
    void check_limits ()
    {
        static auto last_time = now ();

        u32 elapsed_time = std::max (TimeMgr.elapsed_time (), 1U);

        auto now_time = now ();
        if (now_time - last_time >= MILLI_SEC)
        {
            last_time = now_time;
            dbg_print ();
        }

        // An engine may not stop pondering until told so by the GUI
        if (Limits.ponder) return;

        if (Limits.use_time_manager ())
        {
            if (   elapsed_time > TimeMgr.maximum_time () - 2 * TIMER_RESOLUTION
                   // Still at first move
                || (    Signals.firstmove_root
                    && !Signals.failedlow_root
                    && elapsed_time > TimeMgr.available_time () * 0.75
                   )
               )
            {
               Signals.force_stop = true;
            }
        }
        else
        if (Limits.movetime != 0)
        {
            if (elapsed_time >= Limits.movetime)
            {
                Signals.force_stop = true;
            }
        }
        else
        if (Limits.nodes != 0)
        {
            if (Threadpool.game_nodes () >= Limits.nodes)
            {
                Signals.force_stop = true;
            }
        }
    }

    void save_hash ()
    {
        if (FirstAutoSave)
        {
            FirstAutoSave = false;
            return;
        }
        TT.save (HashFile);
    }

    // Thread::search() is the main iterative deepening loop. It calls search()
    // repeatedly with increasing depth until the allocated thinking time has been
    // consumed, user stops the search, or the maximum search depth is reached.
    void Thread::search (bool is_main_thread)
    {
        Stack *ss = stack+2; // To allow referencing (ss-2)
        memset (ss-2, 0x00, 5*sizeof (*stack));

        auto easy_move = MOVE_NONE;
        if (is_main_thread)
        {
            easy_move = MoveMgr.easy_move (root_pos.posi_key ());
            MoveMgr.clear ();
            TT.generation (RootPly);
            if (Limits.use_time_manager ())
            {
                TimeMgr.best_move_change = 0.0;
            }
        }

        if (SkillMgr.enabled ()) SkillMgr.clear ();

        // Do have to play with skill handicap?
        // In this case enable MultiPV search by skill pv size
        // that will use behind the scenes to get a set of possible moves.
        LimitPV = std::min (std::max (MultiPV, u16 (SkillMgr.enabled () ? 4 : 0)), u16 (root_moves.size ()));

        Value best_value = VALUE_ZERO
            , window     = VALUE_ZERO
            , bound_a    = -VALUE_INFINITE
            , bound_b    = +VALUE_INFINITE;

        // Iterative deepening loop until target depth reached
        while (++depth < DEPTH_MAX && !Signals.force_stop && (0 == Limits.depth || depth <= Limits.depth))
        {
            // Set up the new depth for the helper threads
            if (is_main_thread)
            {
                depth = Threadpool.main ()->depth + Depth (int (3 * log (1 + this->index)));
            }

            if (is_main_thread && Limits.use_time_manager ())
            {
                // Age out PV variability metric
                TimeMgr.best_move_change *= 0.5;
            }

            // Save last iteration's scores before first PV line is searched and
            // all the move scores but the (new) PV are set to -VALUE_INFINITE.
            root_moves.backup ();

            const bool aspiration = depth > 4*DEPTH_ONE;

            // MultiPV loop. Perform a full root search for each PV line
            for (pv_index = 0; pv_index < LimitPV && !Signals.force_stop; ++pv_index)
            {
                // Reset Aspiration window starting size
                if (aspiration)
                {
                    window = Value(18);
                        //Value (depth <= 32*DEPTH_ONE ? 14 + (u16 (depth)-1)/4 : 22); // Increasing window

                    bound_a = std::max (root_moves[pv_index].old_value - window, -VALUE_INFINITE);
                    bound_b = std::min (root_moves[pv_index].old_value + window, +VALUE_INFINITE);
                }

                // Start with a small aspiration window and, in case of fail high/low,
                // research with bigger window until not failing high/low anymore.
                do
                {
                    best_value = depth_search<Root, true> (root_pos, ss, bound_a, bound_b, depth, false);

                    // Bring the best move to the front. It is critical that sorting is
                    // done with a stable algorithm because all the values but the first
                    // and eventually the new best one are set to -VALUE_INFINITE and
                    // want to keep the same order for all the moves but the new PV
                    // that goes to the front. Note that in case of MultiPV search
                    // the already searched PV lines are preserved.
                    stable_sort (root_moves.begin () + pv_index, root_moves.end ());

                    // Write PV back to transposition table in case the relevant
                    // entries have been overwritten during the search.
                    for (i16 i = pv_index; i >= 0; --i)
                    {
                        root_moves[i].insert_pv_into_tt (root_pos);
                    }

                    // If search has been stopped break immediately.
                    // Sorting and writing PV back to TT is safe becuase
                    // RootMoves is still valid, although refers to previous iteration.
                    if (Signals.force_stop) break;

                    // When failing high/low give some update
                    // (without cluttering the UI) before to re-search.
                    if (   is_main_thread
                        && LimitPV == 1
                        && (bound_a >= best_value || best_value >= bound_b)
                        && TimeMgr.elapsed_time () > 3*MILLI_SEC
                       )
                    {
                        sync_cout << multipv_info (root_pos, depth, bound_a, bound_b) << sync_endl;
                    }

                    // In case of failing low/high increase aspiration window and re-search,
                    // otherwise exit the loop.
                    if (best_value <= bound_a)
                    {
                        bound_b = (bound_a + bound_b)/2;
                        bound_a = std::max (best_value - window, -VALUE_INFINITE);
                        
                        if (is_main_thread)
                        {
                            Signals.failedlow_root = true;
                            Signals.ponderhit_stop = false;
                        }
                    }
                    else
                    if (best_value >= bound_b)
                    {
                        bound_a = (bound_a + bound_b)/2;
                        bound_b = std::min (best_value + window, +VALUE_INFINITE);
                    }
                    else
                        break;

                    window += window / 4 + 5;

                    assert (-VALUE_INFINITE <= bound_a && bound_a < bound_b && bound_b <= +VALUE_INFINITE);
                }
                while (true);

                // Sort the PV lines searched so far and update the GUI
                std::stable_sort (root_moves.begin (), root_moves.begin () + pv_index + 1);

                if (!is_main_thread) break;

                if (Signals.force_stop)
                {
                    sync_cout
                        << "info"
                        << " nodes " << root_pos.game_nodes ()
                        << " time "  << TimeMgr.elapsed_time ()
                        << sync_endl;
                }
                else
                if (pv_index + 1 == LimitPV || TimeMgr.elapsed_time () > 3*MILLI_SEC)
                {
                    sync_cout << multipv_info (root_pos, depth, bound_a, bound_b) << sync_endl;
                }
            }

            if (!is_main_thread) continue;

            if (ContemptValue != 0)
            {
                Value valued_contempt = Value (i32 (root_moves[0].new_value)/ContemptValue);
                DrawValue[ RootColor] = BaseContempt[ RootColor] - valued_contempt;
                DrawValue[~RootColor] = BaseContempt[~RootColor] + valued_contempt;
            }

            // If skill levels are enabled and time is up, pick a sub-optimal best move
            if (SkillMgr.enabled () && SkillMgr.depth_to_pick (depth)) SkillMgr.pick_best_move ();

            if (!SearchFile.empty ())
            {
                SearchLog << pretty_pv_info (root_pos, depth, root_moves[0].new_value, TimeMgr.elapsed_time (), root_moves[0].pv) << endl;
            }

            if (!Signals.force_stop && !Signals.ponderhit_stop)
            {
                // Stop the search early:
                bool stop = false;

                // Do have time for the next iteration? Can stop searching now?
                if (Limits.use_time_manager ())
                {
                    // If PV limit = 1 then take some extra time if the best move has changed
                    if (aspiration && LimitPV == 1)
                    {
                        TimeMgr.instability ();
                    }

                    // Stop the search
                    // If there is only one legal move available or 
                    // If all of the available time has been used or
                    // If matched an easy move from the previous search and just did a fast verification.
                    if (   root_moves.size () == 1
                        || TimeMgr.elapsed_time () > TimeMgr.available_time ()
                        || (   root_moves[0] == easy_move
                            && TimeMgr.best_move_change < 0.03
                            && TimeMgr.elapsed_time () > TimeMgr.available_time () * 0.10
                           )
                       )
                    {
                        stop = true;
                    }

                    if (root_moves[0].size () >= 3)
                    {
                        MoveMgr.update (root_pos, root_moves[0].pv);
                    }
                    else
                    {
                        MoveMgr.clear ();
                    }
                }
                else
                // Stop if have found a "mate in <x>"
                if (   MateSearch
                    && best_value >= +VALUE_MATE_IN_MAX_DEPTH
                    && i16 (VALUE_MATE - best_value) <= 2*Limits.mate
                   )
                {
                    stop = true;
                }

                if (stop)
                {
                    // If allowed to ponder do not stop the search now but
                    // keep pondering until GUI sends "ponderhit" or "stop".
                    if (Limits.ponder)
                    {
                        Signals.ponderhit_stop = true;
                    }
                    else
                    {
                        Signals.force_stop = true;
                    }
                }
            }

        }

        searching = false;
        notify_one (); // Wake up main thread if is sleeping waiting for us

        if (!is_main_thread) return;

        // Clear any candidate easy move that wasn't stable for the last search iterations;
        // the second condition prevents consecutive fast moves.
        if (   Limits.use_time_manager ()
            && (MoveMgr.stable_count < 6 || TimeMgr.elapsed_time () < TimeMgr.available_time ())
           )
        {
            MoveMgr.clear ();
        }
        // If skill level is enabled, swap best PV line with the sub-optimal one
        if (SkillMgr.enabled ())
        {
            std::swap (root_moves[0], *std::find (root_moves.begin (), root_moves.end (), SkillMgr.best_move ()));
        }
    }
    
    void MainThread::think ()
    {
        static PolyglotBook book; // Defined static to initialize the PRNG only once

        RootColor   = root_pos.active ();
        RootPly     = root_pos.game_ply ();

        TimeMgr.initialize ();

        MateSearch  = 0 != Limits.mate;

        if (!SearchFile.empty ())
        {
            SearchLog.open (SearchFile, ios_base::out|ios_base::app);

            SearchLog
                << "----------->\n" << boolalpha
                << "RootPos  : " << root_pos.fen ()                 << "\n"
                << "RootSize : " << root_moves.size ()              << "\n"
                << "Infinite : " << Limits.infinite                 << "\n"
                << "Ponder   : " << Limits.ponder                   << "\n"
                << "ClockTime: " << Limits.clock[RootColor].time    << "\n"
                << "Increment: " << Limits.clock[RootColor].inc     << "\n"
                << "MoveTime : " << Limits.movetime                 << "\n"
                << "MovesToGo: " << u16 (Limits.movestogo)          << "\n"
                << " Depth Score    Time       Nodes  PV\n"
                << "-----------------------------------------------------------"
                << endl;
        }

        if (root_moves.size () != 0)
        {
            // Check if can play with own book
            if (OwnBook && RootPly <= 20 && !Limits.infinite && !MateSearch && !BookFile.empty ())
            {
                book.open (BookFile, ios_base::in|ios_base::binary);
                if (book.is_open ())
                {
                    bool found = false;
                    auto book_move = book.probe_move (root_pos, BookMoveBest);
                    if (book_move != MOVE_NONE && std::count (root_moves.begin (), root_moves.end (), book_move) != 0)
                    {
                        found = true;
                        std::swap (root_moves[0], *std::find (root_moves.begin (), root_moves.end (), book_move));
                        StateInfo si;
                        root_pos.do_move (root_moves[0][0], si, root_pos.gives_check (root_moves[0][0], CheckInfo (root_pos)));
                        book_move = book.probe_move (root_pos, BookMoveBest);
                        root_moves[0] += book_move;
                        root_pos.undo_move ();
                    }
                    book.close ();
                    if (found) goto finish;
                }
            }

            i16 timed_contempt = 0;
            i32 diff_time = 0;
            if (ContemptTime != 0
                && Limits.use_time_manager ()
                && (diff_time = (Limits.clock[RootColor].time - Limits.clock[~RootColor].time)/MILLI_SEC) != 0
                //&& ContemptTime <= abs (diff_time)
                )
            {
                timed_contempt = i16 (diff_time/ContemptTime);
            }

            Value contempt = cp_to_value (double (FixedContempt + timed_contempt) / 0x64);
            DrawValue[ RootColor] = BaseContempt[ RootColor] = VALUE_DRAW - contempt;
            DrawValue[~RootColor] = BaseContempt[~RootColor] = VALUE_DRAW + contempt;

            for (auto *th : Threadpool)
            {
                th->max_ply = 0;
                th->depth = DEPTH_ZERO;
                th->searching = true;
                if (th != this)
                {
                    th->root_pos   = Position (root_pos, th);
                    th->root_moves = root_moves;
                    th->notify_one (); // Wake up the thread and start searching
                }
            }

            if (AutoSaveHashTime != 0 && !HashFile.empty ())
            {
                FirstAutoSave = true;
                Threadpool.save_hash_th        = new_thread<TimerThread> ();
                Threadpool.save_hash_th->task  = save_hash;
                Threadpool.save_hash_th->resolution = AutoSaveHashTime*MINUTE_MILLI_SEC;
                Threadpool.save_hash_th->start ();
                Threadpool.save_hash_th->notify_one ();
            }

            Threadpool.check_limits_th->start ();
            Threadpool.check_limits_th->notify_one (); // Wake up the recurring timer

            search (true); // Let's start searching !

            // Stop the threads and the timer
            Signals.force_stop = true;
            Threadpool.check_limits_th->stop ();

            if (Threadpool.save_hash_th != nullptr)
            {
                Threadpool.save_hash_th->stop ();
                delete_thread (Threadpool.save_hash_th);
            }

            // Wait until all threads have finished
            for (auto *th : Threadpool)
            {
                if (th != this)
                {
                    th->wait_while (th->searching);
                }
            }
        }
        else
        {
            root_moves += RootMove ();

            sync_cout
                << "info"
                << " depth " << 0
                << " score " << to_string (root_pos.checkers () != U64 (0) ? -VALUE_MATE : VALUE_DRAW)
                << " time "  << 0
                << sync_endl;
        }

    finish:

        u32 elapsed_time = std::max (TimeMgr.elapsed_time (), 1U);

        assert (root_moves[0].size () != 0);

        if (!SearchFile.empty ())
        {
            SearchLog
                << "Time (ms)  : " << elapsed_time                              << "\n"
                << "Nodes (N)  : " << root_pos.game_nodes ()                     << "\n"
                << "Speed (N/s): " << root_pos.game_nodes ()*MILLI_SEC / elapsed_time << "\n"
                << "Hash-full  : " << TT.hash_full ()                           << "\n"
                << "Best move  : " << move_to_san (root_moves[0][0], root_pos) << "\n";
            if (   root_moves[0] != MOVE_NONE
                && (root_moves[0].size () > 1 || root_moves[0].extract_ponder_move_from_tt (root_pos))
               )
            {
                StateInfo si;
                root_pos.do_move (root_moves[0][0], si, root_pos.gives_check (root_moves[0][0], CheckInfo (root_pos)));
                SearchLog << "Ponder move: " << move_to_san (root_moves[0][1], root_pos) << "\n";
                root_pos.undo_move ();
            }
            SearchLog << endl;
            SearchLog.close ();
        }

        // When playing in 'nodes as time' mode, subtract the searched nodes from
        // the available ones before to exit.
        if (Limits.npmsec != 0)
        {
            TimeMgr.available_nodes += Limits.clock[RootColor].inc - Threadpool.game_nodes ();
        }

        // When reach max depth arrive here even without Signals.force_stop is raised,
        // but if are pondering or in infinite search, according to UCI protocol,
        // shouldn't print the best move before the GUI sends a "stop" or "ponderhit" command.
        // Simply wait here until GUI sends one of those commands (that raise Signals.force_stop).
        if (!Signals.force_stop && (Limits.ponder || Limits.infinite))
        {
            Signals.ponderhit_stop = true;
            wait_until (Signals.force_stop);
        }

        // Best move could be MOVE_NONE when searching on a stalemate position
        sync_cout << "bestmove " << move_to_can (root_moves[0][0], Chess960);
        if (   root_moves[0] != MOVE_NONE
            && (root_moves[0].size () > 1 || root_moves[0].extract_ponder_move_from_tt (root_pos))
           )
        {
            cout << " ponder " << move_to_can (root_moves[0][1], Chess960);
        }
        cout << sync_endl;

    }

}
