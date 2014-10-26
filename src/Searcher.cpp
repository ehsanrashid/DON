#include "Searcher.h"

#include <cfloat>
#include <cmath>
#include <sstream>
#include <iomanip>

#include "TimeManager.h"
#include "Transposition.h"
#include "MoveGenerator.h"
#include "MovePicker.h"
#include "Material.h"
#include "Pawns.h"
#include "Evaluator.h"
#include "Thread.h"
#include "Notation.h"
#include "Debugger.h"

using namespace std;
using namespace Time;

namespace Search {

    using namespace BitBoard;
    using namespace MoveGen;
    using namespace MovePick;
    using namespace Transposition;
    using namespace OpeningBook;
    using namespace Evaluate;
    using namespace Notation;
    using namespace Debug;
    using namespace UCI;

    namespace {

        const Depth FutilityMarginDepth = Depth(7);
        CACHE_ALIGN(16)
        // Futility margin lookup table (initialized at startup)
        // [depth]
        Value FutilityMargins[FutilityMarginDepth];

        const Depth RazorDepth = Depth(4);
        CACHE_ALIGN(16)
        // Razoring margin lookup table (initialized at startup)
        // [depth]
        Value RazorMargins[RazorDepth];

        const Depth FutilityMoveCountDepth = Depth(16);
        CACHE_ALIGN(16)
        // Futility move count lookup table (initialized at startup)
        // [improving][depth]
        u08   FutilityMoveCounts[2][FutilityMoveCountDepth];

        const Depth ReductionDepth = Depth(32);
        const u08   ReductionMoveCount = 64;
        CACHE_ALIGN(16)
        // ReductionDepths lookup table (initialized at startup)
        // [pv][improving][depth][move_num]
        u08   ReductionDepths[2][2][ReductionDepth][ReductionMoveCount];

        template<bool PVNode>
        inline Depth reduction_depths (bool imp, Depth d, i32 mc)
        {
            return Depth (ReductionDepths[PVNode][imp][min (d, ReductionDepth-1)][min (mc, ReductionMoveCount-1)]);
        }

        const Depth ProbCutDepth  = Depth(4);

        const u08   MAX_QUIETS    = 64;

        const point INFO_INTERVAL = 3000; // 3 sec

        Color   RootColor;
        i32     RootPly;

        u08     RootSize   // RootMove Count
            ,   LimitPV
            ,   IndexPV;

        Value   DrawValue[CLR_NO]
            ,   BaseContempt[CLR_NO];

        bool    MateSearch;
        bool    FirstAutoSave;

        TimeManager  TimeMgr;
        // Gain statistics
        GainStats    GainStatistics;
        // History statistics
        HistoryStats HistoryStatistics;
        // Move statistics
        MoveStats    CounterMoveStats    // Counter
            ,        FollowupMoveStats;  // Followup

        // update_stats() updates history, killer, counter & followup moves
        // after a fail-high of a quiet move.
        inline void update_stats (const Position &pos, Stack *ss, Move move, Depth depth, Move *quiet_moves, u08 quiets)
        {
            if ((ss)->killer_moves[0] != move)
            {
                (ss)->killer_moves[1] = (ss)->killer_moves[0];
                (ss)->killer_moves[0] = move;
            }

            // Increase history value of the cut-off move and decrease all the other played quiet moves.
            Value value = Value(4*u16(depth)*u16(depth));
            HistoryStatistics.update (pos, move, value);
            for (u08 i = 0; i < quiets; ++i)
            {
                HistoryStatistics.update (pos, quiet_moves[i], -value);
            }
            Move opp_move = (ss-1)->current_move;
            if (_ok (opp_move))
            {
                CounterMoveStats.update (pos, opp_move, move);
            }
            Move own_move = (ss-2)->current_move;
            if (_ok (own_move) && opp_move == (ss-1)->tt_move)
            {
                FollowupMoveStats.update (pos, own_move, move);
            }
        }

        // value_to_tt() adjusts a mate score from "plies to mate from the root" to
        // "plies to mate from the current position". Non-mate scores are unchanged.
        // The function is called before storing a value to the transposition table.
        inline Value value_to_tt (Value v, i32 ply)
        {
            ASSERT (v != VALUE_NONE);
            return v >= +VALUE_MATE_IN_MAX_DEPTH ? v + ply :
                   v <= -VALUE_MATE_IN_MAX_DEPTH ? v - ply :
                   v;
        }
        // value_of_tt() is the inverse of value_to_tt ():
        // It adjusts a mate score from the transposition table
        // (where refers to the plies to mate/be mated from current position)
        // to "plies to mate/be mated from the root".
        inline Value value_of_tt (Value v, i32 ply)
        {
            return v == VALUE_NONE             ? VALUE_NONE :
                   v >= +VALUE_MATE_IN_MAX_DEPTH ? v - ply :
                   v <= -VALUE_MATE_IN_MAX_DEPTH ? v + ply :
                   v;
        }

        // info_multipv() formats PV information according to UCI protocol.
        // UCI requires to send all the PV lines also if are still to be searched
        // and so refer to the previous search score.
        inline string info_multipv (const Position &pos, i16 depth, Value alpha, Value beta, point time)
        {
            ASSERT (time >= 0);

            stringstream ss;

            for (u08 i = 0; i < LimitPV; ++i)
            {
                bool updated = i <= IndexPV;

                i16   d;
                Value v;

                if (updated)
                {
                    d = depth;
                    v = RootMoves[i].new_value;
                }
                else
                {
                    if (1 == depth) return "";

                    d = depth - 1;
                    v = RootMoves[i].old_value;
                }

                // Not at first line
                if (ss.rdbuf ()->in_avail ()) ss << "\n";

                ss  << "info"
                    << " multipv "  << u16(i + 1)
                    << " depth "    << d
                    << " seldepth " << u16(Threadpool.max_ply)
                    << " score "    << ((i == IndexPV) ? pretty_score (v, alpha, beta) : pretty_score (v))
                    << " time "     << time
                    << " nodes "    << pos.game_nodes ()
                    << " nps "      << pos.game_nodes () * MILLI_SEC / max (time, point(1))
                    << " hashfull " << 0//TT.permill_full ()
                    << " pv"        << RootMoves[i].info_pv ();
            }

            return ss.str ();
        }

        template<NodeT NT, bool InCheck>
        // search_quien() is the quiescence search function,
        // which is called by the main depth limited search function
        // when the remaining depth is ZERO.
        inline Value search_quien  (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth)
        {
            const bool    PVNode = NT == PV;

            ASSERT (NT == PV || NT == NonPV);
            ASSERT (InCheck == (pos.checkers () != U64(0)));
            ASSERT (alpha >= -VALUE_INFINITE && alpha < beta && beta <= +VALUE_INFINITE);
            ASSERT (PVNode || alpha == beta-1);
            ASSERT (depth <= DEPTH_ZERO);

            (ss)->current_move = MOVE_NONE;
            (ss)->ply = (ss-1)->ply + 1;

            // Check for aborted search, immediate draw or maximum ply reached
            if (Signals.force_stop || pos.draw () || (ss)->ply > MAX_DEPTH)
            {
                return (ss)->ply > MAX_DEPTH && !InCheck ? evaluate (pos) : DrawValue[pos.active ()];
            }

            // To flag EXACT a node with eval above alpha and no available moves
            Value pv_alpha = PVNode ? alpha : -VALUE_INFINITE;

            // Transposition table lookup
            Key   posi_key;
            const Entry *tte;
            Move  tt_move    = MOVE_NONE
                , best_move  = MOVE_NONE;
            Value tt_value   = VALUE_NONE
                , best_value = -VALUE_INFINITE;
            Depth tt_depth   = DEPTH_NONE;
            Bound tt_bound   = BOUND_NONE;
            
            posi_key = pos.posi_key ();
            tte      = TT.retrieve (posi_key);
            if (tte != NULL)
            {
                tt_move  = tte->move ();
                tt_value = value_of_tt (tte->value (), (ss)->ply);
                tt_depth = tte->depth ();
                tt_bound = tte->bound ();
            }

            Thread *thread = pos.thread ();
            // Decide whether or not to include checks, this fixes also the type of
            // TT entry depth that are going to use. Note that in search_quien use
            // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
            Depth qs_depth = (InCheck || depth >= DEPTH_QS_CHECKS) ?
                               DEPTH_QS_CHECKS :
                               DEPTH_QS_NO_CHECKS;

            CheckInfo cc, *ci = NULL;

            if (  tte != NULL
               && tt_depth >= qs_depth
               && tt_value != VALUE_NONE // Only in case of TT access race
               && (         PVNode ? tt_bound == BOUND_EXACT :
                  tt_value >= beta ? tt_bound &  BOUND_LOWER :
                                     tt_bound &  BOUND_UPPER
                  )
               )
            {
                (ss)->current_move = tt_move; // Can be MOVE_NONE
                return tt_value;
            }

            Value futility_base = -VALUE_INFINITE;
            // Evaluate the position statically
            if (InCheck)
            {
                (ss)->static_eval = VALUE_NONE;
            }
            else
            {
                if (tte != NULL)
                {
                    best_value = tte->eval ();
                    // Never assume anything on values stored in TT
                    if (VALUE_NONE == best_value) best_value = evaluate (pos);
                    (ss)->static_eval = best_value;

                    // Can tt_value be used as a better position evaluation?
                    if (  VALUE_NONE != tt_value
                       && tt_bound & (best_value < tt_value ? BOUND_LOWER : BOUND_UPPER)
                       )
                    {
                        best_value = tt_value;
                    }
                }
                else
                {
                    (ss)->static_eval = best_value =
                        (ss-1)->current_move != MOVE_NULL ? evaluate (pos) : -(ss-1)->static_eval + 2*TEMPO;
                }

                if (alpha < best_value)
                {
                    // Stand pat. Return immediately if static value is at least beta
                    if (best_value >= beta)
                    {
                        if (tte == NULL)
                        {
                            TT.store (
                                posi_key,
                                MOVE_NONE,
                                DEPTH_NONE,
                                BOUND_LOWER,
                                value_to_tt (best_value, (ss)->ply),
                                (ss)->static_eval);
                        }

                        ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
                        return best_value;
                    }

                    ASSERT (best_value < beta);
                    // Update alpha here! always alpha < beta
                    if (PVNode) alpha = best_value;
                }

                futility_base = best_value + VALUE_EG_PAWN/2; // QS Futility Margin
            }

            // Initialize a MovePicker object for the current position, and prepare
            // to search the moves. Because the depth is <= 0 here, only captures,
            // queen promotions and checks (only if depth >= DEPTH_QS_CHECKS) will
            // be generated.
            MovePicker mp (pos, HistoryStatistics, tt_move, depth, dst_sq ((ss-1)->current_move));
            StateInfo si;
            if (ci == NULL) { cc = CheckInfo (pos); ci = &cc; }

            Move move;
            u08 legals = 0;
            // Loop through the moves until no moves remain or a beta cutoff occurs
            while ((move = mp.next_move<false> ()) != MOVE_NONE)
            {
                ASSERT (_ok (move));
                
                bool gives_check = pos.gives_check (move, *ci);

                if (!PVNode && !MateSearch)
                {
                    // Futility pruning
                    if (  !InCheck
                       && !gives_check
                       && move != tt_move
                       && futility_base > -VALUE_KNOWN_WIN
                       && futility_base < beta
                       && !pos.advanced_pawn_push (move)
                       )
                    {
                        ASSERT (mtype (move) != ENPASSANT); // Due to !pos.advanced_pawn_push()

                        Value futility_value = futility_base + PIECE_VALUE[EG][ptype (pos[dst_sq (move)])];

                        if (futility_value < beta)
                        {
                            best_value = max (futility_value, best_value);
                            continue;
                        }
                        // Prune moves with negative or zero SEE
                        if (pos.see (move) <= VALUE_ZERO)
                        {
                            best_value = max (futility_base, best_value);
                            continue;
                        }
                    }

                    // Don't search moves with negative SEE values
                    if (  move != tt_move
                       && mtype (move) != PROMOTE
                       && (  !InCheck
                          // Detect non-capture evasions that are candidate to be pruned (evasion_prunable)
                          || (  best_value > -VALUE_MATE_IN_MAX_DEPTH
                             && !pos.capture (move)
                             && !pos.can_castle (pos.active ())
                             )
                          )
                       && pos.see_sign (move) < VALUE_ZERO
                       )
                    {
                        continue;
                    }

                }

                // Speculative prefetch as early as possible
                prefetch (reinterpret_cast<char*> (TT.cluster_entry (pos.posi_move_key (move))));

                // Check for legality just before making the move
                if (!pos.legal (move, ci->pinneds)) continue;

                ++legals;

                (ss)->current_move = move;
                // Make and search the move
                pos.do_move (move, si, gives_check ? ci : NULL);

                prefetch (reinterpret_cast<char*> (thread->pawn_table[pos.pawn_key ()]));
                prefetch (reinterpret_cast<char*> (thread->matl_table[pos.matl_key ()]));

                Value value;
                
                value =
                    gives_check ?
                        -search_quien<NT, true > (pos, ss+1, -beta, -alpha, depth-DEPTH_ONE) :
                        -search_quien<NT, false> (pos, ss+1, -beta, -alpha, depth-DEPTH_ONE);

                /*
                bool move_pv = PVNode && 1 == legals;

                if (move_pv)
                {
                    value =
                        gives_check ?
                            -search_quien<PV, true > (pos, ss+1, -beta, -alpha, depth-DEPTH_ONE) :
                            -search_quien<PV, false> (pos, ss+1, -beta, -alpha, depth-DEPTH_ONE);
                }
                else
                {
                    value =
                        gives_check ?
                            -search_quien<NonPV, true > (pos, ss+1, -alpha-1, -alpha, depth-DEPTH_ONE) :
                            -search_quien<NonPV, false> (pos, ss+1, -alpha-1, -alpha, depth-DEPTH_ONE);
                    
                    if (alpha < value && value < beta)
                    {
                        value =
                            gives_check ?
                                -search_quien<PV, true > (pos, ss+1, -beta, -alpha, depth-DEPTH_ONE) :
                                -search_quien<PV, false> (pos, ss+1, -beta, -alpha, depth-DEPTH_ONE);
                    }
                }
                */

                // Undo the move
                pos.undo_move ();

                ASSERT (-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Check for new best move
                if (best_value < value)
                {
                    best_value = value;

                    if (alpha < value)
                    {
                        best_move = move;
                        // Fail high
                        if (value >= beta)
                        {
                            TT.store (
                                posi_key,
                                best_move,
                                qs_depth,
                                BOUND_LOWER,
                                value_to_tt (best_value, (ss)->ply),
                                (ss)->static_eval);

                            ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
                            return best_value;
                        }

                        ASSERT (value < beta);
                        // Update alpha here! always alpha < beta
                        if (PVNode) alpha = value;
                    }
                }
            }
            
            // All legal moves have been searched.
            // A special case: If in check and no legal moves were found, it is checkmate.
            if (InCheck && legals == 0)
            {
                // Plies to mate from the root
                best_value = mated_in ((ss)->ply);
            }

            TT.store (
                posi_key,
                best_move,
                qs_depth,
                PVNode && pv_alpha < best_value ? BOUND_EXACT : BOUND_UPPER,
                value_to_tt (best_value, (ss)->ply),
                (ss)->static_eval);

            ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        template<NodeT NT, bool SPNode, bool NullMove>
        // search<>() is the main depth limited search function
        // for PV/NonPV nodes also for normal/splitpoint nodes.
        // It calls itself recursively with decreasing (remaining) depth
        // until we run out of depth, and then drops into search_quien.
        // When called just after a splitpoint the search is simpler because
        // already probed the hash table, done a null move search, and searched
        // the first move before splitting, don't have to repeat all this work again.
        // Also don't need to store anything to the hash table here.
        // This is taken care of after return from the splitpoint.
        inline Value search_depth  (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth, bool cut_node)
        {
            const bool RootNode = NT == Root;
            const bool   PVNode = NT == Root || NT == PV;

            ASSERT (-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
            ASSERT (PVNode || alpha == beta-1);
            ASSERT (depth > DEPTH_ZERO);

            Key   posi_key;
            const Entry *tte;
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
            Thread *thread = pos.thread ();
            bool in_check  = pos.checkers () != U64(0);
            bool singular_ext_node = false;

            SplitPoint *splitpoint = NULL;

            CheckInfo cc, *ci = NULL;

            if (SPNode)
            {
                splitpoint  = (ss)->splitpoint;
                best_value  = splitpoint->best_value;
                best_move   = splitpoint->best_move;

                ASSERT (splitpoint->best_value > -VALUE_INFINITE);
                ASSERT (splitpoint->legals > 0);
            }
            else
            {
                (ss)->ply = (ss-1)->ply + 1;
                (ss)->current_move = (ss+1)->exclude_move = MOVE_NONE;
                fill ((ss+2)->killer_moves, (ss+2)->killer_moves + sizeof ((ss+2)->killer_moves) / sizeof (*((ss+2)->killer_moves)), MOVE_NONE);

                // Used to send 'seldepth' info to GUI
                if (PVNode)
                {
                    Threadpool.max_ply = max ((ss)->ply, Threadpool.max_ply);
                }

                if (!RootNode)
                {
                    // Step 2. Check end condition
                    // Check for aborted search, immediate draw or maximum ply reached
                    if (Signals.force_stop || pos.draw () || (ss)->ply > MAX_DEPTH)
                    {
                        return (ss)->ply > MAX_DEPTH && !in_check ? evaluate (pos) : DrawValue[pos.active ()];
                    }

                    // Step 3. Mate distance pruning. Even if mate at the next move our score
                    // would be at best mates_in((ss)->ply+1), but if alpha is already bigger because
                    // a shorter mate was found upward in the tree then there is no need to search
                    // further, will never beat current alpha. Same logic but with reversed signs
                    // applies also in the opposite condition of being mated instead of giving mate,
                    // in this case return a fail-high score.
                    alpha = max (mated_in ((ss)->ply +0), alpha);
                    beta  = min (mates_in ((ss)->ply +1), beta);

                    if (alpha >= beta) return alpha;
                }

                // Step 4. Transposition table lookup
                // Don't want the score of a partial search to overwrite a previous full search
                // TT value, so use a different position key in case of an excluded move.
                exclude_move = (ss)->exclude_move;
                posi_key = exclude_move == MOVE_NONE ?
                            pos.posi_key () :
                            pos.posi_key () ^ Zobrist::EXC_KEY;

                tte      = TT.retrieve (posi_key);
                (ss)->tt_move =
                tt_move  = RootNode ? RootMoves[IndexPV].pv[0] :
                           tte != NULL ? tte->move () : MOVE_NONE;
                if (tte != NULL)
                {
                    tt_value = value_of_tt (tte->value (), (ss)->ply);
                    tt_depth = tte->depth ();
                    tt_bound = tte->bound ();
                }

                if (!RootNode)
                {
                    // At PV nodes check for exact scores, while at non-PV nodes check for
                    // a fail high/low. Biggest advantage at probing at PV nodes is to have a
                    // smooth experience in analysis mode. Don't probe at Root nodes otherwise
                    // should also update RootMoveList to avoid bogus output.
                    if (  tte != NULL
                       && tt_value != VALUE_NONE // Only in case of TT access race
                       && tt_depth >= depth
                       && (         PVNode ? tt_bound == BOUND_EXACT :
                          tt_value >= beta ? tt_bound &  BOUND_LOWER :
                                             tt_bound &  BOUND_UPPER
                          )
                       )
                    {
                        (ss)->current_move = tt_move; // Can be MOVE_NONE

                        // If tt_move is quiet, update history, killer moves, countermove and followupmove on TT hit
                        if (  !in_check
                           && tt_value >= beta
                           && tt_move != MOVE_NONE
                           && !pos.capture_or_promotion (tt_move)
                           )
                        {
                            update_stats (pos, ss, tt_move, depth, NULL, 0);
                        }

                        return tt_value;
                    }
                }

                // Step 5. Evaluate the position statically and update parent's gain statistics
                if (in_check)
                {
                    (ss)->static_eval = static_eval = VALUE_NONE;
                }
                else
                {
                    if (tte != NULL)
                    {
                        static_eval = tte->eval ();
                        // Never assume anything on values stored in TT
                        if (VALUE_NONE == static_eval) static_eval = evaluate (pos);
                        (ss)->static_eval = static_eval;

                        // Can tt_value be used as a better position evaluation?
                        if (  VALUE_NONE != tt_value
                           && tt_bound & (static_eval < tt_value ? BOUND_LOWER : BOUND_UPPER)
                           )
                        {
                            static_eval = tt_value;
                        }
                    }
                    else
                    {
                        (ss)->static_eval = static_eval =
                            (ss-1)->current_move != MOVE_NULL ? evaluate (pos) : -(ss-1)->static_eval + 2*TEMPO;

                        TT.store (
                            posi_key,
                            MOVE_NONE,
                            DEPTH_NONE,
                            BOUND_NONE,
                            VALUE_NONE,
                            (ss)->static_eval);
                    }

                    move = (ss-1)->current_move;
                    // Updates Gain Statistics
                    if (  move != MOVE_NONE
                       && move != MOVE_NULL
                       && mtype (move) == NORMAL
                       && (ss-0)->static_eval != VALUE_NONE
                       && (ss-1)->static_eval != VALUE_NONE
                       && pos.capture_type () == NONE
                       )
                    {
                        GainStatistics.update (pos, move, -(ss-1)->static_eval - (ss-0)->static_eval);
                    }

                    if (!PVNode && !MateSearch) // (is omitted in PV nodes)
                    {
                        // Step 6. Razoring sort of forward pruning where rather than skipping an entire subtree,
                        // you search it to a reduced depth, typically one less than normal depth.
                        if (  depth < RazorDepth
                           && static_eval + RazorMargins[depth] <= alpha
                           && tt_move == MOVE_NONE
                           && !pos.pawn_on_7thR (pos.active ())
                           )
                        {
                            if (  depth <= 1*DEPTH_ONE
                               && static_eval + RazorMargins[3*DEPTH_ONE] <= alpha
                               )
                            {
                                return search_quien<NonPV, false> (pos, ss, alpha, beta, DEPTH_ZERO);
                            }

                            Value reduced_alpha = max (alpha - RazorMargins[depth], -VALUE_INFINITE);
                            //ASSERT (reduced_alpha >= -VALUE_INFINITE);

                            Value value = search_quien<NonPV, false> (pos, ss, reduced_alpha, reduced_alpha+1, DEPTH_ZERO);

                            if (value <= reduced_alpha)
                            {
                                return value;
                            }
                        }

                        if (NullMove)
                        {
                            ASSERT ((ss-1)->current_move != MOVE_NONE);
                            ASSERT ((ss-1)->current_move != MOVE_NULL);
                            ASSERT (exclude_move == MOVE_NONE);

                            StateInfo si;

                            if (pos.non_pawn_material (pos.active ()) > VALUE_ZERO)
                            {
                                // Step 7. Futility pruning: child node
                                // Betting that the opponent doesn't have a move that will reduce
                                // the score by more than FutilityMargins[depth] if do a null move.
                                if (  depth < FutilityMarginDepth
                                   && abs (static_eval) < +VALUE_KNOWN_WIN // Do not return unproven wins
                                   )
                                {
                                    Value stand_pat = static_eval - FutilityMargins[depth];

                                    if (stand_pat >= beta)
                                    {
                                        return stand_pat;
                                    }
                                }

                                // Step 8. Null move search with verification search
                                if (  depth > 1*DEPTH_ONE
                                   && static_eval >= beta
                                   )
                                {
                                    (ss)->current_move = MOVE_NULL;

                                    // Null move dynamic reduction based on depth and static evaluation
                                    Depth reduction_depth = (3 + depth/4 + min (i32 (static_eval - beta)/VALUE_EG_PAWN, 3))*DEPTH_ONE;
                                    
                                    Depth reduced_depth   = depth - reduction_depth;

                                    // Do null move
                                    pos.do_null_move (si);

                                    // Speculative prefetch as early as possible
                                    prefetch (reinterpret_cast<char*> (TT.cluster_entry (pos.posi_key ())));

                                    // Null (zero) window (alpha, beta) = (beta-1, beta):
                                    Value null_value =
                                        reduced_depth < DEPTH_ONE ?
                                            -search_quien<NonPV, false>        (pos, ss+1, -beta, -beta+1, DEPTH_ZERO) :
                                            -search_depth<NonPV, false, false> (pos, ss+1, -beta, -beta+1, reduced_depth, !cut_node);

                                    // Undo null move
                                    pos.undo_null_move ();

                                    if (null_value >= beta)
                                    {
                                        // Don't do verification search at low depths
                                        if (depth < 8*DEPTH_ONE && abs (beta) < +VALUE_KNOWN_WIN)
                                        {
                                            // Don't return unproven unproven mates
                                            return null_value < +VALUE_MATE_IN_MAX_DEPTH ? null_value : beta;
                                        }
                                        
                                        // Do verification search at high depths
                                        Value value =
                                            reduced_depth < DEPTH_ONE ?
                                                search_quien<NonPV, false>        (pos, ss, beta-1, beta, DEPTH_ZERO) :
                                                search_depth<NonPV, false, false> (pos, ss, beta-1, beta, reduced_depth, false);

                                        if (value >= beta)
                                        {
                                            // Don't return unproven unproven mates
                                            return value < +VALUE_MATE_IN_MAX_DEPTH ? value : beta;
                                        }
                                    }
                                }
                            }

                            // Step 9. Prob-Cut
                            // If have a very good capture (i.e. SEE > see[captured_piece_type])
                            // and a reduced search returns a value much above beta,
                            // can (almost) safely prune the previous move.
                            if (  depth > ProbCutDepth
                               && abs (beta) < +VALUE_MATE_IN_MAX_DEPTH
                               )
                            {
                                Depth reduced_depth = depth - ProbCutDepth; // Shallow Depth
                                Value extended_beta = min (beta + VALUE_MG_PAWN, +VALUE_INFINITE); // ProbCut Threshold
                                //ASSERT (reduced_depth >= DEPTH_ONE);
                                //ASSERT (extended_beta <= +VALUE_INFINITE);

                                // Initialize a MovePicker object for the current position,
                                // and prepare to search the moves.
                                MovePicker mp (pos, HistoryStatistics, tt_move, pos.capture_type ());
                                if (ci == NULL) { cc = CheckInfo (pos); ci = &cc; }

                                while ((move = mp.next_move<false> ()) != MOVE_NONE)
                                {
                                    // Speculative prefetch as early as possible
                                    prefetch (reinterpret_cast<char*> (TT.cluster_entry (pos.posi_move_key (move))));

                                    if (!pos.legal (move, ci->pinneds)) continue;

                                    (ss)->current_move = move;
                                    
                                    pos.do_move (move, si, pos.gives_check (move, *ci) ? ci : NULL);

                                    prefetch (reinterpret_cast<char*> (thread->pawn_table[pos.pawn_key ()]));
                                    prefetch (reinterpret_cast<char*> (thread->matl_table[pos.matl_key ()]));

                                    Value value = -search_depth<NonPV, false, true> (pos, ss+1, -extended_beta, -extended_beta+1, reduced_depth, !cut_node);
                                    
                                    pos.undo_move ();

                                    if (value >= extended_beta)
                                    {
                                        return value;
                                    }
                                }
                            }
                        }
                    }

                    // Step 10. Internal iterative deepening (skipped when in check)
                    if (  tt_move == MOVE_NONE
                       && depth > (PVNode ? 4*DEPTH_ONE : 8*DEPTH_ONE)          // IID Activation Depth
                       && (PVNode || (ss)->static_eval + VALUE_EG_PAWN >= beta) // IID Margin
                       )
                    {
                        Depth iid_depth = (2*(depth - 2*DEPTH_ONE) - (PVNode ? DEPTH_ZERO : depth/2))/2; // IID Reduced Depth
                        
                        search_depth<PVNode ? PV : NonPV, false, false> (pos, ss, alpha, beta, iid_depth, true);

                        tte = TT.retrieve (posi_key);
                        if (tte != NULL)
                        {
                            tt_move  = tte->move ();
                            tt_value = value_of_tt (tte->value (), (ss)->ply);
                            tt_depth = tte->depth ();
                            tt_bound = tte->bound ();
                        }
                    }
                }

                singular_ext_node =
                       !RootNode
                    && exclude_move == MOVE_NONE // Recursive singular search is not allowed
                    && tt_move != MOVE_NONE
                    &&    depth >= (PVNode ? 6*DEPTH_ONE : 8*DEPTH_ONE)
                    && tt_depth >= depth-3*DEPTH_ONE
                    && abs (tt_value) < +VALUE_KNOWN_WIN
                    && tt_bound & BOUND_LOWER;

            }

            // Splitpoint start
            // When in check and at SPNode search starts from here

            Value value = -VALUE_INFINITE;

            bool improving =
                   ((ss-2)->static_eval == VALUE_NONE)
                || ((ss-0)->static_eval == VALUE_NONE)
                || ((ss-0)->static_eval >= (ss-2)->static_eval);

            point time;

            if (RootNode)
            {
                if (Threadpool.main () == thread)
                {
                    time = now () - SearchTime;
                    if (time > INFO_INTERVAL)
                    {
                        sync_cout
                            << "info"
                            << " depth " << u16(depth)
                            << " time "  << time
                            << sync_endl;
                    }
                }
            }

            Move * counter_moves =  CounterMoveStats.moves (pos, dst_sq ((ss-1)->current_move));
            Move *followup_moves = FollowupMoveStats.moves (pos, dst_sq ((ss-2)->current_move));

            MovePicker mp (pos, HistoryStatistics, tt_move, depth, counter_moves, followup_moves, ss);
            StateInfo si;
            if (ci == NULL) { cc = CheckInfo (pos); ci = &cc; }

            u08   legals = 0
                , quiets = 0;

            Move quiet_moves[MAX_QUIETS] = { MOVE_NONE };

            // Step 11. Loop through moves
            // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
            while ((move = mp.next_move<SPNode> ()) != MOVE_NONE)
            {
                ASSERT (_ok (move));

                if (move == exclude_move) continue;

                bool move_legal;
                // At root obey the "searchmoves" option and skip moves not listed in
                // RootMove list, as a consequence any illegal move is also skipped.
                // In MultiPV mode also skip PV moves which have been already searched.
                if (RootNode)
                {
                    if (count (RootMoves.begin () + IndexPV, RootMoves.end (), move) == 0) continue;
                    move_legal = true;
                }
                else
                {
                    move_legal = pos.legal (move, ci->pinneds);
                }

                if (SPNode)
                {
                    // Shared counter cannot be decremented later if move turns out to be illegal
                    if (!move_legal) continue;

                    legals = ++splitpoint->legals;
                    splitpoint->mutex.unlock ();
                }
                else
                {
                    ++legals;
                }

                //u64 nodes = U64(0);

                if (RootNode)
                {
                    //nodes = pos.game_nodes ();

                    Signals.root_1stmove = (1 == legals);

                    if (Threadpool.main () == thread)
                    {
                        time = now () - SearchTime;
                        if (time > INFO_INTERVAL)
                        {
                            sync_cout
                                << "info"
                                //<< " depth "          << u16(depth)
                                << " currmovenumber " << setw (2) << u16(legals + IndexPV)
                                << " currmove "       << move_to_can (move, Chess960)
                                << " time "           << time
                                << sync_endl;
                        }
                    }
                }

                Depth ext = DEPTH_ZERO;

                bool capture_or_promotion = pos.capture_or_promotion (move);

                bool gives_check = pos.gives_check (move, *ci);

                MoveT mt = mtype (move);

                bool dangerous =
                       gives_check
                    || NORMAL != mt
                    || pos.advanced_pawn_push (move);

                // Step 12. Extend the move which seems dangerous like ...checks etc.
                if (gives_check && pos.see_sign (move) >= VALUE_ZERO)
                {
                    ext = DEPTH_ONE;
                }

                // Singular extension(SE) search.
                // We extend the TT move if its value is much better than its siblings.
                // If all moves but one fail low on a search of (alpha-s, beta-s),
                // and just one fails high on (alpha, beta), then that move is singular
                // and should be extended. To verify this do a reduced search on all the other moves
                // but the tt_move, if result is lower than tt_value minus a margin then extend tt_move.
                if (  move_legal
                   && singular_ext_node
                   && move == tt_move
                   && ext == DEPTH_ZERO
                   )
                {
                    Value bound = tt_value - 2*i32(depth);

                    (ss)->exclude_move = move;
                    value = search_depth<NonPV, false, false> (pos, ss, bound-1, bound, depth/2, cut_node);
                    (ss)->exclude_move = MOVE_NONE;

                    if (value < bound) ext = DEPTH_ONE;
                }

                // Update the current move (this must be done after singular extension search)
                Depth new_depth = depth - DEPTH_ONE + ext;

                // Step 13. Pruning at shallow depth (exclude PV nodes)
                if (!PVNode && !MateSearch)
                {
                    if (  !capture_or_promotion
                       && !in_check
                       && !dangerous
                       && best_value > -VALUE_MATE_IN_MAX_DEPTH
                       )
                    {
                        // Move count based pruning
                        if (  depth < FutilityMoveCountDepth
                           && legals >= FutilityMoveCounts[improving][depth]
                           )
                        {
                            if (SPNode) splitpoint->mutex.lock ();
                            continue;
                        }

                        // Value based pruning
                        Depth predicted_depth = new_depth - reduction_depths<PVNode> (improving, depth, legals);

                        // Futility pruning: parent node
                        if (predicted_depth < FutilityMarginDepth)
                        {
                            Value futility_value = (ss)->static_eval + FutilityMargins[predicted_depth]
                                                 + GainStatistics[pos[org_sq (move)]][dst_sq (move)] + VALUE_EG_PAWN/2;

                            if (alpha >= futility_value)
                            {
                                best_value = max (futility_value, best_value);

                                if (SPNode)
                                {
                                    splitpoint->mutex.lock ();
                                    if (splitpoint->best_value < best_value) splitpoint->best_value = best_value;
                                }
                                continue;
                            }
                        }

                        // Prune moves with negative SEE at low depths
                        if (  predicted_depth < RazorDepth
                           && pos.see_sign (move) < VALUE_ZERO
                           )
                        {
                            if (SPNode) splitpoint->mutex.lock ();
                            continue;
                        }
                    }
                }

                // Speculative prefetch as early as possible
                prefetch (reinterpret_cast<char*> (TT.cluster_entry (pos.posi_move_key (move))));

                if (!SPNode)
                {
                    if (!RootNode && !move_legal)
                    {
                        --legals;
                        continue;
                    }

                    if (  quiets < MAX_QUIETS
                       && !capture_or_promotion
                       )
                    {
                        quiet_moves[quiets++] = move;
                    }
                }

                bool move_pv = PVNode && 1 == legals;

                (ss)->current_move = move;

                // Step 14. Make the move
                pos.do_move (move, si, gives_check ? ci : NULL);

                prefetch (reinterpret_cast<char*> (thread->pawn_table[pos.pawn_key ()]));
                prefetch (reinterpret_cast<char*> (thread->matl_table[pos.matl_key ()]));

                if (!move_pv)
                {
                    bool full_depth_search = true;
                    // Step 15. Reduced depth search (LMR).
                    // If the move fails high will be re-searched at full depth.
                    if (  depth > 2*DEPTH_ONE
                       && !capture_or_promotion
                       && move != tt_move
                       && move != (ss)->killer_moves[0]
                       && move != (ss)->killer_moves[1]
                       )
                    {
                        Depth reduction_depth = reduction_depths<PVNode> (improving, depth, legals);

                        if (  (!PVNode && cut_node)
                           || HistoryStatistics[pos[dst_sq (move)]][dst_sq (move)] < VALUE_ZERO
                           )
                        {
                            reduction_depth += DEPTH_ONE;
                        }
                        // Decrease reduction for counter moves
                        if (  reduction_depth > DEPTH_ZERO
                           && (move == counter_moves[0] || move == counter_moves[1])
                           )
                        {
                            reduction_depth = max (reduction_depth-DEPTH_ONE, DEPTH_ZERO);
                        }
                        // Decrease reduction for moves that escape a capture
                        if (  reduction_depth > DEPTH_ZERO
                           && mt == NORMAL
                           && ptype (pos[dst_sq (move)]) != PAWN
                           && pos.see (mk_move<NORMAL> (dst_sq (move), org_sq (move))) < VALUE_ZERO // Reverse move
                           )
                        {
                            reduction_depth = max (reduction_depth-DEPTH_ONE, DEPTH_ZERO);
                        }

                        Depth reduced_depth;
                        
                        if (SPNode) alpha = splitpoint->alpha;
                        reduced_depth = max (new_depth - reduction_depth, DEPTH_ONE);
                        // Search with reduced depth
                        value = -search_depth<NonPV, false, true> (pos, ss+1, -alpha-1, -alpha, reduced_depth, true);

                        // Re-search at intermediate depth if reduction is very high
                        if (alpha < value && reduction_depth >= 4*DEPTH_ONE)
                        {
                            reduced_depth = max (new_depth - reduction_depth/2, DEPTH_ONE);
                            // Search with reduced depth
                            value = -search_depth<NonPV, false, true> (pos, ss+1, -alpha-1, -alpha, reduced_depth, true);

                            // Re-search at intermediate depth if reduction is very high
                            if (alpha < value && reduction_depth >= 8*DEPTH_ONE)
                            {
                                reduced_depth = max (new_depth - reduction_depth/4, DEPTH_ONE);
                                // Search with reduced depth
                                value = -search_depth<NonPV, false, true> (pos, ss+1, -alpha-1, -alpha, reduced_depth, true);
                            }
                        }

                        full_depth_search = alpha < value && reduction_depth != DEPTH_ZERO;
                    }

                    // Step 16. Full depth search, when LMR is skipped or fails high
                    if (full_depth_search)
                    {
                        if (SPNode) alpha = splitpoint->alpha;

                        value =
                            new_depth < DEPTH_ONE ?
                                gives_check ?
                                    -search_quien<NonPV, true >   (pos, ss+1, -alpha-1, -alpha, DEPTH_ZERO) :
                                    -search_quien<NonPV, false>   (pos, ss+1, -alpha-1, -alpha, DEPTH_ZERO) :
                                -search_depth<NonPV, false, true> (pos, ss+1, -alpha-1, -alpha, new_depth, !cut_node);
                    }
                }

                // Principal Variation Search (PV nodes only)
                if (PVNode)
                {
                    // Do a full PV search on:
                    // - pv first move
                    // - fail high move (search only if value < beta)
                    // otherwise let the parent node fail low with
                    // alpha >= value and to try another better move.
                    if (move_pv || (alpha < value && (RootNode || value < beta)))
                    {
                        value =
                            new_depth < DEPTH_ONE ?
                                gives_check ?
                                    -search_quien<PV, true >   (pos, ss+1, -beta, -alpha, DEPTH_ZERO) :
                                    -search_quien<PV, false>   (pos, ss+1, -beta, -alpha, DEPTH_ZERO) :
                                -search_depth<PV, false, true> (pos, ss+1, -beta, -alpha, new_depth, false);
                    }
                }

                // Step 17. Undo move
                pos.undo_move ();

                ASSERT (-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Step 18. Check for new best move
                if (SPNode)
                {
                    splitpoint->mutex.lock ();
                    alpha      = splitpoint->alpha;
                    best_value = splitpoint->best_value;
                }
                
                // Finished searching the move. If a stop or a cutoff occurred,
                // the return value of the search cannot be trusted,
                // and return immediately without updating best move, PV and TT.
                if (Signals.force_stop || thread->cutoff_occurred ())
                {
                    return VALUE_ZERO;
                }

                if (RootNode)
                {
                    RootMove &rm = *find (RootMoves.begin (), RootMoves.end (), move);
                    // Remember searched nodes counts for this rootmove
                    //rm.nodes += pos.game_nodes () - nodes;

                    // PV move or new best move ?
                    if (move_pv || alpha < value)
                    {
                        rm.new_value = value;
                        rm.extract_pv_from_tt (pos);

                        // Record how often the best move has been changed in each iteration.
                        // This information is used for time management:
                        // When the best move changes frequently, allocate some more time.
                        if (!move_pv)
                        {
                            RootMoves.best_move_change++;
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
                    best_value = (SPNode) ? splitpoint->best_value = value : value;

                    if (alpha < value)
                    {
                        best_move = (SPNode) ? splitpoint->best_move = move : move;
                        // Fail high
                        if (value >= beta)
                        {
                            if (SPNode) splitpoint->cut_off = true;

                            break;
                        }

                        ASSERT (value < beta);
                        // Update alpha here! always alpha < beta
                        if (PVNode) alpha = (SPNode) ? splitpoint->alpha = value : value;
                    }
                }

                // Step 19. Check for splitting the search (at non-splitpoint node)
                if (!SPNode)
                {
                    if (  Threadpool.split_depth <= depth
                       && Threadpool.size () > 1
                       && thread->splitpoint_count < MAX_SPLITPOINTS
                       && (thread->active_splitpoint == NULL || !thread->active_splitpoint->slave_searching)
                       )
                    {
                        ASSERT (-VALUE_INFINITE <= alpha && alpha >= best_value && alpha < beta && best_value <= beta && beta <= +VALUE_INFINITE);

                        thread->split (pos, ss, alpha, beta, best_value, best_move, depth, legals, mp, NT, cut_node);
                        
                        if (Signals.force_stop || thread->cutoff_occurred ())
                        {
                            return VALUE_ZERO;
                        }

                        if (best_value >= beta)
                        {
                            break;
                        }
                    }
                }
            }

            // Step 20. Check for checkmate and stalemate
            if (!SPNode)
            {
                // If all possible moves have been searched and if there are no legal moves,
                // If in a singular extension search then return a fail low score (alpha).
                // Otherwise it must be mate or stalemate, so return value accordingly.
                if (legals == 0)
                {
                    best_value = 
                        exclude_move != MOVE_NONE ?
                            alpha : in_check ?
                                mated_in ((ss)->ply) :
                                DrawValue[pos.active ()];
                }
                else
                // Quiet best move: Update history, killer, counter & followup moves
                if (  !in_check
                   && best_value >= beta
                   && best_move != MOVE_NONE
                   && !pos.capture_or_promotion (best_move)
                   )
                {
                    update_stats (pos, ss, best_move, depth, quiet_moves, quiets-1);
                }

                TT.store (
                    posi_key,
                    best_move,
                    depth,
                    best_value >= beta ? BOUND_LOWER :
                        PVNode && best_move != MOVE_NONE ? BOUND_EXACT : BOUND_UPPER,
                    value_to_tt (best_value, (ss)->ply),
                    (ss)->static_eval);
            }

            ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        CACHE_ALIGN(32) Stack Stacks[MAX_DEPTH_6];
        // search_iter_deepening() is the main iterative deepening search function.
        // It calls search() repeatedly with increasing depth until:
        // - the allocated thinking time has been consumed,
        // - the user stops the search,
        // - the maximum search depth is reached.
        // Time management; with iterative deepining enabled you can specify how long
        // you want the computer to think rather than how deep you want it to think. 
        inline void search_iter_deepening ()
        {
            Stack *ss = Stacks+2; // To allow referencing (ss-2)
            memset (ss-2, 0x00, 5*sizeof (*ss));

            TT.new_gen ();
            GainStatistics.clear ();
            HistoryStatistics.clear ();
            CounterMoveStats.clear ();
            FollowupMoveStats.clear ();

            u08 skill_pv = Skills.pv_size ();
            if (skill_pv != 0) Skills.clear ();

            // Do have to play with skill handicap?
            // In this case enable MultiPV search by skill pv size
            // that will use behind the scenes to retrieve a set of possible moves.
            LimitPV = min (max (MultiPV, skill_pv), RootSize);

            Value best_value = VALUE_ZERO
                , bound_a    = -VALUE_INFINITE
                , bound_b    = +VALUE_INFINITE
                , window_a   = VALUE_ZERO
                , window_b   = VALUE_ZERO;

            Depth depth = DEPTH_ZERO;

            point iteration_time;

            // Iterative deepening loop until target depth reached
            while (++depth <= MAX_DEPTH && (Limits.depth == 0 || depth <= Limits.depth))
            {
                // Requested to stop?
                if (Signals.force_stop) break;

                // Age out PV variability metric
                RootMoves.best_move_change *= 0.5f;

                // Save last iteration's scores before first PV line is searched and
                // all the move scores but the (new) PV are set to -VALUE_INFINITE.
                for (u08 i = 0; i < RootSize; ++i)
                {
                    RootMoves[i].old_value = RootMoves[i].new_value;
                }

                const bool aspiration = depth > 4*DEPTH_ONE;

                // MultiPV loop. Perform a full root search for each PV line
                for (IndexPV = 0; IndexPV < LimitPV; ++IndexPV)
                {
                    // Requested to stop?
                    if (Signals.force_stop) break;

                    // Reset Aspiration window starting size
                    if (aspiration)
                    {
                        window_a =
                        window_b =
                            //Value(16);
                            Value(depth <= 32*DEPTH_ONE ? 22 - (depth-1)/4 : 14); // Decreasing window

                        bound_a = max (RootMoves[IndexPV].old_value - window_a, -VALUE_INFINITE);
                        bound_b = min (RootMoves[IndexPV].old_value + window_b, +VALUE_INFINITE);
                    }

                    // Start with a small aspiration window and, in case of fail high/low,
                    // research with bigger window until not failing high/low anymore.
                    do
                    {
                        best_value = search_depth<Root, false, true> (RootPos, ss, bound_a, bound_b, depth, false);

                        // Bring to front the best move. It is critical that sorting is
                        // done with a stable algorithm because all the values but the first
                        // and eventually the new best one are set to -VALUE_INFINITE and
                        // want to keep the same order for all the moves but the new PV
                        // that goes to the front. Note that in case of MultiPV search
                        // the already searched PV lines are preserved.
                        //RootMoves.sort_end (IndexPV);
                        stable_sort (RootMoves.begin () + IndexPV, RootMoves.end ());

                        // Write PV back to transposition table in case the relevant
                        // entries have been overwritten during the search.
                        for (i08 i = IndexPV; i >= 0; --i)
                        {
                            RootMoves[i].insert_pv_into_tt (RootPos);
                        }

                        iteration_time = now () - SearchTime;

                        // If search has been stopped break immediately.
                        // Sorting and writing PV back to TT is safe becuase
                        // RootMoves is still valid, although refers to previous iteration.
                        if (Signals.force_stop) break;

                        // When failing high/low give some update
                        // (without cluttering the UI) before to re-search.
                        if (  iteration_time > INFO_INTERVAL
                           && (bound_a >= best_value || best_value >= bound_b)
                           )
                        {
                            sync_cout << info_multipv (RootPos, depth, bound_a, bound_b, iteration_time) << sync_endl;
                        }

                        // In case of failing low/high increase aspiration window and re-search,
                        // otherwise exit the loop.
                        if (best_value <= bound_a)
                        {
                            window_a *= 1.345f;
                            bound_a   = max (best_value - window_a, -VALUE_INFINITE);
                            Signals.root_failedlow = true;
                            Signals.ponderhit_stop = false;
                        }
                        else
                        if (best_value >= bound_b)
                        {
                            window_b *= 1.345f;
                            bound_b   = min (best_value + window_b, +VALUE_INFINITE);
                        }
                        else break;

                        ASSERT (-VALUE_INFINITE <= bound_a && bound_a < bound_b && bound_b <= +VALUE_INFINITE);
                    } while (true);

                    // Sort the PV lines searched so far and update the GUI
                    //RootMoves.sort_beg (IndexPV + 1);
                    stable_sort (RootMoves.begin (), RootMoves.begin () + IndexPV + 1);

                    if (IndexPV + 1 == LimitPV || iteration_time > INFO_INTERVAL)
                    {
                        sync_cout << info_multipv (RootPos, depth, bound_a, bound_b, iteration_time) << sync_endl;
                    }
                }

                if (ContemptValue > 0)
                {
                    i16 valued_contempt = i16(RootMoves[0].new_value)/ContemptValue;
                    DrawValue[ RootColor] = BaseContempt[ RootColor] - Value(valued_contempt);
                    DrawValue[~RootColor] = BaseContempt[~RootColor] + Value(valued_contempt);
                }

                // If skill levels are enabled and time is up, pick a sub-optimal best move
                if (skill_pv != 0 && Skills.can_pick_move (depth))
                {
                    Skills.play_move ();
                }

                iteration_time = now () - SearchTime;

                if (!white_spaces (SearchLog))
                {
                    LogFile logfile (SearchLog);
                    logfile << pretty_pv (RootPos, depth, RootMoves[0].new_value, iteration_time, &RootMoves[0].pv[0]) << endl;
                }

                // Requested to stop?
                if (Signals.force_stop) break;

                // Stop the search early:
                bool stop = false;

                // Do have time for the next iteration? Can stop searching now?
                if (!Signals.ponderhit_stop && Limits.use_timemanager ())
                {
                    // Time adjustments
                    if (aspiration && LimitPV == 1)
                    {
                        // Take in account some extra time if the best move has changed
                        TimeMgr.instability (RootMoves.best_move_change);
                    }

                    // If there is only one legal move available or 
                    // If all of the available time has been used.
                    if (RootSize == 1 || iteration_time > TimeMgr.available_time ())
                    {
                        stop = true;
                    }
                }
                else
                {
                    // Have found a "mate in <x>"?
                    if (  MateSearch
                       && best_value >= +VALUE_MATE_IN_MAX_DEPTH
                       && i16(VALUE_MATE - best_value) <= 2*Limits.mate
                       )
                    {
                        stop = true;
                    }
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
                        Signals.force_stop     = true;
                    }
                }

            }

            if (skill_pv != 0) Skills.play_move ();
        }

        // perft<>() is our utility to verify move generation. All the leaf nodes
        // up to the given depth are generated and counted and the sum returned.
        template<bool RootNode>
        u64 perft (Position &pos, Depth depth)
        {
            u64 leaf_nodes = U64(0);

            CheckInfo ci (pos);
            for (MoveList<LEGAL> ms (pos); *ms != MOVE_NONE; ++ms)
            {
                u64 inter_nodes = 1;
                if (!RootNode || depth > 1*DEPTH_ONE)
                {
                    Move m = *ms;
                    StateInfo si;
                    pos.do_move (m, si, pos.gives_check (m, ci) ? &ci : NULL);
                    inter_nodes = depth <= 2*DEPTH_ONE ? MoveList<LEGAL>(pos).size () : perft<false> (pos, depth-DEPTH_ONE);
                    pos.undo_move ();
                }

                if (RootNode)
                {
                    sync_cout <<  left << setw ( 7) << setfill (' ') <<
                              //move_to_can (*ms, Chess960)
                              move_to_san (*ms, pos)
                              << right << setw (12) << setfill ('.') << inter_nodes << sync_endl;
                }

                leaf_nodes += inter_nodes;
            }
            return leaf_nodes;
        }

    } // namespace

    bool                Chess960 = false;

    LimitsT             Limits;
    SignalsT volatile   Signals;

    RootMoveList        RootMoves;
    Position            RootPos;
    StateInfoStackPtr   SetupStates;

    point               SearchTime;

    u08                 MultiPV       = 1;
    //i32                 MultiPV_cp    = 0;

    i16                 FixedContempt = 0
        ,               ContemptTime  = 22
        ,               ContemptValue = 34;

    string              HashFile      = "Hash.dat";
    u16                 AutoSaveTime  = 0;
    bool                AutoLoadHash  = false;

    string              BookFile      = "";
    bool                BestBookMove  = true;

    string              SearchLog     = "";

    // initialize the PRNG only once
    PolyglotBook        Book;

    Skill               Skills (MAX_SKILL_LEVEL);

    // ------------------------------------

    // RootMove::extract_pv_from_tt() builds a PV by adding moves from the TT table.
    // Consider also failing high nodes and not only EXACT nodes so to
    // allow to always have a ponder move even when fail high at root node.
    // This results in a long PV to print that is important for position analysis.
    void   RootMove::extract_pv_from_tt (Position &pos)
    {
        StateInfo states[MAX_DEPTH_6]
                , *si = states;

        i08 ply = 0; // Ply starts from 1, we need to start from 0
        Move m = pv[ply];
        pv.clear ();
        Value expected_value = new_value;
        const Entry *tte;
        do
        {
            ASSERT (MoveList<LEGAL> (pos).contains (m));

            pv.push_back (m);
            pos.do_move (m, *si++);

            ++ply;
            expected_value = -expected_value;

            tte = TT.retrieve (pos.posi_key ());
        } while (  tte != NULL
                && expected_value == value_of_tt (tte->value (), ply+1)
                && (m = tte->move ()) != MOVE_NONE // Local copy, TT could change
                && pos.pseudo_legal (m)
                && pos.legal (m)
                && ply < MAX_DEPTH
                && (!pos.draw () || ply < 2));
        
        do
        {
            pos.undo_move ();
            --ply;
        } while (0 != ply);

        pv.push_back (MOVE_NONE); // Must be zero-terminating
    }
    // RootMove::insert_pv_in_tt() is called at the end of a search iteration, and
    // inserts the PV back into the TT. This makes sure the old PV moves are searched
    // first, even if the old TT entries have been overwritten.
    void   RootMove:: insert_pv_into_tt (Position &pos)
    {
        StateInfo states[MAX_DEPTH_6]
                , *si = states;

        i08 ply = 0; // Ply starts from 1, we need to start from 0
        Move m = pv[ply];
        const Entry *tte;
        do
        {
            ASSERT (MoveList<LEGAL> (pos).contains (m));

            tte = TT.retrieve (pos.posi_key ());
            // Don't overwrite correct entries
            if (tte == NULL || tte->move () != m)
            {
                TT.store (
                    pos.posi_key (),
                    m,
                    DEPTH_NONE,
                    BOUND_NONE,
                    VALUE_NONE,
                    VALUE_NONE);
            }

            pos.do_move (m, *si++);

            m = pv[++ply];
        } while (MOVE_NONE != m);

        do
        {
            pos.undo_move ();
            --ply;
        } while (0 != ply);
    }

    string RootMove::info_pv () const
    {
        stringstream ss;
        for (u08 i = 0; pv[i] != MOVE_NONE; ++i)
        {
            ss << " " << move_to_can (pv[i], Chess960);
        }
        return ss.str ();
    }

    // ------------------------------------

    void RootMoveList::initialize (const Position &pos, const vector<Move> &root_moves)
    {
        best_move_change = 0.0f;
        clear ();
        for (MoveList<LEGAL> ms (pos); *ms != MOVE_NONE; ++ms)
        {
            if (root_moves.empty () || count (root_moves.begin (), root_moves.end (), *ms))
            {
                push_back (RootMove (*ms));
            }
        }
    }

    // ------------------------------------

    u08  Skill::pv_size () const
    {
        return _level < MAX_SKILL_LEVEL ? min (MIN_SKILL_MULTIPV, RootSize) : 0;
    }

    // When playing with a strength handicap, choose best move among the first 'candidates'
    // RootMoves using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
    Move Skill::pick_move ()
    {
        static RKISS rk;
        // PRNG sequence should be not deterministic
        for (i08 i = now () % 50; i > 0; --i) rk.rand64 ();

        _best_move = MOVE_NONE;

        u08 skill_pv = pv_size ();
        // RootMoves are already sorted by score in descending order
        Value variance   = min (RootMoves[0].new_value - RootMoves[skill_pv - 1].new_value, VALUE_MG_PAWN);
        Value weakness   = Value(MAX_DEPTH - 2 * _level);
        Value best_value = -VALUE_INFINITE;
        // Choose best move. For each move score add two terms both dependent on
        // weakness, one deterministic and bigger for weaker moves, and one random,
        // then choose the move with the resulting highest score.
        for (u08 i = 0; i < skill_pv; ++i)
        {
            Value v = RootMoves[i].new_value;

            // Don't allow crazy blunders even at very low skills
            if (i > 0 && RootMoves[i-1].new_value > (v + 2*VALUE_MG_PAWN))
            {
                break;
            }

            // This is our magic formula
            v += weakness * i32(RootMoves[0].new_value - v)
                +  variance * i32(rk.rand<u32> () % weakness) / i32(VALUE_EG_PAWN/2);

            if (best_value < v)
            {
                best_value = v;
                _best_move = RootMoves[i].pv[0];
            }
        }
        return _best_move;
    }

    // Swap best PV line with the sub-optimal one
    void Skill::play_move ()
    {
        swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), _best_move != MOVE_NONE ? _best_move : pick_move ()));
    }

    // ------------------------------------

    u64  perft (Position &pos, Depth depth)
    {
        return perft<true> (pos, depth);
    }

    // Main searching method
    void think ()
    {
        RootColor = RootPos.active ();
        RootPly   = RootPos.game_ply ();
        RootSize  = RootMoves.size ();

        if (!white_spaces (SearchLog))
        {
            LogFile logfile (SearchLog);

            logfile
                << "----------->\n" << boolalpha
                << "RootPos  : " << RootPos.fen ()                   << "\n"
                << "RootSize : " << u16(RootSize)                    << "\n"
                << "Infinite : " << Limits.infinite                  << "\n"
                << "Ponder   : " << Limits.ponder                    << "\n"
                << "ClockTime: " << Limits.gameclock[RootColor].time << "\n"
                << "Increment: " << Limits.gameclock[RootColor].inc  << "\n"
                << "MoveTime : " << Limits.movetime                  << "\n"
                << "MovesToGo: " << u16(Limits.movestogo)            << "\n"
                << " Depth Score    Time       Nodes  PV\n"
                << "-----------------------------------------------------------"
                << endl;
        }

        MateSearch = Limits.mate != 0;

        if (RootSize != 0)
        {
            if (!white_spaces (BookFile) && !Limits.infinite && !MateSearch)
            {
                trim (BookFile);
                convert_path (BookFile);

                if (!Book.is_open () && !white_spaces (BookFile))
                {
                    Book.open (BookFile, ios_base::in|ios_base::binary);
                }
                if (Book.is_open ())
                {
                    Move book_move = Book.probe_move (RootPos, BestBookMove);
                    if (book_move != MOVE_NONE && count (RootMoves.begin (), RootMoves.end (), book_move))
                    {
                        swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), book_move));
                        goto finish;
                    }
                }
            }

            TimeMgr.initialize (Limits.gameclock[RootColor], Limits.movestogo, RootPly);

            i16 timed_contempt = 0;
            i16 diff_time = 0;
            if (  ContemptTime > 0
               && (diff_time = i16(Limits.gameclock[RootColor].time - Limits.gameclock[~RootColor].time)/MILLI_SEC) != 0
               //&& ContemptTime <= abs (diff_time)
               )
            {
                timed_contempt = diff_time / ContemptTime;
            }

            Value contempt = Value(cp_to_value (float(FixedContempt + timed_contempt) / 0x64)); // 100
            DrawValue[ RootColor] = BaseContempt[ RootColor] = VALUE_DRAW - contempt;
            DrawValue[~RootColor] = BaseContempt[~RootColor] = VALUE_DRAW + contempt;

            // Reset the threads, still sleeping: will wake up at split time
            Threadpool.max_ply = 0;
            if (AutoLoadHash)
            {
                TT.load (HashFile);
            }
            if (AutoSaveTime != 0)
            {
                FirstAutoSave = true;
                Threadpool.auto_save_th        = new_thread<TimerThread> ();
                Threadpool.auto_save_th->task  = auto_save;
                Threadpool.auto_save_th->resolution = AutoSaveTime*MINUTE_MILLI_SEC;
                Threadpool.auto_save_th->start ();
                Threadpool.auto_save_th->notify_one ();
            }

            Threadpool.check_limits_th->start ();
            Threadpool.check_limits_th->notify_one (); // Wake up the recurring timer

            search_iter_deepening (); // Let's start searching !

            Threadpool.check_limits_th->stop ();

            if (AutoSaveTime > 0)
            {
                Threadpool.auto_save_th->stop ();
                Threadpool.auto_save_th->kill ();
                delete_thread (Threadpool.auto_save_th);
                Threadpool.auto_save_th = NULL;
            }

            if (!white_spaces (SearchLog))
            {
                LogFile logfile (SearchLog);

                point time = now () - SearchTime;

                logfile
                    << "Time (ms)  : " << time                                      << "\n"
                    << "Nodes (N)  : " << RootPos.game_nodes ()                     << "\n"
                    << "Speed (N/s): " << RootPos.game_nodes ()*MILLI_SEC / max (time, point(1)) << "\n"
                    << "Hash-full  : " << TT.permill_full ()                        << "\n"
                    << "Best move  : " << move_to_san (RootMoves[0].pv[0], RootPos) << "\n";
                if (RootMoves[0].pv[0] != MOVE_NONE)
                {
                    StateInfo si;
                    RootPos.do_move (RootMoves[0].pv[0], si);
                    logfile << "Ponder move: " << move_to_san (RootMoves[0].pv[1], RootPos);
                    RootPos.undo_move ();
                }
                logfile << endl;
            }
        }
        else
        {
            sync_cout
                << "info"
                << " depth " << 0
                << " score " << pretty_score (RootPos.checkers () != U64(0) ? -VALUE_MATE : VALUE_DRAW)
                << " time "  << 0
                << sync_endl;

            RootMoves.push_back (RootMove (MOVE_NONE));

            if (!white_spaces (SearchLog))
            {
                LogFile logfile (SearchLog);

                logfile
                    << pretty_pv (RootPos, 0, RootPos.checkers () != U64(0) ? -VALUE_MATE : VALUE_DRAW, 0, &RootMoves[0].pv[0]) << "\n"
                    << "Time (ms)  : " << 0        << "\n"
                    << "Nodes (N)  : " << 0        << "\n"
                    << "Speed (N/s): " << 0        << "\n"
                    << "Hash-full  : " << 0        << "\n"
                    << "Best move  : " << "(none)" << "\n"
                    << endl;
            }

        }

    finish:
        point time = now () - SearchTime;

        // When search is stopped this info is printed
        sync_cout
            << "info"
            << " time "     << time
            << " nodes "    << RootPos.game_nodes ()
            << " nps "      << RootPos.game_nodes () * MILLI_SEC / max (time, point(1))
            << " hashfull " << 0//TT.permill_full ()
            << sync_endl;

        // When reach max depth arrive here even without Signals.force_stop is raised,
        // but if are pondering or in infinite search, according to UCI protocol,
        // shouldn't print the best move before the GUI sends a "stop" or "ponderhit" command.
        // Simply wait here until GUI sends one of those commands (that raise Signals.force_stop).
        if (!Signals.force_stop && (Limits.ponder || Limits.infinite))
        {
            Signals.ponderhit_stop = true;
            RootPos.thread ()->wait_for (Signals.force_stop);
        }

        // Best move could be MOVE_NONE when searching on a stalemate position
        sync_cout << "bestmove " << move_to_can (RootMoves[0].pv[0], Chess960);
        if (RootMoves[0].pv[0] != MOVE_NONE)
        {
            cout << " ponder " << move_to_can (RootMoves[0].pv[1], Chess960);
        }
        cout << sync_endl;

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
            FutilityMoveCounts[0][d] = u08(2.40f + 0.773f * pow (0.00f + d, 1.80f));
            FutilityMoveCounts[1][d] = u08(2.90f + 1.045f * pow (0.49f + d, 1.80f));
        }

        float red[2];
        ReductionDepths[0][0][0][0] =
        ReductionDepths[0][1][0][0] =
        ReductionDepths[1][0][0][0] =
        ReductionDepths[1][1][0][0] = 0;
        // Initialize reductions lookup table
        for (d = 1; d < ReductionDepth; ++d) // depth
        {
            for (mc = 1; mc < ReductionMoveCount; ++mc) // move-count
            {
                red[0] = 0.000f + log (float(d)) * log (float(mc)) / 3.00f;
                red[1] = 0.333f + log (float(d)) * log (float(mc)) / 2.25f;
                ReductionDepths[1][1][d][mc] = u08(red[0] >= 1.0f ? red[0] + 0.5f : 0);
                ReductionDepths[0][1][d][mc] = u08(red[1] >= 1.0f ? red[1] + 0.5f : 0);

                ReductionDepths[1][0][d][mc] = ReductionDepths[1][1][d][mc];
                ReductionDepths[0][0][d][mc] = ReductionDepths[0][1][d][mc];
                // Smoother transition for LMR
                if (ReductionDepths[0][0][d][mc] >= 2*DEPTH_ONE)
                {
                    ReductionDepths[0][0][d][mc] += DEPTH_ONE;
                }
            }
        }
    }

}

namespace Threads {

    // check_limits() is called by the timer thread when the timer triggers.
    // It is used to print debug info and, more importantly,
    // to detect when out of available time or reached limits
    // and thus stop the search.
    void check_limits ()
    {
        static point last_time = now ();

        point now_time = now ();
        if (now_time - last_time >= MILLI_SEC)
        {
            last_time = now_time;
            dbg_print ();
        }

        point movetime = now_time - SearchTime;
        if (!Limits.ponder && Limits.use_timemanager ())
        {
            if (  movetime > TimeMgr.maximum_time () - 2 * TimerResolution
                  // Still at first move
               || (   Signals.root_1stmove
                  && !Signals.root_failedlow
                  && movetime > TimeMgr.available_time () * 0.75f
                  )
               )
            {
               Signals.force_stop = true;
            }
        }
        else
        if (Limits.movetime != 0 && movetime >= Limits.movetime)
        {
            Signals.force_stop = true;
        }
        else
        if (Limits.nodes != 0)
        {
            u64 nodes = RootPos.game_nodes ();

            Threadpool.mutex.lock ();
            
            // Loop across all splitpoints and sum accumulated splitpoint nodes plus
            // all the currently active positions nodes.
            for (u08 t = 0; t < Threadpool.size (); ++t)
            {
                for (u08 s = 0; s < Threadpool[t]->splitpoint_count; ++s)
                {
                    SplitPoint &sp = Threadpool[t]->splitpoints[s];
                    sp.mutex.lock ();

                    nodes += sp.nodes;
                    for (u08 idx = 0; idx < Threadpool.size (); ++idx)
                    {
                        if (sp.slaves_mask.test (idx))
                        {
                            const Position *pos = Threadpool[idx]->active_pos;
                            if (pos != NULL) nodes += pos->game_nodes ();
                        }
                    }

                    sp.mutex.unlock ();
                }
            }

            Threadpool.mutex.unlock ();

            if (nodes >= Limits.nodes)
            {
                Signals.force_stop = true;
            }
        }
    }

    void auto_save ()
    {
        if (FirstAutoSave)
        {
            FirstAutoSave = false;
            return;
        }
        TT.save (HashFile);
    }

    // Thread::idle_loop() is where the thread is parked when it has no work to do
    void Thread::idle_loop ()
    {
        // Pointer 'splitpoint' is not null only if called from split<>(), and not
        // at the thread creation. So it means this is the splitpoint's master.
        SplitPoint *splitpoint = splitpoint_count != 0 ? active_splitpoint : NULL;
        ASSERT (splitpoint == NULL || (splitpoint->master == this && searching));

        do
        {
            // If this thread has been assigned work, launch a search
            while (searching)
            {
                ASSERT (alive);

                Threadpool.mutex.lock ();

                ASSERT (active_splitpoint != NULL);
                SplitPoint *sp = active_splitpoint;

                Threadpool.mutex.unlock ();

                Stack stack[MAX_DEPTH_6]
                   , *ss = stack+2; // To allow referencing (ss-2)

                Position pos (*(sp)->pos, this);

                memcpy (ss-2, (sp)->ss-2, 5*sizeof (*ss));
                (ss)->splitpoint = sp;

                // Lock splitpoint
                (sp)->mutex.lock ();

                ASSERT (active_pos == NULL);

                active_pos = &pos;

                switch ((sp)->node_type)
                {
                case  Root: search_depth<Root , true, true> (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
                case    PV: search_depth<PV   , true, true> (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
                case NonPV: search_depth<NonPV, true, true> (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
                default: ASSERT (false);
                }

                ASSERT (searching);
                searching  = false;
                active_pos = NULL;
                (sp)->slaves_mask.reset (idx);
                (sp)->slave_searching = false;
                (sp)->nodes += pos.game_nodes ();

                // Wake up master thread so to allow it to return from the idle loop
                // in case the last slave of the splitpoint.
                if (  this != (sp)->master
                   && (sp)->slaves_mask.none ()
                   )
                {
                    ASSERT (!(sp)->master->searching);
                    (sp)->master->notify_one ();
                }

                // After releasing the lock, cannot access anymore any splitpoint
                // related data in a safe way becuase it could have been released under
                // our feet by the sp master.
                (sp)->mutex.unlock ();

                // Try to late join to another split point if none of its slaves has already finished.
                if (Threadpool.size () > 2)
                {
                    for (u08 t = 0; t < Threadpool.size (); ++t)
                    {
                        Thread *thread = Threadpool[t];
                        u08 count = thread->splitpoint_count; // Local copy

                        sp = count != 0 ? &thread->splitpoints[count - 1] : NULL;

                        if (  sp != NULL
                           && (sp)->slave_searching
                           && available_to (thread)
                           )
                        {
                            // Recheck the conditions under lock protection
                            Threadpool.mutex.lock ();
                            (sp)->mutex.lock ();

                            if (  (sp)->slave_searching
                               && available_to (thread)
                               )
                            {
                                (sp)->slaves_mask.set (idx);
                                active_splitpoint = sp;
                                searching = true;
                            }

                            (sp)->mutex.unlock ();
                            Threadpool.mutex.unlock ();

                            //if (searching)
                            break; // Just a single attempt
                        }
                    }
                }
            }

            // Grab the lock to avoid races with Thread::notify_one()
            mutex.lock ();
            // If master and all slaves have finished then exit idle_loop()
            if (splitpoint != NULL && splitpoint->slaves_mask.none ())
            {
                ASSERT (!searching);
                mutex.unlock ();
                break;
            }

            // If not searching, wait for a condition to be signaled instead of
            // wasting CPU time polling for work.
            // Do sleep after retesting sleep conditions under lock protection, in
            // particular to avoid a deadlock in case a master thread has,
            // in the meanwhile, allocated us and sent the notify_one() call before
            // the chance to grab the lock.
            if (alive && !searching)
            {
                sleep_condition.wait (mutex);
            }
            // Release the lock
            mutex.unlock ();

        } while (alive);
    }
}
