#include "Material.h"

#include <cstring>

#include "Position.h"
#include "Thread.h"

namespace Material {

    using namespace std;
    using namespace BitBoard;
    using namespace EndGame;

    namespace {

        // Polynomial material imbalance parameters:

        const i32 OwnSideLinearCoefficient[NONE] =
        {
            - 168, // P
            -1027, // N
            - 166, // B 
            + 238, // R
            - 138, // Q
            +1667  // BP
        };

        const i32 OwnSideQuadraticCoefficient[NONE][NONE] =
        {
            //          OWN PIECES
            //  P     N     B     R     Q    BP
            { +  2,    0,    0,    0,    0, + 40 }, // P
            { +255, -  3,    0,    0,    0, + 32 }, // N
            { +104, +  4,    0,    0,    0,    0 }, // B     OWN PIECES
            { -  2, + 47, +105, -149,    0, - 26 }, // R
            { + 24, +122, +137, -134,    0, -185 }, // Q
            {    0,    0,    0,    0,    0,    0 }  // BP
        };

        const i32 OppSideQuadraticCoefficient[NONE][NONE] =
        {
            //          OPP PIECES
            //  P     N     B     R     Q    BP
            {    0,    0,    0,    0,    0, + 36 }, // P
            { + 63,    0,    0,    0,    0, +  9 }, // N
            { + 65, + 42,    0,    0,    0, + 59 }, // B     OWN PIECES
            { + 39, + 24, - 24,    0,    0, + 46 }, // R
            { +100, - 37, +141, +268,    0, +101 }, // Q
            {    0,    0,    0,    0,    0,    0 }  // BP
        };

        // Endgame evaluation and scaling functions are accessed direcly and not through
        // the function maps because they correspond to more than one material hash key.
        Endgame<KXK>   EvaluateKXK  [CLR_NO] = { Endgame<KXK>    (WHITE), Endgame<KXK>    (BLACK) };

        Endgame<KBPsKs> ScaleKBPsKs [CLR_NO] = { Endgame<KBPsKs> (WHITE), Endgame<KBPsKs> (BLACK) };
        Endgame<KQKRPs> ScaleKQKRPs [CLR_NO] = { Endgame<KQKRPs> (WHITE), Endgame<KQKRPs> (BLACK) };

        Endgame<KPsK>   ScaleKPsK   [CLR_NO] = { Endgame<KPsK>   (WHITE), Endgame<KPsK>   (BLACK) };
        Endgame<KPKP>   ScaleKPKP   [CLR_NO] = { Endgame<KPKP>   (WHITE), Endgame<KPKP>   (BLACK) };

        // Helper used to detect a given material distribution
        bool is_KXK (const Position &pos, Color c)
        {
            return pos.non_pawn_material (c) >= VALUE_MG_ROOK
                //&& pos.count<NONPAWN> (~c) == 0
                //&& pos.count<PAWN> (~c) == 0
                && !more_than_one (pos.pieces (~c));
        }

        bool is_KBPsKs (const Position &pos, Color c)
        {
            return pos.non_pawn_material ( c) == VALUE_MG_BSHP
                && pos.count<BSHP> (c) == 1
                && pos.count<PAWN> (c) != 0;
        }

        bool is_KQKRPs (const Position &pos, Color c)
        {
            return pos.non_pawn_material ( c) == VALUE_MG_QUEN
                && pos.count<QUEN> (c) == 1
                && pos.count<PAWN> ( c) == 0
                && pos.non_pawn_material (~c) == VALUE_MG_ROOK
                && pos.count<ROOK> (~c) == 1
                && pos.count<PAWN> (~c) != 0;
        }

        template<Color Own>
        // imbalance<>() calculates imbalance comparing
        // piece count of each piece type for both colors.
        // KING == BISHOP_PAIR
        Value imbalance (const i32 count[][NONE])
        {
            const auto Opp = WHITE == Own ? BLACK : WHITE;

            auto value = VALUE_ZERO;

            // "The Evaluation of Material Imbalances in Chess"

            // Second-degree polynomial material imbalance
            for (auto pt1 = PAWN; pt1 < KING; ++pt1)
            {
                if (count[Own][pt1] != 0)
                {
                    auto v = OwnSideLinearCoefficient[pt1];

                    for (auto pt2 = PAWN; pt2 <= pt1; ++pt2)
                    {
                        v += count[Own][pt2] * OwnSideQuadraticCoefficient[pt1][pt2]
                          +  count[Opp][pt2] * OppSideQuadraticCoefficient[pt1][pt2];
                    }
                    v += count[Own][KING] * OwnSideQuadraticCoefficient[pt1][KING]
                      +  count[Opp][KING] * OppSideQuadraticCoefficient[pt1][KING];

                    value += count[Own][pt1] * v;
                }
            }
            value += count[Own][KING] * OwnSideLinearCoefficient[KING];

            return value;
        }

    }

    // probe() takes a position object as input,
    // looks up a MaterialEntry object, and returns a pointer to it.
    // If the material configuration is not already present in the table,
    // it is computed and stored there, so don't have to recompute everything
    // when the same material configuration occurs again.
    Entry* probe (const Position &pos)
    {
        Key matl_key = pos.matl_key ();
        Entry *e     = pos.thread ()->matl_table[matl_key];

        // If e->_matl_key matches the position's material hash key, it means that
        // have analysed this material configuration before, and can simply
        // return the information found the last time instead of recomputing it.
        if (e->matl_key != matl_key)
        {
            std::memset (e, 0x00, sizeof (*e));
            e->matl_key      = matl_key;
            e->factor[WHITE] = e->factor[BLACK] = SCALE_FACTOR_NORMAL;
            e->game_phase    = pos.game_phase ();

            // Let's look if have a specialized evaluation function for this
            // particular material configuration. First look for a fixed
            // configuration one, then a generic one if previous search failed.
            if ((e->evaluation_func = EndGames->probe<Value> (matl_key)) != nullptr)
            {
                return e;
            }
            // Generic evaluation
            for (auto c = WHITE; c <= BLACK; ++c)
            {
                if (is_KXK (pos, c))
                {
                    e->evaluation_func = &EvaluateKXK[c];
                    return e;
                }
            }

            // OK, didn't find any special evaluation function for the current
            // material configuration. Is there a suitable scaling function?
            //
            // Face problems when there are several conflicting applicable
            // scaling functions and need to decide which one to use.
            EndgameBase<ScaleFactor> *scaling_func;
            if ((scaling_func = EndGames->probe<ScaleFactor> (matl_key)) != nullptr)
            {
                e->scaling_func[scaling_func->strong_side ()] = scaling_func; // Only strong color assigned
                return e;
            }

            // Didn't find any specialized scaling function, so fall back on
            // generic scaling functions that refer to more than one material distribution.
            // Note that these ones don't return after setting the function.
            for (auto c = WHITE; c <= BLACK; ++c)
            {
                if (is_KBPsKs (pos, c))
                {
                    e->scaling_func[c] = &ScaleKBPsKs[c];
                }
                else
                if (is_KQKRPs (pos, c))
                {
                    e->scaling_func[c] = &ScaleKQKRPs[c];
                }
            }

            const Value npm[CLR_NO] = 
            {
                pos.non_pawn_material (WHITE),
                pos.non_pawn_material (BLACK)
            };

            // Only pawns on the board
            if (   npm[WHITE] + npm[BLACK] == VALUE_ZERO
                && pos.pieces (PAWN) != U64(0)
               )
            {
                if (pos.count<PAWN> (BLACK) == 0)
                {
                    assert(pos.count<PAWN> (WHITE) > 1);
                    e->scaling_func[WHITE] = &ScaleKPsK[WHITE];
                }
                else
                if (pos.count<PAWN> (WHITE) == 0)
                {
                    assert(pos.count<PAWN> (BLACK) > 1);
                    e->scaling_func[BLACK] = &ScaleKPsK[BLACK];
                }
                else
                if (   pos.count<PAWN> (WHITE) == 1
                    && pos.count<PAWN> (BLACK) == 1
                   )
                {
                    // This is a special case because set scaling functions for both colors instead of only one.
                    e->scaling_func[WHITE] = &ScaleKPKP[WHITE];
                    e->scaling_func[BLACK] = &ScaleKPKP[BLACK];
                }
            }

            // No pawns makes it difficult to win, even with a material advantage.
            // This catches some trivial draws like KK, KBK and KNK and gives a very drawish
            // scale factor for cases such as KRKBP and KmmKm (except for KBBKN).

            if (npm[WHITE] - npm[BLACK] <= VALUE_MG_BSHP)
            {
                if (pos.count<PAWN> (WHITE) == 0)
                {
                    e->factor[WHITE] = u08(
                        npm[WHITE] <  VALUE_MG_ROOK ? SCALE_FACTOR_DRAW :
                        npm[BLACK] <= VALUE_MG_BSHP ? 4 : 14);
                }
                if (pos.count<PAWN> (WHITE) == 1)
                {
                    e->factor[WHITE] = u08(SCALE_FACTOR_ONEPAWN);
                }
            }

            if (npm[BLACK] - npm[WHITE] <= VALUE_MG_BSHP)
            {
                if (pos.count<PAWN> (BLACK) == 0)
                {
                    e->factor[BLACK] = u08(
                        npm[BLACK] <  VALUE_MG_ROOK ? SCALE_FACTOR_DRAW :
                        npm[WHITE] <= VALUE_MG_BSHP ? 4 : 14);
                }
                if (pos.count<PAWN> (BLACK) == 1)
                {
                    e->factor[BLACK] = u08(SCALE_FACTOR_ONEPAWN);
                }
            }

            // Evaluate the material imbalance.
            // Use KING as a place holder for the bishop pair "extended piece",
            // this allow us to be more flexible in defining bishop pair bonuses.
            const i32 piece_count[CLR_NO][NONE] =
            {
                {
                    pos.count<PAWN> (WHITE), pos.count<NIHT> (WHITE), pos.count<BSHP> (WHITE),
                    pos.count<ROOK> (WHITE), pos.count<QUEN> (WHITE), pos.bishops_pair (WHITE)
                },
                {
                    pos.count<PAWN> (BLACK), pos.count<NIHT> (BLACK), pos.count<BSHP> (BLACK),
                    pos.count<ROOK> (BLACK), pos.count<QUEN> (BLACK), pos.bishops_pair (BLACK)
                }
            };

            auto value = Value((imbalance<WHITE> (piece_count) - imbalance<BLACK> (piece_count)) / 16);
            e->imbalance = mk_score (value, value);
        }

        return e;
    }

}
