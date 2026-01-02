/*
  DON, a UCI chess playing engine derived from Stockfish

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

#include <array>
#include <chrono>
#include <cstdlib>
#include <list>
#include <random>
#include <ratio>
#include <string>

#include "bitboard.h"
#include "evaluate.h"
#include "movegen.h"
#include "movepick.h"
#include "nnue/network.h"
#include "option.h"
#include "prng.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"

namespace DON {

// (*Scaler):
// Search features marked by "(*Scaler)" have proven non-linear scaling.
// They are optimized to time controls of 180 + 1.8 and longer,
// so changing them or adding conditions that are similar requires
// tests at these types of time controls.

namespace {

constexpr int QUIET_HISTORY_DEFAULT_VALUE = 68;

// Reductions lookup table using [depth or moveCount]
alignas(CACHE_LINE_SIZE) constexpr auto Reductions = []() constexpr noexcept {
    StdArray<std::int16_t, MAX_MOVES> reductions{};

    reductions[0] = 0;
    for (std::size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = 21.4609 * constexpr_log(i);

    return reductions;
}();

constexpr int
reduction(Depth depth, std::uint8_t moveCount, int deltaRatio, bool improve) noexcept {
    int reductionScale = Reductions[depth] * Reductions[moveCount];
    return 1182 + reductionScale - deltaRatio + !improve * int(0.4648 * reductionScale);
}

// Add a small random value to draw evaluation to avoid 3-fold blindness
constexpr Value draw_value(Key key, std::uint64_t nodes) noexcept {
    return VALUE_DRAW + (key & 1) - (nodes & 1);
}

// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
constexpr Value value_to_tt(Value v, std::int16_t ply) noexcept {
    return is_win(v) ? v + ply : is_loss(v) ? v - ply : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated from
// current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, return the highest non-TB score instead.
constexpr Value value_from_tt(Value v, std::int16_t ply, std::int16_t rule50Count) noexcept {

    if (!is_valid(v))
        return v;

    // Handle TB win or better
    if (is_win(v))
    {
        // Downgrade a potentially false mate value
        if (is_mate_win(v) && VALUE_MATE - v > 2 * Position::DrawMoveCount - rule50Count)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB - v > 2 * Position::DrawMoveCount - rule50Count)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        return v - ply;
    }
    // Handle TB loss or worse
    if (is_loss(v))
    {
        // Downgrade a potentially false mate value
        if (is_mate_loss(v) && VALUE_MATE + v > 2 * Position::DrawMoveCount - rule50Count)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB + v > 2 * Position::DrawMoveCount - rule50Count)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        return v + ply;
    }

    return v;
}

constexpr Bound fail_bound(bool failHigh) noexcept {
    return failHigh ? Bound::LOWER : Bound::UPPER;
}

Move legal_tt_move(Move ttMove, const Position& pos) noexcept {
    return ttMove != Move::None && pos.legal(ttMove) ? ttMove : Move::None;
}

// Appends move and appends child Pv[]
void update_pv(Move* pv, Move m, const Move* childPv) noexcept {
    assert(m.is_ok());

    for (*pv++ = m; childPv != nullptr && *childPv != Move::None;)
        *pv++ = *childPv++;
    *pv = Move::None;
}

// Updates histories of the move pairs formed by
// move at ply -1, -2, -3, -4, -5, -6, -7 and -8 with move at ply 0.
void update_continuation_history(Stack* const ss, Piece pc, Square dstSq, int bonus) noexcept {
    assert(is_ok(dstSq));

    constexpr std::size_t MaxContHistorySize = 8;

    constexpr StdArray<double, MaxContHistorySize> ContHistoryWeights{
      1.1064, 0.6670, 0.3047, 0.5684, 0.1455, 0.4629, 0.1092, 0.2167  //
    };
    constexpr StdArray<int, MaxContHistorySize> ContHistoryOffsets{
      88, 00, 00, 00, 00, 00, 00, 00  //
    };

    // In check only update 2-ply continuation history
    const std::size_t contHistorySize = ss->inCheck ? 2 : MaxContHistorySize;

    for (std::size_t i = 0; i < contHistorySize; ++i)
    {
        auto* const ssi = (ss - 1) - i;

        if (!ssi->move.is_ok())
            break;

        (*ssi->pieceSqHistory)[+pc][dstSq] << int(ContHistoryWeights[i] * bonus)  //
                                                + ContHistoryOffsets[i];
    }
}

// Update raw evaluation according to various CorrectionHistory value
// and guarantee evaluation does not hit the tablebase range.
Value adjust_eval_value(Value evalValue, int correctionValue) noexcept {
    return in_range(evalValue + int(7.6294e-6 * correctionValue));
}

bool is_shuffling(const Position& pos, const Stack* const ss, Move move) noexcept {
    return !(pos.capture_promo(move) || pos.rule50_count() < 10 || pos.null_ply() <= 6
             || ss->ply < 20)
        && (ss - 2)->move.is_ok() && move.org_sq() == (ss - 2)->move.dst_sq()
        && (ss - 4)->move.is_ok() && (ss - 2)->move.org_sq() == (ss - 4)->move.dst_sq()
        && (ss - 6)->move.is_ok() && (ss - 4)->move.org_sq() == (ss - 6)->move.dst_sq()
        && (ss - 8)->move.is_ok() && (ss - 6)->move.org_sq() == (ss - 8)->move.dst_sq();
}

}  // namespace

// Initialize the worker with its thread and NUMA information
Worker::Worker(std::size_t               threadIdx,
               std::size_t               threadCnt,
               std::size_t               numaIdx,
               std::size_t               numaThreadCnt,
               NumaReplicatedAccessToken accessToken,
               ISearchManagerPtr         searchManager,
               const SharedState&        sharedState) noexcept :
    threadId(threadIdx),
    threadCount(threadCnt),
    numaId(numaIdx),
    numaThreadCount(numaThreadCnt),
    numaAccessToken(accessToken),
    manager(std::move(searchManager)),
    networks(sharedState.networks),
    options(sharedState.options),
    threads(sharedState.threads),
    transpositionTable(sharedState.transpositionTable),
    histories(sharedState.historiesMap.at(accessToken.numa_index())),
    accCaches(networks[accessToken]) {}

constexpr Worker::IndexRange Worker::numa_range(std::size_t size) const noexcept {
    assert(numa_thread_count() != 0 && numa_id() < numa_thread_count());

    std::size_t count  = size / numa_thread_count();
    std::size_t begIdx = numa_id() * count;
    std::size_t endIdx = numa_id() != numa_thread_count() - 1 ? begIdx + count : size;

    assert(begIdx <= endIdx && endIdx <= size);

    return {begIdx, endIdx};
}

// Initialize per-thread data structures
void Worker::init() noexcept {

    // Each thread initializes its NUMA-local range of history entries to prevent false sharing

    auto pawnRange = numa_range(histories.pawn_size());

    histories.pawn().fill(pawnRange.begIdx, pawnRange.endIdx, -1238);

    auto correctionRange = numa_range(histories.correction_size());

    histories.pawn_correction().fill(correctionRange.begIdx, correctionRange.endIdx, 5);
    histories.minor_correction().fill(correctionRange.begIdx, correctionRange.endIdx, 0);
    histories.non_pawn_correction().fill(correctionRange.begIdx, correctionRange.endIdx, 0);

    // Initialize search histories

    captureHistory.fill(-689);
    quietHistory.fill(QUIET_HISTORY_DEFAULT_VALUE);
    ttMoveHistory = 0;

    for (bool inCheck : {false, true})
        for (bool capture : {false, true})
            for (auto& toPieceSqHist : continuationHistory[inCheck][capture])
                for (auto& pieceSqHist : toPieceSqHist)
                    pieceSqHist.fill(-529);

    for (auto& toPieceSqCorrHist : continuationCorrectionHistory)
        for (auto& pieceSqCorrHist : toPieceSqCorrHist)
            pieceSqCorrHist.fill(8);

    accCaches.init(networks[numa_access_token()]);
}

// Ensure that the neural networks are replicated on this NUMA node
void Worker::ensure_network_replicated() noexcept {
    // Access once to force lazy initialization.
    // Do this because want to avoid initialization during search.
    (void) (networks[numa_access_token()]);
}

// Called when the program receives the UCI 'go' command.
void Worker::start_search() noexcept {
    auto* const mainManager = is_main_worker() ? main_manager() : nullptr;

    rootDepth = completedDepth = DEPTH_ZERO;
    nmpPly                     = 0;

    multiPV = 1;

    if (mainManager != nullptr)
    {
        multiPV = options["MultiPV"];

        // When playing with strength handicap enable MultiPV search that
        // will use behind-the-scenes to retrieve a set of sub-optimal moves.
        if (mainManager->skill.enabled())
            if (multiPV < Skill::MIN_MULTI_PV)
                multiPV = Skill::MIN_MULTI_PV;
    }

    if (multiPV > rootMoves.size())
        multiPV = rootMoves.size();

    accStack.reset();

    for (auto& colorQuietHist : quietHistory)
        for (auto& quietHist : colorQuietHist)
            quietHist = (3 * quietHist + QUIET_HISTORY_DEFAULT_VALUE) / 4;

    lowPlyQuietHistory.fill(97);

    // Non-main threads go directly to iterative_deepening()
    if (mainManager == nullptr)
    {
        iterative_deepening();
        return;
    }

    mainManager->callsCount     = limit.calls_count();
    mainManager->ponder         = limit.ponder;
    mainManager->ponderhitStop  = false;
    mainManager->sumMoveChanges = 0.0;
    mainManager->timeReduction  = 1.0;
    mainManager->skill.init(options);
    mainManager->timeManager.init(rootPos.active_color(), rootPos.ply(), rootPos.move_num(),
                                  options, limit);
    if (!limit.infinite)
        transpositionTable.increment_generation();

    bool think = false;

    if (rootMoves.empty())
    {
        rootMoves.emplace_back(Move::None);

        Value       value = rootPos.checkers_bb() != 0 ? -VALUE_MATE : VALUE_DRAW;
        std::string score = UCI::to_score({value, rootPos});
        mainManager->updateCxt.onUpdateShort({DEPTH_ZERO, score});
    }
    else
    {
        Move bookBestMove = Move::None;

        // Check polyglot book
        if (!limit.infinite && limit.mate == 0)
            bookBestMove = Book.probe(rootPos, rootMoves, options);

        if (bookBestMove != Move::None)
        {
            State st;
            rootPos.do_move(bookBestMove, st, this);

            RootMoves oRootMoves;

            for (auto m : MoveList<LEGAL>(rootPos))
                oRootMoves.emplace_back(m);

            Move bookPonderMove = Book.probe(rootPos, oRootMoves, options);

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

            threads.start_search();  // start non-main threads
            iterative_deepening();   // main thread start searching
        }
    }

    // When reach the maximum depth, can arrive here without a raise of threads.stop.
    // However, if pondering or in an infinite search, the UCI protocol states that
    // shouldn't print the best move before the GUI sends a "stop" or "ponderhit" command.
    // Therefore simply wait here until the GUI sends one of those commands.
    while (!threads.is_stopped() && (limit.infinite || mainManager->ponder))
    {}  // Busy wait for a stop or a mainManager->ponder reset

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
            mainManager->timeManager.advance_time_nodes(threads.sum(&Worker::nodes)
                                                        - limit.clocks[rootPos.active_color()].inc);

        // If the skill is enabled, swap the best PV line with the sub-optimal one
        if (mainManager->skill.enabled())
        {
            Move skillMove = mainManager->skill.pick_move(rootMoves, multiPV, false);

            for (auto&& th : threads)
                th->worker->rootMoves.swap_to_front(skillMove);
        }
        else if (threads.size() > 1 && multiPV == 1
                 && limit.mate == 0
                 //&& limit.depth == DEPTH_ZERO
                 && rootMoves[0].pv[0] != Move::None)
        {
            bestWorker = threads.best_thread()->worker.get();

            // Send PV info again if have a new best worker
            if (bestWorker != this)
                mainManager->show_pv(*bestWorker, bestWorker->completedDepth);
        }

        if (limit.use_time_manager())
        {
            mainManager->atFirst          = false;
            mainManager->preBestCurValue  = bestWorker->rootMoves[0].curValue;
            mainManager->preBestAvgValue  = bestWorker->rootMoves[0].avgValue;
            mainManager->preTimeReduction = mainManager->timeReduction;
        }
    }

    assert(!bestWorker->rootMoves.empty() && !bestWorker->rootMoves[0].pv.empty());
    const auto& rm = bestWorker->rootMoves[0];

    std::string bestMove   = UCI::move_to_can(rm.pv[0]);
    std::string ponderMove = UCI::move_to_can(rm.pv.size() > 1                            //
                                                  || bestWorker->ponder_move_extracted()  //
                                                ? rm.pv[1]
                                                : Move::None);

    mainManager->updateCxt.onUpdateMove({bestMove, ponderMove});
}

// Main iterative deepening loop. It calls search() repeatedly with increasing depth
// until the allocated thinking time has been consumed, the user stops the search,
// or the maximum search depth is reached.
void Worker::iterative_deepening() noexcept {
    auto* const mainManager = is_main_worker() ? main_manager() : nullptr;

    // Allocate stack with extra size to allow access from (ss - 9) to (ss + 1):
    // (ss - 9) is needed for update_continuation_history(ss - 1) which accesses (ss - 8),
    // (ss + 1) is needed for initialization of cutoffCount.
    constexpr std::uint16_t StackOffset = 9;

    StdArray<Stack, StackOffset + (MAX_PLY + 1) + 1> stacks{};

    Stack* const ss = &stacks[StackOffset];

    for (std::int16_t i = 0 - StackOffset; i < int(stacks.size()) - StackOffset; ++i)
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

    assert(stacks[0].ply == -StackOffset && stacks[stacks.size() - 1].ply == MAX_PLY + 1);
    assert(ss->ply == 0);

    StdArray<Move, MAX_PLY + 1> pv;

    ss->pv = pv.data();

    Color ac = rootPos.active_color();

    std::uint16_t researchCnt = 0;

    Value bestValue = -VALUE_INFINITE;

    auto  lastBestPV       = Moves{Move::None};
    Value lastBestCurValue = -VALUE_INFINITE;
    Value lastBestPreValue = -VALUE_INFINITE;
    Value lastBestUciValue = -VALUE_INFINITE;
    Depth lastBestDepth    = DEPTH_ZERO;

    // Iterative deepening loop until requested to stop or the target depth is reached
    while (!threads.is_stopped() && ++rootDepth <= MAX_PLY - 1
           && (mainManager == nullptr || limit.depth == DEPTH_ZERO || rootDepth <= limit.depth))
    {
        // Age out PV variability metric
        if (mainManager != nullptr && limit.use_time_manager())
            mainManager->sumMoveChanges *= 0.50;

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (auto& rm : rootMoves)
            rm.preValue = rm.curValue;

        if (threads.is_researching())
            ++researchCnt;

        std::size_t begPV = endPV = 0;
        // MultiPV loop. Perform a full root search for each PV line
        for (curPV = 0; curPV < multiPV; ++curPV)
        {
            if (curPV == endPV)
                for (begPV = endPV++; endPV < rootMoves.size(); ++endPV)
                    if (rootMoves[endPV].tbRank != rootMoves[begPV].tbRank)
                        break;

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = 1;

            auto avgValue    = rootMoves[curPV].avgValue;
            auto avgSqrValue = rootMoves[curPV].avgSqrValue;

            // Reset aspiration window starting size
            int delta = 5 + std::min(int(threads.size()) - 1, 8)  //
                      + int(1.1111e-4 * std ::abs(avgSqrValue));

            Value alpha = std::max(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min(avgValue + delta, +VALUE_INFINITE);

            // Adjust optimism based on root move's avgValue
            optimism[ac]  = 142 * avgValue / (91 + std::abs(avgValue));
            optimism[~ac] = -optimism[ac];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, research with a bigger window until don't fail high/low anymore.
            std::uint16_t failHighCnt = 0;
            while (true)
            {
                rootDelta = beta - alpha;
                assert(rootDelta > 0);

                // Adjust the effective depth searched, but ensure at least one
                // effective increment for every 4 researchCnt steps.
                Depth adjustedDepth = rootDepth - failHighCnt - 3 * (1 + researchCnt) / 4;

                if (adjustedDepth < 1)
                    adjustedDepth = 1;

                bestValue = search<Root>(rootPos, ss, alpha, beta, adjustedDepth);

                // Bring the best move to the front. It is critical that sorting
                // is done with a stable algorithm because all the values but the
                // first and eventually the new best one is set to -VALUE_INFINITE
                // and want to keep the same order for all the moves except the
                // new PV that goes to the front. Note that in the case of MultiPV
                // search the already searched PV lines are preserved.
                rootMoves.sort(curPV, endPV);

                // If the search has been stopped, break immediately.
                // Sorting is safe because RootMoves is still valid,
                // although it refers to the previous iteration.
                if (threads.is_stopped())
                    break;

                // When failing high/low give some update before a re-search.
                if (mainManager != nullptr && multiPV == 1 && rootDepth > 30
                    && (alpha >= bestValue || bestValue >= beta))
                    mainManager->show_pv(*this, rootDepth);

                // In case of failing low/high increase aspiration window and research,
                // otherwise exit the loop.
                if (bestValue <= alpha)
                {
                    beta  = std::max(+alpha, -VALUE_INFINITE + 1);
                    alpha = std::max(bestValue - delta, -VALUE_INFINITE);

                    failHighCnt = 0;

                    if (mainManager != nullptr)
                        mainManager->ponderhitStop = false;
                }
                else if (bestValue >= beta)
                {
                    alpha = std::max(+alpha, beta - delta);
                    beta  = std::min(bestValue + delta, +VALUE_INFINITE);

                    ++failHighCnt;
                }
                else
                    break;

                delta *= 1.3333;

                if (delta > 2 * VALUE_INFINITE)
                    delta = 2 * VALUE_INFINITE;

                assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
            }

            // Sort the PV lines searched so far
            rootMoves.sort(begPV, 1 + curPV);

            // Give some update about the PV
            if (
              mainManager != nullptr
              && (threads.is_stopped() || 1 + curPV == multiPV || rootDepth > 30)
              // A thread that aborted search can have mated-in/TB-loss PV and score that cannot be trusted,
              // i.e. it can be delayed or refuted if have had time to fully search other root-moves.
              // Thus, suppress this output and below pick a proven score/PV for this thread (from the previous iteration).
              && !(threads.is_aborted() && is_loss(rootMoves[0].uciValue)))
                mainManager->show_pv(*this, rootDepth);

            if (threads.is_stopped())
                break;
        }

        if (!threads.is_stopped())
            completedDepth = rootDepth;

        // Make sure not to pick an unproven mated-in score,
        // in case this worker prematurely stopped the search (aborted-search).
        if (threads.is_aborted() && lastBestPV[0] != Move::None
            && rootMoves[0].curValue != -VALUE_INFINITE && is_loss(rootMoves[0].curValue))
        {
            // Bring the last best rootMove to the front for best thread selection.
            rootMoves.move_to_front([&lastBestPV = std::as_const(lastBestPV)](
                                      const auto& rm) noexcept { return rm == lastBestPV[0]; });
            rootMoves[0].pv       = lastBestPV;
            rootMoves[0].curValue = lastBestCurValue;
            rootMoves[0].preValue = lastBestPreValue;
            rootMoves[0].uciValue = lastBestUciValue;
        }
        else if (rootMoves[0].pv[0] != lastBestPV[0])
        {
            lastBestPV       = rootMoves[0].pv;
            lastBestCurValue = rootMoves[0].curValue;
            lastBestPreValue = rootMoves[0].preValue;
            lastBestUciValue = rootMoves[0].uciValue;
            lastBestDepth    = completedDepth;
        }

        if (mainManager == nullptr)
            continue;

        // Have found a "mate in x"?
        if (limit.mate != 0 && rootMoves[0].curValue == rootMoves[0].uciValue
            && ((rootMoves[0].curValue != +VALUE_INFINITE && is_mate_win(rootMoves[0].curValue)
                 && VALUE_MATE - rootMoves[0].curValue <= 2 * limit.mate)
                || (rootMoves[0].curValue != -VALUE_INFINITE && is_mate_loss(rootMoves[0].curValue)
                    && VALUE_MATE + rootMoves[0].curValue <= 2 * limit.mate)))
            threads.request_stop();

        // If the skill is enabled and time is up, pick a sub-optimal best move
        if (mainManager->skill.enabled() && mainManager->skill.time_to_pick(rootDepth))
            mainManager->skill.pick_move(rootMoves, multiPV);

        // Do have time for the next iteration? Can stop searching now?
        if (limit.use_time_manager() && !(threads.is_stopped() || mainManager->ponderhitStop))
        {
            // Use part of the gained time from a previous stable move for the current move
            mainManager->sumMoveChanges += threads.sum(&Worker::moveChanges);

            // Reset move changes
            threads.set(&Worker::moveChanges, 0U);

            // clang-format off

            // Compute evaluation inconsistency based on differences from previous best scores
            double inconsistencyFactor = std::clamp(0.1185
                                                  + 0.0224 * (mainManager->preBestAvgValue - bestValue)
                                                  + 0.0093 * (mainManager->preBestCurValue - bestValue),
                                                    1.0000 - !mainManager->atFirst * 0.4300,
                                                    1.0000 + !mainManager->atFirst * 0.7000);

            // Compute stable depth (difference between the current search depth and the last best depth)
            Depth stableDepth = completedDepth - lastBestDepth;
            assert(stableDepth >= DEPTH_ZERO);

            // Use the stability factor to adjust the time reduction
            mainManager->timeReduction = 0.6600 + 0.8500 / (0.9800 + std::exp(0.5100 * (12.1500 - stableDepth)));

            // Compute ease factor that factors in previous time reduction
            double easeFactor = 0.4386 * (1.4300 + mainManager->preTimeReduction) / mainManager->timeReduction;

            // Compute move instability factor based on the total move changes and the number of threads
            double instabilityFactor = 1.0200 + 2.1400 * mainManager->sumMoveChanges / threads.size();

            // Compute node effort factor that reduces time if root move has consumed a large fraction of total nodes
            double nodeEffortExcess = -933.40 + 1000.0 * rootMoves[0].nodes / std::max(nodes_(), std::uint64_t(1));
            double nodeEffortFactor = 1.0 - 37.5207e-4 * std::max(nodeEffortExcess, 0.0);

            // Compute recapture factor that reduces time if recapture conditions are met
            double recaptureFactor = 1.0;
            if ( rootPos.captured_sq() == rootMoves[0].pv[0].dst_sq()
             && (rootPos.captured_sq() & rootPos.pieces_bb(~ac))
             && rootPos.see(rootMoves[0].pv[0]) >= 200)
                recaptureFactor -= 4.0040e-3 * std::min(+stableDepth, 25);

            // Calculate total time by combining all factors with the optimum time
            TimePoint totalTime = mainManager->timeManager.optimum() * inconsistencyFactor * easeFactor * instabilityFactor * nodeEffortFactor * recaptureFactor;
            assert(totalTime >= 0.0);
            // clang-format on

            // Cap totalTime to the available maximum time
            if (totalTime > mainManager->timeManager.maximum())
                totalTime = mainManager->timeManager.maximum();

            // Cap totalTime in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
            {
                totalTime *= 0.5500;

                if (totalTime > TimeManager::SINGLE_MOVE_MAX_TIME)
                    totalTime = TimeManager::SINGLE_MOVE_MAX_TIME;
            }

            TimePoint elapsedTime = mainManager->elapsed(threads);

            // Stop the search if have exceeded the total time
            if (elapsedTime > totalTime)
            {
                // If allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainManager->ponder)
                    mainManager->ponderhitStop = true;
                else
                    threads.request_stop();
            }

            if (!mainManager->ponder && elapsedTime > TimePoint(0.5030 * totalTime))
                threads.request_research();

            mainManager->preBestCurValue = bestValue;
        }
    }
}

// The main alpha-beta search function with negamax framework and
// various enhancements like aspiration windows, late move reductions, etc.
template<NodeType NT>
Value Worker::search(Position&    pos,
                     Stack* const ss,
                     Value        alpha,
                     Value        beta,
                     Depth        depth,
                     std::int8_t  red,
                     Move         excludedMove) noexcept {
    constexpr bool RootNode = NT == Root;
    constexpr bool PVNode   = RootNode || NT == PV;
    constexpr bool CutNode  = NT == Cut;  // !PVNode
    constexpr bool AllNode  = NT == All;  // !PVNode
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (1 + alpha == beta));
    assert(ss->ply >= 0);
    assert(!RootNode || (DEPTH_ZERO < depth && depth <= MAX_PLY - 1));

    Key key = pos.key();

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
        if (depth > MAX_PLY - 1)
            depth = MAX_PLY - 1;
        assert(DEPTH_ZERO < depth && depth < MAX_PLY);
    }

    // Check for the available remaining time
    if (is_main_worker())
        main_manager()->check_time(*this);

    StdArray<Move, MAX_PLY + 1> pv;

    if constexpr (PVNode)
    {
        // Update selDepth (selDepth from 1, ply from 0)
        if (selDepth < 1 + ss->ply)
            selDepth = 1 + ss->ply;
    }

    // Step 1. Initialize node
    ss->inCheck   = pos.checkers_bb();
    ss->moveCount = 0;
    ss->history   = 0;

    if constexpr (!RootNode)
    {
        // Step 2. Check for stopped search or maximum ply reached or immediate draw
        if (threads.is_stopped() || ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
            return ss->ply >= MAX_PLY && !ss->inCheck ? evaluate(pos) : draw_value(key, nodes_());

        // Step 3. Mate distance pruning.
        // Even if mate at the next move score would be at best mates_in(1 + ss->ply),
        // but if alpha is already bigger because a shorter mate was found upward in the tree
        // then there is no need to search further because will never beat the current alpha.
        // Same logic but with a reversed signs apply also in the opposite condition of being mated
        // instead of giving mate. In this case, return a fail-high score.
        Value mated = mated_in(0 + ss->ply);
        if (alpha < mated)
            alpha = mated;
        Value mates = mates_in(1 + ss->ply);
        if (beta > mates)
            beta = mates;
        if (alpha >= beta)
            return alpha;
    }

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    (ss + 1)->cutoffCount = 0;

    const bool exclude = excludedMove != Move::None;

    // Step 4. Transposition table lookup
    auto [ttd, ttu] = transpositionTable.probe(key);

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttd.move  = RootNode ? rootMoves[curPV].pv[0]
              : ttd.hit  ? legal_tt_move(ttd.move, pos)
                         : Move::None;
    assert(ttd.move == Move::None || pos.legal(ttd.move));
    ss->ttMove     = ttd.move;
    bool ttCapture = ttd.move != Move::None && pos.capture_promo(ttd.move);

    if (!exclude)
        ss->ttPv = PVNode || (ttd.hit && ttd.pv);

    Square preSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.dst_sq() : SQ_NONE;

    bool preCapture = is_ok(pos.captured_pc());
    bool preNonPawn =
      is_ok(preSq) && type_of(pos[preSq]) != PAWN && (ss - 1)->move.type() != MT::PROMOTION;

    int correctionValue = correction_value(pos, ss);

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

        ttu.update(Move::None, VALUE_NONE, evalValue, DEPTH_NONE, Bound::NONE, ss->ttPv);
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
    if (depth < MAX_PLY - 1 && red >= 3 && !worsen)
        ++depth;

    if (depth > 1 && red >= 2 && ss->evalValue > 169 - (ss - 1)->evalValue)
        --depth;

    Key pawnKey = pos.pawn_key();

    State st;

    // Check for an early TT cutoff at non-pv nodes
    if (!PVNode && !exclude && is_valid(ttd.value)        //
        && ttd.depth > depth - (ttd.value <= beta)        //
        && (CutNode == (ttd.value >= beta) || depth > 5)  //
        && is_ok(ttd.bound & fail_bound(ttd.value >= beta)))
    {
        // If ttMove fails high, update move sorting heuristics on TT hit
        if (ttd.move != Move::None && ttd.value >= beta)
        {
            // Bonus for a quiet ttMove
            if (!ttCapture)
                update_quiet_histories(pos, pawnKey, ss, ttd.move,
                                       std::min(-72 + 132 * depth, +985));

            // Extra penalty for early quiet moves of the previous ply
            if (is_ok(preSq) && !preCapture && (ss - 1)->moveCount < 4)
                update_continuation_history(ss - 1, pos[preSq], preSq, -2060);
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < int((1.0 - 0.20 * pos.has_rule50_high()) * rule50_threshold()))
        {
            // If the depth is big enough, verify that the ttMove is really a good move
            if (depth >= 8 && !is_decisive(ttd.value) && ttd.move != Move::None
                && pos.legal(ttd.move))
            {
                pos.do_move(ttd.move, st, this);

                auto [_ttd, _ttu] = transpositionTable.probe(pos.key());
                _ttd.value        = _ttd.hit  //
                                    ? value_from_tt(_ttd.value, ss->ply, pos.rule50_count())
                                    : VALUE_NONE;

                pos.undo_move(ttd.move);

                // Check that the ttValue after the ttMove would also trigger a cutoff
                if (!is_valid(_ttd.value) || ((ttd.value >= beta) == (-_ttd.value >= beta)))
                    return ttd.value;
            }
            else
                return ttd.value;
        }
    }

    Color ac = pos.active_color();

    bool  hasNonPawn   = pos.has_non_pawn(ac);
    Value nonPawnValue = hasNonPawn ? pos.non_pawn_value(ac) : VALUE_ZERO;

    [[maybe_unused]] Value maxValue = +VALUE_INFINITE;

    Value value, bestValue = -VALUE_INFINITE;

    Move move, bestMove = Move::None;

    // Step 6. Tablebase probe
    if constexpr (!RootNode)
        if (!exclude && tbConfig.cardinality != 0)
        {
            std::uint8_t pieceCount = pos.count();

            if (pieceCount <= tbConfig.cardinality
                && (pieceCount < tbConfig.cardinality || depth >= tbConfig.probeDepth)
                && pos.rule50_count() == 0 && !pos.has_castling_rights())
            {
                Tablebase::ProbeState wdlPs;

                auto wdlScore = Tablebase::probe_wdl(pos, &wdlPs);

                // Force check of time on the next occasion
                if (is_main_worker())
                    main_manager()->callsCount = 1;

                if (wdlPs != Tablebase::PS_FAIL)
                {
                    tbHits.fetch_add(1, std::memory_order_relaxed);

                    int drawValue = tbConfig.useRule50 * 1;

                    // Use the range VALUE_TB to VALUE_TB_WIN_IN_MAX_PLY to value
                    value = wdlScore < -drawValue ? -VALUE_TB + ss->ply
                          : wdlScore > +drawValue ? +VALUE_TB - ss->ply
                                                  : VALUE_DRAW + 2 * wdlScore * drawValue;

                    Bound bound = wdlScore < -drawValue ? Bound::UPPER
                                : wdlScore > +drawValue ? Bound::LOWER
                                                        : Bound::EXACT;

                    if (bound == Bound::EXACT
                        || (bound == Bound::LOWER ? value >= beta : value <= alpha))
                    {
                        ttu.update(Move::None, value_to_tt(value, ss->ply), evalValue,
                                   std::min(depth + 6, MAX_PLY - 1), bound, ss->ttPv);

                        return value;
                    }

                    if constexpr (PVNode)
                    {
                        if (bound == Bound::LOWER)
                        {
                            bestValue = value;

                            alpha = std::max(alpha, bestValue);
                        }
                        else
                            maxValue = value;
                    }
                }
            }
        }

    int absCorrectionValue = std::abs(correctionValue);

    // Skip early pruning when in check
    if (ss->inCheck)
        goto S_MOVES_LOOP;

    // Use static evaluation difference to improve quiet move ordering
    if (is_ok(preSq) && !preCapture && !(ss - 1)->inCheck)
    {
        int bonus = 59 + std::clamp(-((ss - 1)->evalValue + (ss - 0)->evalValue), -209, +167);

        if (!ttd.hit && preNonPawn)
            update_pawn_history(pawnKey, pos[preSq], preSq, 13.0000 * bonus);

        update_quiet_history(~ac, (ss - 1)->move, 9.0000 * bonus);
    }

    // Step 7. Razoring
    // If eval is really low, check with qsearch then return speculative fail low.
    if constexpr (!RootNode)
        if (!is_decisive(alpha) && ttEvalValue + 485 + 281 * depth * depth <= alpha)
        {
            value = qsearch<PVNode>(pos, ss, alpha, beta);

            if (value <= alpha && !is_decisive(value))
                return value;

            ss->ttMove = ttd.move;
        }

    // Step 8. Futility pruning: child node
    // The depth condition is important for mate finding.
    {
        const auto futility_margin = [&](bool cond) noexcept {
            Value futilityMult = 53 + cond * 23;

            return depth * futilityMult                                      //
                 - int((improve * 2.4160 + worsen * 0.3232) * futilityMult)  //
                 + int(5.7252e-6 * absCorrectionValue);
        };

        if (!ss->ttPv && !exclude && depth < 14 && !is_win(ttEvalValue) && !is_loss(beta)
            && (ttd.move == Move::None || ttCapture)
            && ttEvalValue - std::max(futility_margin(ttd.hit), 0) >= beta)
            return (ttEvalValue + beta) / 2;
    }

    // Step 9. Null move search with verification search
    // The non-pawn condition is important for finding Zugzwangs.
    if (CutNode && !exclude && hasNonPawn && ss->ply >= nmpPly
        && ss->evalValue - 350 + 18 * depth >= beta)
    {
        assert((ss - 1)->move != Move::Null);

        // Null move dynamic reduction
        Depth R = 7 + depth / 3;

        do_null_move(pos, st, ss);

        Value nullValue = -search<All>(pos, ss + 1, -beta, -beta + 1, depth - R);

        undo_null_move(pos);

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && !is_decisive(nullValue))
        {
            if (nmpPly != 0 || depth < 16)
                return nullValue;

            assert(nmpPly == 0);  // Recursive verification is not allowed

            // Do verification search at high depths,
            // with null move pruning disabled until ply exceeds nmpPly.
            nmpPly = ss->ply + 3 * (depth - R) / 4;

            Value v = search<All>(pos, ss, beta - 1, beta, depth - R);

            nmpPly = 0;

            if (v >= beta)
                return nullValue;

            ss->ttMove = ttd.move;
        }
    }

    improve |= ss->evalValue >= beta;

    // Step 10. Internal iterative reductions
    // For deep enough nodes without ttMoves, reduce search depth.
    // (*Scaler) Making IIR more aggressive scales poorly.
    if constexpr (!AllNode)
        if (depth > 5 && red <= 3 && ttd.move == Move::None)
            --depth;

    // Step 11. ProbCut
    // If have a good enough capture or any promotion and a reduced search
    // returns a value much above beta, can (almost) safely prune previous move.
    if (depth > 2 && !is_decisive(beta))
    {
        // clang-format off
        Value probCutBeta = std::min(235 + beta - improve * 63, +VALUE_INFINITE);
        assert(beta < probCutBeta && probCutBeta <= +VALUE_INFINITE);

        // If value from transposition table is less than probCutBeta, don't attempt probCut
        if (!(is_valid(ttd.value) && ttd.value < probCutBeta))
        {
        Depth probCutDepth = std::clamp(depth - 5 - (ss->evalValue - beta) / 315, 0, depth - 0);
        int   probCutThreshold = probCutBeta - ss->evalValue;

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
                if (!rootMoves.contains(curPV, endPV, move))
                    continue;

            do_move(pos, move, st, ss);

            // Perform a preliminary qsearch to verify that the move holds
            value = -qsearch<false>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (value >= probCutBeta && probCutDepth > DEPTH_ZERO)
                value = -search<~NT>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, probCutDepth);

            undo_move(pos, move);

            assert(is_ok(value));

            if (threads.is_stopped())
                return VALUE_ZERO;

            if (value >= probCutBeta)
            {
                // Save ProbCut data into transposition table
                if (!exclude)
                    ttu.update(move, value_to_tt(value, ss->ply), evalValue,
                               probCutDepth + 1, Bound::LOWER, ss->ttPv);

                if (!is_decisive(value))
                    return value - (probCutBeta - beta);
            }
        }
        }
        // clang-format on
    }

S_MOVES_LOOP:  // When in check, search starts here

    // Step 12. Small ProbCut idea
    if (!is_decisive(beta) && is_valid(ttd.value) && !is_decisive(ttd.value))
    {
        Value probCutBeta = std::min(418 + beta, +VALUE_INFINITE);
        if (ttd.value >= probCutBeta && ttd.depth >= depth - 4 && is_ok(ttd.bound & Bound::LOWER))
            return probCutBeta;
    }

    value = bestValue;

    std::uint8_t moveCount = 0;

    StdArray<SearchedMoves, 2> searchedMoves;

    const History<H_PIECE_SQ>* contHistory[8]{
      (ss - 1)->pieceSqHistory, (ss - 2)->pieceSqHistory,  //
      (ss - 3)->pieceSqHistory, (ss - 4)->pieceSqHistory,  //
      (ss - 5)->pieceSqHistory, (ss - 6)->pieceSqHistory,  //
      (ss - 7)->pieceSqHistory, (ss - 8)->pieceSqHistory   //
    };

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
            if (!rootMoves.contains(curPV, endPV, move))
                continue;

        ss->moveCount = ++moveCount;

        if constexpr (RootNode)
            if (is_main_worker() && rootDepth > 30 && !options["ReportMinimal"])
            {
                std::string currMove       = UCI::move_to_can(move);
                std::size_t currMoveNumber = curPV + moveCount;
                main_manager()->updateCxt.onUpdateIter({rootDepth, currMove, currMoveNumber});
            }

        if constexpr (PVNode)
            (ss + 1)->pv = nullptr;

        Square dstSq = move.dst_sq();

        Piece movedPc = pos.moved_pc(move);

        bool check    = pos.check(move);
        bool capture  = pos.capture_promo(move);
        auto captured = capture ? pos.captured_pt(move) : NO_PIECE_TYPE;

        // Calculate new depth for this move
        Depth newDepth = depth - 1;

        int deltaRatio = 608 * (beta - alpha) / rootDelta;

        int r = reduction(depth, moveCount, deltaRatio, improve);

        // (*Scaler) Increase reduction for pvHit nodes, Larger values scales well
        r += ss->ttPv * 946;

        // Step 14. Pruning at shallow depths
        // Depth conditions are important for mate finding.
        if (!RootNode && hasNonPawn && !is_loss(bestValue))
        {
            // Skip quiet moves if moveCount exceeds Futility Move Count threshold
            mp.quietAllowed &= moveCount < ((3 + depth * depth) >> !improve);

            // Reduced depth of the next LMR search
            Depth lmrDepth = newDepth - r / 1024;

            if (capture)
            {
                int history = captureHistory[+movedPc][dstSq][captured];

                // Futility pruning: for captures
                if (lmrDepth < 7 && !check)
                {
                    Value futilityValue = std::min(232 + ss->evalValue + piece_value(captured)
                                                     + 217 * lmrDepth + int(0.1279 * history),
                                                   +VALUE_INFINITE);

                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures and checks
                int margin = -std::max(166 * depth + int(34.4828e-3 * history), 0);
                if (  // Avoid pruning sacrifices of our last piece for stalemate
                  (alpha >= VALUE_DRAW || nonPawnValue != piece_value(type_of(movedPc)))
                  && pos.see(move) < margin)
                    continue;
            }
            else
            {
                int history = histories.pawn(pawnKey)[+movedPc][dstSq]
                            + (*contHistory[0])[+movedPc][dstSq]  //
                            + (*contHistory[1])[+movedPc][dstSq];

                // History based pruning
                if (history < -4083 * depth && !check)
                    continue;

                history += int(2.1563 * quietHistory[ac][move.raw()]);

                // (*Scaler) Generally, higher history scales well
                lmrDepth += int(3.1172e-4 * history);

                // Futility pruning: for quiets
                // (*Scaler) Generally, more frequent futility pruning scales well
                if (lmrDepth < 13 && !check && !ss->inCheck)
                {
                    Value futilityValue = std::min(42 + ss->evalValue + 127 * lmrDepth  //
                                                     + (ss->evalValue > alpha) * 85     //
                                                     + (bestMove == Move::None) * 161,
                                                   +VALUE_INFINITE);

                    if (futilityValue <= alpha)
                    {
                        if (!is_decisive(bestValue) && !is_win(futilityValue))
                            if (bestValue < futilityValue)
                                bestValue = futilityValue;
                        continue;
                    }
                }

                // SEE based pruning for quiets and checks
                int margin = -std::max(check * 64 * depth + 25 * lmrDepth * std::abs(lmrDepth), 0);
                if (  // Avoid pruning sacrifices of our last piece for stalemate
                  (alpha >= VALUE_DRAW || nonPawnValue != piece_value(type_of(movedPc)))
                  && pos.see(move) < margin)
                    continue;
            }
        }

        // Step 15. Extensions
        // Singular extension search. If all moves but one fail low on a search
        // of (alpha-s, beta-s), and just one fails high on (alpha, beta),
        // then that move is singular and should be extended.
        // To verify this do a reduced search on the position excluding the ttMove and
        // if the result is lower than ttValue minus a margin, then will extend the ttMove.
        // Recursive singular search is avoided.
        Depth extension = 0;

        // (*Scaler) Generally, frequent extensions scales well.
        // This includes high singularBeta values (i.e closer to ttValue) and low extension margins.
        if (!RootNode && !exclude && depth > 5 + ss->ttPv && move == ttd.move && is_valid(ttd.value)
            && !is_decisive(ttd.value) && ttd.depth >= depth - 3 && is_ok(ttd.bound & Bound::LOWER)
            && !is_shuffling(pos, ss, move))
        {
            Value singularBeta = std::max(
              ttd.value - int((0.8833 + (!PVNode && ss->ttPv) * 1.2500) * depth), -VALUE_INFINITE);
            assert(singularBeta >= -VALUE_INFINITE);
            Depth singularDepth = newDepth / 2;
            assert(singularDepth > DEPTH_ZERO);

            value = search<~~NT>(pos, ss, singularBeta, singularBeta + 1, singularDepth, 0, move);

            ss->ttMove    = ttd.move;
            ss->moveCount = moveCount;

            if (value <= singularBeta)
            {
                int corrValue = int(4.3351e-6 * absCorrectionValue);

                // clang-format off
                int doubleMargin = -4 + PVNode * 199 - !ttCapture * 201 - corrValue - (1 * ss->ply > 1 * rootDepth) * 42 - 7.0271e-3 * ttMoveHistory;
                int tripleMargin = 73 + PVNode * 302 - !ttCapture * 248 - corrValue - (2 * ss->ply > 3 * rootDepth) * 50 + ss->ttPv * 90;

                extension = 1 + (value <= singularBeta - doubleMargin)
                              + (value <= singularBeta - tripleMargin);
                // clang-format on

                if (depth < MAX_PLY - 1)
                    ++depth;
            }

            // Multi-cut pruning
            // If the ttMove is assumed to fail high based on the bound of the TT entry, and
            // if after excluding the ttMove with a reduced search fail high over the original beta,
            // assume this expected cut-node is not singular (multiple moves fail high),
            // and can prune the whole subtree by returning a soft-bound.
            else if (value >= beta && !is_decisive(value))
            {
                ttMoveHistory << -std::min(+400 + 100 * depth, +4000);

                return value;
            }

            // Negative extensions
            // If other moves failed high over (ttValue - margin) without the ttMove on a reduced search,
            // but cannot do multi-cut because (ttValue - margin) is lower than the original beta,
            // do not know if the ttMove is singular or can do a multi-cut,
            // so reduce the ttMove in favor of other moves based on some conditions:

            // If the ttMove is assumed to fail high over current beta
            else if (ttd.value >= beta)
                extension = -3;

            // If on CutNode but the ttMove is not assumed to fail high over current beta
            else if constexpr (CutNode)
                extension = -2;
        }

        // Add extension to new depth
        newDepth += extension;

        [[maybe_unused]] std::uint64_t preNodes;
        if constexpr (RootNode)
            preNodes = nodes_();

        // Step 16. Make the move
        do_move(pos, move, st, check, ss);

        assert(captured == type_of(pos.captured_pc()));

        ss->history = capture ? int(6.7813 * piece_value(captured))  //
                                  + captureHistory[+movedPc][dstSq][captured]
                              : 2 * quietHistory[ac][move.raw()]        //
                                  + (*contHistory[0])[+movedPc][dstSq]  //
                                  + (*contHistory[1])[+movedPc][dstSq];

        // Base reduction offset to compensate for other tweaks
        r += 714;
        r -= 73 * moveCount;
        r -= int(32.9272e-6 * absCorrectionValue);

        // (*Scaler) Decrease reduction if position is or has been on the PV
        r -= ss->ttPv
           * (2719 + PVNode * 983 + (is_valid(ttd.value) && ttd.value > alpha) * 922
              + (ttd.depth >= depth) * (934 + CutNode * 1011));

        // Increase reduction for CutNode
        if constexpr (CutNode)
            r += 3372 + (ttd.move == Move::None) * 997;

        // Increase reduction if ttMove is a capture
        r += ttCapture * 1119;

        // Increase reduction for quiet moves at high depth.
        // Quiet moves at high depth are less likely to be critical.
        r += (!capture && depth >= 12) * 256;

        // Increase reduction if current ply has a lot of fail high
        r += (ss->cutoffCount > 1) * (128 + (ss->cutoffCount - 2) * 512 + AllNode * 1024);

        // For first picked move (ttMove) reduce reduction
        r -= (move == ttd.move) * 2151;

        // Decrease/Increase reduction for moves with a good/bad history
        r -= int(103.7598e-3 * ss->history);

        // Scale up reduction for AllNode
        if constexpr (AllNode)
            r = r * (depth + 2) / (depth + 1);

        // Step 17. Late moves reduction / extension (LMR)
        if (depth > 1 && moveCount > 1)
        {
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth redDepth = std::max(std::min(newDepth - r / 1024, newDepth + 2), 1) + PVNode * 1;

            value = -search<Cut>(pos, ss + 1, -alpha - 1, -alpha, redDepth, newDepth - redDepth);

            // (*Scaler) Do a full-depth search when reduced LMR search fails high
            // Shallower searches here don't scales well.
            if (value > alpha)
            {
                // If the value was good enough search deeper
                bool extend = redDepth < newDepth && value > 50 + bestValue;
                // If the value was bad enough search shallower
                bool reduce = value < 9 + bestValue;

                // Adjust full-depth search based on LMR value
                newDepth += int(extend) - int(reduce);

                if (redDepth < newDepth)
                    value = -search<~NT>(pos, ss + 1, -alpha - 1, -alpha, newDepth);

                // Post LMR continuation history updates
                update_continuation_history(ss, movedPc, dstSq, 1365);
            }
        }
        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present
            r += (ttd.move == Move::None) * 1140;

            // Reduce search depth if expected reduction is high
            value = -search<~NT>(pos, ss + 1, -alpha - 1, -alpha,
                                 newDepth - ((r > 3957) + (r > 5654 && newDepth > 2)));
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if (PVNode && (moveCount == 1 || value > alpha))
        {
            pv[0]        = Move::None;
            (ss + 1)->pv = pv.data();

            // Extends ttMove if about to dive into qsearch
            if (newDepth < 1 && move == ttd.move
                && (  // TT entry is deep
                  ttd.depth > 1
                  // Handles decisive score. Improves mate finding and retrograde analysis.
                  || (ttd.depth > 0 && is_valid(ttd.value) && is_decisive(ttd.value))))
                newDepth = 1;

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth);
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
                rm.curValue = rm.uciValue = value;
                rm.boundLower = rm.boundUpper = false;

                if (value >= beta)
                {
                    rm.boundLower = true;
                    rm.uciValue   = beta;
                }
                else if (value <= alpha)
                {
                    rm.boundUpper = true;
                    rm.uciValue   = alpha;
                }

                rm.pv.resize(1);

                assert((ss + 1)->pv != nullptr);
                for (const auto* childPv = (ss + 1)->pv; *childPv != Move::None; ++childPv)
                    rm.pv.push_back(*childPv);

                // Record how often the best move has been changed in each iteration.
                // This information is used for time management.
                // In MultiPV mode, must take care to only do this for the first PV line.
                if (moveCount > 1 && curPV == 0)
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
                && !is_win(std::abs(value) + 1);

        if (bestValue < value + int(inc))
        {
            bestValue = value;

            if (alpha < value + int(inc))
            {
                bestMove = move;

                if constexpr (PVNode && !RootNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                {
                    // (*Scaler) Infrequent and small cutoff increments scales well
                    if constexpr (!RootNode)
                        (ss - 1)->cutoffCount += PVNode || extension < 2;

                    break;  // Fail-high
                }

                alpha = value;  // Update alpha! Always alpha < beta

                // Reduce depth for other moves if have found at least one score improvement
                if (depth < 16 && !is_decisive(value))
                {
                    depth -= 1 + (depth < 8);

                    if (depth < 1)
                        depth = 1;
                }
            }
        }

        // Collection of worse moves
        if (move != bestMove && moveCount <= MOVE_CAPACITY)
            searchedMoves[capture].push_back(move);
    }

    assert(moveCount != 0 || !ss->inCheck || exclude || (MoveList<LEGAL, true>(pos).empty()));
    assert(ss->moveCount == moveCount && ss->ttMove == ttd.move);

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves, it must be a mate or a stalemate.
    // If in a singular extension search then return a fail low score.
    if (moveCount == 0)
        bestValue = exclude ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;
    // Adjust best value for fail high cases
    else if (bestValue > beta && !is_win(bestValue) && !is_loss(beta))
        bestValue = (depth * bestValue + beta) / (depth + 1);

    // Don't let best value inflate too high (tb)
    if constexpr (PVNode)
        if (bestValue > maxValue)
            bestValue = maxValue;

    // If there is a move that produces search value greater than alpha update the history of searched moves
    if (bestMove != Move::None)
    {
        update_histories(pos, pawnKey, ss, depth, bestMove, searchedMoves);

        if constexpr (!RootNode)
            ttMoveHistory << (bestMove == ttd.move ? +809 : -865);
    }
    // If prior move is valid, that caused the fail low
    else if (is_ok(preSq))
    {
        // Bonus for prior quiet move
        if (!preCapture)
        {
            // clang-format off
            int bonusScale = std::max(-215
                            // Increase bonus when depth is high
                            + std::min(56 * depth, 489)
                            // Increase bonus when bestValue is lower than current static evaluation
                            + (!(ss    )->inCheck && bestValue <= +(ss    )->evalValue - 107) * 147
                            // Increase bonus when bestValue is higher than previous static evaluation
                            + (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->evalValue -  65) * 156
                            // Increase bonus when the previous moveCount is high
                            +  80 * std::min(((ss - 1)->moveCount - 1) / 5, 4)
                            // Increase bonus if the previous move has a bad history
                            - int(10.0000e-3 * (ss - 1)->history), 0);
            // clang-format on
            int bonus = bonusScale * std::min(-87 + 141 * depth, +1351);

            if (preNonPawn)
                update_pawn_history(pawnKey, pos[preSq], preSq, 35.4004e-3 * bonus);

            update_quiet_history(~ac, (ss - 1)->move, 7.4158e-3 * bonus);

            update_continuation_history(ss - 1, pos[preSq], preSq, 12.3901e-3 * bonus);
        }
        // Bonus for prior capture move
        else
        {
            auto captured = type_of(pos.captured_pc());
            assert(captured != NO_PIECE_TYPE);

            update_capture_history(pos[preSq], preSq, captured, 1012);
        }
    }

    // If no good move is found and the previous position was pvHit, then the previous
    // opponent move is probably good and the new position is added to the search tree.
    ss->ttPv |= bestValue <= alpha && (ss - 1)->ttPv;

    // Save gathered information in transposition table
    if ((!RootNode || curPV == 0) && !exclude)
        ttu.update(bestMove, value_to_tt(bestValue, ss->ply), evalValue,
                   moveCount != 0 ? depth : std::min(depth + 6, MAX_PLY - 1),
                   bestValue >= beta                  ? Bound::LOWER
                   : PVNode && bestMove != Move::None ? Bound::EXACT
                                                      : Bound::UPPER,
                   ss->ttPv);

    // Adjust correction history if the best move is none or not a capture
    // and the error direction matches whether the above/below bounds.
    if (!ss->inCheck && (bestMove == Move::None || !pos.capture(bestMove))
        && (bestValue > ss->evalValue) == (bestMove != Move::None))
    {
        int bonus = (bestValue - ss->evalValue) * depth / (8 + (bestMove != Move::None) * 2);

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
    assert(PVNode || (1 + alpha == beta));

    Key key = pos.key();

    // Check if have an upcoming move that draws by repetition
    if (alpha < VALUE_DRAW && pos.is_upcoming_repetition(ss->ply))
    {
        alpha = draw_value(key, nodes_());
        if (alpha >= beta)
            return alpha;
    }

    StdArray<Move, MAX_PLY + 1> pv;

    if constexpr (PVNode)
    {
        ss->pv[0]    = Move::None;
        (ss + 1)->pv = pv.data();

        // Update selDepth (selDepth from 1, ply from 0)
        if (selDepth < 1 + ss->ply)
            selDepth = 1 + ss->ply;
    }

    // Step 1. Initialize node
    ss->inCheck = pos.checkers_bb();

    // Step 2. Check for maximum ply reached or immediate draw
    if (ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
        return ss->ply >= MAX_PLY && !ss->inCheck ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Step 3. Transposition table lookup
    auto [ttd, ttu] = transpositionTable.probe(key);

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttd.move  = ttd.hit ? legal_tt_move(ttd.move, pos) : Move::None;
    assert(ttd.move == Move::None || pos.legal(ttd.move));
    ss->ttMove = ttd.move;
    bool ttPv  = ttd.hit && ttd.pv;

    // Check for an early TT cutoff at non-pv nodes
    if (!PVNode && ttd.depth >= DEPTH_ZERO && is_valid(ttd.value)
        && is_ok(ttd.bound & fail_bound(ttd.value >= beta)))
        return ttd.value;

    int correctionValue = ss->inCheck ? 0 : correction_value(pos, ss);

    Value evalValue, bestValue, futBaseValue;

    // Step 4. Static evaluation of the position
    if (ss->inCheck)
    {
        evalValue = VALUE_NONE;

        bestValue = futBaseValue = -VALUE_INFINITE;

        goto QS_MOVES_LOOP;
    }

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
            bestValue = (bestValue + beta) / 2;

        if (!ttd.hit)
            ttu.update(Move::None, value_to_tt(bestValue, ss->ply), evalValue, DEPTH_NONE,
                       Bound::LOWER, false);

        return bestValue;
    }

    if (alpha < bestValue)
        alpha = bestValue;

    futBaseValue = std::min(351 + ss->evalValue, +VALUE_INFINITE);

QS_MOVES_LOOP:

    Square preSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.dst_sq() : SQ_NONE;

    State st;

    Value value;

    Move move, bestMove = Move::None;

    std::uint8_t moveCount = 0;

    const History<H_PIECE_SQ>* contHistory[1]{(ss - 1)->pieceSqHistory};

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
            if (!check && dstSq != preSq && move.type() != MT::PROMOTION && !is_loss(futBaseValue))
            {
                if (moveCount > 2)
                    continue;

                // Static evaluation + value of piece going to captured
                Value futilityValue =
                  std::min(futBaseValue + piece_value(pos.captured_pt(move)), +VALUE_INFINITE);

                if (futilityValue <= alpha)
                {
                    if (bestValue < futilityValue)
                        bestValue = futilityValue;
                    continue;
                }

                // SEE based pruning
                if (pos.see(move) < (alpha - futBaseValue))
                {
                    bestValue = std::min(alpha, futBaseValue);
                    continue;
                }
            }

            // Skip quiets
            if (!capture)
                continue;

            // SEE based pruning
            if (pos.see(move) < -80)
                continue;
        }

        // Step 7. Make the move
        do_move(pos, move, st, check, ss);

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

                if constexpr (PVNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

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
            assert((MoveList<LEGAL, true>(pos).empty()));
            bestValue = mated_in(ss->ply);  // Plies to mate from the root
        }
        else
        {
            Color ac                = pos.active_color();
            pos.state()->checkersBB = PROMOTION_RANKS_BB;
            if (bestValue != VALUE_DRAW  //
                && type_of(pos.captured_pc()) >= ROOK
                && !pos.has_non_pawn(ac)
                // No pawn pushes available
                && (pawn_push_bb(pos.pieces_bb(ac, PAWN), ac) & ~pos.pieces_bb()) == 0
                && MoveList<LEGAL, true>(pos).empty())
                bestValue = VALUE_DRAW;
            pos.state()->checkersBB = 0;
        }
    }
    // Adjust best value for fail high cases
    else if (bestValue > beta && !is_win(bestValue) && !is_loss(beta))
        bestValue = (bestValue + beta) / 2;

    // Save gathered info in transposition table
    ttu.update(bestMove, value_to_tt(bestValue, ss->ply), evalValue, DEPTH_ZERO,
               fail_bound(bestValue >= beta), ttPv);

    assert(is_ok(bestValue));

    return bestValue;
}

void Worker::do_move(Position& pos, Move m, State& st, bool check, Stack* const ss) noexcept {
    bool capture = pos.capture_promo(m);
    auto db      = pos.do_move(m, st, check, this);
    nodes.fetch_add(1, std::memory_order_relaxed);
    if (ss != nullptr)
    {
        // clang-format off
        auto dstSq                   = m.dst_sq();
        ss->move                     = m;
        ss->pieceSqHistory           = &continuationHistory[ss->inCheck][capture][+db.dp.movedPc][dstSq];
        ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[+db.dp.movedPc][dstSq];
        // clang-format on
    }
    accStack.push(std::move(db));
}
void Worker::do_move(Position& pos, Move m, State& st, Stack* const ss) noexcept {
    do_move(pos, m, st, pos.check(m), ss);
}

void Worker::undo_move(Position& pos, Move m) noexcept {
    accStack.pop();
    pos.undo_move(m);
}

void Worker::do_null_move(Position& pos, State& st, Stack* const ss) noexcept {
    pos.do_null_move(st, this);
    if (ss != nullptr)
    {
        // clang-format off
        ss->move                     = Move::Null;
        ss->pieceSqHistory           = &continuationHistory[0][0][+Piece::NO_PIECE][SQUARE_ZERO];
        ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[+Piece::NO_PIECE][SQUARE_ZERO];
        // clang-format on
    }
}

void Worker::undo_null_move(Position& pos) const noexcept { pos.undo_null_move(); }

Value Worker::evaluate(const Position& pos) noexcept {
    return Evaluate::evaluate(pos, networks[numa_access_token()], accStack, accCaches,
                              optimism[pos.active_color()]);
}

// clang-format off

void Worker::update_pawn_history(Key pawnKey, Piece movedPc, Square dstSq, int bonus) noexcept {
    histories.pawn(pawnKey)[+movedPc][dstSq] << bonus;
}

void Worker::update_capture_history(Piece movedPc, Square dstSq, PieceType capturedPt, int bonus) noexcept {
    captureHistory[+movedPc][dstSq][capturedPt] << bonus;
}
void Worker::update_capture_history(const Position& pos, Move m, int bonus) noexcept {
    update_capture_history(pos.moved_pc(m), m.dst_sq(), pos.captured_pt(m), bonus);
}
void Worker::update_quiet_history(Color ac, Move m, int bonus) noexcept {
    quietHistory[ac][m.raw()] << bonus;
}
void Worker::update_low_ply_quiet_history(std::int16_t ssPly, Move m, int bonus) noexcept {
    assert(m.is_ok());

    if (ssPly < LOW_PLY_QUIET_SIZE)
        lowPlyQuietHistory[ssPly][m.raw()] << bonus;
}

// Updates quiet histories (move sorting heuristics)
void Worker::update_quiet_histories(const Position& pos, Key pawnKey, Stack* const ss, Move m, int bonus) noexcept {
    assert(m.is_ok());

    update_pawn_history(pawnKey, pos.moved_pc(m), m.dst_sq(), (bonus > 0 ? 0.8837 : 0.4932) * bonus);

    update_quiet_history(pos.active_color(), m, 1.0000 * bonus);

    update_low_ply_quiet_history(ss->ply, m, 0.7861 * bonus);

    update_continuation_history(ss, pos.moved_pc(m), m.dst_sq(), 0.8750 * bonus);
}

// Updates history at the end of search() when a bestMove is found and other searched moves are known
void Worker::update_histories(const Position& pos, Key pawnKey, Stack* const ss, Depth depth, Move bestMove, const StdArray<SearchedMoves, 2>& searchedMoves) noexcept {
    assert(ss->moveCount != 0);

    const int bonus =          std::min(- 81 + 116 * depth, +1515) + 347 * (bestMove == ss->ttMove);
    const int malus = std::max(std::min(-207 + 848 * depth, +2446) -  17 * ss->moveCount, 1);

    if (pos.capture_promo(bestMove))
    {
        update_capture_history(pos, bestMove, 1.3623 * bonus);
    }
    else
    {
        update_quiet_histories(pos, pawnKey, ss, bestMove, 0.8887 * bonus);

        // Decrease history for all non-best quiet moves
        const auto& quietMoves = searchedMoves[0];
        for (std::size_t i = 0; i < quietMoves.size(); ++i)
            update_quiet_histories(pos, pawnKey, ss, quietMoves[i], -1.0596 * (i < 5 ? 1.0 : 5.0 / (i + 1)) * malus);
    }

    // Decrease history for all non-best capture moves
    const auto& captureMoves = searchedMoves[1];
    for (std::size_t i = 0; i < captureMoves.size(); ++i)
        update_capture_history(pos, captureMoves[i], -1.4141 * malus);

    Square preSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.dst_sq() : SQ_NONE;
    // Extra penalty for a quiet early move that was not a TT move in the previous ply when it gets refuted.
    if (is_ok(preSq) && !is_ok(pos.captured_pc()) && (ss - 1)->moveCount == 1 + ((ss - 1)->ttMove != Move::None))
        update_continuation_history(ss - 1, pos[preSq], preSq, -0.5879 * malus);
}

// Updates correction histories at the end of search() when a bestMove is found
void Worker::update_correction_histories(const Position& pos, Stack* const ss, int bonus) noexcept {
    Color ac = pos.active_color();

    bonus = std::clamp(bonus, -CORRECTION_HISTORY_LIMIT / 4, +CORRECTION_HISTORY_LIMIT / 4);

    histories.    pawn_correction<WHITE>(pos.    pawn_key(WHITE))[ac] << 1.0000 * bonus;
    histories.    pawn_correction<BLACK>(pos.    pawn_key(BLACK))[ac] << 1.0000 * bonus;
    histories.   minor_correction<WHITE>(pos.   minor_key(WHITE))[ac] << 1.2188 * bonus;
    histories.   minor_correction<BLACK>(pos.   minor_key(BLACK))[ac] << 1.2188 * bonus;
    histories.non_pawn_correction<WHITE>(pos.non_pawn_key(WHITE))[ac] << 1.3906 * bonus;
    histories.non_pawn_correction<BLACK>(pos.non_pawn_key(BLACK))[ac] << 1.3906 * bonus;

    Square preSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.dst_sq() : SQ_NONE;

    if (is_ok(preSq))
    {
        (*(ss - 2)->pieceSqCorrectionHistory)[+pos[preSq]][preSq]                   << 0.9922 * bonus;
        (*(ss - 4)->pieceSqCorrectionHistory)[+pos[preSq]][preSq]                   << 0.4609 * bonus;
    }
}

// Computes the correction value for the current position from the correction histories
int Worker::correction_value(const Position& pos, const Stack* const ss) noexcept {
    constexpr std::int64_t Limit = 0x7FFFFFFF;

    Color ac = pos.active_color();

    Square preSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.dst_sq() : SQ_NONE;

    return std::clamp<std::int64_t>(
           + 5174LL * (histories.    pawn_correction<WHITE>(pos.    pawn_key(WHITE))[ac]
                     + histories.    pawn_correction<BLACK>(pos.    pawn_key(BLACK))[ac])
           + 4411LL * (histories.   minor_correction<WHITE>(pos.   minor_key(WHITE))[ac]
                     + histories.   minor_correction<BLACK>(pos.   minor_key(BLACK))[ac])
           +11530LL * (histories.non_pawn_correction<WHITE>(pos.non_pawn_key(WHITE))[ac]
                     + histories.non_pawn_correction<BLACK>(pos.non_pawn_key(BLACK))[ac])
           + 7841LL * (is_ok(preSq)
                      ? (*(ss - 2)->pieceSqCorrectionHistory)[+pos[preSq]][preSq]
                      + (*(ss - 4)->pieceSqCorrectionHistory)[+pos[preSq]][preSq]
                      : 8),
            -Limit, +Limit);
}

// clang-format on

// Called in case have no ponder move before exiting the search,
// for instance, in case stop the search during a fail high at root.
// Try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' have nothing to think about.
bool Worker::ponder_move_extracted() noexcept {
    static std::mt19937 prng(std::random_device{}());

    auto& rm0 = rootMoves[0];
    assert(rm0.pv.size() == 1);

    auto bestMove = rm0.pv[0];

    if (bestMove == Move::None)
        return false;

    State st;
    rootPos.do_move(bestMove, st, this);

    // Legal moves for the opponent
    const MoveList<LEGAL> oLegalMoves(rootPos);

    if (!oLegalMoves.empty())
    {
        Move ponderMove;

        auto [ttd, ttu] = transpositionTable.probe(rootPos.key());

        ponderMove = ttd.hit ? legal_tt_move(ttd.move, rootPos) : Move::None;

        if (ponderMove == Move::None || !oLegalMoves.contains(ponderMove))
        {
            ponderMove = Move::None;

            for (auto&& th : threads)
            {
                if (th->worker.get() == this || th->worker->completedDepth == DEPTH_ZERO)
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
                    if (th->worker.get() == this || th->worker->completedDepth == DEPTH_ZERO)
                        continue;
                    if (const auto& rm = *th->worker->rootMoves.find(bestMove); rm.pv.size() > 1)
                    {
                        ponderMove = rm.pv[1];
                        break;
                    }
                }

            if (ponderMove == Move::None)
            {
                std::uniform_int_distribution<std::size_t> distribution(0, oLegalMoves.size() - 1);
                ponderMove = *(oLegalMoves.begin() + distribution(prng));
            }
        }

        rm0.pv.push_back(ponderMove);
    }

    rootPos.undo_move(bestMove);

    return rm0.pv.size() > 1;
}

// Used to correct and extend PVs for moves that have a TB (but not a mate) score.
// Keeps the search based PV for as long as it is verified to maintain the game outcome, truncates afterwards.
// Finally, extends to mate the PV, providing a possible continuation (but not a proven mating line).
void Worker::extend_tb_pv(std::size_t index, Value& value) noexcept {
    assert(index < rootMoves.size());

    if (!options["SyzygyPVExtend"])
        return;

    bool useTimeManager = limit.use_time_manager() && options["NodesTime"] == 0;

    const TimePoint moveOverhead = options["MoveOverhead"];

    // If time manager is active, don't use more than 50% of moveOverhead time
    auto startTime = std::chrono::steady_clock::now();

    const auto time_to_abort = [&]() noexcept -> bool {
        auto endTime = std::chrono::steady_clock::now();
        return useTimeManager
            && std::chrono::duration<double, std::milli>(endTime - startTime).count()
                 > 0.5000 * moveOverhead;
    };

    bool aborted = false;

    bool useRule50 = options["Syzygy50MoveRule"];

    auto& rootMove = rootMoves[index];

    std::list<State> states;

    // Step 0. Do the rootMove, no correction allowed, as needed for MultiPV in TB
    auto& rootSt = states.emplace_back();
    rootPos.do_move(rootMove.pv[0], rootSt);

    std::int16_t ply = 1;
    // Step 1. Walk the PV to the last position in TB with correct decisive score
    while (std::size_t(ply) < rootMove.pv.size())
    {
        auto pvMove = rootMove.pv[ply];

        RootMoves rms;

        for (auto m : MoveList<LEGAL>(rootPos))
            rms.emplace_back(m);

        auto tbCfg = Tablebase::rank_root_moves(rootPos, rms, options, false, time_to_abort);

        if (rms.find(pvMove)->tbRank != rms[0].tbRank)
            break;

        auto& st = states.emplace_back();
        rootPos.do_move(pvMove, st);
        ++ply;

        // Don't allow for repetitions or drawing moves along the PV in TB regime
        if (tbCfg.rootInTB && rootPos.is_draw(ply, useRule50))
        {
            --ply;
            rootPos.undo_move(pvMove);
            break;
        }

        // Full PV shown will thus be validated and end in TB.
        // If can not validate the full PV in time, do not show it.
        if (tbCfg.rootInTB && (aborted = time_to_abort()))
            break;
    }

    // Resize the PV to the correct part
    rootMove.pv.resize(ply);

    // Step 2. Now extend the PV to mate, as if the user explores syzygy-tables.info using
    // top ranked moves (minimal DTZ), which gives optimal mates only for simple endgames e.g. KRvK
    while (!(useRule50 && rootPos.is_draw(0)))
    {
        if (aborted || (aborted = time_to_abort()))
            break;

        RootMoves rms;

        for (auto m : MoveList<LEGAL>(rootPos))
        {
            auto& rm = rms.emplace_back(m);

            State st;
            rootPos.do_move(m, st);
            // Give a score of each move to break DTZ ties
            // restricting opponent mobility, but not giving the opponent a capture.
            for (auto om : MoveList<LEGAL>(rootPos))
                rm.tbRank -= 1 + 99 * rootPos.capture(om);
            rootPos.undo_move(m);
        }

        // Mate found
        if (rms.empty())
            break;

        // Sort moves according to their above assigned TB rank.
        // This will break ties for moves with equal DTZ in rank_root_moves.
        rms.sort([](const RootMove& rm1, const RootMove& rm2) noexcept {
            return rm1.tbRank > rm2.tbRank;
        });

        // The winning side tries to minimize DTZ, the losing side maximizes it
        auto tbCfg = Tablebase::rank_root_moves(rootPos, rms, options, true, time_to_abort);

        // If DTZ is not available might not find a mate, so bail out
        if (!tbCfg.rootInTB || tbCfg.cardinality != 0)
            break;

        auto pvMove = rms[0].pv[0];
        rootMove.pv.push_back(pvMove);
        auto& st = states.emplace_back();
        rootPos.do_move(pvMove, st);
    }

    // Finding a draw in this function is an exceptional case,
    // that cannot happen when rule50 is false or during engine game play,
    // since we have a winning score, and play correctly with TB support.
    // However, it can be that a position is draw due to the 50 move rule
    // if it has been been reached on the board with a non-optimal 50 move counter
    // (e.g. 8/8/6k1/3B4/3K4/4N3/8/8 w - - 54 106) which TB with dtz counter rounding
    // cannot always correctly rank.
    // Adjust the score to match the found PV. Note that a TB loss score can be displayed
    // if the engine did not find a drawing move yet, but eventually search will figure it out.
    // (e.g. 1kq5/q2r4/5K2/8/8/8/8/7Q w - - 96 1)
    if (rootPos.is_draw(0))
        value = VALUE_DRAW;

    // Undo the PV moves
    for (auto itr = rootMove.pv.rbegin(); itr != rootMove.pv.rend(); ++itr)
        rootPos.undo_move(*itr);

    if (aborted)
        UCI::print_info_string(
          "Syzygy based PV extension requires more time, increase MoveOverhead as needed.");
}

// Initializes the time manager and resets previous search info
void MainSearchManager::init() noexcept {

    timeManager.init();
    atFirst          = true;
    preBestCurValue  = VALUE_ZERO;
    preBestAvgValue  = VALUE_ZERO;
    preTimeReduction = 0.85;
}

// Used to print debug info and, more importantly,
// to detect when out of available time and thus stop the search.
void MainSearchManager::check_time(Worker& worker) noexcept {
    assert(callsCount > 0);
    if (--callsCount > 0)
        return;
    // When using nodes, ensure checking rate is not lower than 0.1% of nodes
    callsCount = worker.limit.calls_count();

    TimePoint elapsedTime = elapsed(worker.threads);

#if !defined(NDEBUG)
    static TimePoint infoTime = now();
    if (TimePoint curTime = worker.limit.startTime + elapsedTime; curTime - infoTime > 1000)
    {
        infoTime = curTime;
        Debug::print();
    }
#endif

    // clang-format off
    if (
      // Should not stop pondering until told so by the GUI
      !ponder
      // Later rely on the fact that at least use the main-thread previous root-search
      // score and PV in a multi-threaded environment to prove mated-in scores.
      && worker.completedDepth > DEPTH_ZERO
      && ((worker.limit.use_time_manager() &&      (ponderhitStop || elapsedTime >= timeManager.maximum()))
       || (worker.limit.moveTime != 0      &&                        elapsedTime >= worker.limit.moveTime)
       || (worker.limit.nodes != 0         && worker.threads.sum(&Worker::nodes) >= worker.limit.nodes)))
        worker.threads.request_abort();
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

// Displays the principal variation (PV) along with associated information
void MainSearchManager::show_pv(Worker& worker, Depth depth) const noexcept {

    const auto& rootPos            = worker.rootPos;
    const auto& rootMoves          = worker.rootMoves;
    const auto& options            = worker.options;
    const auto& threads            = worker.threads;
    const auto& transpositionTable = worker.transpositionTable;
    const auto& tbConfig           = worker.tbConfig;
    std::size_t multiPV            = worker.multiPV;
    std::size_t curPV              = worker.curPV;

    TimePoint     time     = std::max(elapsed(), TimePoint(1));
    std::uint64_t nodes    = threads.sum(&Worker::nodes);
    std::uint16_t hashfull = transpositionTable.hashfull();
    std::uint64_t tbHits   = threads.sum(&Worker::tbHits, tbConfig.rootInTB ? rootMoves.size() : 0);

    for (std::size_t i = 0; i < multiPV; ++i)
    {
        auto& rm = rootMoves[i];

        bool updated = rm.curValue != -VALUE_INFINITE;

        if (i != 0 && depth == 1 && !updated)
            continue;

        Depth d = updated ? depth : std::max(depth - 1, 1);
        Value v = updated ? rm.uciValue : rm.preValue;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = tbConfig.rootInTB && !is_mate(v);
        if (tb)
            v = rm.tbValue;

        // tablebase- and previous-scores are exact
        bool exact = i != curPV || tb || !updated;

        // Potentially correct and extend the PV, and in exceptional cases value also
        if (is_decisive(v) && !is_mate(v) && (exact || !(rm.boundLower || rm.boundUpper)))
            worker.extend_tb_pv(i, v);

        std::string score = UCI::to_score({v, rootPos});

        std::string bound;
        if (!exact)
        {
            if (rm.boundLower)
                bound = " lowerbound";
            else if (rm.boundUpper)
                bound = " upperbound";
        }

        std::string wdl;
        if (options["UCI_ShowWDL"])
            wdl = UCI::to_wdl(v, rootPos);

        std::string pv;
        pv.reserve(6 * rm.pv.size());
        for (auto m : rm.pv)
        {
            pv += ' ';
            pv += UCI::move_to_can(m);
        }

        updateCxt.onUpdateFull(
          {{d, score}, rm.selDepth, i + 1, bound, wdl, time, nodes, hashfull, tbHits, pv});
    }
}

// Converts a Value to a Score object, considering the position for centipawn conversion
Score::Score(Value v, const Position& pos) noexcept {
    assert(is_ok(v));

    if (!is_decisive(v))
    {
        score = Unit{UCI::to_cp(v, pos)};
    }
    else if (!is_mate(v))
    {
        auto ply = VALUE_TB - std::abs(v);
        score    = v > 0 ? Tablebase{+ply, true} : Tablebase{-ply, false};
    }
    else
    {
        auto ply = VALUE_MATE - std::abs(v);
        score    = v > 0 ? Mate{+ply} : Mate{-ply};
    }
}

// Skill module for playing at reduced strength
void Skill::init(const Options& options) noexcept {

    if (options["UCI_LimitStrength"])
    {
        std::uint16_t uciELO = options["UCI_ELO"];

        auto e = double(uciELO - MIN_ELO) / (MAX_ELO - MIN_ELO);
        auto l = ((37.2473 * e - 40.8525) * e + 22.2943) * e - 0.311438;
        level  = std::clamp(l, MIN_LEVEL, MAX_LEVEL - 0.01);
    }
    else
    {
        level = options["SkillLevel"];
    }

    bestMove = Move::None;
}

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move Skill::pick_move(const RootMoves& rootMoves, std::size_t multiPV, bool pickBest) noexcept {
    assert(1 <= multiPV && multiPV <= rootMoves.size());
    static XorShift64Star prng(now());  // PRNG sequence should be non-deterministic

    if (pickBest || bestMove == Move::None)
    {
        // RootMoves are already sorted by value in descending order
        Value maxValue = rootMoves[0].curValue;

        Value delta = std::min(maxValue - rootMoves[multiPV - 1].curValue, int(VALUE_PAWN));

        Value bestValue = -VALUE_INFINITE;
        // Choose best move. For each move value add two terms, both dependent on weakness.
        // One is deterministic and bigger for weaker levels, and one is random.
        // Then choose the move with the resulting highest value.
        for (std::size_t i = 0; i < multiPV; ++i)
        {
            Value diff  = maxValue - rootMoves[i].curValue;
            Value noise = prng.rand<std::uint32_t>() % weakness();
            Value push  = (weakness() * diff + delta * noise) / 128;

            Value value = rootMoves[i].curValue + push;

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
