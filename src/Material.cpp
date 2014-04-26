#include "Material.h"

#include <algorithm>
#include <cstring>

#include "Position.h"

namespace Material {

    using namespace std;
    using namespace BitBoard;
    using namespace EndGame;

    namespace {

        const Value MidgameLimit = Value (15581);
        const Value EndgameLimit = Value ( 3998);

        // Polynomial material balance parameters: P      N      B      R      Q     BP
        const i32 LinearCoefficient[NONE] = { - 162, -1122, - 183, + 249, - 154, +1852, };

        const i32 OwnColorQuadraticCoefficient[NONE][NONE] =
        {
            //  P     N     B     R     Q    BP
            { +  2, +  0, +  0, +  0, +  0, + 39, }, // P
            { +271, -  4, +  0, +  0, +  0, + 35, }, // N
            { +105, +  4, +  0, +  0, +  0, +  0, }, // B
            { -  2, + 46, +100, -141, +  0, - 27, }, // R
            { + 25, +129, +142, -137, +  0, -177, }, // Q
            { +  0, +  0, +  0, +  0, +  0, +  0, }, // BP
        };

        const i32 OppColorQuadraticCoefficient[NONE][NONE] =
        {
            //          THEIR PIECES
            //  P     N     B     R     Q    BP
            { +  0, +  0, +  0, +  0, +  0, + 37, }, // P
            { + 62, +  0, +  0, +  0, +  0, + 10, }, // N
            { + 64, + 39, +  0, +  0, +  0, + 57, }, // B     OUR PIECES
            { + 40, + 23, - 22, +  0, +  0, + 50, }, // R
            { +105, - 39, +141, +274, +  0, + 98, }, // Q
            { +  0, +  0, +  0, +  0, +  0, +  0, }, // BP
        };

        // Endgame evaluation and scaling functions are accessed direcly and not through
        // the function maps because they correspond to more than one material hash key.
        Endgame<KXK>   EvaluateKXK  [CLR_NO] = { Endgame<KXK>    (WHITE), Endgame<KXK>    (BLACK) };

        Endgame<KBPsKs> ScaleKBPsKs [CLR_NO] = { Endgame<KBPsKs> (WHITE), Endgame<KBPsKs> (BLACK) };
        Endgame<KQKRPs> ScaleKQKRPs [CLR_NO] = { Endgame<KQKRPs> (WHITE), Endgame<KQKRPs> (BLACK) };

        Endgame<KPsK>   ScaleKPsK   [CLR_NO] = { Endgame<KPsK>   (WHITE), Endgame<KPsK>   (BLACK) };
        Endgame<KPKP>   ScaleKPKP   [CLR_NO] = { Endgame<KPKP>   (WHITE), Endgame<KPKP>   (BLACK) };

        // Helper templates used to detect a given material distribution
        template<Color C>
        inline bool is_KXK (const Position &pos)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            return pos.non_pawn_material (C ) >= VALUE_MG_ROOK
                && pos.non_pawn_material (C_) == VALUE_ZERO
                && pos.count<PAWN> (C_) == 0;
        }

        template<Color C> 
        inline bool is_KBPsKs (const Position &pos)
        {
            return pos.non_pawn_material (C ) == VALUE_MG_BSHP
                && pos.count<BSHP> (C ) == 1
                && pos.count<PAWN> (C ) >= 1;
        }

        template<Color C>
        inline bool is_KQKRPs (const Position &pos)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            return pos.non_pawn_material (C ) == VALUE_MG_QUEN
                //&& pos.non_pawn_material (C_) == VALUE_MG_ROOK
                && pos.count<QUEN> (C ) == 1
                && pos.count<PAWN> (C ) == 0
                && pos.count<ROOK> (C_) == 1
                && pos.count<PAWN> (C_) >= 1;
        }

        template<Color C>
        // imbalance<> () calculates imbalance comparing
        // piece count of each piece type for both colors.
        // KING == BISHOP_PAIR
        inline Value imbalance (const i32 count[][NONE])
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            i32 value = VALUE_ZERO;

            // "The Evaluation of Material Imbalances in Chess"

            // Second-degree polynomial material imbalance
            for (i08 pt1 = PAWN; pt1 < KING; ++pt1)
            {
                i32 pc = count[C ][pt1];
                if (pc > 0)
                {
                    i32 v = LinearCoefficient[pt1];

                    for (i08 pt2 = PAWN; pt2 <= pt1; ++pt2)
                    {
                        v += count[C ][pt2] * OwnColorQuadraticCoefficient[pt1][pt2]
                          +  count[C_][pt2] * OppColorQuadraticCoefficient[pt1][pt2];
                    }
                    v += count[C ][KING] * OwnColorQuadraticCoefficient[pt1][KING]
                      +  count[C_][KING] * OppColorQuadraticCoefficient[pt1][KING];

                    value += pc * v;
                }
            }
            value += count[C ][KING] * LinearCoefficient[KING];

            return Value (value);
        }

    } // namespace

    // Material::probe() takes a position object as input,
    // looks up a MaterialEntry object, and returns a pointer to it.
    // If the material configuration is not already present in the table,
    // it is computed and stored there, so we don't have to recompute everything
    // when the same material configuration occurs again.
    Entry* probe     (const Position &pos, Table &table)
    {
        Key matl_key = pos.matl_key ();
        Entry *e     = table[matl_key];

        // If e->_matl_key matches the position's material hash key, it means that
        // we have analysed this material configuration before, and we can simply
        // return the information we found the last time instead of recomputing it.
        if (e->_matl_key != matl_key)
        {
            memset (e, 0x00, sizeof (*e));
            e->_matl_key      = matl_key;
            e->_factor[WHITE] = e->_factor[BLACK] = SCALE_FACTOR_NORMAL;
            e->_game_phase    = game_phase (pos);

            // Let's look if we have a specialized evaluation function for this
            // particular material configuration. First we look for a fixed
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

            // OK, we didn't find any special evaluation function for the current
            // material configuration. Is there a suitable scaling function?
            //
            // We face problems when there are several conflicting applicable
            // scaling functions and we need to decide which one to use.
            EndgameBase<ScaleFactor> *eg_sf;
            if (EndGames->probe (matl_key, eg_sf))
            {
                e->scaling_func[eg_sf->color ()] = eg_sf;
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

            if      (is_KQKRPs<WHITE> (pos))
            {
                e->scaling_func[WHITE] = &ScaleKQKRPs[WHITE];
            }
            else if (is_KQKRPs<BLACK> (pos))
            {
                e->scaling_func[BLACK] = &ScaleKQKRPs[BLACK];
            }

            Value npm[CLR_NO] = 
            {
                pos.non_pawn_material (WHITE),
                pos.non_pawn_material (BLACK),
            };

            if (   (npm[WHITE] + npm[BLACK] == VALUE_ZERO)
                && (pos.pieces<PAWN> () != U64 (0))
               )
            {
                if (      (pos.count<PAWN> (BLACK) == 0)
                       && (pos.count<PAWN> (WHITE) >= 2)
                   )
                {
                    e->scaling_func[WHITE] = &ScaleKPsK[WHITE];
                }
                else if ( (pos.count<PAWN> (WHITE) == 0)
                       && (pos.count<PAWN> (BLACK) >= 2)
                        )
                {
                    e->scaling_func[BLACK] = &ScaleKPsK[BLACK];
                }
                else if ( (pos.count<PAWN> (WHITE) == 1)
                       && (pos.count<PAWN> (BLACK) == 1)
                        )
                {
                    // This is a special case because we set scaling functions for both colors instead of only one.
                    e->scaling_func[WHITE] = &ScaleKPKP[WHITE];
                    e->scaling_func[BLACK] = &ScaleKPKP[BLACK];
                }
            }

            // No pawns makes it difficult to win, even with a material advantage.
            // This catches some trivial draws like KK, KBK and KNK and gives a very drawish
            // scale factor for cases such as KRKBP and KmmKm (except for KBBKN).

            if (npm[WHITE] - npm[BLACK] <= VALUE_MG_BSHP)
            {
                if      (pos.count<PAWN> (WHITE) == 0)
                {
                    e->_factor[WHITE] = u08 (npm[WHITE] <= VALUE_MG_BSHP ?
                        SCALE_FACTOR_DRAW : !pos.count<NIHT> (WHITE) && !pos.bishops_pair (WHITE) ?
                        1 : npm[BLACK] <= VALUE_MG_BSHP ? 
                        4 : 12);
                }
                else if (pos.count<PAWN> (WHITE) == 1)
                {
                    e->_factor[WHITE] = u08 ((npm[WHITE] == npm[BLACK] || npm[WHITE] <= VALUE_MG_BSHP) ?
                        4 : SCALE_FACTOR_ONEPAWN / (pos.count<PAWN> (BLACK) + 1));
                }
            }

            if (npm[BLACK] - npm[WHITE] <= VALUE_MG_BSHP)
            {
                if      (pos.count<PAWN> (BLACK) == 0)
                {
                    e->_factor[BLACK] = u08 (npm[BLACK] <= VALUE_MG_BSHP ?
                        SCALE_FACTOR_DRAW : !pos.count<NIHT> (BLACK) && !pos.bishops_pair (BLACK) ?
                        1 : npm[WHITE] <= VALUE_MG_BSHP ? 
                        4 : 12);
                }
                else if (pos.count<PAWN> (BLACK) == 1)
                {
                    e->_factor[BLACK] = u08 ((npm[BLACK] == npm[WHITE] || npm[BLACK] <= VALUE_MG_BSHP) ?
                        4 : SCALE_FACTOR_ONEPAWN / (pos.count<PAWN> (WHITE) + 1));
                }
            }

            // Compute the space weight
            if (npm[WHITE] + npm[BLACK] >= 2 * VALUE_MG_QUEN + 4 * VALUE_MG_ROOK + 2 * VALUE_MG_NIHT)
            {
                i32 minor_count = pos.count<NIHT> () + pos.count<BSHP> ();
                e->_space_weight = mk_score (minor_count * minor_count, 0);
            }

            // Evaluate the material imbalance.
            // We use KING as a place holder for the bishop pair "extended piece",
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

            e->_value = i16 ((imbalance<WHITE> (count) - imbalance<BLACK> (count)) / 0x10);
        }

        return e;
    }

    // Material::game_phase() calculates the phase given the current position.
    // Because the phase is strictly a function of the material, it is stored in MaterialEntry.
    Phase game_phase (const Position &pos)
    {
        Value npm = pos.non_pawn_material (WHITE) + pos.non_pawn_material (BLACK);

        return npm >= MidgameLimit ? PHASE_MIDGAME
            :  npm <= EndgameLimit ? PHASE_ENDGAME
            :  Phase (((npm - EndgameLimit) * PHASE_MIDGAME) / (MidgameLimit - EndgameLimit));
    }

} // namespace Material
