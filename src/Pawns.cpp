#include "Pawns.h"

#include <algorithm>
#include <cassert>

#include "BitBoard.h"
#include "Helper.h"
#include "Table.h"
#include "Thread.h"

using namespace std;

namespace Pawns {

    namespace {
        // Connected pawn bonus
        constexpr Array<i32, RANKS> Connected{ 0, 7, 8, 12, 29, 48, 86, 0 };

#   define S(mg, eg) makeScore(mg, eg)
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
        constexpr Array<Score, FILES/2, RANKS> Storm
        {{
            { S( 85, 0), S(-289, 0), S(-166, 0), S( 97, 0), S( 50, 0), S( 45, 0), S( 50, 0), S(0, 0) },
            { S( 46, 0), S( -25, 0), S( 122, 0), S( 45, 0), S( 37, 0), S(-10, 0), S( 20, 0), S(0, 0) },
            { S( -6, 0), S(  51, 0), S( 168, 0), S( 34, 0), S( -2, 0), S(-22, 0), S(-14, 0), S(0, 0) },
            { S(-15, 0), S( -11, 0), S( 101, 0), S(  4, 0), S( 11, 0), S(-15, 0), S(-29, 0), S(0, 0) }
        }};

        constexpr Score BlockedStorm  { S(82,82) };

        constexpr Score Initial       { S( 5, 5) };
        constexpr Score Backward      { S( 9,24) };
        constexpr Score Isolated      { S( 5,15) };
        constexpr Score Unopposed     { S(13,27) };
        constexpr Score WeakDoubled   { S(11,56) };
        constexpr Score WeakTwiceLever{ S( 0,56) };

#   undef S

        /// evaluateSafetyOn() calculates shelter & storm for king,
        /// looking at the king file and the two closest files.
        template<Color Own>
        Score evaluateSafetyOn(const Position &pos, Square kSq)
        {
            constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

            Bitboard frontPawns = ~frontRanksBB(Opp, kSq) & pos.pieces(PAWN);
            Bitboard ownFrontPawns = pos.pieces(Own) & frontPawns;
            Bitboard oppFrontPawns = pos.pieces(Opp) & frontPawns;

            Score safety = Initial;

            auto kF = clamp(sFile(kSq), FILE_B, FILE_G);
            for (File f = File(kF - 1); f <= File(kF + 1); ++f)
            {
                assert(FILE_A <= f && f <= FILE_H);
                Bitboard ownFrontFilePawns = ownFrontPawns & fileBB(f);
                auto ownR = 0 != ownFrontFilePawns ?
                            relativeRank(Own, scanFrontMostSq(Opp, ownFrontFilePawns)) : RANK_1;
                Bitboard oppFrontFilePawns = oppFrontPawns & fileBB(f);
                auto oppR = 0 != oppFrontFilePawns ?
                            relativeRank(Own, scanFrontMostSq(Opp, oppFrontFilePawns)) : RANK_1;
                assert((ownR != oppR)
                    || (RANK_1 == ownR
                     && RANK_1 == oppR));

                auto ff = mapFile(f);
                assert(FILE_E > ff);

                safety += Shelter[ff][ownR];
                if (RANK_1 != ownR
                 && (ownR + 1) == oppR)
                {
                    safety -= BlockedStorm * (RANK_3 == oppR);
                }
                else
                {
                    safety -= Storm[ff][oppR];
                }
            }

            return safety;
        }
        // Explicit template instantiations
        template Score evaluateSafetyOn<WHITE>(const Position&, Square);
        template Score evaluateSafetyOn<BLACK>(const Position&, Square);

    }

    /// Entry::evaluateKingSafety()
    template<Color Own>
    Score Entry::evaluateKingSafety(const Position &pos, Bitboard attacks)
    {
        auto kSq = pos.square(Own|KING);

        // Find King path
        Array<Bitboard, CASTLE_SIDES> kPaths
        {
            pos.castleKingPath[Own][CS_KING] * (pos.canCastle(Own, CS_KING) && pos.castleExpeded(Own, CS_KING)),
            pos.castleKingPath[Own][CS_QUEN] * (pos.canCastle(Own, CS_QUEN) && pos.castleExpeded(Own, CS_QUEN))
        };
        if (0 != (kPaths[CS_KING] & attacks))
        {
            kPaths[CS_KING] = 0;
        }
        if (0 != (kPaths[CS_QUEN] & attacks))
        {
            kPaths[CS_QUEN] = 0;
        }

        Bitboard kPath = kPaths[CS_KING]
                       | kPaths[CS_QUEN];

        if (kingSq[Own]   != kSq
         || kingPath[Own] != kPath)
        {
            auto safety = evaluateSafetyOn<Own>(pos, kSq);

            if (0 != kPaths[CS_KING])
            {
                safety = std::max(evaluateSafetyOn<Own>(pos, relativeSq(Own, SQ_G1)), safety,
                                  [](Score s1, Score s2) { return mgValue(s1) < mgValue(s2); });
            }
            if (0 != kPaths[CS_QUEN])
            {
                safety = std::max(evaluateSafetyOn<Own>(pos, relativeSq(Own, SQ_C1)), safety,
                                  [](Score s1, Score s2) { return mgValue(s1) < mgValue(s2); });
            }

            kingSafety[Own] = safety;

            if (kingSq[Own] != kSq)
            {
                // In endgame, king near to closest pawn
                i32 kDist = 0;
                Bitboard pawns = pos.pieces(Own, PAWN);
                if (0 != pawns)
                {
                    if (0 != (pawns & PieceAttacks[KING][kSq]))
                    {
                        kDist = 1;
                    }
                    else
                    {
                        kDist = 8;
                        while (0 != pawns)
                        {
                            kDist = std::min(dist(kSq, popLSq(pawns)), kDist);
                        }
                    }
                }
                kingDist[Own] = makeScore(0, 16 * kDist);
            }

            kingSq[Own] = kSq;
            kingPath[Own] = kPath;
        }

        return (kingSafety[Own] - kingDist[Own]);
    }
    // Explicit template instantiations
    template Score Entry::evaluateKingSafety<WHITE>(const Position&, Bitboard);
    template Score Entry::evaluateKingSafety<BLACK>(const Position&, Bitboard);

    /// Entry::evaluate()
    template<Color Own>
    void Entry::evaluate(const Position &pos)
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;
        constexpr auto Push = pawnPush(Own);
        const auto Attack = PawnAttacks[Own];

        Bitboard pawns = pos.pieces(PAWN);
        Bitboard ownPawns = pos.pieces(Own) & pawns;
        Bitboard oppPawns = pos.pieces(Opp) & pawns;

        kingSq    [Own] = SQ_NONE;
        //kingPath  [Own] = 0;
        //kingSafety[Own] = SCORE_ZERO;
        //kingDist  [Own] = SCORE_ZERO;
        attackSpan[Own] = pawnSglAttacks<Own>(ownPawns);
        passers   [Own] = 0;
        score     [Own] = SCORE_ZERO;
        for (Square s : pos.squares[Own|PAWN])
        {
            assert((Own|PAWN) == pos[s]);

            auto r{ relativeRank(Own, s) };
            assert(RANK_2 <= r && r <= RANK_7);

            Bitboard neighbours = ownPawns & adjacentFilesBB(s);
            Bitboard supporters = neighbours & rankBB(s - Push);
            Bitboard phalanxes  = neighbours & rankBB(s);
            Bitboard stoppers   = oppPawns & pawnPassSpan(Own, s);
            Bitboard blockers   = stoppers & (s + Push);
            Bitboard levers     = stoppers & Attack[s];
            Bitboard escapes    = stoppers & Attack[s + Push]; // Push levers

            bool opposed  = 0 != (stoppers & frontSquaresBB(Own, s));
            // Backward: A pawn is backward when it is behind all pawns of the same color
            // on the adjacent files and cannot be safely advanced.
            bool backward = 0 == (neighbours & frontRanksBB(Opp, s + Push))
                         && 0 != (blockers | escapes);

            // Compute additional span if pawn is not blocked nor backward
            if (0 == blockers
             && !backward)
            {
                attackSpan[Own] |= pawnAttackSpan(Own, s);
            }

            // A pawn is passed if one of the three following conditions is true:
            // - there is no stoppers except the levers
            // - there is no stoppers except the escapes, but we outnumber them
            // - there is only one front stopper which can be levered.
            // Passed pawns will be properly scored later in evaluation when we have full attack info.
            if ((stoppers == levers) // Also handles 0 == stoppers
             || (stoppers == (levers | escapes)
              && popCount(phalanxes) >= popCount(escapes))
             || (stoppers == blockers
              && RANK_4 < r
              && 0 != ( pawnSglPushes(Own, supporters)
                     & ~(oppPawns | pawnDblAttacks<Opp>(oppPawns)))))
            {
                passers[Own] |= s;
            }

            Score sp{SCORE_ZERO};

            if (0 != supporters
             || 0 != phalanxes)
            {
                i32 v{ Connected[r] * (2 + (0 != phalanxes) - opposed)
                     + 21 * popCount(supporters) };
                sp += makeScore(v, v * (r - RANK_3) / 4);
            }
            else
            if (0 == neighbours)
            {
                sp -= Isolated
                    + Unopposed * !opposed;
            }
            else
            if (backward)
            {
                sp -= Backward
                    + Unopposed * !opposed;
            }

            if (0 == supporters)
            {
                sp -= WeakDoubled * contains(ownPawns, s - Push)
                        // Attacked twice by enemy pawns
                    + WeakTwiceLever * moreThanOne(levers);
            }

            score[Own] += sp;
        }
    }
    // Explicit template instantiations
    template void Entry::evaluate<WHITE>(const Position&);
    template void Entry::evaluate<BLACK>(const Position&);

    /// Pawns::probe() looks up a current position's pawn configuration in the pawn hash table
    /// and returns a pointer to it if found, otherwise a new Entry is computed and stored there.
    Entry* probe(const Position &pos)
    {
        Key pawnKey = pos.pawnKey();
        auto *e = pos.thread->pawnHash[pawnKey];

        if (e->key == pawnKey)
        {
            return e;
        }

        e->key = pawnKey;
        e->evaluate<WHITE>(pos),
        e->evaluate<BLACK>(pos);

        return e;
    }
}
