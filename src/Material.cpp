#include "Material.h"

#include <algorithm>
#include <cassert>
#include <cstring>

using namespace std;
using namespace BitBoard;
using namespace EndGame;

namespace {

    // Values modified by Joona Kiiski
    const Value MidgameLimit = Value (15581);
    const Value EndgameLimit = Value ( 3998);

    // Polynomial material balance parameters
    //                                            P      N      B      R      Q     BP
    const int32_t LinearCoefficients[NONE] = { -162, -1122,  -183,   249,   -52,  1852, };

    const int32_t QuadraticCoefficientsSameColor[NONE][NONE] =
    {
        // P    N    B    R    Q    BP
        {   2,   0,   0,   0,   0,  39, }, // P
        { 271,  -4,   0,   0,   0,  35, }, // N
        { 105,   4,   0,   0,   0,   0, }, // B
        {  -2,  46, 100,-141,   0, -27, }, // R
        {  29,  83, 148,-163,   0,  58, }, // Q
        {   0,   0,   0,   0,   0,   0, }, // BP
    };

    const int32_t QuadraticCoefficientsOppositeColor[NONE][NONE] =
    {
        //       THEIR PIECES
        // P    N    B    R    Q    BP
        {   0,   0,   0,   0,   0,  37, }, // P
        {  62,   0,   0,   0,   0,  10, }, // N
        {  64,  39,   0,   0,   0,  57, }, // B     OUR PIECES
        {  40,  23, -22,   0,   0,  50, }, // R
        { 101,   3, 151, 171,   0, 106, }, // Q
        {   0,   0,   0,   0,   0,   0, }, // BP
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
    inline bool is_KXK(const Position &pos)
    {
        const Color C_ = ((WHITE == C) ? BLACK : WHITE);

        return pos.non_pawn_material (C ) >= VALUE_MG_ROOK
            && pos.non_pawn_material (C_) == VALUE_ZERO
            && pos.count<PAWN> (C_) == 0;
    }

    template<Color C> 
    inline bool is_KBPsKs(const Position &pos)
    {
        return pos.non_pawn_material (C ) == VALUE_MG_BISHOP
            && pos.count<BSHP> (C ) == 1
            && pos.count<PAWN> (C ) >= 1;
    }

    template<Color C>
    inline bool is_KQKRPs(const Position &pos)
    {
        const Color C_  = ((WHITE == C) ? BLACK : WHITE);

        return pos.non_pawn_material (C ) == VALUE_MG_QUEEN
            && pos.count<QUEN> (C ) == 1
            && pos.count<PAWN> (C ) == 0
            && pos.count<ROOK> (C_) == 1
            && pos.count<PAWN> (C_) >= 1;
    }

    template<Color C>
    // imbalance<> () calculates imbalance comparing
    // piece count of each piece type for both colors.
    // KING == BISHOP PAIR
    inline int32_t imbalance (const int32_t count[CLR_NO][NONE])
    {
        const Color C_  = ((WHITE == C) ? BLACK : WHITE);

        int32_t value = VALUE_ZERO;

        // "The Evaluation of Material Imbalances in Chess"

        // Second-degree polynomial material imbalance by Tord Romstad
        for (PieceT pt1 = PAWN; pt1 <= QUEN; ++pt1)
        {
            int32_t pc = count[C][pt1];
            if (!pc) continue;

            int32_t v = LinearCoefficients[pt1];

            for (PieceT pt2 = PAWN; pt2 <= pt1; ++pt2)
            {
                v += count[C ][pt2] * QuadraticCoefficientsSameColor    [pt1][pt2]
                +    count[C_][pt2] * QuadraticCoefficientsOppositeColor[pt1][pt2];
            }
            v += count[C ][KING] * QuadraticCoefficientsSameColor    [pt1][KING]
            +    count[C_][KING] * QuadraticCoefficientsOppositeColor[pt1][KING];

            value += pc * v;
        }
        value += count[C][KING] * LinearCoefficients[KING];

        return value;
    }

} // namespace

namespace Material {

    // Material::probe () takes a position object as input,
    // looks up a MaterialEntry object, and returns a pointer to it.
    // If the material configuration is not already present in the table,
    // it is computed and stored there, so we don't have to recompute everything
    // when the same material configuration occurs again.
    Entry* probe     (const Position &pos, Table &table, Endgames &endgames)
    {
        Key key  = pos.matl_key ();
        Entry *e = table[key];

        // If e->_key matches the position's material hash key, it means that we
        // have analysed this material configuration before, and we can simply
        // return the information we found the last time instead of recomputing it.
        if (e->_key == key) return e;

        std::memset (e, 0, sizeof (Entry));
        e->_key           = key;
        e->_factor[WHITE] = e->_factor[BLACK] = SCALE_FACTOR_NORMAL;
        e->_game_phase    = game_phase (pos);

        // Let's look if we have a specialized evaluation function for this
        // particular material configuration. First we look for a fixed
        // configuration one, then a generic one if previous search failed.
        if (endgames.probe (key, e->evaluation_func))
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
        if (endgames.probe (key, eg_sf))
        {
            e->scaling_func[eg_sf->color ()] = eg_sf;
            return e;
        }

        // Generic scaling functions that refer to more then one material distribution.
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

        if (npm[WHITE] + npm[BLACK] == VALUE_ZERO)
        {
            if      (pos.count<PAWN> (BLACK) == 0
                &&   pos.count<PAWN> (WHITE) >= 2)
            {
                //ASSERT (pos.count<PAWN> (WHITE) >= 2);
                e->scaling_func[WHITE] = &ScaleKPsK[WHITE];
            }
            else if (pos.count<PAWN> (WHITE) == 0
                &&   pos.count<PAWN> (BLACK) >= 2)
            {
                //ASSERT (pos.count<PAWN> (BLACK) >= 2);
                e->scaling_func[BLACK] = &ScaleKPsK[BLACK];
            }
            else if (pos.count<PAWN> (WHITE) == 1
                &&   pos.count<PAWN> (BLACK) == 1)
            {
                // This is a special case because we set scaling functions for both colors instead of only one.
                e->scaling_func[WHITE] = &ScaleKPKP[WHITE];
                e->scaling_func[BLACK] = &ScaleKPKP[BLACK];
            }
        }

        // No pawns makes it difficult to win, even with a material advantage.
        // This catches some trivial draws like KK, KBK and KNK and gives a very drawish
        // scale factor for cases such as KRKBP and KmmKm (except for KBBKN).

        if (npm[WHITE] - npm[BLACK] <= VALUE_MG_BISHOP)
        {
            if      (pos.count<PAWN> (WHITE) == 0)
            {
                e->_factor[WHITE] = npm[WHITE] < VALUE_MG_ROOK ?
                    0 : !pos.count<NIHT> (WHITE) && !pos.bishops_pair (WHITE) ?
                    2 : 12;
            }
            else if (pos.count<PAWN> (WHITE) == 1)
            {
                e->_factor[WHITE] = (npm[WHITE] == npm[BLACK] || npm[WHITE] < VALUE_MG_ROOK) ?
                    2 : (pos.count<PAWN> (BLACK) <= 1) ? SCALE_FACTOR_ONEPAWN/8 : SCALE_FACTOR_ONEPAWN;
            }
        }

        if (npm[BLACK] - npm[WHITE] <= VALUE_MG_BISHOP)
        {
            if      (pos.count<PAWN> (BLACK) == 0)
            {
                e->_factor[BLACK] = npm[BLACK] < VALUE_MG_ROOK ?
                    0 : !pos.count<NIHT> (BLACK) && !pos.bishops_pair (BLACK) ?
                    2 : 12;
            }
            else if (pos.count<PAWN> (BLACK) == 1)
            {
                e->_factor[BLACK] = (npm[BLACK] == npm[WHITE] || npm[BLACK] < VALUE_MG_ROOK) ?
                    2 : (pos.count<PAWN> (WHITE) <= 1) ? SCALE_FACTOR_ONEPAWN/8 : SCALE_FACTOR_ONEPAWN;
            }
        }

        // Compute the space weight
        if (npm[WHITE] + npm[BLACK] >= 2 * VALUE_MG_QUEEN + 4 * VALUE_MG_ROOK + 2 * VALUE_MG_KNIGHT)
        {
            int32_t minor_piece_count = pos.count<NIHT> () + pos.count<BSHP> ();
            e->_space_weight = mk_score (minor_piece_count * minor_piece_count, 0);
        }

        // Evaluate the material imbalance.
        // We use KING as a place holder for the bishop pair "extended piece",
        // this allow us to be more flexible in defining bishop pair bonuses.
        const int32_t count[CLR_NO][NONE] =
        {
            {
                pos.count<PAWN> (WHITE), pos.count<NIHT> (WHITE), pos.count<BSHP> (WHITE),
                pos.count<ROOK> (WHITE), pos.count<QUEN> (WHITE), pos.bishops_pair (WHITE),
            },
            {
                pos.count<PAWN> (BLACK), pos.count<NIHT> (BLACK), pos.count<BSHP> (BLACK),
                pos.count<ROOK> (BLACK), pos.count<QUEN> (BLACK), pos.bishops_pair (BLACK),
            },
        };

        e->_value = int16_t ((imbalance<WHITE> (count) - imbalance<BLACK> (count)) / 16);
        return e;
    }

    // Material::game_phase () calculates the phase given the current position.
    // Because the phase is strictly a function of the material, it is stored in MaterialEntry.
    Phase game_phase (const Position &pos)
    {
        Value npm = pos.non_pawn_material (WHITE) + pos.non_pawn_material (BLACK);

        return npm >= MidgameLimit ? PHASE_MIDGAME
            :  npm <= EndgameLimit ? PHASE_ENDGAME
            :  Phase (((npm - EndgameLimit) * 128) / (MidgameLimit - EndgameLimit));
    }

} // namespace Material
