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
#include <initializer_list>
#include <list>
#include <ratio>

#include "bitboard.h"
#include "evaluate.h"
#include "movepick.h"
#include "polybook.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include "ucioption.h"
#include "nnue/network.h"
#include "nnue/nnue_misc.h"

namespace DON {

// (*Scaler):
// Search features marked by "(*Scaler)" have proven non-linear scaling.
// They are optimized to time controls of 180 + 1.8 and longer,
// so changing them or adding conditions that are similar requires
// tests at these types of time controls.

namespace {

// Adjusts a mate or TB score from "plies to mate from the root"
// to "plies to mate from the current position". Standard scores are unchanged.
// The function is called before storing a value in the transposition table.
constexpr Value value_to_tt(Value v, std::int16_t ply) noexcept {

    if (!is_valid(v))
        return v;
    assert(is_ok(v));
    return is_win(v)  ? std::min(v + ply, +VALUE_MATE)
         : is_loss(v) ? std::max(v - ply, -VALUE_MATE)
                      : v;
}

// Inverse of value_to_tt(): it adjusts a mate or TB score
// from the transposition table (which refers to the plies to mate/be mated from
// current position) to "plies to mate/be mated (TB win/loss) from the root".
// However, to avoid potentially false mate or TB scores related to the 50 moves rule
// and the graph history interaction, return the highest non-TB score instead.
constexpr Value value_from_tt(Value v, std::int16_t ply, std::uint8_t rule50) noexcept {

    if (!is_valid(v))
        return v;
    assert(is_ok(v));
    // Handle TB win or better
    if (is_win(v))
    {
        // Downgrade a potentially false mate value
        if (is_mate_win(v) && VALUE_MATE - v > 2 * Position::DrawMoveCount - rule50)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        // Downgrade a potentially false TB value
        if (VALUE_TB - v > 2 * Position::DrawMoveCount - rule50)
            return VALUE_TB_WIN_IN_MAX_PLY - 1;

        return v - ply;
    }
    // Handle TB loss or worse
    if (is_loss(v))
    {
        // Downgrade a potentially false mate value
        if (is_mate_loss(v) && VALUE_MATE + v > 2 * Position::DrawMoveCount - rule50)
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
  Depth depth, bool pv, Bound bound, const Move& move, Value value, Value eval) noexcept {

    if (tte->key16 != key16)
    {
        tte = &ttc->entry[0];
        for (std::uint8_t i = 0; i < TTCluster::EntryCount; ++i)
        {
            if (ttc->entry[i].key16 == key16)
            {
                tte = &ttc->entry[i];
                break;
            }
            // Find an entry to be replaced according to the replacement strategy
            if (i != 0 && tte->worth(generation) > ttc->entry[i].worth(generation))
                tte = &ttc->entry[i];
        }
    }
    else
    {
        for (; tte > &ttc->entry[0] && (tte - 1)->key16 == key16; --tte)
            tte->clear();
    }

    tte->save(key16, depth, pv, bound, move, value_to_tt(value, ssPly), eval, generation);

    if (move != Move::None
        && depth - DEPTH_OFFSET + 2 * pv + 4 * (bound == BOUND_EXACT)
             >= std::max({ttc->entry[0].depth8, ttc->entry[1].depth8, ttc->entry[2].depth8}))
        ttc->move = move;
}

// clang-format off

namespace {

constexpr inline int MaxQuietThreshold = -7998;

// History
History<HCapture>           captureHistory;
History<HQuiet>               quietHistory;
History<HPawn>                 pawnHistory;
History<HContinuation> continuationHistory[2][2];

// Low Ply History
History<HLowPlyQuiet> lowPlyQuietHistory;

// Correction History
CorrectionHistory<CHPawn>                 pawnCorrectionHistory;
CorrectionHistory<CHMinor>               minorCorrectionHistory;
CorrectionHistory<CHMajor>               majorCorrectionHistory;
CorrectionHistory<CHNonPawn>           nonPawnCorrectionHistory;
CorrectionHistory<CHContinuation> continuationCorrectionHistory;

// Reductions lookup table initialized at startup
std::array<std::int16_t, MAX_MOVES> reductions;  // [depth or moveCount]

PolyBook polyBook;

constexpr int
reduction(Depth depth, std::uint8_t moveCount, int deltaRatio, bool improve) noexcept {
    int reductionScale = reductions[depth] * reductions[moveCount];
    int baseReduction  = 1132 + reductionScale - deltaRatio;
    assert(baseReduction >= 0);
    return baseReduction + 0.3730f * !improve * reductionScale;
}

// Futility margin
template<bool CutNode>
constexpr Value futility_margin(Depth depth, bool ttHit, bool improve, bool opworse) noexcept {
    return (depth - 2.0000f * improve - 0.3333f * opworse) * (111 - 25 * (CutNode && !ttHit));
}

// History and stats update bonus, based on depth
constexpr int stat_bonus(Depth depth) noexcept { return std::min(-98 + 158 * depth, 1622); }

// History and stats update malus, based on depth
constexpr int stat_malus(Depth depth) noexcept { return std::min(-243 + 802 * depth, 2850); }

// Add a small random value to draw evaluation to avoid 3-fold blindness
constexpr Value draw_value(Key key, std::uint64_t nodes) noexcept {
    return VALUE_DRAW + (key & 1) - (nodes & 1);
}

constexpr Bound bound_for_fail(bool failHigh) noexcept {
    return failHigh ? BOUND_LOWER : BOUND_UPPER;
}
constexpr Bound bound_for_fail(bool failHigh, bool failLow) noexcept {
    return failHigh ? BOUND_LOWER : failLow ? BOUND_UPPER : BOUND_NONE;
}

// Appends move and appends child Pv[]
void update_pv(Move* pv, const Move& move, const Move* childPv) noexcept {
    assert(move.is_ok());

    *pv++ = move;
    if (childPv != nullptr)
        while (*childPv != Move::None)
            *pv++ = *childPv++;
    *pv = Move::None;
}

void update_capture_history(Piece pc, Square dst, PieceType captured, int bonus) noexcept;
void update_capture_history(const Position& pos, const Move& m, int bonus) noexcept;

void update_quiet_history(Color ac, const Move& m, int bonus) noexcept;
void update_pawn_history(const Position& pos, Piece pc, Square dst, int bonus) noexcept;
void update_continuation_history(Stack* const ss, Piece pc, Square dst, int bonus) noexcept;
void update_low_ply_quiet_history(std::int16_t ssPly, const Move& m, int bonus) noexcept;
void update_all_quiet_history(const Position& pos, Stack* const ss, const Move& m, int bonus) noexcept;

void update_all_history(const Position& pos, Stack* const ss, Depth depth, const Move& bm, const std::array<Moves, 2>& moves) noexcept;

void update_correction_history(const Position& pos, Stack* const ss, int bonus) noexcept;
int  correction_value(const Position& pos, const Stack* const ss) noexcept;

Value adjust_static_eval(Value ev, int cv) noexcept;

void extend_tb_pv(Position&      rootPos,
                  RootMove&      rootMove,
                  Value&         value,
                  const Limit&   limit,
                  const Options& options) noexcept;

}  // namespace

namespace Search {

void init() noexcept {
    captureHistory.fill(-655);
      quietHistory.fill(65);
       pawnHistory.fill(-1215);
    for (bool inCheck : {false, true})
        for (bool capture : {false, true})
            for (auto& toPieceSqHist : continuationHistory[inCheck][capture])
                for (auto& pieceSqHist : toPieceSqHist)
                    pieceSqHist.fill(-493);
    continuationHistory[0][0][NO_PIECE][SQ_ZERO].fill(0);

       pawnCorrectionHistory.fill(4);
      minorCorrectionHistory.fill(0);
      majorCorrectionHistory.fill(0);
    nonPawnCorrectionHistory.fill(0);
    for (auto& toPieceSqCorrHist : continuationCorrectionHistory)
        for (auto& pieceSqCorrHist : toPieceSqCorrHist)
            pieceSqCorrHist.fill(0);
    continuationCorrectionHistory[NO_PIECE][SQ_ZERO].fill(0);

    reductions[0] = 0;
    for (std::size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = 22.95 * std::log(i);
}

void load_book(const std::string& bookFile) noexcept { polyBook.init(bookFile); }

}  // namespace Search

// clang-format on

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
void Worker::init() noexcept {  //
    accCaches.init(networks[numaAccessToken]);
}

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
            if (multiPV < 4)
                multiPV = 4;
    }
    if (multiPV > rootMoves.size())
        multiPV = rootMoves.size();

    // Non-main threads go directly to iterative_deepening()
    if (!mainManager)
    {
        iterative_deepening();
        return;
    }

    mainManager->callsCount     = limit.hitRate;
    mainManager->ponder         = limit.ponder;
    mainManager->ponderhitStop  = false;
    mainManager->sumMoveChanges = 0.0000f;
    mainManager->timeReduction  = 1.0000f;
    mainManager->skill.init(options);
    mainManager->timeManager.init(limit, rootPos, options);

    tt.update_generation(!limit.infinite);

    lowPlyQuietHistory.fill(95);

    bool think = false;

    if (rootMoves.empty())
    {
        rootMoves.emplace_back(Move::None);
        mainManager->updateCxt.onUpdateEnd({bool(rootPos.checkers())});
    }
    else
    {
        Move bookBestMove = Move::None;

        if (!limit.infinite && limit.mate == 0)
        {
            // Check polyglot book
            if (options["OwnBook"] && polyBook.enabled()
                && rootPos.move_num() < options["BookDepth"])
                bookBestMove = polyBook.probe(rootPos, options["BookBestPick"]);
        }

        if (bookBestMove != Move::None && rootMoves.contains(bookBestMove))
        {
            State st;
            ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

            rootPos.do_move(bookBestMove, st);
            Move bookPonderMove = polyBook.probe(rootPos, options["BookBestPick"]);
            rootPos.undo_move(bookBestMove);

            for (auto&& th : threads)
            {
                th->worker->rootMoves.swap_to_front(bookBestMove);
                if (bookPonderMove != Move::None)
                    th->worker->rootMoves.front().push_back(bookPonderMove);
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
    while (!threads.stop && (mainManager->ponder || limit.infinite))
    {}  // Busy wait for a stop or a mainManager->ponder reset

    // Stop the threads if not already stopped
    // (also raise the stop if "ponderhit" just reset mainManager->ponder).
    threads.stop = true;

    // Wait until all threads have finished
    threads.wait_finish();

    // When playing in 'Nodes as Time' mode,
    // subtract the searched nodes from the start nodes before exiting.
    if (mainManager->timeManager.use_nodes_time())
        mainManager->timeManager.update_nodes(  //
          threads.nodes() - limit.clocks[rootPos.active_color()].inc);

    Worker* bestWorker = this;

    if (think)
    {
        // If the skill level is enabled, swap the best PV line with the sub-optimal one
        if (mainManager->skill.enabled())
            rootMoves.swap_to_front(mainManager->skill.pick_best_move(rootMoves, multiPV, false));

        else if (multiPV == 1 && threads.size() > 1 && limit.mate == 0
                 && rootMoves.front()[0] != Move::None)
        {
            bestWorker = threads.best_thread()->worker.get();

            // Send PV info again if have a new best worker
            if (bestWorker != this)
                mainManager->show_pv(*bestWorker, bestWorker->completedDepth);
        }

        if (limit.use_time_manager())
        {
            mainManager->moveFirst        = false;
            mainManager->preBestCurValue  = bestWorker->rootMoves.front().curValue;
            mainManager->preBestAvgValue  = bestWorker->rootMoves.front().avgValue;
            mainManager->preTimeReduction = mainManager->timeReduction;
        }
    }

    // clang-format off
    assert(!bestWorker->rootMoves.empty()
        && !bestWorker->rootMoves.front().empty());
    Move bestMove   = bestWorker->rootMoves.front()[0];
    Move ponderMove = bestMove != Move::None
                  && (bestWorker->rootMoves.front().size() >= 2 || bestWorker->ponder_move_extracted())
                    ? bestWorker->rootMoves.front()[1] : Move::None;
    // clang-format on
    mainManager->updateCxt.onUpdateMove({bestMove, ponderMove});
}

// Main iterative deepening loop. It calls search() repeatedly with increasing depth
// until the allocated thinking time has been consumed, the user stops the search,
// or the maximum search depth is reached.
void Worker::iterative_deepening() noexcept {
    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    // Allocate stack with extra size to allow access from (ss - 9) to (ss + 1):
    // (ss - 9) is needed for update_continuation_history(ss - 1) which accesses (ss - 8),
    // (ss + 1) is needed for initialization of cutoffCount.
    constexpr std::uint16_t STACK_OFFSET = 9;
    constexpr std::uint16_t STACK_SIZE   = STACK_OFFSET + (MAX_PLY + 1) + 1;
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
            (ss + i)->pieceSqHistory           = &continuationHistory[0][0][NO_PIECE][SQ_ZERO];
            (ss + i)->pieceSqCorrectionHistory = &continuationCorrectionHistory[NO_PIECE][SQ_ZERO];
        }
    }
    assert(stack[0].ply == -STACK_OFFSET && stack[STACK_SIZE - 1].ply == MAX_PLY + 1);
    assert(ss->ply == 0);
    // clang-format on

    std::vector<Move> pv(MAX_PLY + 1);

    ss->pv = pv.data();

    Color ac = rootPos.active_color();

    std::uint16_t researchCnt = 0;

    Value bestValue = -VALUE_INFINITE;

    Moves lastBestPV{Move::None};
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
            mainManager->sumMoveChanges *= 0.5000f;

        // Save the last iteration's scores before the first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (auto& rm : rootMoves)
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

            auto avgValue    = rootMoves[curIdx].avgValue;
            auto avgSqrValue = rootMoves[curIdx].avgSqrValue;
            if (avgValue == -VALUE_INFINITE)
                avgValue = VALUE_ZERO;
            if (avgSqrValue == sign_sqr(-VALUE_INFINITE))
                avgSqrValue = VALUE_ZERO;

            // Reset aspiration window starting size
            int   delta = 5 + 76.9230e-6f * std::abs(avgSqrValue);
            Value alpha = std::max(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min(avgValue + delta, +VALUE_INFINITE);

            // Adjust optimism based on root move's avgValue
            optimism[ac]  = 138 * avgValue / (81 + std::abs(avgValue));
            optimism[~ac] = -optimism[ac];

            // Start with a small aspiration window and, in the case of a fail
            // high/low, research with a bigger window until don't fail high/low anymore.
            std::uint16_t failHighCnt = 0;
            while (true)
            {
                nmpMinPly = 0;
                rootDelta = beta - alpha;
                assert(rootDelta > 0);
                // Adjust the effective depth searched, but ensure at least one
                // effective increment for every 4 researchCnt steps.
                Depth adjustedDepth = rootDepth - failHighCnt - 0.75f * (1 + researchCnt);
                if (adjustedDepth < 1)
                    adjustedDepth = 1;

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
                    if (mainManager && mainManager->ponder)
                        mainManager->ponderhitStop = false;
                }
                else if (bestValue >= beta)
                {
                    beta = std::min(bestValue + delta, +VALUE_INFINITE);

                    ++failHighCnt;
                }
                else
                    break;

                delta *= 1.3333f;

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
                && !(threads.abort && is_loss(rootMoves.front().uciValue)))
                mainManager->show_pv(*this, rootDepth);

            if (threads.stop)
                break;
        }

        if (!threads.stop)
            completedDepth = rootDepth;

        // Make sure not to pick an unproven mated-in score,
        // in case this worker prematurely stopped the search (aborted-search).
        if (threads.abort && lastBestPV[0] != Move::None
            && (rootMoves.front().curValue != -VALUE_INFINITE
                && is_loss(rootMoves.front().curValue)))
        {
            // Bring the last best rootmove to the front for best thread selection.
            rootMoves.move_to_front([&lastBestPV = std::as_const(lastBestPV)](
                                      const auto& rm) noexcept { return rm == lastBestPV[0]; });
            rootMoves.front().pv       = lastBestPV;
            rootMoves.front().curValue = lastBestCurValue;
            rootMoves.front().preValue = lastBestPreValue;
            rootMoves.front().uciValue = lastBestUciValue;
        }
        else if (rootMoves.front()[0] != lastBestPV[0])
        {
            lastBestPV       = rootMoves.front().pv;
            lastBestCurValue = rootMoves.front().curValue;
            lastBestPreValue = rootMoves.front().preValue;
            lastBestUciValue = rootMoves.front().uciValue;
            lastBestDepth    = completedDepth;
        }

        if (!mainManager)
            continue;

        // Have found a "mate in x"?
        if (limit.mate != 0 && rootMoves.front().curValue == rootMoves.front().uciValue
            && ((rootMoves.front().curValue != +VALUE_INFINITE
                 && is_mate_win(rootMoves.front().curValue)
                 && VALUE_MATE - rootMoves.front().curValue <= 2 * limit.mate)
                || (rootMoves.front().curValue != -VALUE_INFINITE
                    && is_mate_loss(rootMoves.front().curValue)
                    && VALUE_MATE + rootMoves.front().curValue <= 2 * limit.mate)))
            threads.stop = true;

        // If the skill level is enabled and time is up, pick a sub-optimal best move
        if (mainManager->skill.enabled() && mainManager->skill.time_to_pick(rootDepth))
            mainManager->skill.pick_best_move(rootMoves, multiPV);

        // Do have time for the next iteration? Can stop searching now?
        if (!(mainManager->ponderhitStop || threads.stop) && limit.use_time_manager())
        {
            // Use part of the gained time from a previous stable move for the current move
            for (auto&& th : threads)
            {
                mainManager->sumMoveChanges += th->worker->moveChanges;
                th->worker->moveChanges = 0;
            }

            // clang-format off

            // Compute evaluation inconsistency based on differences from previous best scores
            float inconsistencyFactor = std::clamp(0.11396f
                                                 + 0.02035f * (mainManager->preBestAvgValue - bestValue)
                                                 + 0.00968f * (mainManager->preBestCurValue - bestValue),
                                            0.9999f - 0.4213f * !mainManager->moveFirst,
                                            1.0001f + 0.6751f * !mainManager->moveFirst);

            // Compute stable depth (differnce between the current search depth and the last best depth)
            Depth stableDepth = completedDepth - lastBestDepth;
            assert(stableDepth >= DEPTH_ZERO);

            // Compute stability factor from the stable depth. This factor is used to reduce time if the best move remains stable
            float stabilityFactor = std::ceil(stableDepth / (3.0f + 2.0f * std::log10((1.0f + stableDepth) / 2.0f)) - 1.27f);

            // Use the stability factor to adjust the time reduction
            mainManager->timeReduction = 0.7046f + 0.39055f * std::clamp(stabilityFactor, 0.0f, 3.0f);

            // Compute ease factor that factors in previous time reduction
            float easeFactor = 0.46311f * (1.4540f + mainManager->preTimeReduction) / mainManager->timeReduction;

            // Compute move instability factor based on the total move changes and the number of threads
            float instabilityFactor = 0.9929f + 1.8519f * mainManager->sumMoveChanges / threads.size();

            // Compute node effort factor which slightly reduces effort if the completed depth is sufficiently high
            float nodeEffortFactor = 1.0f;
            if (completedDepth >= 10)
                nodeEffortFactor -= 70.7929e-6f * std::max(-95056.0f + 100000.0f * rootMoves.front().nodes / std::max(std::uint64_t(nodes), std::uint64_t(1)), 0.0f);

            // Compute recapture factor that reduces time if recapture conditions are met
            float recaptureFactor = 1.0f;
            if ( rootPos.cap_square() == rootMoves.front()[0].dst_sq()
             && (rootPos.cap_square() & rootPos.pieces(~ac))
             && rootPos.see(rootMoves.front()[0]) >= 200)
                recaptureFactor -= 13.8400e-3f * std::min<int>(stableDepth, 25);

            // Calculate total time by combining all factors with the optimum time
            float totalTime = mainManager->timeManager.optimum() * inconsistencyFactor * easeFactor * instabilityFactor * nodeEffortFactor * recaptureFactor;
            assert(totalTime >= 0.0f);

            // Cap totalTime in case of a single legal move for a better viewer experience
            if (rootMoves.size() == 1)
                totalTime = std::min(0.5000f * totalTime, 500.0f);

            TimePoint elapsedTime = mainManager->elapsed(threads);

            // Stop the search if have exceeded the totalTime
            if (elapsedTime > totalTime)
            {
                // If allowed to ponder do not stop the search now but
                // keep pondering until the GUI sends "ponderhit" or "stop".
                if (mainManager->ponder)
                    mainManager->ponderhitStop = true;
                else
                    threads.stop = true;
            }

            if (!mainManager->ponder)
                threads.research = elapsedTime > 0.5138f * totalTime;

            // clang-format on

            mainManager->preBestCurValue = bestValue;
        }
    }
}

// Main search function for different type of nodes.
template<NodeType NT>
// clang-format off
Value Worker::search(Position& pos, Stack* const ss, Value alpha, Value beta, Depth depth, std::int8_t red, const Move& excludedMove) noexcept {
    // clang-format on
    constexpr bool RootNode = NT == Root;
    constexpr bool PVNode   = RootNode || NT == PV;
    constexpr bool CutNode  = NT == Cut;  // !PVNode
    constexpr bool AllNode  = NT == All;  // !PVNode
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (1 + alpha == beta));
    assert((RootNode && ss->ply == 0) || ss->ply > 0);
    assert(!RootNode || (DEPTH_ZERO < depth && depth < MAX_PLY));

    Color ac  = pos.active_color();
    Key   key = pos.key();

    if constexpr (!RootNode)
    {
        // Dive into quiescence search when depth <= DEPTH_ZERO
        if (depth <= DEPTH_ZERO)
            return qsearch<PVNode>(pos, ss, alpha, beta);

        // Check if have an upcoming move that draws by repetition
        if (alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
        {
            alpha = draw_value(key, nodes);
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

    std::vector<Move> pv(MAX_PLY + 1 - ss->ply);

    if constexpr (PVNode)
    {
        // Update selDepth (selDepth from 1, ply from 0)
        if (selDepth < 1 + ss->ply)
            selDepth = 1 + ss->ply;
    }

    // Step 1. Initialize node
    ss->inCheck   = bool(pos.checkers());
    ss->moveCount = 0;
    ss->history   = 0;

    if constexpr (!RootNode)
    {
        // Step 2. Check for stopped search and immediate draw
        if (threads.stop.load(std::memory_order_relaxed)  //
            || ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
            return ss->ply >= MAX_PLY && !ss->inCheck ? evaluate(pos) : draw_value(key, nodes);

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

    // Step 4. Transposition table lookup
    Key16 key16 = compress_key(key);

    auto [ttd, tte, ttc] = tt.probe(key, key16);
    TTUpdater ttu(tte, ttc, key16, ss->ply, tt.generation());

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttd.move  = RootNode ? rootMoves[curIdx][0]
              : ttd.hit  ? extract_tt_move(pos, ttd.move)
                         : Move::None;

    Move pttm      = ttd.move != Move::None   ? ttd.move
                   : tte->move() != ttc->move ? extract_tt_move(pos, ttc->move, false)
                                              : Move::None;
    ss->ttMove     = ttd.move;
    bool ttCapture = ttd.move != Move::None && pos.capture_promo(ttd.move);

    // At this point, if excludedMove, skip straight to step 6, static evaluation.
    // However, to save indentation, list the condition in all code between here and there.
    const bool exclude = excludedMove != Move::None;

    ss->pvHit = exclude ? ss->pvHit : PVNode || (ttd.hit && ttd.pv);

    Move preMove = (ss - 1)->move;
    auto preSq   = preMove.is_ok() ? preMove.dst_sq() : SQ_NONE;

    bool preCapture = pos.captured_piece() != NO_PIECE;
    bool preNonPawn =
      is_ok(preSq) && type_of(pos.piece_on(preSq)) != PAWN && preMove.type_of() != PROMOTION;

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && !exclude && is_valid(ttd.value)        //
        && (CutNode == (ttd.value >= beta) || depth > 9)  //
        && ttd.depth > depth - (ttd.value <= beta)        //
        && (ttd.bound & bound_for_fail(ttd.value >= beta)) != 0)
    {
        // If ttMove fails high, update move sorting heuristics on TT hit
        if (ttd.move != Move::None && ttd.value >= beta)
        {
            // Bonus for a quiet ttMove
            if (!ttCapture)
                update_all_quiet_history(pos, ss, ttd.move, +0.7656f * stat_bonus(depth));

            // Extra penalty for early quiet moves of the previous ply
            if (is_ok(preSq) && !preCapture && (ss - 1)->moveCount <= 3)
                update_continuation_history(ss - 1, pos.piece_on(preSq), preSq,
                                            -0.9941f * stat_malus(depth + 1));
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < (1.0f - 0.5f * pos.rule50_high()) * Position::rule50_threshold())
        {
            if (ttd.value > beta && ttd.depth > DEPTH_ZERO && !is_decisive(ttd.value))
            {
                ttd.value = in_range((ttd.depth * ttd.value + beta) / (ttd.depth + 1));
            }

            return ttd.value;
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
            Tablebases::ProbeState ps;
            Tablebases::WDLScore   wdl;
            wdl = Tablebases::probe_wdl(pos, &ps);

            // Force check of time on the next occasion
            if (is_main_worker())
                main_manager()->callsCount = 1;

            if (ps != Tablebases::FAIL)
            {
                tbHits.fetch_add(1, std::memory_order_relaxed);

                std::int8_t drawValue = 1 * tbConfig.rule50Use;

                Value tbValue = VALUE_TB - ss->ply;

                // Use the range VALUE_TB to VALUE_TB_WIN_IN_MAX_PLY to value
                value = wdl < -drawValue ? -tbValue
                      : wdl > +drawValue ? +tbValue
                                         : 2 * wdl * drawValue;

                Bound bound = wdl < -drawValue ? BOUND_UPPER
                            : wdl > +drawValue ? BOUND_LOWER
                                               : BOUND_EXACT;

                if (bound == BOUND_EXACT || bound == bound_for_fail(value >= beta, value <= alpha))
                {
                    depth = std::min(depth + 6, MAX_PLY - 1);
                    ttu.update(depth, ss->pvHit, bound, Move::None, value, VALUE_NONE);

                    return value;
                }

                if constexpr (PVNode)
                {
                    if (bound == BOUND_LOWER)
                    {
                        bestValue = value;
                        if (alpha < value)
                            alpha = value;
                    }
                    else
                        maxValue = value;
                }
            }
        }
    }

    int correctionValue = ss->inCheck ? 0 : correction_value(pos, ss);

    int absCorrectionValue = std::abs(correctionValue);

    Value unadjustedStaticEval, eval;

    bool improve, opworse;

    Value probCutBeta;

    Move move;

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    // Step 6. Static evaluation of the position
    if (ss->inCheck)
    {
        unadjustedStaticEval = VALUE_NONE;

        eval = ss->staticEval = (ss - 2)->staticEval;

        improve = opworse = false;

        // Skip early pruning when in check
        goto S_MOVES_LOOP;
    }

    if (exclude)
    {
        assert(is_ok(ss->staticEval));

        unadjustedStaticEval = eval = ss->staticEval;
        // Providing the hint that this node's accumulator will often be used
        NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);
    }
    else if (ttd.hit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = ttd.eval;
        if (!is_valid(unadjustedStaticEval))
            unadjustedStaticEval = evaluate(pos);
        else if constexpr (PVNode)
            NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);

        eval = ss->staticEval = adjust_static_eval(unadjustedStaticEval, correctionValue);

        // Can ttValue be used as a better position evaluation
        if (is_valid(ttd.value) && (ttd.bound & bound_for_fail(ttd.value > eval)) != 0)
            eval = ttd.value;
    }
    else
    {
        unadjustedStaticEval = evaluate(pos);

        eval = ss->staticEval = adjust_static_eval(unadjustedStaticEval, correctionValue);

        ttu.update(DEPTH_NONE, ss->pvHit, BOUND_NONE, Move::None, VALUE_NONE, unadjustedStaticEval);
    }

    // Set up the improve flag, which is true if the current static evaluation
    // is bigger than the previous static evaluation at our turn
    // (if in check at previous move go back until not in check).
    // The improve flag is used in various pruning heuristics.
    improve = !(ss - 2)->inCheck && ss->staticEval > +(ss - 2)->staticEval;
    opworse = (ss - 1)->inCheck || ss->staticEval > -(ss - 1)->staticEval;

    // Retroactive LMR adjustments
    if (red > 2 && depth < MAX_PLY - 1 && !opworse)
        ++depth;
    if (red > 0 && depth > 1 && ((ss - 1)->inCheck || ss->staticEval > 200 - (ss - 1)->staticEval))
        --depth;

    // Use static evaluation difference to improve quiet move ordering
    if (is_ok(preSq) && !preCapture && !(ss - 1)->inCheck)
    {
        int bonus = 638 + std::clamp(-10 * (ss->staticEval + (ss - 1)->staticEval), -1906, +1450);

        update_quiet_history(~ac, preMove, +1.1094f * bonus);
        if (preNonPawn)
            update_pawn_history(pos, pos.piece_on(preSq), preSq, +1.1670f * bonus);
    }

    // Step 7. Razoring
    // If eval is really low, skip search entirely and return the qsearch value.
    if (!PVNode && eval < -446 + alpha - 303 * sqr(depth))
        return qsearch<false>(pos, ss, alpha, beta);

    // Step 8. Futility pruning: child node
    // The depth condition is important for mate finding.
    if (!ss->pvHit && depth < 15 && eval >= beta && (ttd.move == Move::None || ttCapture)
        && !is_loss(beta) && !is_win(eval)
        && eval - futility_margin<CutNode>(depth, ttd.hit, improve, opworse)
               + (37 - 7.5289e-6f * absCorrectionValue) - 3.0675e-3f * (ss - 1)->history
             >= beta)
        return in_range((2 * eval + beta) / 3);

    // Step 9. Null move search with verification search
    if (CutNode && !exclude && pos.non_pawn_material(ac) && preMove != Move::Null && !is_loss(beta)
        && eval >= beta && ss->ply >= nmpMinPly
        && ss->staticEval >= 455 + beta - 21 * depth - 60 * improve)
    {
        int diff = eval - beta;
        assert(diff >= 0);

        // Null move dynamic reduction based on depth, eval and phase
        Depth R = 4 + 0.3333f * depth + std::min(4.2194e-3f * diff, 6.0f) + 0.1111f * pos.phase();
        if (R > depth)
            R = depth;

        pos.do_null_move(st);

        // Speculative prefetch as early as possible
        tt.prefetch_key(pos.key());
        nodes.fetch_add(1, std::memory_order_relaxed);
        // clang-format off
        ss->move                     = Move::Null;
        ss->pieceSqHistory           = &continuationHistory[0][0][NO_PIECE][SQ_ZERO];
        ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[NO_PIECE][SQ_ZERO];
        // clang-format on

        Value nullValue = -search<All>(pos, ss + 1, -beta, -beta + 1, depth - R);

        pos.undo_null_move();

        // Do not return unproven mate or TB scores
        if (nullValue >= beta && !is_win(nullValue))
        {
            assert(!is_loss(nullValue));

            if (nmpMinPly != 0 || depth < 16)
                return nullValue;

            assert(nmpMinPly == 0);  // Recursive verification is not allowed

            // Do verification search at high depths,
            // with null move pruning disabled until ply exceeds nmpMinPly.
            nmpMinPly = ss->ply + 0.75f * (depth - R);

            Value v = search<All>(pos, ss, beta - 1, beta, depth - R);

            nmpMinPly = 0;

            if (v >= beta)
                return nullValue;

            ss->ttMove = ttd.move;
        }
    }

    improve = improve || ss->staticEval >= 101 + beta;

    // Step 10. Internal iterative reductions
    // Decrease depth for PVNode or deep CutNode without ttMove. (*Scaler)
    if ((PVNode || CutNode) && depth > 2 + 3 * CutNode && ttd.move == Move::None)
        --depth;

    assert(depth > DEPTH_ZERO);

    // Step 11. ProbCut
    // If have a good enough capture or any promotion and a reduced search
    // returns a value much above beta, can (almost) safely prune previous move.
    probCutBeta = std::min(187 + beta - 55 * improve, +VALUE_INFINITE - 1);
    if (depth >= 3
        && !is_decisive(beta)
        // If value from transposition table is atleast probCutBeta
        && is_valid(ttd.value) && !is_decisive(ttd.value) && ttd.value >= probCutBeta)
    {
        assert(beta < probCutBeta && probCutBeta < +VALUE_INFINITE);

        Depth probCutDepth     = std::max(depth - 4, 0);
        int   probCutThreshold = probCutBeta - ss->staticEval;

        MovePicker mp(pos, pttm, &captureHistory, probCutThreshold);
        // Loop through all pseudo-legal moves
        while ((move = mp.next_move()) != Move::None)
        {
            assert(pos.pseudo_legal(move));
            assert(pos.capture_promo(move) && pos.see(move) >= probCutThreshold);

            // Check for legality
            if (move == excludedMove || !pos.legal(move))
                continue;

            // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
            // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
            if (RootNode && !rootMoves.contains(curIdx, lstIdx, move))
                continue;

            Square dst        = move.dst_sq();
            Piece  movedPiece = pos.moved_piece(move);

            pos.do_move(move, st);

            // Speculative prefetch as early as possible
            tt.prefetch_key(pos.key());
            nodes.fetch_add(1, std::memory_order_relaxed);
            // clang-format off
            ss->move                     = move;
            ss->pieceSqHistory           = &continuationHistory[ss->inCheck][true][movedPiece][dst];
            ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[movedPiece][dst];
            // clang-format on

            // Perform a preliminary qsearch to verify that the move holds
            value = -qsearch<false>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (value >= probCutBeta && probCutDepth > DEPTH_ZERO)
                value = -search<~NT>(pos, ss + 1, -probCutBeta, -probCutBeta + 1, probCutDepth);

            pos.undo_move(move);

            assert(is_ok(value));

            if (threads.stop.load(std::memory_order_relaxed))
                return VALUE_ZERO;

            if (value >= probCutBeta)
            {
                assert(!is_loss(value));

                // Save ProbCut data into transposition table
                ttu.update(probCutDepth + 1, ss->pvHit, BOUND_LOWER, move, value,
                           unadjustedStaticEval);

                if (!is_win(value))
                    return value - (probCutBeta - beta);
            }
        }
    }

S_MOVES_LOOP:  // When in check, search starts here

    // Step 12. Small ProbCut idea
    probCutBeta = std::min(413 + beta, VALUE_TB_WIN_IN_MAX_PLY - 1);
    if (!is_decisive(beta) && is_valid(ttd.value) && !is_decisive(ttd.value)
        && ttd.value >= probCutBeta && ttd.depth >= depth - 4 && (ttd.bound & BOUND_LOWER))
        return probCutBeta;

    if (!ss->inCheck && ttd.hit && ttd.move == Move::None && tte->move() != Move::None)
    {
        ttd.move   = extract_tt_move(pos, tte->move(), false);
        pttm       = ttd.move != Move::None   ? ttd.move
                   : tte->move() != ttc->move ? extract_tt_move(pos, ttc->move, false)
                                              : Move::None;
        ss->ttMove = ttd.move;
        ttCapture  = ttd.move != Move::None && pos.capture_promo(ttd.move);
    }
    assert(ss->ttMove == ttd.move);

    value = bestValue;

    Move bestMove = Move::None;

    auto pawnIndex = pawn_index(pos.pawn_key());

    std::uint8_t moveCount  = 0;
    std::uint8_t promoCount = 0;

    std::array<Moves, 2> moves;

    Value singularValue = +VALUE_INFINITE;

    const History<HPieceSq>* contHistory[8]{(ss - 1)->pieceSqHistory, (ss - 2)->pieceSqHistory,
                                            (ss - 3)->pieceSqHistory, (ss - 4)->pieceSqHistory,
                                            (ss - 5)->pieceSqHistory, (ss - 6)->pieceSqHistory,
                                            (ss - 7)->pieceSqHistory, (ss - 8)->pieceSqHistory};

    int quietThreshold = std::min((-3560 - 10 * improve) * depth, MaxQuietThreshold);

    MovePicker mp(pos, pttm, &captureHistory, &quietHistory, &pawnHistory, contHistory,
                  &lowPlyQuietHistory, ss->ply, quietThreshold);
    mp.quietPick = true;
    // Step 13. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None)
    {
        assert(pos.pseudo_legal(move));

        // Check for legality
        if (move == excludedMove || !pos.legal(move))
            continue;

        // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
        // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
        if (RootNode && !rootMoves.contains(curIdx, lstIdx, move))
            continue;

        ss->moveCount = ++moveCount;
        promoCount += move.type_of() == PROMOTION && move.promotion_type() != QUEEN;

        if (RootNode && is_main_worker() && rootDepth > 30 && !options["ReportMinimal"])
            main_manager()->updateCxt.onUpdateIter({rootDepth, move, curIdx + moveCount});

        if constexpr (PVNode)
            (ss + 1)->pv = nullptr;

        Square dst = move.dst_sq();

        Piece movedPiece = pos.moved_piece(move);

        bool check    = pos.check(move);
        bool dblCheck = check && pos.dbl_check(move);
        bool capture  = pos.capture_promo(move);
        auto captured = capture ? pos.captured(move) : NO_PIECE_TYPE;

        // Calculate new depth for this move
        Depth newDepth = depth - 1;

        int deltaRatio = 735 * (beta - alpha) / rootDelta;

        int r = reduction(depth, moveCount, deltaRatio, improve);

        // Increase reduction for ttPv nodes (*Scaler)
        // Smaller or even negative value is better for short time controls
        // Bigger value is better for long time controls
        r += 1031 * ss->pvHit - 32 * moveCount;

        // Step 14. Pruning at shallow depth
        // Depth conditions are important for mate finding.
        if (!RootNode && !is_loss(bestValue))
        {
            // Skip quiet moves if moveCount exceeds Futility Move Count threshold
            mp.quietPick = mp.quietPick
                        && moveCount < ((3 + sqr(depth)) >> (!improve)) + promoCount
                                         - (!improve && singularValue < -80 + alpha);

            // Reduced depth of the next LMR search
            Depth lmrDepth = newDepth - r / 1024;

            if (capture)
            {
                int captHist = captureHistory[movedPiece][dst][captured];

                // Futility pruning for captures not check
                if (!ss->inCheck && lmrDepth < 7 && !check)
                {
                    Value futilityValue =
                      std::min(242 + ss->staticEval + PIECE_VALUE[captured] + promotion_value(move)
                                 + int(0.1357f * captHist) + 238 * lmrDepth,
                               VALUE_TB_WIN_IN_MAX_PLY - 1);
                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures
                int seeHist = std::clamp(int(0.0278f * captHist), -153 * depth, +134 * depth);
                if (pos.see(move) < -(seeHist + 157 * depth + 256 * dblCheck))
                    continue;
            }
            else
            {
                int contHist = (*contHistory[0])[movedPiece][dst]  //
                             + (*contHistory[1])[movedPiece][dst]  //
                             + pawnHistory[pawnIndex][movedPiece][dst];

                // Continuation history based pruning
                if (contHist < -4107 * depth)
                    continue;

                contHist += 2.1250f * quietHistory[ac][move.org_dst()];

                lmrDepth += 27.9642e-5f * contHist;

                // Futility pruning for quiets not check
                if (!ss->inCheck && lmrDepth < 13 && !check)
                {
                    Value futilityValue =
                      std::min(49 + ss->staticEval + 86 * (bestMove == Move::None) + 150 * lmrDepth,
                               VALUE_TB_WIN_IN_MAX_PLY - 1);
                    if (futilityValue <= alpha)
                    {
                        if (bestValue < futilityValue)
                            bestValue = futilityValue;
                        continue;
                    }
                }

                if (lmrDepth < 0)
                    lmrDepth = 0;

                // SEE based pruning for quiets
                if (pos.see(move) < -(26 * sqr(lmrDepth) + 256 * dblCheck))
                    continue;
            }
        }

        // Step 15. Extensions
        Depth extension = DEPTH_ZERO;
        // Take care to not overdo to avoid search getting stuck
        if (ss->ply <= 2 * (2 + rootDepth))
        {
            // Singular extension search. If all moves but one fail low on a search
            // of (alpha-s, beta-s), and just one fails high on (alpha, beta), then
            // that move is singular and should be extended.
            // To verify this do a reduced search on the position excluding the ttMove and
            // if the result is lower than ttValue minus a margin, then will extend the ttMove.
            // Recursive singular search is avoided.

            // Note:
            // Generally, tweaks that make extensions more frequent scale well.
            // This includes higher values of singularBeta (i.e closer to ttValue)
            // and lower extension margins. (*Scaler)
            if (!RootNode && !exclude && move == ttd.move
                && depth > 4 - (completedDepth > 32) + ss->pvHit   //
                && is_valid(ttd.value) && !is_decisive(ttd.value)  //
                && ttd.depth >= depth - 3 && (ttd.bound & BOUND_LOWER))
            {
                // clang-format off
                Value singularBeta  = ttd.value - (0.9483f + 1.3966f * (!PVNode && ss->pvHit)) * depth;
                Depth singularDepth = 0.5f * newDepth;
                assert(singularDepth > DEPTH_ZERO);

                value = search<~~NT>(pos, ss, singularBeta - 1, singularBeta, singularDepth, 0, move);

                ss->moveCount = moveCount;

                if (value < singularBeta)
                {
                    singularValue = value;

                    int doubleMargin =  0 + 267 * PVNode - 181 * !ttCapture +   0 * ss->pvHit - 3.7724e-6f * absCorrectionValue;
                    int tripleMargin = 96 + 282 * PVNode - 250 * !ttCapture + 103 * ss->pvHit - 3.9420e-6f * absCorrectionValue;

                    extension = 1 + (value < singularBeta - doubleMargin)
                                  + (value < singularBeta - tripleMargin);

                    if (depth < MAX_PLY - 1)
                        ++depth;
                }
                // clang-format on

                // Multi-cut pruning
                // If the ttMove is assumed to fail high based on the bound of the TT entry, and
                // if after excluding the ttMove with a reduced search fail high over the original beta,
                // assume this expected cut-node is not singular (multiple moves fail high),
                // and can prune the whole subtree by returning a soft-bound.
                else if (value >= beta && !is_win(value))
                {
                    assert(!is_loss(value));
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

            // Check extension
            else if (check && depth > 12 && pos.rule50_count() < 10
                     && pos.see(move) > (0 - 256 * dblCheck))
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;

        [[maybe_unused]] std::uint64_t preNodes;
        if constexpr (RootNode)
            preNodes = std::uint64_t(nodes);

        // Step 16. Make the move
        pos.do_move(move, st, check);

        // Speculative prefetch as early as possible
        tt.prefetch_key(pos.key());
        nodes.fetch_add(1, std::memory_order_relaxed);
        // Update the move (this must be done after singular extension search)
        // clang-format off
        ss->move                     = move;
        ss->pieceSqHistory           = &continuationHistory[ss->inCheck][capture][movedPiece][dst];
        ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[movedPiece][dst];
        // clang-format on

        ss->history = capture ? 6.88 * PIECE_VALUE[captured] + 3 * promotion_value(move)  //
                                  + captureHistory[movedPiece][dst][captured] - 4653
                              : 2 * quietHistory[ac][move.org_dst()]    //
                                  + (*contHistory[0])[movedPiece][dst]  //
                                  + (*contHistory[1])[movedPiece][dst] - 3591;

        // Decrease reduction if position is or has been on the PV (*Scaler)
        r -= (2230                                                //
              + 925 * (is_valid(ttd.value) && ttd.value > alpha)  //
              + 971 * (ttd.depth >= depth))
           * ss->pvHit;

        // Decrease reduction for PvNodes (*Scaler)
        r -= 1013 * PVNode;

        // These reduction adjustments have no proven non-linear scaling.

        // Adjust reduction with move count and correction value
        r += 316 - 32 * moveCount - 1024 * dblCheck - 31.6776e-6f * absCorrectionValue;

        // Increase reduction for cut nodes
        r += (2608 - 1159 * (ss->pvHit && ttd.depth >= depth)) * CutNode;

        // Increase reduction if ttMove is a capture and the current move is not a capture
        r += (1123 + 982 * (depth < 8)) * (ttCapture && !capture);

        // Increase reduction on repetition
        r += 2048 * (move == (ss - 4)->move && pos.repetition() == 4);

        r += ss->cutoffCount > 3  //
             ?
             // Increase reduction if next ply has a lot of fail high
               981 + 833 * AllNode
             // Decrease reduction for first picked move (ttMove)
             : -1982 * (move == ttd.move);

        // Decrease/Increase reduction for moves with a good/bad history
        r -= 85.8764e-3f * ss->history;

        // Step 17. Late moves reduction / extension (LMR)
        if (depth > 1 && moveCount > 1)
        {
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth redDepth =
              std::max(1, std::min(newDepth - r / 1024,
                                   newDepth + !AllNode + (PVNode && bestMove == Move::None)));

            value = -search<Cut>(pos, ss + 1, -(alpha + 1), -alpha, redDepth, newDepth - redDepth);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && newDepth > redDepth)
            {
                // Adjust full-depth search based on LMR value
                newDepth +=
                  // - if the value was good enough search deeper
                  +(value > 41 + bestValue + 2 * newDepth)
                  // - if the value was bad enough search shallower
                  - (value < 9 + bestValue);

                if (newDepth > redDepth)
                    value = -search<~NT>(pos, ss + 1, -(alpha + 1), -alpha, newDepth);

                if (value >= beta)
                {
                    // Post LMR continuation history updates
                    update_continuation_history(ss, movedPiece, dst, 2010);
                }
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Increase reduction if ttMove is not present
            r += 1111 * (ttd.move == Move::None);

            // Reduce search depth if expected reduction is high
            value =
              -search<~NT>(pos, ss + 1, -(alpha + 1), -alpha, newDepth - ((r > 3554) + (r > 5373)));
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if (PVNode && (moveCount == 1 || value > alpha))
        {
            pv[0]        = Move::None;
            (ss + 1)->pv = pv.data();

            // Extend if about to dive into qsearch
            if (newDepth < 1 && rootDepth > 6 && move == ttd.move && tt.lastHashFull <= 960)
                newDepth = 1;

            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth);
        }

        // Step 19. Undo move
        pos.undo_move(move);

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
            assert(rm[0] == move);

            rm.nodes += nodes - preNodes;
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

                rm.resize(1);

                const Move* childPv = (ss + 1)->pv;
                assert(childPv != nullptr);
                while (*childPv != Move::None)
                    rm.push_back(*childPv++);

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
        bool inc = value == bestValue && (nodes & 0xF) == 0 && 2 + ss->ply >= rootDepth
                && !is_win(value + 1);

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
                    // If make cutoffCount increment more often (*Scaler)
                    (ss - 1)->cutoffCount += PVNode || (extension < 2);
                    break;  // Fail-high
                }

                alpha = value;  // Update alpha! Always alpha < beta

                // Reduce other moves if have found at least one score improvement
                if (depth < 18 && !is_decisive(value))
                    depth = std::max(depth - (depth < 8 ? 3 : depth < 14 ? 2 : 1), 1);

                assert(depth > DEPTH_ZERO);
            }
        }

        // Collection of worse moves
        if (move != bestMove && moveCount <= 32)
            moves[capture].push_back(move);
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves,
    // it must be a mate or a stalemate.
    // If in a singular extension search then return a fail low score.
    assert(moveCount != 0 || !ss->inCheck || exclude || LegalMoveList(pos).empty());
    assert(moveCount == ss->moveCount);

    // Adjust best value for fail high cases at non-pv nodes
    if (!PVNode && bestValue > beta && !is_decisive(bestValue))
    {
        bestValue = in_range((depth * bestValue + beta) / (depth + 1));
    }

    if (moveCount == 0)
        bestValue = exclude ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha update the history of searched moves
    else if (bestMove != Move::None)
        update_all_history(pos, ss, depth, bestMove, moves);

    // If prior move is valid, that caused the fail low
    else if (is_ok(preSq))
    {
        // Bonus for prior quiet move
        if (!preCapture)
        {
            // clang-format off
            // Make sure the bonus is positive
            auto bonusScale =
                            // Increase bonus when depth is high
                            + 118 * (depth > 5) + 36 * !AllNode
                            // Increase bonus when the previous move count is high
                            + 161 * ((ss - 1)->moveCount > 8)
                            // Increase bonus when bestValue is lower than current static evaluation
                            + 133 * (!(ss    )->inCheck && bestValue <= +(ss    )->staticEval - 107)
                            // Increase bonus when bestValue is higher than previous static evaluation
                            + 120 * (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->staticEval - 84)
                            // Increase bonus when the previous move is TT move
                            +  81 * ((ss - 1)->move == (ss - 1)->ttMove)
                            // Increase bonus when the previous cutoffCount is low
                            + 100 * ((ss - 1)->cutoffCount <= 3)
                            + 100 * (red > 1)
                            // Increase bonus if the previous move has a bad history
                            + std::min(-9.2593e-3f * (ss - 1)->history, 320.0f);
            // clang-format on
            if (bonusScale < 0.0f)
                bonusScale = 0.0f;

            int bonus = bonusScale * stat_bonus(depth);

            update_quiet_history(~ac, preMove, +6.6833e-3f * bonus);
            update_continuation_history(ss - 1, pos.piece_on(preSq), preSq, +12.6953e-3f * bonus);
            if (preNonPawn)
                update_pawn_history(pos, pos.piece_on(preSq), preSq, +33.6609e-3f * bonus);
        }
        // Bonus for prior capture move
        else
        {
            auto captured = type_of(pos.captured_piece());
            assert(captured != NO_PIECE_TYPE);
            int bonus = +2.0000f * stat_bonus(depth);
            update_capture_history(pos.piece_on(preSq), preSq, captured, bonus);
        }
    }

    // Don't let bestValue inflate too high (tb)
    if constexpr (PVNode)
        if (bestValue > maxValue)
            bestValue = maxValue;

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree.
    ss->pvHit = ss->pvHit || ((ss - 1)->pvHit && bestValue <= alpha);

    // Save gathered information in transposition table
    if ((!RootNode || curIdx == 0) && !exclude)
    {
        Bound bound = bestValue >= beta                ? BOUND_LOWER
                    : PVNode && bestMove != Move::None ? BOUND_EXACT
                                                       : BOUND_UPPER;
        ttu.update(depth, ss->pvHit, bound, bestMove, bestValue, unadjustedStaticEval);
    }

    // Adjust correction history
    if (!ss->inCheck  //
        && !(bestMove != Move::None && pos.capture(bestMove))
        && (
          // negative correction & no fail high
          (bestValue < ss->staticEval && bestValue < beta)
          // positive correction & no fail low
          || (bestValue > ss->staticEval && bestMove != Move::None)))
    {
        int bonus = 0.1250f * depth * (bestValue - ss->staticEval);
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
    if (alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply))
    {
        alpha = draw_value(key, nodes);
        if (alpha >= beta)
            return alpha;
    }

    if (is_main_worker() && main_manager()->callsCount > 1)
        main_manager()->callsCount--;

    std::vector<Move> pv(MAX_PLY + 1 - ss->ply);

    if constexpr (PVNode)
    {
        ss->pv[0]    = Move::None;
        (ss + 1)->pv = pv.data();

        // Update selDepth (selDepth from 1, ply from 0)
        if (selDepth < 1 + ss->ply)
            selDepth = 1 + ss->ply;
    }

    // Step 1. Initialize node
    ss->inCheck = bool(pos.checkers());

    // Step 2. Check for an immediate draw or maximum ply reached
    if (ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
        return ss->ply >= MAX_PLY && !ss->inCheck ? evaluate(pos) : VALUE_DRAW;

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Step 3. Transposition table lookup
    Key16 key16 = compress_key(key);

    auto [ttd, tte, ttc] = tt.probe(key, key16);
    TTUpdater ttu(tte, ttc, key16, ss->ply, tt.generation());

    ttd.value = ttd.hit ? value_from_tt(ttd.value, ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttd.move  = ttd.hit ? extract_tt_move(pos, ttd.move) : Move::None;

    Move pttm  = ttd.move != Move::None   ? ttd.move
               : tte->move() != ttc->move ? extract_tt_move(pos, ttc->move, false)
                                          : Move::None;
    ss->ttMove = ttd.move;
    bool pvHit = ttd.hit && ttd.pv;

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && is_valid(ttd.value) && ttd.depth >= DEPTH_ZERO
        && (ttd.bound & bound_for_fail(ttd.value >= beta)) != 0
        // For high rule50 counts don't produce transposition table cutoffs.
        && pos.rule50_count() < (1.0f - 0.5f * pos.rule50_high()) * Position::rule50_threshold())
    {
        if (ttd.value > beta && ttd.depth > DEPTH_ZERO && !is_decisive(ttd.value))
        {
            ttd.value = in_range((ttd.depth * ttd.value + beta) / (ttd.depth + 1));
        }

        return ttd.value;
    }

    Move preMove = (ss - 1)->move;
    auto preSq   = preMove.is_ok() ? preMove.dst_sq() : SQ_NONE;

    int correctionValue = ss->inCheck ? 0 : correction_value(pos, ss);

    Value unadjustedStaticEval, bestValue;
    Value futilityBase;

    // Step 4. Static evaluation of the position
    if (ss->inCheck)
    {
        unadjustedStaticEval = VALUE_NONE;

        bestValue = futilityBase = -VALUE_INFINITE;

        goto QS_MOVES_LOOP;
    }

    if (ttd.hit)
    {
        // Never assume anything about values stored in TT
        unadjustedStaticEval = ttd.eval;
        if (!is_valid(unadjustedStaticEval))
            unadjustedStaticEval = evaluate(pos);

        bestValue = ss->staticEval = adjust_static_eval(unadjustedStaticEval, correctionValue);

        // Can ttValue be used as a better position evaluation
        if (is_valid(ttd.value) && (ttd.bound & bound_for_fail(ttd.value > bestValue)) != 0)
            bestValue = ttd.value;
    }
    else
    {
        // In case of null move search, use previous staticEval with a opposite sign
        unadjustedStaticEval = preMove != Move::Null ? evaluate(pos) : -(ss - 1)->staticEval;

        bestValue = ss->staticEval = adjust_static_eval(unadjustedStaticEval, correctionValue);
    }

    // Stand pat. Return immediately if bestValue is at least beta
    if (bestValue >= beta)
    {
        if (bestValue > beta && !is_decisive(bestValue))
        {
            bestValue = in_range((bestValue + beta) / 2);
        }

        if (!ttd.hit)
            ttu.update(DEPTH_NONE, false, BOUND_LOWER, Move::None, bestValue, unadjustedStaticEval);

        return bestValue;
    }

    if (alpha < bestValue)
        alpha = bestValue;

    futilityBase = std::min(325 + ss->staticEval, VALUE_TB_WIN_IN_MAX_PLY - 1);

QS_MOVES_LOOP:

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    Value value;
    Move  move;

    std::uint8_t moveCount  = 0;
    std::uint8_t promoCount = 0;

    Move bestMove = Move::None;

    auto pawnIndex = pawn_index(pos.pawn_key());

    const History<HPieceSq>* contHistory[2]{(ss - 1)->pieceSqHistory, (ss - 2)->pieceSqHistory};

    // Initialize a MovePicker object for the current position, prepare to search the moves.
    // Because the depth is <= DEPTH_ZERO here, only captures, promotions will be generated.
    MovePicker mp(pos, pttm, &captureHistory, &quietHistory, &pawnHistory, contHistory,
                  &lowPlyQuietHistory, ss->ply);
    mp.quietPick = ss->inCheck;
    // Step 5. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None)
    {
        assert(pos.pseudo_legal(move));
        assert(ss->inCheck || pos.capture_promo(move));

        // Check for legality
        if (!pos.legal(move))
            continue;

        ++moveCount;
        promoCount += move.type_of() == PROMOTION && move.promotion_type() != QUEEN;

        Square dst = move.dst_sq();

        Piece movedPiece = pos.moved_piece(move);

        bool check    = pos.check(move);
        bool dblCheck = check && pos.dbl_check(move);
        bool capture  = pos.capture_promo(move);
        auto captured = capture ? pos.captured(move) : NO_PIECE_TYPE;

        // Step 6. Pruning
        if (!is_loss(bestValue))
        {
            // Skip quiet moves
            mp.quietPick = mp.quietPick && moveCount < 4 + promoCount;

            // Futility pruning and moveCount pruning
            if (!check && dst != preSq && !is_loss(futilityBase)
                && (move.type_of() != PROMOTION
                    || (!ss->inCheck && move.promotion_type() != QUEEN)))
            {
                if (moveCount > 2 + promoCount)
                    continue;

                Value futilityValue =
                  std::min(futilityBase + PIECE_VALUE[captured] + promotion_value(move),
                           VALUE_TB_WIN_IN_MAX_PLY - 1);
                // If static evaluation + value of piece going to captured is much lower than alpha
                if (futilityValue <= alpha)
                {
                    if (bestValue < futilityValue)
                        bestValue = futilityValue;
                    continue;
                }

                // SEE based pruning
                if (pos.see(move) < (alpha - futilityBase))
                {
                    auto margin = std::min(alpha, futilityBase);
                    if (bestValue < margin)
                        bestValue = margin;
                    continue;
                }
            }

            if (capture)
            {
                ;
            }
            else
            {
                // Continuation history based pruning
                int contHist = (*contHistory[0])[movedPiece][dst]  //
                             + (*contHistory[1])[movedPiece][dst]  //
                             + pawnHistory[pawnIndex][movedPiece][dst];
                if (contHist <= 5389)
                    continue;
            }

            // SEE based pruning
            if (pos.see(move) < -(75 + 64 * dblCheck))
                continue;
        }

        // Step 7. Make and search the move
        pos.do_move(move, st, check);

        // Speculative prefetch as early as possible
        tt.prefetch_key(pos.key());
        nodes.fetch_add(1, std::memory_order_relaxed);
        // Update the move
        // clang-format off
        ss->move                     = move;
        ss->pieceSqHistory           = &continuationHistory[ss->inCheck][capture][movedPiece][dst];
        ss->pieceSqCorrectionHistory = &continuationCorrectionHistory[movedPiece][dst];
        // clang-format on

        value = -qsearch<PVNode>(pos, ss + 1, -beta, -alpha);

        pos.undo_move(move);

        assert(is_ok(value));

        // Step 8. Check for a new best move
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

    // Step 9. Check for checkmate
    // All legal moves have been searched.
    // A special case: if in check and no legal moves were found, it is checkmate.
    if (ss->inCheck && bestValue == -VALUE_INFINITE)
    {
        assert(moveCount == 0 && LegalMoveList(pos).empty());
        return mated_in(ss->ply);  // Plies to mate from the root
    }

    // Adjust best value for fail high cases
    if (bestValue > beta && !is_decisive(bestValue))
    {
        bestValue = in_range((3 * bestValue + beta) / 4);
    }

    // Save gathered info in transposition table
    Bound bound = bound_for_fail(bestValue >= beta);
    ttu.update(DEPTH_ZERO, pvHit, bound, bestMove, bestValue, unadjustedStaticEval);

    assert(is_ok(bestValue));

    return bestValue;
}

Value Worker::evaluate(const Position& pos) noexcept {
    using DON::evaluate;
    return evaluate(pos, networks[numaAccessToken], accCaches, optimism[pos.active_color()]);
}

Move Worker::extract_tt_move(const Position& pos, Move ttMove, bool deep) const noexcept {
    if (ttMove != Move::None && pos.pseudo_legal(ttMove))
        return ttMove;

    if (deep)
    {
        auto rule50 = pos.rule50_count();
        while (rule50 >= Position::R50Offset)
        {
            rule50 -= Position::R50Factor;
            Key key = pos.key(rule50 - pos.rule50_count());

            auto [ttd, tte, ttc] = tt.probe(key);

            ttMove = ttd.hit ? ttd.move : Move::None;

            if (ttMove != Move::None && pos.pseudo_legal(ttMove))
                return ttMove;
        }
    }

    return Move::None;
}

// Called in case have no ponder move before exiting the search,
// for instance, in case stop the search during a fail high at root.
// Try hard to have a ponder move to return to the GUI,
// otherwise in case of 'ponder on' have nothing to think about.
bool Worker::ponder_move_extracted() noexcept {
    assert(rootMoves.front().size() == 1 && rootMoves.front()[0] != Move::None);

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

        auto [ttd, tte, ttc] = tt.probe(key);

        pm = ttd.hit ? extract_tt_move(rootPos, ttd.move) : Move::None;
        if (pm == Move::None || !legalMoves.contains(pm))
        {
            pm = Move::None;
            for (auto&& th : threads)
                if (th->worker.get() != this && th->worker->rootMoves.front()[0] == bm
                    && th->worker->rootMoves.front().size() >= 2)
                {
                    pm = th->worker->rootMoves.front()[1];
                    break;
                }
            if (pm == Move::None && rootMoves.size() >= 2)
                for (auto&& th : threads)
                {
                    if (th->worker.get() == this || th->worker->completedDepth == DEPTH_ZERO)
                        continue;
                    const auto& rm = *th->worker->rootMoves.find(bm);
                    if (rm.size() >= 2)
                    {
                        pm = rm[1];
                        break;
                    }
                }
            if (pm == Move::None)
            {
                std::srand(std::time(nullptr));
                pm = *(legalMoves.begin() + (std::rand() % legalMoves.size()));
            }
        }

        rootMoves.front().push_back(pm);
    }

    rootPos.undo_move(bm);
    return rootMoves.front().size() == 2;
}

void MainSearchManager::init() noexcept {

    timeManager.init();
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
      && ((worker.limit.use_time_manager() && (ponderhitStop || elapsedTime >= timeManager.maximum()))
       || (worker.limit.moveTime != 0      &&                   elapsedTime >= worker.limit.moveTime)
       || (worker.limit.nodes != 0         &&        worker.threads.nodes() >= worker.limit.nodes)))
        worker.threads.stop = worker.threads.abort = true;
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

    auto time     = std::max(elapsed(), TimePoint(1));
    auto nodes    = worker.threads.nodes();
    auto hashFull = worker.tt.hashFull();
    auto tbHits   = worker.threads.tbHits() + worker.tbConfig.rootInTB * worker.rootMoves.size();
    bool wdlShow  = worker.options["UCI_ShowWDL"];

    for (std::size_t i = 0; i < worker.multiPV; ++i)
    {
        auto& rm = worker.rootMoves[i];

        bool updated = rm.curValue != -VALUE_INFINITE;

        if (i != 0 && depth == 1 && !updated)
            continue;

        Depth d = updated ? depth : std::max(depth - 1, 1);
        Value v = updated ? rm.uciValue : rm.preValue;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = worker.tbConfig.rootInTB && !is_mate(v);
        if (tb)
            v = rm.tbValue;

        // tablebase- and previous-scores are exact
        bool exact = i != worker.curIdx || tb || !updated;

        // Potentially correct and extend the PV, and in exceptional cases value also
        if (is_decisive(v) && !is_mate(v) && (exact || !(rm.boundLower || rm.boundUpper)))
            extend_tb_pv(worker.rootPos, rm, v, worker.limit, worker.options);

        FullInfo info(worker.rootPos, rm);
        info.depth     = d;
        info.value     = v;
        info.multiPV   = 1 + i;
        info.boundShow = !exact;
        info.wdlShow   = wdlShow;
        info.time      = time;
        info.nodes     = nodes;
        info.hashFull  = hashFull;
        info.tbHits    = tbHits;

        updateCxt.onUpdateFull(info);
    }
}

void Skill::init(const Options& options) noexcept {

    if (options["UCI_LimitStrength"])
    {
        std::uint16_t uciELO = options["UCI_ELO"];

        auto e = float(uciELO - MIN_ELO) / float(MAX_ELO - MIN_ELO);
        level  = std::clamp(-311.4380e-3f + (22.2943f + (-40.8525f + 37.2473f * e) * e) * e,  //
                            MIN_LEVEL, MAX_LEVEL - 0.01f);
    }
    else
    {
        level = options["SkillLevel"];
    }

    bestMove = Move::None;
}

// When playing with strength handicap, choose the best move among a set of RootMoves
// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move Skill::pick_best_move(const RootMoves& rootMoves,
                           std::size_t      multiPV,
                           bool             bestPick) noexcept {
    assert(1 <= multiPV && multiPV <= rootMoves.size());
    static PRNG rng(now());  // PRNG sequence should be non-deterministic

    if (bestPick || bestMove == Move::None)
    {
        // RootMoves are already sorted by value in descending order
        Value curValue = rootMoves[0].curValue;
        auto  delta    = std::min(curValue - rootMoves[multiPV - 1].curValue, +VALUE_PAWN);
        auto  weakness = 2.0f * (3.0f * MAX_LEVEL - level);

        Value maxValue = -VALUE_INFINITE;
        // Choose best move. For each move value add two terms, both dependent on weakness.
        // One is deterministic and bigger for weaker levels, and one is random.
        // Then choose the move with the resulting highest value.
        for (std::size_t i = 0; i < multiPV; ++i)
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

// clang-format off

void update_capture_history(Piece pc, Square dst, PieceType captured, int bonus) noexcept {
    captureHistory[pc][dst][captured] << bonus;
}
void update_capture_history(const Position& pos, const Move& m, int bonus) noexcept {
    assert(pos.pseudo_legal(m));
    update_capture_history(pos.moved_piece(m), m.dst_sq(), pos.captured(m), bonus);
}

void update_quiet_history(Color ac, const Move& m, int bonus) noexcept {
    assert(m.is_ok());
    quietHistory[ac][m.org_dst()] << bonus;
}
void update_pawn_history(const Position& pos, Piece pc, Square dst, int bonus) noexcept {
    pawnHistory[pawn_index(pos.pawn_key())][pc][dst] << bonus;
}
// Updates histories of the move pairs formed by
// move at ply -1, -2, -3, -4, -5 and -6 with move at ply 0.
void update_continuation_history(Stack* const ss, Piece pc, Square dst, int bonus) noexcept {
    assert(is_ok(dst));

    static constexpr std::array<std::pair<std::int16_t, float>, 8> ContHistoryWeights{
      {{1, +1.0048f}, {2, +0.6407f}, {3, +0.3184f}, {4, +0.5234f},
       {5, +0.2698f}, {6, +0.5244f}, {7, +0.2222f}, {8, +0.3251f}}};

    for (auto [i, weight] : ContHistoryWeights)
    {
        // Only first 2 continuation histories if in check
        if ((ss->inCheck && i > 2) || !(ss - i)->move.is_ok())
            break;

        (*(ss - i)->pieceSqHistory)[pc][dst] << weight * bonus;
    }
}
void update_low_ply_quiet_history(std::int16_t ssPly, const Move& m, int bonus) noexcept {
    assert(m.is_ok());
    if (ssPly < LOW_PLY_SIZE)
        lowPlyQuietHistory[ssPly][m.org_dst()] << bonus;
}
void update_all_quiet_history(const Position& pos, Stack* const ss, const Move& m, int bonus) noexcept {
    assert(m.is_ok());
    update_quiet_history(pos.active_color(), m, +1.0000f * bonus);
    update_pawn_history(pos, pos.moved_piece(m), m.dst_sq(), +0.6006f * bonus);
    update_continuation_history(ss, pos.moved_piece(m), m.dst_sq(), +0.9414f * bonus);
    update_low_ply_quiet_history(ss->ply, m, +0.8243f * bonus);
}

// Updates history at the end of search() when a bestMove is found
void update_all_history(const Position& pos, Stack* const ss, Depth depth, const Move& bm, const std::array<Moves, 2>& moves) noexcept {
    assert(pos.pseudo_legal(bm));
    assert(ss->moveCount != 0);

    int bonus = stat_bonus(depth) + 298 * (bm == ss->ttMove);
    int malus = std::max(stat_malus(depth) - 32 * (ss->moveCount - 1), 1);

    if (pos.capture_promo(bm))
    {
        update_capture_history(pos, bm, +1.2070f * bonus);
    }
    else
    {
        update_all_quiet_history(pos, ss, bm, +1.1738f * bonus);

        // Decrease history for all non-best quiet moves
        for (const Move& qm : moves[false])
            update_all_quiet_history(pos, ss, qm, -1.1250f * malus);
    }

    // Decrease history for all non-best capture moves
    for (const Move& cm : moves[true])
        update_capture_history(pos, cm, -1.1953f * malus);

    Move m = (ss - 1)->move;
    // Extra penalty for a quiet early move that was not a TT move
    // in the previous ply when it gets refuted.
    if (m.is_ok() && pos.captured_piece() == NO_PIECE && (ss - 1)->moveCount == 1 + ((ss - 1)->ttMove != Move::None))
        update_continuation_history(ss - 1, pos.piece_on(m.dst_sq()), m.dst_sq(), -0.9531f * malus);
}

void update_correction_history(const Position& pos, Stack* const ss, int bonus) noexcept {
    Color ac = pos.active_color();
    Move  m  = (ss - 1)->move;

    static constexpr int BonusLimit = CORRECTION_HISTORY_LIMIT / 4;

    bonus = std::clamp(bonus, -BonusLimit, +BonusLimit);

    for (Color c : {WHITE, BLACK})
    {
       pawnCorrectionHistory[correction_index(pos.    pawn_key(c))][ac][c] << +0.8516f * bonus;
      minorCorrectionHistory[correction_index(pos.   minor_key(c))][ac][c] << +1.1016f * bonus;
      majorCorrectionHistory[correction_index(pos.   major_key(c))][ac][c] << +0.5508f * bonus;
    nonPawnCorrectionHistory[correction_index(pos.non_pawn_key(c))][ac][c] << +1.2891f * bonus;
    }
    if (m.is_ok())
      (*(ss - 2)->pieceSqCorrectionHistory)[pos.piece_on(m.dst_sq())][m.dst_sq()] << +1.0781f * bonus;
}

int correction_value(const Position& pos, const Stack* const ss) noexcept {
    Color ac = pos.active_color();
    Move  m  = (ss - 1)->move;

    int pcv  = 0;
    int micv = 0;
    int mjcv = 0;
    int npcv = 0;
    for (Color c : {WHITE, BLACK})
    {
        pcv  +=    pawnCorrectionHistory[correction_index(pos.    pawn_key(c))][ac][c];
        micv +=   minorCorrectionHistory[correction_index(pos.   minor_key(c))][ac][c];
        mjcv +=   majorCorrectionHistory[correction_index(pos.   major_key(c))][ac][c];
        npcv += nonPawnCorrectionHistory[correction_index(pos.non_pawn_key(c))][ac][c];
    }
    int cntcv = m.is_ok()
              ? (*(ss - 2)->pieceSqCorrectionHistory)[pos.piece_on(m.dst_sq())][m.dst_sq()]
              : 0;

    return (+6995 * pcv + 6593 * micv + 3296 * mjcv + 7753 * npcv + 6049 * cntcv);
}

// Update raw staticEval according to various CorrectionHistory value
// and guarantee evaluation does not hit the tablebase range.
Value adjust_static_eval(Value ev, int cv) noexcept {    
    return in_range(ev + 7.6294e-6f * cv);
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

    // Do not use more than (0.5 * moveOverhead) time, if time manager is active.
    const auto time_to_abort = [&]() {
        auto endTime = SteadyClock::now();
        return limit.use_time_manager()
            && std::chrono::duration<float, std::milli>(endTime - startTime).count()
                 > 0.5f * options["MoveOverhead"];
    };

    bool rule50Use = options["Syzygy50MoveRule"];

    std::list<State> states;

    // Step 0. Do the rootMove, no correction allowed, as needed for MultiPV in TB.
    auto& rootSt = states.emplace_back();
    rootPos.do_move(rootMove[0], rootSt);

    // Step 1. Walk the PV to the last position in TB with correct decisive score
    std::int16_t ply = 1;
    while (ply < std::int16_t(rootMove.size()))
    {
        const Move& pvMove = rootMove[ply];

        RootMoves legalRootMoves;
        for (const Move& m : LegalMoveList(rootPos))
            legalRootMoves.emplace_back(m);

        auto tbConfig = Tablebases::rank_root_moves(rootPos, legalRootMoves, options);

        auto& rm = *legalRootMoves.find(pvMove);

        if (rm.tbRank != legalRootMoves.front().tbRank)
            break;

        auto& st = states.emplace_back();
        rootPos.do_move(pvMove, st);

        ++ply;

        // Don't allow for repetitions or drawing moves along the PV in TB regime.
        if (tbConfig.rootInTB && rootPos.is_draw(ply, rule50Use))
        {
            rootPos.undo_move(pvMove);

            --ply;

            break;
        }

        // Full PV shown will thus be validated and end TB.
        // If we can't validate the full PV in time, we don't show it.
        if (tbConfig.rootInTB && time_to_abort())
            break;
    }

    // Resize the PV to the correct part
    rootMove.resize(ply);

    // Step 2. Now extend the PV to mate, as if the user explores syzygy-tables.info using
    // top ranked moves (minimal DTZ), which gives optimal mates only for simple endgames e.g. KRvK
    while (!(rule50Use && rootPos.is_draw(0, rule50Use)))
    {
        RootMoves legalRootMoves;
        for (const Move& m : LegalMoveList(rootPos))
        {
            auto& rm = legalRootMoves.emplace_back(m);

            State st;
            rootPos.do_move(m, st);
            // Give a score of each move to break DTZ ties
            // restricting opponent mobility, but not giving the opponent a capture.
            for (const Move& om : LegalMoveList(rootPos))
                rm.tbRank -= 1 + 99 * rootPos.capture(om);
            rootPos.undo_move(m);
        }

        // Mate found
        if (legalRootMoves.empty())
            break;

        // Sort moves according to their above assigned TB rank.
        // This will break ties for moves with equal DTZ in rank_root_moves.
        legalRootMoves.sort([](const RootMove& rm1, const RootMove& rm2) noexcept {
            return rm1.tbRank > rm2.tbRank;
        });

        // The winning side tries to minimize DTZ, the losing side maximizes it.
        auto tbConfig = Tablebases::rank_root_moves(rootPos, legalRootMoves, options, true);

        // If DTZ is not available might not find a mate, so bail out.
        if (!tbConfig.rootInTB || tbConfig.cardinality != 0)
            break;

        const Move& pvMove = legalRootMoves.front()[0];
        rootMove.push_back(pvMove);
        auto& st = states.emplace_back();
        rootPos.do_move(pvMove, st);

        //++ply;

        if (time_to_abort())
            break;
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
    if (rootPos.is_draw(0, rule50Use))
        value = VALUE_DRAW;

    // Undo the PV moves.
    for (auto itr = rootMove.rbegin(); itr != rootMove.rend(); ++itr)
        rootPos.undo_move(*itr);

    // Inform if couldn't get a full extension in time.
    if (time_to_abort())
        UCI::print_info_string("PV extension requires more time, increase MoveOverhead as needed.");
}

}  // namespace
}  // namespace DON
