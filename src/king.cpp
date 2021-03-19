#include "king.h"

#include <cassert>
#include <algorithm>

#include "bitboard.h"
#include "thread.h"
#include "zobrist.h"

namespace King {

    namespace {

        auto constexpr S = makeScore;

        // Safety of friend pawns shelter for our king by [distance from edge][rank].
        // RANK_1 is used for files where we have no pawn, or pawn is behind our king.
        constexpr Score Shelter[FILES/2][RANKS]{
            { S( -5, 0), S( 82, 0), S( 92, 0), S( 54, 0), S( 36, 0), S( 22, 0), S(  28, 0), S(0, 0) },
            { S(-44, 0), S( 63, 0), S( 33, 0), S(-50, 0), S(-30, 0), S(-12, 0), S( -62, 0), S(0, 0) },
            { S(-11, 0), S( 77, 0), S( 22, 0), S( -6, 0), S( 31, 0), S(  8, 0), S( -45, 0), S(0, 0) },
            { S(-39, 0), S(-12, 0), S(-29, 0), S(-50, 0), S(-43, 0), S(-68, 0), S(-164, 0), S(0, 0) }
        };

        // Danger of unblocked enemy pawns storm toward our king by [distance from edge][rank].
        // RANK_1 is used for files where the enemy has no pawn, or their pawn is behind our king.
        // [0][1 - 2] accommodate opponent pawn on edge (likely blocked by king)
        constexpr Score UnblockedStorm[FILES/2][RANKS]{
            { S( 87, 0), S(-288, 0), S(-168, 0), S( 96, 0), S( 47, 0), S( 44, 0), S( 46, 0), S(0, 0) },
            { S( 42, 0), S( -25, 0), S( 120, 0), S( 45, 0), S( 34, 0), S( -9, 0), S( 24, 0), S(0, 0) },
            { S( -8, 0), S(  51, 0), S( 167, 0), S( 35, 0), S( -4, 0), S(-16, 0), S(-12, 0), S(0, 0) },
            { S(-17, 0), S( -13, 0), S( 100, 0), S(  4, 0), S(  9, 0), S(-16, 0), S(-31, 0), S(0, 0) }
        };

        constexpr Score BlockedStorm[RANKS]{
            S(0, 0), S(0, 0), S(75, 78), S(-8, 16), S(-6, 10), S(-6, 6), S(0, 2), S(0, 0)
        };

        // KingOnFile[semi-open own][semi-open opp] contains bonuses/penalties
        // for king when the king is on a semi-open or open file.
        constexpr Score KingOnFile[2][2]{
            { S(-21, 10), S(-7,  1) },
            { S(  0, -3), S( 9, -4) }
        };

        constexpr Score BasicShelter{ S( 5, 5) };

    }

    template<Color Own>
    Score Entry::evaluateBonusOn(Position const &pos, Square kSq) noexcept {
        constexpr auto Opp{ ~Own };

        Bitboard const frontPawns   { pos.pieces(PAWN) & ~frontRanksBB(Opp, kSq) };
        Bitboard const ownFrontPawns{ pos.pieces(Own) & frontPawns & ~pawnEntry->sglAttacks[Opp] };
        Bitboard const oppFrontPawns{ pos.pieces(Opp) & frontPawns };

        Score bonus{ BasicShelter };

        auto kF{ std::clamp(sFile(kSq), FILE_B, FILE_G) };
        for (File f = File(kF - 1); f <= File(kF + 1); ++f) {
            assert(FILE_A <= f && f <= FILE_H);
            Bitboard const ownFrontFilePawns{ ownFrontPawns & fileBB(f) };
            auto const ownR{ ownFrontFilePawns != 0 ? relativeRank(Own, scanFrontMostSq<Opp>(ownFrontFilePawns)) : RANK_1 };
            Bitboard const oppFrontFilePawns{ oppFrontPawns & fileBB(f) };
            auto const oppR{ oppFrontFilePawns != 0 ? relativeRank(Own, scanFrontMostSq<Opp>(oppFrontFilePawns)) : RANK_1 };
            assert((ownR != oppR)
                || (ownR == RANK_1
                 && oppR == RANK_1));

            auto const d{ edgeDistance(f) };
            bonus +=
                Shelter[d][ownR]
              - (ownR != RANK_1
              && ownR == oppR - 1 ?
                    BlockedStorm[oppR] :
                    UnblockedStorm[d][oppR]);
        }

        // King On File
        bonus -= KingOnFile[pos.semiopenFileOn(Own, kSq)][pos.semiopenFileOn(Opp, kSq)];

        return bonus;
    }
    // Explicit template instantiations
    template Score Entry::evaluateBonusOn<WHITE>(Position const&, Square);
    template Score Entry::evaluateBonusOn<BLACK>(Position const&, Square);

    template<Color Own>
    Score Entry::evaluateSafety(Position const &pos, Bitboard attacks) noexcept {

        auto const sq{ pos.square(Own|KING) };
        bool const cSide[CASTLE_SIDES]{
            pos.canCastle(Own, CS_KING) && pos.castleExpeded(Own, CS_KING) && ((attacks & pos.castleKingPath(Own, CS_KING)) == 0),
            pos.canCastle(Own, CS_QUEN) && pos.castleExpeded(Own, CS_QUEN) && ((attacks & pos.castleKingPath(Own, CS_QUEN)) == 0)
        };

        if (square[Own] != sq
         || castleSide[Own][CS_KING] != cSide[CS_KING]
         || castleSide[Own][CS_QUEN] != cSide[CS_QUEN]) {

            auto const compare{ [](Score s1, Score s2) { return mgValue(s1) < mgValue(s2); } };

            auto bonus{ evaluateBonusOn<Own>(pos, sq) };
            if (cSide[CS_KING]) {
                bonus = std::max(bonus, evaluateBonusOn<Own>(pos, relativeSq(Own, SQ_G1)), compare);
            }
            if (cSide[CS_QUEN]) {
                bonus = std::max(bonus, evaluateBonusOn<Own>(pos, relativeSq(Own, SQ_C1)), compare);
            }

            // In endgame, king near to closest pawn
            int32_t minPawnDist{ 6 }; // Max can be 6 because pawn on last rank promotes
            Bitboard pawns{ pos.pieces(Own, PAWN) };
            if ((pawns & attacksBB(KING, sq)) != 0) {
                minPawnDist = 1;
            } else {
                while (pawns != 0
                    && minPawnDist != 2) {
                    minPawnDist = std::min(minPawnDist, distance(sq, popLSq(pawns)));
                }
            }
            safety[Own] = bonus - makeScore(0, 16 * minPawnDist);

            square[Own] = sq;
            castleSide[Own][CS_KING] = cSide[CS_KING];
            castleSide[Own][CS_QUEN] = cSide[CS_QUEN];
        }

        assert(square[Own] != SQ_NONE);
        return safety[Own];
    }
    // Explicit template instantiations
    template Score Entry::evaluateSafety<WHITE>(Position const&, Bitboard);
    template Score Entry::evaluateSafety<BLACK>(Position const&, Bitboard);

    Entry* probe(Position const &pos, Pawns::Entry *pe) noexcept {

        Key const kingKey{
            pe->key
          ^ RandZob.psq[W_KING][pos.square(W_KING)]
          ^ RandZob.psq[B_KING][pos.square(B_KING)] };

        auto *e{ pos.thread()->kingTable[kingKey] };

        if (e->key == kingKey) {
            assert(e->pawnEntry == pe);
            return e;
        }

        std::memset(e, 0, sizeof(*e));
        e->key = kingKey;
        e->pawnEntry = pe;

        e->outflanking = distance<File>(pos.square(W_KING), pos.square(B_KING))
                       + int(sRank(pos.square(W_KING))
                           - sRank(pos.square(B_KING)));
        e->infiltration = sRank(pos.square(W_KING)) > RANK_4
                       || sRank(pos.square(B_KING)) < RANK_5;

        e->square[WHITE] =
        e->square[BLACK] = SQ_NONE;
        return e;
    }

}
