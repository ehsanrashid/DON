#include "Endgame.h"

#include "Position.h"
#include "BitBases.h"
#include "MoveGenerator.h"

EndGame::Endgames *EndGames = nullptr; // Global Endgames

namespace EndGame {

    using namespace std;
    using namespace BitBoard;
    using namespace BitBases;
    using namespace MoveGen;

    namespace {

        // Table used to drive the king towards the edge of the board
        // in KX vs K and KQ vs KR endgames.
        const i32 PUSH_TO_EDGE  [SQ_NO] =
        {
            100, 90, 80, 70, 70, 80, 90, 100,
            90,  70, 60, 50, 50, 60, 70,  90,
            80,  60, 40, 30, 30, 40, 60,  80,
            70,  50, 30, 20, 20, 30, 50,  70,
            70,  50, 30, 20, 20, 30, 50,  70,
            80,  60, 40, 30, 30, 40, 60,  80,
            90,  70, 60, 50, 50, 60, 70,  90,
            100, 90, 80, 70, 70, 80, 90, 100
        };

        // Table used to drive the king towards a corner square of the right color
        // in KBN vs K and KBB vs KN endgames.
        const i32 PUSH_TO_CORNER[SQ_NO] =
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

        // Tables used to drive a piece towards or away from another piece
        const i32 PUSH_CLOSE[8] = {  0,  0, 100,  80,  60,  40,  20,  10 };
        const i32 PUSH_AWAY [8] = {  0,  5,  20,  40,  60,  80,  90, 100 };

        // Pawn Rank based scaling factors used in KRPPKRP endgame
        const i32 KRPPKRPScaleFactors[R_NO] = { 0, 9, 10, 14, 21, 44, 0, 0 };

#ifndef NDEBUG
        bool verify_material (const Position &pos, Color c, Value npm, i32 num_pawns)
        {
            return pos.non_pawn_material (c) == npm && pos.count<PAWN> (c) == num_pawns;
        }
#endif

        // Map the square as if strong_side is white and
        // strong_side's only pawn is on the left half of the board.
        Square normalize (const Position &pos, Color strong_side, Square sq)
        {
            assert(pos.count<PAWN> (strong_side) == 1);

            if (_file (pos.square<PAWN> (strong_side)) >= F_E)
            {
                sq = !sq; // MIRROR- SQ_A1 -> SQ_H1
            }
            if (BLACK == strong_side)
            {
                sq = ~sq; // FLIP  - SQ_A1 -> SQ_A8
            }

            return sq;
        }

        // Get the material key of a Position out of the given endgame key code
        // like "KBPKN". The trick here is to first forge an ad-hoc FEN string
        // and then let a Position object to do the work.
        Key key (const string &code, Color c)
        {
            assert(0 < code.length () && code.length () <= 8);
            assert(code[0] == 'K');

            string sides[CLR_NO] =
            {
                code.substr (   code.find ('K', 1)), // Weak
                code.substr (0, code.find ('K', 1)), // Strong
            };

            transform (sides[c].begin (), sides[c].end (), sides[c].begin (), ::tolower);
            
            string fen = sides[0] + char (8 - sides[0].length () + '0') + "/8/8/8/8/8/8/"
                       + sides[1] + char (8 - sides[1].length () + '0') + " w - - 0 1";

            return Position (fen).matl_key ();
        }

        template<class M>
        void delete_endgame (const typename M::value_type &p) { delete p.second; }

    }

    Endgames::Endgames ()
    {
        add<KPK>     ("KPK");
        add<KNNK>    ("KNNK");
        add<KBNK>    ("KBNK");
        add<KRKP>    ("KRKP");
        add<KRKB>    ("KRKB");
        add<KRKN>    ("KRKN");
        add<KQKP>    ("KQKP");
        add<KQKR>    ("KQKR");
        add<KBBKN>   ("KBBKN");

        add<KNPK>    ("KNPK");
        add<KNPKB>   ("KNPKB");
        add<KRPKR>   ("KRPKR");
        add<KRPKB>   ("KRPKB");
        add<KBPKB>   ("KBPKB");
        add<KBPKN>   ("KBPKN");
        add<KBPPKB>  ("KBPPKB");
        add<KRPPKRP> ("KRPPKRP");
    }

    template<EndgameT E, class T>
    void Endgames::add (const string &code)
    {
        map<T> ()[key (code, WHITE)] = unique_ptr<EndgameBase<T>> (new Endgame<E> (WHITE));
        map<T> ()[key (code, BLACK)] = unique_ptr<EndgameBase<T>> (new Endgame<E> (BLACK));
    }

    template<>
    // Mate with KX vs K. This function is used to evaluate positions with
    // King and plenty of material vs a lone king. It simply gives the
    // attacking side a bonus for driving the defending king towards the edge
    // of the board, and for keeping the distance between the two kings small.
    Value Endgame<KXK>::operator() (const Position &pos) const
    {
        assert(verify_material (pos,   _weak_side, VALUE_ZERO, 0));
        assert(pos.checkers () == U64(0)); // Eval is never called when in check

        // Stalemate detection with lone weak king
        if (pos.active () == _weak_side && MoveList<LEGAL> (pos).size () == 0)
        {
            return VALUE_DRAW;
        }

        auto sk_sq = pos.square<KING> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);

        Value value = pos.count<PAWN> (_strong_side) * VALUE_EG_PAWN
                    + pos.non_pawn_material (_strong_side)
                    + PUSH_TO_EDGE[wk_sq]
                    + PUSH_CLOSE[dist (sk_sq, wk_sq)];

        if (    pos.count<QUEN> (_strong_side) != 0
            ||  pos.count<ROOK> (_strong_side) != 0
            ||  pos.bishops_pair (_strong_side)
            || (pos.count<BSHP> (_strong_side) != 0 && pos.count<NIHT> (_strong_side) != 0)
            ||  pos.count<NIHT> (_strong_side) > 2
           )
        {
            value += VALUE_KNOWN_WIN;
        }

        return pos.active () == _strong_side ? +value : -value;
    }

    template<>
    // KP vs K. This endgame is evaluated with the help of a bitbase.
    Value Endgame<KPK>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_ZERO, 1));
        assert(verify_material (pos,   _weak_side, VALUE_ZERO, 0));

        // Assume _strong_side is white and the pawn is on files A-D
        auto sk_sq = normalize (pos, _strong_side, pos.square<KING> (_strong_side));
        auto wk_sq = normalize (pos, _strong_side, pos.square<KING> (  _weak_side));
        auto sp_sq = normalize (pos, _strong_side, pos.square<PAWN> (_strong_side));

        if (!probe (pos.active () == _strong_side ? WHITE : BLACK, sk_sq, sp_sq, wk_sq))
        {
            return VALUE_DRAW;
        }

        Value value = VALUE_KNOWN_WIN + VALUE_EG_PAWN + _rank (sp_sq);

        return pos.active () == _strong_side ? +value : -value;
    }

    template<>
    // Mate with KBN vs K. This is similar to KX vs K, but have to drive the
    // defending king towards a corner square of the right color.
    Value Endgame<KBNK>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_NIHT + VALUE_MG_BSHP, 0));
        assert(verify_material (pos,   _weak_side, VALUE_ZERO, 0));

        auto sk_sq = pos.square<KING> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);
        auto sb_sq = pos.square<BSHP> (_strong_side);

        // kbnk mate table tries to drive toward corners A1 or H8,
        // if have a bishop that cannot reach the above squares
        // flip the kings so to drive enemy toward corners A8 or H1.
        if (opposite_colors (sb_sq, SQ_A1))
        {
            sk_sq = ~sk_sq;
            wk_sq = ~wk_sq;
        }

        Value value = VALUE_KNOWN_WIN
                    + PUSH_CLOSE[dist (sk_sq, wk_sq)]
                    + PUSH_TO_CORNER[wk_sq];

        return pos.active () == _strong_side ? +value : -value;
    }

    template<>
    Value Endgame<KNNK> ::operator() (const Position &) const
    {
        //assert(verify_material (pos, _strong_side, 2 * VALUE_MG_NIHT, 0));
        return VALUE_DRAW;
    }

    template<>
    // KR vs KP. This is a somewhat tricky endgame to evaluate precisely without a bitbase.
    // The function below returns drawish scores when the pawn is far advanced
    // with support of the king, while the attacking king is far away.
    Value Endgame<KRKP>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_ROOK, 0));
        assert(verify_material (pos,   _weak_side, VALUE_ZERO   , 1));

        auto sk_sq = rel_sq (_strong_side, pos.square<KING> (_strong_side));
        auto wk_sq = rel_sq (_strong_side, pos.square<KING> (  _weak_side));
        auto sr_sq = rel_sq (_strong_side, pos.square<ROOK> (_strong_side));
        auto wp_sq = rel_sq (_strong_side, pos.square<PAWN> (  _weak_side));

        auto promote_sq = _file (wp_sq)|R_1;

        Value value;

        // If the stronger side's king is in front of the pawn, it's a win. or
        // If the weaker side's king is too far from the pawn and the rook, it's a win.
        if (   (sk_sq < wp_sq && _file (sk_sq) == _file (wp_sq))
            || (   dist (wk_sq, wp_sq) >= 3 + (pos.active () == _weak_side)
                && dist (wk_sq, sr_sq) >= 3
               )
           )
        {
            value = VALUE_EG_ROOK - dist (sk_sq, wp_sq);
        }
        // If the pawn is far advanced and supported by the defending king, it's a drawish.
        else
        if (   _rank (wk_sq) <= R_3
            && dist (wk_sq, wp_sq) == 1
            && _rank (sk_sq) >= R_4
            && dist (sk_sq, wp_sq) > 2 + (pos.active () == _strong_side)
           )
        {
            value = Value(80 - 8 * dist (sk_sq, wp_sq));
        }
        else
        {
            value = Value(200 - 8 * (  dist (sk_sq, wp_sq+DEL_S)
                                     - dist (wk_sq, wp_sq+DEL_S)
                                     - dist (wp_sq, promote_sq)
                                    )
                         );
        }

        return pos.active () == _strong_side ? +value : -value;
    }

    template<>
    // KR vs KB. This is very simple, and always returns drawish scores.
    // The score is slightly bigger when the defending king is close to the edge.
    Value Endgame<KRKB>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_ROOK, 0));
        assert(verify_material (pos,   _weak_side, VALUE_MG_BSHP, 0));

        auto sk_sq = pos.square<KING> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);
        auto wb_sq = pos.square<BSHP> (  _weak_side);

        // To draw, the weaker side should run towards the corner.
        // And not just any corner! Only a corner that's not the same color as the bishop will do.
        if (   (CORNER_bb & wk_sq) != U64(0)
            && opposite_colors (wk_sq, wb_sq)
            && dist (wk_sq, wb_sq) == 1
            && dist (sk_sq, wb_sq) > 1
           )
        {
            return VALUE_DRAW;
        }

        // When the weaker side ended up in the same corner as bishop.
        Value value = Value(PUSH_TO_EDGE[wk_sq]);

        return pos.active () == _strong_side ? +value : -value;
    }

    template<>
    // KR vs KN.  The attacking side has slightly better winning chances than
    // in KR vs KB, particularly if the king and the knight are far apart.
    Value Endgame<KRKN>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_ROOK, 0));
        assert(verify_material (pos,   _weak_side, VALUE_MG_NIHT, 0));

        auto sk_sq = pos.square<KING> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);
        auto wn_sq = pos.square<NIHT> (  _weak_side);

        // If weaker king is near the knight, it's a draw.
        if (   dist (wk_sq, wn_sq) + (pos.active () == _strong_side) <= 3
            && dist (sk_sq, wn_sq) > 1
           )
        {
            return VALUE_DRAW;
        }

        Value value = Value(PUSH_AWAY[dist (wk_sq, wn_sq)] + PUSH_TO_EDGE[wk_sq]);

        return pos.active () == _strong_side ? +value : -value;
    }

    template<>
    // KQ vs KP. In general, this is a win for the stronger side, but there are a
    // few important exceptions. A pawn on 7th rank and on the A,C,F or H files
    // with a king positioned next to it can be a draw, so in that case, only
    // use the distance between the kings.
    Value Endgame<KQKP>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_QUEN, 0));
        assert(verify_material (pos,   _weak_side, VALUE_ZERO   , 1));

        auto sk_sq = pos.square<KING> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);
        auto wp_sq = pos.square<PAWN> (  _weak_side);

        Value value = Value(PUSH_CLOSE[dist (sk_sq, wk_sq)]);

        if (   rel_rank (_weak_side, wp_sq) != R_7
            || dist (wk_sq, wp_sq) != 1
            || ((FA_bb|FC_bb|FF_bb|FH_bb) & wp_sq) == U64(0)
           )
        {
            value += VALUE_EG_QUEN - VALUE_EG_PAWN;
        }

        return pos.active () == _strong_side ? +value : -value;
    }

    template<>
    // KQ vs KR. This is almost identical to KX vs K: give the attacking
    // king a bonus for having the kings close together, and for forcing the
    // defending king towards the edge. If also take care to avoid null move for
    // the defending side in the search, this is usually sufficient to win KQ vs KR.
    Value Endgame<KQKR>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_QUEN, 0));
        assert(verify_material (pos,   _weak_side, VALUE_MG_ROOK, 0));

        auto sk_sq = pos.square<KING> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);

        Value value = VALUE_EG_QUEN - VALUE_EG_ROOK
                    + PUSH_CLOSE[dist (sk_sq, wk_sq)]
                    + PUSH_TO_EDGE[wk_sq];

        return pos.active () == _strong_side ? +value : -value;
    }

    template<>
    // KBB vs KN. This is almost always a win. Try to push enemy king to a corner
    // and away from his knight. For a reference of this difficult endgame see:
    // en.wikipedia.org/wiki/Chess_endgame#Effect_of_tablebases_on_endgame_theory
    // But this endgame is not known, there are many position where it takes 50+ moves to win.
    // Because exact rule is not possible better to retire and allow the search to workout the endgame.
    Value Endgame<KBBKN>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, 2 * VALUE_MG_BSHP, 0));
        assert(verify_material (pos,   _weak_side,     VALUE_MG_NIHT, 0));

        auto sk_sq = pos.square<KING> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);
        auto wn_sq = pos.square<NIHT> (  _weak_side);

        Value value;

        if (pos.bishops_pair (_strong_side))
        {
            if (  std::min (dist (wk_sq, SQ_A8), dist (wk_sq, SQ_H1))
                < std::min (dist (wk_sq, SQ_A1), dist (wk_sq, SQ_H8))
               )
            {
                sk_sq = ~sk_sq;
                wk_sq = ~wk_sq;
                wn_sq = ~wn_sq;
            }

            value = VALUE_MG_BSHP
                  + PUSH_CLOSE[dist (sk_sq, wk_sq)] // Bring attacking king close to defending king
                  + PUSH_AWAY [dist (wk_sq, wn_sq)] // Driving the defending king and knight apart
                  + PUSH_TO_CORNER[wk_sq]
                  + PUSH_TO_EDGE[wn_sq];            // Restricting the knight's mobility
        }
        else
        {
            value = Value(PUSH_CLOSE[dist (sk_sq, wk_sq)]);
        }

        return pos.active () == _strong_side ? +value : -value;
    }


    // ---------------------------------------------------------
    // Scaling functions are used when any side have some pawns
    // ---------------------------------------------------------

    // ---------------------------------------------------------
    // Special Scaling functions
    // ---------------------------------------------------------

    template<>
    // KRP vs KR. This function knows a handful of the most important classes of
    // drawn positions, but is far from perfect. It would probably be a good idea
    // to add more knowledge in the future.
    //
    // It would also be nice to rewrite the actual code for this function,
    // which is mostly copied from Glaurung 1.x, and isn't very pretty.
    ScaleFactor Endgame<KRPKR>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_ROOK, 1));
        assert(verify_material (pos,   _weak_side, VALUE_MG_ROOK, 0));

        // Assume _strong_side is white and the pawn is on files A-D
        auto sk_sq = normalize (pos, _strong_side, pos.square<KING> (_strong_side));
        auto wk_sq = normalize (pos, _strong_side, pos.square<KING> (  _weak_side));
        auto sr_sq = normalize (pos, _strong_side, pos.square<ROOK> (_strong_side));
        auto sp_sq = normalize (pos, _strong_side, pos.square<PAWN> (_strong_side));
        auto wr_sq = normalize (pos, _strong_side, pos.square<ROOK> (  _weak_side));

        auto f = _file (sp_sq);
        auto r = _rank (sp_sq);
        auto promote_sq = f|R_8;
        i32 tempo = (pos.active () == _strong_side);

        // If the pawn is not too far advanced and the defending king defends the
        // queening square, use the third-rank defence.
        if (   r <= R_5
            && sk_sq <= SQ_H5
            && dist (wk_sq, promote_sq) <= 1
            && (_rank (wr_sq) == R_6 || (r <= R_3 && _rank (sr_sq) != R_6))
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        // The defending side saves a draw by checking from behind in case the pawn
        // has advanced to the 6th rank with the king behind.
        if (   r == R_6
            && dist (wk_sq, promote_sq) <= 1
            && _rank (sk_sq) + tempo <= R_6
            && (_rank (wr_sq) == R_1 || (!tempo && dist<File> (wr_sq, sp_sq) >= 3))
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        if (   r >= R_6
            && wk_sq == promote_sq
            && _rank (wr_sq) == R_1
            && (!tempo || dist (sk_sq, sp_sq) >= 2)
           )
        {
            return SCALE_FACTOR_DRAW;
        }
        // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
        // and the black rook is behind the pawn.
        if (   sp_sq == SQ_A7
            && sr_sq == SQ_A8
            && (wk_sq == SQ_H7 || wk_sq == SQ_G7)
            && _file (wr_sq) == F_A
            && (_rank (wr_sq) <= R_3 || _file (sk_sq) >= F_D || _rank (sk_sq) <= R_5)
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        // If the defending king blocks the pawn and the attacking king is too far away, it's a draw.
        if (   r <= R_5
            && wk_sq == sp_sq+DEL_N
            && dist (sk_sq, sp_sq) - tempo >= 2
            && dist (sk_sq, wr_sq) - tempo >= 2
           )
        {
            return SCALE_FACTOR_DRAW;
        }
        // Pawn on the 7th rank supported by the rook from behind usually wins if the
        // attacking king is closer to the queening square than the defending king,
        // and the defending king cannot gain tempi by threatening the attacking rook.
        if (   r == R_7
            && f != F_A
            && f == _file (sr_sq)
            && sr_sq != promote_sq
            && dist (sk_sq, promote_sq) < dist (wk_sq, promote_sq) - 2 + tempo
            && dist (sk_sq, promote_sq) < dist (wk_sq, sr_sq) + tempo
           )
        {
            return ScaleFactor(SCALE_FACTOR_MAX - 2 * dist (sk_sq, promote_sq));
        }

        // Similar to the above, but with the pawn further back
        if (   f != F_A
            && f == _file (sr_sq)
            && sr_sq < sp_sq
            && dist (sk_sq, promote_sq) < dist (wk_sq, promote_sq) - 2 + tempo
            && dist (sk_sq, sp_sq+DEL_N) < dist (wk_sq, sp_sq+DEL_N) - 2 + tempo
            && (   dist (wk_sq, sr_sq) + tempo >= 3
                || (   dist (sk_sq, promote_sq) < dist (wk_sq, sr_sq) + tempo
                    && dist (sk_sq, sp_sq+DEL_N) < dist (wk_sq, sr_sq) + tempo
                   )
               )
           )
        {
            return ScaleFactor(  SCALE_FACTOR_MAX
                               - 8 * dist (sp_sq, promote_sq)
                               - 2 * dist (sk_sq, promote_sq));
        }

        // If the pawn is not far advanced, and the defending king is somewhere in
        // the pawn's path, it's probably a draw.
        if (r <= R_4 && wk_sq > sp_sq)
        {
            if (_file (wk_sq) == _file (sp_sq))
            {
                return ScaleFactor(10);
            }
            if (dist<File> (wk_sq, sp_sq) == 1 && dist (sk_sq, wk_sq) > 2)
            {
                return ScaleFactor(24 - 2 * dist (sk_sq, wk_sq));
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KRP vs KB.
    ScaleFactor Endgame<KRPKB>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_ROOK, 1));
        assert(verify_material (pos,   _weak_side, VALUE_MG_BSHP, 0));

        // Test for a rook pawn
        if ((pos.pieces (PAWN) & (FA_bb|FH_bb)) != U64(0))
        {
            auto wk_sq = pos.square<KING> (  _weak_side);
            auto wb_sq = pos.square<BSHP> (  _weak_side);
            auto sp_sq = pos.square<PAWN> (_strong_side);
            auto r     = rel_rank (_strong_side, sp_sq);
            auto push  = pawn_push (_strong_side);

            // If the pawn is on the 5th rank and the pawn (currently) is on the 
            // same color square as the bishop then there is a chance of a fortress.
            // Depending on the king position give a moderate reduction or a stronger one
            // if the defending king is near the corner but not trapped there.
            if (r == R_5 && !opposite_colors (wb_sq, sp_sq))
            {
                i32 d = dist (sp_sq + 3 * push, wk_sq);
                return d <= 2 && !(d == 0 && wk_sq == pos.square<KING> (_strong_side) + 2 * push) ?
                            ScaleFactor(24) : ScaleFactor(48);
            }

            // When the pawn has moved to the 6th rank can be fairly sure it's drawn
            // if the bishop attacks the square in front of the pawn from a reasonable distance
            // and the defending king is near the corner
            if (   r == R_6
                && dist (sp_sq + 2 * push, wk_sq) <= 1
                && (PIECE_ATTACKS[BSHP][wb_sq] & (sp_sq + push)) != U64(0)
                && dist<File> (wb_sq, sp_sq) >= 2
               )
            {
                return ScaleFactor(8);
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KRPP vs KRP. There is just a single rule: if the stronger side has no passed
    // pawns and the defending king is actively placed, the position is drawish.
    ScaleFactor Endgame<KRPPKRP>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_ROOK, 2));
        assert(verify_material (pos,   _weak_side, VALUE_MG_ROOK, 1));

        auto sp1_sq = pos.square<PAWN> (_strong_side, 0);
        auto sp2_sq = pos.square<PAWN> (_strong_side, 1);
        auto wk_sq  = pos.square<KING> (  _weak_side);

        // Does the stronger side have a passed pawn?
        if (   !pos.passed_pawn (_strong_side, sp1_sq)
            && !pos.passed_pawn (_strong_side, sp2_sq)
           )
        {
            auto r = std::max (rel_rank (_strong_side, sp1_sq), rel_rank (_strong_side, sp2_sq));

            if (   dist<File> (wk_sq, sp1_sq) <= 1
                && dist<File> (wk_sq, sp2_sq) <= 1
                && rel_rank (_strong_side, wk_sq) > r
               )
            {
                assert(r > R_1 && r < R_7);
                return ScaleFactor(KRPPKRPScaleFactors[r]);
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // K and two or more pawns vs K. There is just a single rule here: If all pawns
    // are on the same rook file and are blocked by the defending king, it's a draw.
    ScaleFactor Endgame<KPsK>::operator() (const Position &pos) const
    {
        assert(pos.non_pawn_material (_strong_side) == VALUE_ZERO);
        assert(pos.count<PAWN> (_strong_side) >= 2);
        assert(verify_material (pos,   _weak_side, VALUE_ZERO, 0));

        auto wk_sq  = pos.square<KING> (_weak_side);
        auto spawns = pos.pieces (_strong_side, PAWN);
        auto sp_sq  = scan_frntmost_sq (_strong_side, spawns);

        // If all pawns are ahead of the king, all pawns are on a single
        // rook file and the king is within one file of the pawns then draw.
        if (   (spawns & ~FRONT_RANK_bb[_weak_side][_rank (wk_sq)]) == U64(0)
            && ((spawns & ~FA_bb) == U64(0) || (spawns & ~FH_bb) == U64(0))
            && dist<File> (wk_sq, sp_sq) <= 1
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KP vs KP. This is done by removing the weakest side's pawn and probing the
    // KP vs K bitbase: If the weakest side has a draw without the pawn, it probably
    // has at least a draw with the pawn as well. The exception is when the stronger
    // side's pawn is far advanced and not on a rook file; in this case it is often
    // possible to win (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
    ScaleFactor Endgame<KPKP>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_ZERO, 1));
        assert(verify_material (pos,   _weak_side, VALUE_ZERO, 1));

        // Assume _strong_side is white and the pawn is on files A-D
        auto sk_sq = normalize (pos, _strong_side, pos.square<KING> (_strong_side));
        auto wk_sq = normalize (pos, _strong_side, pos.square<KING> (  _weak_side));
        auto sp_sq = normalize (pos, _strong_side, pos.square<PAWN> (_strong_side));

        // If the pawn has advanced to the fifth rank or further, and is not a rook pawn,
        // then it's too dangerous to assume that it's at least a draw.
        if (_rank (sp_sq) < R_5 || _file (sp_sq) == F_A)
        {
            // Probe the KPK bitbase with the weakest side's pawn removed.
            // If it's a draw, it's probably at least a draw even with the pawn.
            if (!probe (pos.active () == _strong_side ? WHITE : BLACK, sk_sq, sp_sq, wk_sq))
            {
                return SCALE_FACTOR_DRAW;
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KNP vs K. There is a single rule: if the pawn is a rook pawn on the 7th rank
    // and the defending king prevents the pawn from advancing the position is drawn.
    ScaleFactor Endgame<KNPK>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_NIHT, 1));
        assert(verify_material (pos,   _weak_side, VALUE_ZERO   , 0));

        // Assume _strong_side is white and the pawn is on files A-D
        auto sp_sq = normalize (pos, _strong_side, pos.square<PAWN> (_strong_side));
        auto wk_sq = normalize (pos, _strong_side, pos.square<KING> (  _weak_side));

        if (sp_sq == SQ_A7 && dist (wk_sq, SQ_A8) <= 1)
        {
            return SCALE_FACTOR_DRAW;
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KBP vs KB. There are two rules: if the defending king is somewhere along the
    // path of the pawn, and the square of the king is not of the same color as the
    // stronger side's bishop, it's a draw. If the two bishops have opposite color,
    // it's almost always a draw.
    ScaleFactor Endgame<KBPKB>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_BSHP, 1));
        assert(verify_material (pos,   _weak_side, VALUE_MG_BSHP, 0));

        auto sp_sq = pos.square<PAWN> (_strong_side);
        auto sb_sq = pos.square<BSHP> (_strong_side);
        auto wb_sq = pos.square<BSHP> (  _weak_side);
        auto wk_sq = pos.square<KING> (  _weak_side);

        // Case 1: Defending king blocks the pawn, and cannot be driven away
        if (   _file (wk_sq) == _file (sp_sq)
            && rel_rank (_strong_side, sp_sq) < rel_rank (_strong_side, wk_sq)
            && (opposite_colors (wk_sq, sb_sq) || rel_rank (_strong_side, wk_sq) <= R_6)
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        // Case 2: Opposite colored bishops
        if (opposite_colors (sb_sq, wb_sq))
        {
            // Assume that the position is drawn in the following three situations:
            //
            //   a. The pawn is on rank 5 or further back.
            //   b. The defending king is somewhere in the pawn's path.
            //   c. The defending bishop attacks some square along the pawn's path,
            //      and is at least three squares away from the pawn.
            //
            // These rules are probably not perfect, but in practice they work reasonably well.

            if (rel_rank (_strong_side, sp_sq) <= R_5)
            {
                return SCALE_FACTOR_DRAW;
            }
            
            auto path = FRONT_SQRS_bb[_strong_side][sp_sq];
            if (    (path & pos.pieces (_weak_side, KING)) != U64(0)
                || ((path & attacks_bb<BSHP> (wb_sq, pos.pieces ())) != U64(0) && dist (wb_sq, sp_sq) >= 3)
               )
            {
                return SCALE_FACTOR_DRAW;
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KBPP vs KB. It detects a few basic draws with opposite-colored bishops.
    ScaleFactor Endgame<KBPPKB>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_BSHP, 2));
        assert(verify_material (pos,   _weak_side, VALUE_MG_BSHP, 0));

        auto sb_sq = pos.square<BSHP> (_strong_side);
        auto wb_sq = pos.square<BSHP> (  _weak_side);

        if (opposite_colors (sb_sq, wb_sq))
        {

            auto wk_sq  = pos.square<KING> (  _weak_side);
            auto sp1_sq = pos.square<PAWN> (_strong_side, 0);
            auto sp2_sq = pos.square<PAWN> (_strong_side, 1);
        
            auto block1_sq = SQ_NO;
            auto block2_sq = SQ_NO;

            if (rel_rank (_strong_side, sp1_sq) > rel_rank (_strong_side, sp2_sq))
            {
                block1_sq = sp1_sq + pawn_push (_strong_side);
                block2_sq = _file (sp2_sq)|_rank (sp1_sq);
            }
            else
            {
                block1_sq = sp2_sq + pawn_push (_strong_side);
                block2_sq = _file (sp1_sq)|_rank (sp2_sq);
            }

            switch (dist<File> (sp1_sq, sp2_sq))
            {
            // Both pawns are on the same file. It's an easy draw if the defender firmly
            // controls some square in the frontmost pawn's path.
            case 0:
            {
                if (   _file (wk_sq) == _file (block1_sq)
                    && rel_rank (_strong_side, wk_sq) >= rel_rank (_strong_side, block1_sq)
                    && opposite_colors (wk_sq, sb_sq)
                   )
                {
                    return SCALE_FACTOR_DRAW;
                }
                break;
            }
        
            // Pawns on adjacent files. It's a draw if the defender firmly controls the
            // square in front of the frontmost pawn's path, and the square diagonally
            // behind this square on the file of the other pawn.
            case 1:
            {
           
                if (   wk_sq == block1_sq
                    && opposite_colors (wk_sq, sb_sq)
                    && (   wb_sq == block2_sq
                        || (attacks_bb<BSHP> (block2_sq, pos.pieces ()) & pos.pieces (_weak_side, BSHP)) != U64(0)
                        || dist<Rank> (sp1_sq, sp2_sq) >= 2
                       )
                   )
                {
                    return SCALE_FACTOR_DRAW;
                }

                if (   wk_sq == block2_sq
                    && opposite_colors (wk_sq, sb_sq)
                    && (   wb_sq == block1_sq
                        || (attacks_bb<BSHP> (block1_sq, pos.pieces ()) & pos.pieces (_weak_side, BSHP)) != U64(0)
                       )
                   )
                {
                    return SCALE_FACTOR_DRAW;
                }
                break;
            }
        
            // The pawns are not on the same file or adjacent files. No scaling.
            default:
                break;
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KBP vs KN. There is a single rule: If the defending king is somewhere along
    // the path of the pawn, and the square of the king is not of the same color as
    // the stronger side's bishop, it's a draw.
    ScaleFactor Endgame<KBPKN>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_BSHP, 1));
        assert(verify_material (pos,   _weak_side, VALUE_MG_NIHT, 0));

        auto sp_sq = pos.square<PAWN> (_strong_side);
        auto sb_sq = pos.square<BSHP> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);

        if (   _file (wk_sq) == _file (sp_sq)
            && rel_rank (_strong_side, sp_sq) < rel_rank (_strong_side, wk_sq)
            && (opposite_colors (wk_sq, sb_sq) || rel_rank (_strong_side, wk_sq) <= R_6)
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KNP vs KB. If knight can block bishop from taking pawn, it's a win.
    // Otherwise the position is a draw.
    ScaleFactor Endgame<KNPKB>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_NIHT, 1));
        assert(verify_material (pos,   _weak_side, VALUE_MG_BSHP, 0));

        auto sp_sq = pos.square<PAWN> (_strong_side);
        auto sb_sq = pos.square<BSHP> (  _weak_side);
        auto wk_sq = pos.square<KING> (  _weak_side);
        
        // King needs to get close to promoting pawn to prevent knight from blocking.
        // Rules for this are very tricky, so just approximate.
        if ((FRONT_SQRS_bb[_strong_side][sp_sq] & attacks_bb<BSHP> (sb_sq, pos.pieces ())) != U64(0))
        {
            return ScaleFactor(dist (wk_sq, sp_sq));
        }

        return SCALE_FACTOR_NONE;
    }

    // --------------------------------------------------------------
    // Generic Scaling functions
    // --------------------------------------------------------------

    template<>
    // KB and one or more pawns vs K and zero or more pawns.
    // It checks for draws with rook pawns and a bishop of the wrong color.
    // If such a draw is detected, SCALE_FACTOR_DRAW is returned.
    // If not, the return value is SCALE_FACTOR_NONE, i.e. no scaling will be used.
    ScaleFactor Endgame<KBPsKs>::operator() (const Position &pos) const
    {
        assert(pos.non_pawn_material (_strong_side) == VALUE_MG_BSHP);
        assert(pos.count<BSHP> (_strong_side) == 1);
        assert(pos.count<PAWN> (_strong_side) != 0);
        // No assertions about the material of _weak_side, because we want draws to
        // be detected even when the weaker side has some materials or pawns.

        auto spawns = pos.pieces (_strong_side, PAWN);
        auto sp_sq = scan_frntmost_sq (_strong_side, spawns);
        auto sp_f  = _file (sp_sq);

        // All pawns on same A or H file? (rook file)
        // Then potential draw
        if (   (sp_f == F_A || sp_f == F_H)
            && (spawns & ~FILE_bb[sp_f]) == U64(0)
           )
        {
            auto sb_sq = pos.square<BSHP> (_strong_side);
            auto promote_sq = rel_sq (_strong_side, sp_f|R_8);
            auto wk_sq = pos.square<KING> (_weak_side);

            // The bishop has the wrong color.
            if (opposite_colors (promote_sq, sb_sq))
            {
                // If the defending king defends the queening square.
                if (dist (promote_sq, wk_sq) <= 1)
                {
                    return SCALE_FACTOR_DRAW;
                }
            }
        }

        // All pawns on same B or G file?
        // Then potential draw
        if (   (sp_f == F_B || sp_f == F_G)
            && (pos.pieces (PAWN) & ~FILE_bb[sp_f]) == U64(0)
            && pos.non_pawn_material (_weak_side) == VALUE_ZERO
           )
        {
            auto sk_sq = pos.square<KING> (_strong_side);
            auto wk_sq = pos.square<KING> (  _weak_side);
            auto sb_sq = pos.square<BSHP> (_strong_side);

            if (pos.count<PAWN> (_weak_side) != 0)
            {
                // Get _weak_side pawn that is closest to home rank
                auto wp_sq = scan_backmost_sq (_weak_side, pos.pieces (_weak_side, PAWN));

                //// It's a draw if weaker pawn is on rank 7, bishop can't attack the pawn, and
                //// weaker king can stop opposing opponent's king from penetrating.
                //if (   rel_rank (_strong_side, wp_sq) == R_7
                //    && opposite_colors (sb_sq, wp_sq)
                //    && dist (wp_sq, wk_sq) <= dist (wp_sq, sk_sq)
                //   )
                //{
                //    return SCALE_FACTOR_DRAW;
                //}

                // There's potential for a draw if weak pawn is blocked on the 7th rank
                // and the bishop cannot attack it or they only have one pawn left
                if (   rel_rank (_strong_side, wp_sq) == R_7
                    && (pos.pieces (_strong_side, PAWN) & (wp_sq + pawn_push (_weak_side)))
                    && (opposite_colors (sb_sq, wp_sq) || pos.count<PAWN> (_strong_side) == 1)
                   )
                {
                    // It's a draw if the weak king is on its back two ranks, within 2
                    // squares of the blocking pawn and the strong king is not
                    // closer. (I think this rule only fails in practically
                    // unreachable positions such as 5k1K/6p1/6P1/8/8/3B4/8/8 w
                    // and positions where qsearch will immediately correct the
                    // problem such as 8/4k1p1/6P1/1K6/3B4/8/8/8 w)
                    if (   rel_rank (_strong_side, wk_sq) >= R_7
                        && dist (wk_sq, wp_sq) <= 2
                        && dist (wk_sq, wp_sq) <= dist (sk_sq, wp_sq)
                       )
                    {
                        return SCALE_FACTOR_DRAW;
                    }
                }
            }

            auto promote_sq = rel_sq (_strong_side, sp_f|R_8);
            // If the defending king defends the queening square.
            // and strong pawn block bishop and king can't be driven away
            if (   dist (promote_sq, wk_sq) <= 1
                && dist (sp_sq, sk_sq) > 1
                && rel_rank (_strong_side, sp_sq) == R_6
                && rel_rank (_strong_side, sb_sq) == R_7
                && (file_bb (sb_sq) & (FA_bb|FH_bb)) != U64(0)
                && (PAWN_ATTACKS[_weak_side][sb_sq] & spawns) != U64(0)
               )
            {
                return SCALE_FACTOR_DRAW;
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KQ vs KR and one or more pawns.
    // It tests for fortress draws with a rook on the 3rd rank defended by a pawn.
    ScaleFactor Endgame<KQKRPs>::operator() (const Position &pos) const
    {
        assert(verify_material (pos, _strong_side, VALUE_MG_QUEN, 0));
        assert(pos.count<ROOK> (_weak_side) == 1);
        assert(pos.count<PAWN> (_weak_side) != 0);
        
        auto sk_sq = pos.square<KING> (_strong_side);
        auto wk_sq = pos.square<KING> (  _weak_side);
        auto wr_sq = pos.square<ROOK> (  _weak_side);

        if (   rel_rank (_weak_side, wk_sq) <= R_2
            && rel_rank (_weak_side, sk_sq) >= R_4
            && rel_rank (_weak_side, wr_sq) == R_3
            && (  pos.pieces (_weak_side, PAWN)
                & PIECE_ATTACKS[KING][wk_sq]
                & PAWN_ATTACKS[_strong_side][wr_sq]
               ) != U64(0)
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        return SCALE_FACTOR_NONE;
    }


    void initialize ()
    {
        if (EndGames == nullptr)
        {
            EndGames = new Endgames ();
            assert(EndGames != nullptr);
        }
    }

    void deinitialize ()
    {
        if (EndGames != nullptr)
        {
            delete EndGames;
            EndGames = nullptr;
        }
    }

}
