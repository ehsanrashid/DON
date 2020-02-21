#include "Searcher.h"

#include <cmath>
#include <ctime>
#include <cstdlib>

#include "Debugger.h"
#include "Evaluator.h"
#include "Helper.h"
#include "Logger.h"
#include "MoveGenerator.h"
#include "MovePicker.h"
#include "Notation.h"
#include "Polyglot.h"
#include "Position.h"
#include "SyzygyTB.h"
#include "Thread.h"
#include "TimeManager.h"
#include "Transposition.h"
#include "SkillManager.h"
#include "UCI.h"

using namespace std;

Limit Limits;

u32  PVCount;

Depth TBProbeDepth  = 1;
i32   TBLimitPiece  = 6;
bool  TBUseRule50   = true;
bool  TBHasRoot     = false;


using namespace Searcher;

namespace {

    /// Breadcrumbs are used to pair thread and position key
    struct Breadcrumb {
        std::atomic<const Thread*> thread;
        std::atomic<Key>           posiKey;

        void store(const Thread *th, Key key) {
            thread.store(th, std::memory_order::memory_order_relaxed);
            posiKey.store(key, std::memory_order::memory_order_relaxed);
        }
    };

    constexpr size_t BreadcrumbSize{ 1024 };
    Array<Breadcrumb, BreadcrumbSize> Breadcrumbs;

    /// ThreadMarker structure keeps track of which thread left breadcrumbs at the given
    /// node for potential reductions. A free node will be marked upon entering the moves
    /// loop by the constructor, and unmarked upon leaving that loop by the destructor.
    class ThreadMarker {
    private:
        Breadcrumb *breadcrumb;

    public:

        bool marked;

        ThreadMarker() = delete;
        ThreadMarker(const Thread *thread, Key posiKey, i16 ply)
            : breadcrumb(nullptr)
            , marked(false) {
            auto *bc{ 8 > ply ?
                        &Breadcrumbs[posiKey & (BreadcrumbSize - 1)] :
                        nullptr };
            if (nullptr != bc) {
                // Check if another already marked it, if not, mark it
                auto *th{ bc->thread.load(std::memory_order::memory_order_relaxed) };
                if (nullptr == th) {
                    bc->store(thread, posiKey);
                    breadcrumb = bc;
                }
                else
                if (th != thread
                 && bc->posiKey.load(std::memory_order::memory_order_relaxed) == posiKey) {
                        marked = true;
                }
            }
        }

        ~ThreadMarker() {
            if (nullptr != breadcrumb) { // Free the marked location
                breadcrumb->store(nullptr, 0);
            }
        }
    };

    constexpr u64 TTHitAverageWindow = 4096;
    constexpr u64 TTHitAverageResolution = 1024;

    // Razor margin
    constexpr Value RazorMargin = Value(531);
    // Futility margin
    constexpr Value futilityMargin(Depth d, bool imp) {
        return Value(217 * (d - imp));
    }
    // Futility move count threshold
    constexpr i16 futilityMoveCount(Depth d, bool imp) {
        return (4 + d * d) / (2 - imp);
    }

    Depth reduction(Depth d, u08 mc, bool imp) {
        assert(0 <= d);
        auto r{ 0 != d
             && 0 != mc ?
                Threadpool.reductionFactor * std::log(d) * std::log(mc) : 0 };
        return Depth((r + 511) / 1024
                   + (!imp && (r > 1007)));
    }

    // Add a small random component to draw evaluations to keep search dynamic and to avoid 3-fold-blindness.
    Value drawValue() {
        return VALUE_DRAW + rand() % 3 - 1;
    }

    /// statBonus() is the bonus, based on depth
    constexpr i32 statBonus(Depth depth) {
        return 15 >= depth ? (19 * depth + 155) * depth - 132 : -8;
    }

    constexpr Square prevDst(Color c, Move pm) {
        return CASTLE != mType(pm) ?
                dstSq(pm) :
                kingRelativeSq(~c, dstSq(pm) > orgSq(pm));
    }

    /// updateContinuationStats() updates Stats of the move pairs formed
    /// by moves at ply -1, -2, -4 and -6 with current move.
    void updateContinuationStats(Stack *const &ss, Piece p, Square dst, i32 bonus) {
        assert(NO_PIECE != p);
        for (auto i : { 1, 2, 4, 6 }) {
            if (isOk((ss-i)->playedMove)) {
                (*(ss-i)->pieceStats)[p][dst] << bonus;
            }
        }
    }

    /// updateQuietStats() updates move sorting heuristics when a new quiet best move is found
    void updateQuietStats(Stack *const &ss, const Position &pos, Move move, i32 bonus) {
        if (ss->killerMoves[0] != move) {
            ss->killerMoves[1] = ss->killerMoves[0];
            ss->killerMoves[0] = move;
        }
        assert(1 == std::count(ss->killerMoves.begin(), ss->killerMoves.end(), move));

        if (isOk((ss-1)->playedMove)) {
            auto pmDst{ prevDst(pos.active, (ss-1)->playedMove) };
            assert(NO_PIECE != pos[pmDst]);
            pos.thread->quietCounterMoves[pos[pmDst]][pmDst] = move;
        }

        pos.thread->quietStats[pos.active][mIndex(move)] << bonus;
        if (PAWN != pType(pos[orgSq(move)])) {
            pos.thread->quietStats[pos.active][mIndex(reverseMove(move))] << -bonus;
        }

        updateContinuationStats(ss, pos[orgSq(move)], dstSq(move), bonus);
    }

    /// updatePV() appends the move and child pv
    void updatePV(list<Move> &pv, Move move, const list<Move> &childPV) {
        pv.assign(childPV.begin(), childPV.end());
        pv.push_front(move);
        assert(pv.front() == move
            && ((pv.size() == 1 && childPV.empty())
             || (pv.back() == childPV.back() && !childPV.empty())));
    }

    /// quienSearch() is quiescence search function, which is called by the main depth limited search function when the remaining depth <= 0.
    template<bool PVNode>
    Value quienSearch(Position &pos, Stack *const &ss, Value alfa, Value beta, Depth depth = DEPTH_ZERO) {
        assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
        assert(PVNode || (alfa == beta-1));
        assert(DEPTH_ZERO >= depth);

        Value actualAlfa;

        if (PVNode) {
            actualAlfa = alfa; // To flag BOUND_EXACT when eval above alpha and no available moves
            ss->pv.clear();
        }

        bool inCheck{ 0 != pos.checkers() };

        // Check for maximum ply reached or immediate draw.
        if (pos.draw(ss->ply)
         || ss->ply >= MaxDepth) {
            return ss->ply >= MaxDepth
                && !inCheck ?
                        Evaluator::evaluate(pos) : VALUE_DRAW;
        }

        assert(ss->ply >= 1
            && ss->ply == (ss-1)->ply + 1
            && ss->ply < MaxDepth);

        // Transposition table lookup.
        Key key{ pos.posiKey() };
        bool ttHit;
        auto *tte   { TT.probe(key, ttHit) };
        auto ttMove { ttHit ?
                          tte->move() : MOVE_NONE };
        auto ttValue{ ttHit ?
                          valueOfTT(tte->value(), ss->ply, pos.clockPly()) : VALUE_NONE };
        auto ttPV   { ttHit && tte->pv() };

        // Decide whether or not to include checks.
        // Fixes also the type of TT entry depth that are going to use.
        // Note that in quienSearch use only 2 types of depth: DEPTH_QS_CHECK or DEPTH_QS_NO_CHECK.
        Depth qsDepth{ inCheck
                    || DEPTH_QS_CHECK <= depth ?
                        DEPTH_QS_CHECK :
                        DEPTH_QS_NO_CHECK };

        if (!PVNode
         && VALUE_NONE != ttValue // Handle ttHit
         && qsDepth <= tte->depth()
         && BOUND_NONE != (tte->bound()
                         & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER))) {
            return ttValue;
        }

        Value bestValue
            , futilityBase;

        // Evaluate the position statically.
        if (inCheck) {
            ss->staticEval = VALUE_NONE;
            // Starting from the worst case which is checkmate
            bestValue = futilityBase = -VALUE_INFINITE;
        }
        else {
            if (ttHit) {
                // Never assume anything on values stored in TT.
                if (VALUE_NONE == (ss->staticEval = bestValue = tte->eval())) {
                    ss->staticEval = bestValue = Evaluator::evaluate(pos);
                }

                // Can ttValue be used as a better position evaluation?
                if (VALUE_NONE != ttValue
                 && BOUND_NONE != (tte->bound()
                                 & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER))) {
                    bestValue = ttValue;
                }
            }
            else {
                ss->staticEval = bestValue =
                    MOVE_NULL != (ss-1)->playedMove ?
                        Evaluator::evaluate(pos) :
                        -(ss-1)->staticEval + 2 * VALUE_TEMPO;
            }

            if (alfa < bestValue) {
                // Stand pat. Return immediately if static value is at least beta
                if (bestValue >= beta) {
                    if (!ttHit) {
                        tte->save(key,
                                  MOVE_NONE,
                                  valueToTT(bestValue, ss->ply),
                                  ss->staticEval,
                                  DEPTH_NONE,
                                  BOUND_LOWER,
                                  ttPV);
                    }

                    assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);
                    return bestValue;
                }

                assert(bestValue < beta);
                // Update alfa! Always alfa < beta
                if (PVNode) {
                    alfa = bestValue;
                }
            }

            futilityBase = bestValue + 154;
        }

        Move move;

        auto *thread{ pos.thread };

        auto bestMove{ MOVE_NONE };
        StateInfo si;

        u08 moveCount{ 0 };

        const PieceSquareStatsTable* pieceStats[]
        {
            (ss-1)->pieceStats, (ss-2)->pieceStats,
            nullptr           , (ss-4)->pieceStats,
            nullptr           , (ss-6)->pieceStats
        };

        auto recapSq{ isOk((ss-1)->playedMove) ?
                        dstSq((ss-1)->playedMove) :
                        SQ_NONE };

        // Initialize move-picker(2) for the current position
        MovePicker mp{ pos
                     , &thread->quietStats
                     , &thread->captureStats
                     , pieceStats
                     , ttMove, depth, recapSq };
        // Loop through all the pseudo-legal moves until no moves remain or a beta cutoff occurs
        while (MOVE_NONE != (move = mp.nextMove())) {
            assert(isOk(move)
                && pos.pseudoLegal(move));

            ++moveCount;

            auto org{ orgSq(move) };
            auto dst{ dstSq(move) };
            auto mpc{ pos[org] };
            bool giveCheck{ pos.giveCheck(move) };
            bool captureOrPromotion{ pos.captureOrPromotion(move) };

            // Futility pruning
            if (!inCheck
             && !giveCheck
             && -VALUE_KNOWN_WIN < futilityBase
             && !pos.pawnAdvanceAt(pos.active, org)
             && 0 == Limits.mate) {
                assert(ENPASSANT != mType(move)); // Due to !pos.pawnAdvanceAt
                // Futility pruning parent node
                auto futilityValue{ futilityBase + PieceValues[EG][CASTLE != mType(move) ? pType(pos[dst]) : NONE] };
                if (futilityValue <= alfa) {
                    bestValue = std::max(futilityValue, bestValue);
                    continue;
                }
                // Prune moves with negative or zero SEE
                if (futilityBase <= alfa
                 && !pos.see(move, Value(1))) {
                    bestValue = std::max(futilityBase, bestValue);
                    continue;
                }
            }

            // Pruning: Don't search moves with negative SEE
            if ((!inCheck
                 // Evasion Prunable: Detect non-capture evasions that are candidates to be pruned
              || ((DEPTH_ZERO != depth
                || 2 < moveCount)
               && -VALUE_MATE_2_MAX_PLY < bestValue
               && !pos.capture(move)))
             && !pos.see(move)
             && 0 == Limits.mate) {
                continue;
            }

            // Check for legality just before making the move
            if (!pos.legal(move)) {
                --moveCount;
                continue;
            }

            // Speculative prefetch as early as possible
            prefetch(TT.cluster(pos.movePosiKey(move))->entries);

            // Update the current move
            ss->playedMove = move;
            ss->pieceStats = &thread->continuationStats[inCheck][captureOrPromotion][mpc][dst];
            // Do the move
            pos.doMove(move, si, giveCheck);
            auto value{ -quienSearch<PVNode>(pos, ss+1, -beta, -alfa, depth - 1) };
            // Undo the move
            pos.undoMove(move);

            assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

            // Check for new best move.
            if (bestValue < value) {
                bestValue = value;

                if (alfa < value) {
                    bestMove = move;

                    if (PVNode) { // Update pv even in fail-high case
                        updatePV(ss->pv, move, (ss+1)->pv);
                    }

                    if (value >= beta) { // Fail high
                        break;
                    }
                    else
                    if (PVNode) { // Update alfa! Always alfa < beta
                        alfa = value;
                    }
                }
            }
        }

        // All legal moves have been searched. A special case: If we're in check
        // and no legal moves were found, it is checkmate.
        if (inCheck
         && -VALUE_INFINITE == bestValue) {
            return matedIn(ss->ply); // Plies to mate from the root
        }

        tte->save(key,
                  bestMove,
                  valueToTT(bestValue, ss->ply),
                  ss->staticEval,
                  qsDepth,
                  bestValue >= beta ?
                      BOUND_LOWER :
                      PVNode
                   && bestValue > actualAlfa ?
                          BOUND_EXACT :
                          BOUND_UPPER,
                  ttPV);

        assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);
        return bestValue;
    }
    /// depthSearch() is main depth limited search function, which is called when the remaining depth > 0.
    template<bool PVNode>
    Value depthSearch(Position &pos, Stack *const &ss, Value alfa, Value beta, Depth depth, bool cutNode) {
        bool rootNode = PVNode
                     && 0 == ss->ply;

        // Check if there exists a move which draws by repetition,
        // or an alternative earlier move to this position.
        if (!rootNode
         && alfa < VALUE_DRAW
         && pos.clockPly() >= 3
         && pos.cycled(ss->ply)) {
            alfa = drawValue();
            if (alfa >= beta) {
                return alfa;
            }
        }

        // Dive into quiescence search when the depth reaches zero
        if (DEPTH_ZERO >= depth) {
            return quienSearch<PVNode>(pos, ss, alfa, beta);
        }

        assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
        assert(PVNode || (alfa == beta-1));
        assert(!(PVNode && cutNode));
        assert(DEPTH_ZERO < depth && depth < MaxDepth);

        // Step 1. Initialize node
        ss->moveCount = 0;

        auto *thread{ pos.thread };

        // Check for the available remaining limit
        if (Threadpool.mainThread() == thread) {
            Threadpool.mainThread()->doTick();
        }

        if (PVNode) {
            // Used to send selDepth info to GUI (selDepth from 1, ply from 0)
            if (thread->selDepth <= ss->ply) {
                thread->selDepth = ss->ply + 1;
            }
        }

        bool inCheck{ 0 != pos.checkers() };

        if (!rootNode)
        {
            // Step 2. Check for aborted search, maximum ply reached or immediate draw.
            if (Threadpool.stop.load(std::memory_order::memory_order_relaxed)
             || pos.draw(ss->ply)
             || ss->ply >= MaxDepth) {
                return ss->ply >= MaxDepth
                    && !inCheck ?
                            Evaluator::evaluate(pos) : drawValue();
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
        }

        Value value;
        auto bestValue{ -VALUE_INFINITE };
        auto maxValue{ +VALUE_INFINITE };

        auto bestMove{ MOVE_NONE };

        assert(ss->ply >= 0
            && ss->ply == (ss-1)->ply + 1
            && ss->ply < MaxDepth);

        assert(MOVE_NONE == (ss+1)->excludedMove);
        (ss+2)->killerMoves.fill(MOVE_NONE);

        // Initialize stats to zero for the grandchildren of the current position.
        // So stats is shared between all grandchildren and only the first grandchild starts with stats = 0.
        // Later grandchildren start with the last calculated stats of the previous grandchild.
        // This influences the reduction rules in LMR which are based on the stats of parent position.
        (ss+2 + 2 * rootNode)->stats = 0;

        // Step 4. Transposition table lookup.
        // Don't want the score of a partial search to overwrite a previous full search
        // TT value, so use a different position key in case of an excluded move.
        Key key{ pos.posiKey()
               ^ (Key(ss->excludedMove) << 0x10) };
        bool ttHit;
        auto *tte   { TT.probe(key, ttHit) };
        auto ttMove { rootNode ?
                          thread->rootMoves[thread->pvCur].front() :
                          ttHit ?
                              tte->move() : MOVE_NONE };
        auto ttValue{ ttHit ?
                          valueOfTT(tte->value(), ss->ply, pos.clockPly()) : VALUE_NONE };
        auto ttPV   { PVNode
                   || (ttHit && tte->pv()) };

        // ttHitAvg can be used to approximate the running average of ttHit
        thread->ttHitAvg = (TTHitAverageWindow - 1) * thread->ttHitAvg / TTHitAverageWindow
                         + TTHitAverageResolution * ttHit;

        bool pmCaptureOrPromotion{ isOk((ss-1)->playedMove)
                                && (NONE != pos.captured()
                                 || PROMOTE == mType((ss-1)->playedMove)) };

        // At non-PV nodes we check for an early TT cutoff
        if (!PVNode
         && VALUE_NONE != ttValue // Handle ttHit
         && depth <= tte->depth()
         && BOUND_NONE != (tte->bound()
                         & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER))) {
            // Update move sorting heuristics on ttMove
            if (MOVE_NONE != ttMove
             && pos.pseudoLegal(ttMove)
             && pos.legal(ttMove)) {
                if (ttValue >= beta) {
                    // Bonus for a quiet ttMove that fails high
                    if (!pos.captureOrPromotion(ttMove)) {
                        updateQuietStats(ss, pos, ttMove, statBonus(depth));
                    }
                }
                // Penalty for a quiet ttMove that fails low
                else {
                    if (!pos.captureOrPromotion(ttMove)) {
                        auto bonus{ -statBonus(depth) };
                        thread->quietStats[pos.active][mIndex(ttMove)] << bonus;
                        updateContinuationStats(ss, pos[orgSq(ttMove)], dstSq(ttMove), bonus);
                    }
                }
            }

            if (ttValue >= beta) {
                // Extra penalty for early quiet moves in previous ply when it gets refuted
                if (!pmCaptureOrPromotion
                 && 2 >= (ss-1)->moveCount
                 && isOk((ss-1)->playedMove)) {
                    auto pmDst{ prevDst(pos.active, (ss-1)->playedMove) };
                    updateContinuationStats(ss-1, pos[pmDst], pmDst, -statBonus(depth + 1));
                }
            }

            if (90 > pos.clockPly()) {
                return ttValue;
            }
        }

        // Step 5. Tablebases probe.
        if (!rootNode
         && 0 != TBLimitPiece)
        {
            auto pieceCount{ pos.count() };

            if (( pieceCount < TBLimitPiece
              || (pieceCount == TBLimitPiece
               && depth >= TBProbeDepth))
             && 0 == pos.clockPly()
             && CR_NONE == pos.castleRights()) {
                ProbeState probeState;
                auto wdlScore{ probeWDL(pos, probeState) };

                // Force check of time on the next occasion
                if (Threadpool.mainThread() == thread) {
                    Threadpool.mainThread()->setTicks(1);
                }

                if (FAILURE != probeState) {
                    thread->tbHits.fetch_add(1, std::memory_order::memory_order_relaxed);

                    i16 draw{ TBUseRule50 };

                    value = wdlScore < -draw ? -VALUE_MATE_1_MAX_PLY + (ss->ply + 1) :
                            wdlScore > +draw ? +VALUE_MATE_1_MAX_PLY - (ss->ply + 1) :
                                                VALUE_DRAW + 2 * i32(wdlScore) * draw;

                    auto bound{ wdlScore < -draw ? BOUND_UPPER :
                                wdlScore > +draw ? BOUND_LOWER :
                                                   BOUND_EXACT };

                    if ( BOUND_EXACT == bound
                     || (BOUND_LOWER == bound ? beta <= value : value <= alfa)) {
                        tte->save(key,
                                  MOVE_NONE,
                                  valueToTT(value, ss->ply),
                                  VALUE_NONE,
                                  Depth(std::min(depth + 6, MaxDepth - 1)),
                                  bound,
                                  ttPV);

                        return value;
                    }

                    if (PVNode) {
                        if (BOUND_LOWER == bound) {
                            bestValue = value;
                            if (alfa < value) {
                                alfa = value;
                            }
                        }
                        else {
                            maxValue = value;
                        }
                    }
                }
            }
        }

        StateInfo si;
        Move move;
        bool improving;
        Value eval;

        // Step 6. Static evaluation of the position
        if (inCheck) {
            ss->staticEval = eval = VALUE_NONE;
            improving = false;
        }
        // Skip early pruning when in check
        else {
            if (ttHit) {
                // Never assume anything on values stored in TT.
                if (VALUE_NONE == (ss->staticEval = eval = tte->eval())) {
                    ss->staticEval = eval = Evaluator::evaluate(pos);
                }

                if (eval == VALUE_DRAW) {
                    eval = drawValue();
                }
                // Can ttValue be used as a better position evaluation?
                if (VALUE_NONE != ttValue
                 && BOUND_NONE != (tte->bound()
                                 & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER))) {
                    eval = ttValue;
                }
            }
            else {
                ss->staticEval = eval =
                    MOVE_NULL != (ss-1)->playedMove ?
                        Evaluator::evaluate(pos) + (-(ss-1)->stats / 512) :
                        -(ss-1)->staticEval + 2 * VALUE_TEMPO;

                tte->save(key,
                          MOVE_NONE,
                          VALUE_NONE,
                          eval,
                          DEPTH_NONE,
                          BOUND_NONE,
                          ttPV);
            }

            // Step 7. Razoring. (~1 ELO)
            if (!rootNode // The required RootNode PV handling is not available in qsearch
             && 2 > depth
             && eval + RazorMargin <= alfa) {
                return quienSearch<PVNode>(pos, ss, alfa, beta);
            }

            improving = VALUE_NONE != (ss-2)->staticEval ?
                            ss->staticEval > (ss-2)->staticEval :
                            VALUE_NONE != (ss-4)->staticEval ?
                                ss->staticEval > (ss-4)->staticEval :
                                VALUE_NONE != (ss-6)->staticEval ?
                                    ss->staticEval > (ss-6)->staticEval :
                                    true;

            // Step 8. Futility pruning: child node. (~50 ELO)
            // Betting that the opponent doesn't have a move that will reduce
            // the score by more than futility margins if do a null move.
            if (!rootNode
             && 6 > depth
             && eval < +VALUE_KNOWN_WIN // Don't return unproven wins.
             && eval - futilityMargin(depth, improving) >= beta
             && 0 == Limits.mate) {
                return eval;
            }

            // Step 9. Null move search with verification search. (~40 ELO)
            if (!PVNode
             && (ss-1)->playedMove != MOVE_NULL
             && (ss-1)->stats < 23397
             && eval >= beta
             && eval >= ss->staticEval
             && ss->staticEval - 292 + 32 * depth + 30 * improving - 120 * ttPV >= beta
             && MOVE_NONE == ss->excludedMove
             && VALUE_ZERO != pos.nonPawnMaterial(pos.active)
             && (thread->nmpPly <= ss->ply
              || thread->nmpColor != pos.active)
             && 0 == Limits.mate) {
                // Null move dynamic reduction based on depth and static evaluation.
                auto R = Depth((854 + 68 * depth) / 258 + std::min(i32(eval - beta) / 192, 3));

                ss->playedMove = MOVE_NULL;
                ss->pieceStats = &thread->continuationStats[0][0][NO_PIECE][0];

                pos.doNullMove(si);

                auto null_value = -depthSearch<false>(pos, ss+1, -beta, -beta+1, depth-R, !cutNode);

                pos.undoNullMove();

                if (null_value >= beta) {
                    // Skip verification search
                    if (0 != thread->nmpPly // Recursive verification is not allowed
                     || (13 > depth
                      && abs(beta) < +VALUE_KNOWN_WIN)) {
                        // Don't return unproven wins
                        return null_value >= +VALUE_MATE_2_MAX_PLY ? beta : null_value;
                    }

                    // Do verification search at high depths,
                    // with null move pruning disabled for nmpColor until ply exceeds nmpPly
                    thread->nmpColor = pos.active;
                    thread->nmpPly = ss->ply + 3 * (depth-R) / 4;
                    value = depthSearch<false>(pos, ss, beta-1, beta, depth-R, false);
                    thread->nmpPly = 0;

                    if (value >= beta) {
                        // Don't return unproven wins
                        return null_value >= +VALUE_MATE_2_MAX_PLY ? beta : null_value;
                    }
                }
            }

            // Step 10. ProbCut. (~10 ELO)
            // If good enough capture and a reduced search returns a value much above beta,
            // then can (almost) safely prune the previous move.
            if (!PVNode
             && 4 < depth
             && abs(beta) < +VALUE_MATE_2_MAX_PLY
             && 0 == Limits.mate) {
                auto raisedBeta{ std::min(beta + 189 - 45 * improving, +VALUE_INFINITE) };

                u08 pcMoveCount{ 0 };
                // Initialize move-picker(3) for the current position
                MovePicker mp{ pos
                             , &thread->captureStats
                             , ttMove, raisedBeta - ss->staticEval };
                // Loop through all the pseudo-legal moves until no moves remain or a beta cutoff occurs
                while (2 + 2 * cutNode > pcMoveCount
                    && MOVE_NONE != (move = mp.nextMove())) {
                    assert(isOk(move)
                        && pos.pseudoLegal(move)
                        && pos.captureOrPromotion(move));

                    if (move == ss->excludedMove
                     || !pos.legal(move)) {
                        continue;
                    }

                    ++pcMoveCount;

                    // Speculative prefetch as early as possible
                    prefetch(TT.cluster(pos.movePosiKey(move))->entries);

                    ss->playedMove = move;
                    ss->pieceStats = &thread->continuationStats[0][1][pos[orgSq(move)]][dstSq(move)];

                    pos.doMove(move, si);

                    // Perform a preliminary quienSearch to verify that the move holds
                    value = -quienSearch<false>(pos, ss+1, -raisedBeta, -raisedBeta+1);

                    // If the quienSearch held perform the regular search
                    if (value >= raisedBeta) {
                        value = -depthSearch<false>(pos, ss+1, -raisedBeta, -raisedBeta+1, depth - 4, !cutNode);
                    }

                    pos.undoMove(move);

                    if (value >= raisedBeta) {
                        return value;
                    }
                }
            }

            // Step 11. Internal iterative deepening (IID). (~1 ELO)
            if (6 < depth
             && (MOVE_NONE == ttMove
              || !pos.pseudoLegal(ttMove))) {
                depthSearch<PVNode>(pos, ss, alfa, beta, depth - 7, cutNode);

                tte = TT.probe(key, ttHit);
                ttMove = ttHit ?
                            tte->move() : MOVE_NONE;
                ttValue = ttHit ?
                            valueOfTT(tte->value(), ss->ply, pos.clockPly()) : VALUE_NONE;
            }
        }

        value = bestValue;

        u08 moveCount{ 0 };

        // Mark this node as being searched.
        ThreadMarker threadMarker{ thread, pos.posiKey(), ss->ply };

        vector<Move> quietMoves
            ,        captureMoves;
        quietMoves.reserve(32);
        captureMoves.reserve(16);

        bool singularLMR{ false };
        bool ttmCapture{ MOVE_NONE != ttMove
                      && pos.captureOrPromotion(ttMove)
                      && pos.pseudoLegal(ttMove) };

        const PieceSquareStatsTable* pieceStats[]
        {
            (ss-1)->pieceStats, (ss-2)->pieceStats,
            nullptr           , (ss-4)->pieceStats,
            nullptr           , (ss-6)->pieceStats
        };

        auto counterMove{ MOVE_NONE };
        if (isOk((ss-1)->playedMove)) {
            auto pmDst{ prevDst(pos.active, (ss-1)->playedMove) };
            assert(NO_PIECE != pos[pmDst]);
            counterMove = pos.thread->quietCounterMoves[pos[pmDst]][pmDst];
        }

        // Initialize move-picker(1) for the current position
        MovePicker mp{ pos
                     , &thread->quietStats
                     , &thread->captureStats
                     , pieceStats
                     , ttMove, depth, ss->killerMoves, counterMove };
        // Step 12. Loop through all pseudo-legal moves until no moves remain or a beta cutoff occurs.
        while (MOVE_NONE != (move = mp.nextMove())) {
            assert(isOk(move)
                && pos.pseudoLegal(move));

            // Skip exclusion move
            if (move == ss->excludedMove) {
                continue;
            }

            if (rootNode) {
                // At root obey the "searchmoves" option and skip moves not listed in Root
                // Move List. As a consequence any illegal move is also skipped. In MultiPV
                // mode we also skip PV moves which have been already searched and those
                // of lower "TB rank" if we are in a TB root position.
                if (std::find(std::next(thread->rootMoves.begin(), thread->pvCur),
                              std::next(thread->rootMoves.begin(), thread->pvEnd), move)
                           == std::next(thread->rootMoves.begin(), thread->pvEnd)) {
                    continue;
                }

                if (Threadpool.mainThread() == thread) {
                    auto elapsed{ TimeMgr.elapsed() + 1 };
                    if (3000 < elapsed) {
                        sync_cout << std::setfill('0')
                                  << "info"
                                  << " currmove "       << move
                                  << " currmovenumber " << std::setw(2) << thread->pvCur + moveCount + 1
                                  //<< " maxmoves "       << thread->rootMoves.size()
                                  << " depth "          << depth
                                  //<< " seldepth "       << (*std::find(std::next(thread->rootMoves.begin(), thread->pvCur),
                                  //                                     std::next(thread->rootMoves.begin(), thread->pvEnd), move)).selDepth
                                  << " time "           << elapsed
                                  << std::setfill('0') << sync_endl;
                    }
                }
            }

            ss->moveCount = ++moveCount;

            if (PVNode) {
                (ss+1)->pv.clear();
            }

            auto org{ orgSq(move) };
            auto dst{ dstSq(move) };
            auto mpc{ pos[org] };
            bool giveCheck{ pos.giveCheck(move) };
            bool captureOrPromotion{ pos.captureOrPromotion(move) };

            // Calculate new depth for this move
            auto newDepth{ Depth(depth - 1) };

            // Step 13. Pruning at shallow depth. (~200 ELO)
            if (!rootNode
             && -VALUE_MATE_2_MAX_PLY < bestValue
             && VALUE_ZERO < pos.nonPawnMaterial(pos.active)
             && 0 == Limits.mate) {
                // Skip quiet moves if move count exceeds our futilityMoveCount() threshold
                mp.skipQuiets = futilityMoveCount(depth, improving) <= moveCount;

                if (!captureOrPromotion
                 && !giveCheck) {
                    // Reduced depth of the next LMR search.
                    i32 lmrDepth{ std::max(newDepth - reduction(depth, moveCount, improving), 0) };
                    // Counter moves based pruning: (~20 ELO)
                    if (4 + (0 < (ss-1)->stats || 1 == (ss-1)->moveCount) > lmrDepth
                     && (*pieceStats[0])[mpc][dst] < CounterMovePruneThreshold
                     && (*pieceStats[1])[mpc][dst] < CounterMovePruneThreshold) {
                        continue;
                    }
                    // Futility pruning: parent node. (~5 ELO)
                    if (!inCheck
                     && 6 > lmrDepth
                     && ss->staticEval + 235 + 172 * lmrDepth <= alfa
                     && thread->quietStats[pos.active][mIndex(move)]
                      + (*pieceStats[0])[mpc][dst]
                      + (*pieceStats[1])[mpc][dst]
                      + (*pieceStats[3])[mpc][dst] < 25000) {
                        continue;
                    }
                    // SEE based pruning: negative SEE (~20 ELO)
                    if (!pos.see(move, Value(-(32 - std::min(lmrDepth, 18)) * lmrDepth * lmrDepth))) {
                        continue;
                    }
                }
                // SEE based pruning: negative SEE (~25 ELO)
                else
                if (!pos.see(move, Value(-194 * depth)))
                {
                    continue;
                }
            }

            // Step 14. Extensions. (~75 ELO)
            auto extension{ DEPTH_ZERO };

            // Singular extension (SE) (~70 ELO)
            // Extend the TT move if its value is much better than its siblings.
            // If all moves but one fail low on a search of (alfa-s, beta-s),
            // and just one fails high on (alfa, beta), then that move is singular and should be extended.
            // To verify this do a reduced search on all the other moves but the ttMove,
            // if result is lower than ttValue minus a margin then extend ttMove.
            if (!rootNode
             && 5 < depth
             && move == ttMove
             && MOVE_NONE == ss->excludedMove // Avoid recursive singular search
             // && VALUE_NONE != ttValue  Already implicit in the next condition
             && +VALUE_KNOWN_WIN > abs(ttValue) // Handle ttHit
             && depth < tte->depth() + 4
             && BOUND_NONE != (tte->bound() & BOUND_LOWER)
             && pos.legal(ttMove)) {
                auto singularBeta{ ttValue - ((4 + (ttPV && !PVNode)) * depth) / 2 };

                ss->excludedMove = move;
                value = depthSearch<false>(pos, ss, singularBeta -1, singularBeta, depth / 2, cutNode);
                ss->excludedMove = MOVE_NONE;

                if (value < singularBeta) {
                    extension = 1;
                    singularLMR = true;
                }
                // Multi-cut pruning
                // Our ttMove is assumed to fail high, and now failed high also on a reduced
                // search without the ttMove. So assume this expected Cut-node is not singular,
                // multiple moves fail high, and can prune the whole subtree by returning the soft bound.
                else
                if (singularBeta >= beta) {
                    return singularBeta;
                }
            }
            else
            if (// Previous capture extension
                (PieceValues[EG][pos.captured()] > VALUE_EG_PAWN
              && pos.nonPawnMaterial() <= 2 * VALUE_MG_ROOK)
                // Check extension (~2 ELO)
             || (giveCheck
              && (contains(pos.kingBlockers(~pos.active), org)
               || pos.see(move)))
                // Passed pawn extension
             || (ss->killerMoves[0] == move
              && pos.pawnAdvanceAt(pos.active, org)
              && pos.pawnPassedAt(pos.active, dst))) {
                extension = 1;
            }

            // Castle extension
            if (0 == extension
             && CASTLE == mType(move)) {
                extension = 1;
            }

            // Check for legality just before making the move
            if (!rootNode
             && !pos.legal(move)) {
                ss->moveCount = --moveCount;
                if (ttmCapture
                 && move == ttMove) {
                    ttmCapture = false;
                }
                continue;
            }

            // Add extension to new depth
            newDepth += extension;

            // Speculative prefetch as early as possible
            prefetch(TT.cluster(pos.movePosiKey(move))->entries);

            // Update the current move.
            ss->playedMove = move;
            ss->pieceStats = &thread->continuationStats[inCheck][captureOrPromotion][mpc][dst];

            // Step 15. Do the move
            pos.doMove(move, si, giveCheck);

            bool doLMR{
                2 < depth
             && 1 + 2 * rootNode + 1 < moveCount
             && (!rootNode
                // At root if zero best counter
              || 0 == thread->moveBestCount(move))
             && (cutNode
              || !captureOrPromotion
              || mp.skipQuiets
              || ss->staticEval + PieceValues[EG][pos.captured()] <= alfa
                // If ttHit running average is small
              || thread->ttHitAvg < 375 * TTHitAverageWindow) };

            bool doFullSearch;
            // Step 16. Reduced depth search (LMR, ~200 ELO).
            // If the move fails high will be re-searched at full depth.
            if (doLMR) {
                auto reductDepth{ reduction(depth, moveCount, improving) };
                reductDepth +=
                    // If other threads are searching this position.
                    +1 * threadMarker.marked
                    // If the ttHit running average is large
                    -1 * (thread->ttHitAvg > 500 * TTHitAverageWindow)
                    // If opponent's move count is high (~5 ELO)
                    -1 * ((ss-1)->moveCount >= 15)
                    // If position is or has been on the PV (~10 ELO)
                    -2 * ttPV
                    // If move has been singularly extended (~3 ELO)
                    -2 * singularLMR;

                if (!captureOrPromotion) {
                    // If TT move is a capture (~5 ELO)
                    reductDepth += 1 * ttmCapture;

                    // If cut nodes (~10 ELO)
                    if (cutNode) {
                        reductDepth += 2;
                    }
                    else
                    // If move escapes a capture in no-cut nodes (~2 ELO)
                    if (NORMAL == mType(move)
                     && !pos.see(reverseMove(move))) {
                        reductDepth -= 2 + ttPV;
                    }

                    ss->stats = thread->quietStats[~pos.active][mIndex(move)]
                                + (*pieceStats[0])[mpc][dst]
                                + (*pieceStats[1])[mpc][dst]
                                + (*pieceStats[3])[mpc][dst]
                                - 4926;
                    // Reset stats to zero if negative and most stats shows >= 0
                    if (0 >  ss->stats
                     && 0 <= thread->quietStats[~pos.active][mIndex(move)]
                     && 0 <= (*pieceStats[0])[mpc][dst]
                     && 0 <= (*pieceStats[1])[mpc][dst]) {
                        ss->stats = 0;
                    }

                    // Decrease/Increase reduction by comparing opponent's stat score (~10 Elo)
                    reductDepth +=
                        +1 * ((ss-0)->stats < -154
                           && (ss-1)->stats >= -116)
                        -1 * ((ss-0)->stats >= -102
                           && (ss-1)->stats < -114);
                    assert(!((ss-0)->stats < -154
                          && (ss-1)->stats >= -116)
                        || !((ss-0)->stats >= -102
                          && (ss-1)->stats < -114));
                    // Decrease/Increase reduction for moves with a good/bad history (~30 Elo)
                    reductDepth -= i16(ss->stats / 0x4000);
                }
                else
                // Increase reduction for captures/promotions if late move and at low depth
                if (8 > depth
                 && 2 < moveCount) {
                    reductDepth += 1;
                }

                reductDepth = std::max(reductDepth, DEPTH_ZERO);
                auto d{ Depth(std::max(newDepth - reductDepth, 1)) };
                assert(d <= newDepth);

                value = -depthSearch<false>(pos, ss+1, -alfa-1, -alfa, d, true);

                doFullSearch = alfa < value
                            && d < newDepth;
            }
            else {
                doFullSearch = !PVNode
                            || 1 < moveCount;
            }

            // Step 17. Full depth search when LMR is skipped or fails high.
            if (doFullSearch) {
                value = -depthSearch<false>(pos, ss+1, -alfa-1, -alfa, newDepth, !cutNode);

                if (doLMR
                 && !captureOrPromotion) {
                    auto bonus{ alfa < value ?
                                +statBonus(newDepth) :
                                -statBonus(newDepth) };
                    if (ss->killerMoves[0] == move) {
                        bonus += bonus / 4;
                    }
                    updateContinuationStats(ss, mpc, dst, bonus);
                }
            }

            // Full PV search.
            if (PVNode
             && (1 == moveCount
              || (alfa < value
               && (rootNode
                || value < beta)))) {
                (ss+1)->pv.clear();

                value = -depthSearch<true>(pos, ss+1, -beta, -alfa, newDepth, false);
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
                assert(std::find(thread->rootMoves.begin(), thread->rootMoves.end(), move) != thread->rootMoves.end());
                auto &rm{ *std::find(thread->rootMoves.begin(), thread->rootMoves.end(), move) };
                // First PV move or new best move?
                if (1 == moveCount
                 || alfa < value) {
                    rm.newValue = value;
                    rm.selDepth = thread->selDepth;
                    rm.resize(1);
                    rm.insert(rm.end(), (ss+1)->pv.begin(), (ss+1)->pv.end());

                    // Record how often the best move has been changed in each iteration.
                    // This information is used for time management:
                    // When the best move changes frequently, allocate some more time.
                    if (1 < moveCount
                     && Limits.useTimeMgmt()) {
                        ++thread->pvChange;
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

                if (alfa < value) {
                    bestMove = move;

                    // Update pv even in fail-high case.
                    if (PVNode
                     && !rootNode) {
                        updatePV(ss->pv, move, (ss+1)->pv);
                    }

                    if (value >= beta) { // Fail high
                        ss->stats = 0;
                        break;
                    }
                    else
                    if (PVNode) { // Update alfa! Always alfa < beta
                        alfa = value;
                    }
                }
            }

            if (move != bestMove) {
                if (captureOrPromotion) {
                    captureMoves.push_back(move);
                }
                else {
                    quietMoves.push_back(move);
                }
            }
        }

        assert(0 != moveCount
            || !inCheck
            || MOVE_NONE != ss->excludedMove
            || 0 == MoveList<GenType::LEGAL>(pos).size());

        // Step 21. Check for checkmate and stalemate.
        // If all possible moves have been searched and if there are no legal moves,
        // If in a singular extension search then return a fail low score (alfa).
        // Otherwise it must be a checkmate or a stalemate, so return value accordingly.
        if (0 == moveCount) {
            bestValue = MOVE_NONE != ss->excludedMove ?
                            alfa :
                            inCheck ?
                                matedIn(ss->ply) :
                                VALUE_DRAW;
        }
        else
        // Quiet best move: update move sorting heuristics.
        if (MOVE_NONE != bestMove) {
            auto bonus1{ statBonus(depth + 1) };

            if (!pos.captureOrPromotion(bestMove)) {
                auto bonus2{ bestValue - VALUE_MG_PAWN > beta ?
                                bonus1 :
                                statBonus(depth) };

                updateQuietStats(ss, pos, bestMove, bonus2);
                // Decrease all the other played quiet moves.
                for (auto qm : quietMoves) {
                    thread->quietStats[pos.active][mIndex(qm)] << -bonus2;
                    updateContinuationStats(ss, pos[orgSq(qm)], dstSq(qm), -bonus2);
                }
            }
            else {
                thread->captureStats[pos[orgSq(bestMove)]][dstSq(bestMove)][pos.captureType(bestMove)] << bonus1;
            }

            // Decrease all the other played capture moves.
            for (auto cm : captureMoves) {
                thread->captureStats[pos[orgSq(cm)]][dstSq(cm)][pos.captureType(cm)] << -bonus1;
            }
            // Extra penalty for a quiet TT move or main killer move in previous ply when it gets refuted
            if (!pmCaptureOrPromotion
             && (1 == (ss-1)->moveCount
              || (ss-1)->killerMoves[0] == (ss-1)->playedMove)
             && isOk((ss-1)->playedMove)) {
                auto pmDst{ prevDst(pos.active, (ss-1)->playedMove) };
                updateContinuationStats(ss-1, pos[pmDst], pmDst, -bonus1);
            }
        }
        else
        // Bonus for prior quiet move that caused the fail low.
        if (!pmCaptureOrPromotion
         && (PVNode
          || 2 < depth)
         && isOk((ss-1)->playedMove)) {
            auto pmDst{ prevDst(pos.active, (ss-1)->playedMove) };
            updateContinuationStats(ss-1, pos[pmDst], pmDst, statBonus(depth));
        }

        if (PVNode)
        {
            if (bestValue > maxValue) {
                bestValue = maxValue;
            }
        }

        if (MOVE_NONE == ss->excludedMove) {
            tte->save(key,
                      bestMove,
                      valueToTT(bestValue, ss->ply),
                      ss->staticEval,
                      depth,
                      bestValue >= beta ?
                          BOUND_LOWER :
                          PVNode
                       && MOVE_NONE != bestMove ?
                              BOUND_EXACT :
                              BOUND_UPPER,
                      ttPV);
        }

        assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);
        return bestValue;
    }
}

bool Limit::useTimeMgmt() const {
    return !infinite
        && 0 == moveTime
        && DEPTH_ZERO == depth
        && 0 == nodes
        && 0 == mate;
}

void Limit::clear() {
    clock[WHITE].time = 0; clock[WHITE].inc = 0;
    clock[BLACK].time = 0; clock[BLACK].inc = 0;

    movestogo   = 0;
    moveTime    = 0;
    depth       = DEPTH_ZERO;
    nodes       = 0;
    mate        = 0;
    infinite    = false;
    ponder      = false;

    searchMoves.clear();

    startTime   = 0;
}


namespace Searcher {


    void initialize()
    {}
}

/// Thread::search() is thread iterative deepening loop function.
/// It calls depthSearch() repeatedly with increasing depth until
/// - Force stop requested.
/// - Allocated thinking time has been consumed.
/// - Maximum search depth is reached.
void Thread::search() {
    ttHitAvg = (TTHitAverageResolution / 2) * TTHitAverageWindow;

    i16 timedContempt{ 0 };
    i32 contemptTime{ Options["Contempt Time"] };
    if (0 != contemptTime
     && Limits.useTimeMgmt()) {
        i64 diffTime{ (i64(Limits.clock[rootPos.active].time)
                     - i64(Limits.clock[~rootPos.active].time)) / 1000 };
        timedContempt = i16(diffTime / contemptTime);
    }
    // Basic Contempt
    i32 bc{ toValue(i16(Options["Fixed Contempt"]) + timedContempt) };
    // In analysis mode, adjust contempt in accordance with user preference
    if (Limits.infinite
     || Options["UCI_AnalyseMode"]) {
        bc = Options["Analysis Contempt"] == "Off"                              ? 0 :
             Options["Analysis Contempt"] == "White" && BLACK == rootPos.active ? -bc :
             Options["Analysis Contempt"] == "Black" && WHITE == rootPos.active ? -bc :
             /*Options["Analysis Contempt"] == "Both"                           ? +bc :*/ +bc;
    }

    contempt = WHITE == rootPos.active ?
                +makeScore(bc, bc / 2) :
                -makeScore(bc, bc / 2);

    auto *mainThread{ Threadpool.mainThread() == this ?
                        Threadpool.mainThread() :
                        nullptr };
    if (nullptr != mainThread) {
        mainThread->iterValues.fill(mainThread->prevBestValue);
    }

    i16 iterIdx{ 0 };
    double pvChangeSum{ 0.0 };
    i16 researchCount{ 0 };

    auto bestValue{ -VALUE_INFINITE };
    auto window{ +VALUE_ZERO };
    auto  alfa{ -VALUE_INFINITE }
        , beta{ +VALUE_INFINITE };

    // To allow access to (ss-7) up to (ss+2), the stack must be over-sized.
    // The former is needed to allow updateContinuationStats(ss-1, ...),
    // which accesses its argument at ss-4, also near the root.
    // The latter is needed for stats and killer initialization.
    Stack stacks[MaxDepth + 10];
    for (auto ss{ stacks }; ss < stacks + MaxDepth + 10; ++ss) {
        ss->ply             = i16(ss - (stacks+7));
        ss->playedMove      = MOVE_NONE;
        ss->excludedMove    = MOVE_NONE;
        ss->moveCount       = 0;
        ss->staticEval      = VALUE_ZERO;
        ss->stats           = 0;
        ss->pieceStats      = &continuationStats[0][0][NO_PIECE][0];
        ss->killerMoves.fill(MOVE_NONE);
        ss->pv.clear();
    }

    // Iterative deepening loop until requested to stop or the target depth is reached.
    while (++rootDepth < MaxDepth
        && !Threadpool.stop
        && (nullptr == mainThread
         || DEPTH_ZERO == Limits.depth
         || rootDepth <= Limits.depth)) {
        if (nullptr != mainThread
         && Limits.useTimeMgmt()) {
            // Age out PV variability metric
            pvChangeSum *= 0.5;
        }

        // Save the last iteration's values before first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (auto &rm : rootMoves) {
            rm.oldValue = rm.newValue;
        }

        pvBeg = 0;
        pvEnd = 0;

        // MultiPV loop. Perform a full root search for each PV line.
        for (pvCur = 0; pvCur < PVCount && !Threadpool.stop; ++pvCur) {
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
            if (4 <= rootDepth) {
                auto oldValue{ rootMoves[pvCur].oldValue };
                window = Value(21 + std::abs(oldValue) / 256);
                alfa = std::max(oldValue - window, -VALUE_INFINITE);
                beta = std::min(oldValue + window, +VALUE_INFINITE);

                // Dynamic contempt
                auto dc{ bc };
                i32 contemptValue{ Options["Contempt Value"] };
                if (0 != contemptValue) {
                    dc += ((102 - bc / 2) * oldValue * 100) / ((abs(oldValue) + 157) * contemptValue);
                }
                contempt = WHITE == rootPos.active ?
                            +makeScore(dc, dc / 2) :
                            -makeScore(dc, dc / 2);
            }

            if (Threadpool.research) {
                ++researchCount;
            }

            i16 failHighCount{ 0 };

            // Start with a small aspiration window and, in case of fail high/low,
            // research with bigger window until not failing high/low anymore.
            do {
                auto adjustedDepth{ Depth(std::max(rootDepth - failHighCount - researchCount, 1)) };
                bestValue = depthSearch<true>(rootPos, stacks+7, alfa, beta, adjustedDepth, false);

                // Bring the best move to the front. It is critical that sorting is
                // done with a stable algorithm because all the values but the first
                // and eventually the new best one are set to -VALUE_INFINITE and
                // want to keep the same order for all the moves but the new PV
                // that goes to the front. Note that in case of MultiPV search
                // the already searched PV lines are preserved.
                std::stable_sort(std::next(rootMoves.begin(), pvCur),
                                 std::next(rootMoves.begin(), pvEnd));

                // If search has been stopped, break immediately.
                // Sorting is safe because RootMoves is still valid, although it refers to the previous iteration.
                if (Threadpool.stop) {
                    break;
                }

                // Give some update before to re-search.
                if (nullptr != mainThread
                 && 1 == PVCount
                 && (bestValue <= alfa
                  || beta <= bestValue)
                 && 3000 < TimeMgr.elapsed()) {
                    sync_cout << multipvInfo(mainThread, rootDepth, alfa, beta) << sync_endl;
                }

                // If fail low set new bounds.
                if (bestValue <= alfa) {
                    beta = (alfa + beta) / 2;
                    alfa = std::max(bestValue - window, -VALUE_INFINITE);

                    failHighCount = 0;
                    if (nullptr != mainThread) {
                        mainThread->stopOnPonderhit = false;
                    }
                }
                else
                // If fail high set new bounds.
                if (beta <= bestValue) {
                    // NOTE:: Don't change alfa = (alfa + beta) / 2
                    beta = std::min(bestValue + window, +VALUE_INFINITE);

                    ++failHighCount;
                }
                // Otherwise exit the loop.
                else {
                    //// Research if fail count is not zero
                    //if (0 != failHighCount) {
                    //    failHighCount = 0;
                    //    continue;
                    //}

                    ++rootMoves[pvCur].bestCount;
                    break;
                }

                window += window / 4 + 5;

                assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
            } while (true);

            // Sort the PV lines searched so far and update the GUI.
            std::stable_sort(std::next(rootMoves.begin(), pvBeg),
                             std::next(rootMoves.begin(), pvCur + 1));

            if (nullptr != mainThread
             && (Threadpool.stop
              || PVCount - 1 == pvCur
              || 3000 < TimeMgr.elapsed())) {
                sync_cout << multipvInfo(mainThread, rootDepth, alfa, beta) << sync_endl;
            }
        }

        if (!Threadpool.stop) {
            finishedDepth = rootDepth;
        }

        // Has any of the threads found a "mate in <x>"?
        if (0 != Limits.mate
         && !Limits.useTimeMgmt()
         && bestValue >= +VALUE_MATE_1_MAX_PLY
         && bestValue >= +VALUE_MATE - 2 * Limits.mate) {
            Threadpool.stop = true;
        }

        if (nullptr != mainThread) {
            // If skill level is enabled and can pick move, pick a sub-optimal best move.
            if (SkillMgr.enabled()
             && SkillMgr.canPick(rootDepth)) {
                SkillMgr.clearBestMove();
                SkillMgr.pickBestMove();
            }

            if (Limits.useTimeMgmt()
             && !Threadpool.stop
             && !mainThread->stopOnPonderhit) {
                if (mainThread->bestMove != rootMoves.front().front()) {
                    mainThread->bestMove = rootMoves.front().front();
                    mainThread->bestMoveDepth = rootDepth;
                }

                // Reduce time if the bestMove is stable over 10 iterations
                // Time Reduction factor
                double timeReduction{ 0.91 + 1.03 * (9 < finishedDepth - mainThread->bestMoveDepth) };
                // Reduction factor - Use part of the gained time from a previous stable move for the current move
                double reduction{ (1.41 + mainThread->prevTimeReduction) / (2.27 * timeReduction) };
                // Eval Falling factor
                double evalFalling{ clamp((332
                                         + 6 * (mainThread->prevBestValue * i32(+VALUE_INFINITE != mainThread->prevBestValue) - bestValue)
                                         + 6 * (mainThread->iterValues[iterIdx] * i32(+VALUE_INFINITE != mainThread->iterValues[iterIdx]) - bestValue)) / 704.0,
                                          0.50, 1.50) };

                pvChangeSum += Threadpool.sum(&Thread::pvChange);
                // Reset pv change
                Threadpool.reset(&Thread::pvChange);

                double pvInstability{ 1.00 + pvChangeSum / Threadpool.size() };

                auto availableTime{ TimePoint(TimeMgr.optimum()
                                            * reduction
                                            * evalFalling
                                            * pvInstability) };
                auto elapsed{ TimeMgr.elapsed() + 1 };

                // Stop the search
                // - If all of the available time has been used
                // - If there is less than 2 legal move available
                if (elapsed > availableTime * (1 < rootMoves.size())) {
                    // If allowed to ponder do not stop the search now but
                    // keep pondering until GUI sends "stop"/"ponderhit".
                    if (!mainThread->ponder) {
                        Threadpool.stop = true;
                    }
                    else {
                        mainThread->stopOnPonderhit = true;
                    }
                }
                else
                if (elapsed > availableTime * 0.60) {
                    if (!mainThread->ponder) {
                        Threadpool.research = true;
                    }
                }

                mainThread->prevTimeReduction = timeReduction;

                mainThread->iterValues[iterIdx] = bestValue;
                iterIdx = (iterIdx + 1) % 4;
            }
        }
    }
}

/// MainThread::search() is main thread search function.
/// It searches from root position and outputs the "bestmove"/"ponder".
void MainThread::search() {
    assert(Threadpool.mainThread() == this);

    if (Limits.useTimeMgmt()) {
        // Initialize the time manager before searching.
        TimeMgr.setup(rootPos.active, rootPos.ply);
    }
    assert(0 <= rootPos.ply);
    TEntry::Generation += 8;

    bool think{ true };

    if (rootMoves.empty()) {
        think = false;

        rootMoves += MOVE_NONE;

        sync_cout << "info"
                  << " depth " << 0
                  << " score " << toString(0 != rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW)
                  << " time "  << 0 << sync_endl;
    }
    else {
        if (!Limits.infinite
         && 0 == Limits.mate
         && Options["Use Book"]) {
            auto bm{ Book.probe(rootPos, Options["Book Move Num"], Options["Book Pick Best"]) };
            if (MOVE_NONE != bm) {
                auto rmItr{ std::find(rootMoves.begin(), rootMoves.end(), bm) };
                if (rmItr != rootMoves.end()) {
                    think = false;
                    std::swap(rootMoves.front(), *rmItr);
                    rootMoves.front().newValue = VALUE_NONE;
                    StateInfo si;
                    rootPos.doMove(bm, si);
                    auto pm{ Book.probe(rootPos, Options["Book Move Num"], Options["Book Pick Best"]) };
                    if (MOVE_NONE != pm) {
                        rootMoves.front() += pm;
                    }
                    rootPos.undoMove(bm);
                }
            }
        }

        if (think) {

            if (Limits.useTimeMgmt()) {
                bestMove = MOVE_NONE;
                bestMoveDepth = DEPTH_ZERO;
            }

            SkillMgr.setLevel(Options["UCI_LimitStrength"] ?
                                clamp(i16(std::pow((double(Options["UCI_Elo"]) - 1346.6) / 143.4, 1.240)), {0}, MaxLevel) :
                                i16(Options["Skill Level"]));

            // Have to play with skill handicap?
            // In this case enable MultiPV search by skill pv size
            // that will use behind the scenes to get a set of possible moves.
            PVCount = clamp(u32(Options["MultiPV"]),
                                       u32(1 + 3 * SkillMgr.enabled()),
                                       u32(rootMoves.size()));

            for (auto *th : Threadpool) {
                if (th != this) {
                    th->wakeUp();
                }
            }

            setTicks(1);
            Thread::search(); // Let's start searching !

            // Swap best PV line with the sub-optimal one if skill level is enabled
            if (SkillMgr.enabled()) {
                std::swap(rootMoves.front(), *std::find(rootMoves.begin(), rootMoves.end(), SkillMgr.pickBestMove()));
            }
        }
    }

    // When reach the maximum depth, can arrive here without a raise of Threads.stop.
    // However, if in an infinite search or pondering, shouldn't print the best move
    // before receiving a "stop"/"ponderhit" command. Therefore simply wait here until
    // receives one of those commands (which also raises Threads.stop).
    // Busy wait for a "stop"/"ponderhit" command.
    while (!Threadpool.stop
        && (ponder
         || Limits.infinite))
    {} // Busy wait for a stop or a ponder reset

    Thread *bestThread{ this };
    if (think) {
        // Stop the threads if not already stopped (Also raise the stop if "ponderhit" just reset Threads.ponder).
        Threadpool.stop = true;
        // Wait until all threads have finished.
        for (auto *th : Threadpool) {
            if (th != this) {
                th->waitIdle();
            }
        }
        // Check if there is better thread than main thread.
        if (1 == PVCount
         && DEPTH_ZERO == Limits.depth // Depth limit search don't use deeper thread
         && !SkillMgr.enabled()
         && !Options["UCI_LimitStrength"]
         && MOVE_NONE != rootMoves.front().front()) {

            bestThread = Threadpool.bestThread();
            // If new best thread then send PV info again.
            if (bestThread != this) {
                sync_cout << multipvInfo(bestThread, bestThread->finishedDepth, -VALUE_INFINITE, +VALUE_INFINITE) << sync_endl;
            }
        }
    }

    assert(!bestThread->rootMoves.empty()
        && !bestThread->rootMoves.front().empty());

    auto &rm{ bestThread->rootMoves.front() };

    if (Limits.useTimeMgmt()) {
        // When playing in 'nodes as time' mode, subtract the searched nodes from
        // the available ones before exiting.
        if (0 != TimeMgr.timeNodes) {
            TimeMgr.availableNodes += Limits.clock[rootPos.active].inc
                                    - Threadpool.sum(&Thread::nodes);
        }

        prevBestValue = rm.newValue;
    }

    auto bm{ rm.front() };
    auto pm{ MOVE_NONE };
    if (MOVE_NONE != bm) {
        auto itr{ std::next(rm.begin()) };
        pm = itr != rm.end() ?
            *itr : TT.extractNextMove(rootPos, bm);
        assert(bm != pm);
    }

    // Best move could be MOVE_NONE when searching on a stalemate position.
    sync_cout << "bestmove " << bm;
    if (MOVE_NONE != pm) {
        cout << " ponder " << pm;
    }
    cout << sync_endl;
}

/// MainThread::doTick() is used as timer function.
/// Used to detect when out of available limit and thus stop the search, also print debug info.
void MainThread::doTick() {
    static TimePoint InfoTime{ now() };

    if (0 < --ticks) {
        return;
    }
    // When using nodes, ensure checking rate is in range [1, 1024]
    setTicks(0 != Limits.nodes ? clamp(i32(Limits.nodes / 1024), 1, 1024) : 1024);

    auto elapsed{ TimeMgr.elapsed() };
    TimePoint time = Limits.startTime + elapsed;

    if (InfoTime + 1000 <= time) {
        InfoTime = time;

        Debugger::print();
    }

    // Do not stop until told so by the GUI.
    if (ponder) {
        return;
    }

    if ((Limits.useTimeMgmt()
      && (stopOnPonderhit
       || TimeMgr.maximum() < elapsed + 10))
     || (0 != Limits.moveTime
      && Limits.moveTime <= elapsed)
     || (0 != Limits.nodes
      && Limits.nodes <= Threadpool.sum(&Thread::nodes))) {
        Threadpool.stop = true;
    }
}
