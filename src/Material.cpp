#include "Material.h"

#include <cassert>

#include "Helper.h"
#include "Thread.h"

namespace Material {

    namespace {

        // Polynomial material imbalance parameters
        constexpr Array<i32, PIECE_TYPES, PIECE_TYPES> OwnQuadratic
        {{
            // BP    P    N    B    R    Q
            {1438                          }, // BP
            {  40,  38                     }, // P
            {  32, 255, -62                }, // N
            {   0, 104,   4,   0           }, // B
            { -26,  -2,  47, 105,-208      }, // R
            {-189,  24, 117, 133,-134,  -6 }, // Q
            {  0,    0,   0,   0,   0,   0 }  // K
        }};
        constexpr Array<i32, PIECE_TYPES, PIECE_TYPES> OppQuadratic
        {{
            // BP    P    N    B    R    Q
            {   0                          }, // BP
            {  36,   0                     }, // P
            {   9,  63,   0                }, // N
            {  59,  65,  42,   0           }, // B
            {  46,  39,  24, -24,   0      }, // R
            {  97, 100, -42, 137, 268,   0 }, // Q
            {  0,    0,   0,   0,   0,   0 }  // K
        }};

        // Endgame evaluation and scaling functions are accessed direcly and not through
        // the function maps because they correspond to more than one material hash key.
        Array<Endgame<KXK>, COLORS> ValueKXK
        {
            Endgame<KXK>(WHITE),
            Endgame<KXK>(BLACK)
        };
        // Endgame generic scale functions
        Array<Endgame<KPKP>, COLORS> ScaleKPKP
        {
            Endgame<KPKP>(WHITE),
            Endgame<KPKP>(BLACK)
        };
        Array<Endgame<KPsK>, COLORS> ScaleKPsK
        {
            Endgame<KPsK>(WHITE),
            Endgame<KPsK>(BLACK)
        };
        Array<Endgame<KBPsK>, COLORS> ScaleKBPsK
        {
            Endgame<KBPsK>(WHITE),
            Endgame<KBPsK>(BLACK)
        };
        Array<Endgame<KQKRPs>, COLORS> ScaleKQKRPs
        {
            Endgame<KQKRPs>(WHITE),
            Endgame<KQKRPs>(BLACK)
        };

        /// imbalance() calculates the imbalance by the piece count of each piece type for both colors.
        /// NOTE:: KING == BISHOP PAIR
        template<Color Own>
        i32 computeImbalance(Array<i32, COLORS, PIECE_TYPES> const &count) {
            constexpr auto Opp{ ~Own };

            i32 value{ 0 };
            // "The Evaluation of Material Imbalances in Chess"
            // Second-degree polynomial material imbalance by Tord Romstad
            for (PieceType pt1 = NONE; pt1 <= QUEN; ++pt1) {
                if (0 != count[Own][pt1]) {
                    i32 v{ 0 };
                    for (PieceType pt2 = NONE; pt2 <= pt1; ++pt2) {
                        v += count[Own][pt2] * OwnQuadratic[pt1][pt2]
                           + count[Opp][pt2] * OppQuadratic[pt1][pt2];
                    }
                    value += count[Own][pt1] * v;
                }
            }
            return value;
        }
    }

    void Entry::evaluate(Position const &pos) {

        Array<Value, COLORS> npm
        {
            pos.nonPawnMaterial(WHITE),
            pos.nonPawnMaterial(BLACK)
        };

        // Calculates the phase interpolating total non-pawn material between endgame and midgame limits.
        phase = (i32(clamp(npm[WHITE] + npm[BLACK], VALUE_ENDGAME, VALUE_MIDGAME) - VALUE_ENDGAME) * PhaseResolution)
              / i32(VALUE_MIDGAME - VALUE_ENDGAME);
        imbalance = SCORE_ZERO;
        scale.fill(SCALE_NORMAL);
        scalingFunc.fill(nullptr);

        // Let's look if have a specialized evaluation function for this
        // particular material configuration. First look for a fixed
        // configuration one, then a generic one if previous search failed.
        evaluationFunc = EndGame::probe<Value>(pos.matlKey());
        if (nullptr != evaluationFunc) {
            return;
        }
        // Generic evaluation
        for (Color c : { WHITE, BLACK }) {
            if (npm[ c] >= VALUE_MG_ROOK
             && pos.count(~c) == 1) {
                evaluationFunc = &ValueKXK[c];
                return;
            }
        }

        // Didn't find any special evaluation function for the current
        // material configuration. Is there a suitable scaling function?
        //
        // Face problems when there are several conflicting applicable
        // scaling functions and need to decide which one to use.
        auto const *scalingFn{ EndGame::probe<Scale>(pos.matlKey()) };
        if (nullptr != scalingFn) {
            scalingFunc[scalingFn->stngColor] = scalingFn;
            return;
        }

        // Didn't find any specialized scaling function, so fall back on
        // generic scaling functions that refer to more than one material distribution.
        for (Color c : { WHITE, BLACK }) {

            if (npm[ c] == VALUE_MG_BSHP
             //&& pos.count( c|BSHP) == 1
             && pos.count( c|PAWN) != 0) {
                scalingFunc[c] = &ScaleKBPsK[c];
            }
            else
            if (npm[ c] == VALUE_MG_QUEN
             //&& pos.count( c|QUEN) == 1
             && pos.count( c|PAWN) == 0
             && npm[~c] == VALUE_MG_ROOK
             //&& pos.count(~c|ROOK) == 1
             && pos.count(~c|PAWN) != 0) {
                scalingFunc[c] = &ScaleKQKRPs[c];
            }

            // Zero or just one pawn makes it difficult to win, even with a material advantage.
            // This catches some trivial draws like KK, KBK and KNK and gives a very drawish
            // scale for cases such as KRKBP and KmmKm (except for KBBKN).
            if (pos.count( c|PAWN) == 0
             && npm[ c] - npm[~c] <= VALUE_MG_BSHP) {
                scale[c] =
                    npm[ c] < VALUE_MG_ROOK ?
                        SCALE_DRAW :
                        Scale(14 - 10 * (npm[~c] <= VALUE_MG_BSHP));
            }
        }

        // Only pawns left
        if (npm[WHITE] + npm[BLACK] == VALUE_ZERO
         && pos.pieces(PAWN) != 0) {
            if (pos.pieces(BLACK, PAWN) == 0) {
                assert(2 <= pos.count(W_PAWN));
                scalingFunc[WHITE] = &ScaleKPsK[WHITE];
            }
            else
            if (pos.pieces(WHITE, PAWN) == 0) {
                assert(2 <= pos.count(B_PAWN));
                scalingFunc[BLACK] = &ScaleKPsK[BLACK];
            }
            else
            if (pos.count(W_PAWN) == 1
             && pos.count(B_PAWN) == 1) {
                scalingFunc[WHITE] = &ScaleKPKP[WHITE];
                scalingFunc[BLACK] = &ScaleKPKP[BLACK];
            }
        }

        // Evaluate the material imbalance.
        // Use KING as a place holder for the bishop pair "extended piece",
        // this allow us to be more flexible in defining bishop pair bonuses.
        Array<i32, COLORS, PIECE_TYPES> pieceCount
        {{
            {
                pos.bishopPaired(WHITE),
                pos.count(W_PAWN), pos.count(W_NIHT),
                pos.count(W_BSHP), pos.count(W_ROOK),
                pos.count(W_QUEN), pos.count(W_KING)
            },
            {
                pos.bishopPaired(BLACK),
                pos.count(B_PAWN), pos.count(B_NIHT),
                pos.count(B_BSHP), pos.count(B_ROOK),
                pos.count(B_QUEN), pos.count(B_KING)
            }
        }};

        auto value{ (computeImbalance<WHITE>(pieceCount)
                   - computeImbalance<BLACK>(pieceCount)) / 16 }; // Imbalance Resolution
        imbalance = makeScore(value, value);
    }

    /// Material::probe() looks up a current position's material configuration in the material hash table
    /// and returns a pointer to it if found, otherwise a new Entry is computed and stored there.
    Entry* probe(Position const &pos) {
        Key matlKey{ pos.matlKey() };
        auto *e{ pos.thread()->matlHash[matlKey] };

        if (e->key == matlKey) {
            return e;
        }

        e->key = matlKey;
        e->evaluate(pos);

        return e;
    }
}
