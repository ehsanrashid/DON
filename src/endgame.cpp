#include "endgame.h"

#include <string_view>

#include "bitbase.h"
#include "movegenerator.h"

namespace {

    /// Drive a piece towards the edge of the board,
    /// used in KX vs K and KQ vs KR
    inline int32_t pushToEdge(Square s) noexcept {
        return 90 - (7 * int32_t(nSqr(edgeDistance(sFile(s)))) / 2
                   + 7 * int32_t(nSqr(edgeDistance(sRank(s)))) / 2);
    }
    /// Drive a piece towards the corner of the board,
    /// used in KBN vs K to A1H8 corners
    inline int32_t pushToCorner(Square s) noexcept {
        return 420 * std::abs(7 - sFile(s) - sRank(s));
    }
    /// Drive a piece close to another piece
    inline int32_t pushClose(Square s1, Square s2) noexcept {
        return 20 * (7 - distance(s1, s2));
    }
    /// Drive a piece away from another piece
    inline int32_t pushAway(Square s1, Square s2) noexcept {
        return 20 * (distance(s1, s2) - 1);
    }

    /// Required stngColor must have a single pawn
    /// Map the square as if stngColor is white and pawn square is on files A-D
    Square normalize(Square sq, Color stngColor, File spF) {

        if (stngColor == BLACK) {
            sq = flip<Rank>(sq);
        }
        if (spF >= FILE_E) {
            sq = flip<File>(sq);
        }
        return sq;
    }

#if !defined(NDEBUG)
    bool verifyMaterial(Position const &pos, Color c, Value npm, int32_t pawnCount) {
        return pos.nonPawnMaterial(c) == npm
            && pos.count(c|PAWN) == pawnCount;
    }
#endif
}

/// Mate with KX vs K. This gives the attacking side a bonus
/// for driving the defending king towards the edge of the board and
/// for keeping the distance between the two kings small.
template<> Value Endgame<KXK>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, weakColor, VALUE_ZERO, 0));
    assert(pos.checkers() == 0); // Eval is never called when in check

    // Stalemate detection with lone weak king
    if (pos.activeSide() == weakColor
     && MoveList<LEGAL>(pos).size() == 0) {
        return VALUE_DRAW;
    }

    auto const skSq{ pos.square(stngColor|KING) };
    auto const wkSq{ pos.square(weakColor|KING) };

    auto value{
        std::min(pos.count(stngColor|PAWN) * VALUE_EG_PAWN
      + pos.nonPawnMaterial(stngColor)
      + pushToEdge(wkSq)
      + pushClose(skSq, wkSq),
        +VALUE_KNOWN_WIN - 1) };

    if (pos.count(stngColor|QUEN) > 0
     || pos.count(stngColor|ROOK) > 0
     || pos.bishopPaired(stngColor)
     || (pos.count(stngColor|BSHP) > 0
      && pos.count(stngColor|NIHT) > 0)
     || pos.count(stngColor|NIHT) > 2) {
        value += +VALUE_KNOWN_WIN;
    }

    return pos.activeSide() == stngColor ? +value : -value;
}

/// KP vs K. This endgame is evaluated with the help of a bitbase.
template<> Value Endgame<KPK>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_ZERO, 1)
        && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

    auto const spFile{ sFile(pos.square(stngColor|PAWN)) };
    auto const skSq{ normalize(pos.square(stngColor|KING), stngColor, spFile) };
    auto const spSq{ normalize(pos.square(stngColor|PAWN), stngColor, spFile) };
    auto const wkSq{ normalize(pos.square(weakColor|KING), stngColor, spFile) };

    if (!BitBase::probe(pos.activeSide() == stngColor, skSq, wkSq, spSq)) {
        return VALUE_DRAW;
    }

    auto const value{
        VALUE_KNOWN_WIN
      + VALUE_EG_PAWN
      + sRank(spSq) };

    return pos.activeSide() == stngColor ? +value : -value;
}

/// Mate with KBN vs K. This is similar to KX vs K, but have to drive the
/// defending king towards a corner square of the attacking bishop attacks.
template<> Value Endgame<KBNK>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_NIHT + VALUE_MG_BSHP, 0)
        && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

    auto const skSq{ pos.square(stngColor|KING) };
    auto const sbSq{ pos.square(stngColor|BSHP) };
    auto const wkSq{ pos.square(weakColor|KING) };

    // If Bishop does not attack A1/H8, flip the enemy king square to drive to opposite corners (A8/H1).
    auto const value{
        VALUE_KNOWN_WIN
      + VALUE_EG_BSHP
      + VALUE_EG_NIHT
      + pushClose(skSq, wkSq)
      + pushToCorner(colorOpposed(sbSq, SQ_A1) ? flip<File>(wkSq) : wkSq) };
    assert(value < +VALUE_MATE_2_MAX_PLY);
    return pos.activeSide() == stngColor ? +value : -value;
}

/// Draw with KNN vs K
template<> Value Endgame<KNNK>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, 2*VALUE_MG_NIHT, 0)
        && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

    auto const value{
        VALUE_DRAW
      + pos.count(stngColor|NIHT) / 2 };
    return pos.activeSide() == stngColor ? +value : -value;
}

/// KR vs KP. This is a somewhat tricky endgame to evaluate precisely without a bitbase.
/// This returns drawish scores when the pawn is far advanced with support of the king,
/// while the attacking king is far away.
template<> Value Endgame<KRKP>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
        && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

    auto const skSq{ pos.square(stngColor|KING) };
    auto const srSq{ pos.square(stngColor|ROOK) };
    auto const wkSq{ pos.square(weakColor|KING) };
    auto const wpSq{ pos.square(weakColor|PAWN) };

    auto const promoteSq{ makeSquare(sFile(wpSq), relativeRank(weakColor, RANK_8)) };

    Value value;

    // If the strong side's king is in front of the pawn, or
    // If the weak side's king is too far from the rook and pawn, it's a win.
    if (contains(frontSquaresBB(stngColor, skSq), wpSq)
     || (distance(wkSq, srSq) >= 3
      && distance(wkSq, wpSq) >= 3 + (pos.activeSide() == weakColor))) {
        value = VALUE_EG_ROOK
              - distance(skSq, wpSq);
    }
    else
    // If the pawn is far advanced and supported by the defending king, it's a drawish.
    if (relativeRank(stngColor, wkSq) <= RANK_3
     && distance(wpSq, wkSq) == 1
     && relativeRank(stngColor, skSq) >= RANK_4
     && distance(wpSq, skSq) > 2 + (pos.activeSide() == stngColor)) {
        value = Value(80)
              - 8 * distance(wpSq, skSq);
    }
    else {
        value = Value(200)
              - 8 * (distance(skSq, wpSq + PawnPush[weakColor])
                   - distance(wkSq, wpSq + PawnPush[weakColor])
                   - distance(wpSq, promoteSq));
    }

    return pos.activeSide() == stngColor ? +value : -value;
}

/// KR vs KB. This is very simple, and always returns drawish scores.
/// The score is slightly bigger when the defending king is close to the edge.
template<> Value Endgame<KRKB>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
        && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

    auto const wkSq{ pos.square(weakColor|KING) };

    auto const value{
        VALUE_DRAW
      + pushToEdge(wkSq) };

    return pos.activeSide() == stngColor ? +value : -value;
}

/// KR vs KN. The attacking side has slightly better winning chances than
/// in KR vs KB, particularly if the king and the knight are far apart.
template<> Value Endgame<KRKN>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
        && verifyMaterial(pos, weakColor, VALUE_MG_NIHT, 0));

    auto const wkSq{ pos.square(weakColor|KING) };
    auto const wnSq{ pos.square(weakColor|NIHT) };

    auto const value{
        VALUE_DRAW
      + pushToEdge(wkSq)
      + pushAway(wkSq, wnSq) };

    return pos.activeSide() == stngColor ? +value : -value;
}

/// KQ vs KP. In general, this is a win for the strong side, but there are a
/// few important exceptions. A pawn on 7th rank and on the A,C,F or H files
/// with a king positioned next to it can be a draw, so in that case, only
/// use the distance between the kings.
template<> Value Endgame<KQKP>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_QUEN, 0)
        && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

    auto const skSq{ pos.square(stngColor|KING) };
    auto const wkSq{ pos.square(weakColor|KING) };
    auto const wpSq{ pos.square(weakColor|PAWN) };

    auto value{
        VALUE_DRAW
      + pushClose(skSq, wkSq) };

    if (relativeRank(weakColor, wpSq) != RANK_7
     || distance(wkSq, wpSq) > 1
     || contains(FileBB[FILE_B]
                |FileBB[FILE_D]
                |FileBB[FILE_E]
                |FileBB[FILE_G], wpSq)) {
        value += VALUE_EG_QUEN
               - VALUE_EG_PAWN;
    }

    return pos.activeSide() == stngColor ? +value : -value;
}

/// KQ vs KR. This is almost identical to KX vs K: give the attacking
/// king a bonus for having the kings close together, and for forcing the
/// defending king towards the edge. If also take care to avoid null move for
/// the defending side in the search, this is usually sufficient to win KQ vs KR.
template<> Value Endgame<KQKR>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_QUEN, 0)
        && verifyMaterial(pos, weakColor, VALUE_MG_ROOK, 0));

    auto const skSq{ pos.square(stngColor|KING) };
    auto const wkSq{ pos.square(weakColor|KING) };

    auto const value{
        VALUE_EG_QUEN
      - VALUE_EG_ROOK
      + pushToEdge(wkSq)
      + pushClose(skSq, wkSq) };

    return pos.activeSide() == stngColor ? +value : -value;
}

/// KNN vs KP. Very drawish, but there are some mate opportunities if we can
/// press the weakSide King to a corner before the pawn advances too much.
template<> Value Endgame<KNNKP>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, 2*VALUE_MG_NIHT, 0)
        && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

    auto const wkSq{ pos.square(weakColor|KING) };
    auto const wpSq{ pos.square(weakColor|PAWN) };

    auto const value{
        VALUE_EG_PAWN
      +  2 * pushToEdge(wkSq)
      - 10 * relativeRank(weakColor, wpSq) };

    return pos.activeSide() == stngColor ? +value : -value;
}

/// Special Scaling functions

/// KRP vs KR. This function knows a handful of the most important classes of
/// drawn positions, but is far from perfect. It would probably be a good idea
/// to add more knowledge in the future.
///
/// It would also be nice to rewrite the actual code for this function,
/// which is mostly copied from Glaurung 1.x, and isn't very pretty.
template<> Scale Endgame<KRPKR>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 1)
        && verifyMaterial(pos, weakColor, VALUE_MG_ROOK, 0));

    auto const spFile{ sFile(pos.square(stngColor|PAWN)) };
    auto const skSq{ normalize(pos.square(stngColor|KING), stngColor, spFile) };
    auto const srSq{ normalize(pos.square(stngColor|ROOK), stngColor, spFile) };
    auto const spSq{ normalize(pos.square(stngColor|PAWN), stngColor, spFile) };
    auto const wkSq{ normalize(pos.square(weakColor|KING), stngColor, spFile) };
    auto const wrSq{ normalize(pos.square(weakColor|ROOK), stngColor, spFile) };

    auto const spF{ sFile(spSq) };
    auto const spR{ sRank(spSq) };
    auto const promoteSq{ makeSquare(spF, RANK_8) };
    bool const sTempo{ pos.activeSide() == stngColor };

    // If the pawn is not too far advanced and the defending king defends the
    // queening square, use the third-rank defense.
    if (spR <= RANK_5
     && skSq <= SQ_H5
     && distance(wkSq, promoteSq) <= 1
     && (sRank(wrSq) == RANK_6
      || (spR <= RANK_3
       && sRank(srSq) != RANK_6))) {
        return SCALE_DRAW;
    }
    // The defending side saves a draw by checking from behind in case the pawn
    // has advanced to the 6th rank with the king behind.
    if (spR == RANK_6
     && distance(wkSq, promoteSq) <= 1
     && sRank(skSq) <= RANK_6 - sTempo
     && (sRank(wrSq) == RANK_1
      || (!sTempo
       && distance<File>(wrSq, spSq) >= 3))) {
        return SCALE_DRAW;
    }
    //
    if (spR >= RANK_6
     && wkSq == promoteSq
     && sRank(wrSq) == RANK_1
     && (!sTempo
      || distance(skSq, spSq) >= 2)) {
        return SCALE_DRAW;
    }
    // White pawn on a7 and rook on a8 is a draw if black king is on g7 or h7
    // and the black rook is behind the pawn.
    if (spSq == SQ_A7
     && srSq == SQ_A8
     && (wkSq == SQ_H7
      || wkSq == SQ_G7)
     && sFile(wrSq) == FILE_A
     && (sRank(wrSq) <= RANK_3
      || sFile(skSq) >= FILE_D
      || sRank(skSq) <= RANK_5)) {
        return SCALE_DRAW;
    }
    // If the defending king blocks the pawn and the attacking king is too far away, it's a draw.
    if (spR <= RANK_5
     && wkSq == spSq + NORTH
     && distance(skSq, spSq) >= 2 + sTempo
     && distance(skSq, wrSq) >= 2 + sTempo) {
        return SCALE_DRAW;
    }
    // Pawn on the 7th rank supported by the rook from behind usually wins if the
    // attacking king is closer to the queening square than the defending king,
    // and the defending king cannot gain tempo by threatening the attacking rook.
    if (spR == RANK_7
     && spF != FILE_A
     && spF == sFile(srSq)
     && srSq != promoteSq
     && distance(skSq, promoteSq) < distance(wkSq, promoteSq) - 2 + sTempo
     && distance(skSq, promoteSq) < distance(wkSq, srSq) + sTempo) {
        return Scale(SCALE_MAX
                   - 2 * distance(skSq, promoteSq));
    }
    // Similar to the above, but with the pawn further back
    if (spF != FILE_A
     && spF == sFile(srSq)
     && srSq < spSq
     && distance(skSq, promoteSq) < distance(wkSq, promoteSq) - 2 + sTempo
     && distance(skSq, spSq + NORTH) < distance(wkSq, spSq + NORTH) - 2 + sTempo
     && (distance(wkSq, srSq) >= 3 - sTempo
      || (distance(skSq, promoteSq) < distance(wkSq, srSq) + sTempo
       && distance(skSq, spSq + NORTH) < distance(wkSq, srSq) + sTempo))) {
        return Scale(SCALE_MAX
                   - 8 * distance(spSq, promoteSq)
                   - 2 * distance(skSq, promoteSq));
    }
    // If the pawn is not far advanced
    // and the defending king is somewhere in the pawn's path, it's probably a draw.
    if (spR <= RANK_4
     && contains(pawnPassSpan(stngColor, spSq - PawnPush[stngColor]), wkSq)) {
        if (contains(frontSquaresBB(stngColor, spSq), wkSq)) {
            return Scale(10);
        }
        if (distance(skSq, wkSq) > 2 + sTempo) {
            return Scale(24
                       - 2 * distance(skSq, wkSq));
        }
    }

    return SCALE_NONE;
}

/// KRP vs KB.
template<> Scale Endgame<KRPKB>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 1)
        && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

    // If rook pawns
    if ((pos.pieces(stngColor, PAWN)
       & (FileBB[FILE_A]|FileBB[FILE_H])) != 0) {
        auto const skSq{ pos.square(stngColor|KING) };
        auto const spSq{ pos.square(stngColor|PAWN) };
        auto const wkSq{ pos.square(weakColor|KING) };
        auto const wbSq{ pos.square(weakColor|BSHP) };
        auto const spR{ relativeRank(stngColor, spSq) };

        auto const Push{ PawnPush[stngColor] };

        // If the pawn is on the 5th rank and the pawn (currently) is on the
        // same color square as the bishop then there is a chance of a fortress.
        // Depending on the king position give a moderate reduction or a strong one
        // if the defending king is near the corner but not trapped there.
        if (spR == RANK_5
         && !colorOpposed(wbSq, spSq)) {
            auto const d{ distance(spSq + Push * 3, wkSq) };
            return d <= 2
                && (d != 0
                 || wkSq != skSq + Push * 2) ?
                    Scale(24) : Scale(48);
        }
        // When the pawn has moved to the 6th rank can be fairly sure it's drawn
        // if the bishop attacks the square in front of the pawn from a reasonable distance
        // and the defending king is near the corner
        if (spR == RANK_6
         && distance(spSq + Push * 2, wkSq) <= 1
         && distance<File>(spSq, wbSq) >= 2
         && contains(attacksBB<BSHP>(wbSq), spSq + Push)) {
            return Scale(8);
        }
    }

    return SCALE_NONE;
}

/// KRPP vs KRP. If the defending king is actively placed, the position is drawish.
template<> Scale Endgame<KRPPKRP>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 2)
        && verifyMaterial(pos, weakColor, VALUE_MG_ROOK, 1));

    auto const sp1Sq{ pos.square(stngColor|PAWN, 0) };
    auto const sp2Sq{ pos.square(stngColor|PAWN, 1) };
    auto const wkSq{ pos.square(weakColor|KING) };

    // Does the stronger side have a passed pawn?
    if (pos.pawnPassedAt(stngColor, sp1Sq)
     || pos.pawnPassedAt(stngColor, sp2Sq)) {
        return SCALE_NONE;
    }

    auto const spR{ std::max(relativeRank(stngColor, sp1Sq), relativeRank(stngColor, sp2Sq)) };
    if (distance<File>(wkSq, sp1Sq) <= 1
     && distance<File>(wkSq, sp2Sq) <= 1
     && spR < relativeRank(stngColor, wkSq)) {
        assert(RANK_2 <= spR && spR <= RANK_6); // Not RANK_7 due to pawnPassedAt()
        return Scale(7 * spR);
    }

    return SCALE_NONE;
}

/// KBP vs KB. There are two rules:
/// If the two bishops have opposite color, it's almost always a draw.
/// If the defending king is somewhere along the path of the pawn,
/// and the square of the king is not of the same color as the strong side's bishop, it's a draw.
template<> Scale Endgame<KBPKB>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_BSHP, 1)
        && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

    auto const spSq{ pos.square(stngColor|PAWN) };
    auto const sbSq{ pos.square(stngColor|BSHP) };
    auto const wbSq{ pos.square(weakColor|BSHP) };
    auto const wkSq{ pos.square(weakColor|KING) };

    if (// Opposite colored bishops
        colorOpposed(sbSq, wbSq)
        // Defending king blocks the pawn, and cannot be driven away
     || (contains(frontSquaresBB(stngColor, spSq), wkSq)
      && (colorOpposed(sbSq, wkSq)
       || relativeRank(stngColor, wkSq) <= RANK_6))) {
        return SCALE_DRAW;
    }

    return SCALE_NONE;
}

/// KBPP vs KB. It detects a few basic draws with opposite-colored bishops.
template<> Scale Endgame<KBPPKB>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_BSHP, 2)
        && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

    auto const sbSq{ pos.square(stngColor|BSHP) };
    auto const wbSq{ pos.square(weakColor|BSHP) };
    auto const wkSq{ pos.square(weakColor|KING) };

    if (colorOpposed(sbSq, wbSq)) {
        auto const sp1Sq{ pos.square(stngColor|PAWN, 0) };
        auto const sp2Sq{ pos.square(stngColor|PAWN, 1) };

        auto const Push{ PawnPush[stngColor] };

        Square block1Sq;
        Square block2Sq;

        if (relativeRank(stngColor, sp1Sq) > relativeRank(stngColor, sp2Sq)) {
            block1Sq = sp1Sq + Push;
            block2Sq = makeSquare(sFile(sp2Sq), sRank(sp1Sq));
        }
        else {
            block1Sq = sp2Sq + Push;
            block2Sq = makeSquare(sFile(sp1Sq), sRank(sp2Sq));
        }

        switch (distance<File>(sp1Sq, sp2Sq)) {
        case 0:
            // Both pawns are on the same file. It's an easy draw if the defender firmly
            // controls some square in the front most pawn's path.
            if (colorOpposed(sbSq, wkSq)
             && contains(frontSquaresBB(stngColor, block1Sq - Push), wkSq)) {
                return SCALE_DRAW;
            }
            break;
        case 1:
            // Pawns on adjacent files. It's a draw if the defender firmly controls the
            // square in front of the front most pawn's path, and the square diagonally
            // behind this square on the file of the other pawn.
            if ((wkSq == block1Sq
              && colorOpposed(sbSq, wkSq)
              && (wbSq == block2Sq
               || distance<Rank>(sp1Sq, sp2Sq) >= 2
               || (attacksBB<BSHP>(block2Sq, pos.pieces())
                 & pos.pieces(weakColor, BSHP)) != 0))
             || (wkSq == block2Sq
              && colorOpposed(sbSq, wkSq)
              && (wbSq == block1Sq
               || (attacksBB<BSHP>(block1Sq, pos.pieces())
                 & pos.pieces(weakColor, BSHP)) != 0))) {
                return SCALE_DRAW;
            }
            break;
        default:
            // The pawns are not on the same file or adjacent files. No scaling.
            break;
        }
    }

    return SCALE_NONE;
}

/// KBP vs KN. There is a single rule: If the defending king is somewhere along
/// the path of the pawn, and the square of the king is not of the same color as
/// the strong side's bishop or pawn is less advance, it's a draw.
template<> Scale Endgame<KBPKN>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_BSHP, 1)
        && verifyMaterial(pos, weakColor, VALUE_MG_NIHT, 0));

    auto const spSq{ pos.square(stngColor|PAWN) };
    auto const sbSq{ pos.square(stngColor|BSHP) };
    auto const wkSq{ pos.square(weakColor|KING) };

    if (contains(frontSquaresBB(stngColor, spSq), wkSq)
     && (colorOpposed(wkSq, sbSq)
      || relativeRank(stngColor, spSq) < RANK_6)) {
        return SCALE_DRAW;
    }

    return SCALE_NONE;
}

/// Generic Scaling functions

/// K and two or more pawns vs K. There is just a single rule here: if all pawns
/// are on the same rook file and are blocked by the defending king, it's a draw.
template<> Scale Endgame<KPsK>::operator()(Position const &pos) const {
    assert(pos.nonPawnMaterial(stngColor) == VALUE_ZERO);
    assert(pos.count(stngColor|PAWN) >= 2);
    assert(verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

    auto const wkSq{ pos.square(weakColor|KING) };
    Bitboard const sPawns{ pos.pieces(stngColor, PAWN) };

    // If all pawns are ahead of the king on a single rook file, it's a draw.
    if (((sPawns & ~FileBB[FILE_A]) == 0
      || (sPawns & ~FileBB[FILE_H]) == 0)
     && (sPawns & ~pawnPassSpan(weakColor, wkSq)) == 0) {
        return SCALE_DRAW;
    }

    return SCALE_NONE;
}

/// KP vs KP. This is done by removing the weakest side's pawn and probing the
/// KP vs K bitbase: If the weakest side has a draw without the pawn, it probably
/// has at least a draw with the pawn as well. The exception is when the strong
/// side's pawn is far advanced and not on a rook file; in this case it is often
/// possible to win (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
template<> Scale Endgame<KPKP>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_ZERO, 1)
        && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

    auto const spFile{ sFile(pos.square(stngColor|PAWN)) };
    auto const skSq{ normalize(pos.square(stngColor|KING), stngColor, spFile) };
    auto const spSq{ normalize(pos.square(stngColor|PAWN), stngColor, spFile) };
    auto const wkSq{ normalize(pos.square(weakColor|KING), stngColor, spFile) };

    // If the pawn has advanced to the fifth rank or further, and is not a rook pawn,
    // then it's too dangerous to assume that it's at least a draw.
    if (sRank(spSq) < RANK_5
     || sFile(spSq) == FILE_A) {
        // Probe the KPK bitbase with the weakest side's pawn removed.
        // If it's a draw, it's probably at least a draw even with the pawn.
        if (!BitBase::probe(pos.activeSide() == stngColor, skSq, wkSq, spSq)) {
            return SCALE_DRAW;
        }
    }

    return SCALE_NONE;
}

/// KB and one or more pawns vs K.
/// It checks for draws with rook pawns and a bishop of the wrong color.
/// If such a draw is detected, SCALE_DRAW is returned.
/// If not, the return value is SCALE_NONE, i.e. no scaling will be used.
template<> Scale Endgame<KBPsK>::operator()(Position const &pos) const {
    assert(pos.nonPawnMaterial(stngColor) == VALUE_MG_BSHP
        && pos.count(stngColor|PAWN) != 0);
    // No assertions about the material of weak side, because we want draws to
    // be detected even when the weak side has some materials or pawns.

    auto const skSq{ pos.square(stngColor|KING) };
    auto const sbSq{ pos.square(stngColor|BSHP) };
    auto const wkSq{ pos.square(weakColor|KING) };

    // All pawns of strong side on same A or H file? (rook file)
    // Then potential draw
    Bitboard const sPawns{ pos.pieces(stngColor, PAWN) };
    if ((sPawns & ~FileBB[FILE_A]) == 0
     || (sPawns & ~FileBB[FILE_H]) == 0) {
        auto const promoteSq{ relativeSq(stngColor, makeSquare(sFile(scanLSq(sPawns)), RANK_8)) };

        // The bishop has the wrong color and the defending king defends the queening square.
        if (colorOpposed(promoteSq, sbSq)
         && distance(promoteSq, wkSq) <= 1) {
            return SCALE_DRAW;
        }
    }

    // All pawns on same B or G file?
    // Then potential draw
    Bitboard const pawns{ pos.pieces(PAWN) };
    Bitboard const wPawns{ pos.pieces(weakColor, PAWN) };
    if (((pawns & ~FileBB[FILE_B]) == 0
      || (pawns & ~FileBB[FILE_G]) == 0)
     && pos.nonPawnMaterial(weakColor) == VALUE_ZERO
     && wPawns != 0) {
        // Get weak side pawn that is closest to home rank
        auto const wpSq{
            stngColor == WHITE ?
                scanFrontMostSq<WHITE>(wPawns) :
                scanFrontMostSq<BLACK>(wPawns) };

        // There's potential for a draw if weak pawn is blocked on the 7th rank
        // and the bishop cannot attack it or only one strong pawn left
        if (relativeRank(stngColor, wpSq) == RANK_7
         && contains(sPawns, wpSq + PawnPush[weakColor])
         && (colorOpposed(sbSq, wpSq)
          || !moreThanOne(sPawns))) {
            // It's a draw if the weak king is on its back two ranks, within 2
            // squares of the blocking pawn and the strong king is not closer.
            // This rule only fails in practically unreachable
            // positions such as 5k1K/6p1/6P1/8/8/3B4/8/8 w and
            // where Q-search will immediately correct the problem
            // positions such as 8/4k1p1/6P1/1K6/3B4/8/8/8 w
            if (relativeRank(stngColor, wkSq) >= RANK_7
             && distance(wpSq, wkSq) <= 2
             && distance(wpSq, wkSq) <= distance(wpSq, skSq)) {
                return SCALE_DRAW;
            }
        }

    }

    return SCALE_NONE;
}

/// KQ vs KR and one or more pawns.
/// It tests for fortress draws with a rook on the 3rd rank defended by a pawn.
template<> Scale Endgame<KQKRPs>::operator()(Position const &pos) const {
    assert(verifyMaterial(pos, stngColor, VALUE_MG_QUEN, 0)
        && pos.count(weakColor|ROOK) == 1
        && pos.count(weakColor|PAWN) != 0);

    auto const skSq{ pos.square(stngColor|KING) };
    auto const wkSq{ pos.square(weakColor|KING) };
    auto const wrSq{ pos.square(weakColor|ROOK) };

    if (relativeRank(weakColor, wkSq) <= RANK_2
     && relativeRank(weakColor, skSq) >= RANK_4
     && relativeRank(weakColor, wrSq) == RANK_3
     && (pos.pieces(weakColor, PAWN)
       & attacksBB<KING>(wkSq)
       & pawnAttacksBB(stngColor, wrSq)) != 0) {
        return SCALE_DRAW;
    }

    return SCALE_NONE;
}


namespace EndGame {

    EGMapPair<Value, Scale> EndGames;

    namespace {

        template<EndgameCode EC, typename T = EndgameType<EC>>
        void addEG(std::string_view code) {
            StateInfo si;
            mapEG<T>()[Position().setup(code, WHITE, si).matlKey()] = EGPtr<T>(new Endgame<EC>(WHITE));
            mapEG<T>()[Position().setup(code, BLACK, si).matlKey()] = EGPtr<T>(new Endgame<EC>(BLACK));
        }

    }

    void initialize() {
        // EVALUATION_FUNCTIONS
        addEG<KPK  >("KPK");
        addEG<KNNK >("KNNK");
        addEG<KBNK >("KBNK");
        addEG<KRKP >("KRKP");
        addEG<KRKB >("KRKB");
        addEG<KRKN >("KRKN");
        addEG<KQKP >("KQKP");
        addEG<KQKR >("KQKR");
        addEG<KNNKP>("KNNKP");

        // SCALING_FUNCTIONS
        addEG<KRPKR  >("KRPKR");
        addEG<KRPKB  >("KRPKB");
        addEG<KRPPKRP>("KRPPKRP");
        addEG<KBPKB  >("KBPKB");
        addEG<KBPPKB >("KBPPKB");
        addEG<KBPKN  >("KBPKN");
    }

}
