#include "Evaluator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

#include "BitBoard.h"
#include "Material.h"
#include "Notation.h"
#include "Option.h"
#include "Pawns.h"
#include "Thread.h"

using namespace std;
using namespace BitBoard;

namespace {

    enum Term : u08
    {
        // The first 6 entries are for PieceType
        MATERIAL = NONE,
        IMBALANCE,
        MOBILITY,
        THREAT,
        PASSER,
        SPACE,
        INITIATIVE,
        TOTAL,
    };

    array<array<Score, CLR_NO>, TOTAL + 1> Scores;

    void clear()
    {
        for (auto &s : Scores) { s.fill(SCORE_ZERO); }
    }

    void write(Term term, Color c, Score score)
    {
        Scores[term][c] = score;
    }
    void write(Term term, Score wScore, Score bScore = SCORE_ZERO)
    {
        write(term, WHITE, wScore);
        write(term, BLACK, bScore);
    }

    ostream& operator<<(ostream &os, Term term)
    {
        const auto &score = Scores[term];
        switch (term)
        {
        case Term::MATERIAL:
        case Term::IMBALANCE:
        case Term::INITIATIVE:
        case Term::TOTAL:
            os << " | ----- -----"
               << " | ----- -----";
            break;
        default:
            os << " | " << score[WHITE]
               << " | " << score[BLACK];
            break;
        }
        os << " | " << score[WHITE] - score[BLACK] << endl;
        return os;
    }


#define S(mg, eg) makeScore(mg, eg)

    constexpr array<array<Score, 28>, 4> Mobility
    {{
        { // Knight
            S(-62,-81), S(-53,-56), S(-12,-30), S( -4,-14), S(  3,  8), S( 13, 15),
            S( 22, 23), S( 28, 27), S( 33, 33)
        },
        { // Bishop
            S(-48,-59), S(-20,-23), S( 16, -3), S( 26, 13), S( 38, 24), S( 51, 42),
            S( 55, 54), S( 63, 57), S( 63, 65), S( 68, 73), S( 81, 78), S( 81, 86),
            S( 91, 88), S( 98, 97)
        },
        { // Rook
            S(-58,-76), S(-27,-18), S(-15, 28), S(-10, 55), S( -5, 69), S( -2, 82),
            S(  9,112), S( 16,118), S( 30,132), S( 29,142), S( 32,155), S( 38,165),
            S( 46,166), S( 48,169), S( 58,171)
        },
        { // Queen
            S(-39,-36), S(-21,-15), S(  3,  8), S(  3, 18), S( 14, 34), S( 22, 54),
            S( 28, 61), S( 41, 73), S( 43, 79), S( 48, 92), S( 56, 94), S( 60,104),
            S( 60,113), S( 66,120), S( 67,123), S( 70,126), S( 71,133), S( 73,136),
            S( 79,140), S( 88,143), S( 88,148), S( 99,166), S(102,170), S(102,175),
            S(106,184), S(109,191), S(113,206), S(116,212)
        }
    }};

    constexpr array<Score, 2> RookOnFile
    {
        S(21, 4), S(47,25)
    };

    constexpr array<Score, NONE> MinorThreat
    {
        S( 6,32), S(59,41), S(79,56), S(90,119), S(79,161), S( 0, 0)
    };
    constexpr array<Score, NONE> MajorThreat
    {
        S( 3,44), S(38,71), S(38,61), S( 0, 38), S(51, 38), S( 0, 0)
    };

    constexpr array<Score, R_NO> PasserRank
    {
        S( 0, 0), S(10,28), S(17,33), S(15,41), S(62,72), S(168,177), S(276,260), S( 0, 0)
    };

    constexpr Score MinorBehindPawn =    S( 18,  3);
    constexpr Score MinorOutpost =       S( 30, 21);
    constexpr Score KnightReachablepost =S( 32, 10);
    constexpr Score MinorKingProtect =   S(  7,  8);
    constexpr Score BishopOnDiagonal =   S( 45,  0);
    constexpr Score BishopPawns =        S(  3,  7);
    constexpr Score BishopTrapped =      S( 50, 50);
    constexpr Score RookOnQueenFile =    S(  7,  6);
    constexpr Score RookTrapped =        S( 52, 10);
    constexpr Score QueenWeaken =        S( 49, 15);
    constexpr Score PawnLessFlank =      S( 17, 95);
    constexpr Score PasserFile =         S( 11,  8);
    constexpr Score KingFlankAttacks =   S(  8,  0);
    constexpr Score PieceRestricted =    S(  7,  7);
    constexpr Score PieceHanged =        S( 69, 36);
    constexpr Score PawnThreat =         S(173, 94);
    constexpr Score PawnPushThreat =     S( 48, 39);
    constexpr Score KingThreat =         S( 24, 89);
    constexpr Score KnightOnQueen =      S( 16, 12);
    constexpr Score SliderOnQueen =      S( 59, 18);

#undef S

    // Threshold for lazy and space evaluation
    constexpr Value LazyThreshold = Value(1400);
    constexpr Value SpaceThreshold = Value(12222);

    constexpr array<i32, NONE> SafeCheckWeight
    {
        0, 790, 635, 1080, 780, 0
    };

    constexpr array<i32, NONE> KingAttackerWeight
    {
        0, 81, 52, 44, 10, 0
    };

    // Evaluator class contains various evaluation functions.
    template<bool Trace>
    class Evaluator
    {
    private:

        const Position &pos;

        Pawns::Entry *pe;
        Material::Entry *me;

        // Contains all squares attacked by the color and piece type.
        array<Bitboard              , CLR_NO> fulAttacks;

        // Contains all squares attacked by the color and piece type with pinned removed.
        array<array<Bitboard, PT_NO>, CLR_NO> sqlAttacks;

        // Contains all squares attacked by more than one pieces of a color, possibly via x-ray or by one pawn and one piece.
        array<Bitboard              , CLR_NO> pawnsDblAttacks;
        array<Bitboard              , CLR_NO> dblAttacks;

        // Contains all squares from which queen can be attacked
        array<array<Bitboard, 3>    , CLR_NO> queenAttacked;


        array<Bitboard              , CLR_NO> mobArea;
        array<Score                 , CLR_NO> mobility;

        // The squares adjacent to the king plus some other very near squares, depending on king position.
        array<Bitboard              , CLR_NO> kingRing;
        // Number of pieces of the color, which attack a square in the kingRing of the enemy king.
        array<i32                   , CLR_NO> kingAttackersCount;
        // Sum of the "weight" of the pieces of the color which attack a square in the kingRing of the enemy king.
        // The weights of the individual piece types are given by the KingAttackerWeight[piece-type]
        array<i32                   , CLR_NO> kingAttackersWeight;
        // Number of attacks by the color to squares directly adjacent to the enemy king.
        // Pieces which attack more than one square are counted multiple times.
        // For instance, if there is a white knight on g5 and black's king is on g8, this white knight adds 2 to kingAttacksCount[WHITE]
        array<i32                   , CLR_NO> kingAttacksCount;

        template<Color> void initAttacks();
        template<Color> void initMobility();
        template<Color, PieceType> Score pieces();
        template<Color> Score king() const;
        template<Color> Score threats() const;
        template<Color> Score passers() const;
        template<Color> Score space() const;

        Score initiative(Score) const;
        Scale scale(Value) const;

    public:
        Evaluator() = delete;
        Evaluator(const Evaluator&) = delete;
        Evaluator& operator=(const Evaluator&) = delete;

        Evaluator(const Position &p)
            : pos(p)
        {}

        Value value();
    };

    /// initAttacks() computes pawn and king attacks.
    template<bool Trace> template<Color Own>
    void Evaluator<Trace>::initAttacks()
    {
        Bitboard pawns = pos.pieces(Own, PAWN);

        auto kSq = pos.square(Own|KING);

        sqlAttacks[Own].fill(0);
        sqlAttacks[Own][PAWN] =  pawnSglAttacks(Own, pawns & ~pos.si->kingBlockers[Own])
                              | (pawnSglAttacks(Own, pawns &  pos.si->kingBlockers[Own]) & PieceAttacks[BSHP][kSq]);
        sqlAttacks[Own][KING] = PieceAttacks[KING][kSq];
        sqlAttacks[Own][NONE] = sqlAttacks[Own][PAWN]
                              | sqlAttacks[Own][KING];
        //
        fulAttacks[Own] = pawnSglAttacks(Own, pawns)
                        | sqlAttacks[Own][KING];
        //
        pawnsDblAttacks[Own] = pawnDblAttacks(Own, pawns)
                             & sqlAttacks[Own][PAWN];
        dblAttacks[Own] = pawnsDblAttacks[Own]
                        | (  sqlAttacks[Own][PAWN]
                           & sqlAttacks[Own][KING]);
        //
        queenAttacked[Own].fill(0);
    }
    /// initMobility() computes mobility and king-ring.
    template<bool Trace> template<Color Own>
    void Evaluator<Trace>::initMobility()
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        // Mobility area: Exclude followings
        mobArea[Own] = ~(  // Squares protected by enemy pawns
                           sqlAttacks[Opp][PAWN]
                            // Squares occupied by friend Queen and King
                         | pos.pieces(Own, QUEN, KING)
                            // Squares occupied by friend King blockers
                         | (pos.si->kingBlockers[Own] /*& pos.pieces(Own)*/)
                            // Squares occupied by block pawns (pawns on ranks 2-3/blocked)
                         | (  pos.pieces(Own, PAWN)
                            & (  LowRanks[Own]
                               | pawnSglPushes(Opp, pos.pieces()))));
        mobility[Own] = SCORE_ZERO;

        auto kSq = pos.square(Own|KING);
        // King safety tables
        auto sq = makeSquare(clamp(sFile(kSq), F_B, F_G),
                             clamp(sRank(kSq), R_2, R_7));
        kingRing[Own] = PieceAttacks[KING][sq] | sq;

        kingAttackersCount [Opp] = popCount(  kingRing[Own]
                                            & sqlAttacks[Opp][PAWN]);
        kingAttackersWeight[Opp] = 0;
        kingAttacksCount   [Opp] = 0;

        // Remove from kingRing the squares defended by two pawns
        kingRing[Own] &= ~pawnsDblAttacks[Own];
    }

    /// pieces() evaluates the pieces of the color and type.
    template<bool Trace> template<Color Own, PieceType PT>
    Score Evaluator<Trace>::pieces()
    {
        static_assert (NIHT <= PT && PT <= QUEN, "PT incorrect");

        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        Score score = SCORE_ZERO;

        for (auto s : pos.squares[Own|PT])
        {
            assert((Own|PT) == pos[s]);

            fulAttacks[Own] |= pos.attacksFrom(PT, s);
            // Find attacked squares, including x-ray attacks for Bishops, Rooks and Queens
            Bitboard attacks = pos.xattacksFrom<PT>(s, Own);
            if (contains(pos.si->kingBlockers[Own], s))
            {
                attacks &= lines(pos.square(Own|KING), s);
            }

            switch (PT)
            {
            case BSHP:
            {
                Bitboard att =  attacks
                             &  pos.pieces(Own)
                             & ~pos.si->kingBlockers[Own];
                dblAttacks[Own] |= sqlAttacks[Own][NONE]
                                 & (  attacks
                                    | (  pawnSglAttacks(Own, att & pos.pieces(PAWN) & frontRanks(Own, s))
                                       & PieceAttacks[BSHP][s]));
            }
                break;
            case QUEN:
            {
                Bitboard att =  attacks
                             &  pos.pieces(Own)
                             & ~pos.si->kingBlockers[Own];
                dblAttacks[Own] |= sqlAttacks[Own][NONE]
                                 & (  attacks
                                    | (  pawnSglAttacks(Own, att & pos.pieces(PAWN) & frontRanks(Own, s))
                                       & PieceAttacks[BSHP][s])
                                    | attacksBB<BSHP>(s, pos.pieces() ^ (att & pos.pieces(BSHP) & PieceAttacks[BSHP][s]))
                                    | attacksBB<ROOK>(s, pos.pieces() ^ (att & pos.pieces(ROOK) & PieceAttacks[ROOK][s])));
            }
                break;
            default:
                dblAttacks[Own] |= sqlAttacks[Own][NONE]
                                 & attacks;
                break;
            }

            sqlAttacks[Own][PT]   |= attacks;
            sqlAttacks[Own][NONE] |= attacks;

            if ((  attacks
                 & kingRing[Opp]) != 0)
            {
                kingAttackersCount [Own]++;
                kingAttackersWeight[Own] += KingAttackerWeight[PT];
                kingAttacksCount   [Own] += popCount(  attacks
                                                     & sqlAttacks[Opp][KING]);
            }

            auto mob = popCount(attacks & mobArea[Own]);
            assert(0 <= mob && mob <= 27);

            // Bonus for piece mobility
            mobility[Own] += Mobility[PT - NIHT][mob];

            Bitboard b;
            // Special extra evaluation for pieces
            switch (PT)
            {
            case NIHT:
            case BSHP:
            {
                // Bonus for minor behind a pawn
                score += MinorBehindPawn * contains(pawnSglPushes(Opp, pos.pieces(PAWN)), s);

                // Penalty for distance from the friend king
                score -= MinorKingProtect * dist(s, pos.square(Own|KING));

                b = Outposts[Own]
                  & ~pe->attackSpan[Opp]
                  & sqlAttacks[Own][PAWN];

                if (NIHT == PT)
                {
                    // Bonus for knight outpost squares
                    if (contains(b, s))
                    {
                        score += MinorOutpost * 2;
                    }
                    else
                    if (0 != (b & attacks & ~pos.pieces(Own)))
                    {
                        score += KnightReachablepost;
                    }
                }
                else
                if (BSHP == PT)
                {
                    // Bonus for bishop outpost squares
                    if (contains(b, s))
                    {
                        score += MinorOutpost * 1;
                    }

                    // Penalty for pawns on the same color square as the bishop,
                    // more when the center files are blocked with pawns.
                    b = pos.pieces(Own, PAWN)
                      & Sides[CS_NO]
                      & pawnSglPushes(Opp, pos.pieces());
                    score -= BishopPawns
                           * (1 + popCount(b))
                           * popCount(pos.pieces(Own, PAWN) & Colors[sColor(s)]);

                    // Bonus for bishop on a long diagonal which can "see" both center squares
                    score += BishopOnDiagonal * moreThanOne(attacksBB<BSHP>(s, pos.pieces(PAWN)) & CenterBB);

                    if (bool(Options["UCI_Chess960"]))
                    {
                        // An important Chess960 pattern: A cornered bishop blocked by a friend pawn diagonally in front of it.
                        // It is a very serious problem, especially when that pawn is also blocked.
                        // Bishop (white or black) on a1/h1 or a8/h8 which is trapped by own pawn on b2/g2 or b7/g7.
                        if (   1 >= mob
                            && contains(FABB|FHBB, s)
                            && R_1 == relRank(Own, s))
                        {
                            auto del = pawnPush(Own) + (DEL_W + DEL_EE * i32(F_A == sFile(s)));
                            if (contains(pos.pieces(Own, PAWN), s + del))
                            {
                                score -= BishopTrapped
                                       * (!contains(pos.pieces(), s + del + pawnPush(Own)) ?
                                              !contains(pos.pieces(Own, PAWN), s + del + del) ?
                                                  1 : 2 : 4);
                            }
                        }
                    }
                }
            }
                break;
            case ROOK:
            {
                // Bonus for rook on the same file as a queen
                if (fileBB(s) & pos.pieces(QUEN))
                {
                    score += RookOnQueenFile;
                }

                // Bonus for rook when on an open or semi-open file
                if (pos.semiopenFileOn(Own, s))
                {
                    score += RookOnFile[pos.semiopenFileOn(Opp, s)];
                }
                else
                // Penalty for rook when trapped by the king, even more if the king can't castle
                if (   3 >= mob
                    && R_5 > relRank(Own, s))
                {
                    auto kF = sFile(pos.square(Own|KING));
                    if ((kF < F_E) == (sFile(s) < kF))
                    {
                        score -= RookTrapped * (1 + (CR_NONE == pos.castleRight(Own)));
                    }
                }
            }
                break;
            case QUEN:
            {
                queenAttacked[Own][0] |= pos.attacksFrom(NIHT, s);
                queenAttacked[Own][1] |= pos.attacksFrom(BSHP, s);
                queenAttacked[Own][2] |= pos.attacksFrom(ROOK, s);

                // Penalty for pin or discover attack on the queen
                b = 0; // Queen attackers
                if ((   pos.sliderBlockersAt(s, pos.pieces(Opp, BSHP, ROOK), b, b)
                     & ~pos.si->kingBlockers[Opp]
                     & ~(   pos.pieces(Opp, PAWN)
                         &  fileBB(s)
                         & ~pawnSglAttacks(Own, pos.pieces(Own)))) != 0)
                {
                    score -= QueenWeaken;
                }
            }
                break;
            }
        }

        if (Trace)
        {
            write(Term (PT), Own, score);
        }

        return score;
    }

    /// king() evaluates the king of the color.
    template<bool Trace> template<Color Own>
    Score Evaluator<Trace>::king() const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        auto kSq = pos.square(Own|KING);

        // Main king safety evaluation
        i32 kingDanger = 0;

        // Attacked squares defended at most once by friend queen or king
        Bitboard weakArea =  sqlAttacks[Opp][NONE]
                          & ~dblAttacks[Own]
                          & (  ~sqlAttacks[Own][NONE]
                             |  sqlAttacks[Own][QUEN]
                             |  sqlAttacks[Own][KING]);

        // Safe squares where enemy's safe checks are possible on next move
        Bitboard safeArea = ~pos.pieces(Opp)
                          & (  ~sqlAttacks[Own][NONE]
                             | (  weakArea
                                & dblAttacks[Opp]));

        Bitboard unsafeCheck = 0;

        Bitboard rookPins = attacksBB<ROOK>(kSq, pos.pieces() ^ pos.pieces(Own, QUEN));
        Bitboard bshpPins = attacksBB<BSHP>(kSq, pos.pieces() ^ pos.pieces(Own, QUEN));

        // Enemy rooks checks
        Bitboard rookSafeChecks =  rookPins
                                &  sqlAttacks[Opp][ROOK]
                                &  safeArea;
        if (0 != rookSafeChecks)
        {
            kingDanger += SafeCheckWeight[ROOK];
        }
        else
        {
            unsafeCheck |= rookPins
                         & sqlAttacks[Opp][ROOK];
        }

        // Enemy queens checks
        Bitboard quenSafeChecks = (rookPins | bshpPins)
                                &  sqlAttacks[Opp][QUEN]
                                &  safeArea
                                & ~sqlAttacks[Own][QUEN]
                                & ~rookSafeChecks;
        if (0 != quenSafeChecks)
        {
            kingDanger += SafeCheckWeight[QUEN];
        }

        // Enemy bishops checks
        Bitboard bshpSafeChecks =  bshpPins
                                &  sqlAttacks[Opp][BSHP]
                                &  safeArea
                                & ~quenSafeChecks;
        if (0!= bshpSafeChecks)
        {
            kingDanger += SafeCheckWeight[BSHP];
        }
        else
        {
            unsafeCheck |= bshpPins
                         & sqlAttacks[Opp][BSHP];
        }

        // Enemy knights checks
        Bitboard nihtChecks =  PieceAttacks[NIHT][kSq]
                            &  sqlAttacks[Opp][NIHT];
        if (0 != (nihtChecks & safeArea))
        {
            kingDanger += SafeCheckWeight[NIHT];
        }
        else
        {
            unsafeCheck |= nihtChecks;
        }

        Bitboard b;

        b = KingFlanks[sFile(kSq)]
          & Camps[Own]
          & sqlAttacks[Opp][NONE];
        // Friend king flank attack count
        i32 kfAttacks = popCount(b)                    // Squares attacked by enemy in friend king flank
                       + popCount(b & dblAttacks[Opp]);// Squares attacked by enemy twice in friend king flank.
        // Friend king flank defense count
        b = KingFlanks[sFile(kSq)]
          & Camps[Own]
          & sqlAttacks[Own][NONE];
        i32 kfDefense = popCount(b);

        // King Safety:
        Score score = pe->evaluateKingSafety<Own>(pos, fulAttacks[Opp]);

        kingDanger +=   1 * (kingAttackersCount[Opp] * kingAttackersWeight[Opp])
                    +  69 * kingAttacksCount[Opp]
                    + 185 * popCount(kingRing[Own] & weakArea)
                    + 148 * popCount(unsafeCheck)
                    +  98 * popCount(pos.si->kingBlockers[Own])
                    +   3 * kfAttacks * kfAttacks / 8
                    // Enemy queen is gone
                    - 873 * (0 == pos.pieces(Opp, QUEN))
                    // Friend knight is near by to defend king
                    - 100 * (0 != (   sqlAttacks[Own][NIHT]
                                   & (sqlAttacks[Own][KING] | kSq)))
                    // Mobility
                    -   1 * i32(mgValue(mobility[Own] - mobility[Opp]))
                    -   4 * kfDefense
                    // Pawn Safety quality
                    -   3 * i32(mgValue(score)) / 4
                    +  37;

        // Transform the king danger into a score
        if (100 < kingDanger)
        {
            score -= makeScore(kingDanger * kingDanger / 0x1000, kingDanger / 0x10);
        }

        // Penalty for king on a pawn less flank
        score -= PawnLessFlank * (0 == (pos.pieces(PAWN) & KingFlanks[sFile(kSq)]));

        // King tropism: Penalty for slow motion attacks moving towards friend king zone
        score -= KingFlankAttacks * kfAttacks;

        if (Trace)
        {
            write(Term (KING), Own, score);
        }

        return score;
    }

    /// threats() evaluates the threats of the color.
    template<bool Trace> template<Color Own>
    Score Evaluator<Trace>::threats() const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        Score score = SCORE_ZERO;

        // Enemy non-pawns
        Bitboard nonPawnsEnemies =  pos.pieces(Opp)
                                 & ~pos.pieces(PAWN);
        // Squares defended by the opponent,
        // - attack the square with a pawn
        // - attack the square twice and not defended twice.
        Bitboard defendedArea = sqlAttacks[Opp][PAWN]
                              | (   dblAttacks[Opp]
                                 & ~dblAttacks[Own]);
        // Enemy not defended and under attacked by any friend piece
        Bitboard attackedUndefendedEnemies =  pos.pieces(Opp)
                                           & ~defendedArea
                                           &  sqlAttacks[Own][NONE];
        // Non-pawn enemies, defended by enemies
        Bitboard defendedNonPawnsEnemies = nonPawnsEnemies
                                         & defendedArea;

        Bitboard b;

        if (0 != (  attackedUndefendedEnemies
                  | defendedNonPawnsEnemies))
        {
            // Bonus according to the type of attacking pieces

            // Enemies attacked by minors
            b = (  attackedUndefendedEnemies
                 | defendedNonPawnsEnemies)
              & (  sqlAttacks[Own][NIHT]
                 | sqlAttacks[Own][BSHP]);
            while (0 != b)
            {
                score += MinorThreat[pType(pos[popLSq(b)])];
            }

            if (0 != attackedUndefendedEnemies)
            {
                // Enemies attacked by majors
                b =  attackedUndefendedEnemies
                  &  sqlAttacks[Own][ROOK];
                while (0 != b)
                {
                    score += MajorThreat[pType(pos[popLSq(b)])];
                }

                // Enemies attacked by king
                b =  attackedUndefendedEnemies
                  &  sqlAttacks[Own][KING];
                if (0 != b)
                {
                    score += KingThreat;
                }

                // Enemies attacked are hanging
                b = attackedUndefendedEnemies
                  & (  ~sqlAttacks[Opp][NONE]
                     | (  nonPawnsEnemies
                        & dblAttacks[Own]));
                score += PieceHanged * popCount(b);
            }
        }

        // Bonus for restricting their piece moves
        b = ~defendedArea
          &  sqlAttacks[Opp][NONE]
          &  sqlAttacks[Own][NONE];
        score += PieceRestricted * popCount(b);

        Bitboard safeArea;

        // Defended or Unattacked squares
        safeArea =  sqlAttacks[Own][NONE]
                 | ~sqlAttacks[Opp][NONE];
        // Safe friend pawns
        b =  safeArea
          &  pos.pieces(Own, PAWN);
        // Safe friend pawns attacks on non-pawn enemies
        b =  nonPawnsEnemies
          &  pawnSglAttacks(Own, b)
          &  sqlAttacks[Own][PAWN];
        score += PawnThreat * popCount(b);

        // Friend pawns who can push on the next move
        b =  pos.pieces(Own, PAWN)
          & ~pos.si->kingBlockers[Own];
        // Friend pawns push (squares where friend pawns can push on the next move)
        b =  pawnSglPushes(Own, b)
          & ~pos.pieces();
        b |= pawnSglPushes(Own, b & rankBB(relRank(Own, R_3)))
          & ~pos.pieces();
        // Friend pawns push safe (only the squares which are relatively safe)
        b &= safeArea
          & ~sqlAttacks[Opp][PAWN];
        // Friend pawns push safe attacks an enemies
        b =  nonPawnsEnemies
          &  pawnSglAttacks(Own, b);
        score += PawnPushThreat * popCount(b);

        // Bonus for threats on the next moves against enemy queens
        if (0 != pos.pieces(Opp, QUEN))
        {
            safeArea =  mobArea[Own]
                     & ~defendedArea;
            b = safeArea
              & (sqlAttacks[Own][NIHT] & queenAttacked[Opp][0]);
            score += KnightOnQueen * popCount(b);

            b = safeArea
              & (  (sqlAttacks[Own][BSHP] & queenAttacked[Opp][1])
                 | (sqlAttacks[Own][ROOK] & queenAttacked[Opp][2]))
              & dblAttacks[Own];
            score += SliderOnQueen * popCount(b);
        }

        if (Trace)
        {
            write(Term::THREAT, Own, score);
        }

        return score;
    }

    /// passers() evaluates the passed pawns of the color.
    template<bool Trace> template<Color Own>
    Score Evaluator<Trace>::passers() const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        auto kingProximity = [&](Color c, Square s) { return std::min(dist(pos.square(c|KING), s), 5); };

        Score score = SCORE_ZERO;

        Bitboard psr = pe->passers[Own];
        while (0 != psr)
        {
            auto s = popLSq(psr);
            assert(0 == (  (  pawnSglPushes(Own, frontSquares(Own, s))
                            | (   pawnPassSpan(Own, s + pawnPush(Own))
                               & ~PawnAttacks[Own][s + pawnPush(Own)]))
                         & pos.pieces(Opp, PAWN)));

            i32 r = relRank(Own, s);
            // Base bonus depending on rank.
            Score bonus = PasserRank[r];

            auto pushSq = s + pawnPush(Own);

            if (R_3 < r)
            {
                i32 w = 5*r - 13;

                // Adjust bonus based on the king's proximity
                bonus += makeScore(0, i32(+4.75*w*kingProximity(Opp, pushSq)
                                          -2.00*w*kingProximity(Own, pushSq)));
                // If block square is not the queening square then consider also a second push.
                if (R_7 != r)
                {
                    bonus += makeScore(0, -1*w*kingProximity(Own, pushSq + pawnPush(Own)));
                }

                // If the pawn is free to advance.
                if (pos.empty(pushSq))
                {
                    Bitboard attackedSquares = pawnPassSpan(Own, s);

                    Bitboard behindMajors = frontSquares(Opp, s)
                                          & pos.pieces(ROOK, QUEN);
                    if (0 == (pos.pieces(Opp) & behindMajors))
                    {
                        attackedSquares &= sqlAttacks[Opp][NONE];
                    }

                    // Bonus according to attacked squares.
                    i32 k = 0 == attackedSquares                          ? 35 :
                            0 == (attackedSquares & frontSquares(Own, s)) ? 20 :
                            !contains(attackedSquares, pushSq)            ?  9 : 0;

                    // Bonus according to defended squares.
                    if (   0 != (pos.pieces(Own) & behindMajors)
                        || contains(sqlAttacks[Own][NONE], pushSq))
                    {
                        k += 5;
                    }

                    bonus += makeScore(k*w, k*w);
                }
            }

            // Scale down bonus for candidate passers
            // - have a pawn in front of it.
            // - need more than one pawn push to become passer.
            if (   contains(pos.pieces(PAWN), pushSq)
                || !pos.pawnPassedAt(Own, pushSq))
            {
                bonus /= 2;
            }

            score += bonus
                   - PasserFile * mapFile(sFile(s));
        }

        if (Trace)
        {
            write(Term::PASSER, Own, score);
        }

        return score;
    }

    /// space() evaluates the space of the color.
    /// The space evaluation is a simple bonus based on the number of safe squares
    /// available for minor pieces on the central four files on ranks 2-4
    /// Safe squares one, two or three squares behind a friend pawn are counted twice
    /// The aim is to improve play on opening
    template<bool Trace> template<Color Own>
    Score Evaluator<Trace>::space() const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;
        // Space Threshold
        if (pos.nonPawnMaterial() < SpaceThreshold)
        {
            return SCORE_ZERO;
        }

        // Find all squares which are at most three squares behind some friend pawn
        Bitboard behind = pos.pieces(Own, PAWN);
        behind |= pawnSglPushes(Opp, behind);
        behind |= pawnDblPushes(Opp, behind);

        // Safe squares for friend pieces inside the area defined by SpaceMask.
        Bitboard safeSpace = Regions[Own]
                           & Sides[CS_NO]
                           & ~pos.pieces(Own, PAWN)
                           & ~sqlAttacks[Opp][PAWN];

        i32 bonus = popCount(safeSpace)
                  + popCount(  behind
                             & safeSpace
                             & ~sqlAttacks[Opp][NONE]);
        i32 weight = pos.count(Own) - 1;
        Score score = makeScore(bonus * weight * weight / 16, 0);

        if (Trace)
        {
            write(Term::SPACE, Own, score);
        }

        return score;
    }

    /// initiative() evaluates the initiative correction value for the position
    /// i.e. second order bonus/malus based on the known attacking/defending status of the players
    template<bool Trace>
    Score Evaluator<Trace>::initiative(Score s) const
    {
        i32 outflanking = dist<File>(pos.square(WHITE|KING), pos.square(BLACK|KING))
                        - dist<Rank>(pos.square(WHITE|KING), pos.square(BLACK|KING));
        // Compute the initiative bonus for the attacking side
        i32 complexity = 11 * pos.count(PAWN)
                       +  9 * pe->passedCount()
                       +  9 * outflanking
                        // King infiltration
                       + 24 * (   sRank(pos.square(WHITE|KING)) > R_4
                               || sRank(pos.square(BLACK|KING)) < R_5)
                       + 51 * (VALUE_ZERO == pos.nonPawnMaterial())
                       - 110;

        // Pawn on both flanks
        if (   0 != (pos.pieces(PAWN) & Sides[CS_KING])
            && 0 != (pos.pieces(PAWN) & Sides[CS_QUEN]))
        {
            complexity += 21;
        }
        else
        // Almost Unwinnable
        if (   0 > outflanking
            && 0 == pe->passedCount())
        {
            complexity -= 43;
        }

        auto mg = mgValue(s);
        auto eg = egValue(s);
        // Now apply the bonus: note that we find the attacking side by extracting the
        // sign of the midgame or endgame values, and that we carefully cap the bonus
        // so that the midgame and endgame scores do not change sign after the bonus.
        Score score = makeScore(sign(mg) * clamp(complexity + 50, -abs(mg), 0),
                                sign(eg) * std::max(complexity  , -abs(eg)));

        if (Trace)
        {
            write(Term::INITIATIVE, score);
        }

        return score;
    }

    /// scale() evaluates the scale for the position
    template<bool Trace>
    Scale Evaluator<Trace>::scale(Value eg) const
    {
        auto stngColor = eg >= VALUE_ZERO ? WHITE : BLACK;

        auto scl = nullptr != me->scalingFunc[stngColor] ?
                    (*me->scalingFunc[stngColor])(pos) :
                    SCALE_NONE;
        if (SCALE_NONE == scl)
        {
            scl = me->scale[stngColor];
        }
        assert(SCALE_NONE != scl);

        // If don't already have an unusual scale, check for certain types of endgames.
        if (SCALE_NORMAL == scl)
        {
            bool oppositeBishop = 1 == pos.count(WHITE|BSHP)
                               && 1 == pos.count(BLACK|BSHP)
                               && oppositeColor(pos.square(WHITE|BSHP), pos.square(BLACK|BSHP));
            scl = oppositeBishop
               && 2 * VALUE_MG_BSHP == pos.nonPawnMaterial() ?
                    // Endings with opposite-colored bishops and no other pieces is almost a draw
                    Scale(22) :
                    std::min(Scale(36 + (7 - 5 * oppositeBishop) * pos.count(stngColor|PAWN)), SCALE_NORMAL);

            // Scale down endgame factor when shuffling
            scl = std::max(Scale(scl - std::max(pos.si->clockPly / 4 - 3, 0)), SCALE_DRAW);
        }
        return scl;
    }

    /// value() computes the various parts of the evaluation and
    /// returns the value of the position from the point of view of the side to move.
    template<bool Trace>
    Value Evaluator<Trace>::value()
    {
        assert(0 == pos.si->checkers);

        // Probe the material hash table
        me = Material::probe(pos);
        // If have a specialized evaluation function for the material configuration
        if (nullptr != me->evaluationFunc)
        {
            return (*me->evaluationFunc)(pos);
        }

        // Probe the pawn hash table
        pe = Pawns::probe(pos);

        // Score is computed internally from the white point of view, initialize by
        // - incrementally updated scores (material + piece square tables).
        // - material imbalance.
        // - pawn score
        Score score = pos.psq
                    + me->imbalance
                    + (pe->score[WHITE] - pe->score[BLACK])
                    + pos.thread->contempt;

        // Lazy Threshold
        // Early exit if score is high
        Value v = (mgValue(score) + egValue(score)) / 2;
        if (abs(v) > LazyThreshold + pos.nonPawnMaterial() / 64)
        {
            return WHITE == pos.active ? +v : -v;
        }

        if (Trace)
        {
            clear();
        }

        initAttacks <WHITE>(), initAttacks <BLACK>();
        initMobility<WHITE>(), initMobility<BLACK>();

        // Pieces should be evaluated first (populate attack information)
        score += pieces<WHITE, NIHT>() - pieces<BLACK, NIHT>();
        score += pieces<WHITE, BSHP>() - pieces<BLACK, BSHP>();
        score += pieces<WHITE, ROOK>() - pieces<BLACK, ROOK>();
        score += pieces<WHITE, QUEN>() - pieces<BLACK, QUEN>();

        assert((sqlAttacks[WHITE][NONE] & dblAttacks[WHITE]) == dblAttacks[WHITE]);
        assert((sqlAttacks[BLACK][NONE] & dblAttacks[BLACK]) == dblAttacks[BLACK]);

        score += mobility[WHITE]  - mobility[BLACK]
               + king   <WHITE>() - king   <BLACK>()
               + threats<WHITE>() - threats<BLACK>()
               + passers<WHITE>() - passers<BLACK>()
               + space  <WHITE>() - space  <BLACK>();

        score += initiative(score);

        assert(-VALUE_INFINITE < mgValue(score) && mgValue(score) < +VALUE_INFINITE);
        assert(-VALUE_INFINITE < egValue(score) && egValue(score) < +VALUE_INFINITE);
        assert(0 <= me->phase && me->phase <= Material::PhaseResolution);

        // Interpolates between midgame and scaled endgame values.
        v = mgValue(score) * (me->phase)
          + egValue(score) * (Material::PhaseResolution - me->phase) * scale(egValue(score)) / SCALE_NORMAL;
        v /= Material::PhaseResolution;

        if (Trace)
        {
            // Write remaining evaluation terms
            write(Term (PAWN)    , pe->score[WHITE], pe->score[BLACK]);
            write(Term::MATERIAL , pos.psq);
            write(Term::IMBALANCE, me->imbalance);
            write(Term::MOBILITY , mobility[WHITE], mobility[BLACK]);
            write(Term::TOTAL    , score);
        }

        // Active side's point of view
        return (WHITE == pos.active ? +v : -v) + Tempo;
    }
}

/// evaluate() returns a static evaluation of the position from the point of view of the side to move.
Value evaluate(const Position &pos)
{
    return Evaluator<false>(pos).value();
}

/// trace() returns a string(suitable for outputting to stdout) that contains
/// the detailed descriptions and values of each evaluation term.
string trace(const Position &pos)
{
    if (0 != pos.si->checkers)
    {
        return "Total evaluation: none (in check)";
    }

    pos.thread->contempt = SCORE_ZERO; // Reset any dynamic contempt
    auto value = Evaluator<true>(pos).value();
    // Trace scores are from White's point of view
    value = WHITE == pos.active ? +value : -value;

    ostringstream oss;

    oss << setprecision(2) << fixed
        << "      Eval Term |    White    |    Black    |    Total     \n"
        << "                |   MG    EG  |   MG    EG  |   MG    EG   \n"
        << "----------------+-------------+-------------+--------------\n"
        << "       Material" << Term::MATERIAL
        << "      Imbalance" << Term::IMBALANCE
        << "           Pawn" << Term (PAWN)
        << "         Knight" << Term (NIHT)
        << "         Bishop" << Term (BSHP)
        << "           Rook" << Term (ROOK)
        << "          Queen" << Term (QUEN)
        << "       Mobility" << Term::MOBILITY
        << "           King" << Term (KING)
        << "         Threat" << Term::THREAT
        << "         Passer" << Term::PASSER
        << "          Space" << Term::SPACE
        << "     Initiative" << Term::INITIATIVE
        << "----------------+-------------+-------------+--------------\n"
        << "          Total" << Term::TOTAL
        << endl
        << showpos << showpoint
        << "Evaluation: " << valueCP(value) / 100.0 << " (white side)\n"
        << noshowpoint << noshowpos;

    return oss.str();
}
