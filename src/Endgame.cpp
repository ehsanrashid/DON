#include "Endgame.h"

#include <algorithm>

#include "Position.h"
#include "BitBoard.h"
#include "BitCount.h"
#include "BitBases.h"
#include "MoveGenerator.h"

EndGame::Endgames *EndGames = NULL; // Global Endgames

namespace EndGame {

    using namespace std;
    using namespace BitBoard;
    using namespace BitBases;
    using namespace MoveGenerator;

    namespace {

        // Table used to drive the king towards the edge of the board
        // in KX vs K and KQ vs KR endgames.
        const i32 PushToEdges  [SQ_NO] =
        {
            100, 90,  80,  70,  70,  80,  90, 100,
            90,  70,  60,  50,  50,  60,  70,  90,
            80,  60,  40,  30,  30,  40,  60,  80,
            70,  50,  30,  20,  20,  30,  50,  70,
            70,  50,  30,  20,  20,  30,  50,  70,
            80,  60,  40,  30,  30,  40,  60,  80,
            90,  70,  60,  50,  50,  60,  70,  90,
            100, 90,  80,  70,  70,  80,  90, 100
        };

        // Table used to drive the king towards a corner square of the
        // right color in KBN vs K endgames.
        const i32 PushToCorners[SQ_NO] =
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
        const i32 PushClose[8] = {  0,  0, 100,  80,  60,  40,  20,  10 };
        const i32 PushAway [8] = {  0,  5,  20,  40,  60,  80,  90, 100 };

#ifndef NDEBUG
        inline bool verify_material (const Position &pos, Color c, Value npm, i32 num_pawns)
        {
            return (pos.non_pawn_material(c) == npm) && (pos.count<PAWN> (c) == num_pawns);
        }
#endif

        // Map the square as if strong_side is white and
        // strong_side's only pawn is on the left half of the board.
        inline Square normalize (const Position &pos, Color strong_side, Square sq)
        {
            ASSERT (pos.count<PAWN> (strong_side) == 1);

            if (_file (pos.list<PAWN> (strong_side)[0]) >= F_E)
            {
                sq = !sq; // MIRROR
            }
            if (BLACK == strong_side)
            {
                sq = ~sq; // FLIP
            }

            return sq;
        }

        template<Color C>
        // Get the material key of a Position out of the given endgame key code
        // like "KBPKN". The trick here is to first forge an ad-hoc fen string
        // and then let a Position object to do the work for us.
        inline Key key (const string &code)
        {
            ASSERT (0 < code.length () && code.length () <= 8);
            ASSERT (code[0] == 'K');

            string sides[CLR_NO] =
            {
                code.substr (   code.find('K', 1)), // Weak
                code.substr (0, code.find('K', 1)), // Strong
            };

            transform (sides[C].begin (), sides[C].end (), sides[C].begin (), ::tolower);
            
            string fen = sides[0] + char (8 - sides[0].length() + '0') + "/8/8/8/8/8/8/"
                       + sides[1] + char (8 - sides[1].length() + '0') + " w - - 0 1";

            return Position (fen).matl_key ();
        }

        template<class M>
        inline void delete_endgame (const typename M::value_type &p) { delete p.second; }

    } // namespace

    // Endgames members definitions
    Endgames:: Endgames ()
    {
        add<KPK>     ("KPK");
        add<KNNK>    ("KNNK");
        add<KBNK>    ("KBNK");
        add<KRKP>    ("KRKP");
        add<KRKB>    ("KRKB");
        add<KRKN>    ("KRKN");
        add<KQKP>    ("KQKP");
        add<KQKR>    ("KQKR");
        add<KBBKN>   ("KBBKN"); // retired

        add<KNPK>    ("KNPK");
        add<KNPKB>   ("KNPKB");
        add<KRPKR>   ("KRPKR");
        add<KRPKB>   ("KRPKB");
        add<KBPKB>   ("KBPKB");
        add<KBPKN>   ("KBPKN");
        add<KBPPKB>  ("KBPPKB");
        add<KRPPKRP> ("KRPPKRP");
    }

    Endgames::~Endgames ()
    {
        for_each (m1.begin (), m1.end (), delete_endgame<M1>);
        for_each (m2.begin (), m2.end (), delete_endgame<M2>);
    }

    template<EndgameT ET>
    void Endgames::add (const string &code)
    {
        map ((Endgame<ET>*) NULL)[key<WHITE> (code)] = new Endgame<ET> (WHITE);
        map ((Endgame<ET>*) NULL)[key<BLACK> (code)] = new Endgame<ET> (BLACK);
    }

    template<>
    // Mate with KX vs K. This function is used to evaluate positions with
    // King and plenty of material vs a lone king. It simply gives the
    // attacking side a bonus for driving the defending king towards the edge
    // of the board, and for keeping the distance between the two kings small.
    Value Endgame<KXK>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _weak_side, VALUE_ZERO, 0));
        ASSERT (pos.checkers () == U64 (0)); // Eval is never called when in check

        // Stalemate detection with lone weak king
        if (_weak_side == pos.active () && MoveList<LEGAL> (pos).size () == 0)
        {
            return VALUE_DRAW;
        }

        Square sk_sq = pos.king_sq (_stong_side);
        Square wk_sq = pos.king_sq (_weak_side);

        Value value = pos.count<PAWN> (_stong_side) * VALUE_EG_PAWN
            +   PushToEdges[wk_sq] + PushClose[SquareDist[sk_sq][wk_sq]];

        if (  pos.count<QUEN> (_stong_side) != 0
           || pos.count<ROOK> (_stong_side) != 0
           || pos.count<NIHT> (_stong_side) > 2
           || pos.bishops_pair (_stong_side)
           )
        {
            value += pos.non_pawn_material (_stong_side) + VALUE_KNOWN_WIN;
        }
        else
        {
            value /= 8;
        }

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    // KP vs K. This endgame is evaluated with the help of a bitbase.
    Value Endgame<KPK>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_ZERO, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO, 0));

        // Assume _stong_side is white and the pawn is on files A-D
        Square sk_sq = normalize (pos, _stong_side, pos.king_sq (_stong_side));
        Square wk_sq = normalize (pos, _stong_side, pos.king_sq (_weak_side));
        Square sp_sq = normalize (pos, _stong_side, pos.list<PAWN> (_stong_side)[0]);

        Color c = (_stong_side == pos.active ()) ? WHITE : BLACK;

        Value value;

        if (probe_kpk (c, sk_sq, sp_sq, wk_sq))
        {
            value = VALUE_KNOWN_WIN + VALUE_EG_PAWN + Value (_rank (sp_sq));
        }
        else
        {
            value = Value ((
                PushClose[SquareDist[sk_sq][wk_sq]] +
                PushClose[SquareDist[sp_sq][sk_sq]] +
                PushAway [SquareDist[sp_sq][wk_sq]]) / 10);
        }

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    // Mate with KBN vs K. This is similar to KX vs K, but we have to drive the
    // defending king towards a corner square of the right color.
    Value Endgame<KBNK>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_NIHT + VALUE_MG_BSHP, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO, 0));

        Square sk_sq = pos.king_sq (_stong_side);
        Square wk_sq = pos.king_sq (_weak_side);
        Square sb_sq = pos.list<BSHP> (_stong_side)[0];

        // kbnk_mate_table() tries to drive toward corners A1 or H8,
        // if we have a bishop that cannot reach the above squares we
        // mirror the kings so to drive enemy toward corners A8 or H1.
        if (opposite_colors (sb_sq, SQ_A1))
        {
            sk_sq = ~sk_sq;
            wk_sq = ~wk_sq;
        }

        Value value = VALUE_KNOWN_WIN
            + PushClose[SquareDist[sk_sq][wk_sq]] + PushToCorners[wk_sq];

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    Value Endgame<KNNK> ::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, 2 * VALUE_MG_NIHT, 0));

        Square wk_sq = pos.king_sq (_weak_side);

        Value value = Value (PushToEdges[wk_sq] / 8);

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    // KR vs KP. This is a somewhat tricky endgame to evaluate precisely without a bitbase.
    // The function below returns drawish scores when the pawn is far advanced
    // with support of the king, while the attacking king is far away.
    Value Endgame<KRKP>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO   , 1));

        Square sk_sq = rel_sq (_stong_side, pos.king_sq (_stong_side));
        Square wk_sq = rel_sq (_stong_side, pos.king_sq (_weak_side));
        Square sr_sq = rel_sq (_stong_side, pos.list<ROOK> (_stong_side)[0]);
        Square wp_sq = rel_sq (_stong_side, pos.list<PAWN> (_weak_side)[0]);

        Square queening_sq = _file (wp_sq) | R_1;

        Value value;

        // If the stronger side's king is in front of the pawn, it's a win. or
        // If the weaker side's king is too far from the pawn and the rook, it's a win.
        if (   (sk_sq < wp_sq && _file (sk_sq) == _file (wp_sq))
            || ( (SquareDist[wk_sq][wp_sq] >= 3 + (_weak_side == pos.active ()))
              && (SquareDist[wk_sq][sr_sq] >= 3)
               )
           )
        {
            value = VALUE_EG_ROOK - Value (SquareDist[sk_sq][wp_sq]);
        }
        // If the pawn is far advanced and supported by the defending king, it's a drawish.
        else if ( (_rank (wk_sq) <= R_3)
               && (SquareDist[wk_sq][wp_sq] == 1)
               && (_rank (sk_sq) >= R_4)
               && (SquareDist[sk_sq][wp_sq] > 2 + (_stong_side == pos.active ()))
                )
        {
            value = Value (80 - SquareDist[sk_sq][wp_sq] * 8);
        }
        else
        {
            value = Value (200)
                  - Value (8 * SquareDist[sk_sq][wp_sq+DEL_S])
                  + Value (8 * SquareDist[wk_sq][wp_sq+DEL_S])
                  + Value (8 * SquareDist[wp_sq][queening_sq]);
        }

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    // KR vs KB. This is very simple, and always returns drawish scores.
    // The score is slightly bigger when the defending king is close to the edge.
    Value Endgame<KRKB>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BSHP, 0));

        Square sk_sq = pos.king_sq (_stong_side);
        Square wk_sq = pos.king_sq (_weak_side);
        Square wb_sq = pos.list<BSHP> (_weak_side)[0];

        // When the weaker side ended up in the same corner as bishop.
        Value value  = Value (PushToEdges[wk_sq] / 4);

        // To draw, the weaker side should run towards the corner.
        // And not just any corner! Only a corner that's not the same color as the bishop will do.
        if (   (CRNR_bb & wk_sq)
            && (opposite_colors (wk_sq, wb_sq))
            && (SquareDist[wk_sq][wb_sq] == 1)
            && (SquareDist[sk_sq][wb_sq] >  1)
           )
        {
            value /= 8;
        }

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    // KR vs KN.  The attacking side has slightly better winning chances than
    // in KR vs KB, particularly if the king and the knight are far apart.
    Value Endgame<KRKN>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_NIHT, 0));

        Square wk_sq = pos.king_sq (_weak_side);
        Square wn_sq = pos.list<NIHT> (_weak_side)[0];
        Value value  = Value (PushToEdges[wk_sq] + PushAway[SquareDist[wk_sq][wn_sq]]);

        // If weaker king is near the knight, it's a draw.
        if (   (_weak_side == pos.active ())
            && (SquareDist[wk_sq][wn_sq] <= 3)
           )
        {
            value /= 8;
        }

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    // KQ vs KP. In general, this is a win for the stronger side, but there are a
    // few important exceptions. A pawn on 7th rank and on the A,C,F or H files
    // with a king positioned next to it can be a draw, so in that case, we only
    // use the distance between the kings.
    Value Endgame<KQKP>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_QUEN, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO   , 1));

        Square sk_sq = pos.king_sq (_stong_side);
        Square wk_sq = pos.king_sq (_weak_side);
        Square wp_sq = pos.list<PAWN> (_weak_side)[0];

        Value value = Value (PushClose[SquareDist[sk_sq][wk_sq]]);

        if (   (rel_rank (_weak_side, wp_sq) != R_7)
            || (SquareDist[wk_sq][wp_sq] != 1)
            || !((FA_bb | FC_bb | FF_bb | FH_bb) & wp_sq)
           )
        {
            value += VALUE_EG_QUEN - VALUE_EG_PAWN;
        }

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    // KQ vs KR. This is almost identical to KX vs K:  We give the attacking
    // king a bonus for having the kings close together, and for forcing the
    // defending king towards the edge. If we also take care to avoid null move for
    // the defending side in the search, this is usually sufficient to win KQ vs KR.
    Value Endgame<KQKR>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_QUEN, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_ROOK, 0));

        Square sk_sq = pos.king_sq (_stong_side);
        Square wk_sq = pos.king_sq (_weak_side);

        Value value  = VALUE_EG_QUEN - VALUE_EG_ROOK
            + PushToEdges[wk_sq] + PushClose[SquareDist[sk_sq][wk_sq]];

        return (_stong_side == pos.active ()) ? +value : -value;
    }

    template<>
    // KBB vs KN. This is almost always a win. We try to push enemy king to a corner
    // and away from his knight. For a reference of this difficult endgame see:
    // en.wikipedia.org/wiki/Chess_endgame#Effect_of_tablebases_on_endgame_theory
    // But this endgame is not known, there are many position where it takes 50+ moves to win.
    // Because exact rule is not possible better to retire and allow the search to workout the endgame.
    Value Endgame<KBBKN>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, 2 * VALUE_MG_BSHP, 0));
        ASSERT (verify_material (pos,  _weak_side,     VALUE_MG_NIHT, 0));

        Square sk_sq = pos.king_sq (_stong_side);
        Square wk_sq = pos.king_sq (_weak_side);
        Square wn_sq = pos.list<NIHT> (_weak_side)[0];

        Value value;

        if (pos.bishops_pair (_stong_side))
        {
            u08 diag18_dist = min (SquareDist[wk_sq][SQ_A1], SquareDist[wk_sq][SQ_H8]);
            u08 diag81_dist = min (SquareDist[wk_sq][SQ_A8], SquareDist[wk_sq][SQ_H1]);
            if (diag81_dist < diag18_dist)
            {
                sk_sq = ~sk_sq;
                wk_sq = ~wk_sq;
                wn_sq = ~wn_sq;
            }

            value = VALUE_MG_BSHP + PushToCorners[wk_sq]
                  + PushClose[SquareDist[sk_sq][wk_sq]]
                  + PushAway [SquareDist[wk_sq][wn_sq]];
        }
        else
        {
            value = Value (PushClose[SquareDist[sk_sq][wk_sq]] / 8);
        }

        return (_stong_side == pos.active ()) ? +value : -value;
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
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_ROOK, 0));

        // Assume _stong_side is white and the pawn is on files A-D
        Square sk_sq = normalize (pos, _stong_side, pos.king_sq (_stong_side));
        Square wk_sq = normalize (pos, _stong_side, pos.king_sq (_weak_side));
        Square sr_sq = normalize (pos, _stong_side, pos.list<ROOK> (_stong_side)[0]);
        Square sp_sq = normalize (pos, _stong_side, pos.list<PAWN> (_stong_side)[0]);
        Square wr_sq = normalize (pos, _stong_side, pos.list<ROOK> (_weak_side)[0]);

        File f = _file (sp_sq);
        Rank r = _rank (sp_sq);
        Square queening_sq = f | R_8;
        i32 tempo = (pos.active () == _stong_side);

        // If the pawn is not too far advanced and the defending king defends the
        // queening square, use the third-rank defence.
        if (   (r <= R_5)
            && (sk_sq <= SQ_H5)
            && (SquareDist[wk_sq][queening_sq] <= 1)
            && (_rank (wr_sq) == R_6 || (r <= R_3 && _rank (sr_sq) != R_6))
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        // The defending side saves a draw by checking from behind in case the pawn
        // has advanced to the 6th rank with the king behind.
        if (   (r == R_6)
            && (SquareDist[wk_sq][queening_sq] <= 1)
            && (_rank (sk_sq) + tempo <= R_6)
            && (_rank (wr_sq) == R_1 || (!tempo && FileRankDist[_file (wr_sq)][f] >= 3))
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        if (   (r >= R_6)
            && (wk_sq == queening_sq)
            && (_rank (wr_sq) == R_1)
            && (!tempo || SquareDist[sk_sq][sp_sq] >= 2)
           )
        {
            return SCALE_FACTOR_DRAW;
        }
        // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
        // and the black rook is behind the pawn.
        if (   (sp_sq == SQ_A7)
            && (sr_sq == SQ_A8)
            && (wk_sq == SQ_H7 || wk_sq == SQ_G7)
            && (_file (wr_sq) == F_A)
            && (_rank (wr_sq) <= R_3 || _file (sk_sq) >= F_D || _rank (sk_sq) <= R_5)
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        // If the defending king blocks the pawn and the attacking king is too far away, it's a draw.
        if (   (r <= R_5)
            && (wk_sq == sp_sq+DEL_N)
            && (SquareDist[sk_sq][sp_sq] - tempo >= 2)
            && (SquareDist[sk_sq][wr_sq] - tempo >= 2)
           )
        {
            return SCALE_FACTOR_DRAW;
        }
        // Pawn on the 7th rank supported by the rook from behind usually wins if the
        // attacking king is closer to the queening square than the defending king,
        // and the defending king cannot gain tempi by threatening the attacking rook.
        if (   (r == R_7)
            && (f != F_A)
            && (f == _file (sr_sq))
            && (sr_sq != queening_sq)
            && (SquareDist[sk_sq][queening_sq] < SquareDist[wk_sq][queening_sq] - 2 + tempo)
            && (SquareDist[sk_sq][queening_sq] < SquareDist[wk_sq][sr_sq] + tempo)
           )
        {
            return ScaleFactor (SCALE_FACTOR_MAX - 2 * SquareDist[sk_sq][queening_sq]);
        }

        // Similar to the above, but with the pawn further back
        if (   (f != F_A)
            && (f == _file (sr_sq))
            && (sr_sq < sp_sq)
            && (SquareDist[sk_sq][queening_sq] < SquareDist[wk_sq][queening_sq] - 2 + tempo)
            && (SquareDist[sk_sq][sp_sq+DEL_N] < SquareDist[wk_sq][sp_sq+DEL_N] - 2 + tempo)
            && ( SquareDist[wk_sq][sr_sq] + tempo >= 3
             || ( SquareDist[sk_sq][queening_sq] < SquareDist[wk_sq][sr_sq] + tempo
              &&  SquareDist[sk_sq][sp_sq+DEL_N] < SquareDist[wk_sq][sr_sq] + tempo
                )
               )
           )
        {
            return ScaleFactor (SCALE_FACTOR_MAX - 8 * SquareDist[sp_sq][queening_sq] - 2 * SquareDist[sk_sq][queening_sq]);
        }

        // If the pawn is not far advanced, and the defending king is somewhere in
        // the pawn's path, it's probably a draw.
        if (r <= R_4 && wk_sq > sp_sq)
        {
            if (_file (wk_sq) == _file (sp_sq))
            {
                return ScaleFactor (10);
            }
            if (file_dist (wk_sq, sp_sq) == 1 && SquareDist[sk_sq][wk_sq] > 2)
            {
                return ScaleFactor (24 - 2 * SquareDist[sk_sq][wk_sq]);
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KRP vs KB.
    // TODO::
    ScaleFactor Endgame<KRPKB>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BSHP, 0));

        // Test for a rook pawn
        if (pos.pieces<PAWN> () & (FA_bb | FH_bb))
        {
            Square wk_sq = pos.king_sq(_weak_side);
            Square wb_sq = pos.list<BSHP> (_weak_side)[0];
            Square sp_sq = pos.list<PAWN> (_stong_side)[0];
            Rank   r     = rel_rank (_stong_side, sp_sq);
            Delta  push  = pawn_push (_stong_side);

            // If the pawn is on the 5th rank and the pawn (currently) is on the 
            // same color square as the bishop then there is a chance of a fortress.
            // Depending on the king position give a moderate reduction or a stronger one
            // if the defending king is near the corner but not trapped there.
            if (r == R_5 && !opposite_colors (wb_sq, sp_sq))
            {
                u08 d = SquareDist[sp_sq + 3 * push][wk_sq];

                if (d <= 2 && !(d == 0 && wk_sq == pos.king_sq(_stong_side) + 2 * push))
                {
                    return ScaleFactor (24);
                }
                else
                {
                    return ScaleFactor (48);
                }
            }

            // When the pawn has moved to the 6th rank we can be fairly sure it's drawn
            // if the bishop attacks the square in front of the pawn from a reasonable distance
            // and the defending king is near the corner
            if (   (r == R_6)
                && (SquareDist[sp_sq + 2 * push][wk_sq] <= 1)
                && (PieceAttacks[BSHP][wb_sq] & (sp_sq + push))
                && (file_dist (wb_sq, sp_sq) >= 2)
               )
            {
                return ScaleFactor (8);
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KRPP vs KRP. There is just a single rule: if the stronger side has no passed
    // pawns and the defending king is actively placed, the position is drawish.
    ScaleFactor Endgame<KRPPKRP>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK, 2));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_ROOK, 1));

        Square sp_sq1 = pos.list<PAWN> (_stong_side)[0];
        Square sp_sq2 = pos.list<PAWN> (_stong_side)[1];
        Square wk_sq  = pos.king_sq (_weak_side);

        // Does the stronger side have a passed pawn?
        if (pos.passed_pawn (_stong_side, sp_sq1) || pos.passed_pawn (_stong_side, sp_sq2))
        {
            return SCALE_FACTOR_NONE;
        }

        Rank r = max (rel_rank (_stong_side, sp_sq1), rel_rank (_stong_side, sp_sq2));

        if (   (file_dist (wk_sq, sp_sq1) <= 1)
            && (file_dist (wk_sq, sp_sq2) <= 1)
            && (rel_rank (_stong_side, wk_sq) > r)
           )
        {
            switch (r)
            {
            case R_2: return ScaleFactor (10);
            case R_3: return ScaleFactor (10);
            case R_4: return ScaleFactor (15);
            case R_5: return ScaleFactor (20);
            case R_6: return ScaleFactor (40);
            default: ASSERT (false);
            }
        }
        return SCALE_FACTOR_NONE;
    }

    template<>
    // K and two or more pawns vs K. There is just a single rule here: If all pawns
    // are on the same rook file and are blocked by the defending king, it's a draw.
    ScaleFactor Endgame<KPsK>::operator() (const Position &pos) const
    {
        ASSERT (pos.non_pawn_material(_stong_side) == VALUE_ZERO);
        ASSERT (pos.count<PAWN> (_stong_side) >= 2);
        ASSERT (verify_material (pos, _weak_side, VALUE_ZERO, 0));

        Square    wk_sq = pos.king_sq (_weak_side);
        Bitboard spawns = pos.pieces<PAWN> (_stong_side);
        Square    sp_sq = scan_frntmost_sq (_stong_side, spawns);

        // If all pawns are ahead of the king, all pawns are on a single
        // rook file and the king is within one file of the pawns then draw.
        if (   !(spawns & ~FrontRank_bb[_weak_side][_rank (wk_sq)])
            && !((spawns & FA_bb_) && (spawns & FH_bb_))
            && (file_dist (wk_sq, sp_sq) <= 1)
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
        ASSERT (verify_material (pos, _stong_side, VALUE_ZERO, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO, 1));

        // Assume _stong_side is white and the pawn is on files A-D
        Square sk_sq = normalize (pos, _stong_side, pos.king_sq (_stong_side));
        Square wk_sq = normalize (pos, _stong_side, pos.king_sq (_weak_side));
        Square sp_sq = normalize (pos, _stong_side, pos.list<PAWN> (_stong_side)[0]);

        // If the pawn has advanced to the fifth rank or further, and is not a
        // rook pawn, it's too dangerous to assume that it's at least a draw.
        if (_rank (sp_sq) >= R_5 && _file (sp_sq) != F_A)
        {
            return SCALE_FACTOR_NONE;
        }

        Color c = (_stong_side == pos.active ()) ? WHITE : BLACK;

        // Probe the KPK bitbase with the weakest side's pawn removed. If it's a draw,
        // it's probably at least a draw even with the pawn.
        return probe_kpk (c, sk_sq, sp_sq, wk_sq)
            ? SCALE_FACTOR_NONE
            : SCALE_FACTOR_DRAW;
    }

    template<>
    // KNP vs K. There is a single rule: if the pawn is a rook pawn on the 7th rank
    // and the defending king prevents the pawn from advancing the position is drawn.
    ScaleFactor Endgame<KNPK>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_NIHT, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO   , 0));

        // Assume _stong_side is white and the pawn is on files A-D
        Square sp_sq = normalize (pos, _stong_side, pos.list<PAWN> (_stong_side)[0]);
        Square wk_sq = normalize (pos, _stong_side, pos.king_sq (_weak_side));

        if (sp_sq == SQ_A7 && SquareDist[SQ_A8][wk_sq] <= 1)
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
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_BSHP, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BSHP, 0));

        Square sp_sq = pos.list<PAWN> (_stong_side)[0];
        Square sb_sq = pos.list<BSHP> (_stong_side)[0];
        Square wb_sq = pos.list<BSHP> (_weak_side)[0];
        Square wk_sq = pos.king_sq (_weak_side);

        // Case 1: Defending king blocks the pawn, and cannot be driven away
        if (   (_file (wk_sq) == _file (sp_sq))
            && (rel_rank (_stong_side, sp_sq) < rel_rank (_stong_side, wk_sq))
            && (opposite_colors (wk_sq, sb_sq) || rel_rank (_stong_side, wk_sq) <= R_6)
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        // Case 2: Opposite colored bishops
        if (opposite_colors (sb_sq, wb_sq))
        {
            // We assume that the position is drawn in the following three situations:
            //
            //   a. The pawn is on rank 5 or further back.
            //   b. The defending king is somewhere in the pawn's path.
            //   c. The defending bishop attacks some square along the pawn's path,
            //      and is at least three squares away from the pawn.
            //
            // These rules are probably not perfect, but in practice they work reasonably well.

            if (rel_rank (_stong_side, sp_sq) <= R_5)
            {
                return SCALE_FACTOR_DRAW;
            }
            
            Bitboard path = FrontSqs_bb[_stong_side][sp_sq];
            if (   (path & pos.pieces<KING> (_weak_side))
               || ((path & attacks_bb<BSHP> (wb_sq, pos.pieces ())) && SquareDist[wb_sq][sp_sq] >= 3)
               )
            {
                return SCALE_FACTOR_DRAW;
            }
        }
        else
        {
            // TODO:: position fen 8/6K1/5P2/6kb/2B5/8/8/8 w - - 0 1
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KBPP vs KB. It detects a few basic draws with opposite-colored bishops.
    ScaleFactor Endgame<KBPPKB>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_BSHP, 2));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BSHP, 0));

        Square sb_sq = pos.list<BSHP> (_stong_side)[0];
        Square wb_sq = pos.list<BSHP> (_weak_side)[0];

        if (!opposite_colors (sb_sq, wb_sq))
        {
            return SCALE_FACTOR_NONE;
        }

        Square wk_sq = pos.king_sq (_weak_side);
        Square sp_sq1 = pos.list<PAWN> (_stong_side)[0];
        Square sp_sq2 = pos.list<PAWN> (_stong_side)[1];
        Rank r1 = _rank (sp_sq1);
        Rank r2 = _rank (sp_sq2);
        Square block_sq1, block_sq2;

        if (rel_rank (_stong_side, sp_sq1) > rel_rank (_stong_side, sp_sq2))
        {
            block_sq1 = sp_sq1 + pawn_push (_stong_side);
            block_sq2 = _file (sp_sq2) | _rank (sp_sq1);
        }
        else
        {
            block_sq1 = sp_sq2 + pawn_push (_stong_side);
            block_sq2 = _file (sp_sq1) | _rank (sp_sq2);
        }

        switch (file_dist (sp_sq1, sp_sq2))
        {
        case 0:
            // Both pawns are on the same file. It's an easy draw if the defender firmly
            // controls some square in the frontmost pawn's path.
            if (   (_file (wk_sq) == _file (block_sq1))
                && (rel_rank (_stong_side, wk_sq) >= rel_rank (_stong_side, block_sq1))
                && opposite_colors (wk_sq, sb_sq)
               )
            {
                return SCALE_FACTOR_DRAW;
            }

            break;
            
        case 1:
            // Pawns on adjacent files. It's a draw if the defender firmly controls the
            // square in front of the frontmost pawn's path, and the square diagonally
            // behind this square on the file of the other pawn.
            if (   (wk_sq == block_sq1)
                && opposite_colors (wk_sq, sb_sq)
                && ((wb_sq == block_sq2)
                 || (attacks_bb<BSHP> (block_sq2, pos.pieces ()) & pos.pieces<BSHP> (_weak_side))
                 || (FileRankDist[r1][r2] >= 2)
                   )
               )
            {
                return SCALE_FACTOR_DRAW;
            }

            if (   (wk_sq == block_sq2)
                && opposite_colors (wk_sq, sb_sq)
                && ((wb_sq == block_sq1)
                 || (attacks_bb<BSHP> (block_sq1, pos.pieces ()) & pos.pieces<BSHP> (_weak_side))
                   )
               )
            {
                return SCALE_FACTOR_DRAW;
            }

            break;

        default:
            // The pawns are not on the same file or adjacent files. No scaling.
            break;
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KBP vs KN. There is a single rule: If the defending king is somewhere along
    // the path of the pawn, and the square of the king is not of the same color as
    // the stronger side's bishop, it's a draw.
    ScaleFactor Endgame<KBPKN>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_BSHP, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_NIHT, 0));

        Square sp_sq = pos.list<PAWN> (_stong_side)[0];
        Square sb_sq = pos.list<BSHP> (_stong_side)[0];
        Square wk_sq = pos.king_sq (_weak_side);

        if (   (_file (wk_sq) == _file (sp_sq))
            && (rel_rank (_stong_side, sp_sq) < rel_rank (_stong_side, wk_sq))
            && (opposite_colors (wk_sq, sb_sq) || rel_rank (_stong_side, wk_sq) <= R_6)
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
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_NIHT, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BSHP, 0));

        Square sp_sq = pos.list<PAWN> (_stong_side)[0];
        Square sb_sq = pos.list<BSHP> (_weak_side)[0];
        Square wk_sq = pos.king_sq (_weak_side);
        
        // King needs to get close to promoting pawn to prevent knight from blocking.
        // Rules for this are very tricky, so just approximate.
        if (FrontSqs_bb[_stong_side][sp_sq] & attacks_bb<BSHP> (sb_sq, pos.pieces ()))
        {
            return ScaleFactor (SquareDist[wk_sq][sp_sq]);
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
        ASSERT (pos.non_pawn_material (_stong_side) == VALUE_MG_BSHP);
        ASSERT (pos.count<BSHP> (_stong_side) == 1);
        ASSERT (pos.count<PAWN> (_stong_side) >= 1);
        // No assertions about the material of _weak_side, because we want draws to
        // be detected even when the weaker side has some materials or pawns.

        Bitboard spawns = pos.pieces<PAWN> (_stong_side);
        Square    sp_sq = scan_frntmost_sq (_stong_side, spawns);
        File       wp_f = _file (sp_sq);

        // All pawns are on a single rook file ?
        if ((wp_f == F_A || wp_f == F_H) && !(spawns & ~File_bb[wp_f]))
        {
            Square sb_sq = pos.list<BSHP> (_stong_side)[0];
            Square queening_sq = rel_sq (_stong_side, wp_f | R_8);
            Square sk_sq = pos.king_sq (_stong_side);
            Square wk_sq = pos.king_sq (_weak_side);

            // The bishop has the wrong color.
            if (opposite_colors (queening_sq, sb_sq))
            {
                // If the defending king defends the queening square.
                if (SquareDist[queening_sq][wk_sq] <= 1)
                {
                    return SCALE_FACTOR_DRAW;
                }

                // If the defending king has some pawns
                Bitboard wpawns = pos.pieces<PAWN> (_weak_side);
                if (wpawns && !(wpawns & ~File_bb[wp_f]))
                {
                    Square wp_sq = scan_frntmost_sq (_weak_side, wpawns);
                    if (   (rel_rank (_weak_side, wp_sq) == R_5)
                        && (rel_rank (_weak_side, sp_sq) == R_6)
                        && opposite_colors (wp_sq, sb_sq)
                       )
                    {
                        i32 tempo = (pos.active () == _stong_side);
                        if (SquareDist[queening_sq][wk_sq] < SquareDist[wp_sq][sk_sq] + 4 - tempo)
                        {
                            return SCALE_FACTOR_DRAW;
                        }

                        return ScaleFactor (SquareDist[queening_sq][wk_sq]);
                    }

                }
            }
        }

        // All pawns on same B or G file? Then potential draw
        if (   (wp_f == F_B || wp_f == F_G)
            && !(pos.pieces<PAWN> () & ~File_bb[wp_f])
            && (pos.non_pawn_material (_weak_side) == 0)
            && (pos.count<PAWN> (_weak_side) >= 1)
           )
        {
            // Get _weak_side pawn that is closest to home rank
            Square wp_sq = scan_backmost_sq (_weak_side, pos.pieces<PAWN> (_weak_side));

            Square sk_sq = pos.king_sq (_stong_side);
            Square wk_sq = pos.king_sq (_weak_side);
            Square sb_sq = pos.list<BSHP> (_stong_side)[0];

            //// It's a draw if weaker pawn is on rank 7, bishop can't attack the pawn, and
            //// weaker king can stop opposing opponent's king from penetrating.
            //if (   rel_rank (_stong_side, wp_sq) == R_7
            //    && opposite_colors (sb_sq, wp_sq)
            //    && SquareDist[wp_sq][wk_sq] <= SquareDist[wp_sq][sk_sq]
            //   )
            //{
            //    return SCALE_FACTOR_DRAW;
            //}

            // There's potential for a draw if our pawn is blocked on the 7th rank
            // the bishop cannot attack it or they only have one pawn left
            if (   (rel_rank (_stong_side, wp_sq) == R_7)
                && (pos.pieces<PAWN> (_stong_side) & (wp_sq + pawn_push (_weak_side)))
                && (opposite_colors (sb_sq, wp_sq) || pos.count<PAWN> (_stong_side) == 1)
               )
            {
                i32 sk_dist = SquareDist[sk_sq][wp_sq];
                i32 wk_dist = SquareDist[wk_sq][wp_sq];

                // It's a draw if the weak king is on its back two ranks, within 2
                // squares of the blocking pawn and the strong king is not
                // closer. (I think this rule only fails in practically
                // unreachable positions such as 5k1K/6p1/6P1/8/8/3B4/8/8 w
                // and positions where qsearch will immediately correct the
                // problem such as 8/4k1p1/6P1/1K6/3B4/8/8/8 w)
                if (   (rel_rank (_stong_side, wk_sq) >= R_7)
                    && (wk_dist <= 2)
                    && (wk_dist <= sk_dist)
                   )
                {
                    return SCALE_FACTOR_DRAW;
                }
            }
        }
        return SCALE_FACTOR_NONE;
    }

    template<>
    // KQ vs KR and one or more pawns.
    // It tests for fortress draws with a rook on the 3rd rank defended by a pawn.
    ScaleFactor Endgame<KQKRPs>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_QUEN, 0));
        ASSERT (pos.count<ROOK> (_weak_side) == 1);
        ASSERT (pos.count<PAWN> (_weak_side) >= 1);

        Square wk_sq = pos.king_sq (_weak_side);
        Square wr_sq = pos.list<ROOK> (_weak_side)[0];

        if (   (rel_rank (_weak_side, wk_sq) <= R_2)
            && (rel_rank (_weak_side, pos.king_sq (_stong_side)) >= R_4)
            && (rel_rank (_weak_side, wr_sq) == R_3)
            && (pos.pieces<PAWN> (_weak_side)
              & PieceAttacks[KING][wk_sq]
              & PawnAttacks[_stong_side][wr_sq]
               )
           )
        {
            return SCALE_FACTOR_DRAW;
        }

        return SCALE_FACTOR_NONE;
    }


    void   initialize ()
    {
        if (EndGames == NULL)
        {
            EndGames = new Endgames();
        }
    }

    void deinitialize ()
    {
        if (EndGames != NULL)
        {
            delete EndGames;
            EndGames = NULL;
        }
    }

}
