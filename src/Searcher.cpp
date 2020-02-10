#include "Searcher.h"

#include <cmath>
#include <ctime>

#include "Evaluator.h"
#include "Logger.h"
#include "MoveGenerator.h"
#include "Notation.h"
#include "Option.h"
#include "Polyglot.h"
#include "Position.h"
#include "TBsyzygy.h"
#include "Thread.h"
#include "Transposition.h"

using namespace std;
using namespace BitBoard;
using namespace TBSyzygy;

namespace Searcher {

    Depth TBProbeDepth  = 1;
    i32   TBLimitPiece  = 6;
    bool  TBUseRule50   = true;
    bool  TBHasRoot     = false;

    namespace {

        /// Stack keeps the information of the nodes in the tree during the search.
        struct Stack
        {
        public:
            i16   ply;
            Move  playedMove;
            Move  excludedMove;
            u08   moveCount;
            Value staticEval;
            i32   stats;
            PieceDestinyHistory *pdHistory;

            array<Move, 2> killerMoves;
            list<Move> pv;
        };

        /// MovePicker class is used to pick one legal moves from the current position.
        class MovePicker
        {
        private:
            enum Stage : u08
            {
                NT_TT, NT_INIT, NT_GOOD_CAPTURES, NT_REFUTATIONS, NT_QUIETS, NT_BAD_CAPTURES,
                EV_TT, EV_INIT, EV_MOVES,
                PC_TT, PC_INIT, PC_CAPTURES,
                QS_TT, QS_INIT, QS_CAPTURES, QS_CHECKS,
            };

            const Position &pos;

            Move    ttMove;
            Depth   depth;

            array<const PieceDestinyHistory*, 6> pdHistories;

            Value   threshold;
            Square  recapSq;

            ValMoves vmoves;
            ValMoves::iterator vmItr
                ,              vmEnd;

            std::vector<Move> refutationMoves
                ,             badCaptureMoves;
            std::vector<Move>::iterator mItr
                ,                       mEnd;

            u08 stage;

            /// value() assigns a numerical value to each move in a list, used for sorting.
            /// Captures are ordered by Most Valuable Victim (MVV) with using the histories.
            /// Quiets are ordered using the histories.
            template<GenType GT>
            void value()
            {
                static_assert (GenType::CAPTURE == GT
                            || GenType::QUIET == GT
                            || GenType::EVASION == GT, "GT incorrect");

                auto *thread = pos.thread;

                for (auto &vm : vmoves)
                {
                    if (GenType::CAPTURE == GT)
                    {
                        assert(pos.captureOrPromotion(vm));
                        vm.value = 6 * i32(PieceValues[MG][pos.captureType(vm)])
                                 + thread->captureHistory[pos[orgSq(vm)]][dstSq(vm)][pos.captureType(vm)];
                    }
                    else
                    if (GenType::QUIET == GT)
                    {
                        vm.value = thread->butterflyHistory[pos.active][mIndex(vm)]
                                 + 2 * (*pdHistories[0])[pos[orgSq(vm)]][dstSq(vm)]
                                 + 2 * (*pdHistories[1])[pos[orgSq(vm)]][dstSq(vm)]
                                 + 2 * (*pdHistories[3])[pos[orgSq(vm)]][dstSq(vm)]
                                 + 1 * (*pdHistories[5])[pos[orgSq(vm)]][dstSq(vm)];
                        if (vm.value < threshold)
                            vm.value = threshold - 1;
                    }
                    else // GenType::EVASION == GT
                    {
                        vm.value = pos.capture(vm) ?
                                       i32(PieceValues[MG][pos.captureType(vm)])
                                     - pType(pos[orgSq(vm)]) :
                                       thread->butterflyHistory[pos.active][mIndex(vm)]
                                     + (*pdHistories[0])[pos[orgSq(vm)]][dstSq(vm)]
                                     - (0x10000000);
                    }
                }
            }

            /// pick() returns the next move satisfying a predicate function
            template<typename Pred>
            bool pick(Pred filter)
            {
                while (vmItr != vmEnd)
                {
                    std::swap(*vmItr, *std::max_element(vmItr, vmEnd));
                    assert(ttMove != *vmItr
                        && pos.fullLegal(*vmItr));

                    bool ok = filter();

                    ++vmItr;
                    if (ok) return true;
                }
                return false;
            }

        public:

            bool skipQuiets;

            MovePicker() = delete;
            MovePicker(const MovePicker&) = delete;
            MovePicker& operator=(const MovePicker&) = delete;

            /// MovePicker constructor for the main search
            MovePicker(const Position &p, Move ttm, Depth d, const array<const PieceDestinyHistory*, 6> &pdhs,
                       const array<Move, 2> &km, Move cm)
                : pos(p)
                , ttMove(ttm)
                , depth(d)
                , pdHistories(pdhs)
                , threshold(Value(-3000 * d))
                , refutationMoves({ km[0], km[1], cm })
                , skipQuiets(false)
            {
                assert(MOVE_NONE == ttMove
                   || (pos.pseudoLegal(ttMove)
                    && pos.legal(ttMove)));
                assert(DEP_ZERO < depth);

                stage = 0 != pos.checkers() ?
                        Stage::EV_TT :
                        Stage::NT_TT;
                stage += (MOVE_NONE == ttMove);
            }

            /// MovePicker constructor for quiescence search
            /// Because the depth <= DEP_ZERO here, only captures, queen promotions
            /// and quiet checks (only if depth >= DEP_QS_CHECK) will be generated.
            MovePicker(const Position &p, Move ttm, Depth d, const array<const PieceDestinyHistory*, 6> &pdhs, Square rs)
                : pos(p)
                , ttMove(ttm)
                , depth(d)
                , pdHistories(pdhs)
                , recapSq(rs)
                //, skipQuiets(false)
            {
                assert(MOVE_NONE == ttMove
                    || (pos.pseudoLegal(ttMove)
                     && pos.legal(ttMove)));
                assert(DEP_ZERO >= depth);

                if (   MOVE_NONE != ttMove
                    && !(   DEP_QS_RECAP < depth
                         || dstSq(ttMove) == recapSq))
                {
                    ttMove = MOVE_NONE;
                }
                stage = 0 != pos.checkers() ?
                        Stage::EV_TT :
                        Stage::QS_TT;
                stage += (MOVE_NONE == ttMove);
            }

            /// MovePicker constructor for ProbCut search.
            /// Generate captures with SEE greater than or equal to the given threshold.
            MovePicker(const Position &p, Move ttm, Value thr)
                : pos(p)
                , ttMove(ttm)
                , threshold(thr)
                //, skipQuiets(false)
            {
                assert(0 == pos.checkers());
                assert(MOVE_NONE == ttMove
                    || (pos.pseudoLegal(ttMove)
                     && pos.legal(ttMove)));

                if (   MOVE_NONE != ttMove
                    && !(   pos.capture(ttMove)
                         && pos.see(ttMove, threshold)))
                {
                    ttMove = MOVE_NONE;
                }
                stage = Stage::PC_TT;
                stage += (MOVE_NONE == ttMove);
            }

            /// next_move() is the most important method of the MovePicker class.
            /// It returns a new legal move every time it is called, until there are no more moves left.
            /// It picks the move with the biggest value from a list of generated moves
            /// taking care not to return the ttMove if it has already been searched.
            Move next_move()
            {
                reStage:
                switch (stage)
                {

                case Stage::NT_TT:
                case Stage::EV_TT:
                case Stage::PC_TT:
                case Stage::QS_TT:
                    ++stage;
                    return ttMove;

                case Stage::NT_INIT:
                case Stage::PC_INIT:
                case Stage::QS_INIT:
                    generate<GenType::CAPTURE>(vmoves, pos);
                    vmoves.erase(std::remove_if(vmoves.begin(), vmoves.end(),
                                                [&](const ValMove &vm) { return ttMove == vm
                                                                             || !pos.fullLegal(vm); }),
                                 vmoves.end());
                    value<GenType::CAPTURE>();
                    vmItr = vmoves.begin();
                    vmEnd = vmoves.end();
                    ++stage;
                    // Re-branch at the top of the switch
                    goto reStage;

                case Stage::NT_GOOD_CAPTURES:
                    if (pick([&]() { return pos.see(*vmItr, Value(-(vmItr->value) * 55 / 1024)) ?
                                             true :
                                             // Put losing capture to badCaptureMoves to be tried later
                                             (badCaptureMoves.push_back(*vmItr), false); }))
                    {
                        return *std::prev(vmItr);
                    }

                    // If the countermove is the same as a killers, skip it
                    if (   MOVE_NONE != refutationMoves[2]
                        && (   refutationMoves[2] == refutationMoves[0]
                            || refutationMoves[2] == refutationMoves[1]))
                    {
                        refutationMoves[2] = MOVE_NONE;
                    }
                    refutationMoves.erase(std::remove_if(refutationMoves.begin(), refutationMoves.end(),
                                                         [&](Move m) { return MOVE_NONE == m
                                                                           || ttMove == m
                                                                           || pos.capture(m)
                                                                           || !pos.pseudoLegal(m)
                                                                           || !pos.legal(m); }),
                                          refutationMoves.end());
                    mItr = refutationMoves.begin();
                    mEnd = refutationMoves.end();
                    ++stage;
                    /* fall through */
                case NT_REFUTATIONS:
                    // Refutation moves: Killers, Counter moves
                    if (mItr != mEnd)
                    {
                        return *mItr++;
                    }
                    mItr = refutationMoves.begin();
                    if (!skipQuiets)
                    {
                        generate<GenType::QUIET>(vmoves, pos);
                        vmoves.erase(std::remove_if(vmoves.begin(), vmoves.end(),
                                                    [&](const ValMove &vm) { return ttMove == vm
                                                                                 || std::find(mItr, mEnd, vm.move) != mEnd
                                                                                 || !pos.fullLegal(vm); }),
                                     vmoves.end());
                        value<GenType::QUIET>();
                        std::sort(vmoves.begin(), vmoves.end(), greater<ValMove>());
                        vmItr = vmoves.begin();
                        vmEnd = vmoves.end();
                    }
                    ++stage;
                    /* fall through */
                case Stage::NT_QUIETS:
                    if (   !skipQuiets
                        && vmItr != vmEnd)
                    {
                        return *vmItr++;
                    }

                    mItr = badCaptureMoves.begin();
                    mEnd = badCaptureMoves.end();
                    ++stage;
                    /* fall through */
                case Stage::NT_BAD_CAPTURES:
                    return mItr != mEnd ?
                            *mItr++ :
                            MOVE_NONE;
                    /* end */

                case Stage::EV_INIT:
                    generate<GenType::EVASION>(vmoves, pos);
                    vmoves.erase(std::remove_if(vmoves.begin(), vmoves.end(),
                                                [&](const ValMove &vm) { return ttMove == vm
                                                                             || !pos.fullLegal(vm); }),
                                 vmoves.end());
                    value<GenType::EVASION>();
                    vmItr = vmoves.begin();
                    vmEnd = vmoves.end();
                    ++stage;
                    /* fall through */
                case Stage::EV_MOVES:
                    return pick([]() { return true; }) ?
                            *std::prev(vmItr) :
                            MOVE_NONE;
                    /* end */

                case Stage::PC_CAPTURES:
                    return pick([&]() { return pos.see(*vmItr, threshold); }) ?
                            *std::prev(vmItr) :
                            MOVE_NONE;
                    /* end */

                case Stage::QS_CAPTURES:
                    if (pick([&]() { return DEP_QS_RECAP < depth
                                         || dstSq(*vmItr) == recapSq; }))
                    {
                        return *std::prev(vmItr);
                    }
                    // If did not find any move then do not try checks, finished.
                    if (DEP_QS_CHECK > depth)
                    {
                        return MOVE_NONE;
                    }

                    generate<GenType::QUIET_CHECK>(vmoves, pos);
                    vmoves.erase(std::remove_if(vmoves.begin(), vmoves.end(),
                                                [&](const ValMove &vm) { return ttMove == vm
                                                                             || !pos.fullLegal(vm); }),
                                 vmoves.end());
                    vmItr = vmoves.begin();
                    vmEnd = vmoves.end();
                    ++stage;
                    /* fall through */
                case Stage::QS_CHECKS:
                    return vmItr != vmEnd ?
                            *vmItr++ :
                            MOVE_NONE;
                    /* end */
                }
                assert(false);
                return MOVE_NONE;
            }
        };

        /// Breadcrumbs are used to pair thread and position key
        struct Breadcrumb
        {
            std::atomic<const Thread*> thread;
            std::atomic<Key>           posiKey;

            void store(const Thread *th, Key key)
            {
                thread.store(th, std::memory_order::memory_order_relaxed);
                posiKey.store(key, std::memory_order::memory_order_relaxed);
            }
        };

        array<Breadcrumb, 1024> Breadcrumbs;

        /// ThreadMarker structure keeps track of which thread left breadcrumbs at the given
        /// node for potential reductions. A free node will be marked upon entering the moves
        /// loop by the constructor, and unmarked upon leaving that loop by the destructor.
        class ThreadMarker
        {
        private:
            Breadcrumb *breadcrumb;

        public:

            bool marked;

            explicit ThreadMarker(const Thread *thread, Key posiKey, i16 ply)
                : breadcrumb(nullptr)
                , marked(false)
            {
                auto *bc = ply < 8 ?
                            &Breadcrumbs[posiKey & (Breadcrumbs.size() - 1)] :
                            nullptr;
                if (nullptr != bc)
                {
                    // Check if another already marked it, if not, mark it.
                    auto *th = bc->thread.load(std::memory_order::memory_order_relaxed);
                    if (nullptr == th)
                    {
                        bc->store(thread, posiKey);
                        breadcrumb = bc;
                    }
                    else
                    {
                        if (   th != thread
                            && bc->posiKey.load(std::memory_order::memory_order_relaxed) == posiKey)
                        {
                            marked = true;
                        }
                    }
                }
            }

            virtual ~ThreadMarker()
            {
                if (nullptr != breadcrumb) // Free the marked one.
                {
                    breadcrumb->store(nullptr, Key(0));
                }
            }
        };

        constexpr u64 TTHitAverageWindow = 4096;
        constexpr u64 TTHitAverageResolution = 1024;

        // Razor margin
        constexpr Value RazorMargin = Value(531);
        // Futility margin
        constexpr Value futilityMargin(Depth d, bool imp)
        {
            return Value(217 * (d - imp));
        }
        // Futility move count threshold
        constexpr i16 futilityMoveCount(Depth d, bool imp)
        {
            return (5 + d * d) * (1 + imp) / 2 - 1;
        }

        Depth reduction(Depth d, u08 mc, bool imp)
        {
            assert(0 <= d);
            auto r = 0 != d
                  && 0 != mc ?
                        Threadpool.reductionFactor * std::log(d) * std::log(mc) : 0;
            return Depth(  (r + 511) / 1024
                         + (!imp && (r > 1007)));
        }

        /// statBonus() is the bonus, based on depth
        constexpr i32 statBonus(Depth depth)
        {
            return 15 >= depth ?
                    (19 * depth + 155) * depth - 132 :
                    -8;
        }

        // Add a small random component to draw evaluations to keep search dynamic and to avoid 3-fold-blindness.
        Value drawValue()
        {
            return VALUE_DRAW + rand() % 3 - 1;
        }

        /// updateContinuationHistories() updates tables of the move pairs with current move.
        void updateContinuationHistories(Stack *const &ss, Piece p, Square dst, i32 bonus)
        {
            for (const auto *const &s : { ss-1, ss-2, ss-4, ss-6 })
            {
                if (isOk(s->playedMove))
                {
                    (*s->pdHistory)[p][dst] << bonus;
                }
            }
        }
        /// updateQuietStats() updates move sorting heuristics when a new quiet best move is found
        void updateQuietStats(Stack *const &ss, const Position &pos, Move move, i32 bonus)
        {
            if (ss->killerMoves[0] != move)
            {
                ss->killerMoves[1] = ss->killerMoves[0];
                ss->killerMoves[0] = move;
            }
            assert(1 == std::count(ss->killerMoves.begin(), ss->killerMoves.end(), move));

            if (isOk((ss-1)->playedMove))
            {
                auto pmDst = CASTLE != mType((ss-1)->playedMove) ?
                                dstSq((ss-1)->playedMove) :
                                relSq(~pos.active, dstSq((ss-1)->playedMove) > orgSq((ss-1)->playedMove) ? SQ_G1 : SQ_C1);
                assert(NO_PIECE != pos[pmDst]);
                pos.thread->moveHistory[pos[pmDst]][pmDst] = move;
            }

            pos.thread->butterflyHistory[pos.active][mIndex(move)] << bonus;
            if (PAWN != pType(pos[orgSq(move)]))
            {
                pos.thread->butterflyHistory[pos.active][mIndex(reverseMove(move))] << -bonus;
            }

            updateContinuationHistories(ss, pos[orgSq(move)], dstSq(move), bonus);
        }

        /// updatePV() appends the move and child pv
        void updatePV(list<Move> &pv, Move move, const list<Move> &childPV)
        {
            pv.assign(childPV.begin(), childPV.end());
            pv.push_front(move);
            assert(pv.front() == move
                && ((pv.size() == 1 && childPV.empty())
                 || (pv.back() == childPV.back() && !childPV.empty())));
        }

        /// quienSearch() is quiescence search function, which is called by the main depth limited search function when the remaining depth <= 0.
        template<bool PVNode>
        Value quienSearch(Position &pos, Stack *const &ss, Value alfa, Value beta, Depth depth = DEP_ZERO)
        {
            assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
            assert(PVNode || (alfa == beta-1));
            assert(DEP_ZERO >= depth);

            Value actualAlfa;

            if (PVNode)
            {
                actualAlfa = alfa; // To flag BOUND_EXACT when eval above alpha and no available moves
                ss->pv.clear();
            }

            bool inCheck = 0 != pos.checkers();

            // Check for maximum ply reached or immediate draw.
            if (   ss->ply >= DEP_MAX
                || pos.draw(ss->ply))
            {
                return ss->ply >= DEP_MAX
                    && !inCheck ?
                           evaluate(pos) :
                           VALUE_DRAW;
            }

            assert(ss->ply >= 1
                && ss->ply == (ss-1)->ply + 1
                && ss->ply < DEP_MAX);

            // Transposition table lookup.
            Key key = pos.posiKey();
            bool ttHit;
            auto *tte = TT.probe(key, ttHit);
            auto ttMove = ttHit ?
                            tte->move() :
                            MOVE_NONE;
            auto ttValue = ttHit ?
                            valueOfTT(tte->value(), ss->ply, pos.clockPly()) :
                            VALUE_NONE;
            auto ttPV = ttHit
                     && tte->pv();

            // Decide whether or not to include checks.
            // Fixes also the type of TT entry depth that are going to use.
            // Note that in quienSearch use only 2 types of depth: DEP_QS_CHECK or DEP_QS_NO_CHECK.
            Depth qsDepth = inCheck
                         || DEP_QS_CHECK <= depth ?
                                DEP_QS_CHECK :
                                DEP_QS_NO_CHECK;

            if (   !PVNode
                && VALUE_NONE != ttValue // Handle ttHit
                && qsDepth <= tte->depth()
                && BOUND_NONE != (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
            {
                return ttValue;
            }

            Value bestValue
                , futilityBase;

            auto *thread = pos.thread;
            auto bestMove = MOVE_NONE;
            StateInfo si;

            // Evaluate the position statically.
            if (inCheck)
            {
                ss->staticEval = VALUE_NONE;
                // Starting from the worst case which is checkmate
                bestValue = futilityBase = -VALUE_INFINITE;
            }
            else
            {
                if (ttHit)
                {
                    ss->staticEval = bestValue = tte->eval();
                    // Never assume anything on values stored in TT.
                    if (VALUE_NONE == bestValue)
                    {
                        ss->staticEval = bestValue = evaluate(pos);
                    }

                    // Can ttValue be used as a better position evaluation?
                    if (   VALUE_NONE != ttValue
                        && BOUND_NONE != (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                    {
                        bestValue = ttValue;
                    }
                }
                else
                {
                    if (MOVE_NULL != (ss-1)->playedMove)
                    {
                        ss->staticEval = bestValue = evaluate(pos);
                    }
                    else
                    {
                        ss->staticEval = bestValue = -(ss-1)->staticEval + 2*Tempo;
                    }
                }

                if (alfa < bestValue)
                {
                    // Stand pat. Return immediately if static value is at least beta.
                    if (bestValue >= beta)
                    {
                        if (!ttHit)
                        {
                            tte->save(key,
                                      MOVE_NONE,
                                      valueToTT(bestValue, ss->ply),
                                      ss->staticEval,
                                      DEP_NONE,
                                      BOUND_LOWER,
                                      ttPV);
                        }

                        assert(-VALUE_INFINITE < bestValue && bestValue < +VALUE_INFINITE);
                        return bestValue;
                    }

                    assert(bestValue < beta);
                    // Update alfa! Always alfa < beta
                    if (PVNode)
                    {
                        alfa = bestValue;
                    }
                }

                futilityBase = bestValue + 154;
            }

            if (   MOVE_NONE != ttMove
                && !(   pos.pseudoLegal(ttMove)
                     && pos.legal(ttMove)))
            {
                ttMove = MOVE_NONE;
            }

            Move move;
            u08 moveCount = 0;

            const array<const PieceDestinyHistory*, 6> pdHistories
            {
                (ss-1)->pdHistory, (ss-2)->pdHistory,
                nullptr          , (ss-4)->pdHistory,
                nullptr          , (ss-6)->pdHistory
            };

            auto recapSq = isOk((ss-1)->playedMove) ?
                                dstSq((ss-1)->playedMove) :
                                SQ_NO;

            // Initialize move picker (2) for the current position
            MovePicker mp(pos, ttMove, depth, pdHistories, recapSq);
            // Loop through the moves until no moves remain or a beta cutoff occurs
            while (MOVE_NONE != (move = mp.next_move()))
            {
                assert(pos.pseudoLegal(move)
                    && pos.legal(move));

                ++moveCount;

                auto org = orgSq(move);
                auto dst = dstSq(move);
                auto mpc = pos[org];
                bool giveCheck = pos.giveCheck(move);
                bool captureOrPromotion = pos.captureOrPromotion(move);

                // Futility pruning
                if (   !inCheck
                    && !giveCheck
                    && 0 == Threadpool.limit.mate
                    && -VALUE_KNOWN_WIN < futilityBase
                    && !pos.pawnAdvanceAt(pos.active, org))
                {
                    assert(ENPASSANT != mType(move)); // Due to !pos.pawnAdvanceAt

                    // Futility pruning parent node
                    auto futilityValue = futilityBase + PieceValues[EG][CASTLE != mType(move) ? pType(pos[dst]) : NONE];
                    if (futilityValue <= alfa)
                    {
                        bestValue = std::max(futilityValue, bestValue);
                        continue;
                    }

                    // Prune moves with negative or zero SEE
                    if (   futilityBase <= alfa
                        && !pos.see(move, Value(1)))
                    {
                        bestValue = std::max(futilityBase, bestValue);
                        continue;
                    }
                }

                // Pruning: Don't search moves with negative SEE
                if (   (   !inCheck
                        // Evasion pruning: Detect non-capture evasions for pruning
                        || (   (DEP_ZERO != depth || 2 < moveCount)
                            && -VALUE_MATE_MAX_PLY < bestValue
                            && !pos.capture(move)))
                    && 0 == Threadpool.limit.mate
                    && !pos.see(move))
                {
                    continue;
                }

                // Speculative prefetch as early as possible
                prefetch(TT.cluster(pos.movePosiKey(move))->entries);

                // Update the current move.
                ss->playedMove = move;
                ss->pdHistory = &thread->continuationHistory[inCheck][captureOrPromotion][mpc][dst];

                // Make the move.
                pos.doMove(move, si, giveCheck);

                auto value = -quienSearch<PVNode>(pos, ss+1, -beta, -alfa, depth - 1);

                // Undo the move.
                pos.undoMove(move);

                assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Check for new best move.
                if (bestValue < value)
                {
                    bestValue = value;

                    if (alfa < value)
                    {
                        bestMove = move;

                        // Update pv even in fail-high case
                        if (PVNode)
                        {
                            updatePV(ss->pv, move, (ss+1)->pv);
                        }

                        // Update alfa! Always alfa < beta
                        if (   PVNode
                            && value < beta)
                        {
                            alfa = value;
                        }
                        else
                        {
                            assert(value >= beta); // Fail high
                            break;
                        }
                    }
                }
            }

            // All legal moves have been searched. A special case: If we're in check
            // and no legal moves were found, it is checkmate.
            if (   inCheck
                && -VALUE_INFINITE == bestValue)
            {
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
        Value depthSearch(Position &pos, Stack *const &ss, Value alfa, Value beta, Depth depth, bool cutNode)
        {
            bool rootNode = PVNode
                         && 0 == ss->ply;

            // Check if there exists a move which draws by repetition,
            // or an alternative earlier move to this position.
            if (   !rootNode
                && alfa < VALUE_DRAW
                && pos.clockPly() >= 3
                && pos.cycled(ss->ply))
            {
                alfa = drawValue();
                if (alfa >= beta)
                {
                    return alfa;
                }
            }

            // Dive into quiescence search when the depth reaches zero
            if (DEP_ZERO >= depth)
            {
                return quienSearch<PVNode>(pos, ss, alfa, beta);
            }

            assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
            assert(PVNode || (alfa == beta-1));
            assert(!(PVNode && cutNode));
            assert(DEP_ZERO < depth && depth < DEP_MAX);

            // Step 1. Initialize node.
            auto *thread = pos.thread;
            bool inCheck = 0 != pos.checkers();
            ss->moveCount = 0;

            // Check for the available remaining limit.
            if (Threadpool.mainThread() == thread)
            {
                Threadpool.mainThread()->tick();
            }

            if (PVNode)
            {
                // Used to send selDepth info to GUI (selDepth from 1, ply from 0)
                thread->selDepth = std::max(Depth(ss->ply + 1), thread->selDepth);
            }

            Value value;
            auto bestValue = -VALUE_INFINITE;
            auto maxValue = +VALUE_INFINITE;

            auto bestMove = MOVE_NONE;

            if (!rootNode)
            {
                // Step 2. Check for aborted search, maximum ply reached or immediate draw.
                if (   Threadpool.stop.load(std::memory_order::memory_order_relaxed)
                    || ss->ply >= DEP_MAX
                    || pos.draw(ss->ply))
                {
                    return ss->ply >= DEP_MAX
                        && !inCheck ?
                               evaluate(pos) :
                               drawValue();
                }

                // Step 3. Mate distance pruning.
                // Even if mate at the next move our score would be at best matesIn(ss->ply+1),
                // but if alfa is already bigger because a shorter mate was found upward in the tree
                // then there is no need to search further, will never beat current alfa.
                // Same logic but with reversed signs applies also in the opposite condition of
                // being mated instead of giving mate, in this case return a fail-high score.
                alfa = std::max(matedIn(ss->ply+0), alfa);
                beta = std::min(matesIn(ss->ply+1), beta);
                if (alfa >= beta)
                {
                    return alfa;
                }
            }

            assert(ss->ply >= 0
                && ss->ply == (ss-1)->ply + 1
                && ss->ply < DEP_MAX);

            assert(MOVE_NONE == (ss+1)->excludedMove);
            (ss+2)->killerMoves.fill(MOVE_NONE);

            // Initialize stats to zero for the grandchildren of the current position.
            // So stats is shared between all grandchildren and only the first grandchild starts with stats = 0.
            // Later grandchildren start with the last calculated stats of the previous grandchild.
            // This influences the reduction rules in LMR which are based on the stats of parent position.
            (ss+2+2*rootNode)->stats = 0;

            // Step 4. Transposition table lookup.
            // Don't want the score of a partial search to overwrite a previous full search
            // TT value, so use a different position key in case of an excluded move.
            Key key = pos.posiKey() ^ (Key(ss->excludedMove) << 0x10);
            bool ttHit;
            auto *tte = TT.probe(key, ttHit);
            auto ttMove = rootNode ?
                            thread->rootMoves[thread->pvCur].front() :
                               ttHit ?
                                tte->move() :
                                MOVE_NONE;
            auto ttValue = ttHit ?
                            valueOfTT(tte->value(), ss->ply, pos.clockPly()) :
                            VALUE_NONE;
            auto ttPV = PVNode
                     || (   ttHit
                         && tte->pv());

            if (   MOVE_NONE != ttMove
                && !(   pos.pseudoLegal(ttMove)
                     && pos.legal(ttMove)))
            {
                ttMove = MOVE_NONE;
            }

            // ttHitAvg can be used to approximate the running average of ttHit
            thread->ttHitAvg = (TTHitAverageWindow - 1) * thread->ttHitAvg / TTHitAverageWindow
                             + TTHitAverageResolution * ttHit;

            bool pmCaptureOrPromotion = isOk((ss-1)->playedMove)
                                     && (   NONE != pos.captured()
                                         || PROMOTE == mType((ss-1)->playedMove));

            // At non-PV nodes we check for an early TT cutoff.
            if (   !PVNode
                && VALUE_NONE != ttValue // Handle ttHit
                && depth <= tte->depth()
                && BOUND_NONE != (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
            {
                // Update move sorting heuristics on ttMove.
                if (MOVE_NONE != ttMove)
                {
                    if (ttValue >= beta)
                    {
                        // Bonus for a quiet ttMove that fails high.
                        if (!pos.captureOrPromotion(ttMove))
                        {
                            auto bonus = statBonus(depth);
                            updateQuietStats(ss, pos, ttMove, bonus);
                        }

                        // Extra penalty for early quiet moves in previous ply when it gets refuted.
                        if (   !pmCaptureOrPromotion
                            && 2 >= (ss-1)->moveCount)
                        {
                            auto bonus = -statBonus(depth + 1);
                            updateContinuationHistories(ss-1, pos[dstSq((ss-1)->playedMove)], dstSq((ss-1)->playedMove), bonus);
                        }
                    }
                    else
                    // Penalty for a quiet ttMove that fails low.
                    if (!pos.captureOrPromotion(ttMove))
                    {
                        auto bonus = -statBonus(depth);
                        thread->butterflyHistory[pos.active][mIndex(ttMove)] << bonus;
                        updateContinuationHistories(ss, pos[orgSq(ttMove)], dstSq(ttMove), bonus);
                    }
                }

                if (90 > pos.clockPly())
                {
                    return ttValue;
                }
            }

            // Step 5. Tablebases probe.
            if (   !rootNode
                && 0 != TBLimitPiece)
            {
                auto pieceCount = pos.count();

                if (   (   pieceCount < TBLimitPiece
                        || (   pieceCount == TBLimitPiece
                            && depth >= TBProbeDepth))
                    && 0 == pos.clockPly()
                    && CR_NONE == pos.castleRights())
                {
                    ProbeState probeState;
                    auto wdl = probeWDL(pos, probeState);

                    // Force check of time on the next occasion
                    if (Threadpool.mainThread() == thread)
                    {
                        Threadpool.mainThread()->tickCount = 1;
                    }

                    if (ProbeState::FAILURE != probeState)
                    {
                        thread->tbHits.fetch_add(1, std::memory_order::memory_order_relaxed);

                        i16 draw = TBUseRule50;

                        value = wdl < -draw ? -VALUE_MATE + (DEP_MAX + ss->ply + 1) :
                                wdl > +draw ? +VALUE_MATE - (DEP_MAX + ss->ply + 1) :
                                               VALUE_ZERO + 2 * wdl * draw;

                        auto bound = wdl < -draw ? BOUND_UPPER :
                                     wdl > +draw ? BOUND_LOWER :
                                                   BOUND_EXACT;

                        if (   BOUND_EXACT == bound
                            || (BOUND_LOWER == bound ? beta <= value : value <= alfa))
                        {
                            tte->save(key,
                                      MOVE_NONE,
                                      valueToTT(value, ss->ply),
                                      VALUE_NONE,
                                      Depth(std::min(depth + 6, DEP_MAX - 1)),
                                      bound,
                                      ttPV);

                            return value;
                        }

                        if (PVNode)
                        {
                            if (BOUND_LOWER == bound)
                            {
                                bestValue = value;
                                alfa = std::max(bestValue, alfa);
                            }
                            else
                            {
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
            if (inCheck)
            {
                ss->staticEval = eval = VALUE_NONE;
                improving = false;
            }
            else
            {
                if (ttHit)
                {
                    ss->staticEval = eval = tte->eval();
                    // Never assume anything on values stored in TT.
                    if (VALUE_NONE == eval)
                    {
                        ss->staticEval = eval = evaluate(pos);
                    }

                    if (VALUE_DRAW == eval)
                    {
                        eval = drawValue();
                    }
                    // Can ttValue be used as a better position evaluation?
                    if (   VALUE_NONE != ttValue
                        && BOUND_NONE != (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
                    {
                        eval = ttValue;
                    }
                }
                else
                {
                    if (MOVE_NULL != (ss-1)->playedMove)
                    {
                        ss->staticEval = eval = evaluate(pos) - (ss-1)->stats / 512;
                    }
                    else
                    {
                        ss->staticEval = eval = -(ss-1)->staticEval + 2*Tempo;
                    }

                    tte->save(key,
                              MOVE_NONE,
                              VALUE_NONE,
                              eval,
                              DEP_NONE,
                              BOUND_NONE,
                              ttPV);
                }

                // Step 7. Razoring. (~1 ELO)
                if (   !rootNode // The required RootNode PV handling is not available in qsearch
                    && 2 > depth
                    && (  eval
                        + RazorMargin) <= alfa)
                {
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
                if (   !rootNode
                    && 6 > depth
                    && 0 == Threadpool.limit.mate
                    && eval < +VALUE_KNOWN_WIN // Don't return unproven wins.
                    && (  eval
                        - futilityMargin(depth, improving)) >= beta)
                {
                    return eval;
                }

                // Step 9. Null move search with verification search. (~40 ELO)
                if (   !PVNode
                    && MOVE_NULL != (ss-1)->playedMove
                    && MOVE_NONE == ss->excludedMove
                    && 0 == Threadpool.limit.mate
                    && VALUE_ZERO != pos.nonPawnMaterial(pos.active)
                    && 23397 > (ss-1)->stats
                    && eval >= beta
                    && eval >= ss->staticEval
                    && (  ss->staticEval
                        + 32 * depth
                        + 30 * improving
                        - 120 * ttPV
                        - 292) >= beta
                    && (   thread->nmpPly <= ss->ply
                        || thread->nmpColor != pos.active))
                {
                    // Null move dynamic reduction based on depth and static evaluation.
                    auto R = Depth((68 * depth + 854) / 258 + std::min(i32(eval - beta) / 192, 3));

                    ss->playedMove = MOVE_NULL;
                    ss->pdHistory = &thread->continuationHistory[0][0][NO_PIECE][SQ_NONE];

                    pos.doNullMove(si);

                    auto null_value = -depthSearch<false>(pos, ss+1, -beta, -beta+1, depth-R, !cutNode);

                    pos.undoNullMove();

                    if (null_value >= beta)
                    {
                        // Skip verification search
                        if (   0 != thread->nmpPly // Recursive verification is not allowed
                            || (   13 > depth
                                && abs(beta) < +VALUE_KNOWN_WIN))
                        {
                            // Don't return unproven wins
                            return null_value >= +VALUE_MATE_MAX_PLY ? beta : null_value;
                        }

                        // Do verification search at high depths,
                        // with null move pruning disabled for nmpColor until ply exceeds nmpPly
                        thread->nmpColor = pos.active;
                        thread->nmpPly = ss->ply + 3 * (depth-R) / 4;
                        value = depthSearch<false>(pos, ss, beta-1, beta, depth-R, false);
                        thread->nmpPly = 0;

                        if (value >= beta)
                        {
                            // Don't return unproven wins
                            return null_value >= +VALUE_MATE_MAX_PLY ? beta : null_value;
                        }
                    }
                }

                // Step 10. ProbCut. (~10 ELO)
                // If good enough capture and a reduced search returns a value much above beta,
                // then can (almost) safely prune the previous move.
                if (   !PVNode
                    && 4 < depth
                    && 0 == Threadpool.limit.mate
                    && abs(beta) < +VALUE_MATE_MAX_PLY)
                {
                    auto raisedBeta = std::min(beta + 189 - 45 * improving, +VALUE_INFINITE);
                    u08 moveCount = 0;
                    // Initialize move picker (3) for the current position
                    MovePicker mp(pos, ttMove, raisedBeta - ss->staticEval);
                    // Loop through all legal moves until no moves remain or a beta cutoff occurs
                    while (   moveCount < 2 + 2 * cutNode
                           && MOVE_NONE != (move = mp.next_move()))
                    {
                        assert(pos.pseudoLegal(move)
                            && pos.legal(move)
                            && pos.captureOrPromotion(move));

                        if (move == ss->excludedMove)
                        {
                            continue;
                        }

                        ++moveCount;

                        // Speculative prefetch as early as possible
                        prefetch(TT.cluster(pos.movePosiKey(move))->entries);

                        ss->playedMove = move;
                        ss->pdHistory = &thread->continuationHistory[0][1][pos[orgSq(move)]][dstSq(move)];

                        pos.doMove(move, si);

                        // Perform a preliminary quienSearch to verify that the move holds
                        value = -quienSearch<false>(pos, ss+1, -raisedBeta, -raisedBeta+1);

                        // If the quienSearch held perform the regular search
                        if (value >= raisedBeta)
                        {
                            value = -depthSearch<false>(pos, ss+1, -raisedBeta, -raisedBeta+1, depth - 4, !cutNode);
                        }

                        pos.undoMove(move);

                        if (value >= raisedBeta)
                        {
                            return value;
                        }
                    }
                }
            }

            // Step 11. Internal iterative deepening (IID). (~1 ELO)
            if (   6 < depth
                && MOVE_NONE == ttMove)
            {
                depthSearch<PVNode>(pos, ss, alfa, beta, depth - 7, cutNode);

                tte = TT.probe(key, ttHit);
                ttMove = ttHit ?
                            tte->move() :
                            MOVE_NONE;
                ttValue = ttHit ?
                            valueOfTT(tte->value(), ss->ply, pos.clockPly()) :
                            VALUE_NONE;

                if (   MOVE_NONE != ttMove
                    && !(   pos.pseudoLegal(ttMove)
                         && pos.legal(ttMove)))
                {
                    ttMove = MOVE_NONE;
                }
            }

            value = bestValue;

            u08 moveCount = 0;

            // Mark this node as being searched.
            ThreadMarker threadMarker(thread, pos.posiKey(), ss->ply);

            vector<Move> quietMoves
                ,        captureMoves;
            quietMoves.reserve(32);
            captureMoves.reserve(16);

            bool singularLMR = false;
            bool ttmCapture = MOVE_NONE != ttMove
                           && pos.captureOrPromotion(ttMove);

            const array<const PieceDestinyHistory*, 6> pdHistories
            {
                (ss-1)->pdHistory, (ss-2)->pdHistory,
                nullptr          , (ss-4)->pdHistory,
                nullptr          , (ss-6)->pdHistory
            };

            auto counterMove = MOVE_NONE;
            if (isOk((ss-1)->playedMove))
            {
                auto pmDst = CASTLE != mType((ss-1)->playedMove) ?
                                dstSq((ss-1)->playedMove) :
                                relSq(~pos.active, dstSq((ss-1)->playedMove) > orgSq((ss-1)->playedMove) ? SQ_G1 : SQ_C1);
                assert(NO_PIECE != pos[pmDst]);
                counterMove = pos.thread->moveHistory[pos[pmDst]][pmDst];
            }

            // Initialize move picker (1) for the current position
            MovePicker mp(pos, ttMove, depth, pdHistories, ss->killerMoves, counterMove);
            // Step 12. Loop through all legal moves until no moves remain or a beta cutoff occurs.
            while (MOVE_NONE != (move = mp.next_move()))
            {
                assert(pos.pseudoLegal(move)
                    && pos.legal(move));

                if (   // Skip exclusion move
                       (move == ss->excludedMove)
                       // Skip at root node:
                    || (   rootNode
                           // In "searchmoves" mode, skip moves not listed in RootMoves, as a consequence any illegal move is also skipped.
                           // In MultiPV mode we not only skip PV moves which have already been searched and those of lower "TB rank" if we are in a TB root position.
                        && std::find(std::next(thread->rootMoves.begin(), thread->pvCur),
                                     std::next(thread->rootMoves.begin(), thread->pvEnd), move)
                                  == std::next(thread->rootMoves.begin(), thread->pvEnd)))
                {
                    continue;
                }

                ss->moveCount = ++moveCount;

                if (   rootNode
                    && Threadpool.mainThread() == thread)
                {
                    auto elapsedTime = Threadpool.mainThread()->timeMgr.elapsedTime();
                    if (elapsedTime > 3000)
                    {
                        sync_cout << setfill('0')
                                  << "info"
                                  << " currmove "       << move
                                  << " currmovenumber " << setw(2) << thread->pvCur + moveCount
                                  //<< " maxmoves "       << thread->rootMoves.size()
                                  << " depth "          << depth
                                  //<< " seldepth "       << (*std::find(std::next(thread->rootMoves.begin(), thread->pvCur),
                                  //                                     std::next(thread->rootMoves.begin(), thread->pvEnd), move)).selDepth
                                  << " time "           << elapsedTime
                                  << setfill('0') << sync_endl;
                    }
                }

                /*
                // In MultiPV mode also skip moves which will be searched later as PV moves
                if (   rootNode
                    //&& thread->pvCur < Threadpool.pvCount
                    && std::find(std::next(thread->rootMoves.begin(), thread->pvCur + 1),
                                 std::next(thread->rootMoves.begin(), Threadpool.pvCount), move)
                              != std::next(thread->rootMoves.begin(), Threadpool.pvCount))
                {
                    continue;
                }
                */

                if (PVNode)
                {
                    (ss+1)->pv.clear();
                }

                auto org = orgSq(move);
                auto dst = dstSq(move);
                auto mpc = pos[org];
                bool giveCheck = pos.giveCheck(move);
                bool captureOrPromotion = pos.captureOrPromotion(move);

                // Calculate new depth for this move
                auto newDepth = Depth(depth - 1);

                // Step 13. Pruning at shallow depth. (~200 ELO)
                if (   !rootNode
                    && -VALUE_MATE_MAX_PLY < bestValue
                    && 0 == Threadpool.limit.mate
                    && VALUE_ZERO < pos.nonPawnMaterial(pos.active))
                {
                    // Skip quiet moves if move count exceeds our futilityMoveCount() threshold
                    mp.skipQuiets = futilityMoveCount(depth, improving) <= moveCount;

                    if (   !captureOrPromotion
                        && !giveCheck)
                    {
                        // Reduced depth of the next LMR search.
                        auto lmrDepth = std::max(newDepth - reduction(depth, moveCount, improving), 0);
                        // Counter moves based pruning: (~20 ELO)
                        if (   (  4
                                + (   0 < (ss-1)->stats
                                   || 1 == (ss-1)->moveCount)) > lmrDepth
                            && (*pdHistories[0])[mpc][dst] < CounterMovePruneThreshold
                            && (*pdHistories[1])[mpc][dst] < CounterMovePruneThreshold)
                        {
                            continue;
                        }
                        // Futility pruning: parent node. (~5 ELO)
                        if (   !inCheck
                            && 6 > lmrDepth
                            && (  ss->staticEval
                                + 172 * lmrDepth
                                + 235) <= alfa
                            && (  thread->butterflyHistory[pos.active][mIndex(move)]
                                + (*pdHistories[0])[mpc][dst]
                                + (*pdHistories[1])[mpc][dst]
                                + (*pdHistories[3])[mpc][dst]) < 25000)
                        {
                            continue;
                        }
                        // SEE based pruning: negative SEE (~20 ELO)
                        if (!pos.see(move, Value(-(32 - std::min(lmrDepth, 18)) * lmrDepth * lmrDepth)))
                        {
                            continue;
                        }
                    }
                    else
                    // SEE based pruning: negative SEE (~25 ELO)
                    if (!pos.see(move, Value(-194 * depth)))
                    {
                        continue;
                    }
                }

                // Step 14. Extensions. (~75 ELO)
                auto extension = DEP_ZERO;

                // Singular extension (SE) (~70 ELO)
                // Extend the TT move if its value is much better than its siblings.
                // If all moves but one fail low on a search of (alfa-s, beta-s),
                // and just one fails high on (alfa, beta), then that move is singular and should be extended.
                // To verify this do a reduced search on all the other moves but the ttMove,
                // if result is lower than ttValue minus a margin then extend ttMove.
                if (   !rootNode
                    && 5 < depth
                    && move == ttMove
                    && MOVE_NONE == ss->excludedMove // Avoid recursive singular search.
                    && +VALUE_KNOWN_WIN > abs(ttValue) // Handle ttHit
                    && depth < tte->depth() + 4
                    && BOUND_NONE != (tte->bound() & BOUND_LOWER))
                {
                    auto singularBeta = ttValue - 2 * depth;

                    ss->excludedMove = move;
                    value = depthSearch<false>(pos, ss, singularBeta -1, singularBeta, depth/2, cutNode);
                    ss->excludedMove = MOVE_NONE;

                    if (value < singularBeta)
                    {
                        extension = 1;
                        singularLMR = true;
                    }
                    else
                    // Multi-cut pruning
                    // Our ttMove is assumed to fail high, and now failed high also on a reduced
                    // search without the ttMove. So assume this expected Cut-node is not singular,
                    // multiple moves fail high, and can prune the whole subtree by returning the soft bound.
                    if (singularBeta >= beta)
                    {
                        return singularBeta;
                    }
                }
                else
                if (// Last captures extension
                       (   PieceValues[EG][pos.captured()] > VALUE_EG_PAWN
                        && pos.nonPawnMaterial() <= 2 * VALUE_MG_ROOK)
                    // Check extension (~2 ELO)
                    || (   giveCheck
                        && (   contains(pos.si->kingBlockers[~pos.active], org)
                            || pos.see(move)))
                    // Passed pawn extension
                    || (   ss->killerMoves[0] == move
                        && pos.pawnAdvanceAt(pos.active, org)
                        && pos.pawnPassedAt(pos.active, dst)))
                {
                    extension = 1;
                }

                // Castle extension
                if (CASTLE == mType(move))
                {
                    extension = 1;
                }

                // Add extension to new depth
                newDepth += extension;

                // Speculative prefetch as early as possible
                prefetch(TT.cluster(pos.movePosiKey(move))->entries);

                // Update the current move.
                ss->playedMove = move;
                ss->pdHistory = &thread->continuationHistory[inCheck][captureOrPromotion][mpc][dst];

                // Step 15. Make the move.
                pos.doMove(move, si, giveCheck);

                bool doLMR =
                       2 < depth
                    && (2 * rootNode + 1) < moveCount
                    && (   !rootNode
                        // At root if zero best counter
                        || thread->moveBestCount(move) == 0)
                    && (   cutNode
                        || !captureOrPromotion
                        || mp.skipQuiets
                        || (  ss->staticEval
                            + PieceValues[EG][pos.captured()]) <= alfa
                        // If ttHit running average is small
                        || thread->ttHitAvg < (375 * TTHitAverageWindow));

                bool doFullSearch;
                // Step 16. Reduced depth search (LMR, ~200 ELO).
                // If the move fails high will be re-searched at full depth.
                if (doLMR)
                {
                    auto reductDepth = reduction(depth, moveCount, improving);
                    reductDepth +=
                        // If other threads are searching this position.
                        +1 * threadMarker.marked
                        // If the ttHit running average is large
                        -1 * (thread->ttHitAvg > (500 * TTHitAverageWindow))
                        // If opponent's move count is high (~5 ELO)
                        -1 * ((ss-1)->moveCount >= 15)
                        // If position is or has been on the PV (~10 ELO)
                        -2 * ttPV
                        // If move has been singularly extended (~3 ELO)
                        -2 * singularLMR;

                    if (!captureOrPromotion)
                    {
                        // If TT move is a capture (~5 ELO)
                        reductDepth += 1 * ttmCapture;

                        // If cut nodes (~10 ELO)
                        if (cutNode)
                        {
                            reductDepth += 2;
                        }
                        else
                        // If move escapes a capture in no-cut nodes (~2 ELO)
                        if (   NORMAL == mType(move)
                            && !pos.see(reverseMove(move)))
                        {
                            reductDepth -= 2 + ttPV;
                        }

                        ss->stats = thread->butterflyHistory[~pos.active][mIndex(move)]
                                  + (*pdHistories[0])[mpc][dst]
                                  + (*pdHistories[1])[mpc][dst]
                                  + (*pdHistories[3])[mpc][dst]
                                  - 4926;
                        // Reset stats to zero if negative and most stats shows >= 0
                        if (   0 >  ss->stats
                            && 0 <= thread->butterflyHistory[~pos.active][mIndex(move)]
                            && 0 <= (*pdHistories[0])[mpc][dst]
                            && 0 <= (*pdHistories[1])[mpc][dst])
                        {
                            ss->stats = 0;
                        }

                        // Decrease/Increase reduction by comparing stats (~10 ELO)
                        if (   (ss-1)->stats >= -116
                            && ss->stats < -154)
                        {
                            reductDepth += 1;
                        }
                        else
                        if (   ss->stats >= -102
                            && (ss-1)->stats < -114)
                        {
                            reductDepth -= 1;
                        }

                        // If move with +/-ve stats (~30 ELO)
                        reductDepth -= Depth(ss->stats / 0x4000);
                    }
                    else
                    // Increase reduction for captures/promotions if late move and at low depth
                    if (   8 > depth
                        && 2 < moveCount)
                    {
                        reductDepth += 1;
                    }

                    reductDepth = std::max(reductDepth, DEP_ZERO);
                    auto d = Depth(std::max(newDepth - reductDepth, 1));
                    assert(d <= newDepth);

                    value = -depthSearch<false>(pos, ss+1, -alfa-1, -alfa, d, true);

                    doFullSearch = alfa < value
                                && d < newDepth;
                }
                else
                {
                    doFullSearch = !PVNode
                                || 1 < moveCount;
                }

                // Step 17. Full depth search when LMR is skipped or fails high.
                if (doFullSearch)
                {
                    value = -depthSearch<false>(pos, ss+1, -alfa-1, -alfa, newDepth, !cutNode);

                    if (   doLMR
                        && !captureOrPromotion)
                    {
                        int bonus = alfa < value ?
                                        +statBonus(newDepth) :
                                        -statBonus(newDepth);
                        if (ss->killerMoves[0] == move)
                        {
                            bonus += bonus / 4;
                        }
                        updateContinuationHistories(ss, mpc, dst, bonus);
                    }
                }

                // Full PV search.
                if (   PVNode
                    && (   1 == moveCount
                        || (   alfa < value
                            && (   rootNode
                                || value < beta))))
                {
                    (ss+1)->pv.clear();

                    value = -depthSearch<true>(pos, ss+1, -beta, -alfa, newDepth, false);
                }

                // Step 18. Undo move.
                pos.undoMove(move);

                assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Step 19. Check for the new best move.
                // Finished searching the move. If a stop or a cutoff occurred,
                // the return value of the search cannot be trusted,
                // and return immediately without updating best move, PV and TT.
                if (Threadpool.stop.load(std::memory_order::memory_order_relaxed))
                {
                    return VALUE_ZERO;
                }

                if (rootNode)
                {
                    assert(std::find(thread->rootMoves.begin(), thread->rootMoves.end(), move) != thread->rootMoves.end());
                    auto &rm = *std::find(thread->rootMoves.begin(), thread->rootMoves.end(), move);
                    // First PV move or new best move?
                    if (   1 == moveCount
                        || alfa < value)
                    {
                        rm.newValue = value;
                        rm.selDepth = thread->selDepth;
                        rm.resize(1);
                        rm.insert(rm.end(), (ss+1)->pv.begin(), (ss+1)->pv.end());

                        // Record how often the best move has been changed in each iteration.
                        // This information is used for time management:
                        // When the best move changes frequently, allocate some more time.
                        if (   1 < moveCount
                            && Threadpool.limit.useTimeMgr())
                        {
                            ++thread->pvChange;
                        }
                    }
                    else
                    {
                        // All other moves but the PV are set to the lowest value, this
                        // is not a problem when sorting because sort is stable and move
                        // position in the list is preserved, just the PV is pushed up.
                        rm.newValue = -VALUE_INFINITE;
                    }
                }

                // Step 20. Check best value.
                if (bestValue < value)
                {
                    bestValue = value;

                    if (alfa < value)
                    {
                        bestMove = move;

                        // Update pv even in fail-high case.
                        if (   PVNode
                            && !rootNode)
                        {
                            updatePV(ss->pv, move, (ss+1)->pv);
                        }

                        // Update alfa! Always alfa < beta
                        if (   PVNode
                            && value < beta)
                        {
                            alfa = value;
                        }
                        else
                        {
                            assert(value >= beta); // Fail high
                            ss->stats = 0;
                            break;
                        }

                    }
                }

                if (move != bestMove)
                {
                    if (captureOrPromotion)
                    {
                        captureMoves.push_back(move);
                    }
                    else
                    {
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
            if (0 == moveCount)
            {
                bestValue = MOVE_NONE != ss->excludedMove ?
                                alfa :
                                inCheck ?
                                    matedIn(ss->ply) :
                                    VALUE_DRAW;
            }
            else
            // Quiet best move: update move sorting heuristics.
            if (MOVE_NONE != bestMove)
            {
                auto bonus1 = statBonus(depth + 1);

                if (!pos.captureOrPromotion(bestMove))
                {
                    auto bonus2 = bestValue - VALUE_MG_PAWN > beta ?
                                    bonus1 :
                                    statBonus(depth);

                    updateQuietStats(ss, pos, bestMove, bonus2);
                    // Decrease all the other played quiet moves.
                    for (auto qm : quietMoves)
                    {
                        thread->butterflyHistory[pos.active][mIndex(qm)] << -bonus2;
                        updateContinuationHistories(ss, pos[orgSq(qm)], dstSq(qm), -bonus2);
                    }
                }
                else
                {
                    thread->captureHistory[pos[orgSq(bestMove)]][dstSq(bestMove)][pos.captureType(bestMove)] << bonus1;
                }

                // Decrease all the other played capture moves.
                for (auto cm : captureMoves)
                {
                    thread->captureHistory[pos[orgSq(cm)]][dstSq(cm)][pos.captureType(cm)] << -bonus1;
                }

                // Extra penalty for a quiet TT move or main killer move in previous ply when it gets refuted
                if (   !pmCaptureOrPromotion
                    && (   1 == (ss-1)->moveCount
                        || (ss-1)->killerMoves[0] == (ss-1)->playedMove))
                {
                    updateContinuationHistories(ss-1, pos[dstSq((ss-1)->playedMove)], dstSq((ss-1)->playedMove), -bonus1);
                }
            }
            else
            // Bonus for prior quiet move that caused the fail low.
            if (   !pmCaptureOrPromotion
                && (   PVNode
                    || 2 < depth))
            {
                auto bonus = statBonus(depth);
                updateContinuationHistories(ss-1, pos[dstSq((ss-1)->playedMove)], dstSq((ss-1)->playedMove), bonus);
            }

            if (PVNode)
            {
                if (bestValue > maxValue)
                {
                    bestValue = maxValue;
                }
            }

            if (MOVE_NONE == ss->excludedMove)
            {
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

    /// initialize() initializes.
    void initialize()
    {
        srand((u32)(time(NULL)));
    }
    /// clear() resets search state to its initial value.
    void clear()
    {
        Threadpool.stop = true;
        Threadpool.mainThread()->waitIdle();

        TT.clear();
        Threadpool.clear();
        TBSyzygy::initialize(string(Options["SyzygyPath"])); // Free up mapped files
    }

}

using namespace Searcher;

/// Thread::search() is thread iterative deepening loop function.
/// It calls depthSearch() repeatedly with increasing depth until
/// - Force stop requested.
/// - Allocated thinking time has been consumed.
/// - Maximum search depth is reached.
void Thread::search()
{
    ttHitAvg = (TTHitAverageResolution / 2) * TTHitAverageWindow;

    i16 timedContempt = 0;
    auto contemptTime = i32(Options["Contempt Time"]);
    if (   0 != contemptTime
        && Threadpool.limit.useTimeMgr())
    {
        i64 diffTime = (i64(Threadpool.limit.clock[ rootPos.active].time)
                      - i64(Threadpool.limit.clock[~rootPos.active].time)) / 1000;
        timedContempt = i16(diffTime / contemptTime);
    }
    // Basic Contempt
    auto bc = i32(cpValue(i16(i32(Options["Fixed Contempt"])) + timedContempt));
    // In analysis mode, adjust contempt in accordance with user preference
    if (   Threadpool.limit.infinite
        || bool(Options["UCI_AnalyseMode"]))
    {
        bc = Options["Analysis Contempt"] == "Off"                              ? 0 :
             Options["Analysis Contempt"] == "White" && BLACK == rootPos.active ? -bc :
             Options["Analysis Contempt"] == "Black" && WHITE == rootPos.active ? -bc :
             /*Options["Analysis Contempt"] == "Both"                           ? +bc :*/ +bc;
    }

    contempt = WHITE == rootPos.active ?
                +makeScore(bc, bc / 2) :
                -makeScore(bc, bc / 2);

    auto *mainThread = Threadpool.mainThread() == this ?
                        Threadpool.mainThread() :
                        nullptr;
    if (nullptr != mainThread)
    {
        mainThread->iterValues.fill(mainThread->prevBestValue);
    }

    i16 iterIdx = 0;
    double pvChangeSum = 0.0;
    i16 researchCount = 0;

    auto bestValue = -VALUE_INFINITE;
    auto window = +VALUE_ZERO;
    auto  alfa = -VALUE_INFINITE
        , beta = +VALUE_INFINITE;

    // To allow access to (ss-7) up to (ss+2), the stack must be over-sized.
    // The former is needed to allow updateContinuationHistories(ss-1, ...),
    // which accesses its argument at ss-4, also near the root.
    // The latter is needed for stats and killer initialization.
    Stack stacks[DEP_MAX + 10];
    for (auto ss = stacks; ss < stacks + DEP_MAX + 10; ++ss)
    {
        ss->ply             = i16(ss - (stacks+7));
        ss->playedMove      = MOVE_NONE;
        ss->excludedMove    = MOVE_NONE;
        ss->moveCount       = 0;
        ss->staticEval      = VALUE_ZERO;
        ss->stats           = 0;
        ss->pdHistory       = &continuationHistory[0][0][NO_PIECE][SQ_NONE];
        ss->killerMoves.fill(MOVE_NONE);
        ss->pv.clear();
    }

    // Iterative deepening loop until requested to stop or the target depth is reached.
    while (   ++rootDepth < DEP_MAX
           && !Threadpool.stop
           && (   nullptr == mainThread
               || DEP_ZERO == Threadpool.limit.depth
               || rootDepth <= Threadpool.limit.depth))
    {
        if (   nullptr != mainThread
            && Threadpool.limit.useTimeMgr())
        {
            // Age out PV variability metric
            pvChangeSum *= 0.5;
        }

        // Save the last iteration's values before first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (auto &rm : rootMoves)
        {
            rm.oldValue = rm.newValue;
        }

        pvBeg = 0;
        pvEnd = 0;

        // MultiPV loop. Perform a full root search for each PV line.
        for (pvCur = 0; pvCur < Threadpool.pvCount && !Threadpool.stop; ++pvCur)
        {
            if (pvCur == pvEnd)
            {
                pvBeg = pvEnd;
                while (++pvEnd < rootMoves.size())
                {
                    if (rootMoves[pvEnd].tbRank != rootMoves[pvBeg].tbRank)
                    {
                        break;
                    }
                }
            }

            // Reset UCI info selDepth for each depth and each PV line
            selDepth = DEP_ZERO;

            // Reset aspiration window starting size.
            if (4 <= rootDepth)
            {
                auto oldValue = rootMoves[pvCur].oldValue;
                window = Value(21 + std::abs(oldValue) / 256);
                alfa = std::max(oldValue - window, -VALUE_INFINITE);
                beta = std::min(oldValue + window, +VALUE_INFINITE);

                // Dynamic contempt
                auto dc = bc;
                auto contemptValue = i32(Options["Contempt Value"]);
                if (0 != contemptValue)
                {
                    dc += ((102 - bc / 2) * oldValue * 100) / ((abs(oldValue) + 157) * contemptValue);
                }
                contempt = WHITE == rootPos.active ?
                            +makeScore(dc, dc / 2) :
                            -makeScore(dc, dc / 2);
            }

            if (Threadpool.research)
            {
                ++researchCount;
            }

            i16 failHighCount = 0;

            // Start with a small aspiration window and, in case of fail high/low,
            // research with bigger window until not failing high/low anymore.
            do
            {
                auto adjustedDepth = Depth(std::max(rootDepth - failHighCount - researchCount, 1));
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
                if (Threadpool.stop)
                {
                    break;
                }

                // Give some update before to re-search.
                if (   nullptr != mainThread
                    && 1 == Threadpool.pvCount
                    && (bestValue <= alfa || beta <= bestValue)
                    && mainThread->timeMgr.elapsedTime() > 3000)
                {
                    sync_cout << multipvInfo(mainThread, rootDepth, alfa, beta) << sync_endl;
                }

                // If fail low set new bounds.
                if (bestValue <= alfa)
                {
                    beta = (alfa + beta) / 2;
                    alfa = std::max(bestValue - window, -VALUE_INFINITE);

                    failHighCount = 0;
                    if (nullptr != mainThread)
                    {
                        mainThread->stopOnPonderhit = false;
                    }
                }
                else
                // If fail high set new bounds.
                if (beta <= bestValue)
                {
                    // NOTE:: Don't change alfa = (alfa + beta) / 2
                    beta = std::min(bestValue + window, +VALUE_INFINITE);

                    ++failHighCount;
                }
                // Otherwise exit the loop.
                else
                {
                    //// Research if fail count is not zero
                    //if (0 != failHighCount)
                    //{
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

            if (   nullptr != mainThread
                && (   Threadpool.stop
                    || Threadpool.pvCount - 1 == pvCur
                    || mainThread->timeMgr.elapsedTime() > 3000))
            {
                sync_cout << multipvInfo(mainThread, rootDepth, alfa, beta) << sync_endl;
            }
        }

        if (!Threadpool.stop)
        {
            finishedDepth = rootDepth;
        }

        // Has any of the threads found a "mate in <x>"?
        if (   0 != Threadpool.limit.mate
            && !Threadpool.limit.useTimeMgr()
            && bestValue >= +VALUE_MATE - 2 * Threadpool.limit.mate)
        {
            Threadpool.stop = true;
        }

        if (nullptr != mainThread)
        {
            // If skill level is enabled and can pick move, pick a sub-optimal best move.
            if (   rootDepth == mainThread->skillMgr.level + 1
                && mainThread->skillMgr.enabled())
            {
                mainThread->skillMgr.bestMove = MOVE_NONE;
                mainThread->skillMgr.pickBestMove();
            }

            if (   Threadpool.limit.useTimeMgr()
                && !Threadpool.stop
                && !mainThread->stopOnPonderhit)
            {
                if (mainThread->bestMove != rootMoves.front().front())
                {
                    mainThread->bestMove = rootMoves.front().front();
                    mainThread->bestMoveDepth = rootDepth;
                }

                // Reduce time if the bestMove is stable over 10 iterations
                // Time Reduction factor
                double timeReduction = 0.91 + 1.03 * (9 < finishedDepth - mainThread->bestMoveDepth);
                // Reduction factor - Use part of the gained time from a previous stable move for the current move
                double reduction = (1.41 + mainThread->prevTimeReduction) / (2.27 * timeReduction);
                // Eval Falling factor
                double evalFalling = clamp(( 332
                                            +  6 * (mainThread->prevBestValue * i32(+VALUE_INFINITE != mainThread->prevBestValue) - bestValue)
                                            +  6 * (mainThread->iterValues[iterIdx] * i32(+VALUE_INFINITE != mainThread->iterValues[iterIdx]) - bestValue)) / 704.0,
                                              0.50, 1.50);

                pvChangeSum += Threadpool.sum(&Thread::pvChange);
                // Reset pv change
                Threadpool.reset(&Thread::pvChange);

                double pvInstability = 1.00 + pvChangeSum / Threadpool.size();


                auto availableTime = TimePoint( mainThread->timeMgr.optimumTime
                                              * reduction
                                              * evalFalling
                                              * pvInstability);
                auto elapsedTime = mainThread->timeMgr.elapsedTime();

                // Stop the search
                // - If all of the available time has been used
                // - If there is less than 2 legal move available
                if (elapsedTime > availableTime * i32(2 <= rootMoves.size()))
                {
                    // If allowed to ponder do not stop the search now but
                    // keep pondering until GUI sends "stop"/"ponderhit".
                    if (mainThread->ponder)
                    {
                        mainThread->stopOnPonderhit = true;
                    }
                    else
                    {
                        Threadpool.stop = true;
                    }
                }
                else
                if (   !mainThread->ponder
                    && elapsedTime > availableTime * 0.60)
                {
                    Threadpool.research = true;
                }

                mainThread->prevTimeReduction = timeReduction;

                mainThread->iterValues[iterIdx] = bestValue;
                iterIdx = (iterIdx + 1) % 4;
            }
            /*
            if (Threadpool.outputStream.is_open())
            {
                Threadpool.outputStream << prettyInfo(mainThread) << endl;
            }
            */
        }
    }
}

/// MainThread::search() is main thread search function.
/// It searches from root position and outputs the "bestmove"/"ponder".
void MainThread::search()
{
    assert(Threadpool.mainThread() == this
        && 0 == index);

    timeMgr.startTime = now();
    debugTime = 0;
    /*
    if (!whiteSpaces(string(Options["Output File"])))
    {
        Threadpool.outputStream.open(string(Options["Output File"]), ios_base::out|ios_base::app);
        if (Threadpool.outputStream.is_open())
        {
            Threadpool.outputStream
                << boolalpha
                << "RootPos  : " << rootPos.fen() << "\n"
                << "MaxMoves : " << rootMoves.size() << "\n"
                << "ClockTime: " << Threadpool.limit.clock[rootPos.active].time << " ms\n"
                << "ClockInc : " << Threadpool.limit.clock[rootPos.active].inc << " ms\n"
                << "MovesToGo: " << Threadpool.limit.movestogo+0 << "\n"
                << "MoveTime : " << Threadpool.limit.moveTime << " ms\n"
                << "Depth    : " << Threadpool.limit.depth << "\n"
                << "Infinite : " << Threadpool.limit.infinite << "\n"
                << "Ponder   : " << ponder << "\n"
                << " Depth Score    Time       Nodes PV\n"
                << "-----------------------------------------------------------"
                << noboolalpha << endl;
        }
    }
    */

    if (Threadpool.limit.useTimeMgr())
    {
        // Set the time manager before searching.
        timeMgr.set(rootPos.active, rootPos.ply);
    }
    assert(0 <= rootPos.ply);
    TEntry::Generation += 8; //u08((rootPos.ply + 1) << 3);

    bool think = true;

    if (rootMoves.empty())
    {
        think = false;

        rootMoves += MOVE_NONE;

        sync_cout << "info"
                  << " depth " << 0
                  << " score " << toString(0 != rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW)
                  << " time "  << 0 << sync_endl;
    }
    else
    {
        if (   !Threadpool.limit.infinite
            && 0 == Threadpool.limit.mate
            && bool(Options["Use Book"]))
        {
            auto bm = Book.probe(rootPos, i16(i32(Options["Book Move Num"])), bool(Options["Book Pick Best"]));
            if (MOVE_NONE != bm)
            {
                auto rmItr = std::find(rootMoves.begin(), rootMoves.end(), bm);
                if (rmItr != rootMoves.end())
                {
                    think = false;
                    std::swap(rootMoves.front(), *rmItr);
                    rootMoves.front().newValue = VALUE_NONE;
                    StateInfo si;
                    rootPos.doMove(bm, si);
                    auto pm = Book.probe(rootPos, i16(i32(Options["Book Move Num"])), bool(Options["Book Pick Best"]));
                    if (MOVE_NONE != pm)
                    {
                        rootMoves.front() += pm;
                    }
                    rootPos.undoMove(bm);
                }
            }
        }

        if (think)
        {
            if (Threadpool.limit.useTimeMgr())
            {
                bestMove = MOVE_NONE;
                bestMoveDepth = DEP_ZERO;
            }

            skillMgr.level = bool(Options["UCI_LimitStrength"]) ?
                                clamp(i16(std::pow((i32(Options["UCI_Elo"]) - 1346.6) / 143.4, 1.240)), i16(0), MaxLevel) :
                                i16(i32(Options["Skill Level"]));

            // Have to play with skill handicap?
            // In this case enable MultiPV search by skill pv size
            // that will use behind the scenes to get a set of possible moves.
            Threadpool.pvCount = clamp(u32(i32(Options["MultiPV"])), u32(1 + 3 * skillMgr.enabled()), u32(rootMoves.size()));

            setTickCount();

            for (auto *th : Threadpool)
            {
                if (th != this)
                {
                    th->start();
                }
            }

            Thread::search(); // Let's start searching !

            // Swap best PV line with the sub-optimal one if skill level is enabled
            if (skillMgr.enabled())
            {
                skillMgr.pickBestMove();
                std::swap(rootMoves.front(), *std::find(rootMoves.begin(), rootMoves.end(), skillMgr.bestMove));
            }
        }
    }

    // When reach the maximum depth, can arrive here without a raise of Threads.stop.
    // However, if in an infinite search or pondering, shouldn't print the best move
    // before receiving a "stop"/"ponderhit" command. Therefore simply wait here until
    // receives one of those commands (which also raises Threads.stop).
    // Busy wait for a "stop"/"ponderhit" command.
    while (   (   ponder
               || Threadpool.limit.infinite)
           && !Threadpool.stop)
    {} // Busy wait for a stop or a ponder reset

    Thread *bestThread = this;
    if (think)
    {
        // Stop the threads if not already stopped (Also raise the stop if "ponderhit" just reset Threads.ponder).
        Threadpool.stop = true;
        // Wait until all threads have finished.
        for (auto *th : Threadpool)
        {
            if (th != this)
            {
                th->waitIdle();
            }
        }
        // Check if there is better thread than main thread.
        if (   1 == Threadpool.pvCount
            && DEP_ZERO == Threadpool.limit.depth // Depth limit search don't use deeper thread
            && !skillMgr.enabled()
            && !bool(Options["UCI_LimitStrength"]))
        {
            assert(MOVE_NONE != rootMoves.front().front());

            bestThread = Threadpool.bestThread();

            // If new best thread then send PV info again.
            if (bestThread != this)
            {
                sync_cout << multipvInfo(bestThread, bestThread->finishedDepth, -VALUE_INFINITE, +VALUE_INFINITE) << sync_endl;
            }
        }
    }

    assert(!bestThread->rootMoves.empty()
        && !bestThread->rootMoves.front().empty());

    auto &rm = bestThread->rootMoves.front();

    if (Threadpool.limit.useTimeMgr())
    {
        // Update the time manager after searching.
        timeMgr.update(rootPos.active);
        prevBestValue = rm.newValue;
    }

    auto bm = rm.front();
    auto pm = MOVE_NONE;
    if (MOVE_NONE != bm)
    {
        auto itr = std::next(rm.begin());
        pm = itr != rm.end() ?
            *itr :
            TT.extractNextMove(rootPos, bm);
        assert(bm != pm);
    }
    /*
    if (Threadpool.outputStream.is_open())
    {
        auto nodes = Threadpool.sum(&Thread::nodes);
        auto elapsedTime = std::max(timeMgr.elapsedTime(), TimePoint(1));

        auto pm_str = sanMove(MOVE_NONE, rootPos);
        if (   MOVE_NONE != bm
            && MOVE_NONE != pm)
        {
            StateInfo si;
            rootPos.doMove(bm, si);
            pm_str = sanMove(pm, rootPos);
            rootPos.undoMove(bm);
        }

        Threadpool.outputStream
            << "Nodes      : " << nodes << " N\n"
            << "Time       : " << elapsedTime << " ms\n"
            << "Speed      : " << nodes * 1000 / elapsedTime << " N/s\n"
            << "Hash-full  : " << TT.hashFull() << "\n"
            << "Best Move  : " << sanMove(bm, rootPos) << "\n"
            << "Ponder Move: " << pm_str << "\n" << endl;
        Threadpool.outputStream.close();
    }
    */

    // Best move could be MOVE_NONE when searching on a stalemate position.
    sync_cout << "bestmove " << bm;
    if (MOVE_NONE != pm)
    {
        cout << " ponder " << pm;
    }
    cout << sync_endl;
}
/// MainThread::setTickCount()
void MainThread::setTickCount()
{
    // At low node count increase the checking rate otherwise use a default value.
    tickCount = 0 != Threadpool.limit.nodes ?
                    clamp(Threadpool.limit.nodes / 1024, u64(1), u64(1024)) :
                    u64(1024);
    assert(0 != tickCount);
}
/// MainThread::tick() is used as timer function.
/// Used to detect when out of available limit and thus stop the search, also print debug info.
void MainThread::tick()
{
    if (0 < --tickCount)
    {
        return;
    }
    setTickCount();

    auto elapsedTime = timeMgr.elapsedTime();

    if (debugTime + 1000 <= elapsedTime)
    {
        debugTime = elapsedTime;

        debugPrint();
    }

    // Do not stop until told so by the GUI.
    if (ponder)
    {
        return;
    }

    if (   (   Threadpool.limit.useTimeMgr()
            && (   stopOnPonderhit
                || timeMgr.maximumTime < elapsedTime + 10))
        || (   0 != Threadpool.limit.moveTime
            && Threadpool.limit.moveTime <= elapsedTime)
        || (   0 != Threadpool.limit.nodes
            && Threadpool.limit.nodes <= Threadpool.sum(&Thread::nodes)))
    {
        Threadpool.stop = true;
    }
}
