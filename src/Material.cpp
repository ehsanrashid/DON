#include "Material.h"

#include <array>
#include <cassert>
#include <cstring>

#include "Thread.h"

namespace Material {

    using namespace std;
    using namespace Endgames;

    namespace {

        // Polynomial material imbalance parameters

        constexpr Table<i32, NONE, NONE> OwnQuadratic
        {{
            //          Own Pieces
            //  P    N    B     R   Q    BP
            {  38,   0,   0,    0,  0,   40 }, // P
            { 255, -62,   0,    0,  0,   32 }, // N
            { 104,   4,   0,    0,  0,    0 }, // B     Own Pieces
            {  -2,  47, 105, -208,  0,  -26 }, // R
            {  24, 117, 133, -134, -6, -189 }, // Q
            {   0,   0,   0,    0,  0, 1438 }  // BP
        }};

        constexpr Table<i32, NONE, NONE> OppQuadratic
        {{
            //          Opp Pieces
            //  P    N    B     R   Q    BP
            {   0,   0,   0,    0,  0,   36 }, // P
            {  63,   0,   0,    0,  0,    9 }, // N
            {  65,  42,   0,    0,  0,   59 }, // B     Own Pieces
            {  39,  24, -24,    0,  0,   46 }, // R
            { 100, -42, 137,  268,  0,   97 }, // Q
            {   0,   0,   0,    0,  0,    0 }  // BP
        }};

        // Endgame evaluation and scaling functions are accessed direcly and not through
        // the function maps because they correspond to more than one material hash key.
        Table<Endgame<KXK>, COLORS>    ValueKXK
        {
            Endgame<KXK>(WHITE),
            Endgame<KXK>(BLACK)
        };
        // Endgame generic scale functions
        Table<Endgame<KPKP>, COLORS>   ScaleKPKP
        {
            Endgame<KPKP>(WHITE),
            Endgame<KPKP>(BLACK)
        };
        Table<Endgame<KPsK>, COLORS>   ScaleKPsK
        {
            Endgame<KPsK>(WHITE),
            Endgame<KPsK>(BLACK)
        };
        Table<Endgame<KBPsK>, COLORS> ScaleKBPsK
        {
            Endgame<KBPsK>(WHITE),
            Endgame<KBPsK>(BLACK)
        };
        Table<Endgame<KQKRPs>, COLORS> ScaleKQKRPs
        {
            Endgame<KQKRPs>(WHITE),
            Endgame<KQKRPs>(BLACK)
        } ;

        /// imbalance() calculates the imbalance by the piece count of each piece type for both colors.
        /// NOTE:: KING == BISHOP PAIR
        template<Color Own>
        i32 computeImbalance(const Table<i32, COLORS, NONE> &count)
        {
            constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

            i32 value = 0;
            // "The Evaluation of Material Imbalances in Chess"
            // Second-degree polynomial material imbalance by Tord Romstad
            for (auto pt1 : { PAWN, NIHT, BSHP, ROOK, QUEN })
            {
                if (0 != count[Own][pt1])
                {
                    i32 v = 0;

                    for (auto pt2 = PAWN; pt2 <= pt1; ++pt2)
                    {
                        v += count[Own][pt2] * OwnQuadratic[pt1][pt2]
                           + count[Opp][pt2] * OppQuadratic[pt1][pt2];
                    }
                    v += count[Own][KING] * OwnQuadratic[pt1][KING]
                       + count[Opp][KING] * OppQuadratic[pt1][KING];

                    value += count[Own][pt1] * v;
                }
            }
            if (0 != count[Own][KING])
            {
                value += count[Own][KING] * OwnQuadratic[KING][KING];
                       //+ count[Opp][KING] * OppQuadratic[KING][KING]; // OppQuadratic[KING][KING] = 0
            }

            return value;
        }
    }

    void Entry::evaluate(const Position &pos)
    {
        // Calculates the phase interpolating total non-pawn material between endgame and midgame limits.
        phase = (i32(clamp(pos.nonPawnMaterial(), VALUE_ENDGAME, VALUE_MIDGAME) - VALUE_ENDGAME)
                 * PhaseResolution)
              / i32(VALUE_MIDGAME - VALUE_ENDGAME);
        imbalance = SCORE_ZERO;
        scale.fill(SCALE_NORMAL);
        scalingFunc.fill(nullptr);

        // Let's look if have a specialized evaluation function for this
        // particular material configuration. First look for a fixed
        // configuration one, then a generic one if previous search failed.
        evaluationFunc = Endgames::probe<Value>(pos.matlKey());
        if (nullptr != evaluationFunc)
        {
            return;
        }
        // Generic evaluation
        for (Color c : { WHITE, BLACK })
        {
            if (   pos.nonPawnMaterial( c) >= VALUE_MG_ROOK
                && pos.count(~c) == 1)
            {
                evaluationFunc = &ValueKXK[c];
                return;
            }
        }

        // Didn't find any special evaluation function for the current
        // material configuration. Is there a suitable scaling function?
        //
        // Face problems when there are several conflicting applicable
        // scaling functions and need to decide which one to use.
        const auto *scalingFn = Endgames::probe<Scale>(pos.matlKey());
        if (nullptr != scalingFn)
        {
            scalingFunc[scalingFn->stngColor] = scalingFn;
            return;
        }

        // Didn't find any specialized scaling function, so fall back on
        // generic scaling functions that refer to more than one material distribution.
        for (Color c : { WHITE, BLACK })
        {
            if (   pos.nonPawnMaterial( c) == VALUE_MG_BSHP
                //&& pos.count( c|BSHP) == 1
                && pos.count( c|PAWN) != 0)
            {
                scalingFunc[c] = &ScaleKBPsK[c];
            }
            else
            if (   pos.nonPawnMaterial( c) == VALUE_MG_QUEN
                //&& pos.count( c|QUEN) == 1
                && pos.count( c|PAWN) == 0
                && pos.nonPawnMaterial(~c) == VALUE_MG_ROOK
                //&& pos.count(~c|ROOK) == 1
                && pos.count(~c|PAWN) != 0)
            {
                scalingFunc[c] = &ScaleKQKRPs[c];
            }

            // Zero or just one pawn makes it difficult to win, even with a material advantage.
            // This catches some trivial draws like KK, KBK and KNK and gives a very drawish
            // scale for cases such as KRKBP and KmmKm (except for KBBKN).
            if (   pos.count( c|PAWN) == 0
                && abs(  pos.nonPawnMaterial( c)
                       - pos.nonPawnMaterial(~c)) <= VALUE_MG_BSHP)
            {
                scale[c] = pos.nonPawnMaterial( c) <  VALUE_MG_ROOK ?
                                SCALE_DRAW :
                                Scale(14 - 10 * (pos.nonPawnMaterial(~c) <= VALUE_MG_BSHP));
            }
        }

        // Only pawns left
        if (   pos.nonPawnMaterial() == VALUE_ZERO
            && pos.pieces(PAWN) != 0)
        {
            if (pos.pieces(BLACK, PAWN) == 0)
            {
                assert(2 <= pos.count(WHITE|PAWN));
                scalingFunc[WHITE] = &ScaleKPsK[WHITE];
            }
            else
            if (pos.pieces(WHITE, PAWN) == 0)
            {
                assert(2 <= pos.count(BLACK|PAWN));
                scalingFunc[BLACK] = &ScaleKPsK[BLACK];
            }
            else
            if (   pos.count(WHITE|PAWN) == 1
                && pos.count(BLACK|PAWN) == 1)
            {
                scalingFunc[WHITE] = &ScaleKPKP[WHITE];
                scalingFunc[BLACK] = &ScaleKPKP[BLACK];
            }
        }

        // Evaluate the material imbalance.
        // Use KING as a place holder for the bishop pair "extended piece",
        // this allow us to be more flexible in defining bishop pair bonuses.
        Table<i32, COLORS, NONE> pieceCount
        {{
            {
                pos.count(WHITE|PAWN),
                pos.count(WHITE|NIHT),
                pos.count(WHITE|BSHP),
                pos.count(WHITE|ROOK),
                pos.count(WHITE|QUEN),
                pos.pairedBishop(WHITE)
            },
            {
                pos.count(BLACK|PAWN),
                pos.count(BLACK|NIHT),
                pos.count(BLACK|BSHP),
                pos.count(BLACK|ROOK),
                pos.count(BLACK|QUEN),
                pos.pairedBishop(BLACK)
            }
        }};

        auto value = (computeImbalance<WHITE>(pieceCount)
                    - computeImbalance<BLACK>(pieceCount)) / 16; // Imbalance Resolution
        imbalance = makeScore(value, value);
    }

    /// Material::probe() looks up a current position's material configuration in the material hash table
    /// and returns a pointer to it if found, otherwise a new Entry is computed and stored there.
    Entry* probe(const Position &pos)
    {
        auto *e = pos.thread->matlHash[pos.matlKey()];

        if (e->key == pos.matlKey())
        {
            return e;
        }

        e->key = pos.matlKey();
        e->evaluate(pos);

        return e;
    }
}
