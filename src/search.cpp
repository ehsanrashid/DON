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

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <list>
#include <ratio>

#include "evaluate.h"
#include "misc.h"
#include "polybook.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "ucioption.h"
#include "nnue/nnue_common.h"
#include "nnue/nnue_misc.h"

namespace DON {

namespace {

// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
constexpr Value value_to_tt(Value v, std::int16_t ply) noexcept {

    if (v == VALUE_NONE)
        return v;
    assert(-VALUE_INFINITE < v && v < +VALUE_INFINITE);
    return v >= VALUE_TB_WIN_IN_MAX_PLY  ? std::min(v + ply, +VALUE_MATE)
         : v <= VALUE_TB_LOSS_IN_MAX_PLY ? std::max(v - ply, -VALUE_MATE)
                                         : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated from
// current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, return the highest non-TB score instead.
constexpr Value value_from_tt(Value v, std::int16_t ply, std::uint8_t rule50) noexcept {

    if (v == VALUE_NONE)
        return v;
    assert(-VALUE_INFINITE < v && v < +VALUE_INFINITE);
    // Handle TB win or better
    if (v >= VALUE_TB_WIN_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate value
        if (v >= VALUE_MATES_IN_MAX_PLY  //
            && VALUE_MATE - v > 2 * Position::DrawMoveCount - rule50)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB - v > 2 * Position::DrawMoveCount - rule50)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        return v - ply;
    }
    // Handle TB loss or worse
    if (v <= VALUE_TB_LOSS_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate value
        if (v <= VALUE_MATED_IN_MAX_PLY  //
            && VALUE_MATE + v > 2 * Position::DrawMoveCount - rule50)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB + v > 2 * Position::DrawMoveCount - rule50)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        return v + ply;
    }

    return v;
}

}  // namespace

void TTUpdater::update(
  Depth depth, bool pv, Bound bound, Move move, Value value, Value eval) noexcept {

    if (tte->key16 != key16)
    {
        tte = fte;
        for (std::uint8_t i = 0; i < TT_CLUSTER_ENTRY_COUNT; ++i)
        {
            if (fte[i].key16 == key16)
            {
                tte = &fte[i];
                break;
            }
            // Find an entry to be replaced according to the replacement strategy
            if (i != 0 && tte->worth(generation) > fte[i].worth(generation))
                tte = &fte[i];
        }
    }
    else
    {
        if (tte == &fte[TT_CLUSTER_ENTRY_COUNT - 1] && fte->key16 == key16)
            tte = fte;
        while (tte != fte && (tte - 1)->key16 == key16)
            --tte;
    }

    tte->save(key16, depth, pv, bound, move, value_to_tt(value, ssPly), eval, generation);
}

namespace {

// clang-format off
// History
History<HButterfly>            mainHistory;
History<HCapture>           captureHistory;
History<HPawn>                 pawnHistory;
History<HContinuation> continuationHistory;
History<HLowPly>             lowPlyHistory;

// Correction History
CorrectionHistory<CHPawn>                 pawnCorrectionHistory;
CorrectionHistory<CHNonPawn>           nonPawnCorrectionHistory;
CorrectionHistory<CHMinor>               minorCorrectionHistory;
CorrectionHistory<CHMajor>               majorCorrectionHistory;
CorrectionHistory<CHContinuation> continuationCorrectionHistory;
// clang-format on

PolyBook polyBook;

// Reductions lookup table initialized at startup
std::array<std::int16_t, MAX_MOVES> reductions;  // [depth or moveCount]

constexpr Depth reduction(
  Depth depth, std::uint8_t moveCount, int deltaRatio, bool improve, bool recover) noexcept {
    int reductionScale = reductions[depth] * reductions[moveCount];
    return std::max(1239 + reductionScale - deltaRatio, 0)
         + 1135 * (reductionScale > 1341 && !improve)  //
         + 1024 * (reductionScale > 2800 && !improve && !recover);
}

// Futility margin
constexpr Value
futility_margin(Depth depth, bool cutNode, bool ttHit, bool improve, bool recover) noexcept {
    return (depth - 2.0 * improve - 0.33334 * recover) * (118 - 33 * (cutNode && !ttHit));
}

// History and stats update bonus, based on depth
constexpr int stat_bonus(Depth depth) noexcept { return std::min(-108 + 179 * depth, 1598); }

// History and stats update malus, based on depth
constexpr int stat_malus(Depth depth) noexcept { return std::min(-261 + 820 * depth, 2246); }

// Add a small random value to draw evaluation to avoid 3-fold blindness
constexpr Value draw_value(Key key, std::uint64_t nodes) noexcept {
    return VALUE_DRAW + (key & 1) - (nodes & 1);
}

constexpr Bound bound_for_tt(bool failHigh) noexcept {
    return failHigh ? BOUND_LOWER : BOUND_UPPER;
}
constexpr Bound bound_for_tt(bool failHigh, bool failLow) noexcept {
    return failHigh ? BOUND_LOWER : failLow ? BOUND_UPPER : BOUND_NONE;
}

// Adds move and appends pv[] at ply +1
void update_pv(Stack* ss, Move move) noexcept {
    assert(move.is_ok());
    ss->pv = (ss + 1)->pv;
    ss->pv.push_front(move);
}

// clang-format off
void update_main_history(Color ac, Move m, int bonus) noexcept;
void update_pawn_history(const Position& pos, Piece pc, Square to, int bonus) noexcept;
void update_continuation_history(Stack* ss, Piece pc, Square to, int bonus) noexcept;
void update_low_ply_history(std::int16_t ssPly, Move m, int bonus) noexcept;
void update_quiet_history(const Position& pos, Stack* ss, Move m, int bonus) noexcept;
void update_capture_history(const Position& pos, Move m, int bonus) noexcept;
void update_history(const Position& pos, Stack* ss, Depth depth, Move bm, const std::array<Moves, 2>& moves) noexcept;
void update_correction_history(const Position& pos, Stack* ss, int bonus) noexcept;
Value adjust_static_eval(Value ev, const Position& pos, Stack* ss) noexcept;
// clang-format on

void extend_tb_pv(Position&      rootPos,
                  RootMove&      rootMove,
                  Value&         value,
                  const Limit&   limit,
                  const Options& options) noexcept;

}  // namespace

Worker::Worker(std::uint16_t             threadId,
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
    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    rootDepth      = DEPTH_ZERO;
    completedDepth = DEPTH_ZERO;
    nodes          = 0;
    tbHits         = 0;
    moveChanges    = 0;

    multiPV = DEFAULT_MULTI_PV;
    if (mainManager)
    {
        multiPV = options["MultiPV"];
        // When playing with strength handicap enable MultiPV search that will
        // use behind-the-scenes to retrieve a set of possible moves.
        if (mainManager->skill.enabled())
            multiPV = std::max<size_t>(multiPV, 4);
    }
    multiPV = std::min<size_t>(multiPV, rootMoves.size());

    // Non-main threads go directly to iterative_deepening()
    if (!mainManager)
    {
        iterative_deepening();
        return;
    }

    mainManager->callsCount    = limit.hitRate;
    mainManager->ponder        = limit.ponder;
    mainManager->ponderhitStop = false;
    mainManager->timeManager.init(limit, rootPos, options);
    mainManager->skill.init(options);
    mainManager->iterBestValue.fill(mainManager->preBestCurValue);

    tt.update_generation(!limit.infinite);
    lowPlyHistory.fill(0);

    bool bookMovePlayed = false;

    if (rootMoves.empty())
    {
        rootMoves.emplace(Move::None());
        mainManager->updateCxt.onUpdateEnd({bool(rootPos.checkers())});
    }
    else
    {
        Move bookBestMove = Move::None();
        if (!limit.infinite && limit.mate == 0)
        {
            // Check polyglot book
            if (options["OwnBook"] && polyBook.enabled()
                && rootPos.move_num() < options["BookDepth"])
                bookBestMove = polyBook.probe(rootPos, options["BookPickBest"]);
        }

        if (bookBestMove != Move::None() && rootMoves.contains(bookBestMove))
        {
            bookMovePlayed = true;

            State st;
            ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

            rootPos.do_move(bookBestMove, st);
            Move bookPonderMove = polyBook.probe(rootPos, options["BookPickBest"]);
            rootPos.undo_move(bookBestMove);

            for (auto&& th : threads)
            {
                auto& rms = th->worker->rootMoves;
                rms.swap_to_front(bookBestMove);
                if (bookPonderMove != Move::None())
                    rms.front() += bookPonderMove;
            }
        }
        else
        {
            threads.start_search();  // start non-main threads
            iterative_deepening();   // main thread start searching
        }
    }

    // When reach the maximum depth, can arrive here without a raise of threads.stop.
    // However, if in an infinite search or pondering, the UCI protocol states that
    // shouldn't print the best move before the GUI sends a "stop" or "ponderhit" command.
    // Therefore simply wait here until the GUI sends one of those commands.
    while (!threads.stop && (limit.infinite || mainManager->ponder))
    {}  // Busy wait for a stop or a mainManager->ponder reset

    // Stop the threads if not already stopped
    // (also raise the stop if "ponderhit" just reset mainManager->ponder).
    threads.stop = true;

    // Wait until all threads have finished
    threads.wait_finish();

    // When playing in 'Nodes as Time' mode,
    // subtract the searched nodes from the start nodes before exiting.
    if (mainManager->timeManager.use_nodes_time())
        mainManager->timeManager.advance_nodes(  //
          threads.nodes() - limit.clocks[rootPos.active_color()].inc);

    Worker* bestWorker = this;
    if (multiPV == 1 && threads.size() > 1 && limit.mate == 0 && !bookMovePlayed
        && rootMoves.front()[0] != Move::None() && !mainManager->skill.enabled())
        bestWorker = threads.best_thread()->worker.get();

    mainManager->first           = false;
    mainManager->preBestCurValue = bestWorker->rootMoves.front().curValue;
    mainManager->preBestAvgValue = bestWorker->rootMoves.front().avgValue;

    // Send PV info again if have a new best worker
    if (bestWorker != this)
        mainManager->show_pv(*bestWorker, bestWorker->completedDepth);

    assert(!bestWorker->rootMoves.empty() && !bestWorker->rootMoves.front().empty());
    // clang-format off
    Move bestMove   = bestWorker->rootMoves.front()[0];
    Move ponderMove = bestMove != Move::None()
                  && (bestWorker->rootMoves.front().size() >= 2 || bestWorker->ponder_move_extracted())
                    ? bestWorker->rootMoves.front()[1] : Move::None();
    // clang-format on
    mainManager->updateCxt.onUpdateMove({bestMove, ponderMove});
}

// Main iterative deepening loop. It calls search() repeatedly with increasing depth
// until the allocated thinking time has been consumed, the user stops the search,
// or the maximum search depth is reached.
void Worker::iterative_deepening() noexcept {
    // Allocate stack with extra size to allow access from (ss - 7) to (ss + 1):
    // (ss - 7) is needed for update_continuation_history(ss - 1) which accesses (ss - 6),
    // (ss + 1) is needed for initialization of cutoffCount.
    constexpr std::uint16_t STACK_OFFSET = 7;
    constexpr std::uint16_t STACK_SIZE   = MAX_PLY + STACK_OFFSET + 1;
    // clang-format off
    Stack  stack[STACK_SIZE]{};
    Stack* ss = stack + STACK_OFFSET;
    for (std::int16_t i = 0 - STACK_OFFSET; i < STACK_SIZE - STACK_OFFSET; ++i)
    {
        (ss + i)->ply = i;
        if (i < 0)
        {
            // Use as a sentinel
            (ss + i)->staticEval               = VALUE_NONE;
            (ss + i)->pieceSqHistory           = &continuationHistory[false][false][NO_PIECE][SQ_ZERO];
            (ss + i)->pieceSqCorrectionHistory = &continuationCorrectionHistory[NO_PIECE][SQ_ZERO];
        }
    }
    assert(stack[0].ply == -STACK_OFFSET && stack[STACK_SIZE - 1].ply == MAX_PLY);
    assert(ss->ply == 0);
    // clang-format on

    Color ac = rootPos.active_color();

    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    std::uint8_t  iterIdx     = 0;
    std::uint16_t researchCnt = 0;

    double timeReduction = 1.0, sumMoveChanges = 0.0;

    Value bestValue = -VALUE_INFINITE;

    Moves lastBestPV{Move::None()};
    Value lastBestCurValue = -VALUE_INFINITE;
    Value lastBestPreValue = -VALUE_INFINITE;
    Value lastBestUciValue = -VALUE_INFINITE;
    Depth lastBestDepth    = DEPTH_ZERO;

    // Iterative deepening loop until requested to stop or the target depth is reached
    while (!threads.stop && ++rootDepth < MAX_PLY
           && !(mainManager && limit.depth != DEPTH_ZERO && rootDepth > limit.depth))
    {
        // Age out PV variability metric
        if (mainManager && limit.use_time_manager())
            sumMoveChanges *= 0.50;

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (RootMove& rm : rootMoves)
            rm.preValue = rm.curValue;

        if (threads.research)
            ++researchCnt;

        fstIdx = lstIdx = 0;
        // MultiPV loop. Perform a full root search for each PV line
        for (curIdx = 0; curIdx < multiPV; ++curIdx)
        {
            if (curIdx == lstIdx)
            {
                fstIdx = lstIdx++;
                for (; lstIdx < rootMoves.size(); ++lstIdx)
                    if (rootMoves[lstIdx].tbRank != rootMoves[fstIdx].tbRank)
                        break;
            }

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = DEPTH_ZERO;

            Value avgValue = rootMoves[curIdx].avgValue != -VALUE_INFINITE  //
                             ? rootMoves[curIdx].avgValue
                             : 0;
            assert(-VALUE_INFINITE < avgValue && avgValue < +VALUE_INFINITE);

            SqrValue avgSqrValue = rootMoves[curIdx].avgSqrValue != -VALUE_INFINITE * VALUE_INFINITE
                                   ? rootMoves[curIdx].avgSqrValue
                                   : 0;

            // Reset aspiration window starting size
            int   delta = 5 + 7.2480e-5 * std::abs(avgSqrValue);
            Value alpha = std::max(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min(avgValue + delta, +VALUE_INFINITE);

            // Adjust optimism based on root move's avgValue (~4 Elo)
            optimism[ac]  = 132 * avgValue / (89 + std::abs(avgValue));
            optimism[~ac] = -optimism[ac];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, research with a bigger window until don't fail high/low anymore.
            std::uint8_t failHighCnt = 0;
            while (true)
            {
                nmpMinPly = 0;
                rootDelta = beta - alpha;
                assert(rootDelta > 0);
                // Adjust the effective depth searched, but ensure at least one
                // effective increment for every 4 researchCnt steps.
                Depth adjustedDepth =
                  std::max(rootDepth - failHighCnt - int(0.75 * (1 + researchCnt)), 1);

                bestValue = search<Root>(rootPos, ss, alpha, beta, adjustedDepth);

                // Bring the best move to the front. It is critical that sorting
                // is done with a stable algorithm because all the values but the
                // first and eventually the new best one is set to -VALUE_INFINITE
                // and want to keep the same order for all the moves except the
                // new PV that goes to the front. Note that in the case of MultiPV
                // search the already searched PV lines are preserved.
                rootMoves.sort(curIdx, lstIdx);

                // If the search has been stopped, break immediately.
                // Sorting is safe because RootMoves is still valid,
                // although it refers to the previous iteration.
                if (threads.stop)
                    break;

                // When failing high/low give some update before a re-search.
                if (mainManager && multiPV == 1 && rootDepth > 30
                    && (alpha >= bestValue || bestValue >= beta))
                    mainManager->show_pv(*this, rootDepth);

                // In case of failing low/high increase aspiration window and research,
                // otherwise exit the loop.
                if (bestValue <= alpha)
                {
                    beta  = (alpha + beta) / 2;
                    alpha = std::max(bestValue - delta, -VALUE_INFINITE);

                    failHighCnt = 0;
                    if (mainManager)
                        mainManager->ponderhitStop = false;
                }
                else if (bestValue >= beta)
                {
                    beta = std::min(bestValue + delta, +VALUE_INFINITE);

                    ++failHighCnt;
                }
                else
                    break;

                delta *= 1.33334;

                assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
            }

            // Sort the PV lines searched so far
            rootMoves.sort(fstIdx, 1 + curIdx);

            // Give some update about the PV
            if (mainManager
                && (threads.stop || 1 + curIdx == multiPV || rootDepth > 30)
                // A thread that aborted search can have mated-in/TB-loss PV and score
                // that cannot be trusted, i.e. it can be delayed or refuted if have
                // had time to fully search other root-moves. Thus, suppress this output and
                // below pick a proven score/PV for this thread (from the previous iteration).
                && !(threads.abort && rootMoves.front().uciValue <= VALUE_TB_LOSS_IN_MAX_PLY))
                mainManager->show_pv(*this, rootDepth);

            if (threads.stop)
                break;
        }

        if (!threads.stop)
            completedDepth = rootDepth;

        // Make sure not to pick an unproven mated-in score,
        // in case this worker prematurely stopped the search (aborted-search).
        if (threads.abort && lastBestPV[0] != Move::None()
            && (rootMoves.front().curValue != -VALUE_INFINITE
                && rootMoves.front().curValue <= VALUE_TB_LOSS_IN_MAX_PLY))
        {
            // Bring the last best rootmove to the front for best thread selection.
            rootMoves.move_to_front(
              [&lastBestPV = std::as_const(lastBestPV)](const RootMove& rm) noexcept -> bool {
                  return rm == lastBestPV[0];
              });
            rootMoves.front().pv       = lastBestPV;
            rootMoves.front().curValue = lastBestCurValue;
            rootMoves.front().preValue = lastBestPreValue;
            rootMoves.front().uciValue = lastBestUciValue;
        }
        else
        {
            if (rootMoves.front()[0] != lastBestPV[0])
                lastBestDepth = completedDepth;
            lastBestPV       = rootMoves.front().pv;
            lastBestCurValue = rootMoves.front().curValue;
            lastBestPreValue = rootMoves.front().preValue;
            lastBestUciValue = rootMoves.front().uciValue;
        }

        if (!mainManager)
            continue;

        // Have found a "mate in x"?
        if (limit.mate != 0 && rootMoves.front().curValue == rootMoves.front().uciValue
            && ((rootMoves.front().curValue != +VALUE_INFINITE
                 && rootMoves.front().curValue >= VALUE_MATES_IN_MAX_PLY
                 && VALUE_MATE - rootMoves.front().curValue <= 2 * limit.mate)
                || (rootMoves.front().curValue != -VALUE_INFINITE
                    && rootMoves.front().curValue <= VALUE_MATED_IN_MAX_PLY
                    && VALUE_MATE + rootMoves.front().curValue <= 2 * limit.mate)))
            threads.stop = true;

        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (mainManager->skill.enabled() && mainManager->skill.time_to_pick(rootDepth))
            mainManager->skill.pick_best_move(rootMoves, multiPV);

        // Do have time for the next iteration? Can stop searching now?
        if (limit.use_time_manager() && !threads.stop && !mainManager->ponderhitStop)
        {
            // Use part of the gained time from a previous stable move for the current move
            sumMoveChanges += threads.move_changes();
            threads.set_move_changes(0);

            TimePoint optimumTime = mainManager->timeManager.optimum();
            TimePoint elapsedTime = mainManager->elapsed(threads);

            bool moveSingle = rootMoves.size() == 1;
            // clang-format off
            if (elapsedTime > (0.365 - 0.161 * moveSingle) * optimumTime)
            {
            double evalChange =
              std::clamp((11.0
                         + 2.0 * (mainManager->preBestAvgValue        - bestValue)
                         + 1.0 * (mainManager->iterBestValue[iterIdx] - bestValue)) / 100.0,
                         1.0 - 0.420 * !mainManager->first, 1.0 + 0.667 * !mainManager->first);
            timeReduction        = 0.687 + 0.808 * (completedDepth > 8 + lastBestDepth);
            double reduction     = 0.4608295 * (1.48 + mainManager->preTimeReduction) / timeReduction;
            double instability   = 1.0 + 1.88 * sumMoveChanges / threads.size();
            double nodeReduction = 1.0 - 3.262e-3 * (completedDepth > 10)
                                 * std::max<int>(-920 + 1000 * rootMoves.front().nodes / nodes, 0);
            double reCapture     = 1.01 - 95e-3 * (rootPos.cap_square() == rootMoves.front()[0].dst_sq()
                                                && rootPos.pieces(~ac) & rootPos.cap_square());

            double totalTime = optimumTime * evalChange * reduction * instability * nodeReduction * reCapture;

            // Cap totalTime in case of a single legal move for a better viewer experience
            if (moveSingle)
                totalTime = std::min(0.5 * totalTime, 500.0);

            // Stop the search if have exceeded the totalTime
            if (elapsedTime > totalTime)
            {
                // If allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainManager->ponder)
                    mainManager->ponderhitStop = true;
                else
                {
                    threads.stop = true;
                    break;
                }
            }

            threads.research = elapsedTime > 0.506 * totalTime && !mainManager->ponder;
            }
            // clang-format on
        }

        mainManager->iterBestValue[iterIdx] = bestValue;

        iterIdx = (1 + iterIdx) % mainManager->iterBestValue.size();
    }

    if (!mainManager)
        return;

    mainManager->preTimeReduction = timeReduction;

    // If the skill level is enabled, swap the best PV line with the sub-optimal one
    if (mainManager->skill.enabled())
        rootMoves.swap_to_front(mainManager->skill.pick_best_move(rootMoves, multiPV, false));
}

// Main search function for different type of nodes.
template<NodeType NT>
// clang-format off
Value Worker::search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, Move excludedMove) noexcept {
    // clang-format on
    constexpr bool RootNode = NT == Root;
    constexpr bool PVNode   = NT == PV || RootNode;
    constexpr bool CutNode  = NT == Cut;  // !PVNode
    constexpr bool AllNode  = NT == All;  // !PVNode
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || 1 + alpha == beta);
    assert((RootNode && ss->ply == 0) || ss->ply > 0);
    assert(!RootNode || (DEPTH_ZERO < depth && depth < MAX_PLY));

    Key key = pos.key();

    if constexpr (!RootNode)
    {
        // Dive into quiescence search when depth <= DEPTH_ZERO
        if (depth <= DEPTH_ZERO)
            return qsearch<PVNode>(pos, ss, alpha, beta);

        // Check if have an upcoming move that draws by repetition (~1 Elo)
        if (alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
        {
            alpha = draw_value(key, nodes);
            if (alpha >= beta)
                return alpha;
        }

        // Limit the depth if extensions made it too large
        depth = std::min(+depth, MAX_PLY - 1);
        assert(DEPTH_ZERO < depth && depth < MAX_PLY);
    }

    Color ac = pos.active_color();

    // Check for the available remaining time
    if (is_main_worker())
        main_manager()->check_time(*this);

    if constexpr (PVNode)
    {
        // Update selDepth (selDepth from 1, ply from 0)
        selDepth = std::max(+selDepth, 1 + ss->ply);
    }

    // Step 1. Initialize node
    ss->moveCount = 0;
    ss->history   = 0;
    ss->inCheck   = pos.checkers();

    if constexpr (!RootNode)
    {
        // Step 2. Check for stopped search and immediate draw
        if (threads.stop.load(std::memory_order_relaxed)  //
            || ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
            return ss->ply >= MAX_PLY && !ss->inCheck
                   ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[ac])
                   : draw_value(key, nodes);

        // Step 3. Mate distance pruning. Even if mate at the next move score would
        // be at best mates_in(1 + ss->ply), but if alpha is already bigger because
        // a shorter mate was found upward in the tree then there is no need to search
        // because will never beat the current alpha. Same logic but with a reversed
        // signs apply also in the opposite condition of being mated instead of giving
        // mate. In this case, return a fail-high score.
        alpha = std::max(mated_in(0 + ss->ply), alpha);
        beta  = std::min(mates_in(1 + ss->ply), beta);
        if (alpha >= beta)
            return alpha;
    }

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    (ss + 1)->cutoffCount = 0;
    (ss + 1)->move        = Move::None();

    Move preMove    = (ss - 1)->move;
    auto preSq      = preMove.is_ok() ? preMove.dst_sq() : SQ_NONE;
    bool preQuiet   = preSq != SQ_NONE && pos.captured_piece() == NO_PIECE;
    bool preNonPawn = preSq != SQ_NONE && type_of(pos.piece_on(preSq)) != PAWN  //
                   && preMove.type_of() != PROMOTION;

    bool exclude = excludedMove != Move::None();

    // Step 4. Transposition table lookup
    Key16 key16 = compress_key(key);

    auto [ttHit, tte, fte] = tt.probe(key, key16);
    TTEntry const ttd(*tte);
    TTUpdater     ttu(tte, fte, key16, ss->ply, tt.generation());

    Value ttValue  = ttHit ? value_from_tt(ttd.value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move  ttMove   = RootNode ? rootMoves[curIdx][0] : extract_tt_move(pos, ttd.move());
    ss->ttM        = ttMove != Move::None();
    bool ttCapture = ss->ttM && pos.capture_promo(ttMove);

    // At this point, if excludedMove, skip straight to step 6, static evaluation.
    // However, to save indentation, list the condition in all code between here and there.
    if (!exclude)
        ss->ttPv = PVNode || (ttHit && ttd.pv());

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && !exclude && ttValue != VALUE_NONE && ttd.depth() > depth - (ttValue <= beta)
        && (ttd.bound() & bound_for_tt(ttValue >= beta)))
    {
        // If ttMove fails high, update move sorting heuristics on TT hit (~2 Elo)
        if (ss->ttM && ttValue >= beta)
        {
            // Bonus for a quiet ttMove (~2 Elo)
            if (!ttCapture)
                update_quiet_history(pos, ss, ttMove, +stat_bonus(depth));

            // Extra penalty for early quiet moves of the previous ply (~1 Elo on STC, ~2 Elo on LTC)
            if (preQuiet && (ss - 1)->moveCount <= 2)
                update_continuation_history(ss - 1, pos.prev_ex_moved_piece(preMove), preSq,
                                            -stat_malus(depth + 1));
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < Position::rule50_threshold()
            && (!pos.rule50_high() || pos.rule50_count() < 10))
        {
            if (ttValue > beta && ttd.depth() > 0 && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY)
                ttValue = (ttd.depth() * ttValue + beta) / (ttd.depth() + 1);

            return ttValue;
        }
    }

    Value value, bestValue = -VALUE_INFINITE;

    [[maybe_unused]] Value maxValue = +VALUE_INFINITE;

    // Step 5. Tablebases probe
    if (!RootNode && !exclude && tbConfig.cardinality != 0)
    {
        auto pieceCount = pos.count<ALL_PIECE>();

        if (pieceCount <= tbConfig.cardinality
            && (pieceCount < tbConfig.cardinality || depth >= tbConfig.probeDepth)
            && pos.rule50_count() == 0 && !pos.can_castle(ANY_CASTLING))
        {
            Tablebases::ProbeState wdlPs;
            Tablebases::WDLScore   wdl;
            wdl = Tablebases::probe_wdl(pos, &wdlPs);

            // Force check of time on the next occasion
            if (is_main_worker())
                main_manager()->callsCount = 1;

            if (wdlPs != Tablebases::FAIL)
            {
                ++tbHits;

                std::int8_t drawValue = 1 * tbConfig.useRule50;

                Value tbValue = VALUE_TB - ss->ply;

                // Use the range VALUE_TB to VALUE_TB_WIN_IN_MAX_PLY to value
                value = wdl < -drawValue ? -tbValue
                      : wdl > +drawValue ? +tbValue
                                         : 2 * wdl * drawValue;

                Bound bound = wdl < -drawValue ? BOUND_UPPER
                            : wdl > +drawValue ? BOUND_LOWER
                                               : BOUND_EXACT;

                if (bound == BOUND_EXACT || bound == bound_for_tt(value >= beta, value <= alpha))
                {
                    ttu.update(std::min(depth + 6, MAX_PLY - 1), ss->ttPv, bound, Move::None(),
                               value, VALUE_NONE);
                    return value;
                }

                if constexpr (PVNode)
                {
                    if (bound == BOUND_LOWER)
                        bestValue = value, alpha = std::max(alpha, value);
                    else
                        maxValue = value;
                }
            }
        }
    }

    Value unadjustedStaticEval, eval, probCutBeta;

    bool improve, recover;

    Move move;

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    // Step 6. Static evaluation of the position
    if (ss->inCheck)
    {
        unadjustedStaticEval = VALUE_NONE;

        Value pieceValue = -(PIECE_VALUE[type_of(pos.captured_piece())]
                             + PIECE_VALUE[type_of(pos.promoted_piece())]);
        if (pos.state()->preState)
            pieceValue += +(PIECE_VALUE[type_of(pos.state()->preState->capturedPiece)]
                            + PIECE_VALUE[type_of(pos.state()->preState->promotedPiece)]);

        eval = ss->staticEval = (ss - 2)->staticEval + pieceValue;

        improve = recover = false;
        // Skip early pruning when in check
        goto MAIN_MOVES_LOOP;
    }

    if (exclude)
    {
        unadjustedStaticEval = eval = ss->staticEval;
        // Providing the hint that this node's accumulator will often be used
        // brings significant Elo gain (~13 Elo).
        NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);
    }
    else if (ttHit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = ttd.eval();
        if (unadjustedStaticEval == VALUE_NONE)
            unadjustedStaticEval =
              evaluate(pos, networks[numaAccessToken], accCaches, optimism[ac]);
        else if constexpr (PVNode)
            NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);

        eval = ss->staticEval = adjust_static_eval(unadjustedStaticEval, pos, ss);

        // Can ttValue be used as a better position evaluation (~7 Elo)
        if (ttValue != VALUE_NONE && (ttd.bound() & bound_for_tt(ttValue > eval)))
            eval = ttValue;
    }
    else
    {
        unadjustedStaticEval = evaluate(pos, networks[numaAccessToken], accCaches, optimism[ac]);

        eval = ss->staticEval = adjust_static_eval(unadjustedStaticEval, pos, ss);

        // Static evaluation is saved as it was before adjustment by correction history
        ttu.update(DEPTH_NONE, ss->ttPv, BOUND_NONE, Move::None(), VALUE_NONE,
                   unadjustedStaticEval);
    }

    // Use static evaluation difference to improve quiet move ordering (~9 Elo)
    if (preQuiet && !(ss - 1)->inCheck)
    {
        // clang-format off
        int bonus = 760 + std::clamp(-10 * ((ss - 0)->staticEval + (ss - 1)->staticEval), -1641, +1423);

        update_main_history(~ac, preMove, bonus);
        if (preNonPawn)
            update_pawn_history(pos, pos.prev_ex_moved_piece(preMove), preSq, bonus / 2);
        // clang-format on
    }

    // Set up the improve flag, which is true if the current static evaluation
    // is bigger than the previous static evaluation at our turn
    // (if in check at previous move go back until not in check).
    // The improve flag is used in various pruning heuristics.
    improve = (ss - 2)->staticEval != VALUE_NONE && ss->staticEval > 0 + (ss - 2)->staticEval;
    recover = (ss - 1)->staticEval != VALUE_NONE && ss->staticEval > 2 - (ss - 1)->staticEval;

    // Step 7. Razoring (~1 Elo)
    // If eval is really low check with qsearch if it can exceed alpha,
    // if it can't, return a fail low.
    if (eval < -510 + alpha - 272 * sqr(depth))
    {
        value = qsearch<false>(pos, ss, alpha - 1, alpha);
        if (value < alpha)
            return std::max(+value, VALUE_TB_LOSS_IN_MAX_PLY + 1);
    }

    // Step 8. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 13 && eval >= beta && (!ss->ttM || ttCapture)
        && eval < VALUE_TB_WIN_IN_MAX_PLY && beta > VALUE_TB_LOSS_IN_MAX_PLY
        && eval - futility_margin(depth, CutNode, ttHit, improve, recover) - (ss - 1)->history / 272
             >= beta)
        return (2 * beta + eval) / 3;

    improve |= ss->staticEval >= 100 + beta;

    // Step 9. Null move search with verification search (~35 Elo)
    if (CutNode && !exclude && preMove != Move::Null() && ss->move != Move::Null()  //
        && eval >= beta && beta > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(ac)
        && ss->staticEval >= 400 + beta - 23 * depth && ss->ply >= nmpMinPly)
    {
        int diff = eval - beta;
        assert(diff >= 0);

        // Null move dynamic reduction based on depth, eval and phase
        Depth R = 4 + depth / 3 + std::min(diff / 209, 6) + pos.phase() / 9;

        tt.prefetch_entry(pos.move_key());
        // clang-format off
        ss->move                     = Move::Null();
        ss->pieceSqHistory           = &continuationHistory[false][false][NO_PIECE][SQ_ZERO];
        ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[NO_PIECE][SQ_ZERO];
        // clang-format on
        pos.do_null_move(st);

        Value nullValue = -search<All>(pos, ss + 1, -beta, -beta + 1, depth - R);

        pos.undo_null_move();

        // Do not return unproven mate or TB scores
        nullValue = std::min(+nullValue, std::max(+beta, VALUE_TB_WIN_IN_MAX_PLY - 1));

        if (nullValue >= beta)
        {
            if (nmpMinPly != 0 || depth < 16)
                return nullValue;

            assert(nmpMinPly == 0);  // Recursive verification is not allowed

            // Do verification search at high depths,
            // with null move pruning disabled until ply exceeds nmpMinPly.
            nmpMinPly = ss->ply + 3 * (depth - R) / 4;

            Value v = search<Cut>(pos, ss, beta - 1, beta, depth - R);

            nmpMinPly = 0;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 10. Internal iterative reductions (~9 Elo)
    // Decrease depth for PVNode without a ttMove.
    if (PVNode && !ss->ttM)
    {
        depth -= 3;
        // Use qsearch if depth <= DEPTH_ZERO
        if (depth <= DEPTH_ZERO)
            return qsearch<true>(pos, ss, alpha, beta);
    }
    assert(depth > DEPTH_ZERO);

    // Decrease depth for CutNode,
    // If the depth is high enough and without a ttMove or an upper bound.
    if (CutNode && depth > 6 && (!ss->ttM || (ttHit && ttd.bound() == BOUND_UPPER)))
        depth -= 1 + !ss->ttM;

    // Step 11. ProbCut (~10 Elo)
    // If have a good enough capture or any promotion and a reduced search
    // returns a value much above beta, can (almost) safely prune previous move.
    probCutBeta = std::min(189 + beta - 53 * improve - 30 * recover, +VALUE_INFINITE - 1);
    if (!PVNode && depth > 3
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
        // If value from transposition table is lower than probCutBeta, don't attempt probCut
        // there and in further interactions with transposition table cutoff depth is set to
        // depth - 3 because probCut search has depth set to depth - 4 but also did a move before it
        // So effective depth is equal to depth - 3.
        && !(ttValue != VALUE_NONE && ttValue < probCutBeta && ttd.depth() >= depth - 3))
    {
        assert(beta < probCutBeta && probCutBeta < +VALUE_INFINITE);

        Depth probCutDepth = depth - 3;

        Value probCutThreshold = probCutBeta - ss->staticEval;

        MovePicker mp(pos, ttMove, &captureHistory, probCutThreshold);
        // Loop through all pseudo-legal moves
        while ((move = mp.next_move()) != Move::None())
        {
            assert(move.is_ok() && pos.pseudo_legal(move));
            assert(pos.capture_promo(move));
            assert(!ss->inCheck);

            // Check for legality
            if ((exclude && move == excludedMove) || (move != ttMove && !pos.legal(move)))
                continue;

            // Speculative prefetch as early as possible
            tt.prefetch_entry(pos.move_key(move));
            // clang-format off
            ss->move                     = move;
            ss->pieceSqHistory           = &continuationHistory[false][true][pos.moved_piece(move)][move.dst_sq()];
            ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[pos.moved_piece(move)][move.dst_sq()];
            // clang-format on
            ++nodes;
            pos.do_move(move, st);

            // Perform a preliminary qsearch to verify that the move holds
            value = -qsearch<false>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (value >= probCutBeta && probCutDepth > 1)
                value = -search<~NT>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, probCutDepth - 1);

            pos.undo_move(move);

            assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

            // Finished searching the move. If a stop occurred, the return value of
            // the search cannot be trusted, and return immediately without updating.
            if (threads.stop.load(std::memory_order_relaxed))
                return VALUE_ZERO;

            if (value >= probCutBeta)
            {
                update_capture_history(pos, move, +stat_bonus(probCutDepth));

                // Save ProbCut data into transposition table
                ttu.update(probCutDepth, ss->ttPv, BOUND_LOWER, move, value, unadjustedStaticEval);

                return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probCutBeta - beta)
                                                                 : value;
            }
        }

        NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);
    }

MAIN_MOVES_LOOP:  // When in check, search starts here

    // Step 12. A small ProbCut idea (~4 Elo)
    probCutBeta = std::min(379 + beta, VALUE_TB_WIN_IN_MAX_PLY - 1);
    if (!exclude && ttValue >= probCutBeta  //
        && ttd.depth() >= depth - 4 && (ttd.bound() & BOUND_LOWER)
        && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY  //
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY)
        return probCutBeta;

    if (!exclude && !ss->inCheck && !ss->ttM && tte->move() != Move::None())
    {
        ttMove    = extract_tt_move(pos, tte->move());
        ss->ttM   = ttMove != Move::None();
        ttCapture = ss->ttM && pos.capture_promo(ttMove);
    }

    value = bestValue;

    Move bestMove = Move::None();

    std::uint8_t moveCount = 0;

    std::array<Moves, 2> moves;

    Value singularValue   = +VALUE_INFINITE;
    bool  singularFailLow = false;

    const History<HPieceSq>* pieceSqHistory[6]{(ss - 1)->pieceSqHistory,
                                               (ss - 2)->pieceSqHistory,
                                               (ss - 3)->pieceSqHistory,
                                               (ss - 4)->pieceSqHistory,
                                               nullptr,
                                               (ss - 6)->pieceSqHistory};

    Value quietThreshold = std::max(200 - 3600 * depth, -7997);

    MovePicker mp(pos, ttMove, &mainHistory, &captureHistory, &pawnHistory, pieceSqHistory,
                  &lowPlyHistory, ss->ply, quietThreshold);
    mp.pickQuiets = true;
    // Step 13. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None())
    {
        assert(move.is_ok() && pos.pseudo_legal(move));

        // Check for legality
        if (
          (exclude && move == excludedMove)
          // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
          // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
          || (RootNode ? !rootMoves.contains(curIdx, lstIdx, move)
                       : move != ttMove && !pos.legal(move)))
            continue;

        ss->moveCount = ++moveCount;

        if (RootNode && is_main_worker() && rootDepth > 30 && !options["ReportMinimal"])
            main_manager()->updateCxt.onUpdateIter({rootDepth, move, uint16_t(moveCount + curIdx)});

        if constexpr (PVNode)
            (ss + 1)->pv.clear();

        Square dst          = move.dst_sq();
        Piece  movedPiece   = pos.moved_piece(move);
        Piece  exMovedPiece = pos.ex_moved_piece(move);

        bool check   = pos.check(move);
        bool capture = pos.capture_promo(move);

        // Calculate new depth for this move
        Depth newDepth = depth - 1;

        int deltaRatio = 795 * (beta - alpha) / rootDelta;

        auto r = reduction(depth, moveCount, deltaRatio, improve, recover);

        // Step 14. Pruning at shallow depth (~120 Elo).
        // Depth conditions are important for mate finding.
        if (!RootNode && bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(ac))
        {
            // Skip quiet moves if moveCount exceeds our Futility Move Count threshold (~8 Elo)
            // clang-format off
            mp.pickQuiets = mp.pickQuiets
                         && moveCount < ((3 + sqr(depth)) >> (1 - improve))
                                      - (!improve && singularFailLow && singularValue < -80 + alpha);
            // clang-format on

            // Reduced depth of the next LMR search
            Depth lmrDepth = newDepth - r / 1024;

            if (capture || check)
            {
                auto captured = pos.ex_captured(move);
                assert(captured != KING);

                int capHist = captureHistory[movedPiece][dst][captured];

                // Futility pruning for captures (~2 Elo)
                if (!check && lmrDepth < 7 && !ss->inCheck)
                {
                    // clang-format off
                    Value futilityValue = 
                      std::min(300 + ss->staticEval + PIECE_VALUE[captured]
                                   + capHist / 7 + 238 * lmrDepth
                                   , VALUE_TB_WIN_IN_MAX_PLY - 1);
                    // clang-format on
                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures and checks (~11 Elo)
                Value seeThreshold =
                  -std::clamp(capHist / 32, -159 * depth, +160 * depth) - 167 * depth;

                if (pos.see(move) < seeThreshold)
                    continue;
            }
            else
            {
                int conHist = (*pieceSqHistory[0])[exMovedPiece][dst]  //
                            + (*pieceSqHistory[1])[exMovedPiece][dst]  //
                            + pawnHistory[pawn_index(pos.pawn_key())][exMovedPiece][dst];

                // Continuation history based pruning (~2 Elo)
                if (conHist < -4071 * depth)
                    continue;

                conHist += 2 * mainHistory[ac][move.org_dst()];

                lmrDepth += conHist / 3653;

                // Futility pruning: parent node (~13 Elo)
                if (lmrDepth < 12 && !ss->inCheck)
                {
                    // clang-format off
                    Value futilityValue =
                      std::min(49 + ss->staticEval + 144 * lmrDepth
                                  + std::clamp(-51 + ss->staticEval - bestValue, 0, 96)
                                  , VALUE_TB_WIN_IN_MAX_PLY - 1);
                    // clang-format on
                    if (futilityValue <= alpha)
                        continue;
                }

                lmrDepth = std::max(lmrDepth, DEPTH_ZERO);

                // Prune moves with negative SEE (~4 Elo)
                Value seeThreshold =
                  -std::min(24 * int(sqr(lmrDepth)), VALUE_TB_WIN_IN_MAX_PLY - 1);

                if (pos.see(move) < seeThreshold)
                    continue;
            }
        }

        // Step 15. Extensions (~100 Elo)
        // Take care to not overdo to avoid search getting stuck.
        Depth extension = DEPTH_ZERO;

        if (ss->ply < 2 * rootDepth)
        {
            // Singular extension search (~76 Elo, 170 Elo). If all moves but one fail low
            // on a search of (alpha-s, beta-s), and just one fails high on (alpha, beta),
            // then that move is singular and should be extended. To verify this do a
            // reduced search on the position excluding the ttMove and if the result
            // is lower than ttValue minus a margin, then will extend the ttMove.
            // Recursive singular search is avoided.

            // Note:
            // Depth margin and singularBeta margin are known for having non-linear scaling.
            // Their values are optimized to time controls of 180+1.8 and longer
            // so changing them requires tests at these types of time controls.
            // Generally, higher singularBeta (i.e closer to ttValue) and
            // lower extension margins scales well.
            if (!RootNode && !exclude && ss->ttM && move == ttMove
                && depth > 3 - (completedDepth > 38) + ss->ttPv  //
                && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY   //
                && ttd.depth() >= depth - 3 && (ttd.bound() & BOUND_LOWER))
            {
                // clang-format off
                Value singularBeta  = std::max(ttValue - (54 + 77 * (!PVNode && ss->ttPv)) * depth / 64, -VALUE_INFINITE + 1);
                Depth singularDepth = newDepth / 2;
                assert(singularDepth > DEPTH_ZERO);

                value = search<~~NT>(pos, ss, singularBeta - 1, singularBeta, singularDepth, move);

                singularValue   = value;
                singularFailLow = value < singularBeta;

                if (singularFailLow)
                {
                    Value doubleMargin =  0 + 262 * PVNode - 204 * !ttCapture;
                    Value tripleMargin = 97 + 266 * PVNode - 255 * !ttCapture + 94 * ss->ttPv;

                    extension = 1 + (value < singularBeta - doubleMargin)
                                  + (value < singularBeta - tripleMargin);

                    depth += (!PVNode && depth < 14);
                }
                // clang-format on
                // Multi-cut pruning
                // If the ttMove is assumed to fail high based on the bound of the TT entry, and
                // if after excluding the ttMove with a reduced search fail high over the original beta,
                // assume this expected cut-node is not singular (multiple moves fail high),
                // and can prune the whole subtree by returning a soft-bound.
                else if (singularValue = std::min(+singularValue, VALUE_TB_WIN_IN_MAX_PLY - 1);
                         singularValue >= beta)
                    return singularValue;

                // Negative extensions
                // If other moves failed high over (ttValue - margin) without the ttMove on a reduced search,
                // but cannot do multi-cut because (ttValue - margin) is lower than the original beta,
                // do not know if the ttMove is singular or can do a multi-cut,
                // so reduce the ttMove in favor of other moves based on some conditions:

                // If the ttMove is assumed to fail high over current beta (~7 Elo)
                else if (ttValue >= beta)
                    extension = -3;

                // If on a CutNode but the ttMove is not assumed to fail high over current beta (~1 Elo)
                else if constexpr (CutNode)
                    extension = -2 + (depth > 30);

                // If the ttMove is assumed to fail low over the value of the reduced search (~1 Elo)
                else if (ttValue <= value - 30)
                    extension = -1;
            }

            // Recapture extension (~0 Elo on STC, ~1 Elo on LTC)
            else if (PVNode && capture && dst == preSq
                     && captureHistory[movedPiece][dst][type_of(pos.piece_on(dst))] > 4299)
                extension = 1;

            // Check extension (~1 Elo)
            else if (check && depth > 12 && pos.rule50_count() < 8 && pos.see(move) >= 0)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;

        // Speculative prefetch as early as possible
        tt.prefetch_entry(pos.move_key(move));

        // Update the move (this must be done after singular extension search)
        // clang-format off
        ss->move                     = move;
        ss->pieceSqHistory           = &continuationHistory[ss->inCheck][capture][exMovedPiece][dst];
        ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[exMovedPiece][dst];
        // clang-format on
        [[maybe_unused]] std::uint64_t preNodes;
        if constexpr (RootNode)
            preNodes = nodes;

        // Step 16. Make the move
        ++nodes;
        pos.do_move(move, st, check);

        // These reduction adjustments have proven non-linear scaling.
        // They are optimized to time controls of 180 + 1.8 and longer
        // so changing them or adding conditions that are similar
        // requires tests at these types of time controls.

        // Decrease reduction if position is or has been on the PV (~7 Elo)
        r -= ss->ttPv
           * (1024 + 1024 * (ttValue != VALUE_NONE && ttValue > alpha)
              + 1024 * (ttHit && ttd.depth() >= depth));

        // Decrease reduction for PvNodes (~0 Elo on STC, ~2 Elo on LTC)
        r -= 1024 * PVNode;

        // These reduction adjustments have no proven non-linear scaling.

        // Increase reduction for cut nodes (~4 Elo)
        r += CutNode * (2518 - 991 * (ttHit && ss->ttPv && ttd.depth() >= depth));

        // Increase reduction if ttMove is a capture but the current move is not a capture (~3 Elo)
        r += (ttCapture && !capture) * (1043 + 999 * (depth < 8));

        // Increase reduction on repetition (~1 Elo)
        r += 1024 * ss->ply >= 4 && move == (ss - 4)->move && pos.has_repeated();

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if (ss->cutoffCount > 3)
            r += 938 + 960 * AllNode;

        // Decrease reduction for first picked move (ttMove) (~3 Elo)
        else
            r -= 1879 * (ss->ttM && move == ttMove);

        ss->history = 2 * mainHistory[ac][move.org_dst()]      //
                    + (*pieceSqHistory[0])[exMovedPiece][dst]  //
                    + (*pieceSqHistory[1])[exMovedPiece][dst] - 4410;

        // Decrease/increase reduction for moves with a good/bad history (~8 Elo)
        r -= 1287 * ss->history / 0x4000;

        // Step 17. Late moves reduction / extension (LMR) (~117 Elo)
        if (depth > 1 && moveCount > 1)
        {
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth lmrDepth = std::max(std::min(newDepth - r / 1024, newDepth + !AllNode), 1);

            value = -search<Cut>(pos, ss + 1, -(alpha + 1), -alpha, lmrDepth);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && newDepth > lmrDepth)
            {
                // Adjust full-depth search based on LMR value
                newDepth +=
                  // - if the value was good enough search deeper. (~1 Elo)
                  +(value > 38 + bestValue + 2 * newDepth)
                  // - if the value was bad enough search shallower. (~2 Elo)
                  - (value < 8 + bestValue);

                if (newDepth > lmrDepth)
                    value = -search<~NT>(pos, ss + 1, -(alpha + 1), -alpha, newDepth);

                if (value >= beta)
                {
                    // Post LMR continuation history updates (~1 Elo)
                    int bonus = 2 * stat_bonus(newDepth);

                    update_continuation_history(ss, exMovedPiece, dst, bonus);
                }
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Reduce search depth
            // - if expected reduction is high. (~9 Elo)
            // - if expected reduction and no ttMove. (~6 Elo)
            value = -search<~NT>(pos, ss + 1, -(alpha + 1), -alpha,
                                 newDepth - ((r + 2037 * !ss->ttM) > 2983));
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if (PVNode && (moveCount == 1 || value > alpha))
        {
            (ss + 1)->pv.clear();

            // Extend move from transposition table
            newDepth += newDepth == DEPTH_ZERO && move == ttMove;

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth);
        }

        // Step 19. Undo move
        pos.undo_move(move);

        assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and return immediately without updating
        // best move, principal variation and transposition table.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if constexpr (RootNode)
        {
            RootMove& rm = *rootMoves.find(move);

            rm.nodes += nodes - preNodes;
            // clang-format off
            rm.avgValue    = rm.avgValue    != -VALUE_INFINITE                  ? (value                   + rm.avgValue)    / 2 : value;
            rm.avgSqrValue = rm.avgSqrValue != -VALUE_INFINITE * VALUE_INFINITE ? (value * std::abs(value) + rm.avgSqrValue) / 2 : value * std::abs(value);
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

                rm.resize(1);
                rm.append((ss + 1)->pv);

                // Record how often the best move has been changed in each iteration.
                // This information is used for time management.
                // In MultiPV mode, must take care to only do this for the first PV line.
                if (curIdx == 0 && moveCount > 1 && limit.use_time_manager())
                    ++moveChanges;
            }
            else
                // All other moves but the PV, are set to the lowest value, this
                // is not a problem when sorting because the sort is stable and the
                // move position in the list is preserved - just the PV is pushed up.
                rm.curValue = -VALUE_INFINITE;
        }

        // In case have an alternative move equal in eval to the current bestmove,
        // promote it to bestmove by pretending it just exceeds alpha (but not beta).
        bool inc = value == bestValue && 1 + value < VALUE_TB_WIN_IN_MAX_PLY  //
                && (nodes & 0xF) == 0 && 2 + ss->ply >= rootDepth;

        if (bestValue < value + inc)
        {
            bestValue = value;

            if (alpha < value + inc)
            {
                bestMove = move;

                if constexpr (PVNode && !RootNode)  // Update pv even in fail-high case
                    update_pv(ss, move);

                if (value >= beta)
                {
                    if constexpr (!RootNode)
                        (ss - 1)->cutoffCount += !ss->ttM + (extension < 2);

                    break;  // Fail-high
                }

                alpha = value;  // Update alpha! Always alpha < beta

                // Reduce other moves if have found at least one score improvement (~2 Elo)
                if (depth < 15 && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
                    depth = std::max(depth - 2, 1);

                assert(depth > DEPTH_ZERO);
            }
        }

        // Collection of worse moves
        if (move != bestMove)
            moves[capture] += move;
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves,
    // it must be a mate or a stalemate.
    // If in a singular extension search then return a fail low score.
    assert(moveCount != 0 || !ss->inCheck || exclude || LegalMoveList(pos).empty());

    // Adjust best value for fail high cases at non-pv nodes
    if (!PVNode && bestValue > beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (depth * bestValue + beta) / (depth + 1);

    if (moveCount == 0)
        bestValue = exclude ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha update the history of searched moves
    else if (bestMove != Move::None())
        update_history(pos, ss, depth, bestMove, moves);

    // Bonus for prior quiet move that caused the fail low
    else if (preQuiet)
    {
        // clang-format off
        int bonusMul = std::max(
                     + 118 * (depth > 5) + 38 * !AllNode + 169 * ((ss - 1)->moveCount > 8)
                     + 116 * (!(ss - 0)->inCheck && bestValue <= +(ss - 0)->staticEval - 101)
                     + 133 * (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->staticEval - 92)
                     // proportional to "how much damage have to undo"
                     + std::min(-(ss - 1)->history / 102, 305), 0);

        int bonus = bonusMul * stat_bonus(depth);

        update_main_history(~ac, preMove, bonus / 174);
        update_continuation_history(ss - 1, pos.prev_ex_moved_piece(preMove), preSq, bonus / 107);
        if (preNonPawn)
            update_pawn_history(pos, pos.prev_ex_moved_piece(preMove), preSq, bonus / 25);
        // clang-format on
    }

    // Bonus for TT move when search fails low
    else if (!AllNode && ss->ttM)
        update_main_history(ac, ttMove, stat_bonus(depth) / 4);

    if constexpr (PVNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~7 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    // Static evaluation is saved as it was before correction history
    if ((!RootNode || curIdx == 0) && !exclude)
    {
        Bound bound = bestValue >= beta                  ? BOUND_LOWER
                    : PVNode && bestMove != Move::None() ? BOUND_EXACT
                                                         : BOUND_UPPER;
        ttu.update(depth, ss->ttPv, bound, bestMove, bestValue, unadjustedStaticEval);
    }

    // Adjust correction history
    if (!ss->inCheck  //
        && !(bestMove != Move::None() && pos.capture(bestMove))
        && !(bestValue >= beta && bestValue <= ss->staticEval)
        && !(bestMove == Move::None() && bestValue >= ss->staticEval))
    {
        int bonus = (bestValue - ss->staticEval) * depth / 8;
        update_correction_history(pos, ss, bonus);
    }

    assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);

    return bestValue;
}

// Quiescence search function, which is called by the main search function,
// should be using static evaluation only, but tactical moves may confuse the static evaluation.
// To fight this horizon effect, implemented this qsearch of tactical moves only. (~155 Elo)
template<bool PVNode>
Value Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) noexcept {
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || 1 + alpha == beta);

    Key key = pos.key();

    // Check if have an upcoming move that draws by repetition (~1 Elo)
    if (alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
    {
        alpha = draw_value(key, nodes);
        if (alpha >= beta)
            return alpha;
    }

    Color ac = pos.active_color();

    // Step 1. Initialize node
    if constexpr (PVNode)
    {
        // Update selDepth (selDepth from 1, ply from 0)
        selDepth = std::max(+selDepth, 1 + ss->ply);
        (ss + 1)->pv.clear();
    }

    ss->inCheck = pos.checkers();

    if (is_main_worker() && main_manager()->callsCount > 1)
        main_manager()->callsCount--;

    // Step 2. Check for an immediate draw or maximum ply reached
    if (ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
        return ss->ply >= MAX_PLY && !ss->inCheck
               ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[ac])
               : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Step 3. Transposition table lookup
    Key16 key16 = compress_key(key);

    auto [ttHit, tte, fte] = tt.probe(key, key16);
    TTEntry const ttd(*tte);
    TTUpdater     ttu(tte, fte, key16, ss->ply, tt.generation());

    Value ttValue = ttHit ? value_from_tt(ttd.value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move  ttMove  = extract_tt_move(pos, ttd.move());
    bool  ttPv    = ttHit && ttd.pv();

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && ttValue != VALUE_NONE && ttd.depth() >= DEPTH_ZERO
        && (ttd.bound() & bound_for_tt(ttValue >= beta))
        // For high rule50 counts don't produce transposition table cutoffs.
        && pos.rule50_count() < Position::rule50_threshold()
        && (!pos.rule50_high() || pos.rule50_count() < 10))
    {
        if (ttValue > beta && ttd.depth() > 0 && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY)
            ttValue = (ttd.depth() * ttValue + beta) / (ttd.depth() + 1);

        return ttValue;
    }

    Move preMove = (ss - 1)->move;
    auto preSq   = preMove.is_ok() ? preMove.dst_sq() : SQ_NONE;

    Value unadjustedStaticEval, bestValue, futilityBase;

    // Step 4. Static evaluation of the position
    if (ss->inCheck)
    {
        unadjustedStaticEval = VALUE_NONE;
        bestValue = futilityBase = -VALUE_INFINITE;

        goto QS_MOVES_LOOP;
    }

    if (ttHit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = ttd.eval();
        if (unadjustedStaticEval == VALUE_NONE)
            unadjustedStaticEval =
              evaluate(pos, networks[numaAccessToken], accCaches, optimism[ac]);

        bestValue = ss->staticEval = adjust_static_eval(unadjustedStaticEval, pos, ss);

        // Can ttValue be used as a better position evaluation (~13 Elo)
        if (ttValue != VALUE_NONE && (ttd.bound() & bound_for_tt(ttValue > bestValue)))
            bestValue = ttValue;
    }
    else
    {
        // In case of null move search, use previous staticEval with a opposite sign
        unadjustedStaticEval = preMove != Move::Null()
                               ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[ac])
                               : -(ss - 1)->staticEval;

        bestValue = ss->staticEval = adjust_static_eval(unadjustedStaticEval, pos, ss);
    }

    // Stand pat. Return immediately if bestValue is at least beta
    if (bestValue >= beta)
    {
        if (bestValue > beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
            bestValue = (bestValue + beta) / 2;

        if (!ttHit)
            ttu.update(DEPTH_NONE, false, BOUND_LOWER, Move::None(), bestValue,
                       unadjustedStaticEval);

        return bestValue;
    }

    alpha = std::max(alpha, bestValue);

    futilityBase = std::min(280 + ss->staticEval, VALUE_TB_WIN_IN_MAX_PLY - 1);

QS_MOVES_LOOP:

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    Value value;

    Move move, bestMove = Move::None();

    std::uint8_t moveCount = 0;

    const History<HPieceSq>* pieceSqHistory[2]{(ss - 1)->pieceSqHistory,  //
                                               (ss - 2)->pieceSqHistory};

    // Initialize a MovePicker object for the current position, prepare to search the moves.
    // Because the depth is <= DEPTH_ZERO here, only captures, promotions will be generated.
    MovePicker mp(pos, ttMove, &mainHistory, &captureHistory, &pawnHistory, pieceSqHistory,
                  &lowPlyHistory, ss->ply);
    // Step 5. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None())
    {
        assert(move.is_ok() && pos.pseudo_legal(move));
        assert(move == ttMove || pos.checkers() || pos.capture_promo(move));

        // Check for legality
        if (move != ttMove && !pos.legal(move))
            continue;

        ++moveCount;

        Square dst          = move.dst_sq();
        Piece  exMovedPiece = pos.ex_moved_piece(move);

        bool check   = pos.check(move);
        bool capture = pos.capture_promo(move);

        // Step 6. Pruning
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(ac))
        {
            // Futility pruning and moveCount pruning (~10 Elo)
            if (!check && dst != preSq && futilityBase > VALUE_TB_LOSS_IN_MAX_PLY
                && (move.type_of() != PROMOTION || move.promotion_type() != QUEEN))
            {
                if (moveCount > 2 - (preMove == Move::Null()))
                    continue;

                // clang-format off
                Value promoValue = move.type_of() != PROMOTION ? VALUE_ZERO : PIECE_VALUE[move.promotion_type()] - VALUE_PAWN;

                Value futilityValue = std::min(futilityBase + PIECE_VALUE[pos.captured(move)] + promoValue, VALUE_TB_WIN_IN_MAX_PLY - 1);

                // If static evaluation + value of piece going to captured is much lower than alpha, then prune this move. (~2 Elo)
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }

                // If static exchange evaluation is low enough, then prune this move. (~2 Elo)
                Value seeThreshold = -std::min(futilityBase - alpha, VALUE_TB_WIN_IN_MAX_PLY - 1);
                if (pos.see(move) < seeThreshold)
                {
                    bestValue = std::max(bestValue, std::min(alpha, futilityBase));
                    continue;
                }
                // clang-format on
            }

            // Continuation history based pruning (~3 Elo)
            if (!capture)
            {
                int conHist = (*pieceSqHistory[0])[exMovedPiece][dst]  //
                            + (*pieceSqHistory[1])[exMovedPiece][dst]  //
                            + pawnHistory[pawn_index(pos.pawn_key())][exMovedPiece][dst];
                if (conHist <= 5036)
                    continue;
            }

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (pos.see(move) < -82)
                continue;
        }

        // Speculative prefetch as early as possible
        tt.prefetch_entry(pos.move_key(move));

        // Update the move
        // clang-format off
        ss->move                     = move;
        ss->pieceSqHistory           = &continuationHistory[ss->inCheck][capture][exMovedPiece][dst];
        ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[exMovedPiece][dst];
        // clang-format on
        // Step 7. Make and search the move
        ++nodes;
        pos.do_move(move, st, check);

        value = -qsearch<PVNode>(pos, ss + 1, -beta, -alpha);

        pos.undo_move(move);

        assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

        // Step 8. Check for a new best move
        if (bestValue < value)
        {
            bestValue = value;

            if (alpha < value)
            {
                bestMove = move;

                if constexpr (PVNode)  // Update pv even in fail-high case
                    update_pv(ss, move);

                if (value >= beta)
                    break;  // Fail-high

                alpha = value;  // Update alpha! Always alpha < beta
            }
        }
    }

    // Step 9. Check for checkmate
    // All legal moves have been searched.
    // A special case: if in check and no legal moves were found, it is checkmate.
    if (ss->inCheck && bestValue == -VALUE_INFINITE)
    {
        assert(moveCount == 0 /*LegalMoveList(pos).empty()*/);
        return mated_in(ss->ply);  // Plies to mate from the root
    }

    // Adjust best value for fail high cases at non-pv nodes
    if (bestValue > beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (3 * bestValue + beta) / 4;

    // Save gathered info in transposition table
    // Static evaluation is saved as it was before adjustment by correction history
    Bound bound = bound_for_tt(bestValue >= beta);
    ttu.update(DEPTH_ZERO, ttPv, bound, bestMove, bestValue, unadjustedStaticEval);

    assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);

    return bestValue;
}

Move Worker::extract_tt_move(const Position& pos, Move ttMove) const noexcept {
    bool ttmFound = ttMove != Move::None() && pos.pseudo_legal(ttMove) && pos.legal(ttMove);
    int  rule50   = pos.rule50_count();
    while (!ttmFound && rule50 >= 14)
    {
        rule50 -= 8;
        Key key  = pos.key(rule50 - pos.rule50_count());
        ttMove   = tt.probe(key, compress_key(key)).tte->move();
        ttmFound = ttMove != Move::None() && pos.pseudo_legal(ttMove) && pos.legal(ttMove);
    }
    return ttmFound ? ttMove : Move::None();
}

// Called in case have no ponder move before exiting the search,
// for instance, in case stop the search during a fail high at root.
// Try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' have nothing to think about.
bool Worker::ponder_move_extracted() noexcept {
    assert(rootMoves.front().size() == 1 && rootMoves.front()[0] != Move::None());

    Move bm = rootMoves.front()[0];

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    rootPos.do_move(bm, st);

    // Legal moves for the opponent
    const LegalMoveList legalMoves(rootPos);
    if (!legalMoves.empty())
    {
        Move pm;

        Key key = rootPos.key();

        pm = extract_tt_move(rootPos, tt.probe(key, compress_key(key)).tte->move());
        if (pm == Move::None() || !legalMoves.contains(pm))
        {
            pm = Move::None();
            for (auto&& th : threads)
                if (th->worker.get() != this && th->worker->rootMoves.front()[0] == bm
                    && th->worker->rootMoves.front().size() >= 2)
                {
                    pm = th->worker->rootMoves.front()[1];
                    break;
                }
            if (pm == Move::None() && rootMoves.size() >= 2)
                for (auto&& th : threads)
                {
                    if (th->worker.get() == this || th->worker->completedDepth == DEPTH_ZERO)
                        continue;
                    const RootMove& rm = *th->worker->rootMoves.find(bm);
                    if (rm.size() >= 2)
                    {
                        pm = rm[1];
                        break;
                    }
                }
            if (pm == Move::None())
            {
                std::srand(static_cast<unsigned int>(std::time(nullptr)));
                pm = *(legalMoves.begin() + (std::rand() % legalMoves.size()));
            }
        }
        rootMoves.front() += pm;
    }

    rootPos.undo_move(bm);
    return rootMoves.front().size() == 2;
}

void MainSearchManager::init(std::uint16_t threadCount) noexcept {
    assert(threadCount != 0);

    timeManager.clear();
    first            = true;
    preBestCurValue  = VALUE_ZERO;
    preBestAvgValue  = VALUE_ZERO;
    preTimeReduction = 1.0;

    mainHistory.fill(0);
    captureHistory.fill(-753);
    pawnHistory.fill(-1152);
    for (bool inCheck : {false, true})
        for (bool capture : {false, true})
            for (auto& pieceSqHist : continuationHistory[inCheck][capture])
                for (auto& h : pieceSqHist)
                    h->fill(-678);

    pawnCorrectionHistory.fill(0);
    nonPawnCorrectionHistory.fill(0);
    minorCorrectionHistory.fill(0);
    majorCorrectionHistory.fill(0);
    for (auto& pieceSqCorrHist : continuationCorrectionHistory)
        for (auto& h : pieceSqCorrHist)
            h->fill(0);

    auto threadReduction = 18.43 + 0.50 * std::log(threadCount);
    reductions[0]        = 0;
    for (std::size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = threadReduction * std::log(i);
}

// Used to print debug info and, more importantly,
// to detect when out of available time and thus stop the search.
void MainSearchManager::check_time(Worker& worker) noexcept {
    assert(callsCount > 0);
    if (--callsCount > 0)
        return;
    callsCount = worker.limit.hitRate;

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
      && ((worker.limit.use_time_manager() && (ponderhitStop || elapsedTime > timeManager.maximum()))
       || (worker.limit.moveTime != 0 && elapsedTime >= worker.limit.moveTime)
       || (worker.limit.nodes != 0 && worker.threads.nodes() >= worker.limit.nodes)))
        worker.threads.stop = worker.threads.abort = true;
    // clang-format on
}

void MainSearchManager::load_book(const std::string& bookFile) const noexcept {
    polyBook.init(bookFile);
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

    auto& rootPos   = worker.rootPos;
    auto& rootMoves = worker.rootMoves;

    auto time     = std::max(elapsed(), 1ll);
    auto nodes    = worker.threads.nodes();
    auto hashfull = worker.tt.hashfull();
    auto tbHits   = worker.threads.tbHits() + worker.tbConfig.rootInTB * rootMoves.size();
    bool wdlShow  = worker.options["UCI_ShowWDL"];

    for (std::uint8_t i = 0; i < worker.multiPV; ++i)
    {
        RootMove& rm = rootMoves[i];

        bool updated = rm.curValue != -VALUE_INFINITE;

        if (i != 0 && depth == 1 && !updated)
            continue;

        Depth d = updated ? +depth : std::max(depth - 1, 1);
        Value v = updated ? rm.uciValue : rm.preValue;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = worker.tbConfig.rootInTB && std::abs(v) <= VALUE_TB;
        if (tb)
            v = rm.tbValue;

        // tablebase- and previous-scores are exact
        bool exact = i != worker.curIdx || tb || !updated;

        // Potentially correct and extend the PV, and in exceptional cases value also
        if (std::abs(v) >= VALUE_TB_WIN_IN_MAX_PLY && std::abs(v) < VALUE_MATES_IN_MAX_PLY
            && (exact || !(rm.boundLower || rm.boundUpper)))
            extend_tb_pv(rootPos, rm, v, worker.limit, worker.options);

        FullInfo info(rootPos, rm);
        info.depth     = d;
        info.value     = v;
        info.multiPV   = 1 + i;
        info.boundShow = !exact;
        info.wdlShow   = wdlShow;
        info.time      = time;
        info.nodes     = nodes;
        info.hashfull  = hashfull;
        info.tbHits    = tbHits;

        updateCxt.onUpdateFull(info);
    }
}

void Skill::init(const Options& options) noexcept {

    if (options["UCI_LimitStrength"])
    {
        std::uint16_t uciELO = options["UCI_ELO"];

        auto e = double(uciELO - MIN_ELO) / (MAX_ELO - MIN_ELO);
        level  = std::clamp(((37.2473 * e - 40.8525) * e + 22.2943) * e - 0.311438,  //
                            MIN_LEVEL, MAX_LEVEL - 0.01);
    }
    else
    {
        level = options["Skill Level"];
    }

    bestMove = Move::None();
}

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
// clang-format off
Move Skill::pick_best_move(const RootMoves& rootMoves, std::uint8_t multiPV, bool pickBest) noexcept {
    // clang-format on
    static PRNG rng(now());  // PRNG sequence should be non-deterministic

    if (pickBest || bestMove == Move::None())
    {
        // RootMoves are already sorted by value in descending order
        Value  curValue = rootMoves[0].curValue;
        int    delta    = std::min(curValue - rootMoves[multiPV - 1].curValue, +VALUE_PAWN);
        double weakness = 2.0 * (3.0 * MAX_LEVEL - level);

        Value maxValue = -VALUE_INFINITE;
        // Choose best move. For each move value add two terms, both dependent on weakness.
        // One is deterministic and bigger for weaker levels, and one is random.
        // Then choose the move with the resulting highest value.
        for (std::uint8_t i = 0; i < multiPV; ++i)
        {
            Value value = rootMoves[i].curValue
                        // This is magic formula for Push
                        + int(weakness * (curValue - rootMoves[i].curValue)
                              + delta * (rng.rand<std::uint32_t>() % int(weakness)))
                            / 128;

            if (maxValue <= value)
            {
                maxValue = value;
                bestMove = rootMoves[i][0];
            }
        }
    }

    return bestMove;
}

namespace {

// History updates
void update_main_history(Color ac, Move m, int bonus) noexcept {
    assert(m.is_ok());
    mainHistory[ac][m.org_dst()] << bonus;
}

// Updates histories of the move pairs formed by
// move at ply -1, -2, -3, -4, and -6 with move at ply 0.
void update_continuation_history(Stack* ss, Piece pc, Square dst, int bonus) noexcept {
    assert(is_ok(dst));

    bonus *= 0.8281;

    for (std::int16_t i : {1, 2, 3, 4, 6})
    {
        // Only update the first 2 continuation histories if in check
        if (i > 2 && ss->inCheck)
            break;
        if ((ss - i)->move.is_ok())
            (*(ss - i)->pieceSqHistory)[pc][dst] << (1.0 - 0.5 * (i == 3)) * bonus;
    }
}

void update_pawn_history(const Position& pos, Piece pc, Square dst, int bonus) noexcept {
    pawnHistory[pawn_index(pos.pawn_key())][pc][dst] << bonus;
}

void update_low_ply_history(std::int16_t ssPly, Move m, int bonus) noexcept {
    assert(m.is_ok());
    lowPlyHistory[ssPly][m.org_dst()] << bonus;
}

void update_quiet_history(const Position& pos, Stack* ss, Move m, int bonus) noexcept {
    assert(m.is_ok());

    auto dst          = m.dst_sq();
    auto exMovedPiece = pos.ex_moved_piece(m);

    update_main_history(pos.active_color(), m, bonus);
    update_pawn_history(pos, exMovedPiece, dst, bonus / 2);
    update_continuation_history(ss, exMovedPiece, dst, bonus);
    if (ss->ply < LOW_PLY_SIZE)
        update_low_ply_history(ss->ply, m, bonus);
}

void update_capture_history(const Position& pos, Move m, int bonus) noexcept {
    assert(m.is_ok());

    auto movedPiece = pos.moved_piece(m);
    auto captured   = pos.ex_captured(m);
    assert(captured != KING);
    captureHistory[movedPiece][m.dst_sq()][captured] << bonus;
}

// clang-format off
// Updates history at the end of search() when a bestMove is found
void update_history(const Position& pos, Stack* ss, Depth depth, Move bm, const std::array<Moves, 2>& moves) noexcept {
    assert(bm.is_ok());

    int bonus = +stat_bonus(depth);
    int malus = -stat_malus(depth);

    if (pos.capture_promo(bm))
        update_capture_history(pos, bm, bonus);
    else
    {
        update_quiet_history(pos, ss, bm, bonus);

        // Decrease history for all non-best quiet moves
        for (Move qm : moves[0])
            update_quiet_history(pos, ss, qm, malus);
    }

    // Decrease history for all non-best capture moves
    for (Move cm : moves[1])
        update_capture_history(pos, cm, malus);

    // Extra penalty for a quiet early move that was not a TT move
    // in the previous ply when it gets refuted.
    if ((ss - 1)->move.is_ok() && pos.captured_piece() == NO_PIECE && (ss - 1)->moveCount == 1 + (ss - 1)->ttM)
        update_continuation_history(ss - 1, pos.prev_ex_moved_piece((ss - 1)->move), (ss - 1)->move.dst_sq(), malus);
}

void update_correction_history(const Position& pos, Stack* ss, int bonus) noexcept {
    Color ac = pos.active_color();
    Move  m  = (ss - 1)->move;

    bonus = std::clamp(bonus, -CORRECTION_HISTORY_LIMIT / 4, +CORRECTION_HISTORY_LIMIT / 4);

       pawnCorrectionHistory[ac][correction_index(pos.    pawn_key(WHITE))][WHITE] << 0.7891 * bonus;
       pawnCorrectionHistory[ac][correction_index(pos.    pawn_key(BLACK))][BLACK] << 0.7891 * bonus;
    nonPawnCorrectionHistory[ac][correction_index(pos.non_pawn_key(WHITE))][WHITE] << 1.1250 * bonus;
    nonPawnCorrectionHistory[ac][correction_index(pos.non_pawn_key(BLACK))][BLACK] << 1.1250 * bonus;
      minorCorrectionHistory[ac][correction_index(pos.   minor_key())]             << 1.1953 * bonus;
      majorCorrectionHistory[ac][correction_index(pos.   major_key())]             << 1.2266 * bonus;
    if (m.is_ok())
      (*(ss - 2)->pieceSqCorrectionHistory)[pos.prev_ex_moved_piece(m)][m.dst_sq()]<< 1.0000 * bonus;
}

// Update raw staticEval according to various CorrectionHistory value
// and guarantee evaluation does not hit the tablebase range.
Value adjust_static_eval(Value ev, const Position& pos, Stack* ss) noexcept {
    Color ac = pos.active_color();
    Move  m  = (ss - 1)->move;

    int wpcv  =    pawnCorrectionHistory[ac][correction_index(pos.    pawn_key(WHITE))][WHITE];
    int bpcv  =    pawnCorrectionHistory[ac][correction_index(pos.    pawn_key(BLACK))][BLACK];
    int wnpcv = nonPawnCorrectionHistory[ac][correction_index(pos.non_pawn_key(WHITE))][WHITE];
    int bnpcv = nonPawnCorrectionHistory[ac][correction_index(pos.non_pawn_key(BLACK))][BLACK];
    int micv  =   minorCorrectionHistory[ac][correction_index(pos.   minor_key())];
    int mjcv  =   majorCorrectionHistory[ac][correction_index(pos.   major_key())];
    int cntcv = m.is_ok()
              ? (*(ss - 2)->pieceSqCorrectionHistory)[pos.prev_ex_moved_piece(m)][m.dst_sq()]
              : 0;

    int aev = ev + (5932 * (wpcv + bpcv) + 6666 * (wnpcv + bnpcv) + 5660 * micv + 3269 * mjcv + 5555 * cntcv) / 0x20000;
    return std::clamp(aev, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}
// clang-format on

// Used to correct and extend PVs for moves that have a TB (but not a mate) score.
// Keeps the search based PV for as long as it is verified to maintain the game outcome, truncates afterwards.
// Finally, extends to mate the PV, providing a possible continuation (but not a proven mating line).
void extend_tb_pv(Position&      rootPos,
                  RootMove&      rootMove,
                  Value&         value,
                  const Limit&   limit,
                  const Options& options) noexcept {

    auto startTime = SteadyClock::now();

    TimePoint moveOverhead = options["MoveOverhead"];

    // Do not use more than moveOverhead / 2 time, if time manager is active.
    auto time_to_abort = [&]() -> bool {
        auto endTime = SteadyClock::now();
        return limit.use_time_manager()
            && 2 * std::chrono::duration<double, std::milli>(endTime - startTime).count()
                 > moveOverhead;
    };

    std::list<State> states;

    // Step 0. Do the rootMove, no correction allowed, as needed for MultiPV in TB.
    State& rootSt = states.emplace_back();
    rootPos.do_move(rootMove[0], rootSt);

    // Step 1. Walk the PV to the last position in TB with correct decisive score
    std::int16_t ply = 1;
    while (ply < int(rootMove.size()))
    {
        Move pvMove = rootMove[ply];

        RootMoves legalRootMoves;
        for (Move m : LegalMoveList(rootPos))
            legalRootMoves.emplace(m);

        auto tbConfig = Tablebases::rank_root_moves(rootPos, legalRootMoves, options);

        RootMove& rm = *legalRootMoves.find(pvMove);

        if (rm.tbRank != legalRootMoves.front().tbRank)
            break;

        State& st = states.emplace_back();
        rootPos.do_move(pvMove, st);

        // Don't allow for repetitions or drawing moves along the PV in TB regime.
        if (tbConfig.rootInTB && rootPos.is_draw(1 + ply, true))
        {
            rootPos.undo_move(pvMove);
            states.pop_back();
            break;
        }

        ++ply;

        // Full PV shown will thus be validated and end TB.
        // If we can't validate the full PV in time, we don't show it.
        if (tbConfig.rootInTB && time_to_abort())
            break;
    }

    // Resize the PV to the correct part
    rootMove.resize(ply);

    // Step 2. Now extend the PV to mate, as if the user explores syzygy-tables.info using
    // top ranked moves (minimal DTZ), which gives optimal mates only for simple endgames e.g. KRvK
    while (!rootPos.is_draw(0, true))
    {
        RootMoves legalRootMoves;
        for (Move m : LegalMoveList(rootPos))
        {
            RootMove& rm = legalRootMoves.emplace(m);

            State st;
            rootPos.do_move(m, st);
            // Give a score of each move to break DTZ ties
            // restricting opponent mobility, but not giving the opponent a capture.
            for (Move om : LegalMoveList(rootPos))
                rm.tbRank -= 1 + 99 * rootPos.capture(om);
            rootPos.undo_move(m);
        }

        // Mate found
        if (legalRootMoves.empty())
            break;

        // Sort moves according to their above assigned rank.
        // This will break ties for moves with equal DTZ in rank_root_moves.
        legalRootMoves.sort([](const RootMove& rm1, const RootMove& rm2) noexcept -> bool {
            return rm1.tbRank > rm2.tbRank;
        });

        // The winning side tries to minimize DTZ, the losing side maximizes it.
        auto tbConfig = Tablebases::rank_root_moves(rootPos, legalRootMoves, options, true);

        // If DTZ is not available might not find a mate, so bail out.
        if (!tbConfig.rootInTB || tbConfig.cardinality > 0)
            break;

        ++ply;

        Move pvMove = legalRootMoves.front()[0];
        rootMove.push_back(pvMove);
        State& st = states.emplace_back();
        rootPos.do_move(pvMove, st);

        if (time_to_abort())
            break;
    }

    // Finding a draw in this function is an exceptional case, that cannot happen
    // during engine game play, since we have a winning score, and play correctly
    // with TB support. However, it can be that a position is draw due to the 50 move
    // rule if it has been been reached on the board with a non-optimal 50 move counter
    // (e.g. 8/8/6k1/3B4/3K4/4N3/8/8 w - - 54 106) which TB with dtz counter rounding
    // cannot always correctly rank.
    // Adjust the score to match the found PV. Note that a TB loss score can be displayed
    // if the engine did not find a drawing move yet, but eventually search will figure it out.
    // (e.g. 1kq5/q2r4/5K2/8/8/8/8/7Q w - - 96 1)
    if (rootPos.is_draw(0, true))
        value = VALUE_DRAW;

    // Undo the PV moves.
    for (auto itr = rootMove.rbegin(); itr != rootMove.rend(); ++itr)
        rootPos.undo_move(*itr);

    // Inform if couldn't get a full extension in time.
    if (time_to_abort())
        sync_cout << "info string "
                  << "PV extension requires more time, increase MoveOverhead as needed."
                  << sync_endl;
}

}  // namespace

}  // namespace DON
