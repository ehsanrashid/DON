#include "movepicker.h"

namespace {

    enum Stage : uint8_t {
        STAGE_NONE,

        NORMAL_TT,
        NORMAL_INIT,
        NORMAL_GOOD_CAPTURES,
        NORMAL_REFUTATIONS,
        NORMAL_QUIETS,
        NORMAL_BAD_CAPTURES,

        EVASION_TT,
        EVASION_INIT,
        EVASION_MOVES,

        PROBCUT_TT,
        PROBCUT_INIT,
        PROBCUT_CAPTURE,

        QUIESCENCE_TT,
        QUIESCENCE_INIT,
        QUIESCENCE_CAPTURES,
        QUIESCENCE_CHECKS,
    };

    /// partialSort() sorts (insertion) item in descending order up to and including a given limit.
    /// The order of item smaller than the limit is left unspecified.
    /// Sorts only in range [beg, end]
    void partialSort(ValMoves::iterator beg, ValMoves::iterator end, int32_t limit) noexcept {

        auto sortedEnd{ beg };
        for (auto unsortedBeg{ sortedEnd + 1 }; unsortedBeg < end; ++unsortedBeg) {
            if (unsortedBeg->value >= limit) {
                auto unsortedItem{ *unsortedBeg };
                *unsortedBeg = *++sortedEnd;

                auto itr{ sortedEnd };
                for (; itr != beg && *(itr-1) < unsortedItem; --itr) {
                    *itr = *(itr-1);
                }
                *itr = unsortedItem;
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
    Move ttm, Depth d,
    ButterFlyStatsTable       const *dStats,
    ButterFlyStatsTable       const *sStats,
    PlyIndexStatsTable        const *lpStats,
    PieceSquareTypeStatsTable const *cpStats,
    PieceSquareStatsTable     const **cStats,
    int16_t sp, Move const *km, Move cm) noexcept :
    pickQuiets{ true },
    pos{ p },
    ttMove{ ttm },
    depth{ d },
    dynamicStats{ dStats },
    staticStats{ sStats },
    lowPlyStats{ lpStats },
    captureStats{ cpStats },
    contStats{ cStats },
    ply{ sp },
    refutationMoves{ km[0], km[1], cm } {

    assert(pickQuiets);
    assert(ttMove == MOVE_NONE
        || pos.pseudoLegal(ttMove));
    assert(depth > DEPTH_ZERO);

    if (pos.checkers() != 0) {
        stage = EVASION_TT
              + !(ttMove != MOVE_NONE);
    } else {
        stage = NORMAL_TT
              + !(ttMove != MOVE_NONE);
    }
}

/// MovePicker constructor for quiescence search
/// Because the depth <= DEPTH_ZERO here, only captures, queen & checking knight promotions
/// and other checks (only if depth >= DEPTH_QS_CHECK) will be generated.
MovePicker::MovePicker(
    Position const &p,
    Move ttm, Depth d,
    ButterFlyStatsTable       const *dStats,
    ButterFlyStatsTable       const *sStats,
    PieceSquareTypeStatsTable const *cpStats,
    PieceSquareStatsTable     const **cStats,
    Square rs) noexcept :
    pickQuiets{ false },
    pos{ p },
    ttMove{ ttm },
    depth{ d },
    dynamicStats{ dStats },
    staticStats{ sStats },
    captureStats{ cpStats },
    contStats{ cStats },
    recapSq{ rs } {

    assert(ttMove == MOVE_NONE
        || pos.pseudoLegal(ttMove));
    assert(depth <= DEPTH_QS_CHECK);

    if (pos.checkers() != 0) {
        stage = EVASION_TT
              + !(ttMove != MOVE_NONE);
    } else {
        stage = QUIESCENCE_TT
              + !(ttMove != MOVE_NONE
               && (depth > DEPTH_QS_RECAP
                || dstSq(ttMove) == recapSq));
    }
}

/// MovePicker constructor for ProbCut search.
/// Generate captures with SEE greater than or equal to the given threshold.
MovePicker::MovePicker(
    Position const &p,
    Move ttm, Value thr,
    PieceSquareTypeStatsTable const *cpStats) noexcept :
    pickQuiets{ false },
    pos{ p },
    ttMove{ ttm },
    depth{ DEPTH_ZERO },
    captureStats{ cpStats },
    threshold{ thr } {

    assert(ttMove == MOVE_NONE
        || pos.pseudoLegal(ttMove));
    assert(pos.checkers() == 0);

    stage = PROBCUT_TT
          + !(ttMove != MOVE_NONE
           && pos.capture(ttMove)
           && pos.see(ttMove, threshold));
}


/// value() assigns a numerical value to each move, used for sorting.
/// Captures are ordered by Most Valuable Victim (MVV) with using the histories.
/// Quiets are ordered using the histories.
template<GenType GT>
void MovePicker::value() noexcept {
    static_assert(GT == CAPTURE
                || GT == QUIET
                || GT == EVASION, "GT incorrect");

    for (auto vm{ vmBeg }; vm < vmEnd; ++vm) {

        switch (GT) {

        case CAPTURE: {
            vm->value = int32_t(PieceValues[MG][pos.captured(*vm)]) * 6
                      + (*captureStats)[pos.movedPiece(*vm)][dstSq(*vm)][pos.captured(*vm)];
        }
            break;
        case QUIET: {
            vm->value = (*dynamicStats)[pos.activeSide()][mMask(*vm)]
                      + (*staticStats)[pos.activeSide()][mMask(*vm)]
                      + (*contStats[0])[pos.movedPiece(*vm)][dstSq(*vm)] * 2
                      + (*contStats[1])[pos.movedPiece(*vm)][dstSq(*vm)] * 2
                      + (*contStats[3])[pos.movedPiece(*vm)][dstSq(*vm)] * 2
                      + (*contStats[5])[pos.movedPiece(*vm)][dstSq(*vm)]
                      + (ply < MAX_LOWPLY ? (*lowPlyStats)[ply][mMask(*vm)] * std::min(depth / 3, 4) : 0);
        }
            break;
        case EVASION: {
            if (pos.capture(*vm)) {
                vm->value = int32_t(PieceValues[MG][pos.captured(*vm)])
                         - pType(pos.movedPiece(*vm));
            } else {
                vm->value = (*dynamicStats)[pos.activeSide()][mMask(*vm)]
                          + (*contStats[0])[pos.movedPiece(*vm)][dstSq(*vm)]
                          - 0x10000000; // 1 << 28
            }
        }
            break;
        }
    }
}

/// pick() returns the next move satisfying a predicate function
template<typename Pred>
bool MovePicker::pick(Pred filter) noexcept {
    while (vmBeg < vmEnd) {
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
        vmEnd = vmoves.end();
        if (ttMove != MOVE_NONE
         && vmBeg < vmEnd) {
            vmEnd = std::remove(vmBeg, vmEnd, ttMove);
        }
        value<CAPTURE>();

        ++stage;
    }
        // Re-branch at the top of the switch
        goto reStage;

    case NORMAL_GOOD_CAPTURES: {
        if (pick([&]() noexcept {
                return pos.see(*vmBeg, Value(-69 * vmBeg->value / 1024)) ?
                        // Put losing capture to badCaptureMoves to be tried later
                        true : (badCaptureMoves += *vmBeg, false);
            })) {
            assert(pos.pseudoLegal(*vmBeg));
            return *vmBeg++;
        }

        // If the countermove is the same as a killers, skip it
        if ( refutationMoves[2] != MOVE_NONE
         && (refutationMoves[2] == refutationMoves[0]
          || refutationMoves[2] == refutationMoves[1])) {
            refutationMoves[2] = MOVE_NONE;
        }
        mBeg = refutationMoves.begin();
        mEnd = refutationMoves.end();
        mEnd = std::remove_if(mBeg, mEnd,
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
        if (mBeg < mEnd) {
            return *mBeg++;
        }

        if (pickQuiets) {
            vmoves.clear();
            //vmoves.reserve(48);
            generate<QUIET>(vmoves, pos);
            vmBeg = vmoves.begin();
            vmEnd = vmoves.end();
            vmEnd = std::remove_if(vmBeg, vmEnd,
                                    [&](ValMove const &vm) {
                                        return vm == ttMove
                                            || refutationMoves.contains(vm);
                                    });
            value<QUIET>();
            partialSort(vmBeg, vmEnd, -3000 * depth);
        }
        ++stage;
    }
        [[fallthrough]];
    case NORMAL_QUIETS: {
        if ((pickQuiets /*|| vmBeg->value == (vmBeg - 1)->value*/)
         && vmBeg < vmEnd) {
            assert(pos.pseudoLegal(*vmBeg));
            return *vmBeg++;
        }

        mBeg = badCaptureMoves.begin();
        mEnd = badCaptureMoves.end();
        assert(std::find(mBeg, mEnd, ttMove) == mEnd);

        ++stage;
    }
        [[fallthrough]];
    case NORMAL_BAD_CAPTURES: {
        return mBeg < mEnd ?
                *mBeg++ : MOVE_NONE;
    }
        /* end */

    case EVASION_INIT: {
        vmoves.clear();
        vmoves.reserve(32);
        generate<EVASION>(vmoves, pos);
        vmBeg = vmoves.begin();
        vmEnd = vmoves.end();
        vmEnd = std::remove_if(vmBeg, vmEnd,
                                    [&](ValMove const &vm) {
                                        return vm == ttMove
                                            || (pos.checkers() != 0
                                            && !pos.pseudoLegal(vm));
                                    });
        value<EVASION>();

        ++stage;
    }
        [[fallthrough]];
    case EVASION_MOVES: {
        return pick([&]() noexcept { return true; }) ?
                *vmBeg++ : MOVE_NONE;
    }
        /* end */

    case PROBCUT_CAPTURE: {
        return pick([&]() noexcept { return pos.see(*vmBeg, threshold); }) ?
                *vmBeg++ : MOVE_NONE;
    }
        /* end */

    case QUIESCENCE_CAPTURES: {
        if (pick([&]() noexcept { return depth > DEPTH_QS_RECAP
                                      || dstSq(*vmBeg) == recapSq; })) {
            assert(pos.pseudoLegal(*vmBeg));
            return *vmBeg++;
        }

        // If did not find any move then do not try checks, finished.
        if (depth != DEPTH_QS_CHECK) {
            return MOVE_NONE;
        }

        vmoves.clear();
        generate<QUIET_CHECK>(vmoves, pos);
        vmBeg = vmoves.begin();
        vmEnd = vmoves.end();
        if (ttMove != MOVE_NONE
         && vmBeg < vmEnd) {
            vmEnd = std::remove(vmBeg, vmEnd, ttMove);
        }

        ++stage;
    }
        [[fallthrough]];
    case QUIESCENCE_CHECKS: {
        return vmBeg < vmEnd ?
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
