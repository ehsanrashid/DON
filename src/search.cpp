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
#include <initializer_list>
#include <list>
#include <random>
#include <ratio>
#include <string>

#include "bitboard.h"
#include "evaluate.h"
#include "movegen.h"
#include "movepick.h"
#include "nnue/network.h"
#include "prng.h"
#include "score.h"  // IWYU pragma: keep
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "ucioption.h"

namespace DON {

// (*Scaler):
// Search features marked by "(*Scaler)" have proven non-linear scaling.
// They are optimized to time controls of 180 + 1.8 and longer,
// so changing them or adding conditions that are similar requires
// tests at these types of time controls.

// History
History<HCapture>     CaptureHistory;
History<HQuiet>       QuietHistory;
History<HPawn>        PawnHistory;
History<HLowPlyQuiet> LowPlyQuietHistory;

StdArray<History<HContinuation>, 2, 2> ContinuationHistory;  // [inCheck][capture]

namespace {

History<HTTMove> TTMoveHistory;

// Correction History
CorrectionHistory<CHPawn>         PawnCorrectionHistory;
CorrectionHistory<CHMinor>        MinorCorrectionHistory;
CorrectionHistory<CHNonPawn>      NonPawnCorrectionHistory;
CorrectionHistory<CHContinuation> ContinuationCorrectionHistory;

// Reductions lookup table initialized at startup
StdArray<std::int16_t, MAX_MOVES> Reductions;  // [depth or moveCount]

constexpr int
reduction(Depth depth, std::uint8_t moveCount, int deltaRatio, bool improve) noexcept {
    int reductionScale = Reductions[depth] * Reductions[moveCount];
    return 1200 + reductionScale - deltaRatio + !improve * int(0.4258 * reductionScale);
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

constexpr Bound fail_bound(bool failHigh) noexcept { return failHigh ? BOUND_LOWER : BOUND_UPPER; }

Move pseudo_legal_tt_move(Move ttMove, const Position& pos) noexcept {
    return ttMove != Move::None && pos.pseudo_legal(ttMove) ? ttMove : Move::None;
}

// Appends move and appends child Pv[]
void update_pv(Move* pv, Move m, const Move* childPv) noexcept {
    assert(m.is_ok());

    for (*pv++ = m; childPv != nullptr && *childPv != Move::None;)
        *pv++ = *childPv++;
    *pv = Move::None;
}

void update_capture_history(Piece pc, Square dst, PieceType captured, int bonus) noexcept;
void update_capture_history(const Position& pos, Move m, int bonus) noexcept;
void update_quiet_history(Color ac, Move m, int bonus) noexcept;
void update_pawn_history(Key pawnKey, Piece pc, Square dst, int bonus) noexcept;
void update_continuation_history(Stack* const ss, Piece pc, Square dst, int bonus) noexcept;
void update_low_ply_quiet_history(std::int16_t ssPly, Move m, int bonus) noexcept;
void update_all_quiet_history(const Position& pos, Stack* const ss, Move m, int bonus) noexcept;
void update_all_history(const Position&      pos,
                        Stack* const         ss,
                        Depth                depth,
                        Move                 bm,
                        const MovesArray<2>& movesArr) noexcept;

void update_correction_history(const Position& pos, Stack* const ss, int bonus) noexcept;
int  correction_value(const Position& pos, const Stack* const ss) noexcept;

Value adjust_static_eval(Value ev, int cv) noexcept;

}  // namespace

namespace Search {

void init() noexcept {
    CaptureHistory.fill(-689);
    QuietHistory.fill(68);
    PawnHistory.fill(-1238);
    for (bool inCheck : {false, true})
        for (bool capture : {false, true})
            for (auto& toPieceSqHist : ContinuationHistory[inCheck][capture])
                for (auto& pieceSqHist : toPieceSqHist)
                    pieceSqHist.fill(-529);

    TTMoveHistory = 0;

    PawnCorrectionHistory.fill(5);
    MinorCorrectionHistory.fill(0);
    NonPawnCorrectionHistory.fill(0);
    for (auto& toPieceSqCorrHist : ContinuationCorrectionHistory)
        for (auto& pieceSqCorrHist : toPieceSqCorrHist)
            pieceSqCorrHist.fill(8);

    Reductions[0] = 0;
    for (std::size_t i = 1; i < Reductions.size(); ++i)
        Reductions[i] = 21.9453 * std::log(i);
}

}  // namespace Search

Worker::Worker(std::size_t               threadId,
               const SharedState&        sharedState,
               ISearchManagerPtr         searchManager,
               NumaReplicatedAccessToken accessToken) noexcept :
    // Unpack the SharedState struct into member variables
    threadIdx(threadId),
    manager(std::move(searchManager)),
    options(sharedState.options),
    networks(sharedState.networks),
    threads(sharedState.threads),
    tt(sharedState.tt),
    numaAccessToken(accessToken),
    accCaches(networks[accessToken]) {

    init();
}

// Initialize the Worker
void Worker::init() noexcept { accCaches.init(networks[numaAccessToken]); }

void Worker::ensure_network_replicated() noexcept {
    // Access once to force lazy initialization.
    // Do this because want to avoid initialization during search.
    (void) (networks[numaAccessToken]);
}

void Worker::start_search() noexcept {
    auto* const mainManager = is_main_worker() ? main_manager() : nullptr;

    accStack.reset();

    rootDepth = completedDepth = DEPTH_ZERO;
    nmpPly                     = 0;

    multiPV = DEFAULT_MULTI_PV;
    if (mainManager != nullptr)
    {
        multiPV = options["MultiPV"];
        // When playing with strength handicap enable MultiPV search that will
        // use behind-the-scenes to retrieve a set of possible moves.
        if (mainManager->skill.enabled())
            multiPV = std::max(multiPV, std::size_t(4));
    }
    multiPV = std::min(multiPV, rootMoves.size());

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
    mainManager->timeManager.init(limit, rootPos.active_color(), rootPos.ply(), rootPos.move_num(),
                                  options);
    if (!limit.infinite)
        tt.increment_generation();

    LowPlyQuietHistory.fill(97);

    bool think = false;

    if (rootMoves.empty())
    {
        rootMoves.emplace_back(Move::None);

        auto score = UCI::to_score({Value(rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW), rootPos});
        mainManager->updateCxt.onUpdateShort({DEPTH_ZERO, score});
    }
    else
    {
        Move bookBestMove = Move::None;

        if (!limit.infinite && limit.mate == 0)
        {
            // Check polyglot book
            if (options["OwnBook"] && Book.active()
                && rootPos.move_num() < options["BookProbeDepth"])
                bookBestMove = Book.probe(rootPos, options["BookPickBest"]);
        }

        if (bookBestMove != Move::None && rootMoves.contains(bookBestMove))
        {
            State st;
            rootPos.do_move(bookBestMove, st);
            Move bookPonderMove = Book.probe(rootPos, options["BookPickBest"]);
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
    while (!threads.stop.load(std::memory_order_relaxed) && (mainManager->ponder || limit.infinite))
    {}  // Busy wait for a stop or a mainManager->ponder reset

    // Stop the threads if not already stopped
    // (also raise the stop if "ponderhit" just reset mainManager->ponder).
    threads.stop.store(true, std::memory_order_relaxed);

    // Wait until all threads have finished
    threads.wait_finish();

    auto* bestWorker = this;

    if (think)
    {
        // When playing in 'Nodes as Time' mode, advance the time nodes before exiting.
        if (mainManager->timeManager.nodeTimeEnabled)
            mainManager->timeManager.advance_time_nodes(threads.nodes()
                                                        - limit.clocks[rootPos.active_color()].inc);

        // If the skill level is enabled, swap the best PV line with the sub-optimal one
        if (mainManager->skill.enabled())
        {
            Move m = mainManager->skill.pick_move(rootMoves, multiPV, false);
            for (auto&& th : threads)
                th->worker->rootMoves.swap_to_front(m);
        }

        if (multiPV == 1 && threads.size() > 1 && limit.mate == 0  //&& limit.depth == DEPTH_ZERO
            && rootMoves[0].pv[0] != Move::None)
        {
            bestWorker = threads.best_thread()->worker.get();

            // Send PV info again if have a new best worker
            if (bestWorker != this)
                mainManager->show_pv(*bestWorker, bestWorker->completedDepth);
        }

        if (limit.use_time_manager())
        {
            mainManager->moveFirst        = false;
            mainManager->preBestCurValue  = bestWorker->rootMoves[0].curValue;
            mainManager->preBestAvgValue  = bestWorker->rootMoves[0].avgValue;
            mainManager->preTimeReduction = mainManager->timeReduction;
        }
    }

    assert(!bestWorker->rootMoves.empty() && !bestWorker->rootMoves[0].pv.empty());
    const auto& rm = bestWorker->rootMoves[0];

    auto bestMove   = UCI::move_to_can(rm.pv[0]);
    auto ponderMove = UCI::move_to_can(
      rm.pv.size() > 1 || bestWorker->ponder_move_extracted() ? rm.pv[1] : Move::None);

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

    StdArray<Stack, StackOffset + (MAX_PLY + 1) + 1> stack{};
    Stack*                                           ss = &stack[StackOffset];
    for (std::int16_t i = 0 - StackOffset; i < std::int16_t(stack.size() - StackOffset); ++i)
    {
        (ss + i)->ply = i;
        if (i >= 0)
            continue;
        // Use as a sentinel
        (ss + i)->staticEval               = VALUE_NONE;
        (ss + i)->pieceSqHistory           = &ContinuationHistory[0][0][NO_PIECE][SQUARE_ZERO];
        (ss + i)->pieceSqCorrectionHistory = &ContinuationCorrectionHistory[NO_PIECE][SQUARE_ZERO];
    }
    assert(stack[0].ply == -StackOffset && stack[stack.size() - 1].ply == MAX_PLY + 1);
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
    while (!threads.stop.load(std::memory_order_relaxed) && ++rootDepth < MAX_PLY
           && !(mainManager != nullptr && limit.depth != DEPTH_ZERO && rootDepth > limit.depth))
    {
        // Age out PV variability metric
        if (mainManager != nullptr && limit.use_time_manager())
            mainManager->sumMoveChanges *= 0.50;

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (auto& rm : rootMoves)
            rm.preValue = rm.curValue;

        if (threads.research.load(std::memory_order_relaxed))
            ++researchCnt;

        std::size_t begIdx = endIdx = 0;
        // MultiPV loop. Perform a full root search for each PV line
        for (curIdx = 0; curIdx < multiPV; ++curIdx)
        {
            if (curIdx == endIdx)
                for (begIdx = endIdx++; endIdx < rootMoves.size(); ++endIdx)
                    if (rootMoves[endIdx].tbRank != rootMoves[begIdx].tbRank)
                        break;

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = 1;

            auto avgValue = rootMoves[curIdx].avgValue;
            if (avgValue == -VALUE_INFINITE)
                avgValue = VALUE_ZERO;

            auto avgSqrValue = rootMoves[curIdx].avgSqrValue;
            if (avgSqrValue == sign_sqr(-VALUE_INFINITE))
                avgSqrValue = VALUE_ZERO;

            // Reset aspiration window starting size
            int delta =
              5 + std::min(threads.size() - 1, std::size_t(8)) + std::abs(avgSqrValue) / 9000;
            Value alpha = std::max(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min(avgValue + delta, +VALUE_INFINITE);

            // Adjust optimism based on root move's avgValue
            optimism[ac]  = 137 * avgValue / (91 + std::abs(avgValue));
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
                Depth adjustedDepth =
                  std::max(rootDepth - failHighCnt - 3 * (1 + researchCnt) / 4, 1);

                bestValue = search<Root>(rootPos, ss, alpha, beta, adjustedDepth);

                // Bring the best move to the front. It is critical that sorting
                // is done with a stable algorithm because all the values but the
                // first and eventually the new best one is set to -VALUE_INFINITE
                // and want to keep the same order for all the moves except the
                // new PV that goes to the front. Note that in the case of MultiPV
                // search the already searched PV lines are preserved.
                rootMoves.sort(curIdx, endIdx);

                // If the search has been stopped, break immediately.
                // Sorting is safe because RootMoves is still valid,
                // although it refers to the previous iteration.
                if (threads.stop.load(std::memory_order_relaxed))
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

                delta = std::min(int(1.3333 * delta), 2 * +VALUE_INFINITE);

                assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
            }

            // Sort the PV lines searched so far
            rootMoves.sort(begIdx, 1 + curIdx);

            // Give some update about the PV
            if (mainManager != nullptr
                && (threads.stop.load(std::memory_order_relaxed) || 1 + curIdx == multiPV
                    || rootDepth > 30)
                // A thread that aborted search can have mated-in/TB-loss PV and score
                // that cannot be trusted, i.e. it can be delayed or refuted if have
                // had time to fully search other root-moves. Thus, suppress this output and
                // below pick a proven score/PV for this thread (from the previous iteration).
                && !(threads.abort.load(std::memory_order_relaxed)
                     && is_loss(rootMoves[0].uciValue)))
                mainManager->show_pv(*this, rootDepth);

            if (threads.stop.load(std::memory_order_relaxed))
                break;
        }

        if (!threads.stop.load(std::memory_order_relaxed))
            completedDepth = rootDepth;

        // Make sure not to pick an unproven mated-in score,
        // in case this worker prematurely stopped the search (aborted-search).
        if (threads.abort.load(std::memory_order_relaxed) && lastBestPV[0] != Move::None
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
            threads.stop.store(true, std::memory_order_relaxed);

        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (mainManager->skill.enabled() && mainManager->skill.time_to_pick(rootDepth))
            mainManager->skill.pick_move(rootMoves, multiPV);

        // Do have time for the next iteration? Can stop searching now?
        if (limit.use_time_manager()
            && !(mainManager->ponderhitStop || threads.stop.load(std::memory_order_relaxed)))
        {
            // Use part of the gained time from a previous stable move for the current move
            for (auto&& th : threads)
            {
                mainManager->sumMoveChanges +=
                  th->worker->moveChanges.load(std::memory_order_relaxed);
                th->worker->moveChanges.store(0, std::memory_order_relaxed);
            }

            // clang-format off

            // Compute evaluation inconsistency based on differences from previous best scores
            auto inconsistencyFactor = std::clamp(0.11325
                                                + 0.02115 * (mainManager->preBestAvgValue - bestValue)
                                                + 0.00987 * (mainManager->preBestCurValue - bestValue),
                                           0.9999 - 0.4311 * !mainManager->moveFirst,
                                           1.0001 + 0.5697 * !mainManager->moveFirst);

            // Compute stable depth (difference between the current search depth and the last best depth)
            Depth stableDepth = completedDepth - lastBestDepth;
            assert(stableDepth >= DEPTH_ZERO);

            // Use the stability factor to adjust the time reduction
            mainManager->timeReduction = 0.7230 + 0.7900 / (1.1040 + std::exp(-0.5189 * (stableDepth - 11.57)));

            // Compute ease factor that factors in previous time reduction
            auto easeFactor = 0.4469 * (1.4550 + mainManager->preTimeReduction) / mainManager->timeReduction;

            // Compute move instability factor based on the total move changes and the number of threads
            auto instabilityFactor = 1.0400 + 1.8956 * mainManager->sumMoveChanges / threads.size();

            // Compute node effort factor which slightly reduces effort if the completed depth is sufficiently high
            auto nodeEffortFactor = 1.0;
            if (completedDepth >= 10)
                nodeEffortFactor -= 44.0924e-6 * std::max(-92425.0 + 100000.0 * rootMoves[0].nodes / std::max(nodes.load(std::memory_order_relaxed), std::uint64_t(1)), 0.0);

            // Compute recapture factor that reduces time if recapture conditions are met
            auto recaptureFactor = 1.0;
            if ( rootPos.cap_sq() == rootMoves[0].pv[0].dst_sq()
             && (rootPos.cap_sq() & rootPos.pieces(~ac))
             && rootPos.see(rootMoves[0].pv[0]) >= 200)
                recaptureFactor -= 13.8400e-3 * std::min(+stableDepth, 25);

            // Calculate total time by combining all factors with the optimum time
            TimePoint totalTime = mainManager->timeManager.optimum() * inconsistencyFactor * easeFactor * instabilityFactor * nodeEffortFactor * recaptureFactor;
            assert(totalTime >= 0.0);
            // Cap totalTime to the available maximum time
            totalTime = std::min(totalTime, mainManager->timeManager.maximum());
            // Cap totalTime in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = std::min(0.50 * totalTime, 502.0);

            TimePoint elapsedTime = mainManager->elapsed(threads);

            // Stop the search if have exceeded the total time
            if (elapsedTime > totalTime)
            {
                // If allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainManager->ponder)
                    mainManager->ponderhitStop = true;
                else
                    threads.stop.store(true, std::memory_order_relaxed);
            }

            if (!mainManager->ponder)
                threads.research.store(elapsedTime > 0.5030 * totalTime, std::memory_order_relaxed);

            // clang-format on

            mainManager->preBestCurValue = bestValue;
        }
    }
}

// Main search function for different type of nodes.
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
    assert(!RootNode || (DEPTH_ZERO < depth && depth < MAX_PLY));

    Key key = pos.key();

    if constexpr (!RootNode)
    {
        // Dive into quiescence search when depth <= DEPTH_ZERO
        if (depth <= DEPTH_ZERO)
            return qsearch<PVNode>(pos, ss, alpha, beta);

        // Check if have an upcoming move that draws by repetition
        if (alpha < VALUE_DRAW && pos.is_upcoming_repetition(ss->ply))
        {
            alpha = draw_value(key, nodes.load(std::memory_order_relaxed));
            if (alpha >= beta)
                return alpha;
        }

        // Limit the depth if extensions made it too large
        depth = std::min(+depth, MAX_PLY - 1);
        assert(DEPTH_ZERO < depth && depth < MAX_PLY);
    }

    // Check for the available remaining time
    if (is_main_worker())
        main_manager()->check_time(*this);

    StdArray<Move, MAX_PLY + 1> pv;

    if constexpr (PVNode)
    {
        // Update selDepth (selDepth from 1, ply from 0)
        selDepth = std::max(+selDepth, 1 + ss->ply);
    }

    // Step 1. Initialize node
    ss->inCheck   = pos.checkers();
    ss->moveCount = 0;
    ss->history   = 0;

    if constexpr (!RootNode)
    {
        // Step 2. Check for stopped search or maximum ply reached or immediate draw
        if (threads.stop.load(std::memory_order_relaxed)  //
            || ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
            return ss->ply >= MAX_PLY && !ss->inCheck
                   ? evaluate(pos)
                   : draw_value(key, nodes.load(std::memory_order_relaxed));

        // Step 3. Mate distance pruning. Even if mate at the next move score would be
        // at best mates_in(1 + ss->ply), but if alpha is already bigger because a
        // shorter mate was found upward in the tree then there is no need to search further
        // because will never beat the current alpha. Same logic but with a reversed signs
        // apply also in the opposite condition of being mated instead of giving mate.
        // In this case, return a fail-high score.
        alpha = std::max(mated_in(0 + ss->ply), alpha);
        beta  = std::min(mates_in(1 + ss->ply), beta);
        if (alpha >= beta)
            return alpha;
    }

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    (ss + 1)->cutoffCount = 0;

    // At this point, if excludedMove, skip straight to step 6, static evaluation.
    // However, to save indentation, list the condition in all code between here and there.
    const bool exclude = excludedMove != Move::None;

    // Step 4. Transposition table lookup
    auto [ttd, ttu] = tt.probe(key);

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttd.move  = RootNode ? rootMoves[curIdx].pv[0]
              : ttd.hit  ? pseudo_legal_tt_move(ttd.move, pos)
                         : Move::None;
    assert(ttd.move == Move::None || pos.pseudo_legal(ttd.move));
    ss->ttMove     = ttd.move;
    bool ttCapture = ttd.move != Move::None && pos.capture_promo(ttd.move);

    if (!exclude)
        ss->pvHit = PVNode || (ttd.hit && ttd.pvHit);

    auto preSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.dst_sq() : SQ_NONE;

    bool preCapture = is_ok(pos.captured_piece());
    bool preNonPawn =
      is_ok(preSq) && type_of(pos.piece_on(preSq)) != PAWN && (ss - 1)->move.type_of() != PROMOTION;

    State st;

    // Check for an early TT cutoff at non-pv nodes
    if (!PVNode && !exclude && is_valid(ttd.value)        //
        && ttd.depth > depth - (ttd.value <= beta)        //
        && (depth > 5 || CutNode == (ttd.value >= beta))  //
        && (ttd.bound & fail_bound(ttd.value >= beta)) != 0)
    {
        // If ttMove fails high, update move sorting heuristics on TT hit
        if (ttd.move != Move::None && ttd.value >= beta)
        {
            // Bonus for a quiet ttMove
            if (!ttCapture)
                update_all_quiet_history(pos, ss, ttd.move, std::min(-71 + 130 * depth, +1043));

            // Extra penalty for early quiet moves of the previous ply
            if (is_ok(preSq) && !preCapture && (ss - 1)->moveCount < 4)
                update_continuation_history(ss - 1, pos.piece_on(preSq), preSq, -2142);
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < (1.0 - 0.20 * pos.has_rule50_high()) * rule50_threshold())
        {
            // If the depth is big enough, verify that the ttMove is really a good move
            if (depth >= 8 && !is_decisive(ttd.value) && ttd.move != Move::None
                && pos.legal(ttd.move))
            {
                pos.do_move(ttd.move, st, &tt);
                auto [_ttd, _ttu] = tt.probe(pos.key());
                _ttd.value =
                  _ttd.hit ? value_from_tt(_ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
                pos.undo_move(ttd.move);

                // Check that the ttValue after the ttMove would also trigger a cutoff
                if (!is_valid(_ttd.value) || ((ttd.value >= beta) == (-_ttd.value >= beta)))
                    return ttd.value;
            }
            else
                return ttd.value;
        }
    }

    Value value, bestValue = -VALUE_INFINITE;

    [[maybe_unused]] Value maxValue = +VALUE_INFINITE;

    // Step 5. Tablebases probe
    if (!RootNode && !exclude && tbConfig.cardinality)
    {
        auto pieceCount = pos.count<ALL_PIECE>();

        if (pieceCount <= tbConfig.cardinality
            && (pieceCount < tbConfig.cardinality || depth >= tbConfig.probeDepth)
            && pos.rule50_count() == 0 && !pos.can_castle(ANY_CASTLING))
        {
            Tablebases::ProbeState ps;

            auto wdlScore = Tablebases::probe_wdl(pos, &ps);

            // Force check of time on the next occasion
            if (is_main_worker())
                main_manager()->callsCount = 1;

            if (ps != Tablebases::PS_FAIL)
            {
                tbHits.fetch_add(1, std::memory_order_relaxed);

                auto drawValue = tbConfig.rule50Enabled ? 1 : 0;

                // Use the range VALUE_TB to VALUE_TB_WIN_IN_MAX_PLY to value
                value = wdlScore < -drawValue ? -VALUE_TB + ss->ply
                      : wdlScore > +drawValue ? +VALUE_TB - ss->ply
                                              : VALUE_DRAW + 2 * wdlScore * drawValue;

                Bound bound = wdlScore < -drawValue ? BOUND_UPPER
                            : wdlScore > +drawValue ? BOUND_LOWER
                                                    : BOUND_EXACT;

                if (bound == BOUND_EXACT || (bound == BOUND_LOWER ? value >= beta : value <= alpha))
                {
                    ttu.update(std::min(depth + 6, MAX_PLY - 1), ss->pvHit, bound, Move::None,
                               value_to_tt(value, ss->ply), VALUE_NONE);

                    return value;
                }

                if constexpr (PVNode)
                {
                    if (bound == BOUND_LOWER)
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

    Color ac = pos.active_color();

    int correctionValue = correction_value(pos, ss);

    int absCorrectionValue = std::abs(correctionValue);

    Value unadjustedStaticEval, eval;

    bool improve, worsen;

    Move move;

    // Step 6. Static evaluation of the position
    if (ss->inCheck)
    {
        unadjustedStaticEval = VALUE_NONE;

        ss->staticEval = eval = (ss - 2)->staticEval;

        improve = worsen = false;

        // Skip early pruning when in check
        goto S_MOVES_LOOP;
    }

    if (exclude)
        unadjustedStaticEval = eval = ss->staticEval;
    else if (ttd.hit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = ttd.eval;
        if (!is_valid(unadjustedStaticEval))
            unadjustedStaticEval = evaluate(pos);

        ss->staticEval = eval = adjust_static_eval(unadjustedStaticEval, correctionValue);

        // Can ttValue be used as a better position evaluation
        if (is_valid(ttd.value) && (ttd.bound & fail_bound(ttd.value > eval)) != 0)
            eval = ttd.value;
    }
    else
    {
        unadjustedStaticEval = evaluate(pos);

        ss->staticEval = eval = adjust_static_eval(unadjustedStaticEval, correctionValue);

        ttu.update(DEPTH_NONE, ss->pvHit, BOUND_NONE, Move::None, VALUE_NONE, unadjustedStaticEval);
    }

    // Use static evaluation difference to improve quiet move ordering
    if (is_ok(preSq) && !preCapture && !(ss - 1)->inCheck)
    {
        int bonus = 58 + std::clamp(-((ss - 1)->staticEval + (ss - 0)->staticEval), -200, +156);

        update_quiet_history(~ac, (ss - 1)->move, 9.0000 * bonus);
        if (!ttd.hit && preNonPawn)
            update_pawn_history(pos.pawn_key(), pos.piece_on(preSq), preSq, 14.0000 * bonus);
    }

    // Set up the improve and worsen flags.
    // improve: if the static evaluation is better than it was at the our last turn (two plies ago)
    // worsen: if the static evaluation is better than it was at the opponent last turn (one ply ago).
    improve = ss->staticEval > +(ss - 2)->staticEval;
    worsen  = ss->staticEval > -(ss - 1)->staticEval;

    // Retroactive LMR adjustments
    // The ply after beginning an LMR search, adjust the reduced depth based on
    // how the opponent's move affected the static evaluation.
    if (red >= 3 && !worsen)
        depth = std::min(depth + 1, MAX_PLY - 1);

    if (red >= 2 && ss->staticEval > 173 - (ss - 1)->staticEval)
        depth = std::max(depth - 1, 1);

    // Step 7. Razoring
    // If eval is really low, check with qsearch then return speculative fail low.
    if (!RootNode && eval <= -514 + alpha - 294 * depth * depth)
    {
        value = qsearch<PVNode>(pos, ss, alpha, beta);

        if (value <= alpha && (!PVNode || !is_decisive(value)))
            return value;

        ss->ttMove = ttd.move;
    }

    // Step 8. Futility pruning: child node
    // The depth condition is important for mate finding.
    {
        auto futility_margin = [&](bool ttHit) noexcept {
            Value futilityMult = 91 - 21 * !ttHit;

            return futilityMult * depth             //
                 - 2.0449 * futilityMult * improve  //
                 - 0.3232 * futilityMult * worsen   //
                 + 6.3249e-6 * absCorrectionValue;
        };

        if (!ss->pvHit && depth < 14 && eval >= beta && !is_win(eval) && !is_loss(beta)
            && (ttd.move == Move::None || ttCapture) && eval - futility_margin(ttd.hit) >= beta)
            return (2 * beta + eval) / 3;
    }

    // Step 9. Null move search with verification search
    // The non-pawn condition is important for finding Zugzwangs.
    if (CutNode && !exclude && pos.non_pawn_material(ac) != VALUE_ZERO && ss->ply >= nmpPly
        && !is_loss(beta) && ss->staticEval >= 390 + beta - 18 * depth)
    {
        assert((ss - 1)->move != Move::Null);

        // Null move dynamic reduction based on depth and phase
        Depth R = 6 + depth / 3 + pos.phase() / 9 + improve;

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
            // with null move pruning disabled until ply exceeds nmpMinPly.
            nmpPly = ss->ply + 3 * (depth - R) / 4;

            Value v = search<All>(pos, ss, beta - 1, beta, depth - R, 0, excludedMove);

            nmpPly = 0;

            if (v >= beta)
                return nullValue;

            ss->ttMove = ttd.move;
        }
    }

    if (!improve)
        improve = ss->staticEval >= beta;

    // Step 10. Internal iterative reductions
    // For deep enough nodes without ttMoves, reduce search depth.
    // (*Scaler) Making the reduction less aggressive scales well.
    if (!AllNode && depth > 5 && red <= 3 && ttd.move == Move::None)
        --depth;

    // Step 11. ProbCut
    // If have a good enough capture or any promotion and a reduced search
    // returns a value much above beta, can (almost) safely prune previous move.
    if (depth >= 3 && !is_decisive(beta))
    {
        // clang-format off
        Value probCutBeta = std::min(224 + beta - 64 * improve, +VALUE_INFINITE);
        // If value from transposition table is less than probCutBeta, don't attempt probCut
        if (!(is_valid(ttd.value) && ttd.value < probCutBeta))
        {
        assert(beta < probCutBeta && probCutBeta <= +VALUE_INFINITE);

        Depth probCutDepth = std::clamp(depth - 5 - (ss->staticEval - beta) / 306, 0, depth - 0);
        int   probCutThreshold = probCutBeta - ss->staticEval;

        MovePicker mp(pos, ttd.move, probCutThreshold);
        // Loop through all pseudo-legal moves
        while ((move = mp.next_move()) != Move::None)
        {
            assert(pos.pseudo_legal(move));
            assert(pos.capture_promo(move)
                   && (move == ttd.move || pos.see(move) >= probCutThreshold));

            // Check for legality
            if (move == excludedMove || !pos.legal(move))
                continue;

            // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
            // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
            if (RootNode && !rootMoves.contains(curIdx, endIdx, move))
                continue;

            do_move(pos, move, st, ss);

            // Perform a preliminary qsearch to verify that the move holds
            value = -qsearch<false>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (value >= probCutBeta && probCutDepth > DEPTH_ZERO)
                value = -search<~NT>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, probCutDepth);

            undo_move(pos, move);

            assert(is_ok(value));

            if (threads.stop.load(std::memory_order_relaxed))
                return VALUE_ZERO;

            if (value >= probCutBeta)
            {
                // Save ProbCut data into transposition table
                ttu.update(probCutDepth + 1, ss->pvHit, BOUND_LOWER, move,
                           value_to_tt(value, ss->ply), unadjustedStaticEval);

                if (!is_decisive(value))
                    return in_range(value - (probCutBeta - beta));
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
        if (ttd.value >= probCutBeta && ttd.depth >= depth - 4 && (ttd.bound & BOUND_LOWER))
            return probCutBeta;
    }

    const History<HPieceSq>* contHistory[8]{(ss - 1)->pieceSqHistory, (ss - 2)->pieceSqHistory,
                                            (ss - 3)->pieceSqHistory, (ss - 4)->pieceSqHistory,
                                            (ss - 5)->pieceSqHistory, (ss - 6)->pieceSqHistory,
                                            (ss - 7)->pieceSqHistory, (ss - 8)->pieceSqHistory};

    value = bestValue;

    std::uint8_t moveCount  = 0;
    std::uint8_t promoCount = 0;

    Move bestMove = Move::None;

    MovesArray<2> movesArr;

    MovePicker mp(pos, ttd.move, contHistory, ss->ply, -1);
    // Step 13. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None)
    {
        assert(pos.pseudo_legal(move));

        // Check for legality
        if (move == excludedMove || !pos.legal(move))
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
        // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
        if (RootNode && !rootMoves.contains(curIdx, endIdx, move))
            continue;

        ss->moveCount = ++moveCount;
        promoCount += move.type_of() == PROMOTION && move.promotion_type() < QUEEN;

        if (RootNode && is_main_worker() && rootDepth > 30 && !options["ReportMinimal"])
        {
            auto currMove = UCI::move_to_can(move);
            main_manager()->updateCxt.onUpdateIter({rootDepth, currMove, curIdx + moveCount});
        }

        if constexpr (PVNode)
            (ss + 1)->pv = nullptr;

        Square dst = move.dst_sq();

        Piece movedPiece = pos.moved_piece(move);

        bool check    = pos.check(move);
        bool capture  = pos.capture_promo(move);
        auto captured = capture ? pos.captured(move) : NO_PIECE_TYPE;

        // Calculate new depth for this move
        Depth newDepth = depth - 1;

        int deltaRatio = 757 * (beta - alpha) / rootDelta;

        int r = reduction(depth, moveCount, deltaRatio, improve);

        // (*Scaler) Increase reduction for pvHit nodes
        // Smaller or even negative value is better for short time controls
        // Bigger value is better for long time controls
        r += 946 * ss->pvHit;

        // Step 14. Pruning at shallow depth
        // Depth conditions are important for mate finding.
        if (!RootNode && !is_loss(bestValue) && pos.non_pawn_material(ac) != VALUE_ZERO)
        {
            // Skip quiet moves if moveCount exceeds Futility Move Count threshold
            if (mp.quietAllowed)
                mp.quietAllowed = (moveCount - promoCount) < ((3 + sqr(depth)) >> (!improve));

            // Reduced depth of the next LMR search
            Depth lmrDepth = newDepth - r / 1024;

            if (capture || check)
            {
                int history = CaptureHistory[movedPiece][dst][captured];

                // Futility pruning for captures
                if (lmrDepth < 7 && !check)
                {
                    Value seeGain       = PIECE_VALUE[captured] + promotion_value(move);
                    Value futilityValue = std::min(231 + ss->staticEval + 211 * lmrDepth
                                                     + 130 * history / 1024 + seeGain,
                                                   +VALUE_INFINITE);
                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures and checks
                int margin = std::max(157 * depth + history / 29, 0);
                if (  // Avoid pruning sacrifices of our last piece for stalemate
                  (alpha >= VALUE_DRAW
                   || pos.non_pawn_material(ac) != PIECE_VALUE[type_of(movedPiece)])
                  && pos.see(move) < -margin)
                    continue;
            }
            else
            {
                int history = PawnHistory[pawn_index(pos.pawn_key())][movedPiece][dst]  //
                            + (*contHistory[0])[movedPiece][dst]                        //
                            + (*contHistory[1])[movedPiece][dst];

                // History based pruning
                if (history < -4312 * depth)
                    continue;

                history += 76 * QuietHistory[ac][move.raw()] / 32;

                // (*Scaler) Generally, a lower divisor scales well
                lmrDepth += history / 3220;

                // Futility pruning for quiets
                // (*Scaler) Generally, more frequent futility pruning scales well
                if (lmrDepth < 11 && !ss->inCheck)
                {
                    Value futilityValue =
                      std::min(47 + ss->staticEval + 171 * (bestMove == Move::None) + 134 * lmrDepth
                                 + 90 * (ss->staticEval > alpha),
                               +VALUE_INFINITE);
                    if (futilityValue <= alpha)
                    {
                        if (!is_decisive(bestValue) && !is_decisive(futilityValue))
                            bestValue = std::max(bestValue, futilityValue);
                        continue;
                    }
                }

                lmrDepth = std::max(+lmrDepth, 0);

                // SEE based pruning for quiets
                if (pos.see(move) < -27 * sqr(lmrDepth))
                    continue;
            }
        }

        // Step 15. Extensions
        // Singular extension search. If all moves but one fail low on a search
        // of (alpha-s, beta-s), and just one fails high on (alpha, beta),
        // then that move is singular and should be extended. To verify this
        // do a reduced search on the position excluding the ttMove and
        // if the result is lower than ttValue minus a margin, then will
        // extend the ttMove. Recursive singular search is avoided.
        std::int8_t extension = 0;

        // (*Scaler) Generally, frequent extensions scales well.
        // This includes high singularBeta values (i.e closer to ttValue) and low extension margins.
        if (!RootNode && !exclude && depth > 5 + ss->pvHit && move == ttd.move
            && is_valid(ttd.value) && !is_decisive(ttd.value) && ttd.depth >= depth - 3
            && (ttd.bound & BOUND_LOWER))
        {
            Value singularBeta  = ttd.value - (0.9333 + 1.3500 * (!PVNode && ss->pvHit)) * depth;
            Depth singularDepth = newDepth / 2;
            assert(singularDepth > DEPTH_ZERO);

            value = search<~~NT>(pos, ss, singularBeta - 1, singularBeta, singularDepth, 0, move);

            ss->ttMove    = ttd.move;
            ss->moveCount = moveCount;

            if (value < singularBeta)
            {
                int corrValue = 4.3486e-6 * absCorrectionValue;
                // clang-format off
                int doubleMargin = -4 + 198 * PVNode - 212 * !ttCapture - corrValue - 45 * (1 * ss->ply > 1 * rootDepth) - 7.2151e-3 * TTMoveHistory;
                int tripleMargin = 76 + 308 * PVNode - 250 * !ttCapture - corrValue - 52 * (2 * ss->ply > 3 * rootDepth) + 92 * ss->pvHit;

                extension = 1 + (value < singularBeta - doubleMargin)
                              + (value < singularBeta - tripleMargin);
                // clang-format on

                depth = std::min(depth + 1, MAX_PLY - 1);
            }

            // Multi-cut pruning
            // If the ttMove is assumed to fail high based on the bound of the TT entry, and
            // if after excluding the ttMove with a reduced search fail high over the original beta,
            // assume this expected cut-node is not singular (multiple moves fail high),
            // and can prune the whole subtree by returning a soft-bound.
            else if (value >= beta && !is_decisive(value))
            {
                TTMoveHistory << -std::min(+400 + 100 * depth, +4000);
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
            preNodes = nodes.load(std::memory_order_relaxed);

        // Step 16. Make the move
        do_move(pos, move, st, check, ss);

        assert(captured == type_of(pos.captured_piece()));

        ss->history = capture ? 6.2734 * (PIECE_VALUE[captured] + promotion_value(move))
                                  + CaptureHistory[movedPiece][dst][captured]
                              : 2 * QuietHistory[ac][move.raw()]        //
                                  + (*contHistory[0])[movedPiece][dst]  //
                                  + (*contHistory[1])[movedPiece][dst];

        // (*Scaler) Decrease reduction if position is or has been on the PV
        r -= (2618 + 991 * PVNode + 903 * (is_valid(ttd.value) && ttd.value > alpha)
              + (978 + 1051 * CutNode) * (ttd.depth >= depth))
           * ss->pvHit;

        // These reduction adjustments have no proven non-linear scaling.

        // Base reduction offset to compensate for other tweaks
        r += 843;
        // Adjust reduction with move count and correction value
        r -= 66 * moveCount;
        r -= 32.8407e-6 * absCorrectionValue;

        // Increase reduction for CutNode
        if constexpr (CutNode)
            r += 3094 + 1056 * (ttd.move == Move::None);

        // Increase reduction if ttMove is a capture
        r += 1415 * ttCapture;

        // Increase reduction on repetition
        r += 2048 * (pos.repetition() == 4 && move == (ss - 4)->move);

        // Increase reduction if current ply has a lot of fail high
        r += (1051 + 814 * AllNode) * (ss->cutoffCount > 2);

        // For first picked move (ttMove) reduce reduction
        r -= 2018 * (move == ttd.move);

        // Decrease/Increase reduction for moves with a good/bad history
        r -= 794 * ss->history / 8192;

        // Step 17. Late moves reduction / extension (LMR)
        if (moveCount != 1 && depth > 1)
        {
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth redDepth = std::max(std::min(newDepth - r / 1024, newDepth + 2), 1) + PVNode;

            value = -search<Cut>(pos, ss + 1, -alpha - 1, -alpha, redDepth, newDepth - redDepth);

            // (*Scaler) Do a full-depth search when reduced LMR search fails high
            // Usually doing more shallower searches doesn't scale well to longer TCs
            if (value > alpha)
            {
                // Adjust full-depth search based on LMR value
                newDepth +=
                  // - if the value was good enough search deeper
                  +(redDepth < newDepth && value > 43 + bestValue + 2 * newDepth)
                  // - if the value was bad enough search shallower
                  - (value < 9 + bestValue);

                if (redDepth < newDepth)
                    value = -search<~NT>(pos, ss + 1, -alpha - 1, -alpha, newDepth);

                // Post LMR continuation history updates
                update_continuation_history(ss, movedPiece, dst, 1365);
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present
            r += 1118 * (ttd.move == Move::None);

            // Reduce search depth if expected reduction is high
            value = -search<~NT>(pos, ss + 1, -alpha - 1, -alpha,
                                 newDepth - (r > 3212) - (r > 4784 && newDepth > 2));
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if (PVNode && (moveCount == 1 || value > alpha))
        {
            pv[0]        = Move::None;
            (ss + 1)->pv = pv.data();

            // Extends ttMove if about to dive into qsearch
            if (move == ttd.move
                && (  // Root depth is high & TT entry is deep
                  (rootDepth > 6 && ttd.depth > 1)
                  // Handles decisive score. Improves mate finding and retrograde analysis.
                  || (is_valid(ttd.value) && is_decisive(ttd.value) && ttd.depth >= 1)))
                newDepth = std::max(+newDepth, 1);

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth);
        }

        // Step 19. Unmake move
        undo_move(pos, move);

        assert(is_ok(value));

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and return immediately without updating
        // best move, principal variation and transposition table.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if constexpr (RootNode)
        {
            auto& rm = *rootMoves.find(move);
            assert(rm.pv[0] == move);

            rm.nodes += nodes.load(std::memory_order_relaxed) - preNodes;
            // clang-format off
            rm.avgValue    = rm.avgValue    !=          -VALUE_INFINITE  ? (         value  + rm.avgValue   ) / 2 :          value;
            rm.avgSqrValue = rm.avgSqrValue != sign_sqr(-VALUE_INFINITE) ? (sign_sqr(value) + rm.avgSqrValue) / 2 : sign_sqr(value);
            // clang-format on

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.curValue = rm.uciValue = value;
                rm.selDepth               = selDepth;
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
                if (curIdx == 0 && moveCount > 1)
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
        bool inc = value == bestValue && (nodes.load(std::memory_order_relaxed) & 0xE) == 0
                && 2 + ss->ply >= rootDepth && !is_win(std::abs(value) + 1);

        if (bestValue < value + inc)
        {
            bestValue = value;

            if (alpha < value + inc)
            {
                bestMove = move;

                if constexpr (PVNode && !RootNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                {
                    // (*Scaler) Less frequent cutoff increments scales well
                    if constexpr (!RootNode)
                        (ss - 1)->cutoffCount += PVNode || (extension < 2);
                    break;  // Fail-high
                }

                alpha = value;  // Update alpha! Always alpha < beta

                // Reduce depth for other moves if have found at least one score improvement
                if (depth > 2 && depth < 16 && !is_decisive(value))
                    depth = std::max(depth - 1 - (depth < 8), 2);

                assert(depth > DEPTH_ZERO);
            }
        }

        // Collection of worse moves
        if (move != bestMove && moveCount <= 32)
            movesArr[capture].push_back(move);
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves,
    // it must be a mate or a stalemate.
    // If in a singular extension search then return a fail low score.
    assert(moveCount || !ss->inCheck || exclude || (MoveList<LEGAL, true>(pos).empty()));
    assert(ss->moveCount == moveCount && ss->ttMove == ttd.move);

    if (!moveCount)
        bestValue = exclude ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;
    // Adjust best value for fail high cases
    else if (bestValue > beta && !is_decisive(bestValue) && !is_decisive(alpha))
        bestValue = (depth * bestValue + beta) / (depth + 1);

    // Don't let best value inflate too high (tb)
    if constexpr (PVNode)
        bestValue = std::min(bestValue, maxValue);

    // If there is a move that produces search value greater than alpha update the history of searched moves
    if (bestMove != Move::None)
    {
        update_all_history(pos, ss, depth, bestMove, movesArr);
        if constexpr (!RootNode)
        {
            TTMoveHistory << (bestMove == ttd.move ? +809 : -865);
        }
    }
    // If prior move is valid, that caused the fail low
    else if (is_ok(preSq))
    {
        // Bonus for prior quiet move
        if (!preCapture)
        {
            // clang-format off
            int bonusScale = std::max<int>(-228
                            // Increase bonus when depth is high
                            + std::min(63 * depth, 508)
                            // Increase bonus when bestValue is lower than current static evaluation
                            + 143 * (!(ss    )->inCheck && bestValue <= +(ss    )->staticEval - 92)
                            // Increase bonus when bestValue is higher than previous static evaluation
                            + 149 * (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->staticEval - 70)
                            // Increase bonus when the previous moveCount is high
                            +  21 * ((ss - 1)->moveCount - 1)
                            // Increase bonus if the previous move has a bad history
                            + -9.6154e-3 * (ss - 1)->history, 0);
            // clang-format on
            int bonus = bonusScale * std::min(-92 + 144 * depth, +1365);

            update_quiet_history(~ac, (ss - 1)->move, 6.7139e-3 * bonus);
            update_continuation_history(ss - 1, pos.piece_on(preSq), preSq, 12.2070e-3 * bonus);
            if (preNonPawn)
                update_pawn_history(pos.pawn_key(), pos.piece_on(preSq), preSq, 35.5225e-3 * bonus);
        }
        // Bonus for prior capture move
        else
        {
            auto captured = type_of(pos.captured_piece());
            assert(captured != NO_PIECE_TYPE);
            update_capture_history(pos.piece_on(preSq), preSq, captured, 964);
        }
    }

    // If no good move is found and the previous position was pvHit, then the previous
    // opponent move is probably good and the new position is added to the search tree.
    if (bestValue <= alpha && !ss->pvHit)
        ss->pvHit = (ss - 1)->pvHit;

    // Save gathered information in transposition table
    if ((!RootNode || curIdx == 0) && !exclude)
    {
        Bound bound = bestValue >= beta                ? BOUND_LOWER
                    : PVNode && bestMove != Move::None ? BOUND_EXACT
                                                       : BOUND_UPPER;
        ttu.update(moveCount != 0 ? depth : std::min(depth + 6, MAX_PLY - 1), ss->pvHit, bound,
                   bestMove, value_to_tt(bestValue, ss->ply), unadjustedStaticEval);
    }

    // Adjust correction history if the best move is none or not a capture
    // and the error direction matches whether the above/below bounds.
    if (!ss->inCheck && (bestMove == Move::None || !pos.capture(bestMove))
        && (bestValue < ss->staticEval) == (bestMove == Move::None))
    {
        int bonus = (bestValue - ss->staticEval) * depth / (8 + (bestValue > ss->staticEval));
        bonus     = std::clamp(bonus, -CORRECTION_HISTORY_LIMIT / 4, +CORRECTION_HISTORY_LIMIT / 4);
        update_correction_history(pos, ss, bonus);
    }

    assert(is_ok(bestValue));

    return bestValue;
}

// Quiescence search function, which is called by the main search function,
// should be using static evaluation only, but tactical moves may confuse the static evaluation.
// To fight this horizon effect, implemented this qsearch of tactical moves only.
template<bool PVNode>
Value Worker::qsearch(Position& pos, Stack* const ss, Value alpha, Value beta) noexcept {
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (1 + alpha == beta));

    Key key = pos.key();

    // Check if have an upcoming move that draws by repetition
    if (alpha < VALUE_DRAW && pos.is_upcoming_repetition(ss->ply))
    {
        alpha = draw_value(key, nodes.load(std::memory_order_relaxed));
        if (alpha >= beta)
            return alpha;
    }

    StdArray<Move, MAX_PLY + 1> pv;

    if constexpr (PVNode)
    {
        ss->pv[0]    = Move::None;
        (ss + 1)->pv = pv.data();

        // Update selDepth (selDepth from 1, ply from 0)
        selDepth = std::max(+selDepth, 1 + ss->ply);
    }

    // Step 1. Initialize node
    ss->inCheck = pos.checkers();

    // Step 2. Check for maximum ply reached or immediate draw
    if (ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
        return ss->ply >= MAX_PLY && !ss->inCheck ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Step 3. Transposition table lookup
    auto [ttd, ttu] = tt.probe(key);

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttd.move  = ttd.hit ? pseudo_legal_tt_move(ttd.move, pos) : Move::None;
    assert(ttd.move == Move::None || pos.pseudo_legal(ttd.move));
    ss->ttMove = ttd.move;
    bool pvHit = ttd.hit && ttd.pvHit;

    // Check for an early TT cutoff at non-pv nodes
    if (!PVNode && ttd.depth >= DEPTH_ZERO && is_valid(ttd.value)
        && (ttd.bound & fail_bound(ttd.value >= beta)) != 0)
        return ttd.value;

    int correctionValue;

    Value unadjustedStaticEval, bestValue;
    Value futilityBase;

    // Step 4. Static evaluation of the position
    if (ss->inCheck)
    {
        correctionValue = 0;

        unadjustedStaticEval = VALUE_NONE;

        bestValue = futilityBase = -VALUE_INFINITE;

        goto QS_MOVES_LOOP;
    }

    correctionValue = correction_value(pos, ss);

    if (ttd.hit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = ttd.eval;
        if (!is_valid(unadjustedStaticEval))
            unadjustedStaticEval = evaluate(pos);

        ss->staticEval = bestValue = adjust_static_eval(unadjustedStaticEval, correctionValue);

        // Can ttValue be used as a better position evaluation
        if (is_valid(ttd.value) && !is_decisive(ttd.value)
            && (ttd.bound & fail_bound(ttd.value > bestValue)) != 0)
            bestValue = ttd.value;
    }
    else
    {
        unadjustedStaticEval = evaluate(pos);

        ss->staticEval = bestValue = adjust_static_eval(unadjustedStaticEval, correctionValue);
    }

    // Stand pat. Return immediately if bestValue is at least beta
    if (bestValue >= beta)
    {
        if (bestValue > beta && !is_decisive(bestValue))
            bestValue = (bestValue + beta) / 2;

        if (!ttd.hit)
            ttu.update(DEPTH_NONE, false, BOUND_LOWER, Move::None, value_to_tt(bestValue, ss->ply),
                       unadjustedStaticEval);

        return bestValue;
    }

    alpha = std::max(alpha, bestValue);

    futilityBase = std::min(352 + ss->staticEval, +VALUE_INFINITE);

QS_MOVES_LOOP:

    auto preSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.dst_sq() : SQ_NONE;

    const History<HPieceSq>* contHistory[1]{(ss - 1)->pieceSqHistory};

    Move  move;
    Value value;

    std::uint8_t moveCount  = 0;
    std::uint8_t promoCount = 0;

    Move bestMove = Move::None;

    // Initialize a MovePicker object for the current position, prepare to search the moves.
    // Because the depth is <= DEPTH_ZERO here, only captures, promotions will be generated.
    MovePicker mp(pos, ttd.move, contHistory, ss->ply);
    // Step 5. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None)
    {
        assert(pos.pseudo_legal(move));
        assert(ss->inCheck || move == ttd.move || pos.capture_promo(move));

        // Check for legality
        if (!pos.legal(move))
            continue;

        ++moveCount;
        promoCount += move.type_of() == PROMOTION && move.promotion_type() < QUEEN;

        Square dst = move.dst_sq();

        bool check   = pos.check(move);
        bool capture = pos.capture_promo(move);

        // Step 6. Pruning
        if (!is_loss(bestValue))
        {
            // Futility pruning and moveCount pruning
            if (dst != preSq && !check && !is_loss(futilityBase)
                && (move.type_of() != PROMOTION || move.promotion_type() < QUEEN))
            {
                if ((moveCount - promoCount) > 2)
                    continue;

                auto  captured      = capture ? pos.captured(move) : NO_PIECE_TYPE;
                Value seeGain       = PIECE_VALUE[captured] + promotion_value(move);
                Value futilityValue = std::min(futilityBase + seeGain, +VALUE_INFINITE);
                // If static evaluation + value of piece going to captured is much lower than alpha
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }

                // SEE based pruning
                if (pos.see(move) < (alpha - futilityBase))
                {
                    bestValue = std::min(alpha, futilityBase);
                    continue;
                }
            }

            // Skip non-captures
            if (!capture)
                continue;

            // SEE based pruning
            if (pos.see(move) < -78)
                continue;
        }

        State st;
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
    if (!moveCount)
    {
        // A special case: if in check and no legal moves were found, it is checkmate.
        if (ss->inCheck)
        {
            assert(bestValue == -VALUE_INFINITE);
            assert((MoveList<LEGAL, true>(pos).empty()));
            return mated_in(ss->ply);  // Plies to mate from the root
        }
        else
        {
            Color ac = pos.active_color();
            if (bestValue != VALUE_DRAW  //
                && pos.non_pawn_material(ac) == VALUE_ZERO
                && type_of(pos.captured_piece()) >= ROOK
                // No pawn pushes available
                && !(pawn_push_bb(pos.pieces(ac, PAWN), ac) & ~pos.pieces()))
            {
                pos.state()->checkers = PROMOTION_RANK_BB;
                if (MoveList<LEGAL, true>(pos).empty())
                    bestValue = VALUE_DRAW;
                pos.state()->checkers = 0;
            }
        }
    }
    // Adjust best value for fail high cases
    else if (bestValue > beta && !is_decisive(bestValue))
        bestValue = (bestValue + beta) / 2;

    // Save gathered info in transposition table
    ttu.update(DEPTH_ZERO, pvHit, fail_bound(bestValue >= beta), bestMove,
               value_to_tt(bestValue, ss->ply), unadjustedStaticEval);

    assert(is_ok(bestValue));

    return bestValue;
}

void Worker::do_move(Position& pos, Move m, State& st, bool check, Stack* const ss) noexcept {
    bool capture = pos.capture_promo(m);
    auto dp      = pos.do_move(m, st, check, &tt);
    nodes.fetch_add(1, std::memory_order_relaxed);
    accStack.push(dp);
    if (ss != nullptr)
    {
        auto dst                     = m.dst_sq();
        ss->move                     = m;
        ss->pieceSqHistory           = &ContinuationHistory[ss->inCheck][capture][dp.pc][dst];
        ss->pieceSqCorrectionHistory = &ContinuationCorrectionHistory[dp.pc][dst];
    }
}
void Worker::do_move(Position& pos, Move m, State& st, Stack* const ss) noexcept {
    do_move(pos, m, st, pos.check(m), ss);
}

void Worker::undo_move(Position& pos, Move m) noexcept {
    pos.undo_move(m);
    accStack.pop();
}

void Worker::do_null_move(Position& pos, State& st, Stack* const ss) const noexcept {
    pos.do_null_move(st, &tt);
    if (ss != nullptr)
    {
        ss->move                     = Move::Null;
        ss->pieceSqHistory           = &ContinuationHistory[0][0][NO_PIECE][SQUARE_ZERO];
        ss->pieceSqCorrectionHistory = &ContinuationCorrectionHistory[NO_PIECE][SQUARE_ZERO];
    }
}

void Worker::undo_null_move(Position& pos) const noexcept { pos.undo_null_move(); }

Value Worker::evaluate(const Position& pos) noexcept {
    return DON::evaluate(pos, networks[numaAccessToken], accStack, accCaches,
                         optimism[pos.active_color()]);
}

// Called in case have no ponder move before exiting the search,
// for instance, in case stop the search during a fail high at root.
// Try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' have nothing to think about.
bool Worker::ponder_move_extracted() noexcept {
    static std::mt19937 prng(std::random_device{}());

    auto& rm0 = rootMoves[0];
    assert(rm0.pv.size() == 1);

    auto bm = rm0.pv[0];

    if (bm == Move::None)
        return false;

    State st;
    rootPos.do_move(bm, st, &tt);

    // Legal moves for the opponent
    const MoveList<LEGAL> legalMoveList(rootPos);
    if (!legalMoveList.empty())
    {
        Move pm;

        auto [ttd, ttu] = tt.probe(rootPos.key());

        pm = ttd.hit ? pseudo_legal_tt_move(ttd.move, rootPos) : Move::None;
        if (pm == Move::None || !legalMoveList.contains(pm))
        {
            pm = Move::None;
            for (auto&& th : threads)
            {
                if (th->worker.get() == this || th->worker->completedDepth == DEPTH_ZERO)
                    continue;
                if (const auto& rm = th->worker->rootMoves[0]; rm.pv[0] == bm && rm.pv.size() > 1)
                {
                    pm = rm.pv[1];
                    break;
                }
            }
            if (pm == Move::None)
                for (auto&& th : threads)
                {
                    if (th->worker.get() == this || th->worker->completedDepth == DEPTH_ZERO)
                        continue;
                    if (const auto& rm = *th->worker->rootMoves.find(bm); rm.pv.size() > 1)
                    {
                        pm = rm.pv[1];
                        break;
                    }
                }
            if (pm == Move::None)
            {
                std::uniform_int_distribution<std::size_t> distribute(0, legalMoveList.size() - 1);
                pm = *(legalMoveList.begin() + distribute(prng));
            }
        }

        rm0.pv.push_back(pm);
    }

    rootPos.undo_move(bm);
    return rm0.pv.size() > 1;
}

// Used to correct and extend PVs for moves that have a TB (but not a mate) score.
// Keeps the search based PV for as long as it is verified to maintain the game outcome, truncates afterwards.
// Finally, extends to mate the PV, providing a possible continuation (but not a proven mating line).
void Worker::extend_tb_pv(std::size_t index, Value& value) noexcept {
    assert(index < rootMoves.size());

    if (!options["SyzygyPVExtend"])
        return;

    auto startTime = SteadyClock::now();

    TimePoint moveOverhead = options["MoveOverhead"];

    // Do not use more than (0.5 * moveOverhead) time, if time manager is active.
    const auto time_to_abort = [&]() noexcept -> bool {
        auto endTime = SteadyClock::now();
        return limit.use_time_manager()
            && 2 * std::chrono::duration<double, std::milli>(endTime - startTime).count()
                 > moveOverhead;
    };

    bool rule50Enabled = options["Syzygy50MoveRule"];

    auto& rootMove = rootMoves[index];

    std::list<State> states;

    // Step 0. Do the rootMove, no correction allowed, as needed for MultiPV in TB
    auto& rootSt = states.emplace_back();
    rootPos.do_move(rootMove.pv[0], rootSt);

    // Step 1. Walk the PV to the last position in TB with correct decisive score
    std::int16_t ply = 1;
    while (std::size_t(ply) < rootMove.pv.size())
    {
        auto pvMove = rootMove.pv[ply];

        RootMoves rms;
        for (auto m : MoveList<LEGAL>(rootPos))
            rms.emplace_back(m);

        auto tbc = Tablebases::rank_root_moves(rootPos, rms, options);

        if (rms.find(pvMove)->tbRank != rms[0].tbRank)
            break;

        auto& st = states.emplace_back();
        rootPos.do_move(pvMove, st);
        ++ply;

        // Don't allow for repetitions or drawing moves along the PV in TB regime
        if (tbc.rootInTB && rootPos.is_draw(ply, rule50Enabled))
        {
            --ply;
            rootPos.undo_move(pvMove);
            break;
        }

        // Full PV shown will thus be validated and end in TB.
        // If we cannot validate the full PV in time, we do not show it.
        if (tbc.rootInTB && time_to_abort())
            break;
    }

    // Resize the PV to the correct part
    rootMove.pv.resize(ply);

    // Step 2. Now extend the PV to mate, as if the user explores syzygy-tables.info using
    // top ranked moves (minimal DTZ), which gives optimal mates only for simple endgames e.g. KRvK
    while (!(rule50Enabled && rootPos.is_draw(0)))
    {
        if (time_to_abort())
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
                rm.tbRank -= rootPos.capture(om) ? 100 : 1;
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
        auto tbc = Tablebases::rank_root_moves(rootPos, rms, options, true);

        // If DTZ is not available might not find a mate, so bail out
        if (!tbc.rootInTB || tbc.cardinality)
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

    if (time_to_abort())
        UCI::print_info_string(
          "Syzygy based PV extension requires more time, increase MoveOverhead as needed.");
}

void MainSearchManager::init() noexcept {

    timeManager.clear();
    moveFirst        = true;
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
      && ((worker.limit.use_time_manager() && (ponderhitStop || elapsedTime >= timeManager.maximum()))
       || (worker.limit.moveTime != 0      &&                   elapsedTime >= worker.limit.moveTime)
       || (worker.limit.nodes != 0         &&        worker.threads.nodes() >= worker.limit.nodes)))
    {
        worker.threads.stop.store(true, std::memory_order_relaxed);
        worker.threads.abort.store(true, std::memory_order_relaxed);
    }
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
TimePoint MainSearchManager::elapsed(const ThreadPool& threads) const noexcept {
    return timeManager.elapsed([&threads]() { return threads.nodes(); });
}

void MainSearchManager::show_pv(Worker& worker, Depth depth) const noexcept {

    const auto& rootPos   = worker.rootPos;
    const auto& rootMoves = worker.rootMoves;
    const auto& tbConfig  = worker.tbConfig;

    TimePoint     time     = std::max(elapsed(), TimePoint(1));
    std::uint64_t nodes    = worker.threads.nodes();
    std::uint16_t hashfull = worker.tt.hashfull();
    std::uint64_t tbHits   = worker.threads.tbHits() + (tbConfig.rootInTB ? rootMoves.size() : 0);

    for (std::size_t i = 0; i < worker.multiPV; ++i)
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
        bool exact = i != worker.curIdx || tb || !updated;

        // Potentially correct and extend the PV, and in exceptional cases value also
        if (is_decisive(v) && !is_mate(v) && (exact || !(rm.boundLower || rm.boundUpper)))
            worker.extend_tb_pv(i, v);

        auto score = UCI::to_score({v, rootPos});
        auto bound = std::string_view{exact           ? ""
                                      : rm.boundLower ? " lowerbound"
                                      : rm.boundUpper ? " upperbound"
                                                      : ""};
        auto wdl   = worker.options["UCI_ShowWDL"] ? UCI::to_wdl(v, rootPos) : "";

        std::string pv;
        pv.reserve(6 * rm.pv.size());
        for (auto m : rm.pv)
            pv += " " + UCI::move_to_can(m);

        updateCxt.onUpdateFull(
          {{d, score}, rm.selDepth, i + 1, bound, wdl, time, nodes, hashfull, tbHits, pv});
    }
}

void Skill::init(const Options& options) noexcept {

    if (options["UCI_LimitStrength"])
    {
        std::uint16_t uciELO = options["UCI_ELO"];

        auto e = double(uciELO - MinELO) / (MaxELO - MinELO);
        auto x = ((37.2473 * e - 40.8525) * e + 22.2943) * e - 0.311438;
        level  = std::clamp(x, MinLevel, MaxLevel - 0.01);
    }
    else
    {
        level = options["SkillLevel"];
    }

    bestMove = Move::None;
}

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move Skill::pick_move(const RootMoves& rootMoves, std::size_t multiPV, bool pickEnabled) noexcept {
    assert(1 <= multiPV && multiPV <= rootMoves.size());
    static PRNG<XorShift64Star> prng(now());  // PRNG sequence should be non-deterministic

    if (pickEnabled || bestMove == Move::None)
    {
        // RootMoves are already sorted by value in descending order
        Value curValue = rootMoves[0].curValue;
        auto  delta    = std::min(curValue - rootMoves[multiPV - 1].curValue, int(VALUE_PAWN));
        auto  weakness = 2.0 * (3.0 * MaxLevel - level);

        Value maxValue = -VALUE_INFINITE;
        // Choose best move. For each move value add two terms, both dependent on weakness.
        // One is deterministic and bigger for weaker levels, and one is random.
        // Then choose the move with the resulting highest value.
        for (std::size_t i = 0; i < multiPV; ++i)
        {
            Value value = rootMoves[i].curValue
                        // This is magic formula for Push
                        + int(weakness * (curValue - rootMoves[i].curValue)
                              + delta * (prng.rand<std::uint32_t>() % int(weakness)))
                            / 128;

            if (maxValue <= value)
            {
                maxValue = value;
                bestMove = rootMoves[i].pv[0];
            }
        }
    }

    return bestMove;
}

namespace {

void update_capture_history(Piece pc, Square dst, PieceType captured, int bonus) noexcept {
    CaptureHistory[pc][dst][captured] << bonus;
}
void update_capture_history(const Position& pos, Move m, int bonus) noexcept {
    assert(pos.pseudo_legal(m));
    update_capture_history(pos.moved_piece(m), m.dst_sq(), pos.captured(m), bonus);
}
void update_quiet_history(Color ac, Move m, int bonus) noexcept {
    assert(m.is_ok());
    QuietHistory[ac][m.raw()] << bonus;
}
void update_pawn_history(Key pawnKey, Piece pc, Square dst, int bonus) noexcept {
    PawnHistory[pawn_index(pawnKey)][pc][dst] << bonus;
}

// clang-format off

// Updates histories of the move pairs formed by
// move at ply -1, -2, -3, -4, -5 and -6 with move at ply 0.
void update_continuation_history(Stack* const ss, Piece pc, Square dst, int bonus) noexcept {
    assert(is_ok(dst));

    constexpr StdArray<std::pair<std::uint8_t, double>, 8> ContHistoryWeights{{
        {1, 1.1299}, {2, 0.6328}, {3, 0.2812}, {4, 0.5625},
        {5, 0.1367}, {6, 0.4307}, {7, 0.2222}, {8, 0.2167}}};

    for (auto &[i, weight] : ContHistoryWeights)
    {
        // Only update the first 2 continuation histories if in check
        if ((i > 2 && ss->inCheck) || !(ss - i)->move.is_ok())
            break;

        (*(ss - i)->pieceSqHistory)[pc][dst] << weight * bonus + 88 * (i < 2);
    }
}
void update_low_ply_quiet_history(std::int16_t ssPly, Move m, int bonus) noexcept {
    assert(m.is_ok());
    if (ssPly < LOW_PLY_SIZE)
        LowPlyQuietHistory[ssPly][m.raw()] << bonus;
}
void update_all_quiet_history(const Position& pos, Stack* const ss, Move m, int bonus) noexcept {
    assert(m.is_ok());

    update_quiet_history(pos.active_color(), m, 1.0000 * bonus);
    update_pawn_history(pos.pawn_key(), pos.moved_piece(m), m.dst_sq(), (bonus > 0 ? 0.8301 : 0.5371) * bonus);
    update_continuation_history(ss, pos.moved_piece(m), m.dst_sq(), 0.9326 * bonus);
    update_low_ply_quiet_history(ss->ply, m, 0.7432 * bonus);
}

// Updates history at the end of search() when a bestMove is found
void update_all_history(const Position& pos, Stack* const ss, Depth depth, Move bm, const MovesArray<2>& movesArr) noexcept {
    assert(pos.legal(bm));
    assert(ss->moveCount);

    int bonus =          std::min(- 77 + 121 * depth, +1633) + 375 * (bm == ss->ttMove);
    int malus = std::max(std::min(-196 + 825 * depth, +2159) -  16 * ss->moveCount, 1);

    if (pos.capture_promo(bm))
    {
        update_capture_history(pos, bm, 1.4473 * bonus);
    }
    else
    {
        update_all_quiet_history(pos, ss, bm, 0.8604 * bonus);

        // Decrease history for all non-best quiet moves
        for (auto qm : movesArr[0])
            update_all_quiet_history(pos, ss, qm, -1.0576 * malus);
    }

    // Decrease history for all non-best capture moves
    for (auto cm : movesArr[1])
        update_capture_history(pos, cm, -1.3643 * malus);

    auto m = (ss - 1)->move;
    // Extra penalty for a quiet early move that was not a TT move
    // in the previous ply when it gets refuted.
    if (m.is_ok() && !is_ok(pos.captured_piece()) && (ss - 1)->moveCount == 1 + ((ss - 1)->ttMove != Move::None))
        update_continuation_history(ss - 1, pos.piece_on(m.dst_sq()), m.dst_sq(), -0.5996 * malus);
}

void update_correction_history(const Position& pos, Stack* const ss, int bonus) noexcept {
    Color ac = pos.active_color();

       PawnCorrectionHistory[correction_index(pos.pawn_key(WHITE))][WHITE][ac]     << 1.0000 * bonus;
       PawnCorrectionHistory[correction_index(pos.pawn_key(BLACK))][BLACK][ac]     << 1.0000 * bonus;
      MinorCorrectionHistory[correction_index(pos.minor_key())][ac]                << 1.1328 * bonus;
    NonPawnCorrectionHistory[correction_index(pos.non_pawn_key(WHITE))][WHITE][ac] << 1.2891 * bonus;
    NonPawnCorrectionHistory[correction_index(pos.non_pawn_key(BLACK))][BLACK][ac] << 1.2891 * bonus;

    auto m = (ss - 1)->move;
    if (m.is_ok())
    {
        (*(ss - 2)->pieceSqCorrectionHistory)[pos.piece_on(m.dst_sq())][m.dst_sq()] << 1.0703 * bonus;
        (*(ss - 4)->pieceSqCorrectionHistory)[pos.piece_on(m.dst_sq())][m.dst_sq()] << 0.5000 * bonus;
    }
}

// (*Scaler) All tuned parameters at time controls shorter than
// optimized for require verifications at longer time controls
int correction_value(const Position& pos, const Stack* const ss) noexcept {
    Color ac = pos.active_color();

    auto m = (ss - 1)->move;

    return + 9536 * (   PawnCorrectionHistory[correction_index(pos.pawn_key(WHITE))][WHITE][ac]
                   +    PawnCorrectionHistory[correction_index(pos.pawn_key(BLACK))][BLACK][ac])
           + 8494 * (  MinorCorrectionHistory[correction_index(pos.minor_key())][ac])
           +10132 * (NonPawnCorrectionHistory[correction_index(pos.non_pawn_key(WHITE))][WHITE][ac]
                   + NonPawnCorrectionHistory[correction_index(pos.non_pawn_key(BLACK))][BLACK][ac])
           + 7156 * (m.is_ok()
                    ? (*(ss - 2)->pieceSqCorrectionHistory)[pos.piece_on(m.dst_sq())][m.dst_sq()]
                    + (*(ss - 4)->pieceSqCorrectionHistory)[pos.piece_on(m.dst_sq())][m.dst_sq()]
                    : 8);
}

// clang-format on

// Update raw staticEval according to various CorrectionHistory value
// and guarantee evaluation does not hit the tablebase range.
Value adjust_static_eval(Value ev, int cv) noexcept { return in_range(ev + 7.6294e-6 * cv); }

}  // namespace
}  // namespace DON
