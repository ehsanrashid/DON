#include "Endgame.h"

#include <array>

#include "BitBases.h"
#include "MoveGenerator.h"

namespace Endgames {

    using namespace std;
    using namespace BitBoard;

    EG_MapPair<Value, Scale> EndgameMapPair;

    template<EndgameCode C, typename T = EndgameType<C>>
    void add(const string &code)
    {
        StateInfo si;
        map<T>()[Position().setup(code, WHITE, si).si->matlKey] = EG_Ptr<T>(new Endgame<C>(WHITE));
        map<T>()[Position().setup(code, BLACK, si).si->matlKey] = EG_Ptr<T>(new Endgame<C>(BLACK));
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
        constexpr array<i32, SQ_NO> PushToEdge
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
        constexpr array<i32, SQ_NO> PushToCorner
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
        constexpr array<i32, 8> PushClose { 0, 100,  70,  50,  35,  20,  10,   0 };
        constexpr array<i32, 8> PushAway  { 0,   0,  30,  50,  65,  80,  90, 100 };

        // Pawn Rank based scaling.
        constexpr array<Scale, R_NO> RankScale
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

            if (F_E <= fileOf(pos.square(c|PAWN)))
            {
                sq = !sq;
            }

            return WHITE == c ? sq : ~sq;
        }

#if !defined(NDEBUG)

        bool verifyMaterial(const Position &pos, Color c, Value npm, i32 pawn_count)
        {
            return pos.nonpawnMaterial(c) == npm
                && pos.count(c|PAWN) == pawn_count;
        }

#endif

    }

    /// Mate with KX vs K. This gives the attacking side a bonus
    /// for driving the defending king towards the edge of the board and
    /// for keeping the distance between the two kings small.
    template<> Value Endgame<KXK>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, weakColor, VALUE_ZERO, 0));
        assert(0 == pos.si->checkers); // Eval is never called when in check
        // Stalemate detection with lone weak king
        if (   weakColor == pos.active
            && 0 == MoveList<GenType::LEGAL>(pos).size())
        {
            return VALUE_DRAW;
        }

        auto sk_sq = pos.square(stngColor|KING);
        auto wk_sq = pos.square(weakColor|KING);

        auto value = std::min(pos.count(stngColor|PAWN)*VALUE_EG_PAWN
                             + pos.nonpawnMaterial(stngColor)
                             + PushToEdge[wk_sq]
                             + PushClose[dist(sk_sq, wk_sq)],
                               +VALUE_KNOWN_WIN - 1);

        if (   0 < pos.count(stngColor|QUEN)
            || 0 < pos.count(stngColor|ROOK)
            || pos.bishopPaired(stngColor)
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
        auto sk_sq = normalize(pos, stngColor, pos.square(stngColor|KING));
        auto sp_sq = normalize(pos, stngColor, pos.square(stngColor|PAWN));
        auto wk_sq = normalize(pos, stngColor, pos.square(weakColor|KING));

        if (!BitBases::probe(stngColor == pos.active ? WHITE : BLACK, sk_sq, sp_sq, wk_sq))
        {
            return VALUE_DRAW;
        }

        auto value = VALUE_KNOWN_WIN
                   + VALUE_EG_PAWN
                   + rankOf(sp_sq);

        return stngColor == pos.active ? +value : -value;
    }

    /// Mate with KBN vs K. This is similar to KX vs K, but have to drive the
    /// defending king towards a corner square of the attacking bishop attacks.
    template<> Value Endgame<KBNK>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_NIHT + VALUE_MG_BSHP, 0)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

        auto sk_sq = pos.square(stngColor|KING);
        auto sb_sq = pos.square(stngColor|BSHP);
        auto wk_sq = pos.square(weakColor|KING);

        // If Bishop does not attack A1/H8, flip the enemy king square to drive to opposite corners (A8/H1).
        auto value = VALUE_KNOWN_WIN
                   + PushClose[dist(sk_sq, wk_sq)]
                   + 32*PushToCorner[oppositeColor(sb_sq, SQ_A1) ? ~wk_sq : wk_sq];
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

        auto wk_sq = pos.square(weakColor|KING);

        auto value = 2*VALUE_MG_NIHT
                   - VALUE_EG_PAWN
                   + PushToEdge[wk_sq];

        return stngColor == pos.active ? +value : -value;
    }

    /// KR vs KP. This is a somewhat tricky endgame to evaluate precisely without a bitbase.
    /// This returns drawish scores when the pawn is far advanced with support of the king,
    /// while the attacking king is far away.
    template<> Value Endgame<KRKP>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 1));

        auto sk_sq = relSq(stngColor, pos.square(stngColor|KING));
        auto sr_sq = relSq(stngColor, pos.square(stngColor|ROOK));
        auto wk_sq = relSq(stngColor, pos.square(weakColor|KING));
        auto wp_sq = relSq(stngColor, pos.square(weakColor|PAWN));

        auto promote_sq = fileOf(wp_sq)|R_1;

        Value value;

        // If the strong side's king is in front of the pawn, it's a win. or
        // If the weak side's king is too far from the pawn and the rook, it's a win.
        if (   contains(frontSquares(WHITE, sk_sq), wp_sq)
            || (   3 <= dist(wk_sq, wp_sq) - (weakColor == pos.active)
                && 3 <= dist(wk_sq, sr_sq)))
        {
            value = VALUE_EG_ROOK
                  - dist(sk_sq, wp_sq);
        }
        else
        // If the pawn is far advanced and supported by the defending king, it's a drawish.
        if (   R_3 >= rankOf(wk_sq)
            && 1 == dist(wk_sq, wp_sq)
            && R_4 <= rankOf(sk_sq)
            && 2 < dist(sk_sq, wp_sq) - (stngColor == pos.active))
        {
            value = Value(80)
                  - 8 * dist(sk_sq, wp_sq);
        }
        else
        {
            value = Value(200)
                  - 8 * (dist(sk_sq, wp_sq+DEL_S)
                       - dist(wk_sq, wp_sq+DEL_S)
                       - dist(wp_sq, promote_sq));
        }

        return stngColor == pos.active ? +value : -value;
    }

    /// KR vs KB. This is very simple, and always returns drawish scores.
    /// The score is slightly bigger when the defending king is close to the edge.
    template<> Value Endgame<KRKB>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
            && verifyMaterial(pos, weakColor, VALUE_MG_BSHP, 0));

        //auto sk_sq = pos.square(stngColor|KING);
        auto wk_sq = pos.square(weakColor|KING);
        //auto wb_sq = pos.square(weakColor|BSHP);

        //// To draw, the weak side's king should run towards the corner.
        //// And not just any corner! Only a corner that's not the same color as the bishop will do.
        //if (   contains((FABB|FHBB)&(R1BB|R8BB), wk_sq)
        //    && oppositeColor(wk_sq, wb_sq)
        //    && 1 == dist(wk_sq, wb_sq)
        //    && 1 <  dist(sk_sq, wb_sq))
        //{
        //    return VALUE_DRAW;
        //}

        auto value = Value(PushToEdge[wk_sq]);

        return stngColor == pos.active ? +value : -value;
    }

    /// KR vs KN. The attacking side has slightly better winning chances than
    /// in KR vs KB, particularly if the king and the knight are far apart.
    template<> Value Endgame<KRKN>::operator()(const Position &pos) const
    {
        assert(verifyMaterial(pos, stngColor, VALUE_MG_ROOK, 0)
            && verifyMaterial(pos, weakColor, VALUE_MG_NIHT, 0));

        //auto sk_sq = pos.square(stngColor|KING);
        auto wk_sq = pos.square(weakColor|KING);
        auto wn_sq = pos.square(weakColor|NIHT);

        //// If weak king is near the knight, it's a draw.
        //if (   dist(wk_sq, wn_sq) <= 3 - (stngColor == pos.active)
        //    && dist(sk_sq, wn_sq) > 1)
        //{
        //    return VALUE_DRAW;
        //}

        auto value = Value(PushToEdge[wk_sq]
                         + PushAway[dist(wk_sq, wn_sq)]);

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

        auto sk_sq = pos.square(stngColor|KING);
        auto wk_sq = pos.square(weakColor|KING);
        auto wp_sq = pos.square(weakColor|PAWN);

        auto value = Value(PushClose[dist(sk_sq, wk_sq)]);

        if (   R_7 != relRank(weakColor, wp_sq)
            || 1 != dist(wk_sq, wp_sq)
            || !contains(FABB|FCBB|FFBB|FHBB, wp_sq))
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

        auto sk_sq = pos.square(stngColor|KING);
        auto wk_sq = pos.square(weakColor|KING);

        auto value = VALUE_EG_QUEN
                   - VALUE_EG_ROOK
                   + PushToEdge[wk_sq]
                   + PushClose[dist(sk_sq, wk_sq)];

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
        auto sk_sq = normalize(pos, stngColor, pos.square(stngColor|KING));
        auto sr_sq = normalize(pos, stngColor, pos.square(stngColor|ROOK));
        auto sp_sq = normalize(pos, stngColor, pos.square(stngColor|PAWN));
        auto wk_sq = normalize(pos, stngColor, pos.square(weakColor|KING));
        auto wr_sq = normalize(pos, stngColor, pos.square(weakColor|ROOK));

        auto sp_f = fileOf(sp_sq);
        auto sp_r = rankOf(sp_sq);
        auto promote_sq = sp_f|R_8;
        i32 tempo = (stngColor == pos.active);

        // If the pawn is not too far advanced and the defending king defends the
        // queening square, use the third-rank defense.
        if (   R_5 >= sp_r
            && SQ_H5 >= sk_sq
            && 1 >= dist(wk_sq, promote_sq)
            && (   R_6 == rankOf(wr_sq)
                || (   R_3 >= sp_r
                    && R_6 != rankOf(sr_sq))))
        {
            return SCALE_DRAW;
        }
        // The defending side saves a draw by checking from behind in case the pawn
        // has advanced to the 6th rank with the king behind.
        if (   R_6 == sp_r
            && 1 >= dist(wk_sq, promote_sq)
            && R_6 >= rankOf(sk_sq) + tempo
            && (   R_1 == rankOf(wr_sq)
                || (   0 == tempo
                    && 3 <= dist<File>(wr_sq, sp_sq))))
        {
            return SCALE_DRAW;
        }
        //
        if (   R_6 <= sp_r
            && wk_sq == promote_sq
            && R_1 == rankOf(wr_sq)
            && (   0 == tempo
                || 2 <= dist(sk_sq, sp_sq)))
        {
            return SCALE_DRAW;
        }
        // White pawn on a7 and rook on a8 is a draw if black king is on g7 or h7
        // and the black rook is behind the pawn.
        if (   SQ_A7 == sp_sq
            && SQ_A8 == sr_sq
            && (   SQ_H7 == wk_sq
                || SQ_G7 == wk_sq)
            && F_A == fileOf(wr_sq)
            && (   R_3 >= rankOf(wr_sq)
                || F_D <= fileOf(sk_sq)
                || R_5 >= rankOf(sk_sq)))
        {
            return SCALE_DRAW;
        }
        // If the defending king blocks the pawn and the attacking king is too far away, it's a draw.
        if (   R_5 >= sp_r
            && wk_sq == sp_sq+DEL_N
            && 2 <= dist(sk_sq, sp_sq) - tempo
            && 2 <= dist(sk_sq, wr_sq) - tempo)
        {
            return SCALE_DRAW;
        }
        // Pawn on the 7th rank supported by the rook from behind usually wins if the
        // attacking king is closer to the queening square than the defending king,
        // and the defending king cannot gain tempo by threatening the attacking rook.
        if (   R_7 == sp_r
            && F_A != sp_f
            && sp_f == fileOf(sr_sq)
            && sr_sq != promote_sq
            && dist(sk_sq, promote_sq) < dist(wk_sq, promote_sq) - 2 + tempo
            && dist(sk_sq, promote_sq) < dist(wk_sq, sr_sq) + tempo)
        {
            return Scale(SCALE_MAX
                       - 2 * dist(sk_sq, promote_sq));
        }
        // Similar to the above, but with the pawn further back
        if (   F_A != sp_f
            && sp_f == fileOf(sr_sq)
            && sr_sq < sp_sq
            && dist(sk_sq, promote_sq) < dist(wk_sq, promote_sq) - 2 + tempo
            && dist(sk_sq, sp_sq+DEL_N) < dist(wk_sq, sp_sq+DEL_N) - 2 + tempo
            && (   3 <= dist(wk_sq, sr_sq) + tempo
                || (   dist(sk_sq, promote_sq) < dist(wk_sq, sr_sq) + tempo
                    && dist(sk_sq, sp_sq+DEL_N) < dist(wk_sq, sr_sq) + tempo)))
        {
            return Scale(SCALE_MAX
                       - 8 * dist(sp_sq, promote_sq)
                       - 2 * dist(sk_sq, promote_sq));
        }
        // If the pawn is not far advanced, and the defending king is somewhere in
        // the pawn's path, it's probably a draw.
        if (   R_4 >= sp_r
            && wk_sq > sp_sq)
        {
            if (fileOf(wk_sq) == fileOf(sp_sq))
            {
                return Scale(10);
            }
            if (   1 == dist<File>(wk_sq, sp_sq)
                && 2 <  dist(sk_sq, wk_sq))
            {
                return Scale(24
                           - 2 * dist(sk_sq, wk_sq));
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
        if (0 != ((FABB|FHBB) & pos.pieces(PAWN)))
        {
            auto sp_sq = pos.square(stngColor|PAWN);
            auto sp_r  = relRank(stngColor, sp_sq);
            auto wk_sq = pos.square(weakColor|KING);
            auto wb_sq = pos.square(weakColor|BSHP);

            // If the pawn is on the 5th rank and the pawn (currently) is on the
            // same color square as the bishop then there is a chance of a fortress.
            // Depending on the king position give a moderate reduction or a strong one
            // if the defending king is near the corner but not trapped there.
            if (   R_5 == sp_r
                && !oppositeColor(wb_sq, sp_sq))
            {
                auto d = dist(sp_sq + 3*pawnPush(stngColor), wk_sq);
                return d <= 2
                    && (   0 != d
                        || wk_sq != pos.square(stngColor|KING) + 2*pawnPush(stngColor)) ?
                            Scale(24) :
                            Scale(48);
            }
            // When the pawn has moved to the 6th rank can be fairly sure it's drawn
            // if the bishop attacks the square in front of the pawn from a reasonable distance
            // and the defending king is near the corner
            if (   R_6 == sp_r
                && 1 >= dist(sp_sq + 2*pawnPush(stngColor), wk_sq)
                && contains(PieceAttacks[BSHP][wb_sq], sp_sq + pawnPush(stngColor))
                && 2 <= dist<File>(wb_sq, sp_sq))
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

        auto wk_sq  = pos.square(weakColor|KING);
        auto sp1_sq = pos.square(stngColor|PAWN, 0);
        auto sp2_sq = pos.square(stngColor|PAWN, 1);

        // Does the stronger side have a passed pawn?
        if (   pos.pawnPassedAt(stngColor, sp1_sq)
            || pos.pawnPassedAt(stngColor, sp2_sq))
        {
            return SCALE_NONE;
        }
        auto sp_r = std::max(relRank(stngColor, sp1_sq), relRank(stngColor, sp2_sq));
        if (   1 >= dist<File>(wk_sq, sp1_sq)
            && 1 >= dist<File>(wk_sq, sp2_sq)
            && sp_r < relRank(stngColor, wk_sq))
        {
            assert(R_1 < sp_r && sp_r < R_7);
            return RankScale[sp_r];
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
        auto sp_sq = normalize(pos, stngColor, pos.square(stngColor|PAWN));
        auto wk_sq = normalize(pos, stngColor, pos.square(weakColor|KING));

        if (   SQ_A7 == sp_sq
            && 1 >= dist(wk_sq, SQ_A8))
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

        auto sp_sq = pos.square(stngColor|PAWN);
        auto sb_sq = pos.square(stngColor|BSHP);
        auto wb_sq = pos.square(weakColor|BSHP);
        auto wk_sq = pos.square(weakColor|KING);

        if (// Opposite colored bishops
               oppositeColor(sb_sq, wb_sq)
            // Defending king blocks the pawn, and cannot be driven away
            || (   fileOf(wk_sq) == fileOf(sp_sq)
                && relRank(stngColor, sp_sq) < relRank(stngColor, wk_sq)
                && (   oppositeColor(wk_sq, sb_sq)
                    || R_6 >= relRank(stngColor, wk_sq))))
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

        auto sb_sq = pos.square(stngColor|BSHP);
        auto wb_sq = pos.square(weakColor|BSHP);

        if (oppositeColor(sb_sq, wb_sq))
        {
            auto sp1_sq = pos.square(stngColor|PAWN, 0);
            auto sp2_sq = pos.square(stngColor|PAWN, 1);
            auto wk_sq  = pos.square(weakColor|KING);

            Square block1_sq
                 , block2_sq;

            if (relRank(stngColor, sp1_sq) > relRank(stngColor, sp2_sq))
            {
                block1_sq = sp1_sq + pawnPush(stngColor);
                block2_sq = fileOf(sp2_sq)|rankOf(sp1_sq);
            }
            else
            {
                block1_sq = sp2_sq + pawnPush(stngColor);
                block2_sq = fileOf(sp1_sq)|rankOf(sp2_sq);
            }

            switch (dist<File>(sp1_sq, sp2_sq))
            {
            // Both pawns are on the same file. It's an easy draw if the defender firmly
            // controls some square in the front most pawn's path.
            case 0:
                if (   fileOf(wk_sq) == fileOf(block1_sq)
                    && relRank(stngColor, wk_sq) >= relRank(stngColor, block1_sq)
                    && oppositeColor(wk_sq, sb_sq))
                {
                    return SCALE_DRAW;
                }
                break;
            // Pawns on adjacent files. It's a draw if the defender firmly controls the
            // square in front of the front most pawn's path, and the square diagonally
            // behind this square on the file of the other pawn.
            case 1:
                if (oppositeColor(wk_sq, sb_sq))
                {
                    if (   wk_sq == block1_sq
                        && (   wb_sq == block2_sq
                            || 0 != (  pos.pieces(weakColor, BSHP)
                                     & attacksBB<BSHP>(block2_sq, pos.pieces()))
                            || 2 <= dist<Rank>(sp1_sq, sp2_sq)))
                    {
                        return SCALE_DRAW;
                    }
                    if (   wk_sq == block2_sq
                        && (   wb_sq == block1_sq
                            || 0 != (  pos.pieces(weakColor, BSHP)
                                     & attacksBB<BSHP>(block1_sq, pos.pieces()))))
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

        auto sp_sq = pos.square(stngColor|PAWN);
        auto sb_sq = pos.square(stngColor|BSHP);
        auto wk_sq = pos.square(weakColor|KING);

        if (   fileOf(wk_sq) == fileOf(sp_sq)
            && relRank(stngColor, sp_sq) < relRank(stngColor, wk_sq)
            && (   oppositeColor(wk_sq, sb_sq)
                || R_6 >= relRank(stngColor, wk_sq)))
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

        auto sp_sq = pos.square(stngColor|PAWN);
        auto sb_sq = pos.square(weakColor|BSHP);
        auto wk_sq = pos.square(weakColor|KING);

        // King needs to get close to promoting pawn to prevent knight from blocking.
        // Rules for this are very tricky, so just approximate.
        if (0 != (  frontSquares(stngColor, sp_sq)
                  & attacksBB<BSHP>(sb_sq, pos.pieces())))
        {
            return Scale(dist(wk_sq, sp_sq));
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
        auto sk_sq = normalize(pos, stngColor, pos.square(stngColor|KING));
        auto sp_sq = normalize(pos, stngColor, pos.square(stngColor|PAWN));
        auto wk_sq = normalize(pos, stngColor, pos.square(weakColor|KING));

        // If the pawn has advanced to the fifth rank or further, and is not a rook pawn,
        // then it's too dangerous to assume that it's at least a draw.
        if (   R_5 > rankOf(sp_sq)
            || F_A == fileOf(sp_sq))
        {
            // Probe the KPK bitbase with the weakest side's pawn removed.
            // If it's a draw, it's probably at least a draw even with the pawn.
            if (!BitBases::probe(stngColor == pos.active ? WHITE : BLACK, sk_sq, sp_sq, wk_sq))
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
        assert(pos.nonpawnMaterial(stngColor) == VALUE_ZERO
            && pos.count(stngColor|PAWN) >= 2
            && verifyMaterial(pos, weakColor, VALUE_ZERO, 0));

        auto wk_sq  = pos.square(weakColor|KING);
        auto spawns = pos.pieces(stngColor, PAWN);

        // If all pawns are ahead of the king, all pawns are on a single
        // rook file and the king is within one file of the pawns then draw.
        if (   0 == (spawns & ~frontRanks(weakColor, wk_sq))
            && (   0 == (spawns & ~FABB)
                || 0 == (spawns & ~FHBB))
            && 1 >= dist<File>(wk_sq, scanLSq(spawns)))
        {
            return SCALE_DRAW;
        }

        return SCALE_NONE;
    }

    /// KB and one or more pawns vs K and zero or more pawns.
    /// It checks for draws with rook pawns and a bishop of the wrong color.
    /// If such a draw is detected, SCALE_DRAW is returned.
    /// If not, the return value is SCALE_NONE, i.e. no scaling will be used.
    template<> Scale Endgame<KBPsKP>::operator()(const Position &pos) const
    {
        assert(pos.nonpawnMaterial(stngColor) == VALUE_MG_BSHP
            && pos.count(stngColor|PAWN) != 0
            && pos.count(weakColor|PAWN) >= 0);
        // No assertions about the material of weak side, because we want draws to
        // be detected even when the weak side has some materials or pawns.

        auto spawns = pos.pieces(stngColor, PAWN);
        auto sp_sq = scanLSq(spawns);
        auto sp_f = fileOf(sp_sq);

        // All pawns of strong side on same A or H file? (rook file)
        // Then potential draw
        if (   contains(FABB | FHBB, sp_sq)
            && 0 == (spawns & ~fileBB(sp_f)))
        {
            auto sb_sq = pos.square(stngColor|BSHP);
            auto promote_sq = relSq(stngColor, sp_f|R_8);
            auto wk_sq = pos.square(weakColor|KING);

            // The bishop has the wrong color and the defending king defends the queening square.
            if (   oppositeColor(promote_sq, sb_sq)
                && 1 >= dist(promote_sq, wk_sq))
            {
                return SCALE_DRAW;
            }
        }

        // All pawns on same B or G file?
        // Then potential draw
        if (   contains(FBBB | FGBB, sp_sq)
            && 0 == (pos.pieces(PAWN) & ~fileBB(sp_f))
            && VALUE_ZERO == pos.nonpawnMaterial(weakColor))
        {
            auto sk_sq = pos.square(stngColor|KING);
            auto sb_sq = pos.square(stngColor|BSHP);
            auto wk_sq = pos.square(weakColor|KING);

            Bitboard wpawns = pos.pieces(weakColor, PAWN);

            if (0 != wpawns)
            {
                // Get weak side pawn that is closest to home rank
                auto wp_sq = scanFrontMostSq(stngColor, wpawns);

                // There's potential for a draw if weak pawn is blocked on the 7th rank
                // and the bishop cannot attack it or they only have one pawn left
                if (   R_7 == relRank(stngColor, wp_sq)
                    && contains(spawns, wp_sq + pawnPush(weakColor))
                    && (   oppositeColor(sb_sq, wp_sq)
                        || 1 == pos.count(stngColor|PAWN)))
                {
                    // It's a draw if the weak king is on its back two ranks, within 2
                    // squares of the blocking pawn and the strong king is not closer.
                    // This rule only fails in practically unreachable
                    // positions such as 5k1K/6p1/6P1/8/8/3B4/8/8 w and
                    // where Q-search will immediately correct the problem
                    // positions such as 8/4k1p1/6P1/1K6/3B4/8/8/8 w
                    if (   R_7 <= relRank(stngColor, wk_sq)
                        && 2 >= dist(wk_sq, wp_sq)
                        && dist(wk_sq, wp_sq) <= dist(sk_sq, wp_sq))
                    {
                        return SCALE_DRAW;
                    }
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
            && pos.nonpawnMaterial(weakColor) == VALUE_MG_ROOK
            && pos.count(weakColor|PAWN) != 0);

        auto sk_sq = pos.square(stngColor|KING);
        auto wk_sq = pos.square(weakColor|KING);
        auto wr_sq = pos.square(weakColor|ROOK);

        if (   R_2 >= relRank(weakColor, wk_sq)
            && R_4 <= relRank(weakColor, sk_sq)
            && R_3 == relRank(weakColor, wr_sq)
            && 0 != (  pos.pieces(weakColor, PAWN)
                     & PieceAttacks[KING][wk_sq]
                     & PawnAttacks[stngColor][wr_sq]))
        {
            return SCALE_DRAW;
        }

        return SCALE_NONE;
    }

}
