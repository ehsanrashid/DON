#include "Searcher.h"

#include <cfloat>
#include <iostream>
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

        // Set to true to force running with one thread. Used for debugging
        const bool    FakeSplit     = false;

        const u08     MAX_QUIETS    = 64;

        const point   InfoDuration  = 3000; // 3 sec

        // Futility move count lookup table (initialized at startup)
        CACHE_ALIGN(16) u08   FutilityMoveCount[2][32]; // [improving][depth]
        
        // Futility margin lookup table (initialized at startup)
        CACHE_ALIGN(16) Value FutilityMargin[32];       // [depth]

        // Razoring margin lookup table (initialized at startup)
        CACHE_ALIGN(16) Value RazorMargin   [32];       // [depth]

        // Reduction lookup table (initialized at startup)
        CACHE_ALIGN(16) u08   Reduction[2][2][64][64];  // [pv][improving][depth][move_num]

        template<bool PVNode>
        inline Depth reduction (bool imp, i16 depth, u08 move_num)
        {
            depth /= i32 (ONE_MOVE);
            return Depth (Reduction[PVNode][imp][depth < 63 ? depth : 63][move_num < 63 ? move_num : 63]);
        }

        TimeManager TimeMgr;

        Value   DrawValue[CLR_NO];

        Color   RootColor;
        u08     RootCount;
        u08     MultiPV
            ,   PVIndex;

        struct Skill
        {
            u08  level;
            Move move;

            Skill ()
                : level (MAX_SKILL_LEVEL)
                , move (MOVE_NONE)
            {}

            Skill (u08 lvl)
                : level (lvl)
                , move (MOVE_NONE)
            {}

           ~Skill ()
            {
                if (enabled ()) // Swap best PV line with the sub-optimal one
                {
                    swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), move != MOVE_NONE ? move : pick_move ()));
                }
            }

            bool enabled () const { return (level < MAX_SKILL_LEVEL); }
            bool time_to_pick (i16 depth) const { return (depth == (1 + level)); }

            // When playing with strength handicap choose best move among the MultiPV set
            // using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
            Move pick_move ()
            {
                static RKISS rk;
                // PRNG sequence should be not deterministic
                for (i08 i = now () % 50; i > 0; --i) rk.rand64 ();

                // RootMoves are already sorted by score in descending order
                const Value variance = min (RootMoves[0].value[0] - RootMoves[MultiPV - 1].value[0], VALUE_MG_PAWN);
                const Value weakness = Value (120 - 2 * level);
                
                Value max_v = -VALUE_INFINITE;
                move = MOVE_NONE;
                // Choose best move. For each move score add two terms both dependent on
                // weakness, one deterministic and bigger for weaker moves, and one random,
                // then choose the move with the resulting highest score.
                for (u08 i = 0; i < MultiPV; ++i)
                {
                    Value v = RootMoves[i].value[0];

                    // Don't allow crazy blunders even at very low skills
                    if (i > 0 && RootMoves[i-1].value[0] > (v + 2 * VALUE_MG_PAWN))
                    {
                        break;
                    }

                    // This is our magic formula
                    v += (weakness * i32 (RootMoves[0].value[0] - v)
                      +   variance * i32 (rk.rand<u32> () % weakness) / 0x80);

                    if (max_v < v)
                    {
                        max_v = v;
                        move = RootMoves[i].pv[0];
                    }
                }
                return move;
            }

        };

        GainStats   Gain;
        // History heuristic
        HistoryStats History;
        MoveStats   CounterMoves
            ,       FollowupMoves;

        // update_stats() updates history, killer, counter & followup moves
        // after a fail-high of a quiet move.
        inline void update_stats (const Position &pos, Stack *ss, Move move, i16 depth, Move *quiet_moves, u08 quiets_count)
        {
            if ((ss)->killer_moves[0] != move)
            {
                (ss)->killer_moves[1] = (ss)->killer_moves[0];
                (ss)->killer_moves[0] = move;
            }

            // Increase history value of the cut-off move and decrease all the other played quiet moves.
            Value bonus = Value (depth * depth);
            History.update (pos[org_sq (move)], dst_sq (move), bonus);
            for (u08 i = 0; i < quiets_count; ++i)
            {
                Move m = quiet_moves[i];
                History.update (pos[org_sq (m)], dst_sq (m), -bonus);
            }

            Move opp_move = (ss-1)->current_move;
            if (_ok (opp_move))
            {
                Square opp_move_sq = dst_sq (opp_move);
                CounterMoves.update (pos[opp_move_sq], opp_move_sq, move);
            }

            Move own_move = (ss-2)->current_move;
            if (_ok (own_move) && opp_move == (ss-1)->tt_move)
            {
                Square own_move_sq = dst_sq (own_move);
                FollowupMoves.update (pos[own_move_sq], own_move_sq, move);
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
            
            MultiPV = u08 (i32 (Options["MultiPV"]));
            if (MultiPV > RootCount) MultiPV = RootCount;

            i32 sel_depth = Threadpool.max_ply;

            for (u08 i = 0; i < MultiPV; ++i)
            {
                bool updated = (i <= PVIndex);

                if (1 == depth && !updated) continue;

                i32   d;
                Value v;
                
                if (updated)
                {
                    d = depth;
                    v = RootMoves[i].value[0];
                }
                else
                {
                    d = depth - 1;
                    v = RootMoves[i].value[1];
                }

                // Not at first line
                if (ss.rdbuf ()->in_avail ()) ss << "\n";

                ss  << "info"
                    << " multipv "  << u16 (i + 1)
                    << " depth "    << d
                    << " seldepth " << sel_depth
                    << " score "    << ((i == PVIndex) ? score_uci (v, alpha, beta) : score_uci (v))
                    << " time "     << time
                    << " nodes "    << pos.game_nodes ()
                    << " nps "      << pos.game_nodes () * M_SEC / time
                    << " hashfull " << 0//TT.permill_full ()
                    << " pv"        << RootMoves[i].info_pv (pos);
            }

            return ss.str ();
        }

        // _perft() is our utility to verify move generation. All the leaf nodes
        // up to the given depth are generated and counted and the sum returned.
        inline u64 _perft (Position &pos, i16 depth)
        {
            const bool leaf = (depth == (2*ONE_MOVE));

            u64 leaf_count = U64 (0);

            StateInfo si;
            CheckInfo ci (pos);
            for (MoveList<LEGAL> ms (pos); *ms != MOVE_NONE; ++ms)
            {
                Move m = *ms;
                pos.do_move (m, si, pos.gives_check (m, ci) ? &ci : NULL);
                leaf_count += leaf ? MoveList<LEGAL> (pos).size () : _perft (pos, depth - ONE_MOVE);
                pos.undo_move ();
            }
            return leaf_count;
        }

        template <NodeT NT, bool InCheck>
        // search_quien() is the quiescence search function, which is called by the main search function
        // when the remaining depth is zero (or, to be more precise, less than ONE_MOVE).
        inline Value search_quien  (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth)
        {
            const bool    PVNode = (NT == PV);

            ASSERT (NT == PV || NT == NonPV);
            ASSERT (InCheck == (pos.checkers () != U64 (0)));
            ASSERT (alpha >= -VALUE_INFINITE && alpha < beta && beta <= +VALUE_INFINITE);
            ASSERT (PVNode || (alpha == beta-1));
            ASSERT (depth <= DEPTH_ZERO);

            (ss)->ply = (ss-1)->ply + 1;
            (ss)->current_move = MOVE_NONE;

            // Check for maximum ply reached
            if ((ss)->ply > MAX_PLY) return InCheck ? DrawValue[pos.active ()] : evaluate (pos);
            // Check for immediate draw
            if (pos.draw ())         return DrawValue[pos.active ()];
            // Check for aborted search
            if (Signals.stop)        return VALUE_ZERO;

            StateInfo si;

            Move  best_move = MOVE_NONE;

            Value best_value
                , old_alpha;

            // To flag EXACT a node with eval above alpha and no available moves
            if (PVNode) old_alpha = alpha;

            // Decide whether or not to include checks, this fixes also the type of
            // TT entry depth that are going to use. Note that in search_quien use
            // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
            Depth tt_depth = (InCheck || depth >= DEPTH_QS_CHECKS) ?
                              DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;
            Key posi_key = pos.posi_key ();

            // Transposition table lookup
            const TTEntry *tte;
            Move  tt_move;
            Value tt_value;

            tte      = TT.retrieve (posi_key);
            tt_move  = tte != NULL ?              tte->move ()              : MOVE_NONE;
            tt_value = tte != NULL ? value_of_tt (tte->value (), (ss)->ply) : VALUE_NONE;

            if (   (tte != NULL)
                && (tte->depth () >= tt_depth)
                && (tt_value != VALUE_NONE) // Only in case of TT access race
                && (        PVNode ? (tte->bound () == BND_EXACT) :
                  tt_value >= beta ? (tte->bound () &  BND_LOWER) :
                                     (tte->bound () &  BND_UPPER)
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
                (ss)->static_eval = VALUE_NONE;
                best_value = futility_base = -VALUE_INFINITE;
            }
            else
            {
                if (tte != NULL)
                {
                    // Never assume anything on values stored in TT
                    Value eval_ = tte->eval ();
                    if (VALUE_NONE == eval_) eval_ = evaluate (pos);
                    best_value = (ss)->static_eval = eval_;

                    // Can tt_value be used as a better position evaluation?
                    if (VALUE_NONE != tt_value)
                    {
                        if (tte->bound () & (tt_value >= best_value ? BND_LOWER : BND_UPPER))
                        {
                            best_value = tt_value;
                        }
                    }
                }
                else
                {
                    best_value = (ss)->static_eval = 
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
                                0,//pos.game_nodes (),
                                value_to_tt (best_value, (ss)->ply),
                                (ss)->static_eval);
                        }

                        ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
                        return best_value;
                    }

                    if (PVNode) alpha = best_value;
                }

                futility_base = best_value + 128;
            }

            // Initialize a MovePicker object for the current position, and prepare
            // to search the moves. Because the depth is <= 0 here, only captures,
            // queen promotions and checks (only if depth >= DEPTH_QS_CHECKS) will
            // be generated.

            MovePicker mp (pos, History, tt_move, depth, dst_sq ((ss-1)->current_move));
            CheckInfo  ci (pos);

            Move move;
            // Loop through the moves until no moves remain or a beta cutoff occurs
            while ((move = mp.next_move<false> ()) != MOVE_NONE)
            {
                ASSERT (_ok (move));

                bool gives_check= pos.gives_check (move, ci);

                if (!PVNode)
                {
                    // Futility pruning
                    if (   !(InCheck)
                        && !(gives_check)
                        && (futility_base > -VALUE_KNOWN_WIN)
                        && (move != tt_move)
                        && !(pos.advanced_pawn_push (move))
                       )
                    {
                        ASSERT (mtype (move) != ENPASSANT); // Due to !pos.advanced_pawn_push()

                        Value futility_value = futility_base + PieceValue[EG][ptype (pos[dst_sq (move)])];

                        if (futility_value < beta)
                        {
                            if (best_value < futility_value)
                            {
                                best_value = futility_value;
                            }
                            continue;
                        }

                        // Prune moves with negative or equal SEE and also moves with positive
                        // SEE where capturing piece loses a tempo and SEE < beta - futility_base.
                        if (futility_base < beta && pos.see (move) <= VALUE_ZERO)
                        {
                            if (best_value < futility_base)
                            {
                                best_value = futility_base;
                            }
                            continue;
                        }
                    }

                    // Detect non-capture evasions that are candidate to be pruned
                    bool evasion_prunable =
                        (  InCheck
                        && (best_value > VALUE_MATED_IN_MAX_PLY)
                        && !(pos.can_castle (pos.active ()))
                        && !(pos.capture (move))
                        );

                    // Don't search moves with negative SEE values
                    if (   (evasion_prunable || !InCheck)
                        && (move != tt_move)
                        && (mtype (move) != PROMOTE)
                        && (pos.see_sign (move) < VALUE_ZERO)
                       )
                    {
                        continue;
                    }

                }

                // Check for legality just before making the move
                if (!pos.legal (move, ci.pinneds)) continue;

                (ss)->current_move = move;

                // Make and search the move
                pos.do_move (move, si, gives_check ? &ci : NULL);

                Value value = gives_check ?
                    -search_quien<NT, true > (pos, ss+1, -beta, -alpha, depth - ONE_MOVE) :
                    -search_quien<NT, false> (pos, ss+1, -beta, -alpha, depth - ONE_MOVE);

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
                                tt_depth,
                                BND_LOWER,
                                0,//pos.game_nodes (),
                                value_to_tt (best_value, (ss)->ply),
                                (ss)->static_eval);

                            return best_value;
                        }
                        
                        // Update alpha here! always alpha < beta
                        if (PVNode) alpha = value;
                    }
                }
            }

            // All legal moves have been searched.
            if (best_value == -VALUE_INFINITE)
            {
                // A special case: If in check and no legal moves were found, it is checkmate.
                best_value = (InCheck) ?
                    mated_in ((ss)->ply) :       // Plies to mate from the root
                    DrawValue[pos.active ()];
            }

            TT.store (
                posi_key,
                best_move,
                tt_depth,
                (PVNode && old_alpha < best_value) ? BND_EXACT : BND_UPPER,
                0,//pos.game_nodes (),
                value_to_tt (best_value, (ss)->ply),
                (ss)->static_eval);

            ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        template <NodeT NT, bool SPNode>
        // search<>() is the main search function for both PV and non-PV nodes and for
        // normal and SplitPoint nodes. When called just after a splitpoint the search
        // is simpler because already probed the hash table, done a null move search,
        // and searched the first move before splitting, don't have to repeat all
        // this work again. Also don't need to store anything to the hash table here:
        // This is taken care of after return from the splitpoint.
        inline Value search        (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth, bool cut_node)
        {
            const bool RootNode = (NT == Root);
            const bool   PVNode = (NT == Root || NT == PV);

            ASSERT (-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
            ASSERT (PVNode || (alpha == beta-1));
            ASSERT (depth > DEPTH_ZERO);

            SplitPoint *splitpoint;
            Key   posi_key;

            const TTEntry *tte;

            Move  best_move
                , tt_move
                , excluded_move
                , move;

            Value best_value
                , tt_value
                , eval;

            u08   moves_count
                , quiets_count;

            Move quiet_moves[MAX_QUIETS] = { MOVE_NONE };

            StateInfo si;
            CheckInfo ci (pos);

            // Step 1. Initialize node
            Thread *thread  = pos.thread ();
            bool   in_check = pos.checkers () != U64 (0);

            if (!SPNode)
            {
                moves_count  = 0;
                quiets_count = 0;

                best_value = -VALUE_INFINITE;
                best_move  = (ss)->current_move = (ss)->tt_move = (ss+1)->excluded_move = MOVE_NONE;
                (ss)->ply  = (ss-1)->ply + 1;

                (ss+1)->skip_null_move  = false;
                (ss+1)->reduction       = DEPTH_ZERO;
                (ss+2)->killer_moves[0] = MOVE_NONE;
                (ss+2)->killer_moves[1] = MOVE_NONE;

                // Used to send sel_depth info to GUI
                if (PVNode)
                {
                    if (Threadpool.max_ply < (ss)->ply)
                    {
                        Threadpool.max_ply = (ss)->ply;
                    }
                }

                if (!RootNode)
                {
                    // Step 2. Check end condition
                    // Check for maximum ply reached
                    if ((ss)->ply > MAX_PLY) return in_check ? DrawValue[pos.active ()] : evaluate (pos);
                    // Check for immediate draw
                    if (pos.draw ())         return DrawValue[pos.active ()];
                    // Check for aborted search
                    if (Signals.stop)        return VALUE_ZERO;

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

                posi_key = (excluded_move != MOVE_NONE) ?
                        pos.posi_key_excl () :
                        pos.posi_key ();

                tte      = TT.retrieve (posi_key);
                tt_move  = (ss)->tt_move = RootNode    ? RootMoves[PVIndex].pv[0]
                                         : tte != NULL ? tte->move () : MOVE_NONE;
                tt_value = tte != NULL ? value_of_tt (tte->value (), (ss)->ply) : VALUE_NONE;

                if (!RootNode)
                {
                    // At PV nodes check for exact scores, while at non-PV nodes check for
                    // a fail high/low. Biggest advantage at probing at PV nodes is to have a
                    // smooth experience in analysis mode. Don't probe at Root nodes otherwise
                    // should also update RootMoveList to avoid bogus output.
                    if (   (tte != NULL)
                        && (tt_value != VALUE_NONE) // Only in case of TT access race
                        && (tte->depth () >= depth)
                        && (        PVNode ? (tte->bound () == BND_EXACT) :
                          tt_value >= beta ? (tte->bound () &  BND_LOWER) :
                                             (tte->bound () &  BND_UPPER)
                           )
                       )
                    {
                        (ss)->current_move = tt_move; // Can be MOVE_NONE

                        // If tt_move is quiet, update history, killer moves, countermove and followupmove on TT hit
                        if (   (tt_value >= beta)
                            && (tt_move != MOVE_NONE)
                            && !(pos.capture_or_promotion (tt_move))
                            && !(in_check)
                           )
                        {
                            update_stats (pos, ss, tt_move, depth, NULL, 0);
                        }

                        return tt_value;
                    }
                }

                // Step 5. Evaluate the position statically and update parent's gain statistics
                if (!in_check)
                {
                    if (tte != NULL)
                    {
                        // Never assume anything on values stored in TT
                        Value eval_ = tte->eval ();
                        if (VALUE_NONE == eval_) eval_ = evaluate (pos);
                        eval = (ss)->static_eval = eval_;

                        // Can tt_value be used as a better position evaluation?
                        if (VALUE_NONE != tt_value)
                        {
                            if (tte->bound () & (tt_value >= eval ? BND_LOWER : BND_UPPER))
                            {
                                eval = tt_value;
                            }
                        }
                    }
                    else
                    {
                        eval = (ss)->static_eval = 
                            (ss-1)->current_move != MOVE_NULL ? evaluate (pos) : -(ss-1)->static_eval + 2*TempoBonus;

                        TT.store (
                            posi_key,
                            MOVE_NONE,
                            DEPTH_NONE,
                            BND_NONE,
                            0,//pos.game_nodes (),
                            VALUE_NONE,
                            eval);
                    }

                    move = (ss-1)->current_move;
                    // Updates Gain
                    if (   (pos.capture_type () == NONE)
                        && ((ss  )->static_eval != VALUE_NONE)
                        && ((ss-1)->static_eval != VALUE_NONE)
                        && (move != MOVE_NONE)
                        && (move != MOVE_NULL)
                        && (mtype (move) == NORMAL)
                       )
                    {
                        Square dst = dst_sq (move);
                        Gain.update (pos[dst], dst, -((ss-1)->static_eval + (ss)->static_eval));
                    }

                    if (!PVNode) // (is omitted in PV nodes)
                    {
                
                        // Step 6. Razoring
                        if (   (depth < (4*ONE_MOVE))
                            && (tt_move == MOVE_NONE)
                            && (!pos.pawn_on_7thR (pos.active ()))
                           )
                        {
                            Value ralpha = alpha - RazorMargin[depth];
                            if (eval <= ralpha)
                            {
                                if (   (depth <= (1*ONE_MOVE))
                                    && (eval <= alpha - RazorMargin[3*ONE_MOVE])
                                   )
                                {
                                    return search_quien<NonPV, false> (pos, ss, alpha, beta, DEPTH_ZERO);
                                }

                                Value ver_value = search_quien<NonPV, false> (pos, ss, ralpha, ralpha+1, DEPTH_ZERO);
                                if (ver_value <= ralpha) return ver_value;
                            }
                        }
                    
                        // Step 7,8,9.
                        if (!((ss)->skip_null_move))
                        {
                            //ASSERT ((ss-1)->current_move != MOVE_NONE);
                            //ASSERT ((ss-1)->current_move != MOVE_NULL);

                            if (pos.non_pawn_material (pos.active ()) > VALUE_ZERO)
                            {
                                // Step 7. Futility pruning: child node
                                // Betting that the opponent doesn't have a move that will reduce
                                // the score by more than futility_margin (depth) if do a null move.
                                if (   (depth < (7*ONE_MOVE))
                                    && (abs (eval) < VALUE_KNOWN_WIN)
                                    && (abs (beta) < VALUE_MATES_IN_MAX_PLY)
                                    )
                                {
                                    Value fut_eval = eval - FutilityMargin[depth];

                                    if (fut_eval >= beta) return fut_eval;
                                }

                                // Step 8. Null move search with verification search
                                if (   (depth >= (2*ONE_MOVE))
                                    && (eval >= beta)
                                    )
                                {
                                    (ss)->current_move = MOVE_NULL;

                                    // Null move dynamic (variable) reduction based on depth and value
                                    Depth rdepth = depth - Depth (
                                                    + (3*ONE_MOVE)
                                                    + (depth/4)
                                                    + (abs (beta) < VALUE_KNOWN_WIN ? i32 (eval - beta) / VALUE_MG_PAWN * ONE_MOVE : DEPTH_ZERO));

                                    // Do null move
                                    pos.do_null_move (si);
                                    (ss+1)->skip_null_move = true;

                                    // Null window (alpha, beta) = (beta-1, beta):
                                    Value null_value = (rdepth < ONE_MOVE) ?
                                        -search_quien<NonPV, false> (pos, ss+1, -beta, -(beta-1), DEPTH_ZERO) :
                                        -search      <NonPV, false> (pos, ss+1, -beta, -(beta-1), rdepth, !cut_node);

                                    (ss+1)->skip_null_move = false;
                                    // Undo null move
                                    pos.undo_null_move ();

                                    if (null_value >= beta)
                                    {
                                        // Do not return unproven mate scores
                                        if (null_value >= VALUE_MATES_IN_MAX_PLY)
                                        {
                                            null_value = beta;
                                        }

                                        if (   (depth < (12*ONE_MOVE))
                                            && (abs(beta) < VALUE_KNOWN_WIN)
                                           )
                                        {
                                            return null_value;
                                        }
                                        // Do verification search at high depths
                                        (ss)->skip_null_move = true;

                                        Value veri_value = (rdepth < ONE_MOVE) ?
                                            search_quien<NonPV, false> (pos, ss, beta-1, beta, DEPTH_ZERO) :
                                            search      <NonPV, false> (pos, ss, beta-1, beta, rdepth, false);

                                        (ss)->skip_null_move = false;

                                        if (veri_value >= beta) return null_value;
                                    }
                                }
                            }

                            // Step 9. Prob-Cut
                            // If have a very good capture (i.e. SEE > see[captured_piece_type])
                            // and a reduced search returns a value much above beta,
                            // can (almost) safely prune the previous move.
                            if (   (depth >= (5*ONE_MOVE))
                                && (abs (beta) < VALUE_MATES_IN_MAX_PLY)
                               )
                            {
                                Depth rdepth = depth - (4*ONE_MOVE);
                                Value rbeta  = beta + 200;
                                if (rbeta > VALUE_INFINITE) rbeta = VALUE_INFINITE;
                                //ASSERT (rdepth >= ONE_MOVE);
                                //ASSERT (rbeta <= VALUE_INFINITE);
                            
                                // Initialize a MovePicker object for the current position,
                                // and prepare to search the moves.
                                MovePicker mp (pos, History, tt_move, pos.capture_type ());

                                while ((move = mp.next_move<false> ()) != MOVE_NONE)
                                {
                                    if (!pos.legal (move, ci.pinneds)) continue;

                                    (ss)->current_move = move;
                                    pos.do_move (move, si, pos.gives_check (move, ci) ? &ci : NULL);
                                    Value value = -search<NonPV, false> (pos, ss+1, -rbeta, -(rbeta-1), rdepth, !cut_node);
                                    pos.undo_move ();

                                    if (value >= rbeta) return value;
                                }
                            }

                        }
                
                    }

                    // Step 10. Internal iterative deepening (skipped when in check)
                    if (   (tt_move == MOVE_NONE)
                        && (depth >= ((PVNode ? 5 : 8)*ONE_MOVE))
                        && (PVNode || (ss)->static_eval + 256 >= beta)
                       )
                    {
                        Depth iid_depth = depth - Depth ((2*ONE_MOVE) + (PVNode ? DEPTH_ZERO : depth/4));

                        (ss)->skip_null_move = true;
                        search<PVNode ? PV : NonPV, false> (pos, ss, alpha, beta, iid_depth, true);
                        (ss)->skip_null_move = false;

                        tte = TT.retrieve (posi_key);
                        tt_move = tte != NULL ? tte->move () : MOVE_NONE;
                    }

                }
                else
                {
                    eval = (ss)->static_eval = VALUE_NONE;
                }
            }
            else
            {
                splitpoint = (ss)->splitpoint;
                best_move  = splitpoint->best_move;
                best_value = splitpoint->best_value;

                tte      = NULL;
                tt_move  = excluded_move = MOVE_NONE;
                tt_value = VALUE_NONE;

                ASSERT (splitpoint->best_value > -VALUE_INFINITE);
                ASSERT (splitpoint->moves_count > 0);
            }

        //loop_moves: // When in check and at SPNode search starts from here

            Square opp_move_sq = dst_sq ((ss-1)->current_move);
            Move cm[2] =
            {
                CounterMoves[pos[opp_move_sq]][opp_move_sq].first,
                CounterMoves[pos[opp_move_sq]][opp_move_sq].second
            };

            Square own_move_sq = dst_sq ((ss-2)->current_move);
            Move fm[2] =
            {
                FollowupMoves[pos[own_move_sq]][own_move_sq].first,
                FollowupMoves[pos[own_move_sq]][own_move_sq].second
            };

            MovePicker mp (pos, History, tt_move, depth, cm, fm, ss);

            Value value = best_value; // Workaround a bogus 'uninitialized' warning under gcc

            bool improving =
                   ((ss  )->static_eval >= (ss-2)->static_eval)
                || ((ss  )->static_eval == VALUE_NONE)
                || ((ss-2)->static_eval == VALUE_NONE);

            bool singular_ext_node =
                   (!RootNode && !SPNode)
                && (depth >= (8*ONE_MOVE))
                && (tt_move != MOVE_NONE)
                && (excluded_move == MOVE_NONE) // Recursive singular search is not allowed
                && (abs (tt_value) < VALUE_KNOWN_WIN)
                && (abs (beta)     < VALUE_KNOWN_WIN)
                && (tte->bound () & BND_LOWER)
                && (tte->depth () >= depth - (3*ONE_MOVE));

            point time;

            if (RootNode)
            {
                if (Threadpool.main () == thread)
                {
                    time = now () - SearchTime;
                    if (time > InfoDuration)
                    {
                        sync_cout
                            << "info"
                            << " depth " << u16 (depth/i32 (ONE_MOVE))
                            << " time "  << time
                            << sync_endl;
                    }
                }
            }

            // Step 11. Loop through moves
            // Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs
            while ((move = mp.next_move<SPNode> ()) != MOVE_NONE)
            {
                ASSERT (_ok (move));

                if (move == excluded_move) continue;

                // At root obey the "searchmoves" option and skip moves not listed in
                // RootMove list, as a consequence any illegal move is also skipped.
                // In MultiPV mode also skip PV moves which have been already searched.
                if (RootNode)
                {
                    if (count (RootMoves.begin () + PVIndex, RootMoves.end (), move) == 0) continue;
                }

                if (SPNode)
                {
                    // Shared counter cannot be decremented later if move turns out to be illegal
                    if (!pos.legal (move, ci.pinneds)) continue;

                    moves_count = ++splitpoint->moves_count;
                    splitpoint->mutex.unlock ();
                }
                else
                {
                    ++moves_count;
                }

                u64 nodes = U64 (0);

                if (RootNode)
                {
                    nodes = pos.game_nodes ();

                    Signals.root_1stmove = (1 == moves_count);

                    if (Threadpool.main () == thread)
                    {
                        time = now () - SearchTime;
                        if (time > InfoDuration)
                        {
                            sync_cout
                                << "info"
                                //<< " depth "          << u16 (depth/i32 (ONE_MOVE))
                                << " currmovenumber " << setw (2) << u16 (moves_count + PVIndex)
                                << " currmove "       << move_to_can (move, pos.chess960 ())
                                << " time "           << time
                                << sync_endl;
                        }
                    }
                }

                Depth ext = DEPTH_ZERO;

                bool capture_or_promotion = pos.capture_or_promotion (move);

                bool gives_check= pos.gives_check (move, ci);

                bool dangerous  = 
                       (gives_check)
                    || (NORMAL != mtype (move))
                    || (pos.advanced_pawn_push (move));

                // Step 12. Extend checks
                if (gives_check && pos.see_sign (move) >= VALUE_ZERO)
                {
                    ext = ONE_MOVE;
                }

                // Singular extension(SE) search. If all moves but one fail low on a search of
                // (alpha-s, beta-s), and just one fails high on (alpha, beta), then that move
                // is singular and should be extended. To verify this do a reduced search
                // on all the other moves but the tt_move, if result is lower than tt_value minus
                // a margin then extend tt_move.
                if (   (singular_ext_node)
                    && (move == tt_move)
                    && (ext == DEPTH_ZERO)
                    && (pos.legal (move, ci.pinneds))
                   )
                {
                    Value rbeta = tt_value - i32 (depth);

                    (ss)->excluded_move  = move;
                    (ss)->skip_null_move = true;
                    value = search<NonPV, false> (pos, ss, rbeta-1, rbeta, Depth (depth/2), cut_node);
                    (ss)->skip_null_move = false;
                    (ss)->excluded_move  = MOVE_NONE;

                    if (value < rbeta)
                    {
                        ext = ONE_MOVE;
                    }
                }

                // Update the current move (this must be done after singular extension search)
                Depth new_depth = depth - ONE_MOVE + ext;

                // Step 13. Pruning at shallow depth (exclude PV nodes)
                if (!PVNode)
                {
                    if (   !(capture_or_promotion)
                        && !(in_check)
                        && !(dangerous)
                     // && (move != tt_move) // Already implicit in the next condition
                        && (best_value > VALUE_MATED_IN_MAX_PLY)
                       )
                    {
                        // Move count based pruning
                        if (   (depth < (16*ONE_MOVE))
                            && (moves_count >= FutilityMoveCount[improving][depth])
                           )
                        {
                            if (SPNode) splitpoint->mutex.lock ();
                            continue;
                        }

                        // Value based pruning
                        Depth predicted_depth = new_depth - reduction<PVNode> (improving, depth, moves_count);

                        // Futility pruning: parent node
                        if (predicted_depth < (7*ONE_MOVE))
                        {
                            Value futility_value = (ss)->static_eval + FutilityMargin[predicted_depth]
                                                 + Gain[pos[org_sq (move)]][dst_sq (move)] + 128;

                            if (futility_value <= alpha)
                            {
                                if (best_value < futility_value)
                                {
                                    best_value = futility_value;
                                }

                                if (SPNode)
                                {
                                    splitpoint->mutex.lock ();
                                    if (splitpoint->best_value < best_value)
                                    {
                                        splitpoint->best_value = best_value;
                                    }
                                }
                                continue;
                            }
                        }

                        // Prune moves with negative SEE at low depths
                        if (   (predicted_depth < (4*ONE_MOVE))
                            && (pos.see_sign (move) < VALUE_ZERO)
                           )
                        {
                            if (SPNode) splitpoint->mutex.lock ();
                            continue;
                        }
                    }
                }

                // Check for legality only before to do the move
                if (!SPNode)
                {
                    if (!RootNode)
                    {
                        // Not legal decrement move-count & continue
                        if (!pos.legal (move, ci.pinneds))
                        {
                            --moves_count;
                            continue;
                        }
                    }

                    if (   !(capture_or_promotion)
                        && (quiets_count < MAX_QUIETS)
                       )
                    {
                        quiet_moves[quiets_count++] = move;
                    }
                }

                bool pv_1st_move = PVNode && (1 == moves_count);
                (ss)->current_move = move;

                // Step 14. Make the move
                pos.do_move (move, si, gives_check ? &ci : NULL);

                bool full_depth_search;

                // Step 15. Reduced depth search (LMR).
                // If the move fails high will be re-searched at full depth.
                if (   !(pv_1st_move)
                    && (depth >= (3*ONE_MOVE))
                    && !(capture_or_promotion)
                    && (move != tt_move)
                    && (move != (ss)->killer_moves[0])
                    && (move != (ss)->killer_moves[1])
                   )
                {
                    (ss)->reduction = reduction<PVNode> (improving, depth, moves_count);

                    if (!PVNode && cut_node)
                    {
                        (ss)->reduction += ONE_MOVE;
                    }
                    else if (History[pos[dst_sq (move)]][dst_sq (move)] < VALUE_ZERO)
                    {
                        (ss)->reduction += ONE_PLY;
                    }

                    if (    (ss)->reduction > DEPTH_ZERO
                        && (move == cm[0] || move == cm[1])
                       )
                    {
                        (ss)->reduction -= ONE_MOVE;
                        if ((ss)->reduction < DEPTH_ZERO) (ss)->reduction = DEPTH_ZERO;
                    }

                    // Decrease reduction for moves that escape a capture
                    if (   (ss)->reduction > DEPTH_ZERO
                        && mtype (move) == NORMAL
                        && ptype (pos[dst_sq (move)]) != PAWN
                       )
                    {
                        Move rev_move = mk_move<NORMAL> (dst_sq (move), org_sq (move));
                        if (pos.see (rev_move) < VALUE_ZERO)
                        {
                            (ss)->reduction -= ONE_MOVE;
                            if ((ss)->reduction < DEPTH_ZERO) (ss)->reduction = DEPTH_ZERO;
                        }
                    }

                    Depth red_depth = new_depth - (ss)->reduction;
                    if (red_depth < ONE_MOVE) red_depth = ONE_MOVE;

                    if (SPNode) alpha = splitpoint->alpha;

                    // Search with reduced depth
                    value = -search<NonPV, false> (pos, ss+1, -(alpha+1), -alpha, red_depth, true);

                    // Re-search at intermediate depth if reduction is very high
                    if ((alpha < value) && ((ss)->reduction >= (4*ONE_MOVE)))
                    {
                        Depth inter_depth = new_depth - (2*ONE_MOVE);
                        if (inter_depth < ONE_MOVE) inter_depth = ONE_MOVE;
                        
                        value = -search<NonPV, false> (pos, ss+1, -(alpha+1), -alpha, inter_depth, true);
                    }

                    full_depth_search = ((alpha < value) && ((ss)->reduction != DEPTH_ZERO));
                    (ss)->reduction = DEPTH_ZERO;
                }
                else
                {
                    full_depth_search = !pv_1st_move;
                }

                // Step 16. Full depth search, when LMR is skipped or fails high
                if (full_depth_search)
                {
                    if (SPNode) alpha = splitpoint->alpha;

                    value =
                        (new_depth < ONE_MOVE) ?
                        (gives_check ?
                        -search_quien<NonPV, true > (pos, ss+1, -(alpha+1), -(alpha), DEPTH_ZERO) :
                        -search_quien<NonPV, false> (pos, ss+1, -(alpha+1), -(alpha), DEPTH_ZERO)) :
                        -search      <NonPV, false> (pos, ss+1, -(alpha+1), -(alpha), new_depth, !cut_node);
                }

                // Principal Variation Search
                // For PV nodes only
                if (PVNode)
                {
                    // Do a full PV search on the first move or after a fail high
                    // (in the latter case search only if value < beta), otherwise let the
                    // parent node fail low with value <= alpha and to try another move.
                    if (pv_1st_move || ((alpha < value) && (RootNode || (value < beta))))
                    {
                        value =
                            (new_depth < ONE_MOVE) ?
                            (gives_check ?
                            -search_quien<PV, true > (pos, ss+1, -beta, -alpha, DEPTH_ZERO) :
                            -search_quien<PV, false> (pos, ss+1, -beta, -alpha, DEPTH_ZERO)) :
                            -search      <PV, false> (pos, ss+1, -beta, -alpha, new_depth, false);
                    }
                }

                // Step 17. Undo move
                pos.undo_move ();

                ASSERT (-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Step 18. Check for new best move
                if (SPNode)
                {
                    splitpoint->mutex.lock ();
                    best_value = splitpoint->best_value;
                    alpha      = splitpoint->alpha;
                }
                
                // Finished searching the move. If a stop or a cutoff occurred,
                // the return value of the search cannot be trusted,
                // and return immediately without updating best move, PV and TT.
                if (Signals.stop || thread->cutoff_occurred ())
                {
                    return VALUE_ZERO;
                }

                if (RootNode)
                {
                    RootMove &rm = *find (RootMoves.begin (), RootMoves.end (), move);
                    // Remember searched nodes counts for this move
                    rm.nodes += pos.game_nodes () - nodes;

                    // PV move or new best move ?
                    if (pv_1st_move || alpha < value)
                    {
                        rm.value[0] = value;
                        rm.extract_pv_from_tt (pos);

                        // Record how often the best move has been changed in each
                        // iteration. This information is used for time management:
                        // When the best move changes frequently, allocate some more time.
                        if (!pv_1st_move)
                        {
                            RootMoves.best_move_changes++;
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
                        
                        // Update alpha here! always alpha < beta
                        if (PVNode) alpha = (SPNode) ? splitpoint->alpha = value : value;
                    }
                }

                // Step 19. Check for splitting the search (at non splitpoint node)
                if (!SPNode)
                {
                    if (   Threadpool.split_depth <= depth
                        && Threadpool.size () > 1
                        //&& Threadpool.available_slave (thread)
                        && (    thread->active_splitpoint == NULL
                            || !thread->active_splitpoint->slave_searching
                           )
                        && (thread->splitpoint_threads < MAX_SPLITPOINT_THREADS)
                       )
                    {
                        ASSERT (alpha >= best_value && best_value < beta);

                        thread->split<FakeSplit> (pos, ss, alpha, beta, best_value, best_move, depth, moves_count, mp, NT, cut_node);
                        
                        if (Signals.stop || thread->cutoff_occurred ())
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
                // All legal moves have been searched and if there are no legal moves, it
                // must be mate or stalemate, so return value accordingly.
                // If in a singular extension search then return a fail low score.
                if (0 == moves_count)
                {
                    best_value = 
                        (excluded_move != MOVE_NONE) ? alpha :
                        in_check ? mated_in ((ss)->ply) :
                        DrawValue[pos.active ()];
                }
                // Quiet best move:
                else
                {
                    // Update history, killer, counter & followup moves
                    if ((best_value >= beta) && !in_check && !pos.capture_or_promotion (best_move))
                    {
                        update_stats (pos, ss, best_move, depth, quiet_moves, quiets_count);
                    }
                }

                TT.store (
                    posi_key,
                    best_move,
                    depth,
                    (best_value >= beta) ? BND_LOWER : (PVNode && best_move != MOVE_NONE) ? BND_EXACT : BND_UPPER,
                    0,//pos.game_nodes (),
                    value_to_tt (best_value, (ss)->ply),
                    (ss)->static_eval);
            }

            ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        // iter_deep_loop() is the main iterative deepening loop. It calls search() repeatedly
        // with increasing depth until:
        // - the allocated thinking time has been consumed,
        // - the user stops the search,
        // - the maximum search depth is reached.
        // Time management; with iterative deepining enabled you can specify how long
        // you want the computer to think rather than how deep you want it to think. 
        inline void iter_deep_loop (Position &pos)
        {
            Stack stack[MAX_PLY_6]
                , *ss = stack+2; // To allow referencing (ss-2)

            memset (ss-2, 0x00, 5*sizeof (*ss));

            TT.new_gen ();

            Gain.clear ();
            History.clear ();
            CounterMoves.clear ();
            FollowupMoves.clear ();

            Value best_value = -VALUE_INFINITE
                , bound[2]   = { -VALUE_INFINITE, +VALUE_INFINITE }
                , window[2]  = { VALUE_ZERO, VALUE_ZERO };

            i32 depth = DEPTH_ZERO;

            u08 level = u08 (i32 (Options["Skill Level"]));
            Skill skill (level);

            MultiPV   = u08 (i32 (Options["MultiPV"]));
            // Do have to play with skill handicap?
            // In this case enable MultiPV search to MIN_SKILL_MULTIPV
            // that will use behind the scenes to retrieve a set of possible moves.
            if (skill.enabled ())
            {
                if (MultiPV < MIN_SKILL_MULTIPV) MultiPV = MIN_SKILL_MULTIPV;
            }
            if (MultiPV > RootCount) MultiPV = RootCount;

            point iteration_time;

            // Iterative deepening loop until target depth reached
            while (++depth <= MAX_PLY && (Limits.depth == 0 || depth <= Limits.depth))
            {
                // Requested to stop?
                if (Signals.stop) break;
                
                // Age out PV variability metric
                RootMoves.best_move_changes *= 0.5;

                // Save last iteration's scores before first PV line is searched and
                // all the move scores but the (new) PV are set to -VALUE_INFINITE.
                for (u08 i = 0; i < RootCount; ++i)
                {
                    RootMoves[i].value[1] = RootMoves[i].value[0];
                }
                
                const bool aspiration = depth > (2*ONE_MOVE);

                // MultiPV loop. Perform a full root search for each PV line
                //for (PVIndex = 0; PVIndex < (aspiration ? MultiPV : (RootCount < 4 ? RootCount : 4)); ++PVIndex)
                for (PVIndex = 0; PVIndex < MultiPV; ++PVIndex)
                {
                    // Requested to stop?
                    if (Signals.stop) break;

                    // Reset Aspiration window starting size
                    if (aspiration)
                    {
                        window[0] =
                        window[1] =
                            //Value (depth < (16*ONE_MOVE) ? 14 + (depth/4) : 22);
                            Value (16);
                        bound[0]  = max (RootMoves[PVIndex].value[1] - window[0], -VALUE_INFINITE);
                        bound[1]  = min (RootMoves[PVIndex].value[1] + window[1], +VALUE_INFINITE);
                    }

                    // Start with a small aspiration window and, in case of fail high/low,
                    // research with bigger window until not failing high/low anymore.
                    do
                    {
                        best_value = search<Root, false> (pos, ss, bound[0], bound[1], depth*ONE_MOVE, false);

                        // Bring to front the best move. It is critical that sorting is
                        // done with a stable algorithm because all the values but the first
                        // and eventually the new best one are set to -VALUE_INFINITE and
                        // want to keep the same order for all the moves but the new PV
                        // that goes to the front. Note that in case of MultiPV search
                        // the already searched PV lines are preserved.
                        RootMoves.sort_nonmultipv (PVIndex);

                        // Write PV back to transposition table in case the relevant
                        // entries have been overwritten during the search.
                        for (u08 i = 0; i <= PVIndex; ++i)
                        {
                            RootMoves[i].insert_pv_into_tt (pos);
                        }

                        iteration_time = now () - SearchTime;

                        // If search has been stopped break immediately.
                        // Sorting and writing PV back to TT is safe becuase
                        // RootMoves is still valid, although refers to previous iteration.
                        if (Signals.stop) break;

                        // When failing high/low give some update
                        // (without cluttering the UI) before to re-search.
                        if (   ((bound[0] >= best_value) || (best_value >= bound[1]))
                            && (iteration_time > InfoDuration)
                           )
                        {
                            sync_cout << info_multipv (pos, i16 (depth), bound[0], bound[1], iteration_time) << sync_endl;
                        }

                        // In case of failing low/high increase aspiration window and
                        // re-search, otherwise exit the loop.
                        if      (best_value <= bound[0])
                        {
                            bound[0] = max (best_value - window[0], -VALUE_INFINITE);
                            window[0] *= 1.5;
                            //window[1] *= 1.1;
                            Signals.root_failedlow = true;
                            Signals.stop_ponderhit = false;
                        }
                        else if (best_value >= bound[1])
                        {
                            bound[1] = min (best_value + window[1], +VALUE_INFINITE);
                            window[1] *= 1.5;
                            //window[0] *= 1.1;
                        }
                        else
                        {
                            break;
                        }

                        ASSERT (-VALUE_INFINITE <= bound[0] && bound[0] < bound[1] && bound[1] <= +VALUE_INFINITE);
                    }
                    while (bound[0] < bound[1]);

                    // Sort the PV lines searched so far and update the GUI
                    RootMoves.sort_multipv (PVIndex + 1);
                    
                    if ((PVIndex + 1) == MultiPV || (iteration_time > InfoDuration))
                    {
                        sync_cout << info_multipv (pos, i16 (depth), bound[0], bound[1], iteration_time) << sync_endl;
                    }
                }

                // If skill levels are enabled and time is up, pick a sub-optimal best move
                if (skill.enabled () && skill.time_to_pick (i16 (depth)))
                {
                    skill.pick_move ();
                    if (MOVE_NONE != skill.move)
                    {
                        swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), skill.move));
                    }
                }

                iteration_time = now () - SearchTime;

                if (bool (Options["Write SearchLog"]))
                {
                    string searchlog_fn = string (Options["SearchLog File"]);
                    convert_path (searchlog_fn);

                    LogFile log (searchlog_fn);
                    log << pretty_pv (pos, depth, RootMoves[0].value[0], iteration_time, &RootMoves[0].pv[0]) << endl;
                }

                // Have found a "mate in <x>"?
                if (   (Limits.mate != 0)
                    && (best_value >= VALUE_MATES_IN_MAX_PLY)
                    && (VALUE_MATE - best_value <= (Limits.mate*i32 (ONE_MOVE)))
                   )
                {
                    Signals.stop = true;
                }

                // Do have time for the next iteration? Can stop searching now?
                if (   Limits.use_timemanager ()
                    && !Signals.stop
                    && !Signals.stop_ponderhit
                   )
                {
                    // Take in account some extra time if the best move has changed
                    if (   (4 < depth && depth < MAX_PLY/i32 (ONE_MOVE))
                        && (1 == MultiPV)
                       )
                    {
                        TimeMgr.pv_instability (RootMoves.best_move_changes);
                    }

                    // Stop the search early:
                    bool stop = false;
                    // If there is only one legal move available or 
                    // If all of the available time has been used.
                    if (   (RootCount == 1)
                        || (iteration_time > TimeMgr.available_time ())
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
                            Signals.stop_ponderhit = true;
                        }
                        else
                        {
                            Signals.stop           = true;
                        }
                    }
                }
            }

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
        i08 ply = 0; // Ply starts from 1, we need to start from 0
        Move m = pv[ply];
        
        pv.clear ();

        StateInfo states[MAX_PLY_6]
                , *si = states;
        Value expected_value = value[0];

        const TTEntry *tte;
        do
        {
            pv.push_back (m);

            ASSERT (MoveList<LEGAL> (pos).contains (pv[ply]));

            pos.do_move (pv[ply++], *si++);
            tte = TT.retrieve (pos.posi_key ());
            expected_value = -expected_value;
        }
        while (tte // Local copy, TT could change
            && expected_value == value_of_tt (tte->value (), ply+1)
            && (m = tte->move ()) != MOVE_NONE
            && pos.pseudo_legal (m)
            && pos.legal (m)
            && ply < MAX_PLY
            && (!pos.draw () || ply < 2));
        do
        {
            pos.undo_move ();
            --ply;
        }
        while (ply != 0);

        pv.push_back (MOVE_NONE); // Must be zero-terminating
    }
    // RootMove::insert_pv_in_tt() is called at the end of a search iteration, and
    // inserts the PV back into the TT. This makes sure the old PV moves are searched
    // first, even if the old TT entries have been overwritten.
    void RootMove::insert_pv_into_tt (Position &pos)
    {
        i08 ply = 0; // Ply starts from 1, we need to start from 0
        StateInfo states[MAX_PLY_6]
                , *si = states;

        const TTEntry *tte;
        do
        {
            tte = TT.retrieve (pos.posi_key ());
            // Don't overwrite correct entries
            if (tte == NULL || tte->move () != pv[ply])
            {
                TT.store (
                    pos.posi_key (),
                    pv[ply],
                    DEPTH_NONE,
                    BND_NONE,
                    0,//pos.game_nodes (),
                    VALUE_NONE,
                    VALUE_NONE);
            }

            ASSERT (MoveList<LEGAL> (pos).contains (pv[ply]));

            pos.do_move (pv[ply++], *si++);
        }
        while (MOVE_NONE != pv[ply]);
        do
        {
            pos.undo_move ();
            --ply;
        }
        while (ply != 0);
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

    void RootMoveList::initialize (const Position &pos, const vector<Move> root_moves)
    {
        best_move_changes = 0.0;
        clear ();
        bool all_rootmoves = root_moves.empty ();
        for (MoveList<LEGAL> itr (pos); *itr != MOVE_NONE; ++itr)
        {
            Move m = *itr;
            if (   all_rootmoves
                || count (root_moves.begin (), root_moves.end (), m))
            {
                push_back (RootMove (m));
            }
        }
    }

    u64 RootMoveList::game_nodes () const
    {
        u64 nodes = U64 (0);
        for (const_iterator itr = begin (); itr < end (); ++itr)
        {
            nodes += itr->nodes;
        }
        return nodes;
    }


    u64 perft (Position &pos, Depth depth)
    {
        return (depth > ONE_MOVE) ? _perft (pos, depth) : MoveList<LEGAL> (pos).size ();
    }

    // Main searching starts from here
    void think ()
    {
        RootColor = RootPos.active ();
        TimeMgr.initialize (Limits.gameclock[RootColor], Limits.movestogo, RootPos.game_ply ());

        i32 contempt = i32 (Options["Contempt Factor"]) * VALUE_EG_PAWN / 100; // From centipawns;
        DrawValue[ RootColor] = VALUE_DRAW - contempt;
        DrawValue[~RootColor] = VALUE_DRAW + contempt;

        bool write_searchlog = bool (Options["Write SearchLog"]);
        string searchlog_fn  = "";
        if (write_searchlog)
        {
            searchlog_fn = string (Options["SearchLog File"]);
            convert_path (searchlog_fn);
            write_searchlog = !searchlog_fn.empty ();
        }
        
        i32 autosave_time;

        if (!RootMoves.empty ())
        {
            if (bool (Options["Own Book"]) && !Limits.infinite && Limits.mate == 0)
            {
                string fn_book = string (Options["Book File"]);
                convert_path (fn_book);

                if (!Book.is_open () && !fn_book.empty ())
                {
                    Book.open (fn_book, ios_base::in|ios_base::binary);
                }
                if (Book.is_open ())
                {
                    Move book_move = Book.probe_move (RootPos, bool (Options["Best Book Move"]));
                    if (   book_move != MOVE_NONE
                        && count (RootMoves.begin (), RootMoves.end (), book_move) != 0
                        )
                    {
                        swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), book_move));
                        goto finish;
                    }
                }
            }

            RootCount = RootMoves.size ();

            if (write_searchlog)
            {
                LogFile log (searchlog_fn);

                log << "----------->\n" << boolalpha
                    << "fen:       " << RootPos.fen ()                   << "\n"
                    << "infinite:  " << Limits.infinite                  << "\n"
                    << "ponder:    " << Limits.ponder                    << "\n"
                    << "time:      " << Limits.gameclock[RootColor].time << "\n"
                    << "increment: " << Limits.gameclock[RootColor].inc  << "\n"
                    << "movetime:  " << Limits.movetime                  << "\n"
                    << "movestogo: " << u16 (Limits.movestogo)           << "\n"
                    << "rootcount: " << u16 (RootCount)                  << "\n"
                    << "  d   score   time    nodes  pv\n"
                    << "-----------------------------------------------------------"
                    << endl;
            }

            // Reset the threads, still sleeping: will wake up at split time
            Threadpool.max_ply = 0;

            autosave_time = i32 (Options["Auto-Save Hash (mins)"]);
            if (autosave_time != 0)
            {
                Threadpool.autosave->resolution = autosave_time*60*M_SEC;
                Threadpool.autosave->start ();
                Threadpool.autosave->notify_one ();
            }

            Threadpool.timer->start ();
            Threadpool.timer->notify_one ();// Wake up the recurring timer

            iter_deep_loop (RootPos);       // Let's start searching !

            Threadpool.timer->stop ();

            if (autosave_time != 0)
            {
                Threadpool.autosave->stop ();
            }

            if (write_searchlog)
            {
                LogFile log (searchlog_fn);

                point time = now () - SearchTime;
                if (time == 0) time = 1;

                log << "Time:        " << time                                      << "\n"
                    << "Nodes:       " << RootPos.game_nodes ()                     << "\n"
                    << "Nodes/sec.:  " << RootPos.game_nodes () * M_SEC / time      << "\n"
                    << "Hash-full:   " << TT.permill_full ()                        << "\n"
                    << "Best move:   " << move_to_san (RootMoves[0].pv[0], RootPos) << "\n";
                if (RootMoves[0].pv[0] != MOVE_NONE)
                {
                    StateInfo si;
                    RootPos.do_move (RootMoves[0].pv[0], si);
                    log << "Ponder move: " << move_to_san (RootMoves[0].pv[1], RootPos);
                    RootPos.undo_move ();
                }
                log << endl;
            }

        }
        else
        {
            sync_cout
                << "info"
                << " depth " << 0
                << " score " << score_uci (RootPos.checkers () ? -VALUE_MATE : VALUE_DRAW)
                << sync_endl;

            RootMoves.push_back (RootMove (MOVE_NONE));
        }

    finish:
        point time = now () - SearchTime;
        if (time == 0) time = 1;

        // When search is stopped this info is printed
        sync_cout
            << "info"
            << " time "     << time
            << " nodes "    << RootPos.game_nodes ()
            << " nps "      << RootPos.game_nodes () * M_SEC / time
            << " hashfull " << 0//TT.permill_full ()
            << sync_endl;

        // When reach max depth arrive here even without Signals.stop is raised,
        // but if are pondering or in infinite search, according to UCI protocol,
        // shouldn't print the best move before the GUI sends a "stop" or "ponderhit" command.
        // Simply wait here until GUI sends one of those commands (that raise Signals.stop).
        if (!Signals.stop && (Limits.ponder || Limits.infinite))
        {
            Signals.stop_ponderhit = true;
            RootPos.thread ()->wait_for (Signals.stop);
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
        // Initialize lookup tables
        for (u08 d = 0; d < 32; ++d)    // depth (ONE_MOVE == 2)
        {
            FutilityMoveCount[0][d] = u08 (2.40 + 0.222 * pow (0.00 + d, 1.80));
            FutilityMoveCount[1][d] = u08 (3.00 + 0.300 * pow (0.98 + d, 1.80));
            FutilityMargin      [d] = Value (i32 ( 20 + (95 - 1*d)*d));
            RazorMargin         [d] = Value (i32 (512 + 16*d));
        }

        Reduction[0][0][0][0] = Reduction[0][1][0][0] = Reduction[1][0][0][0] = Reduction[1][1][0][0] = 0;
        // Initialize reductions lookup table
        for (u08 hd = 1; hd < 64; ++hd) // half-depth (ONE_PLY == 1)
        {
            for (u08 mc = 1; mc < 64; ++mc) // move-count
            {
                double     pv_red = 0.00 + log (double (hd)) * log (double (mc)) / 3.00;
                double non_pv_red = 0.33 + log (double (hd)) * log (double (mc)) / 2.25;
                Reduction[1][1][hd][mc] = u08 (    pv_red >= 1.0 ?     pv_red * i32 (ONE_MOVE) : 0);
                Reduction[0][1][hd][mc] = u08 (non_pv_red >= 1.0 ? non_pv_red * i32 (ONE_MOVE) : 0);

                Reduction[1][0][hd][mc] = Reduction[1][1][hd][mc];
                Reduction[0][0][hd][mc] = Reduction[0][1][hd][mc];
                // Smoother transition for LMR
                if      (Reduction[0][0][hd][mc] > (2*ONE_MOVE))
                {
                    Reduction[0][0][hd][mc] += ONE_MOVE;
                }
                else if (Reduction[0][0][hd][mc] > (1*ONE_MOVE))
                {
                    Reduction[0][0][hd][mc] += ONE_PLY;
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
        if ((now_time - last_time) >= M_SEC)
        {
            last_time = now_time;
            Debugger::dbg_print ();
        }

        if (Limits.ponder || Signals.stop)
        {
            return;
        }

        u64 nodes = 0;
        if (Limits.nodes != 0)
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

        bool still_at_1stmove =
                (Signals.root_1stmove)
            && !(Signals.root_failedlow)
            && (time > TimeMgr.available_time () * 75/100);

        bool no_more_time =
               (time > TimeMgr.maximum_time () - 2 * TimerResolution)
            || (still_at_1stmove);

        if (   (Limits.use_timemanager () && no_more_time)
            || (Limits.movetime != 0 && (time  >= Limits.movetime))
            || (Limits.nodes    != 0 && (nodes >= Limits.nodes))
           )
        {
            Signals.stop = true;
        }
    }

    // Thread::idle_loop() is where the thread is parked when it has no work to do
    void Thread::idle_loop ()
    {
        // Pointer 'splitpoint' is not null only if called from split<>(), and not
        // at the thread creation. So it means this is the splitpoint's master.
        SplitPoint *splitpoint = ((splitpoint_threads != 0) ? active_splitpoint : NULL);
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

                Stack stack[MAX_PLY_6]
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
                case  Root: search<Root , true> (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
                case    PV: search<PV   , true> (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
                case NonPV: search<NonPV, true> (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
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
                if (   (this != (sp)->master)
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
                        sp = (size == 0) ? NULL : &thread->splitpoints[size - 1];

                        if (   sp != NULL
                            && (sp)->slave_searching
                            && available_to (thread)
                           )
                        {
                            // Recheck the conditions under lock protection
                            Threadpool.mutex.lock ();
                            (sp)->mutex.lock ();

                            if (   (sp)->slave_searching
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
