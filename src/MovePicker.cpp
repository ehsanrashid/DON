#include "MovePicker.h"

#include <functional>

namespace {

    enum Stage : u08 {
        STAGE_NONE = 0,

        NATURAL_TT = 1,
        NATURAL_INIT,
        NATURAL_GOOD_CAPTURES,
        NATURAL_REFUTATIONS,
        NATURAL_QUIETS,
        NATURAL_BAD_CAPTURES,

        EVASION_TT = 8,
        EVASION_INIT,
        EVASION_MOVES,

        PROBCUT_TT = 12,
        PROBCUT_INIT,
        PROBCUT_CAPTURE,

        QUIESCENCE_TT = 16,
        QUIESCENCE_INIT,
        QUIESCENCE_CAPTURES,
        QUIESCENCE_CHECKS,
    };

    /// limitedInsertionSort() sorts moves in descending order up to and including a given limit.
    /// The order of moves smaller than the limit is left unspecified.
    void limitedInsertionSort(
        ValMoves::iterator iBeg,
        ValMoves::iterator iEnd,
        i32 limit) {

        if (iBeg != iEnd) {

            auto sortedEnd{ iBeg };
            auto p{ std::next(sortedEnd) };
            while (p != iEnd) {

                if (p->value >= limit) {
                    auto item{ *p };
                    *p = *(++sortedEnd);

                    auto q{ sortedEnd };
                    while (q != iBeg
                        && std::prev(q)->value < item.value) {
                        *q = *std::prev(q);
                        --q;
                    }
                    *q = item;
                }
                ++p;
            }
        }
    }

}

/// Constructors of the MovePicker class.
/// As arguments we pass information to help it to return the (presumably)
/// good moves first, to decide which moves to return
/// and how important good move ordering is at the current node.

/// MovePicker constructor for the main search
MovePicker::MovePicker(
    Position const &p,
    ColorIndexStatsTable        const *bfStats,
    PlyIndexStatsTable          const *lpStats,
    PieceSquareTypeStatsTable   const *cStats,
    PieceSquareStatsTable       const **pStats,
    Move ttm, Depth d, i16 sp,
    Array<Move, 2> const &km, Move cm) :
    pos{ p },
    butterFlyStats{ bfStats },
    lowPlyStats{ lpStats },
    captureStats{ cStats },
    pieceStats{ pStats },
    depth{ d },
    ply { sp },
    refutationMoves{ km[0], km[1], cm } {
    assert(ttm == MOVE_NONE
        || pos.pseudoLegal(ttm));
    assert(depth > DEPTH_ZERO);
    assert(!skipQuiets);

    ttMove = ttm;
    stage = (pos.checkers() != 0 ? EVASION_TT : NATURAL_TT)
          + (ttMove == MOVE_NONE);
}

/// MovePicker constructor for quiescence search
/// Because the depth <= DEPTH_ZERO here, only captures, queen promotions
/// and quiet checks (only if depth >= DEPTH_QS_CHECK) will be generated.
MovePicker::MovePicker(
    Position const &p,
    ColorIndexStatsTable        const *bfStats,
    PieceSquareTypeStatsTable   const *cStats,
    PieceSquareStatsTable       const **pStats,
    Move ttm, Depth d, Square rs) :
    pos{ p },
    butterFlyStats{ bfStats },
    captureStats{ cStats },
    pieceStats{ pStats },
    depth{ d },
    recapSq{ rs } {
    assert(ttm == MOVE_NONE
        || pos.pseudoLegal(ttm));
    assert(depth <= DEPTH_QS_CHECK);
    assert(!skipQuiets);

    ttMove = ttm != MOVE_NONE
          && (depth > DEPTH_QS_RECAP
           || dstSq(ttm) == recapSq) ?
                ttm : MOVE_NONE;
    stage = (pos.checkers() != 0 ? EVASION_TT : QUIESCENCE_TT)
          + (ttMove == MOVE_NONE);
}

/// MovePicker constructor for ProbCut search.
/// Generate captures with SEE greater than or equal to the given threshold.
MovePicker::MovePicker(
    Position const &p,
    PieceSquareTypeStatsTable   const *cStats,
    Move ttm, Depth d, Value thr) :
    pos{ p },
    captureStats{ cStats },
    depth{ d },
    threshold{ thr } {
    assert(ttm == MOVE_NONE
        || pos.pseudoLegal(ttm));
    assert(pos.checkers() == 0);
    assert(!skipQuiets);

    ttMove = ttm != MOVE_NONE
          && pos.capture(ttm)
          && pos.see(ttm, threshold) ?
                ttm : MOVE_NONE;
    stage = PROBCUT_TT
          + (ttMove == MOVE_NONE);
}


/// value() assigns a numerical value to each move, used for sorting.
/// Captures are ordered by Most Valuable Victim (MVV) with using the histories.
/// Quiets are ordered using the histories.
template<GenType GT>
void MovePicker::value() {
    static_assert (GT == GenType::CAPTURE
                || GT == GenType::QUIET
                || GT == GenType::EVASION, "GT incorrect");

    auto vmCur = vmBeg;
    while (vmCur != vmEnd) {
        auto &vm = *(vmCur++);

        if (GT == GenType::CAPTURE) {
            auto captured{ pos.captured(vm) };

            vm.value = i32(PieceValues[MG][captured]) * 6
                     + (*captureStats)[pos[orgSq(vm)]][dstSq(vm)][captured];
        }
        if (GT == GenType::QUIET) {
            auto dst{ dstSq(vm) };
            auto mp{ pos[orgSq(vm)] };
            auto mask{ mMask(vm) };

            vm.value = (*butterFlyStats)[pos.activeSide()][mask]
                     + (*pieceStats[0])[mp][dst] * 2
                     + (*pieceStats[1])[mp][dst] * 2
                     + (*pieceStats[3])[mp][dst] * 2
                     + (*pieceStats[5])[mp][dst]
                   + (ply < MAX_LOWPLY ?
                       (*lowPlyStats)[ply][mask] * 4 : 0);
        }
        if (GT == GenType::EVASION) {

            vm.value =
                pos.capture(vm) ?

                    i32(PieceValues[MG][pos.captured(vm)])
                  - pType(pos[orgSq(vm)]) :

                    (*butterFlyStats)[pos.activeSide()][mMask(vm)]
                  + (*pieceStats[0])[pos[orgSq(vm)]][dstSq(vm)]
                  - 0x10000000; // 1 << 28
        }
    }
}

/// pick() returns the next move satisfying a predicate function
template<typename Pred>
bool MovePicker::pick(Pred filter) {
    while (vmBeg != vmEnd) {
        std::swap(*vmBeg, *std::max_element(vmBeg, vmEnd));
        assert(*vmBeg != MOVE_NONE
            && *vmBeg != ttMove
            && (pos.checkers() != 0
             || pos.pseudoLegal(*vmBeg)));

        bool ok{ filter() };

        ++vmBeg;
        if (ok) return true;
    }
    return false;
}

/// nextMove() is the most important method of the MovePicker class.
/// It returns a new legal move every time it is called, until there are no more moves left.
/// It picks the move with the biggest value from a list of generated moves
/// taking care not to return the ttMove if it has already been searched.
Move MovePicker::nextMove() {
    reStage:
    switch (stage) {

    case NATURAL_TT:
    case EVASION_TT:
    case PROBCUT_TT:
    case QUIESCENCE_TT: {
        ++stage;
        //assert(ttMove != MOVE_NONE
        //    && pos.pseudoLegal(ttMove));
        return ttMove;
    }

    case NATURAL_INIT:
    case PROBCUT_INIT:
    case QUIESCENCE_INIT: {
        vmoves.reserve(32);
        vmoves.clear();
        generate<GenType::CAPTURE>(vmoves, pos);

        vmBeg = vmoves.begin();
        vmEnd = stage == QUIESCENCE_INIT
             && depth == DEPTH_QS_RECAP ?
                std::remove_if(vmBeg, vmoves.end(),
                    [&](ValMove const &vm) {
                        return vm == ttMove
                            || dstSq(vm) != recapSq;
                    }) :
                ttMove != MOVE_NONE ?
                    std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();

        value<GenType::CAPTURE>();

        ++stage;
    }
        // Re-branch at the top of the switch
        goto reStage;

    case NATURAL_GOOD_CAPTURES: {
        if (pick([&]() {
                return pos.see(*vmBeg, Value(-55 * vmBeg->value / 1024)) ?
                        // Put losing capture to badCaptureMoves to be tried later
                        true : (badCaptureMoves += *vmBeg, false);
            })) {
            return *std::prev(vmBeg);
        }

        // If the countermove is the same as a killers, skip it
        if (refutationMoves[2] != MOVE_NONE
         && (refutationMoves[2] == refutationMoves[0]
          || refutationMoves[2] == refutationMoves[1])) {
            refutationMoves[2] = MOVE_NONE;
        }
        mBeg = refutationMoves.begin();
        mEnd = std::remove_if(mBeg, refutationMoves.end(),
                [&](Move m) {
                    return m == MOVE_NONE
                        || m == ttMove
                        || pos.capture(m)
                        || !pos.pseudoLegal(m);
                });

        ++stage;
    }
        /* fall through */
    case NATURAL_REFUTATIONS: {
        // Refutation moves: Killers, Counter moves
        if (mBeg != mEnd) {
            return *mBeg++;
        }

        if (!skipQuiets) {
            vmoves.reserve(64);
            vmoves.clear();
            generate<GenType::QUIET>(vmoves, pos);
            mBeg = refutationMoves.begin();
            vmBeg = vmoves.begin();
            vmEnd = std::remove_if(vmBeg, vmoves.end(),
                    [&](ValMove const &vm) {
                        return vm == ttMove
                            || std::find(mBeg, mEnd, vm.move) != mEnd;
                    });
            value<GenType::QUIET>();
            limitedInsertionSort(vmBeg, vmEnd, -3000 * depth);
        }
        ++stage;
    }
        /* fall through */
    case NATURAL_QUIETS: {
        if (!skipQuiets
         && vmBeg != vmEnd) {
            //assert(std::find(mBeg, mEnd, (*vmBeg).move) == mEnd);
            return *vmBeg++;
        }

        mBeg = badCaptureMoves.begin();
        mEnd = badCaptureMoves.end();
        ++stage;
    }
        /* fall through */
    case NATURAL_BAD_CAPTURES: {
        return mBeg != mEnd ?
                *mBeg++ : MOVE_NONE;
    }
        /* end */

    case EVASION_INIT: {
        vmoves.reserve(32);
        vmoves.clear();
        generate<GenType::EVASION>(vmoves, pos);
        vmBeg = vmoves.begin();
        vmEnd = ttMove != MOVE_NONE ?
                std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();
        value<GenType::EVASION>();
        ++stage;
    }
        /* fall through */
    case EVASION_MOVES: {
        return pick([]() {
                    return true;
                }) ?
                *std::prev(vmBeg) : MOVE_NONE;
    }
        /* end */

    case PROBCUT_CAPTURE: {
        return pick([&]() {
                    return pos.see(*vmBeg, threshold);
                }) ?
                *std::prev(vmBeg) : MOVE_NONE;
    }
        /* end */

    case QUIESCENCE_CAPTURES: {
        if (pick([&]() {
                return true; // No filter required, all done in QUIESCENCE_INIT
            })) {
            return *std::prev(vmBeg);
        }

        // If did not find any move then do not try checks, finished.
        if (depth < DEPTH_QS_CHECK) {
            return MOVE_NONE;
        }

        vmoves.clear();
        generate<GenType::QUIET_CHECK>(vmoves, pos);
        vmBeg = vmoves.begin();
        vmEnd = ttMove != MOVE_NONE ?
                std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();
        ++stage;
    }
        /* fall through */
    case QUIESCENCE_CHECKS: {
        return vmBeg != vmEnd ?
                *vmBeg++ : MOVE_NONE;
    }
        /* end */

    case STAGE_NONE:
        return MOVE_NONE;
    default:
        assert(false);
        return MOVE_NONE;
    }
}
