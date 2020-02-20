#include "MovePicker.h"

#include <functional>

using namespace std;

/// Constructors of the MovePicker class.
/// As arguments we pass information to help it to return the (presumably)
/// good moves first, to decide which moves to return
/// and how important good move ordering is at the current node.

/// MovePicker constructor for the main search
MovePicker::MovePicker(
      const Position &p
    , const ColorIndexStatsTable *qStats
    , const PieceSquareTypeStatsTable *cStats
    , const PieceSquareStatsTable **pStats
    , Move ttm, Depth d, const Array<Move, 2> &km, Move cm)
    : pos{ p }
    , quietStats{ qStats }
    , captureStats{ cStats }
    , pieceStats{ pStats }
    , depth{ d }
    , threshold{ Value(-3000 * d) }
    , refutationMoves{ km[0], km[1], cm } {
    assert(DEPTH_ZERO < depth);
    assert(!skipQuiets);

    ttMove = MOVE_NONE != ttm
          && pos.pseudoLegal(ttm) ?
                ttm : MOVE_NONE;
    pickStage = (0 != pos.checkers() ? EVASION_TT : NATURAL_TT)
              + (MOVE_NONE == ttMove);
}

/// MovePicker constructor for quiescence search
/// Because the depth <= DEPTH_ZERO here, only captures, queen promotions
/// and quiet checks (only if depth >= DEPTH_QS_CHECK) will be generated.
MovePicker::MovePicker(
      const Position &p
    , const ColorIndexStatsTable *qStats
    , const PieceSquareTypeStatsTable *cStats
    , const PieceSquareStatsTable **pStats
    , Move ttm, Depth d, Square rs)
    : pos{ p }
    , quietStats{ qStats }
    , captureStats{ cStats }
    , pieceStats{ pStats }
    , depth{ d }
    , recapSq{ rs } {
    assert(DEPTH_ZERO >= depth);
    assert(!skipQuiets);

    ttMove = MOVE_NONE != ttm
          && (DEPTH_QS_RECAP < depth
           || dstSq(ttm) == recapSq)
          && pos.pseudoLegal(ttm) ?
                ttm : MOVE_NONE;
    pickStage = (0 != pos.checkers() ? EVASION_TT : QUIESCENCE_TT)
              + (MOVE_NONE == ttMove);
}

/// MovePicker constructor for ProbCut search.
/// Generate captures with SEE greater than or equal to the given threshold.
MovePicker::MovePicker(
      const Position &p
    , const PieceSquareTypeStatsTable *cStats
    , Move ttm, Value thr)
    : pos{ p }
    , captureStats{ cStats }
    , threshold{ thr } {
    assert(0 == pos.checkers());
    assert(!skipQuiets);

    ttMove = MOVE_NONE != ttm
          && pos.capture(ttm)
          && pos.pseudoLegal(ttm)
          && pos.see(ttm, threshold) ?
                ttm : MOVE_NONE;
    pickStage = PROBCUT_TT
              + (MOVE_NONE == ttMove);
}


/// value() assigns a numerical value to each move in a list, used for sorting.
/// Captures are ordered by Most Valuable Victim (MVV) with using the histories.
/// Quiets are ordered using the histories.
template<GenType GT>
void MovePicker::value() {
    static_assert (GenType::CAPTURE == GT
                || GenType::QUIET == GT
                || GenType::EVASION == GT, "GT incorrect");

    for (auto &vm : vmoves) {
        if (GenType::CAPTURE == GT) {
            assert(pos.captureOrPromotion(vm));
            vm.value = 6 * i32(PieceValues[MG][pos.captureType(vm)])
                     - 1 * pType(pos[orgSq(vm)])
                     +     (*captureStats)[pos[orgSq(vm)]][dstSq(vm)][pos.captureType(vm)];
        }
        else
        if (GenType::QUIET == GT) {
            vm.value =     (*quietStats)[pos.active][mIndex(vm)]
                     + 2 * (*pieceStats[0])[pos[orgSq(vm)]][dstSq(vm)]
                     + 2 * (*pieceStats[1])[pos[orgSq(vm)]][dstSq(vm)]
                     + 2 * (*pieceStats[3])[pos[orgSq(vm)]][dstSq(vm)]
                     + 1 * (*pieceStats[5])[pos[orgSq(vm)]][dstSq(vm)];
            if (vm.value < threshold) {
                vm.value = threshold - 1;
            }
        }
        else { // GenType::EVASION == GT
            if (pos.capture(vm)) {
                vm.value = i32(PieceValues[MG][pos.captureType(vm)])
                         - pType(pos[orgSq(vm)]);
            }
            else {
                vm.value =     (*quietStats)[pos.active][mIndex(vm)]
                         + 1 * (*pieceStats[0])[pos[orgSq(vm)]][dstSq(vm)]
                         - (0x10000000); // 1 << 28
            }
        }
    }
}

/// pick() returns the next move satisfying a predicate function
template<typename Pred>
bool MovePicker::pick(Pred filter) {
    while (vmItr != vmEnd) {
        std::swap(*vmItr, *std::max_element(vmItr, vmEnd));
        assert(ttMove != *vmItr
            && pos.pseudoLegal(*vmItr));

        bool ok{ filter() };

        ++vmItr;
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
    switch (pickStage) {

    case NATURAL_TT:
    case EVASION_TT:
    case PROBCUT_TT:
    case QUIESCENCE_TT:
        ++pickStage;
        assert(MOVE_NONE != ttMove);
        return ttMove;

    case NATURAL_INIT:
    case PROBCUT_INIT:
    case QUIESCENCE_INIT:
        generate<GenType::CAPTURE>(vmoves, pos);
        vmoves.erase(
            std::remove_if(
                vmoves.begin(), vmoves.end(),
                [&](const ValMove &vm) {
                    return ttMove == vm;
                }),
            vmoves.end());
        value<GenType::CAPTURE>();
        vmItr = vmoves.begin();
        vmEnd = vmoves.end();
        ++pickStage;
        // Re-branch at the top of the switch
        goto reStage;

    case NATURAL_GOOD_CAPTURES:
        if (pick([&]() {
                return pos.see(*vmItr, Value(-(vmItr->value) * 55 / 1024)) ?
                    // Put losing capture to badCaptureMoves to be tried later
                        true : (badCaptureMoves.push_back(*vmItr), false);
            })) {
            return *std::prev(vmItr);
        }

        // If the countermove is the same as a killers, skip it
        if (MOVE_NONE != refutationMoves[2]
         && (refutationMoves[2] == refutationMoves[0]
          || refutationMoves[2] == refutationMoves[1])) {
            refutationMoves[2] = MOVE_NONE;
        }
        refutationMoves.erase(
            std::remove_if(
                refutationMoves.begin(), refutationMoves.end(),
                [&](Move m) {
                    return MOVE_NONE == m
                        || ttMove == m
                        || pos.capture(m)
                        || !pos.pseudoLegal(m);
                }),
            refutationMoves.end());
        mItr = refutationMoves.begin();
        mEnd = refutationMoves.end();
        ++pickStage;
        /* fall through */
    case NATURAL_REFUTATIONS:
        // Refutation moves: Killers, Counter moves
        if (mItr != mEnd) {
            return *mItr++;
        }
        mItr = refutationMoves.begin();
        if (!skipQuiets) {
            generate<GenType::QUIET>(vmoves, pos);
            vmoves.erase(
                std::remove_if(
                    vmoves.begin(), vmoves.end(),
                    [&](const ValMove &vm) {
                        return ttMove == vm
                            || std::find(mItr, mEnd, vm.move) != mEnd;
                    }),
                vmoves.end());
            value<GenType::QUIET>();
            std::sort(vmoves.begin(), vmoves.end(), greater<ValMove>());
            vmItr = vmoves.begin();
            vmEnd = vmoves.end();
        }
        ++pickStage;
        /* fall through */
    case NATURAL_QUIETS:
        if (!skipQuiets
         && vmItr != vmEnd) {
            return *vmItr++;
        }

        mItr = badCaptureMoves.begin();
        mEnd = badCaptureMoves.end();
        ++pickStage;
        /* fall through */
    case NATURAL_BAD_CAPTURES:
        return mItr != mEnd ?
                *mItr++ : MOVE_NONE;
        /* end */

    case EVASION_INIT:
        generate<GenType::EVASION>(vmoves, pos);
        vmoves.erase(
            std::remove_if(
                vmoves.begin(), vmoves.end(),
                [&](const ValMove &vm) {
                    return ttMove == vm
                        || (KING == pType(pos[orgSq(vm)])
                         && !pos.pseudoLegal(vm));
                }),
            vmoves.end());
        value<GenType::EVASION>();
        vmItr = vmoves.begin();
        vmEnd = vmoves.end();
        ++pickStage;
        /* fall through */
    case EVASION_MOVES:
        return pick([]() {
                    return true;
                }) ?
                *std::prev(vmItr) : MOVE_NONE;
        /* end */

    case PROBCUT_CAPTURE:
        return pick([&]() {
                    return pos.see(*vmItr, threshold);
                }) ?
                *std::prev(vmItr) : MOVE_NONE;
        /* end */

    case QUIESCENCE_CAPTURES:
        if (pick([&]() {
                return DEPTH_QS_RECAP < depth
                    || dstSq(*vmItr) == recapSq;
            })) {
            return *std::prev(vmItr);
        }
        // If did not find any move then do not try checks, finished.
        if (DEPTH_QS_CHECK > depth) {
            return MOVE_NONE;
        }

        generate<GenType::QUIET_CHECK>(vmoves, pos);
        vmoves.erase(
            std::remove_if(
                vmoves.begin(), vmoves.end(),
                [&](const ValMove &vm) {
                    return ttMove == vm;
                }),
            vmoves.end());
        vmItr = vmoves.begin();
        vmEnd = vmoves.end();
        ++pickStage;
        /* fall through */
    case QUIESCENCE_CHECKS:
        return vmItr != vmEnd ?
                *vmItr++ : MOVE_NONE;
        /* end */
    }
    assert(false);
    return MOVE_NONE;
}
