#include "Endgame.h"
#include <algorithm>
#include <cassert>

#include "BitBoard.h"
#include "BitBases.h"
#include "MoveGenerator.h"

using namespace std;
using namespace BitBoard;
using namespace MoveGenerator;

namespace EndGame {

    namespace {

        const Bitboard Corner_bb = U64(0x8100000000000081);


        // Table used to drive the king towards the edge of the board
        // in KX vs K and KQ vs KR endgames.
        const int32_t PushToEdges[SQ_NO] =
        {
            100, 90,  80,  70,  70,  80,  90, 100,
            90,  70,  60,  50,  50,  60,  70,  90,
            80,  60,  40,  30,  30,  40,  60,  80,
            70,  50,  30,  20,  20,  30,  50,  70,
            70,  50,  30,  20,  20,  30,  50,  70,
            80,  60,  40,  30,  30,  40,  60,  80,
            90,  70,  60,  50,  50,  60,  70,  90,
            100, 90,  80,  70,  70,  80,  90, 100,
        };

        // Table used to drive the king towards a corner square of the
        // right color in KBN vs K endgames.
        const int32_t PushToCorners[SQ_NO] =
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
        const int32_t PushClose[8] = {  0,  0, 100,  80,  60,  40,  20,  10 };
        const int32_t PushAway [8] = {  0,  5,  20,  40,  60,  80,  90, 100 };

#ifdef _DEBUG

        inline bool verify_material (const Position &pos, Color c, Value npm, uint32_t num_pawns)
        {
            return (pos.non_pawn_material(c) == npm) && (pos.piece_count<PAWN> (c) == num_pawns);
        }

#endif

        // Map the square as if strong_side is white and
        // strong_side's only pawn is on the left half of the board.
        inline Square normalize (const Position &pos, Color strong_side, Square sq)
        {
            ASSERT (pos.piece_count<PAWN> (strong_side) == 1);

            if (_file (pos.piece_list<PAWN> (strong_side)[0]) >= F_E)
            {
                sq = !sq; // MIRROR
            }
            if (BLACK == strong_side)
            {
                sq = ~sq; // FLIP
            }

            return sq;
        }

        // Get the material key of a Position out of the given endgame key code
        // like "KBPKN". The trick here is to first forge an ad-hoc fen string
        // and then let a Position object to do the work for us. Note that the
        // fen string could correspond to an illegal position.
        Key key (const string &code, Color c)
        {
            int32_t length = code.length (); 
            ASSERT (0 < length && length <= 8);
            ASSERT (code[0] == 'K');

            string sides[CLR_NO] =
            {
                code.substr (   code.find('K', 1)), // Lossing
                code.substr (0, code.find('K', 1)), // Winning
            };

            transform (sides[c].begin (), sides[c].end (), sides[c].begin (), ::tolower);
            string empty = string ("") + char ('0' + 8 - length);
            if ("0" == empty) empty = "";
            string fen = sides[0] + empty + sides[1] + "/8/8/8/8/8/8/8 w - - 0 10";
            return Position (fen).matl_key ();
        }

        template<class M>
        void delete_endgame (const typename M::value_type &p) { delete p.second; }

    } // namespace

    // Endgames members definitions
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
        // KBBKN is retired
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

    Endgames::~Endgames ()
    {
        for_each (m1.begin (), m1.end (), delete_endgame<M1>);
        for_each (m2.begin (), m2.end (), delete_endgame<M2>);
    }

    template<EndgameType E>
    void Endgames::add (const string &code)
    {
        map ((Endgame<E>*) 0)[key (code, WHITE)] = new Endgame<E> (WHITE);
        map ((Endgame<E>*) 0)[key (code, BLACK)] = new Endgame<E> (BLACK);
    }

    template<>
    // Mate with KX vs K. This function is used to evaluate positions with
    // King and plenty of material vs a lone king. It simply gives the
    // attacking side a bonus for driving the defending king towards the edge
    // of the board, and for keeping the distance between the two kings small.
    Value Endgame<KXK>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _weak_side, VALUE_ZERO, 0));
        ASSERT (!pos.checkers ()); // Eval is never called when in check

        // Stalemate detection with lone king
        if (pos.active () == _weak_side && generate<LEGAL> (pos).size() == 0)
        {
            return VALUE_DRAW;
        }

        Value value;

        Square wk_sq = pos.king_sq (_stong_side);
        Square bk_sq = pos.king_sq (_weak_side);

        if (!pos.piece_count<PAWN> (_stong_side) &&
            pos.non_pawn_material(_stong_side) < VALUE_MG_ROOK)
        {
            value = VALUE_DRAW 
                +   pos.piece_count (_stong_side)
                -   pos.piece_count (_weak_side);
        }
        else
        {
            int32_t bishop_count = pos.piece_count<BSHP> (_stong_side);

            value = pos.non_pawn_material (_stong_side)
                //-   bishop_count * VALUE_MG_BISHOP
                +   pos.piece_count<PAWN> (_stong_side) * VALUE_EG_PAWN
                +   PushToEdges[bk_sq] + PushClose[square_dist (wk_sq, bk_sq)];

            bool bishop_pair = bishop_count > 1 && pos.bishops_pair (_stong_side);
            //value += bishop_pair
            //    ? bishop_count * VALUE_MG_BISHOP
            //    : bishop_count + VALUE_MG_BISHOP;

            if (pos.piece_count<QUEN> (_stong_side) ||
                pos.piece_count<ROOK> (_stong_side) ||
                pos.piece_count<NIHT> (_stong_side) > 2 ||
                bishop_pair)
            {
                value += VALUE_KNOWN_WIN;
            }
        }

        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    Value Endgame<KNNK> ::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, 2 * VALUE_MG_KNIGHT, 0));

        Value value = VALUE_DRAW + pos.piece_count<NIHT> (_stong_side);
        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    // Mate with KBN vs K. This is similar to KX vs K, but we have to drive the
    // defending king towards a corner square of the right color.
    Value Endgame<KBNK>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_KNIGHT + VALUE_MG_BISHOP, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO, 0));

        Square wk_sq = pos.king_sq (_stong_side);
        Square bk_sq = pos.king_sq (_weak_side);
        Square wb_sq = pos.piece_list<BSHP> (_stong_side)[0];

        // kbnk_mate_table() tries to drive toward corners A1 or H8,
        // if we have a bishop that cannot reach the above squares we
        // mirror the kings so to drive enemy toward corners A8 or H1.
        if (opposite_colors (wb_sq, SQ_A1))
        {
            wk_sq = ~(wk_sq);
            bk_sq = ~(bk_sq);
        }

        Value value = VALUE_KNOWN_WIN
            + PushClose[square_dist (wk_sq, bk_sq)] + PushToCorners[bk_sq];

        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    // KR vs KP. This is a somewhat tricky endgame to evaluate precisely without
    // a bitbase. The function below returns drawish scores when the pawn is
    // far advanced with support of the king, while the attacking king is far
    // away.
    Value Endgame<KRKP>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO   , 1));

        Square wk_sq = rel_sq (_stong_side, pos.king_sq (_stong_side));
        Square bk_sq = rel_sq (_stong_side, pos.king_sq (_weak_side));
        Square wr_sq = rel_sq (_stong_side, pos.piece_list<ROOK> (_stong_side)[0]);
        Square bp_sq = rel_sq (_stong_side, pos.piece_list<PAWN> (_weak_side)[0]);

        Square queening_sq = (_file (bp_sq) | R_1);
        Value value;

        // If the stronger side's king is in front of the pawn, it's a win.
        if (wk_sq < bp_sq && _file (wk_sq) == _file (bp_sq))
        {
            value = VALUE_EG_ROOK - Value (square_dist (wk_sq, bp_sq));
        }
        // If the weaker side's king is too far from the pawn and the rook, it's a win.
        else if (square_dist (bk_sq, bp_sq) >= 3 + (pos.active () == _weak_side)
            &&   square_dist (bk_sq, wr_sq) >= 3)
        {
            value = VALUE_EG_ROOK - Value (square_dist (wk_sq, bp_sq));
        }
        // If the pawn is far advanced and supported by the defending king, it's a drawish.
        else if (_rank (bk_sq) <= R_3 
            &&   square_dist (bk_sq, bp_sq) == 1
            &&   _rank (wk_sq) >= R_4
            &&   square_dist (wk_sq, bp_sq) > 2 + (pos.active () == _stong_side))
        {
            value = Value (80 - square_dist (wk_sq, bp_sq) * 8);
        }
        else
        {
            value =  Value (200)
                - Value (square_dist (wk_sq, bp_sq + DEL_S) * 8)
                + Value (square_dist (bk_sq, bp_sq + DEL_S) * 8)
                + Value (square_dist (bp_sq, queening_sq) * 8);
        }

        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    // KR vs KB. This is very simple, and always returns drawish scores.
    // The score is slightly bigger when the defending king is close to the edge.
    Value Endgame<KRKB>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK  , 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BISHOP, 0));

        Square bk_sq = pos.king_sq (_weak_side);

        //// To draw, the weaker side should run towards the corner.
        //// And not just any corner! Only a corner that's not the same color as the bishop will do.
        //if (Corner_bb & bk_sq)
        //{
        //    Square bb_sq = pos.piece_list<BSHP> (_weak_side)[0];
        //    if (opposite_colors (bk_sq, bb_sq)) return VALUE_DRAW;
        //}

        // when the weaker side ended up in the wrong corner.
        Value value = Value (PushToEdges[bk_sq]);

        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    // KR vs KN.  The attacking side has slightly better winning chances than
    // in KR vs KB, particularly if the king and the knight are far apart.
    Value Endgame<KRKN>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK  , 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_KNIGHT, 0));

        Square bk_sq = pos.king_sq (_weak_side);
        Square bn_sq = pos.piece_list<NIHT> (_weak_side)[0];
        Value value = Value (PushToEdges[bk_sq] + PushAway[square_dist (bk_sq, bn_sq)]);

        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    // KQ vs KP. In general, this is a win for the stronger side, but there are a
    // few important exceptions. A pawn on 7th rank and on the A,C,F or H files
    // with a king positioned next to it can be a draw, so in that case, we only
    // use the distance between the kings.
    Value Endgame<KQKP>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_QUEEN, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO    , 1));

        Square wk_sq = pos.king_sq (_stong_side);
        Square bk_sq = pos.king_sq (_weak_side);
        Square bp_sq = pos.piece_list<PAWN> (_weak_side)[0];

        Value value = Value (PushClose[square_dist (wk_sq, bk_sq)]);

        if (rel_rank (_weak_side, bp_sq) != R_7 ||
            square_dist (bk_sq, bp_sq) != 1 ||
            !((FA_bb | FC_bb | FF_bb | FH_bb) & bp_sq))
        {
            value += VALUE_EG_QUEEN - VALUE_EG_PAWN;
        }

        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    // KQ vs KR. This is almost identical to KX vs K:  We give the attacking
    // king a bonus for having the kings close together, and for forcing the
    // defending king towards the edge. If we also take care to avoid null move for
    // the defending side in the search, this is usually sufficient to win KQ vs KR.
    Value Endgame<KQKR>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_QUEEN, 0));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_ROOK , 0));

        Square wk_sq = pos.king_sq (_stong_side);
        Square bk_sq = pos.king_sq (_weak_side);

        Value value = VALUE_EG_QUEEN - VALUE_EG_ROOK
            + PushToEdges[bk_sq] + PushClose[square_dist (wk_sq, bk_sq)];

        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    // KBB vs KN. This is almost always a win. We try to push enemy king to a corner
    // and away from his knight. For a reference of this difficult endgame see:
    // en.wikipedia.org/wiki/Chess_endgame#Effect_of_tablebases_on_endgame_theory
    // But this endgame is not known, there are many position where it takes 50+ moves to win.
    // Because exact rule is not possible better to retire and allow the search to workout the endgame.
    Value Endgame<KBBKN>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, 2 * VALUE_MG_BISHOP, 0));
        ASSERT (verify_material (pos,  _weak_side,     VALUE_MG_KNIGHT, 0));

        Square wk_sq = pos.king_sq (_stong_side);
        Square bk_sq = pos.king_sq (_weak_side);
        Square bn_sq = pos.piece_list<NIHT> (_weak_side)[0];

        Value value;

        if (!pos.bishops_pair (_stong_side))
        {
            value = VALUE_DRAW
                + pos.piece_count (_stong_side)
                - pos.piece_count (_weak_side);
        }
        else
        {
            value = VALUE_MG_KNIGHT + PushToCorners[bk_sq]
            + PushClose[square_dist (wk_sq, bk_sq)]
            + PushAway[square_dist (bk_sq, bn_sq)];
        }

        return (_stong_side == pos.active ()) ? value : -value;
    }

    template<>
    Value Endgame<KmmKm>::operator() (const Position &pos) const
    {
        Value value = VALUE_DRAW
            + pos.piece_count (_stong_side)
            - pos.piece_count (_weak_side);

        return (_stong_side == pos.active ()) ? value : -value;
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
        Square wk_sq = normalize (pos, _stong_side, pos.king_sq (_stong_side));
        Square bk_sq = normalize (pos, _stong_side, pos.king_sq (_weak_side));
        Square wr_sq = normalize (pos, _stong_side, pos.piece_list<ROOK> (_stong_side)[0]);
        Square wp_sq = normalize (pos, _stong_side, pos.piece_list<PAWN> (_stong_side)[0]);
        Square br_sq = normalize (pos, _stong_side, pos.piece_list<ROOK> (_weak_side)[0]);

        File f = _file (wp_sq);
        Rank r = _rank (wp_sq);
        Square queening_sq = f | R_8;
        int32_t tempo = (pos.active () == _stong_side);

        // If the pawn is not too far advanced and the defending king defends the
        // queening square, use the third-rank defence.
        if (r <= R_5 && wk_sq <= SQ_H5 &&
            square_dist (bk_sq, queening_sq) <= 1 &&
            (_rank (br_sq) == R_6 || (r <= R_3 && _rank (wr_sq) != R_6)))
        {
            return SCALE_FACTOR_DRAW;
        }

        // The defending side saves a draw by checking from behind in case the pawn
        // has advanced to the 6th rank with the king behind.
        if (r == R_6 &&
            square_dist (bk_sq, queening_sq) <= 1 &&
            _rank (wk_sq) + tempo <= R_6 &&
            (_rank (br_sq) == R_1 || (!tempo && file_dist (_file (br_sq), f) >= 3)))
        {
            return SCALE_FACTOR_DRAW;
        }

        if (r >= R_6 && bk_sq == queening_sq &&
            _rank (br_sq) == R_1 &&
            (!tempo || square_dist (wk_sq, wp_sq) >= 2))
        {
            return SCALE_FACTOR_DRAW;
        }
        // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
        // and the black rook is behind the pawn.
        if ( wp_sq == SQ_A7 && wr_sq == SQ_A8 &&
            (bk_sq == SQ_H7 || bk_sq == SQ_G7) &&
            _file (br_sq) == F_A &&
            (_rank (br_sq) <= R_3 || _file (wk_sq) >= F_D || _rank (wk_sq) <= R_5))
        {
            return SCALE_FACTOR_DRAW;
        }

        // If the defending king blocks the pawn and the attacking king is too far away, it's a draw.
        if (r <= R_5 && bk_sq == wp_sq + DEL_N &&
            square_dist (wk_sq, wp_sq) - tempo >= 2 &&
            square_dist (wk_sq, br_sq) - tempo >= 2)
        {
            return SCALE_FACTOR_DRAW;
        }
        // Pawn on the 7th rank supported by the rook from behind usually wins if the
        // attacking king is closer to the queening square than the defending king,
        // and the defending king cannot gain tempi by threatening the attacking rook.
        if (r == R_7 && f != F_A && _file (wr_sq) == f && wr_sq != queening_sq &&
            (square_dist (wk_sq, queening_sq) < square_dist (bk_sq, queening_sq) - 2 + tempo) &&
            (square_dist (wk_sq, queening_sq) < square_dist (bk_sq, wr_sq) + tempo))
        {
            return ScaleFactor (SCALE_FACTOR_MAX - 2 * square_dist (wk_sq, queening_sq));
        }

        // Similar to the above, but with the pawn further back
        if ( f != F_A && _file (wr_sq) == f && wr_sq < wp_sq &&
            square_dist (wk_sq, queening_sq) < square_dist (bk_sq, queening_sq) - 2 + tempo &&
            square_dist (wk_sq, wp_sq + DEL_N) < square_dist (bk_sq, wp_sq + DEL_N) - 2 + tempo &&
            (square_dist (bk_sq, wr_sq) + tempo >= 3 ||
            (square_dist (wk_sq, queening_sq) < square_dist (bk_sq, wr_sq) + tempo &&
            square_dist (wk_sq, wp_sq + DEL_N) < square_dist (bk_sq, wr_sq) + tempo)))
        {
            return ScaleFactor (SCALE_FACTOR_MAX - 8 * square_dist (wp_sq, queening_sq) - 2 * square_dist (wk_sq, queening_sq));
        }

        // If the pawn is not far advanced, and the defending king is somewhere in
        // the pawn's path, it's probably a draw.
        if (r <= R_4 && bk_sq > wp_sq)
        {
            if (_file (bk_sq) == _file (wp_sq)) return ScaleFactor (10);
            if (file_dist (bk_sq, wp_sq) == 1 && square_dist (wk_sq, bk_sq) > 2)
            {
                return ScaleFactor (24 - 2 * square_dist (wk_sq, bk_sq));
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // TODO::
    // KRP vs KB.
    ScaleFactor Endgame<KRPKB>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_ROOK  , 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BISHOP, 0));

        // Test for a rook pawn
        if (pos.pieces (PAWN) & (FA_bb | FH_bb))
        {
            Square bk_sq = pos.king_sq(_weak_side);
            Square bb_sq = pos.piece_list<BSHP> (_weak_side)[0];
            Square wp_sq = pos.piece_list<PAWN> (_stong_side)[0];
            Rank r = rel_rank (_stong_side, wp_sq);
            Delta push = pawn_push (_stong_side);

            // If the pawn is on the 5th rank and the pawn (currently) is on the 
            // same color square as the bishop then there is a chance of a fortress.
            // Depending on the king position give a moderate reduction or a stronger one
            // if the defending king is near the corner but not trapped there.
            if (r == R_5 && !opposite_colors (bb_sq, wp_sq))
            {
                int32_t d = square_dist (wp_sq + 3 * push, bk_sq);

                return (d <= 2 && !(d == 0 && bk_sq == pos.king_sq(_stong_side) + 2 * push))
                    ? ScaleFactor (24) : ScaleFactor (48);
            }

            // When the pawn has moved to the 6th rank we can be fairly sure it's drawn
            // if the bishop attacks the square in front of the pawn from a reasonable distance
            // and the defending king is near the corner
            if (r == R_6 &&
                square_dist (wp_sq + 2 * push, bk_sq) <= 1 &&
                attacks_bb<BSHP> (bb_sq) & (wp_sq + push) &&
                file_dist (bb_sq, wp_sq) >= 2)
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

        Square wp_sq1 = pos.piece_list<PAWN> (_stong_side)[0];
        Square wp_sq2 = pos.piece_list<PAWN> (_stong_side)[1];
        Square bk_sq = pos.king_sq (_weak_side);

        // Does the stronger side have a passed pawn?
        if (pos.passed_pawn (_stong_side, wp_sq1) || pos.passed_pawn (_stong_side, wp_sq2))
        {
            return SCALE_FACTOR_NONE;
        }

        Rank r = max (rel_rank (_stong_side, wp_sq1), rel_rank (_stong_side, wp_sq2));

        if (file_dist (bk_sq, wp_sq1) <= 1 &&
            file_dist (bk_sq, wp_sq2) <= 1 &&
            rel_rank (_stong_side, bk_sq) > r)
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
        ASSERT (pos.piece_count<PAWN> (_stong_side) >= 2);
        ASSERT (verify_material (pos, _weak_side, VALUE_ZERO, 0));

        Square bk_sq = pos.king_sq (_weak_side);
        Bitboard pawns = pos.pieces (_stong_side, PAWN);
        Square wp_sq = pos.piece_list<PAWN> (_stong_side)[0];

        // If all pawns are ahead of the king, all pawns are on a single
        // rook file and the king is within one file of the pawns then draw.
        if (!(pawns & ~front_ranks_bb (_weak_side, _rank (bk_sq))) &&
            !((pawns & FA_bb_) && (pawns & FH_bb_)) &&
            file_dist (bk_sq, wp_sq) <= 1)
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
        Square wk_sq = normalize (pos, _stong_side, pos.king_sq (_stong_side));
        Square bk_sq = normalize (pos, _stong_side, pos.king_sq (_weak_side));
        Square wp_sq = normalize (pos, _stong_side, pos.piece_list<PAWN> (_stong_side)[0]);

        // If the pawn has advanced to the fifth rank or further, and is not a
        // rook pawn, it's too dangerous to assume that it's at least a draw.
        if (_rank (wp_sq) >= R_5 && _file (wp_sq) != F_A) return SCALE_FACTOR_NONE;

        Color c = (_stong_side == pos.active ()) ? WHITE : BLACK;

        // Probe the KPK bitbase with the weakest side's pawn removed. If it's a draw,
        // it's probably at least a draw even with the pawn.
        return BitBases::probe_kpk (c, wk_sq, wp_sq, bk_sq)
            ? SCALE_FACTOR_NONE : SCALE_FACTOR_DRAW;
    }

    template<>
    // KNP vs K. There is a single rule: if the pawn is a rook pawn on the 7th rank
    // and the defending king prevents the pawn from advancing the position is drawn.
    ScaleFactor Endgame<KNPK>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_KNIGHT, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO     , 0));

        // Assume _stong_side is white and the pawn is on files A-D
        Square wp_sq = normalize (pos, _stong_side, pos.piece_list<PAWN> (_stong_side)[0]);
        Square bk_sq = normalize (pos, _stong_side, pos.king_sq (_weak_side));

        if (wp_sq == SQ_A7 && square_dist (SQ_A8, bk_sq) <= 1)
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
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_BISHOP, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BISHOP, 0));

        Square wp_sq = pos.piece_list<PAWN> (_stong_side)[0];
        Square wb_sq = pos.piece_list<BSHP> (_stong_side)[0];
        Square bb_sq = pos.piece_list<BSHP> (_weak_side)[0];
        Square bk_sq = pos.king_sq (_weak_side);

        // Case 1: Defending king blocks the pawn, and cannot be driven away
        if (_file (bk_sq) == _file (wp_sq) &&
            rel_rank (_stong_side, wp_sq) < rel_rank (_stong_side, bk_sq) &&
            (opposite_colors (bk_sq, wb_sq) || rel_rank (_stong_side, bk_sq) <= R_6))
        {
            return SCALE_FACTOR_DRAW;
        }

        // Case 2: Opposite colored bishops
        if (opposite_colors (wb_sq, bb_sq))
        {
            // We assume that the position is drawn in the following three situations:
            //
            //   a. The pawn is on rank 5 or further back.
            //   b. The defending king is somewhere in the pawn's path.
            //   c. The defending bishop attacks some square along the pawn's path,
            //      and is at least three squares away from the pawn.
            //
            // These rules are probably not perfect, but in practice they work reasonably well.

            if (rel_rank (_stong_side, wp_sq) <= R_5)
            {
                return SCALE_FACTOR_DRAW;
            }
            else
            {
                Bitboard path = front_squares_bb (_stong_side, wp_sq);
                if (path & pos.pieces (_weak_side, KING) ||
                    (pos.attacks_from<BSHP> (bb_sq) & path) &&
                    square_dist (bb_sq, wp_sq) >= 3)
                {
                    return SCALE_FACTOR_DRAW;
                }
            }
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KBPP vs KB. It detects a few basic draws with opposite-colored bishops.
    ScaleFactor Endgame<KBPPKB>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_BISHOP, 2));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BISHOP, 0));

        Square wb_sq = pos.piece_list<BSHP> (_stong_side)[0];
        Square bb_sq = pos.piece_list<BSHP> (_weak_side)[0];

        if (!opposite_colors (wb_sq, bb_sq)) return SCALE_FACTOR_NONE;

        Square bk_sq = pos.king_sq (_weak_side);
        Square wp_sq1 = pos.piece_list<PAWN> (_stong_side)[0];
        Square wp_sq2 = pos.piece_list<PAWN> (_stong_side)[1];
        Rank r1 = _rank (wp_sq1);
        Rank r2 = _rank (wp_sq2);
        Square block_sq1, block_sq2;

        if (rel_rank (_stong_side, wp_sq1) > rel_rank (_stong_side, wp_sq2))
        {
            block_sq1 = wp_sq1 + pawn_push (_stong_side);
            block_sq2 = _file (wp_sq2) | _rank (wp_sq1);
        }
        else
        {
            block_sq1 = wp_sq2 + pawn_push (_stong_side);
            block_sq2 = _file (wp_sq1) | _rank (wp_sq2);
        }

        switch (file_dist (wp_sq1, wp_sq2))
        {
        case 0:
            // Both pawns are on the same file. It's an easy draw if the defender firmly
            // controls some square in the frontmost pawn's path.
            if (_file (bk_sq) == _file (block_sq1) &&
                rel_rank (_stong_side, bk_sq) >= rel_rank (_stong_side, block_sq1) &&
                opposite_colors (bk_sq, wb_sq))
            {
                return SCALE_FACTOR_DRAW;
            }
            else
            {
                return SCALE_FACTOR_NONE;
            }
            break;

        case 1:
            // Pawns on adjacent files. It's a draw if the defender firmly controls the
            // square in front of the frontmost pawn's path, and the square diagonally
            // behind this square on the file of the other pawn.
            if (bk_sq == block_sq1 &&
                opposite_colors (bk_sq, wb_sq) &&
                (bb_sq == block_sq2 ||
                (pos.attacks_from<BSHP> (block_sq2) & pos.pieces (_weak_side, BSHP)) ||
                rank_dist (r1, r2) >= 2))
            {
                return SCALE_FACTOR_DRAW;
            }
            else if (bk_sq == block_sq2 &&
                opposite_colors (bk_sq, wb_sq) &&
                (bb_sq == block_sq1 ||
                (pos.attacks_from<BSHP> (block_sq1) & pos.pieces (_weak_side, BSHP))))
            {
                return SCALE_FACTOR_DRAW;
            }
            else
            {
                return SCALE_FACTOR_NONE;
            }
            break;

        default:
            // The pawns are not on the same file or adjacent files. No scaling.
            return SCALE_FACTOR_NONE;
            break;
        }
    }

    template<>
    // KBP vs KN. There is a single rule: If the defending king is somewhere along
    // the path of the pawn, and the square of the king is not of the same color as
    // the stronger side's bishop, it's a draw.
    ScaleFactor Endgame<KBPKN>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_BISHOP, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_KNIGHT, 0));

        Square wp_sq = pos.piece_list<PAWN> (_stong_side)[0];
        Square wb_sq = pos.piece_list<BSHP> (_stong_side)[0];
        Square bk_sq = pos.king_sq (_weak_side);

        if (_file (bk_sq) == _file (wp_sq) &&
            rel_rank (_stong_side, wp_sq) < rel_rank (_stong_side, bk_sq) &&
            (opposite_colors (bk_sq, wb_sq) || rel_rank (_stong_side, bk_sq) <= R_6))
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
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_KNIGHT, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_MG_BISHOP, 0));

        Square wp_sq = pos.piece_list<PAWN> (_stong_side)[0];
        Square wb_sq = pos.piece_list<BSHP> (_weak_side)[0];
        Square bk_sq = pos.king_sq (_weak_side);

        // King needs to get close to promoting pawn to prevent knight from blocking.
        // Rules for this are very tricky, so just approximate.
        if (front_squares_bb (_stong_side, wp_sq) & pos.attacks_from<BSHP> (wb_sq))
        {
            return ScaleFactor (square_dist (bk_sq, wp_sq));
        }

        return SCALE_FACTOR_NONE;
    }


    // --------------------------------------------------------------
    // Generic Scaling functions
    // --------------------------------------------------------------

    template<>
    // KB and one or more pawns vs K.
    // It checks for draws with rook pawns and a bishop of the wrong color.
    // If such a draw is detected, SCALE_FACTOR_DRAW is returned.
    // If not, the return value is SCALE_FACTOR_NONE, i.e. no scaling will be used.
    ScaleFactor Endgame<KBPsKs>::operator() (const Position &pos) const
    {
        ASSERT (pos.non_pawn_material (_stong_side) == VALUE_MG_BISHOP);
        ASSERT (pos.piece_count<BSHP> (_stong_side) == 1);
        ASSERT (pos.piece_count<PAWN> (_stong_side) >= 1);
        // No assertions about the material of _weak_side, because we want draws to
        // be detected even when the weaker side has some materials or pawns.

        Bitboard wpawns = pos.pieces (_stong_side, PAWN);
        Square wp_sq = scan_rel_frntmost_sq (_stong_side, wpawns); //pos.piece_list<PAWN> (_stong_side)[0];
        File wp_f = _file (wp_sq);

        // All pawns are on a single rook file ?
        if ((wp_f == F_A || wp_f == F_H) && !(wpawns & ~file_bb (wp_f)))
        {
            Square wb_sq = pos.piece_list<BSHP> (_stong_side)[0];
            Square queening_sq = rel_sq (_stong_side, wp_f | R_8);
            Square wk_sq = pos.king_sq (_stong_side);
            Square bk_sq = pos.king_sq (_weak_side);

            //// The bishop has the wrong color, and the defending king is on the
            //// file of the pawn(s) or the adjacent file.
            //if (   opposite_colors (queening_sq, wb_sq)
            //    && file_dist (_file (bk_sq), wp_f) <= 1)
            //{
            //    // Find the rank of the frontmost pawn.
            //    Square wp_sq = scan_rel_frntmost_sq (_stong_side, pawns);
            //
            //    // If the defending king has distance 1 to the promotion square or
            //    // is placed somewhere in front of the pawn, it's a draw.
            //    if (   square_dist (bk_sq, queening_sq) <= 1
            //        || rel_rank (_weak_side, bk_sq) <= rel_rank (_weak_side, wp_sq))
            //    {
            //        return SCALE_FACTOR_DRAW;
            //    }
            //}

            // The bishop has the wrong color.
            if (opposite_colors (queening_sq, wb_sq))
            {
                // If the defending king defends the queening square.
                if (square_dist (queening_sq, bk_sq) <= 1)
                {
                    return SCALE_FACTOR_DRAW;
                }

                // If the defending king has some pawns
                Bitboard bpawns = pos.pieces (_weak_side, PAWN);
                if (bpawns && !(bpawns & ~file_bb (wp_f)))
                {
                    Square bp_sq = pos.piece_list<PAWN> (_weak_side)[0];
                    int32_t br = rel_rank (_weak_side, bp_sq);
                    int32_t wr = rel_rank (_weak_side, wp_sq);
                    if (br == wr - 1 &&
                        opposite_colors (bp_sq, wb_sq))
                    {
                        int32_t tempo = (pos.active () == _stong_side);
                        if (square_dist (queening_sq, bk_sq) < 
                            square_dist (bp_sq, wk_sq) + br - tempo)
                        {
                            return SCALE_FACTOR_DRAW;
                        }
                    }
                }

            }

        }

        // All pawns on same B or G file? Then potential draw
        if ((wp_f == F_B || wp_f == F_G) && !(pos.pieces (PAWN) & ~file_bb (wp_f)) &&
            (pos.non_pawn_material (_weak_side) == 0) &&
            (pos.piece_count<PAWN> (_weak_side) >= 1))
        {
            // Get _weak_side pawn that is closest to home rank
            Square bp_sq = scan_rel_backmost_sq (_weak_side, pos.pieces (_weak_side, PAWN));

            Square wk_sq = pos.king_sq (_stong_side);
            Square bk_sq = pos.king_sq (_weak_side);
            Square wb_sq = pos.piece_list<BSHP> (_stong_side)[0];

            //// It's a draw if weaker pawn is on rank 7, bishop can't attack the pawn, and
            //// weaker king can stop opposing opponent's king from penetrating.
            //if (   rel_rank (_stong_side, bp_sq) == R_7
            //    && opposite_colors (wb_sq, bp_sq)
            //    && square_dist (bp_sq, bk_sq) <= square_dist (bp_sq, wk_sq))
            //    return SCALE_FACTOR_DRAW;

            // There's potential for a draw if our pawn is blocked on the 7th rank
            // the bishop cannot attack it or they only have one pawn left
            if ((rel_rank (_stong_side, bp_sq) == R_7) &&
                (pos.pieces (_stong_side, PAWN) & (bp_sq + pawn_push (_weak_side))) &&
                (opposite_colors (wb_sq, bp_sq) || pos.piece_count<PAWN> (_stong_side) == 1))
            {
                int32_t wk_dist = square_dist (bp_sq, wk_sq);
                int32_t bk_dist = square_dist (bp_sq, bk_sq);

                // It's a draw if the weak king is on its back two ranks, within 2
                // squares of the blocking pawn and the strong king is not
                // closer. (I think this rule only fails in practically
                // unreachable positions such as 5k1K/6p1/6P1/8/8/3B4/8/8 w
                // and positions where qsearch will immediately correct the
                // problem such as 8/4k1p1/6P1/1K6/3B4/8/8/8 w)
                if (rel_rank (_stong_side, bk_sq) >= R_7 &&
                    bk_dist <= 2 && bk_dist <= wk_dist)
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
        ASSERT (verify_material (pos, _stong_side, VALUE_MG_QUEEN, 0));
        ASSERT (pos.piece_count<ROOK> (_weak_side) == 1);
        ASSERT (pos.piece_count<PAWN> (_weak_side) >= 1);

        Square bk_sq = pos.king_sq (_weak_side);
        Square br_sq = pos.piece_list<ROOK> (_weak_side)[0];

        if (rel_rank (_weak_side, bk_sq) <= R_2 &&
            rel_rank (_weak_side, pos.king_sq (_stong_side)) >= R_4 &&
            rel_rank (_weak_side, br_sq) == R_3 &&
            ( pos.pieces (_weak_side, PAWN)
            & pos.attacks_from<KING> (bk_sq)
            & pos.attacks_from<PAWN> (_stong_side, br_sq)))
        {
            return SCALE_FACTOR_DRAW;
        }

        return SCALE_FACTOR_NONE;
    }

    template<>
    // KP vs K. This endgame is evaluated with the help of a bitbase.
    Value Endgame<KPK>::operator() (const Position &pos) const
    {
        ASSERT (verify_material (pos, _stong_side, VALUE_ZERO, 1));
        ASSERT (verify_material (pos,  _weak_side, VALUE_ZERO, 0));

        // Assume _stong_side is white and the pawn is on files A-D
        Square wk_sq = normalize (pos, _stong_side, pos.king_sq (_stong_side));
        Square bk_sq = normalize (pos, _stong_side, pos.king_sq (_weak_side));
        Square wp_sq = normalize (pos, _stong_side, pos.piece_list<PAWN> (_stong_side)[0]);

        Color c = (_stong_side == pos.active ()) ? WHITE : BLACK;

        Value value;

        if (!BitBases::probe_kpk (c, wk_sq, wp_sq, bk_sq))
        {
            value = VALUE_DRAW + pos.piece_count<PAWN> (_stong_side);
        }
        else
        {
            value = VALUE_KNOWN_WIN + VALUE_EG_PAWN + Value (_rank (wp_sq));
        }

        return (_stong_side == pos.active ()) ? value : -value;
    }

}
