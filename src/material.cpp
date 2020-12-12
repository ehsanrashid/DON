#include "material.h"

#include <cassert>
#include <cstring> // For memset()

#include "thread.h"

namespace Material {

    namespace {

        #define S(mg, eg) makeScore(mg, eg)

        // Polynomial material imbalance parameters
        constexpr Score OwnQuadratic[PIECE_TYPES][PIECE_TYPES]{
            //            OUR PIECES
            // Bishop pair    Pawn         Knight      Bishop       Rook         Queen
            {S(1419, 1455)                                                                  }, // Bishop pair
            {S( 101,   28), S( 37,  39)                                                     }, // Pawn
            {S(  57,   64), S(249, 187), S(-49, -62)                                        }, // Knight      OUR PIECES
            {S(   0,    0), S(118, 137), S( 10,  27), S(  0,   0)                           }, // Bishop
            {S( -63,  -68), S( -5,   3), S(100,  81), S(132, 118), S(-246, -244)            }, // Rook
            {S(-210, -211), S( 37,  14), S(147, 141), S(161, 105), S(-158, -174), S(-9,-31) }  // Queen
        };
        constexpr Score OppQuadratic[PIECE_TYPES][PIECE_TYPES]{
            //           THEIR PIECES
            // Bishop pair    Pawn         Knight      Bishop       Rook         Queen
            {                                                                               }, // Bishop pair
            {S(  33,  30)                                                                   }, // Pawn
            {S(  46,  18), S(106,  84)                                                      }, // Knight      OUR PIECES
            {S(  75,  35), S( 59,  44), S( 60,  15)                                         }, // Bishop
            {S(  26,  35), S(  6,  22), S( 38,  39), S(-12,  -2)                            }, // Rook
            {S(  97,  93), S(100, 163), S(-58, -91), S(112, 192), S(276, 225)               }  // Queen
        };

        #undef S

        // Endgame evaluation and scaling functions are accessed direcly and not through
        // the function maps because they correspond to more than one material hash key.
        Endgame<KXK> ValueKXK[COLORS]{
            Endgame<KXK>(WHITE),
            Endgame<KXK>(BLACK)
        };
        // Endgame generic scaleFactor functions
        Endgame<KPsK> ScaleKPsK[COLORS]{ 
            Endgame<KPsK>(WHITE),
            Endgame<KPsK>(BLACK)
        };
        Endgame<KPKP> ScaleKPKP[COLORS]{
            Endgame<KPKP>(WHITE),
            Endgame<KPKP>(BLACK)
        };
        Endgame<KBPsK> ScaleKBPsK[COLORS]{
            Endgame<KBPsK>(WHITE),
            Endgame<KBPsK>(BLACK)
        };
        Endgame<KQKRPs> ScaleKQKRPs[COLORS]{
            Endgame<KQKRPs>(WHITE),
            Endgame<KQKRPs>(BLACK)
        };

        /// imbalance() calculates the imbalance by the piece count of each piece type for both colors.
        /// NOTE:: KING == BISHOP PAIR
        template<Color Own>
        Score computeImbalance(int32_t const pieceCount[][PIECE_TYPES]) {
            constexpr auto Opp{ ~Own };

            Score imbalance{ SCORE_ZERO };
            // "The Evaluation of Material Imbalances in Chess"
            // Second-degree polynomial material imbalance by Tord Romstad
            for (auto pt1 = NONE; pt1 <= QUEN; ++pt1) {
                if (pieceCount[Own][pt1] != 0) {
                    Score v{ SCORE_ZERO };
                    for (auto pt2 = NONE; pt2 < pt1; ++pt2) {
                        v += OwnQuadratic[pt1][pt2] * pieceCount[Own][pt2]
                           + OppQuadratic[pt1][pt2] * pieceCount[Opp][pt2];
                    }
                    imbalance += v * pieceCount[Own][pt1];
                }
            }
            return imbalance;
        }
    }

    void Entry::evaluate(Position const &pos) {

        // Calculates the phase interpolating total non-pawn material between endgame and midgame limits.
        phase = (int32_t(std::clamp(pos.nonPawnMaterial(), VALUE_ENDGAME, VALUE_MIDGAME) - VALUE_ENDGAME) * PhaseResolution)
               / int32_t(VALUE_MIDGAME - VALUE_ENDGAME);
        scaleFactor[WHITE] = scaleFactor[BLACK] = SCALE_NORMAL;

        // Let's look if have a specialized evaluation function for this particular material configuration.
        // First look for a fixed configuration one, then a generic one if previous search failed.
        if ((evaluatingFunc = EndGame::probe<Value>(key)) != nullptr) {
            return;
        }
        // Generic evaluation
        for (Color const c : { WHITE, BLACK }) {
            if (pos.nonPawnMaterial( c) >= VALUE_MG_ROOK
             && !moreThanOne(pos.pieces(~c))) {
                evaluatingFunc = &ValueKXK[c];
                return;
            }
        }

        // Didn't find any special evaluation function for the current
        // material configuration. Is there a suitable scaling function?
        auto const *scalingFn{ EndGame::probe<Scale>(key) };
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
            if (pos.count(B_PAWN) == 0) {
                assert(pos.count(W_PAWN) >= 2);
                scalingFunc[WHITE] = &ScaleKPsK[WHITE];
            } else
            if (pos.count(W_PAWN) == 0) {
                assert(pos.count(B_PAWN) >= 2);
                scalingFunc[BLACK] = &ScaleKPsK[BLACK];
            } else
            if (pos.count(W_PAWN) == 1
             && pos.count(B_PAWN) == 1) {
                // This is a special case so set scaling functions for both
                scalingFunc[WHITE] = &ScaleKPKP[WHITE];
                scalingFunc[BLACK] = &ScaleKPKP[BLACK];
            }
        } else {
            for (Color c : { WHITE, BLACK }) {

                if (pos.nonPawnMaterial( c) == VALUE_MG_QUEN
                 && pos.count( c|PAWN) == 0
                 && pos.count(~c|ROOK) == 1
                 && pos.count(~c|PAWN) >= 1) {
                    scalingFunc[c] = &ScaleKQKRPs[c];
                } else
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
        int32_t const pieceCount[COLORS][PIECE_TYPES]{
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

        imbalance = ( computeImbalance<WHITE>(pieceCount)
                    - computeImbalance<BLACK>(pieceCount)) / 16; // Imbalance Resolution
    }

    /// Material::probe() looks up a current position's material configuration in the material hash table
    /// and returns a pointer to it if found, otherwise a new Entry is computed and stored there.
    Entry* probe(Position const &pos) noexcept {
        Key const matlKey{ pos.matlKey() };
        auto *e{ pos.thread()->matlTable[matlKey] };

        if (e->key == matlKey) {
            return e;
        }

        std::memset(e, 0, sizeof(*e));
        e->key = matlKey;

        e->evaluate(pos);

        return e;
    }
}
