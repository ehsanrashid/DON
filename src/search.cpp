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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <utility>

#include "evaluate.h"
#include "movegen.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "ucioption.h"
#include "nnue/network.h"
#include "nnue/nnue_common.h"
#include "nnue/nnue_misc.h"

namespace DON {

using Eval::evaluate;

namespace Search {

namespace {

constexpr std::array<double, 10> EvalReduction{0.981, 0.956, 0.895, 0.949, 0.913,
                                               0.942, 0.933, 0.890, 0.984, 0.941};

// Reductions lookup table initialized at startup
std::array<std::uint32_t, MAX_MOVES> reductions;  // [depth or moveCount]
constexpr Depth
reduction(Depth depth, std::uint8_t moveCount, std::uint16_t deltaRatio, bool improving) noexcept {
    auto reductionScale = reductions[depth] * reductions[moveCount];
    return (1318 + reductionScale - deltaRatio) / 1024 + (!improving && reductionScale > 1066);
}

// Futility margin
constexpr Value
futility_margin(Depth depth, bool cutNodeNottHit, bool improving, bool worsening) noexcept {
    Value futilityMult = 126 - 46 * cutNodeNottHit;
    return depth * futilityMult - (58 * futilityMult / 32) * improving
         - ((323 + 52 * improving) * futilityMult / 1024) * worsening;
}

constexpr std::uint16_t futility_move_count(Depth depth, bool improving) noexcept {
    return (3 + depth * depth) >> (1 - improving);
}

// History and stats update bonus, based on depth
constexpr int stat_bonus(Depth depth) noexcept { return std::clamp(208 * depth - 297, 16, 1406); }

// History and stats update malus, based on depth
constexpr int stat_malus(Depth depth) noexcept { return std::clamp(520 * depth - 312, 0, 1479); }

// Add a small random value to draw evaluation to avoid 3-fold blindness
constexpr Value draw_value(std::uint64_t nodes) noexcept { return 1 - (nodes & 2); }

// Add correctionHistory value to raw staticEval and guarantee evaluation does not hit the tablebase range
Value to_corrected_static_eval(Value v, const Worker& worker, const Position& pos) noexcept {
    auto chv = worker.correctionHistory[pos.side_to_move()][correction_index(pos.pawn_key())];
    v += chv * std::abs(chv) / 7350;
    return std::clamp(v, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}

// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
constexpr Value value_to_tt(Value v, std::int16_t ply) noexcept {
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

// Adds current move and appends subPv[]
constexpr void update_pv(Move* mainPv, Move move, const Move* subPv) noexcept {
    *mainPv++ = move;
    if (subPv)
        while (*subPv)
            *mainPv++ = *subPv++;
    *mainPv = Move::None();
}

// Updates histories of the move pairs formed
// by moves at ply -1, -2, -3, -4, and -6 with current move.
void update_continuation_histories(Stack* ss, Piece pc, Square dst, int bonus) noexcept {
    assert(is_ok(pc) && is_ok(dst));

    for (std::uint8_t i : {1, 2, 3, 4, 6})
    {
        // Only update the first 2 continuation histories if in check
        if (i > 2 && ss->inCheck)
            break;
        if (((ss - i)->currentMove).is_ok())
            (*(ss - i)->continuationHistory)[pc][dst] << bonus / (1 + 3 * (i == 3));
    }
}

void update_refutations(
  Worker& worker, const Position& pos, Stack* ss, Move move, Square prevDst) noexcept {
    // Update killerMoves
    if (ss->killerMoves[0] != move)
    {
        ss->killerMoves[1] = ss->killerMoves[0];
        ss->killerMoves[0] = move;
    }
    // Update counterMoves history
    if (is_ok(prevDst))
        worker.counterMoves[pos.piece_on(prevDst)][prevDst] = move;
}

void update_quiet_histories(
  Worker& worker, const Position& pos, Stack* ss, Move move, int bonus) noexcept {

    worker.mainHistory[pos.side_to_move()][move.org_dst()] << bonus;
    worker.pawnHistory[pawn_index(pos.pawn_key())][pos.moved_piece(move)][move.dst_sq()] << bonus;

    update_continuation_histories(ss, pos.moved_piece(move), move.dst_sq(), bonus);
}

// Updates move sorting heuristics
void update_quiet_stats(
  Worker& worker, const Position& pos, Stack* ss, Move move, Square prevDst, int bonus) noexcept {

    update_refutations(worker, pos, ss, move, prevDst);
    update_quiet_histories(worker, pos, ss, move, bonus);
}

void update_capture_histories(Worker& worker, const Position& pos, Move move, int bonus) noexcept {

    worker.captureHistory[pos.moved_piece(move)][move.dst_sq()][type_of(pos.captured_piece(move))]
      << bonus;
}

// Updates stats at the end of search() when a bestMove is found
void update_all_stats(Worker&         worker,
                      const Position& pos,
                      Stack*          ss,
                      Move            bestMove,
                      Depth           depth,
                      bool            bonusMore,
                      Square          prevDst,
                      PieceType       prevCaptured,
                      Move*           quietMoves,
                      std::uint8_t    quietCount,
                      Move*           captureMoves,
                      std::uint8_t    captureCount) noexcept {
    int malus = stat_malus(depth);

    if (!pos.capture_stage(bestMove))
    {
        update_quiet_stats(worker, pos, ss, bestMove, prevDst, stat_bonus(depth + bonusMore));

        // Decrease stats for all non-best quiet moves
        for (std::uint8_t i = 0; i < quietCount; ++i)
            update_quiet_histories(worker, pos, ss, quietMoves[i], -malus);
    }
    else
    {
        // Increase stats for the best move in case it was a capture move
        update_capture_histories(worker, pos, bestMove, stat_bonus(depth + 1));
    }

    // Extra penalty for a quiet early move that was not a TT move or
    // main killer move in the previous ply when it gets refuted.
    if (is_ok(prevDst) && !is_ok(prevCaptured)
        && ((ss - 1)->moveCount == 1 + (ss - 1)->ttHit
            || (ss - 1)->currentMove == (ss - 1)->killerMoves[0]))
        update_continuation_histories(ss - 1, pos.piece_on(prevDst), prevDst, -malus);

    // Decrease stats for all non-best capture moves
    for (std::uint8_t i = 0; i < captureCount; ++i)
        update_capture_histories(worker, pos, captureMoves[i], -malus);
}

}  // namespace

Worker::Worker(std::uint16_t      threadId,
               const SharedState& sharedState,
               ISearchManagerPtr  searchManager) noexcept :
    // Unpack the SharedState struct into member variables
    threadIdx(threadId),
    manager(std::move(searchManager)),
    options(sharedState.options),
    networks(sharedState.networks),
    threads(sharedState.threads),
    tt(sharedState.tt),
    accCaches(networks) {
    clear();
}

void Worker::clear() noexcept {
    counterMoves.fill(Move::None());
    mainHistory.fill(0);
    captureHistory.fill(0);
    pawnHistory.fill(0);
    correctionHistory.fill(0);

    for (bool inCheck : {false, true})
        for (bool isCapture : {false, true})
            for (auto& pieceTo : continuationHistory[inCheck][isCapture])
                for (auto& h : pieceTo)
                    h->fill(-60);

    optimism.fill(VALUE_ZERO);
    accCaches.clear(networks);
}

void Worker::start_search() noexcept {
    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    // Non-main threads go directly to iterative_deepening()
    if (!mainManager)
    {
        iterative_deepening();
        return;
    }

    mainManager->ponderhitStop = false;
    mainManager->ponder        = limits.ponder;
    mainManager->callsCount    = limits.hitRate;
    mainManager->tm.init(limits, rootPos, options);
    mainManager->skill.init(options);
    tt.update_generation(limits.infinite);

    if (rootMoves.empty())
    {
        rootMoves.emplace_back(Move::None());
        mainManager->updateContext.onUpdateShort({rootPos, DEPTH_ZERO});
    }
    else
    {
        Move bookBestMove = Move::None();
        if (!limits.infinite && limits.mate == 0)
        {
            // Check polyglot book
            if (options["OwnBook"] && mainManager->polyBook.is_enabled()
                && rootPos.game_move() < options["BookDepth"])
                bookBestMove = mainManager->polyBook.probe(rootPos, options["BookPickBest"]);
        }

        if (bookBestMove
            && std::find(rootMoves.begin(), rootMoves.end(), bookBestMove) != rootMoves.end())
        {
            StateInfo st;
            ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

            rootPos.do_move(bookBestMove, st);
            Move bookPonderMove = mainManager->polyBook.probe(rootPos, options["BookPickBest"]);
            rootPos.undo_move(bookBestMove);

            for (const Thread* th : threads)
            {
                auto& rms = th->worker->rootMoves;
                std::swap(rms[0], *std::find(rms.begin(), rms.end(), bookBestMove));
                if (bookPonderMove)
                    rms[0].push(bookPonderMove);
            }
        }
        else
        {
            threads.start_search();  // start non-main threads
            iterative_deepening();   // main thread start searching
        }
    }

    // When reach the maximum depth, can arrive here without a raise of threads.stop.
    // However, if pondering or in an infinite search, the UCI protocol states that
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
    // subtract the searched nodes from the available ones before exiting.
    if (mainManager->tm.useNodesTime)
        mainManager->tm.advance(threads.nodes() - limits.clock[rootPos.side_to_move()].inc);

    Worker* bestWorker = this;
    if (threads.size() > 1 && limits.mate == 0 && int(options["MultiPV"]) == 1 && rootMoves[0][0]
        && !mainManager->skill.enabled())
        bestWorker = threads.best_thread()->worker.get();

    mainManager->prevBestValue    = bestWorker->rootMoves[0].value;
    mainManager->prevBestAvgValue = bestWorker->rootMoves[0].avgValue;

    // Send again PV info if have a new best worker
    if (bestWorker != this)
        mainManager->info_pv(*bestWorker, bestWorker->completedDepth);

    Move bestMove   = bestWorker->rootMoves[0][0];
    Move ponderMove = Move::None();
    if (bestMove
        && (bestWorker->rootMoves[0].size() > 1
            || bestWorker->rootMoves[0].extract_ponder_from_tt(rootPos, tt)))
        ponderMove = bestWorker->rootMoves[0][1];

    mainManager->updateContext.onUpdateBestMove({bestMove, ponderMove});
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
    // Allocate stack with extra size to allow access from (ss - 7) to (ss + 2):
    // (ss - 7) is needed for update_continuation_histories(ss - 1) which accesses (ss - 6),
    // (ss + 2) is needed for initialization of cutOffCnt and killerMoves.
    Stack  stack[MAX_PLY + 10]{};
    Stack* ss = stack + 7;
    for (std::int16_t i = 7; i > 0; --i)
    {
        (ss - i)->ply = -i;
        (ss - i)->continuationHistory =
          &continuationHistory[0][0][NO_PIECE][SQ_ZERO];  // Use as a sentinel
        (ss - i)->staticEval = VALUE_NONE;
    }
    for (std::int16_t i = 0; i <= MAX_PLY + 2; ++i)
        (ss + i)->ply = +i;

    Move pv[MAX_PLY + 1];
    ss->pv = pv;

    Color stm = rootPos.side_to_move();

    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    if (mainManager)
        mainManager->iterValue.fill(
          mainManager->prevBestValue != -VALUE_INFINITE ? mainManager->prevBestValue : VALUE_ZERO);

    std::uint8_t multiPV = options["MultiPV"];
    // When playing with strength handicap enable MultiPV search that will
    // use behind-the-scenes to retrieve a set of possible moves.
    if (threads.main_manager()->skill.enabled())
        multiPV = std::max<std::uint8_t>(multiPV, 4);
    multiPV = std::min<std::uint8_t>(multiPV, rootMoves.size());

    std::int16_t timeOptimism = limits.time_diff(stm);

    std::uint16_t researchCounter = 0;
    std::uint8_t  iterIdx         = 0;
    double        timeReduction = 1.0, sumBestMoveChanges = 0.0;

    Value value         = -VALUE_INFINITE;
    Moves lastBestPV    = {Move::None()};
    Value lastBestValue = -VALUE_INFINITE;
    Depth lastBestDepth = DEPTH_ZERO;
    // Iterative deepening loop until requested to stop or the target depth is reached
    while (++rootDepth < MAX_PLY && !threads.stop
           && !(mainManager && limits.depth != DEPTH_ZERO && rootDepth > limits.depth))
    {
        // Age out PV variability metric
        if (mainManager)
            sumBestMoveChanges *= 0.5;

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (RootMove& rm : rootMoves)
            rm.prevValue = rm.value;

        if (threads.research)
            ++researchCounter;

        std::uint8_t pvFirst = 0;
        pvLast               = 0;
        // MultiPV loop. Perform a full root search for each PV line
        for (pvIndex = 0; pvIndex < multiPV && !threads.stop; ++pvIndex)
        {
            if (pvIndex == pvLast)
            {
                pvFirst = pvLast;
                for (pvLast++; pvLast < rootMoves.size(); ++pvLast)
                    if (rootMoves[pvLast].tbRank != rootMoves[pvFirst].tbRank)
                        break;
            }

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = DEPTH_ZERO;

            // Reset aspiration window starting size
            Value avgValue = rootMoves[pvIndex].avgValue;

            int   delta = 10 + avgValue * avgValue / 9530;
            Value alpha = std::max(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min(avgValue + delta, +VALUE_INFINITE);

            // Adjust optimism based on root move's avgValue (~4 Elo)
            optimism[stm]  = timeOptimism + 119 * avgValue / (88 + std::abs(avgValue));
            optimism[~stm] = -optimism[stm];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, research with a bigger window until don't fail high/low anymore.
            std::uint16_t failedHighCounter = 0;
            while (true)
            {
                // Adjust the effective depth searched, but ensure at least one effective increment
                // for every 4 researchCounter steps.
                Depth adjustedDepth =
                  std::max(rootDepth - failedHighCounter - 3 * (1 + researchCounter) / 4, 1);
                value = search<Root>(rootPos, ss, alpha, beta, adjustedDepth, false);

                // Bring the best move to the front. It is critical that sorting
                // is done with a stable algorithm because all the values but the
                // first and eventually the new best one is set to -VALUE_INFINITE
                // and want to keep the same order for all the moves except the
                // new PV that goes to the front. Note that in the case of MultiPV
                // search the already searched PV lines are preserved.
                std::stable_sort(rootMoves.begin() + pvIndex, rootMoves.begin() + pvLast);

                // If the search has been stopped, break immediately.
                // Sorting is safe because RootMoves is still valid,
                // although it refers to the previous iteration.
                if (threads.stop)
                    break;

                // When failing high/low give some update (without cluttering the UI) before a re-search.
                if (mainManager && multiPV == 1 && (alpha >= value || value >= beta)
                    && mainManager->elapsed() > 3000)
                    mainManager->info_pv(*this, rootDepth);

                // In case of failing low/high increase aspiration window and research,
                // otherwise exit the loop.
                if (alpha >= value)
                {
                    beta  = (alpha + beta) / 2;
                    alpha = std::max(value - delta, -VALUE_INFINITE);

                    failedHighCounter = 0;
                    if (mainManager)
                        mainManager->ponderhitStop = false;
                }
                else if (value >= beta)
                {
                    beta = std::min(value + delta, +VALUE_INFINITE);
                    ++failedHighCounter;
                }
                else
                    break;

                delta += delta / 3;

                assert(-VALUE_INFINITE <= alpha && beta <= +VALUE_INFINITE);
            }

            // Sort the PV lines searched so far and update the GUI
            std::stable_sort(rootMoves.begin() + pvFirst, rootMoves.begin() + pvIndex + 1);

            if (mainManager
                && (threads.stop || pvIndex + 1 == multiPV || mainManager->elapsed() > 3000)
                // A thread that aborted search can have mated-in/TB-loss PV and score
                // that cannot be trusted, i.e. it can be delayed or refuted if have
                // had time to fully search other root-moves. Thus, suppress this output and
                // below pick a proven score/PV for this thread (from the previous iteration).
                && !(threads.abort && rootMoves[0].uciValue <= VALUE_TB_LOSS_IN_MAX_PLY))
                mainManager->info_pv(*this, rootDepth);
        }

        if (!threads.stop)
            completedDepth = rootDepth;

        // Make sure not to pick an unproven mated-in score,
        // in case this worker prematurely stopped the search (aborted-search).
        if (threads.abort
            && (rootMoves[0].value != -VALUE_INFINITE
                && rootMoves[0].value <= VALUE_TB_LOSS_IN_MAX_PLY))
        {
            // Bring the last best move to the front for best thread selection.
            move_to_front(rootMoves,
                          [&lastBestPV = std::as_const(lastBestPV)](const RootMove& rm) noexcept {
                              return rm == lastBestPV[0];
                          });
            rootMoves[0].pv    = lastBestPV;
            rootMoves[0].value = rootMoves[0].uciValue = lastBestValue;
        }
        else if (rootMoves[0][0] != lastBestPV[0])
        {
            lastBestPV    = rootMoves[0].pv;
            lastBestValue = rootMoves[0].value;
            lastBestDepth = rootDepth;
        }

        if (!mainManager)
            continue;

        // Have found a "mate in x"?
        if (limits.mate != 0 && rootMoves[0].value == rootMoves[0].uciValue
            && ((rootMoves[0].value != +VALUE_INFINITE
                 && rootMoves[0].value >= VALUE_MATES_IN_MAX_PLY
                 && VALUE_MATE - rootMoves[0].value <= 2 * limits.mate)
                || (rootMoves[0].value != -VALUE_INFINITE
                    && rootMoves[0].value <= VALUE_MATED_IN_MAX_PLY
                    && VALUE_MATE + rootMoves[0].value <= 2 * limits.mate)))
            threads.stop = true;

        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (mainManager->skill.enabled() && mainManager->skill.time_to_pick(rootDepth))
            mainManager->skill.pick_best_move(rootMoves, multiPV);

        // Do have time for the next iteration? Can stop searching now?
        if (limits.use_time_management() && !threads.stop && !mainManager->ponderhitStop)
        {
            // Use part of the gained time from a previous stable move for the current move
            for (const Thread* th : threads)
            {
                sumBestMoveChanges += th->worker->bestMoveChanges;
                th->worker->bestMoveChanges = 0;
            }

            // clang-format off
            double fallingReduction =
              std::clamp(0.1067
                        + 0.0223
                            * ((mainManager->prevBestAvgValue != -VALUE_INFINITE
                                ? mainManager->prevBestAvgValue : VALUE_ZERO) - value)
                        + 0.0097 * (mainManager->iterValue[iterIdx] - value),
                         0.580, 1.667);
            // If the bestMove is stable over several iterations, reduce time accordingly
            timeReduction        = 0.687 + 0.808 * (completedDepth > lastBestDepth + 8);
            double reduction     = 0.4608 * (1.48 + mainManager->prevTimeReduction) / timeReduction;
            double instability   = 1.0 + 1.88 * sumBestMoveChanges / threads.size();
            double evalReduction = EvalReduction[std::clamp<int>((750 + value) / 150, 0, EvalReduction.size() - 1)];
            double recapture     = rootPos.cap_square() == rootMoves[0][0].dst_sq()
                                && rootPos.pieces(~stm) & rootPos.cap_square() ? 0.955 : 1.005;

            double totalTime = mainManager->tm.optimum() * fallingReduction * reduction
                             * instability * evalReduction * recapture;

            // Cap totalTime in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = std::min(0.5 * totalTime, 500.0);

            TimePoint elapsedTime = mainManager->elapsed(*this);

            std::uint8_t nodesEffort = 100 * rootMoves[0].nodes / std::max<std::uint64_t>(nodes, 1ULL);
            if (completedDepth >= 10 && nodesEffort >= 97 && elapsedTime > 0.739 * totalTime && !mainManager->ponder)
                threads.stop = true;
            // clang-format on
            // Stop the search if have exceeded the totalTime
            if (elapsedTime > 1.000 * totalTime)
            {
                // If allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainManager->ponder)
                    mainManager->ponderhitStop = true;
                else
                    threads.stop = true;
            }

            threads.research = (elapsedTime > 0.506 * totalTime) && !mainManager->ponder;
        }

        mainManager->iterValue[iterIdx] = value;

        iterIdx = (iterIdx + 1) % mainManager->iterValue.size();
    }

    if (!mainManager)
        return;

    mainManager->prevTimeReduction = timeReduction;

    // If the skill level is enabled, swap the best PV line with the sub-optimal one
    if (mainManager->skill.enabled())
        std::swap(rootMoves[0],
                  *std::find(rootMoves.begin(), rootMoves.end(),
                             mainManager->skill.bestMove
                               ? mainManager->skill.bestMove
                               : mainManager->skill.pick_best_move(rootMoves, multiPV)));
}

// Main search function for both PV and non-PV nodes.
template<NodeType NT>
// clang-format off
Value Worker::search(
  Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode, Move excludedMove) noexcept {
    // clang-format on
    constexpr bool RootNode = NT == Root;
    constexpr bool PVNode   = RootNode || NT == PV;

    // Dive into quiescence search when the depth reaches zero
    if (depth <= DEPTH_ZERO)
        return qsearch < PVNode ? PV : NonPV > (pos, ss, alpha, beta);

    // Check if have an upcoming move that draws by repetition, or
    // if the opponent had an alternative move earlier to this position.
    if (!RootNode && alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
    {
        alpha = draw_value(nodes);
        if (alpha >= beta)
            return alpha;
    }

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (alpha == beta - 1));
    assert(DEPTH_ZERO < depth && depth < MAX_PLY);
    assert(!(PVNode && cutNode));

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    Move pv[MAX_PLY + 1];

    // Step 1. Initialize node
    ss->inCheck   = pos.checkers();
    ss->moveCount = 0;

    Color stm = pos.side_to_move();

    // Check for the available remaining time
    if (is_main_worker())
        main_manager()->should_abort(*this);

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PVNode)
        selDepth = std::max<std::uint16_t>(ss->ply + 1, selDepth);

    if (RootNode)
        rootDelta = beta - alpha;
    else
    {
        // Step 2. Check for stopped search and immediate draw
        if (threads.stop.load(std::memory_order_relaxed) || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return ss->ply >= MAX_PLY && !ss->inCheck
                   ? evaluate(pos, networks, accCaches, optimism[stm])
                   : draw_value(nodes);

        // Step 3. Mate distance pruning. Even if mate at the next move score would
        // be at best mates_in(ss->ply + 1), but if alpha is already bigger because
        // a shorter mate was found upward in the tree then there is no need to search
        // because will never beat the current alpha. Same logic but with a reversed
        // signs apply also in the opposite condition of being mated instead of giving
        // mate. In this case, return a fail-high score.
        alpha = std::max(mated_in(ss->ply), alpha);
        beta  = std::min(mates_in(ss->ply + 1), beta);
        if (alpha >= beta)
            return alpha;
    }

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    ss->history = 0;
    (ss + 2)->killerMoves.fill(Move::None());
    (ss + 2)->cutoffCount = 0;

    Square prevDst =
      (ss - 1)->currentMove.is_ok() ? (ss - 1)->currentMove.prev_dst_sq(stm) : SQ_NONE;

    PieceType prevCaptured = type_of(pos.captured_piece());
    assert(prevCaptured != KING);

    // Step 4. Transposition table lookup.
    Key      key = pos.key();
    TTEntry* tte = tt.probe(key, ss->ttHit);
    Value    ttValue =
      ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move move;
    Move ttMove = RootNode                                       ? rootMoves[pvIndex][0]
                : (move = tte->move()) && pos.pseudo_legal(move) ? move
                                                                 : Move::None();

    bool ttCapture = ttMove && pos.capture_stage(ttMove);

    // At this point, if excluded, skip straight to step 6, static eval. However,
    // to save indentation, list the condition in all code between here and there.
    if (!excludedMove)
        ss->ttPv = PVNode || (ss->ttHit && tte->is_pv());

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && !excludedMove
        && ttValue != VALUE_NONE  // Possible in case of TT access race or if !ttHit
        && tte->depth() > depth && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
    {
        // If ttMove fails high, update move sorting heuristics on TT hit (~2 Elo)
        if (ttMove && ttValue >= beta)
        {
            // Bonus for a quiet ttMove (~2 Elo)
            if (!ttCapture)
                update_quiet_stats(*this, pos, ss, ttMove, prevDst, stat_bonus(depth));

            // Extra penalty for early quiet moves of the previous ply (~1 Elo on STC, ~2 Elo on LTC)
            if (is_ok(prevDst) && !is_ok(prevCaptured) && (ss - 1)->moveCount <= 2)
                update_continuation_histories(ss - 1, pos.piece_on(prevDst), prevDst,
                                              -stat_malus(depth + 1));
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < 2 * Position::DrawMoveCount - 10)
            return ttValue >= beta && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY
                   ? (3 * ttValue + beta) / 4
                   : ttValue;
    }

    Value bestValue = -VALUE_INFINITE, maxValue = +VALUE_INFINITE, value;

    // Step 5. Tablebases probe
    if (!RootNode && !excludedMove && tbConfig.cardinality)
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

                if (bound == BOUND_EXACT  //
                    || (bound == BOUND_LOWER && value >= beta)
                    || (bound == BOUND_UPPER && alpha >= value))
                {
                    tte->save(key, std::min(depth + 6, MAX_PLY - 1), ss->ttPv, bound, Move::None(),
                              value_to_tt(value, ss->ply), VALUE_NONE, tt.generation());
                    return value;
                }

                if (PVNode)
                {
                    if (bound == BOUND_LOWER)
                        bestValue = value, alpha = std::max(bestValue, alpha);
                    else
                        maxValue = value;
                }
            }
        }
    }

    Value probCutBeta;
    Value unadjustedStaticEval, eval;
    bool  improving, worsening;

    // Step 6. Static evaluation of the position
    if (ss->inCheck)
    {
        // Step 7. A small ProbCut idea, when in check (~4 Elo)
        probCutBeta = beta + 420;
        if (!PVNode && ttCapture && ttValue != VALUE_NONE && tte->depth() >= depth - 4
            && ttValue >= probCutBeta && (tte->bound() & BOUND_LOWER)
            && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY
            && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY)
            return probCutBeta;

        unadjustedStaticEval = eval = ss->staticEval = VALUE_NONE;

        improving = worsening = false;
        // Skip early pruning when in check
        goto moves_loop;
    }

    if (excludedMove)
    {
        // Providing the hint that this node's accumulator will often be used
        // brings significant Elo gain (~13 Elo).
        Eval::NNUE::hint_common_parent_position(pos, networks, accCaches);
        unadjustedStaticEval = eval = ss->staticEval;
    }
    else if (ss->ttHit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = tte->eval();
        if (unadjustedStaticEval == VALUE_NONE)
            unadjustedStaticEval = evaluate(pos, networks, accCaches, optimism[stm]);
        else if (PVNode)
            Eval::NNUE::hint_common_parent_position(pos, networks, accCaches);

        eval = ss->staticEval = to_corrected_static_eval(unadjustedStaticEval, *this, pos);

        // Can ttValue be used as a better position evaluation (~7 Elo)
        if (ttValue != VALUE_NONE && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    }
    else
    {
        unadjustedStaticEval = evaluate(pos, networks, accCaches, optimism[stm]);
        eval = ss->staticEval = to_corrected_static_eval(unadjustedStaticEval, *this, pos);

        // Static evaluation is saved as it was before adjustment by correction history
        tte->save(key, DEPTH_NONE, ss->ttPv, BOUND_NONE, Move::None(), VALUE_NONE,
                  unadjustedStaticEval, tt.generation());
    }

    // Use static evaluation difference to improve quiet move ordering (~9 Elo)
    if (is_ok(prevDst) && !is_ok(prevCaptured) && !(ss - 1)->inCheck)
    {
        int bonus = std::clamp(-13 * (ss->staticEval + (ss - 1)->staticEval), -1796, 1526);
        bonus     = bonus > 0 ? 2 * bonus : bonus / 2;
        mainHistory[~stm][(ss - 1)->currentMove.org_dst()] << bonus;
        if (type_of(pos.piece_on(prevDst)) != PAWN && (ss - 1)->currentMove.type_of() != PROMOTION)
            pawnHistory[pawn_index(pos.pawn_key())][pos.piece_on(prevDst)][prevDst] << bonus / 2;
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

    // Step 8. Razoring (~1 Elo)
    // If eval is really low check with qsearch if it can exceed alpha,
    // if it can't, return a fail low.
    // Adjust razor margin according to cutoffCount. (~1 Elo)
    if (eval < alpha - 433 - (302 - 141 * ((ss + 1)->cutoffCount > 3)) * depth * depth)
    {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha)
            return value;
    }

    // Step 9. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 11
        && eval - futility_margin(depth, cutNode && !ss->ttHit, improving, worsening)
               - (ss - 1)->history / 254
             >= beta
        && eval >= beta && eval < VALUE_TB_WIN_IN_MAX_PLY && (!ttMove || ttCapture))
        return beta > VALUE_TB_LOSS_IN_MAX_PLY ? (eval + beta) / 2 : eval;

    // Step 10. Null move search with verification search (~35 Elo)
    if (!PVNode && !excludedMove && (ss - 1)->currentMove != Move::Null() && eval >= beta
        && ss->staticEval >= beta + 326 - 19 * depth && pos.non_pawn_material(stm)
        && (ss - 1)->history < 16993 && ss->ply >= nmpMinPly && beta > VALUE_TB_LOSS_IN_MAX_PLY)
    {
        int delta = eval - beta;
        assert(delta >= 0);

        // Null move dynamic reduction based on depth and eval
        Depth R = Depth(4 + depth / 3 + std::min(delta / 134, 6));

        ss->currentMove         = Move::Null();
        ss->continuationHistory = &continuationHistory[0][0][NO_PIECE][SQ_ZERO];

        pos.do_null_move(st, tt);

        Value nullValue = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);

        pos.undo_null_move();

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && nullValue < VALUE_TB_WIN_IN_MAX_PLY)
        {
            if (nmpMinPly || depth < 16)
                return nullValue;

            assert(!nmpMinPly);  // Recursive verification is not allowed

            // Do verification search at high depths, with null move pruning
            // disabled until ply exceeds nmpMinPly.
            nmpMinPly = ss->ply + 3 * (depth - R) / 4;

            Value v = search<NonPV>(pos, ss, beta - 1, beta, depth - R, false);

            nmpMinPly = 0;

            if (v >= beta)
                return nullValue;
        }
    }

    // Step 11. Internal iterative reductions (~9 Elo)
    // For PV nodes without a ttMove, decrease depth.
    if (PVNode && !ttMove)
        depth -= 3;

    // Use qsearch if depth <= DEPTH_ZERO.
    if (depth <= DEPTH_ZERO)
        return qsearch<PV>(pos, ss, alpha, beta);

    // For cutNodes without a ttMove, decrease depth if depth is high enough.
    if (depth >= 8 && cutNode && (!ttMove || tte->bound() == BOUND_UPPER))
        depth -= 1 + !ttMove;

    // Step 12. ProbCut (~10 Elo)
    // If have a good enough capture (or queen promotion) and a reduced search
    // returns a value much above beta, can (almost) safely prune the previous move.
    probCutBeta = beta + 159 - 66 * improving;
    if (
      !PVNode && depth > 3
      && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
      // If the value from the transposition table is lower than probCutBeta, don't attempt probCut
      // there and in further interactions with the transposition table cutoff depth is set to
      // depth - 3 because probCut search has depth set to depth - 4 but also did a move before it
      // So effective depth is equal to depth - 3
      && !(ttValue != VALUE_NONE && ttValue < probCutBeta && tte->depth() >= depth - 3))
    {
        assert(probCutBeta < +VALUE_INFINITE && probCutBeta > beta);

        MovePicker mp(pos, ttMove, probCutBeta - ss->staticEval, &captureHistory);
        // Loop through all pseudo-legal moves
        while ((move = mp.next_move()))
        {
            assert(move.is_ok());
            assert(pos.capture_stage(move));

            // Check for legality
            if (move == excludedMove || !pos.legal(move))
                continue;

            // Speculative prefetch as early as possible
            prefetch(tt.first_entry(pos.move_key(move)));

            ss->currentMove = move;
            ss->continuationHistory =
              &continuationHistory[ss->inCheck][true][pos.moved_piece(move)][move.dst_sq()];

            nodes.fetch_add(1, std::memory_order_relaxed);
            pos.do_move(move, st);

            // Perform a preliminary qsearch to verify that the move holds
            value = -qsearch<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (value >= probCutBeta)
                value =
                  -search<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, depth - 4, !cutNode);

            pos.undo_move(move);

            if (value >= probCutBeta)
            {
                // Save ProbCut data into transposition table
                tte->save(key, depth - 3, ss->ttPv, BOUND_LOWER, move, value_to_tt(value, ss->ply),
                          unadjustedStaticEval, tt.generation());
                return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probCutBeta - beta)
                                                                 : value;
            }
        }

        Eval::NNUE::hint_common_parent_position(pos, networks, accCaches);
    }

moves_loop:  // When in check, search starts here

    if (!ttMove && (move = tte->move()) && pos.pseudo_legal(move))
    {
        ttMove    = move;
        ttCapture = pos.capture_stage(ttMove);
    }

    value = bestValue;

    Move bestMove = Move::None();

    std::uint8_t moveCount = 0, captureCount = 0, quietCount = 0;
    Move         captureMoves[32], quietMoves[32];

    const PieceDstHistory* contHistory[6]{(ss - 1)->continuationHistory,
                                          (ss - 2)->continuationHistory,
                                          (ss - 3)->continuationHistory,
                                          (ss - 4)->continuationHistory,
                                          nullptr,
                                          (ss - 6)->continuationHistory};

    Move counterMove = is_ok(prevDst) ? counterMoves[pos.piece_on(prevDst)][prevDst] : Move::None();

    MovePicker mp(pos, ttMove, depth, &mainHistory, &captureHistory, contHistory, &pawnHistory,
                  ss->killerMoves, counterMove);
    // Step 13. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()))
    {
        assert(move.is_ok());

        // Check for legality
        if (move == excludedMove || !pos.legal(move))
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in Root
        // Move List. In MultiPV mode also skip PV moves that have been already
        // searched and those of lower "TB rank" if in a TB root position.
        if (RootNode && !std::count(rootMoves.begin() + pvIndex, rootMoves.begin() + pvLast, move))
            continue;

        ss->moveCount = ++moveCount;

        if (RootNode && is_main_worker() && !main_manager()->reportMinimal
            && main_manager()->elapsed() > 3000)
        {
            main_manager()->updateContext.onUpdateIteration(
              {rootDepth, move, std::uint16_t(moveCount + pvIndex)});
        }

        if (PVNode)
            (ss + 1)->pv = nullptr;

        const Square org = move.org_sq(), dst = move.dst_sq();
        const Piece  movedPiece = pos.piece_on(org);

        const bool givesCheck = pos.gives_check(move);
        const bool isCapture  = pos.capture_stage(move);

        // Calculate new depth for this move
        Depth newDepth = Depth(depth - 1);

        Depth r = reduction(depth, moveCount, 760 * (beta - alpha) / rootDelta, improving);

        // Step 14. Pruning at shallow depth (~120 Elo).
        // Depth conditions are important for mate finding.
        if (!RootNode && pos.non_pawn_material(stm) && bestValue > VALUE_TB_LOSS_IN_MAX_PLY)
        {
            // Skip quiet moves if moveCount exceeds our Futility Move Count threshold (~8 Elo)
            mp.pickQuiets = mp.pickQuiets && moveCount < futility_move_count(depth, improving);

            // Reduced depth of the next LMR search
            Depth lmrDepth = Depth(newDepth - r);

            if (isCapture || givesCheck)
            {
                Piece capturedPiece = pos.captured_piece(move);
                auto  captureHist   = captureHistory[movedPiece][dst][type_of(capturedPiece)];

                // Futility pruning for captures (~2 Elo)
                if (!givesCheck && lmrDepth < 7 && !ss->inCheck)
                {
                    Value futilityValue = ss->staticEval + 295 + 280 * lmrDepth
                                        + PieceValue[capturedPiece] + captureHist / 7;
                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures and checks (~11 Elo)
                auto seeHist = std::clamp(captureHist / 32, -197 * depth, 196 * depth);
                if (!pos.see_ge(move, -186 * depth - seeHist))
                    continue;
            }
            else
            {
                int history = (*contHistory[0])[movedPiece][dst]  //
                            + (*contHistory[1])[movedPiece][dst]  //
                            + pawnHistory[pawn_index(pos.pawn_key())][movedPiece][dst];

                // Continuation history based pruning (~2 Elo)
                if (lmrDepth < 6 && history < -4081 * depth)
                    continue;

                history += 2 * mainHistory[stm][move.org_dst()];

                lmrDepth += Depth(history / 4768);

                Value futilityValue =
                  ss->staticEval + 54 + 80 * (bestValue < ss->staticEval - 52) + 142 * lmrDepth;

                // Futility pruning: parent node (~13 Elo)
                if (lmrDepth < 13 && !ss->inCheck && futilityValue <= alpha)
                {
                    if (bestValue <= futilityValue && futilityValue < VALUE_TB_WIN_IN_MAX_PLY
                        && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
                        bestValue = (3 * futilityValue + bestValue) / 4;
                    continue;
                }

                lmrDepth = std::max(lmrDepth, DEPTH_ZERO);

                // Prune moves with negative SEE (~4 Elo)
                if (!pos.see_ge(move, -28 * lmrDepth * lmrDepth))
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

            // Note: the depth margin and singularBeta margin are known for having non-linear
            // scaling. Their values are optimized to time controls of 180+1.8 and longer
            // so changing them requires tests at these types of time controls.
            // Recursive singular search is avoided.
            if (!RootNode && !excludedMove && move == ttMove && ttValue != VALUE_NONE
                && depth >= 4 - (completedDepth > 32) + ss->ttPv && tte->depth() >= depth - 3
                && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY && (tte->bound() & BOUND_LOWER))
            {
                Value singularBeta  = ttValue - (65 + 52 * (!PVNode && ss->ttPv)) * depth / 63;
                Depth singularDepth = newDepth / 2;

                // clang-format off
                value = search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode, move);

                if (value < singularBeta)
                {
                    int doubleMargin =   0 + 251 * PVNode - 241 * !ttCapture;
                    int tripleMargin = 135 + 234 * PVNode - 248 * !ttCapture + 124 * (ss->ttPv || !ttCapture);
                    int quadleMargin = 447 + 354 * PVNode - 300 * !ttCapture + 206 * (ss->ttPv);

                    extension = 1 + (value < singularBeta - doubleMargin)
                                  + (value < singularBeta - tripleMargin)
                                  + (value < singularBeta - quadleMargin);

                    depth += (!PVNode && depth < 14);
                }
                // clang-format on

                // Multi-cut pruning
                // If the ttMove is assumed to fail high based on the bound of the TT entry, and
                // if after excluding the ttMove with a reduced search fail high over the original beta,
                // assume this expected cut-node is not singular (multiple moves fail high),
                // and can prune the whole subtree by returning a soft-bound.
                else if (singularBeta >= beta)
                {
                    if (!ttCapture)
                        update_quiet_histories(*this, pos, ss, ttMove, -stat_malus(depth));

                    return singularBeta;
                }

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
            else if (PVNode && dst == prevDst && move == ttMove
                     && captureHistory[movedPiece][dst][type_of(pos.captured_piece(move))] > 4016)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.move_key(move)));

        // Update the current move (this must be done after singular extension search)
        ss->currentMove         = move;
        ss->continuationHistory = &continuationHistory[ss->inCheck][isCapture][movedPiece][dst];

        [[maybe_unused]] std::uint64_t startNodes;
        if (RootNode)
            startNodes = std::uint64_t(nodes);

        // Step 16. Make the move
        nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);

        // Decrease reduction if position is or has been on the PV (~7 Elo)
        if (ss->ttPv)
            r -= 1 + (ttValue != VALUE_NONE && ttValue > alpha) + (tte->depth() >= depth);

        // Increase reduction at cut nodes which are not a former PV node
        else if (cutNode && move != ttMove && move != ss->killerMoves[0])
            r++;

        // Increase reduction for cut nodes (~4 Elo)
        if (cutNode)
            r += 2 - (ss->ttPv && tte->depth() >= depth);

        // Increase reduction if ttMove is a capture (~3 Elo)
        if (ttCapture)
            r++;

        // Decrease reduction for PvNodes (~0 Elo on STC, ~2 Elo on LTC)
        if (PVNode)
            r--;

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if ((ss + 1)->cutoffCount > 3)
            r++;

        // Set reduction to 0 for first picked move (ttMove) (~2 Elo)
        // Nullifies all previous reduction adjustments to ttMove and leaves only history to do them
        else if (move == ttMove)
            r = 0;

        ss->history = 2 * mainHistory[stm][move.org_dst()]  //
                    + (*contHistory[0])[movedPiece][dst]    //
                    + (*contHistory[1])[movedPiece][dst] - 5078;

        // Decrease/increase reduction for moves with a good/bad history (~8 Elo)
        r -= ss->history / (17662 - 105 * std::min<Depth>(depth, 16));

        // Step 17. Late moves reduction / extension (LMR, ~117 Elo)
        if (depth >= 2 && moveCount > 1 + RootNode)
        {
            // In general, want to cap the LMR depth search at newDepth, but when
            // reduction is negative, allow this move a limited search extension
            // beyond the first move depth.
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth d = std::max<Depth>(std::min<Depth>(newDepth - r, newDepth + 1), 1);

            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            // Do a full-depth search when reduced LMR search fails high
            if (alpha < value && d < newDepth)
            {
                // Adjust full-depth search based on LMR results - if the result
                // was good enough search deeper, if it was bad enough search shallower.
                newDepth += (value > bestValue + 40 + 2 * newDepth)  // (~1 Elo)
                          - (value < bestValue + newDepth);          // (~2 Elo)

                if (d < newDepth)
                    value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                // Post LMR continuation history updates (~1 Elo)
                int bonus = alpha >= value ? -stat_malus(newDepth)
                          : value >= beta  ? +stat_bonus(newDepth)
                                           : 0;

                update_continuation_histories(ss, movedPiece, dst, bonus);
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present (~6 Elo)
            if (!ttMove)
                r += 2;

            // Note that if expected reduction is high, reduce search depth here (~9 Elo)
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth - (r > 3), !cutNode);
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with alpha >= value and try another move.
        if (PVNode && (moveCount == 1 || alpha < value))
        {
            (ss + 1)->pv    = pv;
            (ss + 1)->pv[0] = Move::None();

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth, false);
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
            RootMove& rm = *std::find(rootMoves.begin(), rootMoves.end(), move);

            rm.avgValue = rm.avgValue != -VALUE_INFINITE ? (2 * value + rm.avgValue) / 3 : value;
            rm.nodes += nodes - startNodes;

            // PV move or new best move?
            if (moveCount == 1 || alpha < value)
            {
                rm.value = rm.uciValue = value;
                rm.selDepth            = selDepth;
                rm.lowerBound = rm.upperBound = false;

                if (value >= beta)
                {
                    rm.lowerBound = true;
                    rm.uciValue   = beta;
                }
                else if (alpha >= value)
                {
                    rm.upperBound = true;
                    rm.uciValue   = alpha;
                }

                rm.pv.resize(1);

                assert((ss + 1)->pv);

                for (Move* m = (ss + 1)->pv; bool(*m); ++m)
                    rm.push(*m);

                // Record how often the best move has been changed in each iteration.
                // This information is used for time management.
                // In MultiPV mode, must take care to only do this for the first PV line.
                if (pvIndex == 0 && moveCount > 1)
                    ++bestMoveChanges;
            }
            else
                // All other moves but the PV, are set to the lowest value, this
                // is not a problem when sorting because the sort is stable and the
                // move position in the list is preserved - just the PV is pushed up.
                rm.value = -VALUE_INFINITE;
        }

        if (bestValue < value)
        {
            bestValue = value;

            if (alpha < value)
            {
                bestMove = move;

                if (PVNode && !RootNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                {
                    ss->cutoffCount += 1 + !ttMove;
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
            if (isCapture)
            {
                if (captureCount < 32)
                    captureMoves[captureCount++] = move;
            }
            else
            {
                if (quietCount < 32)
                    quietMoves[quietCount++] = move;
            }
        }
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves,
    // it must be a mate or a stalemate.
    // If in a singular extension search then return a fail low score.
    assert(moveCount != 0 || !ss->inCheck || excludedMove || MoveList<LEGAL>(pos).size() == 0);

    // Adjust best value for fail high cases at non-pv nodes
    if (!PVNode && bestValue >= beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY && std::abs(alpha) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (depth * bestValue + beta) / (depth + 1);

    if (moveCount == 0)
        bestValue = excludedMove ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha update the stats of searched moves
    else if (bestMove)
        update_all_stats(*this, pos, ss, bestMove, depth, bestValue > beta + 165, prevDst,
                         prevCaptured, quietMoves, quietCount, captureMoves, captureCount);

    // Bonus for prior counterMove that caused the fail low
    else if (is_ok(prevDst) && !is_ok(prevCaptured))
    {
        int bonusMul = (depth > 5) + (PVNode || cutNode)  //
                     + ((ss - 1)->history < -14455)       //
                     + ((ss - 1)->moveCount > 10)         //
                     + (!(ss - 0)->inCheck && bestValue <= +(ss - 0)->staticEval - 130)
                     + (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->staticEval - 77);

        int bonus = stat_bonus(depth) * bonusMul;
        mainHistory[~stm][(ss - 1)->currentMove.org_dst()] << bonus / 2;
        update_continuation_histories(ss - 1, pos.piece_on(prevDst), prevDst, bonus);
    }

    if (PVNode)
        bestValue = std::min(maxValue, bestValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~7 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    // Static evaluation is saved as it was before correction history
    if (!excludedMove && !(RootNode && pvIndex != 0))
        tte->save(key, depth, ss->ttPv,
                  bestValue >= beta    ? BOUND_LOWER
                  : PVNode && bestMove ? BOUND_EXACT
                                       : BOUND_UPPER,
                  bestMove, value_to_tt(bestValue, ss->ply), unadjustedStaticEval, tt.generation());

    // Adjust correction history
    if (!ss->inCheck && !(bestMove && pos.capture(bestMove))
        && !(bestValue >= beta && bestValue <= ss->staticEval)
        && !(!bestMove && bestValue >= ss->staticEval))
    {
        int bonus = std::clamp((bestValue - ss->staticEval) * depth / 8,
                               -CORRECTION_HISTORY_LIMIT / 4, +CORRECTION_HISTORY_LIMIT / 4);
        correctionHistory[stm][correction_index(pos.pawn_key())] << bonus;
    }

    assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);

    return bestValue;
}

// Quiescence search function, which is called by the main search function
// with DEPTH_ZERO, or recursively with further decreasing depth per call. (~155 Elo)
template<NodeType NT>
Value Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth) noexcept {
    static_assert(NT != Root);
    constexpr bool PVNode = NT == PV;

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (alpha == beta - 1));
    assert(depth <= DEPTH_ZERO);

    // Check if have an upcoming move that draws by repetition, or if
    // the opponent had an alternative move earlier to this position. (~1 Elo)
    if (alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply))
    {
        alpha = draw_value(nodes);
        if (alpha >= beta)
            return alpha;
    }

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    Move pv[MAX_PLY + 1];

    // Step 1. Initialize node
    if (PVNode)
    {
        (ss + 1)->pv = pv;
        ss->pv[0]    = Move::None();
    }

    ss->inCheck = pos.checkers();

    Color stm = pos.side_to_move();

    if (is_main_worker())
        main_manager()->callsCount--;

    // Used to send selDepth info to GUI (selDepth counts from 1, ply from 0)
    if (PVNode)
        selDepth = std::max<std::uint16_t>(ss->ply + 1, selDepth);

    // Step 2. Check for an immediate draw or maximum ply reached
    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY)
        return ss->ply >= MAX_PLY && !ss->inCheck
               ? evaluate(pos, networks, accCaches, optimism[stm])
               : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // If in check, search all evasions and thus store DEPTH_QS_CHECKS.
    Depth ttDepth = depth == DEPTH_QS_CHECKS || ss->inCheck ? DEPTH_QS_CHECKS : DEPTH_QS_NORMAL;

    // Step 3. Transposition table lookup
    Key      key = pos.key();
    TTEntry* tte = tt.probe(key, ss->ttHit);
    Value    ttValue =
      ss->ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move move;
    Move ttMove = (move = tte->move()) && pos.pseudo_legal(move) ? move : Move::None();
    bool ttPv   = ss->ttHit && tte->is_pv();

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && ttValue != VALUE_NONE  // Only in case of TT access race or if !ttHit
        && tte->depth() >= ttDepth
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    Square prevDst =
      (ss - 1)->currentMove.is_ok() ? (ss - 1)->currentMove.prev_dst_sq(stm) : SQ_NONE;

    Value unadjustedStaticEval, bestValue, futilityBase;

    // Step 4. Static evaluation of the position
    if (ss->inCheck)
    {
        unadjustedStaticEval = VALUE_NONE;
        bestValue = futilityBase = -VALUE_INFINITE;
    }
    else
    {
        if (ss->ttHit)
        {
            // Never assume anything about values stored in TT
            unadjustedStaticEval = tte->eval();
            if (unadjustedStaticEval == VALUE_NONE)
                unadjustedStaticEval = evaluate(pos, networks, accCaches, optimism[stm]);
            bestValue = ss->staticEval = to_corrected_static_eval(unadjustedStaticEval, *this, pos);

            // Can ttValue be used as a better position evaluation (~13 Elo)
            if (ttValue != VALUE_NONE
                && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttValue;
        }
        else
        {
            // In case of null move search, use previous static eval with a different sign
            unadjustedStaticEval = (ss - 1)->currentMove != Move::Null()
                                   ? evaluate(pos, networks, accCaches, optimism[stm])
                                   : -(ss - 1)->staticEval;
            bestValue = ss->staticEval = to_corrected_static_eval(unadjustedStaticEval, *this, pos);
        }

        // Stand pat. Return immediately if bestValue is at least beta
        if (bestValue >= beta)
        {
            if (!PVNode && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
                bestValue = (3 * bestValue + beta) / 4;

            if (!ss->ttHit)
                tte->save(key, DEPTH_NONE, false, BOUND_LOWER, Move::None(),
                          value_to_tt(bestValue, ss->ply), unadjustedStaticEval, tt.generation());

            return bestValue;
        }

        alpha = std::max(bestValue, alpha);

        futilityBase = ss->staticEval + 270;
    }

    // Initialize a MovePicker object for the current position, and prepare
    // to search the moves. Because the depth is <= DEPTH_ZERO here, only captures,
    // queen promotions, and other checks (only if depth == DEPTH_QS_CHECKS) will be generated.
    Value        value;
    Move         bestMove  = Move::None();
    std::uint8_t moveCount = 0;

    const PieceDstHistory* contHistory[2]{(ss - 1)->continuationHistory,
                                          (ss - 2)->continuationHistory};

    MovePicker mp(pos, ttMove, depth, &mainHistory, &captureHistory, contHistory, &pawnHistory);
    // Step 5. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()))
    {
        assert(move.is_ok());

        // Check for legality
        if (!pos.legal(move))
            continue;

        ++moveCount;

        const Square org = move.org_sq(), dst = move.dst_sq();
        const Piece  movedPiece = pos.piece_on(org);

        const bool givesCheck = pos.gives_check(move);
        const bool isCapture  = pos.capture_stage(move);

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

                // Static eval + value of piece going to captured is much lower than alpha prune this move. (~2 Elo)
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(futilityValue, bestValue);
                    continue;
                }

                // Static eval is much lower than alpha and move is not winning material prune this move. (~2 Elo)
                if (futilityBase <= alpha && !pos.see_ge(move, 1))
                {
                    bestValue = std::max(futilityBase, bestValue);
                    continue;
                }

                // Static exchange evaluation is much worse than what is needed to not fall below alpha prune this move.  (~1 Elo)
                if (futilityBase > alpha && !pos.see_ge(move, 4 * (alpha - futilityBase)))
                {
                    bestValue = alpha;
                    continue;
                }
            }

            // Continuation history based pruning (~3 Elo)
            if (!isCapture
                && ((*contHistory[0])[movedPiece][dst]  //
                    + (*contHistory[1])[movedPiece][dst]
                    + pawnHistory[pawn_index(pos.pawn_key())][movedPiece][dst])
                     <= 4000)
                continue;

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (!pos.see_ge(move, -69))
                continue;
        }

        // Speculative prefetch as early as possible
        prefetch(tt.first_entry(pos.move_key(move)));

        // Update the current move
        ss->currentMove         = move;
        ss->continuationHistory = &continuationHistory[ss->inCheck][isCapture][movedPiece][dst];

        // Step 7. Make and search the move
        nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);

        value = -qsearch<NT>(pos, ss + 1, -beta, -alpha, depth - 1);

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

    if (bestValue >= beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (3 * bestValue + beta) / 4;

    // Save gathered info in transposition table
    // Static evaluation is saved as it was before adjustment by correction history
    tte->save(key, ttDepth, ttPv, bestValue >= beta ? BOUND_LOWER : BOUND_UPPER, bestMove,
              value_to_tt(bestValue, ss->ply), unadjustedStaticEval, tt.generation());

    assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);

    return bestValue;
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
    if (TimePoint curTime = worker.limits.startTime + elapsedTime; curTime - debugTime >= 1000)
    {
        debugTime = curTime;
        dbg_print();
    }
#endif

    if (
      // Should not stop pondering until told so by the GUI
      !ponder
      // Later rely on the fact that at least use the main-thread previous root-search
      // score and PV in a multi-threaded environment to prove mated-in scores.
      && worker.completedDepth > DEPTH_ZERO
      && ((worker.limits.use_time_management() && (ponderhitStop || elapsedTime > tm.maximum()))
          || (worker.limits.moveTime != 0 && elapsedTime >= worker.limits.moveTime)
          || (worker.limits.nodes != 0 && worker.threads.nodes() >= worker.limits.nodes)))
        worker.threads.stop = worker.threads.abort = true;
}

void MainSearchManager::clear(std::uint16_t threadCount) noexcept {
    assert(threadCount != 0);
    prevBestValue     = -VALUE_INFINITE;
    prevBestAvgValue  = -VALUE_INFINITE;
    prevTimeReduction = 1.0;
    tm.clear();

    auto threadReduction = 18.93 + 0.5 * std::log(threadCount);
    reductions[0]        = 0U;
    for (std::uint16_t i = 1; i < reductions.size(); ++i)
        reductions[i] = std::uint32_t(threadReduction * std::log(i));
}

// Returns the actual time elapsed since the start of the search.
// This function is intended for use only when printing PV outputs,
// and not used for making decisions within the search algorithm itself.
TimePoint MainSearchManager::elapsed() const noexcept { return tm.elapsed(); }
// Returns the time elapsed since the search started.
// If the 'NodesTime' option is enabled, it will return the count of nodes searched
// instead. This function is called to check whether the search should be
// stopped based on predefined thresholds like time limits or nodes searched.
TimePoint MainSearchManager::elapsed(const Worker& worker) const noexcept {
    return tm.elapsed([&worker]() { return worker.threads.nodes(); });
}

void MainSearchManager::info_pv(const Search::Worker& worker, Depth depth) const noexcept {

    const auto& rootPos   = worker.rootPos;
    const auto& rootMoves = worker.rootMoves;

    TimePoint     time     = std::max(elapsed(), 1LL);
    std::uint64_t nodes    = worker.threads.nodes();
    std::uint16_t hashfull = worker.tt.hashfull();
    std::uint64_t tbHits   = worker.threads.tbHits() + worker.tbConfig.rootInTB * rootMoves.size();
    std::uint8_t  multiPV  = std::min<std::uint8_t>(worker.options["MultiPV"], rootMoves.size());
    bool          showWDL  = worker.options["UCI_ShowWDL"];

    for (std::uint8_t i = 0; i < multiPV; ++i)
    {
        bool updated = rootMoves[i].value != -VALUE_INFINITE;

        if (i != 0 && depth == 1 && !updated)
            continue;

        Depth d = updated ? depth : std::max<Depth>(depth - 1, 1);
        Value v = updated ? rootMoves[i].uciValue : rootMoves[i].prevValue;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = worker.tbConfig.rootInTB && std::abs(v) <= VALUE_TB;
        if (tb)
            v = rootMoves[i].tbValue;

        InfoFull info(rootPos, d, rootMoves[i]);
        info.value   = v;
        info.multiPV = i + 1;
        // tablebase- and previous-scores are exact
        info.showBound = i == worker.pvIndex && updated && !tb;
        info.showWDL   = showWDL;
        info.time      = time;
        info.nodes     = nodes;
        info.hashfull  = hashfull;
        info.tbHits    = tbHits;

        updateContext.onUpdateFull(info);
    }
}

// Called in case have no ponder move before exiting the search,
// for instance, in case stop the search during a fail high at root.
// Try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' have nothing to think about.
bool RootMove::extract_ponder_from_tt(Position& pos, const TranspositionTable& tt) noexcept {
    assert(size() == 1);
    assert(pv[0].is_ok());

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    pos.do_move(pv[0], st);

    const auto legalMoves = MoveList<LEGAL>(pos);
    if (legalMoves.size() != 0)
    {
        std::srand(std::time(nullptr));

        bool           ttHit;
        const TTEntry* tte = tt.probe(pos.key(), ttHit);
        if (Move m; ((m = tte->move()) && legalMoves.contains(m))
                    || (m = *(legalMoves.begin() + (std::rand() % legalMoves.size()))))
            push(m);
    }

    pos.undo_move(pv[0]);
    return size() > 1;
}

void Skill::init(const OptionsMap& options) noexcept {

    std::uint16_t uciELO = options["UCI_LimitStrength"] ? options["UCI_ELO"] : 0;
    if (uciELO)
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
Move Skill::pick_best_move(const RootMoves& rootMoves, std::uint8_t multiPV) noexcept {
    static PRNG rng(now());  // PRNG sequence should be non-deterministic

    // RootMoves are already sorted by value in descending order
    double weakness = 120.0 - 2.0 * level;
    Value  topValue = rootMoves[0].value;
    int    delta    = std::min(topValue - rootMoves[multiPV - 1].value, +VALUE_PAWN);

    Value bestValue = -VALUE_INFINITE;
    // Choose best move. For each move value add two terms, both dependent on weakness.
    // One is deterministic and bigger for weaker levels, and one is random.
    // Then choose the move with the resulting highest value.
    for (std::uint8_t i = 0; i < multiPV; ++i)
    {
        Value value = rootMoves[i].value
                    // This is magic formula for Push
                    + int(weakness * (topValue - rootMoves[i].value)
                          + delta * (rng.rand<std::uint32_t>() % int(weakness)))
                        / 128;

        if (bestValue <= value)
        {
            bestValue = value;
            bestMove  = rootMoves[i][0];
        }
    }

    return bestMove;
}

}  // namespace Search
}  // namespace DON
