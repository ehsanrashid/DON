#include "movepicker.h"

#include <functional>

namespace {

    enum Stage : u08 {
        STAGE_NONE = 0,

        NORMAL_TT = 1,
        NORMAL_INIT,
        NORMAL_GOOD_CAPTURES,
        NORMAL_REFUTATIONS,
        NORMAL_QUIETS,
        NORMAL_BAD_CAPTURES,

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

}

/// Constructors of the MovePicker class.
/// As arguments we pass information to help it to return the (presumably)
/// good moves first, to decide which moves to return
/// and how important good move ordering is at the current node.

/// MovePicker constructor for the main search
MovePicker::MovePicker(
    Position const &p,
    ButterFlyStatsTable       const *bfStats,
    PlyIndexStatsTable        const *lpStats,
    PieceSquareTypeStatsTable const *cStats,
    PieceSquareStatsTable     const **pStats,
    Move ttm, Depth d, i16 sp,
    Move const *km, Move cm) :
    pos{ p },
    butterFlyStats{ bfStats },
    lowPlyStats{ lpStats },
    captureStats{ cStats },
    pieceStats{ pStats },
    ttMove { ttm },
    depth{ d },
    ply { sp },
    refutationMoves{ km[0], km[1], cm } {

    assert(ttm == MOVE_NONE
        || pos.pseudoLegal(ttm));
    assert(depth > DEPTH_ZERO);
    assert(pickQuiets);

    stage = (pos.checkers() != 0 ? EVASION_TT : NORMAL_TT)
          + !(ttMove != MOVE_NONE);
}

/// MovePicker constructor for quiescence search
/// Because the depth <= DEPTH_ZERO here, only captures, queen & checking knight promotions
/// and other checks (only if depth >= DEPTH_QS_CHECK) will be generated.
MovePicker::MovePicker(
    Position const &p,
    ButterFlyStatsTable       const *bfStats,
    PieceSquareTypeStatsTable const *cStats,
    PieceSquareStatsTable     const **pStats,
    Move ttm, Depth d, Square rs) :
    pos{ p },
    butterFlyStats{ bfStats },
    captureStats{ cStats },
    pieceStats{ pStats },
    ttMove { ttm },
    depth{ d },
    recapSq{ rs } {

    assert(ttm == MOVE_NONE
        || pos.pseudoLegal(ttm));
    assert(depth <= DEPTH_QS_CHECK);
    assert(pickQuiets);

    stage = (pos.checkers() != 0 ? EVASION_TT : QUIESCENCE_TT)
          + !(ttMove != MOVE_NONE
           && (depth > DEPTH_QS_RECAP
            || dstSq(ttMove) == recapSq));
}

/// MovePicker constructor for ProbCut search.
/// Generate captures with SEE greater than or equal to the given threshold.
MovePicker::MovePicker(
    Position const &p,
    PieceSquareTypeStatsTable const *cStats,
    Move ttm, Depth d, Value thr) :
    pos{ p },
    captureStats{ cStats },
    ttMove { ttm },
    depth{ d },
    threshold{ thr } {

    assert(ttm == MOVE_NONE
        || pos.pseudoLegal(ttm));
    assert(pos.checkers() == 0);
    assert(pickQuiets);

    stage = PROBCUT_TT
          + !(ttMove != MOVE_NONE
           && pos.capture(ttMove)
           && pos.see(ttMove, threshold));
}


/// value() assigns a numerical value to each move, used for sorting.
/// Captures are ordered by Most Valuable Victim (MVV) with using the histories.
/// Quiets are ordered using the histories.
template<GenType GT>
void MovePicker::value() {
    static_assert (GT == CAPTURE
                || GT == QUIET
                || GT == EVASION, "GT incorrect");

    auto vmCur{ vmBeg };
    while (vmCur != vmEnd) {
        auto &vm{ *(vmCur++) };

        if (GT == CAPTURE) {
            auto const captured{ pos.captured(vm) };

            vm.value = i32(PieceValues[MG][captured]) * 6
                     + (*captureStats)[pos[orgSq(vm)]][dstSq(vm)][captured];
        }
        if (GT == QUIET) {
            auto const dst{ dstSq(vm) };
            auto const mp{ pos[orgSq(vm)] };
            auto const mask{ mMask(vm) };

            vm.value = (*butterFlyStats)[pos.activeSide()][mask]
                     + (*pieceStats[0])[mp][dst] * 2
                     + (*pieceStats[1])[mp][dst] * 2
                     + (*pieceStats[3])[mp][dst] * 2
                     + (*pieceStats[5])[mp][dst]
                   + (ply < MAX_LOWPLY ?
                       (*lowPlyStats)[ply][mask] * std::min(depth / 3, 4) : 0);
        }
        if (GT == EVASION) {

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

/// limitedInsertionSort() sorts moves in descending order up to and including a given limit.
/// The order of moves smaller than the limit is left unspecified.
/// Sorts only vmoves [vmBeg, vmEnd]
void MovePicker::limitedInsertionSort(i32 limit) const {

    if (vmBeg == vmEnd) {
        return;
    }

    auto iSortedEnd{ vmBeg };
    auto iUnsortedBeg{ iSortedEnd + 1 };
    while (iUnsortedBeg != vmEnd) {
        if (iUnsortedBeg->value >= limit) {
            auto unSortedItem{ *iUnsortedBeg };
            *iUnsortedBeg = *++iSortedEnd;

            auto iE0{ iSortedEnd };
            while (iE0 != vmBeg) {
                auto iE1{ iE0 - 1 };
                if (iE1->value >= unSortedItem.value) {
                    break;
                }
                *iE0 = *iE1;
                iE0 = iE1;
            }
            *iE0 = unSortedItem;
        }
        ++iUnsortedBeg;
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

        if (filter()) {
            return true;
        }
        ++vmBeg;
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

    case NORMAL_TT:
    case EVASION_TT:
    case PROBCUT_TT:
    case QUIESCENCE_TT: {
        ++stage;
        //assert(ttMove != MOVE_NONE
        //    && pos.pseudoLegal(ttMove));
        return ttMove;
    }

    case NORMAL_INIT:
    case PROBCUT_INIT:
    case QUIESCENCE_INIT: {
        vmoves.clear();
        vmoves.reserve(32);
        generate<CAPTURE>(vmoves, pos);

        vmBeg = vmoves.begin();
        vmEnd = stage == QUIESCENCE_INIT
             && depth <= DEPTH_QS_RECAP ?
                    std::remove_if(vmBeg, vmoves.end(),
                                    [&](ValMove const &vm) {
                                        return vm == ttMove
                                            || dstSq(vm) != recapSq;
                                    }) :
                ttMove != MOVE_NONE ? std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();

        value<CAPTURE>();

        ++stage;
    }
        // Re-branch at the top of the switch
        goto reStage;

    case NORMAL_GOOD_CAPTURES: {
        if (pick([&]() {
                return pos.see(*vmBeg, Value(-69 * vmBeg->value / 1024)) ?
                        // Put losing capture to badCaptureMoves to be tried later
                        true : (badCaptureMoves += *vmBeg, false);
            })) {
            return *vmBeg++;
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
                                        ||!pos.pseudoLegal(m);
                                });

        ++stage;
    }
        [[fallthrough]];
    case NORMAL_REFUTATIONS: {
        // Refutation moves: Killers, Counter moves
        if (mBeg != mEnd) {
            return *mBeg++;
        }

        //assert(vmBeg == vmEnd);
        if (pickQuiets) {
            vmoves.clear();
            if (vmoves.capacity() < 64) vmoves.reserve(64);
            generate<QUIET>(vmoves, pos);
            mBeg = refutationMoves.begin();
            vmBeg = vmoves.begin();
            vmEnd = std::remove_if(vmBeg, vmoves.end(),
                                    [&](ValMove const &vm) {
                                        return vm == ttMove
                                            || std::find(mBeg, mEnd, vm.move) != mEnd;
                                    });
            value<QUIET>();
            limitedInsertionSort(-3000 * depth);
        }
        ++stage;
    }
        [[fallthrough]];
    case NORMAL_QUIETS: {
        if (vmBeg != vmEnd
         && (pickQuiets
          || vmBeg->value == (vmBeg - 1)->value)) {
            //assert(std::find(mBeg, mEnd, (*vmBeg).move) == mEnd);
            return *vmBeg++;
        }

        mBeg = badCaptureMoves.begin();
        mEnd = badCaptureMoves.end();
        ++stage;
    }
        [[fallthrough]];
    case NORMAL_BAD_CAPTURES: {
        return mBeg != mEnd ?
                *mBeg++ : MOVE_NONE;
    }
        /* end */

    case EVASION_INIT: {
        vmoves.reserve(32);
        vmoves.clear();
        generate<EVASION>(vmoves, pos);
        vmBeg = vmoves.begin();
        vmEnd = ttMove != MOVE_NONE ?
                std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();
        value<EVASION>();
        ++stage;
    }
        [[fallthrough]];
    case EVASION_MOVES: {
        return pick([]() { return true; }) ?
                *vmBeg++ : MOVE_NONE;
    }
        /* end */

    case PROBCUT_CAPTURE: {
        return pick([&]() { return pos.see(*vmBeg, threshold); }) ?
                *vmBeg++ : MOVE_NONE;
    }
        /* end */

    case QUIESCENCE_CAPTURES: {
        // No filter required, all done in QUIESCENCE_INIT
        if (pick([]() { return true; })) {
            return *vmBeg++;
        }

        // If did not find any move then do not try checks, finished.
        if (depth < DEPTH_QS_CHECK) {
            return MOVE_NONE;
        }

        vmoves.clear();
        generate<QUIET_CHECK>(vmoves, pos);
        vmBeg = vmoves.begin();
        vmEnd = ttMove != MOVE_NONE ?
                std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();
        ++stage;
    }
        [[fallthrough]];
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
