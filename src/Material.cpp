#include "Material.h"

#include <cassert>
#include <cstring> // For std::memset

#include "Helper.h"
#include "Thread.h"

namespace Material {

    namespace {

        // Polynomial material imbalance parameters
        constexpr i32 OwnQuadratic[PIECE_TYPES][PIECE_TYPES]
        {
            // BP    P    N    B    R    Q
            {1438                          }, // BP
            {  40,  38                     }, // P
            {  32, 255, -62                }, // N
            {   0, 104,   4,   0           }, // B
            { -26,  -2,  47, 105,-208      }, // R
            {-189,  24, 117, 133,-134,  -6 }, // Q
            {  0,    0,   0,   0,   0,   0 }  // K
        };
        constexpr i32 OppQuadratic[PIECE_TYPES][PIECE_TYPES]
        {
            // BP    P    N    B    R    Q
            {   0                          }, // BP
            {  36,   0                     }, // P
            {   9,  63,   0                }, // N
            {  59,  65,  42,   0           }, // B
            {  46,  39,  24, -24,   0      }, // R
            {  97, 100, -42, 137, 268,   0 }, // Q
            {  0,    0,   0,   0,   0,   0 }  // K
        };

        // Endgame evaluation and scaling functions are accessed direcly and not through
        // the function maps because they correspond to more than one material hash key.
        Endgame<KXK> ValueKXK[COLORS]
        {
            Endgame<KXK>(WHITE),
            Endgame<KXK>(BLACK)
        };
        // Endgame generic scaleFactor functions
        Endgame<KPsK> ScaleKPsK[COLORS]
        { 
            Endgame<KPsK>(WHITE),
            Endgame<KPsK>(BLACK)
        };
        Endgame<KPKP> ScaleKPKP[COLORS]
        {
            Endgame<KPKP>(WHITE),
            Endgame<KPKP>(BLACK)
        };
        Endgame<KBPsK> ScaleKBPsK[COLORS]
        {
            Endgame<KBPsK>(WHITE),
            Endgame<KBPsK>(BLACK)
        };
        Endgame<KQKRPs> ScaleKQKRPs[COLORS]
        {
            Endgame<KQKRPs>(WHITE),
            Endgame<KQKRPs>(BLACK)
        };

        /// imbalance() calculates the imbalance by the piece count of each piece type for both colors.
        /// NOTE:: KING == BISHOP PAIR
        template<Color Own>
        i32 computeImbalance(const int pieceCount[][PIECE_TYPES]) {
            constexpr auto Opp{ ~Own };

            i32 imbalance{ 0 };
            // "The Evaluation of Material Imbalances in Chess"
            // Second-degree polynomial material imbalance by Tord Romstad
            for (PieceType pt1 = NONE; pt1 <= QUEN; ++pt1) {
                if (pieceCount[Own][pt1] != 0) {
                    i32 v{ 0 };
                    for (PieceType pt2 = NONE; pt2 <= pt1; ++pt2) {
                        v += pieceCount[Own][pt2] * OwnQuadratic[pt1][pt2]
                           + pieceCount[Opp][pt2] * OppQuadratic[pt1][pt2];
                    }
                    imbalance += pieceCount[Own][pt1] * v;
                }
            }
            return imbalance;
        }
    }

    void Entry::evaluate(Position const &pos) {

        // Calculates the phase interpolating total non-pawn material between endgame and midgame limits.
        phase = (i32(clamp(pos.nonPawnMaterial(), VALUE_ENDGAME, VALUE_MIDGAME) - VALUE_ENDGAME) * PhaseResolution)
              / i32(VALUE_MIDGAME - VALUE_ENDGAME);
        scaleFactor[WHITE] = scaleFactor[BLACK] = SCALE_NORMAL;

        // Let's look if have a specialized evaluation function for this particular material configuration.
        // First look for a fixed configuration one, then a generic one if previous search failed.
        if ((evaluationFunc = EndGame::probe<Value>(pos.matlKey())) != nullptr) {
            return;
        }
        // Generic evaluation
        for (Color c : { WHITE, BLACK }) {
            if (pos.nonPawnMaterial( c) >= VALUE_MG_ROOK
             && !moreThanOne(pos.pieces(~c))) {
                evaluationFunc = &ValueKXK[c];
                return;
            }
        }

        // Didn't find any special evaluation function for the current
        // material configuration. Is there a suitable scaling function?
        auto const *scalingFn{ EndGame::probe<Scale>(pos.matlKey()) };
        if (scalingFn != nullptr) {
            scalingFunc[scalingFn->stngColor] = scalingFn; // Only strong color assigned
            return;
        }

        // Didn't find any specialized scaling function, so fall back on
        // generic scaling functions that refer to more than one material distribution.
        // Note that in this case don't return after setting the function.

        // Only pawns left
        if (pos.nonPawnMaterial() == VALUE_ZERO
         && pos.pieces(PAWN) != 0) {
            if (pos.count(W_PAWN) == 0) {
                assert(pos.count(B_PAWN) >= 2);
                scalingFunc[WHITE] = &ScaleKPsK[WHITE];
            }
            else
            if (pos.count(B_PAWN) == 0) {
                assert(pos.count(W_PAWN) >= 2);
                scalingFunc[BLACK] = &ScaleKPsK[BLACK];
            }
            else
            if (pos.count(W_PAWN) == 1
             && pos.count(B_PAWN) == 1) {
                // This is a special case so set scaling functions for both
                scalingFunc[WHITE] = &ScaleKPKP[WHITE];
                scalingFunc[BLACK] = &ScaleKPKP[BLACK];
            }
        }
        else {
            for (Color c : { WHITE, BLACK }) {
                if (pos.nonPawnMaterial( c) == VALUE_MG_QUEN
                 && pos.count( c|PAWN) == 0
                 && pos.count(~c|ROOK) == 1
                 && pos.count(~c|PAWN) >= 1) {
                    scalingFunc[c] = &ScaleKQKRPs[c];
                }
                else
                if (pos.nonPawnMaterial( c) == VALUE_MG_BSHP
                 && pos.count( c|PAWN) >= 1) {
                    scalingFunc[c] = &ScaleKBPsK[c];
                }
            }
        }

        for (Color c : { WHITE, BLACK }) {

            // Zero or just one pawn makes it difficult to win, even with a material advantage.
            // This catches some trivial draws like KK, KBK and KNK and gives a very drawish
            // scaleFactor for cases such as KRKBP and KmmKm (except for KBBKN).
            if (pos.count(c|PAWN) == 0
             && (pos.nonPawnMaterial( c) - pos.nonPawnMaterial(~c)) <= VALUE_MG_BSHP) {
                scaleFactor[c] = Scale((14 - 10 * (pos.nonPawnMaterial(~c) <= VALUE_MG_BSHP)) * (pos.nonPawnMaterial( c) >= VALUE_MG_ROOK));
            }
        }

        // Evaluate the material imbalance.
        // Use KING as a place holder for the bishop pair "extended piece",
        // this allow us to be more flexible in defining bishop pair bonuses.
        i32 pieceCount[COLORS][PIECE_TYPES]
        {
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
        };

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

        std::memset(e, 0, sizeof(*e));
        e->key = matlKey;
        e->evaluate(pos);

        return e;
    }
}
