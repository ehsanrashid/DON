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

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
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
        return VALUE_NONE;
    assert(-VALUE_INFINITE < v && v < +VALUE_INFINITE);
    return v >= VALUE_TB_WIN_IN_MAX_PLY  ? v + ply  //
         : v <= VALUE_TB_LOSS_IN_MAX_PLY ? v - ply
                                         : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated from
// current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, return the highest non-TB score instead.
constexpr Value value_from_tt(Value v, std::int16_t ply, std::uint8_t rule50) noexcept {

    if (v == VALUE_NONE)
        return VALUE_NONE;
    assert(-VALUE_INFINITE < v && v < +VALUE_INFINITE);
    // Handle TB win or better
    if (v >= VALUE_TB_WIN_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate value
        if (v >= VALUE_MATES_IN_MAX_PLY && VALUE_MATE - v > 100 - rule50)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB - v > 100 - rule50)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        return v - ply;
    }
    // Handle TB loss or worse
    if (v <= VALUE_TB_LOSS_IN_MAX_PLY)
    {
        // Downgrade a potentially false mate value
        if (v <= VALUE_MATED_IN_MAX_PLY && VALUE_MATE + v > 100 - rule50)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB + v > 100 - rule50)
            return VALUE_TB_LOSS_IN_MAX_PLY + 1;

        return v + ply;
    }

    return v;
}

}  // namespace

void TTUpdater::update(
  Depth depth, bool isPv, Bound bound, Move move, Value value, Value eval) noexcept {
    _tte->save(_key16, depth, isPv, bound, move, value_to_tt(value, _ply), eval, _tt.generation());
}

using Eval::evaluate;

namespace Search {

namespace {

PolyBook Polybook;

constexpr std::array<double, 10> EvalReduction{0.981, 0.956, 0.895, 0.949, 0.913,
                                               0.942, 0.933, 0.890, 0.984, 0.941};

// Reductions lookup table initialized at startup
std::array<short, MAX_MOVES> reductions;  // [depth or moveCount]
constexpr Depth
reduction(Depth depth, std::uint8_t moveCount, int deltaRatio, bool improving) noexcept {
    int reductionScale = int(reductions[depth]) * int(reductions[moveCount]);
    return (1236 + reductionScale - deltaRatio) / 1024 + (!improving && reductionScale > 1326);
}

// Futility margin
constexpr Value
futility_margin(Depth depth, bool cutNodeNoTtHit, bool improving, bool worsening) noexcept {
    Value futilityMul = 109 - 40 * cutNodeNoTtHit;
    return std::max(depth * futilityMul                        //
                      - 1888 * improving * futilityMul / 1024  //
                      - 341 * worsening * futilityMul / 1024,
                    0);
}

constexpr std::uint16_t futility_move_count(Depth depth, bool improving) noexcept {
    return (3 + depth * depth) >> (1 - improving);
}

// History and stats update bonus, based on depth
constexpr int stat_bonus(Depth depth) noexcept { return std::clamp(186 * depth - 285, 20, 1524); }

// History and stats update malus, based on depth
constexpr int stat_malus(Depth depth) noexcept { return std::clamp(707 * depth - 260, 0, 2073); }

// Add a small random value to draw evaluation to avoid 3-fold blindness
constexpr Value draw_value(Key key, std::uint64_t nodes) noexcept {
    return VALUE_DRAW + (key & 1) - (nodes & 1);
}

// Add correctionHistory value to raw staticEval and guarantee evaluation does not hit the tablebase range
Value adjust_static_eval(Value v, const Worker& worker, const Position& pos) noexcept {
    v += worker.correctionHistory[pos.side_to_move()][correction_index(pos.pawn_key())] / 10;
    return std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}

// Adds current move and appends subPv[]
void update_pv(Moves& mainPv, Move move, const Moves& subPv) noexcept {
    mainPv = subPv;
    mainPv.push_front(move);
}

// Updates histories of the move pairs formed
// by moves at ply -1, -2, -3, -4, and -6 with current move.
void update_continuation_histories(Stack* ss, Piece pc, Square dst, int bonus) noexcept {
    assert(/*is_ok(pc) &&*/ is_ok(dst));

    bonus = 51 * bonus / 64;

    for (std::uint8_t i : {1, 2, 3, 4, 6})
    {
        // Only update the first 2 continuation histories if in check
        if (i > 2 && ss->inCheck)
            break;
        if (((ss - i)->currentMove).is_ok())
            (*(ss - i)->continuationHistory)[pc][dst] << bonus / (1 + (i == 3));
    }
}

void update_refutations(
  Worker& worker, Stack* ss, Move move, Square prevDst, Piece prevMovedPiece) noexcept {
    // Update killerMoves
    if (ss->killerMoves[0] != move)
    {
        ss->killerMoves[1] = ss->killerMoves[0];
        ss->killerMoves[0] = move;
    }
    // Update counterMoves history
    if (is_ok(prevDst))
        worker.counterMoves[prevMovedPiece][prevDst] = move;
}

void update_quiet_histories(
  Worker& worker, const Position& pos, Stack* ss, Move move, int bonus) noexcept {

    worker.mainHistory[pos.side_to_move()][move.org_dst()] << bonus;
    worker.pawnHistory[pawn_index(pos.pawn_key())][pos.ex_moved_piece(move)][move.dst_sq()]
      << bonus / 2;

    update_continuation_histories(ss, pos.ex_moved_piece(move), move.dst_sq(), bonus);
}

// Updates move sorting heuristics
void update_quiet_stats(Worker&         worker,
                        const Position& pos,
                        Stack*          ss,
                        Move            move,
                        Square          prevDst,
                        Piece           prevMovedPiece,
                        int             bonus) noexcept {

    update_refutations(worker, ss, move, prevDst, prevMovedPiece);
    update_quiet_histories(worker, pos, ss, move, bonus);
}

void update_capture_histories(Worker& worker, const Position& pos, Move move, int bonus) noexcept {

    worker.captureHistory[pos.moved_piece(move)][move.dst_sq()][type_of(pos.captured_piece(move))]
      << bonus;
}

// Updates stats at the end of search() when a bestMove is found
void update_stats(Worker&         worker,
                  const Position& pos,
                  Stack*          ss,
                  Move            bestMove,
                  Depth           depth,
                  bool            bonusMore,
                  bool            prevIsQuiet,
                  Square          prevDst,
                  Piece           prevMovedPiece,
                  const Moves&    quietMoves,
                  const Moves&    captureMoves) noexcept {
    int malus = stat_malus(depth);

    if (!pos.capture_stage(bestMove))
    {
        update_quiet_stats(worker, pos, ss, bestMove, prevDst, prevMovedPiece,
                           stat_bonus(bonusMore + depth));

        // Decrease stats for all non-best quiet moves
        for (Move qm : quietMoves)
            update_quiet_histories(worker, pos, ss, qm, -malus);
    }
    else
    {
        // Increase stats for the best move in case it was a capture move
        update_capture_histories(worker, pos, bestMove, stat_bonus(1 + depth));
    }

    // Extra penalty for a quiet early move that was main killer move
    // or not a TT move in the previous ply when it gets refuted.
    if (prevIsQuiet
        && ((ss - 1)->currentMove == (ss - 1)->killerMoves[0]
            || (ss - 1)->moveCount == 1 + ((ss - 1)->ttMove != Move::None())))
        update_continuation_histories(ss - 1, prevMovedPiece, prevDst, -malus);

    // Decrease stats for all non-best capture moves
    for (Move cm : captureMoves)
        update_capture_histories(worker, pos, cm, -malus);
}

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

// Initialize the Histories
void Worker::init() noexcept {
    counterMoves.fill(Move::None());
    mainHistory.fill(0);
    captureHistory.fill(-700);
    pawnHistory.fill(-1193);
    correctionHistory.fill(0);

    for (bool inCheck : {false, true})
        for (bool captures : {false, true})
            for (auto& pieceDst : continuationHistory[inCheck][captures])
                for (auto& h : pieceDst)
                    h->fill(-56);

    accCaches.init(networks[numaAccessToken]);
}

void Worker::start_search() noexcept {
    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    // Non-main threads go directly to iterative_deepening()
    if (!mainManager)
    {
        iterative_deepening();
        return;
    }

    mainManager->stopPonderhit = false;
    mainManager->ponder        = limits.ponder;
    mainManager->callsCount    = limits.hitRate;
    mainManager->timeManager.init(limits, rootPos, options);
    mainManager->skill.init(options);
    tt.update_generation(!limits.infinite);

    if (rootMoves.empty())
    {
        rootMoves.emplace(Move::None());
        mainManager->updateContext.onUpdateEnd({rootPos.checkers() != 0});
    }
    else
    {
        Move bookBestMove = Move::None();
        if (!limits.infinite && limits.mate == 0)
        {
            // Check polyglot book
            if (options["OwnBook"] && Polybook.is_enabled()
                && rootPos.game_move() < options["BookDepth"])
                bookBestMove = Polybook.probe(rootPos, options["BookPickBest"]);
        }

        if (bookBestMove != Move::None() && rootMoves.contains(bookBestMove))
        {
            StateInfo st;
            ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

            rootPos.do_move(bookBestMove, st);
            Move bookPonderMove = Polybook.probe(rootPos, options["BookPickBest"]);
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
    while (!threads.stop && (limits.infinite || mainManager->ponder))
    {}  // Busy wait for a stop or a ponder reset

    // Stop the threads if not already stopped
    // (also raise the stop if "ponderhit" just reset mainManager->ponder).
    threads.stop = true;

    // Wait until all threads have finished
    threads.wait_finish();

    // When playing in 'Nodes as Time' mode,
    // subtract the searched nodes from the start nodes before exiting.
    if (mainManager->timeManager.use_nodes_time())
        mainManager->timeManager.advance(threads.nodes()
                                         - limits.clock[rootPos.side_to_move()].inc);

    Worker* bestWorker = this;
    if (threads.size() > 1 && multiPV == 1 && limits.mate == 0
        && rootMoves.front()[0] != Move::None() && !mainManager->skill.enabled())
        bestWorker = threads.best_thread()->worker.get();

    mainManager->prevBestCurValue = bestWorker->rootMoves.front().curValue;
    mainManager->prevBestAvgValue = bestWorker->rootMoves.front().avgValue;

    // Send again PV info if have a new best worker
    if (bestWorker != this)
        mainManager->info_pv(*bestWorker, bestWorker->completedDepth);

    assert(!bestWorker->rootMoves.empty() && !bestWorker->rootMoves.front().empty());
    Move bestMove = bestWorker->rootMoves.front()[0];
    Move ponderMove =
      bestMove != Move::None()
          && (bestWorker->rootMoves.front().size() >= 2 || bestWorker->extract_ponder_move())
        ? bestWorker->rootMoves.front()[1]
        : Move::None();

    mainManager->updateContext.onUpdateMove({bestMove, ponderMove});
}

// Get a pointer to the search manager,
// Only allowed to be called by the main worker.
MainSearchManager* Worker::main_manager() const noexcept {
    assert(is_main_worker());
    return static_cast<MainSearchManager*>(manager.get());
}

// Main iterative deepening loop. It calls search() repeatedly with increasing depth
// until the allocated thinking time has been consumed, the user stops the search,
// or the maximum search depth is reached.
void Worker::iterative_deepening() noexcept {
    // Allocate stack with extra size to allow access from (ss - 7) to (ss + 1):
    // (ss - 7) is needed for update_continuation_histories(ss - 1) which accesses (ss - 6),
    // (ss + 1) is needed for initialization of cutoffCount & killerMoves.
    constexpr std::int16_t MAX_STACK = MAX_PLY + 8;
    constexpr std::int16_t OFFSET    = 7;

    Stack  stack[MAX_STACK]{};
    Stack* ss = stack + OFFSET;
    for (std::int16_t i = 0 - OFFSET; i < MAX_STACK - OFFSET; ++i)
    {
        (ss + i)->ply = i;
        if (i < 0)
        {
            // Use as a sentinel
            (ss + i)->continuationHistory = &continuationHistory[0][0][NO_PIECE][SQ_ZERO];
            (ss + i)->staticEval          = VALUE_NONE;
        }
    }
    assert(ss->ply == 0);

    Color stm = rootPos.side_to_move();

    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    multiPV = DefaultMultiPV;
    if (mainManager)
    {
        mainManager->iterBestValue.fill(mainManager->prevBestCurValue);

        multiPV = options["MultiPV"];
        // When playing with strength handicap enable MultiPV search that will
        // use behind-the-scenes to retrieve a set of possible moves.
        if (mainManager->skill.enabled())
            multiPV = std::max<std::uint8_t>(multiPV, 4);
    }
    multiPV = std::min<std::uint8_t>(multiPV, rootMoves.size());

    std::uint16_t researchCounter = 0;
    std::uint8_t  iterIdx         = 0;

    double timeReduction = 1.0, sumBestMoveChange = 0.0;

    Value bestValue = -VALUE_INFINITE;

    Moves lastBestPV{Move::None()};
    Value lastBestCurValue = -VALUE_INFINITE;
    Value lastBestPreValue = -VALUE_INFINITE;
    Value lastBestUciValue = -VALUE_INFINITE;
    Depth lastBestDepth    = DEPTH_ZERO;
    if (!mainManager && threads.size() >= 4)
        ++rootDepth;
    // Iterative deepening loop until requested to stop or the target depth is reached
    while (++rootDepth < MAX_PLY
           && !(mainManager && limits.depth != DEPTH_ZERO && rootDepth > limits.depth))
    {
        // Age out PV variability metric
        if (mainManager && limits.use_time_manager())
            sumBestMoveChange *= 0.50;

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (RootMove& rm : rootMoves)
            rm.preValue = rm.curValue;

        if (threads.research)
            ++researchCounter;

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

            // Reset aspiration window starting size
            Value avgValue = rootMoves[curIdx].avgValue;

            Value delta = 9 + (avgValue * avgValue) / 10182;
            Value alpha = std::max(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min(avgValue + delta, +VALUE_INFINITE);

            // Adjust optimism based on root move's avgValue (~4 Elo)
            optimism[stm]  = 127 * avgValue / (86 + std::abs(avgValue));
            optimism[~stm] = -optimism[stm];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, research with a bigger window until don't fail high/low anymore.
            std::uint16_t failedHighCounter = 0;
            while (true)
            {
                rootDelta = beta - alpha;
                // Adjust the effective depth searched, but ensure at least one effective increment
                // for every 4 researchCounter steps.
                Depth adjustedDepth =
                  std::max<Depth>(rootDepth - failedHighCounter - 3 * (1 + researchCounter) / 4, 1);
                bestValue = search<true>(rootPos, ss, alpha, beta, adjustedDepth, false);

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

                // When failing high/low give some update (without cluttering the UI) before a re-search.
                if (mainManager && multiPV == 1 && (alpha >= bestValue || bestValue >= beta)
                    && mainManager->elapsed() > 3000)
                    mainManager->info_pv(*this, rootDepth);

                // In case of failing low/high increase aspiration window and research,
                // otherwise exit the loop.
                if (bestValue <= alpha)
                {
                    beta  = (alpha + beta) / 2;
                    alpha = std::max(bestValue - delta, -VALUE_INFINITE);

                    failedHighCounter = 0;
                    if (mainManager && mainManager->ponder)
                        mainManager->stopPonderhit = false;
                }
                else if (bestValue >= beta)
                {
                    if (failedHighCounter != 0)  // on subsequent fail high raise alpha too
                        alpha = (alpha + beta) / 2;
                    beta = std::min(bestValue + delta, +VALUE_INFINITE);
                    ++failedHighCounter;
                }
                else
                    break;

                delta += delta / 3;

                assert(-VALUE_INFINITE <= alpha && beta <= +VALUE_INFINITE);
            }

            // Sort the PV lines searched so far and update the GUI
            rootMoves.sort(fstIdx, 1 + curIdx);

            if (mainManager
                && (threads.stop || 1 + curIdx == multiPV || mainManager->elapsed() > 3000)
                // A thread that aborted search can have mated-in/TB-loss PV and score
                // that cannot be trusted, i.e. it can be delayed or refuted if have
                // had time to fully search other root-moves. Thus, suppress this output and
                // below pick a proven score/PV for this thread (from the previous iteration).
                && !(threads.abort && rootMoves.front().uciValue <= VALUE_TB_LOSS_IN_MAX_PLY))
                mainManager->info_pv(*this, rootDepth);

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

        if (threads.stop)
            break;
        if (!mainManager)
            continue;

        // Have found a "mate in x"?
        if (limits.mate != 0 && rootMoves.front().curValue == rootMoves.front().uciValue
            && ((rootMoves.front().curValue != +VALUE_INFINITE
                 && rootMoves.front().curValue >= VALUE_MATES_IN_MAX_PLY
                 && VALUE_MATE - rootMoves.front().curValue <= 2 * limits.mate)
                || (rootMoves.front().curValue != -VALUE_INFINITE
                    && rootMoves.front().curValue <= VALUE_MATED_IN_MAX_PLY
                    && VALUE_MATE + rootMoves.front().curValue <= 2 * limits.mate)))
            threads.stop = true;

        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (mainManager->skill.enabled() && mainManager->skill.time_to_pick(rootDepth))
            mainManager->skill.pick_best_move(rootMoves, multiPV);

        // Do have time for the next iteration? Can stop searching now?
        if (limits.use_time_manager() && !threads.stop && !mainManager->stopPonderhit)
        {
            // Use part of the gained time from a previous stable move for the current move
            for (auto&& th : threads)
            {
                sumBestMoveChange += th->worker->bestMoveChange;
                th->worker->bestMoveChange = 0;
            }

            // clang-format off
            bool notInit = mainManager->prevBestCurValue != VALUE_ZERO;
            double evalChanging =
              std::clamp(0.1067
                        + 22.3e-3 * (mainManager->prevBestAvgValue - bestValue)
                        + 09.7e-3 * (mainManager->iterBestValue[iterIdx] - bestValue),
                         1.0 - 0.420 * notInit, 1.0 + 0.667 * notInit);
            // If the bestMove is stable over several iterations, reduce time accordingly
            Depth stableDepth    = std::max<Depth>(completedDepth - lastBestDepth, 1);
            timeReduction        = 0.687 + 0.404 * std::clamp(std::ceil(stableDepth / (3.0 + 2.0 * std::log10((1 + stableDepth) / 2)) - 1.27), 0.0, 3.0);
            double reduction     = 0.4608295 * (1.48 + mainManager->prevTimeReduction) / timeReduction;
            timeReduction        = std::min(timeReduction, 1.495);
            double instability   = 1.0 + 1.88 * sumBestMoveChange / threads.size();
            double evalReduction = EvalReduction[std::clamp<int>((750 + bestValue) / 150, 0, EvalReduction.size() - 1)];
            double nodeReduction = 1.0 - 3.262e-3 * (completedDepth >= 20)
                                 * std::max<int>(1000 * rootMoves.front().nodes / std::uint64_t(nodes) - 920, 0);
            double reCapture     = 1.0 - 50e-3 * (rootPos.cap_square() == rootMoves.front()[0].dst_sq()
                                               && rootPos.pieces(~stm) & rootPos.cap_square());

            TimePoint totalTime = TimePoint(mainManager->timeManager.optimum() * evalChanging * reduction
                                          * instability * evalReduction * nodeReduction * reCapture);
            // clang-format on
            // Cap totalTime in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = TimePoint(std::min(0.5 * totalTime, 500.0));

            TimePoint elapsedTime = mainManager->elapsed(*this);

            // Stop the search if have exceeded the totalTime
            if (elapsedTime > totalTime)
            {
                // If allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainManager->ponder)
                    mainManager->stopPonderhit = true;
                else
                    threads.stop = true;
            }

            threads.research = elapsedTime > 0.506 * totalTime && !mainManager->ponder;
        }

        mainManager->iterBestValue[iterIdx] = bestValue;

        iterIdx = (1 + iterIdx) % mainManager->iterBestValue.size();

        if (threads.stop)
            break;
    }

    if (!mainManager)
        return;

    mainManager->prevTimeReduction = timeReduction;

    // If the skill level is enabled, swap the best PV line with the sub-optimal one
    if (mainManager->skill.enabled())
        rootMoves.swap_to_front(mainManager->skill.pick_best_move(rootMoves, multiPV, false));
}

// Main search function for both PV and non-PV nodes.
template<bool PVNode>
// clang-format off
Value Worker::search(
  Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode, Move excludedMove) noexcept {
    // clang-format on
    const bool RootNode = PVNode && ss->ply == 0;

    // Dive into quiescence search when the depth reaches zero
    if (depth <= DEPTH_ZERO)
        return qsearch<PVNode>(pos, ss, alpha, beta);

    // Limit the depth if extensions made it too large
    depth = std::min<Depth>(depth, MAX_PLY - 1);

    if (!RootNode)
        // Check if have an upcoming move that draws by repetition, or
        // if the opponent had an alternative move earlier to this position.
        // The VALUE_TB_LOSS_IN_MAX_PLY check is necessary as long as has_game_cycle is approximate.
        if (alpha < VALUE_DRAW && alpha > VALUE_TB_LOSS_IN_MAX_PLY && pos.has_game_cycle(ss->ply))
        {
            alpha = draw_value(pos.key(), nodes);
            if (alpha >= beta)
                return alpha;
        }

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (alpha == beta - 1));
    assert(DEPTH_ZERO < depth && depth < MAX_PLY);
    assert(!(PVNode && cutNode));

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    // Step 1. Initialize node
    ss->inCheck   = pos.checkers();
    ss->moveCount = 0;
    ss->history   = 0;

    Color stm = pos.side_to_move();

    // Check for the available remaining time
    if (is_main_worker())
        main_manager()->should_abort(*this);

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if constexpr (PVNode)
        selDepth = std::max<std::uint16_t>(1 + ss->ply, selDepth);

    if (!RootNode)
    {
        // Step 2. Check for stopped search and immediate draw
        if (threads.stop.load(std::memory_order_relaxed) || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return ss->ply >= MAX_PLY && !ss->inCheck
                   ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm])
                   : draw_value(pos.key(), nodes);

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

    (ss + 1)->killerMoves.fill(Move::None());
    (ss + 1)->cutoffCount = 0;

    Square prevDst = (ss - 1)->currentMove.is_ok() ? (ss - 1)->currentMove.dst_sq() : SQ_NONE;

    Piece prevMovedPiece = is_ok(prevDst) ? pos.prev_moved_piece((ss - 1)->currentMove) : NO_PIECE;

    PieceType prevCaptured = type_of(pos.captured_piece());
    assert(prevCaptured != KING);
    bool prevIsQuiet = is_ok(prevDst) && !is_ok(prevCaptured);

    // Step 4. Transposition table lookup.
    Key key           = pos.key();
    auto [ttHit, tte] = tt.probe(key);
    TTEntry const ttd(*tte);
    TTUpdater     ttu(tte, tt, Key16(key), ss->ply);

    ss->ttHit      = ttHit;
    Value ttValue  = ttHit ? value_from_tt(ttd.value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move  ttMove   = RootNode ? rootMoves[curIdx][0] : extract_tt_move(pos, ttd.move());
    ss->ttMove     = ttMove;
    bool ttCapture = ttMove != Move::None() && pos.capture_stage(ttMove);

    // At this point, if excludedMove, skip straight to step 6, static evaluation.
    // However, to save indentation, list the condition in all code between here and there.
    if (excludedMove == Move::None())
        ss->ttPv = PVNode || (ttHit && ttd.is_pv());

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && excludedMove == Move::None() && ttValue != VALUE_NONE
        && ttd.depth() + (ttValue <= beta) > depth
        && (ttd.bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
    {
        // If ttMove fails high, update move sorting heuristics on TT hit (~2 Elo)
        if (ttMove != Move::None() && ttValue >= beta)
        {
            // Bonus for a quiet ttMove (~2 Elo)
            if (!ttCapture)
                update_quiet_stats(*this, pos, ss, ttMove, prevDst, prevMovedPiece,
                                   stat_bonus(depth));

            // Extra penalty for early quiet moves of the previous ply (~1 Elo on STC, ~2 Elo on LTC)
            if (prevIsQuiet && (ss - 1)->moveCount <= 2)
                update_continuation_histories(ss - 1, prevMovedPiece, prevDst,
                                              -stat_malus(1 + depth));
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < -10 + 2 * Position::DrawMoveCount)
        {
            if (ttValue >= beta && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY
                && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY)
                ttValue = (ttd.depth() * ttValue + beta) / (1 + ttd.depth());
            return ttValue;
        }
    }

    Value bestValue = -VALUE_INFINITE, maxValue = +VALUE_INFINITE, value;

    // Step 5. Tablebases probe
    if (!RootNode && excludedMove == Move::None() && tbConfig.cardinality != 0)
    {
        auto pieceCount = pos.count<ALL_PIECE>();

        if (pieceCount <= tbConfig.cardinality
            && (pieceCount < tbConfig.cardinality || depth >= tbConfig.probeDepth)
            && pos.rule50_count() == 0 && !pos.can_castle(ANY_CASTLING))
        {
            Tablebases::ProbeState ps;
            Tablebases::WDLScore   wdl = Tablebases::probe_wdl(pos, &ps);

            // Force check of time on the next occasion
            if (is_main_worker())
                main_manager()->callsCount = 0;

            if (ps != Tablebases::FAIL)
            {
                tbHits.fetch_add(1, std::memory_order_relaxed);

                std::int8_t drawValue = 1 * tbConfig.useRule50;

                Value tbValue = VALUE_TB - ss->ply;

                // Use the range VALUE_TB to VALUE_TB_WIN_IN_MAX_PLY to value
                value = wdl < -drawValue ? -tbValue
                      : wdl > +drawValue ? +tbValue
                                         : 2 * wdl * drawValue;

                Bound bound = wdl < -drawValue ? BOUND_UPPER
                            : wdl > +drawValue ? BOUND_LOWER
                                               : BOUND_EXACT;

                if (bound == BOUND_EXACT || (bound == BOUND_LOWER ? value >= beta : value <= alpha))
                {
                    ttu.update(std::min<Depth>(6 + depth, MAX_PLY - 1), ss->ttPv, bound,
                               Move::None(), value, VALUE_NONE);
                    return value;
                }

                if constexpr (PVNode)
                {
                    if (bound == BOUND_LOWER)
                    {
                        bestValue = value;
                        alpha     = std::max(bestValue, alpha);
                    }
                    else
                        maxValue = value;
                }
            }
        }
    }

    Move  move;
    Value probCutBeta, unadjustedStaticEval, eval;
    bool  improving, worsening;

    // Step 6. Static evaluation of the position
    if (ss->inCheck)
    {
        // Step 7. A small ProbCut idea, when in check (~4 Elo)
        probCutBeta = 388 + beta;
        if (ttValue >= probCutBeta /*ttValue != VALUE_NONE*/
            && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY
            && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY  //
            && ttd.depth() >= depth - 4 && (ttd.bound() & BOUND_LOWER))
            return probCutBeta;

        unadjustedStaticEval = eval = ss->staticEval = VALUE_NONE;

        improving = worsening = false;
        // Skip early pruning when in check
        goto MAIN_MOVES_LOOP;
    }

    if (excludedMove != Move::None())
    {
        unadjustedStaticEval = eval = ss->staticEval;
        // Providing the hint that this node's accumulator will often be used
        // brings significant Elo gain (~13 Elo).
        Eval::NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);
    }
    else if (ttHit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = ttd.eval();
        if (unadjustedStaticEval == VALUE_NONE)
            unadjustedStaticEval =
              evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm]);
        else if constexpr (PVNode)
            Eval::NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);

        eval = ss->staticEval = adjust_static_eval(unadjustedStaticEval, *this, pos);

        // Can ttValue be used as a better position evaluation (~7 Elo)
        if (ttValue != VALUE_NONE /*std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY*/
            && (ttd.bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    }
    else
    {
        unadjustedStaticEval = evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm]);

        eval = ss->staticEval = adjust_static_eval(unadjustedStaticEval, *this, pos);

        // Static evaluation is saved as it was before adjustment by correction history
        ttu.update(DEPTH_NONE, ss->ttPv, BOUND_NONE, Move::None(), VALUE_NONE,
                   unadjustedStaticEval);
    }

    // Use static evaluation difference to improve quiet move ordering (~9 Elo)
    if (prevIsQuiet && !(ss - 1)->inCheck)
    {
        int bonus =
          800 + std::clamp(-10 * ((ss - 1)->staticEval + (ss - 0)->staticEval), -1590, 1371);
        mainHistory[~stm][(ss - 1)->currentMove.org_dst()] << bonus;
        pawnHistory[pawn_index(pos.prev_state()->pawn_key())][prevMovedPiece][prevDst] << bonus / 2;
    }

    // Set up the improving flag, which is true if the current static evaluation is
    // bigger than the previous static evaluation at our turn (if in check at previous
    // move look at static evaluation at the move prior to it and if in check at move
    // prior to it flag is set to true) and is false otherwise.
    // The improving flag is used in various pruning heuristics.
    improving = (ss - 2)->staticEval != VALUE_NONE
                ? ss->staticEval > (ss - 2)->staticEval
                : (ss - 4)->staticEval != VALUE_NONE && ss->staticEval > (ss - 4)->staticEval;

    worsening = ss->staticEval + (ss - 1)->staticEval > 2;

    // Step 8. Razoring (~4 Elo)
    // If eval is really low check with qsearch if it can exceed alpha,
    // if it can't, return a fail low.
    if (eval < alpha - 512 - 293 * depth * depth)
    {
        value = qsearch<false>(pos, ss, alpha - 1, alpha);
        if (value < alpha && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
            return value;
    }

    // Step 9. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 13 && eval >= beta && (ttMove == Move::None() || ttCapture)
        && eval < VALUE_TB_WIN_IN_MAX_PLY && beta > VALUE_TB_LOSS_IN_MAX_PLY
        && eval - futility_margin(depth, cutNode && !ttHit, improving, worsening)
               - (ss - 1)->history / 263
             >= beta)
        return (2 * beta + eval) / 3;

    // Step 10. Null move search with verification search (~35 Elo)
    if (!PVNode && excludedMove == Move::None() && (ss - 1)->currentMove != Move::Null()
        && (ss - 1)->history < 14369 && eval >= beta && ss->staticEval >= beta + 393 - 21 * depth
        && pos.non_pawn_material(stm) && ss->ply >= minNmpPly && beta > VALUE_TB_LOSS_IN_MAX_PLY)
    {
        Value diff = eval - beta;
        assert(diff >= VALUE_ZERO);

        // Null move dynamic reduction based on depth and diff eval
        Depth R = Depth(5 + depth / 3 + std::min(diff / 197, 6));

        ss->currentMove         = Move::Null();
        ss->continuationHistory = &continuationHistory[0][0][NO_PIECE][SQ_ZERO];

        pos.do_null_move(st, tt);

        Value nullValue = -search<false>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);

        pos.undo_null_move();

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && nullValue < VALUE_TB_WIN_IN_MAX_PLY)
        {
            if (minNmpPly != 0 || depth < 16)
                return nullValue;

            assert(minNmpPly == 0);  // Recursive verification is not allowed

            // Do verification search at high depths, with null move pruning
            // disabled until ply exceeds minNmpPly.
            minNmpPly = ss->ply + 3 * (depth - R) / 4;

            value = search<false>(pos, ss, beta - 1, beta, depth - R, false);

            minNmpPly = 0;

            if (value >= beta)
                return nullValue;
        }
    }

    // Step 11. Internal iterative reductions (~9 Elo)
    // For PV nodes without a ttMove, decrease depth.
    // Additionally, if the current position is found in the TT
    // and the stored depth in the TT is greater than or equal to
    // current search depth, we decrease search depth even further.
    if (PVNode && ttMove == Move::None())
        depth -= 3 + (ttHit && ttd.depth() >= depth);

    // Use qsearch if depth <= DEPTH_ZERO.
    if (depth <= DEPTH_ZERO)
        return qsearch<true>(pos, ss, alpha, beta);

    // Decrease depth for cutNodes, if the depth is high enough and
    // if there is a no ttMove or an upper bound, more if there is no ttMove
    if (depth >= 8 && cutNode && (ttMove == Move::None() || ttd.bound() == BOUND_UPPER))
        depth -= 1 + (ttMove == Move::None());

    // Step 12. ProbCut (~10 Elo)
    // If have a good enough capture (or queen promotion) and a reduced search
    // returns a value much above beta, can (almost) safely prune the previous move.
    probCutBeta = 177 + beta - 57 * improving;
    if (
      !PVNode && depth > 3
      && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
      // If the value from the transposition table is lower than probCutBeta, don't attempt probCut
      // there and in further interactions with the transposition table cutoff depth is set to
      // depth - 3 because probCut search has depth set to depth - 4 but also did a move before it
      // So effective depth is equal to depth - 3
      && !(ttValue != VALUE_NONE && ttValue < probCutBeta && ttd.depth() >= depth - 3))
    {
        assert(probCutBeta < +VALUE_INFINITE && probCutBeta > beta);

        Moves probcutCaptureMoves;

        MovePicker mp(pos, ttMove, probCutBeta - ss->staticEval, &captureHistory);
        // Loop through all pseudo-legal moves
        while ((move = mp.next_move()) != Move::None())
        {
            assert(move.is_ok() && pos.pseudo_legal(move));
            assert(pos.capture_stage(move));

            // Check for legality
            if (move == excludedMove || (move != ttMove && !pos.legal(move)))
                continue;

            const Square org = move.org_sq(), dst = move.dst_sq();
            const Piece  movedPiece = pos.piece_on(org);

            // Speculative prefetch as early as possible
            tt.prefetch_entry(pos.move_key(move));

            ss->currentMove         = move;
            ss->continuationHistory = &continuationHistory[ss->inCheck][true][movedPiece][dst];

            nodes.fetch_add(1, std::memory_order_relaxed);
            pos.do_move(move, st);

            // Perform a preliminary qsearch to verify that the move holds
            value = -qsearch<false>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (value >= probCutBeta)
                value =
                  -search<false>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, depth - 4, !cutNode);

            pos.undo_move(move);

            if (value >= probCutBeta)
            {
                update_capture_histories(*this, pos, move, stat_bonus(-2 + depth));
                for (Move pcm : probcutCaptureMoves)
                    update_capture_histories(*this, pos, pcm, -stat_bonus(-3 + depth));

                // Save ProbCut data into transposition table
                ttu.update(-3 + depth, ss->ttPv, BOUND_LOWER, move, value, unadjustedStaticEval);
                return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probCutBeta - beta)
                                                                 : value;
            }

            if (probcutCaptureMoves.size() <= 32)
                probcutCaptureMoves += move;
        }

        Eval::NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);
    }

    if (ttMove == Move::None() && (ttMove = extract_tt_move(pos, tte->move())) != Move::None())
    {
        ss->ttMove = ttMove;
        ttCapture  = ttMove != Move::None() && pos.capture_stage(ttMove);
    }

MAIN_MOVES_LOOP:  // When in check, search starts here

    value = bestValue;

    Move bestMove = Move::None();

    std::uint8_t moveCount = 0;

    Moves captureMoves, quietMoves;

    Value singularValue   = +VALUE_INFINITE;
    bool  singularFailLow = false;

    const PieceDstHistory* contHistory[6]{(ss - 1)->continuationHistory,
                                          (ss - 2)->continuationHistory,
                                          (ss - 3)->continuationHistory,
                                          (ss - 4)->continuationHistory,
                                          nullptr,
                                          (ss - 6)->continuationHistory};

    Move counterMove = Move::None();
    if (is_ok(prevDst))
    {
        counterMove = counterMoves[prevMovedPiece][prevDst];
        if (counterMove == Move::None() && type_of(prevMovedPiece) == EX_PIECE)
            counterMove = counterMoves[make_piece(~stm, KING)][prevDst];
    }

    MovePicker mp(pos, ttMove, depth, &mainHistory, &captureHistory, contHistory, &pawnHistory,
                  ss->killerMoves, counterMove);
    // Step 13. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None())
    {
        assert(move.is_ok() && pos.pseudo_legal(move));

        // Check for legality
        if (
          move == excludedMove
          // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
          // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
          || (RootNode ? !rootMoves.contains(curIdx, lstIdx, move)
                       : move != ttMove && !pos.legal(move)))
            continue;

        ss->moveCount = ++moveCount;

        if (RootNode && is_main_worker() && main_manager()->elapsed() > 3000
            && !options["ReportMinimal"])
        {
            main_manager()->updateContext.onUpdateIter(
              {rootDepth, move, std::uint16_t(moveCount + curIdx)});
        }

        if constexpr (PVNode)
            (ss + 1)->pv.clear();

        const Square org = move.org_sq(), dst = move.dst_sq();
        const Piece  movedPiece   = pos.piece_on(org);
        const Piece  exMovedPiece = pos.ex_moved_piece(move);

        const bool givesCheck = pos.gives_check(move);
        const bool captures   = pos.capture_stage(move);

        // Calculate new depth for this move
        Depth newDepth = Depth(depth - 1);

        Value delta      = beta - alpha;
        int   deltaRatio = 746 * delta / rootDelta;

        Depth red = reduction(depth, moveCount, deltaRatio, improving);

        // Step 14. Pruning at shallow depth (~120 Elo).
        // Depth conditions are important for mate finding.
        if (!RootNode && pos.non_pawn_material(stm) && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
        {
            // Skip quiet moves if moveCount exceeds our Futility Move Count threshold (~8 Elo)
            mp.pickQuiets = mp.pickQuiets
                         && moveCount < futility_move_count(depth, improving)
                                          - (singularFailLow && singularValue < alpha - 50);

            // Reduced depth of the next LMR search
            Depth lmrDepth = Depth(newDepth - red);

            if (captures || givesCheck)
            {
                Piece capturedPiece = pos.captured_piece(move);
                int   captureHist   = captureHistory[movedPiece][dst][type_of(capturedPiece)];

                // Futility pruning for captures (~2 Elo)
                if (lmrDepth < 7 && !givesCheck && !ss->inCheck)
                {
                    Value futilityValue = 294 + ss->staticEval + 246 * lmrDepth
                                        + PieceValue[capturedPiece] + captureHist / 7;
                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures and checks (~11 Elo)
                int seeHist = std::clamp(captureHist / 32, -180 * depth, +163 * depth);
                if (!pos.see_ge(move, -seeHist - 163 * depth))
                    continue;
            }
            else
            {
                int history = (*contHistory[0])[exMovedPiece][dst]  //
                            + (*contHistory[1])[exMovedPiece][dst]  //
                            + pawnHistory[pawn_index(pos.pawn_key())][exMovedPiece][dst];

                // Continuation history based pruning (~2 Elo)
                if (lmrDepth < 6 && history < -3899 * depth)
                    continue;

                history += 2 * mainHistory[stm][move.org_dst()];

                lmrDepth += Depth(history / 4040);

                Value futilityValue = 56 + ss->staticEval                     //
                                    + 79 * (bestValue < ss->staticEval - 51)  //
                                    + 140 * lmrDepth;

                // Futility pruning: parent node (~13 Elo)
                if (lmrDepth < 12 && !ss->inCheck && futilityValue <= alpha)
                {
                    if (bestValue <= futilityValue  //
                        && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY
                        && std::abs(futilityValue) < VALUE_TB_WIN_IN_MAX_PLY)
                        bestValue = (3 * futilityValue + bestValue) / 4;
                    continue;
                }

                lmrDepth = std::max<Depth>(lmrDepth, DEPTH_ZERO);

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, -24 * lmrDepth * lmrDepth))
                    continue;
            }
        }

        Depth extension = DEPTH_ZERO;
        // Step 15. Extensions (~100 Elo)
        // Take care to not overdo to avoid search getting stuck.
        if (ss->ply < 2 * rootDepth)
        {
            // Singular extension search (~94 Elo). If all moves but one fail low on a
            // search of (alpha-s, beta-s), and just one fails high on (alpha, beta),
            // then that move is singular and should be extended. To verify this do a
            // reduced search on the position excluding the ttMove and if the result
            // is lower than ttValue minus a margin, then will extend the ttMove.
            // Recursive singular search is avoided.

            // Note: the depth margin and singularBeta margin are known for having non-linear
            // scaling. Their values are optimized to time controls of 180+1.8 and longer
            // so changing them requires tests at these types of time controls.
            // Generally, higher singularBeta (i.e closer to ttValue) and
            // lower extension margins scales well.
            if (!RootNode && move == ttMove
                && excludedMove == Move::None() /*ttValue != VALUE_NONE*/
                && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && ttd.depth() >= depth - 3
                && depth >= 4 - (completedDepth > 35) + ss->ttPv && (ttd.bound() & BOUND_LOWER))
            {
                Value singularBeta  = ttValue - (52 + 80 * (!PVNode && ss->ttPv)) * depth / 64;
                Depth singularDepth = newDepth / 2;

                // ss->currentMove = Move::None();
                // clang-format off
                singularValue   = search<false>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode, ttMove);
                singularFailLow = singularValue < singularBeta;

                if (singularFailLow)
                {
                    int doubleMargin =   0 + 290 * PVNode - 200 * !ttCapture;
                    int tripleMargin = 107 + 247 * PVNode - 278 * !ttCapture + 99 * ss->ttPv;

                    extension = 1 + (singularValue < singularBeta - doubleMargin)
                                  + (singularValue < singularBeta - tripleMargin);

                    depth += (!PVNode && depth < 18);
                }
                // clang-format on

                // Multi-cut pruning
                // If the ttMove is assumed to fail high based on the bound of the TT entry, and
                // if after excluding the ttMove with a reduced search fail high over the original beta,
                // assume this expected cut-node is not singular (multiple moves fail high),
                // and can prune the whole subtree by returning a soft-bound.
                else if (singularValue >= beta)
                    return std::abs(singularValue) < VALUE_TB_WIN_IN_MAX_PLY ? singularValue
                                                                             : singularBeta;

                // Negative extensions
                // If other moves failed high over (ttValue - margin) without the ttMove on a reduced search,
                // but cannot do multi-cut because (ttValue - margin) is lower than the original beta,
                // do not know if the ttMove is singular or can do a multi-cut,
                // so reduce the ttMove in favor of other moves based on some conditions:

                // If the ttMove is assumed to fail high over current beta (~7 Elo)
                else if (ttValue >= beta)
                    extension = -3;

                // If on a cutNode but the ttMove is not assumed to fail high over current beta (~1 Elo)
                else if (cutNode)
                    extension = -2;
            }
            // Extension for capturing the previous moved piece (~0 Elo on STC, ~1 Elo on LTC)
            else if (PVNode && dst == prevDst
                     && captureHistory[movedPiece][dst][type_of(pos.captured_piece(move))] > 3922)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;

        // Speculative prefetch as early as possible
        tt.prefetch_entry(pos.move_key(move));

        ss->history = 2 * mainHistory[stm][move.org_dst()]  //
                    + (*contHistory[0])[exMovedPiece][dst]  //
                    + (*contHistory[1])[exMovedPiece][dst] - 4747;

        // Update the current move (this must be done after singular extension search)
        ss->currentMove         = move;
        ss->continuationHistory = &continuationHistory[ss->inCheck][captures][exMovedPiece][dst];

        [[maybe_unused]] std::uint64_t initialNodes;
        if (RootNode)
            initialNodes = std::uint64_t(nodes);

        // Step 16. Make the move
        nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);

        // These reduction adjustments have proven non-linear scaling.
        // They are optimized to time controls of 180 + 1.8 and longer
        // so changing them or adding conditions that are similar
        // requires tests at these types of time controls.

        // Decrease reduction if position is or has been on the PV (~7 Elo)
        if (ss->ttPv)
            red -= 1 + (ttValue != VALUE_NONE && ttValue > alpha) + (ttHit && ttd.depth() >= depth)
                 - (PVNode && ttHit && ttValue < alpha && ttd.depth() >= depth);

        // Decrease reduction for PvNodes (~0 Elo on STC, ~2 Elo on LTC)
        if constexpr (PVNode)
            red--;

        // These reduction adjustments have no proven non-linear scaling.

        // Increase reduction for cut nodes (~4 Elo)
        if (cutNode)
            red += 2
                 - (ss->ttPv && ttHit && ttd.depth() >= depth)
                 // At cut nodes which are not a former PV node
                 + (!ss->ttPv && move != ttMove && move != ss->killerMoves[0]);

        // Increase reduction if ttMove is a capture (~3 Elo)
        if (ttCapture)
            red++;

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if (ss->cutoffCount > 3)
            red += 1 + !(PVNode || cutNode);

        // Reduction for first picked move (ttMove) (~3 Elo)
        // reduce reduction but never allow it to go below 0
        else if (move == ttMove)
            red = std::max<Depth>(red - 2, 0);

        // Decrease/increase reduction for moves with a good/bad history (~8 Elo)
        red -= ss->history / 11125;

        // Step 17. Late moves reduction / extension (LMR, ~117 Elo)
        if (depth >= 2 && moveCount > 1 + RootNode)
        {
            // In general, want to cap the LMR depth search at newDepth, but when
            // reduction is negative, allow this move a limited search extension
            // beyond the first move depth.
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth d = std::max<Depth>(std::min<Depth>(newDepth - red, 1 + newDepth), 1);

            value = -search<false>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result
                // was good enough search deeper, if it was bad enough search shallower.
                newDepth += (value > bestValue + 35 + 2 * newDepth)  // (~1 Elo)
                          - (value < bestValue + newDepth);          // (~2 Elo)

                if (d < newDepth)
                    value = -search<false>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                // Post LMR continuation history updates (~1 Elo)
                int bonus = value <= alpha ? -stat_malus(newDepth)
                          : value >= beta  ? +stat_bonus(newDepth)
                                           : 0;

                update_continuation_histories(ss, exMovedPiece, dst, bonus);
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present (~6 Elo)
            if (ttMove == Move::None())
                red += 2;

            // If expected reduction is high, reduce search depth here (~9 Elo)
            value =
              -search<false>(pos, ss + 1, -(alpha + 1), -alpha, newDepth - (red > 3), !cutNode);
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if constexpr (PVNode)
            if (moveCount == 1 || value > alpha)
            {
                (ss + 1)->pv.clear();

                value = -search<true>(pos, ss + 1, -beta, -alpha, newDepth, false);
            }

        // Step 19. Undo move
        pos.undo_move(move);

        assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

        // Step 20. Check for a new best move
        // Finished searching the move. If a stop occurred, the return value of
        // the search cannot be trusted, and return immediately without
        // updating best move, PV and TT.
        if (threads.stop.load(std::memory_order_relaxed))
            return VALUE_ZERO;

        if (RootNode)
        {
            RootMove& rm = *rootMoves.find(move);

            rm.avgValue = rm.avgValue != -VALUE_INFINITE ? (rm.avgValue + value) / 2 : value;
            rm.nodes += nodes - initialNodes;

            // PV move or new best move?
            if (moveCount == 1 || value > alpha)
            {
                rm.curValue = rm.uciValue = value;
                rm.selDepth               = selDepth;
                rm.lowerBound = rm.upperBound = false;

                if (value >= beta)
                {
                    rm.lowerBound = true;
                    rm.uciValue   = beta;
                }
                else if (value <= alpha)
                {
                    rm.upperBound = true;
                    rm.uciValue   = alpha;
                }

                rm.resize(1);
                rm.append((ss + 1)->pv);

                // Record how often the best move has been changed in each iteration.
                // This information is used for time management.
                // In MultiPV mode, must take care to only do this for the first PV line.
                if (curIdx == 0 && moveCount > 1 && limits.use_time_manager())
                    ++bestMoveChange;
            }
            else
                // All other moves but the PV, are set to the lowest value, this
                // is not a problem when sorting because the sort is stable and the
                // move position in the list is preserved - just the PV is pushed up.
                rm.curValue = -VALUE_INFINITE;
        }

        // In case we have an alternative move equal in eval to the current bestmove,
        // promote it to bestmove by pretending it just exceeds alpha (but not beta).
        int inc = value == bestValue && (std::uint64_t(nodes) & 0xF) == 0
               && 2 + 1.03125 * ss->ply >= rootDepth
               && 1 + std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY;

        if (bestValue < value + inc)
        {
            bestValue = value;

            if (alpha < value + inc)
            {
                bestMove = move;

                if (PVNode && !RootNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                {
                    if (!RootNode)
                        (ss - 1)->cutoffCount += 1 + (ttMove == Move::None()) - (extension >= 2);
                    break;  // Fail-high
                }

                // Reduce other moves if have found at least one score improvement (~2 Elo)
                if (depth > 2 && depth < 13 && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
                    depth -= 2;

                assert(depth > DEPTH_ZERO);
                alpha = value;  // Update alpha! Always alpha < beta
            }
        }

        // If the move is worse than some previously searched move,
        // remember it, to update its stats later.
        if (move != bestMove && moveCount <= 32)
        {
            if (captures)
                captureMoves += move;
            else
                quietMoves += move;
        }
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves,
    // it must be a mate or a stalemate.
    // If in a singular extension search then return a fail low score.
    assert(moveCount != 0 || !ss->inCheck || excludedMove != Move::None()
           || MoveList<LEGAL>(pos).size() == 0);

    // Adjust best value for fail high cases at non-pv nodes
    if (!PVNode && bestValue >= beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY && std::abs(alpha) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (depth * bestValue + beta) / (1 + depth);

    if (moveCount == 0)
        bestValue = excludedMove != Move::None() ? alpha
                  : ss->inCheck                  ? mated_in(ss->ply)
                                                 : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha update the stats of searched moves
    else if (bestMove != Move::None())
        update_stats(*this, pos, ss, bestMove, depth, bestValue > beta + 164, prevIsQuiet, prevDst,
                     prevMovedPiece, quietMoves, captureMoves);

    // Bonus for prior counterMove that caused the fail low
    else if (prevIsQuiet)
    {
        int bonusMul = 113 * (depth > 5) + 118 * (PVNode || cutNode)
                     + 119 * ((ss - 1)->moveCount > 8)  //
                     + 64 * (!(ss - 0)->inCheck && bestValue <= +(ss - 0)->staticEval - 107)
                     + 147 * (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->staticEval - 75);
        // proportional to "how much damage have to undo"
        if ((ss - 1)->history < -8000)
            bonusMul += std::clamp(-(ss - 1)->history / 100, 0, 250);

        int bonus = bonusMul * stat_bonus(depth);
        mainHistory[~stm][(ss - 1)->currentMove.org_dst()] << bonus / 200;
        pawnHistory[pawn_index(pos.prev_state()->pawn_key())][prevMovedPiece][prevDst]  //
          << bonus / 25;
        update_continuation_histories(ss - 1, prevMovedPiece, prevDst, bonus / 100);
    }

    if constexpr (PVNode)
        // If the child can't find the win (mostly in qsearch), set back this node to the TB loss value probed.
        if (bestValue > maxValue)
        {
            assert(maxValue != +VALUE_INFINITE);
            bestValue = maxValue;
            (ss + 1)->pv.clear();
        }

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~7 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    // Static evaluation is saved as it was before correction history
    if ((!RootNode || curIdx == 0) && excludedMove == Move::None())
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
        int bonus = std::clamp((bestValue - ss->staticEval) * depth / 8,
                               -CORRECTION_HISTORY_LIMIT / 4, +CORRECTION_HISTORY_LIMIT / 4);
        correctionHistory[stm][correction_index(pos.pawn_key())] << bonus;
    }

    assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);

    return bestValue;
}

// Quiescence search function, which is called by the main search function
// with DEPTH_ZERO, or recursively with further decreasing depth per call.
// With depth <= DEPTH_ZERO, should be using static evaluation only, but
// tactical moves may confuse the static evaluation. To fight this horizon effect,
// implemented this qsearch of tactical moves only. (~155 Elo)
template<bool PVNode>
Value Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) noexcept {
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (alpha == beta - 1));
    assert(depth <= DEPTH_ZERO);

    // Check if have an upcoming move that draws by repetition, or
    // if the opponent had an alternative move earlier to this position. (~1 Elo)
    // The VALUE_TB_LOSS_IN_MAX_PLY check is necessary as long as has_game_cycle is approximate.
    if (alpha < VALUE_DRAW && alpha > VALUE_TB_LOSS_IN_MAX_PLY && pos.has_game_cycle(ss->ply))
    {
        alpha = draw_value(pos.key(), nodes);
        if (alpha >= beta)
            return alpha;
    }

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    // Step 1. Initialize node
    if constexpr (PVNode)
        (ss + 1)->pv.clear();
    ss->inCheck = pos.checkers();

    Color stm = pos.side_to_move();

    if (is_main_worker())
        main_manager()->callsCount--;

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if constexpr (PVNode)
        selDepth = std::max<std::uint16_t>(1 + ss->ply, selDepth);

    // Step 2. Check for an immediate draw or maximum ply reached
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY)
        return ss->ply >= MAX_PLY && !ss->inCheck
               ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm])
               : draw_value(pos.key(), nodes);

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // If in check, search all evasions and thus store DEPTH_QS_CHECKS.
    Depth qsDepth = depth == DEPTH_QS_CHECKS || ss->inCheck ? DEPTH_QS_CHECKS : DEPTH_QS_NORMAL;

    // Step 3. Transposition table lookup
    Key key           = pos.key();
    auto [ttHit, tte] = tt.probe(key);
    TTEntry const ttd(*tte);
    TTUpdater     ttu(tte, tt, Key16(key), ss->ply);

    Value ttValue = ttHit ? value_from_tt(ttd.value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move  ttMove  = extract_tt_move(pos, ttd.move());
    bool  ttPv    = ttHit && ttd.is_pv();

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && ttValue != VALUE_NONE && ttd.depth() >= qsDepth
        && (ttd.bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    Square prevDst = (ss - 1)->currentMove.is_ok() ? (ss - 1)->currentMove.dst_sq() : SQ_NONE;

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
              evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm]);

        bestValue = ss->staticEval = adjust_static_eval(unadjustedStaticEval, *this, pos);

        // Can ttValue be used as a better position evaluation (~13 Elo)
        if (std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY /*ttValue != VALUE_NONE*/
            && (ttd.bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
            bestValue = ttValue;
    }
    else
    {
        // In case of null move search, use previous staticEval with a opposite sign
        unadjustedStaticEval = (ss - 1)->currentMove != Move::Null()
                               ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm])
                               : -(ss - 1)->staticEval;

        bestValue = ss->staticEval = adjust_static_eval(unadjustedStaticEval, *this, pos);
    }

    // Stand pat. Return immediately if bestValue is at least beta
    if (bestValue >= beta)
    {
        if (!PVNode && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
            bestValue = (3 * bestValue + beta) / 4;

        if (!ttHit)
            ttu.update(DEPTH_NONE, false, BOUND_LOWER, Move::None(), bestValue,
                       unadjustedStaticEval);

        return bestValue;
    }

    alpha = std::max(bestValue, alpha);

    futilityBase = ss->staticEval + 294;

QS_MOVES_LOOP:
    // Initialize a MovePicker object for the current position, prepare to search the moves.
    // Because the depth is <= DEPTH_ZERO here, only captures, queen promotions,
    // and other checks (only if depth == DEPTH_QS_CHECKS) will be generated.
    Value        value;
    Move         bestMove  = Move::None(), move;
    std::uint8_t moveCount = 0;

    const PieceDstHistory* contHistory[2]{(ss - 1)->continuationHistory,
                                          (ss - 2)->continuationHistory};

    MovePicker mp(pos, ttMove, depth, &mainHistory, &captureHistory, contHistory, &pawnHistory);
    // Step 5. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None())
    {
        assert(move.is_ok() && pos.pseudo_legal(move));

        // Check for legality
        if (move != ttMove && !pos.legal(move))
            continue;

        ++moveCount;

        const Square /*org = move.org_sq(),*/ dst = move.dst_sq();
        // const Piece  movedPiece  = pos.piece_on(org);
        const Piece exMovedPiece = pos.ex_moved_piece(move);

        const bool givesCheck = pos.gives_check(move);
        const bool captures   = pos.capture_stage(move);

        // Step 6. Pruning
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(stm))
        {
            // Futility pruning and moveCount pruning (~10 Elo)
            if (!givesCheck && dst != prevDst && futilityBase > VALUE_TB_LOSS_IN_MAX_PLY
                && move.type_of() != PROMOTION)
            {
                if (moveCount > 2)
                    continue;

                Value futilityValue = futilityBase + PieceValue[pos.captured_piece(move)];

                // Static evaluation + value of piece going to captured is much lower than alpha prune this move. (~2 Elo)
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(futilityValue, bestValue);
                    continue;
                }

                // Static evaluation is much lower than alpha and move is not winning material prune this move. (~2 Elo)
                if (futilityBase <= alpha && !pos.see_ge(move, 1))
                {
                    bestValue = std::max(futilityBase, bestValue);
                    continue;
                }

                // Static exchange evaluation is much worse than what is needed to not fall below alpha prune this move. (~1 Elo)
                if (futilityBase > alpha && !pos.see_ge(move, -4 * (futilityBase - alpha)))
                {
                    bestValue = alpha;
                    continue;
                }
            }

            // Continuation history based pruning (~3 Elo)
            if (!captures
                && ((*contHistory[0])[exMovedPiece][dst]  //
                    + (*contHistory[1])[exMovedPiece][dst]
                    + pawnHistory[pawn_index(pos.pawn_key())][exMovedPiece][dst])
                     <= 4452)
                continue;

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (!pos.see_ge(move, -74))
                continue;
        }

        // Speculative prefetch as early as possible
        tt.prefetch_entry(pos.move_key(move));

        // Update the current move
        ss->currentMove         = move;
        ss->continuationHistory = &continuationHistory[ss->inCheck][captures][exMovedPiece][dst];

        // Step 7. Make and search the move
        nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);

        value = -qsearch<PVNode>(pos, ss + 1, -beta, -alpha, depth - 1);

        pos.undo_move(move);

        assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

        // Step 8. Check for a new best move
        if (bestValue < value)
        {
            bestValue = value;

            if (alpha < value)
            {
                bestMove = move;

                if (PVNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                    break;  // Fail-high

                alpha = value;  // Update alpha! Always alpha < beta
            }
        }
    }

    // Step 9. Check for mate
    // All legal moves have been searched.
    // A special case: if in check and no legal moves were found, it is checkmate.
    if (ss->inCheck && bestValue == -VALUE_INFINITE)
    {
        assert(MoveList<LEGAL>(pos).size() == 0);
        return mated_in(ss->ply);  // Plies to mate from the root
    }

    if (bestValue > beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (3 * bestValue + beta) / 4;

    // Save gathered info in transposition table
    // Static evaluation is saved as it was before adjustment by correction history
    Bound bound = bestValue >= beta ? BOUND_LOWER : BOUND_UPPER;
    ttu.update(qsDepth, ttPv, bound, bestMove, bestValue, unadjustedStaticEval);

    assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);

    return bestValue;
}

Move Worker::extract_tt_move(const Position& pos, Move ttMove) noexcept {
    bool ttmFound = ttMove != Move::None() && pos.pseudo_legal(ttMove) && pos.legal(ttMove);
    int  rule50   = pos.rule50_count();
    while (!ttmFound && rule50 >= 14)
    {
        rule50 -= 8;
        ttMove   = tt.probe(pos.key(rule50 - pos.rule50_count())).tte->move();
        ttmFound = ttMove != Move::None() && pos.pseudo_legal(ttMove) && pos.legal(ttMove);
    }
    return ttmFound ? ttMove : Move::None();
}

// Called in case have no ponder move before exiting the search,
// for instance, in case stop the search during a fail high at root.
// Try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' have nothing to think about.
bool Worker::extract_ponder_move() noexcept {
    assert(rootMoves.front().size() == 1 && rootMoves.front()[0] != Move::None());

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    Move bm = rootMoves.front()[0];
    rootPos.do_move(bm, st);

    const MoveList<LEGAL> legalMoves(rootPos);
    if (legalMoves.size() != 0)
    {
        Move pm;

        pm = extract_tt_move(rootPos, tt.probe(rootPos.key()).tte->move());
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
                std::srand(std::time(nullptr));
                pm = *(legalMoves.begin() + (std::rand() % legalMoves.size()));
            }
        }
        rootMoves.front() += pm;
    }

    rootPos.undo_move(bm);
    return rootMoves.front().size() == 2;
}

// Used to print debug info and, more importantly,
// to detect when out of available time and thus stop the search.
void MainSearchManager::should_abort(const Worker& worker) noexcept {
    if (--callsCount > 0)
        return;
    callsCount = worker.limits.hitRate;

    TimePoint elapsedTime = elapsed(worker);

#if !defined(NDEBUG)
    static TimePoint debugTime = now();
    if (TimePoint curTime = worker.limits.initialTime + elapsedTime; curTime - debugTime > 1000)
    {
        debugTime = curTime;
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
      && ((worker.limits.use_time_manager() && (stopPonderhit || elapsedTime > timeManager.maximum()))
       || (worker.limits.moveTime != 0 && elapsedTime >= worker.limits.moveTime)
       || (worker.limits.nodes != 0 && worker.threads.nodes() >= worker.limits.nodes)))
        worker.threads.stop = worker.threads.abort = true;
    // clang-format on
}

void MainSearchManager::init(std::uint16_t threadCount) noexcept {
    assert(threadCount != 0);
    timeManager.clear();
    prevBestCurValue  = VALUE_ZERO;
    prevBestAvgValue  = VALUE_ZERO;
    prevTimeReduction = 1.0;

    auto threadReduction = 19.26 + 0.50 * std::log(threadCount);
    reductions[0]        = 0;
    for (std::uint16_t i = 1; i < reductions.size(); ++i)
        reductions[i] = short(threadReduction * std::log(i));
}

void MainSearchManager::load_book(const std::string& bookFile) const noexcept {
    Polybook.init(bookFile);
}

// Returns the actual time elapsed since the start of the search.
// This function is intended for use only when printing PV outputs,
// and not used for making decisions within the search algorithm itself.
TimePoint MainSearchManager::elapsed() const noexcept { return timeManager.elapsed(); }
// Returns the time elapsed since the search started.
// If the 'NodesTime' option is enabled, return the count of nodes searched instead.
// This function is called to check whether the search should be stopped
// based on predefined thresholds like time limits or nodes searched.
TimePoint MainSearchManager::elapsed(const Worker& worker) const noexcept {
    return timeManager.elapsed([&worker]() { return worker.threads.nodes(); });
}

void MainSearchManager::info_pv(const Search::Worker& worker, Depth depth) const noexcept {

    const auto& rootPos   = worker.rootPos;
    const auto& rootMoves = worker.rootMoves;

    TimePoint     time     = std::max(elapsed(), 1LL);
    std::uint64_t nodes    = worker.threads.nodes();
    std::uint16_t hashfull = worker.tt.hashfull();
    std::uint64_t tbHits   = worker.threads.tbHits() + worker.tbConfig.rootInTB * rootMoves.size();
    bool          showWDL  = worker.options["UCI_ShowWDL"];

    for (std::uint8_t i = 0; i < worker.multiPV; ++i)
    {
        bool updated = rootMoves[i].curValue != -VALUE_INFINITE;

        if (i != 0 && depth == 1 && !updated)
            continue;

        Depth d = updated ? depth : std::max<Depth>(depth - 1, 1);
        Value v = updated ? rootMoves[i].uciValue : rootMoves[i].preValue;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = worker.tbConfig.rootInTB && std::abs(v) <= VALUE_TB;
        if (tb)
            v = rootMoves[i].tbValue;

        FullInfo info(rootPos, rootMoves[i]);
        info.depth   = d;
        info.value   = v;
        info.multiPV = 1 + i;
        // tablebase- and previous-scores are exact
        info.showBound = i == worker.curIdx && updated && !tb;
        info.showWDL   = showWDL;
        info.time      = time;
        info.nodes     = nodes;
        info.hashfull  = hashfull;
        info.tbHits    = tbHits;

        updateContext.onUpdateFull(info);
    }
}

void Skill::init(const Options& options) noexcept {

    std::uint16_t uciELO = options["UCI_LimitStrength"] ? options["UCI_ELO"] : 0;
    if (uciELO != 0)
    {
        double e = double(uciELO - MinELO) / (MaxELO - MinELO);
        level =
          std::clamp((((37.2473 * e - 40.8525) * e + 22.2943) * e - 0.311438), 0.0, MaxLevel - 0.1);
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
        Value  maxValue = rootMoves[0].curValue;
        Value  maxDiff  = std::min(maxValue - rootMoves[multiPV - 1].curValue, +VALUE_PAWN);
        double weakness = 2.0 * (3.0 * MaxLevel - level);

        Value bestValue = -VALUE_INFINITE;
        // Choose best move. For each move value add two terms, both dependent on weakness.
        // One is deterministic and bigger for weaker levels, and one is random.
        // Then choose the move with the resulting highest value.
        for (std::uint8_t i = 0; i < multiPV; ++i)
        {
            Value value = rootMoves[i].curValue
                        // This is magic formula for Push
                        + int(weakness * (maxValue - rootMoves[i].curValue)
                              + maxDiff * (rng.rand<std::uint32_t>() % int(weakness)))
                            / 128;

            if (bestValue <= value)
            {
                bestValue = value;
                bestMove  = rootMoves[i][0];
            }
        }
    }

    return bestMove;
}

}  // namespace Search
}  // namespace DON
