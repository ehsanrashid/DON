#include "Searcher.h"

#include <cfloat>
#include <sstream>
#include <iomanip>

#include "UCI.h"
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
using namespace Searcher;

namespace Searcher {

    using namespace BitBoard;
    using namespace MoveGenerator;
    using namespace Evaluator;
    using namespace Notation;

    namespace {

        const Depth           FutilityMoveCountDepth = Depth(16*i16(ONE_MOVE));
        // Futility move count lookup table (initialized at startup)
        CACHE_ALIGN(32) u08   FutilityMoveCounts[2][FutilityMoveCountDepth]; // [improving][depth]
        
        const Depth           FutilityMarginDepth = Depth(7*i16(ONE_MOVE));
        // Futility margin lookup table (initialized at startup)
        CACHE_ALIGN(32) Value FutilityMargins[FutilityMarginDepth];  // [depth]

        const Depth           RazorDepth = Depth(4*i16(ONE_MOVE));
        // Razoring margin lookup table (initialized at startup)
        CACHE_ALIGN(32) Value RazorMargins[RazorDepth];              // [depth]

        const Depth           ReductionDepth     = Depth(32*i16(ONE_MOVE));
        const u08             ReductionMoveCount = 64;
        // Reductions lookup table (initialized at startup)
        CACHE_ALIGN(32) u08   Reductions[2][2][ReductionDepth][ReductionMoveCount];  // [pv][improving][depth][move_num]

        template<bool PVNode>
        inline Depth reduction (bool imp, Depth d, i32 mn)
        {
            return (Depth) Reductions[PVNode][imp][min (d/i32(ONE_MOVE), ReductionDepth-1)][min (mn, ReductionMoveCount-1)];
        }

        const u08           MaxQuiets = 64;

        const point         InfoInterval = 3000; // 3 sec

        Color   RootColor;
        i32     RootPly;
        u08     RootSize   // RootMove Count
            ,   PVLimit
            ,   PVIndex;

        TimeManager TimeMgr;

        Value   DrawValue[CLR_NO]
            ,   BaseContempt[CLR_NO];

        i16     ContemptValue;

        bool    MateSearch;

        string  SearchLog;

        struct Skill
        {
        private:
            u08  level;
            u08  candidates;
            Move move;

        public:

            Skill ()
                : level (MaxSkillLevel)
                , candidates (0)
                , move (MOVE_NONE)
            {}

            Skill (u08 lvl)
                : level (lvl < MaxSkillLevel ? lvl : MaxSkillLevel)
                , candidates (lvl < MaxSkillLevel ? min (MinSkillMultiPV, RootSize) : 0)
                , move (MOVE_NONE)
            {}

           ~Skill ()
            {
                if (candidates) // Swap best PV line with the sub-optimal one
                {
                    swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), move != MOVE_NONE ? move : pick_move ()));
                }
            }

            u08 candidates_size () const { return candidates; }

            bool time_to_pick (i16 depth) const { return (depth == (1 + level)); }

            // When playing with a strength handicap, choose best move among the first 'candidates'
            // RootMoves using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
            Move pick_move ()
            {
                static RKISS rk;
                // PRNG sequence should be not deterministic
                for (i08 i = now () % 50; i > 0; --i) rk.rand64 ();

                // RootMoves are already sorted by score in descending order
                const Value variance = min (RootMoves[0].value[0] - RootMoves[candidates - 1].value[0], VALUE_MG_PAWN);
                const Value weakness = Value(MaxDepth - 2 * level);
                
                Value max_v = -VALUE_INFINITE;
                move        = MOVE_NONE;
                // Choose best move. For each move score add two terms both dependent on
                // weakness, one deterministic and bigger for weaker moves, and one random,
                // then choose the move with the resulting highest score.
                for (u08 i = 0; i < candidates; ++i)
                {
                    Value v = RootMoves[i].value[0];

                    // Don't allow crazy blunders even at very low skills
                    if (i > 0 && RootMoves[i-1].value[0] > (v + 2*VALUE_MG_PAWN))
                    {
                        break;
                    }

                    // This is our magic formula
                    v += (weakness * i32(RootMoves[0].value[0] - v)
                      +   variance * i32(rk.rand<u32> () % weakness) / i32(VALUE_EG_PAWN/2));

                    if (max_v < v)
                    {
                        max_v = v;
                        move = RootMoves[i].pv[0];
                    }
                }
                return move;
            }

        };

        // Gain statistics
        GainStats    GainStatistics;
        // History statistics
        HistoryStats HistoryStatistics;
        // Move statistics
        MoveStats   CounterMoveStats    // Counter
            ,       FollowupMoveStats;  // Followup

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
            HistoryStatistics.success (pos, move, depth);
            for (u08 i = 0; i < quiets; ++i)
            {
                Move m = quiet_moves[i];
                if (m != move) HistoryStatistics.failure (pos, m, depth);
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
            return v >= VALUE_MATES_IN_MAX_PLY ? v + ply :
                   v <= VALUE_MATED_IN_MAX_PLY ? v - ply :
                   v;
        }
        // value_of_tt() is the inverse of value_to_tt ():
        // It adjusts a mate score from the transposition table
        // (where refers to the plies to mate/be mated from current position)
        // to "plies to mate/be mated from the root".
        inline Value value_of_tt (Value v, i32 ply)
        {
            return v == VALUE_NONE             ? VALUE_NONE :
                   v >= VALUE_MATES_IN_MAX_PLY ? v - ply :
                   v <= VALUE_MATED_IN_MAX_PLY ? v + ply :
                   v;
        }

        // info_multipv() formats PV information according to UCI protocol.
        // UCI requires to send all the PV lines also if are still to be searched
        // and so refer to the previous search score.
        inline string info_multipv (const Position &pos, i16 depth, Value alpha, Value beta, point time)
        {
            ASSERT (time >= 0);
            if (time == 0) time = 1;

            stringstream ss;

            for (u08 i = 0; i < PVLimit; ++i)
            {
                bool updated = (i <= PVIndex);

                i16   d;
                Value v;
                
                if (updated)
                {
                    d = depth;
                    v = RootMoves[i].value[0];
                }
                else
                {
                    if (1 == depth) return "";

                    d = depth - 1;
                    v = RootMoves[i].value[1];
                }

                // Not at first line
                if (ss.rdbuf ()->in_avail ()) ss << "\n";

                ss  << "info"
                    << " multipv "  << u16(i + 1)
                    << " depth "    << d
                    << " seldepth " << u16(Threadpool.max_ply)
                    << " score "    << ((i == PVIndex) ? pretty_score (v, alpha, beta) : pretty_score (v))
                    << " time "     << time
                    << " nodes "    << pos.game_nodes ()
                    << " nps "      << pos.game_nodes () * MilliSec / time
                    << " hashfull " << 0//TT.permill_full ()
                    << " pv"        ;//<< RootMoves[i].info_pv (pos);
                for (u08 p = 0; RootMoves[i].pv[p] != MOVE_NONE; ++p)
                {
                    ss << " " << move_to_can (RootMoves[i].pv[p], pos.chess960 ());
                }
            }

            return ss.str ();
        }

        template<NodeT NT, bool InCheck>
        // search_quien() is the quiescence search function,
        // which is called by the main depth limited search function
        // when the remaining depth is ZERO (to be more precise, less than ONE_MOVE).
        inline Value search_quien  (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth)
        {
            const bool    PVNode = NT == PV;

            ASSERT (NT == PV || NT == NonPV);
            ASSERT (InCheck == !!pos.checkers ());
            ASSERT (alpha >= -VALUE_INFINITE && alpha < beta && beta <= +VALUE_INFINITE);
            ASSERT (PVNode || alpha == beta-1);
            ASSERT (depth <= DEPTH_ZERO);

            Move best_move;

            (ss)->current_move = best_move = MOVE_NONE;
            (ss)->ply = (ss-1)->ply + 1;

            // Check for aborted search
            if (Signals.force_stop)   return VALUE_ZERO;
            // Check for immediate draw
            if (pos.draw ())          return DrawValue[pos.active ()];
            // Check for maximum ply reached
            if ((ss)->ply > MaxDepth) return InCheck ? DrawValue[pos.active ()] : evaluate (pos);

            // To flag EXACT a node with eval above alpha and no available moves
            Value old_alpha;
            if (PVNode) old_alpha = alpha;

            // Transposition table lookup
            Key posi_key;
            const TTEntry *tte;
            Move  tt_move    = MOVE_NONE;
            Value tt_value   = VALUE_NONE
                , best_value = VALUE_NONE;
            Depth tt_depth   = DEPTH_NONE;
            Bound tt_bound   = BND_NONE;

            posi_key = pos.posi_key ();
            tte      = TT.retrieve (posi_key);
            if (tte != NULL)
            {
                tt_move  = tte->move ();
                tt_value = value_of_tt (tte->value (), (ss)->ply);
                tt_depth = tte->depth ();
                tt_bound = tte->bound ();
                if (!InCheck) best_value = tte->eval ();
            }

            // Decide whether or not to include checks, this fixes also the type of
            // TT entry depth that are going to use. Note that in search_quien use
            // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
            Depth qs_depth = (InCheck || depth >= DEPTH_QS_CHECKS) ?
                               DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;

            CheckInfo cc
                ,    *ci = NULL;

            if (  tte != NULL
               && tt_depth >= qs_depth
               && tt_value != VALUE_NONE // Only in case of TT access race
               && (         PVNode ? tt_bound == BND_EXACT :
                  tt_value >= beta ? tt_bound &  BND_LOWER :
                                     tt_bound &  BND_UPPER
                  )
               )
            {
                (ss)->current_move = tt_move; // Can be MOVE_NONE
                return tt_value;
            }

            Value futility_base;

            // Evaluate the position statically
            if (InCheck)
            {
                (ss)->static_eval = best_value;
                futility_base = best_value = -VALUE_INFINITE;
            }
            else
            {
                if (tte != NULL)
                {
                    // Never assume anything on values stored in TT
                    if (VALUE_NONE == best_value) best_value = evaluate (pos);
                    (ss)->static_eval = best_value;

                    // Can tt_value be used as a better position evaluation?
                    if (VALUE_NONE != tt_value)
                    {
                        if (tt_bound & (tt_value > best_value ? BND_LOWER : BND_UPPER))
                        {
                            best_value = tt_value;
                        }
                    }
                }
                else
                {
                    (ss)->static_eval = best_value = 
                        (ss-1)->current_move != MOVE_NULL ? evaluate (pos) : -(ss-1)->static_eval + 2*TempoBonus;
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
                                BND_LOWER,
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
            if (ci == NULL)
            {
                cc = CheckInfo (pos);
                ci = &cc;
            }
            Move move;
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
                       && futility_base > -VALUE_KNOWN_WIN
                       && move != tt_move
                       && !pos.advanced_pawn_push (move)
                       )
                    {
                        ASSERT (mtype (move) != ENPASSANT); // Due to !pos.advanced_pawn_push()

                        Value futility_value = futility_base + PieceValue[EG][ptype (pos[dst_sq (move)])];

                        if (futility_value < beta)
                        {
                            best_value = max (futility_value, best_value);
                            continue;
                        }

                        // Prune moves with negative or equal SEE and also moves with positive
                        // SEE where capturing piece loses a tempo and SEE < beta - futility_base.
                        if (  futility_base < beta
                           && pos.see (move) <= VALUE_ZERO
                           //&& depth < DEPTH_ZERO        // TODO::
                           )
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
                          || (  best_value > VALUE_MATED_IN_MAX_PLY
                             && !pos.can_castle (pos.active ())
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
                if (!pos.legal (move, ci->pinneds)) continue;

                (ss)->current_move = move;

                // Make and search the move
                pos.do_move (move, si, gives_check ? ci : NULL);

                Value value = gives_check ?
                    -search_quien<NT, true > (pos, ss+1, -beta, -alpha, depth-1*i16(ONE_MOVE)) :
                    -search_quien<NT, false> (pos, ss+1, -beta, -alpha, depth-1*i16(ONE_MOVE));

                pos.undo_move ();

                ASSERT (-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Check for new best move
                if (best_value < value)
                {
                    best_value = value;

                    if (alpha < value)
                    {
                        best_move = move;

                        if (value >= beta)  // Fail high
                        {
                            TT.store (
                                posi_key,
                                best_move,
                                qs_depth,
                                BND_LOWER,
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
            if (InCheck)
            {
                if (best_value == -VALUE_INFINITE)
                {
                    // Plies to mate from the root
                    return mated_in ((ss)->ply);
                }
            }

            TT.store (
                posi_key,
                best_move,
                qs_depth,
                PVNode && old_alpha < best_value ? BND_EXACT : BND_UPPER,
                value_to_tt (best_value, (ss)->ply),
                (ss)->static_eval);

            ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        template<NodeT NT, bool SPNode, bool MoveNull>
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
            const TTEntry *tte = NULL;
            Move  tt_move     = MOVE_NONE;
            Value tt_value    = VALUE_NONE;
            Depth tt_depth    = DEPTH_NONE;
            Bound tt_bound    = BND_NONE;
            Value static_eval = VALUE_NONE
                , best_value  = -VALUE_INFINITE;

            // Step 1. Initialize node
            bool in_check = pos.checkers ();
            bool singular_ext_node = false;

            SplitPoint *splitpoint;
            Move  move
                , excluded_move = MOVE_NONE
                , best_move     = MOVE_NONE;

            CheckInfo cc
                ,    *ci = NULL;

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
                (ss)->current_move = (ss+1)->excluded_move = MOVE_NONE;
                fill ((ss+2)->killer_moves, (ss+2)->killer_moves + sizeof ((ss+2)->killer_moves) / sizeof (*((ss+2)->killer_moves)), MOVE_NONE);

                // Used to send 'seldepth' info to GUI
                if (PVNode)
                {
                    Threadpool.max_ply = max ((ss)->ply, Threadpool.max_ply);
                }

                if (!RootNode)
                {
                    // Step 2. Check end condition
                    
                    // Check for aborted search
                    if (Signals.force_stop)   return VALUE_ZERO;
                    // Check for immediate draw
                    if (pos.draw ())          return DrawValue[pos.active ()];
                    // Check for maximum ply reached
                    if ((ss)->ply > MaxDepth) return in_check ? DrawValue[pos.active ()] : evaluate (pos);

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
                excluded_move = (ss)->excluded_move;
                posi_key = excluded_move == MOVE_NONE ? pos.posi_key () : pos.posi_exc_key ();

                tte      = TT.retrieve (posi_key);
                (ss)->tt_move =
                tt_move  = RootNode ? RootMoves[PVIndex].pv[0] :
                           tte != NULL ? tte->move () : MOVE_NONE;
                if (tte != NULL)
                {
                    tt_value = value_of_tt (tte->value (), (ss)->ply);
                    tt_depth = tte->depth ();
                    tt_bound = tte->bound ();
                    if (!in_check) static_eval = tte->eval ();
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
                       && (         PVNode ? tt_bound == BND_EXACT :
                          tt_value >= beta ? tt_bound &  BND_LOWER :
                                             tt_bound &  BND_UPPER
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
                    (ss)->static_eval = static_eval;
                }
                else
                {
                    if (tte != NULL)
                    {
                        // Never assume anything on values stored in TT
                        if (VALUE_NONE == static_eval) static_eval = evaluate (pos);
                        (ss)->static_eval = static_eval;

                        // Can tt_value be used as a better position evaluation?
                        if (VALUE_NONE != tt_value)
                        {
                            if (tt_bound & (tt_value > static_eval ? BND_LOWER : BND_UPPER))
                            {
                                static_eval = tt_value;
                            }
                        }
                    }
                    else
                    {
                        (ss)->static_eval = static_eval =
                            (ss-1)->current_move != MOVE_NULL ? evaluate (pos) : -(ss-1)->static_eval + 2*TempoBonus;

                        TT.store (
                            posi_key,
                            MOVE_NONE,
                            DEPTH_NONE,
                            BND_NONE,
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
                           && abs (beta) < VALUE_MATES_IN_MAX_PLY   // TODO::
                           && !pos.pawn_on_7thR (pos.active ())
                           )
                        {
                            if (  depth <= 1*i16(ONE_MOVE)
                               && static_eval + RazorMargins[3*i16(ONE_MOVE)] <= alpha
                               )
                            {
                                return search_quien<NonPV, false> (pos, ss, alpha, beta, DEPTH_ZERO);
                            }

                            Value ralpha = max (alpha - RazorMargins[depth], -VALUE_INFINITE);
                            //ASSERT (ralpha >= -VALUE_INFINITE);

                            Value ver_value = search_quien<NonPV, false> (pos, ss, ralpha, ralpha+1, DEPTH_ZERO);

                            if (ver_value <= ralpha) return ver_value;
                        }

                        // Step 7,8,9.
                        if (MoveNull)
                        {
                            ASSERT ((ss-1)->current_move != MOVE_NONE);
                            ASSERT ((ss-1)->current_move != MOVE_NULL);

                            StateInfo si;

                            // Step 7,8.
                            if (pos.non_pawn_material (pos.active ()) > VALUE_ZERO)
                            {
                                // Step 7. Futility pruning: child node
                                // Betting that the opponent doesn't have a move that will reduce
                                // the score by more than FutilityMargins[depth] if do a null move.
                                if (  depth < FutilityMarginDepth   // TODO:: + 1*i16(ONE_MOVE)
                                   && abs (beta) < VALUE_MATES_IN_MAX_PLY
                                   && abs (static_eval) < VALUE_KNOWN_WIN
                                   )
                                {
                                    Value stand_pat = static_eval - FutilityMargins[depth];

                                    if (stand_pat >= beta) return stand_pat;
                                }

                                // Step 8. Null move search with verification search
                                if (  depth >= 2*i16(ONE_MOVE)
                                   && static_eval >= beta
                                   )
                                {
                                    (ss)->current_move = MOVE_NULL;

                                    // Null move dynamic (variable) reduction based on depth and value
                                    Depth rdepth = depth -
                                                 ( 3*i16(ONE_MOVE)
                                                 + depth/4
                                                 + (abs (beta) < VALUE_KNOWN_WIN ? i32(static_eval - beta)/i32(VALUE_MG_PAWN)*ONE_MOVE : DEPTH_ZERO));

                                    // Do null move
                                    pos.do_null_move (si);

                                    // Null window (alpha, beta) = (beta-1, beta):
                                    Value null_value = (rdepth < 1*i16(ONE_MOVE)) ?
                                        -search_quien<NonPV, false> (pos, ss+1, -beta, -beta+1, DEPTH_ZERO) :
                                        -search_depth<NonPV, false, false> (pos, ss+1, -beta, -beta+1, rdepth, !cut_node);

                                    // Undo null move
                                    pos.undo_null_move ();

                                    if (null_value >= beta)
                                    {
                                        // Do not return unproven mate scores
                                        if (null_value >= VALUE_MATES_IN_MAX_PLY)
                                        {
                                            null_value = beta;
                                        }
                                        // Don't do zugzwang verification search at low depths
                                        if (  depth < 12*i16(ONE_MOVE)   // TODO:: 12*2
                                           && abs (beta) < VALUE_KNOWN_WIN
                                           )
                                        {
                                            return null_value;
                                        }

                                        // Do verification search at high depths
                                        Value veri_value = (rdepth < 1*i16(ONE_MOVE)) ?
                                            search_quien<NonPV, false> (pos, ss, beta-1, beta, DEPTH_ZERO) :
                                            search_depth<NonPV, false, false> (pos, ss, beta-1, beta, rdepth, false);

                                        if (veri_value >= beta) return null_value;
                                    }
                                }
                            }

                            // Step 9. Prob-Cut
                            // If have a very good capture (i.e. SEE > see[captured_piece_type])
                            // and a reduced search returns a value much above beta,
                            // can (almost) safely prune the previous move.
                            if (  depth >= RazorDepth + 1*i16(ONE_MOVE)
                               && abs (beta) < VALUE_MATES_IN_MAX_PLY
                               )
                            {
                                ASSERT (excluded_move == MOVE_NONE);

                                Depth rdepth = depth - RazorDepth;
                                Value rbeta  = min (beta + VALUE_MG_PAWN, +VALUE_INFINITE);
                                //ASSERT (rdepth >= 1*i16(ONE_MOVE));
                                //ASSERT (rbeta <= +VALUE_INFINITE);

                                // Initialize a MovePicker object for the current position,
                                // and prepare to search the moves.
                                MovePicker mp (pos, HistoryStatistics, tt_move, pos.capture_type ());
                                if (ci == NULL)
                                {
                                    cc = CheckInfo (pos);
                                    ci = &cc;
                                }
                                while ((move = mp.next_move<false> ()) != MOVE_NONE)
                                {
                                    if (!pos.legal (move, ci->pinneds)) continue;

                                    (ss)->current_move = move;
                                    pos.do_move (move, si, pos.gives_check (move, *ci) ? ci : NULL);
                                    Value value = -search_depth<NonPV, false, true> (pos, ss+1, -rbeta, -rbeta+1, rdepth, !cut_node);
                                    pos.undo_move ();

                                    if (value >= rbeta) return value;
                                }
                            }

                        }
                    }

                    // Step 10. Internal iterative deepening (skipped when in check)
                    if (  tt_move == MOVE_NONE
                       && depth >= (PVNode ? 5*i16(ONE_MOVE) : 8*i16(ONE_MOVE))     // IID Activation Depth
                       && (PVNode || ((ss)->static_eval + VALUE_EG_PAWN >= beta))   // IID Margin
                       )
                    {
                        Depth iid_depth = depth - (PVNode ? 2*i16(ONE_MOVE) : 2*i16(ONE_MOVE) + depth/4); // IID Reduced Depth

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
                    && depth >= (PVNode ? 6*i16(ONE_MOVE) : 8*i16(ONE_MOVE))    // TODO::
                    && tt_move != MOVE_NONE
                    && excluded_move == MOVE_NONE // Recursive singular search is not allowed
                    && abs (beta)     < VALUE_KNOWN_WIN
                    && abs (tt_value) < VALUE_KNOWN_WIN
                    && tt_bound & BND_LOWER
                    && tt_depth >= depth-3*i16(ONE_MOVE);

            }

            // Splitpoint start
            // When in check and at SPNode search starts from here

            Value value = best_value;

            bool improving =
                   ((ss-2)->static_eval == VALUE_NONE)
                || ((ss-0)->static_eval == VALUE_NONE)
                || ((ss-0)->static_eval >= (ss-2)->static_eval)
                || (  (ss-1)->current_move != MOVE_NULL
                   && (ss-0)->static_eval != VALUE_NONE
                   && (ss-0)->static_eval > -(ss-1)->static_eval
                   );

            Thread *thread  = pos.thread ();
            point time;

            if (RootNode)
            {
                if (Threadpool.main () == thread)
                {
                    time = now () - SearchTime;
                    if (time > InfoInterval)
                    {
                        sync_cout
                            << "info"
                            << " depth " << u16(depth)/i16(ONE_MOVE)
                            << " time "  << time
                            << sync_endl;
                    }
                }
            }

            Move *counter_moves  = _ok ((ss-1)->current_move) ?  CounterMoveStats.moves (pos, dst_sq ((ss-1)->current_move)) : NULL;
            Move *followup_moves = _ok ((ss-2)->current_move) ? FollowupMoveStats.moves (pos, dst_sq ((ss-2)->current_move)) : NULL;

            MovePicker mp (pos, HistoryStatistics, tt_move, depth, counter_moves, followup_moves, ss);
            StateInfo si;
            if (ci == NULL)
            {
                cc = CheckInfo (pos);
                ci = &cc;
            }

            u08   legals = 0
                , quiets = 0;

            Move quiet_moves[MaxQuiets] = { MOVE_NONE };

            // Step 11. Loop through moves
            // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
            while ((move = mp.next_move<SPNode> ()) != MOVE_NONE)
            {
                ASSERT (_ok (move));

                if (move == excluded_move) continue;

                // At root obey the "searchmoves" option and skip moves not listed in
                // RootMove list, as a consequence any illegal move is also skipped.
                // In MultiPV mode also skip PV moves which have been already searched.
                if (RootNode && !count (RootMoves.begin () + PVIndex, RootMoves.end (), move)) continue;

                bool move_legal = RootNode || pos.legal (move, ci->pinneds);

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

                //u64 nodes = U64 (0);

                if (RootNode)
                {
                    //nodes = pos.game_nodes ();

                    Signals.root_1stmove = (1 == legals);

                    if (Threadpool.main () == thread)
                    {
                        time = now () - SearchTime;
                        if (time > InfoInterval)
                        {
                            sync_cout
                                << "info"
                                //<< " depth "          << u16(depth)/i16(ONE_MOVE)
                                << " currmovenumber " << setw (2) << u16(legals + PVIndex)
                                << " currmove "       << move_to_can (move, pos.chess960 ())
                                << " time "           << time
                                << sync_endl;
                        }
                    }
                }

                // Step 12. Decide the new search depth
                Depth ext = DEPTH_ZERO;

                bool capture_or_promotion = pos.capture_or_promotion (move);
                
                bool gives_check = pos.gives_check (move, *ci);
                
                MoveT mt = mtype (move);

                bool dangerous =
                       gives_check
                    || NORMAL != mt
                    || pos.advanced_pawn_push (move);

                // Step 13. Extend the move which seems dangerous like ...checks etc.
                if (gives_check && pos.see_sign (move) >= VALUE_ZERO)
                {
                    ext = 1*ONE_MOVE;
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
                    //ASSERT (tt_value != VALUE_NONE);
                    Value rbeta = tt_value - i32(depth); // TODO::

                    (ss)->excluded_move = move;
                    value = search_depth<NonPV, false, false> (pos, ss, rbeta-1, rbeta, depth/2, cut_node);
                    (ss)->excluded_move = MOVE_NONE;

                    if (value < rbeta) ext = 1*ONE_MOVE;
                }

                // Update the current move (this must be done after singular extension search)
                Depth new_depth = depth - 1*i16(ONE_MOVE) + ext;

                // Step 14. Pruning at shallow depth (exclude PV nodes)
                if (!PVNode && !MateSearch)
                {
                    if (  !capture_or_promotion
                       && !in_check
                       && !dangerous
                       && move != tt_move // Already implicit in the next condition // TODO::
                       && best_value > VALUE_MATED_IN_MAX_PLY
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
                        Depth predicted_depth = new_depth - reduction<PVNode> (improving, depth, legals);

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
                                    // Max value
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

                if (!SPNode)
                {
                    // Check for legality just before making the move
                    if (!RootNode && !move_legal)
                    {
                        --legals;
                        continue;
                    }

                    // Save the quiet move
                    if (  !capture_or_promotion
                       && quiets < MaxQuiets
                       )
                    {
                        quiet_moves[quiets++] = move;
                    }
                }

                bool move_pv = PVNode && (1 == legals);
                (ss)->current_move = move;
                
                // Step 15. Make the move
                pos.do_move (move, si, gives_check ? ci : NULL);

                // Step 16, 17.
                if (!move_pv)
                {
                    bool full_depth_search = true;
                    // Step 16. Reduced depth search (LMR).
                    // If the move fails high will be re-searched at full depth.
                    if (  depth >= 3*i16(ONE_MOVE)
                       && move != tt_move
                       && move != (ss)->killer_moves[0]
                       && move != (ss)->killer_moves[1]
                       //&& !dangerous
                       && !capture_or_promotion
                       )
                    {
                        Depth reduction_depth = reduction<PVNode> (improving, depth, legals);

                        if (!PVNode && cut_node)
                        {
                            reduction_depth += 1*ONE_MOVE;
                        }
                        else
                        if (HistoryStatistics.value (pos[dst_sq (move)], dst_sq (move)) < VALUE_ZERO)
                        {
                            reduction_depth += 1*ONE_PLY;
                        }

                        if (  reduction_depth > DEPTH_ZERO
                           && counter_moves && (move == counter_moves[0] || move == counter_moves[1])
                           )
                        {
                            reduction_depth = max (reduction_depth - 1*i16(ONE_MOVE), DEPTH_ZERO);
                        }

                        // Decrease reduction for moves that escape a capture
                        if (  reduction_depth > DEPTH_ZERO
                           && mt == NORMAL
                           && ptype (pos[dst_sq (move)]) != PAWN
                           )
                        {
                            // Reverse move
                            if (pos.see (mk_move<NORMAL> (dst_sq (move), org_sq (move))) < VALUE_ZERO)
                            {
                                reduction_depth = max (reduction_depth - 1*i16(ONE_MOVE), DEPTH_ZERO);
                            }
                        }

                        if (SPNode) alpha = splitpoint->alpha;
                        Depth reduced_depth = max (new_depth - reduction_depth, 1*ONE_MOVE);
                        // Search with reduced depth
                        value = -search_depth<NonPV, false, true> (pos, ss+1, -alpha-1, -alpha, reduced_depth, true);
                        // Multi Re-search
                        for (i08 i = 0; i <= 2; ++i)
                        {
                            i32 t = 1 << i;
                            // Re-search at intermediate depth if reduction is very high
                            if (alpha < value && reduction_depth >= 4*t*i16(ONE_MOVE))
                            {
                                if (SPNode) alpha = splitpoint->alpha;
                                reduced_depth = max (new_depth - reduction_depth/(1*t*i16(ONE_MOVE)), 1*ONE_MOVE);
                                // Search with reduced depth
                                value = -search_depth<NonPV, false, true> (pos, ss+1, -alpha-1, -alpha, reduced_depth, true);
                            }
                            else break;
                        }

                        full_depth_search = alpha < value && reduced_depth < new_depth;
                    }

                    // Step 17. Full depth search, when LMR is skipped or fails high
                    if (full_depth_search)
                    {
                        if (SPNode) alpha = splitpoint->alpha;

                        value =
                            new_depth < 1*i16(ONE_MOVE) ?
                                gives_check ?
                                    -search_quien<NonPV, true > (pos, ss+1, -alpha-1, -alpha, DEPTH_ZERO) :
                                    -search_quien<NonPV, false> (pos, ss+1, -alpha-1, -alpha, DEPTH_ZERO) :
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
                            new_depth < 1*i16(ONE_MOVE) ?
                                gives_check ?
                                    -search_quien<PV, true > (pos, ss+1, -beta, -alpha, DEPTH_ZERO) :
                                    -search_quien<PV, false> (pos, ss+1, -beta, -alpha, DEPTH_ZERO) :
                                -search_depth<PV, false, true> (pos, ss+1, -beta, -alpha, new_depth, false);
                    }
                }

                // Step 18. Undo move
                pos.undo_move ();

                ASSERT (-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Step 19. Check for new best move
                if (SPNode)
                {
                    splitpoint->mutex.lock ();
                    best_value = splitpoint->best_value;
                    alpha      = splitpoint->alpha;
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
                        rm.value[0] = value;
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
                        rm.value[0] = -VALUE_INFINITE;
                    }
                }

                if (best_value < value)
                {
                    best_value = (SPNode) ? splitpoint->best_value = value : value;

                    if (alpha < value)
                    {
                        best_move = (SPNode) ? splitpoint->best_move = move : move;

                        if (value >= beta)  // Fail high
                        {
                            if (SPNode) splitpoint->cut_off = true;

                            break;
                        }

                        ASSERT (value < beta);
                        // Update alpha here! always alpha < beta
                        if (PVNode) alpha = (SPNode) ? splitpoint->alpha = value : value;
                    }
                }

                // Step 20. Check for splitting the search (at non-splitpoint node)
                if (!SPNode)
                {
                    if (  Threadpool.split_depth <= depth
                       && Threadpool.size () > 1
                       && thread->splitpoint_threads < MaxSplitPointThreads
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

            // Step 21. Check for checkmate and stalemate
            if (!SPNode)
            {
                // All possible moves have been searched and if there are no legal moves,
                // it must be mate or stalemate, so return value accordingly.
                // If in a singular extension search then return a fail low score (alpha).
                // If we have pruned all the moves without searching return a fail-low score (alpha).
                if (best_value == -VALUE_INFINITE || legals == 0)
                {
                    best_value = 
                        excluded_move != MOVE_NONE ? alpha :
                        in_check ? mated_in ((ss)->ply) :
                        DrawValue[pos.active ()];
                }
                else
                // Quiet best move: Update history, killer, counter & followup moves
                if (  best_value >= beta
                   && !in_check
                   && best_move != MOVE_NONE
                   && !pos.capture_or_promotion (best_move)
                   )
                {
                    update_stats (pos, ss, best_move, depth, quiet_moves, quiets);
                }

                TT.store (
                    posi_key,
                    best_move,
                    depth,
                    best_value >= beta ? BND_LOWER : PVNode && best_move != MOVE_NONE ? BND_EXACT : BND_UPPER,
                    value_to_tt (best_value, (ss)->ply),
                    (ss)->static_eval);
            }

            ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        // search_iter_deepening() is the main iterative deepening search function.
        // It calls search() repeatedly with increasing depth until:
        // - the allocated thinking time has been consumed,
        // - the user stops the search,
        // - the maximum search depth is reached.
        // Time management; with iterative deepining enabled you can specify how long
        // you want the computer to think rather than how deep you want it to think. 
        inline void search_iter_deepening ()
        {
            Stack stack[MaxDepth6]
                , *ss = stack+2; // To allow referencing (ss-2)

            memset (ss-2, 0x00, 5*sizeof (*ss));

            TT.new_gen ();

            GainStatistics.clear ();
            HistoryStatistics.clear ();
            CounterMoveStats.clear ();
            FollowupMoveStats.clear ();
            
            PieceT cap_pt  = RootPos.capture_type ();
            Move last_move = RootPos.last_move ();

            u08 level = u08(i32(Options["Skill Level"]));
            Skill skill (level);

            // Do have to play with skill handicap?
            // In this case enable MultiPV search by skill candidates size
            // that will use behind the scenes to retrieve a set of possible moves.
            u08 MultiPV = min (max (u08(i32(Options["MultiPV"])), skill.candidates_size ()), RootSize);
            PVLimit = MultiPV;

            Value best_value = VALUE_ZERO
                , bound [2]  = { -VALUE_INFINITE, +VALUE_INFINITE }
                , window[2]  = { VALUE_ZERO, VALUE_ZERO };

            i16 dep = DEPTH_ZERO;

            point iteration_time;

            // Iterative deepening loop until target depth reached
            while (++dep <= MaxDepth && (!Limits.depth || dep <= Limits.depth))
            {
                // Requested to stop?
                if (Signals.force_stop) break;
                
                // Age out PV variability metric
                RootMoves.best_move_change *= 0.5f;

                // Save last iteration's scores before first PV line is searched and
                // all the move scores but the (new) PV are set to -VALUE_INFINITE.
                for (u08 i = 0; i < RootSize; ++i)
                {
                    RootMoves[i].value[1] = RootMoves[i].value[0];
                }
                
                const bool aspiration = dep > 2*i16(ONE_MOVE);
                
                //PVLimit = dep <= 5*i16(ONE_MOVE) ? min (max (MultiPV, MinSkillMultiPV), RootSize) : MultiPV;
                // MultiPV loop. Perform a full root search for each PV line
                for (PVIndex = 0; PVIndex < PVLimit; ++PVIndex)
                {
                    // Requested to stop?
                    if (Signals.force_stop) break;

                    // Reset Aspiration window starting size
                    if (aspiration)
                    {
                        if (abs (RootMoves[PVIndex].value[1]) < VALUE_KNOWN_WIN)
                        {
                            window[0] =
                            window[1] =
                                Value(16);
                                //Value(dep < 24*i16(ONE_MOVE) ? 18 - dep/8 : 12);
                                //Value(dep < 12*i16(ONE_MOVE) ? 20 - dep/4 : 14); // Decreasing window

                            bound [0] = max (RootMoves[PVIndex].value[1] - window[0], -VALUE_INFINITE);
                            bound [1] = min (RootMoves[PVIndex].value[1] + window[1], +VALUE_INFINITE);
                        }
                        else
                        {
                            if (RootMoves[PVIndex].value[1] <= -VALUE_KNOWN_WIN) { bound [0] = -VALUE_INFINITE; bound [1] = Value(16); };
                            if (RootMoves[PVIndex].value[1] >= +VALUE_KNOWN_WIN) { bound [1] = +VALUE_INFINITE; bound [0] = Value(16); };
                        }
                    }

                    // Start with a small aspiration window and, in case of fail high/low,
                    // research with bigger window until not failing high/low anymore.
                    do
                    {
                        best_value = search_depth<Root, false, true> (RootPos, ss, bound[0], bound[1], i32(dep)*ONE_MOVE, false);

                        // Bring to front the best move. It is critical that sorting is
                        // done with a stable algorithm because all the values but the first
                        // and eventually the new best one are set to -VALUE_INFINITE and
                        // want to keep the same order for all the moves but the new PV
                        // that goes to the front. Note that in case of MultiPV search
                        // the already searched PV lines are preserved.
                        //RootMoves.sort_end (PVIndex);
                        std::stable_sort (RootMoves.begin () + PVIndex, RootMoves.end ());

                        // Write PV back to transposition table in case the relevant
                        // entries have been overwritten during the search.
                        for (i08 i = PVIndex; i >= 0; --i)
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
                        if (  iteration_time > InfoInterval
                           && (bound[0] >= best_value || best_value >= bound[1])
                           )
                        {
                            sync_cout << info_multipv (RootPos, dep, bound[0], bound[1], iteration_time) << sync_endl;
                        }

                        // In case of failing low/high increase aspiration window and
                        // re-search, otherwise exit the loop.
                        if (best_value <= bound[0])
                        {
                            window[0] *= 1.365f;
                            bound [0] = max (best_value - window[0], -VALUE_INFINITE);
                            if (window[1] > 1) window[1] *= 0.925f;
                            bound [1] = min (best_value + window[1], +VALUE_INFINITE);

                            Signals.root_failedlow = true;
                            Signals.ponderhit_stop = false;
                        }
                        else
                        if (best_value >= bound[1])
                        {
                            window[1] *= 1.365f;
                            bound [1] = min (best_value + window[1], +VALUE_INFINITE);
                            if (window[0] > 1) window[0] *= 0.925f;
                            bound [0] = max (best_value - window[0], -VALUE_INFINITE);
                        }
                        else
                        {
                            break;
                        }

                        ASSERT (-VALUE_INFINITE <= bound[0] && bound[0] < bound[1] && bound[1] <= +VALUE_INFINITE);
                    }
                    while (true); //(bound[0] < bound[1]);

                    // Sort the PV lines searched so far and update the GUI
                    //RootMoves.sort_beg (PVIndex + 1);
                    std::stable_sort (RootMoves.begin (), RootMoves.begin () + PVIndex + 1);

                    if (  PVIndex + 1 == PVLimit
                       || iteration_time > InfoInterval
                       )
                    {
                        sync_cout << info_multipv (RootPos, dep, bound[0], bound[1], iteration_time) << sync_endl;
                    }
                }

                if (ContemptValue > 0)
                {
                    i16 valued_contempt = i16(RootMoves[0].value[0])/ContemptValue;
                    DrawValue[ RootColor] = BaseContempt[ RootColor] - Value(valued_contempt);
                    DrawValue[~RootColor] = BaseContempt[~RootColor] + Value(valued_contempt);
                }

                // If skill levels are enabled and time is up, pick a sub-optimal best move
                if (skill.candidates_size () && skill.time_to_pick (dep))
                {
                    Move m = skill.pick_move ();
                    if (MOVE_NONE != m)
                    {
                        swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), m));
                    }
                }

                iteration_time = now () - SearchTime;

                if (!SearchLog.empty ())
                {
                    LogFile logfile (SearchLog);
                    logfile << pretty_pv (RootPos, dep, RootMoves[0].value[0], iteration_time, &RootMoves[0].pv[0]) << endl;
                }
                
                // Requested to stop?
                if (Signals.force_stop) break;

                // Stop the search early:
                bool stop = false;

                // Do have time for the next iteration? Can stop searching now?
                if (!Signals.ponderhit_stop && Limits.use_timemanager ())
                {
                    // Time adjustments
                    if (aspiration && MultiPV == 1)
                    {
                        // Take in account some extra time if the best move has changed
                        TimeMgr.instability (RootMoves.best_move_change);

                        Move root_move = RootMoves[0].pv[0];
                        // Take less time for recaptures if good
                        bool recapture_good = false;
                        if (  RootMoves.best_move_change < 0.05f
                           && cap_pt != NONE
                           && last_move != MOVE_NONE
                           && dst_sq (last_move) == dst_sq (root_move)
                           )
                        {
                            PieceT org_pt = ptype (RootPos[org_sq (root_move)]);
                            PieceT dst_pt = ptype (RootPos[dst_sq (root_move)]);
                            if (org_pt == KING) org_pt = QUEN;

                            recapture_good = dst_pt != NONE // && dst_pt != cap_pt // && org_pt != KING
                                && (  PieceValue[MG][dst_pt] - PieceValue[MG][org_pt] > VALUE_MG_BSHP - VALUE_MG_NIHT
                                   || abs (PieceValue[MG][org_pt] - PieceValue[MG][cap_pt]) <= VALUE_MG_BSHP - VALUE_MG_NIHT
                                   //|| PieceValue[MG][dst_pt] - PieceValue[MG][cap_pt] > VALUE_MG_BSHP - VALUE_MG_NIHT // King capturing must be check further here
                                   );
                        }
                        TimeMgr.recapture (recapture_good);
                    }

                    // If there is only one legal move available or 
                    // If all of the available time has been used.
                    if (  RootSize == 1
                       || iteration_time > TimeMgr.available_time ()
                       )
                    {
                        stop = true;
                    }
                }
                else
                {
                    // Have found a "mate in <x>"?
                    if (  MateSearch
                       && best_value >= VALUE_MATES_IN_MAX_PLY
                       && VALUE_MATE - best_value <= Limits.mate*i16(ONE_MOVE)
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

        }

        // perft<>() is our utility to verify move generation. All the leaf nodes
        // up to the given depth are generated and counted and the sum returned.
        template<bool RootNode>
        u64 perft (Position &pos, Depth depth)
        {
            u64 leaf_nodes = U64 (0);

            StateInfo si;
            CheckInfo ci (pos);
            for (MoveList<LEGAL> ms (pos); *ms != MOVE_NONE; ++ms)
            {
                u64 inter_nodes;
                if (RootNode && depth <= 1*i16(ONE_MOVE))
                {
                    inter_nodes = 1;
                }
                else
                {
                    Move m = *ms;
                    pos.do_move (m, si, pos.gives_check (m, ci) ? &ci : NULL);
                    inter_nodes = depth <= 2*i16(ONE_MOVE) ? MoveList<LEGAL>(pos).size () : perft<false> (pos, depth-1*i16(ONE_MOVE));
                    pos.undo_move ();
                }

                if (RootNode)
                {
                    sync_cout <<  left << setw ( 7) << setfill (' ') <<
                              //move_to_can (*ms, pos.chess960 ())
                              move_to_san (*ms, pos)
                              << right << setw (12) << setfill ('.') << inter_nodes << sync_endl;
                }

                leaf_nodes += inter_nodes;
            }
            return leaf_nodes;
        }

    } // namespace

    LimitsT             Limits;
    SignalsT volatile   Signals;

    RootMoveList        RootMoves;
    Position            RootPos;
    StateInfoStackPtr   SetupStates;

    point               SearchTime;

    // initialize the PRNG only once
    PolyglotBook        Book;

    // RootMove::extract_pv_from_tt() builds a PV by adding moves from the TT table.
    // Consider also failing high nodes and not only EXACT nodes so to
    // allow to always have a ponder move even when fail high at root node.
    // This results in a long PV to print that is important for position analysis.
    void RootMove::extract_pv_from_tt (Position &pos)
    {
        StateInfo states[MaxDepth6]
                , *si = states;

        i08 ply = 0; // Ply starts from 1, we need to start from 0
        Move m = pv[ply];
        pv.clear ();
        Value expected_value = value[0];
        const TTEntry *tte;
        do
        {
            ASSERT (MoveList<LEGAL> (pos).contains (m));

            pv.push_back (m);
            pos.do_move (m, *si++);
            ++ply;
            expected_value = -expected_value;
            tte = TT.retrieve (pos.posi_key ());
        }
        while (  tte != NULL
              && expected_value == value_of_tt (tte->value (), ply+1)
              && (m = tte->move ()) != MOVE_NONE // Local copy, TT could change
              && pos.pseudo_legal (m)
              && pos.legal (m)
              && ply < MaxDepth
              && (!pos.draw () || ply < 2));
        
        do
        {
            pos.undo_move ();
            --ply;
        }
        while (ply);

        pv.push_back (MOVE_NONE); // Must be zero-terminating
    }
    // RootMove::insert_pv_in_tt() is called at the end of a search iteration, and
    // inserts the PV back into the TT. This makes sure the old PV moves are searched
    // first, even if the old TT entries have been overwritten.
    void RootMove::insert_pv_into_tt (Position &pos)
    {
        StateInfo states[MaxDepth6]
                , *si = states;
        
        i08 ply = 0; // Ply starts from 1, we need to start from 0
        Move m = pv[ply];
        const TTEntry *tte;
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
                    BND_NONE,
                    VALUE_NONE,
                    VALUE_NONE); // evaluate (pos) ->To evaluate again
            }

            pos.do_move (m, *si++);
            m = pv[++ply];
        }
        while (MOVE_NONE != m);

        do
        {
            pos.undo_move ();
            --ply;
        }
        while (ply);
    }

    string RootMove::info_pv (const Position &pos) const
    {
        stringstream ss;
        for (u08 i = 0; pv[i] != MOVE_NONE; ++i)
        {
            ss << " " << move_to_can (pv[i], pos.chess960 ());
        }
        return ss.str ();
    }

    void RootMoveList::initialize (const Position &pos, const vector<Move> &root_moves)
    {
        best_move_change = 0.0f;
        clear ();
        for (MoveList<LEGAL> itr (pos); *itr != MOVE_NONE; ++itr)
        {
            Move m = *itr;
            if (  root_moves.empty ()
               || count (root_moves.begin (), root_moves.end (), m)
               )
            {
                push_back (RootMove (m));
            }
        }
    }

    // ------------------------------------

    u64 perft (Position &pos, Depth depth)
    {
        return perft<true> (pos, depth);
    }

    // Main searching starts from here
    void think ()
    {
        RootColor = RootPos.active ();
        RootPly   = RootPos.game_ply ();
        RootSize  = RootMoves.size ();

        SearchLog = string(Options["Search Log"]);
        if (!SearchLog.empty ())
        {
            trim (SearchLog);
            if (!SearchLog.empty ())
            {
                convert_path (SearchLog);
                remove_extension (SearchLog);
                if (!SearchLog.empty ()) SearchLog += ".txt";
            }
            if (SearchLog.empty ()) SearchLog = "SearchLog.txt";
        }
        if (!SearchLog.empty ())
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
                << " Depth Score    Time   Nodes  PV\n"
                << "-----------------------------------------------------------"
                << endl;
        }

        MateSearch = bool(Limits.mate);

        if (RootSize)
        {
            string book_fn = string(Options["Opening Book"]);
            if (!book_fn.empty () && !Limits.infinite && !MateSearch)
            {
                trim (book_fn);
                convert_path (book_fn);

                if (!Book.is_open () && !book_fn.empty ())
                {
                    Book.open (book_fn, ios_base::in|ios_base::binary);
                }
                if (Book.is_open ())
                {
                    Move book_move = Book.probe_move (RootPos, bool(Options["Best Book Move"]));
                    if (  book_move != MOVE_NONE
                       && count (RootMoves.begin (), RootMoves.end (), book_move)
                       )
                    {
                        swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), book_move));
                        goto finish;
                    }
                }
            }

            TimeMgr.initialize (Limits.gameclock[RootColor], Limits.movestogo, RootPly);

            i16 fixed_contempt = i16(i32(Options["Fixed Contempt"]));

            i16 timed_contempt = 0;
            i16 diff_time = 0;
            u16 contempt_time = 0;
            if (  (contempt_time = u16(i32(Options["Timed Contempt (sec)"]))) > 0
               && (diff_time = i16(Limits.gameclock[RootColor].time - Limits.gameclock[~RootColor].time)/MilliSec) != 0
               //&& contempt_time <= abs (diff_time)
               )
            {
                timed_contempt = diff_time / contempt_time;
            }

            Value contempt = Value(cp_to_value (float(fixed_contempt + timed_contempt) / 0x64)); // 100
            DrawValue[ RootColor] = BaseContempt[ RootColor] = VALUE_DRAW - contempt;
            DrawValue[~RootColor] = BaseContempt[~RootColor] = VALUE_DRAW + contempt;

            ContemptValue = i16(i32(Options["Valued Contempt (cp)"]));

            // Reset the threads, still sleeping: will wake up at split time
            Threadpool.max_ply = 0;

            u16 auto_save_time = u16(i32(Options["Auto Save Hash (min)"]));
            if (auto_save_time)
            {
                Threadpool.auto_save        = new_thread<TimerThread> ();
                Threadpool.auto_save->task  = auto_save_hash;
                Threadpool.auto_save->resolution = auto_save_time*60*MilliSec;
                Threadpool.auto_save->start ();
                Threadpool.auto_save->notify_one ();
            }

            Threadpool.timer->start ();
            Threadpool.timer->notify_one (); // Wake up the recurring timer

            search_iter_deepening (); // Let's start searching !

            Threadpool.timer->stop ();

            if (auto_save_time)
            {
                Threadpool.auto_save->stop ();
                Threadpool.auto_save->quit ();
                delete_thread (Threadpool.auto_save);
                Threadpool.auto_save = NULL;
            }

            if (!SearchLog.empty ())
            {
                LogFile logfile (SearchLog);

                point time = now () - SearchTime;
                if (time == 0) time = 1;

                logfile
                    << "Time:        " << time                                      << "\n"
                    << "Nodes:       " << RootPos.game_nodes ()                     << "\n"
                    << "Nodes/sec.:  " << RootPos.game_nodes () * MilliSec / time   << "\n"
                    << "Hash-full:   " << TT.permill_full ()                        << "\n"
                    << "Best move:   " << move_to_san (RootMoves[0].pv[0], RootPos) << "\n";
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
                << " score " << pretty_score (RootPos.checkers () ? -VALUE_MATE : VALUE_DRAW)
                << sync_endl;

            RootMoves.push_back (RootMove (MOVE_NONE));

            if (!SearchLog.empty ())
            {
                LogFile logfile (SearchLog);

                logfile
                    << pretty_pv (RootPos, 0, RootPos.checkers () ? -VALUE_MATE : VALUE_DRAW, 0, &RootMoves[0].pv[0]) << "\n"
                    << "Time:        " << 0        << "\n"
                    << "Nodes:       " << 0        << "\n"
                    << "Nodes/sec.:  " << 0        << "\n"
                    << "Hash-full:   " << 0        << "\n"
                    << "Best move:   " << "(none)" << "\n"
                    << endl;
            }

        }

    finish:
        point time = now () - SearchTime;
        if (time == 0) time = 1;

        // When search is stopped this info is printed
        sync_cout
            << "info"
            << " time "     << time
            << " nodes "    << RootPos.game_nodes ()
            << " nps "      << RootPos.game_nodes () * MilliSec / time
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
        sync_cout << "bestmove " << move_to_can (RootMoves[0].pv[0], RootPos.chess960 ());
        if (RootMoves[0].pv[0] != MOVE_NONE)
        {
            cout << " ponder " << move_to_can (RootMoves[0].pv[1], RootPos.chess960 ());
        }
        cout << sync_endl;

    }

    // initialize() is called during startup to initialize various lookup tables
    void initialize ()
    {
        u08 d;  // depth (ONE_PLY == 2)
        u08 hd; // half depth (ONE_PLY == 1)
        u08 mc; // move count
        // Initialize lookup tables
        for (d = 0; d < RazorDepth; ++d)
        {
            RazorMargins         [d] = Value(i32(0x200 + (0x10 + 0*d)*d));
        }
        for (d = 0; d < FutilityMarginDepth; ++d)
        {
            //FutilityMargins      [d] = Value(i32(  0 + (0x64 - FutilityMarginDepth/2 -1 + 1*d)*d));
            FutilityMargins      [d] = Value(i32(  5 + (0x5A - FutilityMarginDepth/2 -1 + 1*d)*d));
        }
        for (d = 0; d < FutilityMoveCountDepth; ++d)
        {
            FutilityMoveCounts[0][d] = u08(2.400f + 0.222f * pow (0.00f + d, 1.80f));
            FutilityMoveCounts[1][d] = u08(3.000f + 0.300f * pow (0.98f + d, 1.80f));
        }

        Reductions[0][0][0][0] = Reductions[0][1][0][0] = Reductions[1][0][0][0] = Reductions[1][1][0][0] = 0;
        // Initialize reductions lookup table
        for (hd = 1; hd < ReductionDepth; ++hd) // half-depth (ONE_PLY == 1)
        {
            for (mc = 1; mc < ReductionMoveCount; ++mc) // move-count
            {
                float    pv_red = 0.00f + log (float(hd)) * log (float(mc)) / 3.00f;
                float nonpv_red = 0.33f + log (float(hd)) * log (float(mc)) / 2.25f;
                Reductions[1][1][hd][mc] =    pv_red >= 1.0 ? u08(   pv_red*i16(ONE_MOVE)) : 0;
                Reductions[0][1][hd][mc] = nonpv_red >= 1.0 ? u08(nonpv_red*i16(ONE_MOVE)) : 0;

                Reductions[1][0][hd][mc] = Reductions[1][1][hd][mc];
                Reductions[0][0][hd][mc] = Reductions[0][1][hd][mc];
                // Smoother transition for LMR
                if (Reductions[0][0][hd][mc] > 2*i16(ONE_MOVE))
                {
                    Reductions[0][0][hd][mc] += 1*i16(ONE_MOVE);
                }
                else
                if (Reductions[0][0][hd][mc] > 1*i16(ONE_MOVE))
                {
                    Reductions[0][0][hd][mc] += 1*i32(ONE_PLY);
                }
            }
        }
    }

}

namespace Threads {

    // check_time() is called by the timer thread when the timer triggers.
    // It is used to print debug info and, more importantly,
    // to detect when out of available time and thus stop the search.
    void check_time ()
    {
        static point last_time = now ();
        
        point now_time = now ();
        if (now_time - last_time >= MilliSec)
        {
            last_time = now_time;
            Debugger::dbg_print ();
        }

        if (Limits.ponder || Signals.force_stop)
        {
            return;
        }

        u64 nodes = 0;
        if (Limits.nodes)
        {
            Threadpool.mutex.lock ();

            nodes = RootPos.game_nodes ();
            // Loop across all splitpoints and sum accumulated splitpoint nodes plus
            // all the currently active positions nodes.
            for (u08 t = 0; t < Threadpool.size (); ++t)
            {
                for (u08 s = 0; s < Threadpool[t]->splitpoint_threads; ++s)
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
        }

        point time = now_time - SearchTime;

        if (  (  Limits.use_timemanager ()
                    // No more time
              && (  time > TimeMgr.maximum_time () - 2 * TimerResolution
                    // or Still at first move
                 || (   Signals.root_1stmove
                    && !Signals.root_failedlow
                    && time > TimeMgr.available_time () * (RootMoves.best_move_change < 1.0e-4f ? 50 : 75) / 100 // TODO::
                    )
                 ) 
              )
           || (Limits.movetime && time  >= Limits.movetime)
           || (Limits.nodes    && nodes >= Limits.nodes)
           )
        {
            Signals.force_stop = true;
        }
    }

    void auto_save_hash ()
    {
        string hash_fn = string(Options["Hash File"]);
        TT.save (hash_fn);
    }

    // Thread::idle_loop() is where the thread is parked when it has no work to do
    void Thread::idle_loop ()
    {
        // Pointer 'splitpoint' is not null only if called from split<>(), and not
        // at the thread creation. So it means this is the splitpoint's master.
        SplitPoint *splitpoint = ((splitpoint_threads) ? active_splitpoint : NULL);
        ASSERT ((splitpoint == NULL) || ((splitpoint->master == this) && searching));

        do
        {
            // If not searching, wait for a condition to be signaled instead of
            // wasting CPU time polling for work.
            while (!searching)
            {
                // Grab the lock to avoid races with Thread::notify_one()
                mutex.lock ();

                // If master and all slaves have finished then exit idle_loop
                if (splitpoint != NULL && splitpoint->slaves_mask.none ())
                {
                    mutex.unlock ();
                    break;
                }

                // Do sleep after retesting sleep conditions under lock protection, in
                // particular to avoid a deadlock in case a master thread has,
                // in the meanwhile, allocated us and sent the notify_one () call before
                // the chance to grab the lock.
                if (!searching && !exit)
                {
                    sleep_condition.wait (mutex);
                }
                
                mutex.unlock ();
                
                if (exit)
                {
                    ASSERT (splitpoint == NULL);
                    return;
                }
            }

            // If this thread has been assigned work, launch a search
            if (searching)
            {
                ASSERT (!exit);

                Threadpool.mutex.lock ();

                ASSERT (searching);
                ASSERT (active_splitpoint != NULL);
                
                SplitPoint *sp = active_splitpoint;

                Threadpool.mutex.unlock ();

                Stack stack[MaxDepth6]
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
                        const u08 size = thread->splitpoint_threads; // Local copy
                        sp = (size > 0) ? &thread->splitpoints[size - 1] : NULL;

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

                            if (searching) break; // Just a single attempt
                        }
                    }
                }
            }

            // If this thread is the master of a splitpoint and all slaves have finished
            // their work at this splitpoint, return from the idle loop.
            if (splitpoint != NULL && splitpoint->slaves_mask.none ())
            {
                splitpoint->mutex.lock ();
                bool finished = splitpoint->slaves_mask.none (); // Retest under lock protection
                splitpoint->mutex.unlock ();
                if (finished) return;
            }
        }
        while (!exit);
    }
}
