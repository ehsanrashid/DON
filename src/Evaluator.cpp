#include "Evaluator.h"
#include "BitCount.h"
#include "Position.h"
#include "MoveGenerator.h"

using namespace std;
using namespace BitBoard;


static const int16_t _PieceSquareTable    [PT_NO][SQ_NO] =
{
    //// PAWN
    //0,    0,    0,    0,    0,    0,    0,    0,
    //0,    0,    0,    0,    0,    0,    0,    0,
    //0,    0,    0,    0,    0,    0,    0,    0,
    //0,    0,    0,   75,   75,    0,    0,    0,
    //0,    0,   15,   75,   75,   15,    0,    0,
    //0,    0,    0,   40,   40,    0,    0,    0,
    //0,    0,    0, -100, -100,    0,    0,    0,
    //0,    0,    0,    0,    0,    0,    0,    0,

    //// KNIGHT
    //-150, -120, -120, -120, -120, -120, -120, -150,
    //-120,   25,   25,   25,   25,   25,   25, -120,
    //-120,   25,   50,   50,   50,   50,   25, -120,
    //-120,   25,   50,  100,  100,   50,   25, -120,
    //-120,   25,   50,  100,  100,   50,   25, -120,
    //-120,   25,   50,   50,   50,   50,   25, -120,
    //-120,   25,   25,   25,   25,   25,   25, -120,
    //-150, -120, -120, -120, -120, -120, -120, -150,

    //// BISHOP
    //-40,  -40,  -40,  -40,  -40,  -40,  -40,  -40,
    //-40,   20,   20,   20,   20,   20,   20,  -40,
    //-40,   20,   30,   30,   30,   30,   20,  -40,
    //-40,   20,   30,   45,   45,   30,   20,  -40,
    //-40,   20,   30,   45,   45,   30,   20,  -40,
    //-40,   20,   30,   30,   30,   30,   20,  -40,
    //-40,   20,   20,   20,   20,   20,   20,  -40,
    //-40,  -40,  -40,  -40,  -40,  -40,  -40,  -40,

    //// ROOK
    //0,    0,   10,   15,   15,   10,    0,    0,
    //0,    0,   10,   15,   15,   10,    0,    0,
    //0,    0,   10,   15,   15,   10,    0,    0,
    //0,    0,   10,   15,   15,   10,    0,    0,
    //0,    0,   10,   15,   15,   10,    0,    0,
    //0,    0,   10,   15,   15,   10,    0,    0,
    //0,    0,   10,   15,   15,   10,    0,    0,
    //0,    0,   10,   15,   15,   10,    0,    0,

    //// QUEEN
    //0,    0,    0,    0,    0,    0,    0,    0,
    //0,    0,    0,    0,    0,    0,    0,    0,
    //0,    0,   75,   75,   75,   75,    0,    0,
    //0,    0,   75,  100,  100,   75,    0,    0,
    //0,    0,   75,  100,  100,   75,    0,    0,
    //0,    0,   75,   75,   75,   75,    0,    0,
    //0,    0,    0,    0,    0,    0,    0,    0,
    //0,    0,    0,    0,    0,    0,    0,    0,

    //// KING
    //-900, -900, -900, -900, -900, -900, -900, -900,
    //-900, -900, -900, -900, -900, -900, -900, -900,
    //-900, -900, -900, -900, -900, -900, -900, -900,
    //-900, -900, -900, -900, -900, -900, -900, -900,
    //-900, -900, -900, -900, -900, -900, -900, -900,
    //-700, -700, -700, -700, -700, -700, -700, -700,
    //-200, -200, -500, -500, -500, -500, -200, -200,
    //+200, +300, +300, -300, -300, +100, +400, +200,

    // ---

    // PAWN
    + 0,    0,    0,    0,    0,    0,    0,    0,
    +20,   26,   26,   28,   28,   26,   26,   20,
    +12,   14,   16,   21,   21,   16,   14,   12,
    + 8,   10,   12,   18,   18,   12,   10,    8,
    + 4,    6,    8,   16,   16,    8,    6,    4,
    + 2,    2,    4,    6,    6,    4,    2,    2,
    + 0,    0,    0,   -4,   -4,    0,    0,    0,
    + 0,    0,    0,    0,    0,    0,    0,    0,

    // KNIGHT
    -40,  -10,  - 5,  - 5,  - 5,  - 5,  -10,  -40,
    - 5,    5,    5,    5,    5,    5,    5,  - 5,
    - 5,    5,   10,   15,   15,   10,    5,  - 5,
    - 5,    5,   10,   15,   15,   10,    5,  - 5,
    - 5,    5,   10,   15,   15,   10,    5,  - 5,
    - 5,    5,    8,    8,    8,    8,    5,  - 5,
    - 5,    0,    5,    5,    5,    5,    0,  - 5,
    -50,  -20,  -10,  -10,  -10,  -10,  -20,  -50,

    // BISHOP
    -40,  -20,  -15,  -15,  -15,  -15,  -20,  -40,
    + 0,    5,    5,    5,    5,    5,    5,    0,
    + 0,   10,   10,   18,   18,   10,   10,    0,
    + 0,   10,   10,   18,   18,   10,   10,    0,
    + 0,    5,   10,   18,   18,   10,    5,    0,
    + 0,    0,    5,    5,    5,    5,    0,    0,
    + 0,    5,    0,    0,    0,    0,    5,    0,
    -50,  -20,  -10,  -20,  -20,  -10,  -20,  -50,

    // ROOK
    +10,   10,   10,   10,   10,   10,   10,   10,
    + 5,    5,    5,   10,   10,    5,    5,    5,
    + 0,    0,    5,   10,   10,    5,    0,    0,
    + 0,    0,    5,   10,   10,    5,    0,    0,
    + 0,    0,    5,   10,   10,    5,    0,    0,
    + 0,    0,    5,   10,   10,    5,    0,    0,
    + 0,    0,    5,   10,   10,    5,    0,    0,
    + 0,    0,    5,   10,   10,    5,    0,    0,

    // QUEEN
    + 0,    0,    0,    0,    0,    0,    0,    0,
    + 0,    0,    0,    0,    0,    0,    0,    0,
    + 0,    0,   10,   10,   10,   10,    0,    0,
    + 0,    0,   10,   15,   15,   10,    0,    0,
    + 0,    0,   10,   15,   15,   10,    0,    0,
    + 0,    0,   10,   10,   10,   10,    0,    0,
    + 0,    0,    0,    0,    0,    0,    0,    0,
    + 0,    0,    0,    0,    0,    0,    0,    0,

    // KING
    + 0,    0,    0,    0,    0,    0,    0,    0,
    + 0,    0,    0,    0,    0,    0,    0,    0,
    + 0,    0,    0,    0,    0,    0,    0,    0,
    + 0,    0,    0,    0,    0,    0,    0,    0,
    +12,    8,    4,    0,    0,    4,    8,   12,
    +16,   12,    8,    4,    4,    8,   12,   16,
    +24,   20,   16,   12,   12,   16,   20,   24,
    +24,   24,   24,   16,   16,    6,   32,   32,

};

Score pieceSquareTable(const PType t, const Square s)
{
    return (Score) _PieceSquareTable[t][s];
}

static const uint16_t PieceWeight [PT_NO] =
{
    100,    // PAWN
    320,    // KNIGHT
    325,    // BISHOP
    500,    // ROOK
    975,    // QUEEN
    16383,  // KING
};

static Score evaluate_material   (const Position &pos);
static Score evaluate_mobility   (const Position &pos);


namespace Evaluator {

    Score evaluate (const Position &pos)
    {
        Score score = SCORE_DRAW;

        score   = evaluate_material (pos);

        if (SCORE_INFINITE == abs (int16_t (score))) return score;

        score  += evaluate_mobility (pos);

        //uint8_t pieces    [CLR_NO][PT_NO];
        //uint8_t piecesDiff[PT_NO];
        //uint8_t piecesSum [PT_NO];
        //for (PType t = PAWN; t <= KING; ++t)
        //{
        //    for (Color c = WHITE; c <= BLACK; ++c)
        //    {
        //        pieces[c][t] = pos.piece_count (c, t);
        //    }
        //    piecesDiff[t] = pieces[WHITE][t] - pieces[BLACK][t];
        //    piecesSum [t] = pieces[WHITE][t] + pieces[BLACK][t];
        //}
        //for (PType t = PAWN; t <= KING; ++t)
        //{
        //    //score += piecesDiff[t] / piecesSum [t];
        //}

        return score;
    }

}

static Score evaluate_material   (const Position &pos)
{
    const Color active = pos.active ();
    const Color pasive =    ~active;

    size_t kingDiff = pos.piece_count(active, KING) - pos.piece_count(pasive, KING);
    if (kingDiff)
    {
        return (kingDiff > 0) ? SCORE_INFINITE : -SCORE_INFINITE;
    }

    size_t pieceValue   [CLR_NO]  = { 0, 0 };
    for (PType t = PAWN; t <= KING; ++t)
    {
        pieceValue[active]   += PieceWeight[t] * pos.piece_count(active, t);
        pieceValue[pasive]   += PieceWeight[t] * pos.piece_count(pasive, t);
    }

    return (Score) (pieceValue[active] - pieceValue[pasive]);
    //return (SCORE_INFINITE * (double) (pieceValue[active] - pieceValue[pasive])) / (double) (pieceValue[active] + pieceValue[pasive]);
}

static Score evaluate_mobility   (const Position &pos)
{
    const Color active = pos.active ();
    const Color pasive =    ~active;

    uint16_t mobilityValue    [CLR_NO]  = { 0, 0 };

    Bitboard occ     = pos.pieces ();
    Bitboard actives = pos.pieces (active);
    Bitboard pasives = pos.pieces (pasive);

    for (PType t = PAWN; t <= KING; ++t)
    {
        const Piece a_piece         = active | t;
        const SquareList &a_orgs    = pos[a_piece];
        for (SquareList::const_iterator itr_s = a_orgs.begin(); itr_s != a_orgs.end(); ++itr_s)
        {
            const Square s  = *itr_s;
            const Bitboard moves = 0;//PieceMoves(a_piece, s, occ) & ~actives;
            mobilityValue[active] += (PieceWeight[t]) * pop_count<FULL> (moves);
        }

        const Piece p_piece         = pasive | t;
        const SquareList &p_orgs    = pos[p_piece];
        for (SquareList::const_iterator itr_s = p_orgs.begin(); itr_s != p_orgs.end(); ++itr_s)
        {
            const Square s  = *itr_s;
            const Bitboard moves = 0;//PieceMoves(p_piece, s, occ) & ~pasives;
            mobilityValue[pasive] += (PieceWeight[t]) * pop_count<FULL> (moves);
        }
    }

    return (Score) (mobilityValue[active] - mobilityValue[pasive]) / 10;

    //return (SCORE_INFINITE * (mobilityValue[active] - mobilityValue[pasive])) / (mobilityValue[active] + mobilityValue[pasive]) / 10;

}
