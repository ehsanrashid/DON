#include "Pawns.h"

#include <algorithm>
#include <cassert>

#include "BitBoard.h"
#include "Thread.h"

namespace Pawns {

    namespace {
        // Connected pawn bonus
        constexpr i32 Connected[RANKS] { 0, 7, 8, 11, 24, 45, 85, 0 };

    #define S(mg, eg) makeScore(mg, eg)

        constexpr Score Backward       { S( 8,27) };
        constexpr Score Isolated       { S( 5,17) };
        constexpr Score Unopposed      { S(15,25) };
        constexpr Score WeakDoubled    { S(11,55) };
        constexpr Score WeakTwiceLever { S( 2,54) };

        // Bonus for blocked pawns at 5th or 6th rank
        constexpr Score BlockedPawn[2] { S(-13, -4), S(-4, 3) };

    #undef S

    }

    i32 Entry::blockedCount() const {
        return popCount(blockeds);
    }

    i32 Entry::passedCount() const {
        return popCount(passeds[WHITE] | passeds[BLACK]);
    }

    /// Entry::evaluate()
    template<Color Own>
    void Entry::evaluate(Position const &pos) {
        constexpr auto Opp{ ~Own };
        constexpr auto Push{ PawnPush[Own] };

        Bitboard pawns{ pos.pieces(PAWN) };
        Bitboard ownPawns{ pos.pieces(Own) & pawns };
        Bitboard oppPawns{ pos.pieces(Opp) & pawns };

        sglAttacks [Own] =
        attacksSpan[Own] = pawnSglAttackBB<Own>(ownPawns);
        dblAttacks [Opp] = pawnDblAttackBB<Opp>(oppPawns);
        blockeds |= ownPawns
                  & pawnSglPushBB<Opp>(oppPawns | dblAttacks[Opp]);

        passeds    [Own] = 0;
        score      [Own] = SCORE_ZERO;

        Square const *ps{ pos.squares(Own|PAWN) };
        Square s;
        while ((s = *ps++) != SQ_NONE) {
            assert(pos[s] == (Own|PAWN));

            auto r{ relativeRank(Own, s) };
            assert(RANK_2 <= r && r <= RANK_7);

            Bitboard neighbours { ownPawns & adjacentFilesBB(s) };
            Bitboard supporters { neighbours & rankBB(s - Push) };
            Bitboard phalanxes  { neighbours & rankBB(s) };
            Bitboard stoppers   { oppPawns & pawnPassSpan(Own, s) };
            Bitboard levers     { stoppers & pawnAttacksBB(Own, s) };
            Bitboard sentres    { stoppers & pawnAttacksBB(Own, s + Push) }; // push levers
            Bitboard opposers   { stoppers & frontSquaresBB(Own, s) };
            Bitboard blocker    { stoppers & (s + Push) };

            bool opposed { opposers != 0 };
            bool blocked { blocker != 0 };
            // Backward: A pawn is backward when it is behind all pawns of the same color
            // on the adjacent files and cannot be safely advanced.
            bool backward{ (neighbours & frontRanksBB(Opp, s + Push)) == 0
                        && (blocker | sentres) != 0 };

            // Compute additional span if pawn is not blocked nor backward
            if (!blocked
             && !backward) {
                attacksSpan[Own] |= pawnAttackSpan(Own, s);
            }

            // A pawn is passed if no forward friend pawn with
            // one of the three following conditions is true:
            // - Lever there is no stoppers except the levers
            // - Sentry there is no stoppers except the sentres, but we outnumber them
            // - Sneaker there is only one front stopper which can be levered.
            //   (Refined in Evaluation)
            if ((ownPawns & frontSquaresBB(Own, s)) == 0
             && (// Lever
                 (stoppers == levers)
                 // Lever + Sentry
              || (stoppers == (levers | sentres)
               && popCount(phalanxes) >= popCount(sentres))
                 // Sneaker => Blocked pawn
              || (stoppers == blocker
               && r >= RANK_5
               && ( pawnSglPushBB<Own>(supporters)
                 & ~(oppPawns
                   | dblAttacks[Opp])) != 0))) {
                // Passed pawns will be properly scored later in evaluation when we have full attack info.
                passeds[Own] |= s;
            }

            Score sp{ SCORE_ZERO };

            if (supporters != 0
             || phalanxes != 0) {
                i32 v{ Connected[r] * (2 + 1 * (phalanxes != 0) - 1 * opposed)
                     + 21 * popCount(supporters) };
                sp += makeScore(v, v * (r - RANK_3) / 4);
            }
            else
            if (neighbours == 0) {
                if (opposed
                 && (ownPawns & frontSquaresBB(Opp, s)) != 0
                 && (oppPawns & adjacentFilesBB(s)) == 0) {
                    sp -= WeakDoubled;
                }
                else {
                    sp -= Isolated
                        + Unopposed * !opposed;
                }
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

            if (blocked
             && r >= RANK_5) {
                sp += BlockedPawn[r - RANK_5];
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
        e->blockeds = 0;
        e->pawnNotBothFlank = (pos.pieces(PAWN) & SlotFileBB[CS_KING]) == 0
                           || (pos.pieces(PAWN) & SlotFileBB[CS_QUEN]) == 0;
        e->evaluate<WHITE>(pos);
        e->evaluate<BLACK>(pos);
        e->complexity = 12 * pos.count(PAWN)
                      +  9 * e->passedCount();
        return e;
    }

}
