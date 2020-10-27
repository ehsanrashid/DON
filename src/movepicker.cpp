#include "movepicker.h"

namespace {

    enum Stage : uint8_t {
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

    /// sortPartial() sorts (insertion) item in descending order up to and including a given limit.
    /// The order of item smaller than the limit is left unspecified.
    /// Sorts only in range [beg, end]
    void sortPartial(ValMoves::iterator beg, ValMoves::iterator end, int32_t limit) {

        if (beg == end) {
            return;
        }

        auto sortedEnd{ beg };
        auto unsortedBeg{ sortedEnd + 1 };
        while (unsortedBeg != end) {
            if (unsortedBeg->value >= limit) {
                auto unsortedItem{ *unsortedBeg };
                *unsortedBeg = *++sortedEnd;

                auto itr{ sortedEnd };
                while (itr != beg) {
                    if ((itr - 1)->value >= unsortedItem.value) {
                        break;
                    }
                    *itr = *(itr - 1);
                    --itr;
                }
                *itr = unsortedItem;
            }
            ++unsortedBeg;
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
    ButterFlyStatsTable       const *bfStats,
    PlyIndexStatsTable        const *lpStats,
    PieceSquareTypeStatsTable const *cStats,
    PieceSquareStatsTable     const **pStats,
    int16_t sp, Move const *km, Move cm) noexcept :
    pickQuiets{ true },
    pos{ p },
    ttMove{ ttm },
    depth{ d },
    butterFlyStats{ bfStats },
    lowPlyStats{ lpStats },
    captureStats{ cStats },
    pieceStats{ pStats },
    ply{ sp },
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
    Move ttm, Depth d,
    ButterFlyStatsTable       const *bfStats,
    PieceSquareTypeStatsTable const *cStats,
    PieceSquareStatsTable     const **pStats,
    Square rs) noexcept :
    pos{ p },
    ttMove{ ttm },
    depth{ d },
    butterFlyStats{ bfStats },
    captureStats{ cStats },
    pieceStats{ pStats },
    recapSq{ rs } {

    assert(ttm == MOVE_NONE
        || pos.pseudoLegal(ttm));
    assert(depth <= DEPTH_QS_CHECK);

    stage = (pos.checkers() != 0 ? EVASION_TT : QUIESCENCE_TT)
          + !(ttMove != MOVE_NONE
           && (depth > DEPTH_QS_RECAP
            || dstSq(ttMove) == recapSq));
}

/// MovePicker constructor for ProbCut search.
/// Generate captures with SEE greater than or equal to the given threshold.
MovePicker::MovePicker(
    Position const &p,
    Move ttm, Value thr,
    PieceSquareTypeStatsTable const *cStats) noexcept :
    pos{ p },
    ttMove{ ttm },
    depth{ DEPTH_ZERO },
    captureStats{ cStats },
    threshold{ thr } {

    assert(ttm == MOVE_NONE
        || pos.pseudoLegal(ttm));
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
void MovePicker::value() {
    static_assert (GT == CAPTURE
                || GT == QUIET
                || GT == EVASION, "GT incorrect");

    auto vmCur{ vmBeg };
    while (vmCur != vmEnd) {
        auto &vm{ *(vmCur++) };

        switch (GT) {
        case CAPTURE: {
            vm.value = int32_t(PieceValues[MG][pos.captured(vm)]) * 6
                     + (*captureStats)[pos.movedPiece(vm)][dstSq(vm)][pos.captured(vm)];
        }
            break;
        case QUIET: {
            vm.value = (*butterFlyStats)[pos.activeSide()][mMask(vm)]
                     + (*pieceStats[0])[pos.movedPiece(vm)][dstSq(vm)] * 2
                     + (*pieceStats[1])[pos.movedPiece(vm)][dstSq(vm)] * 2
                     + (*pieceStats[3])[pos.movedPiece(vm)][dstSq(vm)] * 2
                     + (*pieceStats[5])[pos.movedPiece(vm)][dstSq(vm)]
                     + (ply < MAX_LOWPLY ? (*lowPlyStats)[ply][mMask(vm)] * std::min(depth / 3, 4) : 0);
        }
            break;
        case EVASION: {
            if (pos.capture(vm)) {
                vm.value = int32_t(PieceValues[MG][pos.captured(vm)])
                         - pType(pos.movedPiece(vm));
            }
            else {
                vm.value = (*butterFlyStats)[pos.activeSide()][mMask(vm)]
                         + (*pieceStats[0])[pos.movedPiece(vm)][dstSq(vm)]
                         - 0x10000000; // 1 << 28
            }
        }
            break;
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
        vmEnd = ttMove != MOVE_NONE ?
                    std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();

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

        if (pickQuiets) {
            vmoves.clear();
            //vmoves.reserve(48);
            generate<QUIET>(vmoves, pos);
            vmBeg = vmoves.begin();
            vmEnd = std::remove_if(vmBeg, vmoves.end(),
                                    [&](ValMove const &vm) {
                                        return vm == ttMove
                                            || refutationMoves.contains(vm.move);
                                    });
            value<QUIET>();
            sortPartial(vmBeg, vmEnd, -3000 * depth);
        }
        ++stage;
    }
        [[fallthrough]];
    case NORMAL_QUIETS: {
        if ((pickQuiets /*|| vmBeg->value == (vmBeg - 1)->value*/)
         && vmBeg != vmEnd) {
            assert(pos.pseudoLegal(*vmBeg));
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
        vmoves.clear();
        vmoves.reserve(32);
        generate<EVASION>(vmoves, pos);
        vmBeg = vmoves.begin();
        vmEnd = ttMove != MOVE_NONE ?
                    std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();
        value<EVASION>();
        ++stage;
    }
        [[fallthrough]];
    case EVASION_MOVES: {
        if (pick([&]() { return pos.checkers() == 0
                             || pos.pseudoLegal(*vmBeg); })) {
            assert(pos.pseudoLegal(*vmBeg));
            return *vmBeg++;
        }
        return MOVE_NONE;
    }
        /* end */

    case PROBCUT_CAPTURE: {
        return pick([&]() { return pos.see(*vmBeg, threshold); }) ?
                *vmBeg++ : MOVE_NONE;
    }
        /* end */

    case QUIESCENCE_CAPTURES: {
        if (pick([&]() { return depth > DEPTH_QS_RECAP
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
        vmEnd = ttMove != MOVE_NONE ?
                    std::remove(vmBeg, vmoves.end(), ttMove) : vmoves.end();
        ++stage;
    }
        [[fallthrough]];
    case QUIESCENCE_CHECKS: {
        if (vmBeg != vmEnd) {
            assert(pos.pseudoLegal(*vmBeg));
            return *vmBeg++;
        }
        return MOVE_NONE;
    }
        /* end */

    case STAGE_NONE:
        return MOVE_NONE;
    default:
        assert(false);
        return MOVE_NONE;
    }
}
