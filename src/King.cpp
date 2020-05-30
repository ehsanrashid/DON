#include "King.h"

#include <algorithm>
#include <cassert>

#include "BitBoard.h"
#include "Thread.h"
#include "Zobrist.h"

namespace King {

    namespace {

    #define S(mg, eg) makeScore(mg, eg)
        // Safety of friend pawns shelter for our king by [distance from edge][rank].
        // RANK_1 is used for files where we have no pawn, or pawn is behind our king.
        constexpr Score Shelter[FILES/2][RANKS]
        {
            { S( -6, 0), S( 81, 0), S( 93, 0), S( 58, 0), S( 39, 0), S( 18, 0), S(  25, 0), S(0, 0) },
            { S(-43, 0), S( 61, 0), S( 35, 0), S(-49, 0), S(-29, 0), S(-11, 0), S( -63, 0), S(0, 0) },
            { S(-10, 0), S( 75, 0), S( 23, 0), S( -2, 0), S( 32, 0), S(  3, 0), S( -45, 0), S(0, 0) },
            { S(-39, 0), S(-13, 0), S(-29, 0), S(-52, 0), S(-48, 0), S(-67, 0), S(-166, 0), S(0, 0) }
        };

        // Danger of unblocked enemy pawns storm toward our king by [distance from edge][rank].
        // RANK_1 is used for files where the enemy has no pawn, or their pawn is behind our king.
        // [0][1 - 2] accommodate opponent pawn on edge (likely blocked by king)
        constexpr Score UnblockedStorm[FILES/2][RANKS]
        {
            { S( 85, 0), S(-289, 0), S(-166, 0), S( 97, 0), S( 50, 0), S( 45, 0), S( 50, 0), S(0, 0) },
            { S( 46, 0), S( -25, 0), S( 122, 0), S( 45, 0), S( 37, 0), S(-10, 0), S( 20, 0), S(0, 0) },
            { S( -6, 0), S(  51, 0), S( 168, 0), S( 34, 0), S( -2, 0), S(-22, 0), S(-14, 0), S(0, 0) },
            { S(-15, 0), S( -11, 0), S( 101, 0), S(  4, 0), S( 11, 0), S(-15, 0), S(-29, 0), S(0, 0) }
        };

        constexpr Score BlockedStorm[RANKS]
        { S( 0, 0), S( 0, 0), S( 76, 78), S(-10, 15), S(-7, 10), S(-4, 6), S(-1, 2), S( 0, 0) };

        constexpr Score BasicSafety { S( 5, 5) };

    #undef S

    }

    template<Color Own>
    Score Entry::evaluateSafetyOn(Position const &pos, Square kSq) {
        constexpr auto Opp{ ~Own };

        Bitboard frontPawns{ ~frontRanksBB(Opp, kSq) & pos.pieces(PAWN) };
        Bitboard ownFrontPawns{ pos.pieces(Own) & frontPawns & ~pawnEntry->sglAttacks[Opp] };
        Bitboard oppFrontPawns{ pos.pieces(Opp) & frontPawns };

        Score safety{ BasicSafety };

        auto kF{ clamp(sFile(kSq), FILE_B, FILE_G) };
        for (File f = File(kF - 1); f <= File(kF + 1); ++f) {
            assert(FILE_A <= f && f <= FILE_H);
            Bitboard ownFrontFilePawns{ ownFrontPawns & FileBB[f] };
            auto ownR{ ownFrontFilePawns != 0 ?
                relativeRank(Own, scanFrontMostSq<Opp>(ownFrontFilePawns)) : RANK_1 };
            Bitboard oppFrontFilePawns{ oppFrontPawns & FileBB[f] };
            auto oppR{ oppFrontFilePawns != 0 ?
                relativeRank(Own, scanFrontMostSq<Opp>(oppFrontFilePawns)) : RANK_1 };
            assert((ownR != oppR)
                || (ownR == RANK_1
                    && oppR == RANK_1));

            auto d{ edgeDistance(f) };
            safety +=
                Shelter[d][ownR]
                - (ownR > RANK_1
                && oppR == ownR + 1 ?
                    BlockedStorm[oppR] :
                    UnblockedStorm[d][oppR]);
        }

        return safety;
    }
    // Explicit template instantiations
    template Score Entry::evaluateSafetyOn<WHITE>(Position const&, Square);
    template Score Entry::evaluateSafetyOn<BLACK>(Position const&, Square);

    template<Color Own>
    Score Entry::evaluateSafety(Position const &pos, Bitboard attacks) {

        auto kSq{ pos.square(Own|KING) };

        u08 cSide{ u08(pos.canCastle(Own) ?
            + 1 * (pos.canCastle(Own, CS_KING) && pos.castleExpeded(Own, CS_KING))
            + 2 * (pos.canCastle(Own, CS_QUEN) && pos.castleExpeded(Own, CS_QUEN)) : 0) };

        if ((cSide & 1) != 0
         && (attacks & pos.castleKingPath(Own, CS_KING)) != 0) {
            cSide -= 1;
        }
        if ((cSide & 2) != 0
         && (attacks & pos.castleKingPath(Own, CS_QUEN)) != 0) {
            cSide -= 2;
        }

        if (castleSide[Own] != cSide) {

            auto safety{ evaluateSafetyOn<Own>(pos, kSq) };

            if ((cSide & 1) != 0) {
                safety = std::max(safety, evaluateSafetyOn<Own>(pos, relativeSq(Own, SQ_G1)),
                    [](Score s1, Score s2) {
                    return mgValue(s1) < mgValue(s2);
                });
            }
            if ((cSide & 2) != 0) {
                safety = std::max(safety, evaluateSafetyOn<Own>(pos, relativeSq(Own, SQ_C1)),
                    [](Score s1, Score s2) {
                    return mgValue(s1) < mgValue(s2);
                });
            }

            pawnSafety[Own] = safety;
            castleSide[Own] = cSide;
        }

        return (pawnSafety[Own] - pawnDist[Own]);
    }
    // Explicit template instantiations
    template Score Entry::evaluateSafety<WHITE>(Position const&, Bitboard);
    template Score Entry::evaluateSafety<BLACK>(Position const&, Bitboard);

    template<Color Own>
    void Entry::evaluate(Position const &pos) {

        auto kSq{ pos.square(Own|KING) };

        // In endgame, king near to closest pawn
        i32 dist{ 7 };
        Bitboard pawns{ pos.pieces(Own, PAWN) };
        if (pawns != 0) {
            dist = 1;
            Bitboard b{ PieceAttacksBB[KING][kSq] };
            while ((pawns & b) == 0) {
                ++dist;
                b = floodFill(b);
            }
            assert(dist <= 6);
        }
        pawnDist[Own] = makeScore(0, 16 * dist);
        castleSide[Own] = 4;
    }


    Entry* probe(Position const &pos, Pawns::Entry* pe) {
        Key kingKey{ pos.pawnKey()
                   ^ RandZob.psq[W_KING][pos.square(W_KING)]
                   ^ RandZob.psq[B_KING][pos.square(B_KING)] };

        auto *e{ pos.thread()->kingHash[kingKey] };

        if (e->key == kingKey) {
            return e;
        }

        e->key = kingKey;
        e->pawnEntry = pe;
        e->evaluate<WHITE>(pos),
        e->evaluate<BLACK>(pos);
        return e;
    }

}

