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
            Move  played_move;
            Move  excluded_move;
            u08   move_count;
            Value static_eval;
            i32   stats;
            PieceDestinyHistory *pd_history;

            array<Move, 2> killer_moves;
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

            Move    tt_move;
            Depth   depth;

            array<const PieceDestinyHistory*, 6> pd_histories;

            Value   threshold;
            Square  recap_sq;

            ValMoves vmoves;
            ValMoves::iterator vm_itr
                ,              vm_end;

            std::vector<Move> refutation_moves
                ,             bad_capture_moves;
            std::vector<Move>::iterator m_itr
                ,                       m_end;

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
                        assert(pos.capture_or_promotion(vm.move));
                        vm.value = 6 * i32(PieceValues[MG][pos.cap_type(vm.move)])
                                 + thread->capture_history[pos[org_sq(vm.move)]][dst_sq(vm.move)][pos.cap_type(vm.move)];
                    }
                    else
                    if (GenType::QUIET == GT)
                    {
                        vm.value = thread->butterfly_history[pos.active][move_index(vm.move)]
                                 + 2 * (*pd_histories[0])[pos[org_sq(vm.move)]][dst_sq(vm.move)]
                                 + 2 * (*pd_histories[1])[pos[org_sq(vm.move)]][dst_sq(vm.move)]
                                 + 2 * (*pd_histories[3])[pos[org_sq(vm.move)]][dst_sq(vm.move)]
                                 + 1 * (*pd_histories[5])[pos[org_sq(vm.move)]][dst_sq(vm.move)];
                        if (vm.value < threshold)
                            vm.value = threshold - 1;
                    }
                    else // GenType::EVASION == GT
                    {
                        vm.value = pos.capture(vm.move) ?
                                       i32(PieceValues[MG][pos.cap_type(vm.move)])
                                     - ptype(pos[org_sq(vm.move)]) :
                                       thread->butterfly_history[pos.active][move_index(vm.move)]
                                     + (*pd_histories[0])[pos[org_sq(vm.move)]][dst_sq(vm.move)]
                                     - (0x10000000);
                    }
                }
            }

            /// pick() returns the next move satisfying a predicate function
            template<typename Pred>
            bool pick(Pred filter)
            {
                while (vm_itr != vm_end)
                {
                    std::swap(*vm_itr, *std::max_element(vm_itr, vm_end));
                    bool ok =  tt_move != vm_itr->move
                            && (   (   ENPASSANT != mtype(vm_itr->move)
                                    && !contains(pos.si->king_blockers[pos.active] | pos.square(pos.active|KING), org_sq(vm_itr->move)))
                                || pos.legal(vm_itr->move))
                            && filter();

                    ++vm_itr;
                    if (ok) return true;
                }
                return false;
            }

        public:

            bool skip_quiets;

            MovePicker() = delete;
            MovePicker(const MovePicker&) = delete;
            MovePicker& operator=(const MovePicker&) = delete;

            /// MovePicker constructor for the main search
            MovePicker(const Position &p, Move ttm, Depth d, const array<const PieceDestinyHistory*, 6> &pdhs,
                       const array<Move, 2> &km, Move cm)
                : pos(p)
                , tt_move(ttm)
                , depth(d)
                , pd_histories(pdhs)
                , threshold(Value(-3000 * d))
                , refutation_moves({ km[0], km[1], cm })
                , skip_quiets(false)
            {
                assert(MOVE_NONE == tt_move
                   || (pos.pseudo_legal(tt_move)
                    && pos.legal(tt_move)));
                assert(DEP_ZERO < depth);

                stage = 0 != pos.si->checkers ?
                        Stage::EV_TT :
                        Stage::NT_TT;
                stage += (MOVE_NONE == tt_move);
            }

            /// MovePicker constructor for quiescence search
            /// Because the depth <= DEP_ZERO here, only captures, queen promotions
            /// and quiet checks (only if depth >= DEP_QS_CHECK) will be generated.
            MovePicker(const Position &p, Move ttm, Depth d, const array<const PieceDestinyHistory*, 6> &pdhs, Square rs)
                : pos(p)
                , tt_move(ttm)
                , depth(d)
                , pd_histories(pdhs)
                , recap_sq(rs)
                //, skip_quiets(false)
            {
                assert(MOVE_NONE == tt_move
                    || (pos.pseudo_legal(tt_move)
                     && pos.legal(tt_move)));
                assert(DEP_ZERO >= depth);

                if (   MOVE_NONE != tt_move
                    && !(   DEP_QS_RECAP < depth
                         || dst_sq(tt_move) == recap_sq))
                {
                    tt_move = MOVE_NONE;
                }
                stage = 0 != pos.si->checkers ?
                        Stage::EV_TT :
                        Stage::QS_TT;
                stage += (MOVE_NONE == tt_move);
            }

            /// MovePicker constructor for ProbCut search.
            /// Generate captures with SEE greater than or equal to the given threshold.
            MovePicker(const Position &p, Move ttm, Value thr)
                : pos(p)
                , tt_move(ttm)
                , threshold(thr)
                //, skip_quiets(false)
            {
                assert(0 == pos.si->checkers);
                assert(MOVE_NONE == tt_move
                    || (pos.pseudo_legal(tt_move)
                     && pos.legal(tt_move)));

                if (   MOVE_NONE != tt_move
                    && !(   pos.capture(tt_move)
                         && pos.see_ge(tt_move, threshold)))
                {
                    tt_move = MOVE_NONE;
                }
                stage = Stage::PC_TT;
                stage += (MOVE_NONE == tt_move);
            }

            /// next_move() is the most important method of the MovePicker class.
            /// It returns a new legal move every time it is called, until there are no more moves left.
            /// It picks the move with the biggest value from a list of generated moves
            /// taking care not to return the tt_move if it has already been searched.
            Move next_move()
            {
                restage:
                switch (stage)
                {

                case Stage::NT_TT:
                case Stage::EV_TT:
                case Stage::PC_TT:
                case Stage::QS_TT:
                    ++stage;
                    return tt_move;

                case Stage::NT_INIT:
                case Stage::PC_INIT:
                case Stage::QS_INIT:
                    generate<GenType::CAPTURE>(vmoves, pos);
                    value<GenType::CAPTURE>();
                    vm_itr = vmoves.begin();
                    vm_end = vmoves.end();
                    ++stage;
                    // Re-branch at the top of the switch
                    goto restage;

                case Stage::NT_GOOD_CAPTURES:
                    if (pick([&]() { return pos.see_ge(vm_itr->move, Value(-(vm_itr->value) * 55 / 1024)) ?
                                             true :
                                             // Put losing capture to bad_capture_moves to be tried later
                                             (bad_capture_moves.push_back(vm_itr->move), false); }))
                    {
                        return std::prev(vm_itr)->move;
                    }

                    // If the countermove is the same as a killers, skip it
                    if (   MOVE_NONE != refutation_moves[2]
                        && (   refutation_moves[0] == refutation_moves[2]
                            || refutation_moves[1] == refutation_moves[2]))
                    {
                        refutation_moves[2] = MOVE_NONE;
                    }
                    refutation_moves.erase(std::remove_if(refutation_moves.begin(), refutation_moves.end(),
                                                          [&](Move m) { return MOVE_NONE == m
                                                                            || tt_move == m
                                                                            || pos.capture(m)
                                                                            || !pos.pseudo_legal(m)
                                                                            || !pos.legal(m); }),
                                            refutation_moves.end());
                    m_itr = refutation_moves.begin();
                    m_end = refutation_moves.end();
                    ++stage;
                    /* fall through */
                case NT_REFUTATIONS:
                    // Refutation moves: Killers, Counter moves
                    if (m_itr != m_end)
                    {
                        return *m_itr++;
                    }
                    m_itr = refutation_moves.begin();
                    if (!skip_quiets)
                    {
                        generate<GenType::QUIET>(vmoves, pos);
                        vmoves.erase(std::remove_if(vmoves.begin(), vmoves.end(),
                                                    [&](const ValMove &vm) { return tt_move == vm.move
                                                                                 || std::find(m_itr, m_end, vm.move) != m_end
                                                                                 || !(   (   ENPASSANT != mtype(vm.move)
                                                                                          && !contains(pos.si->king_blockers[pos.active] | pos.square(pos.active|KING), org_sq(vm.move)))
                                                                                      || pos.legal(vm.move)); }),
                                     vmoves.end());
                        value<GenType::QUIET>();
                        std::sort(vmoves.begin(), vmoves.end(), greater<ValMove>());
                        vm_itr = vmoves.begin();
                        vm_end = vmoves.end();
                    }
                    ++stage;
                    /* fall through */
                case Stage::NT_QUIETS:
                    if (   !skip_quiets
                        && vm_itr != vm_end)
                    {
                        return (vm_itr++)->move;
                    }

                    m_itr = bad_capture_moves.begin();
                    m_end = bad_capture_moves.end();
                    ++stage;
                    /* fall through */
                case Stage::NT_BAD_CAPTURES:
                    return m_itr != m_end ?
                            *m_itr++ :
                            MOVE_NONE;
                    /* end */

                case Stage::EV_INIT:
                    generate<GenType::EVASION>(vmoves, pos);
                    value<GenType::EVASION>();
                    vm_itr = vmoves.begin();
                    vm_end = vmoves.end();
                    ++stage;
                    /* fall through */
                case Stage::EV_MOVES:
                    return pick([]() { return true; }) ?
                            std::prev(vm_itr)->move :
                            MOVE_NONE;
                    /* end */

                case Stage::PC_CAPTURES:
                    return pick([&]() { return pos.see_ge(vm_itr->move, threshold); }) ?
                            std::prev(vm_itr)->move :
                            MOVE_NONE;
                    /* end */

                case Stage::QS_CAPTURES:
                    if (pick([&]() { return DEP_QS_RECAP < depth
                                         || dst_sq(vm_itr->move) == recap_sq; }))
                    {
                        return std::prev(vm_itr)->move;
                    }
                    // If did not find any move then do not try checks, finished.
                    if (DEP_QS_CHECK > depth)
                    {
                        return MOVE_NONE;
                    }

                    generate<GenType::QUIET_CHECK>(vmoves, pos);
                    vmoves.erase(std::remove_if(vmoves.begin(), vmoves.end(),
                                                [&](const ValMove &vm) { return tt_move == vm.move
                                                                             || !(   (   ENPASSANT != mtype(vm.move)
                                                                                      && !contains(pos.si->king_blockers[pos.active] | pos.square(pos.active|KING), org_sq(vm.move)))
                                                                                  || pos.legal(vm.move)); }),
                                 vmoves.end());
                    vm_itr = vmoves.begin();
                    vm_end = vmoves.end();
                    ++stage;
                    /* fall through */
                case Stage::QS_CHECKS:
                    return vm_itr != vm_end ?
                            (vm_itr++)->move :
                            MOVE_NONE;
                    /* end */

                default:
                    assert(false);
                    break;
                }
                return MOVE_NONE;
            }
        };

        /// Breadcrumbs are used to pair thread and position key
        struct Breadcrumb
        {
            std::atomic<const Thread*> thread;
            std::atomic<Key>           posi_key;

            void store(const Thread *th, Key key)
            {
                thread.store(th, std::memory_order::memory_order_relaxed);
                posi_key.store(key, std::memory_order::memory_order_relaxed);
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

            explicit ThreadMarker(const Thread *thread, Key posi_key, i16 ply)
                : breadcrumb(nullptr)
                , marked(false)
            {
                auto *bc = ply < 8 ?
                            &Breadcrumbs[posi_key & (Breadcrumbs.size() - 1)] :
                            nullptr;
                if (nullptr != bc)
                {
                    // Check if another already marked it, if not, mark it.
                    auto *th = bc->thread.load(std::memory_order::memory_order_relaxed);
                    if (nullptr == th)
                    {
                        bc->store(thread, posi_key);
                        breadcrumb = bc;
                    }
                    else
                    {
                        if (   th != thread
                            && bc->posi_key.load(std::memory_order::memory_order_relaxed) == posi_key)
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
        constexpr Value futility_margin(Depth d, bool imp)
        {
            return Value(217 * (d - imp));
        }
        // Futility move count threshold
        constexpr i16 futility_move_count(Depth d, bool imp)
        {
            return (5 + d * d) * (1 + imp) / 2 - 1;
        }

        Depth reduction(Depth d, u08 mc, bool imp)
        {
            assert(0 <= d);
            auto r = 0 != d
                  && 0 != mc ?
                    Threadpool.factor * std::log(d) * std::log(mc) :
                    0;
            return Depth( (r + 511) / 1024
                        + (!imp && (r > 1007)));
        }

        /// stat_bonus() is the bonus, based on depth
        constexpr i32 stat_bonus(Depth depth)
        {
            return 15 >= depth ?
                    (19 * depth + 155) * depth - 132 :
                    -8;
        }

        // Add a small random component to draw evaluations to keep search dynamic and to avoid 3-fold-blindness.
        Value draw_value()
        {
            return VALUE_DRAW + rand() % 3 - 1;
        }

        /// update_continuation_histories() updates tables of the move pairs with current move.
        void update_continuation_histories(Stack *const &ss, Piece pc, Square dst, i32 bonus)
        {
            for (const auto *const &s : { ss-1, ss-2, ss-4, ss-6 })
            {
                if (_ok(s->played_move))
                {
                    (*s->pd_history)[pc][dst] << bonus;
                }
            }
        }
        /// update_quiet_stats() updates move sorting heuristics when a new quiet best move is found
        void update_quiet_stats(Stack *const &ss, const Position &pos, Move move, i32 bonus)
        {
            if (ss->killer_moves[0] != move)
            {
                ss->killer_moves[1] = ss->killer_moves[0];
                ss->killer_moves[0] = move;
            }
            assert(1 == std::count(ss->killer_moves.begin(), ss->killer_moves.end(), move));

            if (_ok((ss-1)->played_move))
            {
                auto p_dst = dst_sq((ss-1)->played_move);
                assert(NO_PIECE != pos[p_dst]
                    || CASTLE == mtype((ss-1)->played_move));
                pos.thread->move_history[pos[p_dst]][p_dst] = move;
            }

            pos.thread->butterfly_history[pos.active][move_index(move)] << bonus;
            if (PAWN != ptype(pos[org_sq(move)]))
            {
                pos.thread->butterfly_history[pos.active][move_index(reverse_move(move))] << -bonus;
            }

            update_continuation_histories(ss, pos[org_sq(move)], dst_sq(move), bonus);
        }

        /// update_pv() appends the move and child pv
        void update_pv(list<Move> &pv, Move move, const list<Move> &child_pv)
        {
            pv.assign(child_pv.begin(), child_pv.end());
            pv.push_front(move);
            assert(pv.front() == move
                && ((pv.size() == 1 && child_pv.empty())
                 || (pv.back() == child_pv.back() && !child_pv.empty())));
        }

        /// quien_search() is quiescence search function, which is called by the main depth limited search function when the remaining depth <= 0.
        template<bool PVNode>
        Value quien_search(Position &pos, Stack *const &ss, Value alfa, Value beta, Depth depth = DEP_ZERO)
        {
            assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
            assert(PVNode || (alfa == beta-1));
            assert(DEP_ZERO >= depth);

            Value actual_alfa;

            if (PVNode)
            {
                actual_alfa = alfa; // To flag BOUND_EXACT when eval above alpha and no available moves
                ss->pv.clear();
            }

            bool in_check = 0 != pos.si->checkers;

            // Check for maximum ply reached or immediate draw.
            if (   ss->ply >= DEP_MAX
                || pos.draw(ss->ply))
            {
                return ss->ply >= DEP_MAX
                    && !in_check ?
                           evaluate(pos) :
                           VALUE_DRAW;
            }

            assert(ss->ply >= 1
                && ss->ply == (ss-1)->ply + 1
                && ss->ply < DEP_MAX);

            // Transposition table lookup.
            Key key = pos.si->posi_key;
            bool tt_hit;
            auto *tte = TT.probe(key, tt_hit);
            auto tt_move = tt_hit ?
                            tte->move() :
                            MOVE_NONE;
            auto tt_value = tt_hit ?
                            value_of_tt(tte->value(), ss->ply, pos.si->clock_ply) :
                            VALUE_NONE;
            auto tt_pv = tt_hit
                      && tte->is_pv();

            // Decide whether or not to include checks.
            // Fixes also the type of TT entry depth that are going to use.
            // Note that in quien_search use only 2 types of depth: DEP_QS_CHECK or DEP_QS_NO_CHECK.
            Depth qs_depth = in_check
                          || DEP_QS_CHECK <= depth ?
                                DEP_QS_CHECK :
                                DEP_QS_NO_CHECK;

            if (   !PVNode
                && VALUE_NONE != tt_value // Handle tt_hit
                && qs_depth <= tte->depth()
                && BOUND_NONE != (tte->bound() & (tt_value >= beta ? BOUND_LOWER : BOUND_UPPER)))
            {
                return tt_value;
            }

            Value best_value
                , futility_base;

            auto *thread = pos.thread;
            auto best_move = MOVE_NONE;
            StateInfo si;

            // Evaluate the position statically.
            if (in_check)
            {
                ss->static_eval = VALUE_NONE;
                // Starting from the worst case which is checkmate
                best_value = futility_base = -VALUE_INFINITE;
            }
            else
            {
                if (tt_hit)
                {
                    ss->static_eval = best_value = tte->eval();
                    // Never assume anything on values stored in TT.
                    if (VALUE_NONE == best_value)
                    {
                        ss->static_eval = best_value = evaluate(pos);
                    }

                    // Can tt_value be used as a better position evaluation?
                    if (   VALUE_NONE != tt_value
                        && BOUND_NONE != (tte->bound() & (tt_value > best_value ? BOUND_LOWER : BOUND_UPPER)))
                    {
                        best_value = tt_value;
                    }
                }
                else
                {
                    if (MOVE_NULL != (ss-1)->played_move)
                    {
                        ss->static_eval = best_value = evaluate(pos);
                    }
                    else
                    {
                        ss->static_eval = best_value = -(ss-1)->static_eval + 2*Tempo;
                    }
                }

                if (alfa < best_value)
                {
                    // Stand pat. Return immediately if static value is at least beta.
                    if (best_value >= beta)
                    {
                        if (!tt_hit)
                        {
                            tte->save(key,
                                      MOVE_NONE,
                                      value_to_tt(best_value, ss->ply),
                                      ss->static_eval,
                                      DEP_NONE,
                                      BOUND_LOWER,
                                      tt_pv);
                        }

                        assert(-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
                        return best_value;
                    }

                    assert(best_value < beta);
                    // Update alfa! Always alfa < beta
                    if (PVNode)
                    {
                        alfa = best_value;
                    }
                }

                futility_base = best_value + 154;
            }

            if (   MOVE_NONE != tt_move
                && !(   pos.pseudo_legal(tt_move)
                     && pos.legal(tt_move)))
            {
                tt_move = MOVE_NONE;
            }

            Move move;
            u08 move_count = 0;

            const array<const PieceDestinyHistory*, 6> pd_histories
            {
                (ss-1)->pd_history, (ss-2)->pd_history,
                nullptr           , (ss-4)->pd_history,
                nullptr           , (ss-6)->pd_history
            };

            auto recap_sq = _ok((ss-1)->played_move) ?
                                dst_sq((ss-1)->played_move) :
                                SQ_NO;

            // Initialize move picker (2) for the current position
            MovePicker move_picker(pos, tt_move, depth, pd_histories, recap_sq);
            // Loop through the moves until no moves remain or a beta cutoff occurs
            while (MOVE_NONE != (move = move_picker.next_move()))
            {
                assert(pos.pseudo_legal(move)
                    && pos.legal(move));

                ++move_count;

                auto org = org_sq(move);
                auto dst = dst_sq(move);

                auto mpc = pos[org];
                bool gives_check = pos.gives_check(move);
                bool capture_or_promotion = pos.capture_or_promotion(move);

                // Futility pruning
                if (   !in_check
                    && !gives_check
                    && !Threadpool.limit.mate_on()
                    && -VALUE_KNOWN_WIN < futility_base
                    && !pos.pawn_advance_at(pos.active, org))
                {
                    assert(ENPASSANT != mtype(move)); // Due to !pos.pawn_advance_at

                    // Futility pruning parent node
                    auto futility_value = futility_base + PieceValues[EG][CASTLE != mtype(move) ? ptype(pos[dst]) : NONE];
                    if (futility_value <= alfa)
                    {
                        best_value = std::max(futility_value, best_value);
                        continue;
                    }

                    // Prune moves with negative or zero SEE
                    if (   futility_base <= alfa
                        && !pos.see_ge(move, Value(1)))
                    {
                        best_value = std::max(futility_base, best_value);
                        continue;
                    }
                }

                // Pruning: Don't search moves with negative SEE
                if (   (   !in_check
                        // Evasion pruning: Detect non-capture evasions for pruning
                        || (   (DEP_ZERO != depth || 2 < move_count)
                            && -VALUE_MATE_MAX_PLY < best_value
                            && !pos.capture(move)))
                    && !Threadpool.limit.mate_on()
                    && !pos.see_ge(move))
                {
                    continue;
                }

                // Speculative prefetch as early as possible
                prefetch(TT.cluster(pos.posi_move_key(move))->entries);

                // Update the current move.
                ss->played_move = move;
                ss->pd_history = &thread->continuation_history[in_check][capture_or_promotion][mpc][dst];

                // Make the move.
                pos.do_move(move, si, gives_check);

                auto value = -quien_search<PVNode>(pos, ss+1, -beta, -alfa, depth - 1);

                // Undo the move.
                pos.undo_move(move);

                assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Check for new best move.
                if (best_value < value)
                {
                    best_value = value;

                    if (alfa < value)
                    {
                        best_move = move;

                        // Update pv even in fail-high case
                        if (PVNode)
                        {
                            update_pv(ss->pv, move, (ss+1)->pv);
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
            if (   in_check
                && -VALUE_INFINITE == best_value)
            {
                return mated_in(ss->ply); // Plies to mate from the root
            }

            tte->save(key,
                      best_move,
                      value_to_tt(best_value, ss->ply),
                      ss->static_eval,
                      qs_depth,
                      best_value >= beta ?
                          BOUND_LOWER :
                             PVNode
                          && best_value > actual_alfa ?
                              BOUND_EXACT :
                              BOUND_UPPER,
                      tt_pv);

            assert(-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
        }
        /// depth_search() is main depth limited search function, which is called when the remaining depth > 0.
        template<bool PVNode>
        Value depth_search(Position &pos, Stack *const &ss, Value alfa, Value beta, Depth depth, bool cut_node)
        {
            bool root_node = PVNode
                          && 0 == ss->ply;

            // Check if there exists a move which draws by repetition,
            // or an alternative earlier move to this position.
            if (   !root_node
                && alfa < VALUE_DRAW
                && pos.si->clock_ply >= 3
                && pos.cycled(ss->ply))
            {
                alfa = draw_value();
                if (alfa >= beta)
                {
                    return alfa;
                }
            }

            // Dive into quiescence search when the depth reaches zero
            if (DEP_ZERO >= depth)
            {
                return quien_search<PVNode>(pos, ss, alfa, beta);
            }

            assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
            assert(PVNode || (alfa == beta-1));
            assert(!(PVNode && cut_node));
            assert(DEP_ZERO < depth && depth < DEP_MAX);

            // Step 1. Initialize node.
            auto *thread = pos.thread;
            bool in_check = 0 != pos.si->checkers;
            ss->move_count = 0;

            // Check for the available remaining limit.
            if (Threadpool.main_thread() == thread)
            {
                Threadpool.main_thread()->tick();
            }

            if (PVNode)
            {
                // Used to send sel_depth info to GUI (sel_depth from 1, ply from 0)
                thread->sel_depth = std::max(Depth(ss->ply + 1), thread->sel_depth);
            }

            Value value;
            auto best_value = -VALUE_INFINITE;
            auto max_value = +VALUE_INFINITE;

            auto best_move = MOVE_NONE;

            if (!root_node)
            {
                // Step 2. Check for aborted search, maximum ply reached or immediate draw.
                if (   Threadpool.stop.load(std::memory_order::memory_order_relaxed)
                    || ss->ply >= DEP_MAX
                    || pos.draw(ss->ply))
                {
                    return ss->ply >= DEP_MAX
                        && !in_check ?
                               evaluate(pos) :
                               draw_value();
                }

                // Step 3. Mate distance pruning.
                // Even if mate at the next move our score would be at best mates_in(ss->ply+1),
                // but if alfa is already bigger because a shorter mate was found upward in the tree
                // then there is no need to search further, will never beat current alfa.
                // Same logic but with reversed signs applies also in the opposite condition of
                // being mated instead of giving mate, in this case return a fail-high score.
                alfa = std::max(mated_in(ss->ply+0), alfa);
                beta = std::min(mates_in(ss->ply+1), beta);
                if (alfa >= beta)
                {
                    return alfa;
                }
            }

            assert(ss->ply >= 0
                && ss->ply == (ss-1)->ply + 1
                && ss->ply < DEP_MAX);

            assert(MOVE_NONE == (ss+1)->excluded_move);
            (ss+2)->killer_moves.fill(MOVE_NONE);

            // Initialize stats to zero for the grandchildren of the current position.
            // So stats is shared between all grandchildren and only the first grandchild starts with stats = 0.
            // Later grandchildren start with the last calculated stats of the previous grandchild.
            // This influences the reduction rules in LMR which are based on the stats of parent position.
            (ss+2+2*(root_node))->stats = 0;

            // Step 4. Transposition table lookup.
            // Don't want the score of a partial search to overwrite a previous full search
            // TT value, so use a different position key in case of an excluded move.
            Key key = pos.si->posi_key ^ (Key(ss->excluded_move) << 0x10);
            bool tt_hit;
            auto *tte = TT.probe(key, tt_hit);
            auto tt_move = root_node ?
                            thread->root_moves[thread->pv_cur].front() :
                               tt_hit ?
                                tte->move() :
                                MOVE_NONE;
            auto tt_value = tt_hit ?
                            value_of_tt(tte->value(), ss->ply, pos.si->clock_ply) :
                            VALUE_NONE;
            auto tt_pv = PVNode
                      || (   tt_hit
                          && tte->is_pv());

            if (   MOVE_NONE != tt_move
                && !(   pos.pseudo_legal(tt_move)
                     && pos.legal(tt_move)))
            {
                tt_move = MOVE_NONE;
            }

            // thread->tt_hit_avg can be used to approximate the running average of ttHit
            thread->tt_hit_avg = (TTHitAverageWindow - 1) * thread->tt_hit_avg / TTHitAverageWindow
                               + TTHitAverageResolution * tt_hit;

            bool prior_capture_or_promotion = NONE != pos.si->capture
                                           || NONE != pos.si->promote;

            // At non-PV nodes we check for an early TT cutoff.
            if (   !PVNode
                && VALUE_NONE != tt_value // Handle tt_hit
                && depth <= tte->depth()
                && BOUND_NONE != (tte->bound() & (tt_value >= beta ? BOUND_LOWER : BOUND_UPPER)))
            {
                // Update move sorting heuristics on tt_move.
                if (MOVE_NONE != tt_move)
                {
                    if (tt_value >= beta)
                    {
                        // Bonus for a quiet tt_move that fails high.
                        if (!pos.capture_or_promotion(tt_move))
                        {
                            auto bonus = stat_bonus(depth + (!PVNode && tt_pv));
                            update_quiet_stats(ss, pos, tt_move, bonus);
                        }

                        // Extra penalty for early quiet moves in previous ply when it gets refuted.
                        if (   !prior_capture_or_promotion
                            && 2 >= (ss-1)->move_count)
                        {
                            auto bonus = -stat_bonus(depth + 1);
                            update_continuation_histories(ss-1, pos[dst_sq((ss-1)->played_move)], dst_sq((ss-1)->played_move), bonus);
                        }
                    }
                    else
                    // Penalty for a quiet tt_move that fails low.
                    if (!pos.capture_or_promotion(tt_move))
                    {
                        auto bonus = -stat_bonus(depth + (!PVNode && tt_pv));
                        thread->butterfly_history[pos.active][move_index(tt_move)] << bonus;
                        update_continuation_histories(ss, pos[org_sq(tt_move)], dst_sq(tt_move), bonus);
                    }
                }

                if (90 > pos.si->clock_ply)
                {
                    return tt_value;
                }
            }

            // Step 5. Tablebases probe.
            if (   !root_node
                && 0 != TBLimitPiece)
            {
                auto piece_count = pos.count();

                if (   (   piece_count < TBLimitPiece
                        || (   piece_count == TBLimitPiece
                            && depth >= TBProbeDepth))
                    && 0 == pos.si->clock_ply
                    && !pos.si->can_castle(CR_ANY))
                {
                    ProbeState state;
                    auto wdl = probe_wdl(pos, state);

                    // Force check of time on the next occasion
                    if (Threadpool.main_thread() == thread)
                    {
                        Threadpool.main_thread()->check_count = 1;
                    }

                    if (ProbeState::FAILURE != state)
                    {
                        thread->tb_hits.fetch_add(1, std::memory_order::memory_order_relaxed);

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
                                      value_to_tt(value, ss->ply),
                                      VALUE_NONE,
                                      Depth(std::min(depth + 6, DEP_MAX - 1)),
                                      bound,
                                      tt_pv);

                            return value;
                        }

                        if (PVNode)
                        {
                            if (BOUND_LOWER == bound)
                            {
                                best_value = value;
                                alfa = std::max(best_value, alfa);
                            }
                            else
                            {
                                max_value = value;
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
            if (in_check)
            {
                ss->static_eval = eval = VALUE_NONE;
                improving = false;
            }
            else
            {
                if (tt_hit)
                {
                    ss->static_eval = eval = tte->eval();
                    // Never assume anything on values stored in TT.
                    if (VALUE_NONE == eval)
                    {
                        ss->static_eval = eval = evaluate(pos);
                    }

                    if (VALUE_DRAW == eval)
                    {
                        eval = draw_value();
                    }
                    // Can tt_value be used as a better position evaluation?
                    if (   VALUE_NONE != tt_value
                        && BOUND_NONE != (tte->bound() & (tt_value > eval ? BOUND_LOWER : BOUND_UPPER)))
                    {
                        eval = tt_value;
                    }
                }
                else
                {
                    if (MOVE_NULL != (ss-1)->played_move)
                    {
                        ss->static_eval = eval = evaluate(pos) - (ss-1)->stats / 512;
                    }
                    else
                    {
                        ss->static_eval = eval = -(ss-1)->static_eval + 2*Tempo;
                    }

                    tte->save(key,
                              MOVE_NONE,
                              VALUE_NONE,
                              eval,
                              DEP_NONE,
                              BOUND_NONE,
                              tt_pv);
                }

                // Step 7. Razoring. (~1 ELO)
                if (   !root_node // The required RootNode PV handling is not available in qsearch
                    && 2 > depth
                    && eval + RazorMargin <= alfa)
                {
                    return quien_search<PVNode>(pos, ss, alfa, beta);
                }

                improving = VALUE_NONE != (ss-2)->static_eval ?
                                ss->static_eval > (ss-2)->static_eval :
                                VALUE_NONE != (ss-4)->static_eval ?
                                    ss->static_eval > (ss-4)->static_eval :
                                    VALUE_NONE != (ss-6)->static_eval ?
                                        ss->static_eval > (ss-6)->static_eval :
                                        true;

                // Step 8. Futility pruning: child node. (~50 ELO)
                // Betting that the opponent doesn't have a move that will reduce
                // the score by more than futility margins if do a null move.
                if (   !root_node
                    && 6 > depth
                    && !Threadpool.limit.mate_on()
                    && eval < +VALUE_KNOWN_WIN // Don't return unproven wins.
                    && eval - futility_margin(depth, improving) >= beta)
                {
                    return eval;
                }

                // Step 9. Null move search with verification search. (~40 ELO)
                if (   !PVNode
                    && MOVE_NULL != (ss-1)->played_move
                    && MOVE_NONE == ss->excluded_move
                    && !Threadpool.limit.mate_on()
                    && VALUE_ZERO != pos.non_pawn_material(pos.active)
                    && 23397 > (ss-1)->stats
                    && eval >= beta
                    && eval >= ss->static_eval
                    && ss->static_eval >= beta - 32 * depth - 30 * improving + 120 * tt_pv + 292
                    && (   thread->nmp_ply <= ss->ply
                        || thread->nmp_color != pos.active))
                {
                    // Null move dynamic reduction based on depth and static evaluation.
                    auto R = Depth((68 * depth + 854) / 258 + std::min(i32(eval - beta) / 192, 3));

                    ss->played_move = MOVE_NULL;
                    ss->pd_history = &thread->continuation_history[0][0][NO_PIECE][0];

                    pos.do_null_move(si);

                    auto null_value = -depth_search<false>(pos, ss+1, -beta, -beta+1, depth-R, !cut_node);

                    pos.undo_null_move();

                    if (null_value >= beta)
                    {
                        // Skip verification search
                        if (   0 != thread->nmp_ply // Recursive verification is not allowed
                            || (   13 > depth
                                && abs(beta) < +VALUE_KNOWN_WIN))
                        {
                            // Don't return unproven wins
                            return null_value >= +VALUE_MATE_MAX_PLY ? beta : null_value;
                        }

                        // Do verification search at high depths,
                        // with null move pruning disabled for nmp_color until ply exceeds nmp_ply
                        thread->nmp_color = pos.active;
                        thread->nmp_ply = ss->ply + 3 * (depth-R) / 4;
                        value = depth_search<false>(pos, ss, beta-1, beta, depth-R, false);
                        thread->nmp_ply = 0;

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
                    && !Threadpool.limit.mate_on()
                    && abs(beta) < +VALUE_MATE_MAX_PLY)
                {
                    auto raised_beta = std::min(beta + 189 - 45 * improving, +VALUE_INFINITE);
                    u08 pc_move_count = 0;
                    // Initialize move picker (3) for the current position
                    MovePicker move_picker(pos, tt_move, raised_beta - ss->static_eval);
                    // Loop through all legal moves until no moves remain or a beta cutoff occurs
                    while (   pc_move_count < 2 + 2 * cut_node
                           && MOVE_NONE != (move = move_picker.next_move()))
                    {
                        assert(pos.pseudo_legal(move)
                            && pos.legal(move)
                            && pos.capture_or_promotion(move));

                        if (move == ss->excluded_move)
                        {
                            continue;
                        }

                        ++pc_move_count;

                        // Speculative prefetch as early as possible
                        prefetch(TT.cluster(pos.posi_move_key(move))->entries);

                        ss->played_move = move;
                        ss->pd_history = &thread->continuation_history[0][1][pos[org_sq(move)]][dst_sq(move)];

                        pos.do_move(move, si);

                        // Perform a preliminary quien_search to verify that the move holds
                        value = -quien_search<false>(pos, ss+1, -raised_beta, -raised_beta+1);

                        // If the quien_search held perform the regular search
                        if (value >= raised_beta)
                        {
                            value = -depth_search<false>(pos, ss+1, -raised_beta, -raised_beta+1, depth - 4, !cut_node);
                        }

                        pos.undo_move(move);

                        if (value >= raised_beta)
                        {
                            return value;
                        }
                    }
                }
            }

            // Step 11. Internal iterative deepening (IID). (~1 ELO)
            if (   6 < depth
                && MOVE_NONE == tt_move)
            {
                depth_search<PVNode>(pos, ss, alfa, beta, depth - 7, cut_node);

                tte = TT.probe(key, tt_hit);
                tt_move = tt_hit ?
                            tte->move() :
                            MOVE_NONE;
                tt_value = tt_hit ?
                            value_of_tt(tte->value(), ss->ply, pos.si->clock_ply) :
                            VALUE_NONE;

                if (   MOVE_NONE != tt_move
                    && !(   pos.pseudo_legal(tt_move)
                         && pos.legal(tt_move)))
                {
                    tt_move = MOVE_NONE;
                }
            }

            value = best_value;

            u08 move_count = 0;

            // Mark this node as being searched.
            ThreadMarker thread_marker(thread, pos.si->posi_key, ss->ply);

            vector<Move> quiet_moves
                ,        capture_moves;
            quiet_moves.reserve(32);
            capture_moves.reserve(16);
            bool singular_lmr = false;
            bool ttm_capture = MOVE_NONE != tt_move
                            && pos.capture_or_promotion(tt_move);

            const array<const PieceDestinyHistory*, 6> pd_histories
            {
                (ss-1)->pd_history, (ss-2)->pd_history,
                nullptr           , (ss-4)->pd_history,
                nullptr           , (ss-6)->pd_history
            };

            auto counter_move = _ok((ss-1)->played_move) ?
                                pos.thread->move_history[pos[dst_sq((ss-1)->played_move)]][dst_sq((ss-1)->played_move)] :
                                MOVE_NONE;

            // Initialize move picker (1) for the current position
            MovePicker move_picker(pos, tt_move, depth, pd_histories, ss->killer_moves, counter_move);
            // Step 12. Loop through all legal moves until no moves remain or a beta cutoff occurs.
            while (MOVE_NONE != (move = move_picker.next_move()))
            {
                assert(pos.pseudo_legal(move)
                    && pos.legal(move));

                if (   // Skip exclusion move
                       (move == ss->excluded_move)
                       // Skip at root node:
                    || (   root_node
                           // In "searchmoves" mode, skip moves not listed in RootMoves, as a consequence any illegal move is also skipped.
                           // In MultiPV mode we not only skip PV moves which have already been searched and those of lower "TB rank" if we are in a TB root position.
                        && std::find(std::next(thread->root_moves.begin(), thread->pv_cur),
                                     std::next(thread->root_moves.begin(), thread->pv_end), move)
                                  == std::next(thread->root_moves.begin(), thread->pv_end)))
                {
                    continue;
                }

                ss->move_count = ++move_count;

                if (   root_node
                    && Threadpool.main_thread() == thread)
                {
                    auto elapsed_time = Threadpool.main_thread()->time_mgr.elapsed_time();
                    if (elapsed_time > 3000)
                    {
                        sync_cout << setfill('0')
                                  << "info"
                                  << " currmove "       << move
                                  << " currmovenumber " << setw(2) << thread->pv_cur + move_count
                                  //<< " maxmoves "       << thread->root_moves.size()
                                  << " depth "          << depth
                                  //<< " seldepth "       << (*std::find(std::next(thread->root_moves.begin(), thread->pv_cur),
                                  //                                     std::next(thread->root_moves.begin(), thread->pv_end), move)).sel_depth
                                  << " time "           << elapsed_time
                                  << setfill('0') << sync_endl;
                    }
                }

                /*
                // In MultiPV mode also skip moves which will be searched later as PV moves
                if (   root_node
                    //&& thread->pv_cur < Threadpool.pv_count
                    && std::find(std::next(thread->root_moves.begin(), thread->pv_cur + 1),
                                 std::next(thread->root_moves.begin(), Threadpool.pv_count), move)
                              != std::next(thread->root_moves.begin(), Threadpool.pv_count))
                {
                    continue;
                }
                */

                if (PVNode)
                {
                    (ss+1)->pv.clear();
                }

                auto org = org_sq(move);
                auto dst = dst_sq(move);

                auto mpc = pos[org];
                bool gives_check = pos.gives_check(move);
                bool capture_or_promotion = pos.capture_or_promotion(move);

                // Calculate new depth for this move
                auto new_depth = Depth(depth - 1);

                // Step 13. Pruning at shallow depth. (~200 ELO)
                if (   !root_node
                    && -VALUE_MATE_MAX_PLY < best_value
                    && !Threadpool.limit.mate_on()
                    && VALUE_ZERO < pos.non_pawn_material(pos.active))
                {
                    // Skip quiet moves if move count exceeds our futility_move_count() threshold
                    move_picker.skip_quiets = futility_move_count(depth, improving) <= move_count;

                    if (   !capture_or_promotion
                        && !gives_check)
                    {
                        // Reduced depth of the next LMR search.
                        auto lmr_depth = std::max(new_depth - reduction(depth, move_count, improving), 0);
                        // Counter moves based pruning: (~20 ELO)
                        if (   (  4
                                + (   0 < (ss-1)->stats
                                   || 1 == (ss-1)->move_count)) > lmr_depth
                            && (*pd_histories[0])[mpc][dst] < CounterMovePruneThreshold
                            && (*pd_histories[1])[mpc][dst] < CounterMovePruneThreshold)
                        {
                            continue;
                        }
                        // Futility pruning: parent node. (~5 ELO)
                        if (   !in_check
                            && 6 > lmr_depth
                            && ss->static_eval + 172 * lmr_depth + 235 <= alfa
                            && (  thread->butterfly_history[pos.active][move_index(move)]
                                + (*pd_histories[0])[mpc][dst]
                                + (*pd_histories[1])[mpc][dst]
                                + (*pd_histories[3])[mpc][dst]) < 25000)
                        {
                            continue;
                        }
                        // SEE based pruning: negative SEE (~20 ELO)
                        if (!pos.see_ge(move, Value(-(32 - std::min(lmr_depth, 18)) * lmr_depth * lmr_depth)))
                        {
                            continue;
                        }
                    }
                    else
                    // SEE based pruning: negative SEE (~25 ELO)
                    if (!pos.see_ge(move, Value(-194 * depth)))
                    {
                        if (capture_or_promotion)
                        {
                            capture_moves.push_back(move);
                        }
                        continue;
                    }
                }

                // Step 14. Extensions. (~75 ELO)
                auto extension = DEP_ZERO;

                // Singular extension (SE) (~70 ELO)
                // Extend the TT move if its value is much better than its siblings.
                // If all moves but one fail low on a search of (alfa-s, beta-s),
                // and just one fails high on (alfa, beta), then that move is singular and should be extended.
                // To verify this do a reduced search on all the other moves but the tt_move,
                // if result is lower than tt_value minus a margin then extend tt_move.
                if (   !root_node
                    && 5 < depth
                    && move == tt_move
                    && MOVE_NONE == ss->excluded_move // Avoid recursive singular search.
                    && +VALUE_KNOWN_WIN > abs(tt_value) // Handle tt_hit
                    && depth < tte->depth() + 4
                    && BOUND_NONE != (tte->bound() & BOUND_LOWER))
                {
                    auto singular_beta = tt_value - 2 * depth;

                    ss->excluded_move = move;
                    value = depth_search<false>(pos, ss, singular_beta -1, singular_beta, depth/2, cut_node);
                    ss->excluded_move = MOVE_NONE;

                    if (value < singular_beta)
                    {
                        extension = 1;
                        singular_lmr = true;
                    }
                    else
                    // Multi-cut pruning
                    // Our tt_move is assumed to fail high, and now failed high also on a reduced
                    // search without the tt_move. So assume this expected Cut-node is not singular,
                    // multiple moves fail high, and can prune the whole subtree by returning the soft bound.
                    if (singular_beta >= beta)
                    {
                        return singular_beta;
                    }
                }
                else
                if (// Last captures extension
                       (   PieceValues[EG][pos.si->capture] > VALUE_EG_PAWN
                        && pos.non_pawn_material() <= 2 * VALUE_MG_ROOK)
                    // Check extension (~2 ELO)
                    || (   gives_check
                        && (   pos.discovery_check_blocker_at(org)
                            || pos.see_ge(move)))
                    // Passed pawn extension
                    || (   ss->killer_moves[0] == move
                        && pos.pawn_advance_at(pos.active, org)
                        && pos.pawn_passed_at(pos.active, dst)))
                {
                    extension = 1;
                }

                // Castle extension
                if (CASTLE == mtype(move))
                {
                    extension = 1;
                }

                // Add extension to new depth
                new_depth += extension;

                // Speculative prefetch as early as possible
                prefetch(TT.cluster(pos.posi_move_key(move))->entries);

                // Update the current move.
                ss->played_move = move;
                ss->pd_history = &thread->continuation_history[in_check][capture_or_promotion][mpc][dst];

                // Step 15. Make the move.
                pos.do_move(move, si, gives_check);

                bool do_lmr =
                       2 < depth
                    && (  1
                        + root_node
                        + (   root_node
                           && alfa > best_value)) < move_count
                    && (   !root_node
                        // At root if zero best counter
                        || thread->move_best_count(move) == 0)
                    && (   cut_node
                        || !capture_or_promotion
                        || move_picker.skip_quiets
                        || ss->static_eval + PieceValues[EG][pos.si->capture] <= alfa
                        // If ttHit running average is small
                        || thread->tt_hit_avg < 375 * TTHitAverageWindow);

                bool full_search;
                // Step 16. Reduced depth search (LMR, ~200 ELO).
                // If the move fails high will be re-searched at full depth.
                if (do_lmr)
                {
                    auto reduct_depth = reduction(depth, move_count, improving);
                    reduct_depth +=
                        // If other threads are searching this position.
                        +1 * thread_marker.marked
                        // If the ttHit running average is large
                        -1 * (thread->tt_hit_avg > 500 * TTHitAverageWindow)
                        // If opponent's move count is high (~5 ELO)
                        -1 * ((ss-1)->move_count >= 15)
                        // If position is or has been on the PV (~10 ELO)
                        -2 * tt_pv
                        // If move has been singularly extended (~3 ELO)
                        -2 * singular_lmr;

                    if (!capture_or_promotion)
                    {
                        // If TT move is a capture (~5 ELO)
                        reduct_depth += 1 * ttm_capture;

                        // If cut nodes (~10 ELO)
                        if (cut_node)
                        {
                            reduct_depth += 2;
                        }
                        else
                        // If move escapes a capture in no-cut nodes (~2 ELO)
                        if (   NORMAL == mtype(move)
                            && !pos.see_ge(reverse_move(move)))
                        {
                            reduct_depth -= 2 + tt_pv;
                        }

                        ss->stats = thread->butterfly_history[~pos.active][move_index(move)]
                                  + (*pd_histories[0])[mpc][dst]
                                  + (*pd_histories[1])[mpc][dst]
                                  + (*pd_histories[3])[mpc][dst]
                                  - 4926;
                        // Reset stats to zero if negative and most stats shows >= 0
                        if (   0 >  ss->stats
                            && 0 <= (*pd_histories[0])[mpc][dst]
                            && 0 <= (*pd_histories[1])[mpc][dst]
                            && 0 <= thread->butterfly_history[~pos.active][move_index(move)])
                        {
                            ss->stats = 0;
                        }

                        // Decrease/Increase reduction by comparing stats (~10 ELO)
                        if (   (ss-1)->stats >= -116
                            && ss->stats < -154)
                        {
                            reduct_depth += 1;
                        }
                        else
                        if (   ss->stats >= -102
                            && (ss-1)->stats < -114)
                        {
                            reduct_depth -= 1;
                        }

                        // If move with +/-ve stats (~30 ELO)
                        reduct_depth -= Depth(ss->stats / 0x4000);
                    }
                    else
                    // Increase reduction for captures/promotions if late move and at low depth
                    if (   8 > depth
                        && 2 < move_count)
                    {
                        reduct_depth += 1;
                    }

                    reduct_depth = std::max(reduct_depth, DEP_ZERO);
                    auto d = Depth(std::max(new_depth - reduct_depth, 1));
                    assert(d <= new_depth);

                    value = -depth_search<false>(pos, ss+1, -alfa-1, -alfa, d, true);

                    full_search = alfa < value
                               && d < new_depth;
                }
                else
                {
                    full_search = !PVNode
                               || 1 < move_count;
                }

                // Step 17. Full depth search when LMR is skipped or fails high.
                if (full_search)
                {
                    value = -depth_search<false>(pos, ss+1, -alfa-1, -alfa, new_depth, !cut_node);

                    if (   do_lmr
                        && !capture_or_promotion)
                    {
                        int bonus = alfa < value ?
                                        +stat_bonus(new_depth) :
                                        -stat_bonus(new_depth);
                        if (ss->killer_moves[0] == move)
                        {
                            bonus += bonus / 4;
                        }
                        update_continuation_histories(ss, mpc, dst, bonus);
                    }
                }

                // Full PV search.
                if (   PVNode
                    && (   1 == move_count
                        || (   alfa < value
                            && (   root_node
                                || value < beta))))
                {
                    (ss+1)->pv.clear();

                    value = -depth_search<true>(pos, ss+1, -beta, -alfa, new_depth, false);
                }

                // Step 18. Undo move.
                pos.undo_move(move);

                assert(-VALUE_INFINITE < value && value < +VALUE_INFINITE);

                // Step 19. Check for the new best move.
                // Finished searching the move. If a stop or a cutoff occurred,
                // the return value of the search cannot be trusted,
                // and return immediately without updating best move, PV and TT.
                if (Threadpool.stop.load(std::memory_order::memory_order_relaxed))
                {
                    return VALUE_ZERO;
                }

                if (root_node)
                {
                    assert(std::find(thread->root_moves.begin(),
                                     thread->root_moves.end(), move)
                                  != thread->root_moves.end());
                    auto &rm = *std::find(thread->root_moves.begin(), thread->root_moves.end(), move);
                    // First PV move or new best move?
                    if (   1 == move_count
                        || alfa < value)
                    {
                        rm.new_value = value;
                        rm.sel_depth = thread->sel_depth;
                        rm.resize(1);
                        rm.insert (rm.end(), (ss+1)->pv.begin(), (ss+1)->pv.end());

                        // Record how often the best move has been changed in each iteration.
                        // This information is used for time management:
                        // When the best move changes frequently, allocate some more time.
                        if (   1 < move_count
                            && Threadpool.limit.time_mgr_used())
                        {
                            ++thread->pv_change;
                        }
                    }
                    else
                    {
                        // All other moves but the PV are set to the lowest value, this
                        // is not a problem when sorting because sort is stable and move
                        // position in the list is preserved, just the PV is pushed up.
                        rm.new_value = -VALUE_INFINITE;
                    }
                }

                // Step 20. Check best value.
                if (best_value < value)
                {
                    best_value = value;

                    if (alfa < value)
                    {
                        best_move = move;

                        // Update pv even in fail-high case.
                        if (   PVNode
                            && !root_node)
                        {
                            update_pv(ss->pv, move, (ss+1)->pv);
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

                if (move != best_move)
                {
                    if (capture_or_promotion)
                    {
                        capture_moves.push_back(move);
                    }
                    else
                    {
                        quiet_moves.push_back(move);
                    }
                }
            }

            assert(0 != move_count
                || !in_check
                || MOVE_NONE != ss->excluded_move
                || 0 == MoveList<GenType::LEGAL>(pos).size());

            // Step 21. Check for checkmate and stalemate.
            // If all possible moves have been searched and if there are no legal moves,
            // If in a singular extension search then return a fail low score (alfa).
            // Otherwise it must be a checkmate or a stalemate, so return value accordingly.
            if (0 == move_count)
            {
                best_value = MOVE_NONE != ss->excluded_move ?
                                alfa :
                                in_check ?
                                    mated_in(ss->ply) :
                                    VALUE_DRAW;
            }
            else
            // Quiet best move: update move sorting heuristics.
            if (MOVE_NONE != best_move)
            {
                auto bonus1 = stat_bonus(depth + 1);

                if (!pos.capture_or_promotion(best_move))
                {
                    auto bonus2 = (!PVNode && tt_pv)
                                || best_value - VALUE_MG_PAWN > beta ?
                                    bonus1 :
                                    stat_bonus(depth);

                    update_quiet_stats(ss, pos, best_move, bonus2);
                    // Decrease all the other played quiet moves.
                    for (auto qm : quiet_moves)
                    {
                        thread->butterfly_history[pos.active][move_index(qm)] << -bonus2;
                        update_continuation_histories(ss, pos[org_sq(qm)], dst_sq(qm), -bonus2);
                    }
                }
                else
                {
                    thread->capture_history[pos[org_sq(best_move)]][dst_sq(best_move)][pos.cap_type(best_move)] << bonus1;
                }

                // Decrease all the other played capture moves.
                for (auto cm : capture_moves)
                {
                    thread->capture_history[pos[org_sq(cm)]][dst_sq(cm)][pos.cap_type(cm)] << -bonus1;
                }

                // Extra penalty for a quiet TT move or main killer move in previous ply when it gets refuted
                if (   !prior_capture_or_promotion
                    && (   1 == (ss-1)->move_count
                        || (ss-1)->killer_moves[0] == (ss-1)->played_move))
                {
                    update_continuation_histories(ss-1, pos[dst_sq((ss-1)->played_move)], dst_sq((ss-1)->played_move), -bonus1);
                }
            }
            else
            // Bonus for prior quiet move that caused the fail low.
            if (   !prior_capture_or_promotion
                && (   PVNode
                    || 2 < depth))
            {
                auto bonus = stat_bonus(depth);
                update_continuation_histories(ss-1, pos[dst_sq((ss-1)->played_move)], dst_sq((ss-1)->played_move), bonus);
            }

            if (PVNode)
            {
                if (best_value > max_value)
                {
                    best_value = max_value;
                }
            }

            if (MOVE_NONE == ss->excluded_move)
            {
                tte->save(key,
                          best_move,
                          value_to_tt(best_value, ss->ply),
                          ss->static_eval,
                          depth,
                          best_value >= beta ?
                              BOUND_LOWER :
                                 PVNode
                              && MOVE_NONE != best_move ?
                                  BOUND_EXACT :
                                  BOUND_UPPER,
                          tt_pv);
            }

            assert(-VALUE_INFINITE < best_value && best_value < +VALUE_INFINITE);
            return best_value;
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
        Threadpool.main_thread()->wait_while_busy();

        TT.clear();
        Threadpool.clear();
        TBSyzygy::initialize(string(Options["SyzygyPath"])); // Free up mapped files
    }

}

using namespace Searcher;

/// Thread::search() is thread iterative deepening loop function.
/// It calls depth_search() repeatedly with increasing depth until
/// - Force stop requested.
/// - Allocated thinking time has been consumed.
/// - Maximum search depth is reached.
void Thread::search()
{
    tt_hit_avg = (TTHitAverageResolution / 2) * TTHitAverageWindow;

    i16 timed_contempt = 0;
    auto contempt_time = i32(Options["Contempt Time"]);
    if (   0 != contempt_time
        && Threadpool.limit.time_mgr_used())
    {
        i64 diff_time = (i64(Threadpool.limit.clock[ root_pos.active].time)
                       - i64(Threadpool.limit.clock[~root_pos.active].time)) / 1000;
        timed_contempt = i16(diff_time/contempt_time);
    }
    // Basic Contempt
    auto bc = i32(cp_to_value(i16(i32(Options["Fixed Contempt"])) + timed_contempt));
    // In analysis mode, adjust contempt in accordance with user preference
    if (   Threadpool.limit.infinite
        || bool(Options["UCI_AnalyseMode"]))
    {
        bc = Options["Analysis Contempt"] == "Off"                               ? 0 :
             Options["Analysis Contempt"] == "White" && BLACK == root_pos.active ? -bc :
             Options["Analysis Contempt"] == "Black" && WHITE == root_pos.active ? -bc :
             /*Options["Analysis Contempt"] == "Both"                            ? +bc :*/ +bc;
    }

    contempt = WHITE == root_pos.active ?
                +make_score(bc, bc / 2) :
                -make_score(bc, bc / 2);

    auto *main_thread = Threadpool.main_thread() == this ?
                        Threadpool.main_thread() :
                        nullptr;
    if (nullptr != main_thread)
    {
        main_thread->iter_value.fill(main_thread->best_value);
    }

    i16 iter_idx = 0;
    double total_pv_changes = 0.0;

    i16 research_count = 0;

    auto best_value = -VALUE_INFINITE;
    auto window = +VALUE_ZERO;
    auto  alfa = -VALUE_INFINITE
        , beta = +VALUE_INFINITE;

    // To allow access to (ss-7) up to (ss+2), the stack must be over-sized.
    // The former is needed to allow update_continuation_histories(ss-1, ...),
    // which accesses its argument at ss-4, also near the root.
    // The latter is needed for stats and killer initialization.
    Stack stacks[DEP_MAX + 10];
    for (auto ss = stacks; ss < stacks + DEP_MAX + 10; ++ss)
    {
        ss->ply = i16(ss - (stacks+7));
        ss->played_move = MOVE_NONE;
        ss->excluded_move = MOVE_NONE;
        ss->move_count = 0;
        ss->static_eval = VALUE_ZERO;
        ss->stats = 0;
        ss->pd_history = &continuation_history[0][0][NO_PIECE][0];
        ss->killer_moves.fill(MOVE_NONE);
        ss->pv.clear();
    }

    // Iterative deepening loop until requested to stop or the target depth is reached.
    while (   ++root_depth < DEP_MAX
           && !Threadpool.stop
           && (   nullptr == main_thread
               || DEP_ZERO == Threadpool.limit.depth
               || root_depth <= Threadpool.limit.depth))
    {
        if (   nullptr != main_thread
            && Threadpool.limit.time_mgr_used())
        {
            // Age out PV variability metric
            total_pv_changes *= 0.5;
        }

        // Save the last iteration's values before first PV line is searched and
        // all the move scores except the (new) PV are set to -VALUE_INFINITE.
        for (auto &rm : root_moves)
        {
            rm.old_value = rm.new_value;
        }

        pv_beg = 0;
        pv_end = 0;

        // MultiPV loop. Perform a full root search for each PV line.
        for (pv_cur = 0; pv_cur < Threadpool.pv_count && !Threadpool.stop; ++pv_cur)
        {
            if (pv_cur == pv_end)
            {
                pv_beg = pv_end;
                while (++pv_end < root_moves.size())
                {
                    if (root_moves[pv_end].tb_rank != root_moves[pv_beg].tb_rank)
                    {
                        break;
                    }
                }
            }

            // Reset UCI info sel_depth for each depth and each PV line
            sel_depth = DEP_ZERO;

            // Reset aspiration window starting size.
            if (4 <= root_depth)
            {
                auto old_value = root_moves[pv_cur].old_value;
                window = Value(21 + std::abs(old_value) / 256);
                alfa = std::max(old_value - window, -VALUE_INFINITE);
                beta = std::min(old_value + window, +VALUE_INFINITE);

                // Dynamic contempt
                auto dc = bc;
                auto contempt_value = i32(Options["Contempt Value"]);
                if (0 != contempt_value)
                {
                    dc += ((102 - bc / 2) * old_value * 100) / ((abs(old_value) + 157) * contempt_value);
                }
                contempt = WHITE == root_pos.active ?
                            +make_score(dc, dc / 2) :
                            -make_score(dc, dc / 2);
            }

            if (Threadpool.research)
            {
                ++research_count;
            }

            i16 fail_high_count = 0;

            // Start with a small aspiration window and, in case of fail high/low,
            // research with bigger window until not failing high/low anymore.
            do
            {
                auto adjusted_depth = Depth(std::max(root_depth - fail_high_count - research_count, 1));
                best_value = depth_search<true>(root_pos, stacks+7, alfa, beta, adjusted_depth, false);

                // Bring the best move to the front. It is critical that sorting is
                // done with a stable algorithm because all the values but the first
                // and eventually the new best one are set to -VALUE_INFINITE and
                // want to keep the same order for all the moves but the new PV
                // that goes to the front. Note that in case of MultiPV search
                // the already searched PV lines are preserved.
                std::stable_sort(std::next(root_moves.begin(), pv_cur),
                                 std::next(root_moves.begin(), pv_end));

                // If search has been stopped, break immediately.
                // Sorting is safe because RootMoves is still valid, although it refers to the previous iteration.
                if (Threadpool.stop)
                {
                    break;
                }

                // Give some update before to re-search.
                if (   nullptr != main_thread
                    && 1 == Threadpool.pv_count
                    && (best_value <= alfa || beta <= best_value)
                    && main_thread->time_mgr.elapsed_time() > 3000)
                {
                    sync_cout << multipv_info(main_thread, root_depth, alfa, beta) << sync_endl;
                }

                // If fail low set new bounds.
                if (best_value <= alfa)
                {
                    beta = (alfa + beta) / 2;
                    alfa = std::max(best_value - window, -VALUE_INFINITE);

                    fail_high_count = 0;
                    if (nullptr != main_thread)
                    {
                        main_thread->stop_on_ponderhit = false;
                    }
                }
                else
                // If fail high set new bounds.
                if (beta <= best_value)
                {
                    // NOTE:: Don't change alfa = (alfa + beta) / 2
                    beta = std::min(best_value + window, +VALUE_INFINITE);

                    ++fail_high_count;
                }
                // Otherwise exit the loop.
                else
                {
                    //// Research if fail count is not zero
                    //if (0 != fail_high_count)
                    //{
                    //    fail_high_count = 0;
                    //    continue;
                    //}

                    ++root_moves[pv_cur].best_count;
                    break;
                }

                window += window / 4 + 5;

                assert(-VALUE_INFINITE <= alfa && alfa < beta && beta <= +VALUE_INFINITE);
            } while (true);

            // Sort the PV lines searched so far and update the GUI.
            std::stable_sort(std::next(root_moves.begin(), pv_beg),
                             std::next(root_moves.begin(), pv_cur + 1));

            if (   nullptr != main_thread
                && (   Threadpool.stop
                    || Threadpool.pv_count - 1 == pv_cur
                    || main_thread->time_mgr.elapsed_time() > 3000))
            {
                sync_cout << multipv_info(main_thread, root_depth, alfa, beta) << sync_endl;
            }
        }

        if (!Threadpool.stop)
        {
            finished_depth = root_depth;
        }

        // Has any of the threads found a "mate in <x>"?
        if (    Threadpool.limit.mate_on()
            && !Threadpool.limit.time_mgr_used()
            && best_value >= +VALUE_MATE - 2 * Threadpool.limit.mate)
        {
            Threadpool.stop = true;
        }

        if (nullptr != main_thread)
        {
            // If skill level is enabled and can pick move, pick a sub-optimal best move.
            if (   root_depth == main_thread->skill_mgr.level + 1
                && main_thread->skill_mgr.enabled())
            {
                main_thread->skill_mgr.best_move = MOVE_NONE;
                main_thread->skill_mgr.pick_best_move();
            }

            if (   Threadpool.limit.time_mgr_used()
                && !Threadpool.stop
                && !main_thread->stop_on_ponderhit)
            {
                if (main_thread->best_move != root_moves.front().front())
                {
                    main_thread->best_move = root_moves.front().front();
                    main_thread->best_move_depth = root_depth;
                }

                // Reduce time if the best_move is stable over 10 iterations
                // Time Reduction factor
                double time_reduction = 0.91 + 1.03 * (9 < finished_depth - main_thread->best_move_depth);
                // Reduction factor - Use part of the gained time from a previous stable move for the current move
                double reduction = (1.41 + main_thread->time_mgr.time_reduction) / (2.27 * time_reduction);
                // Eval Falling factor
                double eval_falling = ::clamp((332
                                               + 6 * (main_thread->best_value * i32(+VALUE_INFINITE != main_thread->best_value) - best_value)
                                               + 6 * (main_thread->iter_value[iter_idx] * i32(+VALUE_INFINITE != main_thread->iter_value[iter_idx]) - best_value)) / 704.0,
                                              0.50, 1.50);

                total_pv_changes += Threadpool.sum(&Thread::pv_change);
                // Reset pv change
                Threadpool.reset(&Thread::pv_change);

                double pv_instability = 1.00 + total_pv_changes / Threadpool.size();


                auto available_time = TimePoint(main_thread->time_mgr.optimum_time
                                              * reduction
                                              * eval_falling
                                              * pv_instability);
                auto elapsed_time = main_thread->time_mgr.elapsed_time();

                // Stop the search
                // - If all of the available time has been used
                // - If there is less than 2 legal move available
                if (elapsed_time > available_time * i32(2 <= root_moves.size()))
                {
                    // If allowed to ponder do not stop the search now but
                    // keep pondering until GUI sends "stop"/"ponderhit".
                    if (main_thread->ponder)
                    {
                        main_thread->stop_on_ponderhit = true;
                    }
                    else
                    {
                        Threadpool.stop = true;
                    }
                }
                else
                if (   !main_thread->ponder
                    && elapsed_time > available_time * 0.60)
                {
                    Threadpool.research = true;
                }

                main_thread->time_mgr.time_reduction = time_reduction;

                main_thread->iter_value[iter_idx] = best_value;
                iter_idx = (iter_idx + 1) % 4;
            }
            /*
            if (Threadpool.output_stream.is_open())
            {
                Threadpool.output_stream << pretty_pv_info(main_thread) << endl;
            }
            */
        }
    }
}

/// MainThread::search() is main thread search function.
/// It searches from root position and outputs the "bestmove"/"ponder".
void MainThread::search()
{
    assert(Threadpool.main_thread() == this
        && 0 == index);

    time_mgr.start_time = now();
    debug_time = 0;
    /*
    if (!white_spaces(string(Options["Output File"])))
    {
        Threadpool.output_stream.open(string(Options["Output File"]), ios_base::out|ios_base::app);
        if (Threadpool.output_stream.is_open())
        {
            Threadpool.output_stream
                << boolalpha
                << "RootPos  : " << root_pos.fen() << "\n"
                << "MaxMoves : " << root_moves.size() << "\n"
                << "ClockTime: " << Threadpool.limit.clock[root_pos.active].time << " ms\n"
                << "ClockInc : " << Threadpool.limit.clock[root_pos.active].inc << " ms\n"
                << "MovesToGo: " << Threadpool.limit.movestogo+0 << "\n"
                << "MoveTime : " << Threadpool.limit.movetime << " ms\n"
                << "Depth    : " << Threadpool.limit.depth << "\n"
                << "Infinite : " << Threadpool.limit.infinite << "\n"
                << "Ponder   : " << ponder << "\n"
                << " Depth Score    Time       Nodes PV\n"
                << "-----------------------------------------------------------"
                << noboolalpha << endl;
        }
    }
    */

    if (Threadpool.limit.time_mgr_used())
    {
        // Set the time manager before searching.
        time_mgr.set(root_pos.active, root_pos.ply);
    }
    assert(0 <= root_pos.ply);
    TEntry::Generation = u08((root_pos.ply + 1) << 3);

    bool think = true;

    if (root_moves.empty())
    {
        think = false;

        root_moves += MOVE_NONE;

        sync_cout << "info"
                  << " depth " << 0
                  << " score " << to_string(0 != root_pos.si->checkers ? -VALUE_MATE : VALUE_DRAW)
                  << " time "  << 0 << sync_endl;
    }
    else
    {
        if (   !Threadpool.limit.infinite
            && !Threadpool.limit.mate_on()
            && bool(Options["Use Book"]))
        {
            auto book_bm = Book.probe(root_pos, i16(i32(Options["Book Move Num"])), bool(Options["Book Pick Best"]));
            if (MOVE_NONE != book_bm)
            {
                auto rmItr = std::find(root_moves.begin(), root_moves.end(), book_bm);
                if (rmItr != root_moves.end())
                {
                    think = false;
                    std::swap(root_moves.front(), *rmItr);
                    root_moves.front().new_value = VALUE_NONE;
                    StateInfo si;
                    root_pos.do_move(book_bm, si);
                    auto book_pm = Book.probe(root_pos, i16(i32(Options["Book Move Num"])), bool(Options["Book Pick Best"]));
                    if (MOVE_NONE != book_pm)
                    {
                        root_moves.front() += book_pm;
                    }
                    root_pos.undo_move(book_bm);
                }
            }
        }

        if (think)
        {
            if (Threadpool.limit.time_mgr_used())
            {
                best_move = MOVE_NONE;
                best_move_depth = DEP_ZERO;
            }

            skill_mgr.set_level(bool(Options["UCI_LimitStrength"]) ?
                                    ::clamp(i16(std::pow((i32(Options["UCI_Elo"]) - 1346.6) / 143.4, 1.240)), i16(0), MaxLevel) :
                                    i16(i32(Options["Skill Level"])));

            // Have to play with skill handicap?
            // In this case enable MultiPV search by skill pv size
            // that will use behind the scenes to get a set of possible moves.
            Threadpool.pv_count = ::clamp(u32(i32(Options["MultiPV"])), u32(1 + 3 * skill_mgr.enabled()), u32(root_moves.size()));

            set_check_count();

            for (auto *th : Threadpool)
            {
                if (th != this)
                {
                    th->start();
                }
            }

            Thread::search(); // Let's start searching !

            // Swap best PV line with the sub-optimal one if skill level is enabled
            if (skill_mgr.enabled())
            {
                skill_mgr.pick_best_move();
                std::swap(root_moves.front(), *std::find(root_moves.begin(), root_moves.end(), skill_mgr.best_move));
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

    Thread *best_thread = this;
    if (think)
    {
        // Stop the threads if not already stopped (Also raise the stop if "ponderhit" just reset Threads.ponder).
        Threadpool.stop = true;
        // Wait until all threads have finished.
        for (auto *th : Threadpool)
        {
            if (th != this)
            {
                th->wait_while_busy();
            }
        }
        // Check if there is better thread than main thread.
        if (   1 == Threadpool.pv_count
            && DEP_ZERO == Threadpool.limit.depth // Depth limit search don't use deeper thread
            && !skill_mgr.enabled()
            && !bool(Options["UCI_LimitStrength"]))
        {
            assert(MOVE_NONE != root_moves.front().front());

            best_thread = Threadpool.best_thread();

            // If new best thread then send PV info again.
            if (best_thread != this)
            {
                sync_cout << multipv_info(best_thread, best_thread->finished_depth, -VALUE_INFINITE, +VALUE_INFINITE) << sync_endl;
            }
        }
    }

    assert(!best_thread->root_moves.empty()
        && !best_thread->root_moves.front().empty());

    auto &rm = best_thread->root_moves.front();

    if (Threadpool.limit.time_mgr_used())
    {
        // Update the time manager after searching.
        time_mgr.update(root_pos.active);
        best_value = rm.new_value;
    }

    auto bm = rm.front();
    auto pm = MOVE_NONE;
    if (MOVE_NONE != bm)
    {
        auto itr = std::next(rm.begin());
        pm = itr != rm.end() ?
            *itr :
            TT.extract_next_move(root_pos, bm);
        assert(bm != pm);
    }
    /*
    if (Threadpool.output_stream.is_open())
    {
        auto nodes = Threadpool.sum(&Thread::nodes);
        auto elapsed_time = std::max(time_mgr.elapsed_time(), TimePoint(1));

        auto pm_str = move_to_san(MOVE_NONE, root_pos);
        if (   MOVE_NONE != bm
            && MOVE_NONE != pm)
        {
            StateInfo si;
            root_pos.do_move(bm, si);
            pm_str = move_to_san(pm, root_pos);
            root_pos.undo_move(bm);
        }

        Threadpool.output_stream
            << "Nodes      : " << nodes << " N\n"
            << "Time       : " << elapsed_time << " ms\n"
            << "Speed      : " << nodes * 1000 / elapsed_time << " N/s\n"
            << "Hash-full  : " << TT.hash_full() << "\n"
            << "Best Move  : " << move_to_san(bm, root_pos) << "\n"
            << "Ponder Move: " << pm_str << "\n" << endl;
        Threadpool.output_stream.close();
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
/// MainThread::set_check_count()
void MainThread::set_check_count()
{
    // At low node count increase the checking rate otherwise use a default value.
    check_count = 0 != Threadpool.limit.nodes ?
                    ::clamp(Threadpool.limit.nodes / 1024, u64(1), u64(1024)) :
                    u64(1024);
    assert(0 != check_count);
}
/// MainThread::tick() is used as timer function.
/// Used to detect when out of available limit and thus stop the search, also print debug info.
void MainThread::tick()
{
    if (0 < --check_count)
    {
        return;
    }
    set_check_count();

    auto elapsed_time = time_mgr.elapsed_time();

    if (debug_time + 1000 <= elapsed_time)
    {
        debug_time = elapsed_time;

        debug_print();
    }

    // Do not stop until told so by the GUI.
    if (ponder)
    {
        return;
    }

    if (   (   Threadpool.limit.time_mgr_used()
            && (   stop_on_ponderhit
                || time_mgr.maximum_time < elapsed_time + 10))
        || (   0 != Threadpool.limit.movetime
            && Threadpool.limit.movetime <= elapsed_time)
        || (   0 != Threadpool.limit.nodes
            && Threadpool.limit.nodes <= Threadpool.sum(&Thread::nodes)))
    {
        Threadpool.stop = true;
    }
}
