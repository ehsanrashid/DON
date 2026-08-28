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

constexpr Depth OUTPUT_DEPTH_LIMIT = 30;

constexpr Array<int, 16> LMR_DIVISORS{
  3307, 2930, 2874, 2818, 3215, 3225, 3224, 2782,  //
  2858, 2919, 3088, 3275, 3180, 2868, 3006, 3599   //
};

// Reductions lookup table using [depth or moveCount]
alignas(CACHE_LINE_SIZE) constexpr auto REDUCTIONS = []() constexpr noexcept {
    Array<u16, MOVE_MAX> reductions{};

    reductions[0] = 0;
    for (usize i = 1; i < reductions.size(); ++i)
        reductions[i] = static_cast<u16>(22.140625 * constexpr_log(i));

    return reductions;
}();

constexpr int reduction(const Depth depth,
                        const u16   moveCount,
                        const int   deltaRatio,
                        const bool  improve) noexcept {
    int reductionScale = REDUCTIONS[depth] * REDUCTIONS[moveCount];
    return std::max(1027 + reductionScale - deltaRatio
                      + int(!improve) * constexpr_ceil(reductionScale * 194.0 / 512.0),
                    0);
}

// Add a small random value to draw evaluation to avoid 3-fold blindness
constexpr Value draw_value(const u64 nodes) noexcept {
    return VALUE_DRAW - 1 + static_cast<Value>(nodes & 2);
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

constexpr Bound fail_bound(const bool failHigh) noexcept {
    return failHigh ? Bound::LOWER : Bound::UPPER;
}

Move legal_move(const Move m, const Position& pos) noexcept {
    return m != Move::None && pos.legal(m) ? m : Move::None;
}

// Build contHistory pointers from the stack frame and validate them in debug builds.
void build_continuation_histories(const Stack* const    ss,
                                  const PieceSqHistory* contHistory[CONT_HISTORY_COUNT]) noexcept {
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
void update_continuation_histories(const Stack* const ss,
                                   const Piece        pc,
                                   const Square       dstSq,
                                   const int          bonus) noexcept {
    assert(dstSq != SQ_NONE);

    constexpr Array<double, CONT_HISTORY_COUNT> ContHistoryWeights{
      1040.0, 780.0, 300.0, 537.0, 129.0, 423.0, 112.0, 121.0  //
    };
    constexpr Array<int, CONT_HISTORY_COUNT + 1> Multipliers{
      96, 113, 101, 105, 127, 121, 126, 128, 130  //
    };

    // In check only update 2-ply continuation history
    const usize ContHistoryCount = ss->inCheck ? 2 : CONT_HISTORY_COUNT;

    int positiveCount = 0;

    for (usize i = 0; i < ContHistoryCount; ++i)
    {
        const Stack* ssi = (ss - 1) - i;

        if (!ssi->move.is_ok())
            break;

        auto& historyEntry = (*ssi->pieceSqHistory)[+pc][dstSq];
        if (historyEntry > 0)
            ++positiveCount;

        historyEntry << constexpr_round(bonus * ContHistoryWeights[i] * Multipliers[positiveCount]
                                        / 131072.0)
                          + int(i < 1) * 71;
    }
}

// Adjust raw evaluation according to various correction histories value
// and guarantee evaluation does not hit the tablebase range.
Value adjust_eval_value(const Value evalue, const int correctionValue) noexcept {
    return in_range(evalue + constexpr_round(correctionValue / 131072.0));
}

bool is_shuffling(const Position& pos, const Stack* const ss, const Move move) noexcept {
    return !(pos.capture_promo(move) || pos.rule50_count() < 10 || pos.null_ply() < 6
             || ss->ply < 20)
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
    accCache(network[accessToken]),
    atomicHistories(sharedState.atomicHistoriesMap.at(accessToken.numa_id())) {}

// Reset per-thread data structures
void Worker::reset() noexcept {
    assert(thread_count() == threads.size());

    captureHistory.fill(-699);
    quietHistory.fill(-5);

    for (bool inCheck : {false, true})
        for (bool capture : {false, true})
            for (auto& toPieceSqHist : continuationHistory[inCheck][capture])
                for (auto& pieceSqHist : toPieceSqHist)
                    pieceSqHist.fill(-552);

    for (auto& toPieceSqCorrHist : continuationCorrectionHistory)
        for (auto& pieceSqCorrHist : toPieceSqCorrHist)
            pieceSqCorrHist.fill(5);

    ttMoveHistory = 0;

    // Each thread resets its NUMA-local range of history entries to prevent false sharing

    auto pawnHistoryRange =
      split_range(numa_id(), numa_thread_count(), atomicHistories.pawn_history_size());

    atomicHistories.pawn_history().fill(pawnHistoryRange.beg, pawnHistoryRange.end, -1262);

    auto correctionHistoryRange =
      split_range(numa_id(), numa_thread_count(), atomicHistories.correction_history_size());

    atomicHistories.pawn_correction_history().fill(correctionHistoryRange.beg,
                                                   correctionHistoryRange.end, -6);
    atomicHistories.minor_correction_history().fill(correctionHistoryRange.beg,
                                                    correctionHistoryRange.end, -6);
    atomicHistories.non_pawn_correction_history().fill(correctionHistoryRange.beg,
                                                       correctionHistoryRange.end, -6);

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

            RootMoves orms;
            for (const Move m : MoveList<GenType::LEGAL>(rootPos))
                orms.emplace_back(m);

            Move bookPonderMove = pgBook.probe(rootPos, orms, options);

            rootPos.undo_move(bookBestMove);

            for (auto&& th : threads)
            {
                auto& rms = th->worker->rootMoves;

                rms.swap_to_front(bookBestMove);

                if (bookPonderMove != Move::None)
                    rms[0].push_back(bookPonderMove);
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
            else if (thread_count() > 1)
            {
                if (limit.mate != 0)
                    bestWorker = threads.best_thread<true>()->worker.get();
                else
                    bestWorker = threads.best_thread<false>()->worker.get();
            }

            if (limit.use_time_manager())
            {
                mainManager->preBestValue     = bestWorker->rootMoves[0].value;
                mainManager->preBestAvgValue  = bestWorker->rootMoves[0].avgValue;
                mainManager->preTimeReduction = mainManager->timeReduction;
                mainManager->atFirst          = false;
            }
        }

        assert(!bestWorker->rootMoves.empty() && !bestWorker->rootMoves[0].empty());

        const auto& rm0 = bestWorker->rootMoves[0];

        if (rm0.size() == 1 && bestWorker->ponder_move_extracted())
            mainManager->pvShown = false;

        // Send PV info again if it has changed since last output
        if (!mainManager->pvShown || bestWorker != this)
        {
            const Depth rDepth = limit.depth != DEPTH_ZERO ? limit.depth : bestWorker->rootDepth;
            mainManager->show_pv(*bestWorker, rDepth);
        }

        bestMove   = move_to_can(rm0[0]);
        ponderMove = move_to_can(rm0.size() > 1 ? rm0[1] : Move::None);
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

    for (auto& colorQuietHist : quietHistory)
        for (auto& quietHist : colorQuietHist)
            quietHist = constexpr_round(quietHist * 789.0 / 1024.0);

    lowPlyQuietHistory.fill(100);

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

    for (i16 i = 0 - StackOffset; i < static_cast<i16>(stacks.size()) - StackOffset; ++i)
    {
        (ss + i)->ply = i;

        if (i >= 0)
            continue;

        // Set sentinel values
        // clang-format off
        (ss + i)->evalue                   = VALUE_NONE;
        (ss + i)->pieceSqHistory           = &continuationHistory[0][0][+Piece::NO_PIECE][SQUARE_ZERO];
        (ss + i)->pieceSqCorrectionHistory = &continuationCorrectionHistory[+Piece::NO_PIECE][SQUARE_ZERO];
        // clang-format on
    }

    assert(stacks[0].ply == -StackOffset && stacks[stacks.size() - 1].ply == PLY_MAX + 1);
    assert(ss->ply == 0);

    PVMoves pv;

    ss->pv = &pv;

    Value bestValue = -VALUE_INFINITE;

    Depth lastBestMoveDepth = DEPTH_ZERO;
    Move  lastBestMove      = Move::None;
    Value lastBestValue     = -VALUE_INFINITE;

    u16 researchCnt = 0;

    // Iterative deepening loop
    const Depth maxDepth = limit.depth != DEPTH_ZERO ? std::min(limit.depth, DEPTH_MAX) : DEPTH_MAX;
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
                rootMoves[i].preValue = rootMoves[i].value;
                rootMoves[i].prePV    = rootMoves[i].pv;
                ++i;
            } while (i < rootMovesSize && rootMoves[i].tbRank == tbRank);
        }
        // Sentinel (critical) to simplify pvEnd access
        tbRankGroups[tbRankGroupCnt] = rootMovesSize;

        // Index in tbRankGroups
        usize tbRankGroupIdx = 0;
        usize pvBeg = pvEnd = 0;
        // MultiPV loop. Perform a full root search for each PV line
        for (pvIdx = 0; pvIdx < multiPv; ++pvIdx)
        {
            const bool pvIdxLast = pvIdx + 1 == multiPv;

            // Advance group if pvIdx reached pvEnd
            if (pvIdx == pvEnd)
            {
                pvBeg = tbRankGroups[tbRankGroupIdx];
                pvEnd = tbRankGroups[tbRankGroupIdx + 1];  // safe because of sentinel
                ++tbRankGroupIdx;
            }

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = 1;

            const auto& rmIdx = rootMoves[pvIdx];

            idxPrePV = rmIdx.prePV;

            const auto avgValue    = rmIdx.avgValue;
            const auto avgSqrValue = rmIdx.avgSqrValue;

            // Reset aspiration window starting size
            int   delta = 5 + thread_id() % 8 + constexpr_abs(avgSqrValue) / 10588.0;
            Value alpha = std::max<int>(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min<int>(avgValue + delta, +VALUE_INFINITE);

            // Adjust optimism based on root move's avgValue
            optimism[ac]  = constexpr_round(137.0 * avgValue / (81.0 + constexpr_abs(avgValue)));
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

                // Bring the best move to the front. A stable sort is critical here because
                // all moves except the first and, eventually, the new best move have a score of -VALUE_INFINITE.
                // Stability preserves their existing order, while moving only the new PV move to the front.
                // In MultiPV search, already searched PV lines are therefore preserved.
                rootMoves.sort(pvIdx, pvEnd);

                // If search has been stopped, break immediately.
                // RootMoves remains valid, although it refers to the previous iteration.
                if (threads.is_stopped())
                    break;

                // When failing high/low give some update before a re-search
                if (mainManager != nullptr && multiPv == 1 && rootDepth > OUTPUT_DEPTH_LIMIT
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

                delta = std::min<int>(constexpr_ceil(delta * 172.0 / 128.0), DELTA_MAX);

                assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
            }

            // In multiPV analysis do not let aborted searches spoil
            // mated-in/TB loss from a completed search in an earlier PV line.
            // Hence guard against an aborted pvIdx line overtaking pvIdx - 1
            // when pvIdx - 1 is a proven loss.
            // Moreover, do not trust an exact loss value from an aborted search.
            if (pvIdx != 0 && threads.is_stopped())
            {
                auto& rmIdx_0 = rootMoves[pvIdx - 0];
                auto& rmIdx_1 = rootMoves[pvIdx - 1];

                if (rmIdx_0.is_exact_loss() || (is_loss(rmIdx_1.value) && rmIdx_0 < rmIdx_1))
                {
                    // If the previous score is worse than pvIdx - 1, can safely use it.
                    // If it is equal, make sure it cannot overtake pvIdx - 1.
                    if (rmIdx_0.preValue != -VALUE_INFINITE && rmIdx_0.preValue <= rmIdx_1.value)
                    {
                        rmIdx_0.value = rmIdx_0.uciValue = rmIdx_0.preValue;
                        rmIdx_0.preValue                 = -VALUE_INFINITE;
                        rmIdx_0.pv                       = rmIdx_0.prePV;
                        rmIdx_0.reset_bound();
                    }
                    // Otherwise, if can, cap the score to the best possible, and mark
                    // the score as a bound (also a valid excuse for the incomplete PV)
                    else
                    {
                        if (is_loss(rmIdx_1.value))
                        {
                            rmIdx_0.value = rmIdx_0.uciValue = rmIdx_1.value;
                            rmIdx_0.preValue                 = -VALUE_INFINITE;
                            rmIdx_0.shrink_to(std::min(rmIdx_0.size(), rmIdx_1.size()));
                            rmIdx_0.bound = Bound::UPPER;
                        }
                        else
                            rmIdx_0.bound = Bound::LOWER;
                    }
                }
            }

            // Sort the PV lines searched so far
            rootMoves.sort(pvBeg, pvIdx + 1);

            if (threads.is_stopped())
                break;

            // Give some update about the PV
            if (mainManager != nullptr && (pvIdxLast || rootDepth > OUTPUT_DEPTH_LIMIT))
            {
                mainManager->show_pv(*this, rootDepth);
                mainManager->pvShown = pvIdxLast;
            }

            if (threads.is_stopped())
                break;
        }

        auto& rm0 = rootMoves[0];

        const bool mateForgotten =
          lastBestValue != -VALUE_INFINITE && is_mate(lastBestValue)
          && (constexpr_abs(rm0.value) < constexpr_abs(lastBestValue) || rm0.is_bound());

        if (threads.is_stopped())
        {
            const bool lossAborted = pvIdx == 0 && rm0.is_exact_loss();

            // An exact mated-in/TB-loss score from an aborted search cannot be trusted:
            // the loss could be delayed or refuted upon exploring the remaining root-moves.
            // Thus here roll back to the score from the previous iteration.
            // Do the same if a search has failed to recover a mate score that was found in a previous iteration.
            if (lossAborted || (mateForgotten && rm0.value != -VALUE_INFINITE))
            {
                if (lastBestMove != Move::None)
                {
                    // Bring the last best move to the front for best thread selection
                    rootMoves.move_to_front(
                      [&lastBestMove = std::as_const(lastBestMove)](const auto& rm) noexcept {
                          return rm == lastBestMove;
                      });
                    rm0.value = rm0.uciValue = rm0.preValue;
                    rm0.pv                   = rm0.prePV;
                    rm0.reset_bound();

                    if (mainManager != nullptr)
                        mainManager->pvShown = false;
                }
                // For aborted (depth 1) search, label the loss score as lower bound
                else if (lossAborted)
                    rm0.bound = Bound::LOWER;
            }

            break;
        }

        if (lastBestMove != rm0[0])
            lastBestMoveDepth = rootDepth;

        // Do not replace (shorter) mate scores from a previous iteration
        if (!mateForgotten)
        {
            lastBestMove  = rm0[0];
            lastBestValue = rm0.value;
        }

        // Have found "mate in x"?
        if (limit.mate != 0 && is_mate(rm0.value)
            && VALUE_MATE - constexpr_abs(rm0.value) <= 2 * limit.mate)
        {
            threads.request_stop();
            break;
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

// The main alpha-beta search function with negamax framework and
// various enhancements like aspiration windows, late move reductions, etc.
template<NT T>
Value Worker::search(Position&    pos,
                     Stack* const ss,
                     Value        alpha,
                     Value        beta,
                     Depth        depth,
                     const i16    red,
                     const Move   excludedMove) noexcept {
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
            alpha = draw_value(nodes);

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

    const usize pvPreIdx = std::max<int>((ss - 1)->ply, 0);

    // Step 1. Initialize node
    ss->inCheck   = pos.checkers_bb() != 0;
    ss->moveCount = 0;
    ss->history   = 0;
    ss->pvFollow  = RootNode
                 || ((ss - 1)->pvFollow
                     && (pvPreIdx < idxPrePV.size() && (ss - 1)->move == idxPrePV[pvPreIdx]));

    if constexpr (!RootNode)
    {
        // Step 2. Check for stopped search or maximum ply reached or immediate draw
        if (threads.is_stopped() || ss->ply >= PLY_MAX || pos.is_draw(ss->ply))
            return ss->ply >= PLY_MAX && !ss->inCheck ? evaluate(pos) : draw_value(nodes);

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

    const auto correctionValue = correction_value(pos, ss);

    // Step 4. Transposition table lookup
    auto [ttd, ttw] = transpositionTable.probe(key);

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;

    bool ttmNone;
    if constexpr (RootNode)
    {
        ttd.move = rootMoves[pvIdx][0];
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

    Value evalue, ttEvalue;

    bool improve, worsen;

    // Step 5. Static evaluation of the position
    if (ss->inCheck)
    {
        evalue = VALUE_NONE;

        ss->evalue = ttEvalue = (ss - 2)->evalue;
    }
    else if (exclude)
    {
        evalue = ttEvalue = ss->evalue;
    }
    else if (ttd.hit)
    {
        // Never assume anything about values stored in TT
        evalue = ttd.evalue;

        if (!is_valid(evalue))
            evalue = evaluate(pos);

        ss->evalue = ttEvalue = adjust_eval_value(evalue, correctionValue);

        // Can ttValue be used as a better position evaluation
        if (is_valid(ttd.value) && is_ok(ttd.bound & fail_bound(ttd.value > ttEvalue)))
            ttEvalue = ttd.value;
    }
    else
    {
        evalue = evaluate(pos);

        ss->evalue = ttEvalue = adjust_eval_value(evalue, correctionValue);

        ttw.write(Move::None, VALUE_NONE, evalue, DEPTH_NONE, Bound::NONE, ss->pvTT);
    }

    // Set up the improve and worsen flags.
    // improve: if the static evaluation is better than it was at the our last turn (two plies ago)
    // worsen: if the static evaluation is better than it was at the opponent last turn (one ply ago).
    improve = ss->evalue > +(ss - 2)->evalue;
    worsen  = ss->evalue > -(ss - 1)->evalue;

    // Retroactive LMR adjustments
    // Hindsight adjustment of reductions based on static evaluation difference.
    // The ply after beginning an LMR search, adjust the reduced depth based on
    // how the opponent's move affected the static evaluation.
    if (depth < DEPTH_MAX && red >= 3 && !worsen)
        ++depth;

    if (depth > 1 && red >= 2 && ss->evalue > 173 - (ss - 1)->evalue)
        --depth;

    State st;

    // Check for an early TT cutoff at non-pv nodes
    if constexpr (!PVNode)
    {
        if (!exclude && is_valid(ttd.value) && (CutNode == (ttd.value >= beta) || depth > 4)
            && ttd.depth > depth - (ttd.value <= beta)
            && is_ok(ttd.bound & fail_bound(ttd.value >= beta)))
        {
            // If ttMove fails high, update move sorting heuristics on TT hit
            if (!ttmNone && ttd.value >= beta)
            {
                // Bonus for a quiet ttMove
                if (!ttmCapture)
                    update_quiet_histories(pos, ss, ttd.move, std::min(114 * depth, +724));

                // Extra penalty for early quiet moves of the previous ply
                if (preOk && !preCapture && (ss - 1)->moveCount < 5)
                    update_continuation_histories(ss - 1, pos[preSq], preSq, -2187);
            }

            // Partial workaround for the graph history interaction problem
            // For high rule50 counts don't produce transposition table cutoffs.
            if (pos.rule50_count()
                < constexpr_round((1.0 - int(pos.has_rule50_high()) * 0.25) * rule50_threshold()))
            {
                // If the depth is big enough, verify that the ttMove is really a good move
                if (depth >= 7 && !is_decisive(ttd.value) && !ttmNone && pos.legal(ttd.move))
                {
                    pos.do_move(ttd.move, st);

                    auto [next_ttd, next_ttw] = transpositionTable.probe(pos.key());

                    next_ttd.value = next_ttd.hit
                                     ? value_from_tt(next_ttd.value, ss->ply, pos.rule50_count())
                                     : VALUE_NONE;

                    pos.undo_move(ttd.move);

                    // Check that the ttValue after the ttMove would also trigger a cutoff
                    if (!is_valid(next_ttd.value)
                        || (ttd.value >= beta) == (-next_ttd.value >= beta))
                        return ttd.value;
                }
                else
                    return ttd.value;
            }
        }
        // No cutoff, but why? Does the stored inexact value mismatch our aspiration window?
        // Penalize the entry since its bound is now no longer useful for this window-bound
        else if (!exclude && depth > 5 && is_valid(ttd.value)
                 && ttd.depth > depth - (ttd.value <= beta) && ttd.bound != Bound::EXACT
                 && is_ok(ttd.bound & fail_bound(ttd.value < beta)))
            ttw.penalize(1);
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
            const auto pieceCount = pos.count();

            if (pieceCount < tbConfig.cardinality
                || (pieceCount == tbConfig.cardinality && depth >= tbConfig.probeDepth))
            {
                Tablebase::Syzygy::ProbeState wdlPs;

                auto wdlScore = Tablebase::Syzygy::probe_wdl(pos, &wdlPs);

                // Force check of time on the next occasion
                if (is_main_worker())
                    main_manager()->callsCount = 1;

                if (wdlPs != Tablebase::Syzygy::PS_FAIL)
                {
                    ++tbHits;

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
                        ttw.write(Move::None, value_to_tt(tbValue, ss->ply), evalue,
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

    const PieceSqHistory* contHistory[CONT_HISTORY_COUNT];

    build_continuation_histories(ss, contHistory);

    // Skip early pruning when in check
    if (!ss->inCheck)
    {
        // clang-format off
    // Use static evaluation difference to improve quiet move ordering
    if (preOk && !preCapture && !(ss - 1)->inCheck)
    {
        int bonus = 62 + std::clamp(-((ss - 1)->evalue + (ss - 0)->evalue), -183, +180);

        if (!ttd.hit && preNonPawn)
            update_pawn_history(pos, pos[preSq], preSq, bonus * 13);

        update_quiet_history(~ac, preMove, bonus * 10);
    }

    // Step 7. Razoring
    // If eval is really low, check with qsearch then return speculative fail low.
    if constexpr (!PVNode)
    {
    if (!exclude && ttEvalue + 465 + 300 * depth * depth <= alpha)
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
    if (!ss->pvTT && !exclude && depth < 17 && !is_win(ttEvalue) && !is_loss(beta)
        && (ttmNone || history_value(pos, ttd.move, ac, contHistory) >= 32768 - int(ttmCapture) * 25968))
    {
        // Compute base futility
        int baseFutility = interpolate(std::min(int(depth), 10), 1, 10, 40, 80) - int(!ttd.hit) * 20;
        // Compute futility
        int futility = std::max(baseFutility * depth
                              - constexpr_ceil(baseFutility * (int(improve) * 2934.0 + int(worsen) * 343.0) / 1024.0)
                              + constexpr_round(absCorrectionValue / 182069.0),
                                0);

        if (ttEvalue - futility >= beta)
            return blend_values(beta, ttEvalue, 716, 1024);
    }
    }

    // Step 9. Null move search with verification search
    if constexpr (CutNode)
    {
    if (!exclude && hasNonPawn /*Zugzwang guard*/ && ss->ply >= nmpPly
        && beta >= -2000 && ss->evalue - 374 + int(improve) * 45 + 14 * depth >= beta)
    {
        assert(preMove != Move::Null);

        // Null move dynamic reduction
        Depth R = 7 + depth / 3 + std::max((ss->evalue - beta) / 256, 0);

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

    improve |= ss->evalue >= beta;

    // Step 10. Internal iterative reductions
    // Reduce search depth for PV/Cut deep enough nodes without ttMoves.
    // (*Scaler) Making IIR more aggressive scales poorly.
    if constexpr (!AllNode)
    {
    depth -= (depth > 5) && ttmNone && !ss->pvFollow;
    }

    // Step 11. ProbCut
    // If have a good enough capture or any promotion and a reduced search
    // returns a value much above beta, can (almost) safely prune previous move.
    if (depth > 2 && !is_loss(beta))
    {
        const Value probCutBeta = std::min(214 + beta - int(improve) * 59, +VALUE_INFINITE);
        assert(beta <= probCutBeta && probCutBeta <= +VALUE_INFINITE);

        // If value from transposition table is less than probCutBeta, Don't attempt probCut
        if (!(is_valid(ttd.value) && ttd.value < probCutBeta))
        {
        const Depth probCutDepth     = std::max<Depth>(depth - 3 - int(improve) * 2, DEPTH_ZERO);
        const int   probCutThreshold = probCutBeta - ss->evalue;

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
                if (!rootMoves.contains(pvIdx, pvEnd, move))
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
                    ttw.write(move, value_to_tt(probCutValue, ss->ply), evalue,
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

    MovePicker mp(pos, ttd.move, &captureHistory, &quietHistory, &lowPlyQuietHistory, contHistory,
                  &atomicHistories, ss->ply, -1);
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
            if (!rootMoves.contains(pvIdx, pvEnd, move))
                continue;
        }

        ss->moveCount = ++moveCount;

        if constexpr (RootNode)
        {
            if (is_main_worker() && rootDepth > OUTPUT_DEPTH_LIMIT && !options["MinimalInfo"])
            {
                std::string currMove{move_to_can(move)};
                usize       currMoveNumber{pvIdx + moveCount};

                main_manager()->updateContext.onUpdateIter({rootDepth, currMove, currMoveNumber});
            }
        }

        if constexpr (PVNode)
        {
            (ss + 1)->pv = nullptr;
        }

        const bool mTT = move == ttd.move;

        const Square dstSq = move.dst_sq();

        const Piece movedPc = pos.moved_pc(move);

        const bool check      = pos.check(move);
        const bool capture    = pos.capture_promo(move);
        const auto capturedPt = capture ? pos.captured_pt(move) : NO_PIECE_TYPE;

        // Calculate new depth for this move
        Depth newDepth = depth - 1;

        int deltaRatio = 617 * (beta - alpha) / rootDelta;

        int r = reduction(depth, moveCount, deltaRatio, improve);

        // (*Scaler) Increase reduction for pvHit nodes, Larger values scales well
        r += int(ss->pvTT) * 1006;

        // Step 14. Pruning at shallow depths
        // Depth conditions are important for mate finding.
        if constexpr (!RootNode)
        {
            if (hasNonPawn && !is_loss(bestValue))
            {
                // Skip quiet moves if moveCount exceeds moveCount threshold
                mp.update_quiets_skip([moveCount, depth, improve]() noexcept -> bool {
                    return moveCount >= ((3 + depth * depth) / (1 + int(!improve)));
                });

                // Reduced depth of the next LMR search
                Depth lmrDepth = newDepth - constexpr_round(r / 1024.0);

                if (capture)
                {
                    int history = captureHistory[+movedPc][dstSq][capturedPt];

                    // Futility pruning: for captures
                    if (!check && lmrDepth < 7)
                    {
                        int futility = 231 + ss->evalue + piece_value(capturedPt) + 232 * lmrDepth
                                     + constexpr_round(history * 131.0 / 1024.0);
                        if (futility <= alpha)
                            continue;
                    }

                    // SEE based pruning for captures and checks
                    if (safe_pruning(movedPc))
                    {
                        int threshold = 175 * depth + constexpr_round(history * 34.0 / 1024.0);
                        if ((mp.cur_stage() != MovePicker::Stage::ENC_GOOD_CAPTURE
                             || mp.threshold_value() > threshold)
                            && pos.see(move) < -threshold)
                            continue;
                    }
                }
                else if (!PVNode || !ss->pvFollow)
                {
                    int history = (*contHistory[0])[+movedPc][dstSq]
                                + (*contHistory[1])[+movedPc][dstSq]
                                + atomicHistories.pawn_entry(pos)[+movedPc][dstSq];

                    // History based pruning
                    if (!check && history < -4313 * depth)
                        continue;

                    history += constexpr_round(64.0 * quietHistory[ac][move.raw()] / 32.0);

                    // (*Scaler) Generally, lower divisor scales well
                    assert(depth > DEPTH_ZERO);
                    const double lrmDivisor =
                      LMR_DIVISORS[std::min<usize>(depth, LMR_DIVISORS.size()) - 1];
                    lmrDepth += constexpr_round(history / lrmDivisor);

                    // Futility pruning: for quiets
                    // (*Scaler) Generally, more frequent futility pruning scales well
                    if (!check && lmrDepth < 12 && !ss->inCheck)
                    {
                        int futility =
                          164 + ss->evalue + 117 * lmrDepth + int(ss->evalue > alpha) * 90;
                        if (futility <= alpha)
                        {
                            if (!is_win(futility))
                                bestValue = static_cast<Value>(std::max(  //
                                  futility, static_cast<int>(bestValue)));
                            continue;
                        }
                    }

                    // SEE based pruning for quiets and checks
                    if (safe_pruning(movedPc))
                    {
                        int threshold = std::max(
                          int(check) * 64 * depth + 25 * lmrDepth * constexpr_abs(lmrDepth), 0);
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
        if (!exclude && mTT && depth > 5 + int(ss->pvTT) && is_valid(ttd.value) && !is_decisive(ttd.value)
             && ttd.depth >= depth - 3 && is_ok(ttd.bound & Bound::LOWER) && !is_shuffling(pos, ss, move))
        {
            Value singularAlpha = std::max(ttd.value - 1 - constexpr_round((60.0 + int(!PVNode && ss->pvTT) * 70.0) * depth / 59.0), -VALUE_INFINITE);

            Depth singularDepth = newDepth / 2;
            assert(singularDepth > DEPTH_ZERO);

            Value singularValue = search<~~T>(pos, ss, singularAlpha, singularAlpha + 1, singularDepth, 0, move);

            ss->ttMove    = ttd.move;
            ss->moveCount = moveCount;

            if (singularValue <= singularAlpha)
            {
                int corrMargin = constexpr_round(absCorrectionValue / 194822.0);

                int doubleMargin = -3 + int(PVNode) * 201 - int(!ttmCapture) * 157 - corrMargin - int(ss->ply > rootDepth) * 41 - constexpr_round(ttMoveHistory * 1081.0 / 117824.0);
                int tripleMargin = 70 + int(PVNode) * 306 - int(!ttmCapture) * 188 - corrMargin - int(ss->ply > rootDepth) * 45 + int(ss->pvTT) * 84;

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
                ttMoveHistory << -(+442 + 108 * depth);

                if (!ss->inCheck && singularValue > ss->evalue)
                {
                    int bonus = constexpr_round((singularValue - ss->evalue) * singularDepth * 177.0 / 1024.0);

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

        [[maybe_unused]] u64 preNodes = 0;
        if constexpr (RootNode)
        {
            preNodes = nodes;
        }

        // Step 16. Make the move
        do_move(pos, move, st, ss, check);

        assert(capturedPt == type_of(pos.captured_pc()));

        ss->history = history_value(capture, move, movedPc, capturedPt, ac, contHistory);

        // Base reduction offset to compensate for other tweaks
        r += 714;
        r -= 62 * moveCount;
        r -= constexpr_round(absCorrectionValue / 26131.0);

        // (*Scaler) Decrease reduction if position is or has been on the PV
        r -= int(ss->pvTT)
           * (+2766                 //
              + int(PVNode) * 1017  //
              + int(is_valid(ttd.value) && ttd.value > alpha) * 838
              + int(ttd.depth >= depth) * (923 + int(CutNode) * 955));

        // Increase reduction for CutNode
        if constexpr (CutNode)
            r += 3995 + int(ttmNone) * 1059;

        // Increase reduction if ttMove is a capture
        r += int(ttmCapture) * 1039;

        // Increase reduction if next ply has many fail-highs
        int x = ss->cutoffCount - 1;
        if (x > 0)
            r +=
              (236 + int(AllNode) * 1143 + 1024 * (x >> 1)  //
               - 512 * (x >> 2) - 256 * (x >> 3) - 128 * (x >> 4) - 64 * (x >> 5) - 32 * (x >> 6));
        // Decrease reduction for first picked move (ttMove)
        else if (mTT)
            r = std::max(r - 2016 + int(CutNode) * 150, -10);

        // Decrease/Increase reduction for moves with a good/bad history
        r -= constexpr_round(445.0 * ss->history / 4096.0);

        // Scale up reduction for AllNode
        if constexpr (AllNode)
        {
            r = constexpr_round(r * (1.0 + 272.0 / (285.0 + 256.0 * depth)));
        }

        // Step 17. Late moves reduction / extension (LMR)
        if (depth > 1 && moveCount > 1)
        {
            Depth redDepth =
              std::max<Depth>(std::min<Depth>(newDepth - constexpr_round(r / 1024.0), newDepth + 2),
                              1)
              + int(PVNode);

            i16 reduction = newDepth - redDepth;

            value = -search<NT::CUT>(pos, ss + 1, -alpha - 1, -alpha, redDepth, reduction);

            // (*Scaler) Do a full-depth search when reduced LMR search fails high
            // Shallower searches here don't scales well.
            if (value > alpha)
            {
                // If the value was good enough search deeper
                bool extend = redDepth < newDepth && value > 52 + bestValue;
                // If the value was bad enough search shallower
                bool reduce = value < 9 + bestValue;

                // Adjust full-depth search based on LMR value
                newDepth += int(extend) - int(reduce);

                if (redDepth < newDepth)
                    value = -search<~T>(pos, ss + 1, -alpha - 1, -alpha, newDepth);

                // Post LMR continuation history updates
                update_continuation_histories(ss, movedPc, dstSq, 1415);
            }
        }
        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present
            r += int(ttmNone) * 1085;

            // Reduce search depth if expected reduction is high
            value = -search<~T>(pos, ss + 1, -alpha - 1, -alpha,
                                newDepth - int(r > 5039) - int(r > 5223 && (newDepth > 2)));
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
                if (newDepth <= DEPTH_ZERO && mTT
                    && (ttd.depth > 1
                        || (ttd.depth > 0 && is_valid(ttd.value) && is_decisive(ttd.value))))
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
            assert(rm[0] == move);

            rm.nodes += nodes - preNodes;
            // clang-format off
            rm.avgValue    = rm.avgValue    !=          -VALUE_INFINITE  ? (         value  + rm.avgValue   ) / 2 :          value;
            rm.avgSqrValue = rm.avgSqrValue != sign_sqr(-VALUE_INFINITE) ? (sign_sqr(value) + rm.avgSqrValue) / 2 : sign_sqr(value);
            // clang-format on

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.selDepth = selDepth;
                rm.value = rm.uciValue = value;
                rm.reset_bound();

                if (value >= beta)
                {
                    rm.uciValue = beta;
                    rm.bound    = Bound::LOWER;
                }
                else if (value <= alpha)
                {
                    rm.uciValue = alpha;
                    rm.bound    = Bound::UPPER;
                }

                rm.shrink_to(1);

                const auto* const childPv = (ss + 1)->pv;
                assert(childPv != nullptr);

                for (const Move m : *childPv)
                    rm.push_back(m);

                // Record how often the best move has been changed in each iteration.
                // This information is used for time management.
                // In MultiPV mode, must take care to only do this for the first PV line.
                if (moveCount > 1 && pvIdx == 0)
                    ++moveChanges;
            }
            else
                // All other moves but the PV, are set to the lowest value, this
                // is not a problem when sorting because the sort is stable and the
                // move position in the list is preserved - just the PV is pushed up.
                rm.value = -VALUE_INFINITE;
        }

        // In case have an alternative move equal in eval to the current bestMove,
        // promote it to bestMove by pretending it just exceeds alpha (but not beta).
        bool inc = value == bestValue && 2 + ss->ply >= rootDepth && (nodes & 0xE) == 0
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
    else
    {
        // Adjust best value for fail high cases
        if (bestValue > beta && !is_win(bestValue) && !is_loss(beta))
            bestValue = blend_values(bestValue, beta, depth, depth + 1);

        // If there is a move that produces search value greater than alpha update the history of searched moves
        if (bestMove != Move::None)
        {
            bool bmTT = bestMove == ttd.move;

            update_histories<PVNode>(pos, ss, depth, bestMove, bmTT, searchedMoves);

            if constexpr (!PVNode)
            {
                ttMoveHistory << (-779 + int(bmTT) * 1571);
            }
        }
        // If prior move is valid, that caused the fail low
        else if (preOk)
        {
            // Bonus for prior quiet move
            if (!preCapture)
            {
                int bonusScale = std::max(
                  -245
                    // Increase bonus when depth is high
                    + std::min(59 * depth, +430)
                    // Increase bonus when bestValue is lower than current static evaluation
                    + 143 * int(!(ss)->inCheck && bestValue <= -103 + (ss)->evalue)
                    // Increase bonus when bestValue is higher than previous static evaluation
                    + 151 * int(!(ss - 1)->inCheck && bestValue <= -78 - (ss - 1)->evalue)
                    // Increase bonus when the previous moveCount is high
                    + 191 * int((ss - 1)->moveCount > 8)
                    // Increase bonus if the previous move has a bad history
                    - constexpr_round((ss - 1)->history / 98.0),
                  0);

                int bonus = bonusScale * std::min(-82 + 141 * depth, +1472);

                update_quiet_history(~ac, preMove, constexpr_round(bonus * 234.0 / 32768.0));

                update_continuation_histories(ss - 1, pos[preSq], preSq,
                                              constexpr_round(bonus * 472.0 / 32768.0));
                if (preNonPawn)
                    update_pawn_history(pos, pos[preSq], preSq,
                                        constexpr_round(bonus * 1288.0 / 32768.0));
            }
            // Bonus for prior capture move
            else
            {
                auto capturedPt = type_of(pos.captured_pc());
                assert(capturedPt != NO_PIECE_TYPE);

                update_capture_history(pos[preSq], preSq, capturedPt, 901);
            }
        }
    }

    // Don't let best value inflate too high (tb)
    if constexpr (PVNode)
    {
        bestValue = std::min<int>(maxValue, bestValue);
    }

    // If no good move is found and the previous position was pvHit, then the previous
    // opponent move is probably good and the new position is added to the search tree.
    ss->pvTT = ss->pvTT || (bestValue <= alpha && (ss - 1)->pvTT);

    // Save gathered information in transposition table
    if ((!RootNode || pvIdx == 0) && !exclude)
        ttw.write(bestMove, value_to_tt(bestValue, ss->ply), evalue,
                  moveCount != 0 ? depth : std::min<Depth>(depth + 6, DEPTH_MAX),
                  bestValue >= beta                  ? Bound::LOWER
                  : PVNode && bestMove != Move::None ? Bound::EXACT
                                                     : Bound::UPPER,
                  ss->pvTT);

    // Adjust correction history if the best move is none or not a capture
    // and the error direction matches whether the above/below bounds.
    if (!ss->inCheck && (bestMove == Move::None || !pos.capture(bestMove))
        && (bestValue > ss->evalue) == (bestMove != Move::None))
    {
        int bonus = constexpr_round((bestValue - ss->evalue) * depth
                                    * (18.0 - int(bestMove != Move::None) * 6.0) / 128.0);

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
        alpha = draw_value(nodes);

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
    auto [ttd, ttw] = transpositionTable.probe(key);

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

    const auto correctionValue = ss->inCheck ? 0 : correction_value(pos, ss);

    Value evalue, bestValue;

    int baseFutility;

    // Step 4. Static evaluation of the position
    if (ss->inCheck)
    {
        evalue = VALUE_NONE;

        bestValue = baseFutility = -VALUE_INFINITE;
    }
    else
    {
        // clang-format off
    if (ttd.hit)
    {
        // Never assume anything about values stored in TT
        evalue = ttd.evalue;

        if (!is_valid(evalue))
            evalue = evaluate(pos);

        ss->evalue = bestValue = adjust_eval_value(evalue, correctionValue);

        // Can ttValue be used as a better position evaluation
        if (is_valid(ttd.value) && !is_decisive(ttd.value)
            && is_ok(ttd.bound & fail_bound(ttd.value > bestValue)))
            bestValue = ttd.value;
    }
    else
    {
        evalue = evaluate(pos);

        ss->evalue = bestValue = adjust_eval_value(evalue, correctionValue);
    }

    // Stand pat. Return immediately if bestValue is at least beta
    if (bestValue >= beta)
    {
        if (bestValue > beta && !is_win(bestValue) && !is_loss(beta))
            bestValue = blend_values(bestValue, beta, 467, 1024);

        if (!ttd.hit)
            ttw.write(Move::None, VALUE_NONE, evalue, DEPTH_NONE, Bound::LOWER, false);

        return bestValue;
    }

    alpha = std::max(bestValue, alpha);

    baseFutility = 335 + ss->evalue;
        // clang-format on
    }

    const Move preMove = (ss - 1)->move;

    const bool   preOk = preMove.is_ok();
    const Square preSq = preOk ? preMove.dst_sq() : SQ_NONE;

    State st;

    Value value;

    Move move, bestMove = Move::None;

    u16 moveCount = 0;

    const PieceSqHistory* contHistory[1]{(ss - 1)->pieceSqHistory};

    // Initialize a MovePicker object for the current position, prepare to search the moves.
    // Because the depth is <= DEPTH_ZERO here, only captures, promotions will be generated.
    MovePicker mp(pos, ttd.move, &captureHistory, &quietHistory, &lowPlyQuietHistory, contHistory,
                  &atomicHistories, ss->ply);
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
                    bestValue = static_cast<Value>(std::max(  //
                      futility, static_cast<int>(bestValue)));
                    continue;
                }

                // SEE based pruning
                int threshold = baseFutility - alpha;
                if (pos.see(move) < -threshold)
                {
                    bestValue = static_cast<Value>(std::max(  //
                      std::min(baseFutility, static_cast<int>(alpha)),
                      static_cast<int>(bestValue)));
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
    if (bestValue > beta && !is_win(bestValue) && !is_loss(beta))
        bestValue = blend_values(bestValue, beta, 481, 1024);

    // Save gathered info in transposition table
    ttw.write(bestMove, value_to_tt(bestValue, ss->ply), evalue, DEPTH_ZERO,
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

    ++nodes;

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

void Worker::update_capture_history(const Piece     movedPc,
                                    const Square    dstSq,
                                    const PieceType capturedPt,
                                    const int       bonus) noexcept {
    assert(is_ok(dstSq));

    captureHistory[+movedPc][dstSq][capturedPt] << bonus;
}
void Worker::update_capture_history(const Position& pos, const Move m, const int bonus) noexcept {
    assert(m.is_ok());

    update_capture_history(pos.moved_pc(m), m.dst_sq(), pos.captured_pt(m), bonus);
}

void Worker::update_quiet_history(const Color ac, const Move m, const int bonus) noexcept {
    assert(m.is_ok());

    quietHistory[ac][m.raw()] << bonus;
}

void Worker::update_low_ply_quiet_history(const i16 ssPly, const Move m, const int bonus) noexcept {
    assert(m.is_ok());

    if (ssPly < LOW_PLY_SIZE)
        lowPlyQuietHistory[ssPly][m.raw()] << bonus;
}

void Worker::update_pawn_history(const Position& pos,
                                 const Piece     pc,
                                 const Square    dstSq,
                                 const int       bonus) noexcept {
    //assert(is_ok(pc));
    assert(is_ok(dstSq));

    atomicHistories.pawn_entry(pos)[+pc][dstSq] << bonus;
}
void Worker::update_pawn_history(const Position& pos, const Move m, const int bonus) noexcept {
    assert(m.is_ok());

    update_pawn_history(pos, pos.moved_pc(m), m.dst_sq(), bonus);
}

// Updates quiet histories (move sorting heuristics)
void Worker::update_quiet_histories(const Position& pos,
                                    Stack* const    ss,
                                    const Move      m,
                                    const int       bonus) noexcept {
    assert(m.is_ok());

    const Color ac = pos.active_color();

    update_quiet_history(ac, m, bonus);

    update_low_ply_quiet_history(ss->ply, m, constexpr_round(bonus * 663.0 / 1024.0));

    update_continuation_histories(ss, pos.moved_pc(m), m.dst_sq(),
                                  constexpr_round(bonus * 820.0 / 1024.0));

    update_pawn_history(pos, m,
                        constexpr_round(bonus * (525.0 + int(bonus > -7) * 513.0) / 1024.0));
}

// Updates history at the end of search() when a bestMove is found and other searched moves are known
template<bool PVNode>
void Worker::update_histories(const Position&                pos,
                              Stack* const                   ss,
                              const Depth                    depth,
                              const Move                     bestMove,
                              const bool                     bmTT,
                              const Array<SearchedMoves, 2>& searchedMoves) noexcept {
    assert(depth > DEPTH_ZERO);
    assert(ss->moveCount != 0);

    int bonus = std::max(std::min(-79 + 134 * depth, +1572)
                           + constexpr_round((ss - 1)->history / 30.0) + int(bmTT) * 382,
                         0);

    int malus = std::min(-205 + 1005 * depth, +2218);

    if constexpr (!PVNode)
    {
        bonus = constexpr_round(
          bonus * (1.0 + (searchedMoves[0].size() + searchedMoves[1].size()) / 256.0));
    }

    if (pos.capture_promo(bestMove))
    {
        update_capture_history(pos, bestMove, constexpr_round(bonus * 1366.0 / 1024.0));
    }
    else
    {
        update_quiet_histories(pos, ss, bestMove, constexpr_round(bonus * 824.0 / 1024.0));

        // Decrease history for all non-best quiet moves
        int decayQuietMalus = constexpr_round(malus * 1061.0 / 1024.0);
        for (const Move qm : searchedMoves[0])
        {
            update_quiet_histories(pos, ss, qm, -decayQuietMalus);
            decayQuietMalus = constexpr_round(decayQuietMalus * 956.0 / 1024.0);
        }
    }

    // Decrease history for all non-best capture moves
    int decayCaptureMalus = constexpr_round(malus * 1518.0 / 1024.0);
    for (const Move cm : searchedMoves[1])
    {
        update_capture_history(pos, cm, -decayCaptureMalus);
        decayCaptureMalus = constexpr_round(decayCaptureMalus * 1014.0 / 1024.0);
    }

    // Extra penalty for a quiet early move that was not a TT move in the previous ply when it gets refuted
    Stack* const ss1 = ss - 1;
    if (ss1->move.is_ok() && pos.captured_pc() == Piece::NO_PIECE
        && ss1->moveCount == 1 + int(ss1->ttMove != Move::None))
    {
        const Square preSq = ss1->move.dst_sq();
        update_continuation_histories(ss1, pos[preSq], preSq,
                                      -constexpr_round(malus * 683.0 / 1024.0));
    }
}

// clang-format off

// Updates correction histories at the end of search() when a bestMove is found
void Worker::update_correction_histories(const Position& pos, const Stack* const ss, int bonus) noexcept {
    constexpr double      BonusDivisor = 128.0;
    constexpr double    PawnBonusScale = 128.0;
    constexpr double   MinorBonusScale = 152.0;
    constexpr double NonPawnBonusScale = 186.0;

    const Color ac = pos.active_color();

    bonus = std::clamp(bonus, -CORRECTION_HISTORY_LIMIT / 4, +CORRECTION_HISTORY_LIMIT / 4);

    atomicHistories.    pawn_correction_entry<WHITE>(pos)[ac] << constexpr_round(bonus *    PawnBonusScale / BonusDivisor);
    atomicHistories.    pawn_correction_entry<BLACK>(pos)[ac] << constexpr_round(bonus *    PawnBonusScale / BonusDivisor);
    atomicHistories.   minor_correction_entry<WHITE>(pos)[ac] << constexpr_round(bonus *   MinorBonusScale / BonusDivisor);
    atomicHistories.   minor_correction_entry<BLACK>(pos)[ac] << constexpr_round(bonus *   MinorBonusScale / BonusDivisor);
    atomicHistories.non_pawn_correction_entry<WHITE>(pos)[ac] << constexpr_round(bonus * NonPawnBonusScale / BonusDivisor);
    atomicHistories.non_pawn_correction_entry<BLACK>(pos)[ac] << constexpr_round(bonus * NonPawnBonusScale / BonusDivisor);

    const Move preMove = (ss - 1)->move;
    if (preMove.is_ok())
    {
        const Square preSq = preMove.dst_sq();
        const Piece  prePc = pos[preSq];

        (*(ss - 2)->pieceSqCorrectionHistory)[+prePc][preSq] << constexpr_round(bonus * 136.0 / BonusDivisor);
        (*(ss - 4)->pieceSqCorrectionHistory)[+prePc][preSq] << constexpr_round(bonus *  68.0 / BonusDivisor);
    }
}

// Computes the correction value for the current position from the correction histories
int Worker::correction_value(const Position& pos, const Stack* const ss) const noexcept {
    const Color ac = pos.active_color();

    i64 correctionValue =
           + i64{6670} * (atomicHistories.    pawn_correction_entry<WHITE>(pos)[ac]
                        + atomicHistories.    pawn_correction_entry<BLACK>(pos)[ac])
           + i64{4640} * (atomicHistories.   minor_correction_entry<WHITE>(pos)[ac]
                        + atomicHistories.   minor_correction_entry<BLACK>(pos)[ac])
           +i64{11840} * (atomicHistories.non_pawn_correction_entry<WHITE>(pos)[ac]
                        + atomicHistories.non_pawn_correction_entry<BLACK>(pos)[ac]);

    const Move preMove = (ss - 1)->move;
    if (preMove.is_ok())
    {
        const Square preSq = preMove.dst_sq();
        const Piece  prePc = pos[preSq];

        correctionValue += i64{8363} * ((*(ss - 2)->pieceSqCorrectionHistory)[+prePc][preSq]
                                      + (*(ss - 4)->pieceSqCorrectionHistory)[+prePc][preSq]);
    }
    else
        correctionValue += i64{64549};

    return std::clamp(correctionValue, -INT_LIMIT, +INT_LIMIT);
}

// clang-format on

int Worker::history_value(const bool                   capture,
                          const Move                   m,
                          const Piece                  movedPc,
                          const PieceType              capturedPt,
                          const Color                  ac,
                          const PieceSqHistory** const contHistory) const noexcept {
    return constexpr_round(capture ? (piece_value(capturedPt) * 873.0 / 128.0
                                      + captureHistory[+movedPc][m.dst_sq()][capturedPt])
                                   : (quietHistory[ac][m.raw()] * 2252.0
                                      + (*contHistory[0])[+movedPc][m.dst_sq()] * 1126.0
                                      + (*contHistory[1])[+movedPc][m.dst_sq()] * 1093.0)
                                       / 1024.0);
}

int Worker::history_value(const Position&              pos,
                          const Move                   m,
                          const Color                  ac,
                          const PieceSqHistory** const contHistory) const noexcept {
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
    assert(rm0.size() == 1);

    const Move bestMove = rm0[0];
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

            auto [ttd, ttw] = transpositionTable.probe(rootPos.key());

            ponderMove = ttd.hit ? legal_move(ttd.move, rootPos) : Move::None;

            if (ponderMove == Move::None || !legalMoves.contains(ponderMove))
            {
                ponderMove = Move::None;

                for (auto&& th : threads)
                {
                    if (th->worker.get() == this)
                        continue;
                    if (const auto& rm = th->worker->rootMoves[0];
                        rm[0] == bestMove && rm.size() > 1)
                    {
                        ponderMove = rm[1];
                        break;
                    }
                }

                if (ponderMove == Move::None)
                    for (auto&& th : threads)
                    {
                        if (th->worker.get() == this)
                            continue;
                        if (const auto& rm = *th->worker->rootMoves.find(bestMove); rm.size() > 1)
                        {
                            ponderMove = rm[1];
                            break;
                        }
                    }

                if (ponderMove == Move::None)
                {
                    std::uniform_int_distribution<usize> distribution(0, legalMoves.size() - 1);
                    ponderMove = *(legalMoves.begin() + distribution(prng));
                }
            }

            rm0.push_back(ponderMove);
        }
    }

    rootPos.undo_move(bestMove);

    return rm0.size() > 1;
}

// Used to correct and extend PVs for moves that have a TB (but not a mate) score.
// Keeps the search based PV for as long as it is verified to maintain the game outcome, truncates afterward.
// Finally, extends to mate the PV, providing a possible continuation (but not a proven mating line).
void Worker::extend_tb_pv(const usize idx, Value& value) noexcept {
    assert(idx < rootMoves.size());

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

    auto& rmIdx = rootMoves[idx];

    std::list<State> states;

    // Step 0. Do the rootMove, no correction allowed, as needed for MultiPV in TB
    State& rootSt = states.emplace_back();
    rootPos.do_move(rmIdx[0], rootSt);

    usize ply = 1;
    // Step 1. Walk the PV to the last position in TB with correct decisive score
    while (ply < rmIdx.size())
    {
        const Move pvMove = rmIdx[ply];

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
        if (tbCfg.rootInTB && rootPos.is_draw(static_cast<i16>(ply), UseRule50))
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

    // Keep only the correct part of the PV
    rmIdx.shrink_to(ply);

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
                rm.tbRank -= 1 + int(rootPos.capture(om)) * 99;

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

        const Move pvMove = rms[0][0];
        rmIdx.push_back(pvMove);
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
    for (usize i = rmIdx.size(); i-- > 0;)
        rootPos.undo_move(rmIdx[i]);

    if (aborted)
        print_info_string(
          "Syzygy based PV extension requires more time, increase Overhead-Time as needed.");
}

MainSearchManager::MainSearchManager(const UpdateContext& updateCtx) noexcept :
    updateContext(updateCtx) {}

// Initializes the time manager and resets previous search info
void MainSearchManager::reset() noexcept {

    timeManager.reset();
    preBestValue     = VALUE_ZERO;
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
    worker.threads.set(&Worker::moveChanges, u32{0});

    // clang-format off

    // Compute evaluation inconsistency based on differences from previous best scores
    const double inconsistencyFactor = std::clamp((11.87
                                                 + 02.21 * (preBestAvgValue - bestValue)
                                                 + 01.00 * (preBestValue - bestValue)) / 100.0,
                                                1.0000 - int(!atFirst) * 0.4280,
                                                1.0000 + int(!atFirst) * 0.7080);

    // Compute stable depth (difference between the current search depth and the last best depth)
    const Depth stableDepth = worker.rootDepth - lastBestMoveDepth;
    assert(stableDepth >= DEPTH_ZERO);

    // Use the stability factor to adjust the time reduction
    timeReduction = std::clamp(interpolate<double, double>(stableDepth, 5.0, 18.0, 0.65, 1.55), 0.65, 1.55);

    // Compute ease factor that factors in previous time reduction
    const double easeFactor = (1.48 + preTimeReduction) / (2.157 * timeReduction);

    // Compute move instability factor based on the total move changes and the number of threads
    const double instabilityFactor = 1.096 + 2.29 * sumMoveChanges / std::max<usize>(worker.thread_count(), 1);

    // Compute node effort factor that reduces time if root move has consumed a large fraction of total nodes
    const u64 nodesEffort = 100000 * worker.rootMoves[0].nodes / std::max<u64>(worker.nodes, 1);

    const double nodesEffortFactor = std::clamp(interpolate<i64, double>(nodesEffort, 79219, 101822, 0.924, 0.710), 0.710, 0.924);

    // Compute recapture factor that reduces time if recapture conditions are met
    const double recaptureFactor = 1.0 - int( worker.rootPos.captured_sq() == worker.rootMoves[0][0].dst_sq()
                                          && (worker.rootPos.captured_sq() & worker.rootPos.pieces_bb(~worker.rootPos.active_color())) != 0
                                          &&  worker.rootPos.see(worker.rootMoves[0][0]) >= 200)
                                         * std::min<Depth>(stableDepth, 25) / 256.0;

    // Calculate total time by combining all factors with the optimum time
    TimePoint totalTime = constexpr_ceil(timeManager.optimum() * inconsistencyFactor * easeFactor * instabilityFactor * nodesEffortFactor * recaptureFactor);
    assert(totalTime >= 0.0);
    // clang-format on

    // Cap totalTime to the available maximum time
    totalTime = std::min<TimePoint>(timeManager.maximum(), totalTime);
    // Cap totalTime to either 55% of total time or MaxForcedMoveTime ms in case of forced move for a better viewer experience
    if (worker.rootMoves.size() == 1)
        totalTime = std::min<TimePoint>(constexpr_ceil(0.55 * totalTime),
                                        worker.options["MaxForcedMoveTime"]);

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

    if (!worker.threads.is_researching() && !ponder && elapsedTime > 0.50 * totalTime)
        worker.threads.request_research();

    preBestValue = bestValue;
}

// Displays the principal variation (PV) along with associated information
void MainSearchManager::show_pv(Worker& worker, const Depth depth) const noexcept {
    assert(depth > DEPTH_ZERO);

    const auto& rootPos            = worker.rootPos;
    const auto& rootMoves          = worker.rootMoves;
    const auto& options            = worker.options;
    const auto& threads            = worker.threads;
    const auto& transpositionTable = worker.transpositionTable;
    const auto& tbConfig           = worker.tbConfig;
    const usize multiPv            = worker.multiPv;
    // Ensure non-zero to avoid a 'divide by zero'
    const TimePoint time   = std::max<TimePoint>(elapsed(), 1);
    const u64       nodes  = threads.sum(&Worker::nodes);
    const u64       tbHits = threads.sum(&Worker::tbHits, tbConfig.rootInTB ? rootMoves.size() : 0);
    const u16       hashfull = transpositionTable.hashfull();
    const bool      ShowWDL  = options["UCI_ShowWDL"];

    for (usize i = 0; i < multiPv; ++i)
    {
        const auto& rm = rootMoves[i];

        const bool isValueInvalid = rm.value == -VALUE_INFINITE;

        if (i != 0 && depth == 1 && isValueInvalid)
            continue;

        const Depth d = !isValueInvalid || depth <= 1 ? depth : depth - 1;

        Value v = isValueInvalid ? rm.preValue : rm.uciValue;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        const bool isValueTB = tbConfig.rootInTB && !is_mate(v);

        if (isValueTB)
            v = rm.tbValue;

        // Potentially correct and extend the PV, and in exceptional cases value also.
        // Previous PVs have already been extended. Bound flags indicate an unreliable PV.
        if (!isValueInvalid && (isValueTB || !rm.is_bound()) && is_decisive(v) && !is_mate(v))
            worker.extend_tb_pv(i, v);

        FixedText score{to_score({v, rootPos})};

        FixedText bound;
        // TB and previous scores are exact, even though their bound flags may say otherwise
        if (!(isValueTB || isValueInvalid) && rm.is_bound())
            bound = FixedText::from_view(to_string(rm.bound));

        FixedText wdl;
        if (ShowWDL)
            wdl = to_wdl(v, rootPos);

        std::string pv{isValueInvalid ? rm.prePV.build_pv() : rm.pv.build_pv()};

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

        double e = static_cast<double>(options["UCI_ELO"] - ELO_MIN) / (ELO_MAX - ELO_MIN);

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
Move Skill::pick_move(const RootMoves& rootMoves,
                      const usize      multiPv,
                      const bool       pickBest) noexcept {
    assert(1 <= multiPv && multiPv <= rootMoves.size());
    static XorShift64Star prng(now());  // PRNG sequence should be non-deterministic

    if (pickBest || bestMove == Move::None)
    {
        // RootMoves are already sorted by value in descending order
        const Value maxValue = rootMoves[0].value;

        const Value delta = std::min<Value>(maxValue - rootMoves[multiPv - 1].value, VALUE_PAWN);

        Value bestValue = -VALUE_INFINITE;
        // Choose best move. For each move value add two terms, both dependent on weakness.
        // One is deterministic and bigger for weaker levels, and one is random.
        // Then choose the move with the resulting highest value.
        for (usize i = 0; i < multiPv; ++i)
        {
            const Value value    = rootMoves[i].value;
            const Value diff     = maxValue - value;
            const Value noise    = prng.rand<u32>() % weakness();
            const Value push     = (weakness() * diff + delta * noise) / 128;
            const Value newValue = value + push;

            if (bestValue <= newValue)
            {
                bestValue = newValue;
                bestMove  = rootMoves[i][0];
            }
        }
    }

    return bestMove;
}

}  // namespace DON
