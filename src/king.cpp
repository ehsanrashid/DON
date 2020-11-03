#include "king.h"

#include <cassert>
#include <algorithm>

#include "bitboard.h"
#include "thread.h"
#include "zobrist.h"

namespace King {

    namespace {

    #define S(mg, eg) makeScore(mg, eg)
        // Safety of friend pawns shelter for our king by [distance from edge][rank].
        // RANK_1 is used for files where we have no pawn, or pawn is behind our king.
        constexpr Score Shelter[FILES/2][RANKS]{
            { S( -6, 0), S( 81, 0), S( 93, 0), S( 58, 0), S( 39, 0), S( 18, 0), S(  25, 0), S(0, 0) },
            { S(-43, 0), S( 61, 0), S( 35, 0), S(-49, 0), S(-29, 0), S(-11, 0), S( -63, 0), S(0, 0) },
            { S(-10, 0), S( 75, 0), S( 23, 0), S( -2, 0), S( 32, 0), S(  3, 0), S( -45, 0), S(0, 0) },
            { S(-39, 0), S(-13, 0), S(-29, 0), S(-52, 0), S(-48, 0), S(-67, 0), S(-166, 0), S(0, 0) }
        };

        // Danger of unblocked enemy pawns storm toward our king by [distance from edge][rank].
        // RANK_1 is used for files where the enemy has no pawn, or their pawn is behind our king.
        // [0][1 - 2] accommodate opponent pawn on edge (likely blocked by king)
        constexpr Score UnblockedStorm[FILES/2][RANKS]{
            { S( 85, 0), S(-289, 0), S(-166, 0), S( 97, 0), S( 50, 0), S( 45, 0), S( 50, 0), S(0, 0) },
            { S( 46, 0), S( -25, 0), S( 122, 0), S( 45, 0), S( 37, 0), S(-10, 0), S( 20, 0), S(0, 0) },
            { S( -6, 0), S(  51, 0), S( 168, 0), S( 34, 0), S( -2, 0), S(-22, 0), S(-14, 0), S(0, 0) },
            { S(-15, 0), S( -11, 0), S( 101, 0), S(  4, 0), S( 11, 0), S(-15, 0), S(-29, 0), S(0, 0) }
        };

        constexpr Score BlockedStorm[RANKS]{
            S( 0, 0), S( 0, 0), S( 76, 78), S(-10, 15), S(-7, 10), S(-4, 6), S(-1, 2), S( 0, 0)
        };

        constexpr Score BasicShelter { S( 5, 5) };

    #undef S

    }

    template<Color Own>
    Score Entry::evaluateShelterOn(Position const &pos, Square kSq) noexcept {
        constexpr auto Opp{ ~Own };

        Bitboard const frontPawns   { pos.pieces(PAWN) & ~frontRanksBB(Opp, kSq) };
        Bitboard const ownFrontPawns{ pos.pieces(Own) & frontPawns & ~pawnEntry->sglAttacks[Opp] };
        Bitboard const oppFrontPawns{ pos.pieces(Opp) & frontPawns };

        Score shelter{ BasicShelter };

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
            shelter +=
                Shelter[d][ownR]
              - (ownR != RANK_1
              && ownR == oppR - 1 ?
                    BlockedStorm[oppR] :
                    UnblockedStorm[d][oppR]);
        }

        return shelter;
    }
    // Explicit template instantiations
    template Score Entry::evaluateShelterOn<WHITE>(Position const&, Square);
    template Score Entry::evaluateShelterOn<BLACK>(Position const&, Square);

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

            auto shelter{ evaluateShelterOn<Own>(pos, sq) };
            if (cSide[CS_KING]) {
                shelter = std::max(shelter, evaluateShelterOn<Own>(pos, relativeSq(Own, SQ_G1)), compare);
            }
            if (cSide[CS_QUEN]) {
                shelter = std::max(shelter, evaluateShelterOn<Own>(pos, relativeSq(Own, SQ_C1)), compare);
            }

            // In endgame, king near to closest pawn
            int32_t minPawnDist{ 6 }; // Max can be 6 because pawn on last rank promotes
            Bitboard pawns{ pos.pieces(Own, PAWN) };
            if ((pawns & attacksBB<KING>(sq)) != 0) {
                minPawnDist = 1;
            } else {
                while (pawns != 0
                    && minPawnDist != 2) {
                    minPawnDist = std::min(minPawnDist, distance(sq, popLSq(pawns)));
                }
            }
            safety[Own] = shelter - makeScore(0, 16 * minPawnDist);

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

    Entry* probe(Position const &pos, Pawns::Entry *pe) {

        Key const kingKey{
            pe->key //pos.pawnKey()
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
                       - distance<Rank>(pos.square(W_KING), pos.square(B_KING));
        e->infiltration = sRank(pos.square(W_KING)) > RANK_4
                       || sRank(pos.square(B_KING)) < RANK_5;

        e->square[WHITE] =
        e->square[BLACK] = SQ_NONE;
        return e;
    }

}
