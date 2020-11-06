// Definition of input features HalfKP of NNUE evaluation function

#include "../../position.h"
#include "half_kp.h"
#include "index_list.h"

namespace Evaluator::NNUE::Features {

    constexpr int32_t OrientSquare[COLORS]{
        SQ_A1, SQ_H8
    };

    // Orient a square according to perspective (rotates by 180 for black)
    inline Square orient(Color perspective, Square s) noexcept {
        return Square(int32_t(s) ^ OrientSquare[perspective]);
    }

    // Find the index of the feature quantity from the king position and PieceSquare
    template <Side AssociatedKing>
    inline IndexType HalfKP<AssociatedKing>::makeIndex(Color perspective, Square s, Piece pc, Square kSq) {
        return IndexType(orient(perspective, s) + PP_BoardIndex[pc][perspective] + PS_END * kSq);
    }

    // Get a list of indices for active features
    template<Side AssociatedKing>
    void HalfKP<AssociatedKing>::appendActiveIndices(Position const &pos, Color perspective, IndexList *activeList) {

        Square const kSq{ orient(perspective, pos.square(perspective|KING)) };
        Bitboard bb{ pos.pieces() & ~pos.pieces(KING) };
        while (bb != 0) {
            Square const s{ popLSq(bb) };
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
