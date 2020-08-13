// Definition of input features HalfKP of NNUE evaluation function

#include "../../Type.h"
#include "half_kp.h"
#include "index_list.h"

namespace Evaluator::NNUE::Features {

    // Find the index of the feature quantity from the king position and PieceSquare
    template <Side AssociatedKing>
    inline IndexType HalfKP<AssociatedKing>::MakeIndex(Square sq_k, PieceSquare p) {
        return static_cast<IndexType>(PS_END) * static_cast<IndexType>(sq_k) + p;
    }

    // Get pieces information
    template <Side AssociatedKing>
    inline void HalfKP<AssociatedKing>::GetPieces(
        Position const &pos, Color perspective,
        PieceSquare **pieces, Square *sq_target_k) {

        *pieces = (perspective == BLACK) ?
            pos.evalList()->pieceListFb() :
            pos.evalList()->pieceListFw();
        const PieceId target = (AssociatedKing == Side::kFriend) ?
            static_cast<PieceId>(PIECE_ID_KING + perspective) :
            static_cast<PieceId>(PIECE_ID_KING + ~perspective);
        *sq_target_k = static_cast<Square>(((*pieces)[target] - PS_W_KING) % SQUARES);
    }

    // Get a list of indices for active features
    template <Side AssociatedKing>
    void HalfKP<AssociatedKing>::appendActiveIndices(
        Position const &pos, Color perspective, IndexList *active) {

        // Do nothing if array size is small to avoid compiler warning
        if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

        PieceSquare *pieces;
        Square sq_target_k;
        GetPieces(pos, perspective, &pieces, &sq_target_k);
        for (PieceId i = PIECE_ID_ZERO; i < PIECE_ID_KING; ++i) {
            if (pieces[i] != PS_NONE) {
                active->push_back(MakeIndex(sq_target_k, pieces[i]));
            }
        }
    }

    // Get a list of indices for recently changed features
    template <Side AssociatedKing>
    void HalfKP<AssociatedKing>::appendChangedIndices(
        Position const &pos, Color perspective,
        IndexList *removed, IndexList *added) {

        PieceSquare *pieces;
        Square sq_target_k;
        GetPieces(pos, perspective, &pieces, &sq_target_k);
        const auto &dp = pos.state()->dirtyPiece;
        for (int i = 0; i < dp.dirty_num; ++i) {
            if (dp.pieceId[i] >= PIECE_ID_KING) continue;
            const auto old_p = static_cast<PieceSquare>(
                dp.oldPiece[i].from[perspective]);
            if (old_p != PS_NONE) {
                removed->push_back(MakeIndex(sq_target_k, old_p));
            }
            const auto new_p = static_cast<PieceSquare>(
                dp.newPiece[i].from[perspective]);
            if (new_p != PS_NONE) {
                added->push_back(MakeIndex(sq_target_k, new_p));
            }
        }
    }

    template class HalfKP<Side::kFriend>;

}  // namespace Evaluator::NNUE::Features
