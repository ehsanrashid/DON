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
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <list>
#include <ratio>

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
        return v;
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

constexpr Bound bound_for_tt(bool failsHigh) noexcept {
    return failsHigh ? BOUND_LOWER : BOUND_UPPER;
}
constexpr Bound bound_for_tt(bool failsHigh, bool failsLow) noexcept {
    return failsHigh ? BOUND_LOWER : failsLow ? BOUND_UPPER : BOUND_NONE;
}

}  // namespace

void TTUpdater::update(
  Depth depth, bool isPv, Bound bound, Move move, Value value, Value eval) noexcept {

    if (key16 != tte->key16)
    {
        tte = fte;
        for (std::uint8_t i = 0; i < TT_CLUSTER_ENTRY_COUNT; ++i)
        {
            TTEntry* cte = fte + i;
            if (key16 == cte->key16)
            {
                tte = cte;
                break;
            }
            // Find an entry to be replaced according to the replacement strategy
            if (i != 0 && tte->worth(generation) > cte->worth(generation))
                tte = cte;
        }
    }

    tte->save(key16, depth, isPv, bound, move, value_to_tt(value, ssPly), eval, generation);
}

using Eval::evaluate;

namespace Search {

namespace {

PolyBook Polybook;

// Reductions lookup table initialized at startup
std::array<std::int16_t, MAX_MOVES> reductions;  // [depth or moveCount]

constexpr std::int16_t reduction(
  Depth depth, std::uint8_t moveCount, int deltaRatio, bool improving, bool recovering) noexcept {
    int reductionScale = int(reductions[depth]) * int(reductions[moveCount]);
    return std::max((1255 + reductionScale - deltaRatio) / 1024, 0)
         + (!improving && reductionScale > 1293)
         + (!improving && !recovering && reductionScale > 2600);
}

// Futility margin
constexpr Value
futility_margin(Depth depth, bool cutNodeNoTtM, bool improving, bool recovering) noexcept {
    Value futilityMul = 125 - 39 * cutNodeNoTtM;
    return std::max(depth * futilityMul                        //
                      - int(improving * 2.0938 * futilityMul)  //
                      - int(recovering * 0.3334 * futilityMul),
                    0);
}

// History and stats update bonus, based on depth
constexpr int stat_bonus(Depth depth) noexcept { return std::min(-109 + 188 * depth, 1687); }

// History and stats update malus, based on depth
constexpr int stat_malus(Depth depth) noexcept { return std::min(-268 + 787 * depth, 2096); }

// Add a small random value to draw evaluation to avoid 3-fold blindness
constexpr Value draw_value(Key key, std::uint64_t nodes) noexcept {
    return VALUE_DRAW + (key & 1) - (nodes & 1);
}

// Add correctionHistory value to raw staticEval and guarantee evaluation does not hit the tablebase range
template<bool Do = true>
Value adjust_static_eval(Value ev, const Worker& worker, const Position& pos) noexcept {
    Color stm = Do ? pos.side_to_move() : ~pos.side_to_move();
    int   cv  = worker.correctionHistory[stm][correction_index(pos.pawn_key())];
    int   aev = ev + (Do ? +1 : -1) * 66 * cv / 512;
    return std::clamp(aev, VALUE_TB_LOSS_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
}

// Adds current move and appends subPv[]
void update_pv(Moves& mainPv, Move move, const Moves& subPv) noexcept {
    mainPv = subPv;
    mainPv.push_front(move);
}

// Updates histories of the move pairs formed by moves
// at ply -1, -2, -3, -4, and -6 with current move.
void update_continuation_histories(Stack* ss, Piece pc, Square dst, int bonus) noexcept {
    assert(/*is_ok(pc) &&*/ is_ok(dst));

    bonus = (57 * bonus) / 64;

    for (std::uint8_t i : {1, 2, 3, 4, 6})
    {
        // Only update the first 2 continuation histories if in check
        if (i > 2 && ss->inCheck)
            break;
        if (((ss - i)->currentMove).is_ok())
            (*(ss - i)->continuationHistory)[pc][dst] << bonus / (1 + (i == 3));
    }
}

// clang-format off

void update_quiet_histories(
  Worker& worker, const Position& pos, Stack* ss, Move move, int bonus) noexcept {

    Color         stm          = pos.side_to_move();
    std::uint16_t orgDst       = move.org_dst();
    Square        dst          = move.dst_sq();
    Key16         pawnIndex    = pawn_index(pos.pawn_key());
    Piece         exMovedPiece = pos.ex_moved_piece(move);

    worker.mainHistory[stm][orgDst]                  << bonus;
    worker.pawnHistory[pawnIndex][exMovedPiece][dst] << bonus / 2;
    update_continuation_histories(ss, exMovedPiece, dst, bonus);
}

void update_prev_quiet_histories(
  Worker& worker, const Position& pos, Stack* ss, int bonus, const std::array<int, 3>& arrBonus = {}) noexcept {

    Color         stm          = ~pos.side_to_move();
    Move          move         = (ss - 1)->currentMove;
    std::uint16_t orgDst       = move.org_dst();
    Square        dst          = move.dst_sq();
    Key16         pawnIndex    = pawn_index(pos.prev_state()->pawnKey);
    Piece         exMovedPiece = pos.prev_ex_moved_piece(move);

    worker.mainHistory[stm][orgDst]                  << (bonus ? bonus     : arrBonus[0]);
    worker.pawnHistory[pawnIndex][exMovedPiece][dst] << (bonus ? bonus / 2 : arrBonus[1]);
    update_continuation_histories(ss - 1, exMovedPiece, dst, (bonus ? bonus : arrBonus[2]));
}

void update_capture_histories(
  Worker& worker, const Position& pos, Move move, int bonus) noexcept {

    Piece     movedPiece = pos.moved_piece(move);
    PieceType captured   = type_of(pos.captured_piece(move));

    worker.captureHistory[movedPiece][move.dst_sq()][captured] << bonus;
}

// Updates history at the end of search() when a bestMove is found
void update_histories(
  Worker& worker, const Position& pos, Stack* ss, Depth depth, Move move, const std::array<Moves, 2>& mainMoves, bool isExcluded) noexcept {

    int bonus = +stat_bonus(depth) / (1 + isExcluded);
    int malus = -stat_malus(depth);

    if (pos.capture_stage(move))
        update_capture_histories(worker, pos, move, bonus);
    else
        update_quiet_histories(worker, pos, ss, move, bonus);

    // Decrease history for all non-best quiet moves
    for (Move qm : mainMoves[0])
        if (qm != move)
            update_quiet_histories(worker, pos, ss, qm, malus);
    // Decrease history for all non-best capture moves
    for (Move cm : mainMoves[1])
        if (cm != move)
            update_capture_histories(worker, pos, cm, malus);

    // Extra penalty for a quiet early move that was not a TT move in the previous ply when it gets refuted.
    const Square prevDst = (ss - 1)->currentMove.is_ok() ? (ss - 1)->currentMove.dst_sq() : SQ_NONE;

    if (is_ok(prevDst) && !is_ok(type_of(pos.captured_piece())) && (ss - 1)->currentMove.type_of() != PROMOTION
        && (ss - 1)->moveCount == 1 + (ss - 1)->ttM)
        update_continuation_histories(ss - 1, pos.prev_ex_moved_piece((ss - 1)->currentMove), prevDst, malus);
}
// clang-format on

// Used to correct and extend PVs for moves that have a TB (but not a mate) score.
// Keeps the search based PV for as long as it is verified to maintain the game outcome, truncates afterwards.
// Finally, extends to mate the PV, providing a possible continuation (but not a proven mating line).
void extend_tb_pv(Position&      rootPos,
                  RootMove&      rootMove,
                  Value&         v,
                  const Limits&  limits,
                  const Options& options) noexcept {

    auto startTime = SteadyClock::now();

    TimePoint moveOverhead = options["Move Overhead"];

    // Do not use more than moveOverhead / 2 time, if time manager is active.
    auto time_to_abort = [&]() -> bool {
        auto endTime = SteadyClock::now();
        return limits.use_time_manager()
            && 2 * std::chrono::duration<double, std::milli>(endTime - startTime).count()
                 > moveOverhead;
    };

    std::list<StateInfo> listStates;

    // Step 0. Do the rootMove, no correction allowed, as needed for MultiPV in TB.
    StateInfo& rootSt = listStates.emplace_back();
    rootPos.do_move(rootMove[0], rootSt);

    // Step 1. Walk the PV to the last position in TB with correct decisive score
    std::int16_t ply = 1;
    while (ply < int(rootMove.size()))
    {
        Move pvMove = rootMove[ply];

        RootMoves legalRootMoves;
        for (const auto& m : MoveList<LEGAL>(rootPos))
            legalRootMoves.emplace(m);

        auto tbConfig = Tablebases::rank_root_moves(rootPos, legalRootMoves, options);

        RootMove& rm = *legalRootMoves.find(pvMove);

        if (legalRootMoves.front().tbRank != rm.tbRank)
            break;

        StateInfo& st = listStates.emplace_back();
        rootPos.do_move(pvMove, st);

        // Don't allow for repetitions or drawing moves along the PV in TB regime.
        if (tbConfig.rootInTB && rootPos.is_draw(1 + ply, true))
        {
            rootPos.undo_move(pvMove);
            listStates.pop_back();
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
        for (const auto& m : MoveList<LEGAL>(rootPos))
        {
            RootMove& rm = legalRootMoves.emplace(m);
            StateInfo st;
            rootPos.do_move(m, st);
            // Give a score of each move to break DTZ ties
            // restricting opponent mobility, but not giving the opponent a capture.
            for (const auto& om : MoveList<LEGAL>(rootPos))
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
        StateInfo& st = listStates.emplace_back();
        rootPos.do_move(pvMove, st);

        if (time_to_abort())
            break;
    }

    // Finding a draw in this function is an exceptional case, that cannot happen
    // during engine game play, since we have a winning score, and play correctly
    // with TB support. However, it can be that a position is draw due to the 50 move
    // rule if it has been been reached on the board with a non-optimal 50 move counter
    // (e.g. 8/8/6k1/3B4/3K4/4N3/8/8 w - - 54 106 ) which TB with dtz counter rounding
    // cannot always correctly rank. See also
    // https://github.com/official-stockfish/Stockfish/issues/5175#issuecomment-2058893495
    // We adjust the score to match the found PV. Note that a TB loss score can be displayed
    // if the engine did not find a drawing move yet, but eventually search will figure it out.
    // E.g. 1kq5/q2r4/5K2/8/8/8/8/7Q w - - 96 1
    if (rootPos.is_draw(0, true))
        v = VALUE_DRAW;

    // Undo the PV moves.
    for (auto itr = rootMove.rbegin(); itr != rootMove.rend(); ++itr)
        rootPos.undo_move(*itr);

    // Inform if couldn't get a full extension in time.
    if (time_to_abort())
        sync_cout << "info string "
                  << "PV extension requires more time, increase Move Overhead as needed."
                  << sync_endl;
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
    mainHistory.fill(0);
    captureHistory.fill(-743);
    pawnHistory.fill(-1097);
    correctionHistory.fill(0);

    for (bool inCheck : {false, true})
        for (bool capture : {false, true})
            for (auto& pieceDst : continuationHistory[inCheck][capture])
                for (auto& h : pieceDst)
                    h->fill(-658);

    accCaches.init(networks[numaAccessToken]);
}

void Worker::ensure_network_replicated() noexcept {
    // Access once to force lazy initialization.
    // We do this because we want to avoid initialization during search.
    (void) (networks[numaAccessToken]);
}

void Worker::start_search() noexcept {
    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    rootDepth      = DEPTH_ZERO;
    completedDepth = DEPTH_ZERO;
    minNmpPly      = 0;
    nodes          = 0;
    tbHits         = 0;
    bestMoveChange = 0;

    multiPV = DEFAULT_MULTI_PV;
    if (mainManager)
    {
        multiPV = options["MultiPV"];
        // When playing with strength handicap enable MultiPV search that will
        // use behind-the-scenes to retrieve a set of possible moves.
        if (mainManager->skill.enabled())
            multiPV = std::max<std::uint8_t>(multiPV, 4);
    }
    multiPV = std::min(std::size_t(multiPV), rootMoves.size());

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
    mainManager->iterBestValue.fill(mainManager->prevBestCurValue);

    tt.update_generation(!limits.infinite);

    bool bookMovePlayed = false;

    if (rootMoves.empty())
    {
        rootMoves.emplace(Move::None());
        mainManager->updateContext.onUpdateEnd({bool(rootPos.checkers())});
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
            bookMovePlayed = true;

            StateInfo st;
            ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

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
    {}  // Busy wait for a stop or a mainManager->ponder reset

    // Stop the threads if not already stopped
    // (also raise the stop if "ponderhit" just reset mainManager->ponder).
    threads.stop = true;

    // Wait until all threads have finished
    threads.wait_finish();

    // When playing in 'Nodes as Time' mode,
    // subtract the searched nodes from the start nodes before exiting.
    if (mainManager->timeManager.use_nodes_time())
        mainManager->timeManager.advance_nodes(threads.nodes()
                                               - limits.clock[rootPos.side_to_move()].inc);

    Worker* bestWorker = this;
    if (threads.size() > 1 && multiPV == 1 && limits.mate == 0 && !bookMovePlayed
        && rootMoves.front()[0] != Move::None() && !mainManager->skill.enabled())
        bestWorker = threads.best_thread()->worker.get();

    mainManager->isInit           = false;
    mainManager->prevBestCurValue = bestWorker->rootMoves.front().curValue;
    mainManager->prevBestAvgValue = bestWorker->rootMoves.front().avgValue;

    // Send again PV info if have a new best worker
    if (bestWorker != this)
        mainManager->info_pv(*bestWorker, bestWorker->completedDepth);

    assert(!bestWorker->rootMoves.empty() && !bestWorker->rootMoves.front().empty());
    // clang-format off
    Move bestMove   = bestWorker->rootMoves.front()[0];
    Move ponderMove = bestMove != Move::None()
                  && (bestWorker->rootMoves.front().size() >= 2 || bestWorker->ponder_move_extracted())
                    ? bestWorker->rootMoves.front()[1] : Move::None();
    // clang-format on
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
    // (ss + 1) is needed for initialization of cutoffCount.
    constexpr std::uint16_t STACK_OFFSET = 7;
    constexpr std::uint16_t STACK_SIZE   = MAX_PLY + STACK_OFFSET + 1;

    Stack  stack[STACK_SIZE]{};
    Stack* ss = stack + STACK_OFFSET;
    for (std::int16_t i = 0 - STACK_OFFSET; i < STACK_SIZE - STACK_OFFSET; ++i)
    {
        (ss + i)->ply = i;
        if (i < 0)
        {
            // Use as a sentinel
            (ss + i)->continuationHistory = &continuationHistory[0][0][NO_PIECE][SQ_ZERO];
            (ss + i)->staticEval          = VALUE_NONE;
        }
    }
    assert(stack[0].ply == -STACK_OFFSET && stack[STACK_SIZE - 1].ply == MAX_PLY);
    assert(ss->ply == 0);

    Color stm = rootPos.side_to_move();

    auto* mainManager = is_main_worker() ? main_manager() : nullptr;

    std::uint16_t researchCounter = 0, iterIdx = 0;

    double timeReduction = 1.0, sumBestMoveChange = 0.0;

    Value bestValue = -VALUE_INFINITE;

    Moves lastBestPV{Move::None()};
    Value lastBestCurValue = -VALUE_INFINITE;
    Value lastBestPreValue = -VALUE_INFINITE;
    Value lastBestUciValue = -VALUE_INFINITE;
    Depth lastBestDepth    = DEPTH_ZERO;

    // Iterative deepening loop until requested to stop or the target depth is reached
    while (!threads.stop && ++rootDepth < MAX_PLY
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
            int avgValue = rootMoves[curIdx].avgValue;
            if (avgValue == -VALUE_INFINITE)
                avgValue = VALUE_ZERO;
            assert(-VALUE_INFINITE < avgValue && avgValue < +VALUE_INFINITE);

            int absAvgValue = std::abs(avgValue);

            int   delta = 5 + 7.4493e-5 * sqr(absAvgValue);
            Value alpha = std::max(avgValue - delta, -VALUE_INFINITE);
            Value beta  = std::min(avgValue + delta, +VALUE_INFINITE);

            // Adjust optimism based on root move's avgValue (~4 Elo)
            optimism[stm]  = (133 * avgValue) / (86 + absAvgValue);
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
                  std::max(rootDepth - failedHighCounter - 3 * (1 + researchCounter) / 4, 1);

                bestValue = search<Root>(rootPos, ss, alpha, beta, adjustedDepth, false);

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
                    mainManager->info_pv(*this, rootDepth);

                // In case of failing low/high increase aspiration window and research,
                // otherwise exit the loop.
                if (bestValue <= alpha)
                {
                    beta  = (alpha + beta) / 2;
                    alpha = std::max(bestValue - delta, -VALUE_INFINITE);

                    failedHighCounter = 0;
                    if (mainManager)
                        mainManager->stopPonderhit = false;
                }
                else if (bestValue >= beta)
                {
                    beta = std::min(bestValue + delta, +VALUE_INFINITE);

                    ++failedHighCounter;
                }
                else
                    break;

                delta = std::min(int(1.3334 * delta), +VALUE_TB_WIN_IN_MAX_PLY);

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

            TimePoint optimumTime = mainManager->timeManager.optimum();
            TimePoint elapsedTime = mainManager->elapsed(*this);

            bool moveSingle = rootMoves.size() == 1;
            // clang-format off
            if (elapsedTime > (0.365 - 0.161 * moveSingle) * optimumTime)
            {
            double evalFalling =
              std::clamp(106.7e-3
                        + 22.3e-3 * (mainManager->prevBestAvgValue - bestValue)
                        +  9.7e-3 * (mainManager->iterBestValue[iterIdx] - bestValue),
                         1.0 - 0.420 * !mainManager->isInit, 1.0 + 0.667 * !mainManager->isInit);
            timeReduction        = 0.687 + 0.808 * (completedDepth - lastBestDepth > 8);
            double reduction     = 0.4608295 * (1.48 + mainManager->prevTimeReduction) / timeReduction;
            double instability   = 1.0 + (1.88 * sumBestMoveChange) / threads.size();
            double nodeReduction = 1.0 - 3.262e-3 * (completedDepth > 16)
                                 * std::max<int>(-920 + 1000 * rootMoves.front().nodes / std::uint64_t(nodes), 0);
            double reCapture     = 1.01 - 95e-3 * (rootPos.cap_square() == rootMoves.front()[0].dst_sq()
                                                && rootPos.pieces(~stm) & rootPos.cap_square());

            double totalTime = optimumTime * evalFalling * reduction * instability * nodeReduction * reCapture;

            // Cap totalTime in case of a single legal move for a better viewer experience
            if (moveSingle)
                totalTime = std::min(0.5 * totalTime, 500.0);

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
            // clang-format on
        }

        mainManager->iterBestValue[iterIdx] = bestValue;

        iterIdx = (1 + iterIdx) % mainManager->iterBestValue.size();
        assert(0 <= iterIdx && iterIdx < mainManager->iterBestValue.size());
    }

    if (!mainManager)
        return;

    mainManager->prevTimeReduction = timeReduction;

    // If the skill level is enabled, swap the best PV line with the sub-optimal one
    if (mainManager->skill.enabled())
        rootMoves.swap_to_front(mainManager->skill.pick_best_move(rootMoves, multiPV, false));
}

// Main search function for both PV and non-PV nodes.
template<NodeType NT>
// clang-format off
Value Worker::search(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode, Move excludedMove) noexcept {
    // clang-format on
    constexpr bool RootNode = NT == Root;
    constexpr bool PVNode   = NT == PV || RootNode;

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (alpha == beta - 1));
    assert(!(PVNode && cutNode));

    // Dive into quiescence search when depth <= DEPTH_ZERO
    if (depth <= DEPTH_ZERO)
        return qsearch<PVNode>(pos, ss, alpha, beta);

    const Key key = pos.key();

    if constexpr (!RootNode)
    {
        // Check if have an upcoming move that draws by repetition.
        if (Move ckMove; alpha < VALUE_DRAW
                         && (ckMove = pos.upcoming_repetition(ss->ply)) != Move::None()
                         && ckMove != excludedMove)
        {
            alpha = draw_value(key, nodes);
            if (alpha >= beta)
                return alpha;
        }
    }

    // Limit the depth if extensions made it too large
    depth = std::min(+depth, MAX_PLY - 1);
    assert(DEPTH_ZERO < depth && depth < MAX_PLY);

    StateInfo st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    // Step 1. Initialize node
    ss->inCheck   = pos.checkers();
    ss->moveCount = 0;
    ss->history   = 0;

    const Color stm = pos.side_to_move();

    // Check for the available remaining time
    if (is_main_worker())
        main_manager()->should_abort(*this);

    if constexpr (PVNode)
        // Update selDepth (selDepth from 1, ply from 0)
        selDepth = std::max<std::uint16_t>(selDepth, 1 + ss->ply);

    if constexpr (!RootNode)
    {
        // Step 2. Check for stopped search and immediate draw
        if (threads.stop.load(std::memory_order_relaxed)  //
            || ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
            return ss->ply >= MAX_PLY && !ss->inCheck
                   ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm])
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

    const Square prevDst = (ss - 1)->currentMove.is_ok() ? (ss - 1)->currentMove.dst_sq() : SQ_NONE;

    const bool prevIsQuiet = is_ok(prevDst) && !is_ok(type_of(pos.captured_piece()))
                          && (ss - 1)->currentMove.type_of() != PROMOTION;

    const bool PVCutNode = PVNode || cutNode;

    const bool isExcluded = excludedMove != Move::None();

    // Step 4. Transposition table lookup
    const Key16 key16 = compress_key(key);

    auto [ttHit, tte, fte] = tt.probe(key, key16);
    TTEntry const ttd(*tte);
    TTUpdater     ttu(tte, fte, key16, ss->ply, tt.generation());

    Value ttValue  = ttHit ? value_from_tt(ttd.value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move  ttMove   = RootNode ? rootMoves[curIdx][0] : extract_tt_move(pos, ttd.move());
    ss->ttM        = ttMove != Move::None();
    bool ttCapture = ss->ttM && pos.capture_stage(ttMove);

    // At this point, if excludedMove, skip straight to step 6, static evaluation.
    // However, to save indentation, list the condition in all code between here and there.
    if (!isExcluded)
        ss->ttPv = PVNode || (ttHit && ttd.is_pv());

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && !isExcluded && ttValue != VALUE_NONE && ttd.depth() > depth - (ttValue <= beta)
        && (ttd.bound() & bound_for_tt(ttValue >= beta)))
    {
        // If ttMove fails high, update move sorting heuristics on TT hit (~2 Elo)
        if (ss->ttM && ttValue >= beta)
        {
            // Bonus for a quiet ttMove (~2 Elo)
            if (!ttCapture)
                update_quiet_histories(*this, pos, ss, ttMove, +stat_bonus(depth));

            // Extra penalty for early quiet moves of the previous ply (~1 Elo on STC, ~2 Elo on LTC)
            if (prevIsQuiet && (ss - 1)->moveCount <= 2)
                update_continuation_histories(ss - 1,
                                              pos.prev_ex_moved_piece((ss - 1)->currentMove),
                                              prevDst, -stat_malus(depth));
        }

        // Partial workaround for the graph history interaction problem
        // For high rule50 counts don't produce transposition table cutoffs.
        if (pos.rule50_count() < -10 + 2 * Position::DrawMoveCount)
        {
            if (ttValue > beta && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY)
                ttValue = (ttd.depth() * ttValue + beta) / (ttd.depth() + 1);

            return ttValue;
        }
    }

    Value value, bestValue = -VALUE_INFINITE, maxValue = +VALUE_INFINITE;

    // Step 5. Tablebases probe
    if (!RootNode && !isExcluded && tbConfig.cardinality != 0)
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
                main_manager()->callsCount = 1;

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

    Move  move;
    Value unadjustedStaticEval, eval, probCutBeta;
    bool  improving, recovering;

    // Step 6. Static evaluation of the position
    if (ss->inCheck)
    {
        unadjustedStaticEval = VALUE_NONE;
        eval = ss->staticEval = (ss - 2)->staticEval;

        improving = recovering = false;
        // Skip early pruning when in check
        goto MAIN_MOVES_LOOP;
    }

    if (isExcluded)
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
        if (ttValue != VALUE_NONE && (ttd.bound() & bound_for_tt(ttValue > eval)))
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
    if (prevIsQuiet && !(ss - 1)->inCheck && !isExcluded)
    {
        int bonus = 699 + std::clamp(-11 * (ss->staticEval + (ss - 1)->staticEval), -1642, +1468);

        update_prev_quiet_histories(*this, pos, ss, bonus);
    }

    // Set up the improving flag, which is true if the current static evaluation
    // is bigger than the previous static evaluation at our turn
    // (if in check at previous move go back until not in check).
    // The improving flag is used in various pruning heuristics.
    improving  = (ss - 2)->staticEval != VALUE_NONE && ss->staticEval > 0 + (ss - 2)->staticEval;
    recovering = (ss - 1)->staticEval != VALUE_NONE && ss->staticEval > 2 - (ss - 1)->staticEval;

    // Step 7. Razoring (~4 Elo)
    // If eval is really low check with qsearch if it can exceed alpha,
    // if it can't, return a fail low.
    if (eval < -488 + alpha - 306 * sqr(depth))
    {
        // Razoring low window search
        Value razoringAlpha =
          std::max(alpha + std::min(-61 + 4 * int(sqr(depth)), 0), -VALUE_INFINITE + 1);

        value = qsearch<false>(pos, ss, razoringAlpha - 1, razoringAlpha);

        if (value < razoringAlpha && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
            return value;
    }

    // Step 8. Futility pruning: child node (~40 Elo)
    // The depth condition is important for mate finding.
    if (!ss->ttPv && depth < 14 && eval >= beta && (!ss->ttM || ttCapture)  //
        && eval < VALUE_TB_WIN_IN_MAX_PLY && beta > VALUE_TB_LOSS_IN_MAX_PLY
        && eval - futility_margin(depth, cutNode && !ss->ttM, improving, recovering)
               - (ss - 1)->history / 243
             >= beta)
        return (3 * beta + eval) / 4;

    // Step 9. Null move search with verification search (~35 Elo)
    if (cutNode && !isExcluded && (ss - 1)->currentMove != Move::Null() && (ss - 1)->history < 14389
        && eval >= beta && ss->ply >= minNmpPly && beta > VALUE_TB_LOSS_IN_MAX_PLY
        && ss->staticEval >= 423 + beta - 21 * depth && pos.non_pawn_material(stm))
    {
        int diff = eval - beta;
        assert(diff >= 0);

        // Null move dynamic reduction based on depth and eval difference
        Depth R = 5 + depth / 3 + std::min(diff / 212, 6);

        tt.prefetch_entry(pos.null_move_key());

        ss->currentMove         = Move::Null();
        ss->continuationHistory = &continuationHistory[0][0][NO_PIECE][SQ_ZERO];

        pos.do_null_move(st);

        Value nullValue = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, false);

        pos.undo_null_move();

        // Do not return unproven mate or TB scores
        nullValue = std::min(+nullValue, std::max(+beta, VALUE_TB_WIN_IN_MAX_PLY - 1));

        if (nullValue >= beta)
        {
            if (minNmpPly != 0 || depth < 16)
                return nullValue;

            assert(minNmpPly == 0);  // Recursive verification is not allowed

            // Do verification search at high depths, with null move pruning
            // disabled until ply exceeds minNmpPly.
            minNmpPly = ss->ply + 3 * (depth - R) / 4;

            value = search<NonPV>(pos, ss, beta - 1, beta, depth - R, false);

            minNmpPly = 0;

            if (value >= beta)
                return nullValue;
        }
    }

    // Step 10. Internal iterative reductions (~9 Elo)
    // For PV nodes without a ttMove, decrease depth.
    if (PVNode && !ss->ttM)
    {
        depth -= 3;
        // Use qsearch if depth <= DEPTH_ZERO
        if (depth <= DEPTH_ZERO)
            return qsearch<true>(pos, ss, alpha, beta);
    }

    // Decrease depth for cutNodes,
    // if the depth is high enough and there is no ttMove or an upper bound.
    if (cutNode && depth > 6 && (!ss->ttM || (ttHit && ttd.bound() == BOUND_UPPER)))
        depth -= 1 + !ss->ttM;

    // Step 11. ProbCut (~10 Elo)
    // If have a good enough capture or queen promotion and a reduced search
    // returns a value much above beta, can (almost) safely prune the previous move.
    probCutBeta = std::min(184 + beta - 53 * improving, +VALUE_INFINITE - 1);
    if (
      !PVNode && depth > 3
      && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY
      // If the value from the transposition table is lower than probCutBeta, don't attempt probCut
      // there and in further interactions with the transposition table cutoff depth is set to
      // depth - 3 because probCut search has depth set to depth - 4 but also did a move before it
      // So effective depth is equal to depth - 3
      && !(ttValue != VALUE_NONE && ttValue < probCutBeta && ttd.depth() >= depth - 3))
    {
        assert(beta < probCutBeta && probCutBeta < +VALUE_INFINITE);

        Depth probCutDepth = depth - 3;
        Moves probCutMoves;

        int probCutThreshold = probCutBeta - ss->staticEval;

        MovePicker mp(pos, ttMove, nullptr, &captureHistory, nullptr, nullptr, probCutThreshold,
                      false);
        // Loop through all pseudo-legal moves
        while ((move = mp.next_move()) != Move::None())
        {
            assert(move.is_ok() && pos.pseudo_legal(move));
            assert(pos.capture_stage(move));
            assert(!ss->inCheck);

            // Check for legality
            if ((isExcluded && move == excludedMove) || (move != ttMove && !pos.legal(move)))
                continue;

            // Speculative prefetch as early as possible
            tt.prefetch_entry(pos.move_key(move));
            // clang-format off
            ss->currentMove         = move;
            ss->continuationHistory = &continuationHistory[0][1][pos.moved_piece(move)][move.dst_sq()];
            // clang-format on
            nodes.fetch_add(1, std::memory_order_relaxed);
            pos.do_move(move, st);

            // Perform a preliminary qsearch to verify that the move holds
            value = -qsearch<false>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);

            // If the qsearch held, perform the regular search
            if (value >= probCutBeta && probCutDepth > 1)
                value = -search<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1,
                                       probCutDepth - 1, !cutNode);

            pos.undo_move(move);

            if (value >= probCutBeta)
            {
                update_capture_histories(*this, pos, move, +stat_bonus(probCutDepth));
                for (Move pcm : probCutMoves)
                    update_capture_histories(*this, pos, pcm, -stat_malus(probCutDepth));

                // Save ProbCut data into transposition table
                ttu.update(probCutDepth, ss->ttPv, BOUND_LOWER, move, value, unadjustedStaticEval);

                return std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY ? value - (probCutBeta - beta)
                                                                 : value;
            }

            probCutMoves += move;
        }

        Eval::NNUE::hint_common_parent_position(pos, networks[numaAccessToken], accCaches);
    }

MAIN_MOVES_LOOP:  // When in check, search starts here

    // Step 12. A small ProbCut idea (~4 Elo)
    probCutBeta = std::min(390 + beta, +VALUE_INFINITE - 1);
    if (!isExcluded && ttValue >= probCutBeta           //
        && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY  //
        && std::abs(beta) < VALUE_TB_WIN_IN_MAX_PLY     //
        && ttd.depth() >= depth - 4 && (ttd.bound() & BOUND_LOWER))
        return probCutBeta;

    if (!ss->inCheck && !ss->ttM && tte->move() != Move::None())
    {
        ttMove    = extract_tt_move(pos, tte->move());
        ss->ttM   = ttMove != Move::None();
        ttCapture = ss->ttM && pos.capture_stage(ttMove);
    }

    value = bestValue;

    Move bestMove = Move::None();

    std::uint8_t moveCount = 0;

    std::array<Moves, 2> mainMoves;

    Value singularValue   = +VALUE_INFINITE;
    bool  singularFailLow = false;

    const PieceDstHistory* contHistory[6]{(ss - 1)->continuationHistory,
                                          (ss - 2)->continuationHistory,
                                          (ss - 3)->continuationHistory,
                                          (ss - 4)->continuationHistory,
                                          nullptr,
                                          (ss - 6)->continuationHistory};

    int quietThreshold = std::max(200 - 3600 * depth, -7997);

    MovePicker mp(pos, ttMove, &mainHistory, &captureHistory, contHistory, &pawnHistory,
                  quietThreshold);

    bool pickQuiets = true;
    // Step 13. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move(pickQuiets)) != Move::None())
    {
        assert(move.is_ok() && pos.pseudo_legal(move));

        // Check for legality
        if (
          (isExcluded && move == excludedMove)
          // At root obey the "searchmoves" option and skip moves not listed in RootMove List.
          // In MultiPV mode also skip PV moves that have been already searched and those of lower "TB rank".
          || (RootNode ? !rootMoves.contains(curIdx, lstIdx, move)
                       : move != ttMove && !pos.legal(move)))
            continue;

        ss->moveCount = ++moveCount;

        if (RootNode && is_main_worker() && rootDepth > 30 && !options["ReportMinimal"])
            main_manager()->updateContext.onUpdateIter(
              {rootDepth, move, std::uint16_t(moveCount + curIdx)});

        if constexpr (PVNode)
            (ss + 1)->pv.clear();

        const Square dst          = move.dst_sq();
        const Piece  movedPiece   = pos.moved_piece(move);
        const Piece  exMovedPiece = pos.ex_moved_piece(move);

        const bool givesCheck = pos.gives_check(move);
        const bool capture    = pos.capture_stage(move);

        // Calculate new depth for this move
        Depth newDepth = depth - 1;

        const int delta      = beta - alpha;
        const int deltaRatio = (758 * delta) / rootDelta;

        std::int16_t r = reduction(depth, moveCount, deltaRatio, improving, recovering);

        // Step 14. Pruning at shallow depth (~120 Elo).
        // Depth conditions are important for mate finding.
        if (!RootNode && bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(stm))
        {
            // Skip quiet moves if moveCount exceeds our Futility Move Count threshold (~8 Elo)
            // clang-format off
            pickQuiets = pickQuiets && moveCount < ((3 + sqr(depth)) >> (1 - improving))
                                                 - (singularFailLow && !improving && singularValue < -80 + alpha);
            // clang-format on

            // Reduced depth of the next LMR search
            Depth lmrDepth = newDepth - r;

            if (capture || givesCheck)
            {
                Piece capturedPiece = pos.captured_piece(move);

                int capHistory = captureHistory[movedPiece][dst][type_of(capturedPiece)];

                // Futility pruning for captures (~2 Elo)
                if (lmrDepth < 7 + 2 * improving && !givesCheck && !ss->inCheck)
                {
                    Value futilityValue = std::min(284 + ss->staticEval + 266 * lmrDepth
                                                     + PIECE_VALUE[capturedPiece] + capHistory / 7,
                                                   +VALUE_INFINITE);
                    if (futilityValue <= alpha)
                        continue;
                }

                // SEE based pruning for captures and checks (~11 Elo)
                int seeHist = std::clamp(capHistory / 32, -177 * depth, +166 * depth);
                if (pos.see(move) < -seeHist - 161 * depth)
                    continue;
            }
            else
            {
                int history = (*contHistory[0])[exMovedPiece][dst]  //
                            + (*contHistory[1])[exMovedPiece][dst]  //
                            + pawnHistory[pawn_index(pos.pawn_key())][exMovedPiece][dst];

                // Continuation history based pruning (~2 Elo)
                if (history < -4165 * depth)
                    continue;

                history += 2 * mainHistory[stm][move.org_dst()];

                lmrDepth += history / 3728;

                // clang-format off
                Value futilityValue = std::min(55 + ss->staticEval + 89 * (bestValue < -52 + ss->staticEval) + 143 * lmrDepth, VALUE_TB_WIN_IN_MAX_PLY - 1);
                // clang-format on

                // Futility pruning: parent node (~13 Elo)
                if (lmrDepth < 12 && !ss->inCheck && futilityValue <= alpha)
                {
                    if (bestValue < futilityValue)
                        bestValue = futilityValue;
                    continue;
                }

                lmrDepth = std::max(lmrDepth, DEPTH_ZERO);

                // Prune moves with negative SEE (~4 Elo)
                if (pos.see(move) < -23 * sqr(lmrDepth))
                    continue;
            }
        }

        std::int8_t extension = 0;
        // Step 15. Extensions (~100 Elo)
        // Take care to not overdo to avoid search getting stuck.
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
            if (!RootNode && !isExcluded && ss->ttM && move == ttMove
                && depth > 3 - (completedDepth > 38) + ss->ttPv  //
                && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY   //
                && ttd.depth() >= depth - 3 && (ttd.bound() & BOUND_LOWER))
            {
                // clang-format off
                Value singularBeta  = std::max(ttValue - (57 + 72 * (!PVNode && ss->ttPv)) * depth / 64, -VALUE_INFINITE + 1);
                Depth singularDepth = newDepth / 2;
                assert(singularDepth > DEPTH_ZERO);

                singularValue   = search<NonPV>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode, move);
                singularFailLow = singularValue < singularBeta;

                if (singularFailLow)
                {
                    int doubleMargin =  0 + 284 * PVNode - 198 * !ttCapture;
                    int tripleMargin = 98 + 273 * PVNode - 261 * !ttCapture + 90 * ss->ttPv;

                    extension = 1 + (singularValue < singularBeta - doubleMargin)
                                  + (singularValue < singularBeta - tripleMargin);

                    depth += !PVNode && depth < 16;
                }

                // Multi-cut pruning
                // If the ttMove is assumed to fail high based on the bound of the TT entry, and
                // if after excluding the ttMove with a reduced search fail high over the original beta,
                // assume this expected cut-node is not singular (multiple moves fail high),
                // and can prune the whole subtree by returning a soft-bound.
                else if (singularValue = std::min(+singularValue, VALUE_TB_WIN_IN_MAX_PLY - 1);
                         singularValue >= beta)
                    return singularValue;
                // clang-format on

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
                     && captureHistory[movedPiece][dst][type_of(pos.piece_on(dst))] > 3994)
                extension = 1;
        }

        // Add extension to new depth
        newDepth += extension;

        // Speculative prefetch as early as possible
        tt.prefetch_entry(pos.move_key(move));

        ss->history = 2 * mainHistory[stm][move.org_dst()]  //
                    + (*contHistory[0])[exMovedPiece][dst]  //
                    + (*contHistory[1])[exMovedPiece][dst] - 4148;

        // Update the current move (this must be done after singular extension search)
        ss->currentMove         = move;
        ss->continuationHistory = &continuationHistory[ss->inCheck][capture][exMovedPiece][dst];

        [[maybe_unused]] std::uint64_t prevNodes;
        if constexpr (RootNode)
            prevNodes = std::uint64_t(nodes);

        // Step 16. Make the move
        nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);

        // These reduction adjustments have proven non-linear scaling.
        // They are optimized to time controls of 180 + 1.8 and longer
        // so changing them or adding conditions that are similar
        // requires tests at these types of time controls.

        // Decrease reduction if position is or has been on the PV (~7 Elo)
        r -= ss->ttPv
           * (1 + (ttValue != VALUE_NONE && ttValue > alpha && ttd.bound() & BOUND_LOWER)
              + (ttHit && ttd.depth() >= depth));

        // Decrease reduction for PvNodes (~0 Elo on STC, ~2 Elo on LTC)
        r -= PVNode;

        // These reduction adjustments have no proven non-linear scaling.

        // Increase reduction for cut nodes (~4 Elo)
        r += cutNode * (2 - (ss->ttPv && ttHit && ttd.depth() >= depth));

        // Increase reduction if ttMove is a capture (~3 Elo)
        r += ttCapture;

        // Increase reduction on repetition (~1 Elo)
        r += ss->ply >= 4 && move == (ss - 4)->currentMove && pos.has_repeated();

        // Increase reduction if next ply has a lot of fail high (~5 Elo)
        if (ss->cutoffCount > 3)
            r += 1 + !PVCutNode;

        // Reduction for first picked move (ttMove) (~3 Elo)
        // Decrease but never allow it to go below 0.
        else if (ss->ttM && move == ttMove)
            r = std::max(r - 2, 0);

        // Decrease/increase reduction for moves with a good/bad history (~8 Elo)
        r -= ss->history / 10029;

        // Step 17. Late moves reduction / extension (LMR) (~117 Elo)
        if (depth > 1 && moveCount > 1 + (RootNode && depth < 10))
        {
            // To prevent problems when the max value is less than the min value,
            // std::clamp has been replaced by a more robust implementation.
            Depth lmrDepth = std::max(std::min(newDepth - r, newDepth + 1), 1);

            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, lmrDepth, true);

            // Do a full-depth search when reduced LMR search fails high
            if (value > alpha && r > 0)
            {
                // Adjust full-depth search based on LMR results
                // - if the result was good enough search deeper. (~1 Elo)
                // - if the result was bad enough search shallower. (~2 Elo)
                newDepth += (value > 34 + bestValue + 2 * newDepth)  //
                          - (value < 00 + bestValue + 1 * newDepth);

                if (newDepth > lmrDepth)
                    value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);

                // Post LMR continuation history updates (~1 Elo)
                int bonus = value >= beta ? +stat_bonus(newDepth) : -stat_malus(newDepth);

                update_continuation_histories(ss, exMovedPiece, dst, bonus);
            }
        }

        // Step 18. Full-depth search when LMR is skipped
        else if (!PVNode || moveCount > 1)
        {
            // Reduce search depth by 1
            // - if expected reduction is high. (~9 Elo)
            // - if expected reduction and no TT move. (~6 Elo)
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha,
                                   newDepth - ((r + 2 * !ss->ttM) > 3), !cutNode);
        }

        // For PV nodes only, do a full PV search on the first move or after a fail high,
        // otherwise let the parent node fail low with value <= alpha and try another move.
        if (PVNode && (moveCount == 1 || value > alpha))
        {
            (ss + 1)->pv.clear();

            // Extend move from transposition table
            newDepth += newDepth == DEPTH_ZERO && move == ttMove;

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

        if constexpr (RootNode)
        {
            RootMove& rm = *rootMoves.find(move);

            rm.avgValue = rm.avgValue != -VALUE_INFINITE ? (rm.avgValue + value) / 2 : value;
            rm.nodes += nodes - prevNodes;

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

                if constexpr (PVNode && !RootNode)  // Update pv even in fail-high case
                    update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta)
                {
                    if constexpr (!RootNode)
                        (ss - 1)->cutoffCount += 1 + !ss->ttM - (extension >= 2);
                    break;  // Fail-high
                }

                // Reduce other moves if have found at least one score improvement (~2 Elo)
                if (depth < 15 && std::abs(value) < VALUE_TB_WIN_IN_MAX_PLY)
                    depth = std::max(depth - 2, 1);
                assert(depth > DEPTH_ZERO);

                alpha = value;  // Update alpha! Always alpha < beta
            }
        }

        // Collection of moves
        mainMoves[capture] += move;
    }

    // Step 21. Check for mate and stalemate
    // All legal moves have been searched and if there are no legal moves,
    // it must be a mate or a stalemate.
    // If in a singular extension search then return a fail low score.
    assert(moveCount != 0 || !ss->inCheck || isExcluded || MoveList<LEGAL>(pos).size() == 0);

    // Adjust best value for fail high cases at non-pv nodes
    if (!PVNode && bestValue > beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (depth * bestValue + beta) / (depth + 1);

    if (moveCount == 0)
        bestValue = isExcluded ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // If there is a move that produces search value greater than alpha update the history of searched moves
    else if (bestMove != Move::None())
        update_histories(*this, pos, ss, depth, bestMove, mainMoves, isExcluded);

    // Bonus for prior quiet move that caused the fail low
    else if (prevIsQuiet)
    {
        int bonusMul = 122 * (depth > 5) + 39 * (PVNode || cutNode)
                     + 165 * ((ss - 1)->moveCount > 8)
                     + 107 * (!(ss - 0)->inCheck && bestValue <= +(ss - 0)->staticEval - 98)
                     + 134 * (!(ss - 1)->inCheck && bestValue <= -(ss - 1)->staticEval - 91)
                     // proportional to "how much damage have to undo"
                     + std::clamp(-(ss - 1)->history / 100, -94, 304);
        bonusMul = std::max(bonusMul, 0);

        int bonus = bonusMul * stat_bonus(depth);

        update_prev_quiet_histories(*this, pos, ss, 0, {bonus / 180, bonus / 25, bonus / 116});
    }

    if constexpr (PVNode)
        bestValue = std::min(bestValue, maxValue);

    // If no good move is found and the previous position was ttPv, then the previous
    // opponent move is probably good and the new position is added to the search tree. (~7 Elo)
    if (bestValue <= alpha)
        ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    // Write gathered information in transposition table
    // Static evaluation is saved as it was before correction history
    if ((!RootNode || curIdx == 0) && !isExcluded)
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

// Quiescence search function, which is called by the main search function,
// should be using static evaluation only, but tactical moves may confuse the static evaluation.
// To fight this horizon effect, implemented this qsearch of tactical moves only. (~155 Elo)
template<bool PVNode>
Value Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) noexcept {
    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= +VALUE_INFINITE);
    assert(PVNode || (alpha == beta - 1));

    const Key key = pos.key();

    // Check if have an upcoming move that draws by repetition. (~1 Elo)
    if (alpha < VALUE_DRAW && pos.upcoming_repetition(ss->ply) != Move::None())
    {
        alpha = draw_value(key, nodes);
        if (alpha >= beta)
            return alpha;
    }

    StateInfo st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    // Step 1. Initialize node
    if constexpr (PVNode)
        (ss + 1)->pv.clear();

    ss->inCheck = pos.checkers();

    const Color stm = pos.side_to_move();

    if (is_main_worker() && main_manager()->callsCount > 1)
        main_manager()->callsCount--;

    if constexpr (PVNode)
        // Update selDepth (selDepth from 1, ply from 0)
        selDepth = std::max<std::uint16_t>(selDepth, 1 + ss->ply);

    // Step 2. Check for an immediate draw or maximum ply reached
    if (ss->ply >= MAX_PLY || pos.is_draw(ss->ply))
        return ss->ply >= MAX_PLY && !ss->inCheck
               ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm])
               : draw_value(key, nodes);

    assert(0 <= ss->ply && ss->ply < MAX_PLY);

    // Step 3. Transposition table lookup
    const Key16 key16 = compress_key(key);

    auto [ttHit, tte, fte] = tt.probe(key, key16);
    TTEntry const ttd(*tte);
    TTUpdater     ttu(tte, fte, key16, ss->ply, tt.generation());

    Value ttValue = ttHit ? value_from_tt(ttd.value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move  ttMove  = extract_tt_move(pos, ttd.move());
    bool  ttPv    = ttHit && ttd.is_pv();

    // At non-PV nodes check for an early TT cutoff
    if (!PVNode && ttValue != VALUE_NONE && ttd.depth() >= DEPTH_ZERO
        && (ttd.bound() & bound_for_tt(ttValue >= beta))
        // For high rule50 counts don't produce transposition table cutoffs.
        && pos.rule50_count() < -10 + 2 * Position::DrawMoveCount)
    {
        if (ttValue > beta && std::abs(ttValue) < VALUE_TB_WIN_IN_MAX_PLY)
            ttValue = (ttd.depth() * ttValue + beta) / (ttd.depth() + 1);

        return ttValue;
    }

    const Square prevDst = (ss - 1)->currentMove.is_ok() ? (ss - 1)->currentMove.dst_sq() : SQ_NONE;

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
        if (ttValue != VALUE_NONE && (ttd.bound() & bound_for_tt(ttValue > bestValue)))
            bestValue = std::min(+ttValue, VALUE_TB_WIN_IN_MAX_PLY - 1);
    }
    else
    {
        // In case of null move search, use previous staticEval with a opposite sign.
        // Compute unadjusted static eval by undoing the correction history adjustment.
        unadjustedStaticEval = (ss - 1)->currentMove != Move::Null()
                               ? evaluate(pos, networks[numaAccessToken], accCaches, optimism[stm])
                               : adjust_static_eval<false>(-(ss - 1)->staticEval, *this, pos);

        bestValue = ss->staticEval = adjust_static_eval(unadjustedStaticEval, *this, pos);
    }

    // Stand pat. Return immediately if bestValue is at least beta
    if (bestValue >= beta)
    {
        if (!PVNode && bestValue > beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
            bestValue = (3 * bestValue + beta) / 4;

        if (!ttHit)
            ttu.update(DEPTH_NONE, false, BOUND_LOWER, Move::None(), bestValue,
                       unadjustedStaticEval);

        return bestValue;
    }

    alpha = std::max(alpha, bestValue);

    futilityBase = std::min(327 + ss->staticEval, +VALUE_INFINITE);

QS_MOVES_LOOP:

    // Initialize a MovePicker object for the current position, prepare to search the moves.
    // Because the depth is <= DEPTH_ZERO here, only captures, promotions will be generated.
    Value value;

    Move move, bestMove = Move::None();

    std::uint8_t moveCount = 0;

    const PieceDstHistory* contHistory[2]{(ss - 1)->continuationHistory,
                                          (ss - 2)->continuationHistory};

    MovePicker mp(pos, ttMove, &mainHistory, &captureHistory, contHistory, &pawnHistory);
    // Step 5. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
    while ((move = mp.next_move()) != Move::None())
    {
        assert(move.is_ok() && pos.pseudo_legal(move));
        assert(move == ttMove || pos.checkers() || pos.capture_stage(move));

        // Check for legality
        if (move != ttMove && !pos.legal(move))
            continue;

        ++moveCount;

        const Square dst          = move.dst_sq();
        const Piece  exMovedPiece = pos.ex_moved_piece(move);

        const bool givesCheck = pos.gives_check(move);
        const bool capture    = pos.capture_stage(move);

        // Step 6. Pruning
        if (bestValue > VALUE_TB_LOSS_IN_MAX_PLY && pos.non_pawn_material(stm))
        {
            // Futility pruning and moveCount pruning (~10 Elo)
            if (!givesCheck && dst != prevDst && futilityBase > VALUE_TB_LOSS_IN_MAX_PLY
                && move.type_of() != PROMOTION)
            {
                if (moveCount > 2)
                    continue;

                Value futilityValue =
                  std::min(futilityBase + PIECE_VALUE[pos.captured_piece(move)], +VALUE_INFINITE);
                // Static evaluation + value of piece going to captured is much lower than alpha prune this move. (~2 Elo)
                if (futilityValue <= alpha)
                {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }

                // Static evaluation is much lower than alpha and move is not winning material prune this move. (~2 Elo)
                if (futilityBase <= alpha && pos.see(move) <= 0)
                {
                    bestValue = std::max(bestValue, futilityBase);
                    continue;
                }

                // Static exchange evaluation is much worse than what is needed to not fall below alpha prune this move. (~1 Elo)
                if (futilityBase > alpha && pos.see(move) < -4 * (futilityBase - alpha))
                {
                    bestValue = alpha;
                    continue;
                }
            }

            // Continuation history based pruning (~3 Elo)
            if (!capture
                && ((*contHistory[0])[exMovedPiece][dst]  //
                    + (*contHistory[1])[exMovedPiece][dst]
                    + pawnHistory[pawn_index(pos.pawn_key())][exMovedPiece][dst])
                     <= 4849)
                continue;

            // Do not search moves with bad enough SEE values (~5 Elo)
            if (pos.see(move) < -82)
                continue;
        }

        // Speculative prefetch as early as possible
        tt.prefetch_entry(pos.move_key(move));

        // Update the current move
        ss->currentMove         = move;
        ss->continuationHistory = &continuationHistory[ss->inCheck][capture][exMovedPiece][dst];

        // Step 7. Make and search the move
        nodes.fetch_add(1, std::memory_order_relaxed);
        pos.do_move(move, st, givesCheck);

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
        assert(moveCount == 0 /*MoveList<LEGAL>(pos).size() == 0*/);
        return mated_in(ss->ply);  // Plies to mate from the root
    }

    // Adjust best value for fail high cases at non-pv nodes
    if (!PVNode && bestValue > beta && std::abs(bestValue) < VALUE_TB_WIN_IN_MAX_PLY)
        bestValue = (3 * bestValue + beta) / 4;

    // Save gathered info in transposition table
    // Static evaluation is saved as it was before adjustment by correction history
    Bound bound = bound_for_tt(bestValue >= beta);
    ttu.update(DEPTH_ZERO, ttPv, bound, bestMove, bestValue, unadjustedStaticEval);

    assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);

    return bestValue;
}

Move Worker::extract_tt_move(const Position& pos, Move ttMove) noexcept {
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

    StateInfo st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    Move bm = rootMoves.front()[0];
    rootPos.do_move(bm, st);

    // Legal moves for the opponent
    const MoveList<LEGAL> legalMoves(rootPos);
    if (legalMoves.size() != 0)
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

// Used to print debug info and, more importantly,
// to detect when out of available time and thus stop the search.
void MainSearchManager::should_abort(Worker& worker) noexcept {
    assert(callsCount > 0);
    if (--callsCount > 0)
        return;
    callsCount = worker.limits.hitRate;

    TimePoint elapsedTime = elapsed(worker);

#if !defined(NDEBUG)
    static TimePoint infoTime = now();
    if (TimePoint curTime = worker.limits.startTime + elapsedTime; curTime - infoTime > 1000)
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
      && ((worker.limits.use_time_manager() && (stopPonderhit || elapsedTime > timeManager.maximum()))
       || (worker.limits.moveTime != 0 && elapsedTime >= worker.limits.moveTime)
       || (worker.limits.nodes != 0 && worker.threads.nodes() >= worker.limits.nodes)))
        worker.threads.stop = worker.threads.abort = true;
    // clang-format on
}

void MainSearchManager::init(std::uint16_t threadCount) noexcept {
    assert(threadCount != 0);
    timeManager.clear();
    isInit            = true;
    prevBestCurValue  = VALUE_ZERO;
    prevBestAvgValue  = VALUE_ZERO;
    prevTimeReduction = 1.0;

    auto threadReduction = 20.73 + 0.50 * std::log(threadCount);
    reductions[0]        = 0;
    for (std::size_t i = 1; i < reductions.size(); ++i)
        reductions[i] = std::int16_t(threadReduction * std::log(i));
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

void MainSearchManager::info_pv(Worker& worker, Depth depth) const noexcept {

    auto& rootPos   = worker.rootPos;
    auto& rootMoves = worker.rootMoves;

    std::uint64_t nodes    = worker.threads.nodes();
    std::uint16_t hashfull = worker.tt.hashfull();
    std::uint64_t tbHits   = worker.threads.tbHits() + worker.tbConfig.rootInTB * rootMoves.size();
    bool          showWDL  = worker.options["UCI_ShowWDL"];

    for (std::uint8_t i = 0; i < worker.multiPV; ++i)
    {
        RootMove& rm = rootMoves[i];

        bool isUpdated = rm.curValue != -VALUE_INFINITE;

        if (i != 0 && depth == 1 && !isUpdated)
            continue;

        Depth d = isUpdated ? depth : std::max<Depth>(depth - 1, 1);
        Value v = isUpdated ? rm.uciValue : rm.preValue;

        if (v == -VALUE_INFINITE)
            v = VALUE_ZERO;

        bool tb = worker.tbConfig.rootInTB && std::abs(v) <= VALUE_TB;
        if (tb)
            v = rm.tbValue;

        // tablebase- and previous-scores are exact
        bool isExact = i != worker.curIdx || tb || !isUpdated;

        // Potentially correct and extend the PV, and in exceptional cases value also
        if (std::abs(v) >= VALUE_TB_WIN_IN_MAX_PLY && std::abs(v) < VALUE_MATES_IN_MAX_PLY
            && (isExact || !(rm.lowerBound || rm.upperBound)))
            extend_tb_pv(rootPos, rm, v, worker.limits, worker.options);

        FullInfo info(rootPos, rm);
        info.depth     = d;
        info.value     = v;
        info.multiPV   = 1 + i;
        info.showBound = !isExact;
        info.showWDL   = showWDL;
        info.time      = std::max(elapsed(), 1ll);
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
        double e = double(uciELO - MIN_ELO) / (MAX_ELO - MIN_ELO);
        level    = std::clamp((((37.2473 * e - 40.8525) * e + 22.2943) * e - 0.311438), 0.0,
                              MAX_LEVEL - 0.1);
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

}  // namespace Search
}  // namespace DON
