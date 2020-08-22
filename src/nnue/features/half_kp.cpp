// Definition of input features HalfKP of NNUE evaluation function

#include "../../Position.h"
#include "half_kp.h"
#include "index_list.h"

namespace Evaluator::NNUE::Features {

    // Find the index of the feature quantity from the king position and PieceSquare
    template<Side AssociatedKing>
    inline IndexType HalfKP<AssociatedKing>::makeIndex(Square kSq, PieceSquare p) {
        return static_cast<IndexType>(PS_END) * static_cast<IndexType>(kSq) + p;
    }

    // Get pieces information
    template<Side AssociatedKing>
    inline void HalfKP<AssociatedKing>::getPieces(Position const &pos, Color perspective, PieceSquare **pieces, Square *targetKSq) {

        *pieces = (perspective == BLACK) ?
                    pos.evalList()->pieceListFb() :
                    pos.evalList()->pieceListFw();
        PieceId const target{ (AssociatedKing == Side::FRIEND) ?
                                static_cast<PieceId>(PIECE_ID_KING +  perspective) :
                                static_cast<PieceId>(PIECE_ID_KING + ~perspective) };
        *targetKSq = static_cast<Square>(((*pieces)[target] - PS_W_KING) % SQUARES);
    }

    // Get a list of indices for active features
    template<Side AssociatedKing>
    void HalfKP<AssociatedKing>::appendActiveIndices(Position const &pos, Color perspective, IndexList *active) {

        // Do nothing if array size is small to avoid compiler warning
        if (RawFeatures::MaxActiveDimensions < MaxActiveDimensions) {
            return;
        }
        PieceSquare *pieces;
        Square targetKSq;
        getPieces(pos, perspective, &pieces, &targetKSq);
        for (PieceId i = PIECE_ID_ZERO; i < PIECE_ID_KING; ++i) {
            if (pieces[i] != PS_NONE) {
                active->push_back(makeIndex(targetKSq, pieces[i]));
            }
        }
    }

    // Get a list of indices for recently changed features
    template<Side AssociatedKing>
    void HalfKP<AssociatedKing>::appendChangedIndices(Position const &pos, Color perspective, IndexList *removed, IndexList *added) {

        PieceSquare *pieces;
        Square targetKSq;
        getPieces(pos, perspective, &pieces, &targetKSq);
        auto const &dp{ pos.state()->dirtyPiece };
        for (int i = 0; i < dp.dirtyCount; ++i) {
            if (dp.pieceId[i] >= PIECE_ID_KING) {
                continue;
            }
            auto const oldPS{ static_cast<PieceSquare>(dp.oldPiece[i].org[perspective]) };
            if (oldPS != PS_NONE) {
                removed->push_back(makeIndex(targetKSq, oldPS));
            }
            auto const newPS{ static_cast<PieceSquare>(dp.newPiece[i].org[perspective]) };
            if (newPS != PS_NONE) {
                added->push_back(makeIndex(targetKSq, newPS));
            }
        }
    }

    template class HalfKP<Side::FRIEND>;

}  // namespace Evaluator::NNUE::Features
