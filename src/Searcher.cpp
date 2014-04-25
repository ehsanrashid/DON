#include "Searcher.h"

#include <cfloat>
#include <cmath>
#include <algorithm>
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

#ifdef _MSC_VER
#   pragma warning (disable: 4189) // 'argument' : local variable is initialized but not referenced
#endif

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

        // Futility lookup tables (initialized at startup) and their access functions
        u08 FutilityMoveCounts[2][32];  // [improving][depth]

        // Reduction lookup tables (initialized at startup) and their access function
        u08 Reductions[2][2][64][64];   // [pv][improving][depth][move_num]

        inline Value futility_margin (u16 depth)
        {
            return Value (depth*100); // TODO::64/100
        }

        template<bool PVNode>
        inline Depth reduction (bool imp, u08 depth, u08 move_num)
        {
            depth >>= ONE_PLY;
            return Depth (Reductions[PVNode][imp][depth < 63 ? depth : 63][move_num < 63 ? move_num : 63]);
        }

        // Dynamic razoring margin based on depth
        inline Value razor_margin (u08 depth)
        {
            return Value (512 + (depth<<4));
        }

        TimeManager TimeMgr;

        Value   DrawValue[CLR_NO];
        Value   Contempt[2]; // [best_value > VALUE_DRAW]

        double  BestMoveChanges;

        u08     MultiPV
            ,   IndexPV;

        GainsStats   Gains;
        // History heuristic
        HistoryStats History;
        MovesStats   CounterMoves
            ,        FollowupMoves;

        // update_stats() updates history, killer, counter & followup moves
        // after a fail-high of a quiet move.
        inline void update_stats (Position &pos, Stack *ss, Move move, u08 depth, Move *quiet_moves, u08 quiets_count)
        {
            if ((ss)->killer_moves[0] != move)
            {
                (ss)->killer_moves[1] = (ss)->killer_moves[0];
                (ss)->killer_moves[0] = move;
            }

            // Increase history value of the cut-off move and decrease all the other played quiet moves.
            Value bonus = Value (1 * depth * depth);
            History.update (pos[org_sq (move)], dst_sq (move), bonus);
            for (u08 i = 0; i < quiets_count; ++i)
            {
                Move m = quiet_moves[i];
                if (m != move) History.update (pos[org_sq (m)], dst_sq (m), -bonus);
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
            return v >= VALUE_MATES_IN_MAX_PLY ? v + ply
                 : v <= VALUE_MATED_IN_MAX_PLY ? v - ply
                 : v;
        }
        // value_fr_tt() is the inverse of value_to_tt ():
        // It adjusts a mate score from the transposition table
        // (where refers to the plies to mate/be mated from current position)
        // to "plies to mate/be mated from the root".
        inline Value value_fr_tt (Value v, i32 ply)
        {
            return v == VALUE_NONE             ? VALUE_NONE
                 : v >= VALUE_MATES_IN_MAX_PLY ? v - ply
                 : v <= VALUE_MATED_IN_MAX_PLY ? v + ply
                 : v;
        }

        // info_pv() formats PV information according to UCI protocol.
        // UCI requires to send all the PV lines also if are still to be searched
        // and so refer to the previous search score.
        inline string info_pv (const Position &pos, u08 depth, Value alpha, Value beta, point elapsed)
        {
            ASSERT (elapsed >= 0);
            if (elapsed == 0) elapsed = 1;

            ostringstream oss;

            u08 rm_size = min<i32> (Options["MultiPV"], RootMoves.size ());
            u08 sel_depth = 0;
            for (u08 t = 0; t < Threadpool.size (); ++t)
            {
                if (sel_depth < Threadpool[t]->max_ply)
                {
                    sel_depth = Threadpool[t]->max_ply;
                }
            }

            for (u08 i = 0; i < rm_size; ++i)
            {
                bool updated = (i <= IndexPV);

                if (1 == depth && !updated) continue;

                u08   d;
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
                if (oss.rdbuf ()->in_avail ()) oss << "\n";

                oss << "info"
                    << " multipv "  << u16 (i + 1)
                    << " depth "    << u16 (d)
                    << " seldepth " << u16 (sel_depth)
                    << " score "    << ((i == IndexPV) ? score_uci (v, alpha, beta) : score_uci (v))
                    << " time "     << elapsed
                    << " nodes "    << pos.game_nodes ()
                    << " nps "      << pos.game_nodes () * M_SEC / elapsed
                    << " hashfull " << TT.permill_full ()
                    << " pv";
                for (u08 j = 0; RootMoves[i].pv[j] != MOVE_NONE; ++j)
                {
                    oss << " " << move_to_can (RootMoves[i].pv[j], pos.chess960 ());
                }
            }

            return oss.str ();
        }

        struct Skill
        {
            u08  level;
            Move move;

            Skill (u08 lvl)
                : level (lvl)
                , move (MOVE_NONE)
            {}

           ~Skill ()
            {
                if (enabled ()) // Swap best PV line with the sub-optimal one
                {
                    swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end(), move ? move : pick_move ()));
                }
            }

            bool enabled () const { return (level < MAX_SKILL_LEVEL); }
            bool time_to_pick (u08 depth) const { return (depth == (1 + level)); }

            // When playing with strength handicap choose best move among the MultiPV set
            // using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
            Move pick_move ()
            {
                static RKISS rk;
                // PRNG sequence should be not deterministic
                for (i08 i = now () % 50; i > 0; --i) rk.rand64 ();

                move = MOVE_NONE;

                // RootMoves are already sorted by score in descending order
                Value variance = min (RootMoves[0].value[0] - RootMoves[MultiPV - 1].value[0], VALUE_MG_PAWN);
                Value weakness = Value (120 - 2 * level);
                Value max_v    = -VALUE_INFINITE;

                // Choose best move. For each move score we add two terms both dependent on
                // weakness, one deterministic and bigger for weaker moves, and one random,
                // then we choose the move with the resulting highest score.
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
                      +  (variance * i32 (rk.rand<u32> () % weakness)) / 128);

                    if (max_v < v)
                    {
                        max_v = v;
                        move = RootMoves[i].pv[0];
                    }
                }
                return move;
            }

        };

        // _perft() is our utility to verify move generation. All the leaf nodes
        // up to the given depth are generated and counted and the sum returned.
        inline u64 _perft (Position &pos, const Depth &depth)
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

        template <NodeT NT, bool IN_CHECK>
        // search_quien() is the quiescence search function, which is called by the main search function
        // when the remaining depth is zero (or, to be more precise, less than ONE_MOVE).
        inline Value search_quien  (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth)
        {
            const bool    PVNode = (NT == PV);

            ASSERT (NT == PV || NT == NonPV);
            ASSERT (IN_CHECK == (pos.checkers () != U64 (0)));
            ASSERT (alpha >= -VALUE_INFINITE && alpha < beta && beta <= +VALUE_INFINITE);
            ASSERT (PVNode || (alpha == beta-1));
            ASSERT (depth <= DEPTH_ZERO);

            (ss)->ply = (ss-1)->ply + 1;
            (ss)->current_move = MOVE_NONE;

            // Check for an instant draw or maximum ply reached
            if (pos.draw () || ((ss)->ply > MAX_PLY))
            {
                return ((ss)->ply > MAX_PLY && !IN_CHECK) ? evaluate (pos)
                    : DrawValue[pos.active ()];
            }

            StateInfo si;

            Move  best_move = MOVE_NONE;

            Value best_value
                , old_alpha;

            // To flag EXACT a node with eval above alpha and no available moves
            if (PVNode)
            {
                old_alpha = alpha;
            }

            // Decide whether or not to include checks, this fixes also the type of
            // TT entry depth that we are going to use. Note that in search_quien we use
            // only two types of depth in TT: DEPTH_QS_CHECKS or DEPTH_QS_NO_CHECKS.
            Depth tt_depth = (IN_CHECK || depth >= DEPTH_QS_CHECKS)
                ?  DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;

            Key posi_key = pos.posi_key ();

            // Transposition table lookup
            const TTEntry *tte;
            Move  tt_move;
            Value tt_value;

            tte      = TT.retrieve (posi_key);
            tt_move  = tte ?              tte->move ()              : MOVE_NONE;
            tt_value = tte ? value_fr_tt (tte->value (), (ss)->ply) : VALUE_NONE;

            if (   (tte != NULL)
                && (tte->depth () >= tt_depth)
                && (tt_value != VALUE_NONE) // Only in case of TT access race
                && (        PVNode ?  tte->bound () == BND_EXACT
                : tt_value >= beta ? (tte->bound () &  BND_LOWER)
                :                    (tte->bound () &  BND_UPPER)
                   )
               )
            {
                (ss)->current_move = tt_move; // Can be MOVE_NONE
                return tt_value;
            }

            Value futility_base;

            // Evaluate the position statically
            if (IN_CHECK)
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
                        if (tte->bound () & (tt_value > best_value ? BND_LOWER : BND_UPPER))
                        {
                            best_value = tt_value;
                        }
                    }
                }
                else
                {
                    best_value = (ss)->static_eval = evaluate (pos);
                }

                // Stand pat. Return immediately if static value is at least beta
                if (best_value >= beta)
                {
                    if (tte == NULL)
                    {
                        TT.store (
                            pos.posi_key (),
                            MOVE_NONE,
                            DEPTH_NONE,
                            BND_LOWER,
                            pos.game_nodes (),
                            value_to_tt (best_value, (ss)->ply),
                            (ss)->static_eval);
                    }

                    ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
                    return best_value;
                }

                if (PVNode)
                {
                    if (alpha < best_value)
                    {
                        alpha = best_value;
                    }
                }

                futility_base = best_value + Value (128);
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
                    //  ((NORMAL == mtype (move)) && ci.discoverers == U64 (0))
                    //? (ci.checking_bb[ptype (pos[org_sq (move)])] & dst_sq (move))
                    //: (pos.gives_check (move, ci));

                if (!PVNode)
                {
                    // Futility pruning
                    if (   !(IN_CHECK)
                        && !(gives_check)
                        && (futility_base > -VALUE_KNOWN_WIN)
                        && (move != tt_move)
                        && !(pos.advanced_pawn_push (move))
                       )
                    {
                        ASSERT (mtype (move) != ENPASSANT); // Due to !pos.advanced_pawn_push

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
                           IN_CHECK
                        && (best_value > VALUE_MATED_IN_MAX_PLY)
                        && !(pos.capture (move))
                        && !(pos.can_castle (pos.active ()));

                    // Don't search moves with negative SEE values
                    if (   (!IN_CHECK || evasion_prunable)
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

                Value value = gives_check
                    ? -search_quien<NT, true > (pos, ss+1, -beta, -alpha, depth - ONE_MOVE)
                    : -search_quien<NT, false> (pos, ss+1, -beta, -alpha, depth - ONE_MOVE);

                pos.undo_move ();

                ASSERT (-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Check for new best move
                if (best_value < value)
                {
                    best_value = value;

                    if (alpha < value)
                    {
                        if (PVNode && (beta > value)) // Update alpha here! Always alpha < beta
                        {
                            alpha = value;
                            best_move = move;
                        }
                        else // Fail high
                        {
                            TT.store (
                                posi_key,
                                move,
                                tt_depth,
                                BND_LOWER,
                                pos.game_nodes (),
                                value_to_tt (value, (ss)->ply),
                                (ss)->static_eval);

                            return value;
                        }
                    }
                }
            }

            // All legal moves have been searched. A special case: If we're in check
            // and no legal moves were found, it is checkmate.
            if (IN_CHECK)
            {
                if (best_value == -VALUE_INFINITE)
                {
                    return mated_in ((ss)->ply); // Plies to mate from the root
                }
            }

            TT.store (
                posi_key,
                best_move,
                tt_depth,
                PVNode && (best_value > old_alpha) ? BND_EXACT : BND_UPPER,
                pos.game_nodes (),
                value_to_tt (best_value, (ss)->ply),
                (ss)->static_eval);

            ASSERT (-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }

        template <NodeT NT>
        // search<>() is the main search function for both PV and non-PV nodes and for
        // normal and SplitPoint nodes. When called just after a splitpoint the search
        // is simpler because we have already probed the hash table, done a null move
        // search, and searched the first move before splitting, we don't have to repeat
        // all this work again. We also don't need to store anything to the hash table
        // here: This is taken care of after we return from the splitpoint.
        inline Value search        (Position &pos, Stack *ss, Value alpha, Value beta, Depth depth, bool cut_node)
        {
            const bool RootNode = (NT == Root             || NT == SplitPointRoot);
            const bool   PVNode = (NT == Root || NT == PV || NT == SplitPointRoot    || NT == SplitPointPV);
            const bool   SPNode = (NT == SplitPointNonPV  || NT == SplitPointRoot    || NT == SplitPointPV);

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

            if (SPNode)
            {
                splitpoint = (ss)->splitpoint;
                best_move  = splitpoint->best_move;
                best_value = splitpoint->best_value;

                tte      = NULL;
                tt_move  = excluded_move = MOVE_NONE;
                tt_value = VALUE_NONE;

                ASSERT (splitpoint->best_value > -VALUE_INFINITE);
                ASSERT (splitpoint->moves_count > 0);

                goto moves_loop;
            }

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
                if (thread->max_ply < (ss)->ply)
                {
                    thread->max_ply = (ss)->ply;
                }
            }

            if (!RootNode)
            {
                // Step 2. Check for aborted search and immediate draw
                if (Signals.stop || pos.draw () || ((ss)->ply > MAX_PLY))
                {
                    return ((ss)->ply > MAX_PLY && !in_check) ? evaluate (pos)
                        : DrawValue[pos.active ()];
                }

                // Step 3. Mate distance pruning. Even if we mate at the next move our score
                // would be at best mates_in((ss)->ply+1), but if alpha is already bigger because
                // a shorter mate was found upward in the tree then there is no need to search
                // further, we will never beat current alpha. Same logic but with reversed signs
                // applies also in the opposite condition of being mated instead of giving mate,
                // in this case return a fail-high score.
                alpha = max (mated_in ((ss)->ply +0), alpha);
                beta  = min (mates_in ((ss)->ply +1), beta);

                if (alpha >= beta)
                {
                    return alpha;
                }
            }

            // Step 4. Transposition table lookup
            // We don't want the score of a partial search to overwrite a previous full search
            // TT value, so we use a different position key in case of an excluded move.
            excluded_move = (ss)->excluded_move;

            posi_key = (excluded_move != MOVE_NONE)
                     ? pos.posi_key_excl ()
                     : pos.posi_key ();

            tte      = TT.retrieve (posi_key);
            tt_move  = (ss)->tt_move = RootNode    ? RootMoves[IndexPV].pv[0]
                                     : tte != NULL ? tte->move ()
                                     : MOVE_NONE;
            tt_value = tte ? value_fr_tt (tte->value (), (ss)->ply)
                     : VALUE_NONE;

            if (!RootNode)
            {
                // At PV nodes we check for exact scores, while at non-PV nodes we check for
                // a fail high/low. Biggest advantage at probing at PV nodes is to have a
                // smooth experience in analysis mode. We don't probe at Root nodes otherwise
                // we should also update RootMoveList to avoid bogus output.
                if (   (tte != NULL)
                    && (tt_value != VALUE_NONE) // Only in case of TT access race
                    && (tte->depth () >= depth)
                    && (        PVNode ?  tte->bound () == BND_EXACT
                    : tt_value >= beta ? (tte->bound () &  BND_LOWER)
                    :                    (tte->bound () &  BND_UPPER))
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
            if (in_check)
            {
                eval = (ss)->static_eval = VALUE_NONE;
                goto moves_loop;
            }

            // Rest of code is skipped when in check
            // -------------------------------------

            if (tte != NULL)
            {
                // Never assume anything on values stored in TT
                Value eval_ = tte->eval ();
                if (VALUE_NONE == eval_) eval_ = evaluate (pos);
                eval = (ss)->static_eval = eval_;

                // Can tt_value be used as a better position evaluation?
                if (VALUE_NONE != tt_value)
                {
                    if (tte->bound () & (tt_value > eval ? BND_LOWER : BND_UPPER))
                    {
                        eval = tt_value;
                    }
                }
            }
            else
            {
                eval = (ss)->static_eval = evaluate (pos);

                TT.store (
                    posi_key,
                    MOVE_NONE,
                    DEPTH_NONE,
                    BND_NONE,
                    pos.game_nodes (),
                    VALUE_NONE,
                    (ss)->static_eval);
            }

            // Updates Gains
            if (   (pos.capture_type () == NONE)
                && ((ss  )->static_eval != VALUE_NONE)
                && ((ss-1)->static_eval != VALUE_NONE)
                && ((move = (ss-1)->current_move) != MOVE_NULL)
                && (mtype (move) == NORMAL)
               )
            {
                Square dst = dst_sq (move);
                Gains.update (pos[dst], dst, -((ss-1)->static_eval + (ss)->static_eval));
            }

            if (!PVNode) // (is omitted in PV nodes)
            {
                // Step 6. Razoring
                if (   (depth < (4*ONE_MOVE))
                    && (abs (beta) < VALUE_MATES_IN_MAX_PLY)
                    && (tt_move == MOVE_NONE)
                    && (!pos.pawn_on_7thR (pos.active ()))
                   )
                {
                    Value ralpha = alpha - razor_margin (depth);
                    if (eval <= ralpha)
                    {
                        Value ver_value = search_quien<NonPV, false> (pos, ss, ralpha, ralpha+1, DEPTH_ZERO);
                        if (ver_value <= ralpha)
                        {
                            return ver_value;
                        }
                    }
                }
            
                // Step 7. Futility pruning: child node
                // We're betting that the opponent doesn't have a move that will reduce
                // the score by more than futility_margin (depth) if we do a null move.
                if (   !((ss)->skip_null_move)
                    && (depth < (7*ONE_MOVE)) // TODO::
                    && (abs (beta) < VALUE_MATES_IN_MAX_PLY)
                    && (abs (eval) < VALUE_KNOWN_WIN)
                    && (pos.non_pawn_material (pos.active ()) != VALUE_ZERO)
                   )
                {
                    Value fut_eval = eval - futility_margin (depth);
                    if (fut_eval >= beta)
                    {
                        return fut_eval;
                    }
                }

                // Step 8. Null move search with verification search
                if (   !((ss)->skip_null_move)
                    && (depth >= (2*ONE_MOVE))
                    && (eval >= beta)
                    && (abs (beta) < VALUE_MATES_IN_MAX_PLY)
                    && (pos.non_pawn_material (pos.active ()) != VALUE_ZERO)
                   )
                {
                    ASSERT (eval >= beta);

                    (ss)->current_move = MOVE_NULL;

                    // Null move dynamic (variable) reduction based on depth and value
                    Depth R = Depth (
                            + (3*ONE_MOVE)
                            + (depth>>2)
                            + ((i32 (eval - beta) / VALUE_EG_PAWN)*ONE_MOVE));

                    // Do null move
                    pos.do_null_move (si);
                    (ss+1)->skip_null_move = true;

                    // Null window (alpha, beta) = (beta-1, beta):
                    Value null_value = (depth-R < ONE_MOVE)
                        ? -search_quien<NonPV, false> (pos, ss+1, -beta, -(beta-1), DEPTH_ZERO)
                        : -search      <NonPV       > (pos, ss+1, -beta, -(beta-1), depth-R, !cut_node);

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
                        if (depth < (12*ONE_MOVE))
                        {
                            return null_value;
                        }

                        // Do verification search at high depths
                        (ss)->skip_null_move = true;
                        Value veri_value = (depth-R < ONE_MOVE)
                            ? search_quien<NonPV, false> (pos, ss, beta-1, beta, DEPTH_ZERO)
                            : search      <NonPV       > (pos, ss, beta-1, beta, depth-R, false); // TODO::
                        (ss)->skip_null_move = false;
                        if (veri_value >= beta)
                        {
                            return null_value;
                        }
                    }
                }

                // Step 9. ProbCut
                // If we have a very good capture (i.e. SEE > see[captured_piece_type])
                // and a reduced search returns a value much above beta,
                // we can (almost) safely prune the previous move.
                if (   (depth >= (5*ONE_MOVE))
                    && !((ss)->skip_null_move)
                    //&& (eval >= alpha + 50) // TODO::
                    && (abs (beta) < VALUE_MATES_IN_MAX_PLY)
                   )
                {
                    Value rbeta  = beta + 200;
                    if (rbeta > VALUE_INFINITE)
                    {
                        rbeta = VALUE_INFINITE;
                    }

                    Depth rdepth = depth - (4*ONE_MOVE);

                    ASSERT (rdepth >= ONE_MOVE);
                    ASSERT ((ss-1)->current_move != MOVE_NONE);
                    ASSERT ((ss-1)->current_move != MOVE_NULL);

                    // Initialize a MovePicker object for the current position,
                    // and prepare to search the moves.
                    MovePicker mp (pos, History, tt_move, pos.capture_type ());

                    while ((move = mp.next_move<false> ()) != MOVE_NONE)
                    {
                        if (!pos.legal (move, ci.pinneds)) continue;

                        (ss)->current_move = move;
                        pos.do_move (move, si, pos.gives_check (move, ci) ? &ci : NULL);
                        Value value = -search<NonPV> (pos, ss+1, -rbeta, -(rbeta-1), rdepth, !cut_node);
                        pos.undo_move ();
                        if (value >= rbeta)
                        {
                            return value;
                        }
                    }
                }

            }

            // Step 10. Internal iterative deepening (skipped when in check)
            if (   (depth >= ((PVNode ? 5 : 8)*ONE_MOVE))
                && (tt_move == MOVE_NONE)
                && (PVNode || (ss)->static_eval + Value (256) >= beta)
               )
            {
                Depth d = depth - Depth ((2*ONE_MOVE) + (PVNode ? 0 : depth>>2)); // TODO::

                (ss)->skip_null_move = true;
                search<PVNode ? PV : NonPV> (pos, ss, alpha, beta, d, true);
                (ss)->skip_null_move = false;

                tte = TT.retrieve (posi_key);
                tt_move = tte ? tte->move () : MOVE_NONE;
            }

        moves_loop: // When in check and at SPNode search starts from here

            Square opp_move_sq = dst_sq ((ss-1)->current_move);
            Move cm[CLR_NO] =
            {
                CounterMoves[pos[opp_move_sq]][opp_move_sq].first,
                CounterMoves[pos[opp_move_sq]][opp_move_sq].second,
            };

            Square own_move_sq = dst_sq ((ss-2)->current_move);
            Move fm[CLR_NO] =
            {
                FollowupMoves[pos[own_move_sq]][own_move_sq].first,
                FollowupMoves[pos[own_move_sq]][own_move_sq].second,
            };

            MovePicker mp (pos, History, tt_move, depth, cm, fm, ss);

            Value value = best_value; // Workaround a bogus 'uninitialized' warning under gcc

            bool improving =
                (  ((ss  )->static_eval >= (ss-2)->static_eval)
                || ((ss  )->static_eval == VALUE_NONE)
                || ((ss-2)->static_eval == VALUE_NONE)
                );

            bool singular_ext_node =
                (  (!RootNode && !SPNode)
                && (depth >= (8*ONE_MOVE))
                && (tt_move != MOVE_NONE)
                && (excluded_move == MOVE_NONE) // Recursive singular search is not allowed
                && (tte->bound () & BND_LOWER)
                && (tte->depth () >= depth - (3*ONE_MOVE))
                );

            point elapsed;

            if (RootNode)
            {
                if (Threadpool.main () == thread)
                {
                    elapsed = now () - SearchTime;
                    if (elapsed > InfoDuration)
                    {
                        sync_cout
                            << "info"
                            << " depth " << u16 (depth>>ONE_PLY)
                            << " time "  << elapsed
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

                // At root obey the "searchmoves" option and skip moves not listed in Root
                // Move List, as a consequence any illegal move is also skipped. In MultiPV
                // mode we also skip PV moves which have been already searched.
                if (RootNode)
                {
                    if (!count (RootMoves.begin () + IndexPV, RootMoves.end (), move)) continue;
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

                if (RootNode)
                {
                    Signals.root_1stmove = (1 == moves_count);

                    if (Threadpool.main () == thread)
                    {
                        elapsed = now () - SearchTime;
                        if (elapsed > InfoDuration)
                        {
                            sync_cout
                                << "info"
                                //<< " depth "          << u16 (depth>>ONE_PLY)
                                << " currmovenumber " << setw (2) << u16 (moves_count + IndexPV)
                                << " currmove "       << move_to_can (move, pos.chess960 ())
                                << " time "           << elapsed
                                << sync_endl;
                        }
                    }
                }

                Depth ext = DEPTH_ZERO;

                bool capture_or_promotion = pos.capture_or_promotion (move);

                bool gives_check= pos.gives_check (move, ci);
                    //  ((NORMAL == mtype (move)) && ci.discoverers == U64 (0))
                    //? (ci.checking_bb[ptype (pos[org_sq (move)])] & dst_sq (move))
                    //: (pos.gives_check (move, ci));

                bool dangerous  = 
                    (  (gives_check)
                    || (NORMAL != mtype (move))
                    || (pos.advanced_pawn_push (move)));

                // Step 12. Extend checks
                if (gives_check && pos.see_sign (move) >= VALUE_ZERO)
                {
                    ext = ONE_MOVE;
                }

                // Singular extension(SE) search. If all moves but one fail low on a search of
                // (alpha-s, beta-s), and just one fails high on (alpha, beta), then that move
                // is singular and should be extended. To verify this we do a reduced search
                // on all the other moves but the tt_move, if result is lower than tt_value minus
                // a margin then we extend tt_move.
                if (   (singular_ext_node)
                    && (move == tt_move)
                    && (ext == DEPTH_ZERO)
                    && (pos.legal (move, ci.pinneds))
                    && (abs (tt_value) < VALUE_KNOWN_WIN)
                   )
                {
                    ASSERT (tt_value != VALUE_NONE);

                    Value rbeta = tt_value - i32 (depth + (depth>>2)); // TODO::depth

                    (ss)->excluded_move  = move;
                    (ss)->skip_null_move = true;
                    value = search<NonPV> (pos, ss, rbeta-1, rbeta, Depth (depth>>1), cut_node);
                    (ss)->skip_null_move = false;
                    (ss)->excluded_move  = MOVE_NONE;

                    if (value < rbeta)
                    {
                        ext = ONE_MOVE;
                    }
                }

                // Update current move (this must be done after singular extension search)
                Depth new_depth = depth - ONE_MOVE + ext;

                if (!PVNode)
                {
                    // Step 13. Pruning at shallow depth (exclude PV nodes)
                    if (   !(capture_or_promotion)
                        && !(in_check)
                        && !(dangerous)
                     // && (move != tt_move) Already implicit in the next condition
                        && (best_value > VALUE_MATED_IN_MAX_PLY)
                       )
                    {
                        // Move count based pruning
                        if (   (depth < (16*ONE_MOVE))
                            && (moves_count >= FutilityMoveCounts[improving][depth])
                           )
                        {
                            if (SPNode)
                            {
                                splitpoint->mutex.lock ();
                            }
                            continue;
                        }

                        // Value based pruning
                        // We illogically ignore reduction condition depth >= (3*ONE_MOVE) for predicted depth,
                        // but fixing this made program slightly weaker.
                        Depth predicted_depth = new_depth - reduction<PVNode> (improving, depth, moves_count);

                        // Futility pruning: parent node
                        if (predicted_depth < (7*ONE_MOVE))
                        {
                            Value futility_value = (ss)->static_eval + futility_margin (predicted_depth)
                                                 + Gains[pos[org_sq (move)]][dst_sq (move)] + Value (128);

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
                            if (SPNode)
                            {
                                splitpoint->mutex.lock ();
                            }
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

                bool is_pv_move = PVNode && (1 == moves_count);
                (ss)->current_move = move;

                // Step 14. Make the move
                pos.do_move (move, si, gives_check ? &ci : NULL);

                bool full_depth_search;

                // Step 15. Reduced depth search (LMR).
                // If the move fails high will be re-searched at full depth.
                if (   !(is_pv_move)
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
                        (ss)->reduction += ONE_MOVE; // (3*ONE_MOVE)>>2; // TODO::
                    }
                    else if (History[pos[dst_sq (move)]][dst_sq (move)] < VALUE_ZERO)
                    {
                        (ss)->reduction += ONE_PLY;
                    }

                    if (move == cm[0] || move == cm[1])
                    {
                        (ss)->reduction = max (DEPTH_ZERO, (ss)->reduction - ONE_MOVE);
                    }

                    // TODO::
                    Depth red_depth = new_depth - (ss)->reduction;

                    if (SPNode)
                    {
                        alpha = splitpoint->alpha;
                    }

                    value = (red_depth < ONE_MOVE)
                        ? (gives_check
                        ? -search_quien<NonPV, true > (pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO)
                        : -search_quien<NonPV, false> (pos, ss+1, -(alpha+1), -alpha, DEPTH_ZERO))
                        : -search      <NonPV       > (pos, ss+1, -(alpha+1), -alpha, red_depth, true);

                    // Research at intermediate depth if reduction is very high
                    if ((value > alpha) && ((ss)->reduction >= (4*ONE_MOVE)))
                    {
                        Depth inter_depth = max (new_depth - (2*ONE_MOVE), ONE_MOVE);
                        value = -search<NonPV> (pos, ss+1, -(alpha+1), -alpha, inter_depth, true);
                    }

                    full_depth_search = ((value > alpha) && ((ss)->reduction != DEPTH_ZERO));
                    (ss)->reduction = DEPTH_ZERO;
                }
                else
                {
                    full_depth_search = !is_pv_move;
                }

                // Step 16. Full depth search, when LMR is skipped or fails high
                if (full_depth_search)
                {
                    if (SPNode)
                    {
                        alpha = splitpoint->alpha;
                    }

                    value =
                        (new_depth < ONE_MOVE)
                        ? (gives_check
                        ? -search_quien<NonPV, true > (pos, ss+1, -(alpha+1), -(alpha), DEPTH_ZERO)
                        : -search_quien<NonPV, false> (pos, ss+1, -(alpha+1), -(alpha), DEPTH_ZERO))
                        : -search      <NonPV       > (pos, ss+1, -(alpha+1), -(alpha), new_depth, !cut_node);
                }

                // Principal Variation Search
                if (PVNode)
                {
                    // For PV nodes only, do a full PV search on the first move or after a fail
                    // high (in the latter case search only if value < beta), otherwise let the
                    // parent node fail low with value <= alpha and to try another move.
                    if (is_pv_move || ((value > alpha) && (RootNode || (value < beta))))
                    {
                        value =
                            (new_depth < ONE_MOVE)
                            ? (gives_check
                            ? -search_quien<PV, true > (pos, ss+1, -beta, -alpha, DEPTH_ZERO)
                            : -search_quien<PV, false> (pos, ss+1, -beta, -alpha, DEPTH_ZERO))
                            : -search      <PV       > (pos, ss+1, -beta, -alpha, new_depth, false);
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

                // Finished searching the move. If Signals.stop is true, the search
                // was aborted because the user interrupted the search or because we
                // ran out of time. In this case, the return value of the search cannot
                // be trusted, and we don't update the best move and/or PV.
                if (Signals.stop || thread->cutoff_occurred ())
                {
                    return value; // To avoid returning VALUE_INFINITE
                }

                if (RootNode)
                {
                    RootMove &rm = *find (RootMoves.begin (), RootMoves.end (), move);

                    // PV move or new best move ?
                    if (is_pv_move || value > alpha)
                    {
                        rm.value[0] = value;
                        //rm.nodes = pos.game_nodes ();
                        rm.extract_pv_from_tt (pos);

                        // We record how often the best move has been changed in each
                        // iteration. This information is used for time management:
                        // When the best move changes frequently, we allocate some more time.
                        if (!is_pv_move) // (value > alpha)
                        {
                            ++BestMoveChanges;
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

                if (value > best_value)
                {
                    best_value = (SPNode) ? splitpoint->best_value = value : value;

                    if (value > alpha)
                    {
                        best_move = (SPNode) ? splitpoint->best_move = move : move;

                        if (PVNode && (value < beta)) // Update alpha! Always alpha < beta
                        {
                            alpha = (SPNode) ? splitpoint->alpha = value : value;
                        }
                        else
                        {
                            ASSERT (value >= beta); // Fail high

                            if (SPNode)
                            {
                                splitpoint->cut_off = true;
                            }
                            break;
                        }
                    }
                }

                // Not Split Point node
                // Step 19. Check for splitting the search
                if (!SPNode)
                {
                    if (   (Threadpool.split_depth <= depth)
                        && (Threadpool.available_slave (thread) != NULL)
                        && (thread->splitpoint_threads < MAX_SPLITPOINT_THREADS)
                       )
                    {
                        ASSERT (best_value < beta);

                        thread->split<FakeSplit> (pos, ss, alpha, beta, best_value, best_move, depth, moves_count, mp, NT, cut_node);

                        if (best_value >= beta)
                        {
                            break;
                        }
                    }
                }
            }

            if (!SPNode)
            {
                // Step 20. Check for mate and stalemate
                // All legal moves have been searched and if there are no legal moves, it
                // must be mate or stalemate. Note that we can have a false positive in
                // case of Signals.stop or thread.cutoff_occurred() are set, but this is
                // harmless because return value is discarded anyhow in the parent nodes.
                // If we are in a singular extension search then return a fail low score.
                // A split node has at least one move, the one tried before to be split.
                if (0 == moves_count)
                {
                    return (excluded_move != MOVE_NONE) ? alpha
                        : in_check ? mated_in ((ss)->ply)
                        : DrawValue[pos.active ()];
                }

                // If we have pruned all the moves without searching return a fail-low score
                if (best_value == -VALUE_INFINITE)
                {
                    best_value = alpha;
                }

                TT.store (
                    posi_key,
                    best_move,
                    depth,
                    best_value >= beta ? BND_LOWER : PVNode && best_move ? BND_EXACT : BND_UPPER,
                    pos.game_nodes (),
                    value_to_tt (best_value, (ss)->ply),
                    (ss)->static_eval);

                // Quiet best move:
                if ((best_value >= beta) && (best_move != MOVE_NONE))
                {
                    // Update history, killer, counter & followup moves
                    if (!in_check && !pos.capture_or_promotion (best_move))
                    {
                        update_stats (pos, ss, best_move, depth, quiet_moves, quiets_count);
                    }
                }
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
            (ss-1)->current_move = MOVE_NULL; // Hack to skip update gains

            TT.new_gen ();

            Gains.clear ();
            History.clear ();
            CounterMoves.clear ();
            FollowupMoves.clear ();

            BestMoveChanges  = 0.0;

            Value best_value = -VALUE_INFINITE
                , alpha      = -VALUE_INFINITE
                , beta       = +VALUE_INFINITE
                , window     =  VALUE_ZERO;

            i32 depth    =  DEPTH_ZERO;

            MultiPV   = i32 (Options["MultiPV"]);
            u08 level = i32 (Options["Skill Level"]);

            Skill skill (level);

            // Do we have to play with skill handicap?
            // In this case enable MultiPV search to MIN_SKILL_MULTIPV
            // that we will use behind the scenes to retrieve a set of possible moves.
            if (skill.enabled ())
            {
                if (MultiPV < MIN_SKILL_MULTIPV)
                {
                    MultiPV = MIN_SKILL_MULTIPV;
                }
            }

            if (MultiPV > RootMoves.size ())
            {
                MultiPV = RootMoves.size ();
            }

            // Iterative deepening loop until requested to stop or target depth reached
            while (++depth <= MAX_PLY && !Signals.stop && (Limits.depth == 0 || depth <= Limits.depth))
            {
                // Age out PV variability metric
                BestMoveChanges *= 0.5;

                // Save last iteration's scores before first PV line is searched and all
                // the move scores but the (new) PV are set to -VALUE_INFINITE.
                for (u08 i = 0; i < RootMoves.size (); ++i)
                {
                    RootMoves[i].value[1] = RootMoves[i].value[0];
                }
                // MultiPV loop. We perform a full root search for each PV line
                for (IndexPV = 0; (IndexPV < MultiPV) && !Signals.stop; ++IndexPV)
                {
                    // Reset Aspiration window starting size
                    if (depth > 4)
                    {
                        window = Value (depth < 48 ? 14 + (depth>>3) : 20);

                        alpha  = max (RootMoves[IndexPV].value[1] - window, -VALUE_INFINITE);
                        beta   = min (RootMoves[IndexPV].value[1] + window, +VALUE_INFINITE);
                    }

                    point elapsed;

                    // Start with a small aspiration window and, in case of fail high/low,
                    // research with bigger window until not failing high/low anymore.
                    do
                    {
                        best_value = search<Root> (pos, ss, alpha, beta, depth*ONE_MOVE, false);

                        Value contempt = Contempt[best_value > VALUE_DRAW];
                        DrawValue[ RootColor] = VALUE_DRAW - contempt;
                        DrawValue[~RootColor] = VALUE_DRAW + contempt;

                        // Bring to front the best move. It is critical that sorting is
                        // done with a stable algorithm because all the values but the first
                        // and eventually the new best one are set to -VALUE_INFINITE and
                        // we want to keep the same order for all the moves but the new
                        // PV that goes to the front. Note that in case of MultiPV search
                        // the already searched PV lines are preserved.
                        stable_sort (RootMoves.begin () + IndexPV, RootMoves.end ());

                        // Write PV back to transposition table in case the relevant
                        // entries have been overwritten during the search.
                        for (u08 i = 0; i <= IndexPV; ++i)
                        {
                            RootMoves[i].insert_pv_into_tt (pos);
                        }

                        // If search has been stopped break immediately. Sorting and
                        // writing PV back to TT is safe becuase RootMoves is still
                        // valid, although refers to previous iteration.
                        if (Signals.stop) break;

                        // When failing high/low give some update
                        // (without cluttering the UI) before to research.
                        if (   ((alpha >= best_value) || (best_value >= beta))
                            && ((elapsed = now () - SearchTime) > InfoDuration)
                           )
                        {
                            sync_cout << info_pv (pos, depth, alpha, beta, elapsed) << sync_endl;
                        }

                        // In case of failing low/high increase aspiration window and
                        // research, otherwise exit the loop.
                        if      (best_value <= alpha)
                        {
                            alpha = max (best_value - window, -VALUE_INFINITE);

                            Signals.root_failedlow = true;
                            Signals.stop_ponderhit = false;
                        }
                        else if (best_value >= beta)
                        {
                            beta  = min (best_value + window, +VALUE_INFINITE);
                        }
                        else
                        {
                            break;
                        }

                        window *= 1.5;

                        ASSERT (-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
                    }
                    while (alpha < beta);

                    // Sort the PV lines searched so far and update the GUI
                    stable_sort (RootMoves.begin (), RootMoves.begin () + IndexPV + 1);
                    
                    elapsed = now () - SearchTime;
                    if ((IndexPV + 1) == MultiPV || (elapsed > InfoDuration))
                    {
                        sync_cout << info_pv (pos, depth, alpha, beta, elapsed) << sync_endl;
                    }
                }

                // If skill levels are enabled and time is up, pick a sub-optimal best move
                if (skill.enabled () && skill.time_to_pick (depth))
                {
                    skill.pick_move ();
                    if (MOVE_NONE != skill.move)
                    {
                        swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), skill.move));
                    }
                }

                bool write_search_log = bool (Options["Write Search Log"]);
                if (write_search_log)
                {
                    string search_log_fn = string (Options["Search Log File"]);
                    LogFile log (search_log_fn);
                    log << pretty_pv (pos, depth, RootMoves[0].value[0], now () - SearchTime, &RootMoves[0].pv[0]) << endl;
                }

                // Have found a "mate in x"?
                if (   (Limits.mate != 0)
                    && (best_value >= VALUE_MATES_IN_MAX_PLY)
                    && (VALUE_MATE - best_value <= (Limits.mate*2))
                   )
                {
                    Signals.stop = true;
                }

                // Do we have time for the next iteration? Can we stop searching now?
                if (   Limits.use_timemanager ()
                    && !Signals.stop
                    && !Signals.stop_ponderhit
                   )
                {
                    // Take in account some extra time if the best move has changed
                    if (   (4 < depth && depth < 50)
                        && (1 == MultiPV)
                       )
                    {
                        TimeMgr.pv_instability (BestMoveChanges);
                    }

                    // Stop the search early:
                    // If there is only one legal move available or 
                    // If all of the available time has been used.
                    if (   (RootMoves.size () == 1)
                        || ((now () - SearchTime) > TimeMgr.available_time ())
                       )
                    {
                        // If we are allowed to ponder do not stop the search now but
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

    vector<RootMove>    RootMoves;
    Position            RootPos;
    Color               RootColor;
    StateInfoStackPtr   SetupStates;

    Time::point         SearchTime;

    // initialize the PRNG only once
    PolyglotBook        Book;

    // RootMove::extract_pv_from_tt() builds a PV by adding moves from the TT table.
    // We consider also failing high nodes and not only EXACT nodes so to
    // allow to always have a ponder move even when we fail high at root node.
    // This results in a long PV to print that is important for position analysis.
    void RootMove::extract_pv_from_tt (Position &pos)
    {
        i08 ply = 0;
        Move m = pv[ply];
        pv.clear ();
        StateInfo states[MAX_PLY_6]
                , *si = states;

        const TTEntry *tte;
        do
        {
            pv.push_back (m);

            ASSERT (MoveList<LEGAL> (pos).contains (pv[ply]));

            pos.do_move (pv[ply++], *si++);
            tte = TT.retrieve (pos.posi_key ());

        }
        while (tte // Local copy, TT could change
            && (ply < MAX_PLY)
            && (m = tte->move ()) != MOVE_NONE
            && pos.pseudo_legal (m)
            && pos.legal (m)
            && (!pos.draw () || ply < 2));

        pv.push_back (MOVE_NONE); // Must be zero-terminating

        do
        {
            pos.undo_move ();
            --ply;
        }
        while (ply != 0);
    }
    // RootMove::insert_pv_in_tt() is called at the end of a search iteration, and
    // inserts the PV back into the TT. This makes sure the old PV moves are searched
    // first, even if the old TT entries have been overwritten.
    void RootMove::insert_pv_into_tt (Position &pos)
    {
        i08 ply = 0;
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
                    pos.game_nodes (),
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

    u64 perft (Position &pos, const Depth &depth)
    {
        return (depth > ONE_MOVE) ? _perft (pos, depth) : MoveList<LEGAL> (pos).size ();
    }

    void think ()
    {
        TimeMgr.initialize (Limits, RootPos.game_ply (), RootColor);

        DrawValue[WHITE] = VALUE_DRAW;
        DrawValue[BLACK] = VALUE_DRAW;
        i32 contempt = i32 (Options["Contempt Factor"]);
        Contempt[0] = (contempt+ 0) * VALUE_EG_PAWN / 100;
        Contempt[1] = (contempt+12) * VALUE_EG_PAWN / 100;

        bool write_search_log = bool (Options["Write Search Log"]);
        string search_log_fn  = string (Options["Search Log File"]);

        if (RootMoves.empty ())
        {
            RootMoves.push_back (RootMove (MOVE_NONE));
            sync_cout
                << "info"
                << " depth " << 0
                << " score " << score_uci (RootPos.checkers () ? -VALUE_MATE : VALUE_DRAW)
                << sync_endl;

            goto finish;
        }

        if (!Limits.infinite && Limits.mate == 0 && bool (Options["Own Book"]))
        {
            if (!Book.is_open ())
            {
                Book.open (string (Options["Book File"]), ios_base::in|ios_base::binary);
            }
            Move book_move = Book.probe_move (RootPos, bool (Options["Best Book Move"]));
            if (   book_move != MOVE_NONE
                && count (RootMoves.begin (), RootMoves.end (), book_move))
            {
                swap (RootMoves[0], *find (RootMoves.begin (), RootMoves.end (), book_move));
                goto finish;
            }
        }
        
        if (write_search_log)
        {
            LogFile log (search_log_fn);

            log << "----------->\n" << boolalpha
                << "fen:       " << RootPos.fen ()                   << "\n"
                << "infinite:  " << Limits.infinite                  << "\n"
                << "ponder:    " << Limits.ponder                    << "\n"
                << "time:      " << Limits.gameclock[RootColor].time << "\n"
                << "increment: " << Limits.gameclock[RootColor].inc  << "\n"
                << "movetime:  " << Limits.movetime                  << "\n"
                << "movestogo: " << u16 (Limits.movestogo)           << "\n"
                << "  d   score   time    nodes  pv\n"
                << "-----------------------------------------------------------"
                << endl;
        }

        // Reset the threads, still sleeping: will wake up at split time
        for (u08 t = 0; t < Threadpool.size (); ++t)
        {
            Threadpool[t]->max_ply = 0;
        }

        Threadpool.idle_sleep = bool (Options["Idle Threads Sleep"]);
        Threadpool.timer->start ();

        Threadpool.timer->notify_one ();// Wake up the recurring timer
        iter_deep_loop (RootPos);       // Let's start searching !

        Threadpool.timer->stop ();
        Threadpool.idle_sleep = true;   // Send idle threads to sleep

        if (write_search_log)
        {
            LogFile log (search_log_fn);

            point elapsed = now () - SearchTime;
            if (elapsed == 0) elapsed = 1;

            log << "Time:        " << elapsed                                   << "\n"
                << "Nodes:       " << RootPos.game_nodes ()                     << "\n"
                << "Nodes/sec.:  " << RootPos.game_nodes () * M_SEC / elapsed   << "\n"
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

    finish:
        
        point elapsed = now () - SearchTime;
        if (elapsed == 0) elapsed = 1;

        // When search is stopped this info is not printed
        sync_cout
            << "info"
            << " time "     << elapsed
            << " nodes "    << RootPos.game_nodes ()
            << " nps "      << RootPos.game_nodes () * M_SEC / elapsed
            << " hashfull " << TT.permill_full ()
            << sync_endl;

        // When we reach max depth we arrive here even without Signals.stop is raised,
        // but if we are pondering or in infinite search, according to UCI protocol,
        // we shouldn't print the best move before the GUI sends a "stop" or "ponderhit" command.
        // We simply wait here until GUI sends one of those commands (that raise Signals.stop).
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
        // Init reductions array
        for (u08 hd = 1; hd < 64; ++hd) // half-depth (ONE_PLY == 1)
        {
            for (u08 mc = 1; mc < 64; ++mc) // move-count
            {
                double     pv_red = 0.00 + log (double (hd)) * log (double (mc)) / 3.00;
                double non_pv_red = 0.33 + log (double (hd)) * log (double (mc)) / 2.25;
                Reductions[1][1][hd][mc] =     pv_red >= 1.0 ? floor (    pv_red * i32 (ONE_MOVE)) : 0;
                Reductions[0][1][hd][mc] = non_pv_red >= 1.0 ? floor (non_pv_red * i32 (ONE_MOVE)) : 0;

                Reductions[1][0][hd][mc] = Reductions[1][1][hd][mc];
                Reductions[0][0][hd][mc] = Reductions[0][1][hd][mc];
                // Smoother transition for LMR
                if      (Reductions[0][0][hd][mc] > (2*ONE_MOVE))
                {
                    Reductions[0][0][hd][mc] += ONE_MOVE;
                }
                else if (Reductions[0][0][hd][mc] > (1*ONE_MOVE))
                {
                    Reductions[0][0][hd][mc] += ONE_PLY;
                }
            }
        }

        // Init futility move count array
        for (u08 d = 0; d < 32; ++d)    // depth (ONE_MOVE == 2)
        {
            FutilityMoveCounts[0][d] = 2.40 + 0.222 * pow (d + 0.00, 1.80);
            FutilityMoveCounts[1][d] = 3.00 + 0.300 * pow (d + 0.98, 1.80);
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

        if (Limits.ponder)
        {
            return;
        }

        u64 nodes = 0;
        if (Limits.nodes != 0)
        {
            Threadpool.mutex.lock ();

            nodes = RootPos.game_nodes ();
            // Loop across all splitpoints and sum accumulated SplitPoint nodes plus
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

        point elapsed = now_time - SearchTime;

        bool still_at_1stmove =
                (Signals.root_1stmove)
            && !(Signals.root_failedlow)
            && (elapsed > TimeMgr.available_time () * (BestMoveChanges < 1.0e-4 ? 2 : 3)>>2); // TODO::

        bool no_more_time =
               (elapsed > TimeMgr.maximum_time () - 2 * TimerThread::Resolution)
            || (still_at_1stmove);

        if (   (Limits.use_timemanager () && no_more_time)
            || (Limits.movetime != 0 && (elapsed >= Limits.movetime))
            || (Limits.nodes    != 0 && (nodes   >= Limits.nodes))
           )
        {
            Signals.stop = true;
        }
    }

    // Thread::idle_loop() is where the thread is parked when it has no work to do
    void Thread::idle_loop ()
    {
        // Pointer 'splitpoint' is not null only if we are called from split<>(), and not
        // at the thread creation. So it means we are the splitpoint's master.
        SplitPoint *splitpoint = ((splitpoint_threads != 0) ? active_splitpoint : NULL);
        ASSERT ((splitpoint == NULL) || ((splitpoint->master == this) && searching));

        do
        {
            // If we are not searching, wait for a condition to be signaled instead of
            // wasting CPU time polling for work.
            while ((!searching && Threadpool.idle_sleep) || exit)
            {
                if (exit)
                {
                    ASSERT (splitpoint == NULL);
                    return;
                }

                // Grab the lock to avoid races with Thread::notify_one ()
                mutex.lock ();

                // If we are master and all slaves have finished then exit idle_loop
                if (splitpoint != NULL && splitpoint->slaves_mask.none ())
                {
                    mutex.unlock ();
                    break;
                }

                // Do sleep after retesting sleep conditions under lock protection, in
                // particular we need to avoid a deadlock in case a master thread has,
                // in the meanwhile, allocated us and sent the notify_one () call before
                // we had the chance to grab the lock.
                if (!searching && !exit)
                {
                    sleep_condition.wait (mutex);
                }

                mutex.unlock ();
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
                case Root:  search<SplitPointRoot > (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
                case PV:    search<SplitPointPV   > (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
                case NonPV: search<SplitPointNonPV> (pos, ss, (sp)->alpha, (sp)->beta, (sp)->depth, (sp)->cut_node); break;
                default: ASSERT (false);
                }

                ASSERT (searching);

                searching  = false;
                active_pos = NULL;
                (sp)->slaves_mask.reset (idx);
                (sp)->nodes += pos.game_nodes ();

                // Wake up master thread so to allow it to return from the idle loop
                // in case we are the last slave of the splitpoint.
                if (   Threadpool.idle_sleep
                    && (this != (sp)->master)
                    && (sp)->slaves_mask.none ())
                {
                    ASSERT (!(sp)->master->searching);
                    (sp)->master->notify_one ();
                }

                // After releasing the lock we cannot access anymore any splitpoint
                // related data in a safe way becuase it could have been released under
                // our feet by the sp master. Also accessing other Thread objects is
                // unsafe because if we are exiting there is a chance are already freed.
                (sp)->mutex.unlock ();
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
