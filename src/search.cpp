/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "search.h"

#include <chrono>
#include <list>
#include <random>
#include <ratio>

#include "attacks.h"
#include "bitboard.h"
#include "evaluate.h"
#include "movegen.h"
#include "movepick.h"
#include "option.h"
#include "prng.h"
#include "thread.h"
#include "tt.h"
#include "nnue/network.h"

namespace DON {

// (*Scaler):
// Search features marked by "(*Scaler)" have proven non-linear scaling.
// They are optimized to time controls of 180 + 1.8 and longer,
// so changing them or adding conditions that are similar requires
// tests at these types of time controls.

namespace {

constexpr Depth OutputLimitDepth = 30;

constexpr Array<int, 16> LMRDivisor{
  3637, 2787, 2761, 2939, 3171, 3347, 3147, 2762,  //
  2772, 3106, 3107, 3060, 3112, 2991, 3090, 3542   //
};

// Reductions lookup table using [depth or moveCount]
alignas(CACHE_LINE_SIZE) constexpr auto Reductions = []() constexpr noexcept {
    Array<u16, MOVE_MAX> reductions{};

    reductions[0] = 0;
    for (usize i = 1; i < reductions.size(); ++i)
        reductions[i] = u16(22.4375 * constexpr_log(double(i)));

    return reductions;
}();

constexpr int reduction(const Depth depth,
                        const u16   moveCount,
                        const int   deltaRatio,
                        const bool  improve) noexcept {
    int reductionScale = Reductions[depth] * Reductions[moveCount];
    return std::max(982 + reductionScale - deltaRatio
                      + int(!improve) * int(0.384765625 * double(reductionScale)),
                    0);
}

// Add a small random value to draw evaluation to avoid 3-fold blindness
constexpr Value draw_value(const Key key, const u64 nodes) noexcept {
    return VALUE_DRAW + Value(key & 1) - Value(nodes & 1);
}

// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
constexpr Value value_to_tt(const Value v, const i16 ply) noexcept {
    return is_win(v) ? v + ply : is_loss(v) ? v - ply : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated from
// current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, return the highest non-TB score instead.
constexpr Value value_from_tt(const Value v, const i16 ply, const i16 rule50Count) noexcept {

    if (!is_valid(v))
        return v;

    // Handle TB win or better
    if (is_win(v))
    {
        // Downgrade a potentially false mate value
        if (is_mate_win(v) && VALUE_MATE - v > 2 * Position::DrawMoveCount - rule50Count)
            return VALUE_TB_WIN_IN_PLY_MAX - 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB - v > 2 * Position::DrawMoveCount - rule50Count)
            return VALUE_TB_WIN_IN_PLY_MAX - 1;

        return v - ply;
    }
    // Handle TB loss or worse
    if (is_loss(v))
    {
        // Downgrade a potentially false mate value
        if (is_mate_loss(v) && VALUE_MATE + v > 2 * Position::DrawMoveCount - rule50Count)
            return VALUE_TB_LOSS_IN_PLY_MAX + 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB + v > 2 * Position::DrawMoveCount - rule50Count)
            return VALUE_TB_LOSS_IN_PLY_MAX + 1;

        return v + ply;
    }

    return v;
}

constexpr Value blend_values(const Value bestValue,
                             const Value targetValue,
                             const i32   bestWeight,
                             const i32   totalWeight) noexcept {
    return (bestWeight * bestValue + (totalWeight - bestWeight) * targetValue) / totalWeight;
}

constexpr Bound fail_bound(bool failHigh) noexcept {
    return failHigh ? Bound::LOWER : Bound::UPPER;
}

Move legal_move(const Move m, const Position& pos) noexcept {
    return m != Move::None && pos.legal(m) ? m : Move::None;
}

// Build contHistory pointers from the stack frame and validate them in debug builds.
void build_continuation_histories(
  const Stack* ss, const History<HType::PIECE_SQ>* contHistory[CONT_HISTORY_COUNT]) noexcept {
    for (usize i = 0; i < CONT_HISTORY_COUNT; ++i)
    {
        const Stack* ssi = (ss - 1) - i;

        // contHistory[i] refers to ssi->pieceSqHistory
        contHistory[i] = ssi->pieceSqHistory;
        assert(contHistory[i] != nullptr && "continuation history pointer must not be null");
    }
}

// Updates the continuation histories for the move pairs formed
// by the current move and the moves played in previous plies.
void update_continuation_histories(Stack* ss, Piece pc, Square dstSq, int bonus) noexcept {
    assert(dstSq != SQ_NONE);

    constexpr Array<double, CONT_HISTORY_COUNT> ContHistoryWeights{
      1.0801, 0.6885, 0.3085, 0.5585, 0.1231, 0.41699, 0.1092, 0.2167  //
    };
    constexpr Array<int, CONT_HISTORY_COUNT> Multipliers{
      94, 103, 110, 106, 119, 121, 126, 128  //
    };
    constexpr Array<int, CONT_HISTORY_COUNT> ContHistoryOffsets{
      73, 00, 00, 00, 00, 00, 00, 00  //
    };

    // In check only update 2-ply continuation history
    usize ContHistoryCount = ss->inCheck ? 2 : CONT_HISTORY_COUNT;

    int positiveCount = 0;

    for (usize i = 0; i < ContHistoryCount; ++i)
    {
        Stack* ssi = (ss - 1) - i;

        if (!ssi->move.is_ok())
            break;

        auto&      historyEntry = (*ssi->pieceSqHistory)[+pc][dstSq];
        const bool positiveHist = historyEntry > 0;

        historyEntry << constexpr_round(ContHistoryWeights[i] * Multipliers[positiveCount] / 131072
                                        * double(bonus))
                          + ContHistoryOffsets[i];

        if (positiveHist)
            ++positiveCount;
    }
}

void update_pawn_history(PawnHistory& pawnHistory,
                         const Piece  movedPc,
                         const Square dstSq,
                         const int    bonus) noexcept {
    pawnHistory[+movedPc][dstSq] << bonus;
}

// Adjust raw evaluation according to various correction histories value
// and guarantee evaluation does not hit the tablebase range.
Value adjust_eval_value(Value evalValue, int correctionValue) noexcept {
    return in_range(evalValue + constexpr_round(7.6294e-6 * double(correctionValue)));
}

bool is_shuffling(const Position& pos, const Stack* const ss, const Move move) noexcept {
    return !(pos.capture_promo(move) || pos.rule50_count() < 11 || pos.null_ply() <= 6
             || ss->ply < 18)
        && (ss - 2)->move.is_ok() && move.org_sq() == (ss - 2)->move.dst_sq()
        && (ss - 4)->move.is_ok() && (ss - 2)->move.org_sq() == (ss - 4)->move.dst_sq();
}

}  // namespace

// Initialize the worker with its thread and NUMA information
Worker::Worker(usize                     threadIdx,
               usize                     threadCnt,
               usize                     numaIdx,
               usize                     numaThreadCnt,
               NumaReplicatedAccessToken accessToken,
               ISearchManagerPtr         searchManager,
               const SharedState&        sharedState) noexcept :
    threadId(threadIdx),
    threadCount(threadCnt),
    numaId(numaIdx),
    numaThreadCount(numaThreadCnt),
    numaAccessToken(accessToken),
    manager(std::move(searchManager)),
    network(sharedState.network),
    options(sharedState.options),
    transpositionTable(sharedState.transpositionTable),
    threads(sharedState.threads),
    histories(sharedState.historiesMap.at(accessToken.numa_id())),
    accCache(network[accessToken]) {}

// Reset per-thread data structures
void Worker::reset() noexcept {
    assert(thread_count() == threads.size());

    // Each thread resets its NUMA-local range of history entries to prevent false sharing

    auto historyRange = split_range(numa_id(), numa_thread_count(), histories.history_size());

    histories.pawn().fill(historyRange.beg, historyRange.end, -1338);

    auto correctionHistoryRange =
      split_range(numa_id(), numa_thread_count(), histories.correction_history_size());

    histories.pawn_correction().fill(correctionHistoryRange.beg, correctionHistoryRange.end, -5);
    histories.minor_correction().fill(correctionHistoryRange.beg, correctionHistoryRange.end, -5);
    histories.non_pawn_correction().fill(correctionHistoryRange.beg, correctionHistoryRange.end,
                                         -5);

    // Reset histories

    captureHistory.fill(-742);
    quietHistory.fill(-5);
    ttMoveHistory = 0;

    for (bool inCheck : {false, true})
        for (bool capture : {false, true})
            for (auto& toPieceSqHist : continuationHistory[inCheck][capture])
                for (auto& pieceSqHist : toPieceSqHist)
                    pieceSqHist.fill(-586);

    for (auto& toPieceSqCorrHist : continuationCorrectionHistory)
        for (auto& pieceSqCorrHist : toPieceSqCorrHist)
            pieceSqCorrHist.fill(5);

    accCache.init(network[numa_access_token()]);
}

// Ensure that the neural network is replicated on this NUMA node
void Worker::ensure_network_replicated() const noexcept {
    // Access once to force lazy initialization.
    // Do this because want to avoid initialization during search.
    (void) (network[numa_access_token()]);
}

// Called when the program receives the UCI 'go' command.
void Worker::start_search() noexcept {
    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    // Non-main threads go directly to iterative_deepening()
    if (mainManager == nullptr)
    {
        iterative_deepening();
        return;
    }

    if (!limit.infinite)
        transpositionTable.advance_generation();

    std::string bestMove, ponderMove;

    if (rootMoves.empty())
    {
        FixedText score{
          to_score({Value(rootPos.checkers_bb() != 0 ? -VALUE_MATE : VALUE_DRAW), rootPos})};

        mainManager->updateContext.onUpdateShort({DEPTH_ZERO, score});

        bestMove   = move_to_can(Move::None);
        ponderMove = {};
    }
    else
    {
        bool think = false;

        Move bookBestMove = Move::None;

        // Check polyglot book
        if (!limit.infinite && limit.mate == 0)
            bookBestMove = pgBook.probe(rootPos, rootMoves, options);

        if (bookBestMove != Move::None)
        {
            State st;
            rootPos.do_move(bookBestMove, st, true, this);

            RootMoves oRootMoves;

            for (auto m : MoveList<GenType::LEGAL>(rootPos))
                oRootMoves.emplace_back(m);

            Move bookPonderMove = pgBook.probe(rootPos, oRootMoves, options);

            rootPos.undo_move(bookBestMove);

            for (auto&& th : threads)
            {
                auto& rms = th->worker->rootMoves;

                rms.swap_to_front(bookBestMove);

                if (bookPonderMove != Move::None)
                    rms[0].pv.push_back(bookPonderMove);
            }
        }
        else
        {
            think = true;

            threads.start_search();  // Starts non-main threads search
            iterative_deepening();   // Start main-thread search
        }

        // When reach the maximum depth, can arrive here without a raise of threads.stop.
        // However, if pondering or in an infinite search, the UCI protocol states that
        // shouldn't print the best move before the GUI sends a "stop" or "ponderhit" command.
        // Therefore simply wait here until the GUI sends one of those commands.
        {
            std::unique_lock condLock(mainManager->mutex);

            // Wait until either:
            // 1. Threads are stopped, OR
            // 2. Not in infinite search AND Not pondering
            mainManager->condVar.wait(condLock, [&]() noexcept {
                return threads.is_stopped() || (!limit.infinite && !mainManager->ponder);
            });
        }

        // Stop the threads if not already stopped
        // (also raise the stop if "ponderhit" just reset mainManager->ponder).
        threads.request_stop();

        // Wait until all threads have finished
        threads.wait_finish();

        Worker* bestWorker = this;

        if (think)
        {
            // When playing in 'Nodes as Time' mode, advance the time nodes before exiting.
            if (mainManager->timeManager.use_nodes_time())
                mainManager->timeManager.advance_time_nodes(
                  threads.sum(&Worker::nodes) - limit.clocks[rootPos.active_color()].inc);

            // If the skill is enabled, swap the best PV line with the sub-optimal one
            if (mainManager->skill.enabled())
            {
                const Move skillMove = mainManager->skill.pick_move(rootMoves, multiPv, false);

                for (auto&& th : threads)
                    th->worker->rootMoves.swap_to_front(skillMove);
            }
            else if (thread_count() > 1 && multiPv == 1 && limit.mate == 0)
                bestWorker = threads.best_thread()->worker.get();

            if (limit.use_time_manager())
            {
                mainManager->preBestCurValue  = bestWorker->rootMoves[0].curValue;
                mainManager->preBestAvgValue  = bestWorker->rootMoves[0].avgValue;
                mainManager->preTimeReduction = mainManager->timeReduction;
                mainManager->atFirst          = false;
            }
        }

        assert(!bestWorker->rootMoves.empty() && !bestWorker->rootMoves[0].pv.empty());

        const auto& rm = bestWorker->rootMoves[0];

        if (rm.pv.size() == 1 && bestWorker->ponder_move_extracted())
            mainManager->pvShown = false;

        // Send PV info again if it has changed since last output
        if (!mainManager->pvShown || bestWorker != this)
            mainManager->show_pv(*bestWorker, bestWorker->completedDepth);

        bestMove   = move_to_can(rm.pv[0]);
        ponderMove = move_to_can(rm.pv.size() > 1 ? rm.pv[1] : Move::None);
    }

    mainManager->updateContext.onUpdateMove({bestMove, ponderMove});
}

// Main iterative deepening loop. It calls search() repeatedly with increasing depth
// until the allocated thinking time has been consumed, the user stops the search,
// or the maximum search depth is reached.
void Worker::iterative_deepening() noexcept {

    multiPv = 1;

    nmpPly = 0;

    accStack.reset();

    lastIterationPV.clear();

    for (auto& colorQuietHist : quietHistory)
        for (auto& quietHist : colorQuietHist)
            quietHist *= 0.7119;

    lowPlyQuietHistory.fill(102);

    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    Color ac = rootPos.active_color();

    usize rootMovesSize = rootMoves.size();
    assert(rootMovesSize != 0 && rootMovesSize <= MOVE_MAX);

    if (mainManager != nullptr)
    {
        multiPv = options["MultiPV"];

        mainManager->skill.init(options);
        // When playing with strength handicap enable MultiPV search that
        // will use behind-the-scenes to retrieve a set of sub-optimal moves.
        if (mainManager->skill.enabled())
            multiPv = std::max<usize>(4, multiPv);

        multiPv = std::min<usize>(rootMovesSize, multiPv);

        mainManager->timeManager.init(rootPos.active_color(), rootPos.ply(), rootPos.move_num(),
                                      options, limit);

        mainManager->sumMoveChanges = 0.0;
        mainManager->timeReduction  = 1.0;
        mainManager->callsCount     = limit.calls_count();
        mainManager->pvShown        = false;
        mainManager->set_ponder(limit.ponder);
        mainManager->ponderhitStop = false;
    }

    // Allocate stack with extra size to allow access from (ss - 9) to (ss + 1):
    // (ss - 9) is needed for update_continuation_histories(ss - 1) which accesses (ss - 8),
    // (ss + 1) is needed for initialization of cutoffCount.
    constexpr u16 StackOffset = 9;

    Array<Stack, StackOffset + (PLY_MAX + 1) + 1> stacks{};

    Stack* ss = &stacks[StackOffset];

    for (i16 i = 0 - StackOffset; i < int(stacks.size()) - StackOffset; ++i)
    {
        (ss + i)->ply = i;

        if (i >= 0)
            continue;

        // Use as a sentinel
        // clang-format off
        (ss + i)->evalValue                = VALUE_NONE;
        (ss + i)->pieceSqHistory           = &continuationHistory[0][0][+Piece::NO_PIECE][SQUARE_ZERO];
        (ss + i)->pieceSqCorrectionHistory = &continuationCorrectionHistory[+Piece::NO_PIECE][SQUARE_ZERO];
        // clang-format on
    }

    assert(stacks[0].ply == -StackOffset && stacks[stacks.size() - 1].ply == PLY_MAX + 1);
    assert(ss->ply == 0);

    PVMoves pv;

    ss->pv = &pv;

    Value bestValue = -VALUE_INFINITE;

    u16 researchCnt = 0;

    Depth lastBestMoveDepth = DEPTH_ZERO;
    completedDepth          = DEPTH_ZERO;

    // Iterative deepening loop
    const Depth maxDepth =
      limit.depth != DEPTH_ZERO ? std::min<Depth>(limit.depth, DEPTH_MAX) : DEPTH_MAX;
    for (rootDepth = 1; rootDepth <= maxDepth; ++rootDepth)
    {
        // Signal the start of a new iteration
        if (mainManager != nullptr)
            mainManager->pvShown = false;

        // Precompute the start indices of each tbRank group
        Array<usize, MOVE_MAX + 1> tbRankGroups{};
        usize                      tbRankGroupCnt = 0;
        // Group moves by tbRank and snapshot scores before search
        for (usize i = 0; i < rootMovesSize;)
        {
            tbRankGroups[tbRankGroupCnt++] = i;
            // Scan group: record boundaries and snapshot scores
            auto tbRank = rootMoves[i].tbRank;
            do
            {
                // Save the last iteration's scores before the first PV line is searched and
                // all the move scores except the (new) PV are set to -VALUE_INFINITE.
                rootMoves[i].preValue = rootMoves[i].curValue;
                ++i;
            } while (i < rootMovesSize && rootMoves[i].tbRank == tbRank);
        }
        // Sentinel (critical) to simplify pvEnd access
        tbRankGroups[tbRankGroupCnt] = rootMovesSize;

        // Index in tbRankGroups
        usize tbRankGroupIdx = 0;
        usize pvBeg = pvEnd = 0;
        // MultiPV loop. Perform a full root search for each PV line
        for (pvCur = 0; pvCur < multiPv; ++pvCur)
        {
            const bool pvLast = pvCur + 1 == multiPv;

            // Advance group if pvCur reached pvEnd
            if (pvCur == pvEnd)
            {
                pvBeg = tbRankGroups[tbRankGroupIdx];
                pvEnd = tbRankGroups[tbRankGroupIdx + 1];  // safe because of sentinel
                ++tbRankGroupIdx;
            }

            auto avgValue    = rootMoves[pvCur].avgValue;
            auto avgSqrValue = rootMoves[pvCur].avgSqrValue;

            // Reset aspiration window starting size
            int delta = 5 + std::min<usize>(thread_count() - 1, 8)
                      + constexpr_round(1.0032e-4 * double(constexpr_abs(avgSqrValue)));

            Value alpha = std::max<int>(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min<int>(avgValue + delta, +VALUE_INFINITE);

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = 1;

            // Adjust optimism based on root move's avgValue
            optimism[ac]  = 114 * avgValue / (85 + constexpr_abs(avgValue));
            optimism[~ac] = -optimism[ac];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, research with a bigger window until don't fail high/low anymore.
            u16 failHighCnt = 0;
            while (true)
            {
                ss->cutoffCount = 0;

                rootDelta = beta - alpha;
                assert(rootDelta != 0);

                // Reduce search depth according to fail-highs and research count.
                const Depth penaltyDepth  = failHighCnt + 3 * (1 + researchCnt) / 4;
                const Depth adjustedDepth = std::max<Depth>(rootDepth - penaltyDepth, 1);

                bestValue = search<NT::ROOT>(rootPos, ss, alpha, beta, adjustedDepth);

                // Bring the best move to the front. It is critical that sorting
                // is done with a stable algorithm because all the values but the
                // first and eventually the new best one is set to -VALUE_INFINITE
                // and want to keep the same order for all the moves except the
                // new PV that goes to the front. Note that in the case of MultiPV
                // search the already searched PV lines are preserved.
                rootMoves.sort(pvCur, pvEnd);

                // If the search has been stopped, break immediately.
                // Sorting is safe because RootMoves is still valid, although it refers to the previous iteration.
                if (threads.is_stopped())
                    break;

                // When failing high/low give some update before a re-search
                if (mainManager != nullptr && multiPv == 1 && rootDepth > OutputLimitDepth
                    && (alpha >= bestValue || bestValue >= beta))
                    mainManager->show_pv(*this, rootDepth);

                // In case of failing low/high increase aspiration window and research, otherwise exit
                if (bestValue <= alpha)
                {
                    assert(alpha > -VALUE_INFINITE);

                    beta  = alpha;
                    alpha = std::max<int>(bestValue - delta, -VALUE_INFINITE);

                    failHighCnt = 0;

                    if (mainManager != nullptr)
                        mainManager->ponderhitStop = false;
                }
                else if (bestValue >= beta)
                {
                    alpha = std::max<int>(beta - delta, alpha);
                    beta  = std::min<int>(bestValue + delta, +VALUE_INFINITE);

                    ++failHighCnt;
                }
                else
                    break;

                delta = std::min<int>(constexpr_ceil(1.3672 * double(delta)), DELTA_MAX);

                assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
            }

            // Sort the PV lines searched so far
            rootMoves.sort(pvBeg, pvCur + 1);

            if (threads.is_stopped())
                break;

            // Give some update about the PV
            if (mainManager != nullptr && (pvLast || rootDepth > OutputLimitDepth))
            {
                mainManager->show_pv(*this, rootDepth);
                mainManager->pvShown = pvLast;
            }
        }

        if (threads.is_stopped())
        {
            // A mated-in/TB-loss score from an aborted search cannot be trusted:
            // the loss could be delayed or refuted upon exploring the remaining root-moves.
            // Thus here roll back to the score from the previous iteration.
            if (rootMoves[0].curValue != -VALUE_INFINITE && is_loss(rootMoves[0].curValue))
            {
                // Bring the last best move to the front for best thread selection.
                if (!lastIterationPV.empty())
                {
                    rootMoves.move_to_front(
                      [&lastIterationPV = std::as_const(lastIterationPV)](const auto& rm) noexcept {
                          return rm == lastIterationPV[0];
                      });

                    rootMoves[0].pv       = lastIterationPV;
                    rootMoves[0].curValue = rootMoves[0].uciValue = rootMoves[0].preValue;

                    if (mainManager != nullptr)
                        mainManager->pvShown = true;
                }
                // For an aborted depth 1 search label the loss score as inexact.
                else if (rootMoves[0].bound != Bound::LOWER)
                    rootMoves[0].bound = Bound::UPPER;
            }

            break;
        }

        completedDepth = rootDepth;

        if (lastIterationPV.empty() || lastIterationPV[0] != rootMoves[0].pv[0])
            lastBestMoveDepth = rootDepth;

        lastIterationPV = rootMoves[0].pv;

        // Have found "mate in x"?
        if (mainManager != nullptr && limit.mate != 0
            && rootMoves[0].curValue == rootMoves[0].uciValue)
        {
            auto value = rootMoves[0].curValue;
            bool mate  = (value != +VALUE_INFINITE && is_mate_win(value))   // mate-win
                     || (value != -VALUE_INFINITE && is_mate_loss(value));  // mate-loss
            if (mate && VALUE_MATE - constexpr_abs(value) <= 2 * limit.mate)
            {
                threads.request_stop();
                break;
            }
        }

        if (mainManager != nullptr)
        {
            // If the skill is enabled and time is up, pick a sub-optimal best move
            if (mainManager->skill.enabled() && mainManager->skill.time_to_pick(rootDepth))
                mainManager->skill.pick_move(rootMoves, multiPv);

            // Do have time for the next iteration? Can stop searching now?
            if (limit.use_time_manager() && !threads.is_stopped())
            {
                if (!mainManager->ponderhitStop)
                    mainManager->handle_time_management(*this, bestValue, lastBestMoveDepth);
                // Decay PV variability metric on every completed iteration to reduce influence of previous iterations
                mainManager->sumMoveChanges *= 0.50;
            }
        }

        if (threads.is_stopped())
            break;
        if (threads.is_researching())
            ++researchCnt;
    }
}

// clang-format off
// The main alpha-beta search function with negamax framework and
// various enhancements like aspiration windows, late move reductions, etc.
template<NT T>
Value Worker::search(Position& pos, Stack* const ss, Value alpha, Value beta, Depth depth, const i16 red, const Move excludedMove) noexcept {
    // clang-format on
    constexpr bool RootNode = T == NT::ROOT;
    constexpr bool PVNode   = RootNode || T == NT::PV;
    constexpr bool CutNode  = T == NT::CUT;  // !PVNode
    constexpr bool AllNode  = T == NT::ALL;  // !PVNode
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (alpha + 1 == beta));
    assert(ss->ply >= 0);
    assert(!RootNode || (DEPTH_ZERO < depth && depth <= DEPTH_MAX));

    const Key key = pos.key();

    if constexpr (!RootNode)
    {
        // Dive into quiescence search when depth <= DEPTH_ZERO
        if (depth <= DEPTH_ZERO)
            return qsearch<PVNode>(pos, ss, alpha, beta);

        // Check if have an upcoming move that draws by repetition
        if (alpha < VALUE_DRAW && pos.is_upcoming_repetition(ss->ply))
        {
            alpha = draw_value(key, nodes_());

            if (alpha >= beta)
                return alpha;
        }

        // Limit the depth if extensions made it too large
        if (depth > DEPTH_MAX)
            depth = DEPTH_MAX;

        assert(DEPTH_ZERO < depth && depth <= DEPTH_MAX);
    }

    // Check for the available remaining time
    if (is_main_worker())
        main_manager()->check_time(*this);

    PVMoves pv;

    if constexpr (PVNode)
    {
        // Update selDepth (selDepth from 1, ply from 0)
        selDepth = std::max<u16>(ss->ply + 1, selDepth);
    }

    const usize prePvIdx = std::max<int>((ss - 1)->ply, 0);

    // Step 1. Initialize node
    ss->inCheck   = pos.checkers_bb() != 0;
    ss->moveCount = 0;
    ss->history   = 0;
    ss->pvFollow  = RootNode                               //
                || ((ss - 1)->pvFollow                     //
                    && (prePvIdx < lastIterationPV.size()  //
                        && (ss - 1)->move == lastIterationPV[prePvIdx]));

    if constexpr (!RootNode)
    {
        // Step 2. Check for stopped search or maximum ply reached or immediate draw
        if (threads.is_stopped() || ss->ply >= PLY_MAX || pos.is_draw(ss->ply))
            return ss->ply >= PLY_MAX && !ss->inCheck ? evaluate(pos) : draw_value(key, nodes_());

        // Step 3. Mate distance pruning.
        // Even if mate at the next move score would be at best mates_in(ss->ply + 1),
        // but if alpha is already bigger because a shorter mate was found upward in the tree
        // then there is no need to search further because will never beat the current alpha.
        // Same logic but with a reversed signs apply also in the opposite condition of being mated
        // instead of giving mate. In this case, return a fail-high score.
        alpha = std::max<int>(mated_in(ss->ply + 0), alpha);
        beta  = std::min<int>(mates_in(ss->ply + 1), beta);

        if (alpha >= beta)
            return alpha;
    }

    assert(0 <= ss->ply && ss->ply < PLY_MAX);

    (ss + 1)->cutoffCount = 0;

    const bool exclude = excludedMove != Move::None;

    // Step 4. Transposition table lookup
    auto [ttd, ttu] = transpositionTable.probe(key);

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;

    bool ttmNone;
    if constexpr (RootNode)
    {
        ttd.move = rootMoves[pvCur].pv[0];
        ttmNone  = false;
    }
    else
    {
        ttd.move = ttd.hit ? legal_move(ttd.move, pos) : Move::None;
        ttmNone  = ttd.move == Move::None;
        assert(ttmNone || pos.legal(ttd.move));
    }

    ss->ttMove = ttd.move;

    const bool ttmCapture = !ttmNone && pos.capture_promo(ttd.move);

    if (!exclude)
        ss->pvTT = PVNode || (ttd.hit && ttd.pv);

    const Move preMove = (ss - 1)->move;

    const bool   preOk = preMove.is_ok();
    const Square preSq = preOk ? preMove.dst_sq() : SQ_NONE;

    const bool preCapture = pos.captured_pc() != Piece::NO_PIECE;
    const bool preNonPawn = preOk && type_of(pos[preSq]) != PAWN && preMove.type() != MT::PROMOTION;

    const int correctionValue = correction_value(pos, ss);

    Value evalValue, ttEvalValue;

    bool improve, worsen;

    // Step 5. Static evaluation of the position
    if (ss->inCheck)
    {
        evalValue = VALUE_NONE;

        ss->evalValue = ttEvalValue = (ss - 2)->evalValue;
    }
    else if (exclude)
    {
        evalValue = ttEvalValue = ss->evalValue;
    }
    else if (ttd.hit)
    {
        // Never assume anything about values stored in TT
        evalValue = ttd.evalValue;

        if (!is_valid(evalValue))
            evalValue = evaluate(pos);

        ss->evalValue = ttEvalValue = adjust_eval_value(evalValue, correctionValue);

        // Can ttValue be used as a better position evaluation
        if (is_valid(ttd.value) && is_ok(ttd.bound & fail_bound(ttd.value > ttEvalValue)))
            ttEvalValue = ttd.value;
    }
    else
    {
        evalValue = evaluate(pos);

        ss->evalValue = ttEvalValue = adjust_eval_value(evalValue, correctionValue);

        ttu.update(Move::None, VALUE_NONE, evalValue, DEPTH_NONE, Bound::NONE, ss->pvTT);
    }

    // Set up the improve and worsen flags.
    // improve: if the static evaluation is better than it was at the our last turn (two plies ago)
    // worsen: if the static evaluation is better than it was at the opponent last turn (one ply ago).
    improve = ss->evalValue > +(ss - 2)->evalValue;
    worsen  = ss->evalValue > -(ss - 1)->evalValue;

    // Retroactive LMR adjustments
    // Hindsight adjustment of reductions based on static evaluation difference.
    // The ply after beginning an LMR search, adjust the reduced depth based on
    // how the opponent's move affected the static evaluation.
    if (depth < DEPTH_MAX && red >= 3 && !worsen)
        ++depth;

    if (depth > 1 && red >= 2 && ss->evalValue > 166 - (ss - 1)->evalValue)
        --depth;

    auto& pawnHistory = histories.pawn(pos.pawn_key());

    State st;

    // Check for an early TT cutoff at non-pv nodes
    if constexpr (!PVNode)
    {
        if (!exclude && is_valid(ttd.value)                   //
            && ttd.depth > depth - (ttd.value <= beta)        //
            && (CutNode == (ttd.value >= beta) || depth > 4)  //
            && is_ok(ttd.bound & fail_bound(ttd.value >= beta)))
        {
            // If ttMove fails high, update move sorting heuristics on TT hit
            if (!ttmNone && ttd.value >= beta)
            {
                // Bonus for a quiet ttMove
                if (!ttmCapture)
                    update_quiet_histories(pos, pawnHistory, ss, ttd.move,
                                           std::min(-0 + 112 * depth, +695));

                // Extra penalty for early quiet moves of the previous ply
                if (preOk && !preCapture && (ss - 1)->moveCount < 5)
                    update_continuation_histories(ss - 1, pos[preSq], preSq, -2210);
            }

            // Partial workaround for the graph history interaction problem
            // For high rule50 counts don't produce transposition table cutoffs.
            if (pos.rule50_count() < constexpr_round((1.0 - double(pos.has_rule50_high()) * 0.20)
                                                     * double(rule50_threshold())))
            {
                // If the depth is big enough, verify that the ttMove is really a good move
                if (depth >= 7 && !is_decisive(ttd.value) && !ttmNone && pos.legal(ttd.move))
                {
                    pos.do_move(ttd.move, st);

                    auto [ttdNext, ttuNext] = transpositionTable.probe(pos.key());

                    ttdNext.value = ttdNext.hit
                                    ? value_from_tt(ttdNext.value, ss->ply, pos.rule50_count())
                                    : VALUE_NONE;

                    pos.undo_move(ttd.move);

                    // Check that the ttValue after the ttMove would also trigger a cutoff
                    if (!is_valid(ttdNext.value) || (ttd.value >= beta) == (-ttdNext.value >= beta))
                        return ttd.value;
                }
                else
                    return ttd.value;
            }
        }
    }

    const Color ac = pos.active_color();

    const bool  hasNonPawn   = pos.has_non_pawn(ac);
    const Value nonPawnValue = hasNonPawn ? pos.non_pawn_value(ac) : VALUE_ZERO;

    Value bestValue = -VALUE_INFINITE;

    [[maybe_unused]] Value maxValue = +VALUE_INFINITE;

    Move move, bestMove = Move::None;

    // Step 6. Tablebase probe
    if constexpr (!RootNode)
    {
        if (!exclude && tbConfig.cardinality != 0 && !pos.has_castling_rights())
        {
            u8 pieceCount = pos.count();

            if (pieceCount < tbConfig.cardinality
                || (pieceCount == tbConfig.cardinality  //
                    && depth >= tbConfig.probeDepth))
            {
                Tablebase::Syzygy::ProbeState wdlPs;

                auto wdlScore = Tablebase::Syzygy::probe_wdl(pos, &wdlPs);

                // Force check of time on the next occasion
                if (is_main_worker())
                    main_manager()->callsCount = 1;

                if (wdlPs != Tablebase::Syzygy::PS_FAIL)
                {
                    tbHits.fetch_add(1, std::memory_order_relaxed);

                    int drawValue = int(tbConfig.useRule50);

                    // Use the range VALUE_TB to VALUE_TB_WIN_IN_PLY_MAX to value
                    Value tbValue = wdlScore < -drawValue ? -VALUE_TB + ss->ply
                                  : wdlScore > +drawValue ? +VALUE_TB - ss->ply
                                                          : VALUE_DRAW + 2 * wdlScore * drawValue;

                    Bound bound = wdlScore < -drawValue ? Bound::UPPER
                                : wdlScore > +drawValue ? Bound::LOWER
                                                        : Bound::EXACT;

                    if (bound == Bound::EXACT
                        || (bound == Bound::LOWER ? tbValue >= beta : tbValue <= alpha))
                    {
                        ttu.update(Move::None, value_to_tt(tbValue, ss->ply), evalValue,
                                   std::min<Depth>(depth + 6, DEPTH_MAX), bound, ss->pvTT);

                        return tbValue;
                    }

                    if constexpr (PVNode)
                    {
                        if (bound == Bound::LOWER)
                        {
                            bestValue = tbValue;

                            alpha = std::max<int>(tbValue, alpha);
                        }
                        else
                            maxValue = tbValue;
                    }
                }
            }
        }
    }

    int absCorrectionValue = constexpr_abs(correctionValue);

    const History<HType::PIECE_SQ>* contHistory[CONT_HISTORY_COUNT];

    build_continuation_histories(ss, contHistory);

    // Skip early pruning when in check
    if (!ss->inCheck)
    {
        // clang-format off
    // Use static evaluation difference to improve quiet move ordering
    if (preOk && !preCapture && !(ss - 1)->inCheck)
    {
        int bonus = 60 + std::clamp(-((ss - 1)->evalValue + (ss - 0)->evalValue), -189, +194);

        if (!ttd.hit && preNonPawn)
            update_pawn_history(pawnHistory, pos[preSq], preSq, 13 * bonus);

        update_quiet_history(~ac, preMove, 11 * bonus);
    }

    // Step 7. Razoring
    // If eval is really low, check with qsearch then return speculative fail low.
    if constexpr (!PVNode)
    {
    if (!exclude && ttEvalValue + 483 + 318 * depth * depth <= alpha)
    {
        const Value razorAlpha = std::max<int>(alpha - 1, -VALUE_INFINITE);

        const Value razorValue = qsearch<false>(pos, ss, razorAlpha, razorAlpha + 1);

        if (razorValue <= razorAlpha && !is_loss(razorValue))
            return razorValue;

        ss->ttMove = ttd.move;
    }
    }

    // Step 8. Reverse Futility Pruning: child node
    if constexpr (!PVNode)
    {
    // The depth condition is important for mate finding
    if (!exclude && !ss->pvTT && depth < 19 && !is_win(ttEvalValue) && !is_loss(beta)
        && (ttmNone || history_value(pos, ttd.move, ac, contHistory) >= 32768 - int(ttmCapture) * 25968))
    {
        // Compute base futility
        int baseFutility = std::min(+25 + 4 * depth, +65) + int(ttd.hit) * 20;
        // Compute futility
        int futility = std::max(depth * baseFutility
                              - constexpr_round((double(improve) * 2.7236 + double(worsen) * 0.3271) * double(baseFutility))
                              + constexpr_round(5.0394e-6 * double(absCorrectionValue)),
                                0);

        if (ttEvalValue - futility >= beta)
            return (661 * beta + 363 * ttEvalValue) / 1024;
    }
    }

    // Step 9. Null move search with verification search
    if constexpr (CutNode)
    {
    if (!exclude && hasNonPawn /*Zugzwang guard*/ && ss->ply >= nmpPly
        && beta >= -2000 && ss->evalValue - 365 + int(improve) * 47 + 13 * depth >= beta)
    {
        assert(preMove != Move::Null);

        // Null move dynamic reduction
        Depth R = 7 + depth / 3 + std::max((ss->evalValue - beta) / 256, 0);

        do_null_move(pos, st, ss);

        Value nullValue = -search<NT::ALL>(pos, ss + 1, -beta, -beta + 1, depth - R);

        undo_null_move(pos);

        // If null move fails high, do a verification search
        if (nullValue >= beta && !is_win(nullValue))
        {
            assert(!is_loss(nullValue));

            // At low depths or when verification is disabled,
            // return immediately to avoid expensive verification search.
            if (depth < 16 || nmpPly != 0)
                return nullValue;

            assert(nmpPly == 0);  // Recursive verification is not allowed

            // Do verification search at high depths,
            // with null move pruning disabled until ply exceeds nmpPly.
            nmpPly = ss->ply + 3 * (depth - R) / 4;

            Value verifyValue = search<NT::ALL>(pos, ss, beta - 1, beta, depth - R);

            nmpPly = 0;

            if (verifyValue >= beta)
                return nullValue;

            ss->ttMove = ttd.move;
        }
    }
    }

    improve |= ss->evalValue >= beta;

    // Step 10. Internal iterative reductions
    // Reduce search depth for PV/Cut deep enough nodes without ttMoves.
    // (*Scaler) Making IIR more aggressive scales poorly.
    if constexpr (!AllNode)
    {
    depth -= (depth > 5) & (ttmNone) & !ss->pvFollow;
    }

    // Step 11. ProbCut
    // If have a good enough capture or any promotion and a reduced search
    // returns a value much above beta, can (almost) safely prune previous move.
    if (depth > 2 && !is_loss(beta))
    {
        const Value probCutBeta = std::min(241 + beta - int(improve) * 64, +VALUE_INFINITE);
        assert(beta <= probCutBeta && probCutBeta <= +VALUE_INFINITE);

        // If value from transposition table is less than probCutBeta, Don't attempt probCut
        if (!(is_valid(ttd.value) && ttd.value < probCutBeta))
        {
        const Depth probCutDepth     = std::max<Depth>(depth - 3 - int(improve) * 2, DEPTH_ZERO);
        const int   probCutThreshold = probCutBeta - ss->evalValue;

        MovePicker mp(pos, ttd.move, &captureHistory, probCutThreshold);
        // Loop through all legal moves
        while ((move = mp.next_move()) != Move::None)
        {
            assert(pos.legal(move));
            assert(pos.capture_promo(move)
                   && (move == ttd.move || pos.see(move) >= probCutThreshold));

            // Check for exclusion
            if (move == excludedMove)
                continue;

            // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
            // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
            if constexpr (RootNode)
            {
                if (!rootMoves.contains(pvCur, pvEnd, move))
                    continue;
            }

            do_move(pos, move, st, ss);

            // Perform a preliminary qsearch to verify that the move holds
            Value probCutValue = -qsearch<false>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (probCutValue >= probCutBeta && probCutDepth > DEPTH_ZERO)
                probCutValue = -search<~T>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, probCutDepth);

            undo_move(pos, move);

            assert(is_ok(probCutValue));

            if (threads.is_stopped())
                return VALUE_ZERO;

            if (probCutValue >= probCutBeta)
            {
                assert(!is_loss(probCutValue));

                // Save ProbCut data into transposition table
                if (!exclude)
                    ttu.update(move, value_to_tt(probCutValue, ss->ply), evalValue,
                               std::min<Depth>(probCutDepth + 1, DEPTH_MAX), Bound::LOWER, ss->pvTT);

                if (!is_win(probCutValue))
                    // Adjust probCutValue to align with the current beta window
                    return (probCutValue - (probCutBeta - beta));
            }
        }
        }
        }
        // clang-format on
    }

    // When in check, search starts here

    // Step 12. Small ProbCut idea
    if (!is_loss(beta) && is_valid(ttd.value) && !is_win(ttd.value))
    {
        const Value probCutBeta = std::min(428 + beta, +VALUE_INFINITE);

        if (ttd.value >= probCutBeta && ttd.depth >= depth - 4 && is_ok(ttd.bound & Bound::LOWER))
            return probCutBeta;
    }

    // Pruning is safe if:
    //  • The node is already drawish (no stalemate risk), OR
    //  • The move does not sacrifice our last non-pawn material.
    const auto safe_pruning = [&](Piece movedPc) noexcept -> bool {
        return alpha >= VALUE_DRAW || nonPawnValue != piece_value(type_of(movedPc));
    };

    Value value = bestValue;

    u16 moveCount = 0;

    Array<SearchedMoves, 2> searchedMoves;

    MovePicker mp(pos, ttd.move, &histories, &captureHistory, &quietHistory, &lowPlyQuietHistory,
                  contHistory, ss->ply, -1);
    // Step 13. Loop through all legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None)
    {
        assert(pos.legal(move));

        // Check for exclusion
        if (move == excludedMove)
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
        // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
        if constexpr (RootNode)
        {
            if (!rootMoves.contains(pvCur, pvEnd, move))
                continue;
        }

        ss->moveCount = ++moveCount;

        if constexpr (RootNode)
        {
            if (is_main_worker() && rootDepth > OutputLimitDepth && !options["MinimalInfo"])
            {
                std::string currMove{move_to_can(move)};
                usize       currMoveNumber{pvCur + moveCount};

                main_manager()->updateContext.onUpdateIter({rootDepth, currMove, currMoveNumber});
            }
        }

        if constexpr (PVNode)
        {
            (ss + 1)->pv = nullptr;
        }

        bool ttm = move == ttd.move;

        Square dstSq = move.dst_sq();

        Piece movedPc = pos.moved_pc(move);

        bool check      = pos.check(move);
        bool capture    = pos.capture_promo(move);
        auto capturedPt = capture ? pos.captured_pt(move) : NO_PIECE_TYPE;

        // Calculate new depth for this move
        Depth newDepth = depth - 1;

        int deltaRatio = 577 * (beta - alpha) / rootDelta;

        int r = reduction(depth, moveCount, deltaRatio, improve);

        // (*Scaler) Increase reduction for pvHit nodes, Larger values scales well
        r += int(ss->pvTT) * 929;

        // Step 14. Pruning at shallow depths
        // Depth conditions are important for mate finding.
        if constexpr (!RootNode)
        {
            if (hasNonPawn && !is_loss(bestValue))
            {
                // Skip quiet moves if moveCount exceeds moveCount threshold
                mp.update_quiets_skip(moveCount >= ((3 + depth * depth) / (1 + int(!improve) * 1)));

                // Reduced depth of the next LMR search
                Depth lmrDepth = newDepth - r / 1024;

                if (capture)
                {
                    int history = captureHistory[+movedPc][dstSq][capturedPt];

                    // Futility pruning: for captures
                    if (!check && lmrDepth < 8)
                    {
                        int futility = 234 + ss->evalValue + piece_value(capturedPt)
                                     + 247 * lmrDepth + constexpr_round(0.1309 * double(history));
                        if (futility <= alpha)
                            continue;
                    }

                    // SEE based pruning for captures and checks
                    if (safe_pruning(movedPc))
                    {
                        int threshold =
                          std::max(177 * depth + constexpr_round(33.2031e-3 * double(history)), 0);
                        if ((mp.stage() != MovePicker::Stage::ENC_GOOD_CAPTURE
                             || mp.threshold_value() > threshold)
                            && pos.see(move) < -threshold)
                            continue;
                    }
                }
                else if (!PVNode || !ss->pvFollow)
                {
                    int history = pawnHistory[+movedPc][dstSq]  //
                                + (*contHistory[0])[+movedPc][dstSq]
                                + (*contHistory[1])[+movedPc][dstSq];

                    // History based pruning
                    if (!check && history < -4136 * depth)
                        continue;

                    history += constexpr_round(2.15625 * quietHistory[ac][move.raw()]);

                    // (*Scaler) Generally, lower divisor scales well
                    assert(depth > DEPTH_ZERO);
                    lmrDepth += history / LMRDivisor[std::min<usize>(depth, LMRDivisor.size()) - 1];

                    // Futility pruning: for quiets
                    // (*Scaler) Generally, more frequent futility pruning scales well
                    if (!check && lmrDepth < 12 && !ss->inCheck)
                    {
                        int futility = 164 + ss->evalValue + 119 * lmrDepth  //
                                     + int(ss->evalValue > alpha) * 90;
                        if (futility <= alpha)
                        {
                            if (!is_win(futility))
                                bestValue = std::max<int>(bestValue, futility);
                            continue;
                        }
                    }

                    // SEE based pruning for quiets and checks
                    if (safe_pruning(movedPc))
                    {
                        int threshold = std::max(
                          int(check) * 64 * depth + 23 * lmrDepth * constexpr_abs(lmrDepth), 0);
                        if (safe_pruning(movedPc) && pos.see(move) < -threshold)
                            continue;
                    }
                }
            }
        }

        // Step 15. Extensions
        // Singular extension search. If all moves but one fail low on a search
        // of (alpha-s, beta-s), and just one fails high on (alpha, beta),
        // then that move is singular and should be extended.
        // To verify this do a reduced search on the position excluding the ttMove and
        // if the result is lower than ttValue minus a margin, then will extend the ttMove.
        // Recursive singular search is avoided.
        Depth extension = DEPTH_ZERO;

        // (*Scaler) Generally, frequent extensions scales well.
        // This includes high singularAlpha values (i.e closer to ttValue) and low extension margins.
        if constexpr (!RootNode)
        {
            // clang-format off
        if (!exclude && ttm && depth > 5 + int(ss->pvTT) && is_valid(ttd.value) && !is_decisive(ttd.value)
             && ttd.depth >= depth - 3 && is_ok(ttd.bound & Bound::LOWER) && !is_shuffling(pos, ss, move))
        {
            Value singularAlpha = std::max(ttd.value - 1 - constexpr_round((0.9365 + double(!PVNode && ss->pvTT) * 1.0476) * double(depth)), -VALUE_INFINITE);

            Depth singularDepth = newDepth / 2;
            assert(singularDepth > DEPTH_ZERO);

            Value singularValue = search<~~T>(pos, ss, singularAlpha, singularAlpha + 1, singularDepth, 0, move);

            ss->ttMove    = ttd.move;
            ss->moveCount = moveCount;

            if (singularValue <= singularAlpha)
            {
                int corrMargin = constexpr_round(5.0411e-6 * double(absCorrectionValue));

                int doubleMargin = -2 + int(PVNode) * 204 - int(!ttmCapture) * 152 - corrMargin - int(ss->ply > rootDepth) * 38 - constexpr_round(10.290e-3 * double(ttMoveHistory));
                int tripleMargin = 70 + int(PVNode) * 279 - int(!ttmCapture) * 188 - corrMargin - int(ss->ply > rootDepth) * 43 + int(ss->pvTT) * 81;

                extension = 1 + int(singularValue + doubleMargin <= singularAlpha)
                              + int(singularValue + tripleMargin <= singularAlpha);

                if (depth < DEPTH_MAX)
                    ++depth;
            }
            // Multi-cut pruning
            // If the ttMove is assumed to fail high based on the bound of the TT entry, and
            // if after excluding the ttMove with a reduced search fail high over the original beta,
            // assume this expected cut-node is not singular (multiple moves fail high),
            // and can prune the whole subtree by returning a soft-bound.
            else if (singularValue >= beta && !is_decisive(singularValue))
            {
                ttMoveHistory << -(+421 + 110 * depth);

                if (!ss->inCheck && singularValue > ss->evalValue)
                {
                    int bonus = constexpr_round(0.1729 * (singularValue - ss->evalValue) * singularDepth);

                    update_correction_histories(pos, ss, bonus);
                }

                return singularValue;
            }
            // Negative extensions
            // If other moves failed high over (ttValue - margin) without the ttMove on a reduced search,
            // but cannot do multi-cut because (ttValue - margin) is lower than the original beta,
            // do not know if the ttMove is singular or can do a multi-cut,
            // so reduce the ttMove in favor of other moves based on some conditions:

            // If on CutNode or the ttMove is assumed to fail high over current beta
            else if (CutNode || ttd.value >= beta)
                extension = -3;
        }
            // clang-format on
        }

        // Add extension to new depth
        newDepth += extension;

        [[maybe_unused]] u64 preNodes;
        if constexpr (RootNode)
        {
            preNodes = nodes_();
        }

        // Step 16. Make the move
        do_move(pos, move, st, ss, check);

        assert(capturedPt == type_of(pos.captured_pc()));

        ss->history = history_value(capture, move, movedPc, capturedPt, ac, contHistory);

        // Base reduction offset to compensate for other tweaks
        r += 697;
        r -= 65 * moveCount;
        r -= constexpr_round(38.00836e-6 * double(absCorrectionValue));

        // (*Scaler) Decrease reduction if position is or has been on the PV
        r -= int(ss->pvTT)
           * (+3023                 //
              + int(PVNode) * 1004  //
              + int(is_valid(ttd.value) && ttd.value > alpha) * 885
              + int(ttd.depth >= depth) * (816 + int(CutNode) * 940));

        // Increase reduction for CutNode
        if constexpr (CutNode)
            r += 4026 + int(ttmNone) * 933;

        // Increase reduction if ttMove is a capture
        r += int(ttmCapture) * 1079;

        // Increase reduction if next ply has many fail-highs
        int x = ss->cutoffCount - 1;
        if (x > 0)
            r +=
              (264 + int(AllNode) * 1138 + 1024 * (x >> 1)  //
               - 512 * (x >> 2) - 256 * (x >> 3) - 128 * (x >> 4) - 64 * (x >> 5) - 32 * (x >> 6));
        // Decrease reduction for first picked move (ttMove)
        else
            r -= int(ttm) * 2179;

        // Decrease/Increase reduction for moves with a good/bad history
        r -= constexpr_round(107.1777e-3 * double(ss->history));

        // Scale up reduction for AllNode
        if constexpr (AllNode)
        {
            r = constexpr_round(double(r) * (1.0 + 1.078125 / (1.046875 + double(depth))));
        }

        // Step 17. Late moves reduction / extension (LMR)
        if (depth > 1 && moveCount > 1)
        {
            Depth redDepth =
              std::max<Depth>(std::min<Depth>(newDepth - r / 1024, newDepth + 2), 1) + int(PVNode);

            i16 reduction = newDepth - redDepth;

            value = -search<NT::CUT>(pos, ss + 1, -alpha - 1, -alpha, redDepth, reduction);

            // (*Scaler) Do a full-depth search when reduced LMR search fails high
            // Shallower searches here don't scales well.
            if (value > alpha)
            {
                // If the value was good enough search deeper
                bool extend = redDepth < newDepth && value > 53 + bestValue;
                // If the value was bad enough search shallower
                bool reduce = value < 8 + bestValue;

                // Adjust full-depth search based on LMR value
                newDepth += int(extend) - int(reduce);

                if (redDepth < newDepth)
                    value = -search<~T>(pos, ss + 1, -alpha - 1, -alpha, newDepth);

                // Post LMR continuation history updates
                update_continuation_histories(ss, movedPc, dstSq, 1342);
            }
        }
        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present
            r += int(ttmNone) * 1127;

            // Reduce search depth if expected reduction is high
            value = -search<~T>(pos, ss + 1, -alpha - 1, -alpha,
                                newDepth - int(r > 5234)  //
                                  - (int(newDepth > 2) & int(r > 5487))
                                  - (int(newDepth > 3) & int(r > 8048))
                                  - (int(newDepth > 4) & int(r > 10224)));
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if constexpr (PVNode)
        {
            if (moveCount == 1 || value > alpha)
            {
                pv.clear();
                (ss + 1)->pv = &pv;

                // Extends ttMove if about to dive into qsearch
                if (newDepth <= DEPTH_ZERO && ttm
                    && (ttd.depth > 1
                        || (ttd.depth >= 1 && is_valid(ttd.value) && is_decisive(ttd.value))))
                    newDepth = 1;

                value = -search<NT::PV>(pos, ss + 1, -beta, -alpha, newDepth);
            }
        }

        // Step 19. Unmake move
        undo_move(pos, move);

        assert(is_ok(value));

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and return immediately without updating
        // best move, principal variation and transposition table.
        if (threads.is_stopped())
            return VALUE_ZERO;

        if constexpr (RootNode)
        {
            auto& rm = *rootMoves.find(move);
            assert(rm.pv[0] == move);

            rm.nodes += nodes_() - preNodes;
            // clang-format off
            rm.avgValue    = rm.avgValue    !=          -VALUE_INFINITE  ? (         value  + rm.avgValue   ) / 2 :          value;
            rm.avgSqrValue = rm.avgSqrValue != sign_sqr(-VALUE_INFINITE) ? (sign_sqr(value) + rm.avgSqrValue) / 2 : sign_sqr(value);
            // clang-format on

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.selDepth = selDepth;
                rm.bound    = Bound::NONE;
                rm.curValue = rm.uciValue = value;

                if (value >= beta)
                {
                    rm.bound    = Bound::LOWER;
                    rm.uciValue = beta;
                }
                else if (value <= alpha)
                {
                    rm.bound    = Bound::UPPER;
                    rm.uciValue = alpha;
                }

                rm.pv.resize(1);  // keep root move at index 0

                const auto* const childPv = (ss + 1)->pv;
                assert(childPv != nullptr);

                for (const Move m : *childPv)
                    rm.pv.push_back(m);

                // Record how often the best move has been changed in each iteration.
                // This information is used for time management.
                // In MultiPV mode, must take care to only do this for the first PV line.
                if (moveCount > 1 && pvCur == 0)
                    moveChanges.fetch_add(1, std::memory_order_relaxed);
            }
            else
                // All other moves but the PV, are set to the lowest value, this
                // is not a problem when sorting because the sort is stable and the
                // move position in the list is preserved - just the PV is pushed up.
                rm.curValue = -VALUE_INFINITE;
        }

        // In case have an alternative move equal in eval to the current bestMove,
        // promote it to bestMove by pretending it just exceeds alpha (but not beta).
        bool inc = value == bestValue && 2 + ss->ply >= rootDepth && (nodes_() & 0xE) == 0
                && !is_win(constexpr_abs(value) + 1);

        Value incValue = value + int(inc);

        if (bestValue < incValue)
        {
            bestValue = value;

            if (alpha < incValue)
            {
                bestMove = move;

                if constexpr (PVNode && !RootNode)
                {
                    // Update pv even in fail-high case
                    ss->pv->update(move, (ss + 1)->pv);
                }

                if (value >= beta)
                {
                    // (*Scaler) Infrequent and small cutoff increments scales well
                    if constexpr (!RootNode)
                    {
                        (ss - 1)->cutoffCount += int(PVNode || extension < 2);
                    }

                    break;  // Fail-high
                }

                alpha = value;  // Update alpha! Always alpha < beta

                // Reduce depth for subsequent moves after a non-decisive score improvement
                if (depth > 3 && !is_decisive(value))
                    depth = std::max<Depth>(
                      depth - int(depth < 8) - int(depth < 16) - int(depth < 24), 3);
            }
        }

        // Store bad searched move for history updates
        if (move != bestMove && moveCount <= SEARCHED_MOVE_CAPACITY)
            searchedMoves[capture].push_back(move);
    }

    assert(moveCount != 0 || !ss->inCheck || exclude
           || (MoveList<GenType::LEGAL, true>(pos).empty()));
    assert(ss->moveCount == moveCount && ss->ttMove == ttd.move);

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it must be a mate or a stalemate.
    // If in a singular extension search then return a fail low score.
    if (moveCount == 0)
        bestValue = exclude ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;
    // Adjust best value for fail high cases
    else if (bestValue > beta && !is_win(bestValue) && !is_loss(beta))
        bestValue = blend_values(bestValue, beta, depth, depth + 1);

    // Don't let best value inflate too high (tb)
    if constexpr (PVNode)
    {
        bestValue = std::min<int>(maxValue, bestValue);
    }

    // If there is a move that produces search value greater than alpha update the history of searched moves
    if (bestMove != Move::None)
    {
        bool extra = bestMove == ttd.move;

        update_histories(pos, pawnHistory, ss, depth, bestMove, extra, searchedMoves);

        if constexpr (!PVNode)
        {
            ttMoveHistory << (-747 + int(extra) * 1665);
        }
    }
    // If prior move is valid, that caused the fail low
    else if (preOk)
    {
        // Bonus for prior quiet move
        if (!preCapture)
        {
            // clang-format off
            int bonusScale = std::max(
                            - 241
                            // Increase bonus when depth is high
                            + std::min(59 * depth, 420)
                            // Increase bonus when bestValue is lower than current static evaluation
                            + (!(ss    )->inCheck && bestValue <= +(ss    )->evalValue - 106) * 142
                            // Increase bonus when bestValue is higher than previous static evaluation
                            + (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->evalValue -  68) * 159
                            // Increase bonus when the previous moveCount is high
                            +  86 * ((ss - 1)->moveCount / 5)
                            // Increase bonus if the previous move has a bad history
                            - constexpr_round(10.2041e-3 * double((ss - 1)->history)),
                              1);
            // clang-format on
            int bonus = bonusScale * std::min(-85 + 150 * depth, +1337);

            if (preNonPawn)
                update_pawn_history(pawnHistory, pos[preSq], preSq,
                                    constexpr_round(39.5508e-3 * double(bonus)));

            update_quiet_history(~ac, preMove, constexpr_round(6.5613e-3 * double(bonus)));

            update_continuation_histories(ss - 1, pos[preSq], preSq,
                                          constexpr_round(16.0522e-3 * double(bonus)));
        }
        // Bonus for prior capture move
        else
        {
            auto capturedPt = type_of(pos.captured_pc());
            assert(capturedPt != NO_PIECE_TYPE);

            update_capture_history(pos[preSq], preSq, capturedPt, 892);
        }
    }

    // If no good move is found and the previous position was pvHit, then the previous
    // opponent move is probably good and the new position is added to the search tree.
    ss->pvTT |= bestValue <= alpha && (ss - 1)->pvTT;

    // Save gathered information in transposition table
    if ((!RootNode || pvCur == 0) && !exclude)
        ttu.update(bestMove, value_to_tt(bestValue, ss->ply), evalValue,
                   moveCount != 0 ? depth : std::min<Depth>(depth + 6, DEPTH_MAX),
                   bestValue >= beta                  ? Bound::LOWER
                   : PVNode && bestMove != Move::None ? Bound::EXACT
                                                      : Bound::UPPER,
                   ss->pvTT);

    // Adjust correction history if the best move is none or not a capture
    // and the error direction matches whether the above/below bounds.
    if (!ss->inCheck && (bestMove == Move::None || !pos.capture(bestMove))
        && (bestValue > ss->evalValue) == (bestMove != Move::None))
    {
        int bonus = constexpr_round(1.0361 * (bestMove != Move::None ? 0.0938 : 0.1406)
                                    * (bestValue - ss->evalValue) * depth);

        update_correction_histories(pos, ss, bonus);
    }

    assert(is_ok(bestValue));

    return bestValue;
}

// Quiescence search function, which is called by the main search function,
// should be using static evaluation only, but tactical moves may confuse the static evaluation.
// Therefore, quiescence search extends the search at positions where tactical moves are possible,
// until a "quiet" position is reached.
template<bool PVNode>
Value Worker::qsearch(Position& pos, Stack* const ss, Value alpha, Value beta) noexcept {
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (alpha + 1 == beta));

    const Key key = pos.key();

    // Check if have an upcoming move that draws by repetition
    if (alpha < VALUE_DRAW && pos.is_upcoming_repetition(ss->ply))
    {
        alpha = draw_value(key, nodes_());

        if (alpha >= beta)
            return alpha;
    }

    PVMoves pv;

    if constexpr (PVNode)
    {
        ss->pv->clear();
        (ss + 1)->pv = &pv;

        // Update selDepth (selDepth from 1, ply from 0)
        selDepth = std::max<u16>(ss->ply + 1, selDepth);
    }

    // Step 1. Initialize node
    ss->inCheck = pos.checkers_bb() != 0;

    // Step 2. Check for maximum ply reached or immediate draw
    if (ss->ply >= PLY_MAX || pos.is_draw(ss->ply))
        return ss->ply >= PLY_MAX && !ss->inCheck ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < PLY_MAX);

    // Step 3. Transposition table lookup
    auto [ttd, ttu] = transpositionTable.probe(key);

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttd.move  = ttd.hit ? legal_move(ttd.move, pos) : Move::None;
    assert(ttd.move == Move::None || pos.legal(ttd.move));
    ss->ttMove      = ttd.move;
    const bool pvTT = ttd.hit && ttd.pv;

    // Check for an early TT cutoff at non-pv nodes
    if constexpr (!PVNode)
    {
        if (ttd.depth >= DEPTH_ZERO && is_valid(ttd.value)
            && is_ok(ttd.bound & fail_bound(ttd.value >= beta)))
            return ttd.value;
    }

    const int correctionValue = ss->inCheck ? 0 : correction_value(pos, ss);

    Value evalValue, bestValue;

    int baseFutility;

    // Step 4. Static evaluation of the position
    if (ss->inCheck)
    {
        evalValue = VALUE_NONE;

        bestValue = baseFutility = -VALUE_INFINITE;
    }
    else
    {
        // clang-format off
    if (ttd.hit)
    {
        // Never assume anything about values stored in TT
        evalValue = ttd.evalValue;

        if (!is_valid(evalValue))
            evalValue = evaluate(pos);

        ss->evalValue = bestValue = adjust_eval_value(evalValue, correctionValue);

        // Can ttValue be used as a better position evaluation
        if (is_valid(ttd.value) && !is_decisive(ttd.value)
            && is_ok(ttd.bound & fail_bound(ttd.value > bestValue)))
            bestValue = ttd.value;
    }
    else
    {
        evalValue = evaluate(pos);

        ss->evalValue = bestValue = adjust_eval_value(evalValue, correctionValue);
    }

    // Stand pat. Return immediately if bestValue is at least beta
    if (bestValue >= beta)
    {
        if (bestValue > beta && !is_win(bestValue) && !is_loss(beta))
            bestValue = blend_values(bestValue, beta, 441, 1024);

        if (!ttd.hit)
            ttu.update(Move::None, VALUE_NONE, evalValue, DEPTH_NONE, Bound::LOWER, false);

        return bestValue;
    }

    alpha = std::max(bestValue, alpha);

    baseFutility = 306 + ss->evalValue;
        // clang-format on
    }

    const Move preMove = (ss - 1)->move;

    const bool   preOk = preMove.is_ok();
    const Square preSq = preOk ? preMove.dst_sq() : SQ_NONE;

    State st;

    Value value;

    Move move, bestMove = Move::None;

    u16 moveCount = 0;

    const History<HType::PIECE_SQ>* contHistory[1]{(ss - 1)->pieceSqHistory};

    // Initialize a MovePicker object for the current position, prepare to search the moves.
    // Because the depth is <= DEPTH_ZERO here, only captures, promotions will be generated.
    MovePicker mp(pos, ttd.move, &histories, &captureHistory, &quietHistory, &lowPlyQuietHistory,
                  contHistory, ss->ply);
    // Step 5. Loop through all legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None)
    {
        assert(pos.legal(move));
        assert(ss->inCheck || pos.capture_promo(move));

        ++moveCount;

        Square dstSq = move.dst_sq();

        bool check = pos.check(move);

        // Step 6. Pruning
        if (!is_loss(bestValue))
        {
            bool capture = pos.capture_promo(move);

            // Futility pruning and moveCount pruning
            if (!check && !(preOk && dstSq == preSq) && move.type() != MT::PROMOTION
                && !is_loss(baseFutility))
            {
                if (moveCount > 2)
                    continue;

                // Static evaluation + value of piece going to captured
                int futility = baseFutility + piece_value(pos.captured_pt(move));

                if (futility <= alpha)
                {
                    if (!is_win(futility))
                        bestValue = std::max<int>(bestValue, futility);
                    continue;
                }

                // SEE based pruning
                int threshold = std::max(baseFutility - alpha, -1);
                if (pos.see(move) < -threshold)
                {
                    int minFutility = std::min<int>(alpha, baseFutility);
                    if (!is_win(minFutility))
                        bestValue = std::max<int>(bestValue, minFutility);
                    continue;
                }
            }

            // Skip quiets
            if (!capture)
                continue;

            // SEE based pruning
            if (pos.see(move) < -74)
                continue;
        }

        // Step 7. Make the move
        do_move(pos, move, st, ss, check);

        value = -qsearch<PVNode>(pos, ss + 1, -beta, -alpha);

        // Step 8. Unmake move
        undo_move(pos, move);

        assert(is_ok(value));

        // Step 9. Check for a new best move
        if (bestValue < value)
        {
            bestValue = value;

            if (alpha < value)
            {
                bestMove = move;

                if constexpr (PVNode)
                {
                    // Update pv even in fail-high case
                    ss->pv->update(move, (ss + 1)->pv);
                }

                if (value >= beta)
                    break;  // Fail-high

                alpha = value;  // Update alpha! Always alpha < beta
            }
        }
    }

    // Step 10. Check for checkmate & stalemate
    // All legal moves have been searched.
    if (moveCount == 0)
    {
        // A special case: if in check and no legal moves were found, it is checkmate.
        if (ss->inCheck)
        {
            assert(bestValue == -VALUE_INFINITE);
            assert((MoveList<GenType::LEGAL, true>(pos).empty()));
            bestValue = mated_in(ss->ply);  // Plies to mate from the root
        }
        else
        {
            // Only check for stalemate under specific conditions
            const Color ac = pos.active_color();
            if (bestValue != VALUE_DRAW  //
                && type_of(pos.captured_pc()) >= KNIGHT
                // No pawn pushes available
                && (pawn_push_bb(pos.pieces_bb(ac, PAWN), ac) & ~pos.pieces_bb()) == 0
                && !pos.has_non_pawn(ac)  //
                && MoveList<GenType::LEGAL, true>(pos).empty())
                bestValue = VALUE_DRAW;
        }
    }
    // Adjust best value for fail high cases
    else if (bestValue > beta && !is_win(bestValue) && !is_loss(beta))
        bestValue = blend_values(bestValue, beta, 462, 1024);

    // Save gathered info in transposition table
    ttu.update(bestMove, value_to_tt(bestValue, ss->ply), evalValue, DEPTH_ZERO,
               fail_bound(bestValue >= beta), pvTT);

    assert(is_ok(bestValue));

    return bestValue;
}

void Worker::do_move(
  Position& pos, const Move m, State& st, Stack* const ss, const bool mayCheck) noexcept {
    assert(ss != nullptr);
    // Speculative prefetch as early as possible
    const Key moveKey = pos.move_key(m);
    prefetch(transpositionTable.cluster(moveKey));

    bool capture = pos.capture_promo(m);

    DirtyBoard db = pos.do_move(m, st, mayCheck, this);

    assert(moveKey == pos.key());

    nodes.fetch_add(1, std::memory_order_relaxed);

    auto movedPc                 = db.dirtyPiece.movedPc;
    ss->move                     = m;
    ss->pieceSqHistory           = &continuationHistory[ss->inCheck][capture][+movedPc][m.dst_sq()];
    ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[+movedPc][m.dst_sq()];

    accStack.push(std::move(db));
}

void Worker::undo_move(Position& pos, const Move m) noexcept {
    accStack.pop();

    pos.undo_move(m);
}

void Worker::do_null_move(Position& pos, State& st, Stack* const ss) noexcept {
    assert(ss != nullptr);

    pos.do_null_move(st);

    ss->move                     = Move::Null;
    ss->pieceSqHistory           = &continuationHistory[0][0][+Piece::NO_PIECE][SQUARE_ZERO];
    ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[+Piece::NO_PIECE][SQUARE_ZERO];
}

void Worker::undo_null_move(Position& pos) const noexcept { pos.undo_null_move(); }

Value Worker::evaluate(const Position& pos) noexcept {
    return Evaluate::evaluate(pos, network[numa_access_token()], accCache, accStack,
                              optimism[pos.active_color()]);
}

// clang-format off

void Worker::update_capture_history(const Piece movedPc, const Square dstSq, const PieceType capturedPt, const int bonus) noexcept {
    captureHistory[+movedPc][dstSq][capturedPt] << bonus;
}
void Worker::update_capture_history(const Position& pos, const Move m, const int bonus) noexcept {
    update_capture_history(pos.moved_pc(m), m.dst_sq(), pos.captured_pt(m), bonus);
}
void Worker::update_quiet_history(const Color ac, const Move m, const int bonus) noexcept {
    quietHistory[ac][m.raw()] << bonus;
}
void Worker::update_low_ply_quiet_history(const i16 ssPly, const Move m, const int bonus) noexcept {
    assert(m.is_ok());

    if (ssPly < LOW_PLY_QUIET_SIZE)
        lowPlyQuietHistory[ssPly][m.raw()] << bonus;
}

// Updates quiet histories (move sorting heuristics)
void Worker::update_quiet_histories(const Position& pos, PawnHistory& pawnHistory, Stack* const ss, const Move m, const int bonus) noexcept {
    assert(m.is_ok());

    update_pawn_history(pawnHistory, pos.moved_pc(m), m.dst_sq(), constexpr_round((0.4482 + int(bonus > -4) * 0.6299) * double(bonus)));

    update_quiet_history(pos.active_color(), m, constexpr_round(1.0000 * double(bonus)));

    update_low_ply_quiet_history(ss->ply, m, constexpr_round(0.6953 * double(bonus)));

    update_continuation_histories(ss, pos.moved_pc(m), m.dst_sq(), constexpr_round(0.7324 * double(bonus)));
}

// Updates history at the end of search() when a bestMove is found and other searched moves are known
void Worker::update_histories(const Position& pos, PawnHistory& pawnHistory, Stack* const ss, const Depth depth, const Move bestMove, const bool extra, const Array<SearchedMoves, 2>& searchedMoves) noexcept {
    assert(depth > DEPTH_ZERO);
    assert(ss->moveCount != 0);

    int bonus = std::clamp(-81 + 133 * depth + std::min(constexpr_round(31.2500e-3 * double((ss - 1)->history) / double(depth)), 512), +4, +1888)
              + int(extra) * 364;

    int malus = std::min(-235 + 968 * depth, +2244);

    if (pos.capture_promo(bestMove))
    {
        update_capture_history(pos, bestMove, constexpr_round(1.3936 * double(bonus)));
    }
    else
    {
        update_quiet_histories(pos, pawnHistory, ss, bestMove, constexpr_round(0.8779 * double(bonus)));

        // Decrease history for all non-best quiet moves
        int decayQuietMalus = constexpr_round(1.0180 * double(malus));
        for (const Move qm : searchedMoves[0])
        {
            update_quiet_histories(pos, pawnHistory, ss, qm, -decayQuietMalus);
            decayQuietMalus = constexpr_round(0.8994 * double(decayQuietMalus));
        }
    }

    // Decrease history for all non-best capture moves
    int decayCaptureMalus = constexpr_round(1.4541 * double(malus));
    for (const Move cm : searchedMoves[1])
    {
        update_capture_history(pos, cm, -decayCaptureMalus);
        decayCaptureMalus = constexpr_round(0.9900 * double(decayCaptureMalus));
    }

    // Extra penalty for a quiet early move that was not a TT move in the previous ply when it gets refuted
    Stack* const ss1 = ss - 1;
    if (ss1->move.is_ok() && pos.captured_pc() == Piece::NO_PIECE && ss1->moveCount == 1 + int(ss1->ttMove != Move::None))
    {
        const Square preSq = ss1->move.dst_sq();
        update_continuation_histories(ss1, pos[preSq], preSq, -constexpr_round(0.6963 * double(malus)));
    }
}

// Updates correction histories at the end of search() when a bestMove is found
void Worker::update_correction_histories(const Position& pos, const Stack* const ss, int bonus) noexcept {
    constexpr double    PawnBonusScale = 1.0000;
    constexpr double   MinorBonusScale = 1.1719;
    constexpr double NonPawnBonusScale = 1.4531;

    const Color ac = pos.active_color();

    bonus = std::clamp(bonus, -CORRECTION_HISTORY_LIMIT / 4, +CORRECTION_HISTORY_LIMIT / 4);

    histories.    pawn_correction<WHITE>(pos.    pawn_key(WHITE))[ac] << constexpr_round(   PawnBonusScale * double(bonus));
    histories.    pawn_correction<BLACK>(pos.    pawn_key(BLACK))[ac] << constexpr_round(   PawnBonusScale * double(bonus));
    histories.   minor_correction<WHITE>(pos.   minor_key(WHITE))[ac] << constexpr_round(  MinorBonusScale * double(bonus));
    histories.   minor_correction<BLACK>(pos.   minor_key(BLACK))[ac] << constexpr_round(  MinorBonusScale * double(bonus));
    histories.non_pawn_correction<WHITE>(pos.non_pawn_key(WHITE))[ac] << constexpr_round(NonPawnBonusScale * double(bonus));
    histories.non_pawn_correction<BLACK>(pos.non_pawn_key(BLACK))[ac] << constexpr_round(NonPawnBonusScale * double(bonus));

    const Move preMove = (ss - 1)->move;
    if (preMove.is_ok())
    {
        const Square preSq = preMove.dst_sq();
        const Piece  prePc = pos[preSq];

        (*(ss - 2)->pieceSqCorrectionHistory)[+prePc][preSq] << constexpr_round(1.0156 * double(bonus));
        (*(ss - 4)->pieceSqCorrectionHistory)[+prePc][preSq] << constexpr_round(0.5469 * double(bonus));
    }
}

// Computes the correction value for the current position from the correction histories
int Worker::correction_value(const Position& pos, const Stack* const ss) const noexcept {
    const Color ac = pos.active_color();

    i64 correctionValue =
           + i64{7669} * int(histories.    pawn_correction<WHITE>(pos.    pawn_key(WHITE))[ac]
                           + histories.    pawn_correction<BLACK>(pos.    pawn_key(BLACK))[ac])
           + i64{5284} * int(histories.   minor_correction<WHITE>(pos.   minor_key(WHITE))[ac]
                           + histories.   minor_correction<BLACK>(pos.   minor_key(BLACK))[ac])
           +i64{12906} * int(histories.non_pawn_correction<WHITE>(pos.non_pawn_key(WHITE))[ac]
                           + histories.non_pawn_correction<BLACK>(pos.non_pawn_key(BLACK))[ac]);

    const Move preMove = (ss - 1)->move;
    if (preMove.is_ok())
    {
        const Square preSq = preMove.dst_sq();
        const Piece  prePc = pos[preSq];

        correctionValue += i64{8761} * int((*(ss - 2)->pieceSqCorrectionHistory)[+prePc][preSq]
                                         + (*(ss - 4)->pieceSqCorrectionHistory)[+prePc][preSq]);
    }
    else
        correctionValue += i64{64049};

    return std::clamp(correctionValue, -INT_LIMIT, +INT_LIMIT);
}

// clang-format on

int Worker::history_value(const bool                             capture,
                          const Move                             m,
                          const Piece                            movedPc,
                          const PieceType                        capturedPt,
                          const Color                            ac,
                          const History<HType::PIECE_SQ>** const contHistory) const noexcept {
    return int(capture ? 6.8203 * piece_value(capturedPt)                      //
                           + captureHistory[+movedPc][m.dst_sq()][capturedPt]  //
                       : 2.1992 * quietHistory[ac][m.raw()]                    //
                           + 1.0996 * (*contHistory[0])[+movedPc][m.dst_sq()]  //
                           + 1.0673 * (*contHistory[1])[+movedPc][m.dst_sq()]);
}

int Worker::history_value(const Position&                        pos,
                          const Move                             m,
                          const Color                            ac,
                          const History<HType::PIECE_SQ>** const contHistory) const noexcept {
    Piece movedPc    = pos.moved_pc(m);
    bool  capture    = pos.capture_promo(m);
    auto  capturedPt = capture ? pos.captured_pt(m) : NO_PIECE_TYPE;

    return history_value(capture, m, movedPc, capturedPt, ac, contHistory);
}

// Called in case have no ponder move before exiting the search,
// for instance, in case stop the search during a fail high at root.
// Try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' have nothing to think about.
bool Worker::ponder_move_extracted() noexcept {
    static std::mt19937 prng(std::random_device{}());

    auto& rm0 = rootMoves[0];
    assert(rm0.pv.size() == 1);

    const Move bestMove = rm0.pv[0];
    assert(bestMove != Move::None);

    State st;
    rootPos.do_move(bestMove, st, true, this);

    if (!rootPos.is_draw(1))
    {
        // Legal moves for the opponent
        MoveList<GenType::LEGAL> legalMoves(rootPos);

        if (!legalMoves.empty())
        {
            Move ponderMove;

            auto [ttd, ttu] = transpositionTable.probe(rootPos.key());

            ponderMove = ttd.hit ? legal_move(ttd.move, rootPos) : Move::None;

            if (ponderMove == Move::None || !legalMoves.contains(ponderMove))
            {
                ponderMove = Move::None;

                for (auto&& th : threads)
                {
                    if (th->worker.get() == this || th->worker->completedDepth <= DEPTH_ZERO)
                        continue;
                    if (const auto& rm = th->worker->rootMoves[0];
                        rm.pv[0] == bestMove && rm.pv.size() > 1)
                    {
                        ponderMove = rm.pv[1];
                        break;
                    }
                }

                if (ponderMove == Move::None)
                    for (auto&& th : threads)
                    {
                        if (th->worker.get() == this || th->worker->completedDepth <= DEPTH_ZERO)
                            continue;
                        if (const auto& rm = *th->worker->rootMoves.find(bestMove);
                            rm.pv.size() > 1)
                        {
                            ponderMove = rm.pv[1];
                            break;
                        }
                    }

                if (ponderMove == Move::None)
                {
                    std::uniform_int_distribution<usize> distribution(0, legalMoves.size() - 1);
                    ponderMove = *(legalMoves.begin() + distribution(prng));
                }
            }

            rm0.pv.push_back(ponderMove);
        }
    }

    rootPos.undo_move(bestMove);

    return rm0.pv.size() > 1;
}

// Used to correct and extend PVs for moves that have a TB (but not a mate) score.
// Keeps the search based PV for as long as it is verified to maintain the game outcome, truncates afterward.
// Finally, extends to mate the PV, providing a possible continuation (but not a proven mating line).
void Worker::extend_tb_pv(const usize index, Value& value) noexcept {
    assert(index < rootMoves.size());

    if (!options["SyzygyPVExtend"])
        return;

    const TimePoint OverheadTime = options["OverheadTime"];
    const bool      UseRule50    = options["Syzygy50MoveRule"];

    // If time manager is active, don't use more than 50% of OverheadTime time
    const auto startTime = std::chrono::steady_clock::now();

    auto time_to_abort = [&]() noexcept -> bool {
        const auto endTime = std::chrono::steady_clock::now();
        return limit.use_time_manager()
            && (options["NodesTime"] != 0
                || std::chrono::duration<double, std::milli>(endTime - startTime).count()
                     > 0.5000 * OverheadTime);
    };

    bool aborted = false;

    auto& rootMove = rootMoves[index];

    std::list<State> states;

    // Step 0. Do the rootMove, no correction allowed, as needed for MultiPV in TB
    State& rootSt = states.emplace_back();
    rootPos.do_move(rootMove.pv[0], rootSt);

    i16 ply = 1;
    // Step 1. Walk the PV to the last position in TB with correct decisive score
    while (usize(ply) < rootMove.pv.size())
    {
        const Move pvMove = rootMove.pv[ply];

        RootMoves rms;

        for (const Move m : MoveList<GenType::LEGAL>(rootPos))
            rms.emplace_back(m);

        const auto tbCfg =
          Tablebase::Syzygy::rank_root_moves(rootPos, rms, options, false, time_to_abort);

        if (rms.find(pvMove)->tbRank != rms[0].tbRank)
            break;

        State& st = states.emplace_back();
        rootPos.do_move(pvMove, st);
        ++ply;

        // Don't allow for repetitions or drawing moves along the PV in TB regime
        if (tbCfg.rootInTB && rootPos.is_draw(ply, UseRule50))
        {
            --ply;
            rootPos.undo_move(pvMove);
            break;
        }

        // Full PV shown will thus be validated and end in TB.
        // If can not validate the full PV in time, do not show it.
        if (tbCfg.rootInTB && time_to_abort())
        {
            aborted = true;
            break;
        }
    }

    // Resize the PV to the correct part
    rootMove.pv.resize(ply);

    // Step 2. Now extend the PV to mate, as if the user explores syzygy-tables.info using
    // top ranked moves (minimal DTZ), which gives optimal mates only for simple endgames e.g. KRvK
    while (!(UseRule50 && rootPos.is_draw(0)))
    {
        if (aborted)
            break;
        if (time_to_abort())
        {
            aborted = true;
            break;
        }

        RootMoves rms;

        for (const Move m : MoveList<GenType::LEGAL>(rootPos))
        {
            auto& rm = rms.emplace_back(m);

            State st;
            rootPos.do_move(m, st);
            // Give a score of each move to break DTZ ties
            // restricting opponent mobility, but not giving the opponent a capture.
            for (const Move om : MoveList<GenType::LEGAL>(rootPos))
                rm.tbRank -= 1 + 99 * rootPos.capture(om);

            rootPos.undo_move(m);
        }

        // Mate found
        if (rms.empty())
            break;

        // Sort moves according to their above assigned TB rank.
        // This will break ties for moves with equal DTZ in rank_root_moves.
        rms.sort(root_move_descending);

        // The winning side tries to minimize DTZ, the losing side maximizes it
        const auto tbCfg =
          Tablebase::Syzygy::rank_root_moves(rootPos, rms, options, true, time_to_abort);

        // If DTZ is not available might not find a mate, so bail out
        if (!tbCfg.rootInTB || tbCfg.cardinality != 0)
            break;

        const Move pvMove = rms[0].pv[0];
        rootMove.pv.push_back(pvMove);
        State& st = states.emplace_back();
        rootPos.do_move(pvMove, st);
    }

    // Finding a draw in this function is an exceptional case,
    // that cannot happen when rule50 is false or during engine game play,
    // since have a winning score, and play correctly with TB support.
    // However, it can be that a position is draw due to the 50 move rule
    // if it has been reached on the board with a non-optimal 50 move counter
    // (e.g. 8/8/6k1/3B4/3K4/4N3/8/8 w - - 54 106) which TB with dtz counter rounding
    // cannot always correctly rank.
    // Adjust the score to match the found PV. Note that a TB loss score can be displayed
    // if the engine did not find a drawing move yet, but eventually search will figure it out.
    // (e.g. 1kq5/q2r4/5K2/8/8/8/8/7Q w - - 96 1)
    if (rootPos.is_draw(0))
        value = VALUE_DRAW;

    // Undo the PV moves
    for (usize i = rootMove.pv.size(); i-- > 0;)
        rootPos.undo_move(rootMove.pv[i]);

    if (aborted)
        print_info_string(
          "Syzygy based PV extension requires more time, increase Overhead-Time as needed.");
}

MainSearchManager::MainSearchManager(const UpdateContext& updateCtx) noexcept :
    updateContext(updateCtx) {}

// Initializes the time manager and resets previous search info
void MainSearchManager::reset() noexcept {

    timeManager.reset();
    preBestCurValue  = VALUE_ZERO;
    preBestAvgValue  = VALUE_ZERO;
    preTimeReduction = 0.85;
    atFirst          = true;
}

// Used to print debug info and, more importantly,
// to detect when out of available time and thus stop the search.
void MainSearchManager::check_time(Worker& worker) noexcept {
    assert(callsCount > 0);
    if (--callsCount > 0)
        return;
    // When using nodes, ensure checking rate is not lower than 0.1% of nodes
    callsCount = worker.limit.calls_count();

    const TimePoint elapsedTime = elapsed(worker.threads);

#if !defined(NDEBUG)
    static TimePoint infoTime = now();

    if (const TimePoint curTime = worker.limit.startTime + elapsedTime; curTime - infoTime > 1000)
    {
        infoTime = curTime;
        Debug::print();
    }
#endif

    // Should not stop pondering until told so by the GUI
    if (ponder)
        return;

    // clang-format off
    if ((worker.limit.use_time_manager() &&      (ponderhitStop || elapsedTime >= timeManager.maximum()))
     || (worker.limit.moveTime != 0      &&                        elapsedTime >= worker.limit.moveTime)
     || (worker.limit.nodes != 0         && worker.threads.sum(&Worker::nodes) >= worker.limit.nodes))
        worker.threads.request_stop();
    // clang-format on
}

// Returns the actual time elapsed since the start of the search.
// This function is intended for use only when printing PV outputs,
// and not used for making decisions within the search algorithm itself.
TimePoint MainSearchManager::elapsed() const noexcept { return timeManager.elapsed(); }
// Returns the time elapsed since the search started.
// If the 'NodesTime' option is enabled, return the count of nodes searched instead.
// This function is called to check whether the search should be stopped
// based on predefined thresholds like total time or total nodes.
TimePoint MainSearchManager::elapsed(const Threads& threads) const noexcept {
    return timeManager.elapsed([&threads]() { return threads.sum(&Worker::nodes); });
}

void MainSearchManager::handle_time_management(const Worker& worker,
                                               const Value   bestValue,
                                               const Depth   lastBestMoveDepth) noexcept {

    // Use part of the gained time from a previous stable move for the current move
    sumMoveChanges += worker.threads.sum(&Worker::moveChanges);

    // Reset move changes
    worker.threads.set(&Worker::moveChanges, 0U);

    // clang-format off

    // Compute evaluation inconsistency based on differences from previous best scores
    const double inconsistencyFactor = std::clamp(0.1148
                                                + 0.0230 * (preBestAvgValue - bestValue)
                                                + 0.0011 * (preBestCurValue - bestValue),
                                                1.0000 - int(!atFirst) * 0.4240,
                                                1.0000 + int(!atFirst) * 0.7280);

    // Compute stable depth (difference between the current search depth and the last best depth)
    const Depth stableDepth = worker.completedDepth - lastBestMoveDepth;
    assert(stableDepth >= DEPTH_ZERO);

    // Use the stability factor to adjust the time reduction
    timeReduction = std::clamp(interpolate(double(stableDepth), 4.96, 18.79, 0.6390, 1.7120), 0.6290, 1.5440);

    // Compute ease factor that factors in previous time reduction
    const double easeFactor = 0.4378 * (1.4680 + preTimeReduction) / timeReduction;

    // Compute move instability factor based on the total move changes and the number of threads
    const double instabilityFactor = 1.0770 + 2.2290 * sumMoveChanges / std::max<usize>(worker.thread_count(), 1);

    // Compute node effort factor that reduces time if root move has consumed a large fraction of total nodes
    const u64 nodesEffort = 100000 * worker.rootMoves[0].nodes / std::max<u64>(worker.nodes_(), 1);

    const double nodesEffortFactor = std::clamp(interpolate(i64(nodesEffort), i64(75800), i64(104510), 0.9690, 0.7140), 0.6930, 0.8380);

    // Compute recapture factor that reduces time if recapture conditions are met
    const double recaptureFactor = 1.0 - int( worker.rootPos.captured_sq() == worker.rootMoves[0].pv[0].dst_sq()
                                    && (worker.rootPos.captured_sq() & worker.rootPos.pieces_bb(~worker.rootPos.active_color())) != 0
                                    &&  worker.rootPos.see(worker.rootMoves[0].pv[0]) >= 200)
                                    * 4.0040e-3 * std::min<Depth>(stableDepth, 25);

    // Calculate total time by combining all factors with the optimum time
    TimePoint totalTime = constexpr_ceil(timeManager.optimum() * inconsistencyFactor * easeFactor * instabilityFactor * nodesEffortFactor * recaptureFactor);
    assert(totalTime >= 0.0);
    // clang-format on

    // Cap totalTime to the available maximum time
    totalTime = std::min<TimePoint>(timeManager.maximum(), totalTime);
    // Cap totalTime to either 55% of total time or MaxForcedMoveTime ms in case of forced move for a better viewer experience
    if (worker.rootMoves.size() == 1)
        totalTime = std::min<TimePoint>(55 * totalTime / 100, worker.options["MaxForcedMoveTime"]);

    const TimePoint elapsedTime = elapsed(worker.threads);

    // Stop the search if have exceeded the total time
    if (elapsedTime > totalTime)
    {
        // If allowed to ponder do not stop the search now but
        // keep pondering until the GUI sends "ponderhit" or "stop".
        if (ponder)
            ponderhitStop = true;
        else
            worker.threads.request_stop();
    }

    if (!worker.threads.is_researching() && !ponder && elapsedTime > 0.5000 * totalTime)
        worker.threads.request_research();

    preBestCurValue = bestValue;
}

// Displays the principal variation (PV) along with associated information
void MainSearchManager::show_pv(Worker& worker, const Depth depth) const noexcept {
    //assert(depth > DEPTH_ZERO);

    const auto& rootPos            = worker.rootPos;
    const auto& rootMoves          = worker.rootMoves;
    const auto& options            = worker.options;
    const auto& threads            = worker.threads;
    const auto& transpositionTable = worker.transpositionTable;
    const auto& tbConfig           = worker.tbConfig;
    const usize multiPv            = worker.multiPv;
    const usize pvCur              = worker.pvCur;
    // Ensure non-zero to avoid a 'divide by zero'
    const TimePoint time   = std::max<TimePoint>(elapsed(), 1);
    const u64       nodes  = threads.sum(&Worker::nodes);
    const u64       tbHits = threads.sum(&Worker::tbHits, tbConfig.rootInTB ? rootMoves.size() : 0);
    const u16       hashfull = transpositionTable.hashfull();
    const bool      ShowWDL  = options["UCI_ShowWDL"];

    for (usize i = 0; i < multiPv; ++i)
    {
        const auto& rm = rootMoves[i];

        const bool updated = rm.curValue != -VALUE_INFINITE;

        if (i != 0 && depth == 1 && !updated)
            continue;

        const Depth d = updated || depth <= 1 ? depth : depth - 1;
        Value       v = updated ? rm.uciValue : rm.preValue;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        const bool tb = tbConfig.rootInTB && !is_mate(v);

        if (tb)
            v = rm.tbValue;

        // tablebase- and previous-scores are exact
        bool exact = tb || !updated || i != pvCur;

        // Potentially correct and extend the PV, and in exceptional cases value also
        if ((exact || rm.bound == Bound::NONE) && is_decisive(v) && !is_mate(v))
            worker.extend_tb_pv(i, v);

        FixedText score{to_score({v, rootPos})};

        FixedText bound;
        if (!exact && is_ok(rm.bound))
            bound = FixedText::from_view(to_string(rm.bound));

        FixedText wdl;
        if (ShowWDL)
            wdl = to_wdl(v, rootPos);

        std::string pv{rm.pv.build_pv()};

        updateContext.onUpdateFull(
          {{d, score}, rm.selDepth, i + 1, bound, wdl, time, nodes, tbHits, hashfull, pv});
    }
}

void MainSearchManager::set_ponder(const bool p) noexcept {
    std::lock_guard writeLock(mutex);

    ponder = p;

    condVar.notify_one();
}

// Skill module for playing at reduced strength
void Skill::init(const Options& options) noexcept {

    if (options["UCI_LimitStrength"])
    {
        constexpr Array<double, 4> P{37.2473, -40.8525, 22.2943, -0.311438};

        double e = double(options["UCI_ELO"] - ELO_MIN) / (ELO_MAX - ELO_MIN);

        double l = ((P[0] * e + P[1]) * e + P[2]) * e + P[3];

        level = std::clamp(l, LEVEL_MIN, LEVEL_MAX - 0.01);
    }
    else
    {
        level = options["SkillLevel"];
    }

    assert(level <= LEVEL_MAX);

    bestMove = Move::None;
}

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move Skill::pick_move(const RootMoves& rootMoves, usize multiPv, bool pickBest) noexcept {
    assert(1 <= multiPv && multiPv <= rootMoves.size());
    static XorShift64Star prng(now());  // PRNG sequence should be non-deterministic

    if (pickBest || bestMove == Move::None)
    {
        // RootMoves are already sorted by value in descending order
        Value maxValue = rootMoves[0].curValue;

        Value delta = std::min<int>(maxValue - rootMoves[multiPv - 1].curValue, VALUE_PAWN);

        Value bestValue = -VALUE_INFINITE;
        // Choose best move. For each move value add two terms, both dependent on weakness.
        // One is deterministic and bigger for weaker levels, and one is random.
        // Then choose the move with the resulting highest value.
        for (usize i = 0; i < multiPv; ++i)
        {
            Value curValue = rootMoves[i].curValue;
            Value diff     = maxValue - curValue;
            Value noise    = prng.rand<u32>() % weakness();
            Value push     = (weakness() * diff + delta * noise) / 128;
            Value value    = curValue + push;

            if (bestValue <= value)
            {
                bestValue = value;
                bestMove  = rootMoves[i].pv[0];
            }
        }
    }

    return bestMove;
}

}  // namespace DON
