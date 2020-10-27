#include "searcher.h"

#include <cassert>
#include <cstring> // For memset()
#include <cmath>
#include <algorithm>
#include <iostream>
#include <sstream>

#include "evaluator.h"
#include "movegenerator.h"
#include "movepicker.h"
#include "notation.h"
#include "polyglot.h"
#include "position.h"
#include "syzygytb.h"
#include "thread.h"
#include "threadmarker.h"
#include "timemanager.h"
#include "transposition.h"
#include "skillmanager.h"
#include "uci.h"
#include "zobrist.h"
#include "helper/logger.h"
#include "helper/prng.h"
#include "helper/reporter.h"

using Evaluator::evaluate;

Limit Limits;

namespace SyzygyTB {

    Depth   DepthLimit;
    int16_t PieceLimit;
    bool    Move50Rule;
    bool    HasRoot;
}

namespace {

    /// Stack keeps the information of the nodes in the tree during the search.
    struct Stack {

        Move *pv;
        PieceSquareStatsTable *pieceStats;

        int16_t ply;
        Move    playedMove;
        Move    excludedMove;
        Move    killerMoves[2];

        uint8_t moveCount;
        bool    inCheck;
        bool    ttPV;
        bool    ttHit;
        Value   staticEval;
        int32_t stats;
    };

    constexpr uint64_t TTHitAverageWindow{ 4096 };
    constexpr uint64_t TTHitAverageResolution{ 1024 };

    constexpr int32_t MAX_MOVES{ 256 };
    int32_t Reduction[MAX_MOVES];
    inline Depth reduction(Depth d, uint8_t mc, bool imp) noexcept {
        assert(d >= DEPTH_ZERO);
        auto const r{ Reduction[d] * Reduction[mc] };
        return Depth( (r + 509) / 1024 + 1 * (!imp && (r > 894)) );
    }

    /// Futility Move Count
    constexpr int16_t futilityMoveCount(Depth d, bool imp) noexcept {
        return int16_t( (3 + d*d) / (2 - 1 * imp) );
    }

    /// valueToTT() adjusts a mate or TB score from "plies to mate from the root" to
    /// "plies to mate from the current position". standard scores are unchanged.
    constexpr Value valueToTT(Value v, int32_t ply) noexcept {
        assert(v != VALUE_NONE);
        return v >= +VALUE_MATE_2_MAX_PLY ? v + ply :
               v <= -VALUE_MATE_2_MAX_PLY ? v - ply : v;
    }

    /// valueOfTT() adjusts a mate or TB score from the transposition table
    /// (which refers to the plies to mate/be mated from current position)
    /// to "plies to mate/be mated (TB win/loss) from the root".
    /// However, for mate scores, to avoid potentially false mate scores related to the 50 moves rule,
    /// and the graph history interaction, return an optimal TB score instead.
    constexpr Value valueOfTT(Value v, int32_t ply, int32_t clockPly) noexcept {

        if (v != VALUE_NONE) {
            // TB win or better
            if (v >= +VALUE_MATE_2_MAX_PLY) {
                // Don't return a potentially false mate score
                return v >= +VALUE_MATE_1_MAX_PLY
                    && VALUE_MATE - v >= 100 - clockPly ?
                        +VALUE_MATE_1_MAX_PLY - 1 : v - ply;
            }
            // TB loss or worse
            if (v <= -VALUE_MATE_2_MAX_PLY) {
                // Don't return a potentially false mate score
                return v <= -VALUE_MATE_1_MAX_PLY
                    && VALUE_MATE + v >= 100 - clockPly ?
                        -VALUE_MATE_1_MAX_PLY + 1 : v + ply;
            }
        }
        return v;
    }

    /// statBonus() is the bonus, based on depth
    constexpr int32_t statBonus(Depth depth) noexcept {
        return depth <= 13 ? (17 * depth + 134) * depth - 134 : 29;
    }

    /// Add a small random component to draw evaluations to avoid 3-fold-blindness
    Value drawValue(Thread const* th) noexcept {
        return VALUE_DRAW + Value(2 * (th->nodes & 1) - 1);
    }

    /// updateContinuationStats() updates Stats of the move pairs formed
    /// by moves at ply -1, -2, -4 and -6 with current move.
    void updateContinuationStats(Stack *ss, Piece pc, Square dst, int32_t bonus) noexcept {
        //assert(isOk(p));
        for (auto i : { 1, 2, 4, 6 }) {
            if (i > 2
             && ss->inCheck) {
                break;
            }
            if (isOk((ss-i)->playedMove)) {
                (*(ss-i)->pieceStats)[pc][dst] << bonus;
            }
        }
    }

    /// updateQuietStats() updates move sorting heuristics when a new quiet best move is found
    void updateQuietStats(Stack *ss, Position const &pos, Move move, int32_t bonus) noexcept {

        pos.thread()->butterFlyStats[pos.activeSide()][mMask(move)] << bonus;
        updateContinuationStats(ss, pos.movedPiece(move), dstSq(move), bonus);
    }

    void updateQuietRefutationStats(Stack *ss, Position const &pos, Move move, Depth depth, int32_t bonus) noexcept {

        updateQuietStats(ss, pos, move, bonus);

        if (pType(pos.movedPiece(move)) != PAWN) {
            pos.thread()->butterFlyStats[pos.activeSide()][mMask(reverseMove(move))] << -bonus;
        }

        if (depth > 11
         && ss->ply < MAX_LOWPLY) {
            assert(ss->ply >= 0);
            pos.thread()->lowPlyStats[ss->ply][mMask(move)] << statBonus(depth - 7);
        }

        // Refutation Moves
        if (ss->killerMoves[0] != move) {
            ss->killerMoves[1] = ss->killerMoves[0];
            ss->killerMoves[0] = move;
        }

        if (isOk((ss-1)->playedMove)) {
            pos.thread()->counterMoves[pos.prevMovedPiece((ss-1)->playedMove)][dstSq((ss-1)->playedMove)] = move;
        }
    }

    /// updatePV() appends the move and child pv
    void updatePV(Move *pv, Move move, Move *childPV) noexcept {
        *pv++ = move;
        if (childPV != nullptr) {
            while (*childPV != MOVE_NONE) {
                *pv++ = *childPV++;
            }
        }
        *pv = MOVE_NONE;
    }

    // The win rate model returns the probability (per mille) of winning given an eval
    // and a game-ply. The model fits rather accurately the LTC fishtest statistics.
    int16_t winRateModel(Value v, int16_t ply) {

        // The model captures only up to 240 plies, so limit input (and rescale)
        double const m{ std::min(ply, { 240 }) / 64.0 };

        // Coefficients of a 3rd order polynomial fit based on fishtest data
        // for two parameters needed to transform eval to the argument of a
        // logistic function.
        double const as[]{-8.24404295, 64.23892342, -95.73056462, 153.86478679};
        double const bs[]{-3.37154371, 28.44489198, -56.67657741,  72.05858751};
        double const a{ (((as[0] * m + as[1]) * m + as[2]) * m) + as[3] };
        double const b{ (((bs[0] * m + bs[1]) * m + bs[2]) * m) + bs[3] };

        // transform eval to centipawns with limited range
        double const x{ std::clamp(double(100 * v) / VALUE_EG_PAWN, -1000.0, 1000.0) };

        // Return win rate in per mille (rounded to nearest)
        return int16_t( 0.5 + 1000 / (1 + std::exp((a - x) / b)) );
    }

    /// wdl() report WDL statistics given an evaluation and a game ply, based on
    /// data gathered for fishtest LTC games.
    std::string wdl(Value v, int16_t ply) {
        std::stringstream ss;

        int16_t const wdl_w( winRateModel(v, ply) );
        int16_t const wdl_l( winRateModel(-v, ply) );
        int16_t const wdl_d( 1000 - wdl_w - wdl_l );
        ss << " wdl " << wdl_w << " " << wdl_d << " " << wdl_l;
        return ss.str();
    }

    /// multipvInfo() formats PV information according to UCI protocol.
    /// UCI requires that all (if any) un-searched PV lines are sent using a previous search score.
    std::string multipvInfo(Thread const *th, Depth depth, Value alfa, Value beta) {
        TimePoint const elapsed{ std::max(TimeMgr.elapsed(), { 1 }) };
        auto const nodes{ Threadpool.accumulate(&Thread::nodes) };
        auto const tbHits{ Threadpool.accumulate(&Thread::tbHits)
                         + th->rootMoves.size() * SyzygyTB::HasRoot };

        std::ostringstream oss;
        for (uint16_t i = 0; i < Threadpool.pvCount; ++i) {

            bool const updated{ th->rootMoves[i].newValue != -VALUE_INFINITE };

            if (i > 0
             && depth == 1
             && !updated) {
                continue;
            }

            auto v{ updated ? th->rootMoves[i].newValue : th->rootMoves[i].oldValue };

            if (v == -VALUE_INFINITE) {
                v = VALUE_ZERO;
            }

            bool const tb{
                SyzygyTB::HasRoot
             && std::abs(v) < +VALUE_MATE_1_MAX_PLY };
            if (tb) {
                v = th->rootMoves[i].tbValue;
            }

            oss << std::setfill('0')
                << "info"
                << " depth "    << std::setw(2) << (updated ? depth : std::max(1, depth-1))
                << " seldepth " << std::setw(2) << th->rootMoves[i].selDepth
                << " multipv "  << i + 1
                << std::setfill(' ')
                << " score "    << v;
            if (Options["UCI_ShowWDL"]) {
            oss << wdl(v, th->rootPos.clockPly());
            }
            if (!tb
             && i == th->pvCur) {
            oss << (beta <= v ? " lowerbound" : v <= alfa ? " upperbound" : "");
            }
            oss << " nodes "    << nodes
                << " time "     << elapsed
                << " nps "      << nodes * 1000 / elapsed
                << " tbhits "   << tbHits;
            if (elapsed > 1000) {
            oss << " hashfull " << TT.hashFull();
            }
            oss << " pv "       << th->rootMoves[i];
            if (i + 1 < Threadpool.pvCount) {
            oss << '\n';
            }
        }
        return oss.str();
    }

    /// quienSearch() is quiescence search function, which is called by the main depth limited search function when the remaining depth <= 0.
    template<bool PVNode>
    Value quienSearch(Position &pos, Stack *const ss, Value alfa, Value beta, Depth depth = DEPTH_ZERO) {

        assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
        assert(PVNode || (alfa == beta-1));
        assert(depth <= DEPTH_ZERO);

        Value actualAlfa;
        Move pv[MAX_PLY+1];

        if (PVNode) {
            actualAlfa = alfa; // To flag BOUND_EXACT when eval above alpha and no available moves
            (ss+1)->pv = pv;
            ss->pv[0] = MOVE_NONE;
        }

        ss->inCheck = pos.checkers() != 0;

        assert(1 <= ss->ply && ss->ply < MAX_PLY);
        assert(ss->ply == (ss-1)->ply + 1);
        (ss+1)->ply = ss->ply + 1;

        // Check for immediate draw or maximum ply reached.
        if (pos.draw(ss->ply)
         || ss->ply >= MAX_PLY) {
            return !ss->inCheck
                && ss->ply >= MAX_PLY ?
                    evaluate(pos) : VALUE_DRAW;
        }

        Move move;
        // Transposition table lookup.
        Key const posiKey { pos.posiKey() };
        auto *const tte   { TT.probe(posiKey, ss->ttHit) };
        auto const ttValue{ ss->ttHit ? valueOfTT(tte->value(), ss->ply, pos.clockPly()) : VALUE_NONE };
        auto       ttMove { ss->ttHit ? tte->move() : MOVE_NONE };
        auto const ttPV   { ss->ttHit && tte->pv() };

        // Decide whether or not to include checks.
        // Fixes also the type of TT entry depth that are going to use.
        // Note that in quienSearch use only 2 types of depth: DEPTH_QS_CHECK or DEPTH_QS_NO_CHECK.
        auto const qsDepth{
            ss->inCheck
         || depth >= DEPTH_QS_CHECK ?
                DEPTH_QS_CHECK : DEPTH_QS_NO_CHECK };

        if (!PVNode
         && ss->ttHit
         && ttValue != VALUE_NONE // Only in case of TT access race
         && tte->depth() >= qsDepth
         && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER))) {
            return ttValue;
        }

        if (ttMove != MOVE_NONE
         && !pos.pseudoLegal(ttMove)) {
            ttMove = MOVE_NONE;
        }

        Value bestValue
            , futilityBase;

        // Evaluate the position statically.
        if (ss->inCheck) {
            ss->staticEval = VALUE_NONE;
            // Starting from the worst case which is checkmate
            bestValue = futilityBase = -VALUE_INFINITE;
        }
        else {
            if (ss->ttHit) {
                // Never assume anything on values stored in TT.
                if ((ss->staticEval = bestValue = tte->eval()) == VALUE_NONE) {
                    ss->staticEval = bestValue = evaluate(pos);
                }

                // Can ttValue be used as a better position evaluation?
                if ( ttValue != VALUE_NONE
                 && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER))) {
                    bestValue = ttValue;
                }
            }
            else {
                ss->staticEval = bestValue = ((ss-1)->playedMove != MOVE_NULL ? evaluate(pos) : -(ss-1)->staticEval + 2 * VALUE_TEMPO);
            }

            
            // Stand pat. Return immediately if static value is at least beta
            if (bestValue >= beta) {

                if (!ss->ttHit) {
                    tte->save(posiKey,
                              MOVE_NONE,
                              valueToTT(bestValue, ss->ply),
                              ss->staticEval,
                              DEPTH_NONE,
                              BOUND_LOWER,
                              false);
                }

                assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);
                return bestValue;
            }

            // Update alfa! Always alfa < beta
            if (PVNode) {
                alfa = std::max(alfa, bestValue);
            }

            futilityBase = bestValue + 145;
        }

        auto *thread{ pos.thread() };

        auto bestMove{ MOVE_NONE };

        auto const activeSide{ pos.activeSide() };

        PieceSquareStatsTable const *contStats[]{
            (ss-1)->pieceStats, (ss-2)->pieceStats,
            nullptr           , (ss-4)->pieceStats,
            nullptr           , (ss-6)->pieceStats
        };

        // Initialize move-picker(2) for the current position
        MovePicker movePicker{
            pos,
            ttMove, depth,
            &thread->butterFlyStats,
            &thread->captureStats,
            contStats,
            dstSq((ss-1)->playedMove) };

        uint8_t moveCount{ 0 };
        StateInfo si;
        // Loop through all the pseudo-legal moves until no moves remain or a beta cutoff occurs
        while ((move = movePicker.nextMove()) != MOVE_NONE) {
            assert(isOk(move)
                && (//ss->inCheck ||
                    pos.pseudoLegal(move)));

            // Check for legality
            if (!pos.legal(move)) {
                continue;
            }

            ++moveCount;

            bool const giveCheck{ pos.giveCheck(move) };
            bool const captureOrPromotion{ pos.captureOrPromotion(move) };

            // Futility pruning
            if (!ss->inCheck
             && !giveCheck
             && futilityBase > -VALUE_KNOWN_WIN
             && !pos.advancedPawnPush(move)
             && Limits.mate == 0) {
                assert(mType(move) != ENPASSANT); // Due to !pos.advancedPawnPush()

                // Move Count pruning
                if (moveCount > 2) {
                    continue;
                }

                // Futility pruning parent node
                auto const futilityValue{ futilityBase + PieceValues[EG][pos.captured(move)] };
                if (futilityValue <= alfa) {
                    bestValue = std::max(bestValue, futilityValue);
                    continue;
                }
                // Prune moves with negative or zero SEE
                if (futilityBase <= alfa
                 && !pos.see(move, VALUE_ZERO + 1)) {
                    bestValue = std::max(bestValue, futilityBase);
                    continue;
                }
            }
            assert(moveCount != 0);

            // Don't search moves with negative SEE values
            if (!ss->inCheck
             && !(giveCheck
               && contains(pos.kingBlockers(~activeSide), orgSq(move)))
             && !pos.see(move)
             && Limits.mate == 0) {
                continue;
            }

            // CounterMove based pruning
            if (!captureOrPromotion
             && (*contStats[0])[pos.movedPiece(move)][dstSq(move)] < CounterMovePruneThreshold
             && (*contStats[1])[pos.movedPiece(move)][dstSq(move)] < CounterMovePruneThreshold) {
                continue;
            }

            // Speculative prefetch as early as possible
            prefetch(TT.cluster(pos.movePosiKey(move)));

            // Update the current move
            ss->playedMove = move;
            ss->pieceStats = &thread->continuationStats[ss->inCheck][captureOrPromotion][pos.movedPiece(move)][dstSq(move)];

            // Do the move
            pos.doMove(move, si, giveCheck);
            auto const value{ -quienSearch<PVNode>(pos, ss+1, -beta, -alfa, depth-1) };
            // Undo the move
            pos.undoMove(move);

            assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

            // Check for new best move.
            if (bestValue < value) {
                bestValue = value;

                if (value > alfa) {
                    bestMove = move;

                    if (PVNode) { // Update pv even in fail-high case
                        updatePV(ss->pv, move, (ss+1)->pv);
                    }

                    if (PVNode && value < beta) {// Update alpha here!
                        alfa = value;
                    }
                    else {
                        break; // Fail high
                    }
                }
            }
        }

        // All legal moves have been searched.
        // A special case: If in check and no legal moves were found, it is checkmate.
        if (ss->inCheck
         && bestValue == -VALUE_INFINITE) {
            return matedIn(ss->ply); // Plies to mate from the root
        }

        tte->save(posiKey,
                  bestMove,
                  valueToTT(bestValue, ss->ply),
                  ss->staticEval,
                  qsDepth,
                  bestValue >= beta ? BOUND_LOWER :
                  PVNode && bestValue > actualAlfa ? BOUND_EXACT : BOUND_UPPER,
                  ttPV);

        assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);
        return bestValue;
    }
    // Explicit template instantiations
    template Value quienSearch<false>(Position&, Stack* const, Value, Value, Depth);
    template Value quienSearch<true >(Position&, Stack* const, Value, Value, Depth);

    /// depthSearch() is main depth limited search function, which is called when the remaining depth > 0.
    template<bool PVNode>
    Value depthSearch(Position &pos, Stack *const ss, Value alfa, Value beta, Depth depth, bool cutNode) {

        bool const rootNode{ PVNode && ss->ply == 0 };
        Depth const maxDepth{ rootNode ? depth : Depth(depth + 1) };

        auto *thread{ pos.thread() };

        // Check if there exists a move which draws by repetition,
        // or an alternative earlier move to this position.
        if (!rootNode
         && alfa < VALUE_DRAW
         && pos.clockPly() >= 3
         && pos.cycled(ss->ply)) {

            alfa = drawValue(thread);
            if (alfa >= beta) {
                return alfa;
            }
        }

        // Dive into quiescence search when the depth reaches zero
        if (depth <= DEPTH_ZERO) {
            return quienSearch<PVNode>(pos, ss, alfa, beta);
        }

        assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
        assert(PVNode || (alfa == beta-1));
        assert(!(PVNode && cutNode));
        assert(DEPTH_ZERO < depth && depth < MAX_PLY);

        // Step 1. Initialize node
        ss->moveCount = 0;
        ss->inCheck = pos.checkers() != 0;

        // Check for the available remaining limit
        if (thread == Threadpool.mainThread()) {
            static_cast<MainThread*>(thread)->tick();
        }

        if (PVNode) {
            // Used to send selDepth info to GUI (selDepth from 1, ply from 0)
            thread->selDepth = std::max(Depth(ss->ply + 1), thread->selDepth);
        }

        if (!rootNode) {
            // Step 2. Check for aborted search, immediate draw or maximum ply reached.
            if (Threadpool.stop.load(std::memory_order::memory_order_relaxed)
             || pos.draw(ss->ply)
             || ss->ply >= MAX_PLY) {
                return !ss->inCheck
                    && ss->ply >= MAX_PLY ?
                        evaluate(pos) : drawValue(thread);
            }

            // Step 3. Mate distance pruning.
            // Even if mate at the next move our score would be at best matesIn(ss->ply+1),
            // but if alfa is already bigger because a shorter mate was found upward in the tree
            // then there is no need to search further, will never beat current alfa.
            // Same logic but with reversed signs applies also in the opposite condition of
            // being mated instead of giving mate, in this case return a fail-high score.
            alfa = std::max(matedIn(ss->ply+0), alfa);
            beta = std::min(matesIn(ss->ply+1), beta);
            if (alfa >= beta) {
                return alfa;
            }

            // Initialize stats to zero for the grandchildren of the current position.
            // So stats is shared between all grandchildren and only the first grandchild starts with stats = 0.
            // Later grandchildren start with the last calculated stats of the previous grandchild.
            // This influences the reduction rules in LMR which are based on the stats of parent position.
            (ss+2)->stats = 0;
        }

        assert(0 <= ss->ply && ss->ply < MAX_PLY);
        assert(ss->ply == (ss-1)->ply + 1);
        (ss+1)->ply = ss->ply + 1;
        (ss+1)->ttPV = false;
        (ss+1)->excludedMove = MOVE_NONE;
        (ss+2)->killerMoves[0] =
        (ss+2)->killerMoves[1] = MOVE_NONE;

        Move pv[MAX_PLY + 1];
        Value value;
        auto bestValue{ -VALUE_INFINITE };
        auto maxValue{ +VALUE_INFINITE };

        auto bestMove{ MOVE_NONE };

        auto const excludedMove{ ss->excludedMove };
        // Step 4. Transposition table lookup.
        // Don't want the score of a partial search to overwrite
        // a previous full search TT value, so we use a different
        // position key in case of an excluded move.
        Key const posiKey { excludedMove == MOVE_NONE ?
                                pos.posiKey() :
                                pos.posiKey() ^ makeKey(excludedMove) };
        auto *const tte   { TT.probe(posiKey, ss->ttHit) };
        auto const ttValue{ ss->ttHit ? valueOfTT(tte->value(), ss->ply, pos.clockPly()) : VALUE_NONE };
        auto       ttMove { rootNode ? thread->rootMoves[thread->pvCur][0] :
                            ss->ttHit ? tte->move() : MOVE_NONE };

        if (excludedMove == MOVE_NONE) {
            ss->ttPV = PVNode || (ss->ttHit && tte->pv());
        }
        auto const pastPV { !PVNode && ss->ttPV };

        auto const activeSide{ pos.activeSide() };
        bool const pmCapOrPro{ pos.captured() != NONE
                            || pos.promoted() };

        if (ss->ttPV
         && depth > 12
         && isOk((ss-1)->playedMove)
         && !pmCapOrPro
         && (ss-1)->ply < MAX_LOWPLY) {
            assert((ss-1)->ply >= 0);
            thread->lowPlyStats[(ss-1)->ply][mMask((ss-1)->playedMove)] << statBonus(depth - 5);
        }

        // ttHitAvg can be used to approximate the running average of ttHit
        thread->ttHitAvg = (TTHitAverageWindow - 1) * thread->ttHitAvg / TTHitAverageWindow
                         + TTHitAverageResolution * ss->ttHit;

        // At non-PV nodes we check for an early TT cutoff
        if (!PVNode
         &&  ss->ttHit
         &&  ttValue != VALUE_NONE
         &&  tte->depth() >= depth
         && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER))) {

            if (ttMove != MOVE_NONE) {

                if (ttValue >= beta) {
                    // If ttMove is quiet, update move sorting heuristics on TT hit
                    if (!pos.captureOrPromotion(ttMove)) {
                        updateQuietRefutationStats(ss, pos, ttMove, depth, statBonus(depth));
                    }
                    // Extra penalty for early quiet moves in previous ply when it gets refuted
                    if (!pmCapOrPro
                     && (ss-1)->moveCount <= 2) {
                        updateContinuationStats(ss-1, pos.prevMovedPiece((ss-1)->playedMove), dstSq((ss-1)->playedMove), -statBonus(depth + 1));
                    }
                }
                // Penalty for a quiet ttMove that fails low
                else if (!pos.captureOrPromotion(ttMove)) {
                    updateQuietStats(ss, pos, ttMove, -statBonus(depth));
                }
            }

            if (pos.clockPly() < 90) {
                return ttValue;
            }
        }

        // Step 5. Tablebases probe.
        if (!rootNode
         && SyzygyTB::PieceLimit != 0) {
            auto const pieceCount{ pos.count() };

            if (( pieceCount  < SyzygyTB::PieceLimit
              || (pieceCount == SyzygyTB::PieceLimit
               && depth >= SyzygyTB::DepthLimit))
             && pos.clockPly() == 0
             && pos.castleRights() == CR_NONE) {

                SyzygyTB::ProbeState probeState;
                auto const wdlScore{ SyzygyTB::probeWDL(pos, probeState) };

                // Force check of time on the next occasion
                if (thread == Threadpool.mainThread()) {
                    static_cast<MainThread*>(thread)->tickCount = 0;
                }

                if (probeState != SyzygyTB::ProbeState::PS_FAILURE) {
                    thread->tbHits.fetch_add(1, std::memory_order::memory_order_relaxed);

                    int16_t const draw{ SyzygyTB::Move50Rule };

                    value = wdlScore < -draw ? -VALUE_MATE_1_MAX_PLY + (ss->ply + 1) :
                            wdlScore > +draw ? +VALUE_MATE_1_MAX_PLY - (ss->ply + 1) :
                                                VALUE_DRAW + 2 * int32_t(wdlScore) * draw;

                    auto const bound{
                        wdlScore < -draw ? BOUND_UPPER :
                        wdlScore > +draw ? BOUND_LOWER : BOUND_EXACT };

                    if ( bound == BOUND_EXACT
                     || (bound == BOUND_LOWER ? beta <= value : value <= alfa)) {
                        tte->save(posiKey,
                                  MOVE_NONE,
                                  valueToTT(value, ss->ply),
                                  VALUE_NONE,
                                  Depth(std::min(depth + 6, MAX_PLY - 1)),
                                  bound,
                                  ss->ttPV);
                        return value;
                    }

                    if (PVNode) {
                        if (bound == BOUND_LOWER) {
                            bestValue = value, alfa = std::max(alfa, bestValue);
                        }
                        else {
                            maxValue = value;
                        }
                    }
                }
            }
        }

        if (!rootNode
         && ttMove != MOVE_NONE
         && !pos.pseudoLegal(ttMove)) {
            ttMove = MOVE_NONE;
        }

        StateInfo si;

        bool improving;
        Value eval;
        Move move;

        // Step 6. Static evaluation of the position
        if (ss->inCheck) {

            ss->staticEval = eval = VALUE_NONE;
            improving = false;
        }
        // Early pruning
        else {

            if (ss->ttHit) {
                // Never assume anything on values stored in TT.
                if ((ss->staticEval = eval = tte->eval()) == VALUE_NONE) {
                    ss->staticEval = eval = evaluate(pos);
                }

                if (eval == VALUE_DRAW) {
                    eval = drawValue(thread);
                }
                // Can ttValue be used as a better position evaluation?
                if ( ttValue != VALUE_NONE
                 && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER))) {
                    eval = ttValue;
                }
            }
            else {
                ss->staticEval = eval = ((ss-1)->playedMove != MOVE_NULL ? evaluate(pos) : -(ss-1)->staticEval + 2 * VALUE_TEMPO);

                tte->save(posiKey,
                          MOVE_NONE,
                          VALUE_NONE,
                          eval,
                          DEPTH_NONE,
                          BOUND_NONE,
                          ss->ttPV);
            }

            // Step 7. Razoring (~1 ELO)
            if (!rootNode // The RootNode PV handling is not available in qsearch
             && depth == 1
                // Razor Margin
             && eval <= alfa - 510) {
                return quienSearch<PVNode>(pos, ss, alfa, beta);
            }

            improving = (ss-2)->staticEval != VALUE_NONE ? ss->staticEval > (ss-2)->staticEval :
                        (ss-4)->staticEval != VALUE_NONE ? ss->staticEval > (ss-4)->staticEval :
                        (ss-6)->staticEval != VALUE_NONE ? ss->staticEval > (ss-6)->staticEval : true;

            // Step 8. Futility pruning: child node (~50 ELO)
            // Betting that the opponent doesn't have a move that will reduce
            // the score by more than futility margins if do a null move.
            if (!PVNode
             && depth < 8
             && eval < +VALUE_KNOWN_WIN // Don't return unproven wins.
                // Futility Margin
             && eval - (223 * (depth - improving)) >= beta
             && Limits.mate == 0) {
                return eval;
            }

            // Step 9. Null move search with verification search (~40 ELO)
            if (!PVNode
             && eval >= beta
             && eval >= ss->staticEval
             && excludedMove == MOVE_NONE
             && (ss-1)->playedMove != MOVE_NULL
             && (ss-1)->stats < 22977
             && ss->staticEval >= beta - 30 * depth - 28 * improving + 84 * ss->ttPV + 182
             && pos.nonPawnMaterial(activeSide) != VALUE_ZERO
             // Null move pruning disabled for activeSide until ply exceeds nmpPly
             && (ss->ply >= thread->nmpMinPly
              || activeSide != thread->nmpColor)
             && Limits.mate == 0) {
                // Null move dynamic reduction based on depth and static evaluation.
                Depth const R( (982 + 85 * depth) / 256 + std::min(int32_t(eval - beta) / 192, 3) );
                Depth const nullDepth( depth - R );

                ss->playedMove = MOVE_NULL;
                ss->pieceStats = &thread->continuationStats[0][0][NO_PIECE][0];

                pos.doNullMove(si);

                auto nullValue{ -depthSearch<false>(pos, ss+1, -beta, -beta+1, nullDepth, !cutNode) };

                pos.undoNullMove();

                if (nullValue >= beta) {

                    // Don't return unproven mates or TB scores
                    if (nullValue >= +VALUE_MATE_2_MAX_PLY) {
                        nullValue = beta;
                    }
                    // Skip verification search
                    if (thread->nmpMinPly != 0 // Recursive verification is not allowed
                     || (depth < 13
                      && std::abs(beta) < +VALUE_KNOWN_WIN)) {
                        return nullValue;
                    }

                    assert(thread->nmpMinPly == 0); // Recursive verification is not allowed

                    // Do verification search at high depths,
                    // with null move pruning disabled for activeSide, until ply exceeds nmpMinPly.
                    thread->nmpColor = activeSide;
                    thread->nmpMinPly = ss->ply + 3 * nullDepth / 4;
                    Value v = depthSearch<false>(pos, ss, beta-1, beta, nullDepth, false);
                    thread->nmpMinPly = 0;

                    if (v >= beta) {
                        return nullValue;
                    }
                }
            }

            Value const probCutBeta( beta + 176 - 49 * improving );

            // Step 10. ProbCut. (~10 ELO)
            // If good enough capture and a reduced search returns a value much above beta,
            // then can (almost) safely prune the previous move.
            // Note: Only enter ProbCut with no TT hit, a too low TT depth, or a good enough TT value.
            if (!PVNode
             && depth > 4
             && std::abs(beta) < +VALUE_MATE_2_MAX_PLY
                // if value from transposition table is lower than probCutBeta, don't attempt probCut
                // there and in further interactions with transposition table cutoff depth is set to depth - 3
                // because probCut search has depth set to depth - 4 but we also do a move before it
                // so effective depth is equal to depth - 3
             && !(ss->ttHit
               && ttValue != VALUE_NONE
               && ttValue < probCutBeta
               && tte->depth() >= depth - 3)
             && Limits.mate == 0) {

                // if ttMove is a capture and value from transposition table is good enough produce probCut
                // cutoff without digging into actual probCut search
                if (ss->ttHit
                 && ttMove != MOVE_NONE
                 && ttValue != VALUE_NONE
                 && ttValue >= probCutBeta
                 && tte->depth() >= depth - 3
                 && pos.captureOrPromotion(ttMove)) { 
                    return probCutBeta;
                }

                assert(probCutBeta < +VALUE_INFINITE);

                bool const ttPV{ ss->ttPV };
                ss->ttPV = false;

                bool const captureOrPromotion{ true };
                uint8_t probCutCount{ 0 };
                // Initialize move-picker(3) for the current position
                MovePicker movePicker{
                    pos,
                    ttMove, probCutBeta - ss->staticEval,
                    &thread->captureStats };
                // Loop through all the pseudo-legal moves until no moves remain or a beta cutoff occurs
                while ((move = movePicker.nextMove()) != MOVE_NONE
                    && probCutCount < (2 + 2 * cutNode)) {
                    assert(isOk(move)
                        && pos.pseudoLegal(move));

                    assert(!ss->inCheck);
                    assert(pos.captureOrPromotion(move)
                        && mType(move) != CASTLE);

                    if (move == excludedMove) {
                        continue;
                    }
                    if (!pos.legal(move)) {
                        continue;
                    }

                    ++probCutCount;

                    // Speculative prefetch as early as possible
                    prefetch(TT.cluster(pos.movePosiKey(move)));

                    ss->playedMove = move;
                    ss->pieceStats = &thread->continuationStats[ss->inCheck][captureOrPromotion][pos.movedPiece(move)][dstSq(move)];

                    pos.doMove(move, si);

                    // Perform a preliminary quienSearch to verify that the move holds
                    value = -quienSearch<false>(pos, ss+1, -probCutBeta, -probCutBeta+1);

                    // If the quienSearch held perform the regular search
                    if (value >= probCutBeta) {
                        value = -depthSearch<false>(pos, ss+1, -probCutBeta, -probCutBeta+1, depth - 4, !cutNode);
                    }

                    pos.undoMove(move);

                    if (value >= probCutBeta) {
                        // If TT doesn't have equal or more deep info write probCut data into it
                        if (!(ss->ttHit
                           && ttValue != VALUE_NONE
                           && tte->depth() >= depth - 3)) {

                            tte->save(posiKey,
                                      move,
                                      valueToTT(value, ss->ply),
                                      ss->staticEval,
                                      depth - 3,
                                      BOUND_LOWER,
                                      ttPV);
                        }

                        return value;
                    }
                }

                ss->ttPV = ttPV;
            }

            // Step 11. If the position is not in TT, decrease depth by 2
            if (PVNode
             && depth >= 6
             && ttMove == MOVE_NONE) {
                depth -= 2;
            }

        }

        value = bestValue;

        // Mark this node as being searched.
        ThreadMarker threadMarker{ thread, posiKey, ss->ply };

        bool singularQuietLMR{ false };
        bool moveCountPruning{ false };
        bool const ttmCapture{ ttMove != MOVE_NONE
                            && pos.captureOrPromotion(ttMove) };

        PieceSquareStatsTable const *contStats[]{
            (ss-1)->pieceStats, (ss-2)->pieceStats,
            nullptr           , (ss-4)->pieceStats,
            nullptr           , (ss-6)->pieceStats
        };

        Move const counterMove( pos.thread()->counterMoves[pos.prevMovedPiece((ss-1)->playedMove)][dstSq((ss-1)->playedMove)] );

        // Initialize move-picker(1) for the current position
        MovePicker movePicker{
            pos,
            ttMove, depth,
            &thread->butterFlyStats,
            &thread->lowPlyStats,
            &thread->captureStats,
            contStats,
            ss->ply, ss->killerMoves, counterMove };

        uint8_t moveCount{ 0 };
        Moves quietMoves;   quietMoves.reserve(32);
        Moves captureMoves; captureMoves.reserve(16);

        // Step 12. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
        while ((move = movePicker.nextMove()) != MOVE_NONE) {
            assert(isOk(move)
                && (//ss->inCheck ||
                    pos.pseudoLegal(move)));

            // Skip exclusion move
            if (move == excludedMove) {
                continue;
            }

            if (rootNode) {
                // At root obey the "searchmoves" option and skip moves not listed in
                // RootMove List. As a consequence any illegal move is also skipped.
                // In MultiPV mode we also skip PV moves which have been already searched
                // and those of lower "TB rank" if we are in a TB root position.
                if (!thread->rootMoves.contains(thread->pvCur, thread->pvEnd, move)) {
                    continue;
                }
            }
            else {
                // Check for legality
                if (!pos.legal(move)) {
                    continue;
                }
            }

            ss->moveCount = ++moveCount;

            if (rootNode
             && thread == Threadpool.mainThread()) {
                TimePoint const elapsed{ TimeMgr.elapsed() };
                if (elapsed > 3000) {
                    sync_cout << std::setfill('0')
                              << "info"
                              << " depth "          << std::setw(2) << depth
                              << " seldepth "       << std::setw(2) << thread->rootMoves.find(thread->pvCur, thread->pvEnd, move)->selDepth
                              << " currmove "       << move
                              << " currmovenumber " << std::setw(2) << thread->pvCur + moveCount
                              //<< " maxmoves "       << thread->rootMoves.size()
                              << " time "           << elapsed
                              << std::setfill(' ')  << sync_endl;
                }
            }

            if (PVNode) {
                (ss+1)->pv = nullptr;
            }

            auto const mpc{ pos.movedPiece(move) };
            bool const giveCheck{ pos.giveCheck(move) };
            bool const captureOrPromotion{ pos.captureOrPromotion(move) };

            // Calculate new depth for this move
            Depth newDepth( depth-1 );

            // Step 13. Pruning at shallow depth. (~200 ELO)
            if (!rootNode
             && bestValue > -VALUE_MATE_2_MAX_PLY
             && pos.nonPawnMaterial(activeSide) != VALUE_ZERO
             && Limits.mate == 0) {
                // Skip quiet moves if move count exceeds our futilityMoveCount() threshold
                moveCountPruning = moveCount >= futilityMoveCount(depth, improving);
                movePicker.pickQuiets = !moveCountPruning;

                // Reduced depth of the next LMR search.
                Depth const lmrDepth( std::max(newDepth - reduction(depth, moveCount, improving), 0) );

                if (giveCheck
                 || captureOrPromotion) {

                    // Capture history based pruning when the move doesn't give check
                    if (!giveCheck
                     && lmrDepth < 1
                     && thread->captureStats[mpc][dstSq(move)][pos.captured(move)] < 0) {
                        continue;
                    }
                    // SEE based pruning: negative SEE (~25 ELO)
                    if (!pos.see(move, Value(-221 * depth))) {
                        continue;
                    }
                }
                else {
                    // Counter moves based pruning: (~20 ELO)
                    if (lmrDepth < 4 + ((ss-1)->stats > 0 || (ss-1)->moveCount == 1)
                     && (*contStats[0])[mpc][dstSq(move)] < CounterMovePruneThreshold
                     && (*contStats[1])[mpc][dstSq(move)] < CounterMovePruneThreshold) {
                        continue;
                    }
                    // Futility pruning: parent node. (~5 ELO)
                    if (lmrDepth < 7
                     && !ss->inCheck
                     && ss->staticEval + 170 * lmrDepth + 283 <= alfa
                     && ((*contStats[0])[mpc][dstSq(move)]
                       + (*contStats[1])[mpc][dstSq(move)]
                       + (*contStats[3])[mpc][dstSq(move)]
                       + (*contStats[5])[mpc][dstSq(move)] / 2) < 27376) {
                        continue;
                    }
                    // SEE based pruning: negative SEE (~20 ELO)
                    if (!pos.see(move, Value(-(29 - std::min(lmrDepth, { 18 })) * lmrDepth*lmrDepth))) {
                        continue;
                    }
                }
            }

            // Step 14. Extensions. (~75 ELO)
            Depth extension{ DEPTH_ZERO };

            // Singular extension (SE) (~70 ELO)
            // Extend the TT move if its value is much better than its siblings.
            // If all moves but one fail low on a search of (alfa-s, beta-s),
            // and just one fails high on (alfa, beta), then that move is singular and should be extended.
            // To verify this do a reduced search on all the other moves but the ttMove,
            // if result is lower than ttValue minus a margin then extend ttMove.
            if (!rootNode
             && depth >= 7
             && move == ttMove
             && excludedMove == MOVE_NONE // Avoid recursive singular search
             //&& ttValue != VALUE_NONE  Already implicit in the next condition
             && std::abs(ttValue) < VALUE_KNOWN_WIN
             && (tte->bound() & BOUND_LOWER)
             &&  tte->depth() >= depth - 3) {

                Value const singularBeta( ttValue - ((4 + pastPV) * depth) / 2 );
                Depth const singularDepth( (depth + 3 * pastPV - 1) / 2 );

                ss->excludedMove = ttMove;
                value = depthSearch<false>(pos, ss, singularBeta-1, singularBeta, singularDepth, cutNode);
                ss->excludedMove = MOVE_NONE;

                if (value < singularBeta) {
                    extension = 1;
                    singularQuietLMR = !ttmCapture;
                }
                // Multi-cut pruning
                // Our ttMove is assumed to fail high, and now failed high also on a reduced
                // search without the ttMove. So assume this expected Cut-node is not singular,
                // multiple moves fail high, and can prune the whole subtree by returning the soft bound.
                else
                if (singularBeta >= beta) {
                    return singularBeta;
                }
                // If the eval of ttMove is greater than beta we try also if there is an other move that
                // pushes it over beta, if so also produce a cutoff
                else
                if (ttValue >= beta) {
                    ss->excludedMove = ttMove;
                    value = depthSearch<false>(pos, ss, beta-1, beta, (depth + 3) / 2, cutNode);
                    ss->excludedMove = MOVE_NONE;

                    if (value >= beta) {
                        return beta;
                    }
                }
            }
            else
            // Check extension (~2 ELO)
            if (giveCheck
             && (contains(pos.kingBlockers(~activeSide), orgSq(move))
              || pos.see(move))) {
                extension = 1;
            }
            else
            // Previous capture extension
            if (pos.captured() > PAWN
             && pos.nonPawnMaterial() <= 2 * VALUE_MG_ROOK) {
                extension = 1;
            }

            // Late irreversible move extension
            if (move == ttMove
             && pos.clockPly() > 80
             && (captureOrPromotion
              || pType(mpc) == PAWN)) {
                extension = 2;
            }

            // Add extension to new depth
            newDepth += extension;

            // Speculative prefetch as early as possible
            prefetch(TT.cluster(pos.movePosiKey(move)));

            // Update the current move
            ss->playedMove = move;
            ss->pieceStats = &thread->continuationStats[ss->inCheck][captureOrPromotion][mpc][dstSq(move)];

            // Step 15. Do the move
            pos.doMove(move, si, giveCheck);

            bool const doLMR{
                depth >= 3
             && moveCount > 1 + 2 * rootNode
             && (cutNode
              || !captureOrPromotion
              || moveCountPruning
              || ss->staticEval + PieceValues[EG][pos.captured()] <= alfa
                // If ttHit running average is small
              || thread->ttHitAvg < 427 * TTHitAverageWindow /* TTHitAverageResolution / 1024*/) };

            bool doFullSearch;
            // Step 16. Reduced depth search (LMR, ~200 ELO).
            // If the move fails high will be re-searched at full depth.
            if (doLMR) {

                auto reductDepth{ reduction(depth, moveCount, improving) };

                reductDepth +=
                    // Increase if other threads are searching this position.
                    +1 * (threadMarker.marked)
                    // Increase if move count pruning
                    +1 * (moveCountPruning && !pastPV)
                    // Decrease if the ttHit running average is large
                    -1 * (thread->ttHitAvg > 509 * TTHitAverageWindow /* TTHitAverageResolution / 1024*/)
                    // Decrease if position is or has been on the PV (~10 ELO)
                    -2 * (ss->ttPV)
                    // Decrease if move has been singularly extended (~3 ELO)
                    -1 * (singularQuietLMR)
                    // Decrease if opponent's move count is high (~5 ELO)
                    -1 * ((ss-1)->moveCount > 13);

                if (captureOrPromotion) {
                    // Increase reduction for captures/promotions at low depth and late move
                    if (depth < 8
                     && moveCount > 2) {
                        reductDepth += 1;
                    }
                    // Increase reduction for captures/promotions that don't give check if static eval is bad enough
                    if (!giveCheck
                     && ss->staticEval + PieceValues[EG][pos.captured()] + 213 * depth <= alfa) {
                        reductDepth += 1;
                    }
                }
                else {

                    // Increase reduction at root for late moves in case of consecutive fail highs
                    if (rootNode) {
                        reductDepth += thread->failHighCount*thread->failHighCount * moveCount / 512;
                    }

                    // Increase reduction if TT move is a capture (~5 ELO)
                    reductDepth += 1 * ttmCapture;

                    // Increase reduction if cut nodes (~10 ELO)
                    if (cutNode) {
                        reductDepth += 2;
                    }
                    else
                    // Decrease reduction if move escapes a capture in no-cut nodes
                    // Filter out castling moves, because they are coded as "king captures rook" and
                    // hence break make_move(). (~2 Elo)
                    if (mType(move) == SIMPLE
                     && !pos.see(reverseMove(move))) {
                        reductDepth -= 2 + ss->ttPV - (pType(mpc) == PAWN);
                    }

                    ss->stats =
                          thread->butterFlyStats[activeSide][mMask(move)]
                        + (*contStats[0])[mpc][dstSq(move)]
                        + (*contStats[1])[mpc][dstSq(move)]
                        + (*contStats[3])[mpc][dstSq(move)]
                        - 5287;

                    // Decrease/Increase reduction by comparing opponent's stat score (~10 Elo)
                    if (ss->stats >= -106 && (ss-1)->stats < -104) {
                        reductDepth--;
                    }
                    else
                    if ((ss-1)->stats >= -119 && ss->stats < -140) {
                        reductDepth++;
                    }

                    // Decrease/Increase reduction for moves with a good/bad history (~30 Elo)
                    reductDepth -= ss->stats / 14884;
                }

                Depth const d(std::clamp(newDepth - reductDepth, { 1 }, { newDepth }));
                assert(d <= newDepth);
                value = -depthSearch<false>(pos, ss+1, -(alfa+1), -alfa, d, true);

                doFullSearch = alfa < value && d < newDepth;
            }
            else {
                doFullSearch = !PVNode || moveCount > 1;
            }

            // Step 17. Full depth search when LMR is skipped or fails high.
            if (doFullSearch) {
                value = -depthSearch<false>(pos, ss+1, -(alfa+1), -alfa, newDepth, !cutNode);

                if (doLMR
                 && !captureOrPromotion) {

                    auto bonus{ alfa < value ?
                                    +statBonus(newDepth) :
                                    -statBonus(newDepth) };
                    if (move == ss->killerMoves[0]) {
                        bonus += bonus / 4;
                    }
                    updateContinuationStats(ss, mpc, dstSq(move), bonus);
                }
            }

            // Full PV search
            if (PVNode
             && (moveCount == 1 || (alfa < value && (rootNode || value < beta)))) {

                (ss+1)->pv = pv;
                (ss+1)->pv[0] = MOVE_NONE;

                value = -depthSearch<true>(pos, ss+1, -beta, -alfa, std::min(newDepth, maxDepth), false);
            }

            // Step 18. Undo the move
            pos.undoMove(move);

            assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

            // Step 19. Check for the new best move.
            // Finished searching the move. If a stop or a cutoff occurred,
            // the return value of the search cannot be trusted,
            // and return immediately without updating best move, PV and TT.
            if (Threadpool.stop.load(std::memory_order::memory_order_relaxed)) {
                return VALUE_ZERO;
            }

            if (rootNode) {

                assert(thread->rootMoves.contains(move));
                RootMove &rm{ *thread->rootMoves.find(move) };
                // First PV move or new best move?
                if (moveCount == 1
                 || value > alfa) {

                    rm.newValue = value;
                    rm.selDepth = thread->selDepth;
                    rm.resize(1);

                    assert((ss+1)->pv != nullptr);
                    for (Move *m = (ss+1)->pv; *m != MOVE_NONE; ++m) {
                        rm += *m;
                    }

                    // Record how often the best move has been changed in each iteration.
                    // This information is used for time management:
                    // When the best move changes frequently, allocate some more time.
                    if (moveCount > 1
                     && Limits.useTimeMgmt()) {
                        ++thread->pvChanges;
                    }
                }
                else {
                    // All other moves but the PV are set to the lowest value, this
                    // is not a problem when sorting because sort is stable and move
                    // position in the list is preserved, just the PV is pushed up.
                    rm.newValue = -VALUE_INFINITE;
                }
            }

            // Step 20. Check best value.
            if (bestValue < value) {
                bestValue = value;

                if (value > alfa) {
                    bestMove = move;

                    // Update pv even in fail-high case.
                    if (PVNode
                     && !rootNode) {
                        updatePV(ss->pv, move, (ss+1)->pv);
                    }

                    if (PVNode && value < beta) { // Update alpha! Always alpha < beta
                        alfa = value;
                    }
                    else {
                        assert(value >= beta); // Fail high
                        ss->stats = 0;
                        break;
                    }
                }
            }

            if (move != bestMove) {
                if (captureOrPromotion) {
                    captureMoves += move;
                }
                else {
                    quietMoves += move;
                }
            }
        }

        // The following condition would detect a stop only after move loop has been
        // completed. But in this case bestValue is valid because we have fully
        // searched our subtree, and we can anyhow save the result in TT.
        /*
        if (Threadpool.stop) {
            return VALUE_DRAW;
        }
        */

        assert(moveCount != 0
            || !ss->inCheck
            || excludedMove != MOVE_NONE
            || MoveList<LEGAL>(pos).size() == 0);

        // Step 21. Check for checkmate and stalemate.
        // If all possible moves have been searched and if there are no legal moves,
        // If in a singular extension search then return a fail low score (alfa).
        // Otherwise it must be a checkmate or a stalemate, so return value accordingly.
        if (moveCount == 0) {
            bestValue =
                excludedMove != MOVE_NONE ? alfa :
                ss->inCheck ? matedIn(ss->ply) : VALUE_DRAW;
        }
        else
        if (bestMove != MOVE_NONE) {
            auto const bonus1{ statBonus(depth + 1) };

            // Quiet best move: update move sorting heuristics.
            if (!pos.captureOrPromotion(bestMove)) {
                auto const bonus2{ bestValue > beta + VALUE_MG_PAWN ? bonus1 : statBonus(depth) };

                updateQuietRefutationStats(ss, pos, bestMove, depth, bonus2);
                // Decrease all the other played quiet moves
                for (auto qm : quietMoves) {
                    updateQuietStats(ss, pos, qm, -bonus2);
                }
            }
            else {
                thread->captureStats[pos.movedPiece(bestMove)][dstSq(bestMove)][pos.captured(bestMove)] << bonus1;
            }

            // Decrease all the other played capture moves
            for (auto cm : captureMoves) {
                thread->captureStats[pos.movedPiece(cm)][dstSq(cm)][pos.captured(cm)] << -bonus1;
            }

            // Extra penalty for a quiet early move that was not a TT move or main killer move in previous ply when it gets refuted
            if (!pmCapOrPro
             && (((ss-1)->moveCount == 1 + (ss-1)->ttHit)
              || ((ss-1)->playedMove == (ss-1)->killerMoves[0]))) {
                updateContinuationStats(ss-1, pos.prevMovedPiece((ss-1)->playedMove), dstSq((ss-1)->playedMove), -bonus1);
            }
        }
        else {
            // Bonus for prior quiet move that caused the fail low.
            if (!pmCapOrPro
             && (PVNode
              || depth >= 3)) {
                updateContinuationStats(ss-1, pos.prevMovedPiece((ss-1)->playedMove), dstSq((ss-1)->playedMove), statBonus(depth));
            }
        }

        if (PVNode) {
            bestValue = std::min(bestValue, maxValue);
        }

        if (depth > 3) {
            // If no good move is found and the previous position was ttPv, then the previous
            // opponent move is probably good and the new position is added to the search tree.
            if (bestValue <= alfa) {
                ss->ttPV = ss->ttPV || (ss-1)->ttPV;
            }
            // Otherwise, a counter move has been found and if the position is the last leaf
            // in the search tree, remove the position from the search tree.
            else {
                ss->ttPV = ss->ttPV && (ss+1)->ttPV;
            }
        }

        if (excludedMove == MOVE_NONE &&
            !(rootNode && thread->pvCur != 0)) {

            tte->save(posiKey,
                      bestMove,
                      valueToTT(bestValue, ss->ply),
                      ss->staticEval,
                      depth,
                      bestValue >= beta ? BOUND_LOWER :
                      PVNode && bestMove != MOVE_NONE ? BOUND_EXACT : BOUND_UPPER,
                      ss->ttPV);
        }

        assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);
        return bestValue;
    }
    // Explicit template instantiations
    template Value depthSearch<false>(Position&, Stack* const, Value, Value, Depth, bool);
    template Value depthSearch<true >(Position&, Stack* const, Value, Value, Depth, bool);
}

namespace Searcher {

    void initialize() noexcept {

        double const r{ 22.0 + 2 * std::log(Threadpool.size()) };
        Reduction[0] = 0;
        for (int16_t i = 1; i < MAX_MOVES; ++i) {
            Reduction[i] = int32_t(r * std::log(i + 0.25 * std::log(i)));
        }
    }
}

/// Thread::search() is thread iterative deepening loop function.
/// It calls depthSearch() repeatedly with increasing depth until
/// - Force stop requested.
/// - Allocated thinking time has been consumed.
/// - Maximum search depth is reached.
void Thread::search() {
    ttHitAvg = TTHitAverageWindow * TTHitAverageResolution / 2;

    int32_t const contemptTime { Options["Contempt Time"] };
    int32_t const contemptValue{ Options["Contempt Value"] };

    int16_t timedContempt{ 0 };
    if (contemptTime != 0
     && Limits.useTimeMgmt()) {
        int64_t const diffTime{
            (int64_t(Limits.clock[ rootPos.activeSide()].time)
           - int64_t(Limits.clock[~rootPos.activeSide()].time)) / 1000 };
        timedContempt = int16_t(diffTime / contemptTime);
    }
    // Basic Contempt
    int32_t bc{ toValue(int16_t(Options["Fixed Contempt"]) + timedContempt) };
    // In analysis mode, adjust contempt in accordance with user preference
    if (Limits.infinite
     || Options["UCI_AnalyseMode"]) {
        bc = Options["Analysis Contempt"] == "Off"                                    ? 0 :
             Options["Analysis Contempt"] == "White" && rootPos.activeSide() == BLACK ? -bc :
             Options["Analysis Contempt"] == "Black" && rootPos.activeSide() == WHITE ? -bc :
             /*Options["Analysis Contempt"] == "Both"                         ? +bc :*/ +bc;
    }
    contempt = rootPos.activeSide() == WHITE ?
                +makeScore(bc, bc / 2) :
                -makeScore(bc, bc / 2);

    std::copy(&lowPlyStats[2][0], &lowPlyStats.back().back() + 1, &lowPlyStats[0][0]);
    std::fill(&lowPlyStats[MAX_LOWPLY - 2][0], &lowPlyStats.back().back() + 1, 0);

    auto *mainThread{ this == Threadpool.mainThread() ?
                        static_cast<MainThread*>(this) : nullptr };

    double  timeReduction{ 1.0 };
    int16_t standCount{ 0 };

    auto bestValue{ -VALUE_INFINITE };
    auto window{ VALUE_ZERO };
    auto alfa{ -VALUE_INFINITE },
         beta{ +VALUE_INFINITE };

    // To allow access to (ss-7) up to (ss+2), the stack must be over-sized.
    // The former is needed to allow updateContinuationStats(ss-1, ...),
    // which accesses its argument at ss-6, also near the root.
    // The latter is needed for stats and killer initialization at ss+2.
    Stack stack[MAX_PLY + 10], *ss = stack+7;
    std::memset(stack, 0, 10 * sizeof (*stack));
    for (int16_t i = 7; i > 0; --i) {
        (ss-i)->ply = -i;
        (ss-i)->pieceStats = &this->continuationStats[0][0][NO_PIECE][0]; // Use as a sentinel
    }

    Move pv[MAX_PLY+1];
    ss->pv = pv;

    // Iterative deepening loop until requested to stop or the target depth is reached.
    while (++rootDepth < MAX_PLY
        && !Threadpool.stop
        && (!mainThread
         || Limits.depth == DEPTH_ZERO
         || rootDepth <= Limits.depth)) {

        if (mainThread) {
            // Age out PV variability metric
            Threadpool.pvChangesSum /= 2;
        }

        // Save the last iteration's values before first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        rootMoves.saveValues();

        uint16_t pvBeg = 0;
        pvEnd = 0;

        if (Threadpool.stand) {
            ++standCount;
        }

        // MultiPV loop. Perform a full root search for each PV line.
        for (pvCur = 0; pvCur < Threadpool.pvCount && !Threadpool.stop; ++pvCur) {

            if (pvCur == pvEnd) {
                pvBeg = pvEnd;
                while (++pvEnd < rootMoves.size()) {
                    if (rootMoves[pvEnd].tbRank != rootMoves[pvBeg].tbRank) {
                        break;
                    }
                }
            }

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = DEPTH_ZERO;

            // Reset aspiration window starting size.
            if (rootDepth >= 4) {
                window = Value(17);
                auto const oldValue{ rootMoves[pvCur].oldValue };
                alfa = std::max(oldValue - window, -VALUE_INFINITE);
                beta = std::min(oldValue + window, +VALUE_INFINITE);

                // Dynamic contempt: Adjust contempt based on root move's oldValue
                auto dc{ bc };
                if (contemptValue != 0) {
                    dc += ((105 - bc / 2) * oldValue * 100) / ((std::abs(oldValue) + 149) * contemptValue);
                }
                contempt = rootPos.activeSide() == WHITE ?
                            +makeScore(dc, dc / 2) :
                            -makeScore(dc, dc / 2);
            }

            failHighCount = 0;

            // Start with a small aspiration window and, in case of fail high/low,
            // research with bigger window until not failing high/low anymore.
            do {
                Depth const adjustedDepth( std::max(rootDepth - failHighCount - standCount, 1) );
                bestValue = depthSearch<true>(rootPos, ss, alfa, beta, adjustedDepth, false);

                // Bring the best move to the front. It is critical that sorting is
                // done with a stable algorithm because all the values but the first
                // and eventually the new best one are set to -VALUE_INFINITE and
                // want to keep the same order for all the moves but the new PV
                // that goes to the front. Note that in case of MultiPV search
                // the already searched PV lines are preserved.
                rootMoves.stableSort(pvCur, pvEnd);

                // If search has been stopped, break immediately.
                // Sorting is safe because RootMoves is still valid, although it refers to the previous iteration.
                if (Threadpool.stop) {
                    break;
                }

                // Give some update before to re-search.
                if (mainThread
                 && Threadpool.pvCount == 1
                 && (bestValue <= alfa
                  || beta <= bestValue)
                 && TimeMgr.elapsed() > 3000) {
                    sync_cout << multipvInfo(mainThread, rootDepth, alfa, beta) << sync_endl;
                }

                // If fail low set new bounds
                if (bestValue <= alfa) {
                    beta = (alfa + beta) / 2;
                    alfa = std::max(bestValue - window, -VALUE_INFINITE);

                    failHighCount = 0;
                    if (mainThread) {
                        Threadpool.stopPonderhit = false;
                    }
                }
                else
                // If fail high set new bounds
                if (bestValue >= beta) {
                    // NOTE:: Don't change alfa = (alfa + beta) / 2
                    beta = std::min(bestValue + window, +VALUE_INFINITE);

                    ++failHighCount;
                }
                // Otherwise exit the loop
                else {
                    break;
                }

                window += window / 4 + 5;

                assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
            } while (true);

            // Sort the PV lines searched so far and update the GUI.
            rootMoves.stableSort(pvBeg, pvCur + 1);

            if (mainThread
             && (Threadpool.stop
              || Threadpool.pvCount == pvCur + 1
              || TimeMgr.elapsed() > 3000)) {
                sync_cout << multipvInfo(mainThread, rootDepth, alfa, beta) << sync_endl;
            }
        }

        if (!Threadpool.stop) {
            finishedDepth = rootDepth;
        }

        // Has any of the threads found a "mate in <x>"?
        if ( Limits.mate != 0
         && !Limits.useTimeMgmt()
         && bestValue >= +VALUE_MATE_1_MAX_PLY
         && bestValue >= +VALUE_MATE - 2 * Limits.mate) {
            Threadpool.stop = true;
        }

        if (mainThread) {

            if (Threadpool.bestMove != rootMoves[0][0]) {
                Threadpool.bestMove  = rootMoves[0][0];
                Threadpool.bestDepth = rootDepth;
            }

            // Reduce time if the bestMove is stable over 10 iterations
            // Time Reduction
            timeReduction = 0.95 + 0.97 * ((finishedDepth - Threadpool.bestDepth) > 9);

            if ( Limits.useTimeMgmt()
             && !Threadpool.stop
             && !Threadpool.stopPonderhit) {

                // Reduction Ratio - Use part of the gained time from a previous stable move for the current move
                auto const reductionRatio{ (1.47 + Threadpool.timeReduction) / (2.32 * timeReduction) };
                // Eval Falling factor
                auto const fallingEval{
                    std::clamp((318
                              + 6 * (Threadpool.bestValue - bestValue)
                              + 6 * (Threadpool.iterValues[Threadpool.iterIdx] - bestValue)) / 825.0, 0.50, 1.50) };

                Threadpool.pvChangesSum += Threadpool.accumulate(&Thread::pvChanges);
                // Set pvChanges to 0
                Threadpool.set(&Thread::pvChanges, { 0 });
                auto const pvInstability{ 1.00 + 2 * Threadpool.pvChangesSum / Threadpool.size() };

                auto const totalTime{
                    rootMoves.size() != 1 ?
                        TimeMgr.optimum() * reductionRatio * fallingEval * pvInstability :
                        std::min(TimeMgr.optimum() * 0.001, 4.0) };

                auto const elapsed{ double(TimeMgr.elapsed()) };

                if (elapsed > totalTime * 0.58) {

                    if (!Threadpool.ponder) {
                        Threadpool.stand = true;
                    }

                    // Stop the search if we have exceeded the totalTime (at least 1ms).
                    if (elapsed > totalTime) {

                        // If allowed to ponder do not stop the search now but
                        // keep pondering until GUI sends "stop"/"ponderhit".
                        if (!Threadpool.ponder) {
                            Threadpool.stop = true;
                        }
                        else {
                            Threadpool.stopPonderhit = true;
                        }
                    }
                }

                Threadpool.iterValues[Threadpool.iterIdx] = bestValue;
                Threadpool.iterIdx = (Threadpool.iterIdx + 1) % Threadpool.iterValues.size();
            }

            // If skill level is enabled and can pick move, pick a sub-optimal best move.
            if (SkillMgr.enabled()
             && SkillMgr.canPick(rootDepth)) {
                SkillMgr.pickBestMove();
            }
        }
    }

    if (mainThread) {
        Threadpool.timeReduction = timeReduction;
    }
}

/// MainThread::search() is main thread search function.
/// It searches from root position and outputs the "bestmove"/"ponder".
void MainThread::search() {
    assert(Threadpool.mainThread() == this);

    if (Limits.useTimeMgmt()) {
        // Initialize the time manager before searching.
        TimeMgr.setup(rootPos.activeSide(), rootPos.plyCount());
    }

    TEntry::updateGeneration();

    Evaluator::NNUE::verify();

    bool think{ true };

    if (rootMoves.empty()) {
        think = false;

        rootMoves += MOVE_NONE;

        sync_cout << "info"
                  << " depth " << 0
                  << " score " << toString(rootPos.checkers() != 0 ? -VALUE_MATE : VALUE_DRAW)
                  << " time "  << 0 << sync_endl;
    }
    else {

        if ( Options["Use Book"]
         &&  Book.enabled
         && !Limits.infinite
         &&  Limits.mate == 0) {

            auto bbm{ Book.probe(rootPos, Options["Book Move Num"], Options["Book Pick Best"]) };
            if (bbm != MOVE_NONE
             && rootMoves.contains(bbm)) {
                think = false;

                rootMoves.bringToFront(bbm);
                rootMoves[0].newValue = VALUE_NONE;
                StateInfo si;
                rootPos.doMove(bbm, si);
                auto bpm{ Book.probe(rootPos, Options["Book Move Num"], Options["Book Pick Best"]) };
                if (bpm != MOVE_NONE) {
                    rootMoves[0] += bpm;
                }
                rootPos.undoMove(bbm);
            }
        }

        if (think) {

            Threadpool.pvChangesSum = 0.0;
            Threadpool.bestMove  = MOVE_NONE;
            Threadpool.bestDepth = DEPTH_ZERO;
            if (Limits.useTimeMgmt()) {
                Threadpool.iterValues.fill(Threadpool.bestValue != +VALUE_INFINITE ? Threadpool.bestValue : VALUE_ZERO);
                Threadpool.iterIdx = 0;
            }

            PRNG prng(now());
            double const dbllevel{
                Options["UCI_LimitStrength"] ?
                    std::clamp(std::pow((double(Options["UCI_Elo"]) - 1346.6) / 143.4, 1 / 0.806), 0.0, double(MaxLevel)) :
                    double(Options["Skill Level"]) };
            uint16_t const intLevel = uint16_t(dbllevel) + ((dbllevel - uint16_t(dbllevel)) * 1024 > prng.rand<uint32_t>() % 1024 ? 1 : 0);
            SkillMgr.setLevel(intLevel);

            // Have to play with skill handicap?
            // In this case enable MultiPV search by skill pv size
            // that will use behind the scenes to get a set of possible moves.
            Threadpool.pvCount = std::clamp(uint16_t(Options["MultiPV"]),
                                            uint16_t(1 + 3 * SkillMgr.enabled()),
                                            uint16_t(rootMoves.size()));
            assert(Threadpool.pvCount != 0);

            Threadpool.wakeUpAll(); // start non-main threads searching !
            Thread::search();           // start main thread searching !

            // Swap best PV line with the sub-optimal one if skill level is enabled
            if (SkillMgr.enabled()) {
                rootMoves.bringToFront(SkillMgr.bestMove != MOVE_NONE ? SkillMgr.bestMove : SkillMgr.pickBestMove());
            }
        }
    }

    // When reach the maximum depth, can arrive here without a raise of Threads.stop.
    // However, if in an infinite search or pondering, shouldn't print the best move
    // before receiving a "stop"/"ponderhit" command. Therefore simply wait here until
    // receives one of those commands (which also raises Threads.stop).
    // Busy wait for a "stop"/"ponderhit" command.
    while (!Threadpool.stop
        && (Threadpool.ponder
         || Limits.infinite)) {
    } // Busy wait for a stop or a ponder reset

    Thread *bestThread{ this };
    if (think) {
        // Stop the threads if not already stopped (Also raise the stop if "ponderhit" just reset Threads.ponder)
        Threadpool.stop = true;
        // Wait until non-main threads have finished
        Threadpool.waitIdleAll();

        // Check if there is better thread than main thread
        if (Threadpool.pvCount == 1
         && Threadpool.size() >= 2
         //&& Limits.depth == DEPTH_ZERO // Depth limit search don't use deeper thread
         && !SkillMgr.enabled()
         && !Options["UCI_LimitStrength"]) {

            bestThread = Threadpool.bestThread();
            // If new best thread then send PV info again
            if (bestThread != this) {
                sync_cout << multipvInfo(bestThread, bestThread->finishedDepth, -VALUE_INFINITE, +VALUE_INFINITE) << sync_endl;
            }
        }
    }

    assert(!bestThread->rootMoves.empty()
        && !bestThread->rootMoves[0].empty());

    auto &rm{ bestThread->rootMoves[0] };

    if (Limits.useTimeMgmt()) {
        if (TimeMgr.timeNodes != 0) {
            // In 'Nodes as Time' mode, subtract the searched nodes from the total nodes.
            TimeMgr.remainingNodes[rootPos.activeSide()] +=
                Limits.clock[rootPos.activeSide()].inc - Threadpool.accumulate(&Thread::nodes);
        }
        Threadpool.bestValue = rm.newValue;
    }

    auto bm{ rm[0] };
    auto pm{ MOVE_NONE };
    if (bm != MOVE_NONE) {
        auto const itr{ rm.begin() + 1 };
        pm = itr != rm.end() ? *itr : TT.extractNextMove(rootPos, bm);
        assert(bm != pm);
    }

    // Best move could be MOVE_NONE when searching on a stalemate position.
    sync_cout << "bestmove " << bm;
    if (pm != MOVE_NONE) {
        std::cout << " ponder " << pm;
    }
    std::cout << sync_endl;
}

/// MainThread::tick() is used as timer function.
/// Used to detect when out of available limit and thus stop the search, also print debug info.
void MainThread::tick() {
    static TimePoint reportTime{ now() };

    if (--tickCount > 0) {
        return;
    }
    // When using nodes, ensure checking rate is in range [1, 1024]
    tickCount = int16_t(Limits.nodes != 0 ? std::clamp(int32_t(Limits.nodes / 1024), 1, 1024) : 1024);

    TimePoint elapsed{ TimeMgr.elapsed() };
    TimePoint time{ TimeMgr.startTime + elapsed };

    if (reportTime + 1000 <= time) {
        reportTime = time;

        Reporter::print();
    }

    // Do not stop until told so by the GUI.
    if (Threadpool.ponder) {
        return;
    }

    if ((Limits.useTimeMgmt()
      && (Threadpool.stopPonderhit
       || TimeMgr.maximum() < elapsed + 10))
     || (Limits.moveTime != 0
      && Limits.moveTime <= elapsed)
     || (Limits.nodes != 0
      && Limits.nodes <= Threadpool.accumulate(&Thread::nodes))) {
        Threadpool.stop = true;
    }
}

namespace SyzygyTB {

    void rankRootMoves(Position &pos, RootMoves &rootMoves) noexcept {

        DepthLimit = Options["SyzygyDepthLimit"];
        PieceLimit = Options["SyzygyPieceLimit"];
        Move50Rule = Options["SyzygyMove50Rule"];
        HasRoot    = false;

        bool dtzAvailable{ true };

        // Tables with fewer pieces than SyzygyProbeLimit are searched with DepthLimit == DEPTH_ZERO
        if (PieceLimit > MaxPieceLimit) {
            PieceLimit = MaxPieceLimit;
            DepthLimit = DEPTH_ZERO;
        }

        // Rank moves using DTZ tables
        if (PieceLimit >= pos.count()
         && pos.castleRights() == CR_NONE) {
            // If the current root position is in the table-bases,
            // then RootMoves contains only moves that preserve the draw or the win.
            HasRoot = rootProbeDTZ(pos, rootMoves);
            if (!HasRoot) {
                // DTZ tables are missing; try to rank moves using WDL tables
                dtzAvailable = false;
                HasRoot = rootProbeWDL(pos, rootMoves);
            }
        }

        if (HasRoot) {
            // Sort moves according to TB rank
            rootMoves.stableSort([](RootMove const &rm1, RootMove const &rm2) {
                                    return rm1.tbRank > rm2.tbRank;
                                });
            // Probe during search only if DTZ is not available and winning
            if (dtzAvailable
             || rootMoves[0].tbValue <= VALUE_DRAW) {
                PieceLimit = 0;
            }
        }
        else {
            // Clean up if rootProbeDTZ() and rootProbeWDL() have failed
            for (auto &rm : rootMoves) {
                rm.tbRank = 0;
            }
        }
    }

}
