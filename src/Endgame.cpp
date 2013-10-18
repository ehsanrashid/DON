#include "Endgame.h"
#include <algorithm>
#include <cassert>

#include "BitBoard.h"
#include "BitBases.h"
#include "MoveGenerator.h"

using namespace BitBoard;
using namespace MoveGenerator;

namespace {

    // Table used to drive the king towards the edge of the board
    // in KX vs K and KQ vs KR endgames.
    const int PushToEdges[SQ_NO] =
    {
        100, 90, 80, 70, 70, 80, 90, 100,
        90, 70, 60, 50, 50, 60, 70,  90,
        80, 60, 40, 30, 30, 40, 60,  80,
        70, 50, 30, 20, 20, 30, 50,  70,
        70, 50, 30, 20, 20, 30, 50,  70,
        80, 60, 40, 30, 30, 40, 60,  80,
        90, 70, 60, 50, 50, 60, 70,  90,
        100, 90, 80, 70, 70, 80, 90, 100,
    };

    // Table used to drive the king towards a corner square of the
    // right color in KBN vs K endgames.
    const int PushToCorners[SQ_NO] =
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
    const int PushClose[8] = { 0, 0, 100, 80, 60, 40, 20, 10 };
    const int PushAway [8] = { 0, 5, 20, 40, 60, 80, 90, 100 };

    // Get the material key of a Position out of the given endgame key code
    // like "KBPKN". The trick here is to first forge an ad-hoc fen string
    // and then let a Position object to do the work for us. Note that the
    // fen string could correspond to an illegal position.
    Key key (const std::string &code, Color c)
    {
        ASSERT (code.length() > 0 && code.length() < 8);
        ASSERT (code[0] == 'K');

        std::string sides[] =
        {
            code.substr(code.find('K', 1)),   // Weaker
            code.substr(0, code.find('K', 1)) // Stronger
        };

        std::transform (sides[c].begin (), sides[c].end (), sides[c].begin (), ::tolower);

        std::string fen =  sides[0] + char ('0' + int (8 - code.length()))
            + sides[1] + "/8/8/8/8/8/8/8 w - - 0 10";

        //return Position(fen, false, NULL).matl_key ();
        return Position(fen, false, true).matl_key ();
    }

    template<class M>
    void delete_endgame(const typename M::value_type &p) { delete p.second; }

} // namespace

// Endgames members definitions
Endgames::Endgames()
{
    add<KPK>    ("KPK");
    add<KNNK>   ("KNNK");
    add<KBNK>   ("KBNK");
    add<KRKP>   ("KRKP");
    add<KRKB>   ("KRKB");
    add<KRKN>   ("KRKN");
    add<KQKP>   ("KQKP");
    add<KQKR>   ("KQKR");
    add<KBBKN>  ("KBBKN");

    add<KNPK>   ("KNPK");
    add<KNPKB>  ("KNPKB");
    add<KRPKR>  ("KRPKR");
    add<KBPKB>  ("KBPKB");
    add<KBPKN>  ("KBPKN");
    add<KBPPKB> ("KBPPKB");
    add<KRPPKRP>("KRPPKRP");
}

Endgames::~Endgames()
{
    for_each (m1.begin(), m1.end(), delete_endgame<M1>);
    for_each (m2.begin(), m2.end(), delete_endgame<M2>);
}

template<EndgameType E>
void Endgames::add (const std::string &code)
{
    //map((Endgame<E>*)0)[key (code, WHITE)] = new Endgame<E> (WHITE);
    //map((Endgame<E>*)0)[key (code, BLACK)] = new Endgame<E> (BLACK);
}


/// Mate with KX vs K. This function is used to evaluate positions with
/// King and plenty of material vs a lone king. It simply gives the
/// attacking side a bonus for driving the defending king towards the edge
/// of the board, and for keeping the distance between the two kings small.
template<>
Value Endgame<KXK>::operator()(const Position& pos) const
{
    assert (pos.non_pawn_material(_weak_side) == VALUE_ZERO);
    assert (!pos.piece_count<PAWN>(_weak_side));
    assert (!pos.checkers()); // Eval is never called when in check

    // Stalemate detection with lone king
    if (pos.active () == _weak_side && !generate<LEGAL>(pos).size())
        return VALUE_DRAW;

    Square strong_K_sq = pos.king_sq (_strong_side);
    Square weak_K_sq = pos.king_sq (_weak_side);

    Value value =
        pos.non_pawn_material(_strong_side) +
        pos.pawn_material(_strong_side) +
        PushToEdges[weak_K_sq] +
        PushClose[dist_sq (strong_K_sq, weak_K_sq)];

    if (   pos.piece_count<QUEN>(_strong_side)
        || pos.piece_count<ROOK>(_strong_side)
        || pos.has_pair_bishops(_strong_side))
    {
        value += VALUE_KNOWN_WIN;
    }

    return ((_strong_side == pos.active ())) ? value : -value;
}

/// Mate with KBN vs K. This is similar to KX vs K, but we have to drive the
/// defending king towards a corner square of the right color.
template<>
Value Endgame<KBNK>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_KNIGHT + VALUE_MG_BISHOP);
    assert (pos.non_pawn_material(_weak_side) == VALUE_ZERO);
    assert (pos.piece_count<BSHP>(_strong_side) == 1);
    assert (pos.piece_count<NIHT>(_strong_side) == 1);
    assert (pos.piece_count<PAWN>(_strong_side) == 0);
    assert (pos.piece_count<PAWN>(_weak_side  ) == 0);

    Square strong_K_sq = pos.king_sq (_strong_side);
    Square weak_K_sq = pos.king_sq (_weak_side);
    Square bishopSq = pos.list<BSHP>(_strong_side)[0];
    
    // kbnk_mate_table() tries to drive toward corners A1 or H8,
    // if we have a bishop that cannot reach the above squares we
    // ! the kings so to drive enemy toward corners A8 or H1.
    if (opposite_colors(bishopSq, SQ_A1))
    {
        strong_K_sq = !(strong_K_sq);
        weak_K_sq = !(weak_K_sq);
    }

    Value value =  VALUE_KNOWN_WIN
        + PushClose[dist_sq (strong_K_sq, weak_K_sq)]
    + PushToCorners[weak_K_sq];

    return (_strong_side == pos.active ()) ? value : -value;
}


/// KP vs K. This endgame is evaluated with the help of a bitbase.
template<>
Value Endgame<KPK>::operator()(const Position& pos) const
{
    assert (pos.non_pawn_material(_strong_side) == VALUE_ZERO);
    assert (pos.non_pawn_material(_weak_side) == VALUE_ZERO);
    assert (pos.piece_count<PAWN>(_strong_side) == 1);
    assert (pos.piece_count<PAWN>(_weak_side  ) == 0);

    Square wksq = pos.king_sq (_strong_side);
    Square bksq = pos.king_sq (_weak_side);
    Square psq  = pos.list<PAWN>(_strong_side)[0];
    Color  us   = pos.active ();

    if (_strong_side == BLACK)
    {
        wksq = ~wksq;
        bksq = ~bksq;
        psq  = ~psq;
        us   = ~us;
    }

    if (_file(psq) >= F_E)
    {
        wksq = !(wksq);
        bksq = !(bksq);
        psq  = !(psq);
    }

    //if (!Bitbases::probe_kpk(wksq, psq, bksq, us))
    //    return VALUE_DRAW;

    Value value = VALUE_KNOWN_WIN + VALUE_EG_PAWN + Value(_rank(psq));

    return (_strong_side == pos.active ()) ? value : -value;
}


/// KR vs KP. This is a somewhat tricky endgame to evaluate precisely without
/// a bitbase. The function below returns drawish scores when the pawn is
/// far advanced with support of the king, while the attacking king is far
/// away.
template<>
Value Endgame<KRKP>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_ROOK);
    assert (pos.non_pawn_material(_weak_side) == 0);
    assert (pos.piece_count<PAWN>(_strong_side) == 0);
    assert (pos.piece_count<PAWN>(_weak_side  ) == 1);

    Square wksq = pos.king_sq (_strong_side);
    Square bksq = pos.king_sq (_weak_side);
    Square rsq  = pos.list<ROOK>(_strong_side)[0];
    Square psq  = pos.list<PAWN>(_weak_side)[0];

    if (_strong_side == BLACK)
    {
        wksq = ~wksq;
        bksq = ~bksq;
        rsq  = ~rsq;
        psq  = ~psq;
    }

    Square queeningSq = _file(psq) | R_1;
    Value value;

    // If the stronger side's king is in front of the pawn, it's a win
    if (wksq < psq && _file(wksq) == _file(psq))
        value = VALUE_EG_ROOK - Value(dist_sq (wksq, psq));

    // If the weaker side's king is too far from the pawn and the rook,
    // it's a win.
    else if (   dist_sq (bksq, psq) >= 3 + (pos.active () == _weak_side)
        && dist_sq (bksq, rsq) >= 3)
        value = VALUE_EG_ROOK - Value(dist_sq (wksq, psq));

    // If the pawn is far advanced and supported by the defending king,
    // the position is drawish
    else if (   _rank(bksq) <= R_3
        && dist_sq (bksq, psq) == 1
        && _rank(wksq) >= R_4
        && dist_sq (wksq, psq) > 2 + (pos.active () == _strong_side))
        value = Value(80 - dist_sq (wksq, psq) * 8);

    else
        value =  Value(200)
        - Value(dist_sq (wksq, psq + DEL_S) * 8)
        + Value(dist_sq (bksq, psq + DEL_S) * 8)
        + Value(dist_sq (psq, queeningSq) * 8);

    return (_strong_side == pos.active ()) ? value : -value;
}


/// KR vs KB. This is very simple, and always returns drawish scores.  The
/// score is slightly bigger when the defending king is close to the edge.
template<>
Value Endgame<KRKB>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_ROOK);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_MG_BISHOP);
    assert (pos.piece_count<BSHP>(_weak_side  ) == 1);
    assert (pos.piece_count<  PAWN>(_weak_side  ) == 0);
    assert (pos.piece_count<  PAWN>(_strong_side) == 0);

    Value value = Value(PushToEdges[pos.king_sq (_weak_side)]);
    return (_strong_side == pos.active ()) ? value : -value;
}


/// KR vs KN.  The attacking side has slightly better winning chances than
/// in KR vs KB, particularly if the king and the knight are far apart.
template<>
Value Endgame<KRKN>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_ROOK);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_MG_KNIGHT);
    assert (pos.piece_count<NIHT>(_weak_side  ) == 1);
    assert (pos.piece_count<  PAWN>(_weak_side  ) == 0);
    assert (pos.piece_count<  PAWN>(_strong_side) == 0);

    Square bksq = pos.king_sq (_weak_side);
    Square bnsq = pos.list<NIHT>(_weak_side)[0];
    Value value = Value(PushToEdges[bksq] + PushAway[dist_sq (bksq, bnsq)]);
    return (_strong_side == pos.active ()) ? value : -value;
}


/// KQ vs KP.  In general, a win for the stronger side, however, there are a few
/// important exceptions.  Pawn on 7th rank, A,C,F or H file, with king next can
/// be a draw, so we scale down to distance between kings only.
template<>
Value Endgame<KQKP>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_QUEEN);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_ZERO);
    assert (pos.piece_count<PAWN>(_strong_side) == 0);
    assert (pos.piece_count<PAWN>(_weak_side  ) == 1);

    Square strong_K_sq = pos.king_sq (_strong_side);
    Square weak_K_sq = pos.king_sq (_weak_side);
    Square pawnSq = pos.list<PAWN>(_weak_side)[0];

    Value value = Value(PushClose[dist_sq (strong_K_sq, weak_K_sq)]);

    if (   rel_rank (_weak_side, pawnSq) != R_7
        || dist_sq (weak_K_sq, pawnSq) != 1
        || !((bb_FA | bb_FC | bb_FF | bb_FH) & pawnSq))
        value += VALUE_EG_QUEEN - VALUE_EG_PAWN;

    return (_strong_side == pos.active ()) ? value : -value;
}


/// KQ vs KR.  This is almost identical to KX vs K:  We give the attacking
/// king a bonus for having the kings close together, and for forcing the
/// defending king towards the edge.  If we also take care to avoid null move
/// for the defending side in the search, this is usually sufficient to be
/// able to win KQ vs KR.
template<>
Value Endgame<KQKR>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_QUEEN);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_MG_ROOK);
    assert (pos.piece_count<PAWN>(_strong_side) == 0);
    assert (pos.piece_count<PAWN>(_weak_side  ) == 0);

    Square strong_K_sq = pos.king_sq (_strong_side);
    Square weak_K_sq = pos.king_sq (_weak_side);

    Value value =  VALUE_EG_QUEEN - VALUE_EG_ROOK +
        PushToEdges[weak_K_sq] +
        PushClose[dist_sq (strong_K_sq, weak_K_sq)];

    return (_strong_side == pos.active ()) ? value : -value;
}


/// KBB vs KN. This is almost always a win. We try to push enemy king to a corner
/// and away from his knight. For a reference of this difficult endgame see:
/// en.wikipedia.org/wiki/Chess_endgame#Effect_of_tablebases_on_endgame_theory

template<>
Value Endgame<KBBKN>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == 2 * VALUE_MG_BISHOP);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_MG_KNIGHT);
    assert (pos.piece_count<BSHP>(_strong_side) == 2);
    assert (pos.piece_count<NIHT>(_weak_side  ) == 1);
    assert (!pos.pieces(PAWN));

    Square strong_K_sq = pos.king_sq (_strong_side);
    Square weak_K_sq = pos.king_sq (_weak_side);
    Square knightSq = pos.list<NIHT>(_weak_side)[0];

    Value value =  VALUE_KNOWN_WIN
        + PushToCorners[weak_K_sq]
    + PushClose[dist_sq (strong_K_sq, weak_K_sq)]
    + PushAway[dist_sq (weak_K_sq, knightSq)];

    return (_strong_side == pos.active ()) ? value : -value;
}


/// Some cases of trivial draws
template<> Value Endgame<KNNK>::operator()(const Position&) const { return VALUE_DRAW; }
template<> Value Endgame<KmmKm>::operator()(const Position&) const { return VALUE_DRAW; }


/// K, bishop and one or more pawns vs K. It checks for draws with rook pawns and
/// a bishop of the wrong color. If such a draw is detected, SCALE_FACTOR_DRAW
/// is returned. If not, the return value is SCALE_FACTOR_NONE, i.e. no scaling
/// will be used.
template<>
ScaleFactor Endgame<KBPsK>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_BISHOP);
    assert (pos.piece_count<BSHP>(_strong_side) == 1);
    assert (pos.piece_count<  PAWN>(_strong_side) >= 1);

    // No assertions about the material of _weak_side, because we want draws to
    // be detected even when the weaker side has some pawns.

    Bitboard pawns = pos.pieces(_strong_side, PAWN);
    File pawnFile = _file(pos.list<PAWN>(_strong_side)[0]);

    // All pawns are on a single rook file ?
    if (    (pawnFile == F_A || pawnFile == F_H)
        && !(pawns & ~mask_file (pawnFile)))
    {
        Square bishopSq = pos.list<BSHP>(_strong_side)[0];
        Square queeningSq = rel_sq (_strong_side, pawnFile | R_8);
        Square kingSq = pos.king_sq (_weak_side);
        
        if (   opposite_colors(queeningSq, bishopSq)
            && abs (int32_t (_file(kingSq)) - int32_t (pawnFile)) <= 1)
        {
            // The bishop has the wrong color, and the defending king is on the
            // file of the pawn(s) or the adjacent file. Find the rank of the
            // frontmost pawn.
            Square pawnSq = frontmost_rel_sq (_strong_side, pawns);

            // If the defending king has distance 1 to the promotion square or
            // is placed somewhere in front of the pawn, it's a draw.
            if (   dist_sq (kingSq, queeningSq) <= 1
                || rel_rank (_weak_side, kingSq) <= rel_rank (_weak_side, pawnSq))
                return SCALE_FACTOR_DRAW;
        }
    }

    // All pawns on same B or G file? Then potential draw
    if (    (pawnFile == F_B || pawnFile == F_G)
        && !(pos.pieces(PAWN) & ~mask_file (pawnFile))
        && pos.non_pawn_material(_weak_side) == 0
        && pos.piece_count<PAWN>(_weak_side) >= 1)
    {
        // Get _weak_side pawn that is closest to home rank
        Square weakerPawnSq = backmost_rel_sq (_weak_side, pos.pieces(_weak_side, PAWN));

        Square strongerKingSq = pos.king_sq (_strong_side);
        Square weakerKingSq = pos.king_sq (_weak_side);
        Square bishopSq = pos.list<BSHP>(_strong_side)[0];

        // Draw if weaker pawn is on rank 7, bishop can't attack the pawn, and
        // weaker king can stop opposing opponent's king from penetrating.
        if (   rel_rank (_strong_side, weakerPawnSq) == R_7
            && opposite_colors(bishopSq, weakerPawnSq)
            && dist_sq (weakerPawnSq, weakerKingSq) <= dist_sq (weakerPawnSq, strongerKingSq))
            return SCALE_FACTOR_DRAW;
    }

    return SCALE_FACTOR_NONE;
}


/// K and queen vs K, rook and one or more pawns. It tests for fortress draws with
/// a rook on the third rank defended by a pawn.
template<>
ScaleFactor Endgame<KQKRPs>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_QUEEN);
    assert (pos.piece_count<QUEN>(_strong_side) == 1);
    assert (pos.piece_count<PAWN>(_strong_side) == 0);
    assert (pos.piece_count<ROOK>(_weak_side  ) == 1);
    assert (pos.piece_count<PAWN>(_weak_side  ) >= 1);

    Square kingSq = pos.king_sq (_weak_side);
    Square rsq = pos.list<ROOK>(_weak_side)[0];

    if (    rel_rank (_weak_side, kingSq) <= R_2
        &&  rel_rank (_weak_side, pos.king_sq (_strong_side)) >= R_4
        && (pos.pieces(_weak_side, ROOK) & mask_rank (rel_rank (_weak_side, R_3)))
        && (pos.pieces(_weak_side, PAWN) & mask_rank (rel_rank (_weak_side, R_2)))
        && (pos.attacks_from<KING>(kingSq) & pos.pieces(_weak_side, PAWN))
        && (pos.attacks_from<PAWN>(_strong_side, rsq) & pos.pieces(_weak_side, PAWN)))
        return SCALE_FACTOR_DRAW;

    return SCALE_FACTOR_NONE;
}


/// K, rook and one pawn vs K and a rook. This function knows a handful of the
/// most important classes of drawn positions, but is far from perfect. It would
/// probably be a good idea to add more knowledge in the future.
///
/// It would also be nice to rewrite the actual code for this function,
/// which is mostly copied from Glaurung 1.x, and not very pretty.
template<>
ScaleFactor Endgame<KRPKR>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_ROOK);
    assert (pos.non_pawn_material(_weak_side)   == VALUE_MG_ROOK);
    assert (pos.piece_count<PAWN>(_strong_side) == 1);
    assert (pos.piece_count<PAWN>(_weak_side  ) == 0);

    Square wksq = pos.king_sq (_strong_side);
    Square bksq = pos.king_sq (_weak_side);
    Square wrsq = pos.list<ROOK>(_strong_side)[0];
    Square wpsq = pos.list<PAWN>(_strong_side)[0];
    Square brsq = pos.list<ROOK>(_weak_side)[0];

    // Orient the board in such a way that the stronger side is white, and the
    // pawn is on the left half of the board.
    if (_strong_side == BLACK)
    {
        wksq = ~wksq;
        wrsq = ~wrsq;
        wpsq = ~wpsq;
        bksq = ~bksq;
        brsq = ~brsq;
    }

    if (_file(wpsq) > F_D)
    {
        wksq = !(wksq);
        wrsq = !(wrsq);
        wpsq = !(wpsq);
        bksq = !(bksq);
        brsq = !(brsq);
    }

    File f = _file(wpsq);
    Rank r = _rank(wpsq);
    Square queeningSq = f | R_8;
    int tempo = (pos.active () == _strong_side);

    // If the pawn is not too far advanced and the defending king defends the
    // queening square, use the third-rank defence.
    if (   r <= R_5
        && dist_sq (bksq, queeningSq) <= 1
        && wksq <= SQ_H5
        && (_rank(brsq) == R_6 || (r <= R_3 && _rank(wrsq) != R_6)))
        return SCALE_FACTOR_DRAW;

    // The defending side saves a draw by checking from behind in case the pawn
    // has advanced to the 6th rank with the king behind.
    if (   r == R_6
        && dist_sq (bksq, queeningSq) <= 1
        && _rank(wksq) + tempo <= R_6
        && (_rank(brsq) == R_1 || (!tempo && abs(int32_t (_file(brsq)) - int32_t (f)) >= 3)))
        return SCALE_FACTOR_DRAW;

    if (   r >= R_6
        && bksq == queeningSq
        && _rank(brsq) == R_1
        && (!tempo || dist_sq (wksq, wpsq) >= 2))
        return SCALE_FACTOR_DRAW;

    // White pawn on a7 and rook on a8 is a draw if black's king is on g7 or h7
    // and the black rook is behind the pawn.
    if (   wpsq == SQ_A7
        && wrsq == SQ_A8
        && (bksq == SQ_H7 || bksq == SQ_G7)
        && _file(brsq) == F_A
        && (_rank(brsq) <= R_3 || _file(wksq) >= F_D || _rank(wksq) <= R_5))
        return SCALE_FACTOR_DRAW;

    // If the defending king blocks the pawn and the attacking king is too far
    // away, it's a draw.
    if (   r <= R_5
        && bksq == wpsq + DEL_N
        && dist_sq (wksq, wpsq) - tempo >= 2
        && dist_sq (wksq, brsq) - tempo >= 2)
        return SCALE_FACTOR_DRAW;

    // Pawn on the 7th rank supported by the rook from behind usually wins if the
    // attacking king is closer to the queening square than the defending king,
    // and the defending king cannot gain tempi by threatening the attacking rook.
    if (   r == R_7
        && f != F_A
        && _file(wrsq) == f
        && wrsq != queeningSq
        && (dist_sq (wksq, queeningSq) < dist_sq (bksq, queeningSq) - 2 + tempo)
        && (dist_sq (wksq, queeningSq) < dist_sq (bksq, wrsq) + tempo))
        return ScaleFactor(SCALE_FACTOR_MAX - 2 * dist_sq (wksq, queeningSq));

    // Similar to the above, but with the pawn further back
    if (   f != F_A
        && _file(wrsq) == f
        && wrsq < wpsq
        && (dist_sq (wksq, queeningSq) < dist_sq (bksq, queeningSq) - 2 + tempo)
        && (dist_sq (wksq, wpsq + DEL_N) < dist_sq (bksq, wpsq + DEL_N) - 2 + tempo)
        && (  dist_sq (bksq, wrsq) + tempo >= 3
        || (    dist_sq (wksq, queeningSq) < dist_sq (bksq, wrsq) + tempo
        && (dist_sq (wksq, wpsq + DEL_N) < dist_sq (bksq, wrsq) + tempo))))
        return ScaleFactor(  SCALE_FACTOR_MAX
        - 8 * dist_sq (wpsq, queeningSq)
        - 2 * dist_sq (wksq, queeningSq));

    // If the pawn is not far advanced, and the defending king is somewhere in
    // the pawn's path, it's probably a draw.
    if (r <= R_4 && bksq > wpsq)
    {
        if (_file(bksq) == _file(wpsq))
            return ScaleFactor(10);
        if (   abs(int32_t (_file(bksq)) - int32_t (_file(wpsq))) == 1
            && dist_sq (wksq, bksq) > 2)
            return ScaleFactor(24 - 2 * dist_sq (wksq, bksq));
    }
    return SCALE_FACTOR_NONE;
}


/// K, rook and two pawns vs K, rook and one pawn. There is only a single
/// pattern: If the stronger side has no passed pawns and the defending king
/// is actively placed, the position is drawish.
template<>
ScaleFactor Endgame<KRPPKRP>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_ROOK);
    assert (pos.non_pawn_material(_weak_side)   == VALUE_MG_ROOK);
    assert (pos.piece_count<PAWN>(_strong_side) == 2);
    assert (pos.piece_count<PAWN>(_weak_side  ) == 1);

    Square wpsq1 = pos.list<PAWN>(_strong_side)[0];
    Square wpsq2 = pos.list<PAWN>(_strong_side)[1];
    Square bksq = pos.king_sq (_weak_side);

    // Does the stronger side have a passed pawn?
    if (pos.passed_pawn (_strong_side, wpsq1) || pos.passed_pawn (_strong_side, wpsq2))
        return SCALE_FACTOR_NONE;

    Rank r = std::max(rel_rank (_strong_side, wpsq1), rel_rank (_strong_side, wpsq2));

    if (   dist_file (bksq, wpsq1) <= 1
        && dist_file (bksq, wpsq2) <= 1
        && rel_rank (_strong_side, bksq) > r)
    {
        switch (r) {
        case R_2: return ScaleFactor(10);
        case R_3: return ScaleFactor(10);
        case R_4: return ScaleFactor(15);
        case R_5: return ScaleFactor(20);
        case R_6: return ScaleFactor(40);
        default: assert (false);
        }
    }
    return SCALE_FACTOR_NONE;
}


/// K and two or more pawns vs K. There is just a single rule here: If all pawns
/// are on the same rook file and are blocked by the defending king, it's a draw.
template<>
ScaleFactor Endgame<KPsK>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_ZERO);
    assert (pos.non_pawn_material(_weak_side)   == VALUE_ZERO);
    assert (pos.piece_count<PAWN>(_strong_side) >= 2);
    assert (pos.piece_count<PAWN>(_weak_side  ) == 0);

    Square ksq = pos.king_sq (_weak_side);
    Bitboard pawns = pos.pieces(_strong_side, PAWN);

    // Are all pawns on the 'a' file?
    if (!(pawns & ~bb_FA))
    {
        // Does the defending king block the pawns?
        if (   dist_sq (ksq, rel_sq (_strong_side, SQ_A8)) <= 1
            || (    _file(ksq) == F_A
            && !(mask_front_ranks (_strong_side, _rank(ksq)) & pawns)))
            return SCALE_FACTOR_DRAW;
    }
    // Are all pawns on the 'h' file?
    else if (!(pawns & ~bb_FH))
    {
        // Does the defending king block the pawns?
        if (   dist_sq (ksq, rel_sq (_strong_side, SQ_H8)) <= 1
            || (    _file(ksq) == F_H
            && !(mask_front_ranks (_strong_side, _rank(ksq)) & pawns)))
            return SCALE_FACTOR_DRAW;
    }
    return SCALE_FACTOR_NONE;
}


/// K, bishop and a pawn vs K and a bishop. There are two rules: If the defending
/// king is somewhere along the path of the pawn, and the square of the king is
/// not of the same color as the stronger side's bishop, it's a draw. If the two
/// bishops have opposite color, it's almost always a draw.
template<>
ScaleFactor Endgame<KBPKB>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_BISHOP);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_MG_BISHOP);
    assert (pos.piece_count<BSHP>(_strong_side) == 1);
    assert (pos.piece_count<BSHP>(_weak_side  ) == 1);
    assert (pos.piece_count<  PAWN>(_strong_side) == 1);
    assert (pos.piece_count<  PAWN>(_weak_side  ) == 0);

    Square pawnSq = pos.list<PAWN>(_strong_side)[0];
    Square strongerBishopSq = pos.list<BSHP>(_strong_side)[0];
    Square weakerBishopSq = pos.list<BSHP>(_weak_side)[0];
    Square weakerKingSq = pos.king_sq (_weak_side);

    // Case 1: Defending king blocks the pawn, and cannot be driven away
    if (   _file(weakerKingSq) == _file(pawnSq)
        && rel_rank (_strong_side, pawnSq) < rel_rank (_strong_side, weakerKingSq)
        && (   opposite_colors(weakerKingSq, strongerBishopSq)
        || rel_rank (_strong_side, weakerKingSq) <= R_6))
        return SCALE_FACTOR_DRAW;

    // Case 2: Opposite colored bishops
    if (opposite_colors(strongerBishopSq, weakerBishopSq))
    {
        // We assume that the position is drawn in the following three situations:
        //
        //   a. The pawn is on rank 5 or further back.
        //   b. The defending king is somewhere in the pawn's path.
        //   c. The defending bishop attacks some square along the pawn's path,
        //      and is at least three squares away from the pawn.
        //
        // These rules are probably not perfect, but in practice they work
        // reasonably well.

        if (rel_rank (_strong_side, pawnSq) <= R_5)
            return SCALE_FACTOR_DRAW;
        else
        {
            Bitboard path = mask_front_sq (_strong_side, pawnSq);

            if (path & pos.pieces(_weak_side, KING))
                return SCALE_FACTOR_DRAW;

            if (  (pos.attacks_from<BSHP>(weakerBishopSq) & path)
                && dist_sq (weakerBishopSq, pawnSq) >= 3)
                return SCALE_FACTOR_DRAW;
        }
    }
    return SCALE_FACTOR_NONE;
}


/// K, bishop and two pawns vs K and bishop. It detects a few basic draws with
/// opposite-colored bishops.
template<>
ScaleFactor Endgame<KBPPKB>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_BISHOP);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_MG_BISHOP);
    assert (pos.piece_count<BSHP>(_strong_side) == 1);
    assert (pos.piece_count<BSHP>(_weak_side  ) == 1);
    assert (pos.piece_count<  PAWN>(_strong_side) == 2);
    assert (pos.piece_count<  PAWN>(_weak_side  ) == 0);

    Square wbsq = pos.list<BSHP>(_strong_side)[0];
    Square bbsq = pos.list<BSHP>(_weak_side)[0];

    if (!opposite_colors(wbsq, bbsq))
        return SCALE_FACTOR_NONE;

    Square ksq = pos.king_sq (_weak_side);
    Square psq1 = pos.list<PAWN>(_strong_side)[0];
    Square psq2 = pos.list<PAWN>(_strong_side)[1];
    Rank r1 = _rank(psq1);
    Rank r2 = _rank(psq2);
    Square blockSq1, blockSq2;

    if (rel_rank (_strong_side, psq1) > rel_rank (_strong_side, psq2))
    {
        blockSq1 = psq1 + pawn_push(_strong_side);
        blockSq2 = _file(psq2) | _rank(psq1);
    }
    else
    {
        blockSq1 = psq2 + pawn_push(_strong_side);
        blockSq2 = _file(psq1) | _rank(psq2);
    }

    switch (dist_file (psq1, psq2))
    {
    case 0:
        // Both pawns are on the same file. Easy draw if defender firmly controls
        // some square in the frontmost pawn's path.
        if (   _file(ksq) == _file(blockSq1)
            && rel_rank (_strong_side, ksq) >= rel_rank (_strong_side, blockSq1)
            && opposite_colors(ksq, wbsq))
            return SCALE_FACTOR_DRAW;
        else
            return SCALE_FACTOR_NONE;

    case 1:
        // Pawns on adjacent files. Draw if defender firmly controls the square
        // in front of the frontmost pawn's path, and the square diagonally behind
        // this square on the file of the other pawn.
        if (   ksq == blockSq1
            && opposite_colors(ksq, wbsq)
            && (   bbsq == blockSq2
            || (pos.attacks_from<BSHP>(blockSq2) & pos.pieces(_weak_side, BSHP))
            || abs(int32_t (r1) - int32_t (r2)) >= 2))
            return SCALE_FACTOR_DRAW;

        else if (   ksq == blockSq2
            && opposite_colors(ksq, wbsq)
            && (   bbsq == blockSq1
            || (pos.attacks_from<BSHP>(blockSq1) & pos.pieces(_weak_side, BSHP))))
            return SCALE_FACTOR_DRAW;
        else
            return SCALE_FACTOR_NONE;

    default:
        // The pawns are not on the same file or adjacent files. No scaling.
        return SCALE_FACTOR_NONE;
    }
}


/// K, bisop and a pawn vs K and knight. There is a single rule: If the defending
/// king is somewhere along the path of the pawn, and the square of the king is
/// not of the same color as the stronger side's bishop, it's a draw.
template<>
ScaleFactor Endgame<KBPKN>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_BISHOP);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_MG_KNIGHT);
    assert (pos.piece_count<BSHP>(_strong_side) == 1);
    assert (pos.piece_count<NIHT>(_weak_side  ) == 1);
    assert (pos.piece_count<  PAWN>(_strong_side) == 1);
    assert (pos.piece_count<  PAWN>(_weak_side  ) == 0);

    Square pawnSq = pos.list<PAWN>(_strong_side)[0];
    Square strongerBishopSq = pos.list<BSHP>(_strong_side)[0];
    Square weakerKingSq = pos.king_sq (_weak_side);

    if (   _file(weakerKingSq) == _file(pawnSq)
        && rel_rank (_strong_side, pawnSq) < rel_rank (_strong_side, weakerKingSq)
        && (   opposite_colors(weakerKingSq, strongerBishopSq)
        || rel_rank (_strong_side, weakerKingSq) <= R_6))
        return SCALE_FACTOR_DRAW;

    return SCALE_FACTOR_NONE;
}


/// K, knight and a pawn vs K. There is a single rule: If the pawn is a rook pawn
/// on the 7th rank and the defending king prevents the pawn from advancing, the
/// position is drawn.
template<>
ScaleFactor Endgame<KNPK>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_MG_KNIGHT);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_ZERO);
    assert (pos.piece_count<NIHT>(_strong_side) == 1);
    assert (pos.piece_count<  PAWN>(_strong_side) == 1);
    assert (pos.piece_count<  PAWN>(_weak_side  ) == 0);

    Square pawnSq = pos.list<PAWN>(_strong_side)[0];
    Square weakerKingSq = pos.king_sq (_weak_side);

    if (   pawnSq == rel_sq (_strong_side, SQ_A7)
        && dist_sq (weakerKingSq, rel_sq (_strong_side, SQ_A8)) <= 1)
        return SCALE_FACTOR_DRAW;

    if (   pawnSq == rel_sq (_strong_side, SQ_H7)
        && dist_sq (weakerKingSq, rel_sq (_strong_side, SQ_H8)) <= 1)
        return SCALE_FACTOR_DRAW;

    return SCALE_FACTOR_NONE;
}


/// K, knight and a pawn vs K and bishop. If knight can block bishop from taking
/// pawn, it's a win. Otherwise, drawn.
template<>
ScaleFactor Endgame<KNPKB>::operator()(const Position& pos) const
{

    Square pawnSq = pos.list<PAWN>(_strong_side)[0];
    Square bishopSq = pos.list<BSHP>(_weak_side)[0];
    Square weakerKingSq = pos.king_sq (_weak_side);

    // King needs to get close to promoting pawn to prevent knight from blocking.
    // Rules for this are very tricky, so just approximate.
    if (mask_front_sq (_strong_side, pawnSq) & pos.attacks_from<BSHP>(bishopSq))
        return ScaleFactor(dist_sq (weakerKingSq, pawnSq));

    return SCALE_FACTOR_NONE;
}


/// K and a pawn vs K and a pawn. This is done by removing the weakest side's
/// pawn and probing the KP vs K bitbase: If the weakest side has a draw without
/// the pawn, she probably has at least a draw with the pawn as well. The exception
/// is when the stronger side's pawn is far advanced and not on a rook file; in
/// this case it is often possible to win (e.g. 8/4k3/3p4/3P4/6K1/8/8/8 w - - 0 1).
template<>
ScaleFactor Endgame<KPKP>::operator()(const Position& pos) const
{

    assert (pos.non_pawn_material(_strong_side) == VALUE_ZERO);
    assert (pos.non_pawn_material(_weak_side  ) == VALUE_ZERO);
    assert (pos.piece_count<PAWN>(WHITE) == 1);
    assert (pos.piece_count<PAWN>(BLACK) == 1);

    Square wksq = pos.king_sq (_strong_side);
    Square bksq = pos.king_sq (_weak_side);
    Square psq  = pos.list<PAWN>(_strong_side)[0];
    Color  us   = pos.active ();

    if (_strong_side == BLACK)
    {
        wksq = ~wksq;
        bksq = ~bksq;
        psq  = ~psq;
        us   = ~us;
    }

    if (_file(psq) >= F_E)
    {
        wksq = !(wksq);
        bksq = !(bksq);
        psq  = !(psq);
    }

    // If the pawn has advanced to the fifth rank or further, and is not a
    // rook pawn, it's too dangerous to assume that it's at least a draw.
    if (_rank(psq) >= R_5 && _file(psq) != F_A)
        return SCALE_FACTOR_NONE;

    // Probe the KPK bitbase with the weakest side's pawn removed. If it's a draw,
    // it's probably at least a draw even with the pawn.
    return Bitbases::probe_kpk(us, wksq, psq, bksq) ? SCALE_FACTOR_NONE : SCALE_FACTOR_DRAW;
}
