#include "Endgame.h"

#include <array>
#include <string>
#include <utility>

#include "Bitbase.h"
#include "MoveGenerator.h"

namespace Endgames {

    using namespace std;

    EG_MapPair<Value, Scale> EndgameMapPair;

    template<EndgameCode C, typename T = EndgameType<C>>
    void add(const string &code)
    {
        StateInfo si;
        map<T>()[Position().setup(code, WHITE, si).matlKey()] = EG_Ptr<T>(new Endgame<C>(WHITE));
        map<T>()[Position().setup(code, BLACK, si).matlKey()] = EG_Ptr<T>(new Endgame<C>(BLACK));
    }

    void initialize()
    {
        // EVALUATION_FUNCTIONS
        add<KPK  >("KPK");
        add<KNNK >("KNNK");
        add<KNNKP>("KNNKP");
        add<KBNK >("KBNK");
        add<KRKP >("KRKP");
        add<KRKB >("KRKB");
        add<KRKN >("KRKN");
        add<KQKP >("KQKP");
        add<KQKR >("KQKR");

        // SCALING_FUNCTIONS
        add<KRPKR  >("KRPKR");
        add<KRPKB  >("KRPKB");
        add<KRPPKRP>("KRPPKRP");
        add<KNPK   >("KNPK");
        add<KBPKB  >("KBPKB");
        add<KBPPKB >("KBPPKB");
        add<KBPKN  >("KBPKN");
        add<KNPKB  >("KNPKB");
    }

    namespace {

        // Table used to drive the weak king towards the edge of the board.
        constexpr Array<i32, SQUARES> PushToEdge
        {
            100, 90, 80, 70, 70, 80, 90, 100,
             90, 70, 60, 50, 50, 60, 70,  90,
             80, 60, 40, 30, 30, 40, 60,  80,
             70, 50, 30, 20, 20, 30, 50,  70,
             70, 50, 30, 20, 20, 30, 50,  70,
             80, 60, 40, 30, 30, 40, 60,  80,
             90, 70, 60, 50, 50, 60, 70,  90,
            100, 90, 80, 70, 70, 80, 90, 100
        };
        // Table used to drive the weak king towards a corner square of the right color.
        constexpr Array<i32, SQUARES> PushToCorner
        {
            200, 190, 180, 170, 160, 150, 140, 130,
            190, 180, 170, 160, 150, 140, 130, 140,
            180, 170, 155, 140, 140, 125, 140, 150,
            170, 160, 140, 120, 110, 140, 150, 160,
            160, 150, 140, 110, 120, 140, 160, 170,
            150, 140, 125, 140, 140, 155, 170, 180,
            140, 130, 140, 150, 160, 170, 180, 190,
            130, 140, 150, 160, 170, 180, 190, 200
        };
        // Tables used to drive a piece towards or away from another piece.
        constexpr Array<i32, 8> PushClose { 0, 0, 100, 80, 60, 40, 20,  10 };
        constexpr Array<i32, 8> PushAway  { 0, 5,  20, 40, 60, 80, 90, 100 };

        // Pawn Rank based scaling.
        constexpr Array<Scale, RANKS> RankScale
        {
            Scale(0),
            Scale(9),
            Scale(10),
            Scale(14),
            Scale(21),
            Scale(44),
            Scale(0),
            Scale(0)
        };

        // Map the square as if color is white and square only pawn is on the left half of the board.
        Square normalize(const Position &pos, Color c, Square sq)
        {
            assert(1 == pos.count(c|PAWN));

            if (FILE_E <= sFile(pos.square(c|PAWN)))
            {
                sq = !sq;
            }

            return WHITE == c ? sq : ~sq;
        }

#if !defined(NDEBUG)

        bool verifyMaterial(const Position &pos, Color c, Value npm, i32 pawnCount)
        {
            return pos.nonPawnMaterial(c) == npm
                && pos.count(c|PAWN) == pawnCount;
        }

#endif

    }

    /// Mate with KX vs K. This gives the attacking side a bonus
    /// for driving the defending king towards the edge of the board and
    /// for keeping the distance between the two kings small.
    template<> Value Endgame<KXK>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, weakColor, VALUE_ZERO, 0));
        assert(0 == pos.checkers()); // Eval is never called when in check
        // Stalemate detection with lone weak king
        if (   weakColor == pos.active
            && 0 == MoveList<GenType::LEGAL>(pos).size())
        {
            return VALUE_DRAW;
        }

        auto skSq = pos.square(stngColor|KING);
        auto wkSq = pos.square(weakColor|KING);

        auto value = std::min(pos.count(stngColor|PAWN)*VALUE_EG_PAWN
                             + pos.nonPawnMaterial(stngColor)
                             + PushToEdge[wkSq]
                             + PushClose[dist(skSq, wkSq)],
                               +VALUE_KNOWN_WIN - 1);

        if (   0 < pos.count(stngColor|QUEN)
            || 0 < pos.count(stngColor|ROOK)
            || pos.pairedBishop(stngColor)
            || (   0 < pos.count(stngColor|BSHP)
                && 0 < pos.count(stngColor|NIHT))
            || 2 < pos.count(stngColor|NIHT))
        {
            value += +VALUE_KNOWN_WIN;
        }

        return stngColor == pos.active ? +value : -value;
    }

    /// KP vs K. This endgame is evaluated with the help of a bitbase.
    template<> Value Endgame<KPK>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_ZERO, 1)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

        // Assume stngColor is white and the pawn is on files A-D
        auto skSq = normalize(pos, stngColor, pos.square(stngColor|KING));
        auto spSq = normalize(pos, stngColor, pos.square(stngColor|PAWN));
        auto wkSq = normalize(pos, stngColor, pos.square(weakColor|KING));

        if (!Bitbases::probe(stngColor == pos.active ? WHITE : BLACK, skSq, spSq, wkSq))
        {
            return VALUE_DRAW;
        }

        auto value = VALUE_KNOWN_WIN
                   + VALUE_EG_PAWN
                   + sRank(spSq);

        return stngColor == pos.active ? +value : -value;
    }

    /// Mate with KBN vs K. This is similar to KX vs K, but have to drive the
    /// defending king towards a corner square of the attacking bishop attacks.
    template<> Value Endgame<KBNK>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_NIHT + VALUE_MG_BSHP, 0)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

        auto skSq = pos.square(stngColor|KING);
        auto sbSq = pos.square(stngColor|BSHP);
        auto wkSq = pos.square(weakColor|KING);

        // If Bishop does not attack A1/H8, flip the enemy king square to drive to opposite corners (A8/H1).
        auto value = VALUE_KNOWN_WIN
                   + PushClose[dist(skSq, wkSq)]
                   + 32*PushToCorner[oppositeColor(sbSq, SQ_A1) ? ~wkSq : wkSq];
        assert(abs(value) < +VALUE_MATE_MAX_PLY);
        return stngColor == pos.active ? +value : -value;
    }

    /// Draw with KNN vs K
    template<> Value Endgame<KNNK>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, 2*VALUE_MG_NIHT, 0)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

        auto value = Value(pos.count(stngColor|NIHT) / 2);

        return stngColor == pos.active ? +value : -value;
    }

    /// KNN vs KP. Simply push the opposing king to any corner.
    template<> Value Endgame<KNNKP>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, 2*VALUE_MG_NIHT, 0)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

        auto wkSq = pos.square(weakColor|KING);

        auto value = 2*VALUE_MG_NIHT
                   - VALUE_EG_PAWN
                   + PushToEdge[wkSq];

        return stngColor == pos.active ? +value : -value;
    }

    /// KR vs KP. This is a somewhat tricky endgame to evaluate precisely without a bitbase.
    /// This returns drawish scores when the pawn is far advanced with support of the king,
    /// while the attacking king is far away.
    template<> Value Endgame<KRKP>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

        auto skSq = relSq(stngColor, pos.square(stngColor|KING));
        auto srSq = relSq(stngColor, pos.square(stngColor|ROOK));
        auto wkSq = relSq(stngColor, pos.square(weakColor|KING));
        auto wpSq = relSq(stngColor, pos.square(weakColor|PAWN));

        auto promoteSq = makeSquare(sFile(wpSq), RANK_1);

        Value value;

        // If the strong side's king is in front of the pawn, it's a win. or
        // If the weak side's king is too far from the pawn and the rook, it's a win.
        if (   contains(frontSquares(WHITE, skSq), wpSq)
            || (   3 <= dist(wkSq, wpSq) - (weakColor == pos.active)
                && 3 <= dist(wkSq, srSq)))
        {
            value = VALUE_EG_ROOK
                  - dist(skSq, wpSq);
        }
        else
        // If the pawn is far advanced and supported by the defending king, it's a drawish.
        if (   RANK_3 >= sRank(wkSq)
            && 1 == dist(wkSq, wpSq)
            && RANK_4 <= sRank(skSq)
            && 2 < dist(skSq, wpSq) - (stngColor == pos.active))
        {
            value = Value(80)
                  - 8 * dist(skSq, wpSq);
        }
        else
        {
            value = Value(200)
                  - 8 * (dist(skSq, wpSq+SOUTH)
                       - dist(wkSq, wpSq+SOUTH)
                       - dist(wpSq, promoteSq));
        }

        return stngColor == pos.active ? +value : -value;
    }

    /// KR vs KB. This is very simple, and always returns drawish scores.
    /// The score is slightly bigger when the defending king is close to the edge.
    template<> Value Endgame<KRKB>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
            && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

        //auto skSq = pos.square(stngColor|KING);
        auto wkSq = pos.square(weakColor|KING);
        //auto wbSq = pos.square(weakColor|BSHP);

        //// To draw, the weak side's king should run towards the corner.
        //// And not just any corner! Only a corner that's not the same color as the bishop will do.
        //if (   contains((FABB|FHBB)&(R1BB|R8BB), wkSq)
        //    && oppositeColor(wkSq, wbSq)
        //    && 1 == dist(wkSq, wbSq)
        //    && 1 <  dist(skSq, wbSq))
        //{
        //    return VALUE_DRAW;
        //}

        auto value = Value(PushToEdge[wkSq]);

        return stngColor == pos.active ? +value : -value;
    }

    /// KR vs KN. The attacking side has slightly better winning chances than
    /// in KR vs KB, particularly if the king and the knight are far apart.
    template<> Value Endgame<KRKN>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
            && verifyMaterial(pos, weakColor, VALUE_MG_NIHT, 0));

        //auto skSq = pos.square(stngColor|KING);
        auto wkSq = pos.square(weakColor|KING);
        auto wnSq = pos.square(weakColor|NIHT);

        //// If weak king is near the knight, it's a draw.
        //if (   dist(wkSq, wnSq) <= 3 - (stngColor == pos.active)
        //    && dist(skSq, wnSq) > 1)
        //{
        //    return VALUE_DRAW;
        //}

        auto value = Value(PushToEdge[wkSq]
                         + PushAway[dist(wkSq, wnSq)]);

        return stngColor == pos.active ? +value : -value;
    }

    /// KQ vs KP. In general, this is a win for the strong side, but there are a
    /// few important exceptions. A pawn on 7th rank and on the A,C,F or H files
    /// with a king positioned next to it can be a draw, so in that case, only
    /// use the distance between the kings.
    template<> Value Endgame<KQKP>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_QUEN, 0)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

        auto skSq = pos.square(stngColor|KING);
        auto wkSq = pos.square(weakColor|KING);
        auto wpSq = pos.square(weakColor|PAWN);

        auto value = Value(PushClose[dist(skSq, wkSq)]);

        if (   RANK_7 != relRank(weakColor, wpSq)
            || 1 != dist(wkSq, wpSq)
            || !contains(FABB|FCBB|FFBB|FHBB, wpSq))
        {
            value += VALUE_EG_QUEN
                   - VALUE_EG_PAWN;
        }

        return stngColor == pos.active ? +value : -value;
    }

    /// KQ vs KR. This is almost identical to KX vs K: give the attacking
    /// king a bonus for having the kings close together, and for forcing the
    /// defending king towards the edge. If also take care to avoid null move for
    /// the defending side in the search, this is usually sufficient to win KQ vs KR.
    template<> Value Endgame<KQKR>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_QUEN, 0)
            && verifyMaterial(pos, weakColor, VALUE_MG_ROOK, 0));

        auto skSq = pos.square(stngColor|KING);
        auto wkSq = pos.square(weakColor|KING);

        auto value = VALUE_EG_QUEN
                   - VALUE_EG_ROOK
                   + PushToEdge[wkSq]
                   + PushClose[dist(skSq, wkSq)];

        return stngColor == pos.active ? +value : -value;
    }

    /// Special Scaling functions

    /// KRP vs KR. This function knows a handful of the most important classes of
    /// drawn positions, but is far from perfect. It would probably be a good idea
    /// to add more knowledge in the future.
    ///
    /// It would also be nice to rewrite the actual code for this function,
    /// which is mostly copied from Glaurung 1.x, and isn't very pretty.
    template<> Scale Endgame<KRPKR>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 1)
            && verifyMaterial(pos, weakColor, VALUE_MG_ROOK, 0));

        // Assume stngColor is white and the pawn is on files A-D
        auto skSq = normalize(pos, stngColor, pos.square(stngColor|KING));
        auto srSq = normalize(pos, stngColor, pos.square(stngColor|ROOK));
        auto spSq = normalize(pos, stngColor, pos.square(stngColor|PAWN));
        auto wkSq = normalize(pos, stngColor, pos.square(weakColor|KING));
        auto wrSq = normalize(pos, stngColor, pos.square(weakColor|ROOK));

        auto spF = sFile(spSq);
        auto spR = sRank(spSq);
        auto promoteSq = makeSquare(spF, RANK_8);
        i32 tempo = (stngColor == pos.active);

        // If the pawn is not too far advanced and the defending king defends the
        // queening square, use the third-rank defense.
        if (   RANK_5 >= spR
            && SQ_H5 >= skSq
            && 1 >= dist(wkSq, promoteSq)
            && (   RANK_6 == sRank(wrSq)
                || (   RANK_3 >= spR
                    && RANK_6 != sRank(srSq))))
        {
            return SCALE_DRAW;
        }
        // The defending side saves a draw by checking from behind in case the pawn
        // has advanced to the 6th rank with the king behind.
        if (   RANK_6 == spR
            && 1 >= dist(wkSq, promoteSq)
            && RANK_6 >= sRank(skSq) + tempo
            && (   RANK_1 == sRank(wrSq)
                || (   0 == tempo
                    && 3 <= dist<File>(wrSq, spSq))))
        {
            return SCALE_DRAW;
        }
        //
        if (   RANK_6 <= spR
            && wkSq == promoteSq
            && RANK_1 == sRank(wrSq)
            && (   0 == tempo
                || 2 <= dist(skSq, spSq)))
        {
            return SCALE_DRAW;
        }
        // White pawn on a7 and rook on a8 is a draw if black king is on g7 or h7
        // and the black rook is behind the pawn.
        if (   SQ_A7 == spSq
            && SQ_A8 == srSq
            && (   SQ_H7 == wkSq
                || SQ_G7 == wkSq)
            && FILE_A == sFile(wrSq)
            && (   RANK_3 >= sRank(wrSq)
                || FILE_D <= sFile(skSq)
                || RANK_5 >= sRank(skSq)))
        {
            return SCALE_DRAW;
        }
        // If the defending king blocks the pawn and the attacking king is too far away, it's a draw.
        if (   RANK_5 >= spR
            && wkSq == spSq+NORTH
            && 2 <= dist(skSq, spSq) - tempo
            && 2 <= dist(skSq, wrSq) - tempo)
        {
            return SCALE_DRAW;
        }
        // Pawn on the 7th rank supported by the rook from behind usually wins if the
        // attacking king is closer to the queening square than the defending king,
        // and the defending king cannot gain tempo by threatening the attacking rook.
        if (   RANK_7 == spR
            && FILE_A != spF
            && spF == sFile(srSq)
            && srSq != promoteSq
            && dist(skSq, promoteSq) < dist(wkSq, promoteSq) - 2 + tempo
            && dist(skSq, promoteSq) < dist(wkSq, srSq) + tempo)
        {
            return Scale(SCALE_MAX
                       - 2 * dist(skSq, promoteSq));
        }
        // Similar to the above, but with the pawn further back
        if (   FILE_A != spF
            && spF == sFile(srSq)
            && srSq < spSq
            && dist(skSq, promoteSq) < dist(wkSq, promoteSq) - 2 + tempo
            && dist(skSq, spSq+NORTH) < dist(wkSq, spSq+NORTH) - 2 + tempo
            && (   3 <= dist(wkSq, srSq) + tempo
                || (   dist(skSq, promoteSq) < dist(wkSq, srSq) + tempo
                    && dist(skSq, spSq+NORTH) < dist(wkSq, srSq) + tempo)))
        {
            return Scale(SCALE_MAX
                       - 8 * dist(spSq, promoteSq)
                       - 2 * dist(skSq, promoteSq));
        }
        // If the pawn is not far advanced, and the defending king is somewhere in
        // the pawn's path, it's probably a draw.
        if (   RANK_4 >= spR
            && wkSq > spSq)
        {
            if (sFile(wkSq) == sFile(spSq))
            {
                return Scale(10);
            }
            if (   1 == dist<File>(wkSq, spSq)
                && 2 <  dist(skSq, wkSq))
            {
                return Scale(24
                           - 2 * dist(skSq, wkSq));
            }
        }

        return SCALE_NONE;
    }

    /// KRP vs KB.
    template<> Scale Endgame<KRPKB>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 1)
            && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

        // If rook pawns
        if (0 != (pos.pieces(PAWN) & (FABB|FHBB)))
        {
            auto wkSq = pos.square(weakColor|KING);
            auto wbSq = pos.square(weakColor|BSHP);
            auto spSq = pos.square(stngColor|PAWN);
            auto spR = relRank(stngColor, spSq);

            // If the pawn is on the 5th rank and the pawn (currently) is on the
            // same color square as the bishop then there is a chance of a fortress.
            // Depending on the king position give a moderate reduction or a strong one
            // if the defending king is near the corner but not trapped there.
            if (   RANK_5 == spR
                && !oppositeColor(wbSq, spSq))
            {
                auto d = dist(spSq + 3 * pawnPush(stngColor), wkSq);
                return d <= 2
                    && (   0 != d
                        || wkSq != pos.square(stngColor|KING) + 2 * pawnPush(stngColor)) ?
                            Scale(24) :
                            Scale(48);
            }
            // When the pawn has moved to the 6th rank can be fairly sure it's drawn
            // if the bishop attacks the square in front of the pawn from a reasonable distance
            // and the defending king is near the corner
            if (   RANK_6 == spR
                && 1 >= dist(spSq + 2 * pawnPush(stngColor), wkSq)
                && contains(PieceAttacks[BSHP][wbSq], spSq + pawnPush(stngColor))
                && 2 <= dist<File>(wbSq, spSq))
            {
                return Scale(8);
            }
        }

        return SCALE_NONE;
    }

    /// KRPP vs KRP. If the defending king is actively placed, the position is drawish.
    template<> Scale Endgame<KRPPKRP>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 2)
            && verifyMaterial(pos, weakColor, VALUE_MG_ROOK, 1));

        auto sp1Sq = pos.square(stngColor|PAWN, 0);
        auto sp2Sq = pos.square(stngColor|PAWN, 1);
        auto wkSq = pos.square(weakColor|KING);

        // Does the stronger side have a passed pawn?
        if (   pos.pawnPassedAt(stngColor, sp1Sq)
            || pos.pawnPassedAt(stngColor, sp2Sq))
        {
            return SCALE_NONE;
        }
        auto spR = std::max(relRank(stngColor, sp1Sq),
                            relRank(stngColor, sp2Sq));
        if (   1 >= dist<File>(wkSq, sp1Sq)
            && 1 >= dist<File>(wkSq, sp2Sq)
            && spR < relRank(stngColor, wkSq))
        {
            assert(RANK_1 < spR && spR < RANK_7);
            return RankScale[spR];
        }

        return SCALE_NONE;
    }

    /// KNP vs K. There is a single rule: if the pawn is a rook pawn on the 7th rank
    /// and the defending king prevents the pawn from advancing the position is drawn.
    template<> Scale Endgame<KNPK>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_NIHT, 1)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

        // Assume stngColor is white and the pawn is on files A-D
        auto spSq = normalize(pos, stngColor, pos.square(stngColor|PAWN));
        auto wkSq = normalize(pos, stngColor, pos.square(weakColor|KING));

        if (   SQ_A7 == spSq
            && 1 >= dist(wkSq, SQ_A8))
        {
            return SCALE_DRAW;
        }

        return SCALE_NONE;
    }

    /// KBP vs KB. There are two rules: if the defending king is somewhere along the
    /// path of the pawn, and the square of the king is not of the same color as the
    /// strong side's bishop, it's a draw. If the two bishops have opposite color,
    /// it's almost always a draw.
    template<> Scale Endgame<KBPKB>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_BSHP, 1)
            && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

        auto spSq = pos.square(stngColor|PAWN);
        auto sbSq = pos.square(stngColor|BSHP);
        auto wbSq = pos.square(weakColor|BSHP);
        auto wkSq = pos.square(weakColor|KING);

        if (// Opposite colored bishops
               oppositeColor(sbSq, wbSq)
            // Defending king blocks the pawn, and cannot be driven away
            || (   sFile(wkSq) == sFile(spSq)
                && relRank(stngColor, spSq) < relRank(stngColor, wkSq)
                && (   oppositeColor(wkSq, sbSq)
                    || RANK_6 >= relRank(stngColor, wkSq))))
        {
            return SCALE_DRAW;
        }

        return SCALE_NONE;
    }

    /// KBPP vs KB. It detects a few basic draws with opposite-colored bishops.
    template<> Scale Endgame<KBPPKB>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_BSHP, 2)
            && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

        auto sbSq = pos.square(stngColor|BSHP);
        auto wbSq = pos.square(weakColor|BSHP);

        if (oppositeColor(sbSq, wbSq))
        {
            auto sp1Sq = pos.square(stngColor|PAWN, 0);
            auto sp2Sq = pos.square(stngColor|PAWN, 1);
            auto wkSq  = pos.square(weakColor|KING);

            Square block1Sq
                 , block2Sq;

            if (relRank(stngColor, sp1Sq) > relRank(stngColor, sp2Sq))
            {
                block1Sq = sp1Sq + pawnPush(stngColor);
                block2Sq = makeSquare(sFile(sp2Sq), sRank(sp1Sq));
            }
            else
            {
                block1Sq = sp2Sq + pawnPush(stngColor);
                block2Sq = makeSquare(sFile(sp1Sq), sRank(sp2Sq));
            }

            switch (dist<File>(sp1Sq, sp2Sq))
            {
            // Both pawns are on the same file. It's an easy draw if the defender firmly
            // controls some square in the front most pawn's path.
            case 0:
                if (   sFile(wkSq) == sFile(block1Sq)
                    && relRank(stngColor, wkSq) >= relRank(stngColor, block1Sq)
                    && oppositeColor(wkSq, sbSq))
                {
                    return SCALE_DRAW;
                }
                break;
            // Pawns on adjacent files. It's a draw if the defender firmly controls the
            // square in front of the front most pawn's path, and the square diagonally
            // behind this square on the file of the other pawn.
            case 1:
                if (oppositeColor(wkSq, sbSq))
                {
                    if (   wkSq == block1Sq
                        && (   wbSq == block2Sq
                            || 0 != (  pos.pieces(weakColor, BSHP)
                                     & attacksBB<BSHP>(block2Sq, pos.pieces()))
                            || 2 <= dist<Rank>(sp1Sq, sp2Sq)))
                    {
                        return SCALE_DRAW;
                    }
                    if (   wkSq == block2Sq
                        && (   wbSq == block1Sq
                            || 0 != (  pos.pieces(weakColor, BSHP)
                                     & attacksBB<BSHP>(block1Sq, pos.pieces()))))
                    {
                        return SCALE_DRAW;
                    }
                }
                break;
            // The pawns are not on the same file or adjacent files. No scaling.
            default:
                return SCALE_NONE;
            }
        }

        return SCALE_NONE;
    }

    /// KBP vs KN. There is a single rule: If the defending king is somewhere along
    /// the path of the pawn, and the square of the king is not of the same color as
    /// the strong side's bishop, it's a draw.
    template<> Scale Endgame<KBPKN>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_BSHP, 1)
            && verifyMaterial(pos, weakColor, VALUE_MG_NIHT, 0));

        auto spSq = pos.square(stngColor|PAWN);
        auto sbSq = pos.square(stngColor|BSHP);
        auto wkSq = pos.square(weakColor|KING);

        if (   sFile(wkSq) == sFile(spSq)
            && relRank(stngColor, spSq) < relRank(stngColor, wkSq)
            && (   oppositeColor(wkSq, sbSq)
                || RANK_6 >= relRank(stngColor, wkSq)))
        {
            return SCALE_DRAW;
        }

        return SCALE_NONE;
    }

    /// KNP vs KB. If knight can block bishop from taking pawn, it's a win.
    /// Otherwise the position is a draw.
    template<> Scale Endgame<KNPKB>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_NIHT, 1)
            && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

        auto spSq = pos.square(stngColor|PAWN);
        auto sbSq = pos.square(weakColor|BSHP);
        auto wkSq = pos.square(weakColor|KING);

        // King needs to get close to promoting pawn to prevent knight from blocking.
        // Rules for this are very tricky, so just approximate.
        if (0 != (  frontSquares(stngColor, spSq)
                  & attacksBB<BSHP>(sbSq, pos.pieces())))
        {
            return Scale(dist(wkSq, spSq));
        }

        return SCALE_NONE;
    }

    /// Generic Scaling functions

    /// KP vs KP. This is done by removing the weakest side's pawn and probing the
    /// KP vs K bitbase: If the weakest side has a draw without the pawn, it probably
    /// has at least a draw with the pawn as well. The exception is when the strong
    /// side's pawn is far advanced and not on a rook file; in this case it is often
    /// possible to win (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
    template<> Scale Endgame<KPKP>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_ZERO, 1)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

        // Assume stngColor is white and the pawn is on files A-D
        auto skSq = normalize(pos, stngColor, pos.square(stngColor|KING));
        auto spSq = normalize(pos, stngColor, pos.square(stngColor|PAWN));
        auto wkSq = normalize(pos, stngColor, pos.square(weakColor|KING));

        // If the pawn has advanced to the fifth rank or further, and is not a rook pawn,
        // then it's too dangerous to assume that it's at least a draw.
        if (   RANK_5 > sRank(spSq)
            || FILE_A == sFile(spSq))
        {
            // Probe the KPK bitbase with the weakest side's pawn removed.
            // If it's a draw, it's probably at least a draw even with the pawn.
            if (!Bitbases::probe(stngColor == pos.active ? WHITE : BLACK, skSq, spSq, wkSq))
            {
                return SCALE_DRAW;
            }
        }

        return SCALE_NONE;
    }

    /// K and two or more pawns vs K. There is just a single rule here: If all pawns
    /// are on the same rook file and are blocked by the defending king, it's a draw.
    template<> Scale Endgame<KPsK>::operator()(const Position &pos) const
    {
        assert(pos.nonPawnMaterial(stngColor) == VALUE_ZERO
            && pos.count(stngColor|PAWN) >= 2
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

        auto wkSq = pos.square(weakColor|KING);
        auto sPawns = pos.pieces(stngColor, PAWN);

        // If all pawns are ahead of the king, all pawns are on a single
        // rook file and the king is within one file of the pawns then draw.
        if (   (   0 == (sPawns & ~FABB)
                || 0 == (sPawns & ~FHBB))
            && 0 == (sPawns & ~frontRanks(weakColor, wkSq))
            && 1 >= dist<File>(wkSq, scanLSq(sPawns)))
        {
            return SCALE_DRAW;
        }

        return SCALE_NONE;
    }

    /// KB and one or more pawns vs K.
    /// It checks for draws with rook pawns and a bishop of the wrong color.
    /// If such a draw is detected, SCALE_DRAW is returned.
    /// If not, the return value is SCALE_NONE, i.e. no scaling will be used.
    template<> Scale Endgame<KBPsK>::operator()(const Position &pos) const
    {
        assert(pos.nonPawnMaterial(stngColor) == VALUE_MG_BSHP
            && pos.count(stngColor|PAWN) != 0);
        // No assertions about the material of weak side, because we want draws to
        // be detected even when the weak side has some materials or pawns.

        auto skSq = pos.square(stngColor|KING);
        auto sbSq = pos.square(stngColor|BSHP);
        auto wkSq = pos.square(weakColor|KING);

        // All pawns of strong side on same A or H file? (rook file)
        // Then potential draw
        Bitboard sPawns = pos.pieces(stngColor, PAWN);
        if (   0 == (sPawns & ~FABB)
            || 0 == (sPawns & ~FHBB))
        {
            auto promoteSq = relSq(stngColor, makeSquare(sFile(scanLSq(sPawns)), RANK_8));

            // The bishop has the wrong color and the defending king defends the queening square.
            if (   oppositeColor(promoteSq, sbSq)
                && 1 >= dist(promoteSq, wkSq))
            {
                return SCALE_DRAW;
            }
        }

        // All pawns on same B or G file?
        // Then potential draw
        Bitboard pawns = pos.pieces(PAWN);
        Bitboard wPawns = pos.pieces(weakColor, PAWN);
        if (   (   0 == (pawns & ~FBBB)
                || 0 == (pawns & ~FGBB))
            && VALUE_ZERO == pos.nonPawnMaterial(weakColor)
            && 0 != wPawns)
        {
            // Get weak side pawn that is closest to home rank
            auto wpSq = scanFrontMostSq(stngColor, wPawns);

            // There's potential for a draw if weak pawn is blocked on the 7th rank
            // and the bishop cannot attack it or only one strong pawn left
            if (   RANK_7 == relRank(stngColor, wpSq)
                && contains(sPawns, wpSq + pawnPush(weakColor))
                && (   oppositeColor(sbSq, wpSq)
                    || 1 == pos.count(stngColor|PAWN)))
            {
                // It's a draw if the weak king is on its back two ranks, within 2
                // squares of the blocking pawn and the strong king is not closer.
                // This rule only fails in practically unreachable
                // positions such as 5k1K/6p1/6P1/8/8/3B4/8/8 w and
                // where Q-search will immediately correct the problem
                // positions such as 8/4k1p1/6P1/1K6/3B4/8/8/8 w
                if (   RANK_7 <= relRank(stngColor, wkSq)
                    && 2 >= dist(wkSq, wpSq)
                    && dist(wkSq, wpSq) <= dist(skSq, wpSq))
                {
                    return SCALE_DRAW;
                }
            }

        }

        return SCALE_NONE;
    }

    /// KQ vs KR and one or more pawns.
    /// It tests for fortress draws with a rook on the 3rd rank defended by a pawn.
    template<> Scale Endgame<KQKRPs>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_QUEN, 0)
            && pos.nonPawnMaterial(weakColor) == VALUE_MG_ROOK
            && pos.count(weakColor|PAWN) != 0);

        auto skSq = pos.square(stngColor|KING);
        auto wkSq = pos.square(weakColor|KING);
        auto wrSq = pos.square(weakColor|ROOK);

        if (   RANK_2 >= relRank(weakColor, wkSq)
            && RANK_4 <= relRank(weakColor, skSq)
            && RANK_3 == relRank(weakColor, wrSq)
            && 0 != (  pos.pieces(weakColor, PAWN)
                     & PieceAttacks[KING][wkSq]
                     & PawnAttacks[stngColor][wrSq]))
        {
            return SCALE_DRAW;
        }

        return SCALE_NONE;
    }

}
