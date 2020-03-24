#include "Pawns.h"

#include <algorithm>
#include <cassert>

#include "BitBoard.h"
#include "Helper.h"
#include "Thread.h"

namespace Pawns {

    namespace {
        // Connected pawn bonus
        constexpr Array<i32, RANKS> Connected{ 0, 7, 8, 12, 29, 48, 86, 0 };

    #define S(mg, eg) makeScore(mg, eg)
        // Safety of friend pawns shelter for our king by [distance from edge][rank].
        // RANK_1 is used for files where we have no pawn, or pawn is behind our king.
        constexpr Array<Score, FILES/2, RANKS> Shelter
        {{
            { S( -6, 0), S( 81, 0), S( 93, 0), S( 58, 0), S( 39, 0), S( 18, 0), S(  25, 0), S(0, 0) },
            { S(-43, 0), S( 61, 0), S( 35, 0), S(-49, 0), S(-29, 0), S(-11, 0), S( -63, 0), S(0, 0) },
            { S(-10, 0), S( 75, 0), S( 23, 0), S( -2, 0), S( 32, 0), S(  3, 0), S( -45, 0), S(0, 0) },
            { S(-39, 0), S(-13, 0), S(-29, 0), S(-52, 0), S(-48, 0), S(-67, 0), S(-166, 0), S(0, 0) }
        }};

        // Danger of unblocked enemy pawns storm toward our king by [distance from edge][rank].
        // RANK_1 is used for files where the enemy has no pawn, or their pawn is behind our king.
        // [0][1 - 2] accommodate opponent pawn on edge (likely blocked by king)
        constexpr Array<Score, FILES/2, RANKS> UnblockedStorm
        {{
            { S( 85, 0), S(-289, 0), S(-166, 0), S( 97, 0), S( 50, 0), S( 45, 0), S( 50, 0), S(0, 0) },
            { S( 46, 0), S( -25, 0), S( 122, 0), S( 45, 0), S( 37, 0), S(-10, 0), S( 20, 0), S(0, 0) },
            { S( -6, 0), S(  51, 0), S( 168, 0), S( 34, 0), S( -2, 0), S(-22, 0), S(-14, 0), S(0, 0) },
            { S(-15, 0), S( -11, 0), S( 101, 0), S(  4, 0), S( 11, 0), S(-15, 0), S(-29, 0), S(0, 0) }
        }};

        constexpr Score BlockedStorm  { S(82,82) };

        constexpr Score Basic         { S( 5, 5) };
        constexpr Score Backward      { S( 9,24) };
        constexpr Score Isolated      { S( 5,15) };
        constexpr Score Unopposed     { S(13,27) };
        constexpr Score WeakDoubled   { S(11,56) };
        constexpr Score WeakTwiceLever{ S( 0,56) };

    #undef S

        /// evaluateSafetyOn() calculates shelter & storm for king,
        /// looking at the king file and the two closest files.
        template<Color Own>
        Score evaluateSafetyOn(Position const &pos, Square kSq) {
            constexpr auto Opp{ ~Own };

            Bitboard frontPawns{ ~frontRanksBB(Opp, kSq) & pos.pieces(PAWN) };
            Bitboard ownFrontPawns{ pos.pieces(Own) & frontPawns };
            Bitboard oppFrontPawns{ pos.pieces(Opp) & frontPawns };

            Score safety{ Basic };

            auto kF{ clamp(sFile(kSq), FILE_B, FILE_G) };
            for (File f = File(kF - 1); f <= File(kF + 1); ++f) {
                assert(FILE_A <= f && f <= FILE_H);
                Bitboard ownFrontFilePawns = ownFrontPawns & FileBB[f];
                auto ownR{ ownFrontFilePawns != 0 ?
                            relativeRank(Own, scanFrontMostSq(Opp, ownFrontFilePawns)) : RANK_1 };
                Bitboard oppFrontFilePawns = oppFrontPawns & FileBB[f];
                auto oppR{ oppFrontFilePawns != 0 ?
                            relativeRank(Own, scanFrontMostSq(Opp, oppFrontFilePawns)) : RANK_1 };
                assert((ownR != oppR)
                    || (ownR == RANK_1
                     && oppR == RANK_1));

                safety +=
                      Shelter[foldFile(f)][ownR]
                    - ((ownR != RANK_1)
                    && (ownR + 1) == oppR ?
                          BlockedStorm * (oppR == RANK_3) :
                          UnblockedStorm[foldFile(f)][oppR]);

            }

            return safety;
        }
        // Explicit template instantiations
        template Score evaluateSafetyOn<WHITE>(Position const&, Square);
        template Score evaluateSafetyOn<BLACK>(Position const&, Square);

    }

    i32 Entry::passedCount() const {
        return popCount(passPawns[WHITE] | passPawns[BLACK]);
    }

    /// Entry::evaluateKingSafety()
    template<Color Own>
    Score Entry::evaluateKingSafety(Position const &pos, Bitboard attacks) {

        auto kSq{ pos.square(Own|KING) };
        u08 cSide{ pos.canCastle(Own) ?
                u08(1 * (pos.canCastle(Own, CS_KING) && pos.castleExpeded(Own, CS_KING))
                  + 2 * (pos.canCastle(Own, CS_QUEN) && pos.castleExpeded(Own, CS_QUEN))) : u08(0) };

        if ((cSide & 1) != 0
         && (attacks & pos.castleKingPath(Own, CS_KING)) != 0) {
            cSide -= 1;
        }
        if ((cSide & 2) != 0
         && (attacks & pos.castleKingPath(Own, CS_QUEN)) != 0) {
            cSide -= 2;
        }

        if (kingSq[Own] != kSq
         || castleSide[Own] != cSide) {

            auto safety{ evaluateSafetyOn<Own>(pos, kSq) };

            if ((cSide & 1) != 0) {
                safety = std::max(evaluateSafetyOn<Own>(pos, relativeSq(Own, SQ_G1)), safety,
                        [](Score s1, Score s2) {
                            return mgValue(s1) < mgValue(s2);
                        });
            }
            if ((cSide & 2) != 0) {
                safety = std::max(evaluateSafetyOn<Own>(pos, relativeSq(Own, SQ_C1)), safety,
                        [](Score s1, Score s2) {
                            return mgValue(s1) < mgValue(s2);
                        });
            }

            kingSafety[Own] = safety;
            castleSide[Own] = cSide;

            if (kingSq[Own] != kSq) {
                // In endgame, king near to closest pawn
                i32 kDist{ 0 };
                Bitboard pawns{ pos.pieces(Own, PAWN) };
                if (pawns != 0) {
                    if ((pawns & PieceAttackBB[KING][kSq]) != 0) {
                        kDist = 1;
                    }
                    else {
                        kDist = 8;
                        while (pawns != 0) {
                            kDist = std::min(distance(kSq, popLSq(pawns)), kDist);
                        }
                    }
                }
                kingDist[Own] = makeScore(0, 16 * kDist);
            }

            kingSq[Own] = kSq;
        }

        return (kingSafety[Own] - kingDist[Own]);
    }
    // Explicit template instantiations
    template Score Entry::evaluateKingSafety<WHITE>(Position const&, Bitboard);
    template Score Entry::evaluateKingSafety<BLACK>(Position const&, Bitboard);

    /// Entry::evaluate()
    template<Color Own>
    void Entry::evaluate(Position const &pos) {
        constexpr auto Opp{ ~Own };
        constexpr auto Push{ PawnPush[Own] };

        Bitboard pawns{ pos.pieces(PAWN) };
        Bitboard ownPawns{ pos.pieces(Own) & pawns };
        Bitboard oppPawns{ pos.pieces(Opp) & pawns };

        kingSq     [Own] = SQ_NONE;
        //castleSide [Own] = 0;
        //kingSafety [Own] = SCORE_ZERO;
        //kingDist   [Own] = SCORE_ZERO;
        passPawns  [Own] = 0;
        score      [Own] = SCORE_ZERO;
        sglAttacks [Own] =
        attacksSpan[Own] = pawnSglAttackBB<Own>(ownPawns);
        dblAttacks [Own] = pawnDblAttackBB<Own>(ownPawns);
        for (Square s : pos.squares(Own|PAWN)) {
            assert(pos[s] == (Own|PAWN));

            auto r{ relativeRank(Own, s) };
            assert(RANK_2 <= r && r <= RANK_7);

            Bitboard neighbours { ownPawns & adjacentFilesBB(s) };
            Bitboard supporters { neighbours & rankBB(s - Push) };
            Bitboard phalanxes  { neighbours & rankBB(s) };
            Bitboard stoppers   { oppPawns & pawnPassSpan(Own, s) };
            Bitboard blocker    { stoppers & (s + Push) };
            Bitboard levers     { stoppers & PawnAttackBB[Own][s] };
            Bitboard sentres    { stoppers & PawnAttackBB[Own][s + Push] }; // push levers

            bool opposed { (stoppers & frontSquaresBB(Own, s)) != 0 };
            // Backward: A pawn is backward when it is behind all pawns of the same color
            // on the adjacent files and cannot be safely advanced.
            bool backward{ (neighbours & frontRanksBB(Opp, s + Push)) == 0
                        && (blocker | sentres) != 0 };

            // Compute additional span if pawn is not blocked nor backward
            if (!backward
             && !blocker) {
                attacksSpan[Own] |= pawnAttackSpan(Own, s); // + Push
            }

            // A pawn is passed if one of the three following conditions is true:
            // - Lever there is no stoppers except the levers
            // - Sentry there is no stoppers except the sentres, but we outnumber them
            // - Sneaker there is only one front stopper which can be levered.
            if (// Lever
                (stoppers == levers)
                // Lever + Sentry
             || (stoppers == (levers | sentres)
              && popCount(phalanxes) >= popCount(sentres))
                // Sneaker => Blocked pawn
             || (stoppers == blocker
              && r >= RANK_5
              && ( pawnSglPushBB<Own>(supporters)
                & ~(oppPawns | pawnDblAttackBB<Opp>(oppPawns))) != 0)) {
                // Passed pawns will be properly scored later in evaluation when we have full attack info.
                passPawns[Own] |= s;
            }

            Score sp{ SCORE_ZERO };

            if (supporters != 0
             || phalanxes != 0) {
                i32 v{ Connected[r] * (2 + (phalanxes != 0) - opposed)
                     + 21 * popCount(supporters) };
                sp += makeScore(v, v * (r - RANK_3) / 4);
            }
            else
            if (neighbours == 0) {
                sp -= Isolated
                    + Unopposed * !opposed;
            }
            else
            if (backward) {
                sp -= Backward
                    + Unopposed * !opposed;
            }

            if (supporters == 0) {
                sp -= WeakDoubled * contains(ownPawns, s - Push)
                    // Attacked twice by enemy pawns
                    + WeakTwiceLever * moreThanOne(levers);
            }

            score[Own] += sp;
        }
    }
    // Explicit template instantiations
    template void Entry::evaluate<WHITE>(Position const&);
    template void Entry::evaluate<BLACK>(Position const&);

    /// Pawns::probe() looks up a current position's pawn configuration in the pawn hash table
    /// and returns a pointer to it if found, otherwise a new Entry is computed and stored there.
    Entry* probe(Position const &pos) {
        Key pawnKey{ pos.pawnKey() };
        auto *e{ pos.thread()->pawnHash[pawnKey] };

        if (e->key == pawnKey) {
            return e;
        }

        e->key = pawnKey;
        e->evaluate<WHITE>(pos),
        e->evaluate<BLACK>(pos);

        return e;
    }

}
