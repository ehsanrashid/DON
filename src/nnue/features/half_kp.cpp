// Definition of input features HalfKP of NNUE evaluation function

#include "../../position.h"
#include "half_kp.h"
#include "index_list.h"

namespace Evaluator::NNUE::Features {

    // Get a list of indices for active features
    template<Side AssociatedKing>
    void HalfKP<AssociatedKing>::appendActiveIndices(Position const &pos, Color perspective, IndexList *activeList) {

        Square const kSq{ orient(perspective, pos.square(perspective|KING)) };
        Bitboard bb{ pos.pieces() & ~pos.pieces(KING) };
        while (bb != 0) {
            auto const s{ popLSq(bb) };
            activeList->push_back(makeIndex(perspective, s, pos[s], kSq));
        }
    }

    // Get a list of indices for recently changed features
    template<Side AssociatedKing>
    void HalfKP<AssociatedKing>::appendChangedIndices(Position const &pos, MoveInfo const &mi, Color perspective, IndexList *removedList, IndexList *addedList) {

        Square const kSq{ orient(perspective, pos.square(perspective|KING)) };
        for (uint8_t i = 0; i < mi.pieceCount; ++i) {
            if (pType(mi.piece[i]) == KING) {
                continue;
            }
            if (mi.org[i] != SQ_NONE) {
                removedList->push_back(makeIndex(perspective, mi.org[i], mi.piece[i], kSq));
            }
            if (mi.dst[i] != SQ_NONE) {
                addedList->push_back(makeIndex(perspective, mi.dst[i], mi.piece[i], kSq));
            }
        }
    }

    template class HalfKP<Side::FRIEND>;

}
