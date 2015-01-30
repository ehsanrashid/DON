#include "Material.h"

#include <cstring>

#include "Position.h"
#include "Thread.h"

namespace Material {

    using namespace std;
    using namespace BitBoard;
    using namespace Threads;
    using namespace EndGame;

    namespace {

        // Polynomial material imbalance parameters:

        const i32 OwnSideLinearCoefficient[NONE] =
        {
            - 162, // P
            -1122, // N
            - 183, // B 
            + 249, // R
            - 154, // Q
            +1852  // BP
        };

        const i32 OwnSideQuadraticCoefficient[NONE][NONE] =
        {
            //          OWN PIECES
            //  P     N     B     R     Q    BP
            { +  2, +  0, +  0, +  0, +  0, + 39 }, // P
            { +271, -  4, +  0, +  0, +  0, + 35 }, // N
            { +105, +  4, +  0, +  0, +  0, +  0 }, // B     OWN PIECES
            { -  2, + 46, +100, -141, +  0, - 27 }, // R
            { + 25, +129, +142, -137, +  0, -177 }, // Q
            { +  0, +  0, +  0, +  0, +  0, +  0 }  // BP
        };

        const i32 OppSideQuadraticCoefficient[NONE][NONE] =
        {
            //          OPP PIECES
            //  P     N     B     R     Q    BP
            { +  0, +  0, +  0, +  0, +  0, + 37 }, // P
            { + 62, +  0, +  0, +  0, +  0, + 10 }, // N
            { + 64, + 39, +  0, +  0, +  0, + 57 }, // B     OWN PIECES
            { + 40, + 23, - 22, +  0, +  0, + 50 }, // R
            { +105, - 39, +141, +274, +  0, + 98 }, // Q
            { +  0, +  0, +  0, +  0, +  0, +  0 }  // BP
        };

        // Endgame evaluation and scaling functions are accessed direcly and not through
        // the function maps because they correspond to more than one material hash key.
        Endgame<KXK>   EvaluateKXK  [CLR_NO] = { Endgame<KXK>    (WHITE), Endgame<KXK>    (BLACK) };

        Endgame<KBPsKs> ScaleKBPsKs [CLR_NO] = { Endgame<KBPsKs> (WHITE), Endgame<KBPsKs> (BLACK) };
        Endgame<KQKRPs> ScaleKQKRPs [CLR_NO] = { Endgame<KQKRPs> (WHITE), Endgame<KQKRPs> (BLACK) };

        Endgame<KPsK>   ScaleKPsK   [CLR_NO] = { Endgame<KPsK>   (WHITE), Endgame<KPsK>   (BLACK) };
        Endgame<KPKP>   ScaleKPKP   [CLR_NO] = { Endgame<KPKP>   (WHITE), Endgame<KPKP>   (BLACK) };

        // Helper templates used to detect a given material distribution
        template<Color Own>
        inline bool is_KXK (const Position &pos)
        {
            const Color Opp = WHITE == Own ? BLACK : WHITE;

            return pos.non_pawn_material (Own) >= VALUE_MG_ROOK
                && pos.non_pawn_material (Opp) == VALUE_ZERO
                && pos.count<PAWN> (Opp) == 0;
        }

        template<Color Own> 
        inline bool is_KBPsKs (const Position &pos)
        {
            const Color Opp = WHITE == Own ? BLACK : WHITE;

            return pos.non_pawn_material (Own) == VALUE_MG_BSHP
                && pos.non_pawn_material (Opp) == VALUE_ZERO
                //&& pos.count<BSHP> (Own) == 1
                && pos.count<PAWN> (Own) != 0;
        }

        template<Color Own>
        inline bool is_KQKRPs (const Position &pos)
        {
            const Color Opp = WHITE == Own ? BLACK : WHITE;

            return pos.non_pawn_material (Own) == VALUE_MG_QUEN
                && pos.non_pawn_material (Opp) == VALUE_MG_ROOK
                //&& pos.count<QUEN> (Own) == 1
                //&& pos.count<ROOK> (Opp) == 1
                && pos.count<PAWN> (Own) == 0
                && pos.count<PAWN> (Opp) != 0;
        }

        template<Color Own>
        // imbalance<>() calculates imbalance comparing
        // piece count of each piece type for both colors.
        // KING == BISHOP_PAIR
        inline Value imbalance (const i32 count[][NONE])
        {
            const Color Opp = WHITE == Own ? BLACK : WHITE;

            i32 value = VALUE_ZERO;

            // "The Evaluation of Material Imbalances in Chess"

            // Second-degree polynomial material imbalance
            for (i08 pt1 = PAWN; pt1 < KING; ++pt1)
            {
                if (count[Own][pt1] != 0)
                {
                    i32 v = OwnSideLinearCoefficient[pt1];

                    for (i08 pt2 = PAWN; pt2 <= pt1; ++pt2)
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

            return Value(value);
        }

    }

    // probe() takes a position object as input,
    // looks up a MaterialEntry object, and returns a pointer to it.
    // If the material configuration is not already present in the table,
    // it is computed and stored there, so don't have to recompute everything
    // when the same material configuration occurs again.
    Entry* probe     (const Position &pos)
    {
        Key matl_key = pos.matl_key ();
        Entry *e     = pos.thread ()->matl_table[matl_key];

        // If e->_matl_key matches the position's material hash key, it means that
        // have analysed this material configuration before, and can simply
        // return the information found the last time instead of recomputing it.
        if (e->matl_key != matl_key)
        {
            memset (e, 0x00, sizeof (*e));
            e->matl_key      = matl_key;
            e->factor[WHITE] =
            e->factor[BLACK] = SCALE_FACTOR_NORMAL;
            e->game_phase    = pos.game_phase ();

            // Let's look if have a specialized evaluation function for this
            // particular material configuration. First look for a fixed
            // configuration one, then a generic one if previous search failed.
            if (EndGames->probe (matl_key, e->evaluation_func))
            {
                return e;
            }

            if (is_KXK<WHITE> (pos))
            {
                e->evaluation_func = &EvaluateKXK[WHITE];
                return e;
            }
            if (is_KXK<BLACK> (pos))
            {
                e->evaluation_func = &EvaluateKXK[BLACK];
                return e;
            }

            // OK, didn't find any special evaluation function for the current
            // material configuration. Is there a suitable scaling function?
            //
            // Face problems when there are several conflicting applicable
            // scaling functions and need to decide which one to use.
            EndgameBase<ScaleFactor> *eg_sf;
            if (EndGames->probe (matl_key, eg_sf))
            {
                e->scaling_func[eg_sf->strong_side ()] = eg_sf;
                return e;
            }

            // Generic scaling functions that refer to more than one material distribution.
            // Should be probed after the specialized ones.
            // Note that these ones don't return after setting the function.
            if (is_KBPsKs<WHITE> (pos))
            {
                e->scaling_func[WHITE] = &ScaleKBPsKs[WHITE];
            }
            if (is_KBPsKs<BLACK> (pos))
            {
                e->scaling_func[BLACK] = &ScaleKBPsKs[BLACK];
            }

            if (is_KQKRPs<WHITE> (pos))
            {
                e->scaling_func[WHITE] = &ScaleKQKRPs[WHITE];
            }
            else
            if (is_KQKRPs<BLACK> (pos))
            {
                e->scaling_func[BLACK] = &ScaleKQKRPs[BLACK];
            }

            const Value npm_w = pos.non_pawn_material (WHITE)
                      , npm_b = pos.non_pawn_material (BLACK);

            // Only pawns on the board
            if (  npm_w + npm_b == VALUE_ZERO
               && pos.pieces<PAWN> () != U64(0)
               )
            {
                if (  pos.count<PAWN> (BLACK) == 0
                   && pos.count<PAWN> (WHITE) >  1
                   )
                {
                    e->scaling_func[WHITE] = &ScaleKPsK[WHITE];
                }
                else
                if (  pos.count<PAWN> (WHITE) == 0
                   && pos.count<PAWN> (BLACK) >  1
                   )
                {
                    e->scaling_func[BLACK] = &ScaleKPsK[BLACK];
                }
                else
                if (  pos.count<PAWN> (WHITE) == 1
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

            if (npm_w - npm_b <= VALUE_MG_BSHP)
            {
                if (pos.count<PAWN> (WHITE) == 0)
                {
                    e->factor[WHITE] = u08(
                        npm_w <  VALUE_MG_ROOK ? SCALE_FACTOR_DRAW :
                        npm_b <= VALUE_MG_BSHP ? 4 : 12);
                }
                else
                if (pos.count<PAWN> (WHITE) == 1)
                {
                    e->factor[WHITE] = u08(SCALE_FACTOR_ONEPAWN);
                }
            }

            if (npm_b - npm_w <= VALUE_MG_BSHP)
            {
                if (pos.count<PAWN> (BLACK) == 0)
                {
                    e->factor[BLACK] = u08(
                        npm_b <  VALUE_MG_ROOK ? SCALE_FACTOR_DRAW :
                        npm_w <= VALUE_MG_BSHP ? 4 : 12);
                }
                else
                if (pos.count<PAWN> (BLACK) == 1)
                {
                    e->factor[BLACK] = u08(SCALE_FACTOR_ONEPAWN);
                }
            }

            // Evaluate the material imbalance.
            // Use KING as a place holder for the bishop pair "extended piece",
            // this allow us to be more flexible in defining bishop pair bonuses.
            const i32 count[CLR_NO][NONE] =
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

            Value value = Value((imbalance<WHITE> (count) - imbalance<BLACK> (count)) >> 4);
            e->imbalance = mk_score (value, value);
        }

        return e;
    }

}
